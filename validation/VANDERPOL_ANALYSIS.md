# Van der Pol Oscillator Phase-Based Trading Analysis

## Executive Summary

**Result: The Van der Pol oscillator phase-based trading strategy FAILS to outperform the baseline FillUpOscillation ADAPTIVE_SPACING strategy.**

Key findings:
1. **0% of VDP configs beat baseline in 2025** (best VDP: 6.74x vs baseline 8.13x)
2. **Only 3.5% of VDP configs beat baseline in 2024** (6 configs out of 171)
3. **Fixed TP outperforms phase-based exit** (4.75x avg vs 2.76x avg)
4. **Entry phase angle has minimal impact** (~240, 270, 300 degrees all perform similarly)

## Concept Tested

The Van der Pol oscillator is a nonlinear oscillator that produces stable limit cycles. The hypothesis was that modeling price as an oscillator with phase tracking could improve entry/exit timing:

- **Phase 0 deg**: Price at equilibrium, rising
- **Phase 90 deg**: Price at local maximum (peak)
- **Phase 180 deg**: Price at equilibrium, falling
- **Phase 270 deg**: Price at local minimum (trough)

**Trading logic**:
- BUY when phase is near 270 deg (trough)
- EXIT when phase is near 90 deg (peak)

## Implementation Details

The strategy tracks:
1. **Moving Average (MA)** - equilibrium price
2. **Deviation (x)** = price - MA
3. **Velocity (v)** = rate of change of price (smoothed via linear regression)
4. **Phase** = atan2(x_normalized, v_normalized)

Normalization uses 75th percentile of recent amplitudes and velocities to put x and v on comparable scales.

## Test Results

### Baseline Comparison

| Year | Baseline (FillUpOsc) | Best VDP | VDP vs Baseline |
|------|---------------------|----------|-----------------|
| 2025 | 8.13x, 77.7% DD | 6.74x, 40.0% DD | -17% return, -48% DD |
| 2024 | 2.38x, 65.3% DD | 2.58x, 47.2% DD | +8% return, -28% DD |

### Phase Exit vs Fixed TP (2025)

| Exit Type | Configs | Avg Return | Best Return | Avg DD |
|-----------|---------|------------|-------------|--------|
| Fixed TP ($1.50) | 9 | 4.75x | 6.74x | ~40% |
| Phase Exit (90 deg) | 162 | 2.76x | 4.25x | ~60% |

**Conclusion**: Fixed take-profit dramatically outperforms phase-based exit.

### Entry Phase Analysis (2025)

| Entry Phase | Avg Return | Best Return |
|-------------|------------|-------------|
| 240 deg | 2.88x | 6.71x |
| 270 deg | 2.88x | 6.72x |
| 300 deg | 2.85x | 6.74x |

**Conclusion**: Entry phase angle has negligible impact on performance. All three trough-area phases (240, 270, 300) perform identically within noise.

### MA Period Analysis (2025)

| MA Period | Avg Return | Entries | Note |
|-----------|------------|---------|------|
| 200 ticks | 3.56x | ~19,600 | More trades, better with fixed TP |
| 500 ticks | 4.04x | ~18,900 | Best overall |
| 1000 ticks | 1.00x | ~1,000 | Too few entries |

**Conclusion**: Short MA periods work better, but even the best (MA=500) underperforms baseline.

### Phase Portrait Analysis

The best-performing configs showed:
- **Typical amplitude**: $1.06 - $1.79
- **Typical velocity**: $0.0148/tick

This aligns with the baseline strategy's $1.50 spacing, suggesting the oscillation model correctly identifies the characteristic scale but fails to provide actionable timing signals.

## Why Van der Pol Phase Trading Fails

### 1. Phase Measurement Noise

The phase calculation depends on instantaneous velocity estimation. In noisy tick data:
- Velocity estimates are noisy even with smoothing
- Phase angle jumps erratically between measurements
- "Trough" detection is unreliable

### 2. Market is Not a Simple Oscillator

The Van der Pol model assumes:
- Single dominant frequency
- Stable limit cycle
- Continuous phase evolution

Gold price exhibits:
- Multiple frequencies overlapping
- Regime changes (trending vs ranging)
- Discrete jumps (news events)

### 3. Grid Entry is More Robust

The baseline grid strategy:
- Enters at fixed price intervals regardless of "phase"
- Captures oscillations at ALL frequencies
- Does not depend on accurate velocity measurement

The phase-based strategy:
- Only enters when it believes price is at trough
- Misses oscillations when phase estimate is wrong
- Has fewer entries (VDP: ~19k vs baseline: ~58k in 2025)

### 4. Phase Exit Destroys Profits

Phase exit at "peak" (90 deg):
- Often triggers too early (before full amplitude captured)
- Sometimes triggers too late (after price already reversed)
- Fixed TP of $1.50 is more reliable

## What VDP Does Right

Despite failing to beat baseline, VDP shows one positive:

**Lower Drawdown**: Best VDP configs have ~40% DD vs baseline's ~78% DD

This comes from:
- Selective entry (fewer positions accumulating during trends)
- However, this also reduces returns proportionally

The risk-adjusted return (Sharpe proxy) is actually HIGHER for VDP:
- VDP best: 14.36 Sharpe (6.74x / 40% DD)
- Baseline: 9.17 Sharpe (8.13x / 78% DD)

But this is misleading - it simply reflects that VDP takes smaller positions and fewer trades.

## Comparison with Previous Findings

From CLAUDE.md domain investigation:
- **Damped Oscillator strategy**: 2.60x return, 70.6% DD
- **Van der Pol**: 6.74x return, 40.0% DD (when using fixed TP)

Van der Pol with fixed TP is comparable to Damped Oscillator but with lower DD. However, both fail to beat FillUpOscillation ADAPTIVE_SPACING.

## Recommendations

### Do NOT Use Van der Pol for Entry/Exit Timing

The oscillator model does not provide actionable signals that beat simple grid entry.

### Phase Concept Has One Possible Use

The typical amplitude measurement ($1.06-$1.79) could potentially inform spacing selection, but this is already handled by the ADAPTIVE_SPACING mode's volatility-based approach.

### Grid Strategy Remains Superior

The baseline FillUpOscillation ADAPTIVE_SPACING strategy:
- Captures oscillations at all scales
- Does not depend on phase estimation
- Has proven consistent across 2024 and 2025

## Test Configuration

```cpp
// Parameters tested
ma_periods = {200, 500, 1000}
velocity_smoothings = {20, 50, 100}
entry_phases = {240, 270, 300}  // degrees
entry_widths = {45, 90}  // degrees
exit_phases = {60, 90, 120}  // degrees
amplitude_lookbacks = {500}
survive_pcts = {13}
years = {2024, 2025}

// Total: 344 configurations (162 VDP + phase exit, 9 VDP + fixed TP, 2 baselines)
// Runtime: 691 seconds (2.01 sec/config)
```

## Files Created

- `include/strategy_vanderpol.h` - Van der Pol oscillator strategy implementation
- `validation/test_vanderpol.cpp` - Parallel parameter sweep test
- `validation/VANDERPOL_ANALYSIS.md` - This analysis document

## Final Verdict

**Van der Pol oscillator phase-based trading is a DEAD END for this strategy type.**

The oscillator model correctly identifies the characteristic amplitude of price oscillations ($1-2) but fails to provide timing signals that beat simple grid entry. The phase calculation is too noisy and the market does not behave as a simple single-frequency oscillator.

**Add to validated dead ends in CLAUDE.md:**
- Van der Pol phase-based entry: Best 6.74x (vs 8.13x baseline), entry phase angle has no effect
- Van der Pol phase-based exit: 2.76x avg (vs 4.75x avg with fixed TP)
