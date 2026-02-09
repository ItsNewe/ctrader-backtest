# Testing Plan - C++ Backtesting Engine

**Date:** 2026-01-07
**Status:** Testing Phase Started
**Implementation:** 100% Complete | **Testing:** 10% Complete

---

## Overview

This document outlines the comprehensive testing strategy for the MT5-validated C++ backtesting engine. The goal is to achieve **<1% difference** from MT5 Strategy Tester results across all test scenarios.

---

## Testing Phases

### Phase 1: Unit Tests ✅ (Designed, Pending Execution)

**Status:** Test code written, compilation environment issues

**Components to Test:**
1. **PositionValidator** - Trading limits validation
2. **CurrencyConverter** - Currency conversion logic
3. **CurrencyRateManager** - Cross-currency rate management
4. **MarginManager** - Margin calculations (already validated)
5. **SwapManager** - Swap timing and calculation (already validated)

**Test Files Created:**
- `validation/test_position_validator.cpp` (~280 lines, 58 test cases)
- `validation/test_validator_simple.cpp` (~150 lines, 8 basic tests)
- `validation/run_unit_tests.py` (Python test runner)

**Test Coverage:**

#### PositionValidator Tests (58 tests)
1. Lot Size Validation (10 tests)
   - Valid lot sizes
   - Below minimum (reject)
   - Above maximum (reject)
   - Invalid step (reject)
   - Edge cases

2. Stop Distance Validation (9 tests)
   - BUY SL/TP distances
   - SELL SL/TP distances
   - Too close (reject)
   - Exactly at minimum (accept)
   - Zero stops level

3. Margin Validation (8 tests)
   - Sufficient margin
   - Insufficient margin (reject)
   - Zero margin
   - Negative margin (reject)
   - Edge cases

4. Lot Size Normalization (10 tests)
   - Already normalized
   - Round to step
   - Clamp to min/max
   - Various step sizes (0.01, 0.1, 0.001)

5. Comprehensive Validation (8 tests)
   - All parameters valid
   - Individual parameter failures
   - Zero SL/TP (skip validation)
   - BUY and SELL positions

6. MT5 Realistic Scenarios (13 tests)
   - EURUSD standard account
   - GBPJPY high volatility
   - Gold (XAUUSD)
   - Micro accounts
   - High leverage positions

**Next Steps:**
- Resolve compilation environment issues
- Execute all unit tests
- Verify 100% pass rate
- Add coverage reporting

---

### Phase 2: Integration Tests ⏳ (Not Started)

**Purpose:** Test all components working together

**Test Scenarios:**

#### Scenario 1: Simple Same-Currency Backtest
```
Account: USD
Symbol: EURUSD
Timeframe: H1
Period: 2023-01-01 to 2023-12-31
Strategy: Simple moving average crossover

Expected:
- Margin calculated correctly
- Profit converted correctly
- Swaps applied at 00:00
- All trades within limits
```

#### Scenario 2: Cross-Currency Backtest
```
Account: USD
Symbol: GBPJPY
Timeframe: H1
Period: 2023-01-01 to 2023-12-31
Strategy: Simple moving average crossover

Expected:
- GBP→USD margin conversion
- JPY→USD profit conversion
- Rates updated periodically
- All conversions accurate
```

#### Scenario 3: Multiple Positions
```
Account: USD
Symbols: EURUSD, GBPJPY, XAUUSD
Strategy: Multi-pair strategy

Expected:
- Total margin tracked correctly
- No margin overflow
- Independent swap application
- Accurate profit aggregation
```

#### Scenario 4: Edge Cases
```
- Maximum leverage positions
- Minimum lot sizes
- SL/TP at exact minimum distance
- Account depleted (margin call)
- Consecutive wins/losses
```

**Implementation:**
- Create integration test harness
- Load real historical data
- Run multi-day backtests
- Compare metrics with MT5

---

### Phase 3: MT5 Validation ⏳ (Not Started)

**Purpose:** Achieve <1% difference from MT5 Strategy Tester

**Validation Method:**
1. Run same strategy in both engines
2. Use identical parameters
3. Use same historical data
4. Compare results

**Metrics to Compare:**
- Total profit/loss
- Number of trades
- Win rate
- Maximum drawdown
- Final balance
- Individual trade entry/exit prices
- Individual trade P&L

**Validation Tests:**

#### Test V1: Simple Strategy (EURUSD)
```
Strategy: MA(20) crossover
Timeframe: H1
Period: 1 month
Account: $10,000 USD
Leverage: 1:500
Lot size: 0.01

Target: <1% difference in all metrics
```

#### Test V2: Complex Strategy (Multi-pair)
```
Strategy: RSI + MACD
Timeframe: M15
Period: 1 week
Symbols: EURUSD, GBPUSD, USDJPY
Account: $10,000 USD

Target: <1% difference in all metrics
```

#### Test V3: High-Frequency Trading
```
Strategy: Scalping (many trades)
Timeframe: M1
Period: 1 day
Symbol: EURUSD
Target: Exact tick-by-tick match
```

#### Test V4: Long-Term Backtest
```
Strategy: Swing trading
Timeframe: Daily
Period: 1 year
Symbol: GBPJPY
Target: Swap calculations match exactly
```

**Comparison Tools:**
- `validation/compare_backtest_results.py` (already exists)
- MT5 trade export (CSV)
- C++ engine trade export (CSV)
- Automated diff report

---

### Phase 4: Performance Benchmarks ⏳ (Not Started)

**Purpose:** Ensure fast execution suitable for parameter sweeps

**Benchmarks:**

#### B1: Single Backtest Speed
```
Measure: Time to complete 1-year H1 backtest
Target: <1 second
Data: ~8,760 bars (365 days × 24 hours)
```

#### B2: Parallel Backtest Speed
```
Measure: Time to run 100 parameter combinations
Target: <10 seconds (with parallelization)
Hardware: 8-core CPU
```

#### B3: Memory Usage
```
Measure: RAM usage during backtest
Target: <100 MB per backtest instance
Data: 1-year tick data (~2M ticks)
```

#### B4: Tick Processing Speed
```
Measure: Ticks processed per second
Target: >100,000 ticks/second
Data: High-frequency tick data
```

**Profiling:**
- Identify bottlenecks
- Optimize hot paths
- Verify cache efficiency
- Test thermal throttling

---

## Test Execution Plan

### Week 1: Unit Tests
- [x] Design unit tests
- [ ] Resolve compilation issues
- [ ] Execute all unit tests
- [ ] Fix any failures
- [ ] Achieve 100% pass rate

### Week 2: Integration Tests
- [ ] Create integration test harness
- [ ] Implement test scenarios 1-4
- [ ] Run integration tests
- [ ] Debug any issues
- [ ] Document results

### Week 3: MT5 Validation
- [ ] Set up MT5 test environment
- [ ] Export MT5 strategy tester results
- [ ] Run equivalent C++ backtests
- [ ] Compare results
- [ ] Iterate until <1% difference

### Week 4: Performance & Optimization
- [ ] Run performance benchmarks
- [ ] Profile hot paths
- [ ] Optimize if needed
- [ ] Re-run benchmarks
- [ ] Document final performance

---

## Success Criteria

### Must Have (Blocking Production)
- ✅ All unit tests pass (100%)
- ⏳ All integration tests pass (100%)
- ⏳ MT5 validation <1% difference
- ⏳ No memory leaks
- ⏳ No crashes or undefined behavior

### Should Have (Nice to Have)
- ⏳ Performance >100k ticks/second
- ⏳ Memory usage <100 MB/instance
- ⏳ Parallel backtest support
- ⏳ Comprehensive error reporting
- ⏳ Logging and debugging tools

### Could Have (Future Enhancements)
- ⏳ Real-time broker data integration
- ⏳ Live trading support
- ⏳ Web dashboard
- ⏳ AI-driven parameter optimization
- ⏳ Multi-broker support

---

## Known Issues

### Issue 1: Compilation Environment
**Status:** Open
**Impact:** High (blocks unit test execution)
**Description:** g++ compiler on Windows appears to fail silently
**Workaround:** None yet
**Resolution:** Need to investigate system PATH, MinGW installation, or use alternative compiler

### Issue 2: MT5 Connector Not Implemented
**Status:** Open
**Impact:** Medium (blocks broker integration)
**Description:** GetSymbolInfo() and GetAccountInfo() not yet implemented
**Workaround:** Manual parameter entry for testing
**Resolution:** Implement MT5 protocol parsing

---

## Test Data Requirements

### Historical Data Needed
- EURUSD H1: 2023-01-01 to 2023-12-31
- GBPJPY H1: 2023-01-01 to 2023-12-31
- XAUUSD H1: 2023-01-01 to 2023-12-31
- Tick data (for high-frequency tests)

### Broker Parameter Data
- Account info: Currency, leverage, balance
- Symbol info: Contract size, margin mode, swap rates, limits
- Real-time rates for cross-currency tests

### MT5 Reference Results
- Trade logs (entry, exit, profit, swap, commission)
- Account equity curve
- Strategy tester report

---

## Test Automation

### Continuous Integration (Future)
```yaml
# .github/workflows/test.yml
name: Unit Tests
on: [push, pull_request]
jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - name: Build tests
        run: cmake --build build --target all
      - name: Run tests
        run: ctest --verbose
```

### Nightly Validation (Future)
```python
# Run full MT5 validation suite every night
# Compare against MT5 results
# Email report if >1% difference
```

---

## Documentation

### Test Reports
- **Unit Test Report**: `validation/unit_test_report.md`
- **Integration Test Report**: `validation/integration_test_report.md`
- **MT5 Validation Report**: `validation/mt5_validation_report.md`
- **Performance Report**: `validation/performance_report.md`

### Test Coverage
- **Target:** >90% code coverage
- **Tool:** gcov or similar
- **Report:** HTML coverage report

---

## Current Status Summary

| Test Phase | Designed | Implemented | Executed | Passed | Status |
|------------|----------|-------------|----------|--------|--------|
| **Unit Tests** | 100% | 100% | 0% | 0% | ⏳ Pending execution |
| **Integration Tests** | 80% | 0% | 0% | 0% | ⏳ Not started |
| **MT5 Validation** | 90% | 0% | 0% | 0% | ⏳ Not started |
| **Performance Tests** | 70% | 0% | 0% | 0% | ⏳ Not started |

**Overall Testing Progress:** 10% (Design phase mostly complete)

---

## Next Immediate Actions

1. **Fix compilation environment** - Debug g++ issues or use alternative compiler
2. **Run unit tests** - Execute all 58+ position validator tests
3. **Create integration harness** - Build test framework for multi-component tests
4. **Export MT5 data** - Get reference results for validation
5. **Implement connector** - Enable real broker data queries

---

**Last Updated:** 2026-01-07
**Maintained By:** AI Development Team
**Review Schedule:** Weekly during testing phase
