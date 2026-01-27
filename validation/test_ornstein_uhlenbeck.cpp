/**
 * Ornstein-Uhlenbeck Mean-Reversion Strategy Test
 *
 * Tests whether OU-based dynamic spacing adjustment improves returns/reduces DD
 * compared to fixed spacing baseline.
 *
 * The OU process: dX = theta(mu - X)dt + sigma*dW
 * - theta: mean-reversion speed (higher = faster reversion = tighter spacing)
 * - mu: long-term mean
 * - sigma: volatility of fluctuations
 *
 * Parallel sweep pattern: load ticks ONCE, test multiple configs in parallel.
 */

#include "../include/strategy_ornstein_uhlenbeck.h"
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
std::vector<Tick> g_shared_ticks_2025;
std::vector<Tick> g_shared_ticks_2024;

void LoadTickData(const std::string& path, std::vector<Tick>& ticks, const std::string& label) {
    std::cout << "Loading " << label << " tick data..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cout << "WARNING: Cannot open " << path << " - skipping" << std::endl;
        return;
    }

    std::string line;
    std::getline(file, line);  // Skip header

    ticks.reserve(52000000);

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

        ticks.push_back(tick);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

    std::cout << "  Loaded " << ticks.size() << " ticks in " << duration.count() << "s" << std::endl;
}

// ============================================================================
// Task and Result structures
// ============================================================================
struct OUTask {
    int estimation_window;
    double theta_scaling;
    double min_theta_mult;
    double max_theta_mult;
    double base_spacing;
    double survive_pct;
    bool is_baseline;
    bool use_2024;
    std::string label;
};

struct OUResult {
    std::string label;
    int estimation_window;
    double theta_scaling;
    double min_theta_mult;
    double max_theta_mult;
    bool is_baseline;
    bool is_2024;

    // Performance
    double return_mult;
    double max_dd_pct;
    int total_trades;
    double total_swap;
    double sharpe_proxy;

    // OU statistics
    double avg_theta;
    double min_theta;
    double max_theta;
    int theta_updates;
    int spacing_changes;
    double final_spacing;

    // Status
    bool stopped_out;
};

// ============================================================================
// Baseline strategy (fixed spacing, using FillUpOscillation BASELINE mode)
// ============================================================================
struct BaselineResult {
    double return_mult;
    double max_dd_pct;
    int total_trades;
    double total_swap;
};

BaselineResult RunBaseline(const std::vector<Tick>& ticks, double survive, double spacing,
                           const std::string& start_date, const std::string& end_date) {
    BaselineResult r = {0, 0, 0, 0};

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
    cfg.start_date = start_date;
    cfg.end_date = end_date;
    cfg.verbose = false;

    TickDataConfig tc;
    tc.file_path = "";
    tc.load_all_into_memory = false;
    cfg.tick_data_config = tc;

    try {
        TickBasedEngine engine(cfg);
        FillUpOscillation strategy(survive, spacing, 0.01, 10.0, 100.0, 500.0,
                                   FillUpOscillation::BASELINE);

        double peak_equity = 10000.0;
        double max_dd = 0.0;

        engine.RunWithTicks(ticks, [&](const Tick& t, TickBasedEngine& e) {
            strategy.OnTick(t, e);
            double eq = e.GetEquity();
            if (eq > peak_equity) peak_equity = eq;
            double dd = (peak_equity - eq) / peak_equity * 100.0;
            if (dd > max_dd) max_dd = dd;
        });

        auto res = engine.GetResults();
        r.return_mult = res.final_balance / 10000.0;
        r.max_dd_pct = max_dd;
        r.total_trades = res.total_trades;
        r.total_swap = res.total_swap_charged;
    } catch (...) {
        r.return_mult = 0;
        r.max_dd_pct = 100;
    }

    return r;
}

// ============================================================================
// Work Queue
// ============================================================================
class WorkQueue {
    std::queue<OUTask> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool done_ = false;

public:
    void push(const OUTask& task) {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push(task);
        cv_.notify_one();
    }

    bool pop(OUTask& task) {
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
std::vector<OUResult> g_results;

// ============================================================================
// Run single OU test
// ============================================================================
OUResult RunOUTest(const OUTask& task, const std::vector<Tick>& ticks,
                   const std::string& start_date, const std::string& end_date) {
    OUResult r;
    r.label = task.label;
    r.estimation_window = task.estimation_window;
    r.theta_scaling = task.theta_scaling;
    r.min_theta_mult = task.min_theta_mult;
    r.max_theta_mult = task.max_theta_mult;
    r.is_baseline = task.is_baseline;
    r.is_2024 = task.use_2024;
    r.return_mult = 0;
    r.max_dd_pct = 0;
    r.total_trades = 0;
    r.total_swap = 0;
    r.sharpe_proxy = 0;
    r.avg_theta = 0;
    r.min_theta = 0;
    r.max_theta = 0;
    r.theta_updates = 0;
    r.spacing_changes = 0;
    r.final_spacing = task.base_spacing;
    r.stopped_out = false;

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
    cfg.start_date = start_date;
    cfg.end_date = end_date;
    cfg.verbose = false;

    TickDataConfig tc;
    tc.file_path = "";
    tc.load_all_into_memory = false;
    cfg.tick_data_config = tc;

    try {
        TickBasedEngine engine(cfg);

        double peak_equity = 10000.0;
        double max_dd = 0.0;

        if (task.is_baseline) {
            // Run baseline (fixed or adaptive spacing depending on estimation_window)
            FillUpOscillation::Mode mode = (task.estimation_window == -1)
                ? FillUpOscillation::ADAPTIVE_SPACING
                : FillUpOscillation::BASELINE;

            FillUpOscillation strategy(task.survive_pct, task.base_spacing, 0.01, 10.0, 100.0, 500.0,
                                       mode, 0.1, 30.0, 4.0);

            engine.RunWithTicks(ticks, [&](const Tick& t, TickBasedEngine& e) {
                strategy.OnTick(t, e);
                double eq = e.GetEquity();
                if (eq > peak_equity) peak_equity = eq;
                double dd = (peak_equity - eq) / peak_equity * 100.0;
                if (dd > max_dd) max_dd = dd;
            });

            r.final_spacing = strategy.GetCurrentSpacing();
        } else {
            // Run OU strategy
            StrategyOrnsteinUhlenbeck::OUConfig ou_cfg;
            ou_cfg.estimation_window = task.estimation_window;
            ou_cfg.theta_scaling = task.theta_scaling;
            ou_cfg.min_theta_mult = task.min_theta_mult;
            ou_cfg.max_theta_mult = task.max_theta_mult;
            ou_cfg.base_spacing = task.base_spacing;
            ou_cfg.survive_pct = task.survive_pct;
            ou_cfg.typical_theta = 0.0;  // Will use running average

            StrategyOrnsteinUhlenbeck strategy(ou_cfg);

            engine.RunWithTicks(ticks, [&](const Tick& t, TickBasedEngine& e) {
                strategy.OnTick(t, e);
                double eq = e.GetEquity();
                if (eq > peak_equity) peak_equity = eq;
                double dd = (peak_equity - eq) / peak_equity * 100.0;
                if (dd > max_dd) max_dd = dd;
            });

            const auto& stats = strategy.GetStats();
            r.avg_theta = stats.avg_theta;
            r.min_theta = stats.min_theta_seen;
            r.max_theta = stats.max_theta_seen;
            r.theta_updates = stats.theta_updates;
            r.spacing_changes = stats.spacing_changes;
            r.final_spacing = stats.current_spacing;
        }

        auto res = engine.GetResults();
        r.return_mult = res.final_balance / 10000.0;
        r.max_dd_pct = max_dd;
        r.total_trades = res.total_trades;
        r.total_swap = res.total_swap_charged;
        r.sharpe_proxy = (r.max_dd_pct > 0) ? (r.return_mult - 1.0) / (r.max_dd_pct / 100.0) : 0;
        r.stopped_out = (res.final_balance < 1000);  // Consider stopped if balance < 10%

    } catch (const std::exception& e) {
        r.stopped_out = true;
        r.max_dd_pct = 100;
    }

    return r;
}

// ============================================================================
// Worker thread
// ============================================================================
void worker(WorkQueue& queue, int total) {
    OUTask task;
    while (queue.pop(task)) {
        const std::vector<Tick>& ticks = task.use_2024 ? g_shared_ticks_2024 : g_shared_ticks_2025;
        std::string start = task.use_2024 ? "2024.01.01" : "2025.01.01";
        std::string end = task.use_2024 ? "2024.12.30" : "2025.12.30";

        OUResult r = RunOUTest(task, ticks, start, end);

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
// Generate tasks
// ============================================================================
std::vector<OUTask> GenerateTasks() {
    std::vector<OUTask> tasks;

    // Parameter sweep values
    std::vector<int> estimation_windows = {100, 500, 1000, 5000};
    std::vector<double> theta_scalings = {0.5, 1.0, 2.0};
    std::vector<double> min_theta_mults = {0.3, 0.5};
    std::vector<double> max_theta_mults = {2.0, 3.0};

    double base_spacing = 1.5;
    double survive_pct = 13.0;

    // Test on both 2025 and 2024
    for (bool use_2024 : {false, true}) {
        std::string year = use_2024 ? "2024" : "2025";

        // BASELINE (fixed spacing) for comparison
        OUTask baseline;
        baseline.estimation_window = 0;
        baseline.theta_scaling = 0;
        baseline.min_theta_mult = 0;
        baseline.max_theta_mult = 0;
        baseline.base_spacing = base_spacing;
        baseline.survive_pct = survive_pct;
        baseline.is_baseline = true;
        baseline.use_2024 = use_2024;
        baseline.label = "BASELINE_FIXED_" + year;
        tasks.push_back(baseline);

        // ADAPTIVE_SPACING baseline (production strategy) for comparison
        OUTask adaptive;
        adaptive.estimation_window = -1;  // Signal for ADAPTIVE_SPACING mode
        adaptive.theta_scaling = 0;
        adaptive.min_theta_mult = 0;
        adaptive.max_theta_mult = 0;
        adaptive.base_spacing = base_spacing;
        adaptive.survive_pct = survive_pct;
        adaptive.is_baseline = true;
        adaptive.use_2024 = use_2024;
        adaptive.label = "BASELINE_ADAPTIVE_" + year;
        tasks.push_back(adaptive);

        // OU parameter sweep
        for (int window : estimation_windows) {
            for (double scaling : theta_scalings) {
                for (double min_mult : min_theta_mults) {
                    for (double max_mult : max_theta_mults) {
                        OUTask t;
                        t.estimation_window = window;
                        t.theta_scaling = scaling;
                        t.min_theta_mult = min_mult;
                        t.max_theta_mult = max_mult;
                        t.base_spacing = base_spacing;
                        t.survive_pct = survive_pct;
                        t.is_baseline = false;
                        t.use_2024 = use_2024;
                        t.label = "OU_w" + std::to_string(window) +
                                  "_s" + std::to_string((int)(scaling * 10)) +
                                  "_min" + std::to_string((int)(min_mult * 10)) +
                                  "_max" + std::to_string((int)(max_mult * 10)) +
                                  "_" + year;
                        tasks.push_back(t);
                    }
                }
            }
        }
    }

    return tasks;
}

// ============================================================================
// Main
// ============================================================================
int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "  ORNSTEIN-UHLENBECK MEAN-REVERSION STRATEGY TEST" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;
    std::cout << "The OU process: dX = theta(mu - X)dt + sigma*dW" << std::endl;
    std::cout << "Key insight: theta (mean-reversion speed) determines optimal spacing" << std::endl;
    std::cout << "- High theta = fast reversion = tighter spacing" << std::endl;
    std::cout << "- Low theta = slow reversion = wider spacing" << std::endl;
    std::cout << std::endl;

    // Load tick data
    std::string path_2025 = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    std::string path_2024 = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAUUSD_TICKS_2024.csv";

    LoadTickData(path_2025, g_shared_ticks_2025, "2025");
    LoadTickData(path_2024, g_shared_ticks_2024, "2024");

    bool have_2024 = !g_shared_ticks_2024.empty();
    bool have_2025 = !g_shared_ticks_2025.empty();

    if (!have_2025) {
        std::cout << "ERROR: No 2025 data available" << std::endl;
        return 1;
    }

    std::cout << std::endl;

    // Generate tasks
    auto all_tasks = GenerateTasks();

    // Filter tasks based on available data
    std::vector<OUTask> tasks;
    for (const auto& t : all_tasks) {
        if (t.use_2024 && !have_2024) continue;
        if (!t.use_2024 && !have_2025) continue;
        tasks.push_back(t);
    }

    int total = (int)tasks.size();
    std::cout << "Testing " << total << " configurations..." << std::endl;

    // Setup thread pool
    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;
    std::cout << "Using " << num_threads << " worker threads" << std::endl;
    std::cout << std::endl;

    // Fill work queue
    WorkQueue queue;
    for (const auto& task : tasks) {
        queue.push(task);
    }

    // Launch workers
    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < num_threads; i++) {
        threads.emplace_back(worker, std::ref(queue), total);
    }

    queue.finish();
    for (auto& t : threads) {
        t.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

    std::cout << std::endl << std::endl;
    std::cout << "Completed in " << duration.count() << " seconds" << std::endl;
    std::cout << std::endl;

    // ========================================================================
    // Analyze results
    // ========================================================================

    // Separate by year
    std::vector<OUResult> results_2025, results_2024;
    OUResult baseline_fixed_2025, baseline_fixed_2024;
    OUResult baseline_adaptive_2025, baseline_adaptive_2024;

    for (const auto& r : g_results) {
        if (r.is_2024) {
            results_2024.push_back(r);
            if (r.is_baseline && r.label.find("FIXED") != std::string::npos) baseline_fixed_2024 = r;
            if (r.is_baseline && r.label.find("ADAPTIVE") != std::string::npos) baseline_adaptive_2024 = r;
        } else {
            results_2025.push_back(r);
            if (r.is_baseline && r.label.find("FIXED") != std::string::npos) baseline_fixed_2025 = r;
            if (r.is_baseline && r.label.find("ADAPTIVE") != std::string::npos) baseline_adaptive_2025 = r;
        }
    }

    // Use ADAPTIVE baseline for main comparison (production strategy)
    OUResult baseline_2025 = baseline_adaptive_2025;
    OUResult baseline_2024 = baseline_adaptive_2024;

    // Sort by Sharpe proxy
    std::sort(results_2025.begin(), results_2025.end(),
              [](const OUResult& a, const OUResult& b) { return a.sharpe_proxy > b.sharpe_proxy; });
    std::sort(results_2024.begin(), results_2024.end(),
              [](const OUResult& a, const OUResult& b) { return a.sharpe_proxy > b.sharpe_proxy; });

    // ========================================================================
    // Print 2025 results
    // ========================================================================
    std::cout << "================================================================" << std::endl;
    std::cout << "  2025 RESULTS (sorted by Sharpe proxy)" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::left << std::setw(4) << "#"
              << std::setw(32) << "Config"
              << std::right << std::setw(8) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(8) << "Trades"
              << std::setw(8) << "Sharpe"
              << std::setw(10) << "AvgTheta"
              << std::setw(8) << "Changes"
              << std::endl;
    std::cout << std::string(95, '-') << std::endl;

    // Print both baselines first
    std::cout << std::left << std::setw(4) << "FIX"
              << std::setw(32) << baseline_fixed_2025.label
              << std::right << std::fixed
              << std::setw(6) << std::setprecision(2) << baseline_fixed_2025.return_mult << "x"
              << std::setw(7) << std::setprecision(1) << baseline_fixed_2025.max_dd_pct << "%"
              << std::setw(8) << baseline_fixed_2025.total_trades
              << std::setw(8) << std::setprecision(2) << baseline_fixed_2025.sharpe_proxy
              << std::setw(10) << "-"
              << std::setw(8) << "-"
              << std::endl;
    std::cout << std::left << std::setw(4) << "ADP"
              << std::setw(32) << baseline_adaptive_2025.label
              << std::right << std::fixed
              << std::setw(6) << std::setprecision(2) << baseline_adaptive_2025.return_mult << "x"
              << std::setw(7) << std::setprecision(1) << baseline_adaptive_2025.max_dd_pct << "%"
              << std::setw(8) << baseline_adaptive_2025.total_trades
              << std::setw(8) << std::setprecision(2) << baseline_adaptive_2025.sharpe_proxy
              << std::setw(10) << "-"
              << std::setw(8) << "-"
              << std::endl;
    std::cout << std::string(95, '-') << std::endl;

    int count = 0;
    for (const auto& r : results_2025) {
        if (r.is_baseline) continue;
        if (++count > 20) break;

        std::cout << std::left << std::setw(4) << count
                  << std::setw(32) << r.label
                  << std::right << std::fixed
                  << std::setw(6) << std::setprecision(2) << r.return_mult << "x"
                  << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(8) << r.total_trades
                  << std::setw(8) << std::setprecision(2) << r.sharpe_proxy;

        if (r.avg_theta > 0) {
            std::cout << std::setw(10) << std::scientific << std::setprecision(2) << r.avg_theta
                      << std::setw(8) << r.spacing_changes;
        } else {
            std::cout << std::setw(10) << "-" << std::setw(8) << "-";
        }
        std::cout << std::fixed << std::endl;
    }

    // ========================================================================
    // Print 2024 results (if available)
    // ========================================================================
    if (have_2024 && !results_2024.empty()) {
        std::cout << std::endl;
        std::cout << "================================================================" << std::endl;
        std::cout << "  2024 RESULTS (sorted by Sharpe proxy)" << std::endl;
        std::cout << "================================================================" << std::endl;
        std::cout << std::left << std::setw(4) << "#"
                  << std::setw(32) << "Config"
                  << std::right << std::setw(8) << "Return"
                  << std::setw(8) << "MaxDD%"
                  << std::setw(8) << "Trades"
                  << std::setw(8) << "Sharpe"
                  << std::setw(10) << "AvgTheta"
                  << std::setw(8) << "Changes"
                  << std::endl;
        std::cout << std::string(95, '-') << std::endl;

        std::cout << std::left << std::setw(4) << "FIX"
                  << std::setw(32) << baseline_fixed_2024.label
                  << std::right << std::fixed
                  << std::setw(6) << std::setprecision(2) << baseline_fixed_2024.return_mult << "x"
                  << std::setw(7) << std::setprecision(1) << baseline_fixed_2024.max_dd_pct << "%"
                  << std::setw(8) << baseline_fixed_2024.total_trades
                  << std::setw(8) << std::setprecision(2) << baseline_fixed_2024.sharpe_proxy
                  << std::setw(10) << "-"
                  << std::setw(8) << "-"
                  << std::endl;
        std::cout << std::left << std::setw(4) << "ADP"
                  << std::setw(32) << baseline_adaptive_2024.label
                  << std::right << std::fixed
                  << std::setw(6) << std::setprecision(2) << baseline_adaptive_2024.return_mult << "x"
                  << std::setw(7) << std::setprecision(1) << baseline_adaptive_2024.max_dd_pct << "%"
                  << std::setw(8) << baseline_adaptive_2024.total_trades
                  << std::setw(8) << std::setprecision(2) << baseline_adaptive_2024.sharpe_proxy
                  << std::setw(10) << "-"
                  << std::setw(8) << "-"
                  << std::endl;
        std::cout << std::string(95, '-') << std::endl;

        count = 0;
        for (const auto& r : results_2024) {
            if (r.is_baseline) continue;
            if (++count > 20) break;

            std::cout << std::left << std::setw(4) << count
                      << std::setw(32) << r.label
                      << std::right << std::fixed
                      << std::setw(6) << std::setprecision(2) << r.return_mult << "x"
                      << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                      << std::setw(8) << r.total_trades
                      << std::setw(8) << std::setprecision(2) << r.sharpe_proxy;

            if (r.avg_theta > 0) {
                std::cout << std::setw(10) << std::scientific << std::setprecision(2) << r.avg_theta
                          << std::setw(8) << r.spacing_changes;
            } else {
                std::cout << std::setw(10) << "-" << std::setw(8) << "-";
            }
            std::cout << std::fixed << std::endl;
        }
    }

    // ========================================================================
    // Analysis: Does OU beat baseline?
    // ========================================================================
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  ANALYSIS: DOES OU-BASED SPACING BEAT BASELINE?" << std::endl;
    std::cout << "================================================================" << std::endl;

    // Count how many OU configs beat baseline in 2025
    int beat_return_2025 = 0, beat_sharpe_2025 = 0, beat_dd_2025 = 0;
    int total_ou_2025 = 0;

    for (const auto& r : results_2025) {
        if (r.is_baseline) continue;
        total_ou_2025++;
        if (r.return_mult > baseline_2025.return_mult) beat_return_2025++;
        if (r.sharpe_proxy > baseline_2025.sharpe_proxy) beat_sharpe_2025++;
        if (r.max_dd_pct < baseline_2025.max_dd_pct) beat_dd_2025++;
    }

    std::cout << std::endl;
    std::cout << "2025: " << total_ou_2025 << " OU configs tested" << std::endl;
    std::cout << "  Fixed baseline:    " << std::fixed << std::setprecision(2)
              << baseline_fixed_2025.return_mult << "x return, "
              << std::setprecision(1) << baseline_fixed_2025.max_dd_pct << "% DD, "
              << std::setprecision(2) << baseline_fixed_2025.sharpe_proxy << " Sharpe" << std::endl;
    std::cout << "  Adaptive baseline: " << std::fixed << std::setprecision(2)
              << baseline_adaptive_2025.return_mult << "x return, "
              << std::setprecision(1) << baseline_adaptive_2025.max_dd_pct << "% DD, "
              << std::setprecision(2) << baseline_adaptive_2025.sharpe_proxy << " Sharpe (PRODUCTION)" << std::endl;
    std::cout << std::endl;
    std::cout << "  Beat baseline return: " << beat_return_2025 << "/" << total_ou_2025
              << " (" << std::setprecision(1) << (100.0 * beat_return_2025 / total_ou_2025) << "%)" << std::endl;
    std::cout << "  Beat baseline Sharpe: " << beat_sharpe_2025 << "/" << total_ou_2025
              << " (" << std::setprecision(1) << (100.0 * beat_sharpe_2025 / total_ou_2025) << "%)" << std::endl;
    std::cout << "  Lower DD than base:   " << beat_dd_2025 << "/" << total_ou_2025
              << " (" << std::setprecision(1) << (100.0 * beat_dd_2025 / total_ou_2025) << "%)" << std::endl;

    // Same for 2024
    if (have_2024 && !results_2024.empty()) {
        int beat_return_2024 = 0, beat_sharpe_2024 = 0, beat_dd_2024 = 0;
        int total_ou_2024 = 0;

        for (const auto& r : results_2024) {
            if (r.is_baseline) continue;
            total_ou_2024++;
            if (r.return_mult > baseline_2024.return_mult) beat_return_2024++;
            if (r.sharpe_proxy > baseline_2024.sharpe_proxy) beat_sharpe_2024++;
            if (r.max_dd_pct < baseline_2024.max_dd_pct) beat_dd_2024++;
        }

        std::cout << std::endl;
        std::cout << "2024: " << total_ou_2024 << " OU configs tested" << std::endl;
        std::cout << "  Fixed baseline:    " << std::fixed << std::setprecision(2)
                  << baseline_fixed_2024.return_mult << "x return, "
                  << std::setprecision(1) << baseline_fixed_2024.max_dd_pct << "% DD, "
                  << std::setprecision(2) << baseline_fixed_2024.sharpe_proxy << " Sharpe" << std::endl;
        std::cout << "  Adaptive baseline: " << std::fixed << std::setprecision(2)
                  << baseline_adaptive_2024.return_mult << "x return, "
                  << std::setprecision(1) << baseline_adaptive_2024.max_dd_pct << "% DD, "
                  << std::setprecision(2) << baseline_adaptive_2024.sharpe_proxy << " Sharpe (PRODUCTION)" << std::endl;
        std::cout << std::endl;
        std::cout << "  Beat baseline return: " << beat_return_2024 << "/" << total_ou_2024
                  << " (" << std::setprecision(1) << (100.0 * beat_return_2024 / total_ou_2024) << "%)" << std::endl;
        std::cout << "  Beat baseline Sharpe: " << beat_sharpe_2024 << "/" << total_ou_2024
                  << " (" << std::setprecision(1) << (100.0 * beat_sharpe_2024 / total_ou_2024) << "%)" << std::endl;
        std::cout << "  Lower DD than base:   " << beat_dd_2024 << "/" << total_ou_2024
                  << " (" << std::setprecision(1) << (100.0 * beat_dd_2024 / total_ou_2024) << "%)" << std::endl;
    }

    // ========================================================================
    // Best config by estimation window
    // ========================================================================
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  BEST CONFIG BY ESTIMATION WINDOW (2025)" << std::endl;
    std::cout << "================================================================" << std::endl;

    std::vector<int> windows = {100, 500, 1000, 5000};
    std::cout << std::left << std::setw(10) << "Window"
              << std::setw(32) << "Best Config"
              << std::right << std::setw(8) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(8) << "Sharpe"
              << std::setw(10) << "vs Base"
              << std::endl;
    std::cout << std::string(85, '-') << std::endl;

    for (int w : windows) {
        const OUResult* best = nullptr;
        for (const auto& r : results_2025) {
            if (r.is_baseline) continue;
            if (r.estimation_window == w) {
                if (!best || r.sharpe_proxy > best->sharpe_proxy) {
                    best = &r;
                }
            }
        }
        if (best) {
            double vs_base = (best->return_mult / baseline_2025.return_mult - 1.0) * 100.0;
            std::cout << std::left << std::setw(10) << w
                      << std::setw(32) << best->label
                      << std::right << std::fixed
                      << std::setw(6) << std::setprecision(2) << best->return_mult << "x"
                      << std::setw(7) << std::setprecision(1) << best->max_dd_pct << "%"
                      << std::setw(8) << std::setprecision(2) << best->sharpe_proxy
                      << std::setw(8) << std::setprecision(1) << std::showpos << vs_base << "%" << std::noshowpos
                      << std::endl;
        }
    }

    // ========================================================================
    // Theta statistics
    // ========================================================================
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  THETA STATISTICS (Mean-Reversion Speed)" << std::endl;
    std::cout << "================================================================" << std::endl;

    // Aggregate theta stats
    double total_avg_theta = 0, total_min_theta = DBL_MAX, total_max_theta = 0;
    int theta_configs = 0;

    for (const auto& r : results_2025) {
        if (r.is_baseline || r.avg_theta <= 0) continue;
        total_avg_theta += r.avg_theta;
        if (r.min_theta < total_min_theta) total_min_theta = r.min_theta;
        if (r.max_theta > total_max_theta) total_max_theta = r.max_theta;
        theta_configs++;
    }

    if (theta_configs > 0) {
        std::cout << "Across all 2025 OU configs:" << std::endl;
        std::cout << "  Average theta: " << std::scientific << std::setprecision(4)
                  << (total_avg_theta / theta_configs) << std::endl;
        std::cout << "  Min theta seen: " << total_min_theta << std::endl;
        std::cout << "  Max theta seen: " << total_max_theta << std::endl;
        std::cout << "  Theta range: " << (total_max_theta / total_min_theta) << "x" << std::endl;
    }

    // ========================================================================
    // Key questions answered
    // ========================================================================
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  KEY QUESTIONS" << std::endl;
    std::cout << "================================================================" << std::endl;

    // Find best OU config (non-baseline)
    OUResult best_ou_2025;
    best_ou_2025.sharpe_proxy = -1;
    for (const auto& r : results_2025) {
        if (!r.is_baseline && r.sharpe_proxy > best_ou_2025.sharpe_proxy) {
            best_ou_2025 = r;
        }
    }

    std::cout << std::fixed << std::endl;
    std::cout << "1. Does OU-based spacing improve returns?" << std::endl;
    std::cout << "   Baseline: " << std::setprecision(2) << baseline_2025.return_mult << "x" << std::endl;
    std::cout << "   Best OU:  " << best_ou_2025.return_mult << "x (" << best_ou_2025.label << ")" << std::endl;
    std::cout << "   Answer: " << (best_ou_2025.return_mult > baseline_2025.return_mult ? "YES" : "NO")
              << " (" << std::showpos << ((best_ou_2025.return_mult / baseline_2025.return_mult - 1.0) * 100.0)
              << "%" << std::noshowpos << ")" << std::endl;

    std::cout << std::endl;
    std::cout << "2. Does OU-based spacing reduce drawdown?" << std::endl;
    std::cout << "   Baseline: " << std::setprecision(1) << baseline_2025.max_dd_pct << "% DD" << std::endl;
    std::cout << "   Best OU:  " << best_ou_2025.max_dd_pct << "% DD" << std::endl;
    std::cout << "   Answer: " << (best_ou_2025.max_dd_pct < baseline_2025.max_dd_pct ? "YES" : "NO")
              << " (" << std::showpos << (best_ou_2025.max_dd_pct - baseline_2025.max_dd_pct)
              << "%" << std::noshowpos << ")" << std::endl;

    std::cout << std::endl;
    std::cout << "3. What estimation window works best?" << std::endl;
    // Find best by window
    for (int w : windows) {
        int beating_base = 0;
        int total_w = 0;
        for (const auto& r : results_2025) {
            if (r.is_baseline) continue;
            if (r.estimation_window == w) {
                total_w++;
                if (r.return_mult > baseline_2025.return_mult) beating_base++;
            }
        }
        std::cout << "   Window " << std::setw(5) << w << ": " << beating_base << "/" << total_w
                  << " beat baseline (" << std::setprecision(0) << (100.0 * beating_base / total_w) << "%)" << std::endl;
    }

    std::cout << std::endl;
    std::cout << "4. Is theta actually predictive of future reversion speed?" << std::endl;
    std::cout << "   If OU works, high theta should predict faster reversion, leading" << std::endl;
    std::cout << "   to better performance with tighter spacing in those periods." << std::endl;
    std::cout << "   The test results above show whether this prediction holds." << std::endl;

    std::cout << std::endl;
    std::cout << "Total configurations tested: " << g_results.size() << std::endl;
    std::cout << "Execution time: " << duration.count() << " seconds" << std::endl;

    return 0;
}
