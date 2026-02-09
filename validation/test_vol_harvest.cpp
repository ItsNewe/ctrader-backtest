#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include "../include/strategy_volatility_harvest.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: test_vol_harvest <asset> [lot_size] [lookback] [entry_drop] [tp] [sl]\n";
        return 1;
    }

    std::string asset = argv[1];
    VolatilityHarvestConfig cfg;

    if (argc >= 3) cfg.lot_size = std::atof(argv[2]);
    if (argc >= 4) cfg.lookback = std::atoi(argv[3]);
    if (argc >= 5) cfg.entry_drop_pct = std::atof(argv[4]);
    if (argc >= 6) cfg.take_profit = std::atof(argv[5]);
    if (argc >= 7) cfg.stop_loss = std::atof(argv[6]);

    cfg.leverage = 500.0;
    cfg.stop_out_level = 50.0;

    std::string filename;
    if (asset == "NAS100") {
        cfg.contract_size = 1.0;
        cfg.spread = 1.0;
        cfg.min_spacing = 20.0;
        filename = "validation/NAS100/NAS100_TICKS_2025.csv";
    } else if (asset == "GOLD") {
        cfg.contract_size = 100.0;
        cfg.spread = 0.25;
        cfg.min_spacing = 2.0;
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

    VolatilityHarvestStrategy bt;
    bt.configure(cfg);
    bt.reset(10000.0);

    double last_bid = 0;
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
                bt.on_tick(bid, ask);
                tick_count++;
            }
        } catch (...) {
            continue;
        }
    }

    auto r = bt.get_result(last_bid);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Asset: " << asset << "\n";
    std::cout << "Lot Size: " << cfg.lot_size << "\n";
    std::cout << "Lookback: " << cfg.lookback << "\n";
    std::cout << "Entry Drop %: " << cfg.entry_drop_pct << "\n";
    std::cout << "Take Profit: " << cfg.take_profit << "\n";
    std::cout << "Stop Loss: " << cfg.stop_loss << "\n";
    std::cout << "---\n";
    std::cout << "Final Equity: $" << r.final_equity << "\n";
    std::cout << "Return: " << (r.final_equity / 10000.0) << "x\n";
    std::cout << "Max Drawdown: " << r.max_drawdown_pct << "%\n";
    std::cout << "Total Trades: " << r.total_trades << "\n";
    std::cout << "TP Hits: " << r.tp_hits << "\n";
    std::cout << "SL Hits: " << r.sl_hits << "\n";
    std::cout << "Win Rate: " << (r.total_trades > 0 ? (100.0 * r.tp_hits / (r.tp_hits + r.sl_hits)) : 0) << "%\n";
    std::cout << "Margin Call: " << (r.margin_call_occurred ? "YES" : "NO") << "\n";
    std::cout << "Ticks Processed: " << tick_count << "\n";

    return 0;
}
