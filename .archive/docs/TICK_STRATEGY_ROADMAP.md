# Tick-Based Strategy Roadmap

**Date:** 2026-01-07
**Current Status:** Bar-based engine validated (98.35% accuracy)
**Goal:** Support real tick-based strategy execution

---

## 🎯 Objective

Enhance the C++ backtest engine to support tick-level strategy execution for high-frequency and intra-bar trading strategies.

---

## 📋 Three-Tier Approach

### Tier 1: Enhanced Bar-Based (Quick Win) ⭐ **RECOMMENDED START**

**Timeline:** 3-5 days
**Accuracy:** 95-98% for most strategies
**Effort:** Low

**Features to Add:**
1. ✅ OHLC-based intra-bar simulation
2. ✅ Bid/Ask spread modeling
3. ✅ Configurable slippage
4. ✅ Stop loss/take profit order within bar

**Implementation Files:**
- `include/tick_simulator.h` - Synthesize ticks from OHLC
- `include/spread_model.h` - Bid/ask spread simulation
- `include/slippage_model.h` - Realistic slippage
- Update `simple_price_level_breakout.h` to use High/Low

**Example Code:**
```cpp
// Check if SL hit before TP within same bar
void CheckIntraBarExecution(const Bar& bar) {
    if (position->direction == "BUY") {
        // Determine which happened first: SL or TP
        bool sl_hit = (bar.low <= position->sl);
        bool tp_hit = (bar.high >= position->tp);

        if (sl_hit && tp_hit) {
            // Both hit - need to determine order
            // Use bar direction heuristic
            if (bar.open > bar.close) {
                // Bearish bar: likely hit SL first
                CloseAt(position->sl, "SL");
            } else {
                // Bullish bar: likely hit TP first
                CloseAt(position->tp, "TP");
            }
        } else if (sl_hit) {
            CloseAt(position->sl, "SL");
        } else if (tp_hit) {
            CloseAt(position->tp, "TP");
        }
    }
}
```

### Tier 2: Synthetic Tick Generation (Hybrid)

**Timeline:** 1-2 weeks
**Accuracy:** 97-99% for intra-bar strategies
**Effort:** Medium

**Features to Add:**
1. Generate synthetic ticks from OHLC
2. Configurable tick density (ticks per bar)
3. Volume-weighted tick distribution
4. Time-based tick spacing

**Tick Generation Strategy:**
```
Bar: [Open, High, Low, Close]
Synthetic Ticks:
1. Open (t=0:00)
2. High (t=0:20, estimated)
3. Low (t=0:40, estimated)
4. Close (t=0:59)
+ Interpolated ticks between
```

**Files to Create:**
- `include/tick_generator.h`
- `include/tick_data_loader.h`
- `validation/test_tick_execution.cpp`

### Tier 3: True Tick Data (Production Grade)

**Timeline:** 2-3 weeks
**Accuracy:** 99.5%+ (matches MT5 tick-for-tick)
**Effort:** High

**Features to Add:**
1. Real tick data storage (binary format)
2. Tick data loader from MT5 exports
3. Tick-level order execution
4. Market depth simulation
5. True bid/ask spread from data

**Data Pipeline:**
```
MT5 Tick Export (.csv/.bin)
    ↓
C++ Tick Loader
    ↓
Tick Database (compressed)
    ↓
Backtest Engine (tick-by-tick)
    ↓
Results (same format as current)
```

**Files to Create:**
- `include/tick_data_manager.h`
- `include/tick_based_engine.h`
- `tools/mt5_tick_exporter.mq5`
- `tools/tick_data_converter.cpp`

---

## 🚀 Recommended Implementation Plan

### Week 1: Tier 1 Implementation

**Day 1-2: OHLC Tick Awareness**
- [ ] Create `tick_simulator.h`
- [ ] Add High/Low checking to exit logic
- [ ] Implement intra-bar SL/TP detection

**Day 3: Spread & Slippage**
- [ ] Create `spread_model.h`
- [ ] Implement fixed spread (2-3 pips for EURUSD)
- [ ] Add slippage simulation (0-2 pips random)

**Day 4-5: Testing & Validation**
- [ ] Rerun MT5 comparison with "Every tick" mode
- [ ] Compare results (target: <2% difference)
- [ ] Document findings

**Success Criteria:**
- ✅ Balance difference <2% vs MT5 "Every tick" mode
- ✅ Trade count matches MT5
- ✅ Entry/exit timing reasonable

### Week 2-3: Tier 2 (If Needed)

**Only if Tier 1 shows >3% difference**

- [ ] Implement tick synthesis algorithm
- [ ] Add configurable tick density
- [ ] Test with multiple strategies
- [ ] Optimize performance

### Week 4-6: Tier 3 (Optional)

**Only for high-frequency/scalping strategies**

- [ ] Build tick data pipeline
- [ ] Create MT5 tick exporter
- [ ] Implement tick database
- [ ] Full tick-level engine

---

## 📊 Validation Strategy

### Test Cases

**Test 1: Simple Scalping Strategy**
- Entry: Price crosses 20-period MA
- Exit: 5 pip TP, 3 pip SL
- Timeframe: M5
- Expected: Multiple entries per hour

**Test 2: Breakout with Tight Stops**
- Entry: Break of 15-min high/low
- Exit: 10 pip TP, 5 pip SL
- Timeframe: M15
- Expected: Quick stop-outs common

**Test 3: High-Frequency Reversion**
- Entry: Price deviates >0.5 std dev from MA
- Exit: Mean reversion or 3 pip SL
- Timeframe: M1
- Expected: 10-20 trades per hour

### Success Metrics

| Tier | Accuracy Target | Speed | Use Case |
|------|----------------|-------|----------|
| **Tier 1** | 95-98% | Fast (1x speed) | Intraday, swing |
| **Tier 2** | 97-99% | Medium (5x slower) | Scalping, day trading |
| **Tier 3** | 99.5%+ | Slow (100x slower) | HFT, ultra-precise |

---

## 💡 Key Design Decisions

### Decision 1: Tick Synthesis vs Real Ticks

**Recommendation: Start with Synthesis (Tier 1)**

**Rationale:**
- 95%+ accuracy is sufficient for 90% of strategies
- Much faster development (days vs weeks)
- No tick data storage requirements
- Easy to validate against MT5

**When to upgrade to Real Ticks:**
- Strategy requires <1 min holding periods
- Multiple entries/exits per bar
- Spread/slippage critically impacts results
- After Tier 1 validation shows >3% difference

### Decision 2: Spread Modeling

**Recommendation: Start with Fixed Spread**

**Fixed Spread per Symbol:**
- EURUSD: 2 pips (0.0002)
- GBPUSD: 3 pips
- USDJPY: 2 pips
- XAUUSD: 30 pips

**Later Enhancement: Dynamic Spread**
- Time-of-day based (wider during off-hours)
- Volatility-based (wider during news)
- Volume-based (tighter during high volume)

### Decision 3: Slippage Modeling

**Recommendation: Configurable Slippage**

```cpp
enum class SlippageMode {
    NONE,           // No slippage (current)
    FIXED,          // Fixed pips (e.g., 1 pip always)
    RANDOM,         // Random 0-2 pips
    VOLATILITY_BASED, // Based on ATR
    REALISTIC       // Time + volume + volatility
};

class SlippageModel {
    double CalculateSlippage(
        double volatility,
        int volume,
        TimeOfDay tod
    );
};
```

---

## 🔧 Code Structure

### New Files to Create

```
include/
├── tick_simulator.h         ✅ Tier 1
├── spread_model.h           ✅ Tier 1
├── slippage_model.h         ✅ Tier 1
├── tick_generator.h         ⏳ Tier 2
├── tick_data_manager.h      ⏳ Tier 3
└── tick_based_engine.h      ⏳ Tier 3

validation/
├── test_tick_execution.cpp  ✅ Tier 1 tests
├── test_tick_synthesis.cpp  ⏳ Tier 2 tests
└── test_real_ticks.cpp      ⏳ Tier 3 tests

tools/
├── mt5_tick_exporter.mq5    ⏳ Tier 3
└── tick_data_converter.cpp  ⏳ Tier 3
```

### Integration with Existing Code

**Minimal Changes Needed:**
- ✅ Current bar-based logic still works
- ✅ Add optional tick simulation layer
- ✅ Configurable via BacktestConfig
- ✅ Backward compatible

```cpp
// Enhanced BacktestConfig
struct BacktestConfig {
    // Existing fields...

    // NEW: Tick simulation options
    bool enable_tick_simulation = false;
    double spread_pips = 2.0;
    SlippageMode slippage_mode = SlippageMode::RANDOM;
    double max_slippage_pips = 2.0;
};
```

---

## 📈 Expected Results

### After Tier 1 Implementation

**Bar-Based Strategy (Current):**
- Accuracy: 98.35% ✅
- Speed: Fast
- Use case: Swing, position trading

**Enhanced with Tick Simulation (Tier 1):**
- Accuracy: 96-98% (estimated)
- Speed: Fast (minimal overhead)
- Use case: Intraday, some scalping

**Improvement Areas:**
- ✅ Intra-bar SL/TP execution
- ✅ Realistic spread/slippage
- ✅ Better order fill simulation
- ⚠️ Still approximate for sub-minute strategies

---

## ✅ Action Items

### Immediate (This Week)

1. [ ] Review this roadmap with team
2. [ ] Decide on Tier 1 vs Tier 2 vs Tier 3
3. [ ] Create `tick_simulator.h` header
4. [ ] Implement OHLC-based SL/TP checking
5. [ ] Add spread simulation
6. [ ] Retest against MT5 "Every tick" mode

### Short Term (Next 2 Weeks)

7. [ ] Complete Tier 1 implementation
8. [ ] Validate with 3-5 different strategies
9. [ ] Document accuracy improvements
10. [ ] Decide if Tier 2 needed

### Medium Term (Next 1-2 Months)

11. [ ] Implement Tier 2 if accuracy <95%
12. [ ] Build tick data pipeline if needed
13. [ ] Create comprehensive tick-based test suite
14. [ ] Production deployment

---

## 🎯 Recommendation Summary

**For Your Use Case (Real Tick-Based Strategies):**

1. **Start with Tier 1** (Enhanced Bar-Based)
   - Quick to implement (3-5 days)
   - 95-98% accuracy for most strategies
   - Validates if full tick data needed

2. **Upgrade to Tier 2** if needed
   - Only if Tier 1 shows >3% difference
   - Adds synthetic tick generation
   - 97-99% accuracy

3. **Consider Tier 3** for HFT only
   - Only for true high-frequency strategies
   - 99.5%+ accuracy
   - Significant development effort

**Next Immediate Step:**
Implement Tier 1 and rerun MT5 comparison to measure actual accuracy improvement for tick-based strategies.

---

**Questions to Answer:**
1. What holding period do your strategies typically use? (minutes, hours, days)
2. How many trades per day do you expect?
3. How critical is sub-pip accuracy?
4. Do you need to test scalping strategies (<5 min holds)?

Based on your answers, I can provide more specific guidance on which tier to prioritize.

---

**Created:** 2026-01-07
**Status:** Roadmap Complete - Awaiting Implementation Decision
**Next:** Implement Tier 1 (Enhanced Bar-Based) for validation
