/**
 * Bidirectional Momentum Strategy Test
 *
 * Combines "up while going down" + "up while going up" strategies.
 * Loads tick data once into shared memory for parallel testing.
 */

#include "../include/tick_based_engine.h"
#include "../include/strategy_bidirectional_momentum.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <fstream>
#include <sstream>
#include <algorithm>

using namespace backtest;

// Shared tick data (loaded once)
std::vector<Tick> g_ticks;

// Results mutex
std::mutex g_results_mutex;

struct TestResult {
    double survive_down;
    double survive_up;
    int sizing_mode;
    int closing_mode;
    bool enable_down;
    bool enable_up;
    double final_balance;
    double return_multiple;
    double max_drawdown_pct;
    int down_entries;
    int up_entries;
    double max_volume;
    bool stop_out;
};

std::vector<TestResult> g_results;

void LoadTickData(const std::string& path) {
    std::cout << "Loading tick data..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open: " + path);
    }

    std::string line;
    std::getline(file, line); // Skip header

    g_ticks.reserve(52000000);

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        Tick tick;
        std::stringstream ss(line);
        std::string ts, bid_str, ask_str;

        std::getline(ss, ts, '\t');
        std::getline(ss, bid_str, '\t');
        std::getline(ss, ask_str, '\t');

        tick.timestamp = ts;
        tick.bid = std::stod(bid_str);
        tick.ask = std::stod(ask_str);
        tick.volume = 0;

        g_ticks.push_back(tick);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto dur = std::chrono::duration_cast<std::chrono::seconds>(end - start);
    std::cout << "Loaded " << g_ticks.size() << " ticks in " << dur.count() << "s" << std::endl;
}

void RunTest(const BidirectionalMomentumStrategy::Config& config,
             const std::string& start_date,
             const std::string& end_date) {

    TickBacktestConfig engine_config;
    engine_config.symbol = "XAUUSD";
    engine_config.initial_balance = 10000.0;
    engine_config.contract_size = 100.0;
    engine_config.leverage = 500.0;
    engine_config.pip_size = 0.01;
    engine_config.swap_long = -66.99;
    engine_config.swap_short = 41.2;
    engine_config.swap_mode = 1;
    engine_config.swap_3days = 3;
    engine_config.start_date = start_date;
    engine_config.end_date = end_date;
    engine_config.verbose = false;

    TickBasedEngine engine(engine_config);
    BidirectionalMomentumStrategy strategy(config);

    engine.RunWithTicks(g_ticks, [&](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();
    auto stats = strategy.GetStats();

    TestResult r;
    r.survive_down = config.survive_down_pct;
    r.survive_up = config.survive_up_pct;
    r.sizing_mode = config.sizing_mode;
    r.closing_mode = config.closing_mode;
    r.enable_down = config.enable_up_while_down;
    r.enable_up = config.enable_up_while_up;
    r.final_balance = results.final_balance;
    r.return_multiple = results.final_balance / engine_config.initial_balance;
    r.max_drawdown_pct = results.max_drawdown_pct;
    r.down_entries = stats.down_grid_entries;
    r.up_entries = stats.up_momentum_entries;
    r.max_volume = stats.max_volume;
    r.stop_out = results.stop_out_occurred;

    std::lock_guard<std::mutex> lock(g_results_mutex);
    g_results.push_back(r);
}

int main() {
    std::cout << "=== Bidirectional Momentum Strategy Test ===" << std::endl;
    std::cout << "Testing on XAUUSD 2025" << std::endl;

    // Load data
    LoadTickData("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv");

    std::string start = "2025.01.01";
    std::string end = "2025.12.30";

    // First: Test individual strategies to establish baselines
    std::cout << "\n=== Testing Individual Strategies ===" << std::endl;

    // Test 1: Down-only (original "up while going down") - conservative
    {
        BidirectionalMomentumStrategy::Config cfg;
        cfg.survive_down_pct = 20.0;
        cfg.survive_up_pct = 15.0;
        cfg.sizing_mode = BidirectionalMomentumStrategy::CONSTANT;
        cfg.closing_mode = BidirectionalMomentumStrategy::CLOSE_PROFITABLE;
        cfg.enable_up_while_down = true;
        cfg.enable_up_while_up = false;
        cfg.contract_size = 100.0;
        cfg.leverage = 500.0;
        RunTest(cfg, start, end);
    }

    // Test 2: Up-only (original "up while going up") - conservative
    {
        BidirectionalMomentumStrategy::Config cfg;
        cfg.survive_down_pct = 20.0;
        cfg.survive_up_pct = 15.0;
        cfg.sizing_mode = BidirectionalMomentumStrategy::CONSTANT;
        cfg.closing_mode = BidirectionalMomentumStrategy::CLOSE_PROFITABLE;
        cfg.enable_up_while_down = false;
        cfg.enable_up_while_up = true;
        cfg.contract_size = 100.0;
        cfg.leverage = 500.0;
        RunTest(cfg, start, end);
    }

    // Test 3: Combined (both enabled) - conservative
    {
        BidirectionalMomentumStrategy::Config cfg;
        cfg.survive_down_pct = 20.0;
        cfg.survive_up_pct = 15.0;
        cfg.sizing_mode = BidirectionalMomentumStrategy::CONSTANT;
        cfg.closing_mode = BidirectionalMomentumStrategy::CLOSE_PROFITABLE;
        cfg.enable_up_while_down = true;
        cfg.enable_up_while_up = true;
        cfg.contract_size = 100.0;
        cfg.leverage = 500.0;
        RunTest(cfg, start, end);
    }

    // Print baseline results
    std::cout << "\n--- Baseline Results ---" << std::endl;
    std::cout << std::setw(20) << "Mode"
              << std::setw(12) << "Final $"
              << std::setw(10) << "Return"
              << std::setw(10) << "DD %"
              << std::setw(10) << "Down#"
              << std::setw(10) << "Up#"
              << std::setw(10) << "MaxVol"
              << std::setw(10) << "StopOut"
              << std::endl;

    for (const auto& r : g_results) {
        std::string mode = r.enable_down && r.enable_up ? "Combined" :
                          r.enable_down ? "Down-Only" : "Up-Only";
        std::cout << std::setw(20) << mode
                  << std::setw(12) << std::fixed << std::setprecision(0) << r.final_balance
                  << std::setw(10) << std::setprecision(2) << r.return_multiple << "x"
                  << std::setw(10) << std::setprecision(1) << r.max_drawdown_pct << "%"
                  << std::setw(10) << r.down_entries
                  << std::setw(10) << r.up_entries
                  << std::setw(10) << std::setprecision(2) << r.max_volume
                  << std::setw(10) << (r.stop_out ? "YES" : "no")
                  << std::endl;
    }

    g_results.clear();

    // Parallel parameter sweep
    std::cout << "\n=== Parameter Sweep (Combined Strategy) ===" << std::endl;

    std::vector<std::thread> threads;
    std::vector<BidirectionalMomentumStrategy::Config> configs;

    // Focused sweep on survive_up (momentum) which drives returns
    // Down grid uses fixed conservative value
    for (double survive_down : {15.0, 20.0, 25.0}) {
        for (double survive_up : {8.0, 10.0, 12.0, 15.0, 18.0, 20.0, 25.0, 30.0}) {
            for (int closing : {0, 1}) {
                BidirectionalMomentumStrategy::Config cfg;
                cfg.survive_down_pct = survive_down;
                cfg.survive_up_pct = survive_up;
                cfg.sizing_mode = BidirectionalMomentumStrategy::CONSTANT;
                cfg.closing_mode = static_cast<BidirectionalMomentumStrategy::ClosingMode>(closing);
                cfg.enable_up_while_down = true;
                cfg.enable_up_while_up = true;
                cfg.contract_size = 100.0;
                cfg.leverage = 500.0;
                configs.push_back(cfg);
            }
        }
    }

    std::cout << "Testing " << configs.size() << " configurations..." << std::endl;

    auto sweep_start = std::chrono::high_resolution_clock::now();

    // Run in parallel (limit concurrent threads)
    const int max_threads = std::thread::hardware_concurrency();
    std::cout << "Using " << max_threads << " threads" << std::endl;

    for (size_t i = 0; i < configs.size(); i += max_threads) {
        threads.clear();
        for (size_t j = i; j < std::min(i + max_threads, configs.size()); j++) {
            threads.emplace_back([&configs, j, &start, &end]() {
                RunTest(configs[j], start, end);
            });
        }
        for (auto& t : threads) {
            t.join();
        }
        std::cout << "Progress: " << std::min(i + max_threads, configs.size())
                  << "/" << configs.size() << std::endl;
    }

    auto sweep_end = std::chrono::high_resolution_clock::now();
    auto sweep_dur = std::chrono::duration_cast<std::chrono::seconds>(sweep_end - sweep_start);
    std::cout << "Sweep completed in " << sweep_dur.count() << "s" << std::endl;

    // Sort by return multiple
    std::sort(g_results.begin(), g_results.end(), [](const TestResult& a, const TestResult& b) {
        // Prefer no stop-out, then higher return
        if (a.stop_out != b.stop_out) return !a.stop_out;
        return a.return_multiple > b.return_multiple;
    });

    // Print top 20 results
    std::cout << "\n=== Top 20 Configurations (No Stop-Out) ===" << std::endl;
    std::cout << std::setw(8) << "SrvDown"
              << std::setw(8) << "SrvUp"
              << std::setw(8) << "Size"
              << std::setw(8) << "Close"
              << std::setw(12) << "Final $"
              << std::setw(10) << "Return"
              << std::setw(10) << "DD %"
              << std::setw(8) << "Down#"
              << std::setw(8) << "Up#"
              << std::setw(10) << "MaxVol"
              << std::endl;

    int count = 0;
    for (const auto& r : g_results) {
        if (r.stop_out) continue;
        if (count++ >= 20) break;

        std::string sizing = r.sizing_mode == 0 ? "CONST" : "INCR";
        std::string closing = r.closing_mode == 0 ? "PROFIT" : "ALL";

        std::cout << std::setw(8) << std::fixed << std::setprecision(1) << r.survive_down << "%"
                  << std::setw(8) << r.survive_up << "%"
                  << std::setw(8) << sizing
                  << std::setw(8) << closing
                  << std::setw(12) << std::setprecision(0) << r.final_balance
                  << std::setw(10) << std::setprecision(2) << r.return_multiple << "x"
                  << std::setw(10) << std::setprecision(1) << r.max_drawdown_pct << "%"
                  << std::setw(8) << r.down_entries
                  << std::setw(8) << r.up_entries
                  << std::setw(10) << std::setprecision(2) << r.max_volume
                  << std::endl;
    }

    // Count stop-outs
    int stop_outs = 0;
    for (const auto& r : g_results) {
        if (r.stop_out) stop_outs++;
    }
    std::cout << "\nStop-outs: " << stop_outs << "/" << g_results.size() << std::endl;

    // Find best risk-adjusted (return / DD)
    std::cout << "\n=== Best Risk-Adjusted (Return/DD) ===" << std::endl;
    std::sort(g_results.begin(), g_results.end(), [](const TestResult& a, const TestResult& b) {
        if (a.stop_out != b.stop_out) return !a.stop_out;
        double ra = a.return_multiple / (a.max_drawdown_pct + 1.0);
        double rb = b.return_multiple / (b.max_drawdown_pct + 1.0);
        return ra > rb;
    });

    count = 0;
    for (const auto& r : g_results) {
        if (r.stop_out) continue;
        if (count++ >= 5) break;

        std::string sizing = r.sizing_mode == 0 ? "CONST" : "INCR";
        std::string closing = r.closing_mode == 0 ? "PROFIT" : "ALL";
        double risk_adj = r.return_multiple / (r.max_drawdown_pct + 1.0);

        std::cout << std::setw(8) << std::fixed << std::setprecision(1) << r.survive_down << "%"
                  << std::setw(8) << r.survive_up << "%"
                  << std::setw(8) << sizing
                  << std::setw(8) << closing
                  << std::setw(12) << std::setprecision(0) << r.final_balance
                  << std::setw(10) << std::setprecision(2) << r.return_multiple << "x"
                  << std::setw(10) << r.max_drawdown_pct << "% DD"
                  << std::setw(12) << std::setprecision(3) << risk_adj << " R/DD"
                  << std::endl;
    }

    std::cout << "\n=== Test Complete ===" << std::endl;
    return 0;
}
