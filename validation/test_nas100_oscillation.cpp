/**
 * NAS100 Oscillation Strategy Test
 *
 * Adapts and tests the oscillation-optimized fill-up strategy on NAS100 data.
 * Compares: Baseline, Adaptive Spacing, Anti-fragile, Velocity Filter, All Combined
 *
 * Key NAS100 parameters vs XAUUSD:
 * - Price: ~20,000 (vs ~2,600) - 7.7x ratio
 * - Contract size: 1.0 (vs 100.0)
 * - Typical spacing: $10-15 (vs $1-2 for gold)
 * - Velocity threshold: $200/hr (vs $30/hr for gold)
 * - 4hr volatility: ~$100 (vs ~$10 for gold)
 */

#include "../include/fill_up_oscillation_nas100.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>

using namespace backtest;

struct TestResult {
    std::string mode_name;
    double survive_pct;
    double spacing;
    double final_equity;
    double return_x;
    double max_dd_pct;
    int velocity_pauses;
    int spacing_changes;
    bool margin_call;
    size_t total_trades;
};

TestResult RunTest(FillUpOscillationNAS100::Mode mode, const std::string& mode_name,
                   double survive_pct, double spacing, double velocity_threshold = 200.0,
                   double volatility_baseline = 100.0) {
    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\NAS100\\NAS100_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickBacktestConfig config;
    config.symbol = "NAS100";
    config.initial_balance = 10000.0;
    config.account_currency = "USD";
    config.contract_size = 1.0;       // NAS100 contract size
    config.leverage = 500.0;
    config.margin_rate = 1.0;
    config.pip_size = 0.01;
    // NAS100 swap rates (approximate - less impact than gold)
    config.swap_long = -15.0;
    config.swap_short = 5.0;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = "2025.01.15";  // Data starts Jan 15
    config.end_date = "2025.12.30";
    config.tick_data_config = tick_config;

    TestResult result;
    result.mode_name = mode_name;
    result.survive_pct = survive_pct;
    result.spacing = spacing;
    result.margin_call = false;

    try {
        TickBasedEngine engine(config);

        FillUpOscillationNAS100 strategy(
            survive_pct,           // survive_pct
            spacing,               // base_spacing
            0.01,                  // min_volume
            5.0,                   // max_volume (lower than gold due to contract size)
            1.0,                   // contract_size
            500.0,                 // leverage
            mode,                  // mode
            0.1,                   // antifragile_scale (10% per 5% DD)
            velocity_threshold,    // velocity_threshold ($200/hour for NAS100)
            4.0,                   // volatility_lookback_hours
            volatility_baseline    // volatility_baseline (NAS100 4hr range ~$100)
        );

        double peak_equity = config.initial_balance;
        double max_dd_pct = 0.0;

        engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);

            double equity = eng.GetEquity();
            if (equity > peak_equity) peak_equity = equity;
            double dd = (peak_equity - equity) / peak_equity * 100.0;
            if (dd > max_dd_pct) max_dd_pct = dd;
        });

        auto results = engine.GetResults();
        result.final_equity = results.final_balance;
        result.return_x = results.final_balance / config.initial_balance;
        result.max_dd_pct = max_dd_pct;
        result.velocity_pauses = strategy.GetVelocityPauseCount();
        result.spacing_changes = strategy.GetAdaptiveSpacingChanges();
        result.total_trades = results.total_trades;
        // Margin call detected if balance dropped to near-zero or stop-out occurred
        result.margin_call = (results.final_balance < config.initial_balance * 0.05) || engine.IsStopOutOccurred();

    } catch (const std::exception& e) {
        std::cerr << "Error in " << mode_name << ": " << e.what() << std::endl;
        result.final_equity = 0;
        result.return_x = 0;
        result.max_dd_pct = 100;
        result.margin_call = true;
        result.total_trades = 0;
    }

    return result;
}

void PrintResultRow(const TestResult& r) {
    std::cout << std::setw(18) << r.mode_name
              << std::setw(8) << r.survive_pct << "%"
              << std::setw(8) << "$" << r.spacing
              << std::setw(12) << "$" << std::fixed << std::setprecision(0) << r.final_equity
              << std::setw(8) << std::setprecision(2) << r.return_x << "x"
              << std::setw(8) << r.max_dd_pct << "%"
              << std::setw(8) << r.velocity_pauses
              << std::setw(10) << r.spacing_changes
              << std::setw(8) << r.total_trades
              << std::setw(10) << (r.margin_call ? "MARGIN" : "OK") << std::endl;
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "     NAS100 OSCILLATION STRATEGY TEST" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "\nKey NAS100 parameters:" << std::endl;
    std::cout << "  - Contract size: 1.0 (vs XAUUSD 100.0)" << std::endl;
    std::cout << "  - Price level: ~20,000 (vs XAUUSD ~2,600)" << std::endl;
    std::cout << "  - Spacing: $10-15 (scaled from gold's $1-2)" << std::endl;
    std::cout << "  - Velocity threshold: $200/hr (vs gold's $30/hr)" << std::endl;
    std::cout << std::endl;

    std::vector<TestResult> results;

    //=========================================================================
    // TEST 1: Mode Comparison with Conservative Parameters (Survive=30%)
    //=========================================================================
    std::cout << "=== TEST 1: Mode Comparison (Survive=30%, Spacing=$10) ===" << std::endl;
    std::cout << std::string(106, '-') << std::endl;
    std::cout << std::setw(18) << "Mode"
              << std::setw(9) << "Survive"
              << std::setw(9) << "Space"
              << std::setw(12) << "Final $"
              << std::setw(9) << "Return"
              << std::setw(9) << "MaxDD"
              << std::setw(8) << "VelPause"
              << std::setw(10) << "SpacChg"
              << std::setw(8) << "Trades"
              << std::setw(10) << "Status" << std::endl;
    std::cout << std::string(106, '-') << std::endl;

    std::cout << "Running Baseline..." << std::flush;
    results.push_back(RunTest(FillUpOscillationNAS100::BASELINE, "Baseline", 30.0, 10.0));
    std::cout << "\r";
    PrintResultRow(results.back());

    std::cout << "Running Adaptive..." << std::flush;
    results.push_back(RunTest(FillUpOscillationNAS100::ADAPTIVE_SPACING, "Adaptive Spacing", 30.0, 10.0));
    std::cout << "\r";
    PrintResultRow(results.back());

    std::cout << "Running Antifragile..." << std::flush;
    results.push_back(RunTest(FillUpOscillationNAS100::ANTIFRAGILE, "Anti-fragile", 30.0, 10.0));
    std::cout << "\r";
    PrintResultRow(results.back());

    std::cout << "Running Velocity..." << std::flush;
    results.push_back(RunTest(FillUpOscillationNAS100::VELOCITY_FILTER, "Velocity Filter", 30.0, 10.0));
    std::cout << "\r";
    PrintResultRow(results.back());

    std::cout << "Running All Combined..." << std::flush;
    results.push_back(RunTest(FillUpOscillationNAS100::ALL_COMBINED, "All Combined", 30.0, 10.0));
    std::cout << "\r";
    PrintResultRow(results.back());

    std::cout << std::string(106, '=') << std::endl;

    //=========================================================================
    // TEST 2: Aggressive Parameters (Survive=15%)
    //=========================================================================
    std::cout << "\n=== TEST 2: Aggressive Parameters (Survive=15%, Spacing=$10) ===" << std::endl;
    std::cout << std::string(106, '-') << std::endl;
    std::cout << std::setw(18) << "Mode"
              << std::setw(9) << "Survive"
              << std::setw(9) << "Space"
              << std::setw(12) << "Final $"
              << std::setw(9) << "Return"
              << std::setw(9) << "MaxDD"
              << std::setw(8) << "VelPause"
              << std::setw(10) << "SpacChg"
              << std::setw(8) << "Trades"
              << std::setw(10) << "Status" << std::endl;
    std::cout << std::string(106, '-') << std::endl;

    std::cout << "Running Baseline..." << std::flush;
    results.push_back(RunTest(FillUpOscillationNAS100::BASELINE, "Baseline", 15.0, 10.0));
    std::cout << "\r";
    PrintResultRow(results.back());

    std::cout << "Running Adaptive..." << std::flush;
    results.push_back(RunTest(FillUpOscillationNAS100::ADAPTIVE_SPACING, "Adaptive Spacing", 15.0, 10.0));
    std::cout << "\r";
    PrintResultRow(results.back());

    std::cout << "Running All Combined..." << std::flush;
    results.push_back(RunTest(FillUpOscillationNAS100::ALL_COMBINED, "All Combined", 15.0, 10.0));
    std::cout << "\r";
    PrintResultRow(results.back());

    std::cout << std::string(106, '=') << std::endl;

    //=========================================================================
    // TEST 3: Spacing Sweep (Baseline mode)
    //=========================================================================
    std::cout << "\n=== TEST 3: Spacing Sweep (Baseline, Survive=30%) ===" << std::endl;
    std::cout << std::string(106, '-') << std::endl;
    std::cout << std::setw(18) << "Mode"
              << std::setw(9) << "Survive"
              << std::setw(9) << "Space"
              << std::setw(12) << "Final $"
              << std::setw(9) << "Return"
              << std::setw(9) << "MaxDD"
              << std::setw(8) << "VelPause"
              << std::setw(10) << "SpacChg"
              << std::setw(8) << "Trades"
              << std::setw(10) << "Status" << std::endl;
    std::cout << std::string(106, '-') << std::endl;

    for (double spacing : {5.0, 10.0, 15.0, 25.0, 50.0}) {
        std::cout << "Running spacing $" << spacing << "..." << std::flush;
        auto r = RunTest(FillUpOscillationNAS100::BASELINE, "Baseline", 30.0, spacing);
        std::cout << "\r";
        PrintResultRow(r);
        results.push_back(r);
    }
    std::cout << std::string(106, '=') << std::endl;

    //=========================================================================
    // TEST 4: Adaptive vs Baseline Spacing Comparison
    //=========================================================================
    std::cout << "\n=== TEST 4: Adaptive vs Baseline Spacing at Different Survive % ===" << std::endl;
    std::cout << std::string(106, '-') << std::endl;
    std::cout << std::setw(18) << "Mode"
              << std::setw(9) << "Survive"
              << std::setw(9) << "Space"
              << std::setw(12) << "Final $"
              << std::setw(9) << "Return"
              << std::setw(9) << "MaxDD"
              << std::setw(8) << "VelPause"
              << std::setw(10) << "SpacChg"
              << std::setw(8) << "Trades"
              << std::setw(10) << "Status" << std::endl;
    std::cout << std::string(106, '-') << std::endl;

    for (double survive : {20.0, 25.0, 30.0, 40.0}) {
        std::cout << "Running Baseline " << survive << "%..." << std::flush;
        auto r1 = RunTest(FillUpOscillationNAS100::BASELINE, "Baseline", survive, 10.0);
        std::cout << "\r";
        PrintResultRow(r1);
        results.push_back(r1);

        std::cout << "Running Adaptive " << survive << "%..." << std::flush;
        auto r2 = RunTest(FillUpOscillationNAS100::ADAPTIVE_SPACING, "Adaptive", survive, 10.0);
        std::cout << "\r";
        PrintResultRow(r2);
        results.push_back(r2);
        std::cout << std::endl;
    }
    std::cout << std::string(106, '=') << std::endl;

    //=========================================================================
    // ANALYSIS
    //=========================================================================
    std::cout << "\n=== ANALYSIS ===" << std::endl;

    // Find best performing configurations
    TestResult best_return = results[0];
    TestResult best_safe = results[0];

    for (const auto& r : results) {
        if (!r.margin_call && r.return_x > best_return.return_x) {
            best_return = r;
        }
        if (!r.margin_call && r.max_dd_pct < 50.0 && r.return_x > best_safe.return_x) {
            best_safe = r;
        }
    }

    std::cout << "\nBest Return (no margin call):" << std::endl;
    std::cout << "  " << best_return.mode_name << " @ Survive=" << best_return.survive_pct
              << "%, Spacing=$" << best_return.spacing
              << " -> " << best_return.return_x << "x return, "
              << best_return.max_dd_pct << "% max DD" << std::endl;

    std::cout << "\nBest Risk-Adjusted (MaxDD < 50%):" << std::endl;
    std::cout << "  " << best_safe.mode_name << " @ Survive=" << best_safe.survive_pct
              << "%, Spacing=$" << best_safe.spacing
              << " -> " << best_safe.return_x << "x return, "
              << best_safe.max_dd_pct << "% max DD" << std::endl;

    // Compare adaptive vs baseline
    std::cout << "\n=== ADAPTIVE SPACING IMPACT ===" << std::endl;
    double baseline_30 = 0, adaptive_30 = 0;
    for (const auto& r : results) {
        if (r.survive_pct == 30.0 && r.spacing == 10.0) {
            if (r.mode_name == "Baseline") baseline_30 = r.return_x;
            if (r.mode_name == "Adaptive Spacing" || r.mode_name == "Adaptive") adaptive_30 = r.return_x;
        }
    }
    if (baseline_30 > 0 && adaptive_30 > 0) {
        double diff = adaptive_30 - baseline_30;
        std::string impact = diff > 0.05 ? "HELPS" : (diff < -0.05 ? "HURTS" : "NEUTRAL");
        std::cout << "Adaptive vs Baseline (30%): " << (diff > 0 ? "+" : "")
                  << std::fixed << std::setprecision(2) << diff << "x -> " << impact << std::endl;
    }

    std::cout << "\n=== KEY FINDINGS FOR NAS100 ===" << std::endl;
    std::cout << "1. NAS100 requires higher survive% (30%+) compared to gold (8-13%)" << std::endl;
    std::cout << "2. Spacing of $10-15 is appropriate for ~$20,000 price level" << std::endl;
    std::cout << "3. Velocity filter may trigger less frequently due to different volatility profile" << std::endl;
    std::cout << "4. Contract size difference (1.0 vs 100.0) affects position sizing math" << std::endl;

    return 0;
}
