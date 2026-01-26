# Strategy Backtesting Framework

## Quality Standards

```
Thorough but concise. Detailed but readable. Technical but accessible.
Comprehensive but focused. Complete but not padded.
Precise and correct. Evidence-based. Assumptions stated.
```

---

## 1. Architecture & Build Guide

### Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    TickBasedEngine                          │
│  (include/tick_based_engine.h)                              │
│  - Swap calculation (daily, triple on Wed/Thu)              │
│  - Margin stop-out (20% level)                              │
│  - Date filtering (MT5-style inclusive/exclusive)           │
│  - Position tracking & P/L calculation                      │
└─────────────────────────────────────────────────────────────┘
                           ↑
                    Strategy Callback (OnTick)
                           ↑
┌─────────────────────────────────────────────────────────────┐
│                     Strategy Class                          │
│  - Entry/exit logic                                         │
│  - Position sizing                                          │
│  - DD protection                                            │
└─────────────────────────────────────────────────────────────┘
```

### Key Principle: Use TickBasedEngine

**NEVER** create standalone strategy classes with their own margin/swap calculations. Always use `TickBasedEngine` which handles swap fees, margin stop-out, bid/ask spread, and date filtering.

### Core Files (DO NOT MODIFY unless fixing bugs)

```
include/tick_based_engine.h      # Main engine - swap, margin, positions
include/tick_data.h              # Tick structure and config
include/tick_data_manager.h      # Streaming tick data reader
include/position_validator.h     # Position validation
include/currency_converter.h     # Currency conversion
include/currency_rate_manager.h  # Rate management
```

### Strategy Files

```
include/fill_up_strategy.h             # Basic Fill-Up grid strategy
include/fill_up_oscillation.h          # FillUpOscillation - PRIMARY STRATEGY
include/fill_up_kapitza.h              # Advanced oscillation control (Kapitza)
include/strategy_fillup_hedged.h       # Hedged version (standalone - AVOID)
include/strategy_gamma_scalper.h       # Gamma scalping (buy dips, sell rallies)
include/strategy_shannons_demon.h      # Shannon's Demon (volatility pumping)
include/strategy_stochastic_resonance.h # Stochastic resonance
include/strategy_entropy_harvester.h   # Entropy harvesting (selective grid)
include/strategy_liquidity_premium.h   # Liquidity premium capture
include/strategy_damped_oscillator.h   # Damped oscillator (velocity entries)
include/strategy_asymmetric_vol.h      # Asymmetric volatility harvesting
```

### Test Template

```cpp
#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>

using namespace backtest;

int main() {
    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = 10000.0;
    config.account_currency = "USD";
    config.contract_size = 100.0;
    config.leverage = 500.0;
    config.margin_rate = 1.0;
    config.pip_size = 0.01;
    config.swap_long = -66.99;
    config.swap_short = 41.2;
    config.swap_mode = 1;           // SYMBOL_SWAP_MODE_POINTS
    config.swap_3days = 3;          // Triple swap Thursday
    config.start_date = "2025.01.01";
    config.end_date = "2025.12.30";
    config.tick_data_config = tick_config;

    try {
        TickBasedEngine engine(config);
        FillUpOscillation strategy(13.0, 1.5, 0.01, 10.0, 100.0, 500.0,
            FillUpOscillation::ADAPTIVE_SPACING, 0.1, 30.0, 4.0);

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& engine) {
            strategy.OnTick(tick, engine);
        });

        auto results = engine.GetResults();
        std::cout << "Final: $" << results.final_balance
                  << " | Swap: $" << results.total_swap_charged << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
```

### CMakeLists.txt Entry

```cmake
add_executable(test_my_strategy test_my_strategy.cpp)
target_include_directories(test_my_strategy PRIVATE ../include)
target_compile_features(test_my_strategy PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_my_strategy PRIVATE /W4 /O2)
else()
    target_compile_options(test_my_strategy PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_my_strategy PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_my_strategy PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
```

### Build & Run

```bash
cd build
cmake .. -G "MinGW Makefiles"
mingw32-make test_my_strategy
./validation/test_my_strategy.exe
```

### Common Pitfalls

1. **Standalone strategy classes** - Never implement your own swap/margin logic
2. **Missing swap fees** - $3,000-$7,000/year for XAUUSD. Always verify `results.total_swap_charged`
3. **Wrong stop-out** - MT5 uses 20% margin level (engine handles automatically)
4. **Static linking** - Required for MinGW on Windows
5. **Relative paths** - Always use absolute paths for tick data files

### Swap Calculation Reference

- Charged at **market open** (first tick of new trading day)
- Triple swap on **Thursday** (for Wednesday rollover) when `swap_3days=3`
- Formula (swap_mode=1, points): `swap = swap_long * 0.01 * contract_size * lot_size * multiplier`
- XAUUSD example: `-66.99 * 0.01 * 100 * lot_size = -$66.99/lot/day`
- Expected annual cost at 0.15-0.20 lots average: **$3,000-$5,000/year**

---

## 2. Instrument & Broker Reference

### Tick Data Available

```
validation/Grid/XAUUSD_TICKS_2025.csv    # ~51.7M ticks, 2024.12.31-2025.12.29
validation/Grid/XAUUSD_TICKS_2024.csv    # Full year 2024
mt5/fill_up_xagusd/XAGUSD_TESTER_TICKS.csv  # ~29.3M ticks, 2025.01-2026.01, $28.91-$96.17
validation/USDJPY/USDJPY_TICKS_2025.csv    # ~37.9M ticks, price 139.88-158.87
validation/NAS100/NAS100_TICKS_2025.csv    # NAS100 2025
```

### Broker Settings

| Setting | Grid (XAUUSD) | Broker (XAUUSD) | XAGUSD | USDJPY |
|---------|-----------------|-----------------|--------|--------|
| contract_size | 100.0 | 100.0 | 5000.0 | 100000.0 |
| leverage | 500.0 | 500.0 | 500.0 | 500.0 |
| swap_long | -66.99 | -65.11 | -15.0 | +7.29 |
| swap_short | +41.2 | +33.20 | +13.72 | -21.87 |
| swap_mode | 1 (points) | 1 (points) | 1 | 1 |
| pip_size | 0.01 | 0.01 | 0.001 | 0.001 |

### Instrument Characteristics (2025)

| Instrument | Avg Price | Daily Range | Hourly Range (%) | Trend | Suitability |
|------------|-----------|-------------|------------------|-------|-------------|
| XAUUSD | $3,518 | $59.34 | 0.27% | +65.2% | EXCELLENT |
| XAGUSD | $28-$96 | ~2% | 0.45% | +232% | EXCELLENT (with pct spacing) |
| USDJPY | ~$149 | ~$1.20 | ~0.20% | -5% | POOR |

### XAGUSD: Requires Percentage-Based Spacing

- **Contract size 5000** amplifies all moves (vs 100 for gold)
- **Price tripled** in 2025 ($28.91 → $96.17) — absolute spacing becomes invalid
- **1.7-1.8x more volatile** than gold in percentage terms
- **Solution**: Use percentage-based spacing (v4 EA) — all params scale with price
- **Result**: 43.4x return, 29.3% DD with `BaseSpacingPct=2.0%, SurvivePct=19`
- **Swap**: -15 points ≈ -$75/lot/day (lower than gold in $ terms)

---

## 3. Strategy Reference

### Primary Strategy: FillUpOscillation (ADAPTIVE_SPACING)

**Validated for deployment.** Passes H1/H2 out-of-sample testing, parameter stability analysis, and multi-year backtesting.

```cpp
FillUpOscillation strategy(
    13.0,   // survive_pct - CRITICAL: must exceed max adverse move (~11-12%)
    1.5,    // base_spacing - balanced return/risk
    0.01,   // min_volume
    10.0,   // max_volume
    100.0,  // contract_size
    500.0,  // leverage
    FillUpOscillation::ADAPTIVE_SPACING,  // mode - VALIDATED
    0.1,    // antifragile_scale (unused)
    30.0,   // velocity_threshold (unused)
    4.0     // volatility_lookback_hours
);
```

### Parameter Selection Guide

| Parameter | Conservative | Balanced | Aggressive |
|-----------|--------------|----------|------------|
| survive_pct | 18-20% | **13-15%** | 10% (FAILS) |
| base_spacing | $2.00-$2.50 | **$1.50** | $1.00 |
| lookback | 8h | **4h** | 1h |
| Expected Return | 2.8x-3.3x | **6.2x-6.6x** | 7.0x-7.5x |
| Max DD | ~55% | **~67%** | ~77% |

### Performance Summary (2025, $10k initial)

| Metric | Value |
|--------|-------|
| Total Return | 6.57x ($65,700) |
| Max Drawdown | 66.89% |
| Sharpe Ratio | 11.39 |
| Sortino Ratio | 11.76 |
| Win Rate | 100% (individual trades) |
| Total Trades | ~10,334 |
| Annual Swap Cost | -$6,795 |
| Avg Trade Duration | <5 min (91.4%) |

### H1/H2 Out-of-Sample Validation

| Period | Return | Max DD | Ratio |
|--------|--------|--------|-------|
| H1 (Jan-Jun) | 2.54x | ~66% | - |
| H2 (Jul-Dec) | 2.65x | ~67% | - |
| H1/H2 Ratio | - | - | **0.96 (consistent)** |

### Multi-Year Performance

| Year | Return | Max DD | Oscillations |
|------|--------|--------|--------------|
| 2024 | 2.10x | 43.12% | ~92,000 |
| 2025 | 6.59x | 66.85% | ~281,000 |
| Sequential | **12.83x** | - | $10k → $128k |

**Caution**: 2025 had 3x more oscillations than 2024 (ATH-proximity effect). Returns vary with market regime.

### Alternative: FillUpKapitza (Conservative Mode)

For capital preservation with much lower drawdown:

| Mode | Return | Max DD | Sharpe |
|------|--------|--------|--------|
| Kapitza Conservative (Trend0.9) | 1.59x | 7% | 8.46 |
| Kapitza Aggressive (MaxReturn) | 7.17x | 28% | 22.33 |
| FillUpOscillation ADAPTIVE | 6.57x | 67% | 11.39 |

The aggressive Kapitza config with resonance disabled is equivalent to FillUpOscillation but measured slightly different DD due to different lookback periods used in tests.

### Available Modes in FillUpOscillation

| Mode | Description | Return | Status |
|------|-------------|--------|--------|
| BASELINE | Fixed spacing | ~4.5x | Reference only |
| **ADAPTIVE_SPACING** | Vol-based spacing | **6.57x** | **RECOMMENDED (XAUUSD)** |
| **ADAPTIVE_SPACING + pct_spacing** | %-based spacing | **43.4x** | **RECOMMENDED (XAGUSD)** |
| ANTIFRAGILE | Size increases with DD | ~5.0x | Higher risk |
| VELOCITY_FILTER | Pause on fast drops | ~5.5x | Helps flash crashes |
| ADAPTIVE_LOOKBACK | Dynamic lookback window | ~6.0x | Marginal benefit |
| DOUBLE_ADAPTIVE | Spacing + lookback | ~6.2x | Complex |
| TREND_ADAPTIVE | Cumulative trend spacing | 8.13x | Bull-market bias |

**Note on TREND_ADAPTIVE**: The 8.13x return uses cumulative (YTD) trend, which effectively detects bull markets and keeps tight spacing. It will underperform in bear/sideways markets. Not recommended for production without bear market testing.

---

## 4. Key Research Findings

### Spacing Optimization

| Finding | Detail |
|---------|--------|
| Median oscillation | Stable at ~$1.00 regardless of daily range |
| Trend determines optimal spacing | Strong trend → tight; Weak trend → wide |
| Best fixed spacing (2025) | $0.30 (8.80x) - but bull-market specific |
| Adaptive spacing advantage | Auto-adjusts to volatility, no regime dependency |
| Typical volatility | Now uses 0.5% of price (auto-scales with gold price) |

### Oscillation Characteristics (XAUUSD 2025)

| Scale | Count/Year | Amplitude | Duration | Implied Spacing |
|-------|------------|-----------|----------|-----------------|
| Small (0.05%) | 105,708 | $3.79 | 4.9 min | $1.90 |
| Medium (0.15%) | 13,721 | $10.93 | 38.1 min | $5.50 |
| Large (0.50%) | 1,280 | $36.55 | 408 min | $18.30 |

**Key asymmetry**: Downswings are 17-33% faster than upswings.

### Session Patterns

| Session | Swings/hr | Speed | Spacing Mult |
|---------|-----------|-------|-------------|
| NY (14:00-18:00 UTC) | 7,966-10,300 | 1.5-1.9 min | 0.8x |
| London (07:00-14:00 UTC) | ~3,000 | ~4 min | 1.0x |
| Asian (22:00-07:00 UTC) | 1,603-2,731 | 5.8-7.0 min | 1.3x |

### Regime Detection

| Regime | Frequency (2025) | Duration | Strategy Response |
|--------|-------------------|----------|-------------------|
| OSCILLATING | 988 episodes | <1h | Full trading |
| TRENDING_UP | 345 episodes | <1h | Normal |
| TRENDING_DOWN | 7 episodes | <1h | **PAUSE** |
| HIGH_VOL | 691 episodes | ~6 min | Reduce size 50% |

**Emergency shutdown triggers**:
- 5+ consecutive hourly down moves
- 1-hour directional move > -3%
- Margin level < 50%

### Risk Analysis

| Metric | Value |
|--------|-------|
| Risk of ruin (Monte Carlo, 1000 iterations) | **0.10%** |
| survive_pct cliff | 10% fails, 13%+ survives |
| Max adverse move (2025) | ~11-12% |
| Strategy survives stress scenarios | 0/4 synthetic crashes |

**Stress test failure modes**: Sustained crash (-30%), V-recovery (margin call during dip), choppy decline (-1%/day). All cause margin call with survive=13%. The strategy relies on survive_pct exceeding the maximum adverse move, not on surviving arbitrary crashes.

---

## 5. Market-Parasitic Strategy Investigation (2026-01-23)

### Motivation

Explored strategies that "rely on the market to make them work" rather than trying to predict market direction. Seven physics-inspired approaches were implemented and tested.

### Test File: `validation/test_7_strategies.cpp`

### Strategy Files Created

| Strategy | File | Concept |
|----------|------|---------|
| Shannon's Demon | `include/strategy_shannons_demon.h` | Maintain target exposure, profit from rebalancing |
| Gamma Scalping | `include/strategy_gamma_scalper.h` | Buy dips, sell rallies, profit from (deltaS)^2 |
| Stochastic Resonance | `include/strategy_stochastic_resonance.h` | Trade only when noise is in "resonant band" |
| Entropy Harvester | `include/strategy_entropy_harvester.h` | Selective grid entry (Maxwell's Demon) |
| Liquidity Premium | `include/strategy_liquidity_premium.h` | Grid as passive liquidity provider |
| Damped Oscillator | `include/strategy_damped_oscillator.h` | Enter at velocity zero-crossings |
| Asymmetric Volatility | `include/strategy_asymmetric_vol.h` | Scale position with downside velocity |

### Results Summary

| Strategy | Instrument | Return | Max DD | Trades | Swap |
|----------|-----------|--------|--------|--------|------|
| Shannon's Demon | XAUUSD | 0.90x | 12.4% | 3 | -$1,046 |
| Shannon's Demon | XAGUSD* | 0.00x | 99.98% | 2.7M | -$11,575 |
| **Gamma Scalping** | **XAUUSD*** | **1.48x** | **26.0%** | 6,938 | -$1,685 |
| Stochastic Resonance | XAUUSD | 0.37x | 87.7% | 20,061 | -$164 |
| Stochastic Resonance | USDJPY* | 0.06x | 96.1% | 87 | -$5,000 |
| **Entropy Harvester** | **XAUUSD*** | **11.86x** | **86.7%** | 1,555 | -$19,253 |
| Liquidity Premium | XAUUSD | 1.16x | 21.1% | 18,688 | -$899 |
| **Damped Oscillator** | **XAUUSD*** | **2.60x** | **70.6%** | 8,440 | -$3,210 |
| Asymmetric Vol | XAUUSD | 1.92x | 46.6% | 94,543 | -$4,936 |

Baseline: FillUpOscillation ADAPTIVE_SPACING = **6.57x, 67% DD**

### Key Conclusions

1. **Entropy Harvester (11.86x) is FillUp with different params** - Uses $1.50 spacing/$1.00 TP grid. Its "Maxwell's Demon" selectivity actually HURTS (control without selectivity: 12.02x).

2. **Gamma Scalping has best risk-adjusted profile** - 1.48x at only 26% DD. Sacrifices return for dramatically lower drawdown.

3. **Damped Oscillator validates velocity-confirmed entries** - Enter at velocity zero-crossings (local minima). 2.60x return from timing entries better.

4. **Physics concepts don't add value** - All top performers are grid-on-oscillation variants. Shannon's Demon, stochastic resonance, and entropy harvesting fail to outperform simple grid strategies.

5. **XAGUSD requires percentage-based spacing** - Absolute $ spacing fails on silver because price tripled ($29→$96). With pct_spacing mode: 43.4x return, 29.3% DD.

6. **USDJPY doesn't oscillate enough** - Despite positive swap, too few trading opportunities.

### Three Actionable Insights

1. **Velocity-confirmed entries** (Damped Oscillator) - Enter when velocity crosses from negative to positive (local minimum detected)
2. **Velocity-scaled lot sizing** (Asymmetric Vol) - Larger positions on fast drops, smaller on slow moves
3. **Conservative grid sizing** (Gamma Scalping) - Accept 1.5x return for <30% DD

### Technical Challenges Solved

- **Shannon's Demon**: Initial implementation used margin as "position value" - too small at 500:1 leverage. Rewritten to track notional exposure percentage.
- **Stochastic Resonance**: O(n log n) barrier calculation (sorting 384k elements every 1000 ticks) caused hang. Fixed with 500-sample approximation and incremental O(1) noise tracking.
- **Shannon's Demon XAGUSD**: Ping-pong bug where sell/rebuy cycle repeated every tick due to swap-driven equity decline.

---

## 6. DD Reduction Investigation (2026-01-23)

### Test File: `validation/test_dd_reduction.cpp`

Uses the **C++ parallel sweep pattern**: loads 51.7M ticks into shared memory ONCE, then tests 135 DD-reduction configurations simultaneously across all CPU threads (16 threads, 132s total).

### Mechanisms Tested

1. **DD-based entry pause** - Stop new entries when DD exceeds threshold, resume at lower level
2. **Max concurrent positions cap** - Limit how many positions can be open
3. **Equity hard stop** - Close all positions when DD exceeds threshold
4. **Combined approaches** - Multiple mechanisms together

### Key Result: NO configuration reduces DD >10% with <25% return loss

| Config | Return | MaxDD | DD Saved | Return Loss | Sharpe |
|--------|--------|-------|----------|-------------|--------|
| **BASELINE** | **6.70x** | **67.5%** | - | - | **8.44** |
| MAXPOS_100 | 6.61x | 65.8% | 1.7% | 1.3% | **8.53** |
| PAUSE_50_RES_45 | 6.62x | 66.6% | 0.9% | 1.2% | 8.43 |
| PAUSE_40_RES_36 | 6.35x | 63.9% | 3.6% | 5.2% | 8.37 |
| PAUSE_35_RES_10 | 4.18x | 53.6% | 13.9% | 37.6% | 5.93 |
| PAUSE_30_RES_9 | 4.07x | 54.6% | 12.9% | 39.3% | 5.63 |
| PAUSE_25_RES_12 | 4.00x | 55.8% | 11.7% | 40.3% | 5.37 |

### The Fundamental Trade-off

DD reduction and return are **mechanistically coupled** in this strategy:
- The strategy profits by accumulating positions during price drops
- Those positions ARE the drawdown while they're being accumulated
- Preventing accumulation during DD means no recovery mechanism exists
- Therefore: any DD reduction directly reduces the profit engine

### Practical Options

| Goal | Approach | Expected |
|------|----------|----------|
| Minimal intervention | MAXPOS_100 | 6.61x, 65.8% DD (best Sharpe: 8.53) |
| Moderate DD cap | PAUSE_40_RES_36 | 6.35x, 63.9% DD |
| Significant DD reduction | PAUSE_35_RES_10 | 4.18x, 53.6% DD |
| Maximum safety | PAUSE_25_RES_12 | 4.00x, 55.8% DD |

### Conclusion

**DD reduction is not free** - every 1% of DD reduction costs approximately 3-4% of return. The strategy's DD IS its profit mechanism. For traders who cannot tolerate 67% DD, the options are:
1. Reduce initial capital allocation (use 50% of account) - same strategy, halved DD exposure
2. Use FillUpKapitza conservative mode (1.6x return, 7% DD)
3. Accept the DD trade-off with PAUSE_35 (4.2x return, 54% DD)

---

## 6b. Parallel Testing Infrastructure

### Pattern: Shared Tick Data + Thread Pool (C++)

For parameter sweeps, use the C++ parallel pattern (NOT Python subprocess):

```cpp
// 1. Load tick data ONCE into shared memory
std::vector<Tick> g_shared_ticks;
LoadTickDataOnce(path);  // ~60s for 52M ticks

// 2. Create work queue
WorkQueue queue;
for (const auto& task : tasks) queue.push(task);

// 3. Launch workers (all share same tick data)
unsigned int num_threads = std::thread::hardware_concurrency();
std::vector<std::thread> threads;
for (unsigned int i = 0; i < num_threads; i++) {
    threads.emplace_back(worker, std::ref(queue), total, std::cref(g_shared_ticks));
}

// 4. Each worker uses RunWithTicks (no file I/O per config)
engine.RunWithTicks(ticks, [&strategy](const Tick& t, TickBasedEngine& e) {
    strategy.OnTick(t, e);
});
```

### Performance

| Metric | Value |
|--------|-------|
| Data load | ~60s (1.5GB, 52M ticks) |
| Memory | ~3.5GB shared |
| Per-config runtime | ~1s (on shared data) |
| Parallelism | Hardware threads (16 on this machine) |
| 135 configs | 132s total (0.98s/config) |
| vs Sequential | ~16x speedup |

### Reference Files

| File | Purpose |
|------|---------|
| `validation/test_dd_reduction.cpp` | DD reduction sweep (135 configs) |
| `validation/test_trailing_5d_parallel.cpp` | Trailing stop sweep (768 configs) |
| `backtest_sweep.py` | Python subprocess approach (alternative) |
| `scripts/sweep_dd_reduction.py` | Python sweep (not used, subprocess-based) |

### When to Use Each Approach

| Approach | Use When |
|----------|----------|
| **C++ parallel (shared ticks)** | Parameter sweeps on same instrument/period |
| Python subprocess | Different instruments or periods per config |
| Single-threaded C++ | One-off tests, debugging |

---

## 7. Theoretical Concepts

### Grid Resonance Detection

**Grid resonance** occurs when all open positions accumulate synchronized losses (trending market). Detection via:

| Metric | Threshold | Meaning |
|--------|-----------|---------|
| Position P/L correlation | > 0.85 for 500+ ticks | All losing together |
| Fill rate vs price corr. | < -0.6 | Chasing falling knife |
| Equity velocity | < -$50/sample for 30+ | Sustained bleeding |
| Grid fill completeness | > 90% AND span > 5% | Grid fully deployed |

**Anti-resonance measures** (composite score → action):
- Score 3-5: Widen spacing 1.5x, skip every other entry
- Score 6-8: Double spacing, pause entries
- Score 9+: Triple spacing, close 50% of positions

**Empirical finding**: In FillUpKapitza testing, resonance detection is the primary control limiting returns. Enabling it reduces DD from 28% to 7% but cuts returns from 7.2x to 1.5x.

### Regime Detection Methods

1. **Directional move + consecutive bars** (simple, fast) - Best practical approach
2. **Volatility ratio** (already in ADAPTIVE_SPACING) - Handles amplitude adaptation
3. **Hurst exponent** (expensive, not predictive) - Marginal value, high complexity

**Key finding**: Crash precursors (ATR acceleration, range expansion, velocity) have ~16% hit rate vs 5% random. Not reliable enough for prediction. Focus on **rapid response** instead.

### Frequency Domain Analysis

Current ADAPTIVE_SPACING only considers amplitude. Adding frequency awareness would:
- Reduce position buildup during slow trends (long period → wider spacing)
- Capture more profit during fast oscillations (short period → tighter spacing)
- Expected: -10% DD reduction with minor return impact

Not yet implemented - theoretical framework only.

### Kapitza Pendulum Analogy

Five control mechanisms tested:
1. Frequency analysis - minimal impact
2. Phase detection - minimal impact
3. **Resonance detection** - PRIMARY control (makes/breaks strategy)
4. Regime detection - moderate impact
5. PID control - minimal impact

**Conclusion**: Only resonance detection matters. With it active: safe but low-return. Disabled: equivalent to FillUpOscillation.

---

## 7. Validated Dead Ends

### Strategies That Don't Work

| Approach | Result | Why It Fails |
|----------|--------|--------------|
| MomentumReversal entry filter | 0% H2 survival (180 configs tested) | Severe overfitting |
| 50% position sizing | -17% return for -4.6% DD | Marginal benefit |
| 4h time exit | 1.32x (vs 6.57x baseline) | Kills profitable positions |
| Percentage-based threshold | 85% DD, 49k trades | Too tight, double swap |
| CycleSurfer/Ehlers filters | 0.76x (loss) | Wrong signal type |
| survive_pct < 13% | 100% stop-out | Below max adverse move |
| Shannon's Demon | 0.90x | Rebalancing costs > benefits |
| Stochastic Resonance | 0.37x | Too selective, misses opportunities |
| Absolute spacing on XAGUSD | 96-100% DD | Price tripled, fixed $ spacing fails |
| Any strategy on USDJPY | 0.06x | Insufficient oscillation |
| Self-calibrating thresholds | 5.91x (vs 8.13x hand-tuned) | Early data doesn't generalize |
| Entropy Harvester selectivity | No benefit vs control | Blocking entries hurts, not helps |
| DD reduction (pause/cap/stop) | -38% return per -14% DD | Profit mechanism IS the DD mechanism |
| XAGUSD with absolute $ spacing | 96-100% loss | Price tripled; use pct_spacing mode instead |

### Key Lessons

1. **Never trust single-period backtests** - Always validate H1/H2 split
2. **Simplicity wins** - Base ADAPTIVE_SPACING outperforms all "improvements"
3. **Physics analogies are marketing** - Grid-on-oscillation is the only mechanism that works
4. **Spacing must scale with price** - XAGUSD works with pct_spacing; absolute $ spacing fails when price changes significantly
5. **Swap kills hold-and-wait** - Any strategy holding positions >1 day loses to swap
6. **More parameters = more overfitting risk** - FillUpOscillation's 3 key params are optimal

---

## 8. Spacing & Volatility Deep Dive

### Optimal Spacing Depends on Trend

| Trend Strength | Best Spacing | Example Period |
|----------------|-------------|----------------|
| Strong (>10%) | $0.20-$0.50 | Q1, Q3, Q4 2025 |
| Weak (<6%) | $1.00-$5.00 | Q2 2025, Feb 2025 |

### Trade Economics by Spacing

| Spacing | Trades/Year | Profit/Trade | Annual Return |
|---------|------------|--------------|---------------|
| $0.20 | 244,223 | $0.31 | High (trend) |
| $0.30 | 194,364 | $0.40 | Best (2025) |
| $1.00 | 53,730 | $0.98 | Moderate |
| $1.50 | ~10,334 | ~$5.50 | 6.57x (adaptive) |
| $5.00 | 8,075 | $6.46 | Best (weak trend) |

### Typical Volatility Implementation

```cpp
// Adaptive: scales with price level automatically
double typical_vol = current_bid_ * (typical_vol_pct / 100.0);
double vol_ratio = range / typical_vol;
current_spacing_ = base_spacing_ * std::max(0.5, std::min(3.0, vol_ratio));
```

**Measured median ranges (TypicalVolPct should match):**

| Instrument | 1h Median | 4h Median | Recommended TypicalVolPct |
|------------|-----------|-----------|---------------------------|
| XAUUSD | 0.27% | 0.55% | 0.50% (4h default) |
| XAGUSD | **0.45%** | **0.97%** | 0.45% (1h default) |

Silver is **1.7-1.8x more volatile** than gold in percentage terms.

### Cumulative vs Rolling Trend (TREND_ADAPTIVE)

| Measurement | 2025 Return | Spacing Changes | Bias |
|-------------|-------------|-----------------|------|
| Cumulative YTD | 8.13x | 4,497 | Bull-market optimized |
| Rolling 3-day | 5.30x | 234,741 | Regime-neutral |
| Vol-Adaptive | 6.57x | 2,315 | **Regime-neutral** |

**Warning**: TREND_ADAPTIVE's 8.13x return exploits 2025's +65% bull run. In a bear market, cumulative trend goes negative → wide spacing → poor returns. ADAPTIVE_SPACING is more robust.

---

## 9. Price vs Oscillation Relationship

### Why 2025 Outperforms 2024

| Factor | 2024 | 2025 | Impact |
|--------|------|------|--------|
| Gold price | ~$2,300 | ~$3,500 (+53%) | ~1.5x more dollar oscillations |
| Oscillations/year | ~92,000 | ~281,000 (3.1x) | 3x more trading opportunities |
| ATH proximity | Rarely | Frequently | Extra volatility |
| Combined effect | 2.10x return | 6.59x return | 3.14x ratio |

### Monthly Oscillation Density (2025)

| Month | Avg Price | Oscillations | Normalized (Osc/Price*1k) |
|-------|-----------|--------------|---------------------------|
| Jan | $2,762 | 13,842 | 5.01 |
| Apr | $3,269 | 31,086 | 9.51 (ATH run) |
| **Oct** | **$4,186** | **63,615** | **15.20 (ATH peak)** |
| Dec | $4,245 | 21,654 | 5.10 (normalized) |

**Conclusion**: Strategy is regime-dependent. Expect 2x-7x returns depending on oscillation environment. 2024-like years give ~2x, 2025-like years give ~6-7x.

---

## 10. XAGUSD Percentage-Based Spacing (2026-01-24)

### Problem: Absolute Spacing Fails When Price Changes

Silver tripled from $28.91 to $96.17 in 2025. With fixed $0.75 spacing:
- At $30: spacing = 2.5% of price (reasonable)
- At $96: spacing = 0.78% of price (too tight → overleveraged → margin call)

### Solution: All Spacing Parameters as % of Price

**EA**: `mt5/FillUpAdaptive_v4.mq5`
**C++ test**: `validation/test_engine_xagusd.cpp`

```cpp
// In AdaptiveConfig:
adaptive_cfg.pct_spacing = true;  // Enable percentage mode

// All spacing parameters now interpreted as % of price:
// base_spacing = 2.0 means 2.0% of current bid
// min_spacing_abs = 0.05 means 0.05% of current bid
// max_spacing_abs = 15.0 means 15.0% of current bid
// spacing_change_threshold = 0.2 means 0.2% of current bid
```

### Measured XAGUSD Volatility (29M ticks, 2025)

| Timeframe | Average | Median | P25 | P75 | P90 |
|-----------|---------|--------|-----|-----|-----|
| **1-Hour** | 0.595% | **0.455%** | 0.299% | 0.724% | 1.131% |
| **4-Hour** | 1.227% | **0.970%** | 0.649% | 1.518% | 2.267% |

**TypicalVolPct must match lookback period:**
- 1h lookback → TypicalVolPct = **0.45%**
- 4h lookback → TypicalVolPct = **0.97%**

**Silver vs Gold volatility: 1.7-1.8x** (percentage terms)

### Optimal Parameters (from 42-config sweep)

| Parameter | Value | Notes |
|-----------|-------|-------|
| SurvivePct | **19%** | Range 18-20; below 18 stops out |
| BaseSpacingPct | **2.0%** | Sweet spot; 1.0% stops out, 4.0% too few trades |
| VolatilityLookbackHours | **1.0** | 1h outperforms 4h on XAGUSD |
| TypicalVolPct | **0.45%** | Measured 1h median range |
| MinSpacingPct | 0.05% | Floor |
| MaxSpacingPct | 15.0% | Ceiling |
| SpacingChangeThresholdPct | 0.2% | Hysteresis |
| MinSpacingMult | 0.5 | Vol ratio clamp |
| MaxSpacingMult | 3.0 | Vol ratio clamp |

### Sweep Results (top configurations)

| Config | Return | MaxDD | Trades | Swap | Status |
|--------|--------|-------|--------|------|--------|
| s18_2.0%_lb1 | **115.2x** | **36.9%** | 356 | -$21k | ok |
| s19_2.0%_lb1 | **43.4x** | **29.3%** | 356 | -$13k | ok |
| s20_2.0%_lb1 | **34.1x** | **28.7%** | 356 | -$10k | ok |
| s18_4.0%_lb1 | 139.9x | 53.7% | 76 | -$51k | ok |
| s18_3.0%_lb1 | 55.7x | 81.4% | 163 | -$19k | ok |
| s19_3.0%_lb4 | 42.3x | 60.3% | 163 | -$17k | ok |
| s18_1.0%_lb1 | -0.0x | 101.2% | 176 | -$784 | **SO** |
| s19_3.5%_lb4 | 0.0x | 99.8% | 105 | -$12k | **SO** |

### Key Findings

1. **2.0% spacing is optimal** — produces lowest MaxDD (28-37%) across all survive values
2. **1h lookback beats 4h** — silver oscillates faster than gold
3. **survive=18-20** all work — below 18 stops out (silver dropped ~17% max)
4. **115x return at 37% DD** (survive=18) vs **43x at 29% DD** (survive=19) — choose risk level
5. **3.5%+ spacing with lb4 stops out** — too wide, positions never fill during fast moves
6. **Swap cost scales with return** — higher return configs hold larger positions longer

### XAGUSD Engine Configuration

```cpp
TickBacktestConfig config;
config.symbol = "XAGUSD";
config.initial_balance = 10000.0;
config.contract_size = 5000.0;    // 5000 oz per lot
config.leverage = 500.0;
config.pip_size = 0.001;          // 3 decimal places
config.swap_long = -15.0;         // Points per lot per day
config.swap_short = 0.0;
config.swap_mode = 1;
config.swap_3days = 3;
```

### Why Percentage-Based Works

At $30 silver with 2.0% spacing:
- Grid spacing = $0.60
- TP = spread + $0.60
- Survive distance = $5.70 (19% of $30)
- Trades across distance = 9.5

At $96 silver with 2.0% spacing:
- Grid spacing = $1.92
- TP = spread + $1.92
- Survive distance = $18.24 (19% of $96)
- Trades across distance = 9.5

**Same number of grid levels** regardless of price → consistent risk profile.

### Parallel Sweep Performance (XAGUSD)

| Metric | Value |
|--------|-------|
| Tick data | 29.3M ticks, ~1.3GB |
| Load time | ~38s |
| Threads | 16 (hardware_concurrency) |
| 42 configs | 57s total (1.3s/config) |

---

## 11. MT5 Validation Checklist

- [ ] Uses `TickBasedEngine` (not standalone)
- [ ] Swap rates correct for broker
- [ ] `swap_mode = 1` for points-based
- [ ] `swap_3days = 3` for Wednesday triple
- [ ] `contract_size = 100.0` for XAUUSD
- [ ] `leverage = 500.0`
- [ ] Static linking for MinGW
- [ ] Absolute path for tick data
- [ ] Results include swap verification
- [ ] H1/H2 split validation performed

### Expected MT5 Match Tolerances

| Metric | Tolerance |
|--------|-----------|
| Margin stop-out date | Exact match |
| DD trigger count | ±1-2 |
| Final balance | ±5% |
| Swap total | ±10% |

---

## 12. Test File Reference

### Validation Tests (Key Files)

| Test | File | Purpose |
|------|------|---------|
| Primary strategy | `test_d8_50.cpp` | Reference: survive=8%, spacing=$1, DD=50% |
| 7 parasitic strategies | `test_7_strategies.cpp` | All market-parasitic strategies comparison |
| H1/H2 validation | `test_h1h2_validation.cpp` | Out-of-sample testing |
| Parameter stability | `test_parameter_stability.cpp` | 80 config sweep |
| Risk management | `test_risk_management.cpp` | Monte Carlo, equity stops |
| Multi-year | `test_multi_year.cpp` | 2024 + 2025 sequential |
| Stress scenarios | `test_stress_scenarios.cpp` | Flash crash, sustained crash |
| Risk metrics | `test_risk_metrics.cpp` | Sharpe, Sortino, Calmar |
| Spacing investigation | `test_spacing_investigation.cpp` | Optimal spacing by period |
| Frequency analysis | `test_frequency_analysis.cpp` | Oscillation periods & amplitudes |
| Kapitza tuning | `test_kapitza_tuning.cpp` | Resonance detection impact |
| Trend adaptive | `test_trend_adaptive.cpp` | Trend-based spacing modes |
| Improvements | `test_fillup_osc_improvements.cpp` | Domain investigation validation |
| MomentumReversal | `test_momentum_reversal_sweep.cpp` | Overfitting proof (180 configs) |
| DD reduction | `test_dd_reduction.cpp` | 135-config parallel sweep (DD vs return trade-off) |
| Trailing stop | `test_trailing_5d_parallel.cpp` | 768-config parallel sweep |
| XAGUSD engine sweep | `test_engine_xagusd.cpp` | 42-config pct_spacing sweep |
| XAGUSD volatility | `test_xagusd_vol.cpp` | Measured 1h=0.45%, 4h=0.97% |

### MT5 EA Files

| EA | File | Description |
|----|------|-------------|
| v2 | `mt5/FillUpAdaptive_v2.mq5` | Absolute spacing, fixed typical vol |
| v3 | `mt5/FillUpAdaptive_v3.mq5` | Absolute spacing, %-based typical vol |
| **v4** | **`mt5/FillUpAdaptive_v4.mq5`** | **%-based grid spacing (all params scale with price)** |

### Domain Investigation Tests

| Domain | File | Key Finding |
|--------|------|-------------|
| 1. Oscillation | `test_domain1_oscillation_characterization.cpp` | 281k/year (2025) |
| 2. Regime | `test_domain2_regime_detection.cpp` | 7 TRENDING_DOWN episodes |
| 3. Time patterns | `test_domain3_time_patterns.cpp` | NY session peak |
| 4. Position lifecycle | `test_domain4_position_lifecycle.cpp` | 91% close <5 min |
| 5. Entry optimization | `test_domain5_entry_optimization.cpp` | MomentumRev OVERFIT |
| 6. Exit refinement | `test_domain6_exit_refinement.cpp` | 4h exit hurts |
| 7. Risk of ruin | `test_domain7_risk_of_ruin.cpp` | 0.10% ruin probability |
| 8. Multi-instrument | `test_domain8_multi_instrument.cpp` | NAS100 needs recalibration |
| 9. Capital allocation | `test_domain9_capital_allocation.cpp` | 50% optimal |
| 10. Correlation | `test_domain10_correlation.cpp` | No correlations found |

---

## 13. XAUUSD Parameter Selection Methodology (Reproducible)

This section documents the complete process used to find optimal XAUUSD parameters. Follow these steps in order to reproduce the process for a new instrument, time period, or broker.

### Overview: The 6-Step Process

```
1. MEASURE    → Oscillation characteristics + volatility ranges
2. SWEEP      → Parametric grid across survive/spacing/lookback
3. VALIDATE   → H1/H2 split (anti-overfitting)
4. STRESS     → Multi-year regime robustness
5. REFINE     → Spacing optimization for adaptive mode
6. CONFIRM    → Parameter stability neighborhood check
```

---

### Step 1: Measure Oscillation Characteristics

**Purpose**: Understand how the instrument moves — amplitude, frequency, asymmetry.

**Script**: `validation/test_domain1_oscillation_characterization.cpp`

**Method**:
1. Load full-year tick data into memory (51.7M ticks for XAUUSD 2025)
2. Detect swing highs/lows using minimum threshold ($1.00 for gold)
3. Record: amplitude, duration, direction for each oscillation
4. Compute distributions at multiple scales (0.05%, 0.15%, 0.50% of price)

**Key outputs needed**:
- Median oscillation amplitude (stable at ~$1.00 for XAUUSD regardless of daily range)
- Oscillation count per year (281k in 2025, 92k in 2024)
- Duration distribution (91.4% resolve in <5 minutes)
- Downswing vs upswing speed asymmetry (down is 17-33% faster)

**Why this matters**: The median oscillation amplitude determines the "natural" grid spacing. If median swing is $1.00, spacing near $1.50 captures most reversals with one grid level of profit. Too tight (<$0.50) means excessive trades and swap. Too wide (>$3.00) means missing most oscillations.

---

### Step 2: Measure Volatility for Adaptive Spacing

**Purpose**: Determine TypicalVolPct — the expected price range over the lookback window.

**Script**: `validation/test_xagusd_vol.cpp` (same methodology applies to XAUUSD)

**Method**:
1. Stream through all ticks, tracking high/low over rolling windows
2. Reset window every N hours (test both 1h and 4h)
3. Record the range (high - low) at each reset
4. Compute: mean, median, P25, P75, P90 of ranges as % of price

**XAUUSD 2025 results**:

| Window | Mean | Median | P25 | P75 | P90 |
|--------|------|--------|-----|-----|-----|
| 1-hour | 0.37% | 0.27% | 0.18% | 0.44% | 0.68% |
| 4-hour | 0.72% | 0.55% | 0.37% | 0.87% | 1.32% |

**Parameter selection rule**: TypicalVolPct = **median range** for the chosen lookback window.
- If lookback = 4h → TypicalVolPct = 0.55%
- If lookback = 1h → TypicalVolPct = 0.27%

**Why median**: The adaptive spacing divides observed range by typical_vol to get a ratio. When range = typical (ratio=1.0), spacing = base_spacing. When range > typical (high vol), spacing widens. When range < typical (quiet), spacing tightens. Using median ensures ratio=1.0 is the "normal" condition.

---

### Step 3: Parameter Sweep

**Purpose**: Find optimal combination of survive_pct, base_spacing, lookback.

**Script**: `validation/test_parameter_stability.cpp`

**Sweep grid**:
```
survive_pct:   [10, 13, 15, 18, 20]        # 5 values
base_spacing:  [1.0, 1.5, 2.0, 2.5]        # 4 values (absolute $ for C++ backtest)
lookback:      [1, 2, 4, 8]                 # 4 values (hours)
─────────────────────────────────────────
Total:         80 configurations
```

**For percentage-based EA** (v4), translate base_spacing to BaseSpacingPct:
- At XAUUSD $2,700 (Jan 2025): $1.50 ÷ $2,700 = 0.055% → BaseSpacingPct ≈ 0.06
- At XAUUSD $3,500 (mid 2025): $1.50 ÷ $3,500 = 0.043%
- For consistent behavior across price levels, use the C++ absolute spacing in the backtest

**Infrastructure**: Use parallel sweep pattern (Section 6b):
1. Load ticks once into shared memory (~60s)
2. 16 threads process 80 configs (~5s total)
3. Collect: return multiple, max DD, trade count, Sharpe ratio, swap cost

**Selection criteria** (in priority order):
1. **Must survive**: No margin stop-out (eliminates survive < 13% for XAUUSD)
2. **Best risk-adjusted return**: Sharpe ratio (return / DD)
3. **Trade count > 5,000**: Statistical significance
4. **Reasonable DD**: Prefer < 70% max DD for practical deployment

**XAUUSD 2025 results** (top configs):

| survive | spacing | lookback | Return | MaxDD | Sharpe | Status |
|---------|---------|----------|--------|-------|--------|--------|
| 13% | $1.50 | 4h | 6.57x | 66.9% | 11.39 | SELECTED |
| 13% | $1.50 | 2h | 6.32x | 67.1% | 10.8 | viable |
| 13% | $1.00 | 4h | 7.12x | 72.4% | 9.2 | too risky |
| 15% | $1.50 | 4h | 5.81x | 60.2% | 10.5 | conservative |
| 18% | $2.00 | 4h | 3.31x | 54.8% | 7.1 | too conservative |
| 10% | any | any | STOP-OUT | - | - | ELIMINATED |

---

### Step 4: Out-of-Sample Validation (H1/H2 Split)

**Purpose**: Detect overfitting. If parameters only work on the period they were optimized on, they'll fail live.

**Script**: `validation/test_h1h2_validation.cpp`

**Method**:
1. Run best config on H1 only (Jan-Jun): Record return, DD
2. Run best config on H2 only (Jul-Dec): Record return, DD
3. Calculate H1/H2 ratio of returns
4. Check both halves survive independently

**Pass criteria**:
- Both halves must end with balance > 10% of start (survival)
- H1/H2 return ratio must be in range [0.33, 3.0]
- Ideal ratio is 1.0 (equal performance in both halves)

**XAUUSD results** (survive=13, spacing=$1.50, lookback=4h):
- H1: 2.54x return, ~66% DD → PASS
- H2: 2.65x return, ~67% DD → PASS
- Ratio: 0.96 → PASS (near-perfect consistency)
- Full year: 2.54 × 2.65 ≈ 6.57x (compounds)

**Counter-example** (MomentumReversal filter, 180 configs tested):
- H1: Various returns, some good
- H2: 0% survival across ALL configs → FAIL (pure overfitting)

**Why this works**: Overfitted parameters exploit specific price patterns in the training period. By requiring both halves to perform, you ensure the strategy captures a general market property (oscillation), not a specific sequence.

---

### Step 5: Multi-Year Regime Robustness

**Purpose**: Verify the strategy isn't just exploiting one year's market conditions.

**Script**: `validation/test_multi_year.cpp`

**Method**:
1. Run on 2024 data (lower oscillation, ~92k swings, moderate trend)
2. Run on 2025 data (high oscillation, ~281k swings, strong bull)
3. Run sequentially: Start with 2024, carry balance into 2025
4. Compare performance ratio to oscillation ratio

**Expected**: Performance should scale with oscillation count, not be period-specific.

**Results**:
| Year | Oscillations | Return | DD | Performance/Oscillation |
|------|-------------|--------|-----|-------------------------|
| 2024 | 92,000 | 2.10x | 43% | 2.28e-5 per oscillation |
| 2025 | 281,000 | 6.59x | 67% | 2.35e-5 per oscillation |

Ratio: Performance scales linearly with oscillation count (within 3%). This confirms the strategy captures a structural market property.

**Red flags to watch for**:
- Year 2 return < 0.5x Year 1 (strategy may have stopped working)
- Max DD in Year 2 >> Year 1 (increased risk without increased return)
- Inconsistent performance/oscillation ratio (overfitting to regime)

---

### Step 6: Parameter Stability (Neighborhood Check)

**Purpose**: Ensure the selected parameters aren't a fragile local optimum.

**Method** (within the 80-config sweep):
1. Take the selected config: survive=13, spacing=$1.50, lookback=4h
2. Check all configs that differ by ±1 parameter step
3. Measure return variance across this neighborhood

**Neighborhood of (13, $1.50, 4h)**:
- (13, $1.50, 2h): 6.32x → -3.8% vs baseline
- (13, $1.50, 8h): 6.05x → -7.9%
- (13, $1.00, 4h): 7.12x → +8.4%
- (13, $2.00, 4h): 5.21x → -20.7%
- (15, $1.50, 4h): 5.81x → -11.6%

**Pass criterion**: No neighboring config should differ by >50% in return. All neighbors above are within 21%, indicating a broad optimum rather than a sharp peak.

**Sharp peak = overfitting**: If one config gives 6.57x but all neighbors give <2x, the parameter is exploiting noise.

---

### Step 7: Validate Against Dead Ends

**Purpose**: Confirm that attempted improvements don't help (the baseline is truly optimal).

**Tests performed** (all FAILED to improve baseline):
- Entry filters (MomentumReversal): Overfit, 0% H2 survival
- Exit timing (4h time exit): Kills profitable positions, 1.32x
- DD reduction (pause/cap/stop): -38% return per -14% DD saved
- Trailing stops (768 configs): No config beats TP = spread + spacing
- Position sizing variations: Marginal benefit, added complexity
- Auto-calibration (lookback+typvol): -6% to -47% vs manual tuning

**Conclusion**: The 3-parameter model (survive, spacing, lookback) with ADAPTIVE_SPACING is the correct complexity level. Adding parameters causes overfitting or negligible improvement.

---

### Translating C++ Parameters to EA v4 (Percentage-Based)

The C++ backtest uses absolute dollar values. The v4 EA uses percentage of price. Translation:

```
BaseSpacingPct = (base_spacing_dollars / reference_price) × 100

For XAUUSD at ~$2,700 (Jan 2025 start):
  $1.50 / $2,700 × 100 = 0.055% → use 0.06%

For XAUUSD at ~$3,500 (mid 2025):
  $1.50 / $3,500 × 100 = 0.043%
```

**Important**: The EA's percentage-based spacing automatically adapts to price level. At $2,700 with BaseSpacingPct=0.06, effective spacing = $1.62. At $3,500, effective spacing = $2.10. This handles price drift without manual adjustment — the same feature that makes XAGUSD work despite tripling from $29 to $96.

The EA's TypicalVolPct and VolatilityLookbackHours translate directly (same values as C++ adaptive config).

---

### Reproducing for a New Instrument

To find parameters for a new instrument (e.g., EURUSD, NAS100):

1. **Get tick data**: Full year minimum, ideally 2+ years
2. **Measure oscillations** (Step 1): Find median amplitude — this suggests base_spacing
3. **Measure volatility** (Step 2): Find median 1h and 4h ranges as % of price
4. **Determine survive_pct**: Must exceed the maximum observed adverse move in data
   - Measure: largest peak-to-trough move in the dataset
   - Add safety margin: survive = max_adverse × 1.2 (minimum)
5. **Run sweep** (Step 3): Grid around estimated values
6. **Validate** (Steps 4-6): H1/H2 split, multi-year if data available, neighborhood check

**Known results for other instruments**:
- XAGUSD: survive=19%, BaseSpacingPct=2.0%, lookback=1h, TypicalVolPct=0.45%
- USDJPY: Does NOT work (insufficient oscillation, swap dominates)
- NAS100: Needs recalibration (different characteristics from forex/metals)

---

### Auto-Calibration: Tested and Rejected

An auto-calibration approach was tested where TypicalVolPct and VolatilityLookbackHours would self-tune from historical bar data using M1 bars and swing detection.

**Script**: `validation/test_auto_calibration.cpp`

**Results (XAUUSD 2025)**:

| Config | Return | vs Manual | Verdict |
|--------|--------|-----------|---------|
| MANUAL_ALL (baseline) | 10.61x | — | Best |
| AUTO_LOOKBACK+TYPVOL | 9.96x | -6% | Acceptable but worse |
| AUTO_LOOKBACK alone | 5.79x | -45% | Dangerous (85% DD) |
| AUTO_SPACING alone | 6.80x | -36% | Significant loss |
| AUTO_ALL | 5.65x | -47% | Unacceptable |

**Why it fails**: Auto-calibration uses early data to set parameters, but early data doesn't represent the full year. Lookback and TypVol are mathematically coupled — changing one without the other causes misalignment. Manual tuning on historical data consistently outperforms.

**Conclusion**: Use manual parameters tuned via the sweep methodology above. The 30 minutes to run the sweep produces better parameters than any auto-calibration scheme tested.

---

### Quick Reference: Final XAUUSD Parameters

```
Strategy: FillUpOscillation, ADAPTIVE_SPACING mode
─────────────────────────────────────────────────
survive_pct              = 13.0%    [Exceeds max adverse move 11-12%]
base_spacing             = $1.50    [Near median oscillation amplitude]
volatility_lookback      = 4 hours  [Stable, avoids noise]
typical_vol_pct          = 0.55%    [Measured 4h median range]
min_spacing_mult         = 0.5      [Floor: half of base in quiet markets]
max_spacing_mult         = 3.0      [Ceiling: 3x base in volatile markets]
min_volume               = 0.01
max_volume               = 10.0
contract_size            = 100.0
leverage                 = 500

EA v4 equivalents:
  SurvivePct             = 13.0
  BaseSpacingPct         = 0.06     [$1.50 / $2,700 reference price]
  VolatilityLookbackHours= 4.0
  TypicalVolPct          = 0.55
  MinSpacingMult         = 0.5
  MaxSpacingMult         = 3.0
  MinSpacingPct          = 0.005
  MaxSpacingPct          = 1.0
  SpacingChangeThresholdPct = 0.01
```

### Validation Summary

| Check | Criterion | Result | Status |
|-------|-----------|--------|--------|
| Survival | No stop-out | 66.9% max DD, survived | PASS |
| H1/H2 split | Ratio 0.33-3.0 | 0.96 | PASS |
| Multi-year | Both years profitable | 2.10x (2024), 6.59x (2025) | PASS |
| Stability | Neighbors <50% variance | Max 21% variance | PASS |
| Trade count | >5,000 | 10,334 | PASS |
| Improvements | None beat baseline | All tested failed | PASS |
| Risk of ruin | <1% (Monte Carlo) | 0.10% | PASS |

---

## 14. Percentage-Based Spacing & Regime Independence (2026-01-25)

### Why Percentage-Based Spacing

Absolute dollar spacing fails when price changes significantly:
- Gold 2024 avg: ~$2,300 → $1.50 spacing = 0.065% of price
- Gold 2025 avg: ~$3,500 → $1.50 spacing = 0.043% of price (34% tighter!)
- Gold at $5,000 → $1.50 spacing = 0.030% of price

**The v4 EA uses percentage-based spacing by default** (`BaseSpacingPct`). This ensures consistent grid behavior regardless of price level.

### Regime Independence Analysis

Tested 2024 (+27% trend) vs 2025 (+60% trend) to measure "regime dependence":

| Spacing Type | Best Ratio | Interpretation |
|--------------|------------|----------------|
| Absolute $ | 2.35x | 2025 returned 2.35x more than 2024 |
| **Percentage %** | **1.73x** | More stable across regimes |

Lower ratio = more regime-independent.

### The Sweet Spot: 0.06-0.10% with High TypVol

**Test file**: `validation/test_sweetspot_sweep.cpp` (1020 configs, parallel)

| Config | 2025 | 2024 | Ratio | 2025 DD |
|--------|------|------|-------|---------|
| s12_sp0.085%_lb2.0_tv1.20 | 5.35x | 3.16x | **1.69x** | 91.9% |
| s13_sp0.085%_lb2.0_tv1.20 | 4.71x | 2.75x | **1.71x** | 85.0% |
| s13_sp0.090%_lb2.0_tv1.20 | 4.79x | 2.79x | **1.72x** | 84.7% |

**Key discovery**: Regime independence comes from the **spacing + typvol combination**, not spacing alone.

| typvol | Spacing | Avg Ratio | Regime Dependence |
|--------|---------|-----------|-------------------|
| 0.20-0.35% | any | 4-6x | HIGH |
| **0.80-1.20%** | **0.06-0.10%** | **1.7-2.1x** | **LOW** |

High typvol dampens the adaptive spacing mechanism, making it more stable.

### U-Curve Pattern (Percentage Spacing)

Both tiny and wide percentage spacings show high regime dependence:

```
Regime
Ratio
   8x │                                          ●●
      │                                        ●    ●
   6x │                                              ●
      │  ●
   4x │    ●  ●                              ●
      │         ●    ●●                  ●
   3x │              ●●●●              ●
      │                  ●●●●●●●●●●
   2x │        ●●
      │
   1x │
      └──────────────────────────────────────────────
        0.01  0.05  0.1   0.3  0.5  1   2   3  4  5  6  (%)
              ▲▲
           SWEET SPOT (0.05-0.06% at tv=0.55%)
```

**Why the pattern exists:**
- **Tiny (<0.04%)**: Captures micro-noise, 2025 had more ATH noise
- **Sweet spot (0.05-0.10%)**: Captures median oscillation amplitude
- **Wide (>2%)**: Captures trend pullbacks only, pure trend exposure

### Survive 12% vs 13%

| Survive | Avg 2025 | Avg 2024 | Avg Ratio | Avg DD |
|---------|----------|----------|-----------|--------|
| 12% | 11.34x | 2.85x | 3.98x | 85.3% |
| **13%** | **7.71x** | **2.59x** | **2.98x** | **74.9%** |

**survive=13% is 25% more regime-stable** and has 10% lower DD.

### Recommended Configs by Goal

| Goal | Config | 2025 | 2024 | Ratio | DD |
|------|--------|------|------|-------|-----|
| **Regime stability** | sp=0.085%, lb=2h, tv=1.20, s=13 | 4.71x | 2.75x | 1.71x | 85% |
| **Max return** | sp=0.045%, lb=8h, tv=0.55, s=12 | 17.69x | 3.00x | 5.90x | 82% |
| **Lowest DD** | sp=0.030%, lb=8h, tv=0.20, s=13 | 10.77x | 2.29x | 4.70x | 63% |
| **Balanced** | sp=0.06%, lb=4h, tv=0.55, s=13 | 7.61x | 2.46x | 3.10x | 80% |

### Test Files Created

| File | Purpose |
|------|---------|
| `test_wide_spacing_analysis.cpp` | Wide spacing trend dependence |
| `test_small_spacing_regime.cpp` | Small spacing regime test |
| `test_pct_spacing_regime.cpp` | Absolute vs percentage comparison |
| `test_pct_regime_parallel.cpp` | Full % range (0.01-6%) parallel |
| `test_sweetspot_sweep.cpp` | 1020-config sweet spot sweep |

### Practical Implications

1. **Use percentage-based spacing** (v4 EA default) for price-level independence
2. **For regime stability**: Use higher typvol (0.80-1.20%) with 0.06-0.10% spacing
3. **For max returns**: Accept higher regime dependence with lower typvol
4. **survive=13%** provides better risk-adjusted returns than 12%
5. **The ~1.7x irreducible ratio** represents true market regime difference (volatility, oscillation frequency) that cannot be eliminated by parameter choice

---

## 15. Chaos Theory Investigations (2026-01-26)

### Motivation

The FillUp strategy can be viewed as dynamic stabilization (Kapitza pendulum analogy) - market oscillations stabilize the grid structure. Eight chaos-inspired trading concepts were investigated.

### Summary of Results

| # | Concept | Result | Best Return | vs Baseline | Verdict |
|---|---------|--------|-------------|-------------|---------|
| 1 | **Floating Attractor Grid** | **SUCCESS** | **11.05x** | +36% | Grid tracks EMA, larger TPs |
| 2 | Synchronization (Pairs) | FAIL | 0.92x | -89% | Both metals trend together |
| 3 | **Edge of Chaos** | **SUCCESS** | **15.98x** | +97% | Phase transition at survive=12% |
| 4 | OGY Control (Timing) | PARTIAL | 1.91x | -77% | Timing validated, low returns |
| 5 | Bifurcation Detection | FAIL | 8.14x | +0% | Signals fire too late |
| 6 | Entropy Export | FAIL | 1.45x | -82% | Cutting losers destroys returns |
| 7 | Fractal Multi-Scale | FAIL | 10.71x | +1% | No diversification benefit |
| 8 | Noise Scaling | FAIL | 7.95x | -2% | Noise ratio ~1.0, no signal |

Baseline: FillUpOscillation ADAPTIVE_SPACING = 8.13x return, 77.7% DD

### Investigation #1: Floating Attractor Grid

**Concept**: Grid "floats" around a moving average (EMA) instead of fixed price levels.

**Best config**: EMA-200, TP multiplier 2.0
- Return: **11.05x** (+36% vs baseline)
- Max DD: **67.6%** (-10pp vs baseline)
- Sharpe: **14.87** (+62% vs baseline)

**Why it works**: When price dips below EMA, it typically reverts back past the mean. Larger TPs (2.0x) capture more of this reversion.

**Trade-off discovered**:
- High returns (11.05x) = higher regime dependence (4.45 ratio)
- Low regime dependence (1.89 ratio) = moderate returns (5.08x)

**Files**: `include/strategy_floating_attractor.h`, `validation/test_floating_attractor.cpp`

### Investigation #3: Edge of Chaos

**Concept**: Complex systems are most adaptive at the boundary between order and chaos.

**Key finding**: Clear **phase transition at survive=12%**
- Below 12%: 100% stop-out (chaotic regime)
- At 12%: Highest returns (15.98x) AND lowest chaos scores
- Above 12%: Returns decrease, stability increases

**Best config**: survive=12%, spacing=$1.50, lookback=8h
- Return: **15.98x** (+97% vs baseline)
- Max DD: 81.0%
- Sharpe: **18.49**

**Paradox**: The "edge of chaos" produces both maximum returns AND stable behavior. This is because at survive=12%, the grid is optimally sized to capture oscillations without overloading.

**Goldilocks zone** (stable AND profitable): survive=13%, spacing=$3-4, lookback=8h
- Return: 7.69-8.13x
- Max DD: 63-66%
- Most stable behavior (lowest chaos scores)

**Files**: `validation/test_edge_of_chaos.cpp`

### Investigation #4: OGY Control Method

**Concept**: Chaotic systems can be controlled with tiny, well-timed perturbations at unstable equilibria.

**Key finding**: Timing at velocity zero-crossings (local min/max) **100% outperforms random timing**

| Timing | Return | Max DD |
|--------|--------|--------|
| OGY (velocity zero) | 1.91x | 88.7% |
| Random | 0.01x | 99.4% (SO) |

**Optimal parameters**:
- Velocity window: 100 ticks (10-50 too noisy, 500+ misses signals)
- Detection method: VELOCITY_ZERO only (LOCAL_MINMAX doesn't work)

**Actionable insight**: Velocity zero-crossing detection could improve FillUpOscillation entry timing.

**Files**: `include/strategy_ogy_control.h`, `validation/test_ogy_control.cpp`

### Failed Investigations

#### #2: Chaos Synchronization (Pairs Trading)
- Gold/silver ratio IS mean-reverting (94%+ reversion rate)
- BUT both metals trended together in 2025, negating hedge benefit
- Best pairs config lost 8% while single-instrument made 6.57x
- **Files**: `include/strategy_chaos_sync.h`, `validation/test_chaos_sync.cpp`

#### #5: Bifurcation Detection
- Signals (vol_of_vol, range_ratio, velocity_accel) fire too late
- By the time bifurcation detected, positions already accumulated
- Natural defense (margin exhaustion) more effective than signal-based
- **Files**: `include/strategy_bifurcation.h`, `validation/test_bifurcation.cpp`

#### #6: Entropy Export (Dissipative Structures)
- Cutting losing positions destroys the profit mechanism
- Best config: 1.45x return, 39% DD vs baseline 8.13x, 77% DD
- Trade-off: ~3:1 (lose 3% returns per 1% DD saved)
- **Files**: `include/strategy_entropy_export.h`, `validation/test_entropy_export.cpp`

#### #7: Fractal Multi-Scale Grids
- Triple-scale is harmful: -27% return, +11% DD vs single-scale
- No diversification benefit - all scales lose during drawdowns together
- Micro-scale (0.02%) captures noise, not signal
- **Files**: `validation/test_fractal_grid.cpp`

#### #8: Noise as Signal Carrier
- Noise ratio stays near 1.0 (short/long-term noise highly correlated)
- ADAPTIVE_SPACING already captures volatility information
- 0/216 configs beat baseline
- **Files**: `include/strategy_noise_scaling.h`, `validation/test_noise_scaling.cpp`

### Key Insights

1. **The grid's profit mechanism IS its drawdown mechanism** - You cannot reduce DD without destroying returns. This explains why bifurcation detection, entropy export, and other "protective" mechanisms fail.

2. **Phase transitions exist in parameter space** - survive=12% is a sharp boundary. This is the "edge of chaos" where the strategy is maximally effective.

3. **Entry timing matters** - OGY investigation proved velocity zero-crossing beats random timing by 190x. This can improve existing strategies.

4. **Floating attractor improves both return AND risk** - Rare finding where both metrics improve simultaneously.

### Recommended Combinations

| Goal | Configuration | Expected |
|------|---------------|----------|
| **Max Sharpe** | Floating attractor (EMA-200, TP 2.0) + survive=12% | ~16-18x return, ~70% DD |
| **Regime stable** | Floating attractor + survive=13% + 8h lookback | ~8-10x return, ~60% DD |
| **Entry timing** | Add velocity zero-crossing filter to FillUpOscillation | Fewer but better entries |

### Strategy Files Created

| File | Concept | Status |
|------|---------|--------|
| `strategy_floating_attractor.h` | Floating attractor grid | **PROMISING** |
| `strategy_ogy_control.h` | OGY chaos control | Timing insight useful |
| `strategy_chaos_sync.h` | Pairs trading | FAIL |
| `strategy_bifurcation.h` | Bifurcation detection | FAIL |
| `strategy_entropy_export.h` | Entropy export | FAIL |
| `strategy_noise_scaling.h` | Noise scaling | FAIL |
