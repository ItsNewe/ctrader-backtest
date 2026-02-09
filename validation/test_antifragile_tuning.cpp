/**
 * Test Anti-fragile Scaling with Different Parameters
 * The original test showed no effect - need to investigate why
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>

using namespace backtest;

int main() {
    std::cout << "=== Anti-fragile Parameter Tuning ===" << std::endl;
    std::cout << "Testing why anti-fragile showed no effect" << std::endl;
    std::cout << std::fixed << std::setprecision(2);

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

    std::cout << "\nRunning test with max_volume=1.0 (force smaller sizes)..." << std::endl;

    try {
        TickBasedEngine engine(config);

        FillUpOscillation strategy(
            13.0,                           // survive_pct
            1.0,                            // base_spacing
            0.01,                           // min_volume
            1.0,                            // max_volume (REDUCED to see anti-fragile effect)
            100.0,                          // contract_size
            500.0,                          // leverage
            FillUpOscillation::ANTIFRAGILE, // mode
            0.2,                            // antifragile_scale (20% per 5% DD - more aggressive)
            30.0,                           // velocity_threshold
            4.0                             // volatility_lookback_hours
        );

        double peak_equity = config.initial_balance;
        double max_dd_pct = 0.0;
        int high_dd_ticks = 0;  // Count ticks where DD > 20%

        engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);

            double equity = eng.GetEquity();
            if (equity > peak_equity) peak_equity = equity;
            double dd = (peak_equity - equity) / peak_equity * 100.0;
            if (dd > max_dd_pct) max_dd_pct = dd;
            if (dd > 20.0) high_dd_ticks++;
        });

        auto results = engine.GetResults();
        std::cout << "\nWith max_volume=1.0, anti-fragile=0.2:" << std::endl;
        std::cout << "  Final Balance: $" << results.final_balance << std::endl;
        std::cout << "  Return: " << results.final_balance / config.initial_balance << "x" << std::endl;
        std::cout << "  Max DD: " << max_dd_pct << "%" << std::endl;
        std::cout << "  Ticks with DD>20%: " << high_dd_ticks << " (opportunities for anti-fragile)" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    // Now run baseline with same max_volume for comparison
    std::cout << "\nRunning BASELINE with max_volume=1.0 for comparison..." << std::endl;

    try {
        TickBasedEngine engine(config);

        FillUpOscillation strategy(
            13.0,                           // survive_pct
            1.0,                            // base_spacing
            0.01,                           // min_volume
            1.0,                            // max_volume (same)
            100.0,                          // contract_size
            500.0,                          // leverage
            FillUpOscillation::BASELINE,    // mode (BASELINE)
            0.1,                            // antifragile_scale (not used)
            30.0,                           // velocity_threshold
            4.0                             // volatility_lookback_hours
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
        std::cout << "\nBASELINE with max_volume=1.0:" << std::endl;
        std::cout << "  Final Balance: $" << results.final_balance << std::endl;
        std::cout << "  Return: " << results.final_balance / config.initial_balance << "x" << std::endl;
        std::cout << "  Max DD: " << max_dd_pct << "%" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "\n=== Analysis ===" << std::endl;
    std::cout << "If anti-fragile still shows no difference with lower max_volume," << std::endl;
    std::cout << "the issue is that lot sizing is already at minimum during drawdowns." << std::endl;
    std::cout << "Anti-fragile can't help if we're already margin-constrained." << std::endl;

    return 0;
}
