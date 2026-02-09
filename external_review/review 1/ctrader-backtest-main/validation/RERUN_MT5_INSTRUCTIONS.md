# Rerun MT5 Strategy Tester with Adjusted Trigger Levels

## Issue Identified

The initial trigger levels (1.2000/1.1900) were outside the January 2024 EURUSD price range (1.08-1.10), resulting in **0 trades** in MT5.

## Adjusted Trigger Levels

**New levels (matching actual January 2024 prices):**
- Long Trigger: **1.0950**
- Short Trigger: **1.0900**

## C++ Results (Already Complete) ✅

```
Initial Balance: $10,000.00
Final Balance:   $9,868.90
Total P/L:       -$131.10
Total Trades:    5
Winning Trades:  1
Losing Trades:   4
```

**Trade Details:**
1. BUY @ 1.09566 → SL @ 1.09066 = -$50.00
2. BUY @ 1.09533 → SL @ 1.09033 = -$50.00
3. BUY @ 1.09784 → SL @ 1.09284 = -$50.00
4. BUY @ 1.09508 → SL @ 1.09008 = -$50.00
5. SELL @ 1.08863 → END @ 1.08174 = +$68.90

## Action Required: Rerun MT5

The SimplePriceLevelBreakout.mq5 EA has been **updated with new trigger levels**.

### Steps:

1. **Open MetaTrader 5**

2. **Open Strategy Tester** (Ctrl+R)

3. **Configure:**
   - Expert Advisor: **SimplePriceLevelBreakout**
   - Symbol: **EURUSD**
   - Period: **H1**
   - Dates: **2024.01.01 - 2024.01.31**
   - Deposit: **10000**
   - Leverage: **1:100** (or 1:500, doesn't matter for these small trades)

4. **Input Parameters** (EA will auto-populate with new defaults):
   - LongTriggerLevel: **1.0950** ✅ (updated)
   - ShortTriggerLevel: **1.0900** ✅ (updated)
   - LotSize: **0.10**
   - StopLossPips: **50**
   - TakeProfitPips: **100**

5. **Click Start** and wait for completion

6. **Export Results:**
   - Go to "Results" tab
   - Right-click trade list
   - Select "Export to CSV" or copy to Excel
   - Save as: `mt5_results_adjusted.csv`

7. **Screenshot:**
   - Capture the "Summary" tab showing final balance

## Expected MT5 Results

If implementation is correct, MT5 should show:
- **5 trades** (4 BUY, 1 SELL)
- **Final balance ~$9,868.90** (within $10 of C++ result)
- Entry prices matching C++ within 1-2 pips

## Comparison Criteria

| Metric | C++ Result | MT5 Result | Difference | Pass? |
|--------|------------|------------|------------|-------|
| Trade Count | 5 | ? | ? | ? |
| Final Balance | $9,868.90 | ? | ? | <1%? |
| Trade 1 Entry | 1.09566 | ? | ? | <1 pip? |
| Trade 1 P/L | -$50.00 | ? | ? | <$1? |

## Next Steps After MT5 Rerun

1. Compare trade counts
2. Compare entry/exit prices
3. Compare individual P/L
4. Calculate final balance difference
5. Create MT5_COMPARISON_RESULTS.md with analysis
