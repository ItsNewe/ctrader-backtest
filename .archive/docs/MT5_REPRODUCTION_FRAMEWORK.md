# MT5 EXACT REPRODUCTION FRAMEWORK

**Critical Foundation Requirement:** Without MT5 exact reproduction, all subsequent work (AI optimization, adversarial testing, parameter sweeps) is meaningless.

## Current State Analysis

### What We Have ✅
1. **MT5 Broker Integration** ([broker_api.py](broker_api.py))
   - Symbol specification fetching (pip_value, contract_size, swap rates)
   - Price history retrieval (OHLC bars from MT5 terminal)
   - Account validation

2. **C++ Backtest Engine** ([include/backtest_engine.h](include/backtest_engine.h), [src/backtest_engine.cpp](src/backtest_engine.cpp))
   - Three execution modes: BAR_BY_BAR, EVERY_TICK, EVERY_TICK_OHLC
   - Position tracking with SL/TP execution
   - Slippage, commission, swap calculations
   - Unrealized P&L tracking

3. **Example EA** ([example/fill_up.mq5](example/fill_up.mq5))
   - Grid trading strategy (survive, size, spacing parameters)
   - Position management logic
   - Complex sizing calculations based on margin

### Critical Gaps ❌

#### 1. **NO VALIDATION SYSTEM**
- No MT5 Strategy Tester comparison tests
- No automated verification that our results match MT5
- No test harness for reproduction guarantees
- Cannot prove bit-exact reproduction

#### 2. **INCOMPLETE EXECUTION MODEL**
**Current Implementation Issues:**

**a) Stop Loss/Take Profit Execution Order**
- **MT5 Behavior:** Processes SL/TP in SPECIFIC sequence on each tick
- **Our Engine:** [backtest_engine.cpp:66-72](src/backtest_engine.cpp#L66-L72)
  ```cpp
  if (CheckStopLoss(position, tick)) {
      ClosePosition(position, tick, trades, "sl");
  } else if (CheckTakeProfit(position, tick)) {
      ClosePosition(position, tick, trades, "tp");
  }
  ```
- **Gap:** We check SL first, then TP. Does MT5 do the same? Unknown.
- **Impact:** Different execution order = different trade sequence = different results

**b) Tick Generation from OHLC**
- **MT5 Behavior:** Uses proprietary tick synthesis algorithm
- **Our Engine:** [backtest_engine.h:226-272](include/backtest_engine.h#L226-L272)
  - Determines path: O→H→L→C (bullish) or O→L→H→C (bearish)
  - Linear interpolation with 100 ticks/bar
  - Simplified spread (fixed 0.0001)
- **Gaps:**
  - MT5 may use non-linear interpolation
  - MT5 models spread widening during volatility
  - MT5 may use different tick count
  - MT5 may use different path algorithm
- **Impact:** Different synthetic ticks = different SL/TP triggers = different results

**c) Slippage Model**
- **MT5 Behavior:** Complex slippage based on liquidity, volatility, broker model
- **Our Engine:** [backtest_engine.h:440-448](include/backtest_engine.h#L440-L448)
  ```cpp
  int slippage = rand() % (config.max_slippage_points + 1);
  return is_buy ? slippage : -slippage;
  ```
- **Gap:** Random uniform distribution ≠ MT5's model
- **Impact:** Different slippage = different entry/exit prices = different profit

**d) Spread Modeling**
- **MT5 Behavior:** Variable spread based on time, news events, liquidity
- **Our Engine:** Fixed `spread_points` in config
- **Gap:** No spread widening during high volatility
- **Impact:** Different spreads = different entry costs = different results

**e) Commission Calculation**
- **MT5 Behavior:** May vary by volume tier, time of day, broker settings
- **Our Engine:** [backtest_engine.cpp:514](src/backtest_engine.cpp#L514)
  ```cpp
  trade.commission = config_.commission_per_lot * trade.volume * 2;
  ```
- **Gap:** Fixed commission per lot, no volume tiers
- **Impact:** Different costs = different net profit

**f) Swap Calculation**
- **MT5 Behavior:**
  - Applied at specific rollover time (typically 00:00 broker time)
  - Wednesday may have triple swap
  - Different rates for different instruments
- **Our Engine:** [backtest_engine.cpp:517-520](src/backtest_engine.cpp#L517-L520)
  ```cpp
  double days_held = trade.GetDurationSeconds() / 86400.0;
  trade.swap = trade.is_buy ? (config_.swap_long_per_lot * trade.volume * days_held)
                            : (config_.swap_short_per_lot * trade.volume * days_held);
  ```
- **Gaps:**
  - No rollover time check (applied continuously, not at specific time)
  - No triple swap Wednesday
  - Simplified linear calculation
- **Impact:** Different swap charges = different profit

**g) Margin Calculation**
- **MT5 Behavior:** Complex margin based on SYMBOL_CALC_MODE
  - FOREX: `Lots × Contract_Size / Leverage × Margin_Rate`
  - CFD_LEVERAGE: `(Lots × ContractSize × MarketPrice) / Leverage × Margin_Rate`
  - Hedging may reduce margin
- **Our Engine:** NO margin calculation (not in backtest engine)
- **Gap:** Cannot detect margin calls, stop-outs
- **Impact:** Strategies may survive in our backtester but blow up in MT5

**h) Bar-by-Bar Mode SL/TP**
- **MT5 Behavior:** In "Open prices only" mode, may execute trades at specific bar prices
- **Our Engine:** [backtest_engine.cpp:19-40](src/backtest_engine.cpp#L19-L40)
  - Checks if `bar.low <= position.stop_loss` (for buy)
  - Closes at `bar.close` price (not at SL price)
- **Gap:** We close at bar close, not at actual SL price
- **Impact:** Incorrect profit calculation in BAR_BY_BAR mode

#### 3. **NO EA TRANSLATION SYSTEM**
- Cannot automatically convert MQL5 → C++
- Must manually port strategies
- Error-prone and time-consuming
- Prevents rapid iteration

#### 4. **NO REVERSE-ENGINEERING METHODOLOGY**
- No systematic approach to discover MT5's internal models
- No test cases designed to expose MT5 behavior
- No documentation of MT5's execution edge cases

---

## MT5 Reproduction Requirements

### Phase 1: Reverse-Engineering MT5 Behavior

**Goal:** Systematically discover MT5's exact execution model through controlled experiments.

#### Test Harness Design

**1. Micro-Test Strategy Pattern**
Create minimal EAs that isolate single behaviors:

**Test A: SL/TP Execution Order**
```mql5
// Test if SL and TP both triggered on same tick - which executes?
input double entry_offset = 10;  // pips from current
input double sl_offset = 5;      // pips
input double tp_offset = 5;      // pips

void OnTick() {
    if (PositionsTotal() == 0) {
        // Open position
        double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
        double entry = ask + entry_offset * Point;
        double sl = entry - sl_offset * Point;
        double tp = entry + tp_offset * Point;

        // Use pending order to control entry
        // Then generate price movement that hits both SL and TP on same tick
    }
}
```

**Expected Output:**
- Log: Which executed first (SL or TP)?
- Our backtester must match MT5's choice

**Test B: Tick Synthesis Validation**
```mql5
// Log every tick during bar formation
// Compare real ticks vs our synthetic generation

void OnTick() {
    MqlTick tick;
    if (SymbolInfoTick(_Symbol, tick)) {
        // Write: time_msc, bid, ask, volume, flags
        // to CSV for comparison with our TickGenerator
    }
}
```

**Expected Output:**
- CSV: Real tick sequence from MT5
- Compare with our `TickGenerator::GenerateTicksFromBar()` output
- Measure: Tick count per bar, price path deviation, timing distribution

**Test C: Slippage Characterization**
```mql5
// Execute many identical market orders
// Measure slippage distribution

input int num_trades = 1000;

void OnTick() {
    // Execute trades, log actual vs expected price
    // Calculate slippage histogram
}
```

**Expected Output:**
- Slippage distribution (mean, std dev, min, max)
- Check if normal, uniform, or other distribution
- Our `SimulateSlippage()` must match

**Test D: Spread Behavior During Volatility**
```mql5
// Log spread at every tick for 1 week
// Correlate with volatility (ATR), news times, rollover

void OnTick() {
    int spread = (int)SymbolInfoInteger(_Symbol, SYMBOL_SPREAD);
    double atr = iATR(_Symbol, PERIOD_M1, 14, 0);
    // Write: timestamp, spread, atr, volume
}
```

**Expected Output:**
- Spread widening model parameters
- Implement dynamic spread in our engine

**Test E: Swap Application Timing**
```mql5
// Open position before rollover
// Log balance/equity every minute around rollover time
// Detect exact moment swap applied

void OnTick() {
    static double prev_balance = AccountInfoDouble(ACCOUNT_BALANCE);
    double curr_balance = AccountInfoDouble(ACCOUNT_BALANCE);

    if (curr_balance != prev_balance) {
        // Swap applied!
        Print("Swap applied at: ", TimeCurrent());
    }
}
```

**Expected Output:**
- Exact rollover time (00:00 broker time?)
- Wednesday triple swap confirmation
- Our swap calc must apply at same moment

**Test F: Margin Calculation Verification**
```mql5
// Open positions of varying sizes
// Log: margin_required, margin_free, margin_level
// Compare with our calculations

void OnInit() {
    ENUM_SYMBOL_CALC_MODE calc_mode = (ENUM_SYMBOL_CALC_MODE)SymbolInfoInteger(_Symbol, SYMBOL_TRADE_CALC_MODE);
    double contract_size = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);
    long leverage = AccountInfoInteger(ACCOUNT_LEVERAGE);

    // Open 0.01, 0.1, 1.0 lot positions
    // For each, log margin values
}
```

**Expected Output:**
- Exact margin formulas for each SYMBOL_CALC_MODE
- Implement margin tracking in our engine

#### Test Execution Protocol

**Step 1: MT5 Strategy Tester Run**
```
1. Compile micro-test EA
2. Run in MT5 Strategy Tester (tick-by-tick mode)
3. Export logs/results to CSV
4. Save configuration (symbol, timeframe, date range, balance, leverage)
```

**Step 2: Our Backtester Run**
```
1. Load SAME price data (export from MT5)
2. Use SAME configuration
3. Run test with current implementation
4. Export results to CSV
```

**Step 3: Comparison**
```python
# validation_suite.py
import pandas as pd

def compare_trade_results(mt5_csv, our_csv):
    mt5 = pd.read_csv(mt5_csv)
    ours = pd.read_csv(our_csv)

    # Compare trade count
    assert len(mt5) == len(ours), "Trade count mismatch"

    # Compare each trade
    for i in range(len(mt5)):
        # Entry time (allow ±1 tick tolerance)
        # Entry price (allow ±1 pip tolerance)
        # Exit reason (must match exactly)
        # Exit price (allow ±1 pip tolerance)
        # Profit (allow ±0.01% tolerance)
        pass

    return comparison_report
```

**Step 4: Iteration**
```
IF results match → Test passes, move to next
IF results differ →
    1. Analyze difference
    2. Update our engine
    3. Re-run comparison
    4. Repeat until match
```

---

### Phase 2: Validation Framework Implementation

#### Component 1: MT5 Export Module (MQL5)

**Purpose:** Export MT5 backtest results in machine-readable format

**File:** `validation/mt5_exporter.mq5`

```mql5
// Called after backtest completes
void OnDeinit(const int reason) {
    ExportTrades("validation/mt5_trades.csv");
    ExportEquityCurve("validation/mt5_equity.csv");
    ExportMetrics("validation/mt5_metrics.json");
}

void ExportTrades(string filename) {
    HistorySelect(0, TimeCurrent());
    int file = FileOpen(filename, FILE_WRITE|FILE_CSV);

    FileWrite(file, "ticket,entry_time,entry_time_msc,exit_time,exit_time_msc,",
              "type,entry_price,exit_price,volume,profit,commission,swap,",
              "slippage,exit_reason");

    for (int i = 0; i < HistoryDealsTotal(); i++) {
        ulong ticket = HistoryDealGetTicket(i);
        // Write all trade data
    }

    FileClose(file);
}
```

#### Component 2: Our Backtester Export Module (C++)

**Purpose:** Export our results in SAME format as MT5

**Update:** [include/backtest_engine.h](include/backtest_engine.h)

Add to `Reporter` class:
```cpp
static void SaveTradesForValidation(const std::string& filename,
                                    const std::vector<Trade>& trades) {
    // EXACT same CSV format as MT5 exporter
    // Must be bit-identical column structure
}
```

#### Component 3: Validation Suite (Python)

**File:** `validation/validate_reproduction.py`

```python
import json
import pandas as pd
import numpy as np
from dataclasses import dataclass
from typing import List, Tuple

@dataclass
class ValidationConfig:
    """Tolerance settings for comparison"""
    time_tolerance_ms: int = 1000  # ±1 second
    price_tolerance_pips: float = 0.1  # ±0.1 pip
    profit_tolerance_pct: float = 0.01  # ±0.01%
    require_exact_trade_count: bool = True
    require_exact_exit_reason: bool = True

class MT5ReproductionValidator:
    def __init__(self, config: ValidationConfig):
        self.config = config
        self.errors = []
        self.warnings = []

    def validate_trades(self, mt5_csv: str, our_csv: str) -> bool:
        """Compare trade-by-trade execution"""
        mt5_trades = pd.read_csv(mt5_csv)
        our_trades = pd.read_csv(our_csv)

        # Check 1: Trade count
        if self.config.require_exact_trade_count:
            if len(mt5_trades) != len(our_trades):
                self.errors.append(f"Trade count mismatch: MT5={len(mt5_trades)}, Ours={len(our_trades)}")
                return False

        # Check 2: Trade sequence
        for i in range(min(len(mt5_trades), len(our_trades))):
            self._compare_trade(i, mt5_trades.iloc[i], our_trades.iloc[i])

        return len(self.errors) == 0

    def _compare_trade(self, index: int, mt5_trade, our_trade):
        """Compare single trade with tolerances"""

        # Entry time
        time_diff = abs(mt5_trade['entry_time_msc'] - our_trade['entry_time_msc'])
        if time_diff > self.config.time_tolerance_ms:
            self.errors.append(f"Trade {index}: Entry time diff {time_diff}ms")

        # Entry price
        price_diff = abs(mt5_trade['entry_price'] - our_trade['entry_price'])
        if price_diff > self.config.price_tolerance_pips * 0.0001:
            self.errors.append(f"Trade {index}: Entry price diff {price_diff}")

        # Exit reason (must be exact)
        if self.config.require_exact_exit_reason:
            if mt5_trade['exit_reason'] != our_trade['exit_reason']:
                self.errors.append(f"Trade {index}: Exit reason mismatch")

        # Profit (with tolerance)
        profit_diff_pct = abs((our_trade['profit'] - mt5_trade['profit']) / mt5_trade['profit'] * 100)
        if profit_diff_pct > self.config.profit_tolerance_pct:
            self.errors.append(f"Trade {index}: Profit diff {profit_diff_pct:.3f}%")

    def validate_metrics(self, mt5_json: str, our_json: str) -> bool:
        """Compare final metrics"""
        with open(mt5_json) as f:
            mt5_metrics = json.load(f)
        with open(our_json) as f:
            our_metrics = json.load(f)

        # Critical metrics that must match closely
        for metric in ['total_trades', 'win_rate', 'profit_factor',
                       'max_drawdown_percent', 'final_balance']:
            self._compare_metric(metric, mt5_metrics, our_metrics)

        return len(self.errors) == 0

    def generate_report(self) -> str:
        """Generate validation report"""
        report = "MT5 REPRODUCTION VALIDATION REPORT\n"
        report += "=" * 70 + "\n\n"

        if len(self.errors) == 0:
            report += "✓ VALIDATION PASSED - Exact MT5 reproduction achieved!\n"
        else:
            report += f"✗ VALIDATION FAILED - {len(self.errors)} errors found\n\n"
            report += "ERRORS:\n"
            for error in self.errors:
                report += f"  - {error}\n"

        if len(self.warnings) > 0:
            report += f"\nWARNINGS ({len(self.warnings)}):\n"
            for warning in self.warnings:
                report += f"  - {warning}\n"

        return report

# Usage
if __name__ == "__main__":
    config = ValidationConfig(
        time_tolerance_ms=1000,
        price_tolerance_pips=0.1,
        profit_tolerance_pct=0.01
    )

    validator = MT5ReproductionValidator(config)

    # Run validation
    trades_ok = validator.validate_trades(
        "validation/mt5_trades.csv",
        "validation/our_trades.csv"
    )

    metrics_ok = validator.validate_metrics(
        "validation/mt5_metrics.json",
        "validation/our_metrics.json"
    )

    print(validator.generate_report())

    # Exit code for CI/CD
    exit(0 if trades_ok and metrics_ok else 1)
```

#### Component 4: Automated Test Suite

**File:** `validation/test_suite.py`

```python
from pathlib import Path
import subprocess
import json

class MT5ValidationTestSuite:
    """Runs all validation tests and reports results"""

    def __init__(self, test_dir: Path):
        self.test_dir = test_dir
        self.results = {}

    def run_all_tests(self):
        """Execute all test cases"""

        test_cases = [
            "test_a_sl_tp_order",
            "test_b_tick_synthesis",
            "test_c_slippage",
            "test_d_spread_widening",
            "test_e_swap_timing",
            "test_f_margin_calc"
        ]

        for test_name in test_cases:
            print(f"\n{'='*70}")
            print(f"Running: {test_name}")
            print('='*70)

            # Step 1: User runs MT5 test manually (for now)
            print(f"Please run {test_name}.mq5 in MT5 Strategy Tester")
            print("Press Enter when complete...")
            input()

            # Step 2: Run our backtester with same config
            self._run_our_backtest(test_name)

            # Step 3: Compare results
            passed = self._validate_test(test_name)
            self.results[test_name] = passed

    def _run_our_backtest(self, test_name: str):
        """Run our C++ engine with test configuration"""
        config_file = self.test_dir / f"{test_name}_config.json"
        with open(config_file) as f:
            config = json.load(f)

        # Build command
        cmd = [
            "./build/backtest_engine",
            "--config", str(config_file),
            "--data", config["data_file"],
            "--output", f"validation/{test_name}_our_result.json"
        ]

        subprocess.run(cmd, check=True)

    def _validate_test(self, test_name: str) -> bool:
        """Run validator on test results"""
        validator = MT5ReproductionValidator(ValidationConfig())

        mt5_trades = f"validation/{test_name}_mt5_trades.csv"
        our_trades = f"validation/{test_name}_our_trades.csv"

        return validator.validate_trades(mt5_trades, our_trades)

    def generate_summary(self) -> str:
        """Generate overall test summary"""
        passed = sum(1 for v in self.results.values() if v)
        total = len(self.results)

        summary = f"\n{'='*70}\n"
        summary += "VALIDATION TEST SUITE SUMMARY\n"
        summary += f"{'='*70}\n"
        summary += f"Passed: {passed}/{total}\n\n"

        for test_name, result in self.results.items():
            status = "✓ PASS" if result else "✗ FAIL"
            summary += f"{status}  {test_name}\n"

        return summary
```

---

### Phase 3: Engine Corrections

**After Phase 1 & 2 identify gaps, implement fixes:**

#### Fix 1: Accurate Tick Generation
```cpp
// Replace TickGenerator::GenerateTicksFromBar()
// Based on Test B results

class ImprovedTickGenerator {
public:
    static std::vector<Tick> GenerateTicksFromBar(
        const Bar& bar,
        const TickGenerationConfig& config  // Learned from MT5
    ) {
        // Use MT5-validated algorithm:
        // - Correct path selection
        // - Non-linear timing distribution
        // - Realistic volume distribution
        // - Dynamic spread based on bar volatility
    }
};
```

#### Fix 2: Exact SL/TP Execution Order
```cpp
// Update RunTickByTick() based on Test A results

void ProcessTickExecution(const Tick& tick, Position& position) {
    // MT5's exact order (discovered from Test A):
    if (position.is_open) {
        // Check in MT5's order (e.g., SL before TP? TP before SL?)
        // Also: what if both trigger? Which wins?

        // Implement MT5-identical logic here
    }
}
```

#### Fix 3: Realistic Slippage Model
```cpp
// Replace SimulateSlippage() based on Test C results

class SlippageModel {
private:
    // Learned from MT5 data
    std::normal_distribution<double> distribution_;
    double mean_slippage_;
    double std_dev_;

public:
    int GetSlippage(bool is_buy, double spread, double volatility) {
        // Use MT5-validated distribution
        // Factor in spread and volatility
    }
};
```

#### Fix 4: Dynamic Spread
```cpp
// Add spread widening based on Test D results

class SpreadModel {
public:
    double GetCurrentSpread(const Bar& bar, double atr) {
        double base_spread = config_.spread_points;

        // Widen during high volatility (learned from MT5)
        if (atr > threshold) {
            base_spread *= volatility_multiplier;
        }

        // Widen during news (if time-based data available)
        // ...

        return base_spread;
    }
};
```

#### Fix 5: Exact Swap Application
```cpp
// Add rollover time tracking based on Test E

class SwapManager {
private:
    uint64_t last_rollover_time_;
    bool is_wednesday_;

public:
    void OnTick(uint64_t current_time, Position& position) {
        if (IsRolloverTime(current_time)) {
            double swap = CalculateSwap(position, is_wednesday_);
            ApplySwap(position, swap);

            last_rollover_time_ = current_time;
        }
    }
};
```

#### Fix 6: Margin Tracking
```cpp
// Add margin system based on Test F

class MarginManager {
public:
    double CalculateRequiredMargin(const Position& position,
                                   const SymbolInfo& symbol) {
        switch (symbol.calc_mode) {
            case CALC_MODE_FOREX:
                return position.volume * symbol.contract_size /
                       account.leverage * symbol.margin_rate;

            case CALC_MODE_CFD_LEVERAGE:
                return (position.volume * symbol.contract_size *
                       position.entry_price) / account.leverage *
                       symbol.margin_rate;

            // Other modes...
        }
    }

    bool CheckStopOut(double equity, double margin_used) {
        double margin_level = (equity / margin_used) * 100.0;
        return margin_level <= account.stop_out_level;
    }
};
```

---

### Phase 4: Continuous Validation

**Process:**
1. Every engine change → Re-run full validation suite
2. All tests must pass before merging
3. Maintain validation test library
4. Add new tests when edge cases discovered

**Automation:**
```bash
# CI/CD script
#!/bin/bash

echo "Running MT5 Reproduction Validation Suite..."

# Build engine
cmake --build build

# Run validation tests
python validation/test_suite.py

# Check exit code
if [ $? -eq 0 ]; then
    echo "✓ All validation tests passed"
    exit 0
else
    echo "✗ Validation failed - MT5 reproduction broken"
    exit 1
fi
```

---

## Success Criteria

**MUST achieve ALL of the following:**

1. ✅ **Trade Count Match:** Same number of trades as MT5 (±0)
2. ✅ **Trade Timing Match:** Entry/exit times within ±1 second
3. ✅ **Price Match:** Entry/exit prices within ±0.1 pip
4. ✅ **Exit Reason Match:** SL/TP/signal matches exactly
5. ✅ **Profit Match:** Individual trade profit within ±0.01%
6. ✅ **Final Balance Match:** Within ±0.1%
7. ✅ **Metrics Match:** Win rate, profit factor, drawdown within ±1%

**Test Strategy:** `fill_up.mq5` (your grid EA)
**Test Period:** 1 year of EURUSD H1 data
**Test Configurations:** 10 different parameter combinations

**If ANY test fails → Engine is NOT validated → No further work proceeds**

---

## Timeline Estimate

**Phase 1: Reverse-Engineering (2-3 weeks)**
- Implement 6 micro-test EAs: 3 days
- Run MT5 tests, collect data: 3 days
- Analyze results, document findings: 4 days
- Design corrections: 2 days

**Phase 2: Validation Framework (1 week)**
- MT5 exporter MQL5 code: 2 days
- Python validation suite: 2 days
- Test automation: 2 days
- Documentation: 1 day

**Phase 3: Engine Corrections (2-3 weeks)**
- Implement fixes based on findings: 7-10 days
- Test each fix: 3 days
- Integration testing: 2 days

**Phase 4: Validation (1 week)**
- Full test suite runs: 2 days
- Fix any remaining discrepancies: 3 days
- Final verification: 2 days

**Total: 6-8 weeks to achieve MT5 exact reproduction**

---

## Next Steps

**IMMEDIATE ACTION:**
1. Review this framework with you - confirm approach
2. Create `validation/` directory structure
3. Implement Test A (SL/TP execution order) as proof-of-concept
4. Run comparison, verify methodology works
5. If methodology works → Full Phase 1 execution

**BLOCKING QUESTION:**
Do you have access to MT5 Strategy Tester? We need it to run reference tests.

**Alternative if NO MT5:**
- Use cTrader instead (similar validation approach)
- Or find MT5 test results from trusted source
- Or run on your broker's MT5 account

---

## Open Questions for You

1. **Do you have MT5 Strategy Tester access?** (Required for validation)
2. **Which broker/symbol should we use as reference?** (e.g., GridMarkets EURUSD)
3. **What date range for test data?** (Recommend: 1 year recent data)
4. **Priority: Speed vs Accuracy?** (Slower validation = higher confidence)
5. **Tolerance levels acceptable?** (Current: ±0.1 pip, ±0.01% profit)

---

## Risk Assessment

**HIGH RISK:**
- MT5's internal models may be undocumented/proprietary
- Some behaviors may be broker-specific (not universal)
- Perfect reproduction may be impossible (floating-point rounding)

**MITIGATION:**
- Target "close enough" (within tolerances)
- Document known deviations
- Focus on statistical equivalence over bit-exact match
- Test multiple brokers to find common behavior

**FALLBACK:**
If exact reproduction proves impossible:
- Achieve "statistically equivalent" results
- Validate distribution of outcomes (not individual trades)
- Use ensemble testing (multiple runs, compare distributions)

---

## Conclusion

**Bottom Line:** We cannot proceed with AI optimization, adversarial testing, or strategy development until we prove our backtester reproduces MT5 results.

This framework provides:
- ✅ Systematic methodology to discover MT5 behavior
- ✅ Automated validation suite
- ✅ Clear success criteria
- ✅ Iterative correction process

**Your approval needed to proceed.**
