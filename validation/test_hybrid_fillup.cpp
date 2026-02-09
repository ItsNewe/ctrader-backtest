#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include "../include/strategy_hybrid_fillup.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: test_hybrid_fillup <asset> [hybrid_lot] [hybrid_spacing] [fillup_spacing] [crash_vel]\n";
        return 1;
    }

    std::string asset = argv[1];
    HybridFillUpConfig cfg;

    // Set defaults based on asset
    if (asset == "GOLD") {
        cfg.contract_size = 100.0;
        cfg.spread = 0.25;
        cfg.hybrid_base_lot = 0.02;
        cfg.hybrid_spacing = 5.0;
        cfg.fillup_spacing = 5.0;
    } else {
        cfg.contract_size = 1.0;
        cfg.spread = 1.0;
        cfg.hybrid_base_lot = 0.01;
        cfg.hybrid_spacing = 50.0;
        cfg.fillup_spacing = 50.0;
    }

    // Override with command line args
    if (argc >= 3) cfg.hybrid_base_lot = std::atof(argv[2]);
    if (argc >= 4) cfg.hybrid_spacing = std::atof(argv[3]);
    if (argc >= 5) cfg.fillup_spacing = std::atof(argv[4]);
    if (argc >= 6) cfg.crash_velocity = std::atof(argv[5]);

    cfg.leverage = 500.0;
    cfg.stop_out_level = 50.0;

    std::string filename;
    if (asset == "GOLD") {
        filename = "validation/Grid/XAUUSD_TICKS_2025.csv";
    } else {
        filename = "validation/NAS100/NAS100_TICKS_2025.csv";
    }

    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Cannot open file: " << filename << "\n";
        return 1;
    }

    std::string line;
    std::getline(file, line);  // Skip header

    HybridFillUpStrategy bt;
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
    std::cout << "=== Hybrid + Fill-Up Combined Strategy ===\n";
    std::cout << "Asset: " << asset << "\n";
    std::cout << "Hybrid Lot: " << cfg.hybrid_base_lot << "\n";
    std::cout << "Hybrid Spacing: " << cfg.hybrid_spacing << "\n";
    std::cout << "FillUp Spacing: " << cfg.fillup_spacing << "\n";
    std::cout << "Crash Velocity: " << cfg.crash_velocity << "%\n";
    std::cout << "---\n";
    std::cout << "Final Equity: $" << r.final_equity << "\n";
    std::cout << "Return: " << (r.final_equity / 10000.0) << "x\n";
    std::cout << "Max Drawdown: " << r.max_drawdown_pct << "%\n";
    std::cout << "---\n";
    std::cout << "Hybrid Entries: " << r.hybrid_entries << "\n";
    std::cout << "FillUp Entries: " << r.fillup_entries << "\n";
    std::cout << "TP Hits: " << r.tp_hits << "\n";
    std::cout << "Crash Exits: " << r.crash_exits << "\n";
    std::cout << "Profit Takes: " << r.profit_takes << "\n";
    std::cout << "Mode Changes: " << r.mode_changes << "\n";
    std::cout << "---\n";
    std::cout << "Time in Hybrid: " << r.time_in_hybrid_pct << "%\n";
    std::cout << "Time in FillUp: " << r.time_in_fillup_pct << "%\n";
    std::cout << "---\n";
    std::cout << "Margin Call: " << (r.margin_call_occurred ? "YES" : "NO") << "\n";
    std::cout << "Ticks: " << tick_count << "\n";

    return 0;
}
