#include "../include/strategy_triple_hybrid.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>

using namespace backtest;

void PrintResults(const std::string& name, const TickBasedEngine::BacktestResults& results,
                  double initial_balance) {
    double return_mult = results.final_balance / initial_balance;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << std::left << std::setw(40) << name
              << " $" << std::setw(12) << results.final_balance
              << " (" << std::setw(6) << return_mult << "x)"
              << " DD: " << std::setw(6) << results.max_drawdown_pct << "%"
              << " Trades: " << results.total_trades
              << std::endl;
}

int main() {
    std::cout << "=== Up While Up + Up While Down Test ===" << std::endl;
    std::cout << "Starting from 2025.10.17 (crash period)" << std::endl;
    std::cout << std::endl;

    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    double initial_balance = 10000.0;

    TickBacktestConfig base_config;
    base_config.symbol = "XAUUSD";
    base_config.initial_balance = initial_balance;
    base_config.account_currency = "USD";
    base_config.contract_size = 100.0;
    base_config.leverage = 500.0;
    base_config.margin_rate = 1.0;
    base_config.pip_size = 0.01;
    base_config.swap_long = -66.99;
    base_config.swap_short = 41.2;
    base_config.swap_mode = 1;
    base_config.swap_3days = 3;
    base_config.start_date = "2025.10.17";  // Start from crash period
    base_config.end_date = "2025.12.30";
    base_config.tick_data_config = tick_config;
    base_config.verbose = false;

    std::cout << "Testing period: 2025.10.17 - 2025.12.30" << std::endl;
    std::cout << std::endl;

    // Test 1: Up While Up only (no grid, no reversal)
    {
        TickBacktestConfig config = base_config;
        TickBasedEngine engine(config);

        TripleHybrid::Config cfg;
        cfg.survive_pct = 13.0;
        cfg.base_spacing = 1.5;
        cfg.enable_grid = false;        // NO GRID
        cfg.enable_trend_up = true;     // Up while up
        cfg.enable_reversal = false;    // No reversal
        cfg.trend_up_spacing_mult = 2.0;
        cfg.trend_up_tp_pct = 0.0;      // No TP - hold positions
        cfg.close_trend_on_reversal = false;

        TripleHybrid strategy(cfg);

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        auto stats = strategy.GetStats();
        PrintResults("1. Up While Up only (no TP)", results, initial_balance);
        std::cout << "   TrendUp entries: " << stats.trend_up_entries << std::endl;
    }

    // Test 2: Up While Down only (no grid, no trend up)
    {
        TickBacktestConfig config = base_config;
        TickBasedEngine engine(config);

        TripleHybrid::Config cfg;
        cfg.survive_pct = 13.0;
        cfg.base_spacing = 1.5;
        cfg.enable_grid = false;        // NO GRID
        cfg.enable_trend_up = false;    // No trend up
        cfg.enable_reversal = true;     // Up while down
        cfg.reversal_spacing_mult = 2.0;
        cfg.reversal_tp_pct = 0.0;      // No TP - hold through recovery

        TripleHybrid strategy(cfg);

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        auto stats = strategy.GetStats();
        PrintResults("2. Up While Down only (no TP)", results, initial_balance);
        std::cout << "   Reversal entries: " << stats.reversal_entries << std::endl;
    }

    // Test 3: Combined Up While Up + Up While Down (no grid)
    {
        TickBacktestConfig config = base_config;
        TickBasedEngine engine(config);

        TripleHybrid::Config cfg;
        cfg.survive_pct = 13.0;
        cfg.base_spacing = 1.5;
        cfg.enable_grid = false;        // NO GRID
        cfg.enable_trend_up = true;     // Up while up
        cfg.enable_reversal = true;     // Up while down
        cfg.trend_up_spacing_mult = 2.0;
        cfg.trend_up_tp_pct = 0.0;      // No TP
        cfg.reversal_spacing_mult = 2.0;
        cfg.reversal_tp_pct = 0.0;      // No TP
        cfg.close_trend_on_reversal = false;

        TripleHybrid strategy(cfg);

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        auto stats = strategy.GetStats();
        PrintResults("3. Up While Up + Up While Down", results, initial_balance);
        std::cout << "   TrendUp: " << stats.trend_up_entries
                  << " | Reversal: " << stats.reversal_entries << std::endl;
    }

    // Test 4: Combined with TPs
    {
        TickBacktestConfig config = base_config;
        TickBasedEngine engine(config);

        TripleHybrid::Config cfg;
        cfg.survive_pct = 13.0;
        cfg.base_spacing = 1.5;
        cfg.enable_grid = false;
        cfg.enable_trend_up = true;
        cfg.enable_reversal = true;
        cfg.trend_up_spacing_mult = 2.0;
        cfg.trend_up_tp_pct = 3.0;      // 3% TP
        cfg.reversal_spacing_mult = 2.0;
        cfg.reversal_tp_pct = 3.0;      // 3% TP
        cfg.close_trend_on_reversal = true;

        TripleHybrid strategy(cfg);

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        auto stats = strategy.GetStats();
        PrintResults("4. Combined with 3% TP", results, initial_balance);
        std::cout << "   TrendUp: " << stats.trend_up_entries
                  << " | Reversal: " << stats.reversal_entries << std::endl;
    }

    // Test 5: Tighter spacing (more entries)
    {
        TickBacktestConfig config = base_config;
        TickBasedEngine engine(config);

        TripleHybrid::Config cfg;
        cfg.survive_pct = 13.0;
        cfg.base_spacing = 1.5;
        cfg.enable_grid = false;
        cfg.enable_trend_up = true;
        cfg.enable_reversal = true;
        cfg.trend_up_spacing_mult = 1.0;  // Same as base spacing
        cfg.trend_up_tp_pct = 0.0;
        cfg.reversal_spacing_mult = 1.0;
        cfg.reversal_tp_pct = 0.0;
        cfg.close_trend_on_reversal = false;

        TripleHybrid strategy(cfg);

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        auto stats = strategy.GetStats();
        PrintResults("5. Tight spacing (1x mult)", results, initial_balance);
        std::cout << "   TrendUp: " << stats.trend_up_entries
                  << " | Reversal: " << stats.reversal_entries << std::endl;
    }

    // Test 6: Very wide spacing (conservative)
    {
        TickBacktestConfig config = base_config;
        TickBasedEngine engine(config);

        TripleHybrid::Config cfg;
        cfg.survive_pct = 13.0;
        cfg.base_spacing = 1.5;
        cfg.enable_grid = false;
        cfg.enable_trend_up = true;
        cfg.enable_reversal = true;
        cfg.trend_up_spacing_mult = 5.0;  // Very wide
        cfg.trend_up_tp_pct = 0.0;
        cfg.reversal_spacing_mult = 5.0;
        cfg.reversal_tp_pct = 0.0;
        cfg.close_trend_on_reversal = false;

        TripleHybrid strategy(cfg);

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        auto stats = strategy.GetStats();
        PrintResults("6. Wide spacing (5x mult)", results, initial_balance);
        std::cout << "   TrendUp: " << stats.trend_up_entries
                  << " | Reversal: " << stats.reversal_entries << std::endl;
    }

    std::cout << std::endl;
    std::cout << "=== Analysis ===" << std::endl;
    std::cout << "Up While Up: Opens BUY during uptrends, holds" << std::endl;
    std::cout << "Up While Down: Opens BUY during downtrends, holds through recovery" << std::endl;

    return 0;
}
