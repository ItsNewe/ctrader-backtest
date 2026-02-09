# Final Strategy Recommendations

## Executive Summary

After extensive backtesting of 6 strategy approaches on 2025 tick data (~53M ticks each), here are the final recommendations:

| Rank | Strategy | Best For | Return | Max DD |
|------|----------|----------|--------|--------|
| 1 | **Hybrid** | Gold | 4.93x | 28% |
| 2 | **Grid + Crash Detect** | NAS100 | 1.30x | 11% |
| 3 | **Anti-Fragile** | Both | 1.10x-5.0x | 12-52% |
| 4 | **Bidirectional Grid** | Ranging only | 1.09x | 10% |
| 5 | **Regime-Adaptive** | Conservative | 1.03x | 6% |
| 6 | **Volatility Harvest** | Not recommended | 0.98x | 6% |
| 7 | **Dynamic Hedging** | Not recommended | 0.13x | 99% |

---

## Strategy Implementations

### 1. Hybrid Strategy (RECOMMENDED for Gold)
**File**: `include/strategy_hybrid.h`

Combines anti-fragile sizing with crash detection:
- Larger positions at lower prices (load the spring)
- Exit when velocity indicates crash
- Re-enter after stabilization

**Best Config for Gold**:
```cpp
base_lot_size = 0.02
entry_spacing = 5.0
sizing_exponent = 1.5
crash_velocity_threshold = -0.4  // % drop over lookback
crash_lookback = 500
crash_exit_pct = 0.5
```
**Result**: 4.93x return, 28% max DD

### 2. Grid + Crash Detection (RECOMMENDED for NAS100)
**File**: `include/grid_improved.h`

Original Grid strategy with crash protection:
- Enter on new ATH
- Size based on survive_down %
- Exit 50% when velocity indicates crash

**Best Config for NAS100**:
```cpp
survive_down_pct = 30.0
crash_velocity_threshold = -0.3
crash_lookback = 500
crash_exit_pct = 0.5
```
**Result**: 1.30x return, 11% max DD

### 3. Anti-Fragile Strategy
**File**: `include/strategy_antifragile.h`

Gets stronger from stress:
- Small positions normally
- Larger positions during dips (better prices)
- Take profit when average position is profitable

**Best Config**:
- NAS100: base=0.01, spacing=50, exponent=1.5 → 1.10x, 12% DD
- Gold: base=0.02, spacing=5, exponent=1.5 → 5.0x, 52% DD

### 4. Bidirectional Grid
**File**: `include/strategy_bidirectional_grid.h`

Buy below, sell above, profit from oscillation:
- Works in ranging markets
- **Fails in trending markets** (Gold margin called)

**Best Config** (NAS100 only):
```cpp
grid_spacing = 50.0
lot_size = 0.1
max_levels_per_side = 10
enable_rebalancing = false  // Important!
```
**Result**: 1.09x return, 10% max DD

### 5. Regime-Adaptive Strategy
**File**: `include/strategy_regime_adaptive.h`

Adapts to market conditions:
- More aggressive in uptrends
- Defensive in downtrends
- Smaller positions in high volatility

**Finding**: Too conservative in 2025 data. At tick level, even trending markets appear "ranging" 98% of the time.

### 6. Volatility Harvest (NOT RECOMMENDED)
**File**: `include/strategy_volatility_harvest.h`

Buy dips, quick TP:
- 70% win rate but negative R:R
- Edge too thin for profitability

### 7. Dynamic Hedging (NOT RECOMMENDED)
**File**: `include/strategy_dynamic_hedge.h`

Always long and short:
- Fundamentally broken in trending markets
- Accumulates one-sided exposure

---

## Key Insights

### What Works:
1. **Anti-fragile sizing** - Buying more at lower prices improves average entry
2. **Crash detection** - Velocity-based exit protects against disasters
3. **Survive_down sizing** - Original Grid concept is sound
4. **Direction-agnostic profit** - Possible but requires oscillating markets

### What Doesn't Work:
1. **Symmetric strategies in trending markets** - Grid/hedging fails
2. **Tight stop losses** - Cause churning, destroy profits
3. **Over-sensitive crash detection** - False positives hurt returns
4. **Pure mean reversion** - Edge too thin at tick level

### Asset-Specific Findings:

**NAS100 (2025)**:
- Strong uptrend with high volatility
- Crash detection triggers too often
- Pure anti-fragile or Grid+crash best

**Gold (2025)**:
- Strong uptrend, cleaner moves
- Crash detection works well
- Hybrid approach optimal

---

## Final Recommendations

### For Production Use:

**Gold Trading**:
```cpp
// Use Hybrid Strategy
HybridConfig cfg;
cfg.base_lot_size = 0.02;
cfg.entry_spacing = 5.0;
cfg.sizing_exponent = 1.5;
cfg.crash_velocity_threshold = -0.4;
cfg.crash_lookback = 500;
cfg.crash_exit_pct = 0.5;
cfg.contract_size = 100.0;
cfg.leverage = 500.0;
```
**Expected**: ~5x annual return, ~28% max DD

**NAS100 Trading**:
```cpp
// Use Anti-Fragile (no crash detection)
AntifragileConfig cfg;
cfg.base_lot_size = 0.01;
cfg.entry_spacing = 50.0;
cfg.sizing_exponent = 1.5;
cfg.take_profit_pct = 2.0;
cfg.contract_size = 1.0;
cfg.leverage = 500.0;
```
**Expected**: ~1.1x annual return, ~12% max DD

---

## Files Created

| File | Purpose |
|------|---------|
| `strategy_bidirectional_grid.h` | Grid trading both directions |
| `strategy_antifragile.h` | Load spring on dips |
| `strategy_dynamic_hedge.h` | Long+short hedging |
| `strategy_volatility_harvest.h` | Mean reversion scalping |
| `strategy_hybrid.h` | Anti-fragile + crash detect |
| `strategy_regime_adaptive.h` | Market condition adaptation |
| `STRATEGY_GUIDELINES.md` | Trading philosophy |
| `STRATEGY_COMPARISON.md` | Detailed test results |

---

*Based on backtesting 2025 tick data. Past performance does not guarantee future results.*
