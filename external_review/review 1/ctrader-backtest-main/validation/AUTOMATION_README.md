# MT5 Validation Test Automation

**Goal:** Automate as much of the validation workflow as possible to make testing fast and reproducible.

---

## Quick Start - Fully Automated

```bash
cd ctrader-backtest
python validation\run_test_a_full.py
```

**This runs the complete Test A workflow:**
1. Connects to MT5
2. Exports EURUSD H1 data (last 30 days)
3. Guides you through running MT5 Strategy Tester
4. Retrieves MT5 results automatically
5. Builds our C++ backtest
6. Runs our backtest with same data
7. Compares and reports PASS/FAIL

**Total time:** ~15 minutes

---

## What Gets Automated

### ✅ Fully Automated (No User Action)

1. **MT5 Connection**
   - `run_mt5_test.py` handles initialization
   - Verifies account 000000 is logged in
   - Checks broker settings

2. **Data Export**
   - Automatically exports EURUSD H1 bars
   - Saves to `validation/configs/test_a_data.csv`
   - Uses MT5 Python API (`copy_rates_range`)

3. **Result Retrieval**
   - Finds MT5 result file in `MQL5\Files\` directory
   - Copies to `validation/mt5/test_a_mt5_result.csv`
   - No manual file hunting needed

4. **Build System**
   - CMake configuration
   - C++ compilation
   - Executable location detection

5. **Our Backtest Execution**
   - Runs with exported data
   - Generates result file
   - Saves to `validation/ours/test_a_our_result.csv`

6. **Comparison**
   - Loads both result files
   - Compares exit reasons, prices, profits
   - Displays detailed diff report
   - Returns PASS/FAIL

### ⏸️ Semi-Automated (User Action Required)

**MT5 Strategy Tester Execution:**
- Python CANNOT fully control MT5 Strategy Tester GUI
- Script pauses and prompts you to:
  1. Open Strategy Tester (Ctrl+R)
  2. Select EA: `test_a_sl_tp_order`
  3. Configure: EURUSD, H1, Every tick
  4. Click Start
  5. Wait for completion
  6. Press Enter in Python script
- **Time required:** 30-60 seconds of interaction + 1-2 minutes test runtime

### ❌ Cannot Be Automated

**EA Compilation:**
- Must compile `test_a_sl_tp_order.mq5` in MetaEditor once
- Reason: MetaEditor doesn't have command-line interface
- **Workaround:** Pre-compile before running automation
- **Time required:** 5 seconds (one-time setup)

---

## Scripts Overview

### `run_test_a_full.py` - Master Orchestrator

**Purpose:** Runs entire Test A workflow

**Usage:**
```bash
python validation\run_test_a_full.py
```

**What it does:**
```
Step 1: MT5 Test (calls run_mt5_test.py)
  → Exports data
  → Guides manual Strategy Tester run
  → Retrieves results

Step 2: Build C++ Backtest
  → CMake configure
  → CMake build (or manual g++)
  → Verify executable

Step 3: Run Our Backtest
  → Execute with exported data
  → Generate result file

Step 4: Compare Results
  → Call compare_test_a.py
  → Display PASS/FAIL
```

**Exit codes:**
- `0` = Test PASSED
- `1` = Test FAILED or error occurred

### `run_mt5_test.py` - MT5 Automation

**Purpose:** Handle all MT5-related automation

**Usage:**
```bash
python validation\run_mt5_test.py
```

**Features:**
- Connects to MT5 via Python API
- Exports historical data
- Checks for EA compilation
- Guides manual Strategy Tester execution
- Retrieves and copies result files

**Class:** `MT5TesterAutomation`

**Methods:**
```python
automation = MT5TesterAutomation()
automation.initialize_mt5()  # Connect to MT5
automation.export_historical_data(symbol="EURUSD", timeframe=mt5.TIMEFRAME_H1)
automation.copy_result_file()  # Retrieve results
automation.shutdown_mt5()
```

### `compare_test_a.py` - Result Validator

**Purpose:** Compare MT5 vs our engine results

**Usage:**
```bash
python validation\compare_test_a.py
```

**Checks:**
- Exit reason match (SL vs TP)
- Price differences (within 0.1 pip)
- Profit differences (within 0.01%)

**Output:**
- Detailed comparison table
- Visual diff
- PASS/FAIL determination
- Action items if failed

---

## Requirements

### Python Packages

```bash
pip install MetaTrader5 pandas
```

**MT5 Package Notes:**
- Only works on Windows
- Requires MT5 terminal installed
- MT5 must be running and logged in

### C++ Build Tools

**Option A: CMake + Compiler**
```bash
# Check if installed
cmake --version
g++ --version
```

**Option B: Visual Studio**
- MSVC compiler works
- CMake will auto-detect

**Option C: MinGW**
```bash
# Windows with MinGW
g++ -std=c++17 -O3 validation/test_a_our_backtest.cpp src/backtest_engine.cpp -I include -o validation/test_a_backtest
```

---

## Workflow Diagram

```
┌─────────────────────────────────────────────────────┐
│         run_test_a_full.py (Master)                 │
└─────────────────────────────────────────────────────┘
                        │
        ┌───────────────┼───────────────┐
        │               │               │
        ▼               ▼               ▼
┌──────────────┐ ┌──────────────┐ ┌──────────────┐
│ run_mt5_test │ │ CMake Build  │ │ compare_test │
│     .py      │ │   System     │ │    _a.py     │
└──────────────┘ └──────────────┘ └──────────────┘
        │               │               │
        ▼               ▼               ▼
┌──────────────┐ ┌──────────────┐ ┌──────────────┐
│  MT5 Data    │ │ test_a_back  │ │ Pass/Fail    │
│  Export      │ │   test.exe   │ │  Report      │
└──────────────┘ └──────────────┘ └──────────────┘
        │               │
        ▼               ▼
┌──────────────┐ ┌──────────────┐
│ MT5 Manual   │ │ Run Our      │
│ Test (User)  │ │ Backtest     │
└──────────────┘ └──────────────┘
        │               │
        └───────┬───────┘
                ▼
        ┌──────────────┐
        │  Result CSV  │
        │    Files     │
        └──────────────┘
```

---

## Troubleshooting

### Issue: MT5 not initialized

**Error:** `Failed to initialize MT5`

**Solution:**
1. Check MT5 is running
2. Verify logged into account 000000
3. Try specifying MT5 path:
   ```python
   automation.initialize_mt5(path="C:\\Program Files\\MetaTrader 5\\terminal64.exe")
   ```

### Issue: Data export fails

**Error:** `Failed to get rates for EURUSD`

**Solution:**
1. Check symbol name is correct (`EURUSD` vs `EURUSDm`)
2. Verify historical data exists in MT5
3. Try different date range
4. Manually export as fallback

### Issue: Result file not found

**Error:** `Result file not found: test_a_mt5_result.csv`

**Cause:** MT5 test didn't run or EA didn't write file

**Solution:**
1. Check MT5 Journal tab for errors
2. Verify EA compiled successfully
3. Ensure test ran to completion
4. Check `MQL5\Files\` directory manually

### Issue: Build fails

**Error:** `CMake configuration failed`

**Solution:**
Try manual compilation:
```bash
g++ -std=c++17 -O3 ^
    validation/test_a_our_backtest.cpp ^
    src/backtest_engine.cpp ^
    -I include ^
    -o validation/test_a_backtest.exe
```

### Issue: Comparison script fails

**Error:** File not found errors

**Solution:**
1. Verify both files exist:
   - `validation/mt5/test_a_mt5_result.csv`
   - `validation/ours/test_a_our_result.csv`
2. Check file permissions
3. Try running comparison manually

---

## Advanced: Customization

### Test Different Symbols

Edit `run_mt5_test.py`:
```python
automation.export_historical_data(
    symbol="GBPUSD",  # Change symbol
    timeframe=mt5.TIMEFRAME_H1,
    start_date=datetime(2024, 1, 1),
    end_date=datetime(2024, 12, 1)
)
```

### Test Different Date Ranges

```python
from datetime import datetime, timedelta

# Last 60 days instead of 30
end_date = datetime.now()
start_date = end_date - timedelta(days=60)

automation.export_historical_data(
    symbol="EURUSD",
    timeframe=mt5.TIMEFRAME_H1,
    start_date=start_date,
    end_date=end_date
)
```

### Test Different Timeframes

```python
automation.export_historical_data(
    symbol="EURUSD",
    timeframe=mt5.TIMEFRAME_M15,  # M15 instead of H1
    output_file="validation/configs/test_a_data_m15.csv"
)
```

---

## Future Enhancements

**Potential improvements:**

1. **Full MT5 GUI Automation**
   - Use Windows automation tools (pywinauto)
   - Completely automate Strategy Tester clicks
   - No manual intervention needed

2. **Parallel Testing**
   - Run multiple symbols simultaneously
   - Test multiple timeframes in parallel
   - Faster validation cycles

3. **Continuous Integration**
   - GitHub Actions integration
   - Automated testing on every commit
   - Regression test suite

4. **Result Database**
   - Store all validation results
   - Track performance over time
   - Regression detection

5. **Visual Reports**
   - HTML reports with charts
   - Equity curve comparisons
   - Trade-by-trade visualization

---

## Summary

**Current Automation Level:** ~85%

**Manual Steps Required:**
1. One-time EA compilation (5 seconds)
2. MT5 Strategy Tester run (30 seconds interaction)

**Everything Else:** Fully automated!

**Time Savings:**
- Manual process: 30-40 minutes
- Automated process: 15 minutes
- **Savings: 50-60%**

**Reproducibility:**
- Manual: Error-prone (copy/paste mistakes)
- Automated: 100% reproducible

**Next Tests (B-F):**
- Same automation framework applies
- Just duplicate and modify for each test
- Complete test suite can run in ~90 minutes

---

## Ready to Run?

```bash
# Install dependencies
pip install MetaTrader5 pandas

# Ensure MT5 is running and logged in
# Compile test_a_sl_tp_order.mq5 once in MetaEditor

# Run full automation
python validation\run_test_a_full.py

# Follow on-screen prompts
# Done!
```

**Questions?** Check troubleshooting section or see [TEST_A_INSTRUCTIONS.md](TEST_A_INSTRUCTIONS.md) for detailed manual steps.
