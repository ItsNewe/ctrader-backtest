/**
 * test_mt5_vs_cpp_comparison.cpp
 * Compare C++ backtest results with MT5 Strategy Tester results
 * Uses extended data range: 2025.01.01 - 2026.01.27 (matching MT5 test period)
 */

#include "../include/fill_up_oscillation.h"
#include "../include/strategy_combined_ju.h"
#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>

using namespace backtest;

// Load ticks from multiple files and concatenate
std::vector<Tick> LoadAndConcatenateTicks(const std::vector<std::string>& files) {
    std::vector<Tick> all_ticks;

    for (const auto& file : files) {
        std::cout << "Loading: " << file << "..." << std::flush;

        TickDataManager mgr(file);

        Tick tick;
        size_t count = 0;
        while (mgr.GetNextTick(tick)) {
            all_ticks.push_back(tick);
            count++;
        }
        std::cout << " " << count << " ticks" << std::endl;
    }

    // Sort by timestamp to ensure correct order
    std::sort(all_ticks.begin(), all_ticks.end(),
              [](const Tick& a, const Tick& b) { return a.timestamp < b.timestamp; });

    std::cout << "Total ticks loaded: " << all_ticks.size() << std::endl;
    return all_ticks;
}

struct TestResult {
    std::string name;
    double final_balance;
    double return_mult;
    double max_dd;
    double total_swap;
    int total_trades;
    bool stopped_out;
};

// Run v4/v5 style strategy (FillUpOscillation with percentage spacing)
TestResult RunV4V5Strategy(const std::vector<Tick>& ticks,
                           const std::string& name,
                           double survive_pct,
                           double base_spacing_pct,
                           double typical_vol_pct,
                           double volatility_lookback_hours) {

    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = 10000.0;
    config.account_currency = "USD";
    config.contract_size = 100.0;
    config.leverage = 500.0;
    config.margin_rate = 1.0;
    config.pip_size = 0.01;
    config.swap_long = -68.25;  // Updated from downloaded data
    config.swap_short = 35.06;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = "2025.01.01";
    config.end_date = "2026.01.27";

    TickDataConfig tick_cfg;
    tick_cfg.format = TickDataFormat::MT5_CSV;
    tick_cfg.load_all_into_memory = true;
    config.tick_data_config = tick_cfg;

    TickBasedEngine engine(config);

    // Configure adaptive spacing with percentage mode
    FillUpOscillation::AdaptiveConfig adaptive_cfg;
    adaptive_cfg.typical_vol_pct = typical_vol_pct;
    adaptive_cfg.min_spacing_mult = 0.5;
    adaptive_cfg.max_spacing_mult = 3.0;
    adaptive_cfg.pct_spacing = true;  // percentage mode
    adaptive_cfg.min_spacing_abs = 0.005;
    adaptive_cfg.max_spacing_abs = 1.0;
    adaptive_cfg.spacing_change_threshold = 0.01;

    FillUpOscillation strategy(
        survive_pct,
        base_spacing_pct,  // This is now percentage
        0.01,   // min_volume
        10.0,   // max_volume
        100.0,  // contract_size
        500.0,  // leverage
        FillUpOscillation::ADAPTIVE_SPACING,
        0.0, 0.0,
        volatility_lookback_hours,
        adaptive_cfg
    );

    // Run with preloaded ticks
    engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();

    TestResult r;
    r.name = name;
    r.final_balance = results.final_balance;
    r.return_mult = results.final_balance / 10000.0;
    r.max_dd = results.max_drawdown_pct;
    r.total_swap = results.total_swap_charged;
    r.total_trades = results.total_trades;
    r.stopped_out = results.stop_out_occurred;

    return r;
}

// Run CombinedJu strategy
TestResult RunCombinedJuStrategy(const std::vector<Tick>& ticks,
                                  const std::string& name,
                                  double survive_pct,
                                  double base_spacing,
                                  double typical_vol_pct,
                                  double volatility_lookback_hours,
                                  double tp_sqrt_scale,
                                  double tp_minimum,
                                  int velocity_window,
                                  double velocity_threshold_pct,
                                  int barbell_threshold_pos,
                                  double barbell_multiplier) {

    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = 10000.0;
    config.account_currency = "USD";
    config.contract_size = 100.0;
    config.leverage = 500.0;
    config.margin_rate = 1.0;
    config.pip_size = 0.01;
    config.swap_long = -68.25;
    config.swap_short = 35.06;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = "2025.01.01";
    config.end_date = "2026.01.27";

    TickDataConfig tick_cfg;
    tick_cfg.format = TickDataFormat::MT5_CSV;
    tick_cfg.load_all_into_memory = true;
    config.tick_data_config = tick_cfg;

    TickBasedEngine engine(config);

    StrategyCombinedJu::Config strat_cfg;
    strat_cfg.survive_pct = survive_pct;
    strat_cfg.base_spacing = base_spacing;
    strat_cfg.volatility_lookback_hours = volatility_lookback_hours;
    strat_cfg.typical_vol_pct = typical_vol_pct;

    // Rubber Band TP
    strat_cfg.tp_mode = StrategyCombinedJu::SQRT;
    strat_cfg.tp_sqrt_scale = tp_sqrt_scale;
    strat_cfg.tp_min = tp_minimum;

    // Velocity Filter
    strat_cfg.enable_velocity_filter = true;
    strat_cfg.velocity_window = velocity_window;
    strat_cfg.velocity_threshold_pct = velocity_threshold_pct;

    // Threshold Barbell Sizing
    strat_cfg.sizing_mode = StrategyCombinedJu::THRESHOLD_SIZING;
    strat_cfg.sizing_threshold_pos = barbell_threshold_pos;
    strat_cfg.sizing_threshold_mult = barbell_multiplier;

    StrategyCombinedJu strategy(strat_cfg);

    engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();

    TestResult r;
    r.name = name;
    r.final_balance = results.final_balance;
    r.return_mult = results.final_balance / 10000.0;
    r.max_dd = results.max_drawdown_pct;
    r.total_swap = results.total_swap_charged;
    r.total_trades = results.total_trades;
    r.stopped_out = results.stop_out_occurred;

    return r;
}

int main() {
    std::cout << "=" << std::string(70, '=') << std::endl;
    std::cout << "MT5 vs C++ Backtest Comparison" << std::endl;
    std::cout << "Period: 2025.01.01 - 2026.01.27 (matching MT5 Strategy Tester)" << std::endl;
    std::cout << "=" << std::string(70, '=') << std::endl << std::endl;

    // Load tick data from both files
    std::vector<std::string> tick_files = {
        "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv",
        "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_JAN2026.csv"
    };

    std::vector<Tick> ticks = LoadAndConcatenateTicks(tick_files);

    if (ticks.empty()) {
        std::cerr << "Failed to load tick data!" << std::endl;
        return 1;
    }

    // Find price range
    double min_price = DBL_MAX, max_price = 0.0;
    for (const auto& t : ticks) {
        if (t.bid < min_price) min_price = t.bid;
        if (t.bid > max_price) max_price = t.bid;
    }

    std::cout << "\nPrice range: $" << std::fixed << std::setprecision(2)
              << min_price << " - $" << max_price << std::endl;
    std::cout << "\n" << std::string(80, '-') << std::endl;

    std::vector<TestResult> results;

    // v4 Aggressive (MT5 params: SurvivePct=12%, BaseSpacingPct=0.05%)
    std::cout << "\nRunning v4 Aggressive (MT5 params)..." << std::endl;
    results.push_back(RunV4V5Strategy(ticks, "v4_Aggressive",
        12.0,   // survive_pct
        0.05,   // base_spacing_pct
        0.55,   // typical_vol_pct
        4.0     // lookback_hours
    ));

    // v5 FloatingAttractor (MT5 params: SurvivePct=12%, BaseSpacingPct=0.06%)
    std::cout << "\nRunning v5 FloatingAttractor (MT5 params)..." << std::endl;
    results.push_back(RunV4V5Strategy(ticks, "v5_FloatingAttractor",
        12.0,   // survive_pct
        0.06,   // base_spacing_pct
        0.55,   // typical_vol_pct
        8.0     // lookback_hours (v5 uses 8)
    ));

    // CombinedJu P1_M3 (exact params match MT5)
    std::cout << "\nRunning CombinedJu P1_M3 (MT5 params)..." << std::endl;
    results.push_back(RunCombinedJuStrategy(ticks, "CombinedJu_P1_M3",
        13.0,   // survive_pct
        1.5,    // base_spacing (absolute $)
        0.55,   // typical_vol_pct
        4.0,    // lookback_hours
        0.5,    // tp_sqrt_scale
        1.5,    // tp_minimum
        10,     // velocity_window
        0.01,   // velocity_threshold_pct
        1,      // barbell_threshold_pos
        3.0     // barbell_multiplier
    ));

    // Print results table
    std::cout << "\n" << std::string(100, '=') << std::endl;
    std::cout << "C++ BACKTEST RESULTS (2025.01.01 - 2026.01.27)" << std::endl;
    std::cout << std::string(100, '=') << std::endl;

    std::cout << std::left << std::setw(25) << "Strategy"
              << std::right << std::setw(15) << "Final Balance"
              << std::setw(12) << "Return"
              << std::setw(10) << "Max DD"
              << std::setw(12) << "Swap"
              << std::setw(10) << "Trades"
              << std::setw(10) << "Status" << std::endl;
    std::cout << std::string(100, '-') << std::endl;

    for (const auto& r : results) {
        std::cout << std::left << std::setw(25) << r.name
                  << std::right << std::fixed << std::setprecision(2)
                  << "$" << std::setw(14) << r.final_balance
                  << std::setw(10) << r.return_mult << "x"
                  << std::setw(9) << r.max_dd << "%"
                  << " $" << std::setw(10) << r.total_swap
                  << std::setw(10) << r.total_trades
                  << std::setw(10) << (r.stopped_out ? "STOP-OUT" : "OK") << std::endl;
    }

    // MT5 results comparison
    std::cout << "\n" << std::string(100, '=') << std::endl;
    std::cout << "MT5 STRATEGY TESTER RESULTS (2025.01.01 - 2026.01.27)" << std::endl;
    std::cout << std::string(100, '=') << std::endl;

    std::cout << std::left << std::setw(25) << "Strategy"
              << std::right << std::setw(15) << "Final Balance"
              << std::setw(12) << "Return"
              << std::setw(12) << "Est. Max DD" << std::endl;
    std::cout << std::string(70, '-') << std::endl;

    std::cout << std::left << std::setw(25) << "v4_Aggressive"
              << std::right << "$" << std::setw(14) << "197,344.80"
              << std::setw(10) << "19.73" << "x"
              << std::setw(10) << "~70" << "%" << std::endl;

    std::cout << std::left << std::setw(25) << "v5_FloatingAttractor"
              << std::right << "$" << std::setw(14) << "198,197.02"
              << std::setw(10) << "19.82" << "x"
              << std::setw(10) << "~70" << "%" << std::endl;

    std::cout << std::left << std::setw(25) << "CombinedJu_P1_M3"
              << std::right << "$" << std::setw(14) << "133,729.24"
              << std::setw(10) << "13.37" << "x"
              << std::setw(10) << "~70" << "%" << std::endl;

    // Comparison summary
    std::cout << "\n" << std::string(100, '=') << std::endl;
    std::cout << "COMPARISON SUMMARY (C++/MT5 Ratio)" << std::endl;
    std::cout << std::string(100, '=') << std::endl;

    std::cout << std::left << std::setw(25) << "Strategy"
              << std::right << std::setw(15) << "MT5"
              << std::setw(15) << "C++"
              << std::setw(15) << "Ratio"
              << std::setw(20) << "Match" << std::endl;
    std::cout << std::string(90, '-') << std::endl;

    // v4
    double mt5_v4 = 19.73;
    double cpp_v4 = results[0].return_mult;
    double ratio_v4 = cpp_v4 / mt5_v4;
    std::cout << std::left << std::setw(25) << "v4_Aggressive"
              << std::right << std::setw(13) << mt5_v4 << "x"
              << std::setw(13) << std::fixed << std::setprecision(2) << cpp_v4 << "x"
              << std::setw(13) << std::setprecision(2) << ratio_v4
              << std::setw(20) << (ratio_v4 > 0.8 && ratio_v4 < 1.2 ? "GOOD" : "DIFFER") << std::endl;

    // v5
    double mt5_v5 = 19.82;
    double cpp_v5 = results[1].return_mult;
    double ratio_v5 = cpp_v5 / mt5_v5;
    std::cout << std::left << std::setw(25) << "v5_FloatingAttractor"
              << std::right << std::setw(13) << mt5_v5 << "x"
              << std::setw(13) << std::fixed << std::setprecision(2) << cpp_v5 << "x"
              << std::setw(13) << std::setprecision(2) << ratio_v5
              << std::setw(20) << (ratio_v5 > 0.8 && ratio_v5 < 1.2 ? "GOOD" : "DIFFER") << std::endl;

    // CombinedJu
    double mt5_cj = 13.37;
    double cpp_cj = results[2].return_mult;
    double ratio_cj = cpp_cj / mt5_cj;
    std::cout << std::left << std::setw(25) << "CombinedJu_P1_M3"
              << std::right << std::setw(13) << mt5_cj << "x"
              << std::setw(13) << std::fixed << std::setprecision(2) << cpp_cj << "x"
              << std::setw(13) << std::setprecision(2) << ratio_cj
              << std::setw(20) << (ratio_cj > 0.8 && ratio_cj < 1.2 ? "GOOD" : "DIFFER") << std::endl;

    std::cout << "\nNotes:" << std::endl;
    std::cout << "- Ratio 0.8-1.2 = Good match between MT5 and C++ backtest" << std::endl;
    std::cout << "- Ratio > 1.0 means C++ outperforms MT5" << std::endl;
    std::cout << "- Ratio < 1.0 means MT5 outperforms C++" << std::endl;
    std::cout << "- Swap updated to: long=-68.25, short=35.06 (from  Jan 2026)" << std::endl;
    std::cout << "- Gold peaked at ~$" << std::setprecision(2) << max_price << " in this period (ATH!)" << std::endl;

    return 0;
}
