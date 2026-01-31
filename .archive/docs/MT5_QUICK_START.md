# MT5 Comparison - Quick Start

**Phase 8: MT5 Validation**
**Status:** Ready to Execute ✅

---

## ⚡ Quick Execution Steps

### 1. Export Data from MT5 (5 minutes)

```
1. Open MetaTrader 5
2. Open MetaEditor (press F4)
3. Navigate to: Scripts/ExportEURUSD_H1.mq5
4. Compile (press F7)
5. Close MetaEditor
6. In MT5 Navigator, drag ExportEURUSD_H1 onto any chart
7. Click "OK" on parameters dialog
8. Wait for export to complete (message box will appear)
9. File saved to: Terminal/MQL5/Files/EURUSD_H1_202401.csv
```

### 2. Copy CSV to Project (1 minute)

```bash
# Find the file location (shown in MT5 message)
# Copy to validation directory
cp "C:/Users/user/AppData/Roaming/MetaQuotes/Terminal/<BROKER_ID>/MQL5/Files/EURUSD_H1_202401.csv" \
   "c:/Users/user/Documents/ctrader-backtest/validation/"
```

Or manually:
1. Open file location shown in MT5 message box
2. Copy `EURUSD_H1_202401.csv`
3. Paste to: `c:\Users\user\Documents\ctrader-backtest\validation\`

### 3. Run MT5 Strategy Tester (10 minutes)

```
1. In MT5, press Ctrl+R (Strategy Tester)
2. Select: SimplePriceLevelBreakout EA
3. Symbol: EURUSD
4. Period: H1
5. Date: 2024.01.01 - 2024.01.31
6. Deposit: 10000
7. Click "Start"
8. After completion:
   - Go to Results tab
   - Right-click → Export to CSV
   - Save as: mt5_comparison_mt5_results.csv
   - Screenshot the Summary tab
```

### 4. Run C++ Backtest (30 seconds)

```bash
cd c:/Users/user/Documents/ctrader-backtest/validation
./test_mt5_comparison.exe
```

Results saved to: `mt5_comparison_cpp_results.txt`

### 5. Compare Results (5 minutes)

Open both files and compare:
- **Trade Count**: Should match exactly
- **Entry/Exit Prices**: Should be within 1 pip (0.0001)
- **Final Balance**: Should be within 1% or $10

---

## 📊 What to Expect

### With Sample Data (Already Verified)
```
✅ C++ Engine: $10,127.00 (2 trades, 100% win rate)
```

### With Real January 2024 Data
```
⏳ MT5 Result: TBD
⏳ C++ Result: TBD
🎯 Target: <1% difference
```

---

## ✅ Files Ready

| File | Location | Status |
|------|----------|--------|
| **SimplePriceLevelBreakout.mq5** | MT5/Experts/ | ✅ Ready |
| **ExportEURUSD_H1.mq5** | MT5/Scripts/ | ✅ Ready |
| **test_mt5_comparison.exe** | validation/ | ✅ Compiled |
| **simple_price_level_breakout.h** | include/ | ✅ Ready |

---

## 🎯 Success Criteria

- ✅ Trade count matches exactly
- ✅ Entry prices within 1 pip
- ✅ Exit prices within 1 pip
- ✅ P/L within $1 per trade
- ✅ Final balance within 1%

---

## 📚 Detailed Documentation

For detailed instructions, troubleshooting, and analysis:
- [MT5_COMPARISON_GUIDE.md](MT5_COMPARISON_GUIDE.md) - Complete guide
- [MT5_COMPARISON_STRATEGY.md](MT5_COMPARISON_STRATEGY.md) - Strategy specification

---

**Ready to execute Phase 8 MT5 Validation!** 🚀
