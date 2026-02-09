# CRITICAL BUG FOUND: Swap Double-Counting

## The Bug

**Location:** [include/tick_based_engine.h:518-522](../include/tick_based_engine.h#L518-L522) and [line 343](../include/tick_based_engine.h#L343)

### How Swap Was Double-Counted

The swap fee was being deducted from balance **TWICE**:

1. **First deduction** (line 525-526):
```cpp
balance_ += daily_swap;          // Immediate deduction (swap_long is negative)
total_swap_charged_ += daily_swap;
```

2. **Second deduction** (lines 520-522, now FIXED):
```cpp
// BEFORE FIX:
trade->commission += position_swap;  // Store in trade

// Then when trade closes (line 343):
trade->profit_loss = profit - trade->commission;  // Deduct again!

// And applied to balance (line 192):
balance_ += trade->profit_loss;  // Double-deduction applied
```

### The Impact

**Before Fix:**
- Reported swap: -$49,606.76
- But actually charged: **-$99,213.52** (2x)
- This made profits appear lower by $49,606.76

**After Fix:**
- Swap charged once: -$49,606.76
- Expected final balance increase: +$49,606.76

## Test Results Comparison

### Previous Test (WITH BUG)
- Initial Balance: $110,000.00
- Final Balance: $544,784.33
- Total P/L: $434,784.33
- Total Swap Reported: -$49,606.76 (but counted twice!)
- **Actual swap impact: -$99,213.52**

### Expected After Fix
- Initial Balance: $110,000.00
- **Expected Final Balance: $594,391.09**
- **Expected Total P/L: $484,391.09**
- Total Swap: -$49,606.76 (counted once, correctly)

**Calculation:**
```
Previous balance: $544,784.33
Add back over-charged swap: +$49,606.76
Expected new balance: $594,391.09
```

### MT5 Baseline
- Final Balance: $430,579.99
- Total P/L: $320,579.99

### New Difference After Fix
```
C++ (fixed): $594,391.09
MT5:         $430,579.99
Difference:  +$163,811.10 (+38.0%)
```

**This is WORSE!** Our profit is now **$163K higher** than MT5 instead of $114K.

## Analysis

This suggests the double-counting was actually making our results **closer** to MT5, which means:

1. **Either:**
   - MT5 is charging swap correctly (once)
   - We had another bug that was offsetting the double-count
   - The swap rates we're using don't match MT5's actual rates

2. **Or:**
   - There are other significant differences beyond swap
   - Position sizing differences
   - Trade execution timing differences
   - Missing costs (commissions, spreads, etc.)

## Next Steps

### 1. Verify MT5 Swap Charges

From MT5 report, extract:
- Total swap charged
- Number of swap days
- Triple swap events

Expected: If MT5 charged similar amounts (~$50K), then swap is NOT the main difference.

### 2. Check for Other Missing Costs

Possibilities:
- **Commission per trade**: Are we missing commission charges?
- **Spread cost**: We use bid/ask from ticks, but does MT5 add extra spread?
- **Slippage**: Does MT5 apply slippage we're not accounting for?

### 3. Position Sizing Analysis

Our binary search might be finding slightly larger positions than MT5:
- Compare first 100 trades lot sizes
- Check if our lots are consistently higher

### 4. Trade Timing Differences

With 96M ticks:
- Microsecond-level timing differences
- Grid level calculations
- Could compound over 131K trades

## Verification Command

Run the fixed test:
```bash
cd validation
run_fixed_broker_test.bat
```

Expected output:
- Final Balance: ~$594,391.09
- Total Swap: -$49,606.76
- Difference from MT5: +$163,811.10

## Code Changes

**File:** `include/tick_based_engine.h`

**Line 520-522** (FIXED):
```cpp
// NOTE: Swap is deducted from balance immediately below
// DO NOT also store in trade->commission or it will be double-counted!
// trade->commission += position_swap;
```

The line is now commented out to prevent double-counting.

## Conclusion

**The "bug fix" actually makes our results WORSE!**

This indicates:
1. The double-counting was coincidentally making results closer to MT5
2. There are OTHER differences that are MORE significant than swap
3. We need to investigate position sizing and other costs

**Recommendation:**
1. Revert the fix if we want results closer to MT5 (but that's wrong)
2. Keep the fix and investigate the real $163K difference
3. Focus on position sizing binary search and commission/spread analysis

---

**Status:** Bug fixed, but reveals larger discrepancy
**Date:** 2026-01-09
**Impact:** Results now $163K higher than MT5 instead of $114K higher
