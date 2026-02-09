#include "../include/strategy_parallel_dual.h"
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

struct Result {
    std::string name;
    double survive_pct;
    double final_equity;
    double return_mult;
    double max_dd;
    int entries;
    bool stop_out;
};

Result RunNewStrategy(double survive_pct) {
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

    ParallelDualStrategy::Config strat_config;
    strat_config.survive_pct = survive_pct;
    strat_config.min_volume = 0.01;
    strat_config.max_volume = 10.0;
    strat_config.contract_size = 100.0;
    strat_config.leverage = 500.0;
    strat_config.min_spacing = 1.0;
    strat_config.safety_buffer = 1.5;

    ParallelDualStrategy strategy(strat_config);

    engine.RunWithTicks(g_ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();
    auto stats = strategy.GetStats();

    double total_unrealized = 0;
    for (const Trade* t : engine.GetOpenPositions()) {
        total_unrealized += (g_ticks.back().bid - t->entry_price) * t->lot_size * 100.0;
    }

    Result r;
    r.name = "NEW (Unified)";
    r.survive_pct = survive_pct;
    r.final_equity = results.final_balance + total_unrealized;
    r.return_mult = r.final_equity / 10000.0;
    r.max_dd = results.max_drawdown_pct;
    r.entries = stats.total_entries;
    r.stop_out = results.stop_out_occurred;
    return r;
}

Result RunOriginalStrategy(double survive_pct, double grid_alloc, bool use_tp, double tp_dist) {
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
    strat_config.force_min_volume_entry = false;
    strat_config.use_take_profit = use_tp;
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

    Result r;
    r.name = "OLD (Parallel)";
    r.survive_pct = survive_pct;
    r.final_equity = results.final_balance + total_unrealized;
    r.return_mult = r.final_equity / 10000.0;
    r.max_dd = results.max_drawdown_pct;
    r.entries = stats.total_entries;
    r.stop_out = results.stop_out_occurred;
    return r;
}

void PrintResult(const Result& r) {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  " << std::setw(15) << r.name
              << " survive=" << std::setw(4) << r.survive_pct << "% | ";

    if (r.stop_out) {
        std::cout << "STOP-OUT" << std::endl;
        return;
    }

    std::cout << "Eq: $" << std::setw(10) << r.final_equity
              << " (" << std::setw(6) << r.return_mult << "x) "
              << "DD: " << std::setw(5) << r.max_dd << "% "
              << "Entries: " << std::setw(4) << r.entries
              << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  Strategy Comparison Test" << std::endl;
    std::cout << "  NEW (Unified) vs OLD (Parallel Dual)" << std::endl;
    std::cout << "========================================" << std::endl;

    LoadTickData();
    if (g_ticks.empty()) {
        std::cerr << "ERROR: No ticks loaded!" << std::endl;
        return 1;
    }

    std::cout << "\nPrice: $" << g_ticks.front().bid << " -> $" << g_ticks.back().bid << std::endl;

    std::vector<double> survive_values = {12.0, 15.0, 18.0, 20.0};

    std::cout << "\n=== Comparison: NEW vs OLD (with TP=$3) ===" << std::endl;
    for (double survive : survive_values) {
        std::cout << "\n--- survive_pct = " << survive << "% ---" << std::endl;
        PrintResult(RunNewStrategy(survive));
        PrintResult(RunOriginalStrategy(survive, 0.5, true, 3.0));
    }

    std::cout << "\n=== Comparison: NEW vs OLD (no TP) ===" << std::endl;
    for (double survive : survive_values) {
        std::cout << "\n--- survive_pct = " << survive << "% ---" << std::endl;
        PrintResult(RunNewStrategy(survive));
        PrintResult(RunOriginalStrategy(survive, 0.5, false, 0.0));
    }

    std::cout << "\n=== OLD with different allocations (survive=15%, TP=$3) ===" << std::endl;
    std::vector<double> allocs = {0.3, 0.5, 0.7, 0.9};
    for (double alloc : allocs) {
        std::cout << "  grid_alloc=" << (int)(alloc*100) << "%: ";
        Result r = RunOriginalStrategy(15.0, alloc, true, 3.0);
        if (r.stop_out) {
            std::cout << "STOP-OUT" << std::endl;
        } else {
            std::cout << std::fixed << std::setprecision(2)
                      << r.return_mult << "x, DD=" << r.max_dd << "%, entries=" << r.entries << std::endl;
        }
    }

    std::cout << "\n=== Summary ===" << std::endl;
    std::cout << "NEW strategy: Unified floor tracking with dynamic margin survival" << std::endl;
    std::cout << "OLD strategy: Parallel grid+momentum with allocation splits" << std::endl;

    return 0;
}
