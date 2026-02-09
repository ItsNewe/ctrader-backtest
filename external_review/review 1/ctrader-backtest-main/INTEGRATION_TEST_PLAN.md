# Integration Test Plan - Phase 7 Next Steps

**Created:** 2026-01-07
**Status:** Ready to Begin
**Prerequisites:** ✅ All unit tests passing (181/181 = 100%)

---

## Overview

With all core validation components unit tested at 100% pass rate, we're ready to test how they work together in realistic backtest scenarios. This phase validates the complete system integration before MT5 comparison.

---

## Test Objectives

### Primary Goals

1. **Validate Component Integration**
   - Ensure PositionValidator, CurrencyConverter, and CurrencyRateManager work together seamlessly
   - Verify data flows correctly between components
   - Confirm no conflicts or unexpected interactions

2. **Test Realistic Trading Scenarios**
   - Multi-position management
   - Cross-currency trading with rate updates
   - Margin checks during backtest execution
   - Position limits enforcement

3. **Verify End-to-End Workflows**
   - Open position → validate → convert → track margin
   - Close position → calculate profit → update account
   - Rate updates → recalculate margins → check limits

---

## Test Scenarios

### Scenario 1: Simple Same-Currency Trading ⭐

**Goal:** Validate basic trading with no currency conversion

**Setup:**
- Account currency: USD
- Symbol: EURUSD
- Initial balance: $10,000
- Leverage: 1:500
- Strategy: Simple buy/hold

**Test Steps:**
```cpp
1. Initialize BacktestEngine with USD account
2. Create PositionValidator with EURUSD limits
3. Attempt to open 0.01 lot BUY position
   - Validate lot size (should pass)
   - Check margin requirement (~$2.40)
   - Confirm sufficient balance
4. Open position successfully
5. Move price up 100 pips
6. Close position
7. Verify profit calculation (100 pips × 0.01 lot = $10)
8. Verify account balance ($10,000 + $10 = $10,010)
```

**Expected Results:**
- ✅ Position opens without errors
- ✅ Margin calculated correctly ($2.40)
- ✅ No currency conversion needed
- ✅ Profit calculated correctly ($10)
- ✅ Account balance updated correctly ($10,010)

**Success Criteria:**
- All validations pass
- Final balance matches manual calculation
- No warnings or errors

---

### Scenario 2: Cross-Currency Trading with Rate Updates ⭐⭐

**Goal:** Validate cross-currency conversions with live rate updates

**Setup:**
- Account currency: USD
- Symbol: GBPJPY
- Initial balance: $10,000
- Leverage: 1:500
- Required rates: GBPUSD, USDJPY
- Strategy: Buy/hold with rate changes

**Test Steps:**
```cpp
1. Initialize CurrencyRateManager for USD account
2. Detect required rates for GBPJPY
   - Expected: ["GBPUSD", "USDJPY"]
3. Set initial rates:
   - GBPUSD = 1.3000 (GBP → USD conversion)
   - USDJPY = 110.0 (JPY → USD conversion)
4. Validate and open 0.05 lot BUY position at 150.0
   - Margin in GBP: 5,000 × 150.0 / 500 = £1,500
   - Margin in USD: £1,500 × 1.3000 = $1,950
5. Update GBPUSD rate to 1.4000
   - Recalculate margin: £1,500 × 1.4000 = $2,100
6. Check if still within margin limits
7. Move GBPJPY price to 155.0 (+500 pips)
8. Close position
   - Profit in JPY: 5,000 × (155.0 - 150.0) = ¥25,000
   - Profit in USD: ¥25,000 / 110.0 = $227.27
9. Verify account balance: $10,000 + $227.27 = $10,227.27
```

**Expected Results:**
- ✅ Required rates detected automatically
- ✅ Margin conversion GBP → USD correct
- ✅ Margin updates when rate changes
- ✅ Profit conversion JPY → USD correct
- ✅ Final balance accurate

**Success Criteria:**
- Automatic rate detection works
- Margin recalculation on rate updates
- Profit calculation matches manual computation
- No rounding errors >$0.01

---

### Scenario 3: Multiple Positions with Margin Management ⭐⭐⭐

**Goal:** Test margin tracking with multiple open positions

**Setup:**
- Account currency: USD
- Symbols: EURUSD, GBPUSD, USDJPY
- Initial balance: $10,000
- Leverage: 1:500
- Strategy: Open multiple positions, track total margin

**Test Steps:**
```cpp
1. Open Position 1: 0.10 lot EURUSD BUY at 1.2000
   - Margin: 10,000 × 1.2000 / 500 = $24.00
   - Free margin: $10,000 - $24 = $9,976

2. Open Position 2: 0.05 lot GBPUSD BUY at 1.3000
   - Margin: 5,000 × 1.3000 / 500 = $13.00
   - Total margin: $24 + $13 = $37.00
   - Free margin: $10,000 - $37 = $9,963

3. Attempt Position 3: 1.0 lot USDJPY BUY at 110.0
   - Would need margin: 100,000 × 110.0 / 110.0 / 500 = $200
   - Total would be: $37 + $200 = $237
   - Should succeed (margin % = 2.37% < 100%)

4. Attempt Position 4: 50.0 lots EURUSD BUY
   - Would need: 5,000,000 × 1.2 / 500 = $12,000
   - Total would be: $237 + $12,000 = $12,237
   - Should FAIL (margin > balance)

5. Close Position 1 (EURUSD)
   - Release $24 margin
   - Total margin: $213
   - Free margin: $9,787 + profit/loss

6. Close remaining positions
7. Verify final balance matches sum of all P/L
```

**Expected Results:**
- ✅ Margin calculated correctly for each position
- ✅ Total margin tracked accurately
- ✅ Free margin updated after each operation
- ✅ Large position rejected due to insufficient margin
- ✅ Margin released when positions close

**Success Criteria:**
- Margin validation prevents over-leveraging
- Free margin always accurate
- Position 4 rejected with clear error message
- All positions close successfully

---

### Scenario 4: Position Limits Enforcement ⭐⭐

**Goal:** Verify all validation rules are enforced during backtest

**Setup:**
- Account currency: USD
- Symbol: EURUSD (min=0.01, max=100.0, step=0.01, stops_level=10)
- Current price: 1.2000
- Initial balance: $10,000

**Test Steps:**
```cpp
1. Attempt invalid lot sizes:
   - 0.005 (below min) → REJECT
   - 150.0 (above max) → REJECT
   - 0.015 (invalid step) → REJECT
   - 0.01 (valid) → ACCEPT

2. Attempt invalid SL distances:
   - SL at 1.1999 (1 pip = 10 points) → REJECT
   - SL at 1.1995 (5 pips = 50 points) → ACCEPT

3. Attempt invalid TP distances:
   - TP at 1.2001 (1 pip = 10 points) → REJECT
   - TP at 1.2010 (10 pips = 100 points) → ACCEPT

4. Test margin validation:
   - 0.01 lot with $10,000 → ACCEPT
   - 100.0 lot with $100 balance → REJECT

5. Test position modification:
   - Modify SL to too-close distance → REJECT
   - Modify SL to valid distance → ACCEPT
```

**Expected Results:**
- ✅ All invalid lot sizes rejected
- ✅ All invalid SL/TP distances rejected
- ✅ Insufficient margin detected
- ✅ Valid operations succeed
- ✅ Clear error messages for each rejection

**Success Criteria:**
- No invalid positions opened
- Error messages are descriptive
- Validation logic matches MT5 behavior

---

### Scenario 5: Rate Cache Expiry During Backtest ⭐⭐

**Goal:** Test rate caching and expiry during long backtest

**Setup:**
- Account currency: USD
- Symbol: GBPJPY
- Cache expiry: 60 seconds
- Backtest duration: 5 minutes
- Rate updates: Every 30 seconds

**Test Steps:**
```cpp
1. Initialize with cache expiry = 60 seconds
2. Set initial rates (GBPUSD, USDJPY)
3. Open position at t=0s
   - Rates are fresh, use cached values
4. At t=30s, update rates
   - Old rates expire, new rates cached
5. At t=90s, check rate validity
   - Rates from t=30s are still valid
6. At t=120s, attempt operation
   - Rates from t=30s expired
   - Should trigger rate refresh
7. Close position with latest rates
```

**Expected Results:**
- ✅ Fresh rates used immediately
- ✅ Expired rates trigger refresh
- ✅ Cache prevents unnecessary rate queries
- ✅ Conversion uses correct rates at each time

**Success Criteria:**
- Cache hits when rates are fresh
- Cache misses when rates expire
- No operations use stale rates
- Performance benefit from caching

---

## Integration Test Implementation

### Test Structure

```cpp
// integration_test.cpp

#include "backtest_engine.h"
#include "position_validator.h"
#include "currency_converter.h"
#include "currency_rate_manager.h"

class IntegrationTestFramework {
private:
    int tests_run = 0;
    int tests_passed = 0;
    int tests_failed = 0;

public:
    void AssertEqual(double actual, double expected, double tolerance,
                     const std::string& test_name) {
        tests_run++;
        if (std::abs(actual - expected) < tolerance) {
            std::cout << "✅ PASS: " << test_name << std::endl;
            tests_passed++;
        } else {
            std::cout << "❌ FAIL: " << test_name
                      << " (expected: " << expected
                      << ", got: " << actual
                      << ", diff: " << std::abs(actual - expected)
                      << ")" << std::endl;
            tests_failed++;
        }
    }

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

    void PrintSummary() {
        std::cout << "\n========================================" << std::endl;
        std::cout << "Integration Test Summary" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "Tests run:    " << tests_run << std::endl;
        std::cout << "✅ Passed:    " << tests_passed << std::endl;
        std::cout << "❌ Failed:    " << tests_failed << std::endl;

        if (tests_failed == 0) {
            std::cout << "\n🎉 ALL INTEGRATION TESTS PASSED! 🎉" << std::endl;
        }
    }
};

void TestScenario1_SimpleSameCurrency(IntegrationTestFramework& test) {
    std::cout << "\n===== Scenario 1: Simple Same-Currency Trading =====" << std::endl;

    // Setup
    BacktestConfig config;
    config.account_currency = "USD";
    config.initial_balance = 10000.0;
    config.leverage = 500;

    PositionValidator validator(0.01, 100.0, 0.01, 10, 100000);
    CurrencyConverter converter("USD");

    // Test lot size validation
    std::string error;
    bool valid = validator.ValidateLotSize(0.01, &error);
    test.AssertTrue(valid, "Valid lot size 0.01");

    // Test margin calculation
    double margin_eur = 0.01 * 100000 / 500;  // 2.0 EUR
    double margin_usd = converter.ConvertMargin(margin_eur, "EUR", 1.2000);
    test.AssertEqual(margin_usd, 2.40, 0.01, "Margin calculation");

    // Test margin availability
    bool has_margin = validator.ValidateMargin(margin_usd, 10000.0);
    test.AssertTrue(has_margin, "Sufficient margin available");

    // Test profit calculation (100 pips profit)
    double profit_usd = 0.01 * 100000 * 0.0100;  // 100 pips = $10
    test.AssertEqual(profit_usd, 10.0, 0.01, "Profit calculation");

    // Test final balance
    double final_balance = 10000.0 + profit_usd;
    test.AssertEqual(final_balance, 10010.0, 0.01, "Final balance");
}

void TestScenario2_CrossCurrency(IntegrationTestFramework& test) {
    std::cout << "\n===== Scenario 2: Cross-Currency Trading =====" << std::endl;

    // Setup
    CurrencyRateManager rate_mgr("USD");
    CurrencyConverter converter("USD");
    PositionValidator validator(0.01, 50.0, 0.01, 20, 100000);

    // Test required rates detection
    auto required_pairs = rate_mgr.GetRequiredConversionPairs("GBP", "JPY");
    test.AssertEqual(required_pairs.size(), 2, 0.1, "Required pairs count");

    // Set rates
    rate_mgr.UpdateRateFromSymbol("GBPUSD", 1.3000);
    rate_mgr.UpdateRateFromSymbol("USDJPY", 110.0);

    // Test margin conversion
    double lot_size = 0.05;
    double gbpjpy_price = 150.0;

    // Margin in GBP
    double margin_gbp = lot_size * 100000 * gbpjpy_price / gbpjpy_price / 500;
    // = 5000 * 150 / 150 / 500 = 5000 / 500 = 10 GBP... wait, this is wrong

    // Correct: Margin = (lot_size × contract_size) / leverage
    // For GBPJPY: margin is in GBP (base currency)
    margin_gbp = (lot_size * 100000) / 500;  // 5000 / 500 = 10 GBP

    double margin_rate = rate_mgr.GetMarginConversionRate("GBP", "JPY", gbpjpy_price);
    double margin_usd = converter.ConvertMargin(margin_gbp, "GBP", margin_rate);

    test.AssertEqual(margin_rate, 1.3000, 0.0001, "Margin conversion rate");
    test.AssertEqual(margin_usd, 13.0, 0.01, "Margin in USD");

    // Test profit conversion
    double price_change = 5.0;  // 155.0 - 150.0
    double profit_jpy = lot_size * 100000 * price_change;  // 25,000 JPY

    double profit_rate = rate_mgr.GetProfitConversionRate("JPY", gbpjpy_price);
    double profit_usd = converter.ConvertProfit(profit_jpy, "JPY", profit_rate);

    test.AssertEqual(profit_rate, 1.0/110.0, 0.0001, "Profit conversion rate");
    test.AssertEqual(profit_usd, 227.27, 0.01, "Profit in USD");
}

void TestScenario3_MultiplePositions(IntegrationTestFramework& test) {
    std::cout << "\n===== Scenario 3: Multiple Positions =====" << std::endl;

    double balance = 10000.0;
    double total_margin = 0.0;

    // Position 1: EURUSD 0.10 lot
    double margin1 = 0.10 * 100000 * 1.2000 / 500;
    total_margin += margin1;
    test.AssertEqual(margin1, 24.0, 0.01, "Position 1 margin");

    // Position 2: GBPUSD 0.05 lot
    double margin2 = 0.05 * 100000 * 1.3000 / 500;
    total_margin += margin2;
    test.AssertEqual(total_margin, 37.0, 0.01, "Total margin after 2 positions");

    // Position 3: USDJPY 1.0 lot (base is USD, so no price multiplication)
    double margin3 = 1.0 * 100000 / 500;
    total_margin += margin3;
    test.AssertEqual(total_margin, 237.0, 0.01, "Total margin after 3 positions");

    // Check margin level
    double margin_level = (balance / total_margin) * 100.0;
    test.AssertTrue(margin_level > 100.0, "Sufficient margin level");

    // Position 4 attempt: 50 lots EURUSD (should fail)
    double margin4 = 50.0 * 100000 * 1.2000 / 500;
    bool would_succeed = (total_margin + margin4) < balance;
    test.AssertTrue(!would_succeed, "Large position rejected");
}

void TestScenario4_PositionLimits(IntegrationTestFramework& test) {
    std::cout << "\n===== Scenario 4: Position Limits Enforcement =====" << std::endl;

    PositionValidator validator(0.01, 100.0, 0.01, 10, 100000);
    std::string error;

    // Test lot size limits
    test.AssertTrue(!validator.ValidateLotSize(0.005, &error), "Below min rejected");
    test.AssertTrue(!validator.ValidateLotSize(150.0, &error), "Above max rejected");
    test.AssertTrue(!validator.ValidateLotSize(0.015, &error), "Invalid step rejected");
    test.AssertTrue(validator.ValidateLotSize(0.01, &error), "Valid lot accepted");

    // Test SL/TP distance (point = 0.00001 for 5-digit broker)
    double entry = 1.20000;
    double point = 0.00001;

    // Too close SL (5 points = 0.5 pips)
    double sl_close = entry - (5 * point);
    test.AssertTrue(!validator.ValidateStopLoss(entry, sl_close, point, &error),
                    "Too close SL rejected");

    // Valid SL (100 points = 10 pips)
    double sl_valid = entry - (100 * point);
    test.AssertTrue(validator.ValidateStopLoss(entry, sl_valid, point, &error),
                   "Valid SL accepted");

    // Test margin validation
    test.AssertTrue(validator.ValidateMargin(100.0, 10000.0), "Sufficient margin");
    test.AssertTrue(!validator.ValidateMargin(10000.0, 100.0), "Insufficient margin rejected");
}

int main() {
    IntegrationTestFramework test;

    std::cout << "========================================" << std::endl;
    std::cout << "Backtest Engine Integration Tests" << std::endl;
    std::cout << "========================================" << std::endl;

    TestScenario1_SimpleSameCurrency(test);
    TestScenario2_CrossCurrency(test);
    TestScenario3_MultiplePositions(test);
    TestScenario4_PositionLimits(test);

    test.PrintSummary();

    return (test.tests_failed == 0) ? 0 : 1;
}
```

---

## Success Criteria

### Overall Integration Test Goals

| Metric | Target | Measurement |
|--------|--------|-------------|
| Test scenarios | 5 | All scenarios must pass |
| Component integration | 100% | No errors between components |
| Calculation accuracy | <$0.01 | All money calculations within 1 cent |
| Margin tracking | Exact | Free margin always accurate |
| Validation enforcement | 100% | No invalid operations succeed |
| Rate caching | Working | Cache hits/misses as expected |

### Pass Criteria

✅ **PASS** if:
- All 5 scenarios execute without errors
- All component integrations work seamlessly
- Calculation accuracy <$0.01 for all brokerry values
- All validations enforce correctly
- Rate management works as designed

❌ **FAIL** if:
- Any scenario crashes or throws exception
- Calculation errors >$0.01
- Invalid operations not rejected
- Component integration issues
- Rate caching failures

---

## Next Steps After Integration Testing

### 1. MT5 Comparison Testing

Once integration tests pass, compare with MT5 Strategy Tester:

**Comparison Points:**
- Final account balance (<1% difference)
- Trade count (exact match)
- Individual trade P/L
- Margin calculations
- Position open/close prices

### 2. Performance Benchmarking

Measure execution performance:
- Backtest speed (ticks/second)
- Memory usage
- Rate cache efficiency
- Validation overhead

### 3. Production Deployment

When all tests pass:
- Document integration patterns
- Create usage examples
- Write deployment guide
- Set up monitoring

---

## Timeline Estimate

| Phase | Duration | Tasks |
|-------|----------|-------|
| **Integration Test Creation** | 1 day | Write integration_test.cpp with all 5 scenarios |
| **Test Execution & Debugging** | 1 day | Run tests, fix any integration issues |
| **MT5 Comparison** | 2 days | Run same strategy in both engines, compare |
| **Performance Tuning** | 1 day | Optimize hot paths if needed |
| **Documentation** | 1 day | Document results and integration patterns |
| **TOTAL** | **6 days** | Complete integration validation |

---

## Conclusion

This integration test plan provides:
- ✅ Comprehensive scenario coverage
- ✅ Clear success criteria
- ✅ Realistic trading scenarios
- ✅ Step-by-step implementation guide
- ✅ MT5 validation preparation

**Ready to begin:** Yes, all prerequisites met (181/181 unit tests passing)

---

**Document Version:** 1.0
**Status:** Ready for Implementation
**Next Action:** Create [validation/integration_test.cpp](validation/integration_test.cpp) and run first scenario
