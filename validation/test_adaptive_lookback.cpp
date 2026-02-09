/**
 * Adaptive Lookback Test
 * Compare: ADAPTIVE_SPACING vs ADAPTIVE_LOOKBACK vs DOUBLE_ADAPTIVE
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>

using namespace backtest;

struct TestResult {
    std::string mode_name;
    FillUpOscillation::Mode mode;
    double final_balance;
    double return_x;
    double max_dd_pct;
    int spacing_changes;
    int lookback_changes;
};

TestResult RunTest(const std::string& name, FillUpOscillation::Mode mode) {
    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

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
    config.start_date = "2025.01.01";
    config.end_date = "2025.12.30";
    config.tick_data_config = tick_config;

    TestResult result;
    result.mode_name = name;
    result.mode = mode;

    try {
        TickBasedEngine engine(config);

        FillUpOscillation strategy(
            13.0,       // survive_pct
            1.0,        // base_spacing
            0.01,       // min_volume
            10.0,       // max_volume
            100.0,      // contract_size
            500.0,      // leverage
            mode,       // mode
            0.1,        // antifragile_scale
            30.0,       // velocity_threshold
            1.0         // volatility_lookback_hours (base)
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
        result.final_balance = results.final_balance;
        result.return_x = results.final_balance / config.initial_balance;
        result.max_dd_pct = max_dd_pct;
        result.spacing_changes = strategy.GetAdaptiveSpacingChanges();
        result.lookback_changes = strategy.GetLookbackChanges();

    } catch (const std::exception& e) {
        std::cerr << "Error in " << name << ": " << e.what() << std::endl;
        result.return_x = 0;
    }

    return result;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "ADAPTIVE LOOKBACK COMPARISON TEST" << std::endl;
    std::cout << "Testing if adaptive lookback improves results" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::fixed << std::setprecision(2);

    std::vector<TestResult> results;

    // Test each mode
    std::cout << "\n[1/4] Testing BASELINE..." << std::endl;
    results.push_back(RunTest("Baseline (no adaptation)", FillUpOscillation::BASELINE));
    std::cout << "  Return: " << results.back().return_x << "x" << std::endl;

    std::cout << "\n[2/4] Testing ADAPTIVE_SPACING..." << std::endl;
    results.push_back(RunTest("Adaptive Spacing only", FillUpOscillation::ADAPTIVE_SPACING));
    std::cout << "  Return: " << results.back().return_x << "x" << std::endl;

    std::cout << "\n[3/4] Testing ADAPTIVE_LOOKBACK..." << std::endl;
    results.push_back(RunTest("Adaptive Lookback only", FillUpOscillation::ADAPTIVE_LOOKBACK));
    std::cout << "  Return: " << results.back().return_x << "x" << std::endl;

    std::cout << "\n[4/4] Testing DOUBLE_ADAPTIVE..." << std::endl;
    results.push_back(RunTest("Double Adaptive (both)", FillUpOscillation::DOUBLE_ADAPTIVE));
    std::cout << "  Return: " << results.back().return_x << "x" << std::endl;

    // Results table
    std::cout << "\n" << std::string(90, '=') << std::endl;
    std::cout << "RESULTS COMPARISON" << std::endl;
    std::cout << std::string(90, '=') << std::endl;
    std::cout << std::setw(28) << "Mode"
              << std::setw(14) << "Final $"
              << std::setw(10) << "Return"
              << std::setw(10) << "Max DD"
              << std::setw(14) << "Spc Chg"
              << std::setw(14) << "Lkb Chg" << std::endl;
    std::cout << std::string(90, '-') << std::endl;

    TestResult best = results[0];
    for (const auto& r : results) {
        std::cout << std::setw(28) << r.mode_name
                  << std::setw(13) << "$" << r.final_balance
                  << std::setw(9) << r.return_x << "x"
                  << std::setw(9) << r.max_dd_pct << "%"
                  << std::setw(14) << r.spacing_changes
                  << std::setw(14) << r.lookback_changes << std::endl;
        if (r.return_x > best.return_x) best = r;
    }
    std::cout << std::string(90, '=') << std::endl;

    // Analysis
    double baseline = results[0].return_x;
    double spacing_only = results[1].return_x;
    double lookback_only = results[2].return_x;
    double double_adaptive = results[3].return_x;

    std::cout << "\n=== ANALYSIS ===" << std::endl;
    std::cout << "Adaptive Spacing effect:  " << (spacing_only - baseline > 0 ? "+" : "")
              << (spacing_only - baseline) << "x vs baseline" << std::endl;
    std::cout << "Adaptive Lookback effect: " << (lookback_only - baseline > 0 ? "+" : "")
              << (lookback_only - baseline) << "x vs baseline" << std::endl;
    std::cout << "Double Adaptive effect:   " << (double_adaptive - baseline > 0 ? "+" : "")
              << (double_adaptive - baseline) << "x vs baseline" << std::endl;
    std::cout << "Combined vs Spacing-only: " << (double_adaptive - spacing_only > 0 ? "+" : "")
              << (double_adaptive - spacing_only) << "x" << std::endl;

    std::cout << "\n=== BEST MODE ===" << std::endl;
    std::cout << best.mode_name << " with " << best.return_x << "x return" << std::endl;

    if (double_adaptive > spacing_only) {
        std::cout << "\n[CONCLUSION] Adaptive lookback IMPROVES results!" << std::endl;
    } else if (lookback_only > baseline) {
        std::cout << "\n[CONCLUSION] Adaptive lookback helps alone, but not combined with adaptive spacing" << std::endl;
    } else {
        std::cout << "\n[CONCLUSION] Adaptive lookback does NOT help" << std::endl;
    }

    return 0;
}
