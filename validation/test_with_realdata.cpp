#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include "../include/backtester_accurate.h"

int main() {
    std::cout << "Opening file...\n" << std::flush;
    std::ifstream file("NAS100/NAS100_TICKS_2025.csv");
    if (!file.is_open()) {
        std::cerr << "Cannot open file!\n";
        return 1;
    }

    std::string line;
    std::getline(file, line);  // Skip header
    std::cout << "Header skipped\n" << std::flush;

    std::cout << "Creating backtester...\n" << std::flush;
    AccurateBacktester bt;
    BacktestConfig cfg;
    cfg.survive_down_pct = 30.0;
    cfg.min_entry_spacing = 50.0;
    cfg.enable_trailing = false;
    cfg.leverage = 500.0;
    cfg.stop_out_level = 50.0;
    cfg.contract_size = 1.0;
    cfg.spread = 1.0;

    bt.configure(cfg);
    bt.reset(10000.0);
    std::cout << "Backtester ready\n" << std::flush;

    size_t count = 0;
    size_t max_ticks = 1000000;

    std::cout << "Processing ticks...\n" << std::flush;
    while (std::getline(file, line) && count < max_ticks) {
        try {
            std::stringstream ss(line);
            std::string token;

            std::getline(ss, token, '\t');  // timestamp
            std::getline(ss, token, '\t');  // bid
            if (token.empty()) continue;
            double bid = std::stod(token);

            std::getline(ss, token, '\t');  // ask
            if (token.empty()) continue;
            double ask = std::stod(token);

            if (bid > 0 && ask > 0) {
                bt.on_tick(bid, ask);
                count++;
                if (count % 100000 == 0) {
                    std::cout << "  Processed " << count << " ticks, Positions: "
                              << bt.get_position_count() << "\n" << std::flush;
                }
            }
        } catch (...) {
            continue;
        }
    }

    std::cout << "\nGetting result after " << count << " ticks...\n" << std::flush;
    auto r = bt.get_result(20000.0);  // Use approximate final price

    std::cout << "\n=== RESULT ===\n";
    std::cout << "Final Equity: $" << r.final_equity << "\n";
    std::cout << "Return: " << (r.final_equity / 10000.0) << "x\n";
    std::cout << "Max DD: " << r.max_drawdown_pct << "%\n";
    std::cout << "Trades: " << r.total_trades << "\n";
    std::cout << "Margin Call: " << (r.margin_call_occurred ? "YES" : "NO") << "\n";

    return 0;
}
