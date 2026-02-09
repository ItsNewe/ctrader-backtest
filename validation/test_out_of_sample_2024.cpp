/**
 * Out-of-Sample Validation Test
 * Tests the optimized strategy (Adaptive Spacing, survive=13%, spacing=$1.0, lookback=1h)
 * on 2024 Grid data (never seen during optimization)
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>

using namespace backtest;

struct TestResult {
    std::string period;
    double final_balance;
    double return_x;
    double max_dd_pct;
    int total_trades;
};

TestResult RunTest(const std::string& period, const std::string& file_path,
                   const std::string& start_date, const std::string& end_date) {

    TickDataConfig tick_config;
    tick_config.file_path = file_path;
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
    config.start_date = start_date;
    config.end_date = end_date;
    config.tick_data_config = tick_config;

    TestResult result;
    result.period = period;

    try {
        TickBasedEngine engine(config);

        // Optimized parameters from 2025 testing
        FillUpOscillation strategy(
            13.0,                              // survive_pct (optimal)
            1.0,                               // base_spacing $1.0 (optimal)
            0.01,                              // min_volume
            10.0,                              // max_volume
            100.0,                             // contract_size
            500.0,                             // leverage
            FillUpOscillation::ADAPTIVE_SPACING,  // Best mode
            0.1,                               // antifragile_scale (not used)
            30.0,                              // velocity_threshold (not used)
            1.0                                // volatility_lookback_hours (optimal)
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
        result.total_trades = results.total_trades;

    } catch (const std::exception& e) {
        std::cerr << "Error in " << period << ": " << e.what() << std::endl;
        result.return_x = 0;
        result.final_balance = 0;
        result.max_dd_pct = 100;
        result.total_trades = 0;
    }

    return result;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "OUT-OF-SAMPLE VALIDATION" << std::endl;
    std::cout << "Strategy: Adaptive Spacing (survive=13%, spacing=$1, lookback=1h)" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::fixed << std::setprecision(2);

    std::vector<TestResult> results;

    // In-sample: 2025 (what we optimized on)
    std::cout << "\n[1/2] Testing 2025 (IN-SAMPLE)..." << std::endl;
    results.push_back(RunTest(
        "2025 (In-Sample)",
        "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv",
        "2025.01.01", "2025.12.30"
    ));
    std::cout << "  Return: " << results.back().return_x << "x" << std::endl;

    // Out-of-sample: 2024 (never seen during optimization)
    std::cout << "\n[2/2] Testing 2024 (OUT-OF-SAMPLE)..." << std::endl;
    results.push_back(RunTest(
        "2024 (Out-of-Sample)",
        "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAUUSD_TICKS_2024.csv",
        "2024.01.01", "2024.12.31"
    ));
    std::cout << "  Return: " << results.back().return_x << "x" << std::endl;

    // Summary
    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "RESULTS SUMMARY" << std::endl;
    std::cout << std::string(70, '=') << std::endl;
    std::cout << std::setw(22) << "Period"
              << std::setw(14) << "Final $"
              << std::setw(10) << "Return"
              << std::setw(10) << "Max DD"
              << std::setw(10) << "Trades" << std::endl;
    std::cout << std::string(70, '-') << std::endl;

    for (const auto& r : results) {
        std::cout << std::setw(22) << r.period
                  << std::setw(13) << "$" << r.final_balance
                  << std::setw(9) << r.return_x << "x"
                  << std::setw(9) << r.max_dd_pct << "%"
                  << std::setw(10) << r.total_trades << std::endl;
    }
    std::cout << std::string(70, '=') << std::endl;

    // Validation assessment
    double in_sample_return = results[0].return_x;
    double out_of_sample_return = results[1].return_x;
    double ratio = out_of_sample_return / in_sample_return;

    std::cout << "\n=== VALIDATION ASSESSMENT ===" << std::endl;
    std::cout << "In-Sample (2025): " << in_sample_return << "x" << std::endl;
    std::cout << "Out-of-Sample (2024): " << out_of_sample_return << "x" << std::endl;
    std::cout << "Ratio (OOS/IS): " << ratio << std::endl;
    std::cout << std::endl;

    if (out_of_sample_return > 1.0 && ratio > 0.5) {
        std::cout << "[PASS] Strategy is VALID - profitable in out-of-sample testing" << std::endl;
        if (ratio > 0.8) {
            std::cout << "       Excellent: OOS/IS ratio > 0.8 indicates robust strategy" << std::endl;
        } else if (ratio > 0.5) {
            std::cout << "       Good: OOS/IS ratio > 0.5 indicates decent generalization" << std::endl;
        }
    } else if (out_of_sample_return > 1.0) {
        std::cout << "[CAUTION] Strategy profitable but may be overfit" << std::endl;
        std::cout << "          OOS return significantly lower than in-sample" << std::endl;
    } else {
        std::cout << "[FAIL] Strategy FAILED out-of-sample testing" << std::endl;
        std::cout << "       May be overfit to 2025 market conditions" << std::endl;
    }

    return 0;
}
