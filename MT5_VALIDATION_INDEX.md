# MT5 Validation Framework - Master Index

Complete guide to MT5-exact backtesting engine validation and implementation.

---

## 📋 Quick Navigation

### For First-Time Users
1. Start with → [VALIDATION_TESTS_COMPLETE.md](VALIDATION_TESTS_COMPLETE.md)
2. Then read → [INTEGRATION_GUIDE.md](INTEGRATION_GUIDE.md)
3. Follow → [NEXT_STEPS.md](NEXT_STEPS.md)

### For Developers Integrating
- **Quick Start:** [INTEGRATION_GUIDE.md](INTEGRATION_GUIDE.md)
- **API Reference:** [include/margin_manager.h](include/margin_manager.h), [include/swap_manager.h](include/swap_manager.h)
- **Examples:** See Step 8 in Integration Guide

### For Running New Tests
- **Instructions:** [validation/TEST_INSTRUCTIONS.md](validation/TEST_INSTRUCTIONS.md)
- **Automation:** [validation/retrieve_results.py](validation/retrieve_results.py)

---

## 📚 Documentation Structure

### Phase 1: Validation Framework
Complete MT5 behavior analysis and verification

| Document | Purpose | Status |
|----------|---------|--------|
| [MT5_REPRODUCTION_FRAMEWORK.md](MT5_REPRODUCTION_FRAMEWORK.md) | Framework design | ✅ Complete |
| [IMPLEMENTATION_ROADMAP.md](IMPLEMENTATION_ROADMAP.md) | 6-week implementation plan | ✅ Complete |
| [VALIDATION_SUMMARY.md](VALIDATION_SUMMARY.md) | Executive summary | ✅ Complete |

### Phase 2: Test Execution
Running and analyzing MT5 validation tests

| Document | Purpose | Status |
|----------|---------|--------|
| [POC_READY.md](POC_READY.md) | Test A proof of concept | ✅ Complete |
| [TESTS_BF_READY.md](TESTS_BF_READY.md) | Tests B-F implementation | ✅ Complete |
| [validation/TEST_INSTRUCTIONS.md](validation/TEST_INSTRUCTIONS.md) | How to run each test | ✅ Complete |
| [VALIDATION_TESTS_COMPLETE.md](VALIDATION_TESTS_COMPLETE.md) | **All test results** | ✅ Complete |

### Phase 3: Implementation
MT5-validated C++ classes ready to use

| Document | Purpose | Status |
|----------|---------|--------|
| [include/margin_manager.h](include/margin_manager.h) | Margin calculation class | ✅ Complete |
| [include/swap_manager.h](include/swap_manager.h) | Swap timing class | ✅ Complete |
| [validation/test_margin_swap.cpp](validation/test_margin_swap.cpp) | Unit tests | ✅ Complete |
| [INTEGRATION_GUIDE.md](INTEGRATION_GUIDE.md) | **Integration guide** | ✅ Complete |
| [NEXT_STEPS.md](NEXT_STEPS.md) | Implementation roadmap | ✅ Complete |

---

## 🔬 Test Results

### Test A: SL/TP Execution Order
**Status:** ✅ VALIDATED
**Finding:** Both MT5 and our engine execute TP first
**Action:** No changes needed - current logic is correct

### Test B: Tick Synthesis Pattern
**Status:** ✅ DATA COLLECTED (19.4 MB)
**Finding:** ~1,900 ticks per H1 bar
**Data:** [validation/mt5/test_b_ticks.csv](validation/mt5/test_b_ticks.csv)
**Action:** Implement tick generator (Phase 2)

### Test C: Slippage Distribution
**Status:** ✅ ANALYZED
**Finding:** Zero slippage in MT5 Strategy Tester
**Data:** [validation/mt5/test_c_slippage.csv](validation/mt5/test_c_slippage.csv)
**Action:** Use zero slippage for MT5 reproduction mode

### Test D: Spread Widening
**Status:** ✅ ANALYZED
**Finding:** Constant spread (0.71 pips), weak volatility correlation
**Data:** [validation/mt5/test_d_spread.csv](validation/mt5/test_d_spread.csv)
**Action:** Use constant spread model

### Test E: Swap Timing
**Status:** ✅ VERIFIED
**Finding:** Swap applied at 00:00 server time, daily
**Data:** [validation/mt5/test_e_swap_timing.csv](validation/mt5/test_e_swap_timing.csv)
**Action:** ✅ Implemented in SwapManager

### Test F: Margin Calculation
**Status:** ✅ VERIFIED
**Finding:** Formula = `(lot_size × 100,000 × price) / leverage`
**Data:** [validation/mt5/test_f_margin.csv](validation/mt5/test_f_margin.csv)
**Action:** ✅ Implemented in MarginManager

---

## 🎯 MT5-Validated Formulas

### Margin Calculation
```
Margin = (lot_size × contract_size × price) / leverage

Example:
- 0.01 lots
- EURUSD @ 1.15958
- 1:500 leverage
= (0.01 × 100,000 × 1.15958) / 500
= $2.32 margin required
```

**Implementation:** [include/margin_manager.h:45](include/margin_manager.h)

### Swap Timing
```
Apply swap at 00:00 server time
Check: (current_hour >= 0 && day_changed)
Triple swap on Wednesday
```

**Implementation:** [include/swap_manager.h:39](include/swap_manager.h)

### Swap Calculation
```
Swap = lot_size × contract_size × swap_points × point_value

Wednesday = swap × 3
```

**Implementation:** [include/swap_manager.h:65](include/swap_manager.h)

---

## 🛠️ Implementation Classes

### MarginManager
**File:** [include/margin_manager.h](include/margin_manager.h)
**Methods:**
- `CalculateMargin()` - Get required margin
- `HasSufficientMargin()` - Check if can open position
- `GetMarginLevel()` - Get current margin level %
- `GetFreeMargin()` - Get available margin
- `GetMaxLotSize()` - Calculate max position size

**Unit Tests:** [validation/test_margin_swap.cpp:60](validation/test_margin_swap.cpp)

### SwapManager
**File:** [include/swap_manager.h](include/swap_manager.h)
**Methods:**
- `ShouldApplySwap()` - Check if swap time
- `CalculateSwap()` - Calculate swap charge
- `GetDayOfWeek()` - Get day for triple swap
- `Reset()` - Reset for new backtest

**Unit Tests:** [validation/test_margin_swap.cpp:175](validation/test_margin_swap.cpp)

---

## 📊 Analysis Tools

### Python Scripts
| Script | Purpose |
|--------|---------|
| [validation/analyze_all_tests.py](validation/analyze_all_tests.py) | Master analysis script |
| [validation/analyze_test_c.py](validation/analyze_test_c.py) | Slippage analysis |
| [validation/analyze_test_e.py](validation/analyze_test_e.py) | Swap timing analysis |
| [validation/analyze_test_f.py](validation/analyze_test_f.py) | Margin verification |
| [validation/retrieve_results.py](validation/retrieve_results.py) | Auto-retrieve MT5 results |

### Usage
```bash
# Analyze all tests
python validation/analyze_all_tests.py

# Retrieve results after running MT5 test
python validation/retrieve_results.py test_f
```

---

## 🚀 Integration Workflow

### Step 1: Review Documentation
1. Read [VALIDATION_TESTS_COMPLETE.md](VALIDATION_TESTS_COMPLETE.md) - understand what was tested
2. Read [INTEGRATION_GUIDE.md](INTEGRATION_GUIDE.md) - see how to integrate

### Step 2: Add Headers
```cpp
#include "margin_manager.h"
#include "swap_manager.h"
```

### Step 3: Update BacktestEngine
- Add SwapManager member
- Add margin tracking variables
- Update OpenPosition() to check margin
- Update ClosePosition() to free margin
- Add swap application to main loop

### Step 4: Test Integration
```bash
# Build with tests
cmake -B build -S . -G "MinGW Makefiles"
cmake --build build --target test_margin_swap

# Run unit tests
./build/validation/test_margin_swap.exe
```

### Step 5: Validate Against MT5
- Run same strategy in both engines
- Compare final balance (<1% difference target)
- Verify margin calculations match
- Verify swap charges match

---

## ✅ Completion Status

### Validation Framework
- ✅ Framework designed (MT5_REPRODUCTION_FRAMEWORK.md)
- ✅ 6 micro-tests implemented (A-F)
- ✅ All tests executed and data collected
- ✅ Results analyzed and patterns extracted

### Implementation
- ✅ MarginManager class (MT5-validated)
- ✅ SwapManager class (MT5-validated)
- ✅ Unit tests with real MT5 data
- ✅ CMake build integration
- ✅ Integration guide with examples

### Documentation
- ✅ Test instructions
- ✅ Complete results
- ✅ Integration guide
- ✅ API reference
- ✅ Next steps roadmap

---

## 📈 Success Metrics

| Metric | Target | Status |
|--------|--------|--------|
| Test A (SL/TP) | Validated | ✅ MATCH |
| Test F (Margin) | <$0.10 diff | ✅ VERIFIED |
| Test E (Swap) | Exact timing | ✅ VERIFIED |
| Test C (Slippage) | Zero | ✅ CONFIRMED |
| Test D (Spread) | Constant | ✅ CONFIRMED |
| Test B (Ticks) | ~1900/bar | ✅ MEASURED |
| Unit Tests | All pass | ⏳ Build pending |
| Integration | <1% diff | ⏳ Next phase |

---

## 🎓 Learning Resources

### Understanding the Framework
1. **Why validation?** → [VALIDATION_SUMMARY.md](VALIDATION_SUMMARY.md)
2. **How tests work?** → [MT5_REPRODUCTION_FRAMEWORK.md](MT5_REPRODUCTION_FRAMEWORK.md)
3. **What was found?** → [VALIDATION_TESTS_COMPLETE.md](VALIDATION_TESTS_COMPLETE.md)

### Using the Classes
1. **MarginManager API** → [include/margin_manager.h](include/margin_manager.h) (see comments)
2. **SwapManager API** → [include/swap_manager.h](include/swap_manager.h) (see comments)
3. **Integration examples** → [INTEGRATION_GUIDE.md](INTEGRATION_GUIDE.md) (Step 2-8)

### Running Tests
1. **How to run MT5 tests** → [validation/TEST_INSTRUCTIONS.md](validation/TEST_INSTRUCTIONS.md)
2. **How to retrieve results** → Use `retrieve_results.py`
3. **How to analyze data** → Use `analyze_all_tests.py`

---

## 🔄 Workflow Summary

```
┌─────────────────────────────────────────────────────────────┐
│ Phase 1: Validation (COMPLETE ✅)                           │
├─────────────────────────────────────────────────────────────┤
│ 1. Design micro-tests for MT5 behaviors                    │
│ 2. Implement test EAs in MQL5                              │
│ 3. Run tests in MT5 Strategy Tester                        │
│ 4. Collect and analyze results                             │
│ 5. Extract formulas and patterns                           │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ Phase 2: Implementation (IN PROGRESS ⏳)                    │
├─────────────────────────────────────────────────────────────┤
│ 1. Create C++ classes with validated formulas    ✅        │
│ 2. Write unit tests with real MT5 data          ✅        │
│ 3. Integrate into BacktestEngine                ⏳        │
│ 4. Run integration tests                        ⏳        │
│ 5. Compare results with MT5                     ⏳        │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ Phase 3: Validation Loop (NEXT)                            │
├─────────────────────────────────────────────────────────────┤
│ 1. Run backtest in both engines                            │
│ 2. Compare results (<1% difference target)                 │
│ 3. Fix discrepancies                                        │
│ 4. Repeat until validated                                   │
│ 5. Mark as "MT5-Exact Reproduction Ready" ✓               │
└─────────────────────────────────────────────────────────────┘
```

---

## 📞 Support & Reference

### Quick Reference
- **Margin formula:** `(lots × 100k × price) / leverage`
- **Swap timing:** 00:00 server time, daily
- **Slippage:** Zero (MT5 Tester mode)
- **Tick density:** ~1,900 per H1 bar

### File Locations
- **Headers:** `include/margin_manager.h`, `include/swap_manager.h`
- **Tests:** `validation/test_margin_swap.cpp`
- **MT5 Data:** `validation/mt5/*.csv`
- **Analysis:** `validation/analysis/*.json`

### Key Documents
1. [VALIDATION_TESTS_COMPLETE.md](VALIDATION_TESTS_COMPLETE.md) - **Start here**
2. [INTEGRATION_GUIDE.md](INTEGRATION_GUIDE.md) - **Integration steps**
3. [NEXT_STEPS.md](NEXT_STEPS.md) - **What's next**

---

**Status:** Framework complete, classes implemented, ready for integration
**Quality:** All formulas MT5-validated against real Strategy Tester data
**Next:** Integrate into BacktestEngine and validate full backtest results
