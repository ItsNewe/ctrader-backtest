/**
 * Adaptive Spacing Parameter Sweep
 * Optimize the volatility-based spacing adjustment that showed +0.31x improvement
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>

using namespace backtest;

struct TestResult {
    double base_spacing;
    double lookback_hours;
    double final_balance;
    double return_x;
    double max_dd_pct;
    int spacing_changes;
};

TestResult RunTest(double base_spacing, double lookback_hours) {
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
    result.base_spacing = base_spacing;
    result.lookback_hours = lookback_hours;

    try {
        TickBasedEngine engine(config);

        FillUpOscillation strategy(
            13.0,                              // survive_pct
            base_spacing,                      // base_spacing
            0.01,                              // min_volume
            10.0,                              // max_volume
            100.0,                             // contract_size
            500.0,                             // leverage
            FillUpOscillation::ADAPTIVE_SPACING,
            0.1,                               // antifragile_scale (not used)
            30.0,                              // velocity_threshold (not used)
            lookback_hours                     // volatility_lookback_hours
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

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        result.return_x = 0;
    }

    return result;
}

int main() {
    std::cout << "=== Adaptive Spacing Parameter Sweep ===" << std::endl;
    std::cout << "Finding optimal volatility lookback and base spacing" << std::endl;
    std::cout << std::fixed << std::setprecision(2);

    std::vector<TestResult> results;

    // Test different base spacings and lookback periods
    std::vector<double> base_spacings = {0.5, 1.0, 1.5, 2.0};
    std::vector<double> lookbacks = {1.0, 2.0, 4.0, 8.0, 12.0};

    int total = base_spacings.size() * lookbacks.size();
    int current = 0;

    for (double spacing : base_spacings) {
        for (double lookback : lookbacks) {
            current++;
            std::cout << "\n[" << current << "/" << total << "] Testing spacing=$"
                      << spacing << ", lookback=" << lookback << "h..." << std::endl;
            results.push_back(RunTest(spacing, lookback));
        }
    }

    // Print results table
    std::cout << "\n" << std::string(85, '=') << std::endl;
    std::cout << std::setw(12) << "Spacing"
              << std::setw(12) << "Lookback"
              << std::setw(14) << "Final $"
              << std::setw(10) << "Return"
              << std::setw(10) << "Max DD"
              << std::setw(14) << "Spac Changes" << std::endl;
    std::cout << std::string(85, '-') << std::endl;

    TestResult best = results[0];
    for (const auto& r : results) {
        std::cout << std::setw(11) << "$" << r.base_spacing
                  << std::setw(11) << r.lookback_hours << "h"
                  << std::setw(13) << "$" << r.final_balance
                  << std::setw(9) << r.return_x << "x"
                  << std::setw(9) << r.max_dd_pct << "%"
                  << std::setw(14) << r.spacing_changes << std::endl;
        if (r.return_x > best.return_x) best = r;
    }
    std::cout << std::string(85, '=') << std::endl;

    std::cout << "\n=== Best Configuration ===" << std::endl;
    std::cout << "Base Spacing: $" << best.base_spacing << std::endl;
    std::cout << "Lookback: " << best.lookback_hours << " hours" << std::endl;
    std::cout << "Return: " << best.return_x << "x" << std::endl;
    std::cout << "Max DD: " << best.max_dd_pct << "%" << std::endl;

    return 0;
}
