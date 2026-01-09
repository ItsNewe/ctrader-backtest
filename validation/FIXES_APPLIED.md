# Critical Fixes Applied to C++ Backtest Engine

## Date: 2026-01-08

## Issues Identified and Fixed:

### 1. ✅ Date Filtering (CRITICAL)
**Problem**: C++ engine processed ALL ticks in CSV file, including data after Dec 29, 2025
- MT5 stopped at Dec 29 as configured
- C++ processed 6,299 extra trades (Dec 30-31 + Jan 2026)

**Fix Applied**:
- Added `start_date` and `end_date` to TickBacktestConfig
- MT5 behavior: start_date is INCLUSIVE, end_date is EXCLUSIVE
- Engine now breaks loop when tick.timestamp >= end_date
- Location: `include/tick_based_engine.h` lines 103-108

**Configuration**:
```cpp
config.start_date = "2025.01.01";  // Inclusive
config.end_date = "2025.12.29";    // Exclusive - stops before this date
```

---

### 2. ✅ Swap/Rollover Fees (CRITICAL)
**Problem**: No swap fee implementation - positions held overnight had ZERO cost
- MT5 charges negative swap daily for long XAUUSD positions
- Grid strategy holds many positions overnight for extended periods
- Missing swap fees artificially inflated C++ profits

**Fix Applied**:
- Added swap tracking system with daily rollover
- Swap charged once per day when date changes
- Added `swap_long`, `swap_short`, `swap_mode` to config
- Location: `include/tick_based_engine.h` ProcessSwap() method lines 443-486

**Swap Calculation**:
- Mode 2 (SYMBOL_SWAP_MODE_CURRENCY_SYMBOL) = USD per lot per day
- For each open position: `swap = swap_per_lot × lot_size`
- Deducted from balance daily

**IMPORTANT**: Currently using PLACEHOLDER swap rate of -10.0 USD/lot/day
- Typical XAUUSD long swap ranges from -8 to -15 USD per lot
- **ACTION REQUIRED**: Run `GetSwapRates.mq5` script in MT5 to get actual rate
- Update `config.swap_long` in test_fill_up.cpp with real value

---

### 3. ✅ Contract Size in Unrealized P/L
**Already Fixed Previously**: Changed from hardcoded 100,000 to actual contract_size (100)

---

## Summary of Changes:

### Files Modified:
1. **include/tick_based_engine.h**:
   - Added date filtering fields to TickBacktestConfig
   - Added swap rate fields to TickBacktestConfig
   - Implemented date range checking in Run() loop
   - Added ProcessSwap() method for daily swap charges
   - Added swap tracking variables (last_swap_date_, total_swap_charged_)

2. **validation/test_fill_up.cpp**:
   - Added start_date and end_date configuration
   - Added swap_long/swap_short configuration
   - Using placeholder swap rate (-10.0) pending MT5 verification

---

## Next Steps:

### 1. Get Actual Swap Rates from MT5
Run the `GetSwapRates.mq5` script that was created:
- Location: `MQL5/Scripts/GetSwapRates.mq5`
- Will output: swap_long, swap_short, swap_mode for XAUUSD
- Update test_fill_up.cpp line 36 with actual value

### 2. Run Test with Correct Configuration
Once swap rate is obtained:
```bash
cd validation
./test_fill_up.exe > fill_up_final_corrected_results.log 2>&1
```

### 3. Expected Results
With date filtering and swap fees properly applied:
- Should process ~145,000-147,000 trades (similar to MT5)
- Should stop exactly at Dec 28 end-of-day
- Final balance should be LOWER than previous $680K due to swap costs
- Should be much closer to MT5's $528K baseline

---

## Verification Checklist:
- [x] Date filtering implemented (start inclusive, end exclusive)
- [x] Swap system implemented (daily rollover)
- [x] Compilation successful
- [ ] Actual swap rate obtained from MT5
- [ ] Test run with correct swap rate
- [ ] Results comparison with MT5
