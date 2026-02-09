#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include "../include/strategy_fillup_hedged.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: test_fillup_hedged <asset> [survive] [spacing] [hedge_trigger] [hedge_ratio]\n";
        return 1;
    }

    std::string asset = argv[1];
    HedgedFillUpConfig cfg;

    // Set defaults based on asset (matching working fill-up test)
    if (asset == "GOLD") {
        cfg.contract_size = 100.0;
        cfg.spread = 0.25;
        cfg.spacing = 1.0;        // Match working test
        cfg.hedge_spacing = 1.0;
        cfg.survive_down_pct = 13.0;  // Match working test
    } else {
        cfg.contract_size = 1.0;
        cfg.spread = 1.0;
        cfg.spacing = 10.0;
        cfg.hedge_spacing = 10.0;
        cfg.survive_down_pct = 13.0;
    }

    // Override with command line
    // Args: asset survive spacing hedge_trigger hedge_ratio [dd_pct] [vel_pct]
    if (argc >= 3) cfg.survive_down_pct = std::atof(argv[2]);
    if (argc >= 4) cfg.spacing = std::atof(argv[3]);
    if (argc >= 5) cfg.hedge_trigger_pct = std::atof(argv[4]);
    if (argc >= 6) cfg.hedge_ratio = std::atof(argv[5]);
    if (argc >= 7) {
        double dd_pct = std::atof(argv[6]);
        if (dd_pct > 0) {
            cfg.enable_dd_protection = true;
            cfg.close_all_dd_pct = dd_pct;
        }
    }
    if (argc >= 8) {
        double vel_pct = std::atof(argv[7]);
        if (vel_pct < 0) {  // Velocity is negative for crashes
            cfg.enable_velocity_filter = true;
            cfg.crash_velocity_pct = vel_pct;
        }
    }

    cfg.hedge_spacing = cfg.spacing;  // Same spacing for hedges
    cfg.leverage = 500.0;
    cfg.stop_out_level = 20.0;  // MT5 stop-out level

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
    std::getline(file, line);

    HedgedFillUpStrategy bt;
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
    std::cout << "=== Hedged Fill-Up Strategy ===\n";
    std::cout << "Asset: " << asset << "\n";
    std::cout << "Survive Down: " << cfg.survive_down_pct << "%\n";
    std::cout << "Spacing: " << cfg.spacing << "\n";
    std::cout << "Hedge Trigger: " << cfg.hedge_trigger_pct << "%\n";
    std::cout << "Hedge Ratio: " << cfg.hedge_ratio << "\n";
    std::cout << "---\n";
    std::cout << "Final Equity: $" << r.final_equity << "\n";
    std::cout << "Return: " << (r.final_equity / 10000.0) << "x\n";
    std::cout << "Max Drawdown: " << r.max_drawdown_pct << "%\n";
    std::cout << "---\n";
    std::cout << "Long Entries: " << r.long_entries << "\n";
    std::cout << "Short Entries: " << r.short_entries << "\n";
    std::cout << "Long TP Hits: " << r.long_tp_hits << "\n";
    std::cout << "Short TP Hits: " << r.short_tp_hits << "\n";
    std::cout << "Hedge Activations: " << r.hedge_activations << "\n";
    std::cout << "---\n";
    std::cout << "Max Long Lots: " << r.max_long_lots << "\n";
    std::cout << "Max Short Lots: " << r.max_short_lots << "\n";
    std::cout << "Max Net Exposure: " << r.max_net_exposure << " lots\n";
    std::cout << "---\n";
    std::cout << "DD Triggers: " << r.dd_protection_triggers << "\n";
    std::cout << "Vel Pauses: " << r.velocity_pauses << "\n";
    std::cout << "Margin Call: " << (r.margin_call_occurred ? "YES" : "NO") << "\n";
    std::cout << "Ticks: " << tick_count << "\n";

    return 0;
}
