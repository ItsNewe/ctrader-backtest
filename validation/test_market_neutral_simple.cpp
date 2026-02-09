#include "../include/tick_based_engine.h"
#include <iostream>

using namespace backtest;

int main() {
    std::cout << "Starting market-neutral test..." << std::endl;
    std::cout.flush();

    try {
        TickDataConfig tick_config;
        tick_config.file_path = "validation/XAUUSD_TICKS_2025.csv";
        tick_config.format = TickDataFormat::MT5_CSV;
        tick_config.load_all_into_memory = false;

        std::cout << "Creating config..." << std::endl;
        std::cout.flush();

        TickBacktestConfig config;
        config.symbol = "XAUUSD";
        config.initial_balance = 110000.0;
        config.contract_size = 100.0;
        config.tick_data_config = tick_config;
        config.leverage = 500.0;
        config.pip_size = 0.01;

        std::cout << "Creating engine..." << std::endl;
        std::cout.flush();

        TickBasedEngine engine(config);

        std::cout << "Engine created successfully!" << std::endl;
        std::cout << "Running simple grid strategy..." << std::endl;

        int tick_count = 0;
        const int MAX_TICKS = 10000;  // Just 10k ticks for quick test
        const double GRID_SIZE = 3.0;
        const double LOT_SIZE = 0.01;
        double last_buy_price = 0.0;

        engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
            tick_count++;
            if (tick_count > MAX_TICKS) return;

            // Simple grid logic
            if (last_buy_price == 0.0 || tick.ask <= last_buy_price - GRID_SIZE) {
                eng.OpenMarketOrder("BUY", LOT_SIZE);
                last_buy_price = tick.ask;
            }

            // Close profitable positions
            for (Trade* trade : eng.GetOpenPositions()) {
                double unrealized = (tick.bid - trade->entry_price) * trade->lot_size * config.contract_size;
                if (unrealized >= 150.0) {
                    eng.ClosePosition(trade, "Profit Target");
                    break;
                }
            }
        });

        std::cout << "\n--- SIMPLE GRID TEST RESULTS ---" << std::endl;
        auto results = engine.GetResults();
        std::cout << "Final Balance: $" << results.final_balance << std::endl;
        std::cout << "Total P/L: $" << results.total_profit_loss << std::endl;
        std::cout << "Total Trades: " << results.total_trades << std::endl;

        std::cout << "\nTest PASSED!" << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }
}
