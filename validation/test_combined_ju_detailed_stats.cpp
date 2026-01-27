/**
 * test_combined_ju_detailed_stats.cpp
 *
 * Run CombinedJu with detailed statistics to compare with MT5 diagnostic
 */

#include "../include/strategy_combined_ju.h"
#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>

using namespace backtest;

std::vector<Tick> g_ticks;

void LoadTicks() {
    std::cout << "Loading tick data..." << std::endl;
    std::vector<std::string> files = {
        "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv",
        "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_JAN2026.csv"
    };
    for (const auto& file : files) {
        TickDataManager mgr(file);
        Tick tick;
        while (mgr.GetNextTick(tick)) {
            g_ticks.push_back(tick);
        }
    }
    std::sort(g_ticks.begin(), g_ticks.end(),
              [](const Tick& a, const Tick& b) { return a.timestamp < b.timestamp; });
    std::cout << "Loaded " << g_ticks.size() << " ticks" << std::endl;
}

int main() {
    std::cout << "=" << std::string(80, '=') << std::endl;
    std::cout << "COMBINED JU DETAILED STATISTICS" << std::endl;
    std::cout << "Comparing with MT5 diagnostic output" << std::endl;
    std::cout << "=" << std::string(80, '=') << std::endl << std::endl;

    LoadTicks();

    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = 10000.0;
    config.contract_size = 100.0;
    config.leverage = 500.0;
    config.pip_size = 0.01;
    config.swap_long = -68.25;
    config.swap_short = 35.06;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = "2025.01.01";
    config.end_date = "2026.01.27";

    TickDataConfig tick_cfg;
    tick_cfg.format = TickDataFormat::MT5_CSV;
    config.tick_data_config = tick_cfg;

    TickBasedEngine engine(config);

    // P1_M3 configuration (matching MT5)
    StrategyCombinedJu::Config strat_cfg;
    strat_cfg.survive_pct = 13.0;
    strat_cfg.base_spacing = 1.5;
    strat_cfg.volatility_lookback_hours = 4.0;
    strat_cfg.typical_vol_pct = 0.55;
    strat_cfg.tp_mode = StrategyCombinedJu::SQRT;
    strat_cfg.tp_sqrt_scale = 0.5;
    strat_cfg.tp_min = 1.5;
    strat_cfg.enable_velocity_filter = true;
    strat_cfg.velocity_window = 10;
    strat_cfg.velocity_threshold_pct = 0.01;
    strat_cfg.sizing_mode = StrategyCombinedJu::THRESHOLD_SIZING;
    strat_cfg.sizing_threshold_pos = 1;
    strat_cfg.sizing_threshold_mult = 3.0;

    StrategyCombinedJu strategy(strat_cfg);

    std::cout << "Running backtest..." << std::endl;

    engine.RunWithTicks(g_ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();
    auto& stats = strategy.GetStats();

    // Print results
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "C++ RESULTS" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Final balance:            $" << results.final_balance << std::endl;
    std::cout << "Return:                   " << (results.final_balance / 10000.0) << "x" << std::endl;
    std::cout << "Max drawdown:             " << results.max_drawdown_pct << "%" << std::endl;
    std::cout << "Total trades:             " << results.total_trades << std::endl;

    std::cout << "\n--- Detailed Statistics ---" << std::endl;
    std::cout << "Total ticks:              " << g_ticks.size() << std::endl;
    std::cout << "Ticks with positions:     " << stats.ticks_with_positions << std::endl;
    std::cout << "Spacing condition true:   " << stats.spacing_condition_checks << std::endl;
    std::cout << "Lot size zero blocks:     " << stats.lot_size_zero_blocks << std::endl;
    std::cout << "Velocity blocks:          " << stats.velocity_blocks << std::endl;
    std::cout << "Entries allowed:          " << stats.entries_allowed << std::endl;

    double entry_attempts = stats.velocity_blocks + stats.entries_allowed;
    double total_blocks = stats.lot_size_zero_blocks + stats.velocity_blocks;
    double block_rate = entry_attempts > 0 ? 100.0 * stats.velocity_blocks / entry_attempts : 0;
    std::cout << "Entry attempts:           " << (long)entry_attempts << std::endl;
    std::cout << "Total blocks (margin+vel):" << (long)total_blocks << std::endl;
    std::cout << "Velocity block rate:      " << block_rate << "%" << std::endl;
    std::cout << "Max positions:            " << stats.max_position_count << std::endl;

    double avg_lots = stats.entries_allowed > 0 ? stats.total_lots_opened / stats.entries_allowed : 0;
    double avg_tp = stats.entries_allowed > 0 ? stats.total_tp_set / stats.entries_allowed : 0;
    std::cout << "Avg lots per entry:       " << std::setprecision(4) << avg_lots << std::endl;
    std::cout << "Avg TP distance:          $" << std::setprecision(2) << avg_tp << std::endl;

    // Spacing analysis
    std::cout << "\n--- Spacing Condition Analysis ---" << std::endl;
    double condition_true_rate = stats.ticks_with_positions > 0
        ? 100.0 * stats.spacing_condition_checks / stats.ticks_with_positions : 0;
    std::cout << "Condition true rate:      " << std::setprecision(4) << condition_true_rate << "%" << std::endl;

    if (stats.spacing_condition_checks > 0) {
        double avg_spacing = stats.sum_spacing_when_true / stats.spacing_condition_checks;
        double avg_lowest_buy = stats.sum_lowest_buy_when_true / stats.spacing_condition_checks;
        double avg_ask = stats.sum_ask_when_true / stats.spacing_condition_checks;
        double avg_gap = (stats.sum_lowest_buy_when_true - stats.sum_ask_when_true) / stats.spacing_condition_checks;

        std::cout << "Avg spacing when true:    $" << std::setprecision(4) << avg_spacing << std::endl;
        std::cout << "Avg lowestBuy when true:  $" << std::setprecision(2) << avg_lowest_buy << std::endl;
        std::cout << "Avg ask when true:        $" << std::setprecision(2) << avg_ask << std::endl;
        std::cout << "Avg (lowestBuy - ask):    $" << std::setprecision(4) << avg_gap << std::endl;
    }

    // Comparison with MT5
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "COMPARISON WITH MT5" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    std::cout << std::left << std::setw(30) << "Metric"
              << std::right << std::setw(18) << "C++"
              << std::setw(18) << "MT5"
              << std::setw(12) << "Ratio" << std::endl;
    std::cout << std::string(78, '-') << std::endl;

    auto PrintRow = [](const std::string& name, double cpp, double mt5) {
        std::cout << std::left << std::setw(30) << name
                  << std::right << std::setw(18) << std::fixed << std::setprecision(0) << cpp
                  << std::setw(18) << mt5
                  << std::setw(12) << std::setprecision(2) << (mt5 / cpp) << "x" << std::endl;
    };

    PrintRow("Ticks processed", g_ticks.size(), 56924931);
    PrintRow("Ticks with positions", stats.ticks_with_positions, 56923582);
    PrintRow("Spacing condition true", stats.spacing_condition_checks, 19058571);
    PrintRow("Lot size zero blocks", stats.lot_size_zero_blocks, 16363469);  // 19M - 2.7M entry attempts
    PrintRow("Entry attempts", entry_attempts, 2695102);
    PrintRow("Velocity blocks", stats.velocity_blocks, 2663922);
    PrintRow("Entries allowed", stats.entries_allowed, 31180);

    std::cout << std::left << std::setw(30) << "Block rate"
              << std::right << std::setw(17) << std::setprecision(2) << block_rate << "%"
              << std::setw(17) << "98.84%"
              << std::setw(12) << "-" << std::endl;

    PrintRow("Max positions", stats.max_position_count, 197);

    // Analysis
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "ROOT CAUSE ANALYSIS" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    std::cout << "\nThe key insight is in 'Lot size zero blocks':" << std::endl;
    std::cout << "  - When spacing condition is TRUE but lot sizing returns 0" << std::endl;
    std::cout << "  - MT5 skips the entry, lowestBuy stays stuck at old price" << std::endl;
    std::cout << "  - Condition stays TRUE for millions of consecutive ticks" << std::endl;

    double mt5_margin_blocks = 19058571.0 - 2695102.0;  // 16.36M
    std::cout << "\nMT5 margin-blocked entries: " << std::setprecision(0) << mt5_margin_blocks << std::endl;
    std::cout << "C++ margin-blocked entries: " << stats.lot_size_zero_blocks << std::endl;

    if (stats.lot_size_zero_blocks < mt5_margin_blocks * 0.5) {
        std::cout << "\n*** BUG CONFIRMED ***" << std::endl;
        std::cout << "C++ was forcing entries when lot sizing returned 0!" << std::endl;
        std::cout << "Now fixed - entries blocked when lots < min_volume." << std::endl;
    }

    double blocks_per_entry_cpp = stats.entries_allowed > 0 ? (double)stats.velocity_blocks / stats.entries_allowed : 0;
    double blocks_per_entry_mt5 = 2663922.0 / 31180.0;
    std::cout << "\nVelocity blocks per entry - C++: " << std::setprecision(1) << blocks_per_entry_cpp
              << ", MT5: " << blocks_per_entry_mt5 << std::endl;

    return 0;
}
