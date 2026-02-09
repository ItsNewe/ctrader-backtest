/**
 * @file test_nasdaq_up_nas100.cpp
 * @brief PARALLEL sweep of NasdaqUp strategy on NAS100 uptrend period.
 *
 * Uses the established framework pattern:
 * - TickDataManager for efficient loading
 * - Global shared tick vector (read-only)
 * - Worker thread pool
 * - RunWithTicks() for no-reload execution
 *
 * Data:  NAS100, Apr 7 - Oct 30, 2025
 * Price: ~16,750 → ~25,760 (54% uptrend)
 */

#include "../include/strategy_nasdaq_up.h"
#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <chrono>
#include <algorithm>

using namespace backtest;

// ============================================================================
// GLOBAL SHARED DATA (read-only across threads)
// ============================================================================
std::vector<Tick> g_ticks;

// ============================================================================
// TEST CONFIGURATION AND RESULTS
// ============================================================================
struct TestConfig {
    double multiplier;
    double power;
    double stop_out_margin;
    double leverage;
};

struct TestResult {
    double multiplier;
    double power;
    double stop_out_margin;
    double leverage;
    double final_balance;
    double max_equity;
    int total_entries;
    int stop_outs;
    int cycles;
    double peak_volume;
};

// ============================================================================
// THREAD-SAFE WORK QUEUE
// ============================================================================
std::mutex g_queue_mutex;
std::mutex g_results_mutex;
std::queue<TestConfig> g_work_queue;
std::vector<TestResult> g_results;
std::atomic<int> g_completed(0);
int g_total_tasks = 0;

// ============================================================================
// TICK LOADING (Using TickDataManager)
// ============================================================================
void LoadTickData() {
    auto start = std::chrono::high_resolution_clock::now();
    std::cout << "Loading NAS100 tick data..." << std::endl;

    TickDataConfig cfg;
    cfg.file_path = "C:\\Users\\user\\.claude-worktrees\\ctrader-backtest\\beautiful-margulis\\validation\\Grid\\NAS100_TICKS_2025.csv";
    cfg.format = TickDataFormat::MT5_CSV;

    TickDataManager mgr(cfg);
    Tick tick;
    while (mgr.GetNextTick(tick)) {
        g_ticks.push_back(tick);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();
    std::cout << "  Loaded " << g_ticks.size() << " ticks in " << duration << "s" << std::endl;

    if (!g_ticks.empty()) {
        std::cout << "  First: " << g_ticks.front().ask << " @ " << g_ticks.front().timestamp << std::endl;
        std::cout << "  Last:  " << g_ticks.back().ask << " @ " << g_ticks.back().timestamp << std::endl;
        std::cout << "  Price change: " << std::fixed << std::setprecision(1)
                  << ((g_ticks.back().ask / g_ticks.front().ask - 1.0) * 100.0) << "%" << std::endl;
    }
}

// ============================================================================
// TEST RUNNER (Called from worker threads)
// ============================================================================
TestResult RunTest(const TestConfig& cfg) {
    // Create fresh engine for each test
    TickBacktestConfig config;
    config.symbol = "NAS100";
    config.initial_balance = 10000.0;
    config.contract_size = 1.0;
    // NAS100 margin: Lots * ContractSize * Price * MarginRate
    //  uses margin_rate = 0.01 (1%), giving ~$258/lot at price 25872
    // To achieve this with the engine formula (lots * contract * price / leverage * margin_rate):
    // Set leverage=100, margin_rate=1.0 => margin = price/100 = 258.72 (correct!)
    config.leverage = 100.0;  // Fixed for NAS100 margin calculation
    config.margin_rate = 1.0;
    config.pip_size = 0.01;
    config.swap_long = -5.96;
    config.swap_short = 1.6;
    config.swap_mode = 5;  // 5 = % of current price (not 1=points)
    config.swap_3days = 5;  // Friday (from MT5 query)
    config.start_date = "2025.04.07";
    config.end_date = "2025.10.30";
    config.verbose = false;

    TickDataConfig tick_cfg;
    tick_cfg.format = TickDataFormat::MT5_CSV;
    config.tick_data_config = tick_cfg;

    TickBasedEngine engine(config);

    // Create strategy with test parameters
    NasdaqUp::Config strat_cfg;
    strat_cfg.multiplier = cfg.multiplier;
    strat_cfg.power = cfg.power;
    strat_cfg.stop_out_margin = cfg.stop_out_margin;
    strat_cfg.contract_size = 1.0;
    strat_cfg.leverage = 100.0;  // Fixed leverage for NAS100 margin calc
    strat_cfg.min_volume = 0.01;
    strat_cfg.max_volume = 100.0;

    NasdaqUp strategy(strat_cfg);

    // Run with pre-loaded ticks (no file I/O in thread)
    engine.RunWithTicks(g_ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    // Extract results
    auto results = engine.GetResults();
    auto& stats = strategy.GetStats();

    TestResult result;
    result.multiplier = cfg.multiplier;
    result.power = cfg.power;
    result.stop_out_margin = cfg.stop_out_margin;
    result.leverage = cfg.leverage;
    result.final_balance = results.final_balance;
    result.max_equity = stats.max_equity;
    result.total_entries = stats.total_entries;
    result.stop_outs = stats.stop_outs;
    result.cycles = stats.cycles;
    result.peak_volume = stats.peak_volume;

    return result;
}

// ============================================================================
// WORKER THREAD
// ============================================================================
void Worker() {
    while (true) {
        TestConfig task;
        {
            std::lock_guard<std::mutex> lock(g_queue_mutex);
            if (g_work_queue.empty()) return;
            task = g_work_queue.front();
            g_work_queue.pop();
        }

        TestResult result = RunTest(task);

        {
            std::lock_guard<std::mutex> lock(g_results_mutex);
            g_results.push_back(result);
        }

        int done = ++g_completed;
        if (done % 50 == 0 || done == g_total_tasks) {
            std::cout << "\r  [" << done << "/" << g_total_tasks << "] "
                      << std::fixed << std::setprecision(1)
                      << (100.0 * done / g_total_tasks) << "%"
                      << std::string(20, ' ') << std::flush;
        }
    }
}

// ============================================================================
// MAIN
// ============================================================================
int main() {
    std::cout << std::string(70, '*') << std::endl;
    std::cout << "  NasdaqUp Strategy PARALLEL Sweep - NAS100" << std::endl;
    std::cout << "  Apr 7 - Oct 30, 2025 (Uptrend: 54% rise)" << std::endl;
    std::cout << std::string(70, '*') << std::endl;

    // =========================================================================
    // STEP 1: Load tick data ONCE
    // =========================================================================
    LoadTickData();
    if (g_ticks.empty()) {
        std::cerr << "Failed to load tick data!" << std::endl;
        return 1;
    }

    // =========================================================================
    // STEP 2: Build work queue
    // =========================================================================
    std::cout << "\nBuilding parameter sweep..." << std::endl;

    // Correct parameter ranges per user specification:
    // - multiplier: positive integer 1 to 1,000,000
    // - power: 0 to -2 with 3-4 decimal precision
    // - stop_out (prcnt_manual_stop_out_loss): 20 to thousands
    // - leverage: FIXED at 100 (NAS100 margin rate = 1% = 1/100)
    std::vector<double> multipliers = {1, 10, 50, 100, 500, 1000, 5000, 10000, 50000, 100000, 500000, 1000000};
    std::vector<double> powers = {0.0, -0.1, -0.25, -0.5, -0.75, -1.0, -1.25, -1.5, -1.75, -2.0};
    std::vector<double> stop_outs = {20.0, 50.0, 74.0, 100.0, 150.0, 200.0, 500.0, 1000.0, 2000.0};

    for (double mult : multipliers) {
        for (double pow : powers) {
            for (double stop : stop_outs) {
                TestConfig cfg;
                cfg.multiplier = mult;
                cfg.power = pow;
                cfg.stop_out_margin = stop;
                cfg.leverage = 100.0;  // Fixed
                g_work_queue.push(cfg);
            }
        }
    }

    g_total_tasks = g_work_queue.size();
    std::cout << "  Total configurations: " << g_total_tasks << std::endl;

    // =========================================================================
    // STEP 3: Run parallel sweep
    // =========================================================================
    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;
    std::cout << "\nRunning parallel sweep with " << num_threads << " threads..." << std::endl;

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < num_threads; i++) {
        threads.emplace_back(Worker);
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();
    std::cout << "\n  Completed in " << duration << "s ("
              << std::fixed << std::setprecision(0)
              << (1000.0 * duration / g_total_tasks) << "ms per config avg)" << std::endl;

    // =========================================================================
    // STEP 4: Analyze results
    // =========================================================================
    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "  RESULTS ANALYSIS" << std::endl;
    std::cout << std::string(70, '=') << std::endl;

    // Sort by final balance (descending)
    std::sort(g_results.begin(), g_results.end(),
              [](const TestResult& a, const TestResult& b) {
                  return a.final_balance > b.final_balance;
              });

    // Top 20 configurations
    std::cout << "\n  TOP 20 CONFIGURATIONS BY FINAL BALANCE:" << std::endl;
    std::cout << "  " << std::string(100, '-') << std::endl;
    std::cout << "  Mult  | Power | Stop  | Lev   | Final       | MaxEquity   | Entries | StopOuts | PeakVol" << std::endl;
    std::cout << "  " << std::string(100, '-') << std::endl;

    for (int i = 0; i < std::min(20, (int)g_results.size()); i++) {
        const auto& r = g_results[i];
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "  " << std::setw(5) << r.multiplier
                  << " | " << std::setw(5) << r.power
                  << " | " << std::setw(4) << r.stop_out_margin << "%"
                  << " | " << std::setw(4) << (int)r.leverage
                  << "  | $" << std::setw(9) << r.final_balance
                  << " | $" << std::setw(9) << r.max_equity
                  << " | " << std::setw(6) << r.total_entries
                  << "  | " << std::setw(4) << r.stop_outs
                  << "     | " << std::setw(6) << r.peak_volume << std::endl;
    }

    // Bottom 5 configurations
    std::cout << "\n  BOTTOM 5 CONFIGURATIONS:" << std::endl;
    std::cout << "  " << std::string(100, '-') << std::endl;
    for (int i = std::max(0, (int)g_results.size() - 5); i < (int)g_results.size(); i++) {
        const auto& r = g_results[i];
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "  " << std::setw(5) << r.multiplier
                  << " | " << std::setw(5) << r.power
                  << " | " << std::setw(4) << r.stop_out_margin << "%"
                  << " | " << std::setw(4) << (int)r.leverage
                  << "  | $" << std::setw(9) << r.final_balance
                  << " | $" << std::setw(9) << r.max_equity
                  << " | " << std::setw(6) << r.total_entries
                  << "  | " << std::setw(4) << r.stop_outs
                  << "     | " << std::setw(6) << r.peak_volume << std::endl;
    }

    // Summary statistics
    int profitable = 0;
    double best_return = 0;
    double worst_return = 1e9;
    for (const auto& r : g_results) {
        if (r.final_balance > 10000.0) profitable++;
        best_return = std::max(best_return, r.final_balance);
        worst_return = std::min(worst_return, r.final_balance);
    }

    std::cout << "\n  SUMMARY:" << std::endl;
    std::cout << "  " << std::string(50, '-') << std::endl;
    std::cout << "  Total configs tested: " << g_results.size() << std::endl;
    std::cout << "  Profitable configs:   " << profitable << " ("
              << std::fixed << std::setprecision(1)
              << (100.0 * profitable / g_results.size()) << "%)" << std::endl;
    std::cout << "  Best return:  $" << std::setprecision(2) << best_return
              << " (" << (best_return / 10000.0) << "x)" << std::endl;
    std::cout << "  Worst return: $" << worst_return
              << " (" << (worst_return / 10000.0) << "x)" << std::endl;

    std::cout << "\n" << std::string(70, '*') << std::endl;
    std::cout << "  Test completed." << std::endl;
    std::cout << std::string(70, '*') << std::endl;

    return 0;
}
