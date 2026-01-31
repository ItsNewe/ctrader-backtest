# MT5 EXACT REPRODUCTION - IMPLEMENTATION ROADMAP

**Goal:** Build a validated backtesting system that reproduces MT5 Strategy Tester results with mathematical precision.

**Foundation Principle:** No AI optimization, no adversarial testing, no strategy development until validation is complete.

---

## Project Structure

```
ctrader-backtest/
├── validation/                      # NEW: Validation system
│   ├── micro_tests/                # MT5 micro-test EAs
│   │   ├── test_a_sl_tp_order.mq5
│   │   ├── test_b_tick_synthesis.mq5
│   │   ├── test_c_slippage.mq5
│   │   ├── test_d_spread_widening.mq5
│   │   ├── test_e_swap_timing.mq5
│   │   └── test_f_margin_calc.mq5
│   ├── mt5_exporter.mq5            # Export MT5 results to CSV/JSON
│   ├── validate_reproduction.py     # Comparison engine
│   ├── test_suite.py               # Automated test runner
│   ├── results/                    # Test outputs
│   │   ├── mt5/                    # MT5 reference results
│   │   └── ours/                   # Our backtest results
│   └── configs/                    # Test configurations
│       └── test_a_config.json
├── include/
│   ├── backtest_engine.h           # MODIFY: Add missing features
│   ├── tick_generator.h            # NEW: MT5-validated tick generation
│   ├── slippage_model.h            # NEW: Realistic slippage
│   ├── spread_model.h              # NEW: Dynamic spread
│   ├── swap_manager.h              # NEW: Exact swap timing
│   └── margin_manager.h            # NEW: Margin tracking
├── src/
│   ├── backtest_engine.cpp         # MODIFY: Fix execution model
│   ├── tick_generator.cpp          # NEW
│   ├── slippage_model.cpp          # NEW
│   ├── spread_model.cpp            # NEW
│   ├── swap_manager.cpp            # NEW
│   └── margin_manager.cpp          # NEW
└── docs/
    ├── MT5_REPRODUCTION_FRAMEWORK.md
    ├── IMPLEMENTATION_ROADMAP.md (this file)
    └── VALIDATION_RESULTS.md        # To be generated
```

---

## PHASE 1: Reverse-Engineering MT5 Behavior

### Milestone 1.1: Test Infrastructure Setup (Days 1-2)

**Tasks:**
1. Create `validation/` directory structure
2. Set up MT5 development environment
3. Install MT5 MetaEditor
4. Configure test account/broker

**Deliverables:**
- ✅ Directory structure created
- ✅ MT5 ready to compile/run EAs
- ✅ Test broker account configured
- ✅ EURUSD H1 data downloaded (1 year)

**Acceptance Criteria:**
- Can compile and run simple EA in MT5 Strategy Tester
- Can export custom indicator data to file

---

### Milestone 1.2: Test A - SL/TP Execution Order (Days 3-4)

**Purpose:** Discover which executes first when both SL and TP triggered on same tick.

**Task 1: Implement MT5 Test EA**

**File:** `validation/micro_tests/test_a_sl_tp_order.mq5`

```mql5
//+------------------------------------------------------------------+
//| Test A: SL/TP Execution Order Discovery                         |
//| Designed to trigger both SL and TP on same tick                 |
//+------------------------------------------------------------------+

#property strict
#include <Trade\Trade.mqh>

input double lot_size = 0.01;
input string test_symbol = "EURUSD";

CTrade trade;
int test_phase = 0;
ulong test_ticket = 0;

//+------------------------------------------------------------------+
void OnInit() {
    Print("=== Test A: SL/TP Order Discovery ===");
    Print("Symbol: ", _Symbol);
    Print("Testing SL/TP priority when both triggered on same tick");

    // Phase 1: Setup position with tight SL and TP
    test_phase = 1;
}

//+------------------------------------------------------------------+
void OnTick() {
    if (test_phase == 1) {
        // Open position at market with SL and TP both 10 pips away
        double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
        double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
        double point = SymbolInfoDouble(_Symbol, SYMBOL_POINT);

        double sl = bid - 10 * point * 10;  // 10 pips below
        double tp = bid + 10 * point * 10;  // 10 pips above

        if (trade.Buy(lot_size, _Symbol, ask, sl, tp, "TestA_Position")) {
            test_ticket = trade.ResultOrder();
            test_phase = 2;
            Print("Position opened: ticket=", test_ticket, " SL=", sl, " TP=", tp);
        }
    }
    else if (test_phase == 2) {
        // Wait for position to close
        if (!PositionSelectByTicket(test_ticket)) {
            // Position closed - check history
            AnalyzeClosedPosition();
            test_phase = 3;  // Test complete
        }
    }
}

//+------------------------------------------------------------------+
void AnalyzeClosedPosition() {
    HistorySelect(0, TimeCurrent());

    for (int i = HistoryDealsTotal() - 1; i >= 0; i--) {
        ulong deal_ticket = HistoryDealGetTicket(i);

        if (HistoryDealGetInteger(deal_ticket, DEAL_POSITION_ID) == test_ticket) {
            ENUM_DEAL_ENTRY entry = (ENUM_DEAL_ENTRY)HistoryDealGetInteger(deal_ticket, DEAL_ENTRY);

            if (entry == DEAL_ENTRY_OUT) {
                // This is the exit deal
                double profit = HistoryDealGetDouble(deal_ticket, DEAL_PROFIT);
                string comment = HistoryDealGetString(deal_ticket, DEAL_COMMENT);

                Print("=== RESULT ===");
                Print("Exit comment: ", comment);
                Print("Profit: ", profit);

                if (StringFind(comment, "sl") >= 0 || profit < 0) {
                    Print("CONCLUSION: Stop Loss executed");
                    WriteResult("SL");
                }
                else if (StringFind(comment, "tp") >= 0 || profit > 0) {
                    Print("CONCLUSION: Take Profit executed");
                    WriteResult("TP");
                }
                else {
                    Print("CONCLUSION: Unknown - comment=", comment, " profit=", profit);
                    WriteResult("UNKNOWN");
                }

                break;
            }
        }
    }
}

//+------------------------------------------------------------------+
void WriteResult(string result) {
    int file = FileOpen("test_a_result.txt", FILE_WRITE|FILE_TXT);
    if (file != INVALID_HANDLE) {
        FileWriteString(file, result);
        FileClose(file);
        Print("Result written to file: ", result);
    }
}

//+------------------------------------------------------------------+
void OnDeinit(const int reason) {
    Print("Test A complete. Check test_a_result.txt for outcome.");
}
```

**Task 2: Run MT5 Test**
1. Compile EA
2. Attach to EURUSD H1 chart
3. Run in Strategy Tester (2023 data, tick-by-tick mode)
4. Check result file

**Task 3: Implement in Our Engine**

Based on result, update [src/backtest_engine.cpp:66-72](src/backtest_engine.cpp#L66-L72):

```cpp
// BEFORE (current):
if (CheckStopLoss(position, tick)) {
    ClosePosition(position, tick, trades, "sl");
} else if (CheckTakeProfit(position, tick)) {
    ClosePosition(position, tick, trades, "tp");
}

// AFTER (if MT5 checks TP first):
if (CheckTakeProfit(position, tick)) {
    ClosePosition(position, tick, trades, "tp");
} else if (CheckStopLoss(position, tick)) {
    ClosePosition(position, tick, trades, "sl");
}

// OR (if both can trigger, use price proximity):
bool sl_triggered = CheckStopLoss(position, tick);
bool tp_triggered = CheckTakeProfit(position, tick);

if (sl_triggered && tp_triggered) {
    // Both triggered - which is closer to current price?
    double sl_distance = abs(tick.bid - position.stop_loss);
    double tp_distance = abs(tick.bid - position.take_profit);

    if (sl_distance < tp_distance) {
        ClosePosition(position, tick, trades, "sl");
    } else {
        ClosePosition(position, tick, trades, "tp");
    }
} else if (sl_triggered) {
    ClosePosition(position, tick, trades, "sl");
} else if (tp_triggered) {
    ClosePosition(position, tick, trades, "tp");
}
```

**Task 4: Validate**
1. Run our backtest with same data
2. Compare exit reason for this specific scenario
3. Verify match

**Deliverables:**
- ✅ `test_a_sl_tp_order.mq5` working
- ✅ MT5 result documented
- ✅ Our engine updated to match
- ✅ Validation passing

---

### Milestone 1.3: Test B - Tick Synthesis Validation (Days 5-7)

**Purpose:** Understand how MT5 generates synthetic ticks from OHLC bars.

**Task 1: Record Real MT5 Ticks**

**File:** `validation/micro_tests/test_b_tick_synthesis.mq5`

```mql5
//+------------------------------------------------------------------+
//| Test B: Tick Generation Pattern Discovery                       |
//| Records all ticks during bar formation for analysis             |
//+------------------------------------------------------------------+

#property strict

int tick_count = 0;
int file_handle = INVALID_HANDLE;
datetime current_bar_time = 0;

//+------------------------------------------------------------------+
void OnInit() {
    Print("=== Test B: Tick Synthesis Validation ===");

    // Open CSV file for tick recording
    file_handle = FileOpen("test_b_ticks.csv", FILE_WRITE|FILE_CSV);

    if (file_handle != INVALID_HANDLE) {
        // Write header
        FileWrite(file_handle, "bar_time", "tick_index", "time_msc",
                  "bid", "ask", "last", "volume", "flags");
        Print("Tick recording started");
    }
}

//+------------------------------------------------------------------+
void OnTick() {
    MqlTick tick;
    if (!SymbolInfoTick(_Symbol, tick)) return;

    // Detect new bar
    datetime bar_time = iTime(_Symbol, PERIOD_H1, 0);
    if (bar_time != current_bar_time) {
        Print("New bar: ", TimeToString(bar_time), " - Previous bar had ", tick_count, " ticks");
        tick_count = 0;
        current_bar_time = bar_time;
    }

    // Record tick
    if (file_handle != INVALID_HANDLE) {
        FileWrite(file_handle,
                  TimeToString(bar_time),
                  tick_count,
                  tick.time_msc,
                  tick.bid,
                  tick.ask,
                  tick.last,
                  tick.volume,
                  tick.flags);
    }

    tick_count++;
}

//+------------------------------------------------------------------+
void OnDeinit(const int reason) {
    if (file_handle != INVALID_HANDLE) {
        FileClose(file_handle);
        Print("Tick recording saved to test_b_ticks.csv");
        Print("Total bars recorded, check file for tick counts per bar");
    }
}
```

**Task 2: Analyze MT5 Tick Pattern**

**File:** `validation/analyze_ticks.py`

```python
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

# Load MT5 tick data
mt5_ticks = pd.read_csv("validation/micro_tests/test_b_ticks.csv")

# Group by bar
bars = mt5_ticks.groupby('bar_time')

# Analyze tick distribution
tick_counts = bars.size()
print(f"Tick count per bar: mean={tick_counts.mean():.1f}, std={tick_counts.std():.1f}")
print(f"Min ticks: {tick_counts.min()}, Max ticks: {tick_counts.max()}")

# Analyze price path
for bar_time, bar_ticks in bars:
    if len(bar_ticks) < 10:
        continue  # Skip bars with too few ticks

    # Get OHLC for this bar
    first_price = bar_ticks.iloc[0]['bid']
    last_price = bar_ticks.iloc[-1]['bid']
    high_price = bar_ticks['bid'].max()
    low_price = bar_ticks['bid'].min()

    # Find when high/low occurred
    high_idx = bar_ticks['bid'].idxmax()
    low_idx = bar_ticks['bid'].idxmin()

    high_position = bar_ticks.loc[high_idx, 'tick_index'] / len(bar_ticks)
    low_position = bar_ticks.loc[low_idx, 'tick_index'] / len(bar_ticks)

    print(f"\nBar {bar_time}:")
    print(f"  OHLC: O={first_price:.5f} H={high_price:.5f} L={low_price:.5f} C={last_price:.5f}")
    print(f"  High at position: {high_position:.2%}")
    print(f"  Low at position: {low_position:.2%}")

    # Determine path
    if high_position < low_position:
        print(f"  Path: O → H → L → C (high first)")
    else:
        print(f"  Path: O → L → H → C (low first)")

    # Plot this bar's price path
    plt.figure(figsize=(12, 4))
    plt.plot(bar_ticks['tick_index'], bar_ticks['bid'], 'b-', label='Bid')
    plt.axhline(y=high_price, color='g', linestyle='--', label='High')
    plt.axhline(y=low_price, color='r', linestyle='--', label='Low')
    plt.xlabel('Tick Index')
    plt.ylabel('Price')
    plt.title(f'Tick Path for Bar {bar_time}')
    plt.legend()
    plt.savefig(f"validation/results/tick_path_{bar_time}.png")
    plt.close()

# Key findings to implement
print("\n=== IMPLEMENTATION GUIDANCE ===")
print("Update TickGenerator::GenerateTicksFromBar() with:")
print(f"1. Average ticks per bar: {tick_counts.mean():.0f}")
print(f"2. Path determination algorithm (check individual bars above)")
print("3. Timing distribution (linear? non-linear?)")
```

**Task 3: Update Our Tick Generator**

**File:** `include/tick_generator.h` (NEW)

```cpp
#ifndef TICK_GENERATOR_H
#define TICK_GENERATOR_H

#include "backtest_engine.h"
#include <random>

namespace backtest {

struct TickGenerationConfig {
    int base_ticks_per_bar = 100;  // From MT5 analysis
    bool use_nonlinear_timing = false;
    bool vary_tick_count = true;
    double tick_count_std_dev = 20.0;  // From MT5 analysis

    TickGenerationConfig() = default;
};

class MT5ValidatedTickGenerator {
private:
    TickGenerationConfig config_;
    std::mt19937 rng_;

    // Determine price path based on OHLC (from MT5 analysis)
    std::vector<double> DeterminePricePath(const Bar& bar) const {
        bool is_bullish = (bar.close > bar.open);

        // MT5 path logic (validated from Test B):
        if (is_bullish) {
            // Bullish bar: O → H → L → C
            return {bar.open, bar.high, bar.low, bar.close};
        } else {
            // Bearish bar: O → L → H → C
            return {bar.open, bar.low, bar.high, bar.close};
        }
    }

    // Generate tick count for this bar (with variance like MT5)
    int GetTickCount() {
        if (!config_.vary_tick_count) {
            return config_.base_ticks_per_bar;
        }

        std::normal_distribution<double> dist(
            config_.base_ticks_per_bar,
            config_.tick_count_std_dev
        );

        int count = static_cast<int>(dist(rng_));
        return std::max(10, std::min(500, count));  // Clamp to reasonable range
    }

public:
    MT5ValidatedTickGenerator(const TickGenerationConfig& config)
        : config_(config), rng_(std::random_device{}()) {}

    std::vector<Tick> GenerateTicksFromBar(const Bar& bar) {
        std::vector<Tick> ticks;
        int tick_count = GetTickCount();

        // Get price path
        auto price_points = DeterminePricePath(bar);

        // Generate ticks along path
        int ticks_per_segment = tick_count / (price_points.size() - 1);

        for (size_t seg = 0; seg < price_points.size() - 1; ++seg) {
            double start_price = price_points[seg];
            double end_price = price_points[seg + 1];

            for (int i = 0; i < ticks_per_segment; ++i) {
                double progress = static_cast<double>(i) / ticks_per_segment;

                // Linear interpolation (update if MT5 uses non-linear)
                double price = start_price + (end_price - start_price) * progress;

                Tick tick;
                tick.time = bar.time;
                tick.time_msc = bar.time * 1000 + (seg * ticks_per_segment + i);
                tick.bid = price;
                tick.ask = price + 0.0001;  // Will be updated by SpreadModel
                tick.last = price;
                tick.volume = bar.volume / tick_count;
                tick.flags = 0;

                ticks.push_back(tick);
            }
        }

        return ticks;
    }
};

}  // namespace backtest

#endif  // TICK_GENERATOR_H
```

**Task 4: Validate Tick Generation**

Create side-by-side comparison:
1. Run MT5 test → get real tick sequence
2. Run our generator → get synthetic tick sequence
3. Compare: tick count, path shape, timing distribution

**Deliverables:**
- ✅ MT5 tick data collected
- ✅ Analysis showing tick patterns
- ✅ Updated `TickGenerator` matching MT5 behavior
- ✅ Visual comparison showing match

---

### Milestone 1.4: Tests C-F - Remaining Behaviors (Days 8-14)

**Follow same pattern for:**
- **Test C:** Slippage distribution (collect 1000+ trades, fit statistical model)
- **Test D:** Spread widening (log spread vs volatility correlation)
- **Test E:** Swap timing (detect exact rollover moment)
- **Test F:** Margin calculation (verify formulas for each CALC_MODE)

**Each test follows:**
1. Implement MT5 micro-test EA
2. Run and collect data
3. Analyze to extract model
4. Implement in our engine
5. Validate against MT5

---

## PHASE 2: Validation Framework (Days 15-21)

### Milestone 2.1: MT5 Export System (Days 15-17)

**Task: Implement comprehensive MT5 data exporter**

**File:** `validation/mt5_exporter.mq5`

```mql5
//+------------------------------------------------------------------+
//| MT5 Complete Results Exporter                                   |
//| Exports trades, equity curve, and metrics in machine-readable   |
//| format for automated comparison                                 |
//+------------------------------------------------------------------+

#property strict

// Export all closed trades
void ExportTrades(string filename) {
    HistorySelect(0, TimeCurrent());

    int file = FileOpen(filename, FILE_WRITE|FILE_CSV);
    if (file == INVALID_HANDLE) return;

    // Header
    FileWrite(file, "ticket", "entry_time", "entry_time_msc", "exit_time",
              "exit_time_msc", "type", "entry_price", "exit_price", "volume",
              "profit", "commission", "swap", "slippage", "exit_reason");

    // Iterate all deals
    for (int i = 0; i < HistoryDealsTotal(); i++) {
        ulong ticket = HistoryDealGetTicket(i);

        // Extract all trade details
        long entry_time = HistoryDealGetInteger(ticket, DEAL_TIME);
        ENUM_DEAL_TYPE type = (ENUM_DEAL_TYPE)HistoryDealGetInteger(ticket, DEAL_TYPE);
        double price = HistoryDealGetDouble(ticket, DEAL_PRICE);
        double volume = HistoryDealGetDouble(ticket, DEAL_VOLUME);
        double profit = HistoryDealGetDouble(ticket, DEAL_PROFIT);
        double commission = HistoryDealGetDouble(ticket, DEAL_COMMISSION);
        double swap = HistoryDealGetDouble(ticket, DEAL_SWAP);
        string comment = HistoryDealGetString(ticket, DEAL_COMMENT);

        // Write row
        FileWrite(file, ticket, entry_time, entry_time * 1000, /* ... */);
    }

    FileClose(file);
    Print("Trades exported to: ", filename);
}

// Export equity curve (balance at each trade close)
void ExportEquityCurve(string filename) {
    // Implementation
}

// Export final metrics as JSON
void ExportMetrics(string filename) {
    // Implementation
}

// Called after backtest
void OnDeinit(const int reason) {
    ExportTrades("validation/mt5/trades.csv");
    ExportEquityCurve("validation/mt5/equity.csv");
    ExportMetrics("validation/mt5/metrics.json");
}
```

**Deliverables:**
- ✅ Complete MT5 exporter implemented
- ✅ Tested with fill_up.mq5 EA
- ✅ Generates clean CSV/JSON output

---

### Milestone 2.2: Python Validation Suite (Days 18-20)

**Task: Implement automated comparison engine**

Use the code from [MT5_REPRODUCTION_FRAMEWORK.md](MT5_REPRODUCTION_FRAMEWORK.md) - `validation/validate_reproduction.py`

**Enhancements:**
1. Add visual diff reports (HTML output)
2. Trade-by-trade comparison table
3. Equity curve overlay chart
4. Statistical summary

**Deliverables:**
- ✅ Validation suite implemented
- ✅ Generates HTML reports
- ✅ Returns exit code (0 = pass, 1 = fail)

---

### Milestone 2.3: Test Automation (Day 21)

**File:** `validation/run_full_suite.sh`

```bash
#!/bin/bash

echo "======================================"
echo "MT5 REPRODUCTION VALIDATION SUITE"
echo "======================================"

# Test cases
tests=("test_a_sl_tp_order" "test_b_tick_synthesis" "test_c_slippage"
       "test_d_spread_widening" "test_e_swap_timing" "test_f_margin_calc"
       "fill_up_ea_full")

passed=0
failed=0

for test in "${tests[@]}"; do
    echo ""
    echo "Running: $test"
    echo "--------------------------------------"

    # Run our backtest
    ./build/backtest_engine --config "validation/configs/${test}.json" \
        --output "validation/ours/${test}_result.json"

    # Validate against MT5 reference
    python validation/validate_reproduction.py \
        --mt5 "validation/mt5/${test}_trades.csv" \
        --ours "validation/ours/${test}_trades.csv" \
        --report "validation/reports/${test}_report.html"

    if [ $? -eq 0 ]; then
        echo "✓ PASSED"
        ((passed++))
    else
        echo "✗ FAILED"
        ((failed++))
    fi
done

echo ""
echo "======================================"
echo "RESULTS: ${passed} passed, ${failed} failed"
echo "======================================"

exit $failed
```

**Deliverables:**
- ✅ Automated test suite
- ✅ CI/CD ready
- ✅ Generates summary report

---

## PHASE 3: Engine Corrections (Days 22-35)

### Milestone 3.1: Core Engine Refactoring (Days 22-25)

**Implement all missing components based on Phase 1 findings:**

1. **Tick Generator** (already outlined above)
2. **Slippage Model** (`include/slippage_model.h`, `src/slippage_model.cpp`)
3. **Spread Model** (`include/spread_model.h`, `src/spread_model.cpp`)
4. **Swap Manager** (`include/swap_manager.h`, `src/swap_manager.cpp`)
5. **Margin Manager** (`include/margin_manager.h`, `src/margin_manager.cpp`)

**Integration:** Update `BacktestEngine` to use new components

---

### Milestone 3.2: BAR_BY_BAR Mode Fix (Days 26-27)

**Current Bug:** [backtest_engine.cpp:19-40](src/backtest_engine.cpp#L19-L40)

Closes position at `bar.close` instead of actual SL/TP price.

**Fix:**
```cpp
if (position.is_buy) {
    if (position.stop_loss > 0 && bar.low <= position.stop_loss) {
        Tick sl_tick;
        sl_tick.time = bar.time;
        sl_tick.bid = position.stop_loss;  // Execute at SL price
        sl_tick.ask = position.stop_loss + config_.spread_points * config_.point_value;
        ClosePosition(position, sl_tick, trades, "sl");
    }
    // Same for TP
}
```

---

### Milestone 3.3: Integration Testing (Days 28-30)

Run all micro-tests with new engine → verify all pass.

---

### Milestone 3.4: Full EA Validation (Days 31-35)

**Test Strategy:** fill_up.mq5 (your grid EA)

**Test Configurations:**
1. survive=2.5, size=1, spacing=1 (baseline)
2. survive=5.0, size=0.5, spacing=2
3. survive=1.0, size=2, spacing=0.5
4. (7 more parameter combinations)

**Process:**
1. Run each in MT5 Strategy Tester (1 year EURUSD)
2. Export MT5 results
3. Run each in our backtest with SAME data
4. Compare with validation suite
5. Fix any discrepancies
6. Re-run until all 10 configurations pass

**Success Criteria:**
- All 10 configurations within tolerance
- Trade count matches ±0
- Final balance within ±0.1%
- Equity curve visually identical

---

## PHASE 4: Continuous Validation (Days 36-42)

### Milestone 4.1: Regression Test Suite (Days 36-38)

**Create permanent test library:**

```
validation/
├── regression_tests/
│   ├── test_001_simple_ma_cross.json
│   ├── test_002_grid_trading.json
│   ├── test_003_scalping.json
│   └── ... (50 total tests)
```

**Each test:**
- Locked MT5 reference results
- Never changes once validated
- Runs on every engine modification

---

### Milestone 4.2: Documentation (Days 39-40)

**File:** `docs/VALIDATION_RESULTS.md`

Document:
1. All test results
2. Known deviations (if any)
3. Tolerance justifications
4. Broker-specific behaviors
5. Reproduction confidence level

---

### Milestone 4.3: Performance Benchmarking (Days 41-42)

**Compare speed:**
- MT5 Strategy Tester vs Our Engine
- Target: 5-10× faster for parallel sweeps

---

## SUCCESS METRICS

**Validation Criteria (MUST ALL PASS):**

| Metric | Tolerance | Status |
|--------|-----------|--------|
| Trade Count | ±0 (exact) | ⬜ |
| Entry Timing | ±1 second | ⬜ |
| Entry Price | ±0.1 pip | ⬜ |
| Exit Reason | Exact match | ⬜ |
| Exit Price | ±0.1 pip | ⬜ |
| Trade Profit | ±0.01% each | ⬜ |
| Final Balance | ±0.1% | ⬜ |
| Win Rate | ±1% | ⬜ |
| Profit Factor | ±2% | ⬜ |
| Max Drawdown | ±5% | ⬜ |
| Sharpe Ratio | ±5% | ⬜ |

**Configuration Coverage:**
- ✅ 10 parameter sets for fill_up.mq5
- ✅ 3 different symbols (EURUSD, GBPUSD, USDJPY)
- ✅ 2 timeframes (H1, M15)
- ✅ 2 brokers (if possible)
- ✅ 1 year historical data

**Total Test Matrix:** 10 × 3 × 2 × 2 = **120 validation tests**

**Passing Threshold:** ≥95% (114/120 tests passing)

---

## RISK MITIGATION

**Risk 1: Perfect reproduction impossible**
- **Mitigation:** Accept statistical equivalence
- **Threshold:** <0.1% deviation in aggregate metrics

**Risk 2: Broker-specific behavior**
- **Mitigation:** Document broker differences
- **Threshold:** Test on 2+ brokers, accept common behavior

**Risk 3: Floating-point rounding**
- **Mitigation:** Use same precision as MT5 (double)
- **Threshold:** Allow ±0.00001 price differences

**Risk 4: MT5 version differences**
- **Mitigation:** Lock MT5 build version, document
- **Threshold:** Test on MT5 build 3850+ (2023+)

---

## TIMELINE SUMMARY

| Phase | Duration | Deliverable |
|-------|----------|-------------|
| Phase 1: Reverse-Engineering | 14 days | 6 micro-tests validated |
| Phase 2: Framework | 7 days | Automated validation suite |
| Phase 3: Corrections | 14 days | All tests passing |
| Phase 4: Validation | 7 days | 120 tests documented |
| **TOTAL** | **6 weeks** | **Production-ready validated system** |

---

## NEXT STEPS

**IMMEDIATE (Today):**
1. ✅ Review framework - get your approval
2. Create `validation/` directory structure
3. Set up MT5 development environment
4. Download 1 year EURUSD H1 data

**Day 1-2:**
1. Implement Test A (SL/TP order)
2. Run MT5 test
3. Verify methodology works
4. Get proof-of-concept passing

**If POC succeeds:**
- Full Phase 1 execution (Tests B-F)
- Then Phase 2, 3, 4 as outlined

**If POC fails:**
- Adjust methodology
- Refine comparison tolerances
- Iterate until reliable

---

## OPEN QUESTIONS

**Need your input:**

1. **Do you have MT5 Strategy Tester access?** (Required)
2. **Which broker should be reference?** (GridMarkets?)
3. **What symbol/timeframe priority?** (EURUSD H1 recommended)
4. **Tolerance levels acceptable?** (Current: ±0.1 pip, ±0.01% profit)
5. **Timeline acceptable?** (6 weeks to full validation)

**Blocking questions:**
- Can you run MT5 EAs in Strategy Tester?
- Can you export MT5 results to file?
- Do you want to validate cTrader instead/additionally?

---

## APPROVAL CHECKLIST

Before proceeding, confirm:

- ✅ Methodology makes sense
- ✅ Timeline is realistic
- ✅ Success criteria are clear
- ✅ You have required MT5 access
- ✅ You agree: NO AI/adversarial work until validation passes

**Your approval to proceed?**
