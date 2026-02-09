/**
 * test_combinedju_thorough_sweep.cpp
 *
 * Thorough CombinedJu sweep with all threshold_pos and threshold_mult combinations.
 * Only tests force_min_volume_entry=false for crash safety.
 *
 * Parameters tested:
 * - survive_pct: 12%, 13%
 * - base_spacing: 0.5, 1.0, 1.5, 2.0
 * - tp_mode: SQRT, LINEAR
 * - sizing_mode: UNIFORM, THRESHOLD
 * - threshold_pos: 1, 2, 3, 5, 7, 10 (for THRESHOLD)
 * - threshold_mult: 1.5, 2.0, 2.5, 3.0 (for THRESHOLD)
 * - velocity_filter: ON, OFF
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
    StrategyCombinedJu::SizingMode sizing_mode;
    int threshold_pos;
    double threshold_mult;
    bool velocity_filter;
};

struct TestResult {
    std::string name;
    double return_multiple;
    double max_dd_pct;
    int total_trades;
    int peak_positions;
    long lot_zero_blocks;
    double swap;
    bool stopped_out;
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

    TickDataConfig cfg;
    cfg.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    cfg.format = TickDataFormat::MT5_CSV;
    TickDataManager mgr(cfg);
    Tick tick;
    while (mgr.GetNextTick(tick)) {
        g_ticks.push_back(tick);
    }

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
    jucfg.volatility_lookback_hours = 4.0;
    jucfg.typical_vol_pct = 0.55;

    // TP mode
    jucfg.tp_mode = cfg.tp_mode;
    jucfg.tp_sqrt_scale = 0.5;
    jucfg.tp_linear_scale = 0.3;
    jucfg.tp_min = 1.0;

    // Velocity filter
    jucfg.enable_velocity_filter = cfg.velocity_filter;
    jucfg.velocity_window = 10;
    jucfg.velocity_threshold_pct = 0.01;

    // Sizing mode
    jucfg.sizing_mode = cfg.sizing_mode;
    jucfg.sizing_linear_scale = 0.5;
    jucfg.sizing_threshold_pos = cfg.threshold_pos;
    jucfg.sizing_threshold_mult = cfg.threshold_mult;

    // Force OFF for crash safety
    jucfg.force_min_volume_entry = false;

    StrategyCombinedJu strategy(jucfg);

    engine.RunWithTicks(g_ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();
    auto& stats = strategy.GetStats();

    TestResult result;
    result.name = cfg.name;
    result.return_multiple = results.final_balance / 10000.0;
    result.max_dd_pct = results.max_drawdown_pct;
    result.total_trades = results.total_trades;
    result.peak_positions = stats.max_position_count;
    result.lot_zero_blocks = stats.lot_size_zero_blocks;
    result.swap = results.total_swap_charged;
    result.stopped_out = (results.final_balance < 1000.0);  // Effectively stopped out if < 10% of initial

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
        if (done % 10 == 0 || done == g_total_tasks) {
            std::cout << "\r  [" << done << "/" << g_total_tasks << "] "
                      << std::fixed << std::setprecision(1)
                      << (100.0 * done / g_total_tasks) << "%"
                      << std::string(20, ' ') << std::flush;
        }
    }
}

std::string TPModeStr(StrategyCombinedJu::TPMode m) {
    return (m == StrategyCombinedJu::SQRT) ? "SQR" : "LIN";
}

int main() {
    std::cout << "====================================================================================================" << std::endl;
    std::cout << "COMBINED_JU THOROUGH SWEEP (force=OFF only)" << std::endl;
    std::cout << "Testing all threshold_pos and threshold_mult combinations" << std::endl;
    std::cout << "====================================================================================================" << std::endl;
    std::cout << std::endl;

    // Load tick data
    LoadTickData();

    // Build test configurations
    std::vector<TestConfig> configs;

    // Parameters to sweep
    std::vector<double> survive_pcts = {12.0, 13.0};
    std::vector<double> spacings = {0.5, 1.0, 1.5, 2.0};
    std::vector<StrategyCombinedJu::TPMode> tp_modes = {StrategyCombinedJu::SQRT, StrategyCombinedJu::LINEAR};
    std::vector<bool> velocity_filters = {false, true};

    // Threshold parameters
    std::vector<int> threshold_positions = {1, 2, 3, 5, 7, 10};
    std::vector<double> threshold_mults = {1.5, 2.0, 2.5, 3.0};

    // UNIFORM sizing configurations
    for (double survive : survive_pcts) {
        for (double spacing : spacings) {
            for (auto tp : tp_modes) {
                for (bool vel : velocity_filters) {
                    TestConfig cfg;
                    cfg.survive_pct = survive;
                    cfg.base_spacing = spacing;
                    cfg.tp_mode = tp;
                    cfg.sizing_mode = StrategyCombinedJu::UNIFORM;
                    cfg.threshold_pos = 5;
                    cfg.threshold_mult = 2.0;
                    cfg.velocity_filter = vel;

                    std::ostringstream oss;
                    oss << "s" << (int)survive << "_sp" << std::fixed << std::setprecision(1) << spacing
                        << "_tp" << TPModeStr(tp) << "_szUNI_" << (vel ? "v1" : "v0");
                    cfg.name = oss.str();

                    configs.push_back(cfg);
                }
            }
        }
    }

    // THRESHOLD sizing configurations with all pos/mult combinations
    for (double survive : survive_pcts) {
        for (double spacing : spacings) {
            for (auto tp : tp_modes) {
                for (bool vel : velocity_filters) {
                    for (int thr_pos : threshold_positions) {
                        for (double thr_mult : threshold_mults) {
                            TestConfig cfg;
                            cfg.survive_pct = survive;
                            cfg.base_spacing = spacing;
                            cfg.tp_mode = tp;
                            cfg.sizing_mode = StrategyCombinedJu::THRESHOLD_SIZING;
                            cfg.threshold_pos = thr_pos;
                            cfg.threshold_mult = thr_mult;
                            cfg.velocity_filter = vel;

                            std::ostringstream oss;
                            oss << "s" << (int)survive << "_sp" << std::fixed << std::setprecision(1) << spacing
                                << "_tp" << TPModeStr(tp) << "_szTHR_p" << thr_pos
                                << "_m" << std::setprecision(1) << thr_mult
                                << "_" << (vel ? "v1" : "v0");
                            cfg.name = oss.str();

                            configs.push_back(cfg);
                        }
                    }
                }
            }
        }
    }

    g_total_tasks = (int)configs.size();
    std::cout << "Testing " << g_total_tasks << " configurations..." << std::endl;

    // Create work queue
    for (const auto& cfg : configs) {
        g_work_queue.push(cfg);
    }

    // Launch workers
    unsigned int num_threads = std::thread::hardware_concurrency();
    std::cout << "Using " << num_threads << " threads" << std::endl;

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
    std::cout << std::endl << "Completed in " << duration << "s" << std::endl;

    // Sort by return
    std::sort(g_results.begin(), g_results.end(), [](const TestResult& a, const TestResult& b) {
        return a.return_multiple > b.return_multiple;
    });

    // Print all results
    std::cout << std::endl;
    std::cout << "========================================================================================================================" << std::endl;
    std::cout << "ALL RESULTS (sorted by return, force=OFF)" << std::endl;
    std::cout << "========================================================================================================================" << std::endl;
    std::cout << std::left << std::setw(50) << "Config"
              << std::right << std::setw(10) << "Return"
              << std::setw(10) << "MaxDD%"
              << std::setw(10) << "Trades"
              << std::setw(10) << "PeakPos"
              << std::setw(12) << "LotZero"
              << std::setw(10) << "Status" << std::endl;
    std::cout << std::string(112, '-') << std::endl;

    for (const auto& r : g_results) {
        std::cout << std::left << std::setw(50) << r.name
                  << std::right << std::fixed << std::setprecision(2) << std::setw(9) << r.return_multiple << "x"
                  << std::setprecision(1) << std::setw(9) << r.max_dd_pct << "%"
                  << std::setw(10) << r.total_trades
                  << std::setw(10) << r.peak_positions
                  << std::setw(12) << r.lot_zero_blocks
                  << std::setw(10) << (r.stopped_out ? "STOP-OUT" : "ok") << std::endl;
    }

    // Summary statistics
    std::cout << std::endl;
    std::cout << "====================================================================================================" << std::endl;
    std::cout << "SUMMARY STATISTICS" << std::endl;
    std::cout << "====================================================================================================" << std::endl;

    // Best by survive_pct
    std::cout << std::endl << "BEST BY SURVIVE_PCT:" << std::endl;
    for (double survive : survive_pcts) {
        std::string prefix = "s" + std::to_string((int)survive) + "_";
        for (const auto& r : g_results) {
            if (r.name.find(prefix) == 0 && !r.stopped_out) {
                std::cout << "  survive=" << (int)survive << "%: " << r.name
                          << " -> " << std::fixed << std::setprecision(2) << r.return_multiple << "x, "
                          << std::setprecision(1) << r.max_dd_pct << "% DD" << std::endl;
                break;
            }
        }
    }

    // Best by TP mode
    std::cout << std::endl << "BEST BY TP MODE:" << std::endl;
    for (auto tp : tp_modes) {
        std::string tp_str = "_tp" + TPModeStr(tp) + "_";
        for (const auto& r : g_results) {
            if (r.name.find(tp_str) != std::string::npos && !r.stopped_out) {
                std::cout << "  " << TPModeStr(tp) << ": " << r.name
                          << " -> " << std::fixed << std::setprecision(2) << r.return_multiple << "x, "
                          << std::setprecision(1) << r.max_dd_pct << "% DD" << std::endl;
                break;
            }
        }
    }

    // Best by sizing mode
    std::cout << std::endl << "BEST BY SIZING MODE:" << std::endl;
    for (const auto& r : g_results) {
        if (r.name.find("_szUNI_") != std::string::npos && !r.stopped_out) {
            std::cout << "  UNIFORM: " << r.name
                      << " -> " << std::fixed << std::setprecision(2) << r.return_multiple << "x, "
                      << std::setprecision(1) << r.max_dd_pct << "% DD" << std::endl;
            break;
        }
    }
    for (const auto& r : g_results) {
        if (r.name.find("_szTHR_") != std::string::npos && !r.stopped_out) {
            std::cout << "  THRESHOLD: " << r.name
                      << " -> " << std::fixed << std::setprecision(2) << r.return_multiple << "x, "
                      << std::setprecision(1) << r.max_dd_pct << "% DD" << std::endl;
            break;
        }
    }

    // Best by threshold_pos (for THRESHOLD sizing)
    std::cout << std::endl << "BEST BY THRESHOLD_POS (THRESHOLD sizing only):" << std::endl;
    for (int pos : threshold_positions) {
        std::string pos_str = "_p" + std::to_string(pos) + "_";
        for (const auto& r : g_results) {
            if (r.name.find("_szTHR_") != std::string::npos &&
                r.name.find(pos_str) != std::string::npos && !r.stopped_out) {
                std::cout << "  pos=" << pos << ": " << r.name
                          << " -> " << std::fixed << std::setprecision(2) << r.return_multiple << "x, "
                          << std::setprecision(1) << r.max_dd_pct << "% DD" << std::endl;
                break;
            }
        }
    }

    // Best by threshold_mult (for THRESHOLD sizing)
    std::cout << std::endl << "BEST BY THRESHOLD_MULT (THRESHOLD sizing only):" << std::endl;
    for (double mult : threshold_mults) {
        std::ostringstream oss;
        oss << "_m" << std::fixed << std::setprecision(1) << mult << "_";
        std::string mult_str = oss.str();
        for (const auto& r : g_results) {
            if (r.name.find("_szTHR_") != std::string::npos &&
                r.name.find(mult_str) != std::string::npos && !r.stopped_out) {
                std::cout << "  mult=" << std::fixed << std::setprecision(1) << mult << ": " << r.name
                          << " -> " << std::setprecision(2) << r.return_multiple << "x, "
                          << std::setprecision(1) << r.max_dd_pct << "% DD" << std::endl;
                break;
            }
        }
    }

    // Top 20 overall
    std::cout << std::endl;
    std::cout << "====================================================================================================" << std::endl;
    std::cout << "TOP 20 CONFIGURATIONS (force=OFF, crash-safe)" << std::endl;
    std::cout << "====================================================================================================" << std::endl;
    int count = 0;
    for (const auto& r : g_results) {
        if (r.stopped_out) continue;
        count++;
        std::cout << std::setw(2) << count << ". " << r.name
                  << " -> " << std::fixed << std::setprecision(2) << r.return_multiple << "x, "
                  << std::setprecision(1) << r.max_dd_pct << "% DD, "
                  << r.total_trades << " trades" << std::endl;
        if (count >= 20) break;
    }

    // Specific configs for presets THR3 and P1_M3
    std::cout << std::endl;
    std::cout << "====================================================================================================" << std::endl;
    std::cout << "PRESET CONFIGS (THR3, P1_M3 equivalents)" << std::endl;
    std::cout << "====================================================================================================" << std::endl;

    // THR3 equivalent: pos=3, mult=2.0
    std::cout << std::endl << "THR3 equivalent (threshold_pos=3, mult=2.0):" << std::endl;
    for (const auto& r : g_results) {
        if (r.name.find("_p3_m2.0_") != std::string::npos && !r.stopped_out) {
            std::cout << "  " << r.name << " -> " << std::fixed << std::setprecision(2) << r.return_multiple << "x, "
                      << std::setprecision(1) << r.max_dd_pct << "% DD" << std::endl;
        }
    }

    // P1_M3 equivalent: pos=1, mult=3.0
    std::cout << std::endl << "P1_M3 equivalent (threshold_pos=1, mult=3.0):" << std::endl;
    for (const auto& r : g_results) {
        if (r.name.find("_p1_m3.0_") != std::string::npos && !r.stopped_out) {
            std::cout << "  " << r.name << " -> " << std::fixed << std::setprecision(2) << r.return_multiple << "x, "
                      << std::setprecision(1) << r.max_dd_pct << "% DD" << std::endl;
        }
    }

    // Lowest DD configs
    std::cout << std::endl;
    std::cout << "====================================================================================================" << std::endl;
    std::cout << "LOWEST DRAWDOWN CONFIGS (for conservative traders)" << std::endl;
    std::cout << "====================================================================================================" << std::endl;

    // Sort by DD
    std::vector<TestResult> by_dd = g_results;
    std::sort(by_dd.begin(), by_dd.end(), [](const TestResult& a, const TestResult& b) {
        if (a.stopped_out != b.stopped_out) return !a.stopped_out;
        return a.max_dd_pct < b.max_dd_pct;
    });

    count = 0;
    for (const auto& r : by_dd) {
        if (r.stopped_out) continue;
        if (r.return_multiple < 5.0) continue;  // Skip configs with < 5x return
        count++;
        std::cout << std::setw(2) << count << ". " << r.name
                  << " -> " << std::fixed << std::setprecision(2) << r.return_multiple << "x, "
                  << std::setprecision(1) << r.max_dd_pct << "% DD" << std::endl;
        if (count >= 10) break;
    }

    std::cout << std::endl;
    std::cout << "====================================================================================================" << std::endl;
    std::cout << "RECOMMENDATION: Use configs from TOP 20 list for production (all are crash-safe with force=OFF)" << std::endl;
    std::cout << "====================================================================================================" << std::endl;

    return 0;
}
