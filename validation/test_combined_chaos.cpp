/**
 * Combined Chaos Strategy Test
 *
 * Tests the combined chaos strategy integrating:
 * 1. Floating Attractor (EMA-200 tracking)
 * 2. Velocity Zero-Crossing entry filter
 * 3. Edge-of-Chaos parameters (survive=12%, lookback=8h)
 *
 * Uses PARALLEL pattern: loads tick data ONCE into shared memory,
 * then runs multiple configurations across all CPU threads.
 *
 * Compares:
 * - Combined strategy (all features)
 * - Floating attractor only (no velocity filter)
 * - Baseline FillUpOscillation (ADAPTIVE_SPACING)
 *
 * Key metrics:
 * - Return multiple
 * - Max drawdown
 * - Regime ratio (2025/2024) - lower is better (more regime-independent)
 */

#include "../include/strategy_combined_chaos.h"
#include "../include/strategy_floating_attractor.h"
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
#include <chrono>
#include <algorithm>

using namespace backtest;

// Global shared tick data for both years
std::vector<Tick> g_ticks_2024;
std::vector<Tick> g_ticks_2025;

// Progress tracking
std::atomic<int> g_completed(0);
std::mutex g_output_mutex;

// Results structure
struct TestResult {
    std::string config_name;
    std::string year;
    double final_balance;
    double return_multiple;
    double max_dd_pct;
    int total_trades;
    double total_swap;
    int velocity_blocked;
    int velocity_allowed;
    bool stopped_out;
};

// Configuration structure
struct TestConfig {
    std::string name;
    int ema_period;
    double tp_multiplier;
    double survive_pct;
    double lookback_hours;
    bool velocity_filter;
    int velocity_window;
    bool is_baseline;         // True for FillUpOscillation baseline
    bool is_floating_only;    // True for floating attractor without velocity filter
};

// Load tick data from file
void LoadTickData(const std::string& path, std::vector<Tick>& ticks) {
    TickDataConfig tick_config;
    tick_config.file_path = path;
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = true;

    TickDataManager manager(tick_config);
    // Constructor handles initialization

    ticks.clear();
    ticks.reserve(60000000);  // Reserve for ~60M ticks

    Tick tick;
    while (manager.GetNextTick(tick)) {
        ticks.push_back(tick);
    }

    if (ticks.empty()) {
        throw std::runtime_error("Failed to load tick data or file empty: " + path);
    }

    std::cout << "Loaded " << ticks.size() << " ticks from " << path << std::endl;
}

// Create engine config for XAUUSD
TickBacktestConfig CreateEngineConfig() {
    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = 10000.0;
    config.account_currency = "USD";
    config.contract_size = 100.0;
    config.leverage = 500.0;
    config.margin_rate = 1.0;
    config.pip_size = 0.01;
    config.swap_long = -66.99;
    config.swap_short = 41.2;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.verbose = false;  // Disable trade logging for faster parallel execution
    return config;
}

// Run a single test configuration
TestResult RunTest(const TestConfig& cfg, const std::vector<Tick>& ticks, const std::string& year) {
    TestResult result;
    result.config_name = cfg.name;
    result.year = year;
    result.velocity_blocked = 0;
    result.velocity_allowed = 0;
    result.stopped_out = false;

    TickBacktestConfig engine_config = CreateEngineConfig();

    // Set date range based on year
    if (year == "2024") {
        engine_config.start_date = "2024.01.01";
        engine_config.end_date = "2024.12.31";
    } else {
        engine_config.start_date = "2025.01.01";
        engine_config.end_date = "2025.12.30";
    }

    try {
        TickBasedEngine engine(engine_config);

        if (cfg.is_baseline) {
            // Run baseline FillUpOscillation
            FillUpOscillation strategy(
                cfg.survive_pct,      // survive_pct
                1.50,                 // base_spacing
                0.01,                 // min_volume
                10.0,                 // max_volume
                100.0,                // contract_size
                500.0,                // leverage
                FillUpOscillation::ADAPTIVE_SPACING,
                0.1,                  // antifragile_scale (unused)
                30.0,                 // velocity_threshold (unused)
                cfg.lookback_hours    // volatility_lookback_hours
            );

            engine.RunWithTicks(ticks, [&strategy](const Tick& t, TickBasedEngine& e) {
                strategy.OnTick(t, e);
            });

        } else {
            // Run combined chaos or floating-only strategy
            StrategyCombinedChaos::Config strategy_config;
            strategy_config.survive_pct = cfg.survive_pct;
            strategy_config.base_spacing = 1.50;
            strategy_config.min_volume = 0.01;
            strategy_config.max_volume = 10.0;
            strategy_config.contract_size = 100.0;
            strategy_config.leverage = 500.0;
            strategy_config.ema_period = cfg.ema_period;
            strategy_config.tp_multiplier = cfg.tp_multiplier;
            strategy_config.adaptive_spacing = true;
            strategy_config.typical_vol_pct = 0.5;
            strategy_config.volatility_lookback_hours = cfg.lookback_hours;
            strategy_config.velocity_filter = cfg.velocity_filter;
            strategy_config.velocity_window = cfg.velocity_window;
            strategy_config.min_velocity_threshold = 0.0001;

            StrategyCombinedChaos strategy(strategy_config);

            engine.RunWithTicks(ticks, [&strategy](const Tick& t, TickBasedEngine& e) {
                strategy.OnTick(t, e);
            });

            result.velocity_blocked = strategy.GetVelocityEntriesBlocked();
            result.velocity_allowed = strategy.GetVelocityEntriesAllowed();
        }

        auto results = engine.GetResults();
        result.final_balance = results.final_balance;
        result.return_multiple = results.final_balance / engine_config.initial_balance;
        result.max_dd_pct = results.max_drawdown_pct;
        result.total_trades = results.total_trades;
        result.total_swap = results.total_swap_charged;
        result.stopped_out = results.stop_out_occurred;

    } catch (const std::exception& e) {
        result.final_balance = 0;
        result.return_multiple = 0;
        result.max_dd_pct = 100;
        result.total_trades = 0;
        result.total_swap = 0;
        result.stopped_out = true;
    }

    return result;
}

// Worker function for parallel execution
void Worker(std::queue<std::pair<TestConfig, std::string>>& work_queue,
            std::mutex& queue_mutex,
            std::vector<TestResult>& results,
            std::mutex& results_mutex,
            int total_tasks) {
    while (true) {
        std::pair<TestConfig, std::string> task;

        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            if (work_queue.empty()) break;
            task = work_queue.front();
            work_queue.pop();
        }

        const std::vector<Tick>& ticks = (task.second == "2024") ? g_ticks_2024 : g_ticks_2025;
        TestResult result = RunTest(task.first, ticks, task.second);

        {
            std::lock_guard<std::mutex> lock(results_mutex);
            results.push_back(result);
        }

        int completed = ++g_completed;
        if (completed % 10 == 0 || completed == total_tasks) {
            std::lock_guard<std::mutex> lock(g_output_mutex);
            std::cout << "\rProgress: " << completed << "/" << total_tasks
                      << " (" << (completed * 100 / total_tasks) << "%)" << std::flush;
        }
    }
}

int main() {
    std::cout << "=== COMBINED CHAOS STRATEGY TEST ===" << std::endl;
    std::cout << "Testing floating attractor + velocity zero-crossing + edge-of-chaos parameters" << std::endl;
    std::cout << std::endl;

    auto start_time = std::chrono::high_resolution_clock::now();

    // Load tick data for both years
    std::cout << "Loading tick data..." << std::endl;
    try {
        LoadTickData("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAUUSD_TICKS_2024.csv", g_ticks_2024);
        LoadTickData("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv", g_ticks_2025);
    } catch (const std::exception& e) {
        std::cerr << "Error loading tick data: " << e.what() << std::endl;
        return 1;
    }

    auto load_time = std::chrono::high_resolution_clock::now();
    auto load_duration = std::chrono::duration_cast<std::chrono::seconds>(load_time - start_time);
    std::cout << "Data loaded in " << load_duration.count() << "s" << std::endl;
    std::cout << std::endl;

    // Create test configurations
    std::vector<TestConfig> configs;

    // Parameter sweep values
    std::vector<int> ema_periods = {100, 200, 500};
    std::vector<double> tp_multipliers = {1.5, 2.0, 2.5};
    std::vector<double> survive_pcts = {12.0, 13.0};
    std::vector<double> lookback_hours = {4.0, 8.0};
    std::vector<bool> velocity_filters = {false, true};
    int velocity_window = 100;

    // Generate combined chaos configurations
    for (int ema : ema_periods) {
        for (double tp : tp_multipliers) {
            for (double survive : survive_pcts) {
                for (double lb : lookback_hours) {
                    for (bool vel : velocity_filters) {
                        TestConfig cfg;
                        std::ostringstream name;
                        name << "CC_ema" << ema << "_tp" << std::fixed << std::setprecision(1) << tp
                             << "_s" << (int)survive << "_lb" << (int)lb
                             << (vel ? "_velON" : "_velOFF");
                        cfg.name = name.str();
                        cfg.ema_period = ema;
                        cfg.tp_multiplier = tp;
                        cfg.survive_pct = survive;
                        cfg.lookback_hours = lb;
                        cfg.velocity_filter = vel;
                        cfg.velocity_window = velocity_window;
                        cfg.is_baseline = false;
                        cfg.is_floating_only = !vel;
                        configs.push_back(cfg);
                    }
                }
            }
        }
    }

    // Add baseline FillUpOscillation configurations for comparison
    for (double survive : survive_pcts) {
        for (double lb : lookback_hours) {
            TestConfig cfg;
            std::ostringstream name;
            name << "BASELINE_s" << (int)survive << "_lb" << (int)lb;
            cfg.name = name.str();
            cfg.ema_period = 0;
            cfg.tp_multiplier = 1.0;
            cfg.survive_pct = survive;
            cfg.lookback_hours = lb;
            cfg.velocity_filter = false;
            cfg.velocity_window = 0;
            cfg.is_baseline = true;
            cfg.is_floating_only = false;
            configs.push_back(cfg);
        }
    }

    std::cout << "Total configurations: " << configs.size() << std::endl;
    std::cout << "Total tests: " << configs.size() * 2 << " (x2 years)" << std::endl;
    std::cout << std::endl;

    // Create work queue (each config tested on both years)
    std::queue<std::pair<TestConfig, std::string>> work_queue;
    for (const auto& cfg : configs) {
        work_queue.push({cfg, "2024"});
        work_queue.push({cfg, "2025"});
    }

    int total_tasks = work_queue.size();

    // Results storage
    std::vector<TestResult> results;
    std::mutex queue_mutex, results_mutex;

    // Launch worker threads
    unsigned int num_threads = std::thread::hardware_concurrency();
    std::cout << "Launching " << num_threads << " threads..." << std::endl;

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < num_threads; i++) {
        threads.emplace_back(Worker, std::ref(work_queue), std::ref(queue_mutex),
                            std::ref(results), std::ref(results_mutex), total_tasks);
    }

    // Wait for completion
    for (auto& t : threads) {
        t.join();
    }
    std::cout << std::endl;

    auto end_time = std::chrono::high_resolution_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);
    std::cout << "All tests completed in " << total_duration.count() << "s" << std::endl;
    std::cout << std::endl;

    // Organize results by configuration (pairing 2024 and 2025)
    std::map<std::string, std::pair<TestResult, TestResult>> paired_results;
    for (const auto& r : results) {
        if (r.year == "2024") {
            paired_results[r.config_name].first = r;
        } else {
            paired_results[r.config_name].second = r;
        }
    }

    // Calculate regime ratios and sort by best combined performance
    struct SummaryRow {
        std::string name;
        double return_2024;
        double return_2025;
        double dd_2024;
        double dd_2025;
        double regime_ratio;
        double combined_return;
        bool stopped_out_2024;
        bool stopped_out_2025;
        int vel_blocked_2025;
        int vel_allowed_2025;
    };
    std::vector<SummaryRow> summary;

    for (const auto& p : paired_results) {
        SummaryRow row;
        row.name = p.first;
        row.return_2024 = p.second.first.return_multiple;
        row.return_2025 = p.second.second.return_multiple;
        row.dd_2024 = p.second.first.max_dd_pct;
        row.dd_2025 = p.second.second.max_dd_pct;
        row.stopped_out_2024 = p.second.first.stopped_out;
        row.stopped_out_2025 = p.second.second.stopped_out;
        row.vel_blocked_2025 = p.second.second.velocity_blocked;
        row.vel_allowed_2025 = p.second.second.velocity_allowed;

        // Regime ratio: 2025/2024 (lower is more regime-independent)
        if (row.return_2024 > 0.1) {
            row.regime_ratio = row.return_2025 / row.return_2024;
        } else {
            row.regime_ratio = 999.0;  // Invalid
        }

        // Combined return: geometric mean
        if (row.return_2024 > 0 && row.return_2025 > 0) {
            row.combined_return = std::sqrt(row.return_2024 * row.return_2025);
        } else {
            row.combined_return = 0;
        }

        summary.push_back(row);
    }

    // Sort by combined return (descending)
    std::sort(summary.begin(), summary.end(), [](const SummaryRow& a, const SummaryRow& b) {
        return a.combined_return > b.combined_return;
    });

    // Print detailed results
    std::cout << "=== DETAILED RESULTS ===" << std::endl;
    std::cout << std::left << std::setw(45) << "Configuration"
              << std::right << std::setw(10) << "Ret2024"
              << std::setw(10) << "Ret2025"
              << std::setw(9) << "DD2024"
              << std::setw(9) << "DD2025"
              << std::setw(9) << "Ratio"
              << std::setw(10) << "Combined"
              << std::setw(8) << "Status"
              << std::endl;
    std::cout << std::string(110, '-') << std::endl;

    for (const auto& row : summary) {
        std::string status = "";
        if (row.stopped_out_2024) status += "SO24 ";
        if (row.stopped_out_2025) status += "SO25";
        if (status.empty()) status = "ok";

        std::cout << std::left << std::setw(45) << row.name
                  << std::right << std::fixed << std::setprecision(2)
                  << std::setw(10) << row.return_2024 << "x"
                  << std::setw(9) << row.return_2025 << "x"
                  << std::setw(8) << row.dd_2024 << "%"
                  << std::setw(8) << row.dd_2025 << "%"
                  << std::setw(9) << row.regime_ratio
                  << std::setw(9) << row.combined_return << "x"
                  << std::setw(8) << status
                  << std::endl;
    }

    // Find best configurations
    std::cout << std::endl;
    std::cout << "=== ANALYSIS ===" << std::endl;

    // Best by combined return (excluding stopped out)
    std::cout << std::endl << "TOP 5 BY COMBINED RETURN (both years survived):" << std::endl;
    int count = 0;
    for (const auto& row : summary) {
        if (!row.stopped_out_2024 && !row.stopped_out_2025) {
            std::cout << "  " << (count+1) << ". " << row.name
                      << " | Combined: " << std::fixed << std::setprecision(2) << row.combined_return << "x"
                      << " | 2024: " << row.return_2024 << "x"
                      << " | 2025: " << row.return_2025 << "x"
                      << " | Ratio: " << row.regime_ratio
                      << std::endl;
            if (++count >= 5) break;
        }
    }

    // Best by regime ratio (lowest ratio = most regime-independent)
    std::vector<SummaryRow> ratio_sorted = summary;
    std::sort(ratio_sorted.begin(), ratio_sorted.end(), [](const SummaryRow& a, const SummaryRow& b) {
        if (a.stopped_out_2024 || a.stopped_out_2025) return false;
        if (b.stopped_out_2024 || b.stopped_out_2025) return true;
        return a.regime_ratio < b.regime_ratio;
    });

    std::cout << std::endl << "TOP 5 BY REGIME INDEPENDENCE (lowest 2025/2024 ratio):" << std::endl;
    count = 0;
    for (const auto& row : ratio_sorted) {
        if (!row.stopped_out_2024 && !row.stopped_out_2025 && row.regime_ratio < 100) {
            std::cout << "  " << (count+1) << ". " << row.name
                      << " | Ratio: " << std::fixed << std::setprecision(2) << row.regime_ratio
                      << " | 2024: " << row.return_2024 << "x"
                      << " | 2025: " << row.return_2025 << "x"
                      << std::endl;
            if (++count >= 5) break;
        }
    }

    // Compare velocity filter effect
    std::cout << std::endl << "=== VELOCITY FILTER ANALYSIS ===" << std::endl;
    std::cout << "Comparing configs with velocity filter ON vs OFF:" << std::endl;

    // Find matching pairs (same params, different velocity filter)
    for (const auto& row : summary) {
        if (row.name.find("_velON") != std::string::npos) {
            std::string off_name = row.name;
            size_t pos = off_name.find("_velON");
            off_name.replace(pos, 6, "_velOFF");

            // Find the OFF version
            for (const auto& row2 : summary) {
                if (row2.name == off_name) {
                    double ret_diff_2024 = row.return_2024 - row2.return_2024;
                    double ret_diff_2025 = row.return_2025 - row2.return_2025;
                    double dd_diff_2024 = row.dd_2024 - row2.dd_2024;
                    double dd_diff_2025 = row.dd_2025 - row2.dd_2025;

                    // Only print if both survived
                    if (!row.stopped_out_2024 && !row.stopped_out_2025 &&
                        !row2.stopped_out_2024 && !row2.stopped_out_2025) {
                        std::cout << row.name << ":" << std::endl;
                        std::cout << "  Return change: 2024=" << std::showpos << std::fixed << std::setprecision(2)
                                  << ret_diff_2024 << "x, 2025=" << ret_diff_2025 << "x" << std::noshowpos << std::endl;
                        std::cout << "  DD change: 2024=" << std::showpos << dd_diff_2024 << "%, 2025=" << dd_diff_2025 << "%" << std::noshowpos << std::endl;
                        std::cout << "  Velocity blocked: " << row.vel_blocked_2025 << ", allowed: " << row.vel_allowed_2025 << std::endl;
                    }
                    break;
                }
            }
        }
    }

    // Compare combined strategy vs baseline
    std::cout << std::endl << "=== BASELINE COMPARISON ===" << std::endl;
    std::cout << "Combined Chaos vs FillUpOscillation ADAPTIVE_SPACING:" << std::endl;

    // Find best baseline and best combined for each survive/lookback combo
    for (double survive : survive_pcts) {
        for (double lb : lookback_hours) {
            std::ostringstream baseline_name;
            baseline_name << "BASELINE_s" << (int)survive << "_lb" << (int)lb;

            // Find baseline
            const SummaryRow* baseline = nullptr;
            for (const auto& row : summary) {
                if (row.name == baseline_name.str()) {
                    baseline = &row;
                    break;
                }
            }

            if (!baseline) continue;

            // Find best combined with same survive/lookback
            const SummaryRow* best_combined = nullptr;
            double best_combined_return = 0;
            for (const auto& row : summary) {
                if (row.name.find("CC_") != std::string::npos &&
                    row.name.find("_s" + std::to_string((int)survive) + "_") != std::string::npos &&
                    row.name.find("_lb" + std::to_string((int)lb) + "_") != std::string::npos &&
                    !row.stopped_out_2024 && !row.stopped_out_2025) {
                    if (row.combined_return > best_combined_return) {
                        best_combined_return = row.combined_return;
                        best_combined = &row;
                    }
                }
            }

            std::cout << std::endl << "survive=" << (int)survive << "%, lookback=" << (int)lb << "h:" << std::endl;
            std::cout << "  Baseline: " << baseline_name.str()
                      << " | 2024=" << std::fixed << std::setprecision(2) << baseline->return_2024 << "x"
                      << " | 2025=" << baseline->return_2025 << "x"
                      << " | Combined=" << baseline->combined_return << "x"
                      << " | Ratio=" << baseline->regime_ratio
                      << (baseline->stopped_out_2024 || baseline->stopped_out_2025 ? " [STOPPED OUT]" : "")
                      << std::endl;

            if (best_combined) {
                double improvement = (best_combined->combined_return - baseline->combined_return) / baseline->combined_return * 100;
                std::cout << "  Best CC:  " << best_combined->name
                          << " | 2024=" << best_combined->return_2024 << "x"
                          << " | 2025=" << best_combined->return_2025 << "x"
                          << " | Combined=" << best_combined->combined_return << "x"
                          << " | Ratio=" << best_combined->regime_ratio
                          << std::endl;
                std::cout << "  Improvement: " << std::showpos << improvement << "%" << std::noshowpos << std::endl;
            }
        }
    }

    // Final recommendation
    std::cout << std::endl << "=== RECOMMENDATION ===" << std::endl;

    // Find best overall config
    const SummaryRow* best_overall = nullptr;
    for (const auto& row : summary) {
        if (!row.stopped_out_2024 && !row.stopped_out_2025) {
            if (!best_overall || row.combined_return > best_overall->combined_return) {
                best_overall = &row;
            }
        }
    }

    if (best_overall) {
        std::cout << "Best configuration for production:" << std::endl;
        std::cout << "  Name: " << best_overall->name << std::endl;
        std::cout << "  2024 Return: " << std::fixed << std::setprecision(2) << best_overall->return_2024 << "x" << std::endl;
        std::cout << "  2025 Return: " << best_overall->return_2025 << "x" << std::endl;
        std::cout << "  Combined Return: " << best_overall->combined_return << "x" << std::endl;
        std::cout << "  Regime Ratio: " << best_overall->regime_ratio << " (lower = more stable)" << std::endl;
        std::cout << "  2024 Max DD: " << best_overall->dd_2024 << "%" << std::endl;
        std::cout << "  2025 Max DD: " << best_overall->dd_2025 << "%" << std::endl;
    }

    std::cout << std::endl << "=== TEST COMPLETE ===" << std::endl;

    return 0;
}
