/**
 * Edge Case Tests - Tests for unusual/boundary conditions
 * Tests empty data, invalid prices, extreme values, and boundary conditions
 *
 * Build: g++ -O3 -fno-inline -mavx2 -mfma -std=c++17 -static -I include tests/test_edge_cases.cpp -o build/test_edge_cases.exe
 */
#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cmath>
#include <limits>

using namespace backtest;

struct TestResult {
    std::string name;
    bool passed;
    std::string details;
};

void print_result(const TestResult& result) {
    std::cout << (result.passed ? "[PASS]" : "[FAIL]") << " " << result.name;
    if (!result.details.empty()) {
        std::cout << " - " << result.details;
    }
    std::cout << std::endl;
}

// Test 1: Empty date range (no matching ticks)
TestResult test_empty_date_range() {
    TestResult result;
    result.name = "Empty Date Range";

    try {
        TickDataConfig tick_config;
        tick_config.file_path = "C:/Users/user/Documents/ctrader-backtest/validation/Grid/XAUUSD_TICKS_2025.csv";
        tick_config.format = TickDataFormat::MT5_CSV;

        TickBacktestConfig config;
        config.symbol = "XAUUSD";
        config.initial_balance = 10000.0;
        config.contract_size = 100.0;
        config.leverage = 500.0;
        config.pip_size = 0.01;
        // Use a date range with no data (far in the future)
        config.start_date = "2030.01.01";
        config.end_date = "2030.01.02";
        config.tick_data_config = tick_config;
        config.verbose = false;

        TickBasedEngine engine(config);
        int tick_count = 0;

        engine.Run([&tick_count](const Tick& tick, TickBasedEngine& eng) {
            tick_count++;
        });

        auto results = engine.GetResults();

        // Should complete without crashing, with zero ticks processed
        result.passed = (tick_count == 0) && (results.final_balance == 10000.0);
        result.details = "Ticks: " + std::to_string(tick_count) + ", Balance: $" + std::to_string(results.final_balance);

    } catch (const std::exception& e) {
        result.passed = false;
        result.details = std::string("Exception: ") + e.what();
    }

    return result;
}

// Test 2: Very short backtest (1 tick only scenario)
TestResult test_single_tick() {
    TestResult result;
    result.name = "Single Tick Scenario";

    try {
        TickDataConfig tick_config;
        tick_config.file_path = "C:/Users/user/Documents/ctrader-backtest/validation/Grid/XAUUSD_TICKS_2025.csv";
        tick_config.format = TickDataFormat::MT5_CSV;

        TickBacktestConfig config;
        config.symbol = "XAUUSD";
        config.initial_balance = 10000.0;
        config.contract_size = 100.0;
        config.leverage = 500.0;
        config.pip_size = 0.01;
        // Very narrow date range to get minimal ticks
        config.start_date = "2024.12.31";
        config.end_date = "2024.12.31 00:00:01";
        config.tick_data_config = tick_config;
        config.verbose = false;

        TickBasedEngine engine(config);
        int tick_count = 0;

        engine.Run([&tick_count](const Tick& tick, TickBasedEngine& eng) {
            tick_count++;
        });

        auto results = engine.GetResults();
        result.passed = true;  // Just needs to complete without crash
        result.details = "Ticks: " + std::to_string(tick_count);

    } catch (const std::exception& e) {
        result.passed = false;
        result.details = std::string("Exception: ") + e.what();
    }

    return result;
}

// Test 3: Pending order placement and cancellation
TestResult test_pending_orders() {
    TestResult result;
    result.name = "Pending Orders";

    try {
        TickDataConfig tick_config;
        tick_config.file_path = "C:/Users/user/Documents/ctrader-backtest/validation/Grid/XAUUSD_TICKS_2025.csv";
        tick_config.format = TickDataFormat::MT5_CSV;

        TickBacktestConfig config;
        config.symbol = "XAUUSD";
        config.initial_balance = 10000.0;
        config.contract_size = 100.0;
        config.leverage = 500.0;
        config.pip_size = 0.01;
        config.start_date = "2024.12.31";
        config.end_date = "2025.01.02";
        config.tick_data_config = tick_config;
        config.verbose = false;

        TickBasedEngine engine(config);
        int orders_placed = 0;
        int orders_cancelled = 0;
        int orders_triggered = 0;

        engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
            // On first tick, place some pending orders
            if (orders_placed == 0) {
                // Place a BUY_LIMIT below current price (should trigger on dip)
                eng.PlacePendingOrder(PendingOrderType::BUY_LIMIT, tick.bid - 5.0, 0.01, 0, 0);
                // Place a SELL_LIMIT above current price (should trigger on spike)
                eng.PlacePendingOrder(PendingOrderType::SELL_LIMIT, tick.ask + 5.0, 0.01, 0, 0);
                // Place a BUY_STOP above current price
                int order_id = eng.PlacePendingOrder(PendingOrderType::BUY_STOP, tick.ask + 10.0, 0.01, 0, 0);
                orders_placed = 3;

                // Cancel one order
                if (eng.CancelPendingOrder(order_id)) {
                    orders_cancelled = 1;
                }
            }

            // Track how many orders got triggered
            orders_triggered = eng.GetClosedTrades().size();
        });

        auto results = engine.GetResults();
        result.passed = (orders_placed == 3) && (orders_cancelled == 1);
        result.details = "Placed: " + std::to_string(orders_placed) +
                        ", Cancelled: " + std::to_string(orders_cancelled) +
                        ", Triggered: " + std::to_string(orders_triggered);

    } catch (const std::exception& e) {
        result.passed = false;
        result.details = std::string("Exception: ") + e.what();
    }

    return result;
}

// Test 4: Configurable stop-out level
TestResult test_configurable_stopout() {
    TestResult result;
    result.name = "Configurable Stop-Out";

    try {
        TickDataConfig tick_config;
        tick_config.file_path = "C:/Users/user/Documents/ctrader-backtest/validation/Grid/XAUUSD_TICKS_2025.csv";
        tick_config.format = TickDataFormat::MT5_CSV;

        TickBacktestConfig config;
        config.symbol = "XAUUSD";
        config.initial_balance = 10000.0;
        config.contract_size = 100.0;
        config.leverage = 500.0;
        config.pip_size = 0.01;
        config.stop_out_level = 50.0;  // Set to 50% instead of default 20%
        config.start_date = "2024.12.31";
        config.end_date = "2025.01.02";
        config.tick_data_config = tick_config;
        config.verbose = false;

        TickBasedEngine engine(config);

        engine.Run([](const Tick& tick, TickBasedEngine& eng) {
            // Just run without trading
        });

        auto results = engine.GetResults();
        result.passed = true;  // Config should be accepted without error
        result.details = "Stop-out level: 50% (custom)";

    } catch (const std::exception& e) {
        result.passed = false;
        result.details = std::string("Exception: ") + e.what();
    }

    return result;
}

// Test 5: Maximum positions stress test
TestResult test_max_positions() {
    TestResult result;
    result.name = "Max Positions Stress";

    try {
        TickDataConfig tick_config;
        tick_config.file_path = "C:/Users/user/Documents/ctrader-backtest/validation/Grid/XAUUSD_TICKS_2025.csv";
        tick_config.format = TickDataFormat::MT5_CSV;

        TickBacktestConfig config;
        config.symbol = "XAUUSD";
        config.initial_balance = 1000000.0;  // Large balance to allow many positions
        config.contract_size = 100.0;
        config.leverage = 500.0;
        config.pip_size = 0.01;
        config.start_date = "2024.12.31";
        config.end_date = "2025.01.02";
        config.tick_data_config = tick_config;
        config.verbose = false;

        TickBasedEngine engine(config);
        size_t max_positions = 0;
        bool opened_positions = false;

        engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
            // Open many small positions on first tick
            if (!opened_positions) {
                for (int i = 0; i < 50; i++) {
                    eng.OpenMarketOrder(TradeDirection::BUY, 0.01, 0, 0);
                }
                opened_positions = true;
            }
            max_positions = std::max(max_positions, eng.GetOpenPositions().size());
        });

        auto results = engine.GetResults();
        result.passed = (max_positions >= 50);
        result.details = "Max open positions: " + std::to_string(max_positions);

    } catch (const std::exception& e) {
        result.passed = false;
        result.details = std::string("Exception: ") + e.what();
    }

    return result;
}

// Test 6: Zero lot size handling (should be rejected or handled gracefully)
TestResult test_zero_lot_size() {
    TestResult result;
    result.name = "Zero Lot Size";

    try {
        TickDataConfig tick_config;
        tick_config.file_path = "C:/Users/user/Documents/ctrader-backtest/validation/Grid/XAUUSD_TICKS_2025.csv";
        tick_config.format = TickDataFormat::MT5_CSV;

        TickBacktestConfig config;
        config.symbol = "XAUUSD";
        config.initial_balance = 10000.0;
        config.contract_size = 100.0;
        config.leverage = 500.0;
        config.pip_size = 0.01;
        config.start_date = "2024.12.31";
        config.end_date = "2025.01.02";
        config.tick_data_config = tick_config;
        config.verbose = false;

        TickBasedEngine engine(config);
        bool trade_opened = false;

        engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
            if (!trade_opened) {
                // Try to open with zero lot size (should be handled gracefully)
                Trade* trade = eng.OpenMarketOrder(TradeDirection::BUY, 0.0, 0, 0);
                trade_opened = (trade != nullptr);
            }
        });

        // Engine should not crash, regardless of whether it accepts zero lots
        result.passed = true;
        result.details = "Handled without crash";

    } catch (const std::exception& e) {
        result.passed = false;
        result.details = std::string("Exception: ") + e.what();
    }

    return result;
}

// Test 7: Large balance (check for overflow issues)
TestResult test_large_balance() {
    TestResult result;
    result.name = "Large Balance";

    try {
        TickDataConfig tick_config;
        tick_config.file_path = "C:/Users/user/Documents/ctrader-backtest/validation/Grid/XAUUSD_TICKS_2025.csv";
        tick_config.format = TickDataFormat::MT5_CSV;

        TickBacktestConfig config;
        config.symbol = "XAUUSD";
        config.initial_balance = 1e12;  // 1 trillion
        config.contract_size = 100.0;
        config.leverage = 500.0;
        config.pip_size = 0.01;
        config.start_date = "2024.12.31";
        config.end_date = "2025.01.02";
        config.tick_data_config = tick_config;
        config.verbose = false;

        TickBasedEngine engine(config);

        engine.Run([](const Tick& tick, TickBasedEngine& eng) {
            // No trading, just test large balance handling
        });

        auto results = engine.GetResults();
        result.passed = (results.final_balance == 1e12) && !std::isnan(results.final_balance) && !std::isinf(results.final_balance);
        result.details = "Balance preserved: " + std::to_string(results.final_balance);

    } catch (const std::exception& e) {
        result.passed = false;
        result.details = std::string("Exception: ") + e.what();
    }

    return result;
}

// Test 8: Negative P/L scenario (ensure no overflow)
TestResult test_negative_pnl() {
    TestResult result;
    result.name = "Negative P/L Handling";

    try {
        TickDataConfig tick_config;
        tick_config.file_path = "C:/Users/user/Documents/ctrader-backtest/validation/Grid/XAUUSD_TICKS_2025.csv";
        tick_config.format = TickDataFormat::MT5_CSV;

        TickBacktestConfig config;
        config.symbol = "XAUUSD";
        config.initial_balance = 100.0;  // Very small balance
        config.contract_size = 100.0;
        config.leverage = 500.0;
        config.pip_size = 0.01;
        config.stop_out_level = 20.0;
        config.start_date = "2024.12.31";
        config.end_date = "2025.01.05";
        config.tick_data_config = tick_config;
        config.verbose = false;

        TickBasedEngine engine(config);
        bool opened = false;

        engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
            // Open a position that will likely cause losses
            if (!opened) {
                eng.OpenMarketOrder(TradeDirection::BUY, 0.1, 0, 0);
                opened = true;
            }
        });

        auto results = engine.GetResults();
        // Should either stop out or complete with some balance
        result.passed = !std::isnan(results.final_balance) && !std::isinf(results.final_balance);
        result.details = "Final balance: $" + std::to_string(results.final_balance) +
                        ", Stop-out: " + (results.stop_out_occurred ? "yes" : "no");

    } catch (const std::exception& e) {
        result.passed = false;
        result.details = std::string("Exception: ") + e.what();
    }

    return result;
}

int main() {
    std::cout << "=== Edge Case Tests ===" << std::endl;
    std::cout << "Testing boundary conditions and unusual scenarios" << std::endl;
    std::cout << std::string(60, '-') << std::endl << std::endl;

    std::vector<TestResult> results;

    std::cout << "Running test 1/8: Empty Date Range..." << std::endl;
    results.push_back(test_empty_date_range());
    print_result(results.back());

    std::cout << "Running test 2/8: Single Tick..." << std::endl;
    results.push_back(test_single_tick());
    print_result(results.back());

    std::cout << "Running test 3/8: Pending Orders..." << std::endl;
    results.push_back(test_pending_orders());
    print_result(results.back());

    std::cout << "Running test 4/8: Configurable Stop-Out..." << std::endl;
    results.push_back(test_configurable_stopout());
    print_result(results.back());

    std::cout << "Running test 5/8: Max Positions..." << std::endl;
    results.push_back(test_max_positions());
    print_result(results.back());

    std::cout << "Running test 6/8: Zero Lot Size..." << std::endl;
    results.push_back(test_zero_lot_size());
    print_result(results.back());

    std::cout << "Running test 7/8: Large Balance..." << std::endl;
    results.push_back(test_large_balance());
    print_result(results.back());

    std::cout << "Running test 8/8: Negative P/L..." << std::endl;
    results.push_back(test_negative_pnl());
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
        std::cout << "\n*** ALL EDGE CASE TESTS PASSED ***" << std::endl;
        return 0;
    } else {
        std::cout << "\n*** SOME TESTS FAILED ***" << std::endl;
        return 1;
    }
}
