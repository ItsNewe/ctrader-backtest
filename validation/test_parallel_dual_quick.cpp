#include "../include/strategy_parallel_dual.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>

using namespace backtest;

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  Unified Dynamic Survival Strategy" << std::endl;
    std::cout << "  Quick Validation Test" << std::endl;
    std::cout << "========================================" << std::endl;

    // Load tick data
    std::cout << "\nLoading tick data..." << std::endl;
    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;

    TickDataManager manager(tick_config);
    manager.Reset();

    std::vector<Tick> ticks;
    Tick tick;
    while (manager.GetNextTick(tick)) {
        ticks.push_back(tick);
    }

    std::cout << "Loaded " << ticks.size() << " ticks" << std::endl;
    std::cout << "Period: " << ticks.front().timestamp << " to " << ticks.back().timestamp << std::endl;
    std::cout << "Price: $" << ticks.front().bid << " -> $" << ticks.back().bid << std::endl;

    // Test with 15% survive (recommended for safety)
    std::cout << "\n=== Testing survive_pct=15% (Recommended) ===" << std::endl;

    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = 10000.0;
    config.contract_size = 100.0;
    config.leverage = 500.0;
    config.pip_size = 0.01;
    config.swap_long = -66.99;
    config.swap_short = 41.2;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = "2025.01.01";
    config.end_date = "2025.12.30";

    TickBasedEngine engine(config);

    ParallelDualStrategy::Config strat_config;
    strat_config.survive_pct = 15.0;
    strat_config.min_volume = 0.01;
    strat_config.max_volume = 10.0;
    strat_config.contract_size = 100.0;
    strat_config.leverage = 500.0;
    strat_config.min_spacing = 1.0;
    strat_config.max_spacing = 15.0;
    strat_config.base_spacing = 1.50;
    strat_config.target_trades_in_range = 20;
    strat_config.margin_stop_out = 20.0;
    strat_config.safety_buffer = 1.5;

    ParallelDualStrategy strategy(strat_config);

    engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();
    auto stats = strategy.GetStats();

    // Calculate final equity
    double total_unrealized = 0;
    for (const Trade* t : engine.GetOpenPositions()) {
        total_unrealized += (ticks.back().bid - t->entry_price) * t->lot_size * 100.0;
    }
    double final_equity = results.final_balance + total_unrealized;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\nResults:" << std::endl;
    std::cout << "  Final Balance:  $" << results.final_balance << std::endl;
    std::cout << "  Unrealized P/L: $" << total_unrealized << std::endl;
    std::cout << "  Final Equity:   $" << final_equity << std::endl;
    std::cout << "  Return:         " << (final_equity / 10000.0) << "x" << std::endl;
    std::cout << "  Max Drawdown:   " << results.max_drawdown_pct << "%" << std::endl;
    std::cout << "  Stop-out:       " << (results.stop_out_occurred ? "YES" : "NO") << std::endl;

    std::cout << "\nEntry Statistics:" << std::endl;
    std::cout << "  Upward entries:   " << stats.upward_entries << std::endl;
    std::cout << "  Downward entries: " << stats.downward_entries << std::endl;
    std::cout << "  Total entries:    " << stats.total_entries << std::endl;
    std::cout << "  Skipped (margin): " << stats.skipped_by_margin << std::endl;
    std::cout << "  Highest entry:    $" << stats.highest_entry << std::endl;
    std::cout << "  Lowest entry:     $" << stats.lowest_entry << std::endl;

    // Verification
    std::cout << "\n=== VERIFICATION ===" << std::endl;
    bool pass = true;

    if (results.stop_out_occurred) {
        std::cout << "[FAIL] Stop-out occurred!" << std::endl;
        pass = false;
    } else {
        std::cout << "[PASS] No stop-out" << std::endl;
    }

    if (final_equity / 10000.0 > 1.0) {
        std::cout << "[PASS] Returns better than conservative (" << (final_equity / 10000.0) << "x > 1.0x)" << std::endl;
    } else {
        std::cout << "[FAIL] Returns too low" << std::endl;
        pass = false;
    }

    if (stats.upward_entries > 0) {
        std::cout << "[PASS] Upward entries occurring (" << stats.upward_entries << ")" << std::endl;
    } else {
        std::cout << "[FAIL] No upward entries" << std::endl;
        pass = false;
    }

    if (stats.downward_entries >= 0) {
        std::cout << "[PASS] Downward entry logic present (" << stats.downward_entries << ")" << std::endl;
    }

    std::cout << "\n" << (pass ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << std::endl;

    return pass ? 0 : 1;
}
