#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include "../include/strategy_bidirectional_grid.h"

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: test_bigrid <asset> <spacing> [lot_size] [max_levels] [rebalance]\n";
        std::cerr << "  asset: NAS100 or GOLD\n";
        return 1;
    }

    std::string asset = argv[1];
    BiGridConfig cfg;
    cfg.grid_spacing = std::atof(argv[2]);

    if (argc >= 4) cfg.lot_size = std::atof(argv[3]);
    if (argc >= 5) cfg.max_levels_per_side = std::atoi(argv[4]);
    if (argc >= 6) cfg.enable_rebalancing = (std::atoi(argv[5]) != 0);

    cfg.take_profit = cfg.grid_spacing;  // TP = spacing by default
    cfg.leverage = 500.0;
    cfg.stop_out_level = 50.0;

    std::string filename;
    if (asset == "NAS100") {
        cfg.contract_size = 1.0;
        cfg.spread = 1.0;
        filename = "validation/NAS100/NAS100_TICKS_2025.csv";
    } else if (asset == "GOLD") {
        cfg.contract_size = 100.0;
        cfg.spread = 0.25;
        filename = "validation/Grid/XAUUSD_TICKS_2025.csv";
    } else {
        std::cerr << "Unknown asset: " << asset << "\n";
        return 1;
    }

    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Cannot open file: " << filename << "\n";
        return 1;
    }

    std::string line;
    std::getline(file, line);  // Skip header

    BidirectionalGrid bt;
    bt.configure(cfg);
    bt.reset(10000.0);

    double last_bid = 0, last_ask = 0;
    long tick_count = 0;

    while (std::getline(file, line)) {
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
                last_bid = bid;
                last_ask = ask;
                bt.on_tick(bid, ask);
                tick_count++;
            }
        } catch (...) {
            continue;
        }
    }

    auto r = bt.get_result(last_bid, last_ask);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Asset: " << asset << "\n";
    std::cout << "Grid Spacing: " << cfg.grid_spacing << "\n";
    std::cout << "Lot Size: " << cfg.lot_size << "\n";
    std::cout << "Max Levels/Side: " << cfg.max_levels_per_side << "\n";
    std::cout << "Rebalancing: " << (cfg.enable_rebalancing ? "ON" : "OFF") << "\n";
    std::cout << "---\n";
    std::cout << "Final Equity: $" << r.final_equity << "\n";
    std::cout << "Return: " << (r.final_equity / 10000.0) << "x\n";
    std::cout << "Max Drawdown: " << r.max_drawdown_pct << "%\n";
    std::cout << "Total Trades: " << r.total_trades << "\n";
    std::cout << "Long Trades: " << r.long_trades << "\n";
    std::cout << "Short Trades: " << r.short_trades << "\n";
    std::cout << "TP Hits: " << r.tp_hits << "\n";
    std::cout << "Final Long Lots: " << r.total_long_lots << "\n";
    std::cout << "Final Short Lots: " << r.total_short_lots << "\n";
    std::cout << "Margin Call: " << (r.margin_call_occurred ? "YES" : "NO") << "\n";
    std::cout << "Ticks Processed: " << tick_count << "\n";

    return 0;
}
