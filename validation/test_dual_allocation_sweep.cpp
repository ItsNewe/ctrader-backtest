#include "../include/strategy_dual_allocation.h"
#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <algorithm>

using namespace backtest;

// Global shared tick data
std::vector<Tick> g_shared_ticks;
std::mutex g_print_mutex;
std::atomic<int> g_completed{0};

struct TestConfig {
    std::string name;
    double primary_alloc;
    double extended_alloc;
    double primary_survive;
    double extended_survive;
    double primary_spacing_pct;
    double extended_spacing_pct;
    bool enable_extended;
    bool close_on_reversal;
};

struct TestResult {
    std::string name;
    double final_balance;
    double return_mult;
    double max_dd_pct;
    int total_trades;
    int primary_entries;
    int extended_entries;
    int extended_activations;
    double primary_profit;
    double extended_profit;
};

void LoadTickData(const std::string& path) {
    std::cout << "Loading tick data into shared memory..." << std::endl;

    TickDataConfig config;
    config.file_path = path;
    config.format = TickDataFormat::MT5_CSV;

    TickDataManager manager(config);

    Tick tick;
    long count = 0;
    while (manager.GetNextTick(tick)) {
        g_shared_ticks.push_back(tick);
        count++;
        if (count % 5000000 == 0) {
            std::cout << "  Loaded " << (count / 1000000) << "M ticks..." << std::endl;
        }
    }

    std::cout << "Loaded " << g_shared_ticks.size() << " ticks into memory." << std::endl;
}

TestResult RunTest(const TestConfig& cfg, const std::vector<Tick>& ticks) {
    TickBacktestConfig engine_config;
    engine_config.symbol = "XAUUSD";
    engine_config.initial_balance = 10000.0;
    engine_config.account_currency = "USD";
    engine_config.contract_size = 100.0;
    engine_config.leverage = 500.0;
    engine_config.margin_rate = 1.0;
    engine_config.pip_size = 0.01;
    engine_config.swap_long = -66.99;
    engine_config.swap_short = 41.2;
    engine_config.swap_mode = 1;
    engine_config.swap_3days = 3;
    engine_config.start_date = "2025.01.01";
    engine_config.end_date = "2025.12.30";
    engine_config.verbose = false;  // Disable trade-by-trade logging

    // Empty tick config since we're using shared ticks
    TickDataConfig tick_cfg;
    tick_cfg.file_path = "";
    engine_config.tick_data_config = tick_cfg;

    TickBasedEngine engine(engine_config);

    DualAllocationStrategy::Config strat_cfg;
    strat_cfg.primary_allocation_pct = cfg.primary_alloc;
    strat_cfg.extended_allocation_pct = cfg.extended_alloc;
    strat_cfg.primary_survive_pct = cfg.primary_survive;
    strat_cfg.extended_survive_pct = cfg.extended_survive;
    strat_cfg.primary_base_spacing_pct = cfg.primary_spacing_pct;
    strat_cfg.extended_base_spacing_pct = cfg.extended_spacing_pct;
    strat_cfg.enable_extended = cfg.enable_extended;
    strat_cfg.close_extended_on_reversal = cfg.close_on_reversal;

    DualAllocationStrategy strategy(strat_cfg);

    engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();
    auto stats = strategy.GetStats();

    TestResult result;
    result.name = cfg.name;
    result.final_balance = results.final_balance;
    result.return_mult = results.final_balance / 10000.0;
    result.max_dd_pct = results.max_drawdown_pct;
    result.total_trades = results.total_trades;
    result.primary_entries = stats.primary_entries;
    result.extended_entries = stats.extended_entries;
    result.extended_activations = stats.extended_activations;
    result.primary_profit = stats.primary_profit;
    result.extended_profit = stats.extended_profit;

    return result;
}

TestResult RunBaseline(const std::vector<Tick>& ticks) {
    TickBacktestConfig engine_config;
    engine_config.symbol = "XAUUSD";
    engine_config.initial_balance = 10000.0;
    engine_config.account_currency = "USD";
    engine_config.contract_size = 100.0;
    engine_config.leverage = 500.0;
    engine_config.margin_rate = 1.0;
    engine_config.pip_size = 0.01;
    engine_config.swap_long = -66.99;
    engine_config.swap_short = 41.2;
    engine_config.swap_mode = 1;
    engine_config.swap_3days = 3;
    engine_config.start_date = "2025.01.01";
    engine_config.end_date = "2025.12.30";
    engine_config.verbose = false;  // Disable trade-by-trade logging

    TickDataConfig tick_cfg;
    tick_cfg.file_path = "";
    engine_config.tick_data_config = tick_cfg;

    TickBasedEngine engine(engine_config);

    FillUpOscillation strategy(13.0, 1.5, 0.01, 10.0, 100.0, 500.0,
                               FillUpOscillation::ADAPTIVE_SPACING,
                               0.1, 30.0, 4.0);

    engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();

    TestResult result;
    result.name = "BASELINE_FillUp";
    result.final_balance = results.final_balance;
    result.return_mult = results.final_balance / 10000.0;
    result.max_dd_pct = results.max_drawdown_pct;
    result.total_trades = results.total_trades;
    result.primary_entries = results.total_trades;
    result.extended_entries = 0;
    result.extended_activations = 0;
    result.primary_profit = results.final_balance - 10000.0;
    result.extended_profit = 0.0;

    return result;
}

void Worker(std::queue<TestConfig>& work_queue, std::mutex& queue_mutex,
            std::vector<TestResult>& results, std::mutex& results_mutex,
            int total_tasks) {
    while (true) {
        TestConfig cfg;
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            if (work_queue.empty()) break;
            cfg = work_queue.front();
            work_queue.pop();
        }

        TestResult result = RunTest(cfg, g_shared_ticks);

        {
            std::lock_guard<std::mutex> lock(results_mutex);
            results.push_back(result);
        }

        int done = ++g_completed;
        if (done % 5 == 0 || done == total_tasks) {
            std::lock_guard<std::mutex> lock(g_print_mutex);
            std::cout << "Progress: " << done << "/" << total_tasks
                      << " (" << (100 * done / total_tasks) << "%)" << std::endl;
        }
    }
}

int main() {
    std::cout << "=== Dual Allocation Strategy Sweep ===" << std::endl;
    std::cout << "Testing capital allocation between Primary (FillUp) and Extended modes" << std::endl;
    std::cout << std::endl;

    // Load tick data
    std::string tick_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    LoadTickData(tick_path);

    if (g_shared_ticks.empty()) {
        std::cerr << "Failed to load tick data!" << std::endl;
        return 1;
    }

    // Generate test configurations
    std::vector<TestConfig> configs;

    // Test 1: Baseline comparison (no extended)
    configs.push_back({"Primary_100%", 1.0, 0.0, 13.0, 8.0, 0.055, 0.30, false, true});

    // Test 2: Different allocation splits
    double allocs[] = {0.90, 0.85, 0.80, 0.75, 0.70};
    for (double primary_alloc : allocs) {
        double ext_alloc = 1.0 - primary_alloc;
        std::string name = "P" + std::to_string((int)(primary_alloc*100)) + "_E" + std::to_string((int)(ext_alloc*100));
        configs.push_back({name, primary_alloc, ext_alloc, 13.0, 8.0, 0.055, 0.30, true, true});
    }

    // Test 3: Different extended survive percentages
    double ext_survives[] = {5.0, 8.0, 10.0, 12.0};
    for (double ext_surv : ext_survives) {
        std::string name = "P80_ExtSurv" + std::to_string((int)ext_surv);
        configs.push_back({name, 0.80, 0.20, 13.0, ext_surv, 0.055, 0.30, true, true});
    }

    // Test 4: Different extended spacing
    double ext_spacings[] = {0.15, 0.20, 0.30, 0.40, 0.50};
    for (double ext_sp : ext_spacings) {
        std::string name = "P80_ExtSp" + std::to_string((int)(ext_sp*100));
        configs.push_back({name, 0.80, 0.20, 13.0, 8.0, 0.055, ext_sp, true, true});
    }

    // Test 5: Close on reversal vs hold
    configs.push_back({"P80_HoldExt", 0.80, 0.20, 13.0, 8.0, 0.055, 0.30, true, false});

    // Test 6: More aggressive primary
    configs.push_back({"P80_PriSurv12", 0.80, 0.20, 12.0, 8.0, 0.055, 0.30, true, true});
    configs.push_back({"P80_PriSurv11", 0.80, 0.20, 11.0, 8.0, 0.055, 0.30, true, true});

    int total_configs = (int)configs.size();
    std::cout << "\nTotal configurations to test: " << total_configs << std::endl;

    // Create work queue
    std::queue<TestConfig> work_queue;
    for (const auto& cfg : configs) {
        work_queue.push(cfg);
    }

    // Results storage
    std::vector<TestResult> results;
    std::mutex queue_mutex, results_mutex;

    // Run baseline first (single-threaded for reference)
    std::cout << "\nRunning baseline FillUpOscillation..." << std::endl;
    TestResult baseline = RunBaseline(g_shared_ticks);
    results.push_back(baseline);
    std::cout << "Baseline: $" << std::fixed << std::setprecision(2)
              << baseline.final_balance << " (" << baseline.return_mult << "x)" << std::endl;

    // Launch worker threads
    std::cout << "\nRunning " << total_configs << " dual allocation tests in parallel..." << std::endl;

    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;
    std::cout << "Using " << num_threads << " threads" << std::endl;

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < num_threads; i++) {
        threads.emplace_back(Worker, std::ref(work_queue), std::ref(queue_mutex),
                            std::ref(results), std::ref(results_mutex), total_configs);
    }

    for (auto& t : threads) {
        t.join();
    }

    // Sort results by return
    std::sort(results.begin(), results.end(), [](const TestResult& a, const TestResult& b) {
        return a.return_mult > b.return_mult;
    });

    // Print results
    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "RESULTS (sorted by return)" << std::endl;
    std::cout << std::string(120, '=') << std::endl;
    std::cout << std::left << std::setw(25) << "Config"
              << std::right << std::setw(12) << "Return"
              << std::setw(10) << "MaxDD%"
              << std::setw(10) << "Trades"
              << std::setw(12) << "PriEntries"
              << std::setw(12) << "ExtEntries"
              << std::setw(12) << "ExtActive"
              << std::endl;
    std::cout << std::string(120, '-') << std::endl;

    for (const auto& r : results) {
        std::cout << std::left << std::setw(25) << r.name
                  << std::right << std::fixed << std::setprecision(2)
                  << std::setw(10) << r.return_mult << "x"
                  << std::setw(10) << r.max_dd_pct
                  << std::setw(10) << r.total_trades
                  << std::setw(12) << r.primary_entries
                  << std::setw(12) << r.extended_entries
                  << std::setw(12) << r.extended_activations
                  << std::endl;
    }

    // Analysis
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "ANALYSIS" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    // Find best configs
    auto best_return = results[0];

    TestResult best_risk_adj = results[0];
    double best_ratio = 0;
    for (const auto& r : results) {
        if (r.max_dd_pct > 0) {
            double ratio = r.return_mult / r.max_dd_pct * 100;
            if (ratio > best_ratio) {
                best_ratio = ratio;
                best_risk_adj = r;
            }
        }
    }

    std::cout << "\nBest Return: " << best_return.name
              << " (" << best_return.return_mult << "x, " << best_return.max_dd_pct << "% DD)" << std::endl;
    std::cout << "Best Risk-Adjusted: " << best_risk_adj.name
              << " (" << best_risk_adj.return_mult << "x, " << best_risk_adj.max_dd_pct << "% DD)" << std::endl;

    // Compare extended benefit
    std::cout << "\n--- Extended Mode Benefit Analysis ---" << std::endl;
    for (const auto& r : results) {
        if (r.name.find("P80_E20") != std::string::npos || r.name == "Primary_100%") {
            std::cout << r.name << ": ExtActivations=" << r.extended_activations
                      << ", ExtEntries=" << r.extended_entries << std::endl;
        }
    }

    return 0;
}
