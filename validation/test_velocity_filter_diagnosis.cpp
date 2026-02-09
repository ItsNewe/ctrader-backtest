/**
 * test_velocity_filter_diagnosis.cpp
 *
 * Diagnose the CombinedJu velocity filter discrepancy between C++ and MT5
 *
 * Hypothesis: The velocity filter behaves differently due to:
 * 1. Tick frequency differences (MT5 Strategy Tester vs raw ticks)
 * 2. Circular buffer initialization differences
 * 3. Velocity window (10 ticks) meaning different time spans
 */

#include "../include/strategy_combined_ju.h"
#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>

using namespace backtest;

std::vector<Tick> g_ticks;

struct TestResult {
    std::string name;
    double final_balance;
    double return_mult;
    double max_dd;
    int trades;
    long velocity_blocks;
    long entries_allowed;
    double avg_tp;
    double avg_lots;
    int max_positions;
    bool stopped_out;
};

void LoadTicks(const std::vector<std::string>& files) {
    std::cout << "Loading tick data..." << std::endl;
    for (const auto& file : files) {
        std::cout << "  " << file << "..." << std::flush;
        TickDataManager mgr(file);
        Tick tick;
        size_t count = 0;
        while (mgr.GetNextTick(tick)) {
            g_ticks.push_back(tick);
            count++;
        }
        std::cout << " " << count << " ticks" << std::endl;
    }
    std::sort(g_ticks.begin(), g_ticks.end(),
              [](const Tick& a, const Tick& b) { return a.timestamp < b.timestamp; });
    std::cout << "Total: " << g_ticks.size() << " ticks" << std::endl;
}

TestResult RunTest(const std::string& name,
                   bool enable_velocity_filter,
                   int velocity_window,
                   double velocity_threshold_pct) {

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
    config.verbose = false;

    TickDataConfig tick_cfg;
    tick_cfg.format = TickDataFormat::MT5_CSV;
    tick_cfg.load_all_into_memory = true;
    config.tick_data_config = tick_cfg;

    TickBasedEngine engine(config);

    StrategyCombinedJu::Config strat_cfg;
    strat_cfg.survive_pct = 13.0;
    strat_cfg.base_spacing = 1.5;
    strat_cfg.volatility_lookback_hours = 4.0;
    strat_cfg.typical_vol_pct = 0.55;

    // Rubber Band TP
    strat_cfg.tp_mode = StrategyCombinedJu::SQRT;
    strat_cfg.tp_sqrt_scale = 0.5;
    strat_cfg.tp_min = 1.5;

    // Velocity Filter
    strat_cfg.enable_velocity_filter = enable_velocity_filter;
    strat_cfg.velocity_window = velocity_window;
    strat_cfg.velocity_threshold_pct = velocity_threshold_pct;

    // Threshold Barbell Sizing (P1_M3)
    strat_cfg.sizing_mode = StrategyCombinedJu::THRESHOLD_SIZING;
    strat_cfg.sizing_threshold_pos = 1;
    strat_cfg.sizing_threshold_mult = 3.0;

    StrategyCombinedJu strategy(strat_cfg);

    engine.RunWithTicks(g_ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();
    auto& stats = strategy.GetStats();

    TestResult r;
    r.name = name;
    r.final_balance = results.final_balance;
    r.return_mult = results.final_balance / 10000.0;
    r.max_dd = results.max_drawdown_pct;
    r.trades = results.total_trades;
    r.velocity_blocks = stats.velocity_blocks;
    r.entries_allowed = stats.entries_allowed;
    r.avg_tp = stats.entries_allowed > 0 ? stats.total_tp_set / stats.entries_allowed : 0;
    r.avg_lots = stats.entries_allowed > 0 ? stats.total_lots_opened / stats.entries_allowed : 0;
    r.max_positions = stats.max_position_count;
    r.stopped_out = results.stop_out_occurred;

    return r;
}

int main() {
    std::cout << "=" << std::string(80, '=') << std::endl;
    std::cout << "VELOCITY FILTER DIAGNOSIS" << std::endl;
    std::cout << "Investigating CombinedJu discrepancy (C++ 28.59x vs MT5 13.37x)" << std::endl;
    std::cout << "=" << std::string(80, '=') << std::endl << std::endl;

    // Load tick data
    std::vector<std::string> files = {
        "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv",
        "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_JAN2026.csv"
    };
    LoadTicks(files);

    // Calculate tick frequency
    if (g_ticks.size() > 1000) {
        // Sample first 1000 ticks to estimate frequency
        std::vector<double> intervals;
        for (size_t i = 1; i < 1000; i++) {
            // Parse timestamps - just estimate based on position
        }
    }

    std::cout << "\n--- Tick Statistics ---" << std::endl;
    std::cout << "Total ticks: " << g_ticks.size() << std::endl;
    std::cout << "Period: ~13 months" << std::endl;
    std::cout << "Estimated tick rate: ~" << (g_ticks.size() / 13.0 / 30 / 24 / 60) << " ticks/minute" << std::endl;
    std::cout << "At 10-tick window: ~" << (10.0 * 60 / (g_ticks.size() / 13.0 / 30 / 24 / 60)) << " seconds lookback" << std::endl;

    std::vector<TestResult> results;

    // Test 1: Velocity filter OFF (baseline)
    std::cout << "\n[1/7] Running: Velocity OFF (baseline)..." << std::endl;
    results.push_back(RunTest("VEL_OFF", false, 10, 0.01));

    // Test 2: Velocity filter ON with default params (MT5 match)
    std::cout << "[2/7] Running: Velocity ON (window=10, thresh=0.01%)..." << std::endl;
    results.push_back(RunTest("VEL_ON_W10_T0.01", true, 10, 0.01));

    // Test 3: Larger velocity window (simulates fewer ticks)
    std::cout << "[3/7] Running: Velocity ON (window=50, thresh=0.01%)..." << std::endl;
    results.push_back(RunTest("VEL_ON_W50_T0.01", true, 50, 0.01));

    // Test 4: Even larger window
    std::cout << "[4/7] Running: Velocity ON (window=100, thresh=0.01%)..." << std::endl;
    results.push_back(RunTest("VEL_ON_W100_T0.01", true, 100, 0.01));

    // Test 5: Tighter threshold (more blocking)
    std::cout << "[5/7] Running: Velocity ON (window=10, thresh=0.005%)..." << std::endl;
    results.push_back(RunTest("VEL_ON_W10_T0.005", true, 10, 0.005));

    // Test 6: Even tighter threshold
    std::cout << "[6/7] Running: Velocity ON (window=10, thresh=0.001%)..." << std::endl;
    results.push_back(RunTest("VEL_ON_W10_T0.001", true, 10, 0.001));

    // Test 7: Large window + tight threshold (extreme blocking)
    std::cout << "[7/7] Running: Velocity ON (window=100, thresh=0.005%)..." << std::endl;
    results.push_back(RunTest("VEL_ON_W100_T0.005", true, 100, 0.005));

    // Print results
    std::cout << "\n" << std::string(140, '=') << std::endl;
    std::cout << "RESULTS" << std::endl;
    std::cout << std::string(140, '=') << std::endl;

    std::cout << std::left << std::setw(22) << "Config"
              << std::right << std::setw(12) << "Return"
              << std::setw(10) << "MaxDD"
              << std::setw(10) << "Trades"
              << std::setw(14) << "Vel_Blocks"
              << std::setw(12) << "Entries"
              << std::setw(12) << "Block%"
              << std::setw(10) << "AvgTP"
              << std::setw(10) << "AvgLots"
              << std::setw(10) << "MaxPos"
              << std::setw(10) << "Status" << std::endl;
    std::cout << std::string(140, '-') << std::endl;

    for (const auto& r : results) {
        double block_pct = (r.velocity_blocks + r.entries_allowed) > 0
                           ? 100.0 * r.velocity_blocks / (r.velocity_blocks + r.entries_allowed)
                           : 0.0;

        std::cout << std::left << std::setw(22) << r.name
                  << std::right << std::fixed << std::setprecision(2)
                  << std::setw(10) << r.return_mult << "x"
                  << std::setw(9) << r.max_dd << "%"
                  << std::setw(10) << r.trades
                  << std::setw(14) << r.velocity_blocks
                  << std::setw(12) << r.entries_allowed
                  << std::setw(11) << block_pct << "%"
                  << std::setw(9) << r.avg_tp << "$"
                  << std::setw(10) << r.avg_lots
                  << std::setw(10) << r.max_positions
                  << std::setw(10) << (r.stopped_out ? "STOP-OUT" : "OK") << std::endl;
    }

    // Analysis
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "ANALYSIS" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    double baseline_return = results[0].return_mult;
    double vel_on_return = results[1].return_mult;
    double velocity_impact = (vel_on_return / baseline_return - 1.0) * 100.0;

    std::cout << "\n1. Velocity Filter Impact (default params):" << std::endl;
    std::cout << "   Baseline (VEL_OFF): " << baseline_return << "x" << std::endl;
    std::cout << "   With velocity (VEL_ON): " << vel_on_return << "x" << std::endl;
    std::cout << "   Impact: " << (velocity_impact > 0 ? "+" : "") << velocity_impact << "%" << std::endl;

    std::cout << "\n2. Blocking Rate:" << std::endl;
    for (const auto& r : results) {
        double block_pct = (r.velocity_blocks + r.entries_allowed) > 0
                           ? 100.0 * r.velocity_blocks / (r.velocity_blocks + r.entries_allowed)
                           : 0.0;
        std::cout << "   " << std::left << std::setw(22) << r.name
                  << ": " << std::fixed << std::setprecision(1) << block_pct << "% blocked" << std::endl;
    }

    std::cout << "\n3. Hypothesis Testing:" << std::endl;

    // Check if larger window reduces returns significantly
    double w50_return = results[2].return_mult;
    double w100_return = results[3].return_mult;

    std::cout << "   Window 10:  " << vel_on_return << "x" << std::endl;
    std::cout << "   Window 50:  " << w50_return << "x ("
              << ((w50_return/vel_on_return - 1.0) * 100) << "% vs W10)" << std::endl;
    std::cout << "   Window 100: " << w100_return << "x ("
              << ((w100_return/vel_on_return - 1.0) * 100) << "% vs W10)" << std::endl;

    // MT5 target: 13.37x
    std::cout << "\n4. Finding MT5-equivalent config:" << std::endl;
    std::cout << "   MT5 result: 13.37x" << std::endl;

    double min_diff = DBL_MAX;
    std::string closest_config;
    for (const auto& r : results) {
        double diff = std::abs(r.return_mult - 13.37);
        if (diff < min_diff) {
            min_diff = diff;
            closest_config = r.name;
        }
    }
    std::cout << "   Closest C++ config: " << closest_config << std::endl;

    for (const auto& r : results) {
        if (r.name == closest_config) {
            std::cout << "   - Return: " << r.return_mult << "x" << std::endl;
            std::cout << "   - Block rate: " << std::fixed << std::setprecision(1)
                      << (100.0 * r.velocity_blocks / (r.velocity_blocks + r.entries_allowed))
                      << "%" << std::endl;
        }
    }

    std::cout << "\n5. Key Insight:" << std::endl;
    if (baseline_return > 25.0 && vel_on_return > 25.0) {
        std::cout << "   Velocity filter has MINIMAL impact on C++ returns." << std::endl;
        std::cout << "   MT5's lower return (13.37x) cannot be explained by velocity alone." << std::endl;
        std::cout << "   OTHER FACTORS must be involved:" << std::endl;
        std::cout << "   - MT5 tick delivery differences" << std::endl;
        std::cout << "   - Spread handling" << std::endl;
        std::cout << "   - Position limit differences" << std::endl;
        std::cout << "   - Margin calculation differences" << std::endl;
    } else {
        std::cout << "   Velocity filter HAS significant impact." << std::endl;
        std::cout << "   Look for config where C++ matches MT5's 13.37x return." << std::endl;
    }

    return 0;
}
