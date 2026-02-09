/**
 * Trend Lookback Period Test
 *
 * Tests FillUpOscillation TREND_ADAPTIVE mode with different lookback periods.
 * The lookback determines how far back to measure trend strength.
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>

using namespace backtest;

struct TestResult {
    double lookback_hours;
    std::string lookback_name;
    double return_multiple;
    double max_dd_pct;
    int total_trades;
    int spacing_changes;
    bool stopped_out;
};

TestResult RunTest(double lookback_hours, const std::string& data_path,
                   const std::string& start_date, const std::string& end_date,
                   double initial_balance) {
    TestResult result;
    result.lookback_hours = lookback_hours;

    // Format lookback name
    if (lookback_hours < 24) {
        result.lookback_name = std::to_string((int)lookback_hours) + "h";
    } else if (lookback_hours < 168) {
        result.lookback_name = std::to_string((int)(lookback_hours / 24)) + "d";
    } else {
        result.lookback_name = std::to_string((int)(lookback_hours / 168)) + "w";
    }

    TickDataConfig tick_config;
    tick_config.file_path = data_path;
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = initial_balance;
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
    config.verbose = false;

    try {
        TickBasedEngine engine(config);

        FillUpOscillation strategy(
            13.0,                              // survive_pct
            1.5,                               // base_spacing (not used in trend mode)
            0.01,                              // min_volume
            10.0,                              // max_volume
            100.0,                             // contract_size
            500.0,                             // leverage
            FillUpOscillation::TREND_ADAPTIVE, // mode
            0.1,                               // antifragile_scale
            30.0,                              // velocity_threshold
            lookback_hours                     // lookback hours - used for trend measurement
        );

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto res = engine.GetResults();
        result.return_multiple = res.final_balance / initial_balance;
        double peak = res.final_balance + res.max_drawdown;
        result.max_dd_pct = (peak > 0) ? (res.max_drawdown / peak * 100.0) : 0;
        result.total_trades = res.total_trades;
        result.spacing_changes = strategy.GetTrendSpacingChanges();
        result.stopped_out = engine.IsStopOutOccurred();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        result.return_multiple = 0;
        result.max_dd_pct = 100;
        result.stopped_out = true;
    }

    return result;
}

int main() {
    std::cout << "=== Trend Lookback Period Sweep ===" << std::endl;
    std::cout << "Testing TREND_ADAPTIVE with different lookback periods" << std::endl;
    std::cout << std::endl;

    std::string data_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    double initial_balance = 10000.0;

    // Lookback periods to test
    std::vector<double> lookbacks = {
        4.0,      // 4 hours
        12.0,     // 12 hours
        24.0,     // 1 day
        48.0,     // 2 days
        72.0,     // 3 days
        168.0,    // 1 week
        336.0,    // 2 weeks
        720.0     // 1 month
    };

    std::cout << "Testing Full Year 2025..." << std::endl;
    std::cout << std::endl;

    std::cout << std::setw(10) << "Lookback"
              << std::setw(10) << "Return"
              << std::setw(10) << "MaxDD%"
              << std::setw(10) << "Trades"
              << std::setw(12) << "Changes"
              << std::setw(10) << "Status"
              << std::endl;
    std::cout << std::string(62, '-') << std::endl;

    std::vector<TestResult> results;
    double best_return = 0;
    std::string best_lookback;

    for (double lb : lookbacks) {
        auto result = RunTest(lb, data_path, "2025.01.01", "2025.12.30", initial_balance);
        results.push_back(result);

        std::cout << std::setw(10) << result.lookback_name
                  << std::setw(9) << std::fixed << std::setprecision(2) << result.return_multiple << "x"
                  << std::setw(9) << std::setprecision(0) << result.max_dd_pct << "%"
                  << std::setw(10) << result.total_trades
                  << std::setw(12) << result.spacing_changes
                  << std::setw(10) << (result.stopped_out ? "STOP" : "OK")
                  << std::endl;

        if (!result.stopped_out && result.return_multiple > best_return) {
            best_return = result.return_multiple;
            best_lookback = result.lookback_name;
        }
    }

    std::cout << std::endl;
    std::cout << "=== RESULT ===" << std::endl;
    std::cout << "Best lookback: " << best_lookback << " -> "
              << std::fixed << std::setprecision(2) << best_return << "x return" << std::endl;
    std::cout << std::endl;
    std::cout << "For comparison:" << std::endl;
    std::cout << "  Fixed $0.30:     8.80x" << std::endl;
    std::cout << "  Vol-Adaptive:    7.05x" << std::endl;
    std::cout << "  24h Trend:       5.63x (original test)" << std::endl;

    // Conclusion
    std::cout << std::endl;
    if (best_return > 7.05) {
        std::cout << "LONGER lookback improves trend-adaptive!" << std::endl;
    } else if (best_return < 5.63) {
        std::cout << "SHORTER lookback improves trend-adaptive!" << std::endl;
    } else {
        std::cout << "Lookback period doesn't significantly improve results." << std::endl;
    }

    return 0;
}
