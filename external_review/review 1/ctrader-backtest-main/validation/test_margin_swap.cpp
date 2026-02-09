/**
 * Unit Tests for MarginManager and SwapManager
 * Validates against MT5 Test E and F results
 *
 * Compile: g++ -std=c++17 -I../include validation/test_margin_swap.cpp -o test_margin_swap
 * Run: ./test_margin_swap
 */

#include "margin_manager.h"
#include "swap_manager.h"
#include <iostream>
#include <iomanip>
#include <cassert>
#include <cmath>

// Test result tracking
int tests_passed = 0;
int tests_failed = 0;

void print_test_header(const std::string& test_name) {
    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "TEST: " << test_name << std::endl;
    std::cout << std::string(70, '=') << std::endl;
}

void assert_close(double actual, double expected, double tolerance, const std::string& message) {
    double diff = std::abs(actual - expected);
    if (diff <= tolerance) {
        std::cout << "  [PASS] " << message << std::endl;
        std::cout << "         Expected: " << std::fixed << std::setprecision(2) << expected
                  << ", Got: " << actual
                  << ", Diff: " << std::setprecision(4) << diff << std::endl;
        tests_passed++;
    } else {
        std::cout << "  [FAIL] " << message << std::endl;
        std::cout << "         Expected: " << std::fixed << std::setprecision(2) << expected
                  << ", Got: " << actual
                  << ", Diff: " << std::setprecision(4) << diff
                  << " (tolerance: " << tolerance << ")" << std::endl;
        tests_failed++;
    }
}

void test_margin_calculation() {
    print_test_header("Margin Calculation - MT5 Test F Validation");

    // Test data from validation/mt5/test_f_margin.csv
    double contract_size = 100000.0;
    int leverage = 500;

    std::cout << "\nValidating against MT5 Test F results:" << std::endl;
    std::cout << "Leverage: 1:" << leverage << std::endl;
    std::cout << "Contract size: " << std::fixed << std::setprecision(0) << contract_size << std::endl;
    std::cout << std::endl;

    // Test case 1: 0.01 lots
    {
        double lot_size = 0.01;
        double price = 1.15958;
        double expected_margin = 2.38;  // From MT5

        double calculated = MarginManager::CalculateMargin(
            lot_size, contract_size, price, leverage
        );

        assert_close(calculated, expected_margin, 0.10,
                    "0.01 lots @ 1.15958");
    }

    // Test case 2: 0.05 lots
    {
        double lot_size = 0.05;
        double price = 1.15958;
        double expected_margin = 11.90;

        double calculated = MarginManager::CalculateMargin(
            lot_size, contract_size, price, leverage
        );

        assert_close(calculated, expected_margin, 0.30,
                    "0.05 lots @ 1.15958");
    }

    // Test case 3: 0.1 lots
    {
        double lot_size = 0.1;
        double price = 1.15960;
        double expected_margin = 23.79;

        double calculated = MarginManager::CalculateMargin(
            lot_size, contract_size, price, leverage
        );

        assert_close(calculated, expected_margin, 0.60,
                    "0.1 lots @ 1.15960");
    }

    // Test case 4: 0.5 lots
    {
        double lot_size = 0.5;
        double price = 1.15958;
        double expected_margin = 118.96;

        double calculated = MarginManager::CalculateMargin(
            lot_size, contract_size, price, leverage
        );

        assert_close(calculated, expected_margin, 3.00,
                    "0.5 lots @ 1.15958");
    }

    // Test case 5: 1.0 lots
    {
        double lot_size = 1.0;
        double price = 1.15958;
        double expected_margin = 237.92;

        double calculated = MarginManager::CalculateMargin(
            lot_size, contract_size, price, leverage
        );

        assert_close(calculated, expected_margin, 6.00,
                    "1.0 lots @ 1.15958");
    }
}

void test_margin_check() {
    print_test_header("Margin Sufficiency Check");

    // Test sufficient margin
    {
        double balance = 10000.0;
        double current_margin = 100.0;
        double required_margin = 50.0;

        bool sufficient = MarginManager::HasSufficientMargin(
            balance, current_margin, required_margin, 100.0
        );

        double margin_level = MarginManager::GetMarginLevel(balance, current_margin + required_margin);

        std::cout << "\nTest: Sufficient margin" << std::endl;
        std::cout << "  Balance: $" << balance << std::endl;
        std::cout << "  Current margin: $" << current_margin << std::endl;
        std::cout << "  Required: $" << required_margin << std::endl;
        std::cout << "  Margin level: " << std::fixed << std::setprecision(2) << margin_level << "%" << std::endl;

        if (sufficient) {
            std::cout << "  [PASS] Margin check returned true (expected)" << std::endl;
            tests_passed++;
        } else {
            std::cout << "  [FAIL] Margin check returned false (expected true)" << std::endl;
            tests_failed++;
        }
    }

    // Test insufficient margin
    {
        double balance = 10000.0;
        double current_margin = 9500.0;
        double required_margin = 1000.0;

        bool sufficient = MarginManager::HasSufficientMargin(
            balance, current_margin, required_margin, 100.0
        );

        double margin_level = MarginManager::GetMarginLevel(balance, current_margin + required_margin);

        std::cout << "\nTest: Insufficient margin" << std::endl;
        std::cout << "  Balance: $" << balance << std::endl;
        std::cout << "  Current margin: $" << current_margin << std::endl;
        std::cout << "  Required: $" << required_margin << std::endl;
        std::cout << "  Margin level: " << std::fixed << std::setprecision(2) << margin_level << "%" << std::endl;

        if (!sufficient) {
            std::cout << "  [PASS] Margin check returned false (expected)" << std::endl;
            tests_passed++;
        } else {
            std::cout << "  [FAIL] Margin check returned true (expected false)" << std::endl;
            tests_failed++;
        }
    }
}

void test_max_lot_size() {
    print_test_header("Maximum Lot Size Calculation");

    double free_margin = 1000.0;
    double contract_size = 100000.0;
    double price = 1.20;
    int leverage = 500;

    double max_lots = MarginManager::GetMaxLotSize(
        free_margin, contract_size, price, leverage, 100.0
    );

    // Verify: If we use this lot size, it should consume all free margin
    double margin_for_max = MarginManager::CalculateMargin(
        max_lots, contract_size, price, leverage
    );

    std::cout << "\nFree margin: $" << free_margin << std::endl;
    std::cout << "Price: " << price << std::endl;
    std::cout << "Leverage: 1:" << leverage << std::endl;
    std::cout << "Max lot size: " << std::fixed << std::setprecision(4) << max_lots << std::endl;
    std::cout << "Margin required: $" << std::setprecision(2) << margin_for_max << std::endl;

    assert_close(margin_for_max, free_margin, 0.01,
                "Margin for max lots matches free margin");
}

void test_swap_timing() {
    print_test_header("Swap Timing - MT5 Test E Validation");

    SwapManager swap_mgr(0);  // Swap at midnight (hour 0)

    // Simulate timestamps
    // Tuesday 2025-12-02 00:01:00
    time_t tuesday_midnight = 1733097660;

    // Tuesday 2025-12-02 23:59:00
    time_t tuesday_evening = 1733183940;

    // Wednesday 2025-12-03 00:01:00
    time_t wednesday_midnight = 1733184060;

    std::cout << "\nTesting swap application timing:" << std::endl;

    // First call on Tuesday midnight - should apply
    bool should_apply_1 = swap_mgr.ShouldApplySwap(tuesday_midnight);
    std::cout << "  Tuesday 00:01: " << (should_apply_1 ? "APPLY" : "SKIP") << std::endl;

    if (should_apply_1) {
        std::cout << "    [PASS] Swap applied on first midnight check" << std::endl;
        tests_passed++;
    } else {
        std::cout << "    [FAIL] Swap should have been applied" << std::endl;
        tests_failed++;
    }

    // Second call same day - should not apply
    bool should_apply_2 = swap_mgr.ShouldApplySwap(tuesday_evening);
    std::cout << "  Tuesday 23:59: " << (should_apply_2 ? "APPLY" : "SKIP") << std::endl;

    if (!should_apply_2) {
        std::cout << "    [PASS] Swap not applied same day" << std::endl;
        tests_passed++;
    } else {
        std::cout << "    [FAIL] Swap should not apply twice same day" << std::endl;
        tests_failed++;
    }

    // Next day - should apply again
    bool should_apply_3 = swap_mgr.ShouldApplySwap(wednesday_midnight);
    std::cout << "  Wednesday 00:01: " << (should_apply_3 ? "APPLY" : "SKIP") << std::endl;

    if (should_apply_3) {
        std::cout << "    [PASS] Swap applied on next day" << std::endl;
        tests_passed++;
    } else {
        std::cout << "    [FAIL] Swap should apply on new day" << std::endl;
        tests_failed++;
    }
}

void test_swap_calculation() {
    print_test_header("Swap Calculation");

    // Test normal day swap
    {
        double lot_size = 0.01;
        double swap_long = -0.5;  // Points
        double swap_short = 0.3;
        double point_value = 0.00001;  // For 5-digit quotes
        double contract_size = 100000.0;

        double swap_buy = SwapManager::CalculateSwap(
            lot_size, true, swap_long, swap_short, point_value, contract_size, 1  // Monday
        );

        std::cout << "\nNormal day (Monday) swap for 0.01 lot BUY:" << std::endl;
        std::cout << "  Swap: $" << std::fixed << std::setprecision(4) << swap_buy << std::endl;

        // Expected: 0.01 × 100000 × -0.5 × 0.00001 = -0.05
        assert_close(swap_buy, -0.05, 0.001, "BUY swap calculation");
    }

    // Test Wednesday triple swap
    {
        double lot_size = 0.01;
        double swap_long = -0.5;
        double swap_short = 0.3;
        double point_value = 0.00001;
        double contract_size = 100000.0;

        double swap_buy = SwapManager::CalculateSwap(
            lot_size, true, swap_long, swap_short, point_value, contract_size, 3  // Wednesday
        );

        std::cout << "\nWednesday (triple) swap for 0.01 lot BUY:" << std::endl;
        std::cout << "  Swap: $" << std::fixed << std::setprecision(4) << swap_buy << std::endl;

        // Expected: 0.01 × 100000 × (-0.5 × 3) × 0.00001 = -0.15
        assert_close(swap_buy, -0.15, 0.001, "Triple swap on Wednesday");
    }
}

int main() {
    std::cout << "\n" << std::string(70, '*') << std::endl;
    std::cout << "MARGIN MANAGER & SWAP MANAGER UNIT TESTS" << std::endl;
    std::cout << "Validating against MT5 Test Results" << std::endl;
    std::cout << std::string(70, '*') << std::endl;

    test_margin_calculation();
    test_margin_check();
    test_max_lot_size();
    test_swap_timing();
    test_swap_calculation();

    // Summary
    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "TEST SUMMARY" << std::endl;
    std::cout << std::string(70, '=') << std::endl;
    std::cout << "Tests passed: " << tests_passed << std::endl;
    std::cout << "Tests failed: " << tests_failed << std::endl;
    std::cout << "Total tests:  " << (tests_passed + tests_failed) << std::endl;

    if (tests_failed == 0) {
        std::cout << "\n✓ ALL TESTS PASSED!" << std::endl;
        std::cout << "\nMarginManager and SwapManager are MT5-validated and ready to use." << std::endl;
    } else {
        std::cout << "\n✗ SOME TESTS FAILED" << std::endl;
        std::cout << "\nPlease review failed tests above." << std::endl;
    }

    std::cout << std::string(70, '=') << std::endl;

    return tests_failed == 0 ? 0 : 1;
}
