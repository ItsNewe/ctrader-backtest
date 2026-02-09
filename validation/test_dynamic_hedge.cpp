#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include "../include/strategy_dynamic_hedge.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: test_dynamic_hedge <asset> [lot_size] [spacing] [tp_spread]\n";
        return 1;
    }

    std::string asset = argv[1];
    DynamicHedgeConfig cfg;

    if (argc >= 3) cfg.base_lot_size = std::atof(argv[2]);
    if (argc >= 4) cfg.hedge_spacing = std::atof(argv[3]);
    if (argc >= 5) cfg.tp_spread = std::atof(argv[4]);

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
    std::getline(file, line);

    DynamicHedgeStrategy bt;
    bt.configure(cfg);
    bt.reset(10000.0);

    double last_bid = 0, last_ask = 0;
    long tick_count = 0;

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
    std::cout << "Lot Size: " << cfg.base_lot_size << "\n";
    std::cout << "Hedge Spacing: " << cfg.hedge_spacing << "\n";
    std::cout << "TP Spread: " << cfg.tp_spread << "\n";
    std::cout << "---\n";
    std::cout << "Final Equity: $" << r.final_equity << "\n";
    std::cout << "Return: " << (r.final_equity / 10000.0) << "x\n";
    std::cout << "Max Drawdown: " << r.max_drawdown_pct << "%\n";
    std::cout << "Total Trades: " << r.total_trades << "\n";
    std::cout << "Long Trades: " << r.long_trades << "\n";
    std::cout << "Short Trades: " << r.short_trades << "\n";
    std::cout << "Pairs Closed: " << r.pairs_closed << "\n";
    std::cout << "Max Net Exposure: " << (r.max_net_exposure * 100.0) << "%\n";
    std::cout << "Margin Call: " << (r.margin_call_occurred ? "YES" : "NO") << "\n";
    std::cout << "Ticks Processed: " << tick_count << "\n";

    return 0;
}
