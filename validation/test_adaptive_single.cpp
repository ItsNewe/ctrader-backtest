/**
 * Single Adaptive Spacing Test - for parallel execution
 * Usage: test_adaptive_single.exe <base_spacing> <lookback_hours>
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <cstdlib>

using namespace backtest;

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <base_spacing> <lookback_hours>" << std::endl;
        return 1;
    }

    double base_spacing = std::atof(argv[1]);
    double lookback_hours = std::atof(argv[2]);

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
        double return_x = results.final_balance / config.initial_balance;

        // Output single line result for easy parsing
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "RESULT|" << base_spacing << "|" << lookback_hours << "|"
                  << results.final_balance << "|" << return_x << "|"
                  << max_dd_pct << "|" << strategy.GetAdaptiveSpacingChanges() << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "ERROR|" << base_spacing << "|" << lookback_hours << "|" << e.what() << std::endl;
        return 1;
    }

    return 0;
}
