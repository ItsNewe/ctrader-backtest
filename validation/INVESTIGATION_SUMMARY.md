# Investigation Summary: $114K Difference Between C++ and MT5

## Status: CRITICAL BUG FOUND

### The Discovery

I found that **swap fees were being double-counted** in our C++ implementation!

## The Bug Explained

### How It Happened

The swap fee processing had a fatal flaw:

**Step 1:** When daily swap was charged (lines 518-526):
```cpp
daily_swap += position_swap;
trade->commission += position_swap;  // ❌ BUG: Stored in trade
balance_ += daily_swap;              // ✅ Correctly deducted immediately
```

**Step 2:** When trade closed (lines 334-344):
```cpp
double profit = price_diff * trade->lot_size * config_.contract_size;
trade->profit_loss = profit - trade->commission;  // ❌ BUG: Deducted AGAIN
balance_ += trade->profit_loss;                    // ❌ Double deduction applied
```

### The Impact

**Swap was deducted TWICE:**
1. Immediately when charged: -$49,606.76
2. Again when trade closed: -$49,606.76
3. **Total deducted: -$99,213.52** (but only $49K was real!)

### The Paradox

**Before Fix (with bug):**
- C++ Final Balance: $544,784.33
- MT5 Final Balance: $430,579.99
- **Difference: +$114,204** (C++ higher)

**After Fix (corrected):**
- Expected C++ Final Balance: $594,391.09 (+$49,606.76)
- MT5 Final Balance: $430,579.99
- **Difference: +$163,811** (C++ even HIGHER!)

## Why This Is Concerning

The bug fix makes our results **WORSE**, meaning:

1. **The double-counting was accidentally making us closer to MT5**
2. **There's a BIGGER underlying difference of ~$163K**
3. **Something else is causing C++ to be more profitable**

## Possible Explanations

### Theory 1: MT5 Has Additional Costs

**Missing from C++:**
- Commission per trade?
- Additional spread markup?
- Slippage simulation?
- Regulatory fees?

**To Verify:**
- Check MT5 report for commission line items
- Compare spread values in ticks vs MT5's effective spread
- Look for any "other costs" in MT5 report

### Theory 2: Position Sizing Differences

Our binary search algorithm might be finding **larger optimal positions** than MT5:

**Why this matters:**
- Larger positions = more profit per pip
- Over 131,177 trades, small differences compound
- Need to compare lot sizes trade-by-trade

**To Verify:**
- Extract first 100 trades from both systems
- Compare lot sizes at same timestamps
- Check if C++ lots are consistently 10-20% higher

### Theory 3: Trade Execution Timing

With 96,279,625 ticks processed:
- Microsecond-level timing differences
- Grid level boundary calculations
- Tick interpretation differences

**Could explain:**
- Different entry prices (better fills in C++)
- More favorable exit prices
- Compounding over 131K trades

### Theory 4: MT5 Swap Calculation Different

MT5 might be charging swap differently:
- Different rollover time
- Different triple swap calculation
- Different swap rates than we fetched
- Swap-free days we're not accounting for

**To Verify:**
- Extract MT5 total swap from report
- Should be close to our -$49,606.76
- If significantly different, explains gap

## Action Plan

### Immediate Actions

1. **Run the fixed test** (no more double-counting):
```bash
cd validation
run_fixed_broker_test.bat
```

2. **Wait for your MT5 re-run** to get fresh baseline data

3. **Extract MT5 swap total** from new report

### Investigation Priority

**HIGH PRIORITY:**
1. ✅ Swap double-counting (FIXED)
2. 🔍 Position sizing comparison (next)
3. 🔍 Commission/spread analysis

**MEDIUM PRIORITY:**
4. Trade timing differences
5. MT5 swap calculation verification

**LOW PRIORITY:**
6. Floating-point precision differences
7. Rounding errors

## Expected Next Results

### Scenario A: Swap Fix Reveals Real Difference
```
C++ (fixed):  $594,391.09
MT5:          $430,579.99
Difference:   +$163,811.10 (+38%)
```

**Conclusion:** There's a massive difference beyond swap. Focus on position sizing.

### Scenario B: MT5 Charges Different Swap
```
If MT5 swap = -$163,811 total:
C++ final = $594,391 - $163,811 + $49,607 = $480,187
MT5 final = $430,580
Difference: +$49,607 (matches swap double-count)
```

**Conclusion:** Swap was the main issue all along.

### Scenario C: Commission Missing
```
If commission = $1.25/trade × 131,177 = $163,971
C++ final = $594,391 - $163,971 = $430,420
MT5 final = $430,580
Difference: -$160 (essentially equal!)
```

**Conclusion:** We're missing per-trade commission charges.

## Files for Your Review

1. **Bug Analysis:** [SWAP_DOUBLE_COUNT_BUG_ANALYSIS.md](SWAP_DOUBLE_COUNT_BUG_ANALYSIS.md)
2. **Triple Swap Implementation:** [TRIPLE_SWAP_IMPLEMENTATION.md](TRIPLE_SWAP_IMPLEMENTATION.md)
3. **Previous Results:** [BROKER_RESULTS_COMPARISON.md](BROKER_RESULTS_COMPARISON.md)
4. **Run Script:** [run_fixed_broker_test.bat](run_fixed_broker_test.bat)

## Recommendation

**You mentioned you'll rerun MT5 tests.** Perfect! Here's what to extract:

### From MT5 Report:
1. Total swap charged
2. Total commission (if any)
3. Any "other costs" line items
4. First 20 trades with lot sizes
5. Final balance (confirm $430,579.99)

### From C++ Test (after you run):
1. Final balance (expected ~$594,391)
2. Total swap (should be -$49,606.76)
3. First 20 trades with lot sizes
4. Any error messages

### Comparison:
- If lot sizes match → investigate timing/execution
- If lot sizes differ → binary search issue
- If swap differs significantly → swap calculation issue
- If there's commission in MT5 → add to C++

## Code Status

**✅ Fixed:** Swap double-counting removed
**📝 Changed:** [include/tick_based_engine.h:520-522](../include/tick_based_engine.h#L520-L522)
**🔍 Ready:** For your manual test run

---

**Date:** 2026-01-09
**Status:** Bug fixed, awaiting verification
**Next:** Compare with your MT5 rerun results
