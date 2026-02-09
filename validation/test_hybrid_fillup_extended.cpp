#include "../include/strategy_hybrid_fillup_extended.h"
#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>

using namespace backtest;

void PrintResults(const std::string& name, const TickBasedEngine::BacktestResults& results, double initial_balance) {
    double return_mult = results.final_balance / initial_balance;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << name << ": "
              << "$" << results.final_balance << " (" << return_mult << "x) | "
              << "DD: " << results.max_drawdown_pct << "% | "
              << "Trades: " << results.total_trades << std::endl;
}

int main() {
    std::cout << "=== Hybrid FillUp Extended Test ===" << std::endl;
    std::cout << "Testing concept: Capture oscillations when normal margin exhausted" << std::endl;
    std::cout << std::endl;

    // Config for tick data
    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    double initial_balance = 10000.0;

    // Base engine config
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
    base_config.start_date = "2025.01.01";
    base_config.end_date = "2025.12.30";
    base_config.tick_data_config = tick_config;

    std::cout << "Running backtests..." << std::endl << std::endl;

    // Test 1: Baseline FillUpOscillation (for comparison)
    {
        TickBacktestConfig config = base_config;
        TickBasedEngine engine(config);

        FillUpOscillation strategy(13.0, 1.5, 0.01, 10.0, 100.0, 500.0,
                                   FillUpOscillation::ADAPTIVE_SPACING,
                                   0.1, 30.0, 4.0);

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        PrintResults("Baseline FillUpOscillation", results, initial_balance);
    }

    // Test 2: Hybrid with extended mode
    {
        TickBacktestConfig config = base_config;
        TickBasedEngine engine(config);

        HybridFillUpExtended::Config hybrid_cfg;
        hybrid_cfg.survive_pct = 13.0;
        hybrid_cfg.base_spacing = 1.5;
        hybrid_cfg.min_volume = 0.01;
        hybrid_cfg.max_volume = 10.0;
        hybrid_cfg.contract_size = 100.0;
        hybrid_cfg.leverage = 500.0;
        hybrid_cfg.extended_grid_levels = 3;
        hybrid_cfg.extended_spacing_pct = 0.5;
        hybrid_cfg.close_extended_on_reversal = true;
        hybrid_cfg.volatility_lookback_hours = 4.0;
        hybrid_cfg.typical_vol_pct = 0.55;

        HybridFillUpExtended strategy(hybrid_cfg);

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        auto stats = strategy.GetStats();

        PrintResults("Hybrid (extended_levels=3)", results, initial_balance);
        std::cout << "  Extended stats: entries=" << stats.extended_entries
                  << ", exits=" << stats.extended_exits
                  << ", mode_switches=" << stats.mode_switches_to_extended
                  << ", max_ext_pos=" << stats.max_extended_positions
                  << std::endl;
        std::cout << "  Extended profit: $" << stats.total_extended_profit
                  << " vs Normal profit: $" << stats.total_normal_profit
                  << std::endl;
    }

    // Test 3: Hybrid with more extended levels
    {
        TickBacktestConfig config = base_config;
        TickBasedEngine engine(config);

        HybridFillUpExtended::Config hybrid_cfg;
        hybrid_cfg.survive_pct = 13.0;
        hybrid_cfg.base_spacing = 1.5;
        hybrid_cfg.min_volume = 0.01;
        hybrid_cfg.max_volume = 10.0;
        hybrid_cfg.contract_size = 100.0;
        hybrid_cfg.leverage = 500.0;
        hybrid_cfg.extended_grid_levels = 5;  // More levels
        hybrid_cfg.extended_spacing_pct = 0.5;
        hybrid_cfg.close_extended_on_reversal = true;
        hybrid_cfg.volatility_lookback_hours = 4.0;
        hybrid_cfg.typical_vol_pct = 0.55;

        HybridFillUpExtended strategy(hybrid_cfg);

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        auto stats = strategy.GetStats();

        PrintResults("Hybrid (extended_levels=5)", results, initial_balance);
        std::cout << "  Extended stats: entries=" << stats.extended_entries
                  << ", exits=" << stats.extended_exits
                  << ", mode_switches=" << stats.mode_switches_to_extended
                  << ", max_ext_pos=" << stats.max_extended_positions
                  << std::endl;
    }

    // Test 4: Hybrid with no extended closing on reversal
    {
        TickBacktestConfig config = base_config;
        TickBasedEngine engine(config);

        HybridFillUpExtended::Config hybrid_cfg;
        hybrid_cfg.survive_pct = 13.0;
        hybrid_cfg.base_spacing = 1.5;
        hybrid_cfg.min_volume = 0.01;
        hybrid_cfg.max_volume = 10.0;
        hybrid_cfg.contract_size = 100.0;
        hybrid_cfg.leverage = 500.0;
        hybrid_cfg.extended_grid_levels = 3;
        hybrid_cfg.extended_spacing_pct = 0.5;
        hybrid_cfg.close_extended_on_reversal = false;  // Don't close on reversal
        hybrid_cfg.volatility_lookback_hours = 4.0;
        hybrid_cfg.typical_vol_pct = 0.55;

        HybridFillUpExtended strategy(hybrid_cfg);

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        auto stats = strategy.GetStats();

        PrintResults("Hybrid (no_reversal_close)", results, initial_balance);
        std::cout << "  Extended stats: entries=" << stats.extended_entries
                  << ", exits=" << stats.extended_exits
                  << ", mode_switches=" << stats.mode_switches_to_extended
                  << std::endl;
    }

    std::cout << std::endl;
    std::cout << "=== Analysis ===" << std::endl;
    std::cout << "The hybrid strategy captures oscillations during periods when" << std::endl;
    std::cout << "normal FillUp would be waiting (margin exhausted)." << std::endl;
    std::cout << std::endl;
    std::cout << "Extended mode characteristics:" << std::endl;
    std::cout << "- Activates when normal lot_sizing() returns 0" << std::endl;
    std::cout << "- Uses wider spacing based on remaining survive distance" << std::endl;
    std::cout << "- Uses minimum lot size (0.01) to minimize margin impact" << std::endl;
    std::cout << "- Optional: closes extended positions on direction reversal" << std::endl;

    return 0;
}
