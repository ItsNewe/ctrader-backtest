#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include "../include/grid_improved.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: test_grid_gold_param <mode> [params...]\n";
        return 1;
    }

    std::string mode = argv[1];
    ImprovedConfig cfg;
    cfg.min_entry_spacing = 2.0;  // Gold uses $2 spacing
    cfg.leverage = 500.0;
    cfg.stop_out_level = 50.0;
    cfg.contract_size = 100.0;   // Gold contract size
    cfg.spread = 0.25;           // Gold spread

    std::string test_name;

    if (mode == "baseline" && argc >= 3) {
        cfg.survive_down_pct = std::atof(argv[2]);
        test_name = "Baseline";
    }
    else if (mode == "crash" && argc >= 6) {
        cfg.survive_down_pct = std::atof(argv[2]);
        cfg.enable_crash_detection = true;
        cfg.crash_velocity_threshold = std::atof(argv[3]);
        cfg.crash_lookback = std::atoi(argv[4]);
        cfg.crash_exit_pct = std::atof(argv[5]);
        test_name = "CrashDetect";
    }
    else {
        std::cerr << "Invalid mode\n";
        return 1;
    }

    std::ifstream file("Grid/XAUUSD_TICKS_2025.csv");
    if (!file.is_open()) {
        std::cerr << "Cannot open file\n";
        return 1;
    }

    std::string line;
    std::getline(file, line);

    ImprovedGrid bt;
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
    std::cout << test_name << ","
              << cfg.survive_down_pct << ","
              << r.final_equity << ","
              << (r.final_equity / 10000.0) << ","
              << r.max_drawdown_pct << ","
              << r.total_trades << ","
              << r.crash_exits << ","
              << (r.margin_call_occurred ? "MARGIN_CALL" : "OK") << "\n";

    return 0;
}
