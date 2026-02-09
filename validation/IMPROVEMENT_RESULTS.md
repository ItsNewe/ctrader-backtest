# Grid Strategy Improvement Results

## Baseline Performance

| Asset | Return | Max DD | Trades |
|-------|--------|--------|--------|
| NAS100 (survive=30%) | **1.99x** | **88.28%** | 103 |
| GOLD (survive=15%) | **34.01x** | **74.70%** | 516 |

---

## Improvement 1: Regime Filter (SMA)

**Concept**: Only enter new positions when price > SMA(n)

| Config | Return | Max DD | Regime Blocks |
|--------|--------|--------|---------------|
| SMA(200), 0.5% buffer | 1.99x | 88.28% | 1988 |
| SMA(100), 1.0% buffer | 1.99x | 88.28% | 181 |
| SMA(50), 2.0% buffer | 1.99x | 88.28% | 39 |

**Finding**: No improvement. Strategy only enters on new ATH anyway, so SMA filter doesn't help.

---

## Improvement 2: Profit Taking

**Concept**: Close portion of positions when average profit reaches threshold

| Config | Return | Max DD | Profit Takes |
|--------|--------|--------|--------------|
| Take 50% at +10% | 1.95x | 88.28% | 59 |
| Take 100% at +10% | 1.95x | 88.28% | 97 |
| Take 50% at +20% | 1.99x | 88.28% | 0 |

**Finding**: HURTS performance. Taking profits early means missing the full trend.

---

## Improvement 3: Crash Detection ⭐ BEST IMPROVEMENT

**Concept**: Exit positions when price velocity indicates crash

### NAS100 Results

| Velocity | Lookback | Exit% | Return | Max DD | Improvement |
|----------|----------|-------|--------|--------|-------------|
| -0.3%/500 | 500 | 50% | **1.30x** | **11.22%** | DD -77% |
| -0.4%/500 | 500 | 50% | 1.24x | 22.64% | DD -66% |
| -0.5%/500 | 500 | 50% | 1.31x | 20.53% | DD -68% |
| -0.5%/500 | 500 | 30% | 1.31x | 20.53% | DD -68% |
| -1.0%/1000 | 1000 | 50% | 1.01x | 56.55% | DD -32% |

**Best Config**: Velocity=-0.3%, Lookback=500, Exit=50%
- Return: 1.30x (vs 1.99x baseline = -35%)
- Max DD: 11.22% (vs 88.28% baseline = **-87%**)
- Risk-adjusted: Much better!

### GOLD Results

| Velocity | Lookback | Exit% | Return | Max DD |
|----------|----------|-------|--------|--------|
| Baseline | - | - | 34.01x | 74.70% |
| -0.3%/500 | 500 | 50% | 5.58x | 22.89% |
| -0.5%/500 | 500 | 50% | 7.54x | 39.72% |

**Finding for Gold**: Crash detection significantly reduces returns (34x → 5.6x) but also DD (75% → 23%). May not be worth it for Gold.

---

## Improvement 4: Volatility Sizing

**Concept**: Reduce position size when volatility is high

| Multiplier | Return | Max DD |
|------------|--------|--------|
| 0.5x | 1.99x | 88.29% |
| 1.0x | 1.99x | 88.29% |
| 2.0x | 1.99x | 88.31% |

**Finding**: No meaningful improvement. Survive_down sizing already accounts for risk.

---

## Improvement 5: Combined Approach

| Config | Return | Max DD |
|--------|--------|--------|
| All improvements | 0.99x | 56.56% |

**Finding**: Over-filtering kills returns. Too many conflicting signals.

---

## Summary Table

| Strategy | NAS100 Return | NAS100 DD | GOLD Return | GOLD DD |
|----------|---------------|-----------|-------------|---------|
| Baseline | 1.99x | 88% | 34.01x | 75% |
| Crash Detect (best) | **1.30x** | **11%** | 5.58x | 23% |
| Profit Taking | 1.95x | 88% | - | - |
| Regime Filter | 1.99x | 88% | - | - |
| Volatility Sizing | 1.99x | 88% | - | - |

---

## Recommendations

### For NAS100:
Use **Crash Detection** with:
```cpp
enable_crash_detection = true
crash_velocity_threshold = -0.3  // Exit when -0.3% in 500 ticks
crash_lookback = 500
crash_exit_pct = 0.5             // Exit 50% of positions
```
- Gives 1.30x return with only 11% max DD
- Much better risk-adjusted returns
- Suitable for real money trading

### For GOLD:
**Don't use crash detection** - the baseline returns are so good (34x) that it's worth tolerating 75% DD.

OR if you want lower risk:
- Use crash detection for 5.6x return with 23% DD
- Still excellent returns with manageable risk

---

## Key Insights

1. **Crash detection is the only improvement that works** - it trades some return for much lower DD

2. **Profit taking hurts performance** - the strategy works by riding trends, so taking profits early leaves money on the table

3. **Regime filter doesn't help** - strategy only enters on new ATH anyway

4. **Volatility sizing is redundant** - survive_down sizing already accounts for risk

5. **NAS100 needs protection, Gold doesn't** - Gold's stronger trend makes baseline more robust
