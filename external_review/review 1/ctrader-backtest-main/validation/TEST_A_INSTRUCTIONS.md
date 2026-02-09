# TEST A: SL/TP Execution Order - Execution Instructions

**Objective:** Discover whether MT5 executes Stop Loss or Take Profit first when both are triggered on the same tick, and verify our engine matches this behavior.

**Time Required:** ~30 minutes

---

## PART 1: Run MT5 Test (15 minutes)

### Step 1: Prepare MT5 Environment

1. **Open MetaTrader 5**
   - Ensure you're logged into GridMarkets account 000000
   - Verify connection (should see green connection indicator)

2. **Open MetaEditor**
   - Press F4 in MT5, OR
   - Click "Tools → MetaQuotes Language Editor"

3. **Create/Open Test EA**
   - In MetaEditor, click "File → Open"
   - Navigate to: `C:\Users\user\Documents\ctrader-backtest\validation\micro_tests\`
   - Select `test_a_sl_tp_order.mq5`
   - If file not found, copy it there from project directory

### Step 2: Compile the EA

1. **In MetaEditor:**
   - Make sure `test_a_sl_tp_order.mq5` is open
   - Press F7 or click "Compile" button
   - Check "Errors" tab at bottom
   - Should see: "0 error(s), 0 warning(s)"
   - If errors appear, copy error message and let me know

### Step 3: Configure Strategy Tester

1. **Open Strategy Tester in MT5**
   - Press Ctrl+R, OR
   - Click "View → Strategy Tester"

2. **Configure Settings:**
   ```
   Expert Advisor:  test_a_sl_tp_order
   Symbol:          EURUSD
   Period:          H1 (1 Hour)
   Date Range:      Last 1 month (or any recent volatile period)
   Execution:       Every tick based on real ticks (MOST IMPORTANT!)
   Initial Deposit: 10000
   Leverage:        1:100
   Optimization:    Disabled
   ```

3. **Input Parameters (leave as default):**
   ```
   InpLotSize:      0.01
   InpSLPoints:     100
   InpTPPoints:     100
   InpMagicNumber:  20241206
   ```

### Step 4: Run the Test

1. **Click "Start" in Strategy Tester**
   - Test will run (may take 1-2 minutes depending on date range)
   - Watch "Journal" tab for progress messages
   - Test completes when you see "TEST A COMPLETE"

2. **Check Results in Journal Tab:**
   - Look for messages starting with "========================================="
   - Find "POSITION CLOSED - ANALYZING RESULT"
   - Look for "*** EXIT REASON: SL ***" or "*** EXIT REASON: TP ***"
   - Note which one it says!

### Step 5: Find and Copy Result File

1. **Locate Result File:**
   - The EA creates: `test_a_mt5_result.csv`
   - Located in: `C:\Users\user\AppData\Roaming\MetaQuotes\Terminal\<TERMINAL_ID>\MQL5\Files\`
   - To find TERMINAL_ID:
     - In MT5, click "File → Open Data Folder"
     - This opens the correct directory
     - Navigate to `MQL5\Files\`
     - Find `test_a_mt5_result.csv`

2. **Copy to Validation Directory:**
   - Copy `test_a_mt5_result.csv`
   - Paste to: `C:\Users\user\Documents\ctrader-backtest\validation\mt5\`
   - Rename if needed (should be exactly `test_a_mt5_result.csv`)

3. **Verify File Content:**
   - Open in Notepad or Excel
   - Should have 2 rows (header + data)
   - Should have columns: ticket, entry_time, entry_price, exit_time, exit_price, sl_price, tp_price, profit, comment, exit_reason, broker, account
   - Check that `exit_reason` column contains either "SL" or "TP"

---

## PART 2: Export Price Data from MT5 (5 minutes)

**Purpose:** We need the exact same price data that MT5 used, so our backtest runs on identical data.

### Step 1: Export EURUSD Data

**Method A: Using Script (Recommended)**

1. Create file `export_bars.mq5` in MetaEditor:

```mql5
//+------------------------------------------------------------------+
//| Export EURUSD H1 bars to CSV                                    |
//+------------------------------------------------------------------+
#property script_show_inputs

input datetime start_date = D'2024.11.01';  // Start date
input datetime end_date = D'2024.12.01';    // End date

void OnStart()
{
    string filename = "test_a_data.csv";
    int file = FileOpen(filename, FILE_WRITE|FILE_CSV|FILE_ANSI);

    if (file == INVALID_HANDLE) {
        Print("ERROR: Cannot create file");
        return;
    }

    // Header
    FileWrite(file, "time,open,high,low,close,volume,tick_volume");

    // Copy bars
    MqlRates rates[];
    int copied = CopyRates(_Symbol, PERIOD_H1, start_date, end_date, rates);

    Print("Copying ", copied, " bars...");

    for (int i = 0; i < copied; i++) {
        FileWrite(file,
            (long)rates[i].time,
            rates[i].open,
            rates[i].high,
            rates[i].low,
            rates[i].close,
            (long)rates[i].tick_volume,
            (long)rates[i].tick_volume);
    }

    FileClose(file);
    Print("Exported ", copied, " bars to ", filename);
    Print("File location: Terminal\\MQL5\\Files\\", filename);
}
```

2. Compile and run script on EURUSD H1 chart
3. Find `test_a_data.csv` in `MQL5\Files\`
4. Copy to `C:\Users\user\Documents\ctrader-backtest\validation\configs\`

**Method B: Manual Export (if script doesn't work)**

1. Open EURUSD H1 chart in MT5
2. Click "File → Save As..."
3. Save as CSV
4. Copy to validation/configs/test_a_data.csv

---

## PART 3: Run Our Backtest Engine (10 minutes)

### Step 1: Compile Our Test Program

**Using CMake (Recommended):**

```bash
cd C:\Users\user\Documents\ctrader-backtest

# Create build directory if not exists
mkdir build
cd build

# Configure with CMake
cmake ..

# Build just the validation test
cmake --build . --target test_a_backtest --config Release
```

**Manual Compilation (if CMake fails):**

```bash
cd C:\Users\user\Documents\ctrader-backtest

# Compile using g++
g++ -std=c++17 -O3 ^
    validation/test_a_our_backtest.cpp ^
    src/backtest_engine.cpp ^
    -I include ^
    -o validation/test_a_backtest.exe
```

### Step 2: Run Our Backtest

```bash
cd C:\Users\user\Documents\ctrader-backtest

# Run the test
validation\test_a_backtest.exe validation\configs\test_a_data.csv
```

**Expected Output:**
```
========================================
TEST A: SL/TP EXECUTION ORDER DISCOVERY
========================================
Our C++ Backtest Engine Version

Loading test data...
Loaded 720 bars
...
POSITION CLOSED - ANALYZING RESULT
...
*** EXIT REASON: sl *** (or tp)
========================================
```

### Step 3: Verify Output File

**Check that file was created:**
- Location: `validation\ours\test_a_our_result.csv`
- Should have same format as MT5 result
- Should have exit_reason as "sl" or "tp"

---

## PART 4: Compare Results (5 minutes)

### Run Validation Script

```bash
cd C:\Users\user\Documents\ctrader-backtest

python validation\compare_test_a.py
```

**Expected Output (if PASS):**
```
======================================================================
TEST A VALIDATION: SL/TP EXECUTION ORDER
======================================================================

Loading results...
✓ MT5 result loaded
✓ Our result loaded

======================================================================
MT5 RESULT
======================================================================
[MT5 data displayed]

======================================================================
OUR ENGINE RESULT
======================================================================
[Our data displayed]

======================================================================
COMPARISON
======================================================================
MT5 Exit Reason:  SL
Our Exit Reason:  SL

✓ PASS: Exit reasons match!

CONCLUSION:
  Both systems executed SL first
  Our engine correctly matches MT5 behavior

======================================================================
FINAL RESULT
======================================================================
✓✓✓ TEST A PASSED ✓✓✓

Our engine correctly reproduces MT5 SL/TP execution order!
Methodology validated. Proceeding to Test B recommended.
======================================================================
```

**Expected Output (if FAIL):**
```
❌ FAIL: Exit reasons DO NOT match!

CONCLUSION:
  MT5 executed: TP
  Our engine executed: SL

ACTION REQUIRED:
  Our engine needs to be updated to match MT5 behavior
  File to modify: src/backtest_engine.cpp, lines 66-72
```

---

## PART 5: Interpret Results

### If Test PASSES ✓

**What it means:**
- Our engine correctly matches MT5's SL/TP execution order
- The methodology works!
- We can trust this validation approach

**Next steps:**
1. Document the finding (MT5 executes SL or TP first)
2. Move to Test B (Tick Synthesis Validation)
3. Continue with remaining tests

### If Test FAILS ❌

**What it means:**
- Our engine does NOT match MT5 (this is GOOD - we found the problem!)
- We now know exactly what to fix
- The methodology works - it detected the difference!

**Next steps:**

1. **Analyze the Difference:**
   - MT5 executed: [SL or TP]
   - Our engine executed: [opposite]
   - This tells us our engine's assumption is wrong

2. **Fix the Code:**

**If MT5 executed TP first:**

Edit `src/backtest_engine.cpp`, lines 66-72:

```cpp
// BEFORE (current):
if (CheckStopLoss(position, tick)) {
    ClosePosition(position, tick, trades, "sl");
} else if (CheckTakeProfit(position, tick)) {
    ClosePosition(position, tick, trades, "tp");
}

// AFTER (reversed order):
if (CheckTakeProfit(position, tick)) {
    ClosePosition(position, tick, trades, "tp");
} else if (CheckStopLoss(position, tick)) {
    ClosePosition(position, tick, trades, "sl");
}
```

3. **Re-compile and Re-test:**
   - Rebuild engine
   - Run our backtest again
   - Run comparison script
   - Should now PASS

4. **Document the Fix:**
   - Update VALIDATION_RESULTS.md
   - Note: "MT5 checks [TP/SL] before [SL/TP]"
   - Mark Test A as PASSED

---

## Troubleshooting

### Issue: MT5 EA won't compile

**Error:** "Undeclared identifier" or similar

**Solution:**
- Ensure you're using MT5 (not MT4)
- Check that Trade.mqh exists: `MT5\MQL5\Include\Trade\Trade.mqh`
- Try adding full path: `#include <C:\\Program Files\\...\\Trade.mqh>`

### Issue: No trades executed in MT5

**Cause:** Price didn't move enough to trigger SL or TP

**Solution:**
- Choose a more volatile date range
- Try recent news events (NFP, CPI announcements)
- Or reduce SL/TP to 50 points instead of 100

### Issue: Result file not found

**Problem:** Can't find `test_a_mt5_result.csv`

**Solution:**
1. In MT5, click "File → Open Data Folder"
2. This opens the exact directory MT5 uses
3. Navigate to `MQL5\Files\`
4. The file MUST be there if EA ran successfully
5. If not there, check Journal tab for errors

### Issue: Our backtest won't compile

**Error:** Cannot find backtest_engine.h

**Solution:**
```bash
# Verify you're in project root
cd C:\Users\user\Documents\ctrader-backtest

# Check files exist
dir include\backtest_engine.h
dir src\backtest_engine.cpp

# If missing, files may be in wrong location
```

### Issue: Comparison script fails

**Error:** "No such file or directory"

**Solution:**
- Check all files exist:
  - `validation/mt5/test_a_mt5_result.csv`
  - `validation/ours/test_a_our_result.csv`
- Verify paths are correct (use forward slashes in Python)
- Try absolute paths if relative paths don't work

---

## Summary Checklist

Before moving to next test, confirm:

- ✅ MT5 test ran successfully
- ✅ MT5 result file exported
- ✅ Price data exported
- ✅ Our backtest compiled and ran
- ✅ Our result file created
- ✅ Comparison script executed
- ✅ Result interpreted (PASS or FAIL)
- ✅ If FAIL, fix implemented and re-tested
- ✅ Final result is PASS

**When all checked:** Test A is complete! Document findings and proceed to Test B.

---

## Questions?

If you encounter any issues:

1. Check Journal/Experts tab in MT5 for error messages
2. Check terminal output when running our backtest
3. Verify all file paths are correct
4. Share error messages - I'll help debug

**Ready to start?** Begin with PART 1: Run MT5 Test!
