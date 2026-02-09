#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <map>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <fstream>
#include <sstream>
#include <chrono>

using namespace backtest;

// ============================================================================
// Shared tick data - loaded ONCE, used by ALL threads
// ============================================================================
std::vector<Tick> g_shared_ticks;

void LoadTickDataOnce(const std::string& path) {
    std::cout << "Loading tick data into memory (one-time)..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open tick file: " + path);
    }

    std::string line;
    std::getline(file, line);  // Skip header

    g_shared_ticks.reserve(52000000);  // Pre-allocate for ~52M ticks

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        // Parse MT5 TSV (tab-separated): Time  Bid  Ask  Last  Volume  Flags
        Tick tick;
        std::stringstream ss(line);
        std::string datetime_str, bid_str, ask_str;

        std::getline(ss, datetime_str, '\t');
        std::getline(ss, bid_str, '\t');
        std::getline(ss, ask_str, '\t');

        // Tick struct uses timestamp as string directly
        tick.timestamp = datetime_str;
        tick.bid = std::stod(bid_str);
        tick.ask = std::stod(ask_str);
        tick.volume = 0;

        g_shared_ticks.push_back(tick);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

    std::cout << "Loaded " << g_shared_ticks.size() << " ticks in "
              << duration.count() << " seconds" << std::endl;
    std::cout << "Memory usage: ~" << (g_shared_ticks.size() * sizeof(Tick) / 1024 / 1024)
              << " MB" << std::endl << std::endl;
}

// ============================================================================
// Strategy (unchanged)
// ============================================================================
struct Position {
    double entry_price;
    double lot_size;
    double highest_price;
    double trail_stop;
    bool trailing_active;
};

class TrailingStrategy {
public:
    double spacing_;           // Grid spacing (price $)
    double min_profit_pts_;    // Min price rise to activate trailing (price $)
    double trail_dist_;        // Trailing stop distance (price $)
    double update_thresh_;     // Update trail threshold (price $)
    int max_pos_;
    double survive_pct_;       // How far price can drop before margin call (for sizing)
    double stop_opening_pct_;  // Stop opening new positions after this % drop from peak
    double contract_size_ = 100.0;  // XAUUSD contract size
    std::map<int, Position> positions_;
    double lowest_buy_ = 999999, current_bid_ = 0, current_ask_ = 0;
    double price_peak_ = 0;    // Track highest price seen
    int next_ticket_ = 1, broker_updates_ = 0;

    // All parameters are now in PRICE terms (not dollar profit)
    TrailingStrategy(double spacing, double min_profit_pts, double trail_dist, double update_thresh, int max_pos,
                     double survive_pct = 13.0, double stop_opening_pct = 8.0)
        : spacing_(spacing), min_profit_pts_(min_profit_pts), trail_dist_(trail_dist), update_thresh_(update_thresh),
          max_pos_(max_pos), survive_pct_(survive_pct), stop_opening_pct_(stop_opening_pct) {}

    // Calculate lot size using survive_pct concept
    // This ensures account can survive a price drop of survive_pct%
    double CalculateLotSize(double equity, double price) {
        // How far price can drop before we want to survive
        double price_drop = price * survive_pct_ / 100.0;

        // How many positions we'll accumulate during this drop
        double num_positions = std::min((double)max_pos_, price_drop / spacing_);

        // Average loss per lot across grid (positions are spread, so average is half the drop)
        // For a grid from price to (price - price_drop), average entry is at (price - price_drop/2)
        // So average unrealized loss = price_drop/2 per unit price
        double avg_loss_per_lot = (price_drop / 2.0) * contract_size_;

        // Total risk = num_positions * avg_loss_per_lot * lot_size
        // We want: total_risk = equity (i.e., survive with 0 equity at worst case)
        // Actually, let's target 80% of equity at risk to leave some margin buffer
        double target_risk = equity * 0.80;

        // Solve for lot_size: lot_size = target_risk / (num_positions * avg_loss_per_lot)
        double lot = target_risk / (num_positions * avg_loss_per_lot);

        // Clamp to reasonable range
        return std::min(10.0, std::max(0.01, lot));
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_bid_ = tick.bid;
        current_ask_ = tick.ask;

        // Track price peak for stop_opening_pct calculation
        if (current_bid_ > price_peak_) {
            price_peak_ = current_bid_;
        }

        std::vector<int> to_close;
        lowest_buy_ = 999999;

        for (auto& [ticket, pos] : positions_) {
            // Use price move (not dollar profit) for consistent behavior across lot sizes
            double price_move = current_bid_ - pos.entry_price;

            if (!pos.trailing_active && price_move >= min_profit_pts_) {
                pos.trailing_active = true;
                pos.highest_price = current_bid_;
                pos.trail_stop = current_bid_ - trail_dist_;
                broker_updates_++;
            }

            if (pos.trailing_active) {
                if (current_bid_ >= pos.highest_price + update_thresh_) {
                    pos.highest_price = current_bid_;
                    pos.trail_stop = current_bid_ - trail_dist_;
                    broker_updates_++;
                }
                if (current_bid_ <= pos.trail_stop) {
                    to_close.push_back(ticket);
                    continue;
                }
            }
            lowest_buy_ = std::min(lowest_buy_, pos.entry_price);
        }

        for (int ticket : to_close) {
            for (const Trade* trade : engine.GetOpenPositions()) {
                if (std::abs(trade->entry_price - positions_[ticket].entry_price) < 0.01) {
                    engine.ClosePosition(const_cast<Trade*>(trade));
                    break;
                }
            }
            positions_.erase(ticket);
        }

        int pos_count = positions_.size();
        if (pos_count >= max_pos_) return;

        // Check if price has dropped too far from peak - stop opening new positions
        double drop_from_peak_pct = (price_peak_ - current_bid_) / price_peak_ * 100.0;
        if (drop_from_peak_pct >= stop_opening_pct_) {
            return;  // Don't open new positions - preserve safety buffer
        }

        bool should_open = (pos_count == 0) || (current_ask_ <= lowest_buy_ - spacing_);
        if (should_open) {
            double equity = engine.GetEquity();
            double lot = CalculateLotSize(equity, current_ask_);
            Trade* trade = engine.OpenMarketOrder("BUY", lot, 0.0, 0.0);
            if (trade) {
                Position pos = {trade->entry_price, trade->lot_size, current_bid_, 0, false};
                positions_[next_ticket_++] = pos;
                broker_updates_++;
            }
        }
    }
    int GetUpdates() const { return broker_updates_; }
};

// ============================================================================
// Task structures
// ============================================================================
struct Result {
    int max_pos;
    double spacing, min_profit, trail_dist, update_thresh, stop_open_pct;
    double ret, dd;
    int trades, updates_per_day;
    bool stopped_out;
};

struct Task {
    int max_pos;
    double spacing, min_profit, trail_dist, update_thresh, stop_open_pct;
};

class WorkQueue {
    std::queue<Task> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool done_ = false;

public:
    void push(const Task& task) {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push(task);
        cv_.notify_one();
    }

    bool pop(Task& task) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !tasks_.empty() || done_; });
        if (tasks_.empty()) return false;
        task = tasks_.front();
        tasks_.pop();
        return true;
    }

    void finish() {
        std::lock_guard<std::mutex> lock(mutex_);
        done_ = true;
        cv_.notify_all();
    }
};

// Global state
std::atomic<int> g_completed{0};
std::atomic<int> g_stopped_out{0};
std::mutex g_results_mutex;
std::mutex g_output_mutex;
std::vector<Result> g_results;

// ============================================================================
// Test runner using SHARED tick data
// ============================================================================
Result run_test_with_shared_data(const Task& task, const std::vector<Tick>& ticks) {
    Result r = {task.max_pos, task.spacing, task.min_profit, task.trail_dist, task.update_thresh, task.stop_open_pct, 0, 0, 0, 0, false};

    // Create lightweight engine config (no tick loading)
    TickBacktestConfig cfg;
    cfg.symbol = "XAUUSD";
    cfg.initial_balance = 10000.0;
    cfg.account_currency = "USD";
    cfg.contract_size = 100.0;
    cfg.leverage = 500.0;
    cfg.margin_rate = 1.0;
    cfg.pip_size = 0.01;
    cfg.swap_long = -66.99;
    cfg.swap_short = 41.2;
    cfg.swap_mode = 1;
    cfg.swap_3days = 3;
    cfg.start_date = "2025.01.01";
    cfg.end_date = "2025.12.30";
    cfg.verbose = false;

    // Don't set tick_data_config - we'll feed ticks manually
    TickDataConfig tc;
    tc.file_path = "";  // Empty - won't load
    tc.load_all_into_memory = false;
    cfg.tick_data_config = tc;

    try {
        TickBasedEngine engine(cfg);
        TrailingStrategy strat(task.spacing, task.min_profit, task.trail_dist, task.update_thresh, task.max_pos, 13.0, task.stop_open_pct);

        // Use RunWithTicks to process shared tick data
        engine.RunWithTicks(ticks, [&strat](const Tick& t, TickBasedEngine& e) {
            strat.OnTick(t, e);
        });

        auto res = engine.GetResults();
        r.ret = res.final_balance / 10000.0;
        double peak = res.final_balance + res.max_drawdown;
        r.dd = (peak > 0) ? (res.max_drawdown / peak * 100.0) : 0;
        r.trades = res.total_trades;
        r.updates_per_day = strat.GetUpdates() / 252;
        if (r.dd > 95) r.stopped_out = true;
    } catch (const std::exception& e) {
        static std::mutex error_mutex;
        static bool first_error = true;
        if (first_error) {
            std::lock_guard<std::mutex> lock(error_mutex);
            if (first_error) {
                std::cerr << "Exception: " << e.what() << std::endl;
                first_error = false;
            }
        }
        r.stopped_out = true;
    } catch (...) {
        r.stopped_out = true;
    }

    if (r.stopped_out) g_stopped_out++;
    return r;
}

void worker(WorkQueue& queue, int total, const std::vector<Tick>& ticks) {
    Task task;
    while (queue.pop(task)) {
        Result r = run_test_with_shared_data(task, ticks);

        {
            std::lock_guard<std::mutex> lock(g_results_mutex);
            g_results.push_back(r);
        }

        int done = ++g_completed;
        if (done % 10 == 0 || done == total) {
            std::lock_guard<std::mutex> lock(g_output_mutex);
            std::cout << "Progress: " << done << "/" << total
                      << " (" << std::fixed << std::setprecision(1) << (100.0 * done / total) << "%)"
                      << " | Stopped out: " << g_stopped_out.load()
                      << std::endl << std::flush;
        }
    }
}

int main() {
    std::cout << "=== 6-Dimensional Trailing Stop Sweep (with Stop-Opening Band) ===" << std::endl;
    std::cout << std::endl;

    // Step 1: Load tick data ONCE into shared memory
    std::string tick_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    LoadTickDataOnce(tick_path);

    // Step 2: Use all available threads (data is shared, no duplication)
    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;
    std::cout << "Using " << num_threads << " worker threads (shared tick data)" << std::endl;
    std::cout << std::endl;

    // Parameter space (6 dimensions) - ALL VALUES IN PRICE ($)
    std::vector<int> max_positions = {5, 10, 15, 20};
    std::vector<double> spacings = {1.0, 2.0, 3.0};                // Grid spacing ($)
    std::vector<double> min_profit_pts = {0.20, 0.30, 0.50, 1.0};  // Price rise to activate trail ($)
    std::vector<double> trail_dists = {5.0, 10.0, 15.0, 20.0};     // Trail stop distance ($) - needs to be wide!
    std::vector<double> update_threshs = {1.0, 2.0, 5.0};          // Update threshold ($)
    std::vector<double> stop_open_pcts = {8.0, 13.0};              // Stop opening after X% drop (survive=13%)

    // Build work queue
    WorkQueue queue;
    int total = 0;

    for (int mp : max_positions) {
        for (double sp : spacings) {
            for (double mpp : min_profit_pts) {
                for (double td : trail_dists) {
                    for (double ut : update_threshs) {
                        for (double sop : stop_open_pcts) {
                            queue.push({mp, sp, mpp, td, ut, sop});
                            total++;
                        }
                    }
                }
            }
        }
    }

    std::cout << "Testing " << total << " combinations..." << std::endl;
    std::cout << std::endl << std::flush;

    auto start = std::chrono::high_resolution_clock::now();

    // Launch worker threads - all share the same tick data (const reference)
    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < num_threads; i++) {
        threads.emplace_back(worker, std::ref(queue), total, std::cref(g_shared_ticks));
    }

    queue.finish();
    for (auto& t : threads) {
        t.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

    // Sort results
    std::sort(g_results.begin(), g_results.end(), [](const Result& a, const Result& b) {
        return a.ret > b.ret;
    });

    std::cout << std::endl;
    std::cout << "Completed in " << duration.count() << " seconds" << std::endl;
    std::cout << std::endl;
    std::cout << "=== Top 30 Configurations ===" << std::endl;
    std::cout << "Note: All price parameters in $ (MinPr/Trail/UpdT are price move, not profit)" << std::endl;
    std::cout << std::left
              << std::setw(5) << "MaxP"
              << std::setw(6) << "Space"
              << std::setw(7) << "MinPr"
              << std::setw(7) << "Trail"
              << std::setw(6) << "UpdT"
              << std::setw(6) << "StopO"
              << std::right
              << std::setw(9) << "Return"
              << std::setw(7) << "MaxDD"
              << std::setw(8) << "Trades"
              << std::setw(7) << "Upd/D"
              << std::endl;
    std::cout << std::string(80, '-') << std::endl;

    int shown = 0;
    for (const auto& r : g_results) {
        if (r.stopped_out) continue;
        std::cout << std::left << std::fixed
                  << std::setw(5) << r.max_pos
                  << "$" << std::setw(5) << std::setprecision(1) << r.spacing
                  << "$" << std::setw(6) << std::setprecision(2) << r.min_profit
                  << "$" << std::setw(6) << std::setprecision(2) << r.trail_dist
                  << "$" << std::setw(5) << std::setprecision(2) << r.update_thresh
                  << std::setw(5) << std::setprecision(0) << r.stop_open_pct << "%"
                  << std::right
                  << std::setw(8) << std::setprecision(2) << r.ret << "x"
                  << std::setw(6) << std::setprecision(0) << r.dd << "%"
                  << std::setw(8) << r.trades
                  << std::setw(7) << r.updates_per_day
                  << std::endl;
        if (++shown >= 30) break;
    }

    std::cout << std::endl;
    std::cout << "Survived: " << (total - g_stopped_out.load()) << "/" << total << " configurations" << std::endl;
    std::cout << std::endl;
    std::cout << "For comparison - Fixed TP $0.30: 8.80x" << std::endl;

    return 0;
}
