#include "../include/strategy_parallel_dual_original.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>

using namespace backtest;

std::vector<Tick> g_ticks;

void LoadTickData() {
    std::cout << "Loading tick data..." << std::endl;
    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;

    TickDataManager manager(tick_config);
    manager.Reset();

    Tick tick;
    while (manager.GetNextTick(tick)) {
        g_ticks.push_back(tick);
    }
    std::cout << "Loaded " << g_ticks.size() << " ticks" << std::endl;
}

void RunTest(double survive_pct, double grid_alloc, double tp_dist, bool force_entry) {
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
    config.verbose = false;

    TickBasedEngine engine(config);

    ParallelDualStrategyOriginal::Config strat_config;
    strat_config.survive_pct = survive_pct;
    strat_config.grid_allocation = grid_alloc;
    strat_config.momentum_allocation = 1.0 - grid_alloc;
    strat_config.min_volume = 0.01;
    strat_config.max_volume = 10.0;
    strat_config.contract_size = 100.0;
    strat_config.leverage = 500.0;
    strat_config.base_spacing = 1.50;
    strat_config.momentum_spacing = 5.0;
    strat_config.force_min_volume_entry = force_entry;
    strat_config.use_take_profit = (tp_dist > 0);
    strat_config.tp_distance = tp_dist;

    ParallelDualStrategyOriginal strategy(strat_config);

    engine.RunWithTicks(g_ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();
    auto stats = strategy.GetStats();

    double total_unrealized = 0;
    for (const Trade* t : engine.GetOpenPositions()) {
        total_unrealized += (g_ticks.back().bid - t->entry_price) * t->lot_size * 100.0;
    }
    double final_equity = results.final_balance + total_unrealized;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "survive=" << survive_pct << "% grid=" << (int)(grid_alloc*100) << "% "
              << "TP=$" << tp_dist << " force=" << (force_entry ? "YES" : "NO") << std::endl;

    if (results.stop_out_occurred) {
        std::cout << "  STOP-OUT!" << std::endl;
        return;
    }

    std::cout << "  Balance: $" << results.final_balance << std::endl;
    std::cout << "  Unrealized: $" << total_unrealized << std::endl;
    std::cout << "  Equity: $" << final_equity << " (" << (final_equity/10000.0) << "x)" << std::endl;
    std::cout << "  Max DD: " << results.max_drawdown_pct << "%" << std::endl;
    std::cout << "  Closed trades: " << engine.GetClosedTrades().size() << std::endl;
    std::cout << "  Open positions: " << engine.GetOpenPositions().size() << std::endl;
    std::cout << "  Grid entries: " << stats.grid_entries << std::endl;
    std::cout << "  Momentum entries: " << stats.momentum_entries << std::endl;
    std::cout << "  Swap charged: $" << results.total_swap_charged << std::endl;
    std::cout << std::endl;
}

int main() {
    std::cout << "=== Reproducing Million Dollar Results ===" << std::endl;
    std::cout << "Original config: survive=10%, grid=30%, TP=$5.00" << std::endl;
    std::cout << std::endl;

    LoadTickData();

    std::cout << "\n=== Testing Original Configuration ===" << std::endl;
    RunTest(10.0, 0.3, 5.0, false);  // Original config

    std::cout << "\n=== Testing Variations ===" << std::endl;
    RunTest(10.0, 0.3, 5.0, true);   // With force entry
    RunTest(10.0, 0.5, 5.0, false);  // 50% grid
    RunTest(12.0, 0.3, 5.0, false);  // Higher survive
    RunTest(10.0, 0.3, 3.0, false);  // Tighter TP
    RunTest(10.0, 0.3, 7.0, false);  // Wider TP

    std::cout << "\n=== Fine-Grained Grid Allocation Sweep (10-30%) ===" << std::endl;
    for (double grid = 0.10; grid <= 0.30; grid += 0.02) {
        RunTest(10.0, grid, 5.0, false);
    }

    return 0;
}
