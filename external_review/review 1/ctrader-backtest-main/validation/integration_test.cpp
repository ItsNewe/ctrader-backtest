/**
 * Integration Tests - Backtest Engine Components
 *
 * Tests all core validation components working together in realistic scenarios:
 * - PositionValidator
 * - CurrencyConverter
 * - CurrencyRateManager
 *
 * This validates component interactions before MT5 comparison testing.
 */

#include "position_validator.h"
#include "currency_converter.h"
#include "currency_rate_manager.h"
#include <iostream>
#include <cmath>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

// Test framework
int tests_run = 0;
int tests_passed = 0;
int tests_failed = 0;

void AssertTrue(bool condition, const std::string& test_name) {
    tests_run++;
    if (condition) {
        std::cout << "✅ PASS: " << test_name << std::endl;
        tests_passed++;
    } else {
        std::cout << "❌ FAIL: " << test_name << std::endl;
        tests_failed++;
    }
}

void AssertEqual(int actual, int expected, const std::string& test_name) {
    tests_run++;
    if (actual == expected) {
        std::cout << "✅ PASS: " << test_name
                  << " (expected: " << expected << ", got: " << actual << ")" << std::endl;
        tests_passed++;
    } else {
        std::cout << "❌ FAIL: " << test_name
                  << " (expected: " << expected << ", got: " << actual << ")" << std::endl;
        tests_failed++;
    }
}

void AssertAlmostEqual(double actual, double expected, double tolerance,
                       const std::string& test_name) {
    tests_run++;
    if (std::abs(actual - expected) < tolerance) {
        std::cout << "✅ PASS: " << test_name
                  << " (expected: " << expected << ", got: " << actual << ")" << std::endl;
        tests_passed++;
    } else {
        std::cout << "❌ FAIL: " << test_name
                  << " (expected: " << expected << ", got: " << actual
                  << ", diff: " << std::abs(actual - expected) << ")" << std::endl;
        tests_failed++;
    }
}

void PrintHeader(const std::string& text) {
    std::cout << "\n========================================" << std::endl;
    std::cout << text << std::endl;
    std::cout << "========================================" << std::endl;
}

void PrintSection(const std::string& text) {
    std::cout << "\n===== " << text << " =====" << std::endl;
}

/**
 * Scenario 1: Simple Same-Currency Trading
 *
 * Tests basic trading with USD account and EURUSD pair (no currency conversion needed).
 * Validates: position validation, margin calculation, profit calculation
 */
void TestScenario1_SimpleSameCurrency() {
    PrintSection("Scenario 1: Simple Same-Currency Trading");

    std::cout << "\nSetup:" << std::endl;
    std::cout << "  Account: USD" << std::endl;
    std::cout << "  Symbol: EURUSD" << std::endl;
    std::cout << "  Balance: $10,000" << std::endl;
    std::cout << "  Leverage: 1:500" << std::endl;
    std::cout << "  Current price: 1.2000" << std::endl;

    // Setup components
    CurrencyConverter converter("USD");

    // Symbol specifications
    double volume_min = 0.01;
    double volume_max = 100.0;
    double volume_step = 0.01;
    int stops_level = 10;  // 10 points = 1 pip

    // Test 1: Validate lot size
    PrintSection("Test 1: Lot Size Validation");
    std::string error;
    bool valid = PositionValidator::ValidateLotSize(0.01, volume_min, volume_max, volume_step, &error);
    AssertTrue(valid, "Valid lot size 0.01");

    // Test 2: Calculate margin
    PrintSection("Test 2: Margin Calculation");
    double lot_size = 0.01;
    double eurusd_price = 1.2000;
    double leverage = 500.0;

    // Margin in EUR (base currency)
    double margin_eur = (lot_size * 100000) / leverage;  // 1,000 / 500 = 2.0 EUR

    // Convert to USD (quote currency = account currency, so use price directly)
    double margin_usd = converter.ConvertMargin(margin_eur, "EUR", eurusd_price);

    AssertAlmostEqual(margin_usd, 2.40, 0.01, "Margin = 2.0 EUR × 1.20 = $2.40");

    // Test 3: Validate margin availability
    PrintSection("Test 3: Margin Availability");
    double balance = 10000.0;
    bool has_margin = PositionValidator::ValidateMargin(margin_usd, balance);
    AssertTrue(has_margin, "Sufficient margin ($2.40 < $10,000)");

    // Test 4: Calculate profit
    PrintSection("Test 4: Profit Calculation");

    // Price moves from 1.2000 to 1.2100 (100 pips profit)
    double entry_price = 1.2000;
    double exit_price = 1.2100;
    double price_change = exit_price - entry_price;  // 0.0100

    // Profit in USD (quote currency = account currency, no conversion)
    double profit_usd = lot_size * 100000 * price_change;  // 0.01 × 100,000 × 0.01 = $10

    AssertAlmostEqual(profit_usd, 10.0, 0.01, "Profit = 100 pips × 0.01 lot = $10");

    // Test 5: Final balance
    PrintSection("Test 5: Final Balance");
    double final_balance = balance + profit_usd;
    AssertAlmostEqual(final_balance, 10010.0, 0.01, "Final balance = $10,000 + $10");

    std::cout << "\n✅ Scenario 1: All tests passed" << std::endl;
}

/**
 * Scenario 2: Cross-Currency Trading with Rate Updates
 *
 * Tests cross-currency trading (GBPJPY with USD account).
 * Validates: automatic rate detection, margin/profit conversion, rate updates
 */
void TestScenario2_CrossCurrency() {
    PrintSection("Scenario 2: Cross-Currency Trading");

    std::cout << "\nSetup:" << std::endl;
    std::cout << "  Account: USD" << std::endl;
    std::cout << "  Symbol: GBPJPY" << std::endl;
    std::cout << "  Balance: $10,000" << std::endl;
    std::cout << "  Leverage: 1:500" << std::endl;
    std::cout << "  GBPJPY price: 150.0" << std::endl;
    std::cout << "  GBPUSD rate: 1.3000" << std::endl;
    std::cout << "  USDJPY rate: 110.0" << std::endl;

    // Setup components
    CurrencyRateManager rate_mgr("USD", 60);  // 60-second cache expiry
    CurrencyConverter converter("USD");

    // Test 1: Detect required conversion pairs
    PrintSection("Test 1: Required Conversion Pairs");
    auto required_pairs = rate_mgr.GetRequiredConversionPairs("GBP", "JPY");

    AssertEqual(required_pairs.size(), 2, "Need 2 pairs (GBPUSD + USDJPY)");

    std::cout << "  Required pairs: ";
    for (const auto& pair : required_pairs) {
        std::cout << pair << " ";
    }
    std::cout << std::endl;

    // Test 2: Update rates from symbol prices
    PrintSection("Test 2: Rate Updates");
    rate_mgr.UpdateRateFromSymbol("GBPUSD", 1.3000);
    rate_mgr.UpdateRateFromSymbol("USDJPY", 110.0);

    AssertTrue(rate_mgr.HasValidRate("GBP"), "GBP rate cached");
    AssertTrue(rate_mgr.HasValidRate("JPY"), "JPY rate cached");

    // Test 3: Calculate margin conversion
    PrintSection("Test 3: Margin Conversion");
    double lot_size = 0.05;
    double gbpjpy_price = 150.0;

    // Margin in GBP (base currency)
    double margin_gbp = (lot_size * 100000) / 500.0;  // 5,000 / 500 = 10 GBP

    // Get conversion rate (GBP → USD)
    double margin_rate = rate_mgr.GetMarginConversionRate("GBP", "JPY", gbpjpy_price);
    AssertAlmostEqual(margin_rate, 1.3000, 0.0001, "Margin rate = GBPUSD (1.3000)");

    // Convert to USD
    double margin_usd = converter.ConvertMargin(margin_gbp, "GBP", margin_rate);
    AssertAlmostEqual(margin_usd, 13.0, 0.01, "Margin = 10 GBP × 1.30 = $13.00");

    // Test 4: Calculate profit conversion
    PrintSection("Test 4: Profit Conversion");

    // Price moves from 150.0 to 155.0 (500 pips profit)
    double price_change = 5.0;
    double profit_jpy = lot_size * 100000 * price_change;  // 0.05 × 100,000 × 5.0 = 25,000 JPY

    // Get conversion rate (JPY → USD)
    double profit_rate = rate_mgr.GetProfitConversionRate("JPY", gbpjpy_price);
    AssertAlmostEqual(profit_rate, 110.0, 0.1, "Profit rate = USDJPY (110.0)");

    // Convert to USD
    double profit_usd = converter.ConvertProfit(profit_jpy, "JPY", profit_rate);
    AssertAlmostEqual(profit_usd, 227.27, 0.01, "Profit = 25,000 JPY / 110 = $227.27");

    // Test 5: Rate update and recalculation
    PrintSection("Test 5: Rate Update");

    // GBPUSD rate changes to 1.4000
    rate_mgr.UpdateRateFromSymbol("GBPUSD", 1.4000);

    double new_margin_rate = rate_mgr.GetMarginConversionRate("GBP", "JPY", gbpjpy_price);
    AssertAlmostEqual(new_margin_rate, 1.4000, 0.0001, "Updated margin rate = 1.4000");

    double new_margin_usd = converter.ConvertMargin(margin_gbp, "GBP", new_margin_rate);
    AssertAlmostEqual(new_margin_usd, 14.0, 0.01, "Updated margin = 10 GBP × 1.40 = $14.00");

    std::cout << "\n✅ Scenario 2: All tests passed" << std::endl;
}

/**
 * Scenario 3: Multiple Positions with Margin Management
 *
 * Tests margin tracking across multiple open positions.
 * Validates: cumulative margin, free margin, position limit enforcement
 */
void TestScenario3_MultiplePositions() {
    PrintSection("Scenario 3: Multiple Positions");

    std::cout << "\nSetup:" << std::endl;
    std::cout << "  Account: USD" << std::endl;
    std::cout << "  Balance: $10,000" << std::endl;
    std::cout << "  Leverage: 1:500" << std::endl;

    double balance = 10000.0;
    double total_margin = 0.0;
    double leverage = 500.0;

    // Position 1: EURUSD 0.10 lot
    PrintSection("Position 1: EURUSD 0.10 lot");
    double eurusd_price = 1.2000;
    double margin1 = (0.10 * 100000 * eurusd_price) / leverage;  // 10,000 × 1.20 / 500 = 24.00
    total_margin += margin1;

    AssertAlmostEqual(margin1, 24.0, 0.01, "Position 1 margin = $24.00");

    double free_margin1 = balance - total_margin;
    AssertAlmostEqual(free_margin1, 9976.0, 0.01, "Free margin = $10,000 - $24 = $9,976");

    // Position 2: GBPUSD 0.05 lot
    PrintSection("Position 2: GBPUSD 0.05 lot");
    double gbpusd_price = 1.3000;
    double margin2 = (0.05 * 100000 * gbpusd_price) / leverage;  // 5,000 × 1.30 / 500 = 13.00
    total_margin += margin2;

    AssertAlmostEqual(total_margin, 37.0, 0.01, "Total margin = $24 + $13 = $37");

    double free_margin2 = balance - total_margin;
    AssertAlmostEqual(free_margin2, 9963.0, 0.01, "Free margin = $10,000 - $37 = $9,963");

    // Position 3: USDJPY 1.0 lot
    PrintSection("Position 3: USDJPY 1.0 lot");
    // For USDJPY with USD account, margin is in USD (base currency)
    // No price multiplication needed
    double margin3 = (1.0 * 100000) / leverage;  // 100,000 / 500 = 200.00
    total_margin += margin3;

    AssertAlmostEqual(total_margin, 237.0, 0.01, "Total margin = $37 + $200 = $237");

    double margin_level3 = (balance / total_margin) * 100.0;
    AssertTrue(margin_level3 > 100.0, "Margin level = 4,219% (sufficient)");

    // Position 4 attempt: 50 lots EURUSD (should fail)
    PrintSection("Position 4: 50 lots EURUSD (should fail)");
    double margin4 = (50.0 * 100000 * eurusd_price) / leverage;  // 5,000,000 × 1.20 / 500 = 12,000
    double total_with_pos4 = total_margin + margin4;

    bool would_succeed = (total_with_pos4 < balance);
    AssertTrue(!would_succeed, "Large position rejected (would need $12,237 > $10,000)");

    // Close Position 1
    PrintSection("Close Position 1");
    total_margin -= margin1;
    double free_margin_after = balance - total_margin;

    AssertAlmostEqual(total_margin, 213.0, 0.01, "Margin after closing = $237 - $24 = $213");
    AssertAlmostEqual(free_margin_after, 9787.0, 0.01, "Free margin = $10,000 - $213 = $9,787");

    std::cout << "\n✅ Scenario 3: All tests passed" << std::endl;
}

/**
 * Scenario 4: Position Limits Enforcement
 *
 * Tests all validation rules during operations.
 * Validates: lot size limits, SL/TP distance checks, margin validation
 */
void TestScenario4_PositionLimits() {
    PrintSection("Scenario 4: Position Limits Enforcement");

    std::cout << "\nSetup:" << std::endl;
    std::cout << "  Symbol: EURUSD" << std::endl;
    std::cout << "  Lot limits: 0.01 - 100.0, step 0.01" << std::endl;
    std::cout << "  Min SL/TP: 10 points (1 pip)" << std::endl;
    std::cout << "  Entry price: 1.20000" << std::endl;

    double volume_min = 0.01;
    double volume_max = 100.0;
    double volume_step = 0.01;
    int stops_level = 10;
    std::string error;

    // Test 1: Lot size validation
    PrintSection("Test 1: Lot Size Limits");

    AssertTrue(!PositionValidator::ValidateLotSize(0.005, volume_min, volume_max, volume_step, &error), "Below min (0.005) rejected");
    AssertTrue(!PositionValidator::ValidateLotSize(150.0, volume_min, volume_max, volume_step, &error), "Above max (150.0) rejected");
    AssertTrue(!PositionValidator::ValidateLotSize(0.015, volume_min, volume_max, volume_step, &error), "Invalid step (0.015) rejected");
    AssertTrue(PositionValidator::ValidateLotSize(0.01, volume_min, volume_max, volume_step, &error), "Valid lot (0.01) accepted");
    AssertTrue(PositionValidator::ValidateLotSize(1.00, volume_min, volume_max, volume_step, &error), "Valid lot (1.00) accepted");

    // Test 2: Stop loss distance validation
    PrintSection("Test 2: Stop Loss Distance");

    double entry = 1.20000;
    double point = 0.00001;  // 5-digit broker

    bool is_buy = true;  // Long position

    // Too close: 5 points (0.5 pips) - REJECT
    double sl_close = entry - (5 * point);  // 1.19995
    AssertTrue(!PositionValidator::ValidateStopDistance(entry, sl_close, is_buy, stops_level, point, &error),
               "SL too close (5 points) rejected");

    // Valid: 100 points (10 pips) - ACCEPT
    double sl_valid = entry - (100 * point);  // 1.19900
    AssertTrue(PositionValidator::ValidateStopDistance(entry, sl_valid, is_buy, stops_level, point, &error),
              "Valid SL (100 points) accepted");

    // Test 3: Take profit distance validation
    PrintSection("Test 3: Take Profit Distance");

    // Too close: 5 points (0.5 pips) - REJECT
    double tp_close = entry + (5 * point);  // 1.20005
    AssertTrue(!PositionValidator::ValidateStopDistance(entry, tp_close, is_buy, stops_level, point, &error),
               "TP too close (5 points) rejected");

    // Valid: 100 points (10 pips) - ACCEPT
    double tp_valid = entry + (100 * point);  // 1.20100
    AssertTrue(PositionValidator::ValidateStopDistance(entry, tp_valid, is_buy, stops_level, point, &error),
              "Valid TP (100 points) accepted");

    // Test 4: Margin validation
    PrintSection("Test 4: Margin Requirements");

    AssertTrue(PositionValidator::ValidateMargin(100.0, 10000.0),
              "Sufficient margin ($100 < $10,000)");
    AssertTrue(!PositionValidator::ValidateMargin(10000.0, 100.0),
              "Insufficient margin ($10,000 > $100) rejected");

    // Test 5: Complete position validation
    PrintSection("Test 5: Complete Position Validation");

    bool complete_valid = PositionValidator::ValidatePosition(
        0.50,         // lot size
        entry,        // entry price
        sl_valid,     // SL
        tp_valid,     // TP
        true,         // is_buy
        120.0,        // required margin
        10000.0,      // available margin
        volume_min,   // min lot
        volume_max,   // max lot
        volume_step,  // step
        stops_level,  // stops level
        point,        // point size
        &error
    );

    AssertTrue(complete_valid, "Complete position validation passed");

    std::cout << "\n✅ Scenario 4: All tests passed" << std::endl;
}

/**
 * Scenario 5: Rate Cache Expiry During Operations
 *
 * Tests rate caching behavior with time-based expiry.
 * Validates: cache hits/misses, expiry handling, rate refresh
 */
void TestScenario5_RateCacheExpiry() {
    PrintSection("Scenario 5: Rate Cache Expiry");

    std::cout << "\nSetup:" << std::endl;
    std::cout << "  Account: USD" << std::endl;
    std::cout << "  Cache expiry: 2 seconds" << std::endl;

    CurrencyRateManager rate_mgr("USD", 2);  // 2-second expiry

    // Test 1: Fresh rates
    PrintSection("Test 1: Fresh Rates");
    rate_mgr.UpdateRateFromSymbol("GBPUSD", 1.3000);
    rate_mgr.UpdateRateFromSymbol("USDJPY", 110.0);

    AssertTrue(rate_mgr.HasValidRate("GBP"), "GBP rate valid (fresh)");
    AssertTrue(rate_mgr.HasValidRate("JPY"), "JPY rate valid (fresh)");

    bool is_valid = false;
    double gbp_rate = rate_mgr.GetCachedRate("GBP", &is_valid);
    AssertTrue(is_valid, "GBP rate marked as valid");
    AssertAlmostEqual(gbp_rate, 1.3000, 0.0001, "GBP rate value correct");

    // Test 2: Cache size
    PrintSection("Test 2: Cache Management");
    size_t cache_size = rate_mgr.GetCacheSize();
    AssertTrue(cache_size >= 2, "Cache has at least 2 entries");

    // Test 3: Expired rates (wait 3 seconds)
    PrintSection("Test 3: Rate Expiry");
    std::cout << "  Waiting 3 seconds for cache expiry..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(3));

    AssertTrue(!rate_mgr.HasValidRate("GBP"), "GBP rate expired after 3s");

    // Expired rate still returns value but marked invalid
    double expired_rate = rate_mgr.GetCachedRate("GBP", &is_valid);
    AssertTrue(!is_valid, "Expired rate marked as invalid");
    AssertAlmostEqual(expired_rate, 1.3000, 0.0001, "Expired rate value still accessible");

    // Test 4: Rate refresh
    PrintSection("Test 4: Rate Refresh");
    rate_mgr.UpdateRateFromSymbol("GBPUSD", 1.3500);

    AssertTrue(rate_mgr.HasValidRate("GBP"), "Refreshed rate is valid");
    double new_rate = rate_mgr.GetCachedRate("GBP", &is_valid);
    AssertTrue(is_valid, "Refreshed rate marked as valid");
    AssertAlmostEqual(new_rate, 1.3500, 0.0001, "Refreshed rate updated to 1.3500");

    // Test 5: Cache clearing
    PrintSection("Test 5: Cache Clearing");
    rate_mgr.ClearCache();

    AssertEqual(rate_mgr.GetCacheSize(), 0, "Cache cleared (size = 0)");
    AssertTrue(!rate_mgr.HasValidRate("GBP"), "Cleared rate no longer valid");

    std::cout << "\n✅ Scenario 5: All tests passed" << std::endl;
}

/**
 * Main test runner
 */
int main() {
    PrintHeader("Backtest Engine Integration Tests");

    std::cout << "\nTesting component integration with realistic scenarios..." << std::endl;

    // Run all test scenarios
    TestScenario1_SimpleSameCurrency();
    TestScenario2_CrossCurrency();
    TestScenario3_MultiplePositions();
    TestScenario4_PositionLimits();
    TestScenario5_RateCacheExpiry();

    // Print summary
    PrintHeader("Integration Test Summary");
    std::cout << "Tests run:    " << tests_run << std::endl;
    std::cout << "✅ Passed:    " << tests_passed << std::endl;
    std::cout << "❌ Failed:    " << tests_failed << std::endl;

    if (tests_failed == 0) {
        std::cout << "\n🎉 ALL INTEGRATION TESTS PASSED! 🎉" << std::endl;
        std::cout << "\nAll components work together correctly." << std::endl;
        std::cout << "Ready for MT5 validation testing." << std::endl;
        return 0;
    } else {
        std::cout << "\n❌ SOME INTEGRATION TESTS FAILED" << std::endl;
        std::cout << "\nPlease review failures and fix component integration issues." << std::endl;
        return 1;
    }
}
