#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>

struct Tick {
    std::string timestamp;
    double bid;
    double ask;

    double spread() const { return ask - bid; }
};

std::vector<Tick> LoadTicks(const std::string& filename, size_t start_line, size_t num_lines) {
    std::cout << "Attempting to open: " << filename << std::endl;
    std::vector<Tick> ticks;

    try {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "ERROR: Could not open file: " << filename << std::endl;
            return ticks;
        }

        std::cout << "File opened successfully" << std::endl;

        std::string line;
        size_t current_line = 0;

        if (!std::getline(file, line)) {
            std::cerr << "ERROR: Could not read header" << std::endl;
            return ticks;
        }

        std::cout << "Header: " << line << std::endl;

        while (std::getline(file, line) && ticks.size() < num_lines) {
            current_line++;
            if (current_line < start_line) continue;

            std::stringstream ss(line);
            std::string timestamp, bid_str, ask_str;
            std::getline(ss, timestamp, '\t');
            std::getline(ss, bid_str, '\t');
            std::getline(ss, ask_str, '\t');

            try {
                if (bid_str.empty() || ask_str.empty()) continue;

                Tick tick;
                tick.timestamp = timestamp;
                tick.bid = std::stod(bid_str);
                tick.ask = std::stod(ask_str);

                // Validate prices
                if (tick.bid <= 0 || tick.ask <= 0 || tick.ask < tick.bid) {
                    continue;
                }

                ticks.push_back(tick);

                if (ticks.size() % 10000 == 0) {
                    std::cout << "Loaded " << ticks.size() << " ticks..." << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << "Parse error at line " << current_line << ": " << e.what() << std::endl;
                continue;
            }
        }

        file.close();
        std::cout << "Total ticks loaded: " << ticks.size() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Exception during file load: " << e.what() << std::endl;
    }

    return ticks;
}

int main() {
    std::cout << "Program started" << std::endl;

    std::string filename = "Grid/XAUUSD_TICKS_2025.csv";

    std::cout << "Testing data load..." << std::endl;
    auto ticks = LoadTicks(filename, 1, 10000);

    if (ticks.empty()) {
        std::cerr << "Failed to load any ticks!" << std::endl;
        return 1;
    }

    std::cout << "First tick: " << ticks[0].timestamp << " Bid: " << ticks[0].bid << " Ask: " << ticks[0].ask << std::endl;
    std::cout << "Last tick: " << ticks.back().timestamp << " Bid: " << ticks.back().bid << " Ask: " << ticks.back().ask << std::endl;

    std::cout << "SUCCESS!" << std::endl;
    return 0;
}
