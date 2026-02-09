#include "../include/strategy_adaptive_regime_grid.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>

using namespace backtest;

int main() {
    std::cout << "=== AdaptiveRegimeGrid Strategy Backtest ===" << std::endl;
    std::cout << "Symbol: XAUUSD | Period: 2025.01.01 - 2025.12.30" << std::endl;
    std::cout << std::string(60, '=') << std::endl;

    // Configure tick data
    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;

    // Configure engine (XAUUSD Grid broker settings)
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
    config.tick_data_config = tick_config;

    // Create strategy with default XAUUSD config
    AdaptiveRegimeGrid::Config strat_cfg = AdaptiveRegimeGrid::Config::XAUUSD_Default();
    AdaptiveRegimeGrid strategy(strat_cfg);

    // Create engine and run
    TickBasedEngine engine(config);
    engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    // Get results
    auto results = engine.GetResults();
    const auto& stats = strategy.GetStats();

    // Print results
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n--- Performance ---" << std::endl;
    std::cout << "Initial Balance:   $" << results.initial_balance << std::endl;
    std::cout << "Final Balance:     $" << results.final_balance << std::endl;
    std::cout << "Return Multiple:   " << (results.final_balance / results.initial_balance) << "x" << std::endl;
    std::cout << "Total P/L:         $" << results.total_profit_loss << std::endl;
    std::cout << "Max Drawdown:      " << results.max_drawdown_pct << "%" << std::endl;
    std::cout << "Risk-Adj Return:   " << (results.final_balance / results.initial_balance) / (results.max_drawdown_pct / 100.0 + 0.01) << std::endl;

    std::cout << "\n--- Trade Statistics ---" << std::endl;
    std::cout << "Total Trades:      " << results.total_trades << std::endl;
    std::cout << "Win Rate:          " << results.win_rate << "%" << std::endl;
    std::cout << "Profit Factor:     " << results.profit_factor << std::endl;
    std::cout << "Avg Win:           $" << results.average_win << std::endl;
    std::cout << "Avg Loss:          $" << results.average_loss << std::endl;
    std::cout << "Max Positions:     " << stats.max_position_count << std::endl;

    std::cout << "\n--- Strategy Stats ---" << std::endl;
    std::cout << "Entries Allowed:   " << stats.entries_allowed << std::endl;
    std::cout << "Velocity Blocks:   " << stats.velocity_blocks << std::endl;
    std::cout << "Lot Size 0 Blocks: " << stats.lot_size_zero_blocks << std::endl;
    std::cout << "Regime Changes:    " << stats.regime_changes << std::endl;
    std::cout << "Trending Entries:  " << stats.trending_entries << std::endl;
    std::cout << "Ranging Entries:   " << stats.ranging_entries << std::endl;
    std::cout << "Trailing TP:       " << stats.trailing_tp_entries << std::endl;
    std::cout << "Fixed TP:          " << stats.fixed_tp_entries << std::endl;
    std::cout << "Peak DD%:          " << stats.peak_dd_pct << "%" << std::endl;
    std::cout << "DD Halt Ticks:     " << stats.dd_halt_ticks << std::endl;
    std::cout << "DD Liquidations:   " << stats.dd_liquidations << std::endl;
    std::cout << "Pos Liquidated:    " << stats.positions_liquidated << std::endl;
    std::cout << "DD Reduce Ticks:   " << stats.dd_reduce_active_ticks << std::endl;

    if (results.stop_out_occurred) {
        std::cout << "\n*** STOP-OUT OCCURRED ***" << std::endl;
    }

    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "Backtest complete." << std::endl;

    return 0;
}
