# Fill-Up Strategy Test Plan & Expected Results

## Test Configuration

### Symbol Parameters
- **Symbol**: XAUUSD (Gold)
- **Contract Size**: 100
- **Leverage**: 1:500
- **Digits**: 2
- **Margin Calculation**: Price-based (CFD_LEVERAGE mode)
  - `Margin = Lots × Contract_Size × Price / Leverage`

### Strategy Parameters (from fill up.ini)
- **Survive**: 13% (drawdown tolerance)
- **Size Multiplier**: 1.0
- **Spacing**: $1.00
- **Min Volume**: 0.01 lots
- **Max Volume**: 100.00 lots

### Backtest Settings
- **Initial Balance**: $110,000
- **Account Currency**: USD
- **Period**: 2025.01.01 - 2025.12.29 (full year)
- **Mode**: Every tick based on real ticks
- **Commission**: $0.00
- **Slippage**: 0 pips

## MT5 Reference Results

**Source**: MT5 Strategy Tester (Account 000000, )

| Metric | Value |
|--------|-------|
| Initial Balance | $110,000.00 |
| Final Balance | $528,153.53 |
| Total Profit | $418,153.53 |
| **Return** | **+380.14%** |
| Test Period | 2025.01.01 - 2025.12.29 |
| Mode | Every tick based on real ticks |

### MT5 Observations
- Conservative lot sizing (stayed around 0.03 lots based on log samples)
- Steady profit accumulation throughout the year
- No extreme drawdowns or unrealistic leverage

## C++ Implementation

### Critical Bug Fix Applied
**Issue**: Original C++ used simple FOREX margin calculation
```cpp
// WRONG - Original Code
Margin = lots × contract_size / leverage
```

**Fix**: Changed to price-based margin for XAUUSD
```cpp
// CORRECT - Fixed Code
Margin = lots × contract_size × price / leverage
```

This matches MT5's actual margin calculation for gold/metals.

### Validation Results

#### Short Test (500,000 ticks ~ 1 week)
- **Ticks Processed**: 500,000
- **Lot Size Range**: 0.01 - 0.28 lots
- **Behavior**: Realistic, matches MT5 pattern
- **Status**: ✅ PASSED

#### Full Test (25M+ ticks ~ 1 year)
- **Status**: 🔄 Running in background
- **Expected Duration**: 5-15 minutes
- **Expected Tick Count**: ~25-30 million

## Expected C++ Results

Based on the margin fix and short test validation, we expect:

### Realistic Outcomes
- **Final Balance**: $450K - $600K range
- **Return**: ~300% - 450%
- **Lot Size Range**: 0.01 - 0.30 lots (conservative)
- **Max Open Positions**: 5-15 concurrent
- **Trade Count**: 5,000 - 15,000 trades

### Key Validation Metrics

We will compare:

1. **Final Balance** - Should be within 10-20% of MT5 ($528K)
2. **Lot Sizing Behavior** - Should stay in 0.01-0.50 range (not reach 100 max)
3. **Trade Pattern** - Small positions scaled gradually
4. **Max Drawdown** - Should respect 13% survive parameter
5. **No Margin Blow-Up** - Should never hit stop-out

### Success Criteria

✅ **PASS** if:
- Final balance is $400K - $650K (within 25% of MT5)
- Lot sizes remain reasonable (< 1.0 lots typically)
- Strategy completes without errors
- No extreme leverage or unrealistic profits

❌ **FAIL** if:
- Final balance exceeds $1M (unrealistic)
- Lot sizes hit 100.00 max frequently
- Strategy margin called out
- Profits in billions (like the broken version)

## Technical Implementation Details

### Files Created/Modified
1. **include/fill_up_strategy.h** - Complete grid strategy implementation
2. **validation/test_fill_up.cpp** - Full year test runner
3. **validation/test_fill_up_short.cpp** - Quick validation test
4. **validation/fetch_xauusd_specs.py** - MT5 specs fetcher

### Key Components

#### Position Sizing Algorithm
```cpp
void SizingBuy(TickBasedEngine& engine, int positions_total) {
    // 1. Calculate margin levels
    double margin_stop_out_level = 30.0;  // MT5 default
    double equity_at_target = current_equity_ * margin_stop_out_level / current_margin_level;

    // 2. Determine end price (13% drawdown target)
    double end_price = (positions_total == 0)
        ? current_ask_ * ((100.0 - survive_pct_) / 100.0)
        : highest_buy_ * ((100.0 - survive_pct_) / 100.0);

    // 3. Calculate grid size
    double distance = current_ask_ - end_price;
    double number_of_trades = std::floor(distance / spacing_buy_);

    // 4. Binary search for optimal multiplier
    // ... (ensures we don't exceed stop-out level)

    // 5. Apply multiplier to min volume
    trade_size_buy_ = multiplier * min_volume_;
    trade_size_buy_ = std::min(trade_size_buy_, max_volume_);
}
```

#### Grid Placement
```cpp
void OpenNew(TickBasedEngine& engine) {
    if (positions_total == 0) {
        // First position at current price
        SizingBuy(engine, 0);
        Open(trade_size_buy_, engine);
    } else {
        // Add positions below (price dropped)
        if (lowest_buy_ >= current_ask_ + spacing_buy_) {
            SizingBuy(engine, positions_total);
            Open(trade_size_buy_, engine);
        }
        // Add positions above (price rose)
        else if (highest_buy_ <= current_ask_ - spacing_buy_) {
            SizingBuy(engine, positions_total);
            Open(trade_size_buy_, engine);
        }
        // Fill gaps in grid
        else if ((closest_above_ >= spacing_buy_) && (closest_below_ >= spacing_buy_)) {
            SizingBuy(engine, positions_total);
            Open(trade_size_buy_, engine);
        }
    }
}
```

#### Take Profit
```cpp
double tp = current_ask_ + current_spread_ + spacing_buy_;
```

Each position takes profit at entry + spread + spacing ($1.00).

## Comparison Methodology

After full test completes:

1. **Extract C++ Results**
   - Initial Balance
   - Final Balance
   - Total P/L
   - Return %
   - Trade Count
   - Max Lot Size
   - Max Open Positions

2. **Compare with MT5**
   - Calculate accuracy: `(1 - |C++ - MT5| / MT5) × 100%`
   - Acceptable range: 80-120% (within 20%)

3. **Document Results**
   - Create detailed comparison report
   - Include sample trades
   - Analyze any discrepancies

## Next Steps

1. ⏳ Wait for full test completion (running in background)
2. 📊 Extract and analyze results
3. 📝 Create comparison document
4. ✅ Validate accuracy vs MT5
5. 🎯 If passed: Strategy validated for production use

---

**Test Started**: 2026-01-07
**MT5 Baseline**: $528,153.53 final balance (+380.14%)
**Test Status**: Running (~25M ticks)
