#include <iostream>
#include <iomanip>
#include "../include/tick_based_engine.h"
#include "../include/fill_up_strategy_v3.h"
#include "../include/fill_up_strategy_v7.h"
#include "../include/fill_up_strategy_v10.h"
#include "../include/fill_up_strategy_v12.h"

using namespace backtest;

struct TestResult {
    std::string name;
    double final_equity;
    double max_dd;
    int total_trades;
    double roi;
};

template<typename Strategy>
TestResult run_strategy_test(const std::string& name, const std::string& tick_file,
                              typename Strategy::Config config) {
    TickBacktestConfig engine_config;
    engine_config.symbol = "XAUUSD";
    engine_config.initial_balance = 10000.0;
    engine_config.contract_size = 100.0;
    engine_config.leverage = 500.0;
    engine_config.pip_size = 0.01;
    engine_config.swap_long = -38.0;
    engine_config.swap_short = 17.0;
    engine_config.tick_data_config.filename = tick_file;

    TickBasedEngine engine(engine_config);
    Strategy strategy(config);

    double peak_equity = engine_config.initial_balance;
    double max_dd = 0.0;

    engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);

        double eq = eng.GetEquity();
        if (eq > peak_equity) peak_equity = eq;
        double dd = (peak_equity - eq) / peak_equity * 100.0;
        if (dd > max_dd) max_dd = dd;
    });

    TestResult result;
    result.name = name;
    result.final_equity = engine.GetEquity();
    result.max_dd = max_dd;
    result.total_trades = engine.GetClosedTrades().size();
    result.roi = (result.final_equity / 10000.0 - 1.0) * 100.0;
    return result;
}

int main() {
    std::cout << "================================================================\n";
    std::cout << "COMPREHENSIVE GRID STRATEGY COMPARISON\n";
    std::cout << "Full Year XAUUSD Data (2025)\n";
    std::cout << "================================================================\n\n";

    const std::string tick_file = "Grid/XAUUSD_TICKS_2025.csv";

    std::vector<TestResult> results;

    // Test V3 with different DD levels
    std::cout << "Testing V3 strategies...\n" << std::flush;
    {
        FillUpStrategyV3::Config cfg;
        cfg.survive_pct = 13.0;
        cfg.spacing = 1.0;
        cfg.stop_new_at_dd = 5.0;
        cfg.partial_close_at_dd = 8.0;
        cfg.close_all_at_dd = 25.0;
        cfg.max_positions = 20;
        results.push_back(run_strategy_test<FillUpStrategyV3>("V3 (5/8/25 DD)", tick_file, cfg));
    }

    {
        FillUpStrategyV3::Config cfg;
        cfg.survive_pct = 13.0;
        cfg.spacing = 1.0;
        cfg.stop_new_at_dd = 3.0;
        cfg.partial_close_at_dd = 5.0;
        cfg.close_all_at_dd = 15.0;
        cfg.max_positions = 15;
        results.push_back(run_strategy_test<FillUpStrategyV3>("V3 Conservative (3/5/15 DD)", tick_file, cfg));
    }

    // Test V7 with volatility filter
    std::cout << "Testing V7 strategies...\n" << std::flush;
    {
        FillUpStrategyV7::Config cfg;
        cfg.survive_pct = 13.0;
        cfg.spacing = 1.0;
        cfg.stop_new_at_dd = 5.0;
        cfg.partial_close_at_dd = 8.0;
        cfg.close_all_at_dd = 25.0;
        cfg.max_positions = 20;
        cfg.enable_volatility_filter = false;
        results.push_back(run_strategy_test<FillUpStrategyV7>("V7 (no vol filter)", tick_file, cfg));
    }

    {
        FillUpStrategyV7::Config cfg;
        cfg.survive_pct = 13.0;
        cfg.spacing = 1.0;
        cfg.stop_new_at_dd = 5.0;
        cfg.partial_close_at_dd = 8.0;
        cfg.close_all_at_dd = 25.0;
        cfg.max_positions = 20;
        cfg.enable_volatility_filter = true;
        cfg.volatility_threshold = 0.002;
        results.push_back(run_strategy_test<FillUpStrategyV7>("V7 (vol filter ON)", tick_file, cfg));
    }

    // Test V10 combined features
    std::cout << "Testing V10 strategies...\n" << std::flush;
    {
        FillUpStrategyV10::Config cfg;
        cfg.survive_pct = 13.0;
        cfg.spacing = 1.0;
        cfg.stop_new_at_dd = 5.0;
        cfg.partial_close_at_dd = 8.0;
        cfg.close_all_at_dd = 25.0;
        cfg.max_positions = 20;
        cfg.enable_atr_filter = false;
        results.push_back(run_strategy_test<FillUpStrategyV10>("V10 (ATR OFF)", tick_file, cfg));
    }

    {
        FillUpStrategyV10::Config cfg;
        cfg.survive_pct = 13.0;
        cfg.spacing = 1.0;
        cfg.stop_new_at_dd = 5.0;
        cfg.partial_close_at_dd = 8.0;
        cfg.close_all_at_dd = 25.0;
        cfg.max_positions = 20;
        cfg.enable_atr_filter = true;
        results.push_back(run_strategy_test<FillUpStrategyV10>("V10 (ATR ON)", tick_file, cfg));
    }

    // Test V12 optimized
    std::cout << "Testing V12 strategies...\n" << std::flush;
    {
        FillUpStrategyV12::Config cfg;
        cfg.survive_pct = 13.0;
        cfg.spacing = 1.0;
        cfg.stop_new_at_dd = 5.0;
        cfg.partial_close_at_dd = 8.0;
        cfg.close_all_at_dd = 25.0;
        cfg.max_positions = 20;
        cfg.enable_atr_filter = false;
        cfg.enable_time_exit = true;
        cfg.max_hold_ticks = 50000;
        results.push_back(run_strategy_test<FillUpStrategyV12>("V12 (time exit)", tick_file, cfg));
    }

    // Sort by ROI
    std::sort(results.begin(), results.end(),
              [](const TestResult& a, const TestResult& b) { return a.roi > b.roi; });

    // Print results
    std::cout << "\n================================================================\n";
    std::cout << "RESULTS (sorted by return)\n";
    std::cout << "================================================================\n\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << std::left << std::setw(30) << "Strategy"
              << std::right << std::setw(12) << "Final$"
              << std::setw(10) << "ROI"
              << std::setw(10) << "MaxDD"
              << std::setw(10) << "Trades" << "\n";
    std::cout << std::string(72, '-') << "\n";

    for (const auto& r : results) {
        std::cout << std::left << std::setw(30) << r.name
                  << std::right << "$" << std::setw(10) << r.final_equity
                  << std::setw(9) << r.roi << "%"
                  << std::setw(9) << r.max_dd << "%"
                  << std::setw(10) << r.total_trades << "\n";
    }

    std::cout << "\n================================================================\n";
    std::cout << "Note: These are GRID strategies (many small trades with TP)\n";
    std::cout << "Different from GRID strategies (hold until margin call)\n";
    std::cout << "================================================================\n";

    return 0;
}
