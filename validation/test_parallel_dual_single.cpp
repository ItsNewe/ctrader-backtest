/**
 * ParallelDual Original Strategy - Single test run for debugging
 */

#include "../include/strategy_parallel_dual_original.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <fstream>

using namespace backtest;

int main() {
    std::cout << "=== ParallelDual Single Test ===" << std::endl;

    // Load ticks
    std::cout << "Loading tick data..." << std::endl;
    std::vector<Tick> ticks;
    std::ifstream file("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv");
    if (!file.is_open()) {
        std::cerr << "Failed to open tick file" << std::endl;
        return 1;
    }

    std::string line;
    std::getline(file, line); // Skip header

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        Tick tick;
        size_t pos = 0;
        std::string date_str, time_str;

        // Date
        pos = line.find('\t');
        date_str = line.substr(0, pos);
        line = line.substr(pos + 1);

        // Time
        pos = line.find('\t');
        time_str = line.substr(0, pos);
        line = line.substr(pos + 1);

        // Bid
        pos = line.find('\t');
        tick.bid = std::stod(line.substr(0, pos));
        line = line.substr(pos + 1);

        // Ask
        pos = line.find('\t');
        tick.ask = std::stod(line.substr(0, pos));

        tick.timestamp = date_str + " " + time_str;
        ticks.push_back(tick);

        // Only load first 10000 ticks for debug
        if (ticks.size() >= 10000) break;
    }

    std::cout << "Loaded " << ticks.size() << " ticks" << std::endl;
    std::cout << "First tick: " << ticks[0].timestamp << " Bid=" << ticks[0].bid << " Ask=" << ticks[0].ask << std::endl;

    // Configure engine
    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = 10000.0;
    config.contract_size = 100.0;
    config.leverage = 500.0;
    config.pip_size = 0.01;
    config.swap_long = -78.57;
    config.swap_short = 39.14;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = "2024.12.31";
    config.end_date = "2026.01.30";
    config.verbose = true;  // Enable logging

    TickBasedEngine engine(config);

    // Configure strategy
    ParallelDualStrategyOriginal::Config strat_config;
    strat_config.survive_pct = 19.0;
    strat_config.grid_allocation = 0.15;
    strat_config.momentum_allocation = 0.85;
    strat_config.min_volume = 0.01;
    strat_config.max_volume = 100.0;
    strat_config.contract_size = 100.0;
    strat_config.leverage = 500.0;
    strat_config.base_spacing = 0.50;
    strat_config.momentum_spacing = 2.0;
    strat_config.margin_stop_out = 20.0;
    strat_config.force_min_volume_entry = false;
    strat_config.use_take_profit = true;
    strat_config.tp_distance = 6.0;

    ParallelDualStrategyOriginal strategy(strat_config);

    std::cout << "\n=== Starting Backtest ===" << std::endl;
    std::cout << "Survive: " << strat_config.survive_pct << "%" << std::endl;
    std::cout << "Grid Alloc: " << strat_config.grid_allocation << std::endl;
    std::cout << "Base Spacing: $" << strat_config.base_spacing << std::endl;
    std::cout << "TP Distance: $" << strat_config.tp_distance << std::endl;
    std::cout << "Max Volume: " << strat_config.max_volume << " lots" << std::endl;

    // Run backtest
    engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();
    auto stats = strategy.GetStats();

    std::cout << "\n=== RESULTS ===" << std::endl;
    std::cout << "Final Balance: $" << results.final_balance << std::endl;
    std::cout << "Return: " << (results.final_balance / 10000.0) << "x" << std::endl;
    std::cout << "Max DD: " << results.max_drawdown_pct << "%" << std::endl;
    std::cout << "Total Entries: " << stats.total_entries << std::endl;
    std::cout << "Grid Entries: " << stats.grid_entries << std::endl;
    std::cout << "Momentum Entries: " << stats.momentum_entries << std::endl;

    return 0;
}
