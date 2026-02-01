/**
 * Comprehensive Strategy Tests
 * Tests both FillUpOscillation and CombinedJu strategies with various configurations
 *
 * Build: g++ -O3 -fno-inline -mavx2 -mfma -std=c++17 -static -I include tests/test_strategies_comprehensive.cpp -o build/test_strategies_comprehensive.exe
 */
#include "../include/fill_up_oscillation.h"
#include "../include/strategy_combined_ju.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>

using namespace backtest;

struct TestResult {
    std::string name;
    bool passed;
    double final_balance;
    int total_trades;
    double max_drawdown;
    std::string error_msg;
};

// Create standard XAUUSD config
TickBacktestConfig create_xauusd_config(const std::string& start_date, const std::string& end_date) {
    TickDataConfig tick_config;
    tick_config.file_path = "C:/Users/user/Documents/ctrader-backtest/validation/Grid/XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;

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
    config.start_date = start_date;
    config.end_date = end_date;
    config.tick_data_config = tick_config;
    config.verbose = false;
    return config;
}

// Test 1: FillUpOscillation with default parameters
TestResult test_fillup_default() {
    TestResult result;
    result.name = "FillUp Default Params";

    try {
        auto config = create_xauusd_config("2024.12.31", "2025.01.05");
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

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        result.final_balance = results.final_balance;
        result.total_trades = results.total_trades;
        result.max_drawdown = results.max_drawdown_pct;
        result.passed = (results.final_balance > 0);

    } catch (const std::exception& e) {
        result.passed = false;
        result.error_msg = e.what();
    }

    return result;
}

// Test 2: FillUpOscillation with aggressive parameters
TestResult test_fillup_aggressive() {
    TestResult result;
    result.name = "FillUp Aggressive";

    try {
        auto config = create_xauusd_config("2024.12.31", "2025.01.05");
        TickBasedEngine engine(config);

        FillUpOscillation strategy(
            8.0,    // survive_pct - lower = more aggressive
            1.0,    // base_spacing - tighter
            0.01,   // min_volume
            10.0,   // max_volume
            100.0,  // contract_size
            500.0,  // leverage
            FillUpOscillation::ADAPTIVE_SPACING,
            0.1,    // antifragile_scale
            30.0,   // max_spacing_mult
            2.0     // lookback_hours - shorter
        );

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        result.final_balance = results.final_balance;
        result.total_trades = results.total_trades;
        result.max_drawdown = results.max_drawdown_pct;
        result.passed = (results.final_balance > 0);

    } catch (const std::exception& e) {
        result.passed = false;
        result.error_msg = e.what();
    }

    return result;
}

// Test 3: FillUpOscillation with conservative parameters
TestResult test_fillup_conservative() {
    TestResult result;
    result.name = "FillUp Conservative";

    try {
        auto config = create_xauusd_config("2024.12.31", "2025.01.05");
        TickBasedEngine engine(config);

        FillUpOscillation strategy(
            20.0,   // survive_pct - higher = more conservative
            3.0,    // base_spacing - wider
            0.01,   // min_volume
            10.0,   // max_volume
            100.0,  // contract_size
            500.0,  // leverage
            FillUpOscillation::ADAPTIVE_SPACING,
            0.05,   // antifragile_scale - lower
            30.0,   // max_spacing_mult
            8.0     // lookback_hours - longer
        );

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        result.final_balance = results.final_balance;
        result.total_trades = results.total_trades;
        result.max_drawdown = results.max_drawdown_pct;
        result.passed = (results.final_balance > 0);

    } catch (const std::exception& e) {
        result.passed = false;
        result.error_msg = e.what();
    }

    return result;
}

// Test 4: CombinedJu with default config
TestResult test_combined_default() {
    TestResult result;
    result.name = "CombinedJu Default";

    try {
        auto config = create_xauusd_config("2024.12.31", "2025.01.05");
        TickBasedEngine engine(config);

        StrategyCombinedJu::Config ju_config;
        ju_config.survive_pct = 12.0;
        ju_config.base_spacing = 1.0;
        ju_config.min_volume = 0.01;
        ju_config.max_volume = 10.0;
        ju_config.contract_size = 100.0;
        ju_config.leverage = 500.0;

        StrategyCombinedJu strategy(ju_config);

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        result.final_balance = results.final_balance;
        result.total_trades = results.total_trades;
        result.max_drawdown = results.max_drawdown_pct;
        result.passed = (results.final_balance > 0);

    } catch (const std::exception& e) {
        result.passed = false;
        result.error_msg = e.what();
    }

    return result;
}

// Test 5: Engine with no trades (no strategy callbacks)
TestResult test_engine_no_trades() {
    TestResult result;
    result.name = "Engine No Trades";

    try {
        auto config = create_xauusd_config("2024.12.31", "2025.01.02");
        TickBasedEngine engine(config);

        int tick_count = 0;
        engine.Run([&tick_count](const Tick& tick, TickBasedEngine& eng) {
            tick_count++;
            // No trading - just count ticks
        });

        auto results = engine.GetResults();
        result.final_balance = results.final_balance;
        result.total_trades = results.total_trades;
        result.max_drawdown = results.max_drawdown_pct;

        // Should have balance unchanged and zero trades
        result.passed = (results.final_balance == 10000.0) &&
                       (results.total_trades == 0) &&
                       (tick_count > 0);

        if (!result.passed) {
            result.error_msg = "Expected balance=10000, trades=0, ticks>0. Got balance=" +
                              std::to_string(results.final_balance) + ", trades=" +
                              std::to_string(results.total_trades) + ", ticks=" +
                              std::to_string(tick_count);
        }

    } catch (const std::exception& e) {
        result.passed = false;
        result.error_msg = e.what();
    }

    return result;
}

// Test 6: Engine with single day
TestResult test_single_day() {
    TestResult result;
    result.name = "Single Day Test";

    try {
        auto config = create_xauusd_config("2025.01.02", "2025.01.02");
        TickBasedEngine engine(config);

        FillUpOscillation strategy(
            13.0, 1.5, 0.01, 10.0, 100.0, 500.0,
            FillUpOscillation::ADAPTIVE_SPACING, 0.1, 30.0, 4.0
        );

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        result.final_balance = results.final_balance;
        result.total_trades = results.total_trades;
        result.max_drawdown = results.max_drawdown_pct;
        result.passed = true;  // Just checking it doesn't crash

    } catch (const std::exception& e) {
        result.passed = false;
        result.error_msg = e.what();
    }

    return result;
}

// Test 7: Week-long test for realistic behavior
TestResult test_one_week() {
    TestResult result;
    result.name = "One Week Test";

    try {
        auto config = create_xauusd_config("2025.01.06", "2025.01.12");
        TickBasedEngine engine(config);

        FillUpOscillation strategy(
            13.0, 1.5, 0.01, 10.0, 100.0, 500.0,
            FillUpOscillation::ADAPTIVE_SPACING, 0.1, 30.0, 4.0
        );

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        result.final_balance = results.final_balance;
        result.total_trades = results.total_trades;
        result.max_drawdown = results.max_drawdown_pct;
        result.passed = (results.final_balance > 0);

    } catch (const std::exception& e) {
        result.passed = false;
        result.error_msg = e.what();
    }

    return result;
}

void print_result(const TestResult& result) {
    std::cout << (result.passed ? "[PASS]" : "[FAIL]") << " " << result.name;
    if (result.passed) {
        std::cout << " - Balance: $" << std::fixed << std::setprecision(2) << result.final_balance
                  << ", Trades: " << result.total_trades
                  << ", DD: " << result.max_drawdown << "%";
    } else {
        std::cout << " - Error: " << result.error_msg;
    }
    std::cout << std::endl;
}

int main() {
    std::cout << "=== Comprehensive Strategy Tests ===" << std::endl;
    std::cout << "Testing FillUpOscillation and CombinedJu strategies" << std::endl;
    std::cout << std::string(60, '-') << std::endl << std::endl;

    std::vector<TestResult> results;

    // Run all tests
    std::cout << "Running test 1/7: FillUp Default..." << std::endl;
    results.push_back(test_fillup_default());
    print_result(results.back());

    std::cout << "Running test 2/7: FillUp Aggressive..." << std::endl;
    results.push_back(test_fillup_aggressive());
    print_result(results.back());

    std::cout << "Running test 3/7: FillUp Conservative..." << std::endl;
    results.push_back(test_fillup_conservative());
    print_result(results.back());

    std::cout << "Running test 4/7: CombinedJu Default..." << std::endl;
    results.push_back(test_combined_default());
    print_result(results.back());

    std::cout << "Running test 5/7: Engine No Trades..." << std::endl;
    results.push_back(test_engine_no_trades());
    print_result(results.back());

    std::cout << "Running test 6/7: Single Day..." << std::endl;
    results.push_back(test_single_day());
    print_result(results.back());

    std::cout << "Running test 7/7: One Week..." << std::endl;
    results.push_back(test_one_week());
    print_result(results.back());

    // Summary
    std::cout << std::endl << std::string(60, '-') << std::endl;
    int passed = 0, failed = 0;
    for (const auto& r : results) {
        if (r.passed) passed++;
        else failed++;
    }

    std::cout << "SUMMARY: " << passed << " passed, " << failed << " failed" << std::endl;

    if (failed == 0) {
        std::cout << "\n*** ALL TESTS PASSED ***" << std::endl;
        return 0;
    } else {
        std::cout << "\n*** SOME TESTS FAILED ***" << std::endl;
        return 1;
    }
}
