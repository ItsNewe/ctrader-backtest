#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include "../include/backtester_accurate.h"

int main() {
    std::cout << "Opening file...\n" << std::flush;
    std::ifstream file("NAS100/NAS100_TICKS_2025.csv");
    if (!file.is_open()) {
        std::cerr << "Cannot open file!\n";
        return 1;
    }

    std::string line;
    std::getline(file, line);

    std::cout << "Configuring backtester (survive=4%, spacing=50, no trailing)...\n" << std::flush;
    BacktestConfig cfg;
    cfg.survive_down_pct = 4.0;
    cfg.min_entry_spacing = 50.0;
    cfg.enable_trailing = false;
    cfg.leverage = 500.0;
    cfg.stop_out_level = 50.0;
    cfg.contract_size = 1.0;
    cfg.spread = 1.0;

    AccurateBacktester bt;
    bt.configure(cfg);
    bt.reset(10000.0);

    std::cout << "Processing ALL ticks...\n" << std::flush;
    size_t count = 0;
    double last_bid = 0, first_bid = 0;

    while (std::getline(file, line)) {
        try {
            std::stringstream ss(line);
            std::string token;

            std::getline(ss, token, '\t');
            std::getline(ss, token, '\t');
            if (token.empty()) continue;
            double bid = std::stod(token);

            std::getline(ss, token, '\t');
            if (token.empty()) continue;
            double ask = std::stod(token);

            if (bid > 0 && ask > 0) {
                if (first_bid == 0) first_bid = bid;
                last_bid = bid;
                bt.on_tick(bid, ask);
                count++;
                if (count % 10000000 == 0) {
                    std::cout << "  " << (count / 1000000) << "M ticks, Positions: "
                              << bt.get_position_count()
                              << ", Balance: $" << std::fixed << std::setprecision(2) << bt.get_balance() << "\n" << std::flush;
                }
            }
        } catch (...) {
            continue;
        }
    }

    std::cout << "Processed " << count << " ticks\n" << std::flush;
    std::cout << "Price: " << first_bid << " -> " << last_bid << "\n" << std::flush;

    auto r = bt.get_result(last_bid);
    std::cout << "\n=== RESULT ===\n";
    std::cout << "Final Equity: $" << r.final_equity << "\n";
    std::cout << "Return: " << (r.final_equity / 10000.0) << "x\n";
    std::cout << "Max DD: " << r.max_drawdown_pct << "%\n";
    std::cout << "Trades: " << r.total_trades << "\n";
    std::cout << "Spread Cost: $" << r.total_spread_cost << "\n";
    std::cout << "Margin Call: " << (r.margin_call_occurred ? "YES" : "NO") << "\n";

    return 0;
}
