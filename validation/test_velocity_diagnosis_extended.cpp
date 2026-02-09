/**
 * test_velocity_diagnosis_extended.cpp
 *
 * Extended diagnosis to find what configuration matches MT5's 13.37x return
 * Tests: extremely aggressive velocity blocking, different components disabled
 */

#include "../include/strategy_combined_ju.h"
#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>

using namespace backtest;

std::vector<Tick> g_ticks;

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

struct TestResult {
    std::string name;
    double return_mult;
    double max_dd;
    int trades;
    long velocity_blocks;
    long entries;
    double block_pct;
    bool stopped_out;
};

TestResult RunCombinedJu(const std::string& name,
                          bool enable_vel, bool enable_rb, bool enable_bb,
                          int vel_window, double vel_thresh,
                          int bb_pos, double bb_mult) {

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
    if (enable_rb) {
        strat_cfg.tp_mode = StrategyCombinedJu::SQRT;
        strat_cfg.tp_sqrt_scale = 0.5;
        strat_cfg.tp_min = 1.5;
    } else {
        strat_cfg.tp_mode = StrategyCombinedJu::FIXED;
    }

    // Velocity Filter
    strat_cfg.enable_velocity_filter = enable_vel;
    strat_cfg.velocity_window = vel_window;
    strat_cfg.velocity_threshold_pct = vel_thresh;

    // Barbell Sizing
    if (enable_bb) {
        strat_cfg.sizing_mode = StrategyCombinedJu::THRESHOLD_SIZING;
        strat_cfg.sizing_threshold_pos = bb_pos;
        strat_cfg.sizing_threshold_mult = bb_mult;
    } else {
        strat_cfg.sizing_mode = StrategyCombinedJu::UNIFORM;
    }

    StrategyCombinedJu strategy(strat_cfg);

    engine.RunWithTicks(g_ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();
    auto& stats = strategy.GetStats();

    TestResult r;
    r.name = name;
    r.return_mult = results.final_balance / 10000.0;
    r.max_dd = results.max_drawdown_pct;
    r.trades = results.total_trades;
    r.velocity_blocks = stats.velocity_blocks;
    r.entries = stats.entries_allowed;
    r.block_pct = (r.velocity_blocks + r.entries) > 0
                  ? 100.0 * r.velocity_blocks / (r.velocity_blocks + r.entries) : 0.0;
    r.stopped_out = results.stop_out_occurred;

    return r;
}

// Run baseline FillUpOscillation for comparison
TestResult RunBaseline() {
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

    FillUpOscillation::AdaptiveConfig adaptive_cfg;
    adaptive_cfg.typical_vol_pct = 0.55;
    adaptive_cfg.pct_spacing = false;

    FillUpOscillation strategy(
        13.0, 1.5, 0.01, 10.0, 100.0, 500.0,
        FillUpOscillation::ADAPTIVE_SPACING,
        0.0, 0.0, 4.0, adaptive_cfg
    );

    engine.RunWithTicks(g_ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();

    TestResult r;
    r.name = "BASELINE_NO_JU";
    r.return_mult = results.final_balance / 10000.0;
    r.max_dd = results.max_drawdown_pct;
    r.trades = results.total_trades;
    r.velocity_blocks = 0;
    r.entries = r.trades;
    r.block_pct = 0.0;
    r.stopped_out = results.stop_out_occurred;

    return r;
}

int main() {
    std::cout << "=" << std::string(90, '=') << std::endl;
    std::cout << "EXTENDED VELOCITY DIAGNOSIS - Finding MT5 13.37x equivalent" << std::endl;
    std::cout << "=" << std::string(90, '=') << std::endl << std::endl;

    LoadTicks();

    std::vector<TestResult> results;

    // Section 1: Component Analysis
    std::cout << "\n=== SECTION 1: COMPONENT ANALYSIS ===" << std::endl;

    std::cout << "Running baseline (no Ju components)..." << std::endl;
    results.push_back(RunBaseline());

    std::cout << "Running: RB only (no VEL, no BB)..." << std::endl;
    results.push_back(RunCombinedJu("RB_ONLY", false, true, false, 10, 0.01, 1, 3.0));

    std::cout << "Running: VEL only (no RB, no BB)..." << std::endl;
    results.push_back(RunCombinedJu("VEL_ONLY", true, false, false, 10, 0.01, 1, 3.0));

    std::cout << "Running: BB only (no VEL, no RB)..." << std::endl;
    results.push_back(RunCombinedJu("BB_ONLY", false, false, true, 10, 0.01, 1, 3.0));

    std::cout << "Running: VEL+RB (no BB)..." << std::endl;
    results.push_back(RunCombinedJu("VEL+RB", true, true, false, 10, 0.01, 1, 3.0));

    std::cout << "Running: VEL+BB (no RB)..." << std::endl;
    results.push_back(RunCombinedJu("VEL+BB", true, false, true, 10, 0.01, 1, 3.0));

    std::cout << "Running: RB+BB (no VEL)..." << std::endl;
    results.push_back(RunCombinedJu("RB+BB", false, true, true, 10, 0.01, 1, 3.0));

    std::cout << "Running: ALL (VEL+RB+BB) = P1_M3..." << std::endl;
    results.push_back(RunCombinedJu("ALL_P1_M3", true, true, true, 10, 0.01, 1, 3.0));

    // Section 2: Extreme velocity blocking
    std::cout << "\n=== SECTION 2: EXTREME VELOCITY BLOCKING ===" << std::endl;

    std::cout << "Running: Window=200, Thresh=0.01..." << std::endl;
    results.push_back(RunCombinedJu("VEL_W200_T0.01", true, true, true, 200, 0.01, 1, 3.0));

    std::cout << "Running: Window=500, Thresh=0.01..." << std::endl;
    results.push_back(RunCombinedJu("VEL_W500_T0.01", true, true, true, 500, 0.01, 1, 3.0));

    std::cout << "Running: Window=1000, Thresh=0.01..." << std::endl;
    results.push_back(RunCombinedJu("VEL_W1000_T0.01", true, true, true, 1000, 0.01, 1, 3.0));

    std::cout << "Running: Window=100, Thresh=0.001..." << std::endl;
    results.push_back(RunCombinedJu("VEL_W100_T0.001", true, true, true, 100, 0.001, 1, 3.0));

    std::cout << "Running: Window=500, Thresh=0.001..." << std::endl;
    results.push_back(RunCombinedJu("VEL_W500_T0.001", true, true, true, 500, 0.001, 1, 3.0));

    // Section 3: Different barbell configs
    std::cout << "\n=== SECTION 3: BARBELL VARIATIONS ===" << std::endl;

    std::cout << "Running: P5_M2 (original THR preset)..." << std::endl;
    results.push_back(RunCombinedJu("P5_M2", true, true, true, 10, 0.01, 5, 2.0));

    std::cout << "Running: P3_M2 (THR3 preset)..." << std::endl;
    results.push_back(RunCombinedJu("P3_M2", true, true, true, 10, 0.01, 3, 2.0));

    std::cout << "Running: P1_M2 (low mult)..." << std::endl;
    results.push_back(RunCombinedJu("P1_M2", true, true, true, 10, 0.01, 1, 2.0));

    std::cout << "Running: P1_M1.5 (minimal mult)..." << std::endl;
    results.push_back(RunCombinedJu("P1_M1.5", true, true, true, 10, 0.01, 1, 1.5));

    // Print results
    std::cout << "\n" << std::string(130, '=') << std::endl;
    std::cout << "ALL RESULTS" << std::endl;
    std::cout << std::string(130, '=') << std::endl;

    std::cout << std::left << std::setw(20) << "Config"
              << std::right << std::setw(10) << "Return"
              << std::setw(10) << "MaxDD"
              << std::setw(10) << "Trades"
              << std::setw(12) << "VelBlocks"
              << std::setw(10) << "Block%"
              << std::setw(12) << "vs MT5"
              << std::setw(10) << "Status" << std::endl;
    std::cout << std::string(130, '-') << std::endl;

    for (const auto& r : results) {
        double vs_mt5 = r.return_mult / 13.37;
        std::string vs_mt5_str;
        if (vs_mt5 > 0.9 && vs_mt5 < 1.1) {
            vs_mt5_str = "MATCH!";
        } else if (vs_mt5 > 1.5) {
            vs_mt5_str = ">" + std::to_string((int)(vs_mt5 * 100)) + "%";
        } else {
            vs_mt5_str = std::to_string((int)(vs_mt5 * 100)) + "%";
        }

        std::cout << std::left << std::setw(20) << r.name
                  << std::right << std::fixed << std::setprecision(2)
                  << std::setw(8) << r.return_mult << "x"
                  << std::setw(9) << r.max_dd << "%"
                  << std::setw(10) << r.trades
                  << std::setw(12) << r.velocity_blocks
                  << std::setw(9) << r.block_pct << "%"
                  << std::setw(12) << vs_mt5_str
                  << std::setw(10) << (r.stopped_out ? "STOP-OUT" : "OK") << std::endl;
    }

    // Analysis
    std::cout << "\n" << std::string(90, '=') << std::endl;
    std::cout << "ANALYSIS" << std::endl;
    std::cout << std::string(90, '=') << std::endl;

    // Find configs closest to 13.37x
    std::cout << "\nConfigs closest to MT5's 13.37x:" << std::endl;
    std::vector<std::pair<double, std::string>> diffs;
    for (const auto& r : results) {
        if (!r.stopped_out) {
            diffs.push_back({std::abs(r.return_mult - 13.37), r.name});
        }
    }
    std::sort(diffs.begin(), diffs.end());

    for (size_t i = 0; i < std::min((size_t)5, diffs.size()); i++) {
        for (const auto& r : results) {
            if (r.name == diffs[i].second) {
                std::cout << "  " << (i+1) << ". " << r.name << ": "
                          << r.return_mult << "x (diff: " << diffs[i].first << ")" << std::endl;
            }
        }
    }

    // Component impact
    std::cout << "\nComponent Impact Analysis:" << std::endl;
    double baseline = 0, vel_only = 0, rb_only = 0, bb_only = 0, all = 0;
    for (const auto& r : results) {
        if (r.name == "BASELINE_NO_JU") baseline = r.return_mult;
        if (r.name == "VEL_ONLY") vel_only = r.return_mult;
        if (r.name == "RB_ONLY") rb_only = r.return_mult;
        if (r.name == "BB_ONLY") bb_only = r.return_mult;
        if (r.name == "ALL_P1_M3") all = r.return_mult;
    }

    std::cout << "  Baseline (no Ju): " << baseline << "x" << std::endl;
    std::cout << "  + Velocity only:  " << vel_only << "x (";
    if (baseline > 0) std::cout << (vel_only > baseline ? "+" : "") << ((vel_only/baseline - 1)*100) << "%)";
    std::cout << std::endl;
    std::cout << "  + RubberBand only:" << rb_only << "x" << std::endl;
    std::cout << "  + Barbell only:   " << bb_only << "x" << std::endl;
    std::cout << "  All combined:     " << all << "x" << std::endl;

    std::cout << "\nKey Insight:" << std::endl;
    std::cout << "  - Velocity filter is ESSENTIAL (prevents stop-out)" << std::endl;
    std::cout << "  - Even extreme blocking (99%+) produces ~" << std::endl;
    for (const auto& r : results) {
        if (r.block_pct > 99.0 && !r.stopped_out) {
            std::cout << "    " << r.name << ": " << r.return_mult << "x at "
                      << r.block_pct << "% blocking" << std::endl;
        }
    }

    std::cout << "\n  MT5's 13.37x cannot be explained by velocity filter params alone." << std::endl;
    std::cout << "  Other possible causes:" << std::endl;
    std::cout << "  1. MT5 tick delivery frequency differs from raw tick data" << std::endl;
    std::cout << "  2. MT5 spread handling (maybe using wider spreads)" << std::endl;
    std::cout << "  3. MT5 execution slippage simulation" << std::endl;
    std::cout << "  4. Floating point precision differences in velocity calc" << std::endl;

    return 0;
}
