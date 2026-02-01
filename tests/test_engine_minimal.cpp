/**
 * Minimal Engine Test - Debug segfault issue
 */
#include "../include/tick_based_engine.h"
#include <iostream>

using namespace backtest;

int main() {
    std::cout << "=== Minimal Engine Test ===" << std::endl;

    std::cout << "Step 1: Creating tick_config..." << std::endl;
    TickDataConfig tick_config;
    tick_config.file_path = "C:/Users/user/Documents/ctrader-backtest/validation/Grid/XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;
    std::cout << "  Done. File: " << tick_config.file_path << std::endl;

    std::cout << "Step 2: Creating backtest config..." << std::endl;
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
    config.end_date = "2025.01.02";  // A few days
    config.tick_data_config = tick_config;
    config.verbose = false;
    std::cout << "  Done." << std::endl;

    std::cout << "Step 3: Creating TickBasedEngine..." << std::flush;
    TickBasedEngine engine(config);
    std::cout << " Done!" << std::endl;

    std::cout << "Step 4: Running with no-op callback..." << std::flush;
    int tick_count = 0;
    engine.Run([&tick_count](const Tick& tick, TickBasedEngine& engine) {
        tick_count++;
        if (tick_count <= 3) {
            std::cout << "\n  Tick " << tick_count << ": " << tick.timestamp
                      << " bid=" << tick.bid << std::flush;
        }
    });
    std::cout << "\n  Total ticks: " << tick_count << std::endl;

    std::cout << "Step 5: Getting results..." << std::flush;
    auto results = engine.GetResults();
    std::cout << " Done!" << std::endl;

    std::cout << "\n=== Results ===" << std::endl;
    std::cout << "Final Balance: $" << results.final_balance << std::endl;
    std::cout << "Total Trades: " << results.total_trades << std::endl;

    std::cout << "\n*** TEST PASSED ***" << std::endl;
    return 0;
}
