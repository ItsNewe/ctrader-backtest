# Strategy Direction Comparison

## Test Conditions
- **Starting Balance**: $10,000
- **Leverage**: 500x
- **Stop-out Level**: 50% margin
- **Data**: 2025 tick data (~53M ticks each)
- **Assets**: NAS100 (strong uptrend), Gold (strong uptrend)

---

## Summary Table

### NAS100 Results

| Strategy | Return | Max DD | Notes |
|----------|--------|--------|-------|
| **Grid Baseline** (survive=30%) | 1.99x | 88% | Original strategy |
| **Grid + Crash Detection** | 1.30x | 11% | Best risk-adjusted |
| **Bidirectional Grid** (no rebal) | 1.09x | 10% | Works in ranging markets |
| **Anti-Fragile** (exponent=1.5) | 1.10x | 12% | Promising |
| **Dynamic Hedging** | 0.13x | 99% | Failed (trending market) |
| **Volatility Harvest** | 0.98x | 6% | Breakeven |

### Gold Results

| Strategy | Return | Max DD | Notes |
|----------|--------|--------|-------|
| **Grid Baseline** (survive=15%) | 34.0x | 75% | Original strategy |
| **Grid + Crash Detection** | 5.6x | 23% | Trade off: less return, less risk |
| **Bidirectional Grid** | 0.00x | 99% | Margin call (trending up) |
| **Anti-Fragile** (base=0.02) | 5.0x | 52% | Works with strong trend |
| **Dynamic Hedging** | 0.00x | 99% | Margin call |
| **Volatility Harvest** | 0.92x | 9% | Slight loss |

---

## Detailed Analysis

### 1. Bidirectional Grid
**Concept**: Buy orders below, sell orders above, profit from oscillation

**What worked**:
- Low drawdown (10%) in NAS100 when rebalancing disabled
- Generated 179 TP hits with good win rate
- Simple to understand

**What failed**:
- Margin call on Gold (shorts in strong uptrend)
- Requires oscillating/ranging markets
- Not suitable for trending assets

**Best config**: NAS100, spacing=50, lot=0.1, no rebalancing
- Return: 1.09x, DD: 10%

### 2. Anti-Fragile Strategy
**Concept**: Small positions normally, larger positions during stress (dips)

**What worked**:
- Good returns on both assets
- Survived without margin calls
- Positions get better prices during dips
- Max stress level only reached 5 (25% drop from ATH)

**What failed**:
- Higher drawdown than crash detection approach
- Still vulnerable to extended crashes

**Best config**:
- NAS100: base=0.01, spacing=50, exponent=1.5 → 1.10x return, 12% DD
- Gold: base=0.02, spacing=5, exponent=1.5 → 5.0x return, 52% DD

### 3. Dynamic Hedging
**Concept**: Always have both long and short exposure, profit from spread

**What worked**:
- Nothing significant

**What failed**:
- In trending markets, one side accumulates heavily
- 100% net exposure (opposite of hedged)
- Both assets resulted in massive losses

**Conclusion**: Not suitable for trending assets. May work in truly range-bound markets.

### 4. Volatility Harvesting
**Concept**: Buy dips, quick TP, harvest oscillation premium

**What worked**:
- Very low drawdown (6-9%)
- High trade count (5000-7000 trades)
- ~70% win rate

**What failed**:
- Near-breakeven or slight loss
- Risk:reward ratio not favorable
- Transaction costs eat into edge

**Conclusion**: Edge too small. Need better entry signals or tighter spreads.

---

## Key Insights

### 1. Trending markets destroy symmetric strategies
Bidirectional grid and dynamic hedging both failed on Gold because they assume price will oscillate. In strong trends, one-sided exposure accumulates.

### 2. Anti-fragile principle works
The concept of "loading the spring" during dips is valid. Buying more at lower prices improves average entry and recovers well on bounces.

### 3. Grid with crash detection remains best
For NAS100: 1.30x return with only 11% DD is the best risk-adjusted performance.
For Gold: Either full Grid (34x, 75% DD) or crash-protected (5.6x, 23% DD) depending on risk tolerance.

### 4. Volatility harvesting needs refinement
The concept is sound but execution needs work. May need:
- Better entry signals (not just simple dip detection)
- Tighter spreads (broker dependent)
- Different timeframe analysis

---

## Recommendations

### For NAS100:
1. **Conservative**: Grid + Crash Detection (1.30x, 11% DD)
2. **Moderate**: Anti-Fragile (1.10x, 12% DD)
3. **Aggressive**: Baseline Grid (1.99x, 88% DD)

### For Gold:
1. **Conservative**: Grid + Crash Detection (5.6x, 23% DD)
2. **Moderate**: Anti-Fragile (5.0x, 52% DD)
3. **Aggressive**: Baseline Grid (34x, 75% DD)

### Not Recommended:
- Bidirectional Grid on trending assets
- Dynamic Hedging (fundamentally flawed for trends)
- Volatility Harvest (edge too thin)

---

## Next Steps

1. **Hybrid approach**: Combine Anti-Fragile sizing with Crash Detection exit
2. **Regime detection**: Switch strategies based on market conditions
3. **Volatility adjustment**: Scale position sizes with realized volatility
4. **Correlation signals**: Use related assets to predict movements

---

## Hybrid Strategy Results (Anti-Fragile + Crash Detection)

### Gold Performance

| Crash Velocity | Return | Max DD | Risk-Adjusted |
|----------------|--------|--------|---------------|
| -0.4% | 4.93x | **27.75%** | **Best** |
| -0.5% | 5.24x | 35.20% | Good |
| -0.6% | 6.05x | 59.40% | High risk |
| No crash detect | 5.00x | 52.00% | Baseline |

**Winner**: Hybrid with -0.4% crash velocity
- Nearly same return as pure anti-fragile (4.93x vs 5.0x)
- **Half the drawdown** (28% vs 52%)

### NAS100 Performance

The hybrid approach underperformed on NAS100 because every dip looked like a "crash" but then recovered. NAS100's volatility in 2025 triggered too many false positives.

**Recommendation**: Use pure anti-fragile on NAS100 (crash detection hurts more than helps).

---

## Final Recommendations

### For Gold:
**Hybrid Strategy** with:
```cpp
base_lot = 0.02
spacing = 5.0
sizing_exponent = 1.5
crash_velocity = -0.4%
crash_lookback = 500
```
Expected: **4.93x return, 28% max DD**

### For NAS100:
**Anti-Fragile Strategy** (no crash detection) with:
```cpp
base_lot = 0.01
spacing = 50.0
sizing_exponent = 1.5
```
Expected: **1.10x return, 12% max DD**

Or if preferring lower risk:
**Grid + Crash Detection**: 1.30x return, 11% max DD

---

*Generated from comprehensive backtesting on 2025 tick data*
