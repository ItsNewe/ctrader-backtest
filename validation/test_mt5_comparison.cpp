#include "../include/strategy_parallel_dual.h"
#include "../include/strategy_parallel_dual_original.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>

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

void ValidateOriginalLotSizing() {
    std::cout << "\n=== ORIGINAL STRATEGY: Lot Sizing ===" << std::endl;
    std::cout << "MT5 EA: ParallelDual_Original_EA.mq5" << std::endl;

    double equity = 10000.0, ask = 2700.0, contract_size = 100.0, leverage = 500.0;
    double grid_allocation = 0.10, momentum_allocation = 0.90;
    double min_volume = 0.01, max_volume = 10.0;

    std::cout << "\nTest: equity=$" << equity << ", price=$" << ask << std::endl;

    double margin_per_lot = ask * contract_size / leverage;
    std::cout << "  margin_per_lot = $" << margin_per_lot << std::endl;

    double grid_lot = (equity * 0.05 * grid_allocation) / margin_per_lot;
    double momentum_lot = (equity * 0.05 * momentum_allocation) / margin_per_lot;

    grid_lot = std::floor(std::max(std::min(grid_lot, max_volume), min_volume) * 100.0) / 100.0;
    momentum_lot = std::floor(std::max(std::min(momentum_lot, max_volume), min_volume) * 100.0) / 100.0;

    std::cout << "  grid_lot=" << grid_lot << ", momentum_lot=" << momentum_lot << std::endl;
}

void ValidateUnifiedLotSizing() {
    std::cout << "\n=== UNIFIED STRATEGY: Lot Sizing ===" << std::endl;

    double equity = 10000.0, price = 2700.0, survive_pct = 15.0;
    double contract_size = 100.0, leverage = 500.0;
    double margin_stop_out = 20.0, safety_buffer = 1.5, min_volume = 0.01;

    double floor = price * (1.0 - survive_pct / 100.0);
    double distance = price - floor;

    std::cout << "Test: equity=$" << equity << ", price=$" << price << ", floor=$" << floor << std::endl;

    double loss_per_lot = distance * contract_size;
    double margin_per_lot = contract_size * floor / leverage;
    double target_margin_level = margin_stop_out * safety_buffer;
    double cost_per_lot = loss_per_lot + margin_per_lot * target_margin_level / 100.0;
    double max_lots = equity / cost_per_lot;
    double lot = std::floor((max_lots * 0.05) / min_volume) * min_volume;

    std::cout << "  cost_per_lot=$" << cost_per_lot << ", max_lots=" << max_lots << std::endl;
    std::cout << "  initial_lot=" << lot << std::endl;
}

void RunFullComparison() {
    std::cout << "\n=== FULL BACKTEST COMPARISON ===" << std::endl;

    // ORIGINAL
    {
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

        ParallelDualStrategyOriginal::Config sc;
        sc.survive_pct = 10.0;
        sc.grid_allocation = 0.10;
        sc.momentum_allocation = 0.90;
        sc.min_volume = 0.01;
        sc.max_volume = 10.0;
        sc.contract_size = 100.0;
        sc.leverage = 500.0;
        sc.base_spacing = 1.50;
        sc.momentum_spacing = 5.0;
        sc.use_take_profit = true;
        sc.tp_distance = 5.0;

        ParallelDualStrategyOriginal strategy(sc);
        engine.RunWithTicks(g_ticks, [&](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto r = engine.GetResults();
        auto s = strategy.GetStats();
        double unreal = 0;
        for (const Trade* t : engine.GetOpenPositions())
            unreal += (g_ticks.back().bid - t->entry_price) * t->lot_size * 100.0;

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "\nORIGINAL (grid=10%, TP=$5): ";
        if (r.stop_out_occurred) { std::cout << "STOP-OUT" << std::endl; }
        else {
            std::cout << "Eq=$" << r.final_balance + unreal << " (" << (r.final_balance + unreal)/10000 << "x)";
            std::cout << " DD=" << r.max_drawdown_pct << "% Grid=" << s.grid_entries;
            std::cout << " Mom=" << s.momentum_entries << " Closed=" << engine.GetClosedTrades().size() << std::endl;
        }
    }

    // UNIFIED
    {
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

        ParallelDualStrategy::Config sc;
        sc.survive_pct = 15.0;
        sc.min_volume = 0.01;
        sc.max_volume = 10.0;
        sc.contract_size = 100.0;
        sc.leverage = 500.0;
        sc.min_spacing = 1.0;
        sc.base_spacing = 1.50;
        sc.margin_stop_out = 20.0;
        sc.safety_buffer = 1.5;

        ParallelDualStrategy strategy(sc);
        engine.RunWithTicks(g_ticks, [&](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto r = engine.GetResults();
        auto s = strategy.GetStats();
        double unreal = 0;
        for (const Trade* t : engine.GetOpenPositions())
            unreal += (g_ticks.back().bid - t->entry_price) * t->lot_size * 100.0;

        std::cout << "UNIFIED (survive=15%, no TP): ";
        if (r.stop_out_occurred) { std::cout << "STOP-OUT" << std::endl; }
        else {
            std::cout << "Eq=$" << r.final_balance + unreal << " (" << (r.final_balance + unreal)/10000 << "x)";
            std::cout << " DD=" << r.max_drawdown_pct << "% Up=" << s.upward_entries;
            std::cout << " Down=" << s.downward_entries << std::endl;
        }
    }
}

int main() {
    std::cout << "=== C++ vs MT5 Comparison ===" << std::endl;
    ValidateOriginalLotSizing();
    ValidateUnifiedLotSizing();
    LoadTickData();
    if (!g_ticks.empty()) RunFullComparison();
    return 0;
}
