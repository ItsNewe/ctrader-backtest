#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include "../include/backtester_accurate.h"

int main(int argc, char* argv[]) {
    if (argc < 5) {
        std::cerr << "Usage: test_gold_param <survive> <spacing> <trailing:0|1> <atr>\n";
        return 1;
    }

    double survive = std::atof(argv[1]);
    double spacing = std::atof(argv[2]);
    bool trailing = std::atoi(argv[3]) != 0;
    double atr = std::atof(argv[4]);

    std::ifstream file("Grid/XAUUSD_TICKS_2025.csv");
    if (!file.is_open()) {
        std::cerr << "Cannot open file\n";
        return 1;
    }

    std::string line;
    std::getline(file, line);

    BacktestConfig cfg;
    cfg.survive_down_pct = survive;
    cfg.min_entry_spacing = spacing;
    cfg.enable_trailing = trailing;
    cfg.atr_multiplier = atr;
    cfg.leverage = 500.0;
    cfg.stop_out_level = 50.0;
    cfg.contract_size = 100.0;  // Gold contract size
    cfg.spread = 0.25;          // Gold spread

    AccurateBacktester bt;
    bt.configure(cfg);
    bt.reset(10000.0);

    double last_bid = 0;
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
            }
        } catch (...) {
            continue;
        }
    }

    auto r = bt.get_result(last_bid);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << survive << "," << spacing << "," << (trailing ? "1" : "0") << "," << atr << ","
              << r.final_equity << "," << (r.final_equity / 10000.0) << ","
              << r.max_drawdown_pct << "," << r.total_trades << ","
              << r.total_spread_cost << "," << (r.margin_call_occurred ? "MARGIN_CALL" : "OK") << "\n";

    return 0;
}
