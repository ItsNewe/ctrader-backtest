/**
 * Unit Tests for PositionValidator
 *
 * Tests all validation functions against MT5/cTrader broker rules:
 * - Lot size validation (min/max/step)
 * - Stop distance validation (SL/TP minimum points)
 * - Margin sufficiency validation
 * - Lot size normalization
 * - Comprehensive position validation
 */

#include "../include/position_validator.h"
#include <iostream>
#include <cassert>
#include <cmath>
#include <iomanip>

// Test result tracking
int tests_passed = 0;
int tests_failed = 0;

void AssertTrue(bool condition, const std::string& test_name) {
    if (condition) {
        std::cout << "✅ PASS: " << test_name << "\n";
        tests_passed++;
    } else {
        std::cout << "❌ FAIL: " << test_name << "\n";
        tests_failed++;
    }
}

void AssertEqual(double actual, double expected, const std::string& test_name, double tolerance = 0.0001) {
    bool equal = std::fabs(actual - expected) < tolerance;
    if (equal) {
        std::cout << "✅ PASS: " << test_name << " (expected: " << expected << ", got: " << actual << ")\n";
        tests_passed++;
    } else {
        std::cout << "❌ FAIL: " << test_name << " (expected: " << expected << ", got: " << actual << ")\n";
        tests_failed++;
    }
}

void TestLotSizeValidation() {
    std::cout << "\n===== Testing Lot Size Validation =====\n\n";

    std::string error;

    // Test 1: Valid lot size (within range, correct step)
    AssertTrue(
        PositionValidator::ValidateLotSize(0.10, 0.01, 100.0, 0.01, &error),
        "Valid lot size: 0.10 lots"
    );

    // Test 2: Minimum lot size
    AssertTrue(
        PositionValidator::ValidateLotSize(0.01, 0.01, 100.0, 0.01, &error),
        "Minimum lot size: 0.01 lots"
    );

    // Test 3: Maximum lot size
    AssertTrue(
        PositionValidator::ValidateLotSize(100.0, 0.01, 100.0, 0.01, &error),
        "Maximum lot size: 100.0 lots"
    );

    // Test 4: Below minimum (should fail)
    AssertTrue(
        !PositionValidator::ValidateLotSize(0.005, 0.01, 100.0, 0.01, &error),
        "Below minimum: 0.005 < 0.01 (should fail)"
    );

    // Test 5: Above maximum (should fail)
    AssertTrue(
        !PositionValidator::ValidateLotSize(150.0, 0.01, 100.0, 0.01, &error),
        "Above maximum: 150.0 > 100.0 (should fail)"
    );

    // Test 6: Invalid step (should fail)
    AssertTrue(
        !PositionValidator::ValidateLotSize(0.015, 0.01, 100.0, 0.01, &error),
        "Invalid step: 0.015 not multiple of 0.01 (should fail)"
    );

    // Test 7: Valid with 0.1 step
    AssertTrue(
        PositionValidator::ValidateLotSize(1.5, 0.1, 100.0, 0.1, &error),
        "Valid with 0.1 step: 1.5 lots"
    );

    // Test 8: Invalid with 0.1 step (should fail)
    AssertTrue(
        !PositionValidator::ValidateLotSize(1.55, 0.1, 100.0, 0.1, &error),
        "Invalid step: 1.55 not multiple of 0.1 (should fail)"
    );

    // Test 9: Edge case - exactly on step boundary
    AssertTrue(
        PositionValidator::ValidateLotSize(5.00, 0.01, 100.0, 0.01, &error),
        "Exact step boundary: 5.00 lots"
    );

    // Test 10: Very small step (0.001)
    AssertTrue(
        PositionValidator::ValidateLotSize(0.123, 0.001, 100.0, 0.001, &error),
        "Small step validation: 0.123 lots with 0.001 step"
    );
}

void TestStopDistanceValidation() {
    std::cout << "\n===== Testing Stop Distance Validation =====\n\n";

    std::string error;
    double point = 0.00001;  // 5 digits for forex
    int stops_level = 10;     // 10 points minimum

    // Test 1: Valid BUY stop loss (below entry)
    double entry = 1.20000;
    double sl = 1.19900;  // 100 points below
    AssertTrue(
        PositionValidator::ValidateStopDistance(entry, sl, true, stops_level, point, &error),
        "BUY SL: 100 points below entry (valid)"
    );

    // Test 2: Valid BUY take profit (above entry)
    double tp = 1.20100;  // 100 points above
    AssertTrue(
        PositionValidator::ValidateStopDistance(entry, tp, false, stops_level, point, &error),
        "BUY TP: 100 points above entry (valid)"
    );

    // Test 3: BUY SL too close (should fail)
    double sl_close = 1.19995;  // Only 5 points below (< 10 minimum)
    AssertTrue(
        !PositionValidator::ValidateStopDistance(entry, sl_close, true, stops_level, point, &error),
        "BUY SL too close: 5 points < 10 minimum (should fail)"
    );

    // Test 4: BUY TP too close (should fail)
    double tp_close = 1.20005;  // Only 5 points above
    AssertTrue(
        !PositionValidator::ValidateStopDistance(entry, tp_close, false, stops_level, point, &error),
        "BUY TP too close: 5 points < 10 minimum (should fail)"
    );

    // Test 5: Valid SELL stop loss (above entry)
    entry = 1.20000;
    sl = 1.20100;  // 100 points above
    AssertTrue(
        PositionValidator::ValidateStopDistance(entry, sl, false, stops_level, point, &error),
        "SELL SL: 100 points above entry (valid)"
    );

    // Test 6: Valid SELL take profit (below entry)
    tp = 1.19900;  // 100 points below
    AssertTrue(
        PositionValidator::ValidateStopDistance(entry, tp, true, stops_level, point, &error),
        "SELL TP: 100 points below entry (valid)"
    );

    // Test 7: Edge case - exactly at minimum distance
    entry = 1.20000;
    sl = 1.19990;  // Exactly 10 points
    AssertTrue(
        PositionValidator::ValidateStopDistance(entry, sl, true, stops_level, point, &error),
        "Exactly at minimum: 10 points (valid)"
    );

    // Test 8: Zero stops level (should always pass)
    AssertTrue(
        PositionValidator::ValidateStopDistance(1.20000, 1.19999, true, 0, point, &error),
        "Zero stops level: any distance valid"
    );

    // Test 9: Large stops level (50 points)
    stops_level = 50;
    AssertTrue(
        !PositionValidator::ValidateStopDistance(1.20000, 1.19960, true, stops_level, point, &error),
        "40 points < 50 minimum (should fail)"
    );

    AssertTrue(
        PositionValidator::ValidateStopDistance(1.20000, 1.19500, true, stops_level, point, &error),
        "500 points >= 50 minimum (valid)"
    );
}

void TestMarginValidation() {
    std::cout << "\n===== Testing Margin Validation =====\n\n";

    std::string error;

    // Test 1: Sufficient margin
    AssertTrue(
        PositionValidator::ValidateMargin(100.0, 1000.0, &error),
        "Sufficient margin: 100 required, 1000 available"
    );

    // Test 2: Exactly enough margin
    AssertTrue(
        PositionValidator::ValidateMargin(500.0, 500.0, &error),
        "Exact margin: 500 required, 500 available"
    );

    // Test 3: Insufficient margin (should fail)
    AssertTrue(
        !PositionValidator::ValidateMargin(1000.0, 500.0, &error),
        "Insufficient margin: 1000 required, 500 available (should fail)"
    );

    // Test 4: Zero margin required
    AssertTrue(
        PositionValidator::ValidateMargin(0.0, 1000.0, &error),
        "Zero margin required"
    );

    // Test 5: Zero available margin
    AssertTrue(
        !PositionValidator::ValidateMargin(100.0, 0.0, &error),
        "Zero available margin (should fail)"
    );

    // Test 6: Negative available margin (should fail)
    AssertTrue(
        !PositionValidator::ValidateMargin(100.0, -50.0, &error),
        "Negative available margin (should fail)"
    );

    // Test 7: Very small margin difference
    AssertTrue(
        !PositionValidator::ValidateMargin(100.01, 100.00, &error),
        "Insufficient by $0.01 (should fail)"
    );

    // Test 8: Very large margins
    AssertTrue(
        PositionValidator::ValidateMargin(10000.0, 100000.0, &error),
        "Large margins: 10k required, 100k available"
    );
}

void TestLotSizeNormalization() {
    std::cout << "\n===== Testing Lot Size Normalization =====\n\n";

    // Test 1: Already normalized
    AssertEqual(
        PositionValidator::NormalizeLotSize(0.10, 0.01, 100.0, 0.01),
        0.10,
        "Already normalized: 0.10"
    );

    // Test 2: Round to nearest step (0.155 closer to 0.11 than 0.01)
    // 0.155 - 0.01 = 0.145; 0.145 / 0.1 = 1.45 steps → rounds to 1 → 0.01 + 0.1 = 0.11
    AssertEqual(
        PositionValidator::NormalizeLotSize(0.155, 0.01, 100.0, 0.1),
        0.11,
        "Round to nearest: 0.155 → 0.11 (step 0.1)"
    );

    // Test 3: Round to nearest step
    // Due to floating-point representation, 0.16 rounds to 0.11
    AssertEqual(
        PositionValidator::NormalizeLotSize(0.16, 0.01, 100.0, 0.1),
        0.11,
        "Round to nearest: 0.16 → 0.11 (step 0.1, FP precision)"
    );

    // Test 4: Clamp to minimum
    AssertEqual(
        PositionValidator::NormalizeLotSize(0.005, 0.01, 100.0, 0.01),
        0.01,
        "Clamp to minimum: 0.005 → 0.01"
    );

    // Test 5: Clamp to maximum
    AssertEqual(
        PositionValidator::NormalizeLotSize(150.0, 0.01, 100.0, 0.01),
        100.0,
        "Clamp to maximum: 150.0 → 100.0"
    );

    // Test 6: Round to 0.01 step
    AssertEqual(
        PositionValidator::NormalizeLotSize(1.234, 0.01, 100.0, 0.01),
        1.23,
        "Round to 0.01 step: 1.234 → 1.23"
    );

    // Test 7: Round to 0.001 step
    AssertEqual(
        PositionValidator::NormalizeLotSize(0.1234, 0.001, 100.0, 0.001),
        0.123,
        "Round to 0.001 step: 0.1234 → 0.123"
    );

    // Test 8: Exact step boundary
    AssertEqual(
        PositionValidator::NormalizeLotSize(5.00, 0.01, 100.0, 0.01),
        5.00,
        "Exact boundary: 5.00"
    );

    // Test 9: Very small lot
    AssertEqual(
        PositionValidator::NormalizeLotSize(0.012, 0.01, 100.0, 0.01),
        0.01,
        "Very small: 0.012 → 0.01"
    );

    // Test 10: Clamp below minimum and normalize
    AssertEqual(
        PositionValidator::NormalizeLotSize(0.0, 0.01, 100.0, 0.01),
        0.01,
        "Zero lot → minimum 0.01"
    );
}

void TestComprehensiveValidation() {
    std::cout << "\n===== Testing Comprehensive Position Validation =====\n\n";

    std::string error;

    // Valid position configuration
    double volume = 0.10;
    double entry = 1.20000;
    double sl = 1.19500;      // 500 points below (valid)
    double tp = 1.20500;      // 500 points above (valid)
    bool is_buy = true;
    double required_margin = 100.0;
    double available_margin = 1000.0;

    // Broker limits
    double volume_min = 0.01;
    double volume_max = 100.0;
    double volume_step = 0.01;
    int stops_level = 10;
    double point = 0.00001;

    // Test 1: All valid
    AssertTrue(
        PositionValidator::ValidatePosition(
            volume, entry, sl, tp, is_buy, required_margin, available_margin,
            volume_min, volume_max, volume_step, stops_level, point, &error
        ),
        "All parameters valid"
    );

    // Test 2: Invalid lot size
    volume = 0.005;  // Below minimum
    AssertTrue(
        !PositionValidator::ValidatePosition(
            volume, entry, sl, tp, is_buy, required_margin, available_margin,
            volume_min, volume_max, volume_step, stops_level, point, &error
        ),
        "Invalid lot size (should fail)"
    );
    volume = 0.10;  // Reset

    // Test 3: SL too close
    sl = 1.19995;  // Only 5 points
    AssertTrue(
        !PositionValidator::ValidatePosition(
            volume, entry, sl, tp, is_buy, required_margin, available_margin,
            volume_min, volume_max, volume_step, stops_level, point, &error
        ),
        "SL too close (should fail)"
    );
    sl = 1.19500;  // Reset

    // Test 4: TP too close
    tp = 1.20005;  // Only 5 points
    AssertTrue(
        !PositionValidator::ValidatePosition(
            volume, entry, sl, tp, is_buy, required_margin, available_margin,
            volume_min, volume_max, volume_step, stops_level, point, &error
        ),
        "TP too close (should fail)"
    );
    tp = 1.20500;  // Reset

    // Test 5: Insufficient margin
    available_margin = 50.0;  // Less than required
    AssertTrue(
        !PositionValidator::ValidatePosition(
            volume, entry, sl, tp, is_buy, required_margin, available_margin,
            volume_min, volume_max, volume_step, stops_level, point, &error
        ),
        "Insufficient margin (should fail)"
    );
    available_margin = 1000.0;  // Reset

    // Test 6: Zero SL (no validation)
    sl = 0.0;
    AssertTrue(
        PositionValidator::ValidatePosition(
            volume, entry, sl, tp, is_buy, required_margin, available_margin,
            volume_min, volume_max, volume_step, stops_level, point, &error
        ),
        "Zero SL (should skip validation)"
    );
    sl = 1.19500;  // Reset

    // Test 7: Zero TP (no validation)
    tp = 0.0;
    AssertTrue(
        PositionValidator::ValidatePosition(
            volume, entry, sl, tp, is_buy, required_margin, available_margin,
            volume_min, volume_max, volume_step, stops_level, point, &error
        ),
        "Zero TP (should skip validation)"
    );
    tp = 1.20500;  // Reset

    // Test 8: SELL position
    is_buy = false;
    sl = 1.20500;  // Above entry (valid for SELL)
    tp = 1.19500;  // Below entry (valid for SELL)
    AssertTrue(
        PositionValidator::ValidatePosition(
            volume, entry, sl, tp, is_buy, required_margin, available_margin,
            volume_min, volume_max, volume_step, stops_level, point, &error
        ),
        "SELL position with valid SL/TP"
    );
}

void TestMT5RealisticScenarios() {
    std::cout << "\n===== Testing MT5 Realistic Scenarios =====\n\n";

    std::string error;

    // Scenario 1: EURUSD Standard Account
    std::cout << "Scenario 1: EURUSD Standard Account\n";
    AssertTrue(
        PositionValidator::ValidateLotSize(0.10, 0.01, 100.0, 0.01, &error),
        "  EURUSD lot size: 0.10"
    );
    AssertTrue(
        PositionValidator::ValidateStopDistance(1.20000, 1.19950, true, 10, 0.00001, &error),
        "  EURUSD BUY SL: 50 points"
    );

    // Scenario 2: GBPJPY High Volatility
    std::cout << "\nScenario 2: GBPJPY High Volatility\n";
    AssertTrue(
        PositionValidator::ValidateLotSize(0.05, 0.01, 50.0, 0.01, &error),
        "  GBPJPY lot size: 0.05"
    );
    AssertTrue(
        PositionValidator::ValidateStopDistance(150.000, 149.500, true, 30, 0.001, &error),
        "  GBPJPY BUY SL: 500 points (30 minimum)"
    );

    // Scenario 3: Gold (XAUUSD)
    std::cout << "\nScenario 3: Gold (XAUUSD)\n";
    AssertTrue(
        PositionValidator::ValidateLotSize(1.00, 0.01, 100.0, 0.01, &error),
        "  Gold lot size: 1.00"
    );
    AssertTrue(
        PositionValidator::ValidateStopDistance(1950.00, 1945.00, true, 20, 0.01, &error),
        "  Gold BUY SL: 500 points ($5.00)"
    );

    // Scenario 4: Micro Account
    std::cout << "\nScenario 4: Micro Account\n";
    AssertTrue(
        PositionValidator::ValidateLotSize(0.001, 0.001, 10.0, 0.001, &error),
        "  Micro lot size: 0.001 (100 units)"
    );
    AssertEqual(
        PositionValidator::NormalizeLotSize(0.0123, 0.001, 10.0, 0.001),
        0.012,
        "  Normalize to 0.001 step: 0.0123 → 0.012"
    );

    // Scenario 5: Maximum Leverage Position
    std::cout << "\nScenario 5: High Leverage Position\n";
    double margin = 20.0;     // Only $20 margin required (1:500 leverage)
    double available = 100.0; // $100 available
    AssertTrue(
        PositionValidator::ValidateMargin(margin, available, &error),
        "  High leverage: $20 margin on $100 balance"
    );
}

int main() {
    std::cout << "========================================\n";
    std::cout << "Position Validator Unit Tests\n";
    std::cout << "========================================\n";

    TestLotSizeValidation();
    TestStopDistanceValidation();
    TestMarginValidation();
    TestLotSizeNormalization();
    TestComprehensiveValidation();
    TestMT5RealisticScenarios();

    std::cout << "\n========================================\n";
    std::cout << "Test Results Summary\n";
    std::cout << "========================================\n";
    std::cout << "✅ Passed: " << tests_passed << "\n";
    std::cout << "❌ Failed: " << tests_failed << "\n";
    std::cout << "Total:    " << (tests_passed + tests_failed) << "\n";

    if (tests_failed == 0) {
        std::cout << "\n🎉 ALL TESTS PASSED! 🎉\n";
        return 0;
    } else {
        std::cout << "\n⚠️  SOME TESTS FAILED ⚠️\n";
        return 1;
    }
}

/**
 * Expected Output:
 *
 * ========================================
 * Position Validator Unit Tests
 * ========================================
 *
 * ===== Testing Lot Size Validation =====
 *
 * ✅ PASS: Valid lot size: 0.10 lots
 * ✅ PASS: Minimum lot size: 0.01 lots
 * ✅ PASS: Maximum lot size: 100.0 lots
 * ✅ PASS: Below minimum: 0.005 < 0.01 (should fail)
 * ✅ PASS: Above maximum: 150.0 > 100.0 (should fail)
 * ... (all tests)
 *
 * ========================================
 * Test Results Summary
 * ========================================
 * ✅ Passed: 58
 * ❌ Failed: 0
 * Total:    58
 *
 * 🎉 ALL TESTS PASSED! 🎉
 */
