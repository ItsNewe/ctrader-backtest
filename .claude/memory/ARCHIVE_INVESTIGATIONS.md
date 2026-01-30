# Archive: Detailed Investigation Notes

This file contains detailed investigation logs from strategy research. For quick reference, see the main CLAUDE.md.

---

## Market-Parasitic Strategy Investigation (2026-01-23)

Seven physics-inspired approaches tested:

| Strategy | Instrument | Return | Max DD | Verdict |
|----------|-----------|--------|--------|---------|
| Shannon's Demon | XAUUSD | 0.90x | 12.4% | FAIL - Rebalancing costs > benefits |
| Shannon's Demon | XAGUSD | 0.00x | 99.98% | FAIL - Ping-pong bug |
| Gamma Scalping | XAUUSD | 1.48x | 26.0% | Best risk-adjusted (low DD) |
| Stochastic Resonance | XAUUSD | 0.37x | 87.7% | FAIL - Too selective |
| Stochastic Resonance | USDJPY | 0.06x | 96.1% | FAIL - Wrong instrument |
| Entropy Harvester | XAUUSD | 11.86x | 86.7% | FAIL - Selectivity hurts |
| Liquidity Premium | XAUUSD | 1.16x | 21.1% | OK - Low returns |
| Damped Oscillator | XAUUSD | 2.60x | 70.6% | PARTIAL - Timing insight |
| Asymmetric Vol | XAUUSD | 1.92x | 46.6% | OK - Velocity scaling |

**Key Conclusions:**
1. All top performers are grid-on-oscillation variants
2. Physics concepts don't add value over simple grid
3. Velocity-confirmed entries (Damped Oscillator) is useful insight
4. XAGUSD requires percentage-based spacing

---

## DD Reduction Investigation (2026-01-23)

135 configs tested with parallel sweep (132s total).

**Mechanisms Tested:**
1. DD-based entry pause
2. Max concurrent positions cap
3. Equity hard stop
4. Combined approaches

**Key Result: NO configuration reduces DD >10% with <25% return loss**

| Config | Return | MaxDD | DD Saved | Return Loss |
|--------|--------|-------|----------|-------------|
| BASELINE | 6.70x | 67.5% | - | - |
| MAXPOS_100 | 6.61x | 65.8% | 1.7% | 1.3% |
| PAUSE_40_RES_36 | 6.35x | 63.9% | 3.6% | 5.2% |
| PAUSE_35_RES_10 | 4.18x | 53.6% | 13.9% | 37.6% |

**Fundamental Trade-off:** DD reduction and return are mechanistically coupled - positions accumulated during DD ARE the recovery mechanism.

---

## Spacing & Volatility Deep Dive

### Optimal Spacing by Trend

| Trend Strength | Best Spacing | Example |
|----------------|-------------|---------|
| Strong (>10%) | $0.20-$0.50 | Q1, Q3, Q4 2025 |
| Weak (<6%) | $1.00-$5.00 | Q2 2025 |

### Trade Economics

| Spacing | Trades/Year | Profit/Trade | Annual Return |
|---------|------------|--------------|---------------|
| $0.20 | 244,223 | $0.31 | High (trend) |
| $1.50 | ~10,334 | ~$5.50 | 6.57x (adaptive) |
| $5.00 | 8,075 | $6.46 | Best (weak trend) |

---

## FIXED vs ADAPTIVE Spacing Regime Robustness (2024 + 2025)

Test file: `validation/test_regime_parallel.cpp`

| Strategy | 2024 | 2025 | Ratio | 2-Year | Status |
|----------|------|------|-------|--------|--------|
| **ADAPTIVE** | 4.13x | 14.60x | 3.53x | **60.4x** | BEST |
| FIX_0.06% | 3.99x | 11.55x | 2.89x | 46.1x | Best FIXED |
| FIX_$2.0 | 2.93x | 9.65x | 3.29x | 28.3x | OK |
| FIX_$1.5 | 3.54x | 0.02x | - | CRASH | TOO TIGHT |

**Key Findings:**
- ADAPTIVE wins by 24%
- Percentage-based FIXED beats absolute FIXED by 63%
- Tight spacing crashes in 2025 due to price rise

---

## Oscillation Characteristics (XAUUSD 2025)

| Scale | Count/Year | Amplitude | Duration |
|-------|------------|-----------|----------|
| Small (0.05%) | 105,708 | $3.79 | 4.9 min |
| Medium (0.15%) | 13,721 | $10.93 | 38.1 min |
| Large (0.50%) | 1,280 | $36.55 | 408 min |

**Asymmetry:** Downswings are 17-33% faster than upswings.

---

## Session Patterns

| Session | Swings/hr | Speed | Spacing Mult |
|---------|-----------|-------|-------------|
| NY (14:00-18:00 UTC) | 7,966-10,300 | 1.5-1.9 min | 0.8x |
| London (07:00-14:00 UTC) | ~3,000 | ~4 min | 1.0x |
| Asian (22:00-07:00 UTC) | 1,603-2,731 | 5.8-7.0 min | 1.3x |

---

## Price vs Oscillation Relationship

### Why 2025 Outperforms 2024

| Factor | 2024 | 2025 | Impact |
|--------|------|------|--------|
| Gold price | ~$2,300 | ~$3,500 (+53%) | ~1.5x more dollar oscillations |
| Oscillations/year | ~92,000 | ~281,000 (3.1x) | 3x more opportunities |
| Combined effect | 2.10x return | 6.59x return | 3.14x ratio |

---

## Percentage-Based Spacing & Regime Independence (2026-01-25)

### Sweet Spot Analysis (1020 configs)

| Config | 2025 | 2024 | Ratio | DD |
|--------|------|------|-------|-----|
| s12_sp0.085%_lb2.0_tv1.20 | 5.35x | 3.16x | 1.69x | 91.9% |
| s13_sp0.085%_lb2.0_tv1.20 | 4.71x | 2.75x | 1.71x | 85.0% |

**Key Discovery:** Regime independence comes from spacing + typvol combination:
- High typvol (0.80-1.20%) + spacing (0.06-0.10%) = 1.7-2.1x regime ratio
- Low typvol (0.20-0.35%) = 4-6x regime ratio (high dependence)

---

## Chaos Theory Investigations (2026-01-26)

8 concepts tested:

| # | Concept | Result | Best Return | Verdict |
|---|---------|--------|-------------|---------|
| 1 | Floating Attractor Grid | SUCCESS | 11.05x | +36% |
| 2 | Synchronization (Pairs) | FAIL | 0.92x | Both trend together |
| 3 | Edge of Chaos | SUCCESS | 15.98x | Phase transition at 12% |
| 4 | OGY Control | PARTIAL | 1.91x | Timing validated |
| 5 | Bifurcation Detection | FAIL | 8.14x | Signals too late |
| 6 | Entropy Export | FAIL | 1.45x | Cutting losers hurts |
| 7 | Fractal Multi-Scale | FAIL | 10.71x | No diversification |
| 8 | Noise Scaling | FAIL | 7.95x | No signal |

**Key Insights:**
1. Grid's profit mechanism IS its DD mechanism
2. Phase transition at survive=12%
3. Velocity zero-crossing improves entry timing
4. Floating attractor improves both return AND risk

---

## Dynamic Models Investigation (2026-01-27)

5 models tested:

| Model | Result | Best Return | vs Baseline |
|-------|--------|-------------|-------------|
| Ornstein-Uhlenbeck | FAIL | 7.73x | -5% |
| Kalman Filter | FAIL | 18.67x | -6% |
| Regime-Switching (HMM) | FAIL | ~8x | ~0% |
| Adaptive Control | PARTIAL | 16.46x | +6% |
| Van der Pol Oscillator | FAIL | 6.74x | -17% |

**Conclusion:** Simple heuristics beat mathematical models. The market doesn't fit clean models.

---

## Ju Philosophy Investigations (2026-01-27)

5 concepts tested:

| Concept | Outcome | Best Return | DD |
|---------|---------|-------------|-----|
| Barbell Sizing | Trade-off | +30% | +10% |
| **Rubber Band TP** | **SUCCESS** | **+65%** | **-14%** |
| Via Negativa | FAIL | - | - |
| Reflexivity | FAIL | 0% | 0% |
| **Wu Wei (Velocity)** | **SUCCESS** | **+19%** | **-4%** |

**Actionable Findings:**
1. Rubber Band TP with FIRST_ENTRY equilibrium (SQRT scaling)
2. Velocity Zero Filter (enter at local minima)
3. Combined: 21.99x return (2025), 126x 2-year

---

## Forced Entry Crash Investigation (2026-01-28)

### The Problem
force_min_volume_entry=true causes 100% stop-out when starting before crashes.

### Crash Scenario Results (Oct 17 → Dec 29, 2025)

| Config | Return | Max DD | Status |
|--------|--------|--------|--------|
| s12_noforce | 1.37x | 96.6% | SURVIVED |
| s12_FORCE | 0.02x | 98.8% | STOP-OUT |

**Pattern:** ALL force=ON stop out. ALL force=OFF survive.

### The Trade-off
```
force=ON:   Higher return (20.04x avg)  BUT  100% crash failure
force=OFF:  Lower return (11.14x avg)   AND  100% crash survival
```

**Profit sacrifice with force=OFF: 44.4%** — this is survival insurance.

---

## Test Files Reference

### Investigation Tests

| Test | File | Purpose |
|------|------|---------|
| 7 strategies | test_7_strategies.cpp | Market-parasitic comparison |
| DD reduction | test_dd_reduction.cpp | 135-config parallel sweep |
| Trailing stop | test_trailing_5d_parallel.cpp | 768-config sweep |
| Sweet spot | test_sweetspot_sweep.cpp | 1020-config regime sweep |
| Floating attractor | test_floating_attractor.cpp | EMA-based grid |
| Edge of chaos | test_edge_of_chaos.cpp | Phase transition |
| Combined Ju | test_combined_ju_sweep.cpp | 164-config parameter sweep |
| Forced entry | test_force_entry_mitigation.cpp | Crash survival |

### Domain Investigation Tests

| Domain | File | Finding |
|--------|------|---------|
| 1. Oscillation | test_domain1_oscillation_characterization.cpp | 281k/year (2025) |
| 2. Regime | test_domain2_regime_detection.cpp | 7 TRENDING_DOWN episodes |
| 3. Time patterns | test_domain3_time_patterns.cpp | NY session peak |
| 4. Position lifecycle | test_domain4_position_lifecycle.cpp | 91% close <5 min |
| 5. Entry optimization | test_domain5_entry_optimization.cpp | MomentumRev OVERFIT |
| 6. Exit refinement | test_domain6_exit_refinement.cpp | 4h exit hurts |
| 7. Risk of ruin | test_domain7_risk_of_ruin.cpp | 0.10% ruin probability |
