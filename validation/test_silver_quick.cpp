#include "../include/strategy_parallel_dual.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>

using namespace backtest;

int main() {
    std::cout << "=== XAGUSD Quick Test ===" << std::endl;

    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\XAGUSD\\XAGUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;

    std::vector<Tick> ticks;
    TickDataManager manager(tick_config);
    manager.Reset();
    Tick tick;
    while (manager.GetNextTick(tick)) ticks.push_back(tick);
    std::cout << "Loaded " << ticks.size() << " ticks" << std::endl;
    std::cout << "Price: $" << ticks.front().bid << " -> $" << ticks.back().bid << std::endl;

    // Best config from previous sweep
    TickBacktestConfig config;
    config.symbol = "XAGUSD";
    config.initial_balance = 10000.0;
    config.contract_size = 5000.0;
    config.leverage = 500.0;
    config.pip_size = 0.001;
    config.swap_long = -15.0;
    config.swap_short = 13.72;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = "2025.01.01";
    config.end_date = "2025.12.30";
    config.verbose = false;

    TickBasedEngine engine(config);

    double ref_price = 50.0;
    double min_spacing = ref_price * 0.15 / 100.0;  // 0.15%

    ParallelDualStrategy::Config sc;
    sc.survive_pct = 18.0;
    sc.min_volume = 0.01;
    sc.max_volume = 10.0;
    sc.contract_size = 5000.0;
    sc.leverage = 500.0;
    sc.min_spacing = min_spacing;
    sc.max_spacing = min_spacing * 10.0;
    sc.base_spacing = min_spacing * 2.0;
    sc.target_trades_in_range = 20;
    sc.margin_stop_out = 20.0;
    sc.safety_buffer = 1.5;

    ParallelDualStrategy strategy(sc);

    std::cout << "Running backtest..." << std::endl;
    engine.RunWithTicks(ticks, [&](const Tick& t, TickBasedEngine& eng) {
        strategy.OnTick(t, eng);
    });

    auto r = engine.GetResults();
    auto s = strategy.GetStats();

    double unreal = 0;
    for (const Trade* t : engine.GetOpenPositions())
        unreal += (ticks.back().bid - t->entry_price) * t->lot_size * 5000.0;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n=== RESULTS ===" << std::endl;
    std::cout << "Balance: $" << r.final_balance << std::endl;
    std::cout << "Unrealized: $" << unreal << std::endl;
    std::cout << "Equity: $" << r.final_balance + unreal << std::endl;
    std::cout << "Return: " << (r.final_balance + unreal) / 10000.0 << "x" << std::endl;
    std::cout << "Max DD: " << r.max_drawdown_pct << "%" << std::endl;
    std::cout << "Upward entries: " << s.upward_entries << std::endl;
    std::cout << "Downward entries: " << s.downward_entries << std::endl;
    std::cout << "Total entries: " << s.total_entries << std::endl;
    if (r.stop_out_occurred) std::cout << "*** STOP-OUT ***" << std::endl;

    return 0;
}
