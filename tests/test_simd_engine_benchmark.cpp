/**
 * SIMD Engine Benchmark
 * Tests the performance improvement of SIMD-optimized tick_based_engine
 *
 * Build: g++ -O3 -march=native -mavx512f -mavx2 -mfma -std=c++17 -I include tests/test_simd_engine_benchmark.cpp -o test_simd_engine_benchmark.exe
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <chrono>
#include <iomanip>

using namespace backtest;
using namespace std::chrono;

int main() {
    std::cout << "=== SIMD Engine Benchmark ===" << std::endl;
    std::cout << std::fixed << std::setprecision(2);

    // Initialize and print SIMD capabilities
    simd::init();
    simd::print_cpu_features();
    std::cout << "\n";

    // Configure tick data source
    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;

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
    config.start_date = "2024.12.31";  // Data starts at 2024.12.31
    config.end_date = "2025.01.15";  // Short test period - ~2 weeks
    config.tick_data_config = tick_config;
    config.verbose = false;  // Disable verbose output for benchmark

    std::cout << "Test Configuration:" << std::endl;
    std::cout << "  Period: 2024.12.31 - 2025.01.15 (~2 weeks)" << std::endl;
    std::cout << "  Initial Balance: $" << config.initial_balance << std::endl;
    std::cout << "  Strategy: FillUpOscillation (ADAPTIVE_SPACING)" << std::endl;
    std::cout << "\n";

    // Create engine and strategy
    TickBasedEngine engine(config);
    FillUpOscillation strategy(
        13.0,   // survive_pct
        1.5,    // base_spacing
        0.01,   // min_volume
        10.0,   // max_volume
        100.0,  // contract_size
        500.0,  // leverage
        FillUpOscillation::ADAPTIVE_SPACING,
        0.1,    // antifragile_scale
        30.0,   // max_spacing_mult
        4.0     // lookback_hours
    );

    // Run benchmark
    std::cout << "Running backtest with SIMD optimizations..." << std::endl;

    auto start = high_resolution_clock::now();

    engine.Run([&strategy](const Tick& tick, TickBasedEngine& engine) {
        strategy.OnTick(tick, engine);
    });

    auto end = high_resolution_clock::now();
    double elapsed_ms = duration_cast<milliseconds>(end - start).count();

    auto results = engine.GetResults();

    std::cout << "\n=== Benchmark Results ===" << std::endl;
    std::cout << "Execution Time: " << elapsed_ms << " ms" << std::endl;
    std::cout << "Total Trades: " << results.total_trades << std::endl;
    std::cout << "\n";

    std::cout << "=== Trading Results ===" << std::endl;
    std::cout << "Final Balance: $" << results.final_balance << std::endl;
    std::cout << "Net Profit: $" << (results.final_balance - config.initial_balance) << std::endl;
    std::cout << "Total Trades: " << results.total_trades << std::endl;
    std::cout << "Max Drawdown: " << results.max_drawdown_pct << "%" << std::endl;

    return 0;
}
