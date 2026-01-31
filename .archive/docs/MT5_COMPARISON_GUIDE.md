# MT5 Comparison Testing Guide

**Phase 8: MT5 Validation**
**Date:** 2026-01-07
**Status:** Ready for Execution

---

## 🎯 Overview

This guide explains how to run the MT5 comparison test to validate our C++ backtest engine against MetaTrader 5's Strategy Tester.

---

## 📋 Prerequisites

### Files Created
- ✅ [SimplePriceLevelBreakout.mq5](C:\Users\user\AppData\Roaming\MetaQuotes\Terminal\930119AA53207C8778B41171FBFFB46F\MQL5\Experts\SimplePriceLevelBreakout.mq5) - MT5 Expert Advisor
- ✅ [simple_price_level_breakout.h](include/simple_price_level_breakout.h) - C++ strategy implementation
- ✅ [test_mt5_comparison.cpp](validation/test_mt5_comparison.cpp) - C++ test runner
- ✅ [MT5_COMPARISON_STRATEGY.md](MT5_COMPARISON_STRATEGY.md) - Strategy specification

### Software Required
- ✅ MetaTrader 5 installed
- ✅ MSYS2/MinGW C++ compiler
- ✅ Historical EURUSD data for January 2024

---

## 🚀 Step-by-Step Execution

### Step 1: Prepare MT5 Historical Data

1. **Open MetaTrader 5**

2. **Download EURUSD Historical Data**
   - Press `F2` or go to `Tools` → `History Center`
   - Select `EURUSD` in the symbol list
   - Select `H1` (1 Hour) timeframe
   - Ensure data for **January 2024** is downloaded
   - Click `Download` if needed

3. **Export EURUSD Data to CSV**

   **Method A: Using Script (Recommended)**
   ```mql5
   // Save as: Scripts/ExportEURUSD_H1.mq5
   void OnStart()
   {
       datetime start = D'2024.01.01 00:00';
       datetime end = D'2024.01.31 23:59';

       MqlRates rates[];
       int copied = CopyRates("EURUSD", PERIOD_H1, start, end, rates);

       if(copied > 0)
       {
           int handle = FileOpen("EURUSD_H1_202401.csv", FILE_WRITE|FILE_CSV|FILE_ANSI);
           if(handle != INVALID_HANDLE)
           {
               FileWrite(handle, "Timestamp,Open,High,Low,Close");
               for(int i = 0; i < copied; i++)
               {
                   string timestamp = TimeToString(rates[i].time, TIME_DATE|TIME_MINUTES);
                   FileWrite(handle, timestamp, rates[i].open, rates[i].high,
                            rates[i].low, rates[i].close);
               }
               FileClose(handle);
               Print("Exported ", copied, " bars to EURUSD_H1_202401.csv");
           }
       }
   }
   ```

   **Method B: Manual Export**
   - Open chart: EURUSD H1
   - Right-click chart → `Save as...`
   - Select date range: 2024.01.01 - 2024.01.31
   - Format: CSV
   - Save to: `validation/EURUSD_H1_202401.csv`

4. **Verify CSV Format**

   The CSV file should look like:
   ```csv
   Timestamp,Open,High,Low,Close
   2024.01.01 00:00,1.10500,1.10520,1.10480,1.10510
   2024.01.01 01:00,1.10510,1.10535,1.10500,1.10528
   ...
   ```

5. **Copy CSV to Project Directory**
   ```bash
   # Copy from MT5 Files folder to validation directory
   cp "C:/Users/user/AppData/Roaming/MetaQuotes/Terminal/<BROKER_ID>/MQL5/Files/EURUSD_H1_202401.csv" \
      "c:/Users/user/Documents/ctrader-backtest/validation/"
   ```

---

### Step 2: Run MT5 Strategy Tester

1. **Open MetaTrader 5**

2. **Open Strategy Tester**
   - Press `Ctrl+R` or go to `View` → `Strategy Tester`

3. **Configure Test Settings**

   | Setting | Value |
   |---------|-------|
   | **Expert Advisor** | SimplePriceLevelBreakout |
   | **Symbol** | EURUSD |
   | **Period** | H1 (1 Hour) |
   | **Date Range** | 2024.01.01 - 2024.01.31 |
   | **Execution** | Every tick (most accurate) |
   | **Deposit** | 10000 |
   | **Currency** | USD |
   | **Leverage** | 1:100 |
   | **Optimization** | Disabled |

4. **Configure EA Input Parameters**

   | Parameter | Value |
   |-----------|-------|
   | LongTriggerLevel | 1.2000 |
   | ShortTriggerLevel | 1.1900 |
   | LotSize | 0.10 |
   | StopLossPips | 50 |
   | TakeProfitPips | 100 |

5. **Disable Commissions and Swap**
   - Go to `Settings` tab
   - Set `Commission` to `0`
   - Set `Swap` to `0` (or disable via EA code)

6. **Start Test**
   - Click `Start` button
   - Wait for backtest to complete

7. **Export Results**

   **Export Trade List:**
   - Go to `Results` tab
   - Right-click on trade list
   - Select `Export to CSV` or `Copy All`
   - Save as: `mt5_comparison_mt5_results.csv`

   **Capture Summary:**
   - Take screenshot of `Summary` tab showing:
     - Initial Deposit
     - Net Profit
     - Gross Profit/Loss
     - Total Trades
     - Profit Trades
     - Loss Trades
     - Final Balance
   - Save as: `mt5_summary_screenshot.png`

---

### Step 3: Run C++ Backtest Engine

1. **Navigate to validation directory**
   ```bash
   cd c:/Users/user/Documents/ctrader-backtest/validation
   ```

2. **Verify CSV file is present**
   ```bash
   ls -l EURUSD_H1_202401.csv
   ```

3. **Run C++ backtest**
   ```bash
   ./test_mt5_comparison.exe
   ```

4. **Review output**
   - Console will show:
     - Strategy parameters
     - Trade entries and exits
     - Final results summary
   - Results saved to: `mt5_comparison_cpp_results.txt`

5. **Verify results file created**
   ```bash
   cat mt5_comparison_cpp_results.txt
   ```

---

### Step 4: Compare Results

1. **Load both result files**
   - MT5: `mt5_comparison_mt5_results.csv` or screenshot
   - C++: `mt5_comparison_cpp_results.txt`

2. **Create comparison table**

   | Metric | MT5 Result | C++ Result | Difference | Match? |
   |--------|------------|------------|------------|--------|
   | Initial Balance | $10,000.00 | $10,000.00 | $0.00 | ✅ |
   | Final Balance | TBD | TBD | TBD | ? |
   | Total P/L | TBD | TBD | TBD | ? |
   | Total Trades | TBD | TBD | 0 | ? |
   | Winning Trades | TBD | TBD | 0 | ? |
   | Losing Trades | TBD | TBD | 0 | ? |

3. **Compare individual trades**

   For each trade, verify:
   - Entry time matches (within 1 hour)
   - Entry price matches (within 1 pip = 0.0001)
   - Exit price matches (within 1 pip)
   - Exit reason matches (TP/SL)
   - P/L matches (within $1.00)

4. **Calculate accuracy**
   - Balance difference: `|MT5_balance - CPP_balance|`
   - Balance difference %: `(difference / 10000) × 100%`
   - **Target**: <1% or <$10

---

### Step 5: Document Findings

Create [MT5_COMPARISON_RESULTS.md](MT5_COMPARISON_RESULTS.md) with:

1. **Test Configuration**
   - Symbol, timeframe, date range
   - Initial balance, leverage
   - Strategy parameters

2. **Results Summary**
   - Comparison table from Step 4
   - Balance difference calculation
   - Pass/fail determination

3. **Trade-by-Trade Analysis**
   - Side-by-side trade comparison
   - Any discrepancies noted
   - Root cause analysis for differences

4. **Conclusion**
   - Whether <1% target achieved
   - Engine validation status
   - Recommendations for production

---

## 🎯 Success Criteria

| Criterion | Target | Status |
|-----------|--------|--------|
| Trade Count Match | Exact | ⏳ Pending |
| Entry Price Accuracy | Within 1 pip (0.0001) | ⏳ Pending |
| Exit Price Accuracy | Within 1 pip (0.0001) | ⏳ Pending |
| Individual P/L | Within $1.00 | ⏳ Pending |
| Final Balance | Within 1% or $10 | ⏳ Pending |
| **Overall Validation** | All criteria met | ⏳ Pending |

---

## 🐛 Troubleshooting

### Issue: CSV file not found
**Solution:**
- Verify file exported from MT5
- Check file path matches: `validation/EURUSD_H1_202401.csv`
- Ensure CSV format is correct (comma-separated)

### Issue: MT5 EA not found
**Solution:**
- Verify EA file is in: `C:\Users\user\AppData\Roaming\MetaQuotes\Terminal\<ID>\MQL5\Experts\`
- Compile EA in MetaEditor (press F7)
- Restart MT5

### Issue: C++ compilation error
**Solution:**
```bash
# Recompile with verbose output
C:/msys64/msys2_shell.cmd -ucrt64 -defterm -no-start -here -c \
  "cd validation && \
   g++ -I../include -std=c++17 test_mt5_comparison.cpp -o test_mt5_comparison.exe 2>&1"
```

### Issue: Different number of trades
**Possible Causes:**
- Different bar close times
- Spread differences (MT5 vs backtest)
- Order filling differences
- Check entry logic carefully

### Issue: P/L differences
**Possible Causes:**
- Commission or swap included in MT5
- Different lot size multipliers
- Rounding differences
- Check P/L calculation formulas

---

## 📊 Expected Behavior

### With Sample Data (Already Tested)
```
Initial Balance: $10,000.00
Final Balance: $10,127.00
Total P/L: $127.00
Total Trades: 2
Win Rate: 100%
```

### With Real EURUSD January 2024 Data
- Expected: 5-15 trades (depending on market volatility)
- Mix of winning and losing trades
- Final balance: TBD (depends on market movement)
- **Validation Target**: MT5 and C++ results match within 1%

---

## 📁 Output Files

After completion, you will have:

1. **MT5 Results:**
   - `mt5_comparison_mt5_results.csv` - Trade list from MT5
   - `mt5_summary_screenshot.png` - Summary screenshot

2. **C++ Results:**
   - `mt5_comparison_cpp_results.txt` - Complete backtest results
   - Console output with detailed trade log

3. **Comparison Documentation:**
   - `MT5_COMPARISON_RESULTS.md` - Detailed analysis and findings

4. **Historical Data:**
   - `EURUSD_H1_202401.csv` - Price data used for both tests

---

## ✅ Validation Checklist

### Pre-Test
- [ ] SimplePriceLevelBreakout.mq5 compiled successfully
- [ ] test_mt5_comparison.exe compiled successfully
- [ ] EURUSD H1 data for January 2024 downloaded
- [ ] CSV file exported and in correct location
- [ ] MT5 Strategy Tester configured correctly

### Execution
- [ ] MT5 backtest completed without errors
- [ ] MT5 results exported (CSV + screenshot)
- [ ] C++ backtest completed without errors
- [ ] C++ results saved successfully

### Comparison
- [ ] Trade counts match exactly
- [ ] Entry prices within 1 pip
- [ ] Exit prices within 1 pip
- [ ] Individual P/L within $1
- [ ] Final balance within 1% or $10

### Documentation
- [ ] MT5_COMPARISON_RESULTS.md created
- [ ] All discrepancies documented
- [ ] Root cause analysis complete
- [ ] Pass/fail determination made

---

## 🚀 Next Steps After Validation

### If Validation Passes (✅)
1. Update STATUS.md to Phase 8 complete
2. Create production deployment plan
3. Consider additional validation with different:
   - Symbols (GBPUSD, USDJPY, etc.)
   - Timeframes (H4, D1)
   - Date ranges (different months)

### If Validation Fails (❌)
1. Document all discrepancies in detail
2. Analyze root causes:
   - Logic differences?
   - Calculation errors?
   - Data interpretation issues?
3. Fix identified issues
4. Rerun comparison
5. Repeat until validation passes

---

## 📝 Notes

- **Identical Parameters**: Ensure all parameters match exactly between MT5 and C++
- **Data Quality**: Use the same historical data for both tests
- **No Commissions/Swap**: Disabled to simplify comparison
- **Execution Model**: MT5 uses "Every tick" for accuracy
- **Rounding**: Both implementations use same precision (5 decimal places for EURUSD)

---

**Created:** 2026-01-07
**Status:** Ready for Execution ✅
**Next:** Export EURUSD data and run both tests
