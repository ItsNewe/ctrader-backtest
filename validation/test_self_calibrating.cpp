/**
 * Test Self-Calibrating Trend-Adaptive Strategy
 *
 * The strategy now automatically calibrates its thresholds based on
 * observed trend data during a warmup period.
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>

using namespace backtest;

int main() {
    std::cout << "=== Self-Calibrating Trend-Adaptive Test ===" << std::endl;
    std::cout << std::endl;

    std::string data_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    double initial_balance = 10000.0;

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
    config.start_date = "2025.01.01";
    config.end_date = "2025.12.30";
    config.tick_data_config = tick_config;
    config.verbose = false;

    try {
        TickBasedEngine engine(config);

        // Use 3-day lookback (72 hours)
        FillUpOscillation strategy(
            13.0,                              // survive_pct
            1.5,                               // base_spacing
            0.01,                              // min_volume
            10.0,                              // max_volume
            100.0,                             // contract_size
            500.0,                             // leverage
            FillUpOscillation::TREND_ADAPTIVE, // mode
            0.1,                               // antifragile_scale
            30.0,                              // velocity_threshold
            72.0                               // 3-day lookback
        );

        bool calibration_logged = false;

        engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);

            // Log when calibration completes
            if (strategy.IsCalibrationComplete() && !calibration_logged) {
                std::cout << "Calibration completed after " << strategy.GetWarmupSamples()
                          << " samples" << std::endl;
                std::cout << "Calibrated thresholds:" << std::endl;
                std::cout << "  Strong (p75): " << std::fixed << std::setprecision(2)
                          << strategy.GetThreshStrong() << "%" << std::endl;
                std::cout << "  Moderate (p50): " << strategy.GetThreshModerate() << "%" << std::endl;
                std::cout << "  Weak (p25): " << strategy.GetThreshWeak() << "%" << std::endl;
                std::cout << std::endl;
                calibration_logged = true;
            }
        });

        auto res = engine.GetResults();
        double return_mult = res.final_balance / initial_balance;
        double peak = res.final_balance + res.max_drawdown;
        double max_dd_pct = (peak > 0) ? (res.max_drawdown / peak * 100.0) : 0;

        std::cout << "=== Results ===" << std::endl;
        std::cout << "Return: " << std::fixed << std::setprecision(2) << return_mult << "x" << std::endl;
        std::cout << "Max DD: " << std::setprecision(0) << max_dd_pct << "%" << std::endl;
        std::cout << "Trades: " << res.total_trades << std::endl;
        std::cout << "Spacing changes: " << strategy.GetTrendSpacingChanges() << std::endl;
        std::cout << std::endl;

        std::cout << "=== Comparison ===" << std::endl;
        std::cout << "Fixed $0.30:                    8.80x" << std::endl;
        std::cout << "Hard-coded calibration (3d):    8.13x" << std::endl;
        std::cout << "Self-calibrating (3d):          " << return_mult << "x" << std::endl;

        // Always print final calibration values
        std::cout << std::endl;
        std::cout << "=== Final Calibrated Thresholds ===" << std::endl;
        std::cout << "Samples collected: " << strategy.GetWarmupSamples() << std::endl;
        std::cout << "Strong (p75): " << std::fixed << std::setprecision(2)
                  << strategy.GetThreshStrong() << "%" << std::endl;
        std::cout << "Moderate (p50): " << strategy.GetThreshModerate() << "%" << std::endl;
        std::cout << "Weak (p25): " << strategy.GetThreshWeak() << "%" << std::endl;

        if (return_mult > 8.0) {
            std::cout << std::endl;
            std::cout << "SUCCESS: Self-calibration matches hard-coded performance!" << std::endl;
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
