#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <iomanip>
#include "../include/backtester_accurate.h"

struct Tick {
    double bid;
    double ask;
    double mid() const { return (bid + ask) / 2.0; }
};

std::vector<Tick> load_ticks(const std::string& filename, size_t max_ticks = 60000000) {
    std::vector<Tick> ticks;
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Cannot open: " << filename << std::endl;
        return ticks;
    }

    std::string line;
    std::getline(file, line); // Skip header

    ticks.reserve(std::min(max_ticks, (size_t)60000000));

    while (std::getline(file, line) && ticks.size() < max_ticks) {
        try {
            std::stringstream ss(line);
            std::string token;
            Tick t;

            std::getline(ss, token, '\t'); // timestamp
            std::getline(ss, token, '\t');
            if (token.empty()) continue;
            t.bid = std::stod(token);

            std::getline(ss, token, '\t');
            if (token.empty()) continue;
            t.ask = std::stod(token);

            if (t.bid > 0 && t.ask > 0) {
                ticks.push_back(t);
            }
        } catch (...) {
            continue;
        }
    }
    return ticks;
}

int main() {
    const double INITIAL = 10000.0;

    std::cout << "Loading NAS100 data...\n" << std::flush;
    auto nas_ticks = load_ticks("NAS100/NAS100_TICKS_2025.csv");
    std::cout << "Loaded " << nas_ticks.size() << " ticks\n" << std::flush;

    if (nas_ticks.empty()) {
        std::cerr << "No NAS100 data!\n";
        return 1;
    }

    std::cout << "First tick: " << nas_ticks.front().bid << "/" << nas_ticks.front().ask << "\n" << std::flush;
    std::cout << "Last tick: " << nas_ticks.back().bid << "/" << nas_ticks.back().ask << "\n" << std::flush;

    std::cout << "Running single test...\n" << std::flush;

    BacktestConfig cfg;
    cfg.survive_down_pct = 30.0;
    cfg.min_entry_spacing = 50.0;
    cfg.enable_trailing = false;
    cfg.leverage = 500.0;
    cfg.stop_out_level = 50.0;
    cfg.contract_size = 1.0;
    cfg.spread = 1.0;

    AccurateBacktester bt;
    bt.configure(cfg);
    bt.reset(INITIAL);

    std::cout << "Processing ticks...\n" << std::flush;
    size_t count = 0;
    for (const auto& t : nas_ticks) {
        bt.on_tick(t.bid, t.ask);
        count++;
        if (count % 10000000 == 0) {
            std::cout << "  Processed " << count << " ticks...\n" << std::flush;
        }
    }

    std::cout << "Getting result...\n" << std::flush;
    auto r = bt.get_result(nas_ticks.back().bid);

    std::cout << "\n=== RESULT ===\n";
    std::cout << "Final Equity: $" << std::fixed << std::setprecision(2) << r.final_equity << "\n";
    std::cout << "Return: " << (r.final_equity / INITIAL) << "x\n";
    std::cout << "Max DD: " << r.max_drawdown_pct << "%\n";
    std::cout << "Trades: " << r.total_trades << "\n";
    std::cout << "Margin Call: " << (r.margin_call_occurred ? "YES" : "NO") << "\n";

    return 0;
}
