#include <iostream>
#include "../include/backtester_accurate.h"

int main() {
    std::cout << "Creating backtester...\n" << std::flush;
    AccurateBacktester bt;

    std::cout << "Configuring...\n" << std::flush;
    BacktestConfig cfg;
    cfg.survive_down_pct = 30.0;
    cfg.min_entry_spacing = 50.0;
    cfg.enable_trailing = false;
    bt.configure(cfg);

    std::cout << "Resetting...\n" << std::flush;
    bt.reset(10000.0);

    std::cout << "Simulating ticks...\n" << std::flush;
    // Simulate an uptrend
    double price = 20000.0;
    for (int i = 0; i < 1000; i++) {
        price += 0.5;  // Slight uptick
        bt.on_tick(price);
        if (i % 100 == 0) {
            std::cout << "  Tick " << i << ", Price: " << price
                      << ", Positions: " << bt.get_position_count() << "\n" << std::flush;
        }
    }

    std::cout << "Getting result...\n" << std::flush;
    auto r = bt.get_result(price);

    std::cout << "\nResult:\n";
    std::cout << "  Final Equity: $" << r.final_equity << "\n";
    std::cout << "  Trades: " << r.total_trades << "\n";
    std::cout << "  Max DD: " << r.max_drawdown_pct << "%\n";

    return 0;
}
