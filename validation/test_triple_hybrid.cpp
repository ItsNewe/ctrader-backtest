#include "../include/strategy_triple_hybrid.h"
#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>

using namespace backtest;

void PrintResults(const std::string& name, const TickBasedEngine::BacktestResults& results,
                  double initial_balance) {
    double return_mult = results.final_balance / initial_balance;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << std::left << std::setw(35) << name
              << " $" << std::setw(12) << results.final_balance
              << " (" << std::setw(6) << return_mult << "x)"
              << " DD: " << std::setw(6) << results.max_drawdown_pct << "%"
              << " Trades: " << results.total_trades
              << std::endl;
}

int main() {
    std::cout << "=== Triple Hybrid Strategy Test ===" << std::endl;
    std::cout << "Combining: FillUp Grid + TrendUp + Reversal" << std::endl;
    std::cout << std::endl;

    // Config
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
    base_config.start_date = "2025.01.01";
    base_config.end_date = "2025.12.30";
    base_config.tick_data_config = tick_config;
    base_config.verbose = false;

    std::cout << "Running backtests..." << std::endl << std::endl;

    // Test 1: Baseline FillUpOscillation
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
        PrintResults("1. BASELINE FillUpOscillation", results, initial_balance);
    }

    // Test 2: Grid only (should match baseline roughly)
    {
        TickBacktestConfig config = base_config;
        TickBasedEngine engine(config);

        TripleHybrid::Config hybrid_cfg;
        hybrid_cfg.survive_pct = 13.0;
        hybrid_cfg.base_spacing = 1.5;
        hybrid_cfg.enable_grid = true;
        hybrid_cfg.enable_trend_up = false;
        hybrid_cfg.enable_reversal = false;

        TripleHybrid strategy(hybrid_cfg);

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        auto stats = strategy.GetStats();
        PrintResults("2. TripleHybrid (Grid only)", results, initial_balance);
        std::cout << "   Grid entries: " << stats.grid_entries << std::endl;
    }

    // Test 3: Grid + TrendUp with 5% TP
    {
        TickBacktestConfig config = base_config;
        TickBasedEngine engine(config);

        TripleHybrid::Config hybrid_cfg;
        hybrid_cfg.survive_pct = 13.0;
        hybrid_cfg.base_spacing = 1.5;
        hybrid_cfg.enable_grid = true;
        hybrid_cfg.enable_trend_up = true;
        hybrid_cfg.enable_reversal = false;
        hybrid_cfg.trend_up_spacing_mult = 2.0;
        hybrid_cfg.trend_up_tp_pct = 5.0;  // 5% TP
        hybrid_cfg.close_trend_on_reversal = true;

        TripleHybrid strategy(hybrid_cfg);

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        auto stats = strategy.GetStats();
        PrintResults("3. Grid + TrendUp (5% TP)", results, initial_balance);
        std::cout << "   Grid: " << stats.grid_entries << " | TrendUp: " << stats.trend_up_entries
                  << " | Dirs: " << stats.direction_changes << std::endl;
    }

    // Test 4: Grid + Reversal with 5% TP
    {
        TickBacktestConfig config = base_config;
        TickBasedEngine engine(config);

        TripleHybrid::Config hybrid_cfg;
        hybrid_cfg.survive_pct = 13.0;
        hybrid_cfg.base_spacing = 1.5;
        hybrid_cfg.enable_grid = true;
        hybrid_cfg.enable_trend_up = false;
        hybrid_cfg.enable_reversal = true;
        hybrid_cfg.reversal_spacing_mult = 2.0;
        hybrid_cfg.reversal_tp_pct = 5.0;  // 5% TP

        TripleHybrid strategy(hybrid_cfg);

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        auto stats = strategy.GetStats();
        PrintResults("4. Grid + Reversal (5% TP)", results, initial_balance);
        std::cout << "   Grid: " << stats.grid_entries << " | Reversal: " << stats.reversal_entries
                  << " | Dirs: " << stats.direction_changes << std::endl;
    }

    // Test 5: FULL Triple Hybrid (all three with TPs)
    {
        TickBacktestConfig config = base_config;
        TickBasedEngine engine(config);

        TripleHybrid::Config hybrid_cfg;
        hybrid_cfg.survive_pct = 13.0;
        hybrid_cfg.base_spacing = 1.5;
        hybrid_cfg.enable_grid = true;
        hybrid_cfg.enable_trend_up = true;
        hybrid_cfg.enable_reversal = true;
        hybrid_cfg.trend_up_spacing_mult = 2.0;
        hybrid_cfg.trend_up_tp_pct = 5.0;
        hybrid_cfg.reversal_spacing_mult = 2.0;
        hybrid_cfg.reversal_tp_pct = 5.0;
        hybrid_cfg.close_trend_on_reversal = true;

        TripleHybrid strategy(hybrid_cfg);

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        auto stats = strategy.GetStats();
        PrintResults("5. FULL Triple (all with TP)", results, initial_balance);
        std::cout << "   Grid: " << stats.grid_entries
                  << " | TrendUp: " << stats.trend_up_entries
                  << " | Reversal: " << stats.reversal_entries
                  << " | Dirs: " << stats.direction_changes << std::endl;
    }

    // Test 6: Triple Hybrid with reversal TP
    {
        TickBacktestConfig config = base_config;
        TickBasedEngine engine(config);

        TripleHybrid::Config hybrid_cfg;
        hybrid_cfg.survive_pct = 13.0;
        hybrid_cfg.base_spacing = 1.5;
        hybrid_cfg.enable_grid = true;
        hybrid_cfg.enable_trend_up = true;
        hybrid_cfg.enable_reversal = true;
        hybrid_cfg.trend_up_spacing_mult = 2.0;
        hybrid_cfg.reversal_spacing_mult = 2.0;
        hybrid_cfg.close_trend_on_reversal = true;  // Close trend on reversal
        hybrid_cfg.reversal_tp_pct = 2.0;           // 2% TP on reversal positions

        TripleHybrid strategy(hybrid_cfg);

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        auto stats = strategy.GetStats();
        PrintResults("6. Triple + TP + CloseOnReversal", results, initial_balance);
        std::cout << "   Grid: " << stats.grid_entries
                  << " | TrendUp: " << stats.trend_up_entries
                  << " | Reversal: " << stats.reversal_entries << std::endl;
    }

    // Test 7: Conservative survive with triple hybrid
    {
        TickBacktestConfig config = base_config;
        TickBasedEngine engine(config);

        TripleHybrid::Config hybrid_cfg;
        hybrid_cfg.survive_pct = 15.0;  // More conservative
        hybrid_cfg.base_spacing = 1.5;
        hybrid_cfg.enable_grid = true;
        hybrid_cfg.enable_trend_up = true;
        hybrid_cfg.enable_reversal = true;
        hybrid_cfg.trend_up_spacing_mult = 3.0;  // Wider
        hybrid_cfg.reversal_spacing_mult = 3.0;
        hybrid_cfg.close_trend_on_reversal = false;
        hybrid_cfg.reversal_tp_pct = 0.0;

        TripleHybrid strategy(hybrid_cfg);

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        auto stats = strategy.GetStats();
        PrintResults("7. Triple Conservative (s15 sp3x)", results, initial_balance);
        std::cout << "   Grid: " << stats.grid_entries
                  << " | TrendUp: " << stats.trend_up_entries
                  << " | Reversal: " << stats.reversal_entries << std::endl;
    }

    std::cout << std::endl;
    std::cout << "=== Analysis ===" << std::endl;
    std::cout << "Grid: FillUp-style oscillation capture with TP" << std::endl;
    std::cout << "TrendUp: Opens BUY during uptrends, no TP (rides trend)" << std::endl;
    std::cout << "Reversal: Opens BUY during downtrends, no TP (rides recovery)" << std::endl;

    return 0;
}
