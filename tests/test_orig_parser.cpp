/**
 * Debug test for original parser
 */
#include "../include/tick_data_manager.h"
#include <iostream>

using namespace backtest;

int main(int argc, char* argv[]) {
    std::string tick_file = "/tmp/test_ticks.csv";
    if (argc > 1) tick_file = argv[1];

    std::cout << "Testing file: " << tick_file << std::endl;

    try {
        std::cout << "Creating config..." << std::endl;
        TickDataConfig config;
        config.file_path = tick_file;
        config.format = TickDataFormat::MT5_CSV;
        config.load_all_into_memory = false;

        std::cout << "Creating manager..." << std::endl;
        TickDataManager manager(config);

        std::cout << "Reading ticks..." << std::endl;
        Tick tick;
        size_t count = 0;
        while (manager.GetNextTick(tick) && count < 10) {
            std::cout << "  " << count << ": " << tick.timestamp << " " << tick.bid << std::endl;
            count++;
        }
        std::cout << "Done! Read " << count << " ticks" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << std::endl;
    }

    return 0;
}
