/**
 * test_monte_carlo.cpp
 *
 * Monte Carlo simulation to assess strategy robustness.
 *
 * A single backtest shows ONE path. Monte Carlo shows the DISTRIBUTION
 * of possible outcomes by randomizing:
 * - Trade order (are profits dependent on sequence?)
 * - Skipping trades (what if we miss some entries?)
 * - Slippage variation (what if execution is worse?)
 */

#include "../include/monte_carlo.h"
#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <chrono>

using namespace backtest;

int main() {
    std::cout << std::string(80, '=') << std::endl;
    std::cout << "MONTE CARLO SIMULATION TEST" << std::endl;
    std::cout << "Strategy: FillUpOscillation ADAPTIVE on XAUUSD 2025" << std::endl;
    std::cout << std::string(80, '=') << std::endl << std::endl;

    // Load 2025 tick data
    std::cout << "Loading tick data..." << std::endl;
    auto start_load = std::chrono::high_resolution_clock::now();

    TickDataConfig tick_cfg;
    tick_cfg.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    tick_cfg.format = TickDataFormat::MT5_CSV;

    TickDataManager mgr(tick_cfg);
    std::vector<Tick> ticks;
    Tick tick;
    while (mgr.GetNextTick(tick)) {
        ticks.push_back(tick);
    }

    auto end_load = std::chrono::high_resolution_clock::now();
    auto load_sec = std::chrono::duration_cast<std::chrono::seconds>(end_load - start_load).count();
    std::cout << "Loaded " << ticks.size() << " ticks in " << load_sec << "s" << std::endl << std::endl;

    // Run the original backtest to get trade list
    std::cout << "Running original backtest..." << std::endl;

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
    config.end_date = "2025.12.31";
    config.verbose = false;

    TickBasedEngine engine(config);

    // Use the documented best parameters from CLAUDE.md
    FillUpOscillation strategy(
        13.0,   // survive_pct
        1.5,    // base_spacing
        0.01,   // min_volume
        10.0,   // max_volume
        100.0,  // contract_size
        500.0,  // leverage
        FillUpOscillation::ADAPTIVE_SPACING,
        0.1,    // antifragile_scale (unused)
        30.0,   // velocity_threshold (unused)
        4.0     // volatility_lookback_hours
    );

    engine.RunWithTicks(ticks, [&strategy](const Tick& t, TickBasedEngine& eng) {
        strategy.OnTick(t, eng);
    });

    auto results = engine.GetResults();
    auto trades = engine.GetClosedTrades();

    std::cout << "Original backtest complete:" << std::endl;
    std::cout << "  Profit: $" << std::fixed << std::setprecision(2) << results.total_profit_loss << std::endl;
    std::cout << "  Trades: " << results.total_trades << std::endl;
    std::cout << "  Max DD: " << results.max_drawdown_pct << "%" << std::endl;
    std::cout << std::endl;

    // ============================================
    // Monte Carlo Test 1: Trade Shuffle
    // ============================================
    std::cout << std::string(80, '=') << std::endl;
    std::cout << "TEST 1: TRADE SEQUENCE SHUFFLE" << std::endl;
    std::cout << "Question: Is profit dependent on the order trades occurred?" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    MonteCarloConfig mc_config;
    mc_config.num_simulations = 1000;
    mc_config.mode = MonteCarloMode::SHUFFLE_TRADES;

    MonteCarloSimulator sim1(mc_config);
    auto shuffle_result = sim1.Run(trades, config.initial_balance, true);

    // ============================================
    // Monte Carlo Test 2: Skip Trades
    // ============================================
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "TEST 2: RANDOM TRADE SKIPPING (10%)" << std::endl;
    std::cout << "Question: What if we miss 10% of entries?" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    mc_config.mode = MonteCarloMode::SKIP_TRADES;
    mc_config.skip_probability = 0.10;

    MonteCarloSimulator sim2(mc_config);
    auto skip_result = sim2.Run(trades, config.initial_balance, true);

    // ============================================
    // Monte Carlo Test 3: Slippage Variation
    // ============================================
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "TEST 3: SLIPPAGE VARIATION (stddev=0.5 points)" << std::endl;
    std::cout << "Question: How sensitive is profit to execution quality?" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    mc_config.mode = MonteCarloMode::VARY_SLIPPAGE;
    mc_config.slippage_stddev_points = 0.5;

    MonteCarloSimulator sim3(mc_config);
    auto slippage_result = sim3.Run(trades, config.initial_balance, true);

    // ============================================
    // Monte Carlo Test 4: Bootstrap
    // ============================================
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "TEST 4: BOOTSTRAP SAMPLING" << std::endl;
    std::cout << "Question: How statistically significant are the results?" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    mc_config.mode = MonteCarloMode::BOOTSTRAP;
    mc_config.sample_ratio = 1.0;

    MonteCarloSimulator sim4(mc_config);
    auto bootstrap_result = sim4.Run(trades, config.initial_balance, true);

    // ============================================
    // Monte Carlo Test 5: Combined
    // ============================================
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "TEST 5: COMBINED EFFECTS (shuffle + slippage)" << std::endl;
    std::cout << "Question: Worst realistic scenario?" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    mc_config.mode = MonteCarloMode::COMBINED;
    mc_config.enable_shuffle = true;
    mc_config.enable_skip = false;
    mc_config.enable_slippage = true;
    mc_config.slippage_stddev_points = 0.3;

    MonteCarloSimulator sim5(mc_config);
    auto combined_result = sim5.Run(trades, config.initial_balance, true);

    // ============================================
    // Summary
    // ============================================
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "MONTE CARLO SUMMARY" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    std::cout << std::left << std::setw(25) << "Test"
              << std::right << std::setw(15) << "Original"
              << std::setw(15) << "Median"
              << std::setw(15) << "5th Pctl"
              << std::setw(12) << "Loss Prob"
              << std::setw(12) << "Confidence"
              << std::endl;
    std::cout << std::string(80, '-') << std::endl;

    auto print_row = [](const std::string& name, const MonteCarloResult& r) {
        std::cout << std::left << std::setw(25) << name
                  << std::right << std::fixed << std::setprecision(0)
                  << std::setw(14) << r.original_profit << "$"
                  << std::setw(14) << r.profit_median << "$"
                  << std::setw(14) << r.profit_5th_percentile << "$"
                  << std::setw(11) << std::setprecision(1) << r.probability_of_loss << "%"
                  << std::setw(12) << r.confidence_level
                  << std::endl;
    };

    print_row("Shuffle", shuffle_result);
    print_row("Skip 10%", skip_result);
    print_row("Slippage", slippage_result);
    print_row("Bootstrap", bootstrap_result);
    print_row("Combined", combined_result);

    // Overall assessment
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "OVERALL ASSESSMENT" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    double avg_confidence = (shuffle_result.confidence_score +
                             skip_result.confidence_score +
                             slippage_result.confidence_score +
                             bootstrap_result.confidence_score +
                             combined_result.confidence_score) / 5.0;

    std::cout << "Average confidence score: " << std::fixed << std::setprecision(0) << avg_confidence << "/100" << std::endl;

    if (avg_confidence >= 80) {
        std::cout << "\nVERDICT: HIGHLY ROBUST" << std::endl;
        std::cout << "Strategy shows consistent performance across all Monte Carlo tests." << std::endl;
        std::cout << "Results are statistically significant and not sequence-dependent." << std::endl;
    } else if (avg_confidence >= 60) {
        std::cout << "\nVERDICT: MODERATELY ROBUST" << std::endl;
        std::cout << "Strategy shows acceptable stability but some sensitivity detected." << std::endl;
        std::cout << "Consider using the 5th percentile profit as realistic expectation." << std::endl;
    } else {
        std::cout << "\nVERDICT: FRAGILE" << std::endl;
        std::cout << "Strategy shows high sensitivity to randomization." << std::endl;
        std::cout << "Live trading results may vary significantly from backtest." << std::endl;
    }

    std::cout << "\nKey insight: 5th percentile profit ($" << std::setprecision(0)
              << combined_result.profit_5th_percentile
              << ") is a realistic worst-case expectation." << std::endl;

    return 0;
}
