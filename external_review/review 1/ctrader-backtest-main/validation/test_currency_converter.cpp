/**
 * Unit Tests for CurrencyConverter
 *
 * Tests currency conversion logic for margin and profit calculations:
 * - Rate management (set, get, cache)
 * - Same-currency conversions
 * - Cross-currency conversions
 * - Margin conversion scenarios
 * - Profit conversion scenarios
 * - Edge cases and error handling
 */

#include "../include/currency_converter.h"
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

void TestBasicRateManagement() {
    std::cout << "\n===== Testing Basic Rate Management =====\n\n";

    CurrencyConverter converter("USD");

    // Test 1: Base currency rate is always 1.0
    AssertEqual(
        converter.GetRate("USD", true),
        1.0,
        "Base currency rate (USD)"
    );

    // Test 2: Set EUR rate
    converter.SetRate("EUR", 0.85);  // 1 USD = 0.85 EUR
    AssertTrue(
        converter.HasRate("EUR"),
        "Has EUR rate after setting"
    );

    // Test 3: Get EUR rate (from EUR to USD)
    AssertEqual(
        converter.GetRate("EUR", true),
        1.0 / 0.85,
        "EUR to USD conversion rate",
        0.001
    );

    // Test 4: Set GBP rate
    converter.SetRate("GBP", 0.76);  // 1 USD = 0.76 GBP
    AssertTrue(
        converter.HasRate("GBP"),
        "Has GBP rate after setting"
    );

    // Test 5: Set JPY rate
    converter.SetRate("JPY", 110.0);  // 1 USD = 110 JPY
    AssertTrue(
        converter.HasRate("JPY"),
        "Has JPY rate after setting"
    );

    // Test 6: Unknown currency returns 1.0 (fallback)
    AssertEqual(
        converter.GetRate("XXX", true),
        1.0,
        "Unknown currency fallback"
    );

    // Test 7: HasRate returns false for unknown currency
    AssertTrue(
        !converter.HasRate("XXX"),
        "HasRate false for unknown currency"
    );

    // Test 8: Negative rate is ignored
    converter.SetRate("EUR", -1.0);
    AssertEqual(
        converter.GetRate("EUR", true),
        1.0 / 0.85,  // Should still be old rate
        "Negative rate ignored",
        0.001
    );

    // Test 9: Zero rate is ignored
    converter.SetRate("GBP", 0.0);
    AssertEqual(
        converter.GetRate("GBP", true),
        1.0 / 0.76,  // Should still be old rate
        "Zero rate ignored",
        0.001
    );

    // Test 10: Clear rates
    converter.ClearRates();
    AssertTrue(
        !converter.HasRate("EUR"),
        "EUR rate cleared"
    );
    AssertTrue(
        converter.HasRate("USD"),
        "USD rate remains after clear (base currency)"
    );
}

void TestConversionDirections() {
    std::cout << "\n===== Testing Conversion Directions =====\n\n";

    CurrencyConverter converter("USD");
    converter.SetRate("EUR", 0.85);  // 1 USD = 0.85 EUR

    // Test 1: To account currency (EUR → USD)
    // If 1 USD = 0.85 EUR, then 1 EUR = 1/0.85 USD ≈ 1.176 USD
    AssertEqual(
        converter.GetRate("EUR", true),
        1.0 / 0.85,
        "EUR to USD (to account)",
        0.001
    );

    // Test 2: From account currency (USD → EUR)
    AssertEqual(
        converter.GetRate("EUR", false),
        0.85,
        "USD to EUR (from account)"
    );

    // Test 3: ConvertToAccount (100 EUR → USD)
    double eur_amount = 100.0;
    double usd_amount = converter.ConvertToAccount(eur_amount, "EUR");
    AssertEqual(
        usd_amount,
        100.0 / 0.85,
        "Convert 100 EUR to USD",
        0.01
    );

    // Test 4: ConvertFromAccount (100 USD → EUR)
    double usd_to_convert = 100.0;
    double eur_result = converter.ConvertFromAccount(usd_to_convert, "EUR");
    AssertEqual(
        eur_result,
        100.0 * 0.85,
        "Convert 100 USD to EUR",
        0.01
    );

    // Test 5: Same currency conversion (USD → USD)
    AssertEqual(
        converter.ConvertToAccount(100.0, "USD"),
        100.0,
        "Same currency to account (no conversion)"
    );

    AssertEqual(
        converter.ConvertFromAccount(100.0, "USD"),
        100.0,
        "Same currency from account (no conversion)"
    );
}

void TestMarginConversion() {
    std::cout << "\n===== Testing Margin Conversion =====\n\n";

    // Scenario 1: USD account trading EURUSD
    CurrencyConverter converter_usd("USD");

    // Test 1: EURUSD - Margin in EUR, account in USD
    // Margin: 100 EUR, EURUSD = 1.20
    // Margin in USD = 100 × 1.20 = 120 USD
    double margin_eur = 100.0;
    double eurusd_rate = 1.20;
    AssertEqual(
        converter_usd.ConvertMargin(margin_eur, "EUR", eurusd_rate),
        120.0,
        "EURUSD margin: 100 EUR → 120 USD @ 1.20"
    );

    // Test 2: EURUSD - Same currency (no conversion)
    AssertEqual(
        converter_usd.ConvertMargin(100.0, "USD", 1.20),
        100.0,
        "Same currency margin (no conversion)"
    );

    // Test 3: GBPUSD - Margin in GBP, account in USD
    // Margin: 50 GBP, GBPUSD = 1.30
    // Margin in USD = 50 × 1.30 = 65 USD
    AssertEqual(
        converter_usd.ConvertMargin(50.0, "GBP", 1.30),
        65.0,
        "GBPUSD margin: 50 GBP → 65 USD @ 1.30"
    );

    // Scenario 2: EUR account trading EURUSD
    CurrencyConverter converter_eur("EUR");

    // Test 4: EURUSD with EUR account - no conversion needed
    AssertEqual(
        converter_eur.ConvertMargin(100.0, "EUR", 1.20),
        100.0,
        "EUR account, EUR margin (no conversion)"
    );

    // Test 5: GBPEUR - Margin in GBP, account in EUR
    // Margin: 50 GBP, GBPEUR = 1.15
    AssertEqual(
        converter_eur.ConvertMargin(50.0, "GBP", 1.15),
        57.5,
        "GBPEUR margin: 50 GBP → 57.5 EUR @ 1.15"
    );

    // Test 6: Zero margin
    AssertEqual(
        converter_usd.ConvertMargin(0.0, "EUR", 1.20),
        0.0,
        "Zero margin conversion"
    );

    // Test 7: Large margin value
    AssertEqual(
        converter_usd.ConvertMargin(10000.0, "EUR", 1.20),
        12000.0,
        "Large margin: 10000 EUR → 12000 USD"
    );
}

void TestProfitConversion() {
    std::cout << "\n===== Testing Profit Conversion =====\n\n";

    CurrencyConverter converter("USD");

    // Test 1: EURUSD profit (USD account)
    // Profit in USD (quote currency) = account currency
    // No conversion needed
    AssertEqual(
        converter.ConvertProfit(100.0, "USD", 1.0),
        100.0,
        "EURUSD profit: USD → USD (no conversion)"
    );

    // Test 2: GBPJPY profit (USD account)
    // Profit in JPY, need to convert to USD
    // Profit: 10000 JPY, USDJPY = 110.0
    // Profit in USD = 10000 / 110.0 ≈ 90.91 USD
    double profit_jpy = 10000.0;
    double usdjpy_rate = 110.0;
    AssertEqual(
        converter.ConvertProfit(profit_jpy, "JPY", usdjpy_rate),
        90.909,
        "GBPJPY profit: 10000 JPY → 90.91 USD",
        0.01
    );

    // Test 3: EURJPY profit (USD account)
    // Profit in JPY, convert via USDJPY
    AssertEqual(
        converter.ConvertProfit(5000.0, "JPY", 110.0),
        45.454,
        "EURJPY profit: 5000 JPY → 45.45 USD",
        0.01
    );

    // Test 4: Negative profit (loss)
    AssertEqual(
        converter.ConvertProfit(-1000.0, "JPY", 110.0),
        -9.090,
        "Loss conversion: -1000 JPY → -9.09 USD",
        0.01
    );

    // Test 5: Zero profit
    AssertEqual(
        converter.ConvertProfit(0.0, "JPY", 110.0),
        0.0,
        "Zero profit"
    );

    // Test 6: EUR account, USD profit
    CurrencyConverter converter_eur("EUR");
    // Profit in USD, account in EUR
    // Profit: 100 USD, EURUSD = 1.20
    // Profit in EUR = 100 / 1.20 ≈ 83.33 EUR
    AssertEqual(
        converter_eur.ConvertProfit(100.0, "USD", 1.20),
        83.333,
        "USD profit → EUR: 100 USD → 83.33 EUR @ 1.20",
        0.01
    );

    // Test 7: Same currency profit
    AssertEqual(
        converter_eur.ConvertProfit(100.0, "EUR", 1.0),
        100.0,
        "EUR profit → EUR (no conversion)"
    );
}

void TestCrossCurrencyScenarios() {
    std::cout << "\n===== Testing Cross-Currency Scenarios =====\n\n";

    // Scenario 1: USD account trading EURUSD
    std::cout << "Scenario 1: USD account, EURUSD symbol\n";
    CurrencyConverter usd_converter("USD");

    double margin_eur_1 = 240.0;  // Margin calculated in EUR
    double eurusd = 1.2000;
    double margin_usd_1 = usd_converter.ConvertMargin(margin_eur_1, "EUR", eurusd);
    AssertEqual(margin_usd_1, 288.0, "  Margin: 240 EUR → 288 USD");

    double profit_usd_1 = 100.0;  // Profit in USD (quote)
    double profit_account_1 = usd_converter.ConvertProfit(profit_usd_1, "USD", 1.0);
    AssertEqual(profit_account_1, 100.0, "  Profit: 100 USD → 100 USD");

    // Scenario 2: EUR account trading EURUSD
    std::cout << "\nScenario 2: EUR account, EURUSD symbol\n";
    CurrencyConverter eur_converter("EUR");

    double margin_eur_2 = 240.0;
    double margin_account_2 = eur_converter.ConvertMargin(margin_eur_2, "EUR", eurusd);
    AssertEqual(margin_account_2, 240.0, "  Margin: 240 EUR → 240 EUR");

    double profit_usd_2 = 100.0;
    double profit_eur_2 = eur_converter.ConvertProfit(profit_usd_2, "USD", eurusd);
    AssertEqual(profit_eur_2, 83.333, "  Profit: 100 USD → 83.33 EUR", 0.01);

    // Scenario 3: USD account trading GBPJPY
    std::cout << "\nScenario 3: USD account, GBPJPY symbol\n";
    CurrencyConverter usd_gbpjpy("USD");

    double gbpusd = 1.3000;
    double usdjpy = 110.0;

    double margin_gbp = 300.0;
    double margin_usd_3 = usd_gbpjpy.ConvertMargin(margin_gbp, "GBP", gbpusd);
    AssertEqual(margin_usd_3, 390.0, "  Margin: 300 GBP → 390 USD @ 1.30");

    double profit_jpy = 10000.0;
    double profit_usd_3 = usd_gbpjpy.ConvertProfit(profit_jpy, "JPY", usdjpy);
    AssertEqual(profit_usd_3, 90.909, "  Profit: 10000 JPY → 90.91 USD", 0.01);

    // Scenario 4: GBP account trading EURUSD
    std::cout << "\nScenario 4: GBP account, EURUSD symbol\n";
    CurrencyConverter gbp_converter("GBP");

    double eurgbp = 0.85;  // 1 EUR = 0.85 GBP
    double margin_eur_4 = 200.0;
    double margin_gbp_4 = gbp_converter.ConvertMargin(margin_eur_4, "EUR", eurgbp);
    AssertEqual(margin_gbp_4, 170.0, "  Margin: 200 EUR → 170 GBP @ 0.85");

    double usdgbp = 0.76;  // 1 USD = 0.76 GBP
    double profit_usd_4 = 100.0;
    double profit_gbp_4 = gbp_converter.ConvertProfit(profit_usd_4, "USD", usdgbp);
    AssertEqual(profit_gbp_4, 131.578, "  Profit: 100 USD → 131.58 GBP", 0.01);
}

void TestEdgeCases() {
    std::cout << "\n===== Testing Edge Cases =====\n\n";

    CurrencyConverter converter("USD");

    // Test 1: Very small amounts
    AssertEqual(
        converter.ConvertMargin(0.001, "EUR", 1.20),
        0.0012,
        "Very small margin conversion",
        0.00001
    );

    // Test 2: Very large amounts
    AssertEqual(
        converter.ConvertMargin(1000000.0, "EUR", 1.20),
        1200000.0,
        "Very large margin conversion"
    );

    // Test 3: Conversion rate close to 1.0
    AssertEqual(
        converter.ConvertMargin(100.0, "EUR", 1.0001),
        100.01,
        "Rate very close to 1.0",
        0.001
    );

    // Test 4: Very high conversion rate
    AssertEqual(
        converter.ConvertProfit(1000.0, "JPY", 150.0),
        6.666,
        "Very high conversion rate (JPY)",
        0.01
    );

    // Test 5: Very low conversion rate
    AssertEqual(
        converter.ConvertMargin(100.0, "EUR", 0.001),
        0.1,
        "Very low conversion rate"
    );

    // Test 6: Multiple rate updates
    converter.SetRate("EUR", 1.10);
    converter.SetRate("EUR", 1.20);
    converter.SetRate("EUR", 1.15);
    AssertEqual(
        converter.GetRate("EUR", false),
        1.15,
        "Latest rate after multiple updates"
    );

    // Test 7: Get base currency
    AssertTrue(
        converter.GetBaseCurrency() == "USD",
        "Get base currency"
    );

    // Test 8: Empty string currency (edge case)
    converter.SetRate("", 1.5);
    AssertTrue(
        converter.HasRate(""),
        "Empty string currency handled"
    );
}

void TestRealisticTradingScenarios() {
    std::cout << "\n===== Testing Realistic Trading Scenarios =====\n\n";

    // Real-world example 1: Standard forex trade
    std::cout << "Example 1: 0.01 lot EURUSD on $10,000 USD account\n";
    CurrencyConverter std_account("USD");

    double lot_size = 0.01;
    double contract_size = 100000;
    double eurusd_price = 1.2000;
    double leverage = 500;

    // Margin = (0.01 × 100000 × 1.2000) / 500 = 2.4 EUR
    double margin_eur = (lot_size * contract_size * eurusd_price) / leverage;
    double margin_usd = std_account.ConvertMargin(margin_eur, "EUR", eurusd_price);

    std::cout << "  Margin in EUR: " << margin_eur << "\n";
    std::cout << "  Margin in USD: " << margin_usd << "\n";
    AssertEqual(margin_eur, 2.4, "  Margin calculated correctly", 0.01);
    AssertEqual(margin_usd, 2.88, "  Margin converted correctly", 0.01);

    // 10 pip profit = 10 × 0.00001 × 100000 × 0.01 = 1 USD
    double profit_usd = 1.0;
    double profit_account = std_account.ConvertProfit(profit_usd, "USD", 1.0);
    AssertEqual(profit_account, 1.0, "  10 pip profit = $1 USD");

    // Real-world example 2: Cross-currency trade
    std::cout << "\nExample 2: 0.05 lot GBPJPY on $10,000 USD account\n";
    CurrencyConverter cross_account("USD");

    double gbpjpy_price = 150.0;
    double lot_size_2 = 0.05;
    double leverage_2 = 100;

    // Margin = (0.05 × 100000 × 150.0) / 100 = 7500 GBP
    double margin_gbp = (lot_size_2 * contract_size * gbpjpy_price) / leverage_2;
    double gbpusd_rate = 1.3000;
    double margin_usd_2 = cross_account.ConvertMargin(margin_gbp, "GBP", gbpusd_rate);

    std::cout << "  Margin in GBP: " << margin_gbp << "\n";
    std::cout << "  Margin in USD: " << margin_usd_2 << "\n";
    AssertEqual(margin_gbp, 7500.0, "  Cross-pair margin in GBP");
    AssertEqual(margin_usd_2, 9750.0, "  Cross-pair margin in USD", 0.1);

    // 100 pip profit in JPY
    double profit_jpy = 100.0 * 0.01 * contract_size * lot_size_2;  // 5000 JPY
    double usdjpy_rate = 110.0;
    double profit_usd_2 = cross_account.ConvertProfit(profit_jpy, "JPY", usdjpy_rate);

    std::cout << "  Profit in JPY: " << profit_jpy << "\n";
    std::cout << "  Profit in USD: " << profit_usd_2 << "\n";
    AssertEqual(profit_jpy, 5000.0, "  100 pip profit in JPY");
    AssertEqual(profit_usd_2, 45.454, "  100 pip profit in USD", 0.01);
}

int main() {
    std::cout << "========================================\n";
    std::cout << "Currency Converter Unit Tests\n";
    std::cout << "========================================\n";

    TestBasicRateManagement();
    TestConversionDirections();
    TestMarginConversion();
    TestProfitConversion();
    TestCrossCurrencyScenarios();
    TestEdgeCases();
    TestRealisticTradingScenarios();

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
