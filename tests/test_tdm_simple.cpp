// Test with tick_data_manager.h only
#include "../include/tick_data_manager.h"
#include <iostream>

using namespace backtest;

int main() {
    std::string tick_file = "C:/Users/user/Documents/ctrader-backtest/validation/Grid/XAUUSD_TICKS_2025.csv";
    
    std::cout << "Test: with tick_data_manager.h only" << std::endl;
    
    TickDataConfig config;
    config.file_path = tick_file;
    config.format = TickDataFormat::MT5_CSV;
    config.load_all_into_memory = false;
    
    std::cout << "Creating manager..." << std::endl;
    TickDataManager manager(config);
    std::cout << "Success!" << std::endl;
    
    Tick tick;
    int count = 0;
    while (manager.GetNextTick(tick) && count < 5) {
        std::cout << "Tick " << count << ": " << tick.timestamp << std::endl;
        count++;
    }
    
    return 0;
}
