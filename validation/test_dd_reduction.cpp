/**
 * DD Reduction Parallel Parameter Sweep
 *
 * Loads tick data ONCE into shared memory, then tests multiple DD-reduction
 * configurations in parallel using std::thread + WorkQueue.
 *
 * DD-reduction mechanisms tested:
 *   1. DD-based entry pause (stop new entries when DD > threshold)
 *   2. Max concurrent positions cap
 *   3. Equity hard stop (close all positions above DD threshold)
 *   4. Combined approaches
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <cmath>

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

        Tick tick;
        std::stringstream ss(line);
        std::string datetime_str, bid_str, ask_str;

        std::getline(ss, datetime_str, '\t');
        std::getline(ss, bid_str, '\t');
        std::getline(ss, ask_str, '\t');

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
// Task and Result structures
// ============================================================================
struct DDTask {
    double dd_pause;        // Pause new entries when DD exceeds this % (0=disabled)
    int max_positions;      // Max concurrent positions (0=unlimited)
    double equity_stop;     // Close all if DD exceeds this % (0=disabled)
    double resume_below;    // Resume when DD drops below this % (0=auto: dd_pause*0.5)
    std::string label;
};

struct DDResult {
    std::string label;
    double dd_pause;
    int max_positions;
    double equity_stop;
    double resume_below;
    double return_mult;
    double max_dd_pct;
    int total_trades;
    double total_swap;
    double sharpe_proxy;    // (return - 1) / (DD/100)
    int pause_count;
    bool equity_stopped;
};

// ============================================================================
// DD-Reduction Wrapper Strategy
// ============================================================================
class DDReductionStrategy {
public:
    DDReductionStrategy(const DDTask& task)
        : task_(task),
          base_strategy_(13.0, 1.5, 0.01, 10.0, 100.0, 500.0,
                         FillUpOscillation::ADAPTIVE_SPACING, 0.1, 30.0, 4.0),
          peak_equity_(0.0),
          paused_(false),
          equity_stopped_(false),
          pause_count_(0),
          max_dd_pct_(0.0) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        double equity = engine.GetEquity();
        if (peak_equity_ == 0.0) peak_equity_ = equity;
        if (equity > peak_equity_) peak_equity_ = equity;

        double dd_pct = (peak_equity_ > 0) ? (peak_equity_ - equity) / peak_equity_ * 100.0 : 0.0;
        if (dd_pct > max_dd_pct_) max_dd_pct_ = dd_pct;

        // Mechanism 3: Equity hard stop
        if (task_.equity_stop > 0 && dd_pct >= task_.equity_stop && !equity_stopped_) {
            std::vector<Trade*> to_close(engine.GetOpenPositions().begin(),
                                         engine.GetOpenPositions().end());
            for (Trade* trade : to_close) {
                engine.ClosePosition(trade, "DD_EQUITY_STOP");
            }
            equity_stopped_ = true;
            return;
        }
        if (equity_stopped_) return;

        // Mechanism 1: DD-based entry pause
        if (task_.dd_pause > 0) {
            double resume_level = (task_.resume_below > 0)
                ? task_.resume_below
                : task_.dd_pause * 0.5;

            if (dd_pct >= task_.dd_pause && !paused_) {
                paused_ = true;
                pause_count_++;
            } else if (dd_pct < resume_level && paused_) {
                paused_ = false;
            }
        }

        // Mechanism 2: Max positions cap
        bool positions_capped = false;
        if (task_.max_positions > 0) {
            int open = (int)engine.GetOpenPositions().size();
            if (open >= task_.max_positions) {
                positions_capped = true;
            }
        }

        // If paused or capped, don't call base strategy (existing TPs still execute via engine)
        if (paused_ || positions_capped) {
            return;
        }

        // Normal operation: call base strategy
        base_strategy_.OnTick(tick, engine);
    }

    double GetMaxDDPct() const { return max_dd_pct_; }
    int GetPauseCount() const { return pause_count_; }
    bool IsEquityStopped() const { return equity_stopped_; }

private:
    DDTask task_;
    FillUpOscillation base_strategy_;
    double peak_equity_;
    bool paused_;
    bool equity_stopped_;
    int pause_count_;
    double max_dd_pct_;
};

// ============================================================================
// Work Queue for thread pool
// ============================================================================
class WorkQueue {
    std::queue<DDTask> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool done_ = false;

public:
    void push(const DDTask& task) {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push(task);
        cv_.notify_one();
    }

    bool pop(DDTask& task) {
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

// ============================================================================
// Global state
// ============================================================================
std::atomic<int> g_completed{0};
std::mutex g_results_mutex;
std::mutex g_output_mutex;
std::vector<DDResult> g_results;

// ============================================================================
// Run single test with shared tick data
// ============================================================================
DDResult run_test(const DDTask& task, const std::vector<Tick>& ticks) {
    DDResult r;
    r.label = task.label;
    r.dd_pause = task.dd_pause;
    r.max_positions = task.max_positions;
    r.equity_stop = task.equity_stop;
    r.resume_below = task.resume_below;
    r.return_mult = 0;
    r.max_dd_pct = 0;
    r.total_trades = 0;
    r.total_swap = 0;
    r.sharpe_proxy = 0;
    r.pause_count = 0;
    r.equity_stopped = false;

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

    TickDataConfig tc;
    tc.file_path = "";
    tc.load_all_into_memory = false;
    cfg.tick_data_config = tc;

    try {
        TickBasedEngine engine(cfg);
        DDReductionStrategy strategy(task);

        engine.RunWithTicks(ticks, [&strategy](const Tick& t, TickBasedEngine& e) {
            strategy.OnTick(t, e);
        });

        auto res = engine.GetResults();
        r.return_mult = res.final_balance / 10000.0;
        r.max_dd_pct = strategy.GetMaxDDPct();
        r.total_trades = res.total_trades;
        r.total_swap = res.total_swap_charged;
        r.sharpe_proxy = (r.max_dd_pct > 0) ? (r.return_mult - 1.0) / (r.max_dd_pct / 100.0) : 0;
        r.pause_count = strategy.GetPauseCount();
        r.equity_stopped = strategy.IsEquityStopped();
    } catch (const std::exception& e) {
        r.equity_stopped = true;
        r.max_dd_pct = 100.0;
    }

    return r;
}

void worker(WorkQueue& queue, int total, const std::vector<Tick>& ticks) {
    DDTask task;
    while (queue.pop(task)) {
        DDResult r = run_test(task, ticks);

        {
            std::lock_guard<std::mutex> lock(g_results_mutex);
            g_results.push_back(r);
        }

        int done = ++g_completed;
        if (done % 5 == 0 || done == total) {
            std::lock_guard<std::mutex> lock(g_output_mutex);
            std::cout << "\rProgress: " << done << "/" << total
                      << " (" << std::fixed << std::setprecision(1)
                      << (100.0 * done / total) << "%)" << std::flush;
        }
    }
}

// ============================================================================
// Generate all DD-reduction configurations to test
// ============================================================================
std::vector<DDTask> GenerateTasks() {
    std::vector<DDTask> tasks;

    // 1. BASELINE (no DD reduction)
    tasks.push_back({0, 0, 0, 0, "BASELINE"});

    // 2. DD Pause sweep (single mechanism)
    for (double pause : {15.0, 20.0, 25.0, 30.0, 35.0, 40.0, 45.0, 50.0, 55.0, 60.0}) {
        tasks.push_back({pause, 0, 0, pause * 0.5,
            "PAUSE_" + std::to_string((int)pause)});
    }

    // 3. DD Pause with different resume levels
    for (double pause : {25.0, 30.0, 35.0, 40.0, 50.0}) {
        for (double resume_pct : {0.3, 0.5, 0.7, 0.9}) {
            double resume = pause * resume_pct;
            tasks.push_back({pause, 0, 0, resume,
                "PAUSE_" + std::to_string((int)pause) + "_RES_" + std::to_string((int)resume)});
        }
    }

    // 4. Max positions sweep (single mechanism)
    for (int maxp : {3, 5, 8, 10, 12, 15, 20, 25, 30, 40, 50, 75, 100}) {
        tasks.push_back({0, maxp, 0, 0,
            "MAXPOS_" + std::to_string(maxp)});
    }

    // 5. Equity stop sweep (single mechanism)
    for (double eq : {25.0, 30.0, 35.0, 40.0, 45.0, 50.0, 55.0, 60.0, 65.0, 70.0}) {
        tasks.push_back({0, 0, eq, 0,
            "EQSTOP_" + std::to_string((int)eq)});
    }

    // 6. Combined: DD pause + max positions
    for (double pause : {25.0, 30.0, 35.0, 40.0, 50.0}) {
        for (int maxp : {5, 10, 15, 20, 30}) {
            tasks.push_back({pause, maxp, 0, pause * 0.5,
                "P" + std::to_string((int)pause) + "_M" + std::to_string(maxp)});
        }
    }

    // 7. Combined: DD pause + equity stop
    for (double pause : {25.0, 30.0, 35.0, 40.0}) {
        for (double eq : {45.0, 50.0, 55.0, 60.0, 65.0}) {
            if (eq > pause) {
                tasks.push_back({pause, 0, eq, pause * 0.5,
                    "P" + std::to_string((int)pause) + "_EQ" + std::to_string((int)eq)});
            }
        }
    }

    // 8. Combined: Max positions + equity stop
    for (int maxp : {10, 15, 20, 30}) {
        for (double eq : {40.0, 50.0, 60.0}) {
            tasks.push_back({0, maxp, eq, 0,
                "M" + std::to_string(maxp) + "_EQ" + std::to_string((int)eq)});
        }
    }

    // 9. Triple combined: DD pause + max positions + equity stop
    for (double pause : {25.0, 30.0, 35.0, 40.0}) {
        for (int maxp : {10, 15, 20}) {
            for (double eq : {50.0, 60.0}) {
                if (eq > pause) {
                    tasks.push_back({pause, maxp, eq, pause * 0.5,
                        "P" + std::to_string((int)pause) + "_M" + std::to_string(maxp) + "_EQ" + std::to_string((int)eq)});
                }
            }
        }
    }

    return tasks;
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "  DD REDUCTION PARALLEL SWEEP" << std::endl;
    std::cout << "  Strategy: FillUpOscillation ADAPTIVE_SPACING (survive=13%, spacing=$1.50)" << std::endl;
    std::cout << "  Data: XAUUSD 2025 (full year)" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;

    // Step 1: Load tick data ONCE
    std::string tick_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    LoadTickDataOnce(tick_path);

    // Step 2: Generate tasks
    auto tasks = GenerateTasks();
    int total = (int)tasks.size();

    // Step 3: Setup thread pool
    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;
    std::cout << "Using " << num_threads << " worker threads" << std::endl;
    std::cout << "Testing " << total << " configurations..." << std::endl;
    std::cout << std::endl;

    // Step 4: Fill work queue
    WorkQueue queue;
    for (const auto& task : tasks) {
        queue.push(task);
    }

    // Step 5: Launch workers
    auto start = std::chrono::high_resolution_clock::now();

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

    std::cout << std::endl << std::endl;
    std::cout << "Completed in " << duration.count() << " seconds ("
              << std::fixed << std::setprecision(1) << (double)duration.count() / total
              << "s per config, " << num_threads << " threads)" << std::endl;
    std::cout << std::endl;

    // Step 6: Sort by sharpe_proxy
    std::sort(g_results.begin(), g_results.end(), [](const DDResult& a, const DDResult& b) {
        return a.sharpe_proxy > b.sharpe_proxy;
    });

    // Step 7: Print results
    std::cout << "================================================================" << std::endl;
    std::cout << "  TOP 30 CONFIGURATIONS (sorted by Sharpe proxy = (Return-1)/DD)" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::left << std::setw(4) << "#"
              << std::setw(28) << "Label"
              << std::right << std::setw(9) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(8) << "Trades"
              << std::setw(9) << "Swap$"
              << std::setw(8) << "Sharpe"
              << std::setw(7) << "Pause"
              << std::setw(8) << "Stopped"
              << std::endl;
    std::cout << std::string(90, '-') << std::endl;

    for (int i = 0; i < std::min(30, (int)g_results.size()); i++) {
        const auto& r = g_results[i];
        std::cout << std::left << std::setw(4) << (i + 1)
                  << std::setw(28) << r.label
                  << std::right << std::fixed
                  << std::setw(7) << std::setprecision(2) << r.return_mult << "x"
                  << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(8) << r.total_trades
                  << std::setw(8) << std::setprecision(0) << r.total_swap
                  << std::setw(8) << std::setprecision(2) << r.sharpe_proxy
                  << std::setw(7) << r.pause_count
                  << std::setw(8) << (r.equity_stopped ? "YES" : "no")
                  << std::endl;
    }

    // Find and print baseline
    std::cout << std::string(90, '-') << std::endl;
    for (const auto& r : g_results) {
        if (r.label == "BASELINE") {
            int rank = 0;
            for (size_t i = 0; i < g_results.size(); i++) {
                if (g_results[i].label == "BASELINE") { rank = (int)i + 1; break; }
            }
            std::cout << std::left << std::setw(4) << "BASE"
                      << std::setw(28) << "BASELINE (no reduction)"
                      << std::right << std::fixed
                      << std::setw(7) << std::setprecision(2) << r.return_mult << "x"
                      << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                      << std::setw(8) << r.total_trades
                      << std::setw(8) << std::setprecision(0) << r.total_swap
                      << std::setw(8) << std::setprecision(2) << r.sharpe_proxy
                      << std::setw(7) << r.pause_count
                      << std::setw(8) << (r.equity_stopped ? "YES" : "no")
                      << std::endl;
            std::cout << "  (Baseline rank: #" << rank << " of " << g_results.size() << ")" << std::endl;
            break;
        }
    }

    // DD reduction analysis: configs that reduce DD with acceptable return loss
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  DD REDUCTION ANALYSIS" << std::endl;
    std::cout << "  Configs reducing DD by >10% with <25% return loss" << std::endl;
    std::cout << "================================================================" << std::endl;

    // Find baseline metrics
    DDResult baseline;
    for (const auto& r : g_results) {
        if (r.label == "BASELINE") { baseline = r; break; }
    }

    struct GoodConfig {
        DDResult result;
        double dd_reduction;
        double return_loss_pct;
    };
    std::vector<GoodConfig> good;

    for (const auto& r : g_results) {
        if (r.label == "BASELINE") continue;
        double dd_red = baseline.max_dd_pct - r.max_dd_pct;
        double ret_loss = (baseline.return_mult - r.return_mult) / baseline.return_mult * 100.0;

        if (dd_red > 10 && ret_loss < 25 && r.return_mult > 1.0) {
            good.push_back({r, dd_red, ret_loss});
        }
    }

    std::sort(good.begin(), good.end(), [](const GoodConfig& a, const GoodConfig& b) {
        return a.dd_reduction > b.dd_reduction;
    });

    if (!good.empty()) {
        std::cout << std::left << std::setw(28) << "Label"
                  << std::right << std::setw(9) << "Return"
                  << std::setw(8) << "MaxDD%"
                  << std::setw(10) << "DD Saved"
                  << std::setw(10) << "Ret Loss"
                  << std::setw(8) << "Sharpe"
                  << std::endl;
        std::cout << std::string(85, '-') << std::endl;

        for (size_t i = 0; i < std::min((size_t)20, good.size()); i++) {
            const auto& g2 = good[i];
            std::cout << std::left << std::setw(28) << g2.result.label
                      << std::right << std::fixed
                      << std::setw(7) << std::setprecision(2) << g2.result.return_mult << "x"
                      << std::setw(7) << std::setprecision(1) << g2.result.max_dd_pct << "%"
                      << std::setw(8) << std::setprecision(1) << g2.dd_reduction << "%"
                      << std::setw(9) << std::setprecision(1) << g2.return_loss_pct << "%"
                      << std::setw(8) << std::setprecision(2) << g2.result.sharpe_proxy
                      << std::endl;
        }
    } else {
        std::cout << "  No configs found meeting criteria." << std::endl;
    }

    // Category analysis
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  BY MECHANISM TYPE (best of each category)" << std::endl;
    std::cout << "================================================================" << std::endl;

    // Find best by category
    auto find_best = [&](const std::string& prefix) -> const DDResult* {
        const DDResult* best = nullptr;
        for (const auto& r : g_results) {
            if (r.label.find(prefix) == 0 || r.label == prefix) {
                if (!best || r.sharpe_proxy > best->sharpe_proxy) {
                    best = &r;
                }
            }
        }
        return best;
    };

    std::vector<std::pair<std::string, std::string>> categories = {
        {"BASELINE", "No reduction"},
        {"PAUSE_", "DD Pause only"},
        {"MAXPOS_", "Max Positions only"},
        {"EQSTOP_", "Equity Stop only"},
        {"P", "Combined (2-3 mechanisms)"},
        {"M", "MaxPos + EqStop"},
    };

    std::cout << std::left << std::setw(22) << "Category"
              << std::setw(28) << "Best Config"
              << std::right << std::setw(9) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(8) << "Sharpe"
              << std::endl;
    std::cout << std::string(85, '-') << std::endl;

    for (const auto& [prefix, desc] : categories) {
        const DDResult* best = find_best(prefix);
        if (best) {
            std::cout << std::left << std::setw(22) << desc
                      << std::setw(28) << best->label
                      << std::right << std::fixed
                      << std::setw(7) << std::setprecision(2) << best->return_mult << "x"
                      << std::setw(7) << std::setprecision(1) << best->max_dd_pct << "%"
                      << std::setw(8) << std::setprecision(2) << best->sharpe_proxy
                      << std::endl;
        }
    }

    std::cout << std::endl;
    std::cout << "Total configurations tested: " << g_results.size() << std::endl;
    std::cout << "Execution time: " << duration.count() << "s (" << num_threads << " threads)" << std::endl;

    return 0;
}
