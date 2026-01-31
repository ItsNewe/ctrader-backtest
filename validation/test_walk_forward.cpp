/**
 * test_walk_forward.cpp
 *
 * Validates Walk-Forward Optimization on XAUUSD with FillUpOscillation strategy.
 *
 * Walk-forward prevents overfitting by:
 * 1. Optimizing on IS (in-sample) window
 * 2. Testing on OOS (out-of-sample) window
 * 3. Rolling forward through time
 * 4. Combining OOS results for true performance estimate
 */

#include "../include/walk_forward.h"
#include "../include/fill_up_oscillation.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <chrono>

using namespace backtest;

int main() {
    std::cout << std::string(80, '=') << std::endl;
    std::cout << "WALK-FORWARD OPTIMIZATION TEST" << std::endl;
    std::cout << "Strategy: FillUpOscillation on XAUUSD" << std::endl;
    std::cout << std::string(80, '=') << std::endl << std::endl;

    // Load tick data (2024 + 2025 for sufficient history)
    std::cout << "Loading tick data..." << std::endl;
    auto start_load = std::chrono::high_resolution_clock::now();

    std::vector<Tick> all_ticks;
    std::vector<std::string> files = {
        "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2024.csv",
        "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv"
    };

    for (const auto& file : files) {
        TickDataConfig cfg;
        cfg.file_path = file;
        cfg.format = TickDataFormat::MT5_CSV;
        TickDataManager mgr(cfg);
        Tick tick;
        while (mgr.GetNextTick(tick)) {
            all_ticks.push_back(tick);
        }
        std::cout << "  Loaded: " << file << std::endl;
    }

    // Sort by timestamp
    std::sort(all_ticks.begin(), all_ticks.end(),
              [](const Tick& a, const Tick& b) { return a.timestamp < b.timestamp; });

    auto end_load = std::chrono::high_resolution_clock::now();
    auto load_sec = std::chrono::duration_cast<std::chrono::seconds>(end_load - start_load).count();
    std::cout << "Total ticks: " << all_ticks.size() << " (loaded in " << load_sec << "s)" << std::endl;
    std::cout << "Date range: " << all_ticks.front().timestamp.substr(0, 10)
              << " to " << all_ticks.back().timestamp.substr(0, 10) << std::endl << std::endl;

    // Configure walk-forward
    WalkForwardConfig wf_config;
    wf_config.optimization_window_days = 120;  // 4 months IS
    wf_config.test_window_days = 60;           // 2 months OOS
    wf_config.step_days = 60;                  // Roll forward by 2 months
    wf_config.anchored = false;                // Rolling window (not anchored)

    // Define parameter ranges to optimize
    std::vector<ParamRange> param_ranges = {
        {"survive_pct", 10.0, 15.0, 1.0},      // 10%, 11%, ..., 15%
        {"base_spacing", 1.0, 2.0, 0.5},       // $1.0, $1.5, $2.0
        {"lookback_hours", 2.0, 6.0, 2.0}      // 2h, 4h, 6h
    };

    // Base engine config
    TickBacktestConfig engine_config;
    engine_config.symbol = "XAUUSD";
    engine_config.initial_balance = 10000.0;
    engine_config.contract_size = 100.0;
    engine_config.leverage = 500.0;
    engine_config.pip_size = 0.01;
    engine_config.swap_long = -66.99;
    engine_config.swap_short = 41.2;
    engine_config.swap_mode = 1;
    engine_config.swap_3days = 3;
    engine_config.verbose = false;

    // Strategy factory - creates FillUpOscillation from parameter vector
    auto strategy_factory = [](const std::vector<double>& params) {
        // params: [survive_pct, base_spacing, lookback_hours]
        return FillUpOscillation(
            params[0],                              // survive_pct
            params[1],                              // base_spacing
            0.01,                                   // min_volume
            10.0,                                   // max_volume
            100.0,                                  // contract_size
            500.0,                                  // leverage
            FillUpOscillation::ADAPTIVE_SPACING,    // mode
            0.1,                                    // antifragile_scale (unused)
            30.0,                                   // velocity_threshold (unused)
            params[2]                               // volatility_lookback_hours
        );
    };

    // Create optimizer
    WalkForwardOptimizer<FillUpOscillation> optimizer(
        wf_config,
        param_ranges,
        engine_config,
        strategy_factory
    );

    // Use return/DD ratio as fitness
    optimizer.SetFitnessMetric(FitnessMetric::RETURN_DD_RATIO);

    // Run walk-forward
    std::cout << "Starting walk-forward optimization..." << std::endl;
    auto start_wf = std::chrono::high_resolution_clock::now();

    auto result = optimizer.Run(all_ticks, true);

    auto end_wf = std::chrono::high_resolution_clock::now();
    auto wf_sec = std::chrono::duration_cast<std::chrono::seconds>(end_wf - start_wf).count();

    // Print detailed window results
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "WINDOW DETAILS" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    std::cout << std::left << std::setw(12) << "Window"
              << std::setw(24) << "IS Period"
              << std::setw(24) << "OOS Period"
              << std::right << std::setw(12) << "IS Profit"
              << std::setw(12) << "OOS Profit"
              << std::setw(10) << "Eff%"
              << std::endl;
    std::cout << std::string(80, '-') << std::endl;

    for (size_t i = 0; i < result.windows.size(); i++) {
        const auto& w = result.windows[i];
        std::cout << std::left << std::setw(12) << (std::to_string(i+1))
                  << std::setw(24) << (w.is_start + " - " + w.is_end)
                  << std::setw(24) << (w.oos_start + " - " + w.oos_end)
                  << std::right << std::fixed << std::setprecision(0)
                  << std::setw(11) << w.is_profit << "$"
                  << std::setw(11) << w.oos_profit << "$"
                  << std::setw(9) << std::setprecision(1) << (w.efficiency_ratio * 100) << "%"
                  << std::endl;
    }

    // Parameter stability analysis
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "PARAMETER STABILITY ACROSS WINDOWS" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    for (size_t i = 0; i < result.windows.size(); i++) {
        std::cout << "Window " << (i+1) << ": " << result.windows[i].optimal_param_name << std::endl;
    }

    // Final summary
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "WALK-FORWARD SUMMARY" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    std::cout << "Total runtime: " << wf_sec << " seconds" << std::endl;
    std::cout << "Windows completed: " << result.windows.size() << std::endl;
    std::cout << std::endl;

    std::cout << "Combined OOS Results:" << std::endl;
    std::cout << "  Total profit: $" << std::fixed << std::setprecision(2) << result.total_oos_profit << std::endl;
    std::cout << "  Max drawdown: " << result.oos_max_dd_pct << "%" << std::endl;
    std::cout << "  Total trades: " << result.oos_trades << std::endl;
    std::cout << std::endl;

    std::cout << "Robustness Metrics:" << std::endl;
    std::cout << "  Avg efficiency (OOS/IS): " << std::setprecision(1) << (result.avg_efficiency_ratio * 100) << "%" << std::endl;
    std::cout << "  Parameter stability: " << (result.param_stability * 100) << "%" << std::endl;
    std::cout << "  Walk-forward ratio: " << (result.walk_forward_ratio * 100) << "%" << std::endl;
    std::cout << std::endl;

    std::cout << "Full-period optimization profit: $" << std::setprecision(2) << result.full_period_profit << std::endl;
    std::cout << std::endl;

    std::cout << "====================================" << std::endl;
    std::cout << "ROBUSTNESS SCORE: " << std::setprecision(0) << result.robustness_score << "/100" << std::endl;
    std::cout << "====================================" << std::endl;

    if (result.robustness_score >= 70) {
        std::cout << "\nVERDICT: ROBUST" << std::endl;
        std::cout << "Strategy parameters are stable and OOS performance is consistent." << std::endl;
        std::cout << "Low overfitting risk - suitable for live trading consideration." << std::endl;
    } else if (result.robustness_score >= 50) {
        std::cout << "\nVERDICT: MODERATE" << std::endl;
        std::cout << "Some variation in optimal parameters across windows." << std::endl;
        std::cout << "Moderate overfitting risk - use conservative position sizing." << std::endl;
    } else {
        std::cout << "\nVERDICT: POOR" << std::endl;
        std::cout << "Optimal parameters vary significantly between windows." << std::endl;
        std::cout << "High overfitting risk - strategy may fail in live trading." << std::endl;
    }

    return 0;
}
