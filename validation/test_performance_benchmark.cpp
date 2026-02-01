/**
 * Performance Benchmark Test
 *
 * Tests the tick-based engine performance after optimizations:
 * - Pre-allocated price buffers in CheckMarginStopOut
 * - Capped equity curve growth
 * - Optimized date comparison (no substr allocations)
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <chrono>

using namespace backtest;

int main() {
    try {
    std::cout << "=== Performance Benchmark Test ===" << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    std::cerr << "Starting benchmark..." << std::endl;

    // Configure tick data source - use absolute path
    TickDataConfig tick_config;
    tick_config.file_path = "C:/Users/user/Documents/ctrader-backtest/validation/Grid/XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;  // Streaming mode

    // Configure backtest
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

    // Run for 3 months to get meaningful benchmark
    config.start_date = "2025.01.01";
    config.end_date = "2025.03.31";

    // Test with equity tracking enabled (uses our optimizations)
    config.track_equity_curve = true;
    config.equity_sample_interval = 10000;  // Sample every 10k ticks
    config.max_equity_samples = 10000;      // Cap at 10k samples

    config.tick_data_config = tick_config;

    std::cout << "\nConfiguration:" << std::endl;
    std::cout << "Period: " << config.start_date << " - " << config.end_date << std::endl;
    std::cout << "Initial Balance: $" << config.initial_balance << std::endl;
    std::cout << "Equity tracking: ON (sampled every " << config.equity_sample_interval << " ticks)" << std::endl;
    std::cout << "Max equity samples: " << config.max_equity_samples << std::endl;

    // Strategy parameters (FillUpOscillation ADAPTIVE)
    FillUpOscillation::Config strat_config;
    strat_config.survive_pct = 13.0;
    strat_config.base_spacing = 1.5;
    strat_config.min_volume = 0.01;
    strat_config.max_volume = 10.0;
    strat_config.contract_size = 100.0;
    strat_config.leverage = 500.0;
    strat_config.mode = FillUpOscillation::ADAPTIVE_SPACING;
    strat_config.volatility_lookback_hours = 4.0;
    strat_config.adaptive.typical_vol_pct = 0.55;

    std::cout << "\nStrategy: FillUpOscillation ADAPTIVE" << std::endl;
    std::cout << "Survive %: " << strat_config.survive_pct << std::endl;
    std::cout << "Base spacing: $" << strat_config.base_spacing << std::endl;

    // Create engine and strategy
    TickBasedEngine engine(config);
    FillUpOscillation strategy(strat_config);

    // Time the backtest
    auto start_time = std::chrono::high_resolution_clock::now();

    // Run backtest
    size_t tick_count = 0;
    engine.Run([&strategy, &tick_count](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
        tick_count++;
    });

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    // Get results
    auto results = engine.GetResults();

    std::cout << "\n=== Results ===" << std::endl;
    std::cout << "Ticks processed: " << tick_count << std::endl;
    std::cout << "Time elapsed: " << duration.count() << " ms" << std::endl;

    if (duration.count() > 0) {
        double ticks_per_sec = (tick_count * 1000.0) / duration.count();
        std::cout << "Throughput: " << std::fixed << std::setprecision(0) << ticks_per_sec << " ticks/sec" << std::endl;
    }

    std::cout << "\n=== Trading Results ===" << std::endl;
    std::cout << "Final Balance: $" << results.final_balance << std::endl;
    std::cout << "Total Return: " << ((results.final_balance / config.initial_balance) - 1) * 100 << "%" << std::endl;
    std::cout << "Max Drawdown: " << results.max_drawdown_pct << "%" << std::endl;
    std::cout << "Total Trades: " << results.total_trades << std::endl;
    std::cout << "Winning Trades: " << results.winning_trades << std::endl;
    std::cout << "Equity Samples: " << results.equity_curve.size() << std::endl;

    // Verify results are reasonable
    bool passed = true;
    if (results.final_balance <= 0) {
        std::cout << "\n[FAIL] Final balance is zero or negative!" << std::endl;
        passed = false;
    }
    if (tick_count == 0) {
        std::cout << "\n[FAIL] No ticks processed!" << std::endl;
        passed = false;
    }
    if (results.equity_curve.size() > config.max_equity_samples) {
        std::cout << "\n[FAIL] Equity curve exceeded max samples!" << std::endl;
        passed = false;
    }

    if (passed) {
        std::cout << "\n[PASS] All verification checks passed!" << std::endl;
    }

    return passed ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown exception" << std::endl;
        return 1;
    }
}
