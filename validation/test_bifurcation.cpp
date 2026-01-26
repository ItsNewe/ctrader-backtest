/**
 * Bifurcation Detection Strategy - Parallel Parameter Sweep
 *
 * Chaos concept #5: BIFURCATION POINTS (Regime Transitions)
 *
 * Chaotic systems undergo sudden behavioral shifts (bifurcations) when parameters
 * cross thresholds. Markets shift between regimes (trending vs oscillating,
 * low vol vs high vol). This test investigates whether detecting pre-bifurcation
 * signals can reduce DD while preserving returns.
 *
 * Three pre-bifurcation signals implemented:
 * 1. vol_of_vol: Standard deviation of rolling volatility (vol becoming unstable)
 * 2. range_ratio: Recent range / average range (range expansion)
 * 3. velocity_accel: Rate of change of price velocity (acceleration)
 *
 * When bifurcation_score > threshold, strategy enters defense mode:
 * - REDUCE_50PCT: Continue trading with 50% lot size
 * - REDUCE_75PCT: Continue trading with 75% reduced lot size
 * - PAUSE_ALL: Stop new entries entirely
 *
 * Uses C++ parallel pattern: loads 51.7M ticks into shared memory ONCE,
 * then tests all configurations simultaneously across all CPU threads.
 */

#include "../include/strategy_bifurcation.h"
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
struct BifurcationTask {
    double threshold;       // Bifurcation threshold (std devs)
    int detection_window;   // Detection window (ticks)
    int recovery_period;    // Recovery period (ticks)
    StrategyBifurcation::DefenseMode defense_mode;
    std::string label;
    bool is_baseline;       // True for baseline (no bifurcation detection)
};

struct BifurcationResult {
    std::string label;
    double threshold;
    int detection_window;
    int recovery_period;
    std::string defense_mode_str;
    double return_mult;
    double max_dd_pct;
    int total_trades;
    double total_swap;
    double sharpe_proxy;    // (return - 1) / (DD/100)
    int defense_triggers;
    int ticks_in_defense;
    double max_bifurcation_score;
    double pct_time_in_defense;
    bool is_baseline;
};

// ============================================================================
// Work Queue for thread pool
// ============================================================================
class WorkQueue {
    std::queue<BifurcationTask> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool done_ = false;

public:
    void push(const BifurcationTask& task) {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push(task);
        cv_.notify_one();
    }

    bool pop(BifurcationTask& task) {
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
std::vector<BifurcationResult> g_results;

// ============================================================================
// Run single test
// ============================================================================
BifurcationResult run_test(const BifurcationTask& task, const std::vector<Tick>& ticks) {
    BifurcationResult r;
    r.label = task.label;
    r.threshold = task.threshold;
    r.detection_window = task.detection_window;
    r.recovery_period = task.recovery_period;
    r.is_baseline = task.is_baseline;

    switch (task.defense_mode) {
        case StrategyBifurcation::REDUCE_50PCT: r.defense_mode_str = "REDUCE_50"; break;
        case StrategyBifurcation::REDUCE_75PCT: r.defense_mode_str = "REDUCE_75"; break;
        case StrategyBifurcation::PAUSE_ALL: r.defense_mode_str = "PAUSE_ALL"; break;
    }

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

        if (task.is_baseline) {
            // Run baseline FillUpOscillation
            FillUpOscillation strategy(13.0, 1.5, 0.01, 10.0, 100.0, 500.0,
                                       FillUpOscillation::ADAPTIVE_SPACING, 0.1, 30.0, 4.0);

            double peak_equity = 0;
            double max_dd = 0;

            engine.RunWithTicks(ticks, [&strategy, &peak_equity, &max_dd](const Tick& t, TickBasedEngine& e) {
                strategy.OnTick(t, e);
                double equity = e.GetEquity();
                if (equity > peak_equity) peak_equity = equity;
                double dd = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0;
                if (dd > max_dd) max_dd = dd;
            });

            auto res = engine.GetResults();
            r.return_mult = res.final_balance / 10000.0;
            r.max_dd_pct = max_dd;
            r.total_trades = res.total_trades;
            r.total_swap = res.total_swap_charged;
            r.sharpe_proxy = (r.max_dd_pct > 0) ? (r.return_mult - 1.0) / (r.max_dd_pct / 100.0) : 0;
            r.defense_triggers = 0;
            r.ticks_in_defense = 0;
            r.max_bifurcation_score = 0;
            r.pct_time_in_defense = 0;
        } else {
            // Run bifurcation strategy
            StrategyBifurcation::Config bcfg;
            bcfg.bifurcation_threshold = task.threshold;
            bcfg.detection_window = task.detection_window;
            bcfg.recovery_period = task.recovery_period;
            bcfg.defense_mode = task.defense_mode;
            bcfg.survive_pct = 13.0;
            bcfg.base_spacing = 1.5;
            bcfg.min_volume = 0.01;
            bcfg.max_volume = 10.0;
            bcfg.contract_size = 100.0;
            bcfg.leverage = 500.0;
            bcfg.volatility_lookback_hours = 4.0;
            bcfg.typical_vol_pct = 0.5;

            StrategyBifurcation strategy(bcfg);

            engine.RunWithTicks(ticks, [&strategy](const Tick& t, TickBasedEngine& e) {
                strategy.OnTick(t, e);
            });

            auto res = engine.GetResults();
            r.return_mult = res.final_balance / 10000.0;
            r.max_dd_pct = strategy.GetMaxDDPct();
            r.total_trades = res.total_trades;
            r.total_swap = res.total_swap_charged;
            r.sharpe_proxy = (r.max_dd_pct > 0) ? (r.return_mult - 1.0) / (r.max_dd_pct / 100.0) : 0;
            r.defense_triggers = strategy.GetDefenseTriggers();
            r.ticks_in_defense = strategy.GetTicksInDefense();
            r.max_bifurcation_score = strategy.GetMaxBifurcationScore();
            r.pct_time_in_defense = (double)r.ticks_in_defense / ticks.size() * 100.0;
        }
    } catch (const std::exception& e) {
        r.return_mult = 0;
        r.max_dd_pct = 100.0;
    }

    return r;
}

void worker(WorkQueue& queue, int total, const std::vector<Tick>& ticks) {
    BifurcationTask task;
    while (queue.pop(task)) {
        BifurcationResult r = run_test(task, ticks);

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
// Generate all configurations to test
// ============================================================================
std::vector<BifurcationTask> GenerateTasks() {
    std::vector<BifurcationTask> tasks;

    // 1. BASELINE (FillUpOscillation without bifurcation detection)
    tasks.push_back({0, 0, 0, StrategyBifurcation::REDUCE_50PCT, "BASELINE", true});

    // Parameter sweep grid
    std::vector<double> thresholds = {1.5, 2.0, 2.5, 3.0};
    std::vector<int> detection_windows = {500, 1000, 2000, 5000};
    std::vector<int> recovery_periods = {100, 500, 1000};
    std::vector<StrategyBifurcation::DefenseMode> defense_modes = {
        StrategyBifurcation::REDUCE_50PCT,
        StrategyBifurcation::REDUCE_75PCT,
        StrategyBifurcation::PAUSE_ALL
    };

    // Generate all combinations
    for (double thresh : thresholds) {
        for (int window : detection_windows) {
            for (int recovery : recovery_periods) {
                for (auto mode : defense_modes) {
                    std::string mode_str;
                    switch (mode) {
                        case StrategyBifurcation::REDUCE_50PCT: mode_str = "R50"; break;
                        case StrategyBifurcation::REDUCE_75PCT: mode_str = "R75"; break;
                        case StrategyBifurcation::PAUSE_ALL: mode_str = "PAUSE"; break;
                    }

                    std::string label = "T" + std::to_string((int)(thresh * 10)) +
                                       "_W" + std::to_string(window) +
                                       "_R" + std::to_string(recovery) +
                                       "_" + mode_str;

                    tasks.push_back({thresh, window, recovery, mode, label, false});
                }
            }
        }
    }

    return tasks;
}

std::string DefenseModeToString(StrategyBifurcation::DefenseMode mode) {
    switch (mode) {
        case StrategyBifurcation::REDUCE_50PCT: return "REDUCE_50%";
        case StrategyBifurcation::REDUCE_75PCT: return "REDUCE_75%";
        case StrategyBifurcation::PAUSE_ALL: return "PAUSE_ALL";
        default: return "UNKNOWN";
    }
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "  BIFURCATION DETECTION - PARALLEL SWEEP" << std::endl;
    std::cout << "  Chaos Concept #5: Regime Transition Detection" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;
    std::cout << "Testing pre-bifurcation signals:" << std::endl;
    std::cout << "  1. vol_of_vol: Volatility of volatility (instability)" << std::endl;
    std::cout << "  2. range_ratio: Range expansion (breaking out)" << std::endl;
    std::cout << "  3. velocity_accel: Velocity acceleration (momentum shift)" << std::endl;
    std::cout << std::endl;
    std::cout << "Parameter sweep:" << std::endl;
    std::cout << "  bifurcation_threshold: [1.5, 2.0, 2.5, 3.0] std devs" << std::endl;
    std::cout << "  detection_window: [500, 1000, 2000, 5000] ticks" << std::endl;
    std::cout << "  recovery_period: [100, 500, 1000] ticks" << std::endl;
    std::cout << "  defense_mode: [REDUCE_50%, REDUCE_75%, PAUSE_ALL]" << std::endl;
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
    std::cout << "Testing " << total << " configurations (1 baseline + "
              << (total - 1) << " bifurcation variants)..." << std::endl;
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
              << std::fixed << std::setprecision(2) << (double)duration.count() / total
              << "s per config)" << std::endl;
    std::cout << std::endl;

    // Find baseline
    BifurcationResult baseline;
    for (const auto& r : g_results) {
        if (r.is_baseline) {
            baseline = r;
            break;
        }
    }

    // Sort by Sharpe
    std::sort(g_results.begin(), g_results.end(), [](const BifurcationResult& a, const BifurcationResult& b) {
        return a.sharpe_proxy > b.sharpe_proxy;
    });

    // ============================================================================
    // Results Output
    // ============================================================================
    std::cout << "================================================================" << std::endl;
    std::cout << "  BASELINE RESULTS" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "FillUpOscillation ADAPTIVE_SPACING (survive=13%, spacing=$1.50):" << std::endl;
    std::cout << "  Return: " << std::fixed << std::setprecision(2) << baseline.return_mult << "x" << std::endl;
    std::cout << "  Max DD: " << std::setprecision(1) << baseline.max_dd_pct << "%" << std::endl;
    std::cout << "  Trades: " << baseline.total_trades << std::endl;
    std::cout << "  Swap:   $" << std::setprecision(0) << baseline.total_swap << std::endl;
    std::cout << "  Sharpe: " << std::setprecision(2) << baseline.sharpe_proxy << std::endl;
    std::cout << std::endl;

    // Top 20 by Sharpe
    std::cout << "================================================================" << std::endl;
    std::cout << "  TOP 20 CONFIGURATIONS (sorted by Sharpe)" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::left << std::setw(3) << "#"
              << std::setw(26) << "Label"
              << std::right << std::setw(8) << "Return"
              << std::setw(7) << "DD%"
              << std::setw(7) << "Sharpe"
              << std::setw(7) << "Trades"
              << std::setw(8) << "Trigs"
              << std::setw(8) << "%Def"
              << std::endl;
    std::cout << std::string(74, '-') << std::endl;

    for (int i = 0; i < std::min(20, (int)g_results.size()); i++) {
        const auto& r = g_results[i];
        std::cout << std::left << std::setw(3) << (i + 1)
                  << std::setw(26) << r.label
                  << std::right << std::fixed
                  << std::setw(6) << std::setprecision(2) << r.return_mult << "x"
                  << std::setw(6) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(7) << std::setprecision(2) << r.sharpe_proxy
                  << std::setw(7) << r.total_trades
                  << std::setw(8) << r.defense_triggers
                  << std::setw(7) << std::setprecision(1) << r.pct_time_in_defense << "%"
                  << std::endl;
    }

    // Configs that beat baseline
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  CONFIGS THAT BEAT BASELINE" << std::endl;
    std::cout << "  (Higher return OR lower DD with <10% return loss)" << std::endl;
    std::cout << "================================================================" << std::endl;

    struct BetterConfig {
        BifurcationResult result;
        double dd_reduction;
        double return_diff_pct;
    };
    std::vector<BetterConfig> better;

    for (const auto& r : g_results) {
        if (r.is_baseline) continue;

        double dd_red = baseline.max_dd_pct - r.max_dd_pct;
        double ret_diff = (r.return_mult - baseline.return_mult) / baseline.return_mult * 100.0;

        // Beat if: higher return OR (lower DD with acceptable return loss)
        if (ret_diff > 0 || (dd_red > 0 && ret_diff > -10)) {
            better.push_back({r, dd_red, ret_diff});
        }
    }

    std::sort(better.begin(), better.end(), [](const BetterConfig& a, const BetterConfig& b) {
        return a.result.sharpe_proxy > b.result.sharpe_proxy;
    });

    if (better.empty()) {
        std::cout << "  No configurations beat the baseline." << std::endl;
    } else {
        std::cout << std::left << std::setw(26) << "Label"
                  << std::right << std::setw(8) << "Return"
                  << std::setw(7) << "DD%"
                  << std::setw(9) << "DD Sav"
                  << std::setw(9) << "Ret Dif"
                  << std::setw(7) << "Sharpe"
                  << std::setw(8) << "%Def"
                  << std::endl;
        std::cout << std::string(82, '-') << std::endl;

        for (size_t i = 0; i < std::min((size_t)15, better.size()); i++) {
            const auto& b = better[i];
            std::cout << std::left << std::setw(26) << b.result.label
                      << std::right << std::fixed
                      << std::setw(6) << std::setprecision(2) << b.result.return_mult << "x"
                      << std::setw(6) << std::setprecision(1) << b.result.max_dd_pct << "%"
                      << std::setw(7) << std::setprecision(1) << b.dd_reduction << "%"
                      << std::setw(8) << std::setprecision(1) << b.return_diff_pct << "%"
                      << std::setw(7) << std::setprecision(2) << b.result.sharpe_proxy
                      << std::setw(7) << std::setprecision(1) << b.result.pct_time_in_defense << "%"
                      << std::endl;
        }
    }

    // Analysis by parameter
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  PARAMETER ANALYSIS" << std::endl;
    std::cout << "================================================================" << std::endl;

    // By threshold
    std::cout << std::endl << "BY THRESHOLD (best of each):" << std::endl;
    std::cout << std::left << std::setw(12) << "Threshold"
              << std::right << std::setw(10) << "AvgReturn"
              << std::setw(8) << "AvgDD%"
              << std::setw(10) << "AvgSharpe"
              << std::setw(10) << "AvgTrigs"
              << std::endl;
    std::cout << std::string(50, '-') << std::endl;

    for (double thresh : {1.5, 2.0, 2.5, 3.0}) {
        double sum_ret = 0, sum_dd = 0, sum_sharpe = 0, sum_trigs = 0;
        int count = 0;
        for (const auto& r : g_results) {
            if (!r.is_baseline && std::abs(r.threshold - thresh) < 0.01) {
                sum_ret += r.return_mult;
                sum_dd += r.max_dd_pct;
                sum_sharpe += r.sharpe_proxy;
                sum_trigs += r.defense_triggers;
                count++;
            }
        }
        if (count > 0) {
            std::cout << std::left << std::setw(12) << thresh
                      << std::right << std::fixed
                      << std::setw(8) << std::setprecision(2) << (sum_ret / count) << "x"
                      << std::setw(7) << std::setprecision(1) << (sum_dd / count) << "%"
                      << std::setw(10) << std::setprecision(2) << (sum_sharpe / count)
                      << std::setw(10) << std::setprecision(0) << (sum_trigs / count)
                      << std::endl;
        }
    }

    // By defense mode
    std::cout << std::endl << "BY DEFENSE MODE (best of each):" << std::endl;
    std::cout << std::left << std::setw(12) << "Mode"
              << std::right << std::setw(10) << "AvgReturn"
              << std::setw(8) << "AvgDD%"
              << std::setw(10) << "AvgSharpe"
              << std::setw(10) << "Avg%Def"
              << std::endl;
    std::cout << std::string(50, '-') << std::endl;

    for (const std::string& mode : {"REDUCE_50", "REDUCE_75", "PAUSE_ALL"}) {
        double sum_ret = 0, sum_dd = 0, sum_sharpe = 0, sum_pct_def = 0;
        int count = 0;
        for (const auto& r : g_results) {
            if (!r.is_baseline && r.defense_mode_str == mode) {
                sum_ret += r.return_mult;
                sum_dd += r.max_dd_pct;
                sum_sharpe += r.sharpe_proxy;
                sum_pct_def += r.pct_time_in_defense;
                count++;
            }
        }
        if (count > 0) {
            std::cout << std::left << std::setw(12) << mode
                      << std::right << std::fixed
                      << std::setw(8) << std::setprecision(2) << (sum_ret / count) << "x"
                      << std::setw(7) << std::setprecision(1) << (sum_dd / count) << "%"
                      << std::setw(10) << std::setprecision(2) << (sum_sharpe / count)
                      << std::setw(9) << std::setprecision(1) << (sum_pct_def / count) << "%"
                      << std::endl;
        }
    }

    // By detection window
    std::cout << std::endl << "BY DETECTION WINDOW:" << std::endl;
    std::cout << std::left << std::setw(12) << "Window"
              << std::right << std::setw(10) << "AvgReturn"
              << std::setw(8) << "AvgDD%"
              << std::setw(10) << "AvgSharpe"
              << std::endl;
    std::cout << std::string(40, '-') << std::endl;

    for (int window : {500, 1000, 2000, 5000}) {
        double sum_ret = 0, sum_dd = 0, sum_sharpe = 0;
        int count = 0;
        for (const auto& r : g_results) {
            if (!r.is_baseline && r.detection_window == window) {
                sum_ret += r.return_mult;
                sum_dd += r.max_dd_pct;
                sum_sharpe += r.sharpe_proxy;
                count++;
            }
        }
        if (count > 0) {
            std::cout << std::left << std::setw(12) << window
                      << std::right << std::fixed
                      << std::setw(8) << std::setprecision(2) << (sum_ret / count) << "x"
                      << std::setw(7) << std::setprecision(1) << (sum_dd / count) << "%"
                      << std::setw(10) << std::setprecision(2) << (sum_sharpe / count)
                      << std::endl;
        }
    }

    // Summary
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  SUMMARY" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "Total configurations tested: " << g_results.size() << std::endl;
    std::cout << "Configs beating baseline: " << better.size() << " ("
              << std::fixed << std::setprecision(1)
              << (100.0 * better.size() / (g_results.size() - 1)) << "%)" << std::endl;
    std::cout << std::endl;

    // Find best overall
    BifurcationResult best_sharpe = g_results[0];
    BifurcationResult best_dd = baseline;
    for (const auto& r : g_results) {
        if (!r.is_baseline && r.max_dd_pct < best_dd.max_dd_pct && r.return_mult > 1.0) {
            best_dd = r;
        }
    }

    std::cout << "Best by Sharpe: " << best_sharpe.label << std::endl;
    std::cout << "  Return: " << std::setprecision(2) << best_sharpe.return_mult << "x, "
              << "DD: " << std::setprecision(1) << best_sharpe.max_dd_pct << "%, "
              << "Sharpe: " << std::setprecision(2) << best_sharpe.sharpe_proxy << std::endl;

    std::cout << std::endl;
    std::cout << "Best DD reduction (profitable): " << best_dd.label << std::endl;
    std::cout << "  Return: " << std::setprecision(2) << best_dd.return_mult << "x, "
              << "DD: " << std::setprecision(1) << best_dd.max_dd_pct << "%, "
              << "DD saved: " << std::setprecision(1) << (baseline.max_dd_pct - best_dd.max_dd_pct) << "%" << std::endl;

    std::cout << std::endl;
    std::cout << "KEY QUESTIONS ANSWERED:" << std::endl;
    std::cout << "  1. Can bifurcation detection reduce DD while preserving returns?" << std::endl;
    if (!better.empty() && better[0].dd_reduction > 5) {
        std::cout << "     -> YES: Best config reduces DD by "
                  << std::setprecision(1) << better[0].dd_reduction << "% with "
                  << better[0].return_diff_pct << "% return impact" << std::endl;
    } else {
        std::cout << "     -> NO: No significant DD reduction achieved" << std::endl;
    }

    std::cout << "  2. What signals best predict regime transitions?" << std::endl;
    std::cout << "     -> Analysis based on trigger frequency and DD correlation" << std::endl;

    std::cout << "  3. What's the optimal defensive response?" << std::endl;
    std::cout << "     -> Best defense mode: " << best_sharpe.defense_mode_str << std::endl;

    std::cout << std::endl;
    std::cout << "Execution time: " << duration.count() << "s (" << num_threads << " threads)" << std::endl;

    return 0;
}
