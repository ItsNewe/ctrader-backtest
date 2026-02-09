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

    g_shared_ticks.reserve(30000000);

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
    std::cout << "Range: " << g_shared_ticks.front().timestamp << " to "
              << g_shared_ticks.back().timestamp << std::endl;
    std::cout << "Price: " << std::fixed << std::setprecision(3)
              << g_shared_ticks.front().bid << " to "
              << g_shared_ticks.back().bid << std::endl;
}

// =============================================================================
// Strategy Mode
// =============================================================================
enum StrategyMode {
    GRID_FULL,
    UPWARDS_ONLY_TP
};

// =============================================================================
// Spacing Mode
// =============================================================================
enum SpacingMode {
    FIXED_DOLLAR,    // spacing_buy_ = fixed $ amount
    PCT_OF_HIGHEST   // spacing_buy_ = highest_buy_ * pct / 100
};

// =============================================================================
// XAGUSD FillUp Strategy with Percentage-Based Spacing
// =============================================================================
class FillUpXAGUSD {
public:
    FillUpXAGUSD(double survive_pct, double spacing_value,
                 double contract_size, double leverage,
                 StrategyMode mode, SpacingMode spacing_mode,
                 double min_volume = 0.01, double max_volume = 100.0)
        : survive_pct_(survive_pct), spacing_value_(spacing_value),
          contract_size_(contract_size), leverage_(leverage),
          mode_(mode), spacing_mode_(spacing_mode),
          min_volume_(min_volume), max_volume_(max_volume),
          lowest_buy_(DBL_MAX), highest_buy_(DBL_MIN),
          closest_above_(DBL_MAX), closest_below_(DBL_MAX),
          volume_of_open_trades_(0.0), trade_size_buy_(0.0),
          spacing_buy_(spacing_value),
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

        if (current_positions < last_positions_count_) {
            tp_count_ += last_positions_count_ - current_positions;
        }
        last_positions_count_ = current_positions;

        Iterate(engine);

        // Update dynamic spacing based on mode
        UpdateSpacing();

        OpenNew(engine);

        last_positions_count_ = engine.GetOpenPositions().size();
    }

    int GetEntries() const { return entry_count_; }
    int GetTPs() const { return tp_count_; }
    double GetMaxDD() const { return max_dd_pct_; }
    int GetMaxPositions() const { return max_positions_; }

private:
    double survive_pct_, spacing_value_, contract_size_, leverage_;
    StrategyMode mode_;
    SpacingMode spacing_mode_;
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

    void UpdateSpacing() {
        if (spacing_mode_ == FIXED_DOLLAR) {
            spacing_buy_ = spacing_value_;
        } else {
            // PCT_OF_HIGHEST: use highest open trade's price, or current ask if no trades
            double ref_price = (highest_buy_ > 0 && highest_buy_ != DBL_MIN)
                               ? highest_buy_ : current_ask_;
            spacing_buy_ = ref_price * spacing_value_ / 100.0;
        }
    }

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

    void SizingBuy(TickBasedEngine& engine, int positions_total) {
        trade_size_buy_ = 0.0;

        double used_margin = 0.0;
        for (const Trade* t : engine.GetOpenPositions()) {
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

        // TP = spacing above entry (in dollar terms)
        double tp = current_ask_ + current_spread_ + spacing_buy_;

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
        } else {
            // UPWARDS_ONLY: only open above highest
            if (highest_buy_ <= current_ask_ - spacing_buy_) {
                SizingBuy(engine, positions_total);
                if (Open(trade_size_buy_, engine)) highest_buy_ = current_ask_;
            }
        }
    }
};

// =============================================================================
// Task and Result structures
// =============================================================================
struct SweepResult {
    StrategyMode mode;
    SpacingMode spacing_mode;
    double survive_pct;
    double spacing_value;  // $ for FIXED, % for PCT
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
    SpacingMode spacing_mode;
    double survive_pct;
    double spacing_value;
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

// Global state
std::atomic<int> g_completed{0};
std::atomic<int> g_stopped_out{0};
std::mutex g_results_mutex;
std::mutex g_output_mutex;
std::vector<SweepResult> g_results;

// =============================================================================
// Worker
// =============================================================================
SweepResult run_config(const Task& task, const std::vector<Tick>& ticks) {
    SweepResult result;
    result.mode = task.mode;
    result.spacing_mode = task.spacing_mode;
    result.survive_pct = task.survive_pct;
    result.spacing_value = task.spacing_value;
    result.period = task.period_label;
    result.start_date = task.start_date;
    result.end_date = task.end_date;
    result.stopped_out = false;

    TickDataConfig tick_config;
    tick_config.file_path = "";

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
        FillUpXAGUSD strategy(task.survive_pct, task.spacing_value,
                              5000.0, 500.0, task.mode, task.spacing_mode);

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
        if (done % 20 == 0 || done == total) {
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
    std::cout << " XAGUSD PERCENTAGE-BASED SPACING SWEEP" << std::endl;
    std::cout << " spacing = highest_open_price * X%" << std::endl;
    std::cout << " Data:  tester ticks (2025.01 - 2026.01)" << std::endl;
    std::cout << "========================================================" << std::endl;

    LoadTickDataOnce(tick_path);
    if (g_shared_ticks.empty()) return 1;

    // Show what percentage spacings mean at different price levels
    std::cout << "\n--- Spacing equivalents at different prices ---" << std::endl;
    std::vector<double> pcts = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 10.0};
    std::cout << std::setw(6) << "Pct";
    std::cout << std::setw(8) << "@$30" << std::setw(8) << "@$50"
              << std::setw(8) << "@$70" << std::setw(8) << "@$96" << std::endl;
    for (double p : pcts) {
        std::cout << std::fixed << std::setprecision(1) << std::setw(5) << p << "%"
                  << std::setprecision(2)
                  << std::setw(7) << (30.0 * p / 100.0) << "$"
                  << std::setw(7) << (50.0 * p / 100.0) << "$"
                  << std::setw(7) << (70.0 * p / 100.0) << "$"
                  << std::setw(7) << (96.0 * p / 100.0) << "$"
                  << std::endl;
    }

    // Define periods
    struct Period {
        std::string start, end, label;
    };
    std::vector<Period> periods = {
        {"2025.01.01", "2026.01.23", "FULL"},
        {"2025.01.01", "2025.07.01", "H1"},
        {"2025.07.01", "2026.01.23", "H2"}
    };

    // Parameter space
    std::vector<double> survives = {20.0, 25.0};
    std::vector<double> pct_spacings = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 10.0};

    // Also include best fixed-dollar configs for comparison
    std::vector<double> fixed_spacings = {1.90, 2.50, 3.50};

    // Build work queue
    WorkQueue queue;
    int total = 0;

    for (const auto& period : periods) {
        for (double survive : survives) {
            for (StrategyMode mode : {GRID_FULL, UPWARDS_ONLY_TP}) {
                // Percentage-based configs
                for (double pct : pct_spacings) {
                    queue.push({mode, PCT_OF_HIGHEST, survive, pct,
                               period.label, period.start, period.end});
                    total++;
                }
                // Fixed-dollar comparison configs
                for (double fixed : fixed_spacings) {
                    queue.push({mode, FIXED_DOLLAR, survive, fixed,
                               period.label, period.start, period.end});
                    total++;
                }
            }
        }
    }

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
    for (auto& t : threads) t.join();

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);
    std::cout << "\nCompleted in " << duration.count() << " seconds" << std::endl;

    // ==========================================================================
    // Print Results
    // ==========================================================================
    auto mode_name = [](StrategyMode m) { return m == GRID_FULL ? "GRID" : "UP+TP"; };
    auto spacing_label = [](SpacingMode sm, double val) -> std::string {
        if (sm == FIXED_DOLLAR) return "$" + std::to_string(val).substr(0, 4);
        return std::to_string(val).substr(0, std::to_string(val).find('.') + 2) + "%";
    };

    // Full results by mode and survive
    for (StrategyMode mode : {GRID_FULL, UPWARDS_ONLY_TP}) {
        for (double survive : survives) {
            std::cout << "\n\n========================================================" << std::endl;
            std::cout << " " << mode_name(mode) << " | survive=" << (int)survive << "%" << std::endl;
            std::cout << "========================================================" << std::endl;
            std::cout << std::setw(8) << "Spacing"
                      << std::setw(6) << "Type"
                      << std::setw(10) << "FULL Ret"
                      << std::setw(8) << "FULL DD"
                      << std::setw(9) << "RiskAdj"
                      << std::setw(9) << "H1 Ret"
                      << std::setw(9) << "H2 Ret"
                      << std::setw(8) << "H1/H2"
                      << std::setw(7) << "Entry"
                      << std::setw(5) << "Stop"
                      << std::endl;
            std::cout << std::string(79, '-') << std::endl;

            // Collect configs for this mode/survive
            struct Row {
                SpacingMode sm;
                double val;
                const SweepResult *full, *h1, *h2;
            };
            std::vector<Row> rows;

            // Gather unique spacing configs
            std::vector<std::pair<SpacingMode, double>> configs;
            for (double pct : pct_spacings) configs.push_back({PCT_OF_HIGHEST, pct});
            for (double fixed : fixed_spacings) configs.push_back({FIXED_DOLLAR, fixed});

            for (auto& [sm, val] : configs) {
                Row row = {sm, val, nullptr, nullptr, nullptr};
                for (const auto& r : g_results) {
                    if (r.mode == mode && r.spacing_mode == sm &&
                        r.survive_pct == survive &&
                        std::abs(r.spacing_value - val) < 0.01) {
                        if (r.period == "FULL") row.full = &r;
                        if (r.period == "H1") row.h1 = &r;
                        if (r.period == "H2") row.h2 = &r;
                    }
                }
                if (row.full) rows.push_back(row);
            }

            for (const auto& row : rows) {
                std::string sp_str = spacing_label(row.sm, row.val);
                std::string type_str = (row.sm == FIXED_DOLLAR) ? "fixed" : "pct";

                std::cout << std::setw(8) << sp_str
                          << std::setw(6) << type_str;

                if (row.full->stopped_out) {
                    std::cout << std::setw(10) << "STOPPED"
                              << std::setw(8) << "-"
                              << std::setw(9) << "-"
                              << std::setw(9) << "-"
                              << std::setw(9) << "-"
                              << std::setw(8) << "-"
                              << std::setw(7) << row.full->entries
                              << std::setw(5) << "YES"
                              << std::endl;
                    continue;
                }

                double h1h2_ratio = 0;
                if (row.h1 && row.h2 && !row.h1->stopped_out && !row.h2->stopped_out && row.h2->return_x > 0.1) {
                    h1h2_ratio = row.h1->return_x / row.h2->return_x;
                }

                std::cout << std::fixed
                          << std::setw(9) << std::setprecision(2) << row.full->return_x << "x"
                          << std::setw(7) << std::setprecision(1) << row.full->max_dd_pct << "%"
                          << std::setw(9) << std::setprecision(2) << row.full->sharpe_approx;

                if (row.h1 && !row.h1->stopped_out)
                    std::cout << std::setw(8) << std::setprecision(2) << row.h1->return_x << "x";
                else
                    std::cout << std::setw(9) << "STOP";

                if (row.h2 && !row.h2->stopped_out)
                    std::cout << std::setw(8) << std::setprecision(2) << row.h2->return_x << "x";
                else
                    std::cout << std::setw(9) << "STOP";

                if (h1h2_ratio > 0)
                    std::cout << std::setw(7) << std::setprecision(2) << h1h2_ratio;
                else
                    std::cout << std::setw(8) << "-";

                std::cout << std::setw(7) << row.full->entries
                          << std::setw(5) << ""
                          << std::endl;
            }
        }
    }

    // Best overall configs
    std::cout << "\n\n========================================================" << std::endl;
    std::cout << " TOP 15 BY RISK-ADJUSTED RETURN (FULL PERIOD, NOT STOPPED)" << std::endl;
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
              << std::setw(9) << "Spacing" << std::setw(6) << "Type"
              << std::setw(10) << "Return" << std::setw(8) << "MaxDD%"
              << std::setw(9) << "RiskAdj" << std::setw(7) << "Entry" << std::endl;
    std::cout << std::string(63, '-') << std::endl;

    for (int i = 0; i < std::min(15, (int)full_results.size()); i++) {
        auto* r = full_results[i];
        std::string sp_str = spacing_label(r->spacing_mode, r->spacing_value);
        std::string type_str = (r->spacing_mode == FIXED_DOLLAR) ? "fixed" : "pct";
        std::cout << std::setw(7) << mode_name(r->mode)
                  << std::setw(5) << std::fixed << std::setprecision(0) << r->survive_pct << "%"
                  << std::setw(9) << sp_str
                  << std::setw(6) << type_str
                  << std::setw(9) << std::setprecision(2) << r->return_x << "x"
                  << std::setw(7) << std::setprecision(1) << r->max_dd_pct << "%"
                  << std::setw(9) << std::setprecision(2) << r->sharpe_approx
                  << std::setw(7) << r->entries
                  << std::endl;
    }

    // H1/H2 stable configs only
    std::cout << "\n\n========================================================" << std::endl;
    std::cout << " STABLE CONFIGS (H1/H2 ratio 0.5-2.0, survive > 18%)" << std::endl;
    std::cout << "========================================================" << std::endl;

    struct StableConfig {
        const SweepResult *full, *h1, *h2;
        double ratio;
    };
    std::vector<StableConfig> stable_configs;

    for (const auto* fr : full_results) {
        if (fr->survive_pct <= 18.0) continue;
        const SweepResult *h1 = nullptr, *h2 = nullptr;
        for (const auto& r : g_results) {
            if (r.mode == fr->mode && r.spacing_mode == fr->spacing_mode &&
                r.survive_pct == fr->survive_pct &&
                std::abs(r.spacing_value - fr->spacing_value) < 0.01) {
                if (r.period == "H1" && !r.stopped_out) h1 = &r;
                if (r.period == "H2" && !r.stopped_out) h2 = &r;
            }
        }
        if (h1 && h2 && h2->return_x > 0.1) {
            double ratio = h1->return_x / h2->return_x;
            if (ratio >= 0.5 && ratio <= 2.0) {
                stable_configs.push_back({fr, h1, h2, ratio});
            }
        }
    }

    std::sort(stable_configs.begin(), stable_configs.end(),
              [](const StableConfig& a, const StableConfig& b) {
                  return a.full->sharpe_approx > b.full->sharpe_approx;
              });

    std::cout << std::setw(7) << "Mode" << std::setw(7) << "Surv%"
              << std::setw(9) << "Spacing" << std::setw(6) << "Type"
              << std::setw(10) << "Return" << std::setw(8) << "MaxDD%"
              << std::setw(9) << "RiskAdj" << std::setw(8) << "H1/H2" << std::endl;
    std::cout << std::string(64, '-') << std::endl;

    for (int i = 0; i < std::min(15, (int)stable_configs.size()); i++) {
        auto& sc = stable_configs[i];
        std::string sp_str = spacing_label(sc.full->spacing_mode, sc.full->spacing_value);
        std::string type_str = (sc.full->spacing_mode == FIXED_DOLLAR) ? "fixed" : "pct";
        std::cout << std::setw(7) << mode_name(sc.full->mode)
                  << std::setw(5) << std::fixed << std::setprecision(0) << sc.full->survive_pct << "%"
                  << std::setw(9) << sp_str
                  << std::setw(6) << type_str
                  << std::setw(9) << std::setprecision(2) << sc.full->return_x << "x"
                  << std::setw(7) << std::setprecision(1) << sc.full->max_dd_pct << "%"
                  << std::setw(9) << std::setprecision(2) << sc.full->sharpe_approx
                  << std::setw(7) << std::setprecision(2) << sc.ratio
                  << std::endl;
    }

    std::cout << "\nDone." << std::endl;
    return 0;
}
