/**
 * Test Oscillation Enhancement Modes
 * Compares: Baseline, Adaptive Spacing, Anti-fragile, Velocity Filter, All Combined
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>

using namespace backtest;

struct TestResult {
    std::string mode_name;
    double final_equity;
    double return_x;
    double max_dd_pct;
    int velocity_pauses;
    int spacing_changes;
    bool margin_call;
};

TestResult RunTest(FillUpOscillation::Mode mode, const std::string& mode_name,
                   double survive_pct, double spacing) {
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
    result.mode_name = mode_name;
    result.margin_call = false;

    try {
        TickBasedEngine engine(config);

        FillUpOscillation strategy(
            survive_pct,      // survive_pct
            spacing,          // base_spacing
            0.01,             // min_volume
            10.0,             // max_volume
            100.0,            // contract_size
            500.0,            // leverage
            mode,             // mode
            0.1,              // antifragile_scale (10% per 5% DD)
            30.0,             // velocity_threshold ($30/hour)
            4.0               // volatility_lookback_hours
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
        // Margin call detected if balance dropped to near-zero
        result.margin_call = (results.final_balance < config.initial_balance * 0.05);

    } catch (const std::exception& e) {
        std::cerr << "Error in " << mode_name << ": " << e.what() << std::endl;
        result.final_equity = 0;
        result.return_x = 0;
        result.max_dd_pct = 100;
        result.margin_call = true;
    }

    return result;
}

int main() {
    std::cout << "=== Oscillation Enhancement Modes Test ===" << std::endl;
    std::cout << "Testing: Survive=13%, Base Spacing=$1" << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\nRunning tests..." << std::endl;

    std::vector<TestResult> results;

    // Test each mode
    std::cout << "\n[1/5] Testing BASELINE..." << std::endl;
    results.push_back(RunTest(FillUpOscillation::BASELINE, "Baseline", 13.0, 1.0));

    std::cout << "[2/5] Testing ADAPTIVE_SPACING..." << std::endl;
    results.push_back(RunTest(FillUpOscillation::ADAPTIVE_SPACING, "Adaptive Spacing", 13.0, 1.0));

    std::cout << "[3/5] Testing ANTIFRAGILE..." << std::endl;
    results.push_back(RunTest(FillUpOscillation::ANTIFRAGILE, "Anti-fragile", 13.0, 1.0));

    std::cout << "[4/5] Testing VELOCITY_FILTER..." << std::endl;
    results.push_back(RunTest(FillUpOscillation::VELOCITY_FILTER, "Velocity Filter", 13.0, 1.0));

    std::cout << "[5/5] Testing ALL_COMBINED..." << std::endl;
    results.push_back(RunTest(FillUpOscillation::ALL_COMBINED, "All Combined", 13.0, 1.0));

    // Print results table
    std::cout << "\n" << std::string(85, '=') << std::endl;
    std::cout << std::setw(18) << "Mode"
              << std::setw(14) << "Final $"
              << std::setw(10) << "Return"
              << std::setw(10) << "Max DD"
              << std::setw(12) << "Vel Pauses"
              << std::setw(12) << "Spac Chg"
              << std::setw(10) << "Status" << std::endl;
    std::cout << std::string(85, '-') << std::endl;

    for (const auto& r : results) {
        std::cout << std::setw(18) << r.mode_name
                  << std::setw(13) << "$" << r.final_equity
                  << std::setw(9) << r.return_x << "x"
                  << std::setw(9) << r.max_dd_pct << "%"
                  << std::setw(12) << r.velocity_pauses
                  << std::setw(12) << r.spacing_changes
                  << std::setw(10) << (r.margin_call ? "MARGIN" : "OK") << std::endl;
    }
    std::cout << std::string(85, '=') << std::endl;

    // Analysis
    std::cout << "\n=== Analysis ===" << std::endl;

    double baseline_return = results[0].return_x;
    for (size_t i = 1; i < results.size(); i++) {
        double diff = results[i].return_x - baseline_return;
        std::string impact = diff > 0.1 ? "HELPS" : (diff < -0.1 ? "HURTS" : "NEUTRAL");
        std::cout << results[i].mode_name << ": " << (diff > 0 ? "+" : "") << diff
                  << "x vs baseline -> " << impact << std::endl;
    }

    return 0;
}
