# Archive: Parameter Selection Methodology

This file contains the detailed 6-step parameter selection methodology. For quick reference parameters, see the main CLAUDE.md.

---

## Overview: The 6-Step Process

```
1. MEASURE    → Oscillation characteristics + volatility ranges
2. SWEEP      → Parametric grid across survive/spacing/lookback
3. VALIDATE   → H1/H2 split (anti-overfitting)
4. STRESS     → Multi-year regime robustness
5. REFINE     → Spacing optimization for adaptive mode
6. CONFIRM    → Parameter stability neighborhood check
```

---

## Step 1: Measure Oscillation Characteristics

**Purpose:** Understand how the instrument moves — amplitude, frequency, asymmetry.

**Script:** `validation/test_domain1_oscillation_characterization.cpp`

**Method:**
1. Load full-year tick data into memory (51.7M ticks for XAUUSD 2025)
2. Detect swing highs/lows using minimum threshold ($1.00 for gold)
3. Record: amplitude, duration, direction for each oscillation
4. Compute distributions at multiple scales (0.05%, 0.15%, 0.50% of price)

**Key outputs:**
- Median oscillation amplitude (stable at ~$1.00 for XAUUSD)
- Oscillation count per year (281k in 2025, 92k in 2024)
- Duration distribution (91.4% resolve in <5 minutes)
- Downswing vs upswing speed asymmetry (down is 17-33% faster)

**Why this matters:** The median oscillation amplitude determines the "natural" grid spacing.

---

## Step 2: Measure Volatility for Adaptive Spacing

**Purpose:** Determine TypicalVolPct — the expected price range over the lookback window.

**Script:** `validation/test_xagusd_vol.cpp`

**Method:**
1. Stream through all ticks, tracking high/low over rolling windows
2. Reset window every N hours (test both 1h and 4h)
3. Record the range (high - low) at each reset
4. Compute: mean, median, P25, P75, P90 of ranges as % of price

**XAUUSD 2025 results:**

| Window | Mean | Median | P25 | P75 | P90 |
|--------|------|--------|-----|-----|-----|
| 1-hour | 0.37% | 0.27% | 0.18% | 0.44% | 0.68% |
| 4-hour | 0.72% | 0.55% | 0.37% | 0.87% | 1.32% |

**Parameter selection rule:** TypicalVolPct = **median range** for chosen lookback window.
- If lookback = 4h → TypicalVolPct = 0.55%
- If lookback = 1h → TypicalVolPct = 0.27%

---

## Step 3: Parameter Sweep

**Purpose:** Find optimal combination of survive_pct, base_spacing, lookback.

**Script:** `validation/test_parameter_stability.cpp`

**Sweep grid:**
```
survive_pct:   [10, 13, 15, 18, 20]        # 5 values
base_spacing:  [1.0, 1.5, 2.0, 2.5]        # 4 values
lookback:      [1, 2, 4, 8]                 # 4 values (hours)
─────────────────────────────────────────
Total:         80 configurations
```

**Infrastructure:** Parallel sweep pattern:
1. Load ticks once into shared memory (~60s)
2. 16 threads process 80 configs (~5s total)
3. Collect: return multiple, max DD, trade count, Sharpe ratio, swap cost

**Selection criteria** (priority order):
1. Must survive: No margin stop-out
2. Best risk-adjusted return: Sharpe ratio
3. Trade count > 5,000: Statistical significance
4. Reasonable DD: Prefer < 70% max DD

---

## Step 4: Out-of-Sample Validation (H1/H2 Split)

**Purpose:** Detect overfitting.

**Script:** `validation/test_h1h2_validation.cpp`

**Method:**
1. Run best config on H1 only (Jan-Jun)
2. Run best config on H2 only (Jul-Dec)
3. Calculate H1/H2 ratio of returns
4. Check both halves survive independently

**Pass criteria:**
- Both halves must end with balance > 10% of start
- H1/H2 return ratio in range [0.33, 3.0]
- Ideal ratio is 1.0

**XAUUSD results (survive=13, spacing=$1.50, lookback=4h):**
- H1: 2.54x return, ~66% DD → PASS
- H2: 2.65x return, ~67% DD → PASS
- Ratio: 0.96 → PASS (near-perfect consistency)

---

## Step 5: Multi-Year Regime Robustness

**Purpose:** Verify strategy isn't exploiting one year's conditions.

**Script:** `validation/test_multi_year.cpp`

**Method:**
1. Run on 2024 data (lower oscillation, ~92k swings)
2. Run on 2025 data (high oscillation, ~281k swings)
3. Run sequentially: Start with 2024, carry balance into 2025
4. Compare performance ratio to oscillation ratio

**Results:**
| Year | Oscillations | Return | Performance/Oscillation |
|------|-------------|--------|-------------------------|
| 2024 | 92,000 | 2.10x | 2.28e-5 |
| 2025 | 281,000 | 6.59x | 2.35e-5 |

Performance scales linearly with oscillation count (within 3%).

---

## Step 6: Parameter Stability (Neighborhood Check)

**Purpose:** Ensure selected parameters aren't a fragile local optimum.

**Method:**
1. Take selected config: survive=13, spacing=$1.50, lookback=4h
2. Check all configs that differ by ±1 parameter step
3. Measure return variance across neighborhood

**Neighborhood of (13, $1.50, 4h):**
- (13, $1.50, 2h): 6.32x → -3.8%
- (13, $1.50, 8h): 6.05x → -7.9%
- (13, $1.00, 4h): 7.12x → +8.4%
- (13, $2.00, 4h): 5.21x → -20.7%
- (15, $1.50, 4h): 5.81x → -11.6%

**Pass criterion:** No neighbor differs by >50%. All above within 21%.

---

## Step 7: Validate Against Dead Ends

**Purpose:** Confirm attempted improvements don't help.

**Tests performed (all FAILED):**
- Entry filters (MomentumReversal): Overfit, 0% H2 survival
- Exit timing (4h time exit): Kills positions, 1.32x
- DD reduction: -38% return per -14% DD
- Trailing stops (768 configs): None beat TP = spread + spacing
- Auto-calibration: -6% to -47% vs manual

---

## Translating C++ Parameters to EA v4

C++ uses absolute dollar values. EA v4 uses percentage of price.

```
BaseSpacingPct = (base_spacing_dollars / reference_price) × 100

For XAUUSD at ~$2,700:
  $1.50 / $2,700 × 100 = 0.055% → use 0.06%
```

The EA's percentage-based spacing automatically adapts to price level.

---

## Reproducing for a New Instrument

1. **Get tick data**: Full year minimum, ideally 2+ years
2. **Measure oscillations**: Find median amplitude → suggests base_spacing
3. **Measure volatility**: Find median 1h and 4h ranges as % of price
4. **Determine survive_pct**: Must exceed max observed adverse move
   - Measure: largest peak-to-trough move in dataset
   - Add safety margin: survive = max_adverse × 1.2 (minimum)
5. **Run sweep**: Grid around estimated values
6. **Validate**: H1/H2 split, multi-year if available, neighborhood check

**Known results:**
- XAGUSD: survive=19%, BaseSpacingPct=2.0%, lookback=1h, TypicalVolPct=0.45%
- USDJPY: Does NOT work (insufficient oscillation)
- NAS100: Needs recalibration

---

## Auto-Calibration: Tested and Rejected

**Results:**
| Config | Return | vs Manual |
|--------|--------|-----------|
| MANUAL_ALL (baseline) | 10.61x | — |
| AUTO_LOOKBACK+TYPVOL | 9.96x | -6% |
| AUTO_LOOKBACK alone | 5.79x | -45% |
| AUTO_ALL | 5.65x | -47% |

**Why it fails:** Auto-calibration uses early data, which doesn't represent the full year. Lookback and TypVol are mathematically coupled.

**Conclusion:** Use manual parameters via sweep methodology. 30 minutes to run the sweep beats any auto-tuning.

---

## XAGUSD Percentage-Based Spacing Details

### Problem
Silver tripled from $28.91 to $96.17 in 2025. With fixed $0.75 spacing:
- At $30: spacing = 2.5% of price (reasonable)
- At $96: spacing = 0.78% of price (too tight → overleveraged)

### Solution
All spacing parameters as % of price:
```cpp
adaptive_cfg.pct_spacing = true;
// base_spacing = 2.0 means 2.0% of current bid
```

### Measured XAGUSD Volatility

| Timeframe | Average | Median | P25 | P75 | P90 |
|-----------|---------|--------|-----|-----|-----|
| 1-Hour | 0.595% | 0.455% | 0.299% | 0.724% | 1.131% |
| 4-Hour | 1.227% | 0.970% | 0.649% | 1.518% | 2.267% |

TypicalVolPct for 1h lookback = 0.45%

### Optimal XAGUSD Parameters

| Parameter | Value |
|-----------|-------|
| SurvivePct | 19% |
| BaseSpacingPct | 2.0% |
| VolatilityLookbackHours | 1.0 |
| TypicalVolPct | 0.45% |
| MinSpacingPct | 0.05% |
| MaxSpacingPct | 15.0% |

### Sweep Results (top)

| Config | Return | MaxDD |
|--------|--------|-------|
| s18_2.0%_lb1 | 115.2x | 36.9% |
| s19_2.0%_lb1 | 43.4x | 29.3% |
| s20_2.0%_lb1 | 34.1x | 28.7% |

### Key Findings

1. 2.0% spacing is optimal — lowest DD (28-37%)
2. 1h lookback beats 4h — silver oscillates faster
3. survive=18-20 all work — below 18 stops out
4. 3.5%+ spacing with lb4 stops out — too wide
