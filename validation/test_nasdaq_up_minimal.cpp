/**
 * Minimal NasdaqUp Strategy Test
 *
 * Tests basic functionality of the NasdaqUp strategy using XAUUSD tick data.
 * XAUUSD has upward periods suitable for validating the strategy mechanics.
 */
#include "../include/strategy_nasdaq_up.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>

using namespace backtest;

int main() {
    std::cout << "=== NasdaqUp Strategy Minimal Test ===" << std::endl;
    std::cout << std::fixed << std::setprecision(2);

    // Step 1: Configure tick data
    std::cout << "Step 1: Creating configs..." << std::endl;

    TickDataConfig tick_config;
    tick_config.file_path = "C:/Users/user/Documents/ctrader-backtest/validation/Grid/XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;

    // Step 2: Configure engine for XAUUSD
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
    config.end_date = "2025.01.31";  // 1 month for quick test
    config.tick_data_config = tick_config;
    config.verbose = false;
    std::cout << "  Done." << std::endl;

    // Step 3: Create engine
    std::cout << "Step 2: Creating engine..." << std::flush;
    TickBasedEngine engine(config);
    std::cout << " Done!" << std::endl;

    // Step 4: Create strategy with baseline config
    std::cout << "Step 3: Creating NasdaqUp strategy (baseline)..." << std::flush;
    NasdaqUp::Config strat_config = NasdaqUp::Config::XAUUSD();
    strat_config.verbose = false;  // Set to true for debugging
    NasdaqUp strategy(strat_config);
    std::cout << " Done!" << std::endl;

    std::cout << "  multiplier = " << strat_config.multiplier << std::endl;
    std::cout << "  power = " << strat_config.power << std::endl;
    std::cout << "  stop_out_margin = " << strat_config.stop_out_margin << "%" << std::endl;

    // Step 5: Run backtest
    std::cout << "Step 4: Running backtest..." << std::endl;
    int tick_count = 0;
    engine.Run([&strategy, &tick_count](const Tick& tick, TickBasedEngine& eng) {
        tick_count++;
        strategy.OnTick(tick, eng);
        if (tick_count % 50000 == 0) {
            std::cout << "  Processed " << tick_count << " ticks, positions: "
                      << eng.GetOpenPositions().size()
                      << ", equity: $" << eng.GetEquity() << "\r" << std::flush;
        }
    });
    std::cout << "\n  Total ticks: " << tick_count << std::endl;

    // Step 6: Get results
    std::cout << "Step 5: Getting results..." << std::flush;
    auto results = engine.GetResults();
    auto stats = strategy.GetStats();
    std::cout << " Done!" << std::endl;

    // Print results
    std::cout << "\n=== Results ===" << std::endl;
    std::cout << "Initial Balance:  $" << results.initial_balance << std::endl;
    std::cout << "Final Balance:    $" << results.final_balance << std::endl;
    std::cout << "Profit/Loss:      $" << results.total_profit_loss << std::endl;
    std::cout << "Return:           " << (results.final_balance / results.initial_balance) << "x" << std::endl;
    std::cout << "Max Drawdown:     " << results.max_drawdown_pct << "%" << std::endl;
    std::cout << "Total Trades:     " << results.total_trades << std::endl;
    std::cout << "Win Rate:         " << results.win_rate << "%" << std::endl;

    std::cout << "\n=== Strategy Stats ===" << std::endl;
    std::cout << "Total Entries:    " << stats.total_entries << std::endl;
    std::cout << "Stop-outs:        " << stats.stop_outs << std::endl;
    std::cout << "Cycles:           " << stats.cycles << std::endl;
    std::cout << "Peak Volume:      " << stats.peak_volume << " lots" << std::endl;
    std::cout << "Max Room:         " << stats.max_room_seen << std::endl;
    std::cout << "Min Room:         " << (stats.min_room_seen == DBL_MAX ? 0 : stats.min_room_seen) << std::endl;
    std::cout << "Final Room:       " << stats.final_room << std::endl;
    std::cout << "Peak Equity:      $" << stats.max_equity << std::endl;

    // Test different configurations
    std::cout << "\n=== Testing Different Powers ===" << std::endl;

    double test_powers[] = {-1.0, -0.5, 0.0, 0.1, 0.5, 1.0};
    for (double power : test_powers) {
        // Create fresh engine
        TickBasedEngine test_engine(config);

        // Create strategy with different power
        NasdaqUp::Config test_config = NasdaqUp::Config::XAUUSD();
        test_config.power = power;
        NasdaqUp test_strategy(test_config);

        // Run
        test_engine.Run([&test_strategy](const Tick& tick, TickBasedEngine& eng) {
            test_strategy.OnTick(tick, eng);
        });

        auto test_results = test_engine.GetResults();
        auto test_stats = test_strategy.GetStats();

        std::cout << "  power=" << std::setw(5) << power
                  << " -> Return: " << std::setw(8) << (test_results.final_balance / test_results.initial_balance) << "x"
                  << ", DD: " << std::setw(6) << test_results.max_drawdown_pct << "%"
                  << ", Entries: " << std::setw(5) << test_stats.total_entries
                  << ", StopOuts: " << test_stats.stop_outs
                  << std::endl;
    }

    // Test with different multipliers (more conservative)
    std::cout << "\n=== Testing Different Multipliers (more conservative) ===" << std::endl;

    double test_multipliers[] = {0.5, 1.0, 2.0, 5.0, 10.0, 20.0};
    for (double mult : test_multipliers) {
        TickBasedEngine test_engine(config);

        NasdaqUp::Config test_config = NasdaqUp::Config::XAUUSD();
        test_config.multiplier = mult;
        test_config.power = 0.1;  // Baseline power
        NasdaqUp test_strategy(test_config);

        test_engine.Run([&test_strategy](const Tick& tick, TickBasedEngine& eng) {
            test_strategy.OnTick(tick, eng);
        });

        auto test_results = test_engine.GetResults();
        auto test_stats = test_strategy.GetStats();

        std::cout << "  mult=" << std::setw(5) << mult
                  << " -> Return: " << std::setw(8) << (test_results.final_balance / test_results.initial_balance) << "x"
                  << ", DD: " << std::setw(6) << test_results.max_drawdown_pct << "%"
                  << ", Entries: " << std::setw(5) << test_stats.total_entries
                  << ", StopOuts: " << test_stats.stop_outs
                  << ", PeakVol: " << test_stats.peak_volume
                  << std::endl;
    }

    std::cout << "\n*** TEST COMPLETED ***" << std::endl;
    return 0;
}
