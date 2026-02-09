# MT5 Strategy Tester - Test Instructions

## General Settings (All Tests)

**How to run each test:**
1. Open MT5
2. Open Strategy Tester (Ctrl+R or View → Strategy Tester)
3. Configure settings as shown below for each test
4. Click "Start"
5. Wait for completion
6. Run: `python validation/retrieve_results.py test_X`

---

## Test B: Tick Synthesis Pattern

**Purpose:** Discover how MT5 generates synthetic ticks from OHLC bars

**Strategy Tester Settings:**
```
Expert:         test_b_tick_synthesis
Symbol:         EURUSD
Period:         H1
Date range:     2025.12.01 - 2025.12.10
Deposit:        10000
Leverage:       1:500
Execution:      Every tick based on real ticks
Optimization:   Disabled
```

**Expected Duration:** ~2-3 minutes

**Output Files:**
- `test_b_ticks.csv` - Every tick recorded (100 bars worth)
- `test_b_summary.json` - Statistics

**What it captures:**
- Tick count per bar
- Bid/Ask prices for each tick
- Bar OHLC for pattern analysis
- Tick timing within each bar

---

## Test C: Slippage Distribution

**Purpose:** Measure MT5's slippage model on market orders

**Strategy Tester Settings:**
```
Expert:         test_c_slippage
Symbol:         EURUSD
Period:         H1
Date range:     2025.12.01 - 2025.12.05
Deposit:        10000
Leverage:       1:500
Execution:      Every tick based on real ticks
Optimization:   Disabled
```

**Expected Duration:** ~1 minute

**Output Files:**
- `test_c_slippage.csv` - 50 trades with slippage measurements
- `test_c_summary.json` - Statistics

**What it captures:**
- Requested price vs executed price
- Slippage in points/pips
- Buy vs Sell slippage differences
- Distribution statistics

---

## Test D: Spread Widening

**Purpose:** Correlate spread changes with volatility (ATR)

**Strategy Tester Settings:**
```
Expert:         test_d_spread_widening
Symbol:         EURUSD
Period:         H1
Date range:     2025.12.01 - 2025.12.05
Deposit:        10000
Leverage:       1:500
Execution:      Every tick based on real ticks
Optimization:   Disabled
```

**Expected Duration:** ~5 minutes (simulates 1 hour of monitoring)

**Output Files:**
- `test_d_spread.csv` - Spread samples every 5 seconds
- `test_d_summary.json` - Statistics

**What it captures:**
- Spread in points/pips
- ATR (volatility indicator)
- Tick volume
- Price changes

---

## Test E: Swap Timing

**Purpose:** Detect exact moment when swap is applied

**Strategy Tester Settings:**
```
Expert:         test_e_swap_timing
Symbol:         EURUSD
Period:         H1
Date range:     2025.12.01 - 2025.12.10 (need multiple days)
Deposit:        10000
Leverage:       1:500
Execution:      Every tick based on real ticks
Optimization:   Disabled
```

**Expected Duration:** ~10 minutes (simulates 48 hours)

**Output Files:**
- `test_e_swap_timing.csv` - Swap events log
- `test_e_summary.json` - Statistics

**What it captures:**
- Exact timestamp of swap application
- Day of week (Wednesday triple swap?)
- Swap amount
- Balance changes

---

## Test F: Margin Calculation ✅ COMPLETE

**Purpose:** Verify margin calculation formulas

**Strategy Tester Settings:**
```
Expert:         test_f_margin_calc
Symbol:         EURUSD
Period:         H1
Date range:     2025.12.01 - 2025.12.05
Deposit:        10000
Leverage:       1:500
Execution:      Every tick based on real ticks
Optimization:   Disabled
```

**Expected Duration:** ~1 minute

**Output Files:**
- `test_f_margin.csv` - Margin for 5 lot sizes
- `test_f_summary.json` - Statistics

**Status:** ✅ Complete - Results retrieved

---

## Common Issues

### No output files created

**Possible causes:**
1. Test didn't run long enough (check Journal tab)
2. File write permissions issue
3. EA encountered runtime error

**Check:**
- Strategy Tester → Journal tab for errors
- Look for "ERROR: Cannot create file" messages
- Verify test actually executed trades/operations

### Test completed but no trades

Some tests (B, D, E) don't execute trades - they just monitor and record data.
- Test B: Monitors ticks
- Test C: Executes 50 trades
- Test D: Monitors spread
- Test E: Opens 1 position and monitors swap
- Test F: Opens/closes 5 test positions

### Wrong date range

Make sure you use dates with available historical data.
Recommended: Recent dates like 2025.12.01 - 2025.12.10

---

## After Each Test

Run the retrieval script:
```bash
python validation/retrieve_results.py test_b
python validation/retrieve_results.py test_c
python validation/retrieve_results.py test_d
python validation/retrieve_results.py test_e
```

Files will be copied to: `validation/mt5/`
