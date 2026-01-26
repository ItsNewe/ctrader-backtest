/**
 * FLOATING ATTRACTOR GRID - Parallel Test
 *
 * Chaos concept: Grid center floats with a moving average (attractor).
 * Grid levels move with price trends while capturing oscillations.
 *
 * Tests:
 * 1. EMA periods: 50, 100, 200, 500, 1000 ticks
 * 2. SMA vs EMA comparison
 * 3. TP multiplier variations
 * 4. 2024 vs 2025 regime comparison for regime independence
 *
 * Uses PARALLEL pattern: load ticks ONCE, test configs across threads.
 */

#include "../include/strategy_floating_attractor.h"
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
std::vector<Tick> g_ticks_2025;
std::vector<Tick> g_ticks_2024;

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

    ticks.reserve(55000000);

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
struct FloatingTask {
    int attractor_period;
    StrategyFloatingAttractor::AttractorType attractor_type;
    double tp_multiplier;
    bool adaptive_spacing;
    double survive_pct;
    double base_spacing;
    std::string year;           // "2024" or "2025"
    std::string label;
};

struct FloatingResult {
    std::string label;
    std::string year;
    int attractor_period;
    std::string attractor_type;
    double tp_multiplier;
    double return_mult;
    double max_dd_pct;
    int total_trades;
    double total_swap;
    double sharpe_proxy;
    int attractor_crossings;
    bool stop_out;
};

// ============================================================================
// Work Queue for thread pool
// ============================================================================
class WorkQueue {
    std::queue<FloatingTask> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool done_ = false;

public:
    void push(const FloatingTask& task) {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push(task);
        cv_.notify_one();
    }

    bool pop(FloatingTask& task) {
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
std::vector<FloatingResult> g_results;

// ============================================================================
// Run single floating attractor test
// ============================================================================
FloatingResult run_floating_test(const FloatingTask& task, const std::vector<Tick>& ticks) {
    FloatingResult r;
    r.label = task.label;
    r.year = task.year;
    r.attractor_period = task.attractor_period;
    r.attractor_type = (task.attractor_type == StrategyFloatingAttractor::EMA) ? "EMA" :
                       (task.attractor_type == StrategyFloatingAttractor::SMA) ? "SMA" : "VWAP";
    r.tp_multiplier = task.tp_multiplier;
    r.return_mult = 0;
    r.max_dd_pct = 0;
    r.total_trades = 0;
    r.total_swap = 0;
    r.sharpe_proxy = 0;
    r.attractor_crossings = 0;
    r.stop_out = false;

    if (ticks.empty()) {
        r.stop_out = true;
        return r;
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
    cfg.start_date = (task.year == "2024") ? "2024.01.01" : "2025.01.01";
    cfg.end_date = (task.year == "2024") ? "2024.12.30" : "2025.12.30";
    cfg.verbose = false;

    try {
        TickBasedEngine engine(cfg);

        StrategyFloatingAttractor::Config sc;
        sc.survive_pct = task.survive_pct;
        sc.base_spacing = task.base_spacing;
        sc.min_volume = 0.01;
        sc.max_volume = 10.0;
        sc.contract_size = 100.0;
        sc.leverage = 500.0;
        sc.attractor_period = task.attractor_period;
        sc.attractor_type = task.attractor_type;
        sc.tp_multiplier = task.tp_multiplier;
        sc.adaptive_spacing = task.adaptive_spacing;
        sc.typical_vol_pct = 0.5;
        sc.volatility_lookback_hours = 4.0;

        StrategyFloatingAttractor strategy(sc);

        engine.RunWithTicks(ticks, [&strategy](const Tick& t, TickBasedEngine& e) {
            strategy.OnTick(t, e);
        });

        auto res = engine.GetResults();
        r.return_mult = res.final_balance / 10000.0;
        r.max_dd_pct = res.max_drawdown_pct;
        r.total_trades = (int)res.total_trades;
        r.total_swap = res.total_swap_charged;
        r.sharpe_proxy = (r.max_dd_pct > 0) ? (r.return_mult - 1.0) / (r.max_dd_pct / 100.0) : 0;
        r.attractor_crossings = strategy.GetAttractorCrossings();
        r.stop_out = res.stop_out_occurred;

    } catch (const std::exception& e) {
        r.stop_out = true;
        r.max_dd_pct = 100.0;
    }

    return r;
}

// ============================================================================
// Run baseline FillUpOscillation test for comparison
// ============================================================================
FloatingResult run_baseline_test(const std::string& year, const std::vector<Tick>& ticks) {
    FloatingResult r;
    r.label = "BASELINE_" + year;
    r.year = year;
    r.attractor_period = 0;
    r.attractor_type = "N/A";
    r.tp_multiplier = 1.0;
    r.return_mult = 0;
    r.max_dd_pct = 0;
    r.total_trades = 0;
    r.total_swap = 0;
    r.sharpe_proxy = 0;
    r.attractor_crossings = 0;
    r.stop_out = false;

    if (ticks.empty()) {
        r.stop_out = true;
        return r;
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
    cfg.start_date = (year == "2024") ? "2024.01.01" : "2025.01.01";
    cfg.end_date = (year == "2024") ? "2024.12.30" : "2025.12.30";
    cfg.verbose = false;

    try {
        TickBasedEngine engine(cfg);

        FillUpOscillation strategy(13.0, 1.5, 0.01, 10.0, 100.0, 500.0,
                                   FillUpOscillation::ADAPTIVE_SPACING, 0.1, 30.0, 4.0);

        engine.RunWithTicks(ticks, [&strategy](const Tick& t, TickBasedEngine& e) {
            strategy.OnTick(t, e);
        });

        auto res = engine.GetResults();
        r.return_mult = res.final_balance / 10000.0;
        r.max_dd_pct = res.max_drawdown_pct;
        r.total_trades = (int)res.total_trades;
        r.total_swap = res.total_swap_charged;
        r.sharpe_proxy = (r.max_dd_pct > 0) ? (r.return_mult - 1.0) / (r.max_dd_pct / 100.0) : 0;
        r.stop_out = res.stop_out_occurred;

    } catch (const std::exception& e) {
        r.stop_out = true;
    }

    return r;
}

void worker(WorkQueue& queue, int total) {
    FloatingTask task;
    while (queue.pop(task)) {
        const std::vector<Tick>& ticks = (task.year == "2024") ? g_ticks_2024 : g_ticks_2025;
        FloatingResult r = run_floating_test(task, ticks);

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
// Generate all test configurations
// ============================================================================
std::vector<FloatingTask> GenerateTasks() {
    std::vector<FloatingTask> tasks;

    std::vector<std::string> years = {"2025"};
    if (!g_ticks_2024.empty()) {
        years.push_back("2024");
    }

    // EMA period sweep
    for (int period : {50, 100, 200, 500, 1000, 2000, 5000}) {
        for (const auto& year : years) {
            FloatingTask t;
            t.attractor_period = period;
            t.attractor_type = StrategyFloatingAttractor::EMA;
            t.tp_multiplier = 1.0;
            t.adaptive_spacing = true;
            t.survive_pct = 13.0;
            t.base_spacing = 1.5;
            t.year = year;
            t.label = "EMA_" + std::to_string(period) + "_" + year;
            tasks.push_back(t);
        }
    }

    // SMA comparison at key periods
    for (int period : {100, 200, 500}) {
        for (const auto& year : years) {
            FloatingTask t;
            t.attractor_period = period;
            t.attractor_type = StrategyFloatingAttractor::SMA;
            t.tp_multiplier = 1.0;
            t.adaptive_spacing = true;
            t.survive_pct = 13.0;
            t.base_spacing = 1.5;
            t.year = year;
            t.label = "SMA_" + std::to_string(period) + "_" + year;
            tasks.push_back(t);
        }
    }

    // TP multiplier sweep (at best EMA period)
    for (double tp : {0.5, 0.75, 1.0, 1.25, 1.5, 2.0}) {
        for (const auto& year : years) {
            FloatingTask t;
            t.attractor_period = 200;
            t.attractor_type = StrategyFloatingAttractor::EMA;
            t.tp_multiplier = tp;
            t.adaptive_spacing = true;
            t.survive_pct = 13.0;
            t.base_spacing = 1.5;
            t.year = year;
            t.label = "EMA200_TP" + std::to_string(tp).substr(0,4) + "_" + year;
            tasks.push_back(t);
        }
    }

    // Spacing variations
    for (double spacing : {0.5, 1.0, 1.5, 2.0, 3.0}) {
        for (const auto& year : years) {
            FloatingTask t;
            t.attractor_period = 200;
            t.attractor_type = StrategyFloatingAttractor::EMA;
            t.tp_multiplier = 1.0;
            t.adaptive_spacing = true;
            t.survive_pct = 13.0;
            t.base_spacing = spacing;
            t.year = year;
            t.label = "EMA200_SP" + std::to_string(spacing).substr(0,3) + "_" + year;
            tasks.push_back(t);
        }
    }

    // Survive % variations
    for (double surv : {10.0, 13.0, 15.0, 18.0, 20.0}) {
        for (const auto& year : years) {
            FloatingTask t;
            t.attractor_period = 200;
            t.attractor_type = StrategyFloatingAttractor::EMA;
            t.tp_multiplier = 1.0;
            t.adaptive_spacing = true;
            t.survive_pct = surv;
            t.base_spacing = 1.5;
            t.year = year;
            t.label = "EMA200_SV" + std::to_string((int)surv) + "_" + year;
            tasks.push_back(t);
        }
    }

    // Non-adaptive spacing comparison
    for (const auto& year : years) {
        FloatingTask t;
        t.attractor_period = 200;
        t.attractor_type = StrategyFloatingAttractor::EMA;
        t.tp_multiplier = 1.0;
        t.adaptive_spacing = false;
        t.survive_pct = 13.0;
        t.base_spacing = 1.5;
        t.year = year;
        t.label = "EMA200_FIXED_" + year;
        tasks.push_back(t);
    }

    return tasks;
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "  FLOATING ATTRACTOR GRID - PARALLEL SWEEP" << std::endl;
    std::cout << "  Chaos concept: Grid centered on moving EMA attractor" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;

    // Step 1: Load tick data
    std::string path_2025 = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    std::string path_2024 = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAUUSD_TICKS_2024.csv";

    LoadTickData(path_2025, g_ticks_2025, "2025");
    LoadTickData(path_2024, g_ticks_2024, "2024");

    if (g_ticks_2025.empty()) {
        std::cerr << "ERROR: Cannot load 2025 tick data" << std::endl;
        return 1;
    }

    std::cout << std::endl;

    // Step 2: Run baselines first (single-threaded)
    std::cout << "Running baseline tests..." << std::endl;
    FloatingResult baseline_2025 = run_baseline_test("2025", g_ticks_2025);
    g_results.push_back(baseline_2025);
    std::cout << "  BASELINE 2025: " << std::fixed << std::setprecision(2) << baseline_2025.return_mult
              << "x, DD=" << baseline_2025.max_dd_pct << "%" << std::endl;

    FloatingResult baseline_2024;
    if (!g_ticks_2024.empty()) {
        baseline_2024 = run_baseline_test("2024", g_ticks_2024);
        g_results.push_back(baseline_2024);
        std::cout << "  BASELINE 2024: " << std::fixed << std::setprecision(2) << baseline_2024.return_mult
                  << "x, DD=" << baseline_2024.max_dd_pct << "%" << std::endl;
    }
    std::cout << std::endl;

    // Step 3: Generate floating attractor tasks
    auto tasks = GenerateTasks();
    int total = (int)tasks.size();

    // Step 4: Setup thread pool
    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;
    std::cout << "Testing " << total << " floating attractor configurations..." << std::endl;
    std::cout << "Using " << num_threads << " worker threads" << std::endl;
    std::cout << std::endl;

    // Step 5: Fill work queue and launch workers
    WorkQueue queue;
    for (const auto& task : tasks) {
        queue.push(task);
    }

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

    // Step 6: Analyze results
    std::cout << "================================================================" << std::endl;
    std::cout << "  2025 RESULTS (sorted by Sharpe proxy)" << std::endl;
    std::cout << "================================================================" << std::endl;

    // Filter and sort 2025 results
    std::vector<FloatingResult> results_2025;
    for (const auto& r : g_results) {
        if (r.year == "2025") results_2025.push_back(r);
    }
    std::sort(results_2025.begin(), results_2025.end(), [](const FloatingResult& a, const FloatingResult& b) {
        return a.sharpe_proxy > b.sharpe_proxy;
    });

    std::cout << std::left << std::setw(4) << "#"
              << std::setw(26) << "Label"
              << std::right << std::setw(9) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(8) << "Trades"
              << std::setw(9) << "Swap$"
              << std::setw(8) << "Sharpe"
              << std::setw(10) << "Crossings"
              << std::setw(8) << "Status"
              << std::endl;
    std::cout << std::string(90, '-') << std::endl;

    for (int i = 0; i < std::min(25, (int)results_2025.size()); i++) {
        const auto& r = results_2025[i];
        std::cout << std::left << std::setw(4) << (i + 1)
                  << std::setw(26) << r.label
                  << std::right << std::fixed
                  << std::setw(7) << std::setprecision(2) << r.return_mult << "x"
                  << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(8) << r.total_trades
                  << std::setw(8) << std::setprecision(0) << r.total_swap
                  << std::setw(8) << std::setprecision(2) << r.sharpe_proxy
                  << std::setw(10) << r.attractor_crossings
                  << std::setw(8) << (r.stop_out ? "SO" : "ok")
                  << std::endl;
    }

    // 2024 results if available
    if (!g_ticks_2024.empty()) {
        std::cout << std::endl;
        std::cout << "================================================================" << std::endl;
        std::cout << "  2024 RESULTS (sorted by Sharpe proxy)" << std::endl;
        std::cout << "================================================================" << std::endl;

        std::vector<FloatingResult> results_2024;
        for (const auto& r : g_results) {
            if (r.year == "2024") results_2024.push_back(r);
        }
        std::sort(results_2024.begin(), results_2024.end(), [](const FloatingResult& a, const FloatingResult& b) {
            return a.sharpe_proxy > b.sharpe_proxy;
        });

        std::cout << std::left << std::setw(4) << "#"
                  << std::setw(26) << "Label"
                  << std::right << std::setw(9) << "Return"
                  << std::setw(8) << "MaxDD%"
                  << std::setw(8) << "Trades"
                  << std::setw(9) << "Swap$"
                  << std::setw(8) << "Sharpe"
                  << std::setw(10) << "Crossings"
                  << std::setw(8) << "Status"
                  << std::endl;
        std::cout << std::string(90, '-') << std::endl;

        for (int i = 0; i < std::min(25, (int)results_2024.size()); i++) {
            const auto& r = results_2024[i];
            std::cout << std::left << std::setw(4) << (i + 1)
                      << std::setw(26) << r.label
                      << std::right << std::fixed
                      << std::setw(7) << std::setprecision(2) << r.return_mult << "x"
                      << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                      << std::setw(8) << r.total_trades
                      << std::setw(8) << std::setprecision(0) << r.total_swap
                      << std::setw(8) << std::setprecision(2) << r.sharpe_proxy
                      << std::setw(10) << r.attractor_crossings
                      << std::setw(8) << (r.stop_out ? "SO" : "ok")
                      << std::endl;
        }

        // Regime independence analysis
        std::cout << std::endl;
        std::cout << "================================================================" << std::endl;
        std::cout << "  REGIME INDEPENDENCE ANALYSIS (2025/2024 ratio)" << std::endl;
        std::cout << "================================================================" << std::endl;

        struct RegimeComparison {
            std::string config_base;
            double return_2025;
            double return_2024;
            double ratio;
            double dd_2025;
            double dd_2024;
        };
        std::vector<RegimeComparison> comparisons;

        // Match configs between years
        for (const auto& r25 : results_2025) {
            std::string base = r25.label;
            if (base.length() > 5 && base.substr(base.length() - 5) == "_2025") {
                base = base.substr(0, base.length() - 5);
            }
            for (const auto& r24 : results_2024) {
                std::string b24 = r24.label;
                if (b24.length() > 5 && b24.substr(b24.length() - 5) == "_2024") {
                    b24 = b24.substr(0, b24.length() - 5);
                }
                if (base == b24 && r24.return_mult > 0 && r25.return_mult > 0) {
                    RegimeComparison c;
                    c.config_base = base;
                    c.return_2025 = r25.return_mult;
                    c.return_2024 = r24.return_mult;
                    c.ratio = r25.return_mult / r24.return_mult;
                    c.dd_2025 = r25.max_dd_pct;
                    c.dd_2024 = r24.max_dd_pct;
                    comparisons.push_back(c);
                    break;
                }
            }
        }

        // Sort by regime ratio (lower = more consistent)
        std::sort(comparisons.begin(), comparisons.end(), [](const RegimeComparison& a, const RegimeComparison& b) {
            return a.ratio < b.ratio;
        });

        std::cout << std::left << std::setw(26) << "Config"
                  << std::right << std::setw(10) << "Ret 2025"
                  << std::setw(10) << "Ret 2024"
                  << std::setw(10) << "Ratio"
                  << std::setw(10) << "DD 2025"
                  << std::setw(10) << "DD 2024"
                  << std::endl;
        std::cout << std::string(76, '-') << std::endl;

        // Show most regime-independent configs (ratio closest to 1)
        std::sort(comparisons.begin(), comparisons.end(), [](const RegimeComparison& a, const RegimeComparison& b) {
            return std::abs(a.ratio - 1.0) < std::abs(b.ratio - 1.0);
        });

        std::cout << "MOST REGIME-INDEPENDENT (ratio closest to 1.0):" << std::endl;
        for (int i = 0; i < std::min(10, (int)comparisons.size()); i++) {
            const auto& c = comparisons[i];
            std::cout << std::left << std::setw(26) << c.config_base
                      << std::right << std::fixed
                      << std::setw(8) << std::setprecision(2) << c.return_2025 << "x"
                      << std::setw(8) << std::setprecision(2) << c.return_2024 << "x"
                      << std::setw(10) << std::setprecision(2) << c.ratio
                      << std::setw(9) << std::setprecision(1) << c.dd_2025 << "%"
                      << std::setw(9) << std::setprecision(1) << c.dd_2024 << "%"
                      << std::endl;
        }

        // Show baseline ratio for comparison
        std::cout << std::endl;
        if (baseline_2024.return_mult > 0) {
            double baseline_ratio = baseline_2025.return_mult / baseline_2024.return_mult;
            std::cout << "BASELINE regime ratio: " << std::fixed << std::setprecision(2)
                      << baseline_ratio << " (2025: " << baseline_2025.return_mult
                      << "x, 2024: " << baseline_2024.return_mult << "x)" << std::endl;
        }
    }

    // Summary
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  SUMMARY" << std::endl;
    std::cout << "================================================================" << std::endl;

    // Find best floating attractor config
    FloatingResult* best_fa = nullptr;
    for (auto& r : results_2025) {
        if (r.label.find("BASELINE") == std::string::npos) {
            if (!best_fa || r.sharpe_proxy > best_fa->sharpe_proxy) {
                best_fa = &r;
            }
        }
    }

    std::cout << "BASELINE 2025:           " << std::fixed << std::setprecision(2)
              << baseline_2025.return_mult << "x return, " << baseline_2025.max_dd_pct
              << "% DD, Sharpe=" << baseline_2025.sharpe_proxy << std::endl;

    if (best_fa) {
        std::cout << "BEST FLOATING ATTRACTOR: " << best_fa->label << std::endl;
        std::cout << "                         " << std::fixed << std::setprecision(2)
                  << best_fa->return_mult << "x return, " << best_fa->max_dd_pct
                  << "% DD, Sharpe=" << best_fa->sharpe_proxy << std::endl;

        double return_diff = (best_fa->return_mult - baseline_2025.return_mult) / baseline_2025.return_mult * 100;
        double dd_diff = best_fa->max_dd_pct - baseline_2025.max_dd_pct;
        double sharpe_diff = (best_fa->sharpe_proxy - baseline_2025.sharpe_proxy) / baseline_2025.sharpe_proxy * 100;

        std::cout << std::endl;
        std::cout << "VS BASELINE:" << std::endl;
        std::cout << "  Return: " << (return_diff >= 0 ? "+" : "") << return_diff << "%" << std::endl;
        std::cout << "  MaxDD:  " << (dd_diff >= 0 ? "+" : "") << dd_diff << "pp" << std::endl;
        std::cout << "  Sharpe: " << (sharpe_diff >= 0 ? "+" : "") << sharpe_diff << "%" << std::endl;
    }

    std::cout << std::endl;
    std::cout << "Total configurations tested: " << g_results.size() << std::endl;
    std::cout << "Execution time: " << duration.count() << "s" << std::endl;

    return 0;
}
