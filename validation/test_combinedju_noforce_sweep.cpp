/**
 * test_combinedju_noforce_sweep.cpp
 *
 * CombinedJu sweep with force_min_volume_entry=false
 * Tests the key parameters without forced entry for crash safety.
 *
 * Based on finding: force=true causes 98% loss in crash scenarios (Oct 2025)
 * force=false survives crashes with ~19% profit sacrifice in full year.
 */

#include "../include/strategy_combined_ju.h"
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
#include <sstream>

using namespace backtest;

// Shared tick data
std::vector<Tick> g_ticks;

struct TestConfig {
    std::string name;
    double survive_pct;
    double base_spacing;
    StrategyCombinedJu::TPMode tp_mode;
    double tp_sqrt_scale;
    double tp_linear_scale;
    double tp_min;
    bool enable_velocity_filter;
    int velocity_window;
    double velocity_threshold_pct;
    StrategyCombinedJu::SizingMode sizing_mode;
    double sizing_linear_scale;
    int sizing_threshold_pos;
    double sizing_threshold_mult;
    double volatility_lookback_hours;
    double typical_vol_pct;
    bool force_min_volume_entry;  // Added for comparison
};

struct TestResult {
    std::string name;
    double final_balance;
    double return_multiple;
    double max_dd_pct;
    int total_trades;
    int peak_positions;
    long velocity_blocks;
    long lot_zero_blocks;
    bool force_entry;
};

std::mutex g_queue_mutex;
std::mutex g_results_mutex;
std::queue<TestConfig> g_work_queue;
std::vector<TestResult> g_results;
std::atomic<int> g_completed(0);
int g_total_tasks = 0;

void LoadTickData() {
    auto start = std::chrono::high_resolution_clock::now();
    std::cout << "Loading 2025 tick data..." << std::endl;

    std::vector<std::string> files = {
        "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv"
    };

    for (const auto& file : files) {
        TickDataConfig cfg;
        cfg.file_path = file;
        cfg.format = TickDataFormat::MT5_CSV;
        TickDataManager mgr(cfg);
        Tick tick;
        while (mgr.GetNextTick(tick)) {
            g_ticks.push_back(tick);
        }
    }
    std::sort(g_ticks.begin(), g_ticks.end(),
              [](const Tick& a, const Tick& b) { return a.timestamp < b.timestamp; });

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();
    std::cout << "Loaded " << g_ticks.size() << " ticks in " << duration << "s" << std::endl;
}

TestResult RunTest(const TestConfig& cfg) {
    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = 10000.0;
    config.contract_size = 100.0;
    config.leverage = 500.0;
    config.pip_size = 0.01;
    config.swap_long = -66.99;
    config.swap_short = 41.2;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = "2025.01.01";
    config.end_date = "2025.12.29";
    config.verbose = false;

    TickDataConfig tick_cfg;
    tick_cfg.format = TickDataFormat::MT5_CSV;
    config.tick_data_config = tick_cfg;

    TickBasedEngine engine(config);

    StrategyCombinedJu::Config jucfg;
    jucfg.survive_pct = cfg.survive_pct;
    jucfg.base_spacing = cfg.base_spacing;
    jucfg.min_volume = 0.01;
    jucfg.max_volume = 10.0;
    jucfg.contract_size = 100.0;
    jucfg.leverage = 500.0;
    jucfg.volatility_lookback_hours = cfg.volatility_lookback_hours;
    jucfg.typical_vol_pct = cfg.typical_vol_pct;
    jucfg.tp_mode = cfg.tp_mode;
    jucfg.tp_sqrt_scale = cfg.tp_sqrt_scale;
    jucfg.tp_linear_scale = cfg.tp_linear_scale;
    jucfg.tp_min = cfg.tp_min;
    jucfg.enable_velocity_filter = cfg.enable_velocity_filter;
    jucfg.velocity_window = cfg.velocity_window;
    jucfg.velocity_threshold_pct = cfg.velocity_threshold_pct;
    jucfg.sizing_mode = cfg.sizing_mode;
    jucfg.sizing_linear_scale = cfg.sizing_linear_scale;
    jucfg.sizing_threshold_pos = cfg.sizing_threshold_pos;
    jucfg.sizing_threshold_mult = cfg.sizing_threshold_mult;
    jucfg.force_min_volume_entry = cfg.force_min_volume_entry;  // KEY: set force flag

    StrategyCombinedJu strategy(jucfg);

    engine.RunWithTicks(g_ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();
    auto& stats = strategy.GetStats();

    TestResult result;
    result.name = cfg.name;
    result.final_balance = results.final_balance;
    result.return_multiple = results.final_balance / 10000.0;
    result.max_dd_pct = results.max_drawdown_pct;
    result.total_trades = results.total_trades;
    result.peak_positions = stats.max_position_count;
    result.velocity_blocks = stats.velocity_blocks;
    result.lot_zero_blocks = stats.lot_size_zero_blocks;
    result.force_entry = cfg.force_min_volume_entry;

    return result;
}

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
        if (done % 5 == 0 || done == g_total_tasks) {
            std::cout << "\r  [" << done << "/" << g_total_tasks << "] "
                      << std::fixed << std::setprecision(1)
                      << (100.0 * done / g_total_tasks) << "%"
                      << std::string(20, ' ') << std::flush;
        }
    }
}

std::string TPModeStr(StrategyCombinedJu::TPMode m) {
    switch(m) {
        case StrategyCombinedJu::FIXED: return "FIX";
        case StrategyCombinedJu::SQRT: return "SQR";
        case StrategyCombinedJu::LINEAR: return "LIN";
    }
    return "?";
}

std::string SizingModeStr(StrategyCombinedJu::SizingMode m) {
    switch(m) {
        case StrategyCombinedJu::UNIFORM: return "UNI";
        case StrategyCombinedJu::LINEAR_SIZING: return "LIN";
        case StrategyCombinedJu::THRESHOLD_SIZING: return "THR";
    }
    return "?";
}

int main() {
    std::cout << std::string(100, '=') << std::endl;
    std::cout << "COMBINED_JU SWEEP - NO FORCED ENTRY (force_min_volume_entry=false)" << std::endl;
    std::cout << "Testing safe configuration for crash survival" << std::endl;
    std::cout << std::string(100, '=') << std::endl << std::endl;

    LoadTickData();

    // Build sweep configurations - test BOTH force=false AND force=true for comparison
    std::vector<double> survive_vals = {12.0, 13.0, 14.0};
    std::vector<double> spacing_vals = {1.0, 1.5};
    std::vector<StrategyCombinedJu::TPMode> tp_modes = {
        StrategyCombinedJu::SQRT,
        StrategyCombinedJu::LINEAR
    };
    std::vector<StrategyCombinedJu::SizingMode> sizing_modes = {
        StrategyCombinedJu::UNIFORM,
        StrategyCombinedJu::THRESHOLD_SIZING
    };
    std::vector<bool> velocity_vals = {false, true};
    std::vector<bool> force_vals = {false, true};  // Compare force ON vs OFF

    int config_id = 0;

    // Core sweep: survive × spacing × tp_mode × sizing_mode × velocity × force
    for (double survive : survive_vals) {
        for (double spacing : spacing_vals) {
            for (auto tp_mode : tp_modes) {
                for (auto sizing_mode : sizing_modes) {
                    for (bool vel : velocity_vals) {
                        for (bool force : force_vals) {
                            TestConfig cfg;
                            cfg.survive_pct = survive;
                            cfg.base_spacing = spacing;
                            cfg.tp_mode = tp_mode;
                            cfg.tp_sqrt_scale = 0.5;
                            cfg.tp_linear_scale = 0.3;
                            cfg.tp_min = spacing;
                            cfg.enable_velocity_filter = vel;
                            cfg.velocity_window = 10;
                            cfg.velocity_threshold_pct = 0.01;
                            cfg.sizing_mode = sizing_mode;
                            cfg.sizing_linear_scale = 0.5;
                            cfg.sizing_threshold_pos = 5;
                            cfg.sizing_threshold_mult = 2.0;
                            cfg.volatility_lookback_hours = 4.0;
                            cfg.typical_vol_pct = 0.55;
                            cfg.force_min_volume_entry = force;

                            std::ostringstream name;
                            name << "s" << (int)survive
                                 << "_sp" << std::fixed << std::setprecision(1) << spacing
                                 << "_tp" << TPModeStr(tp_mode)
                                 << "_sz" << SizingModeStr(sizing_mode)
                                 << "_v" << (vel ? "1" : "0")
                                 << "_f" << (force ? "1" : "0");
                            cfg.name = name.str();

                            g_work_queue.push(cfg);
                            config_id++;
                        }
                    }
                }
            }
        }
    }

    g_total_tasks = config_id;
    std::cout << "Testing " << g_total_tasks << " configurations..." << std::endl;

    // Launch worker threads
    unsigned int num_threads = std::thread::hardware_concurrency();
    std::cout << "Using " << num_threads << " threads" << std::endl;

    auto start_time = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> workers;
    for (unsigned int i = 0; i < num_threads; ++i) {
        workers.emplace_back(Worker);
    }

    for (auto& w : workers) {
        w.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count();
    std::cout << "\n\nCompleted in " << duration << "s" << std::endl;

    // Sort by return
    std::sort(g_results.begin(), g_results.end(),
              [](const TestResult& a, const TestResult& b) {
                  return a.return_multiple > b.return_multiple;
              });

    // Print all results
    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "ALL RESULTS (sorted by return)" << std::endl;
    std::cout << std::string(120, '=') << std::endl;

    std::cout << std::left << std::setw(35) << "Config"
              << std::right << std::setw(12) << "Return"
              << std::setw(10) << "MaxDD%"
              << std::setw(10) << "Trades"
              << std::setw(10) << "PeakPos"
              << std::setw(12) << "LotZero"
              << std::setw(8) << "Force"
              << std::endl;
    std::cout << std::string(97, '-') << std::endl;

    for (const auto& r : g_results) {
        std::cout << std::left << std::setw(35) << r.name
                  << std::right << std::fixed << std::setprecision(2) << std::setw(11) << r.return_multiple << "x"
                  << std::setprecision(1) << std::setw(9) << r.max_dd_pct << "%"
                  << std::setprecision(0) << std::setw(10) << r.total_trades
                  << std::setw(10) << r.peak_positions
                  << std::setw(12) << r.lot_zero_blocks
                  << std::setw(8) << (r.force_entry ? "ON" : "OFF")
                  << std::endl;
    }

    // Group results by force setting
    std::cout << "\n" << std::string(100, '=') << std::endl;
    std::cout << "FORCE=ON vs FORCE=OFF COMPARISON" << std::endl;
    std::cout << std::string(100, '=') << std::endl;

    // Calculate averages
    double avg_return_on = 0, avg_return_off = 0;
    double avg_dd_on = 0, avg_dd_off = 0;
    int count_on = 0, count_off = 0;

    for (const auto& r : g_results) {
        if (r.force_entry) {
            avg_return_on += r.return_multiple;
            avg_dd_on += r.max_dd_pct;
            count_on++;
        } else {
            avg_return_off += r.return_multiple;
            avg_dd_off += r.max_dd_pct;
            count_off++;
        }
    }

    if (count_on > 0) {
        avg_return_on /= count_on;
        avg_dd_on /= count_on;
    }
    if (count_off > 0) {
        avg_return_off /= count_off;
        avg_dd_off /= count_off;
    }

    std::cout << "\n| Force | Avg Return | Avg MaxDD | Configs |" << std::endl;
    std::cout << "|-------|------------|-----------|---------|" << std::endl;
    std::cout << "| ON    | " << std::fixed << std::setprecision(2) << std::setw(8) << avg_return_on << "x | "
              << std::setprecision(1) << std::setw(7) << avg_dd_on << "% | " << std::setw(7) << count_on << " |" << std::endl;
    std::cout << "| OFF   | " << std::fixed << std::setprecision(2) << std::setw(8) << avg_return_off << "x | "
              << std::setprecision(1) << std::setw(7) << avg_dd_off << "% | " << std::setw(7) << count_off << " |" << std::endl;

    double return_diff = ((avg_return_on - avg_return_off) / avg_return_on) * 100;
    std::cout << "\nProfit sacrifice with force=OFF: " << std::fixed << std::setprecision(1) << return_diff << "%" << std::endl;
    std::cout << "NOTE: This profit sacrifice is the price of crash survival!" << std::endl;

    // Top 10 with force=OFF
    std::cout << "\n" << std::string(100, '=') << std::endl;
    std::cout << "TOP 10 CRASH-SAFE CONFIGS (force=OFF)" << std::endl;
    std::cout << std::string(100, '=') << std::endl;

    int rank = 1;
    for (const auto& r : g_results) {
        if (!r.force_entry && rank <= 10) {
            std::cout << rank << ". " << r.name << " -> "
                      << std::fixed << std::setprecision(2) << r.return_multiple << "x, "
                      << std::setprecision(1) << r.max_dd_pct << "% DD, "
                      << r.total_trades << " trades" << std::endl;
            rank++;
        }
    }

    // Top 10 with force=ON (for comparison)
    std::cout << "\n" << std::string(100, '=') << std::endl;
    std::cout << "TOP 10 WITH FORCE=ON (WARNING: crash vulnerable!)" << std::endl;
    std::cout << std::string(100, '=') << std::endl;

    rank = 1;
    for (const auto& r : g_results) {
        if (r.force_entry && rank <= 10) {
            std::cout << rank << ". " << r.name << " -> "
                      << std::fixed << std::setprecision(2) << r.return_multiple << "x, "
                      << std::setprecision(1) << r.max_dd_pct << "% DD, "
                      << r.total_trades << " trades" << std::endl;
            rank++;
        }
    }

    std::cout << "\n" << std::string(100, '=') << std::endl;
    std::cout << "RECOMMENDATION: Use force=OFF configs for production" << std::endl;
    std::cout << "The ~" << std::fixed << std::setprecision(0) << return_diff << "% lower return is survival insurance against crashes" << std::endl;
    std::cout << std::string(100, '=') << std::endl;

    return 0;
}
