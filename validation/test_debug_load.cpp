#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>

struct Tick {
    double bid;
    double ask;
};

int main() {
    std::cout << "Opening file...\n" << std::flush;
    std::ifstream file("NAS100/NAS100_TICKS_2025.csv");
    if (!file.is_open()) {
        std::cerr << "Cannot open file!\n";
        return 1;
    }
    std::cout << "File opened\n" << std::flush;

    std::string line;
    std::getline(file, line);
    std::cout << "Header: " << line.substr(0, 100) << "...\n" << std::flush;

    std::vector<Tick> ticks;
    // Don't reserve - just let it grow naturally

    size_t count = 0;
    size_t max_ticks = 1000000;  // Limit to 1M ticks first

    while (std::getline(file, line) && ticks.size() < max_ticks) {
        try {
            std::stringstream ss(line);
            std::string token;
            Tick t;

            std::getline(ss, token, '\t');
            std::getline(ss, token, '\t');
            if (token.empty()) continue;
            t.bid = std::stod(token);

            std::getline(ss, token, '\t');
            if (token.empty()) continue;
            t.ask = std::stod(token);

            if (t.bid > 0 && t.ask > 0) {
                ticks.push_back(t);
            }
            count++;
            if (count % 100000 == 0) {
                std::cout << "Loaded " << count << " lines, " << ticks.size() << " valid ticks\n" << std::flush;
            }
        } catch (...) {
            continue;
        }
    }

    std::cout << "\nTotal: " << ticks.size() << " ticks\n";
    if (!ticks.empty()) {
        std::cout << "First: " << ticks.front().bid << "/" << ticks.front().ask << "\n";
        std::cout << "Last: " << ticks.back().bid << "/" << ticks.back().ask << "\n";
    }

    return 0;
}
