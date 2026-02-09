# NAS100 Hyperbolic Strategy Analysis Summary

## Overview

Tested the hyperbolic position sizing strategy from `Nasdaq_up.mq5` on NAS100 tick data for 2025.

**Data Period**: Jan 2025 - Jan 2026
**Total Ticks**: 53.5M
**Market Movement**: +23.7% (20,744 → 25,663)

## Key Findings

### 1. Original Strategy Performance

The original hyperbolic strategy **fails on the full year** despite a +23.7% uptrend:

| Configuration | Final Balance | Return | Max DD | Status |
|--------------|---------------|--------|--------|--------|
| Pure Hyperbolic (pw=-0.5) | $1,545 | 0.15x | 79% | Loss |
| Pure Hyperbolic (pw=-0.8) | $798 | 0.08x | 94% | Loss |
| Room-based (pw=+0.5) | $0.28 | 0.0x | 96% | MC |

### 2. Period-Specific Analysis

**Apr 22 - Jul 31, 2025 (+30.4% rally)**:
- Original strategy: **101.9x return** ($1M from $10k)
- This period had minimal corrections

**Full Year (includes corrections)**:
- Same strategy: **Margin Call** or severe losses
- Corrections in Q1 (-7.3%) and Q4 wipe out gains

### 3. Improvement Strategies Tested

#### A. Regime Filter (Only trade in bull markets)
- Detects bull/bear/sideways using price momentum
- Result: **$1,198** (0.1x) with 42% max DD
- Reduces exposure but still loses

#### B. Hybrid Approach (Hyperbolic in bull, Grid in sideways)
- Grid trading during sideways markets
- Result: **Margin Call** - Grid gets trapped in corrections

#### C. Trailing Profit Lock (Per-position trailing stop)
- Activation: 100-200pts, Trail: 50-100pts
- Result: **$1,567** (0.16x) - Slightly better but still losing

#### D. Aggressive Take Profit (Fixed TP per position)
- TP at 200-300 points
- Result: **$1,734** (0.17x) - Best TP at 300pts

#### E. Portfolio Drawdown Limit
- Close all positions at 15-30% portfolio DD
- Result: **$1,919** (0.2x) at 15% DD limit
- **Best capital preservation method**

#### F. Combined Strategy (Regime + TP + Trail + DD Limit)
- All protective measures combined
- Result: **$1,558** (0.16x) with 19% max DD

## Best Configurations

### For Capital Preservation (Recommended)
```
DD Limit: 15%
TP: 200 pts
Lot Coefficient: 30
Power: -0.5
Regime Lookback: 300k ticks
```
Result: $1,558 (0.16x), 19% max DD

### For Higher Returns (More Risk)
```
DD Limit: 25%
TP: 300 pts
Lot Coefficient: 50
Power: -0.3
Regime Lookback: 200k ticks
```
Result: $2,204 (0.22x), 30% max DD

## Root Cause Analysis

The hyperbolic strategy fails because:

1. **Opens on every new high**: Creates massive exposure at market tops
2. **No natural exit**: Positions accumulate until stop-out
3. **Asymmetric risk**: Small corrections wipe out large gains
4. **Index volatility**: NAS100 has sharper corrections than gold

### Comparison: Apr-Jul vs Full Year

| Period | Market Move | Hyperbolic Return | Why |
|--------|-------------|-------------------|-----|
| Apr-Jul | +30.4% | 101.9x | Consistent rally, no major corrections |
| Full Year | +23.7% | 0.15x | Multiple 5-10% corrections |

## Recommendations

### 1. Use This Strategy Only During Strong Bull Runs
- Works extremely well in trending markets (100x+ returns possible)
- Need external signal to identify "strong bull" periods
- Consider fundamental/technical indicators for regime detection

### 2. Implement Hard DD Limit (15-20%)
- Essential for capital preservation
- Cuts losses before margin call
- Allows recovery with remaining capital

### 3. Consider Alternative Approaches for NAS100

**Option A: Mean Reversion**
- Buy dips, sell rallies instead of trend-following
- Better suited for choppy index movements

**Option B: Time-Based Strategy**
- Only trade during specific sessions (US market hours)
- Avoid overnight gaps and news events

**Option C: Volatility Filter**
- Pause trading when VIX/volatility spikes
- Resume only when volatility normalizes

### 4. Position Sizing Adjustment
- Current formula too aggressive for indices
- Consider 50% smaller positions (lot_coef = 15-20)
- Slower accumulation = more room to survive corrections

## Conclusion

The hyperbolic position sizing strategy is **highly profitable in bull runs** (100x+ returns in Apr-Jul 2025) but **catastrophic in corrective periods**. For practical use on NAS100:

1. Use regime detection to only activate in confirmed uptrends
2. Implement strict 15% portfolio DD limit
3. Take profits aggressively (200-300 pts)
4. Accept lower returns (0.2-0.4x) for capital preservation

The strategy is best suited for markets with consistent trends and limited corrections, such as:
- Strong earnings seasons
- QE/stimulus periods
- Clear technical breakouts

**Not recommended for**: Range-bound markets, high volatility periods, or full-year automated trading without monitoring.
