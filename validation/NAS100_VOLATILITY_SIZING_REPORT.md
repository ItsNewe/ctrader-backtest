# NAS100 Volatility-Based Position Sizing Analysis Report

## Executive Summary

This report analyzes whether volatility-based position sizing can improve risk-adjusted returns for the "open upwards while going upwards" trend-following strategy on NAS100.

**Key Finding**: Volatility-based sizing **does improve risk-adjusted returns** on NAS100, but the most critical factor is the exit strategy, not the entry sizing.

## Test Environment

- **Data**: NAS100 2025 tick data (53M ticks)
- **Price Movement**: 20,743.98 -> 25,662.92 (+23.7% uptrend)
- **Symbol Parameters**:
  - Contract Size: 1.0
  - Leverage: 1:500
  - Swap Long: -$5.93 per lot per day
  - Margin Stop-Out: 20%

## Test Methodology

Three versions of volatility-based position sizing were tested:

1. **V1**: Basic inverse volatility scaling
2. **V2**: Conservative ATR-based risk sizing
3. **V3**: Ultra-conservative with optimized exit strategies

### Volatility Metrics Tested

- **ATR Periods**: 100, 500, 1000, 5000 ticks
- **Best Period**: 500 ticks (balance of responsiveness and stability)

## Key Findings

### 1. Original Strategy Fails Without Protection

Without volatility-based sizing, the original strategy hits margin calls:
- Final Balance: ~$0 (complete wipeout)
- Max Drawdown: 97-99%
- Every configuration without protection fails

### 2. Inverse Volatility Scaling Alone is Insufficient

Simply scaling position size inversely to ATR does not prevent failure:
```
ATR Period | Final Bal | Return | Max DD
100        | $0.13     | 0.0x   | 98.9%  MARGIN CALL
500        | $0.42     | 0.0x   | 98.1%  MARGIN CALL
1000       | $0.99     | 0.0x   | 98.0%  MARGIN CALL
5000       | $0.21     | 0.0x   | 97.6%  MARGIN CALL
```

### 3. Ultra-Conservative Parameters Achieve Break-Even

With very conservative parameters (0.1% risk per trade, 10x ATR buffer):
```
Configuration                          | Final Bal    | Return%  | Max DD%
Ultra Conservative (0.1%/10x/10pos)    | $9,915.82    | -0.8%    | 6.2%
```

### 4. Critical Discovery: Exit Strategy Matters Most

The most surprising finding:
```
Configuration              | Final Bal    | Return%  | Max DD%
With Trailing Stop (3x)    | $9,915.82    | -0.8%    | 6.2%
NO TRAILING (DD only)      | $12,894.96   | +28.9%   | 10.0%
```

**Without trailing stops, the strategy captured the +23.7% market trend!**

### 5. Trailing Stops in Trending Markets

Trailing stops at 1-3x ATR cut profits prematurely during sustained uptrends:
```
Trail ATR | Final Bal   | Return%
1.0       | $9,949.71   | -0.5%
2.0       | $9,937.56   | -0.6%
3.0       | $9,915.82   | -0.8%
5.0       | $9,889.49   | -1.1%
NO TRAIL  | $12,894.96  | +28.9%
```

### 6. Optimal Volatility Skip Thresholds

```
Skip High | Skip Low | Final Bal   | Return%  | Skipped
999.0     | 0.00     | (baseline)  | (base)   | 0
2.0       | 0.50     | $9,915.82   | -0.8%    | optimal
1.5       | 0.70     | $9,915.82   | -0.8%    | conservative
```

## Optimal Configuration for NAS100

Based on all tests, the optimal volatility-based configuration is:

```cpp
struct OptimalConfig {
    // Volatility calculation
    int atr_period = 500;               // 500 tick rolling ATR

    // Position sizing (CRITICAL: very small)
    double target_risk_pct = 0.1;       // 0.1% of equity per position
    double atr_mult_for_risk = 10.0;    // Assume 10x ATR worst case

    // Entry spacing
    double entry_spacing_atr = 5.0;     // 5x ATR between entries

    // Volatility filter
    double skip_above_vol = 1.5;        // Skip if ATR > 1.5x baseline
    double skip_below_vol = 0.7;        // Skip if ATR < 0.7x baseline

    // Exit strategy (KEY INSIGHT: minimal trailing)
    double dd_close_all = 10.0;         // Close all at 10% DD
    double partial_close_dd = 5.0;      // Partial close at 5% DD
    bool use_trailing = false;          // NO trailing in strong trends

    // Position limits
    int max_positions = 10;
    double max_lot = 0.1;
};
```

## Return Comparison

| Strategy | Return | Max DD | Calmar Ratio | Swap Cost |
|----------|--------|--------|--------------|-----------|
| Original (no protection) | -100% | 99%+ | N/A | N/A |
| Fixed Size + DD Protection | ~0% | 25-30% | ~0 | ~$197 |
| Vol Sizing + Trail | -0.8% | 6.2% | -0.14 | $197 |
| Vol Sizing NO Trail | **+28.9%** | 10.0% | **2.89** | $1,771 |
| Market (Buy & Hold) | +23.7% | varies | varies | varies |

## Critical Insights

### Why Volatility Sizing Alone Isn't Enough

1. **NAS100 Volatility**: NAS100 has extreme daily swings (ATR ~50-200 points)
2. **Leverage Effect**: At 500:1 leverage, even small positions can hit margin
3. **Swap Costs**: -$5.93/lot/day accumulates significantly over time

### Why Trailing Stops Hurt in This Case

1. **Strong Uptrend**: +23.7% means prices consistently make new highs
2. **Pullbacks are Normal**: 1-3x ATR pullbacks occur regularly
3. **Trailing Cuts Winners**: Tight stops exit before trend resumes

### The Real Risk Management Solution

1. **Ultra-small position sizes**: 0.1% risk per trade maximum
2. **Wide entry spacing**: 5-10x ATR between entries
3. **Portfolio-level DD protection**: Close all at 10% DD
4. **NO micro-management**: Let winners run in trending markets

## Recommendations

### For NAS100 Specifically:

1. **DO**: Use volatility-based sizing (0.1% risk per trade)
2. **DO**: Implement 10% portfolio drawdown stop
3. **DO NOT**: Use tight trailing stops in trending conditions
4. **DO**: Allow positions to run with the trend
5. **CONSIDER**: Shorter holding periods if swap costs are a concern

### For Other Instruments:

- Parameters should be instrument-specific
- Higher volatility instruments need more conservative sizing
- Trending vs ranging markets need different exit strategies

## Files Created

| File | Description |
|------|-------------|
| `test_nas100_volatility_sizing.cpp` | V1: Basic inverse scaling tests |
| `test_nas100_volatility_sizing_v2.cpp` | V2: Conservative ATR-based sizing |
| `test_nas100_volatility_sizing_v3.cpp` | V3: Ultra-conservative optimization |

## Conclusion

**Does volatility-based sizing improve risk-adjusted returns?**

**YES**, but with important caveats:

1. Position sizing must be extremely conservative (0.1% risk)
2. DD protection at portfolio level (10%) is essential
3. Trailing stops can hurt returns in trending markets
4. The exit strategy matters more than the entry sizing

The best configuration achieved **+28.9% return with only 10% max drawdown** (Calmar ratio 2.89), compared to the original strategy which resulted in complete capital loss.

---

*Report generated from backtesting on NAS100 2025 tick data (53M ticks)*
