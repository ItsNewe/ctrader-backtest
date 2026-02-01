/**
 * Minimal Strategy Test - Tests FillUpOscillation with engine
 */
#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>

using namespace backtest;

int main() {
    std::cout << "=== Strategy Minimal Test ===" << std::endl;
    std::cout << std::fixed << std::setprecision(2);

    std::cout << "Step 1: Creating configs..." << std::endl;

    TickDataConfig tick_config;
    tick_config.file_path = "C:/Users/user/Documents/ctrader-backtest/validation/Grid/XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;

    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = 10000.0;
    config.contract_size = 100.0;
    config.leverage = 500.0;
    config.pip_size = 0.01;
    config.swap_long = -66.99;
    config.swap_short = 41.2;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = "2024.12.31";
    config.end_date = "2025.01.02";  // Just 2 days
    config.tick_data_config = tick_config;
    config.verbose = false;
    std::cout << "  Done." << std::endl;

    std::cout << "Step 2: Creating engine..." << std::flush;
    TickBasedEngine engine(config);
    std::cout << " Done!" << std::endl;

    std::cout << "Step 3: Creating strategy..." << std::flush;
    FillUpOscillation strategy(
        13.0,   // survive_pct
        1.5,    // base_spacing
        0.01,   // min_volume
        10.0,   // max_volume
        100.0,  // contract_size
        500.0,  // leverage
        FillUpOscillation::ADAPTIVE_SPACING,
        0.1,    // antifragile_scale
        30.0,   // max_spacing_mult
        4.0     // lookback_hours
    );
    std::cout << " Done!" << std::endl;

    std::cout << "Step 4: Running backtest..." << std::endl;
    int tick_count = 0;
    engine.Run([&strategy, &tick_count](const Tick& tick, TickBasedEngine& eng) {
        tick_count++;
        strategy.OnTick(tick, eng);
        if (tick_count % 100 == 0) {
            std::cout << "  Processed " << tick_count << " ticks, positions: "
                      << eng.GetOpenPositions().size() << "\r" << std::flush;
        }
    });
    std::cout << "\n  Total ticks: " << tick_count << std::endl;

    std::cout << "Step 5: Getting results..." << std::flush;
    auto results = engine.GetResults();
    std::cout << " Done!" << std::endl;

    std::cout << "\n=== Results ===" << std::endl;
    std::cout << "Final Balance: $" << results.final_balance << std::endl;
    std::cout << "Total Trades: " << results.total_trades << std::endl;
    std::cout << "Max Drawdown: " << results.max_drawdown_pct << "%" << std::endl;

    std::cout << "\n*** TEST PASSED ***" << std::endl;
    return 0;
}
