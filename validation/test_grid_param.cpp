#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include "../include/grid_improved.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: test_grid_param <mode> [params...]\n";
        std::cerr << "Modes:\n";
        std::cerr << "  baseline <survive>\n";
        std::cerr << "  regime <survive> <sma_period> <buffer_pct>\n";
        std::cerr << "  profit <survive> <take_pct> <take_amount>\n";
        std::cerr << "  crash <survive> <velocity> <lookback> <exit_pct>\n";
        std::cerr << "  volatility <survive> <period> <multiplier>\n";
        std::cerr << "  combined <survive>\n";
        return 1;
    }

    std::string mode = argv[1];
    ImprovedConfig cfg;
    cfg.min_entry_spacing = 50.0;
    cfg.leverage = 500.0;
    cfg.stop_out_level = 50.0;
    cfg.contract_size = 1.0;
    cfg.spread = 1.0;

    std::string test_name;

    if (mode == "baseline" && argc >= 3) {
        cfg.survive_down_pct = std::atof(argv[2]);
        test_name = "Baseline";
    }
    else if (mode == "regime" && argc >= 5) {
        cfg.survive_down_pct = std::atof(argv[2]);
        cfg.enable_regime_filter = true;
        cfg.sma_period = std::atoi(argv[3]);
        cfg.sma_buffer_pct = std::atof(argv[4]);
        test_name = "Regime";
    }
    else if (mode == "profit" && argc >= 5) {
        cfg.survive_down_pct = std::atof(argv[2]);
        cfg.enable_profit_taking = true;
        cfg.profit_take_pct = std::atof(argv[3]);
        cfg.profit_take_amount = std::atof(argv[4]);
        test_name = "ProfitTake";
    }
    else if (mode == "crash" && argc >= 6) {
        cfg.survive_down_pct = std::atof(argv[2]);
        cfg.enable_crash_detection = true;
        cfg.crash_velocity_threshold = std::atof(argv[3]);
        cfg.crash_lookback = std::atoi(argv[4]);
        cfg.crash_exit_pct = std::atof(argv[5]);
        test_name = "CrashDetect";
    }
    else if (mode == "volatility" && argc >= 5) {
        cfg.survive_down_pct = std::atof(argv[2]);
        cfg.enable_volatility_sizing = true;
        cfg.volatility_period = std::atoi(argv[3]);
        cfg.volatility_multiplier = std::atof(argv[4]);
        test_name = "VolatilitySizing";
    }
    else if (mode == "combined" && argc >= 3) {
        cfg.survive_down_pct = std::atof(argv[2]);
        cfg.enable_regime_filter = true;
        cfg.sma_period = 200;
        cfg.sma_buffer_pct = 0.5;
        cfg.enable_profit_taking = true;
        cfg.profit_take_pct = 30.0;
        cfg.profit_take_amount = 0.3;
        cfg.enable_crash_detection = true;
        cfg.crash_velocity_threshold = -1.5;
        cfg.crash_lookback = 1000;
        cfg.crash_exit_pct = 0.5;
        cfg.enable_volatility_sizing = true;
        cfg.volatility_multiplier = 1.0;
        test_name = "Combined";
    }
    else {
        std::cerr << "Invalid mode or parameters\n";
        return 1;
    }

    std::ifstream file("NAS100/NAS100_TICKS_2025.csv");
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
              << r.profit_takes << ","
              << r.crash_exits << ","
              << r.regime_blocks << ","
              << (r.margin_call_occurred ? "MARGIN_CALL" : "OK") << "\n";

    return 0;
}
