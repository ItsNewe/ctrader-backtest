# Next Steps: Implementing MT5-Validated Behaviors

## Overview

All validation tests (A-F) are complete with data collected and analyzed. This document outlines the concrete implementation steps to update our C++ backtest engine with MT5-verified behaviors.

---

## Priority 1: Margin & Swap Management (Week 1)

### Task 1.1: Implement MarginManager Class

**File to create:** `include/margin_manager.h`

```cpp
#ifndef MARGIN_MANAGER_H
#define MARGIN_MANAGER_H

class MarginManager {
public:
    enum CalcMode {
        FOREX = 0,
        CFD = 1,
        FUTURES = 2
    };

    // Calculate required margin for a position
    static double CalculateMargin(
        double lot_size,
        double contract_size,  // 100,000 for FOREX
        double price,
        int leverage,
        CalcMode mode = FOREX
    ) {
        switch (mode) {
            case FOREX:
                return (lot_size * contract_size * price) / leverage;
            case CFD:
                return (lot_size * contract_size * price) / leverage;
            case FUTURES:
                return lot_size * contract_size;  // Fixed margin
            default:
                return 0.0;
        }
    }

    // Check if sufficient margin available
    static bool HasSufficientMargin(
        double account_balance,
        double current_margin_used,
        double required_margin,
        double min_margin_level = 100.0  // %
    ) {
        double total_margin = current_margin_used + required_margin;
        if (total_margin == 0) return true;

        double margin_level = (account_balance / total_margin) * 100.0;
        return margin_level >= min_margin_level;
    }

    // Calculate current margin level
    static double GetMarginLevel(
        double account_balance,
        double current_margin_used
    ) {
        if (current_margin_used == 0) return 0.0;
        return (account_balance / current_margin_used) * 100.0;
    }
};

#endif // MARGIN_MANAGER_H
```

**Integration:**
- Update `BacktestEngine::OpenPosition()` to check margin before opening
- Track `current_margin_used` in account state
- Reject orders if margin insufficient

---

### Task 1.2: Implement SwapManager Class

**File to create:** `include/swap_manager.h`

```cpp
#ifndef SWAP_MANAGER_H
#define SWAP_MANAGER_H

#include <ctime>

class SwapManager {
private:
    int swap_hour_;        // Hour when swap is applied (0 = midnight)
    time_t last_swap_day_; // Track which day swap was last applied

public:
    SwapManager(int swap_hour = 0)
        : swap_hour_(swap_hour), last_swap_day_(0) {}

    // Check if swap should be applied
    bool ShouldApplySwap(time_t current_time) {
        struct tm* timeinfo = gmtime(&current_time);

        // Check if we've crossed the swap hour
        if (timeinfo->tm_hour >= swap_hour_) {
            // Check if it's a different day than last swap
            time_t current_day = current_time / 86400;  // Days since epoch
            if (current_day != last_swap_day_) {
                last_swap_day_ = current_day;
                return true;
            }
        }
        return false;
    }

    // Calculate swap for a position
    double CalculateSwap(
        double lot_size,
        bool is_buy,
        double swap_long,   // Points (from symbol specification)
        double swap_short,  // Points (from symbol specification)
        double point_value,
        int day_of_week
    ) {
        double swap_points = is_buy ? swap_long : swap_short;

        // Triple swap on Wednesday (for weekend)
        if (day_of_week == 3) {  // Wednesday
            swap_points *= 3.0;
        }

        // Convert points to money
        return lot_size * swap_points * point_value * 100000.0;  // Standard lot
    }

    void Reset() {
        last_swap_day_ = 0;
    }
};

#endif // SWAP_MANAGER_H
```

**Integration:**
- Add to `BacktestEngine` main loop
- Call `ShouldApplySwap()` on each bar/tick
- Apply swap to all open positions when triggered

---

## Priority 2: Slippage Model (Week 1)

### Task 2.1: Update Slippage Logic

**File to modify:** `include/slippage_model.h` (or create if doesn't exist)

```cpp
#ifndef SLIPPAGE_MODEL_H
#define SLIPPAGE_MODEL_H

class SlippageModel {
public:
    enum Mode {
        NONE = 0,           // MT5 Strategy Tester mode (0 slippage)
        FIXED = 1,          // Fixed slippage in points
        RANDOM = 2,         // Random slippage (normal distribution)
        REALISTIC = 3       // Spread-based slippage model
    };

private:
    Mode mode_;
    double fixed_slippage_points_;
    double random_mean_;
    double random_std_;

public:
    SlippageModel(Mode mode = NONE)
        : mode_(mode),
          fixed_slippage_points_(0.0),
          random_mean_(0.0),
          random_std_(0.0) {}

    double GetSlippage(bool is_buy, double spread_points) {
        switch (mode_) {
            case NONE:
                return 0.0;  // MT5 Tester behavior

            case FIXED:
                return is_buy ? fixed_slippage_points_ : -fixed_slippage_points_;

            case RANDOM:
                // Implement with std::normal_distribution
                return 0.0;  // TODO: Add RNG

            case REALISTIC:
                // Slippage based on spread
                return is_buy ? spread_points * 0.5 : -spread_points * 0.5;

            default:
                return 0.0;
        }
    }

    void SetMode(Mode mode) { mode_ = mode; }
    void SetFixedSlippage(double points) { fixed_slippage_points_ = points; }
};

#endif // SLIPPAGE_MODEL_H
```

**Integration:**
- Add `SlippageModel` to `BacktestEngine`
- Default to `NONE` mode for MT5 reproduction
- Allow configuration via command-line or config file

---

## Priority 3: Testing Updated Engine (Week 2)

### Task 3.1: Create C++ Test for Margin

**File to create:** `validation/cpp_tests/test_margin_manager.cpp`

```cpp
#include "../include/margin_manager.h"
#include <cassert>
#include <cmath>
#include <iostream>

void test_margin_calculation() {
    // Test case from Test F results
    double lot_size = 0.01;
    double contract_size = 100000.0;
    double price = 1.15958;
    int leverage = 500;

    double margin = MarginManager::CalculateMargin(
        lot_size, contract_size, price, leverage
    );

    double expected = 2.31916;  // (0.01 * 100000 * 1.15958) / 500
    double diff = std::abs(margin - expected);

    std::cout << "Test: Margin Calculation" << std::endl;
    std::cout << "  Expected: $" << expected << std::endl;
    std::cout << "  Got:      $" << margin << std::endl;
    std::cout << "  Diff:     $" << diff << std::endl;

    assert(diff < 0.01);
    std::cout << "  [PASS]" << std::endl;
}

void test_margin_check() {
    double balance = 10000.0;
    double current_margin = 100.0;
    double required_margin = 50.0;

    bool sufficient = MarginManager::HasSufficientMargin(
        balance, current_margin, required_margin, 100.0
    );

    // Margin level = (10000 / 150) * 100 = 6666.67%
    assert(sufficient == true);

    std::cout << "Test: Margin Check (Sufficient)" << std::endl;
    std::cout << "  [PASS]" << std::endl;

    // Test insufficient margin
    current_margin = 9000.0;
    required_margin = 2000.0;
    sufficient = MarginManager::HasSufficientMargin(
        balance, current_margin, required_margin, 100.0
    );

    // Margin level = (10000 / 11000) * 100 = 90.9%
    assert(sufficient == false);

    std::cout << "Test: Margin Check (Insufficient)" << std::endl;
    std::cout << "  [PASS]" << std::endl;
}

int main() {
    test_margin_calculation();
    test_margin_check();

    std::cout << std::endl;
    std::cout << "All tests passed!" << std::endl;

    return 0;
}
```

**Compile and run:**
```bash
g++ -std=c++17 validation/cpp_tests/test_margin_manager.cpp -o test_margin
./test_margin
```

---

### Task 3.2: Run Integration Test

**Compare our engine vs MT5 on simple strategy:**

1. Create simple test strategy:
   - Open 1 position per day
   - Hold for 24 hours
   - Close

2. Run in both engines:
   - MT5 Strategy Tester
   - Our C++ engine

3. Compare:
   - Final balance
   - Margin usage
   - Swap charges
   - Number of trades

4. Target: **<1% difference**

---

## Priority 4: Tick Generation (Weeks 3-4)

### Task 4.1: Analyze Tick Patterns

**Script to create:** `validation/analyze_tick_patterns.py`

Analyze the 19MB tick dataset to extract:
- Price path: Does it go O→H→L→C or O→L→H→C?
- Timing distribution: Linear? Random? Exponential?
- Tick density: How many ticks in different bar phases?

### Task 4.2: Implement Tick Generator

**File to create:** `include/tick_generator.h`

Based on analysis findings, generate ~1,900 ticks per H1 bar with realistic price paths.

---

## Verification Checklist

After implementing each component:

- [ ] **Margin Manager**
  - [ ] Formula matches MT5: `(lot * 100000 * price) / leverage`
  - [ ] Margin check prevents overleveraging
  - [ ] Unit tests pass

- [ ] **Swap Manager**
  - [ ] Applied at 00:00 server time
  - [ ] Only applied once per day
  - [ ] Triple swap on Wednesday (if applicable)
  - [ ] Swap calculation uses correct formula

- [ ] **Slippage Model**
  - [ ] Zero slippage in MT5 Tester mode
  - [ ] Configurable for other modes
  - [ ] Applied correctly to buy/sell orders

- [ ] **Integration Test**
  - [ ] Simple backtest matches MT5 within 1%
  - [ ] All trades executed correctly
  - [ ] Account balance tracks accurately

---

## Timeline Summary

| Week | Tasks | Deliverables |
|------|-------|--------------|
| 1 | Margin & Swap managers, Zero slippage | 3 new header files, unit tests |
| 2 | Integration testing, compare vs MT5 | Test results, validation report |
| 3-4 | Tick generation analysis & implementation | Tick generator, detailed comparison |

---

## Success Criteria

✅ **Phase 1 Complete when:**
- MarginManager implemented and tested
- SwapManager implemented and tested
- SlippageModel set to zero
- Simple backtest matches MT5 within 1%

✅ **Phase 2 Complete when:**
- Tick generator produces ~1,900 ticks/bar
- Price paths match MT5 patterns
- Complex backtest matches MT5 within 1%

✅ **Final Validation:**
- Run all Tests A-F using OUR engine
- Compare our results vs MT5 results
- Achieve <1% difference across all tests

---

**Current Status:** Validation complete, ready for implementation
**Next Action:** Create `include/margin_manager.h` and unit test
