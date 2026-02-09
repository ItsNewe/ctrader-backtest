#include <iostream>
#include <fstream>
#include <sstream>
#include "../include/backtester_accurate.h"

int main() {
    std::cout << "Step 1: Opening file...\n" << std::flush;
    std::ifstream file("NAS100/NAS100_TICKS_2025.csv");
    if (!file.is_open()) {
        std::cerr << "Cannot open file!\n";
        return 1;
    }
    std::cout << "Step 1: File opened\n" << std::flush;

    std::string line;
    std::getline(file, line);
    std::cout << "Step 2: Header read\n" << std::flush;

    std::cout << "Step 3: Creating backtester...\n" << std::flush;
    AccurateBacktester bt;
    std::cout << "Step 3: Backtester created\n" << std::flush;

    std::cout << "Step 4: Configuring...\n" << std::flush;
    BacktestConfig cfg;
    cfg.survive_down_pct = 4.0;  // Low value - likely to cause issues
    cfg.min_entry_spacing = 50.0;
    cfg.enable_trailing = false;
    cfg.leverage = 500.0;
    cfg.stop_out_level = 50.0;
    cfg.contract_size = 1.0;
    cfg.spread = 1.0;
    bt.configure(cfg);
    std::cout << "Step 4: Configured\n" << std::flush;

    std::cout << "Step 5: Resetting...\n" << std::flush;
    bt.reset(10000.0);
    std::cout << "Step 5: Reset complete\n" << std::flush;

    std::cout << "Step 6: Processing ticks...\n" << std::flush;
    size_t count = 0;
    double last_bid = 0;

    while (std::getline(file, line) && count < 5000000) {
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
                count++;
                if (count % 500000 == 0) {
                    std::cout << "  Tick " << count << ", Positions: "
                              << bt.get_position_count()
                              << ", Balance: $" << bt.get_balance() << "\n" << std::flush;
                }
            }
        } catch (...) {
            continue;
        }
    }

    std::cout << "Step 6: Processed " << count << " ticks\n" << std::flush;

    std::cout << "Step 7: Getting result...\n" << std::flush;
    auto r = bt.get_result(last_bid);
    std::cout << "Step 7: Result obtained\n" << std::flush;

    std::cout << "\nFinal: $" << r.final_equity << ", Trades: " << r.total_trades
              << ", Margin call: " << (r.margin_call_occurred ? "YES" : "NO") << "\n";

    return 0;
}
