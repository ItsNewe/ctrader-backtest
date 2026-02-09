#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cfloat>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <chrono>

using namespace backtest;

// =============================================================================
// Shared tick data - loaded ONCE, used by ALL threads
// =============================================================================
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

    g_shared_ticks.reserve(30000000);  // Pre-allocate for ~29M ticks

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        Tick tick;
        size_t pos1 = line.find('\t');
        if (pos1 == std::string::npos) continue;
        tick.timestamp = line.substr(0, pos1);

        size_t pos2 = line.find('\t', pos1 + 1);
        if (pos2 == std::string::npos) continue;
        tick.bid = std::stod(line.substr(pos1 + 1, pos2 - pos1 - 1));

        size_t pos3 = line.find('\t', pos2 + 1);
        if (pos3 == std::string::npos) continue;
        tick.ask = std::stod(line.substr(pos2 + 1, pos3 - pos2 - 1));

        tick.volume = 0;
        g_shared_ticks.push_back(tick);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

    std::cout << "Loaded " << g_shared_ticks.size() << " ticks in "
              << duration.count() << " seconds" << std::endl;
    std::cout << "Memory usage: ~" << (g_shared_ticks.size() * sizeof(Tick) / 1024 / 1024)
              << " MB" << std::endl;
    std::cout << "Range: " << g_shared_ticks.front().timestamp << " to "
              << g_shared_ticks.back().timestamp << std::endl;
    std::cout << "Price: " << g_shared_ticks.front().bid << " to "
              << g_shared_ticks.back().bid << std::endl;
}

// =============================================================================
// Strategy Mode
// =============================================================================
enum StrategyMode {
    GRID_FULL,            // Standard grid: opens above AND below, with TP
    UPWARDS_ONLY_TP,      // Only opens above highest, with TP at spacing+spread
    DOWNWARDS_DIRECTION   // Only opens below lowest, no TP, closes profitable on turn-down
};

// =============================================================================
// XAGUSD FillUp Strategy
// =============================================================================
class FillUpXAGUSD {
public:
    FillUpXAGUSD(double survive_pct, double spacing,
                 double contract_size, double leverage,
                 StrategyMode mode,
                 double min_volume = 0.01, double max_volume = 100.0)
        : survive_pct_(survive_pct), spacing_(spacing),
          contract_size_(contract_size), leverage_(leverage),
          mode_(mode),
          min_volume_(min_volume), max_volume_(max_volume),
          lowest_buy_(DBL_MAX), highest_buy_(DBL_MIN),
          closest_above_(DBL_MAX), closest_below_(DBL_MAX),
          volume_of_open_trades_(0.0), trade_size_buy_(0.0),
          spacing_buy_(spacing),
          entry_count_(0), tp_count_(0),
          max_dd_pct_(0.0), peak_equity_(0.0),
          max_positions_(0), total_lots_traded_(0.0)
    {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_spread_ = tick.spread();
        current_equity_ = engine.GetEquity();

        if (current_equity_ > peak_equity_) peak_equity_ = current_equity_;
        if (peak_equity_ > 0) {
            double dd = (peak_equity_ - current_equity_) / peak_equity_ * 100.0;
            max_dd_pct_ = std::max(max_dd_pct_, dd);
        }

        int current_positions = engine.GetOpenPositions().size();
        max_positions_ = std::max(max_positions_, current_positions);

        if (mode_ == DOWNWARDS_DIRECTION) {
            // Direction detection and close-on-turn logic
            int turn = DirectionCheck();
            Iterate(engine);
            if (turn == -1) {
                // Turn down detected - close profitable positions
                CloseOnTurnDown(engine);
            }
        } else {
            // For GRID and UPWARDS modes, count TPs from engine closures
            if (current_positions < last_positions_count_) {
                tp_count_ += last_positions_count_ - current_positions;
            }
            Iterate(engine);
        }

        OpenNew(engine);

        last_positions_count_ = engine.GetOpenPositions().size();
    }

    int GetEntries() const { return entry_count_; }
    int GetTPs() const { return (mode_ == DOWNWARDS_DIRECTION) ? close_count_ : tp_count_; }
    double GetMaxDD() const { return max_dd_pct_; }
    int GetMaxPositions() const { return max_positions_; }

private:
    double survive_pct_, spacing_, contract_size_, leverage_;
    StrategyMode mode_;
    double min_volume_, max_volume_;
    double lowest_buy_, highest_buy_;
    double closest_above_, closest_below_;
    double volume_of_open_trades_, trade_size_buy_, spacing_buy_;
    double current_ask_, current_bid_, current_spread_, current_equity_;
    int entry_count_, tp_count_;
    double max_dd_pct_, peak_equity_;
    int max_positions_;
    double total_lots_traded_;
    int last_positions_count_ = 0;

    // Direction detection state (for DOWNWARDS_DIRECTION mode)
    double bid_at_turn_up_ = DBL_MAX;
    double ask_at_turn_up_ = DBL_MAX;
    double bid_at_turn_down_ = DBL_MIN;
    double ask_at_turn_down_ = DBL_MIN;
    int direction_ = 0;  // 1=up, -1=down, 0=unknown
    int close_count_ = 0;  // Track direction-based closes

    void Iterate(TickBasedEngine& engine) {
        lowest_buy_ = DBL_MAX;
        highest_buy_ = DBL_MIN;
        closest_above_ = DBL_MAX;
        closest_below_ = DBL_MAX;
        volume_of_open_trades_ = 0.0;

        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->direction == "BUY") {
                double op = trade->entry_price;
                volume_of_open_trades_ += trade->lot_size;
                lowest_buy_ = std::min(lowest_buy_, op);
                highest_buy_ = std::max(highest_buy_, op);

                if (op >= current_ask_) closest_above_ = std::min(closest_above_, op - current_ask_);
                if (op <= current_ask_) closest_below_ = std::min(closest_below_, current_ask_ - op);
            }
        }
    }

    void ClearTurnMarks() {
        bid_at_turn_up_ = DBL_MAX;
        ask_at_turn_up_ = DBL_MAX;
        bid_at_turn_down_ = DBL_MIN;
        ask_at_turn_down_ = DBL_MIN;
    }

    // Direction detection: detects when price has turned up or down by spread amount
    // Returns: -1 if turned down, +1 if turned up, 0 if no change
    int DirectionCheck() {
        // Track turning points (accumulate extremes)
        bid_at_turn_down_ = std::max(current_bid_, bid_at_turn_down_);
        bid_at_turn_up_ = std::min(current_bid_, bid_at_turn_up_);
        ask_at_turn_down_ = std::max(current_ask_, ask_at_turn_down_);
        ask_at_turn_up_ = std::min(current_ask_, ask_at_turn_up_);

        double threshold = current_spread_;  // Use spread as threshold (commission=0)

        // Detect turn DOWN: tracked high has dropped by threshold
        if (direction_ != -1 &&
            ask_at_turn_down_ >= current_ask_ + threshold &&
            bid_at_turn_down_ >= current_bid_ + threshold) {
            direction_ = -1;
            ClearTurnMarks();
            return -1;
        }

        // Detect turn UP: tracked low has risen by threshold
        if (direction_ != 1 &&
            bid_at_turn_up_ <= current_bid_ - threshold &&
            ask_at_turn_up_ <= current_ask_ - threshold) {
            direction_ = 1;
            ClearTurnMarks();
            return 1;
        }

        return 0;
    }

    // Close profitable positions when turn-down detected
    void CloseOnTurnDown(TickBasedEngine& engine) {
        // Check if there are any profitable positions (overall)
        double total_profit = 0.0;
        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->direction == "BUY") {
                double profit = (current_bid_ - trade->entry_price) * trade->lot_size * contract_size_;
                total_profit += profit;
            }
        }

        if (total_profit <= 0.0) return;  // Only close if overall profitable

        // Collect pointers to profitable positions
        std::vector<Trade*> to_close;
        for (Trade* trade : engine.GetOpenPositions()) {
            if (trade->direction == "BUY") {
                double profit = (current_bid_ - trade->entry_price) * trade->lot_size * contract_size_;
                if (profit > 0.0) {
                    to_close.push_back(trade);
                }
            }
        }

        for (Trade* trade : to_close) {
            engine.ClosePosition(trade, "Direction");
            close_count_++;
        }
    }

    void SizingBuy(TickBasedEngine& engine, int positions_total) {
        trade_size_buy_ = 0.0;

        double used_margin = 0.0;
        for (const Trade* t : engine.GetOpenPositions()) {
            // FOREX margin mode: lots * contract_size / leverage (no price)
            used_margin += t->lot_size * contract_size_ / leverage_;
        }

        double margin_stop_out_level = 20.0;
        double current_margin_level = (used_margin > 0) ? (current_equity_ / used_margin * 100.0) : 10000.0;

        double equity_at_target = current_equity_ * margin_stop_out_level / current_margin_level;
        double equity_difference = current_equity_ - equity_at_target;
        double price_difference = 0.0;
        if (volume_of_open_trades_ > 0) {
            price_difference = equity_difference / (volume_of_open_trades_ * contract_size_);
        }

        double end_price = 0.0;
        if (positions_total == 0) {
            end_price = current_ask_ * ((100.0 - survive_pct_) / 100.0);
        } else {
            end_price = highest_buy_ * ((100.0 - survive_pct_) / 100.0);
        }

        double distance = current_ask_ - end_price;
        double number_of_trades = std::floor(distance / spacing_buy_);
        if (number_of_trades < 1) number_of_trades = 1;

        if ((positions_total == 0) || ((current_ask_ - price_difference) < end_price)) {
            equity_at_target = current_equity_ - volume_of_open_trades_ * std::abs(distance) * contract_size_;
            double margin_level = (used_margin > 0) ? (equity_at_target / used_margin * 100.0) : 10000.0;
            double trade_size = min_volume_;

            if (margin_level > margin_stop_out_level || positions_total == 0) {
                double d_equity = contract_size_ * trade_size * spacing_buy_ *
                                  (number_of_trades * (number_of_trades + 1) / 2);
                double d_spread = number_of_trades * trade_size * current_spread_ * contract_size_;
                d_equity += d_spread;
                double local_used_margin = trade_size * contract_size_ / leverage_;
                local_used_margin = number_of_trades * local_used_margin;

                double max = max_volume_ / min_volume_;
                double multiplier = 0.0;
                double equity_backup = equity_at_target;
                double used_margin_backup = used_margin;

                equity_at_target -= max * d_equity;
                used_margin += max * local_used_margin;
                if (margin_stop_out_level < (equity_at_target / used_margin * 100.0)) {
                    multiplier = max;
                } else {
                    used_margin = used_margin_backup;
                    equity_at_target = equity_backup;
                    for (double increment = max; increment >= 1; increment /= 10) {
                        while (margin_stop_out_level < (equity_at_target / used_margin * 100.0)) {
                            equity_backup = equity_at_target;
                            used_margin_backup = used_margin;
                            multiplier += increment;
                            equity_at_target -= increment * d_equity;
                            used_margin += increment * local_used_margin;
                        }
                        multiplier -= increment;
                        used_margin = used_margin_backup;
                        equity_at_target = equity_backup;
                    }
                }

                multiplier = std::max(1.0, multiplier);
                trade_size_buy_ = multiplier * min_volume_;
                trade_size_buy_ = std::min(trade_size_buy_, max_volume_);
            }
        }
    }

    bool Open(double local_unit, TickBasedEngine& engine) {
        if (local_unit < min_volume_) return false;
        double final_unit = std::min(local_unit, max_volume_);
        final_unit = std::round(final_unit * 100.0) / 100.0;

        double tp = 0.0;
        if (mode_ != DOWNWARDS_DIRECTION) {
            tp = current_ask_ + current_spread_ + spacing_buy_;
        }

        Trade* trade = engine.OpenMarketOrder("BUY", final_unit, 0.0, tp);
        if (trade) {
            entry_count_++;
            total_lots_traded_ += final_unit;
            return true;
        }
        return false;
    }

    void OpenNew(TickBasedEngine& engine) {
        int positions_total = engine.GetOpenPositions().size();

        if (positions_total == 0) {
            SizingBuy(engine, positions_total);
            if (Open(trade_size_buy_, engine)) {
                highest_buy_ = current_ask_;
                lowest_buy_ = current_ask_;
            }
        } else if (mode_ == GRID_FULL) {
            if (lowest_buy_ >= current_ask_ + spacing_buy_) {
                SizingBuy(engine, positions_total);
                if (Open(trade_size_buy_, engine)) lowest_buy_ = current_ask_;
            } else if (highest_buy_ <= current_ask_ - spacing_buy_) {
                SizingBuy(engine, positions_total);
                if (Open(trade_size_buy_, engine)) highest_buy_ = current_ask_;
            } else if ((closest_above_ >= spacing_buy_) && (closest_below_ >= spacing_buy_)) {
                SizingBuy(engine, positions_total);
                Open(trade_size_buy_, engine);
            }
        } else if (mode_ == UPWARDS_ONLY_TP) {
            // Only open above highest
            if (highest_buy_ <= current_ask_ - spacing_buy_) {
                SizingBuy(engine, positions_total);
                if (Open(trade_size_buy_, engine)) highest_buy_ = current_ask_;
            }
        } else {
            // DOWNWARDS_DIRECTION: only open below lowest
            if (lowest_buy_ > current_ask_ + spacing_buy_) {
                SizingBuy(engine, positions_total);
                if (Open(trade_size_buy_, engine)) lowest_buy_ = current_ask_;
            }
        }
    }
};

// =============================================================================
// Task and Result structures
// =============================================================================
struct SweepResult {
    StrategyMode mode;
    double survive_pct;
    double spacing;
    std::string period;
    std::string start_date;
    std::string end_date;
    double final_balance;
    double return_x;
    int entries;
    int tps;
    double max_dd_pct;
    int max_positions;
    double total_swap;
    double sharpe_approx;
    bool stopped_out;
};

struct Task {
    StrategyMode mode;
    double survive_pct;
    double spacing;
    std::string period_label;
    std::string start_date;
    std::string end_date;
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

// Global thread-safe state
std::atomic<int> g_completed{0};
std::atomic<int> g_stopped_out{0};
std::mutex g_results_mutex;
std::mutex g_output_mutex;
std::vector<SweepResult> g_results;

// =============================================================================
// Worker function - runs one config using shared tick data
// =============================================================================
SweepResult run_config(const Task& task, const std::vector<Tick>& ticks) {
    SweepResult result;
    result.mode = task.mode;
    result.survive_pct = task.survive_pct;
    result.spacing = task.spacing;
    result.period = task.period_label;
    result.start_date = task.start_date;
    result.end_date = task.end_date;
    result.stopped_out = false;

    TickDataConfig tick_config;
    tick_config.file_path = "";  // Not used - ticks provided directly

    TickBacktestConfig config;
    config.symbol = "XAGUSD";
    config.initial_balance = 10000.0;
    config.account_currency = "USD";
    config.contract_size = 5000.0;
    config.leverage = 500.0;
    config.margin_rate = 1.0;
    config.pip_size = 0.001;
    config.swap_long = -17.03;
    config.swap_short = 0.1;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.verbose = false;
    config.start_date = task.start_date;
    config.end_date = task.end_date;
    config.tick_data_config = tick_config;

    try {
        TickBasedEngine engine(config);
        FillUpXAGUSD strategy(task.survive_pct, task.spacing, 5000.0, 500.0, task.mode);

        engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        result.final_balance = results.final_balance;
        result.return_x = results.final_balance / 10000.0;
        result.entries = strategy.GetEntries();
        result.tps = strategy.GetTPs();
        result.max_dd_pct = strategy.GetMaxDD();
        result.max_positions = strategy.GetMaxPositions();
        result.total_swap = results.total_swap_charged;
        result.stopped_out = engine.IsStopOutOccurred();

        if (result.max_dd_pct > 0) {
            result.sharpe_approx = (result.return_x - 1.0) / (result.max_dd_pct / 100.0);
        } else {
            result.sharpe_approx = 0;
        }
    } catch (...) {
        result.final_balance = 0;
        result.return_x = 0;
        result.entries = 0;
        result.tps = 0;
        result.max_dd_pct = 100.0;
        result.stopped_out = true;
    }

    return result;
}

void worker(WorkQueue& queue, int total, const std::vector<Tick>& ticks) {
    Task task;
    while (queue.pop(task)) {
        SweepResult r = run_config(task, ticks);

        {
            std::lock_guard<std::mutex> lock(g_results_mutex);
            g_results.push_back(r);
        }

        if (r.stopped_out) g_stopped_out++;

        int done = ++g_completed;
        if (done % 10 == 0 || done == total) {
            std::lock_guard<std::mutex> lock(g_output_mutex);
            std::cout << "  Progress: " << done << "/" << total
                      << " (" << std::fixed << std::setprecision(1)
                      << (100.0 * done / total) << "%)"
                      << " | Stopped: " << g_stopped_out.load()
                      << std::endl << std::flush;
        }
    }
}

// =============================================================================
// Main
// =============================================================================
int main() {
    std::string tick_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\mt5\\fill_up_xagusd\\XAGUSD_TESTER_TICKS.csv";

    std::cout << "========================================================" << std::endl;
    std::cout << " XAGUSD COMPREHENSIVE PARAMETER SWEEP (PARALLEL)" << std::endl;
    std::cout << " Comparing: Grid vs Upwards-Only vs Downwards-Direction" << std::endl;
    std::cout << " Data:  tester ticks (2025.01 - 2026.01)" << std::endl;
    std::cout << "========================================================" << std::endl;

    // Step 1: Load all ticks into shared memory ONCE
    LoadTickDataOnce(tick_path);

    if (g_shared_ticks.empty()) {
        std::cerr << "No ticks loaded!" << std::endl;
        return 1;
    }

    // Find max price drop in data
    double peak_price = 0, max_drop_pct = 0;
    for (const auto& t : g_shared_ticks) {
        if (t.bid > peak_price) peak_price = t.bid;
        double drop = (peak_price - t.bid) / peak_price * 100.0;
        if (drop > max_drop_pct) max_drop_pct = drop;
    }
    std::cout << "Max price drop from peak: " << std::fixed << std::setprecision(1)
              << max_drop_pct << "%" << std::endl;

    // Step 2: Define periods and parameter space
    struct Period {
        std::string start, end, label;
    };
    std::vector<Period> periods = {
        {"2025.01.01", "2026.01.23", "FULL"},
        {"2025.01.01", "2025.07.01", "H1"},
        {"2025.07.01", "2026.01.23", "H2"}
    };

    // GRID: survive 13%, 18%, 25%
    // UPWARDS: survive 18%, 20%, 25% (per user: above max silver drop)
    // DOWNWARDS: survive 18%, 20%, 25% (same survive values as upwards)
    std::vector<double> grid_survives = {13.0, 18.0, 25.0};
    std::vector<double> up_survives = {18.0, 20.0, 25.0};
    std::vector<double> down_survives = {18.0, 20.0, 25.0};
    std::vector<double> spacings = {0.50, 1.00, 1.50, 1.90, 2.50, 3.50, 5.00};

    // Step 3: Build work queue with all tasks
    WorkQueue queue;
    int total = 0;

    for (const auto& period : periods) {
        // Grid configs
        for (double survive : grid_survives) {
            for (double spacing : spacings) {
                queue.push({GRID_FULL, survive, spacing, period.label, period.start, period.end});
                total++;
            }
        }
        // Upwards configs
        for (double survive : up_survives) {
            for (double spacing : spacings) {
                queue.push({UPWARDS_ONLY_TP, survive, spacing, period.label, period.start, period.end});
                total++;
            }
        }
        // Downwards configs
        for (double survive : down_survives) {
            for (double spacing : spacings) {
                queue.push({DOWNWARDS_DIRECTION, survive, spacing, period.label, period.start, period.end});
                total++;
            }
        }
    }

    // Step 4: Launch parallel workers
    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;

    std::cout << "\nTotal configurations: " << total << std::endl;
    std::cout << "Using " << num_threads << " worker threads" << std::endl;
    std::cout << std::endl << std::flush;

    auto start_time = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < num_threads; i++) {
        threads.emplace_back(worker, std::ref(queue), total, std::cref(g_shared_ticks));
    }

    queue.finish();
    for (auto& t : threads) {
        t.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);
    std::cout << "\nCompleted in " << duration.count() << " seconds" << std::endl;

    // ==========================================================================
    // Print Summary Tables
    // ==========================================================================
    auto mode_name = [](StrategyMode m) -> std::string {
        switch (m) {
            case GRID_FULL: return "GRID";
            case UPWARDS_ONLY_TP: return "UP+TP";
            case DOWNWARDS_DIRECTION: return "DN+DIR";
            default: return "???";
        }
    };

    std::cout << "\n\n========================================================" << std::endl;
    std::cout << " FULL RESULTS TABLE" << std::endl;
    std::cout << "========================================================" << std::endl;

    for (const auto& period : periods) {
        std::cout << "\n=== " << period.label << " ===" << std::endl;
        std::cout << std::setw(7) << "Mode"
                  << std::setw(7) << "Surv%"
                  << std::setw(7) << "Space"
                  << std::setw(10) << "Return"
                  << std::setw(8) << "MaxDD%"
                  << std::setw(7) << "Entry"
                  << std::setw(6) << "TPs"
                  << std::setw(7) << "MaxP"
                  << std::setw(9) << "Swap"
                  << std::setw(9) << "RiskAdj"
                  << std::setw(5) << "Stop"
                  << std::endl;
        std::cout << std::string(83, '-') << std::endl;

        // Sort results for this period by mode then survive then spacing
        std::vector<const SweepResult*> period_results;
        for (const auto& r : g_results) {
            if (r.period == period.label) period_results.push_back(&r);
        }
        std::sort(period_results.begin(), period_results.end(),
                  [](const SweepResult* a, const SweepResult* b) {
                      if (a->mode != b->mode) return a->mode < b->mode;
                      if (a->survive_pct != b->survive_pct) return a->survive_pct < b->survive_pct;
                      return a->spacing < b->spacing;
                  });

        for (const auto* r : period_results) {
            std::cout << std::setw(7) << mode_name(r->mode)
                      << std::setw(5) << std::fixed << std::setprecision(0) << r->survive_pct << "%"
                      << std::setw(6) << std::setprecision(2) << r->spacing
                      << std::setw(9) << std::setprecision(2) << r->return_x << "x"
                      << std::setw(7) << std::setprecision(1) << r->max_dd_pct << "%"
                      << std::setw(7) << r->entries
                      << std::setw(6) << r->tps
                      << std::setw(7) << r->max_positions
                      << std::setw(8) << std::setprecision(0) << r->total_swap
                      << std::setw(8) << std::setprecision(2) << r->sharpe_approx
                      << std::setw(5) << (r->stopped_out ? "YES" : "")
                      << std::endl;
        }
    }

    // Top performers by risk-adjusted return
    std::cout << "\n\n========================================================" << std::endl;
    std::cout << " TOP 10 BY RISK-ADJUSTED RETURN (FULL PERIOD)" << std::endl;
    std::cout << "========================================================" << std::endl;

    std::vector<const SweepResult*> full_results;
    for (const auto& r : g_results) {
        if (r.period == "FULL" && !r.stopped_out) full_results.push_back(&r);
    }
    std::sort(full_results.begin(), full_results.end(),
              [](const SweepResult* a, const SweepResult* b) {
                  return a->sharpe_approx > b->sharpe_approx;
              });

    std::cout << std::setw(7) << "Mode" << std::setw(7) << "Surv%"
              << std::setw(7) << "Space" << std::setw(10) << "Return"
              << std::setw(8) << "MaxDD%" << std::setw(9) << "RiskAdj" << std::endl;
    std::cout << std::string(48, '-') << std::endl;
    for (int i = 0; i < std::min(10, (int)full_results.size()); i++) {
        auto* r = full_results[i];
        std::cout << std::setw(7) << mode_name(r->mode)
                  << std::setw(5) << std::fixed << std::setprecision(0) << r->survive_pct << "%"
                  << std::setw(6) << std::setprecision(2) << r->spacing
                  << std::setw(9) << std::setprecision(2) << r->return_x << "x"
                  << std::setw(7) << std::setprecision(1) << r->max_dd_pct << "%"
                  << std::setw(8) << std::setprecision(2) << r->sharpe_approx
                  << std::endl;
    }

    // Top performers by absolute return
    std::cout << "\n\n========================================================" << std::endl;
    std::cout << " TOP 10 BY ABSOLUTE RETURN (FULL PERIOD)" << std::endl;
    std::cout << "========================================================" << std::endl;

    std::sort(full_results.begin(), full_results.end(),
              [](const SweepResult* a, const SweepResult* b) {
                  return a->return_x > b->return_x;
              });

    std::cout << std::setw(7) << "Mode" << std::setw(7) << "Surv%"
              << std::setw(7) << "Space" << std::setw(10) << "Return"
              << std::setw(8) << "MaxDD%" << std::setw(9) << "RiskAdj"
              << std::setw(7) << "Entry" << std::setw(6) << "TPs" << std::endl;
    std::cout << std::string(61, '-') << std::endl;
    for (int i = 0; i < std::min(10, (int)full_results.size()); i++) {
        auto* r = full_results[i];
        std::cout << std::setw(7) << mode_name(r->mode)
                  << std::setw(5) << std::fixed << std::setprecision(0) << r->survive_pct << "%"
                  << std::setw(6) << std::setprecision(2) << r->spacing
                  << std::setw(9) << std::setprecision(2) << r->return_x << "x"
                  << std::setw(7) << std::setprecision(1) << r->max_dd_pct << "%"
                  << std::setw(8) << std::setprecision(2) << r->sharpe_approx
                  << std::setw(7) << r->entries
                  << std::setw(6) << r->tps
                  << std::endl;
    }

    // H1/H2 Consistency Analysis
    std::cout << "\n\n========================================================" << std::endl;
    std::cout << " H1/H2 CONSISTENCY (key for unknown future)" << std::endl;
    std::cout << "========================================================" << std::endl;
    std::cout << std::setw(7) << "Mode" << std::setw(7) << "Surv%"
              << std::setw(7) << "Space" << std::setw(9) << "H1 Ret"
              << std::setw(9) << "H2 Ret" << std::setw(8) << "H1/H2"
              << std::setw(8) << "H1 DD%" << std::setw(8) << "H2 DD%"
              << std::setw(7) << "Stable" << std::endl;
    std::cout << std::string(72, '-') << std::endl;

    // Collect all unique survive values
    std::vector<double> all_survive_vals;
    for (double s : grid_survives) all_survive_vals.push_back(s);
    for (double s : up_survives) all_survive_vals.push_back(s);
    for (double s : down_survives) all_survive_vals.push_back(s);
    std::sort(all_survive_vals.begin(), all_survive_vals.end());
    all_survive_vals.erase(std::unique(all_survive_vals.begin(), all_survive_vals.end()), all_survive_vals.end());

    for (double survive : all_survive_vals) {
        for (double spacing : spacings) {
            std::vector<StrategyMode> modes_to_check;
            if (std::find(grid_survives.begin(), grid_survives.end(), survive) != grid_survives.end())
                modes_to_check.push_back(GRID_FULL);
            if (std::find(up_survives.begin(), up_survives.end(), survive) != up_survives.end())
                modes_to_check.push_back(UPWARDS_ONLY_TP);
            if (std::find(down_survives.begin(), down_survives.end(), survive) != down_survives.end())
                modes_to_check.push_back(DOWNWARDS_DIRECTION);

            for (StrategyMode mode : modes_to_check) {
                const SweepResult *h1 = nullptr, *h2 = nullptr;
                for (const auto& r : g_results) {
                    if (r.survive_pct == survive && std::abs(r.spacing - spacing) < 0.01 && r.mode == mode) {
                        if (r.period == "H1") h1 = &r;
                        if (r.period == "H2") h2 = &r;
                    }
                }
                if (h1 && h2 && !h1->stopped_out && !h2->stopped_out) {
                    double ratio = (h2->return_x > 0.1) ? h1->return_x / h2->return_x : 99.0;
                    bool stable = (ratio > 0.5 && ratio < 2.0);
                    std::cout << std::setw(7) << mode_name(mode)
                              << std::setw(5) << std::fixed << std::setprecision(0) << survive << "%"
                              << std::setw(6) << std::setprecision(2) << spacing
                              << std::setw(8) << std::setprecision(2) << h1->return_x << "x"
                              << std::setw(8) << std::setprecision(2) << h2->return_x << "x"
                              << std::setw(7) << std::setprecision(2) << ratio
                              << std::setw(7) << std::setprecision(1) << h1->max_dd_pct << "%"
                              << std::setw(7) << std::setprecision(1) << h2->max_dd_pct << "%"
                              << std::setw(7) << (stable ? "YES" : "NO")
                              << std::endl;
                }
            }
        }
    }

    // Strategy mode comparison
    std::cout << "\n\n========================================================" << std::endl;
    std::cout << " STRATEGY MODE COMPARISON (FULL PERIOD)" << std::endl;
    std::cout << "========================================================" << std::endl;

    for (StrategyMode mode : {GRID_FULL, UPWARDS_ONLY_TP, DOWNWARDS_DIRECTION}) {
        std::vector<double> returns, dds, sharpes;
        int stopped = 0, total_mode = 0;
        for (const auto& r : g_results) {
            if (r.mode == mode && r.period == "FULL") {
                total_mode++;
                if (r.stopped_out) { stopped++; continue; }
                returns.push_back(r.return_x);
                dds.push_back(r.max_dd_pct);
                sharpes.push_back(r.sharpe_approx);
            }
        }
        if (returns.empty()) continue;
        std::sort(returns.begin(), returns.end());
        std::sort(dds.begin(), dds.end());
        std::sort(sharpes.begin(), sharpes.end());

        std::cout << "\n  " << mode_name(mode) << " (" << returns.size() << " survived, "
                  << stopped << " stopped out of " << total_mode << "):" << std::endl;
        std::cout << "    Return:  min=" << std::fixed << std::setprecision(2) << returns.front()
                  << "x  median=" << returns[returns.size()/2]
                  << "x  max=" << returns.back() << "x" << std::endl;
        std::cout << "    MaxDD:   min=" << std::setprecision(1) << dds.front()
                  << "%  median=" << dds[dds.size()/2]
                  << "%  max=" << dds.back() << "%" << std::endl;
        std::cout << "    RiskAdj: min=" << std::setprecision(2) << sharpes.front()
                  << "  median=" << sharpes[sharpes.size()/2]
                  << "  max=" << sharpes.back() << std::endl;
    }

    // Safety analysis
    std::cout << "\n\n========================================================" << std::endl;
    std::cout << " SAFETY ANALYSIS FOR UNKNOWN FUTURE" << std::endl;
    std::cout << "========================================================" << std::endl;
    std::cout << "\nMax observed price drop in 2025 XAGUSD: " << std::setprecision(1) << max_drop_pct << "%" << std::endl;
    std::cout << "survive_pct MUST exceed this to avoid stop-out." << std::endl;
    std::cout << "\nGRID mode: Profits from oscillations in BOTH directions." << std::endl;
    std::cout << "  - Wins in sideways/choppy markets" << std::endl;
    std::cout << "  - Also wins in trends (opens above AND below)" << std::endl;
    std::cout << "  - RISK: Accumulates below-entries in crashes" << std::endl;
    std::cout << "\nUPWARDS mode: Only profits from upward moves (with TP)." << std::endl;
    std::cout << "  - Wins big in bull markets (2025: XAGUSD +243%)" << std::endl;
    std::cout << "  - No exposure to downward grid buildup" << std::endl;
    std::cout << "  - RISK: Dead money in sideways/bear markets" << std::endl;
    std::cout << "\nDOWNWARDS mode: Buy-the-dip, close on direction change." << std::endl;
    std::cout << "  - Fills positions as price drops (mean-reversion)" << std::endl;
    std::cout << "  - Closes profitable positions when turn-down detected" << std::endl;
    std::cout << "  - No fixed TP - exits based on spread-based direction detection" << std::endl;
    std::cout << "  - RISK: Holds through extended drops, no mechanical exit" << std::endl;

    // Find the best "safe" configs (survive >= max_drop, moderate DD)
    std::cout << "\n--- SAFEST CONFIGURATIONS (survive > " << std::setprecision(0) << max_drop_pct
              << "%, DD < 70%) ---" << std::endl;
    std::vector<const SweepResult*> safe_results;
    for (const auto& r : g_results) {
        if (r.period == "FULL" && !r.stopped_out &&
            r.survive_pct > max_drop_pct && r.max_dd_pct < 70.0) {
            safe_results.push_back(&r);
        }
    }
    std::sort(safe_results.begin(), safe_results.end(),
              [](const SweepResult* a, const SweepResult* b) {
                  return a->sharpe_approx > b->sharpe_approx;
              });

    std::cout << std::setw(7) << "Mode" << std::setw(7) << "Surv%"
              << std::setw(7) << "Space" << std::setw(10) << "Return"
              << std::setw(8) << "MaxDD%" << std::setw(9) << "RiskAdj" << std::endl;
    std::cout << std::string(48, '-') << std::endl;
    for (int i = 0; i < std::min(10, (int)safe_results.size()); i++) {
        auto* r = safe_results[i];
        std::cout << std::setw(7) << mode_name(r->mode)
                  << std::setw(5) << std::fixed << std::setprecision(0) << r->survive_pct << "%"
                  << std::setw(6) << std::setprecision(2) << r->spacing
                  << std::setw(9) << std::setprecision(2) << r->return_x << "x"
                  << std::setw(7) << std::setprecision(1) << r->max_dd_pct << "%"
                  << std::setw(8) << std::setprecision(2) << r->sharpe_approx
                  << std::endl;
    }

    std::cout << "\nDone." << std::endl;
    return 0;
}
