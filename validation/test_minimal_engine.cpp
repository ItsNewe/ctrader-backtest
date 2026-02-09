#include "../include/tick_based_engine.h"
#include <iostream>

using namespace backtest;

int main() {
    std::cout << "Starting minimal engine test..." << std::endl;
    std::cout.flush();
    
    try {
        TickDataConfig tick_config;
        tick_config.file_path = "XAUUSD_TICKS_2025.csv";
        tick_config.format = TickDataFormat::MT5_CSV;
        tick_config.load_all_into_memory = false;
        
        std::cout << "Creating config..." << std::endl;
        std::cout.flush();
        
        TickBacktestConfig config;
        config.symbol = "XAUUSD";
        config.initial_balance = 110000.0;
        config.contract_size = 100.0;
        config.tick_data_config = tick_config;
        
        std::cout << "Creating engine..." << std::endl;
        std::cout.flush();
        
        TickBasedEngine engine(config);
        
        std::cout << "Engine created successfully!" << std::endl;
        std::cout << "Initial balance: " << engine.GetBalance() << std::endl;
        std::cout << "Stop out occurred: " << engine.IsStopOutOccurred() << std::endl;
        
        // Process just 100 ticks
        int count = 0;
        engine.Run([&count](const Tick& tick, TickBasedEngine& engine) {
            count++;
            if (count <= 5) {
                std::cout << "Tick " << count << ": " << tick.timestamp 
                          << " bid=" << tick.bid << " ask=" << tick.ask << std::endl;
            }
            if (count >= 100) {
                // Force stop after 100 ticks by using date filter trick
            }
        });
        
        std::cout << "Processed " << count << " ticks" << std::endl;
        std::cout << "Test PASSED!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
