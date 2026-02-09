/**
 * test_mt5_simulation.cpp
 *
 * Simulate MT5 Strategy Tester conditions to match its 13.37x result:
 * 1. Wider minimum spread (broker-enforced)
 * 2. Tick subsampling (aggregation)
 * 3. Combination of both
 *
 * Goal: Find the configuration that produces ~13.37x return
 */

#include "../include/strategy_combined_ju.h"
#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <thread>
#include <mutex>
#include <queue>

using namespace backtest;

std::vector<Tick> g_ticks;
std::mutex g_print_mutex;

void LoadTicks() {
    std::cout << "Loading tick data..." << std::endl;
    std::vector<std::string> files = {
        "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv",
        "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_JAN2026.csv"
    };
    for (const auto& file : files) {
        TickDataManager mgr(file);
        Tick tick;
        while (mgr.GetNextTick(tick)) {
            g_ticks.push_back(tick);
        }
    }
    std::sort(g_ticks.begin(), g_ticks.end(),
              [](const Tick& a, const Tick& b) { return a.timestamp < b.timestamp; });
    std::cout << "Loaded " << g_ticks.size() << " ticks" << std::endl;
}

struct SimConfig {
    std::string name;
    double min_spread;      // Minimum spread to enforce
    int subsample_rate;     // Use every Nth tick (1 = all ticks)
};

struct TestResult {
    std::string name;
    double return_mult;
    double max_dd;
    int trades;
    long velocity_blocks;
    long entries;
    int ticks_processed;
    bool stopped_out;
};

TestResult RunSimulation(const SimConfig& sim_cfg, const std::vector<Tick>& ticks) {
    // Pre-filter ticks: apply subsampling and spread adjustment
    std::vector<Tick> filtered_ticks;
    int tick_counter = 0;

    for (const auto& raw_tick : ticks) {
        tick_counter++;

        // Subsample: only process every Nth tick
        if (tick_counter % sim_cfg.subsample_rate != 0) {
            continue;
        }

        // Create modified tick with enforced minimum spread
        Tick tick = raw_tick;
        double actual_spread = tick.ask - tick.bid;
        if (actual_spread < sim_cfg.min_spread) {
            // Widen the spread symmetrically
            double add = (sim_cfg.min_spread - actual_spread) / 2.0;
            tick.bid -= add;
            tick.ask += add;
        }

        filtered_ticks.push_back(tick);
    }

    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = 10000.0;
    config.contract_size = 100.0;
    config.leverage = 500.0;
    config.pip_size = 0.01;
    config.swap_long = -68.25;
    config.swap_short = 35.06;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = "2025.01.01";
    config.end_date = "2026.01.27";

    TickDataConfig tick_cfg;
    tick_cfg.format = TickDataFormat::MT5_CSV;
    config.tick_data_config = tick_cfg;

    TickBasedEngine engine(config);

    StrategyCombinedJu::Config strat_cfg;
    strat_cfg.survive_pct = 13.0;
    strat_cfg.base_spacing = 1.5;
    strat_cfg.volatility_lookback_hours = 4.0;
    strat_cfg.typical_vol_pct = 0.55;

    // Rubber Band TP
    strat_cfg.tp_mode = StrategyCombinedJu::SQRT;
    strat_cfg.tp_sqrt_scale = 0.5;
    strat_cfg.tp_min = 1.5;

    // Velocity Filter (enabled)
    strat_cfg.enable_velocity_filter = true;
    strat_cfg.velocity_window = 10;
    strat_cfg.velocity_threshold_pct = 0.01;

    // Threshold Barbell Sizing P1_M3
    strat_cfg.sizing_mode = StrategyCombinedJu::THRESHOLD_SIZING;
    strat_cfg.sizing_threshold_pos = 1;
    strat_cfg.sizing_threshold_mult = 3.0;

    StrategyCombinedJu strategy(strat_cfg);

    engine.RunWithTicks(filtered_ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();
    auto& stats = strategy.GetStats();

    TestResult r;
    r.name = sim_cfg.name;
    r.return_mult = results.final_balance / 10000.0;
    r.max_dd = results.max_drawdown_pct;
    r.trades = results.total_trades;
    r.velocity_blocks = stats.velocity_blocks;
    r.entries = stats.entries_allowed;
    r.ticks_processed = (int)filtered_ticks.size();
    r.stopped_out = results.stop_out_occurred;

    return r;
}

int main() {
    std::cout << "=" << std::string(90, '=') << std::endl;
    std::cout << "MT5 SIMULATION TEST" << std::endl;
    std::cout << "Finding configuration that matches MT5's 13.37x return" << std::endl;
    std::cout << "=" << std::string(90, '=') << std::endl << std::endl;

    LoadTicks();

    // Calculate baseline spread
    double sum_spread = 0;
    for (size_t i = 0; i < std::min((size_t)100000, g_ticks.size()); i++) {
        sum_spread += (g_ticks[i].ask - g_ticks[i].bid);
    }
    double avg_spread = sum_spread / 100000;
    std::cout << "Average spread in raw data: $" << std::fixed << std::setprecision(4) << avg_spread << std::endl;
    std::cout << "Target return: 13.37x (MT5 result)" << std::endl << std::endl;

    // Define test configurations
    std::vector<SimConfig> configs = {
        // Baseline (no changes)
        {"BASELINE", 0.0, 1},

        // Wider spread only
        {"SPREAD_0.15", 0.15, 1},
        {"SPREAD_0.20", 0.20, 1},
        {"SPREAD_0.25", 0.25, 1},
        {"SPREAD_0.30", 0.30, 1},
        {"SPREAD_0.35", 0.35, 1},
        {"SPREAD_0.40", 0.40, 1},

        // Tick subsampling only
        {"SUBSAMPLE_2", 0.0, 2},
        {"SUBSAMPLE_3", 0.0, 3},
        {"SUBSAMPLE_4", 0.0, 4},
        {"SUBSAMPLE_5", 0.0, 5},

        // Combined: spread + subsampling
        {"SP0.20_SUB2", 0.20, 2},
        {"SP0.20_SUB3", 0.20, 3},
        {"SP0.25_SUB2", 0.25, 2},
        {"SP0.25_SUB3", 0.25, 3},
        {"SP0.30_SUB2", 0.30, 2},
        {"SP0.30_SUB3", 0.30, 3},
        {"SP0.35_SUB2", 0.35, 2},
        {"SP0.35_SUB3", 0.35, 3},
        {"SP0.40_SUB2", 0.40, 2},
        {"SP0.40_SUB3", 0.40, 3},
    };

    std::vector<TestResult> results;

    // Run tests (not parallel - too much memory needed for multiple engines)
    int total = configs.size();
    int done = 0;
    for (const auto& cfg : configs) {
        done++;
        std::cout << "[" << done << "/" << total << "] Testing " << cfg.name << "..." << std::endl;
        results.push_back(RunSimulation(cfg, g_ticks));
    }

    // Print results
    std::cout << "\n" << std::string(140, '=') << std::endl;
    std::cout << "RESULTS" << std::endl;
    std::cout << std::string(140, '=') << std::endl;

    std::cout << std::left << std::setw(18) << "Config"
              << std::right << std::setw(10) << "Return"
              << std::setw(10) << "MaxDD"
              << std::setw(10) << "Trades"
              << std::setw(14) << "VelBlocks"
              << std::setw(12) << "Entries"
              << std::setw(12) << "Ticks"
              << std::setw(12) << "vs MT5"
              << std::setw(10) << "Status" << std::endl;
    std::cout << std::string(140, '-') << std::endl;

    std::vector<std::pair<double, std::string>> matches;

    for (const auto& r : results) {
        double diff = std::abs(r.return_mult - 13.37);
        std::string vs_mt5 = std::to_string((int)(r.return_mult / 13.37 * 100)) + "%";
        if (diff < 1.0) {
            vs_mt5 = "CLOSE!";
            matches.push_back({diff, r.name});
        }
        if (diff < 0.5) {
            vs_mt5 = "MATCH!";
        }

        std::cout << std::left << std::setw(18) << r.name
                  << std::right << std::fixed << std::setprecision(2)
                  << std::setw(8) << r.return_mult << "x"
                  << std::setw(9) << r.max_dd << "%"
                  << std::setw(10) << r.trades
                  << std::setw(14) << r.velocity_blocks
                  << std::setw(12) << r.entries
                  << std::setw(12) << r.ticks_processed
                  << std::setw(12) << vs_mt5
                  << std::setw(10) << (r.stopped_out ? "STOP-OUT" : "OK") << std::endl;
    }

    // Find closest match
    std::cout << "\n" << std::string(90, '=') << std::endl;
    std::cout << "ANALYSIS" << std::endl;
    std::cout << std::string(90, '=') << std::endl;

    std::sort(matches.begin(), matches.end());

    std::cout << "\nClosest configurations to MT5's 13.37x:" << std::endl;
    if (matches.empty()) {
        // Find top 5 closest anyway
        std::vector<std::pair<double, std::string>> all_diffs;
        for (const auto& r : results) {
            if (!r.stopped_out) {
                all_diffs.push_back({std::abs(r.return_mult - 13.37), r.name});
            }
        }
        std::sort(all_diffs.begin(), all_diffs.end());
        for (size_t i = 0; i < std::min((size_t)5, all_diffs.size()); i++) {
            for (const auto& r : results) {
                if (r.name == all_diffs[i].second) {
                    std::cout << "  " << (i+1) << ". " << r.name << ": "
                              << r.return_mult << "x (diff: " << all_diffs[i].first << ")" << std::endl;
                }
            }
        }
    } else {
        for (size_t i = 0; i < std::min((size_t)5, matches.size()); i++) {
            for (const auto& r : results) {
                if (r.name == matches[i].second) {
                    std::cout << "  " << (i+1) << ". " << r.name << ": "
                              << r.return_mult << "x (diff: " << matches[i].first << ")" << std::endl;
                }
            }
        }
    }

    // Impact analysis
    std::cout << "\n=== FACTOR ANALYSIS ===" << std::endl;

    double baseline_return = 0;
    for (const auto& r : results) {
        if (r.name == "BASELINE") baseline_return = r.return_mult;
    }

    std::cout << "\nSpread impact (subsample=1):" << std::endl;
    for (const auto& r : results) {
        if (r.name.find("SPREAD") != std::string::npos) {
            double impact = (r.return_mult / baseline_return - 1.0) * 100;
            std::cout << "  " << r.name << ": " << std::showpos << std::fixed << std::setprecision(1)
                      << impact << "%" << std::noshowpos << std::endl;
        }
    }

    std::cout << "\nSubsampling impact (no spread adjustment):" << std::endl;
    for (const auto& r : results) {
        if (r.name.find("SUBSAMPLE") != std::string::npos) {
            double impact = (r.return_mult / baseline_return - 1.0) * 100;
            std::cout << "  " << r.name << ": " << std::showpos << std::fixed << std::setprecision(1)
                      << impact << "%" << std::noshowpos << std::endl;
        }
    }

    std::cout << "\n=== CONCLUSION ===" << std::endl;
    std::cout << R"(
If a configuration matches ~13.37x, it suggests MT5 uses that
spread/tick-frequency combination.

Typical MT5 Strategy Tester behavior:
- Uses broker's minimum spread (often 0.15-0.30 for XAUUSD)
- May aggregate ticks that arrive simultaneously
- OnTick() frequency depends on tick arrival, not raw data rate

The matching configuration tells us what MT5 is actually simulating.
)" << std::endl;

    return 0;
}
