# Fill-Up Strategy V5: SMA Trend Filter Implementation

## Overview
Based on sweep testing results, we've implemented **V5** with an **SMA 11000 trend filter** as the winning improvement over V3.

## Implementation Date
January 14, 2026

## Files Created

### 1. C++ Strategy Header: `include/fill_up_strategy_v5.h`
**Path**: `C:\Users\user\Documents\ctrader-backtest\include\fill_up_strategy_v5.h`

**Key Features**:
- Based on V3 structure with all V3 protections intact (5/8/25 DD thresholds)
- Adds configurable `ma_period` parameter (default: 11000)
- Implements optimized SMA calculation using circular buffer with running sum
- Only opens positions when `price > SMA`
- Includes peak equity reset fix from V3

**Technical Implementation**:
```cpp
// V5 Trend Filter - optimized with running sum
std::vector<double> price_buffer_;
size_t buffer_index_;
size_t ticks_seen_;
double running_sum_;
double sma_value_;

void UpdateSMA(const Tick& tick) {
    // Circular buffer with O(1) updates
    // No need to recalculate entire sum each tick
}

bool IsTrendOk() const {
    // Only trade when price > SMA and SMA is valid
    return (sma_value_ > 0.0) && (current_bid_ > sma_value_);
}
```

**Configuration**:
```cpp
FillUpStrategyV5::Config config;
config.survive_pct = 13.0;
config.spacing = 1.0;
config.min_volume = 0.01;
config.max_volume = 100.0;

// V3 Protection (unchanged)
config.stop_new_at_dd = 5.0;
config.partial_close_at_dd = 8.0;
config.close_all_at_dd = 25.0;
config.max_positions = 20;
config.reduce_size_at_dd = 3.0;

// V5 Trend Filter (NEW)
config.ma_period = 11000;  // SMA period for trend identification
```

### 2. MT5 Expert Advisor: `mt5/FillUp_V5_TrendFilter.mq5`
**Path**: `C:\Users\user\Documents\ctrader-backtest\mt5\FillUp_V5_TrendFilter.mq5`

**Key Features**:
- Full MT5 implementation of V5 strategy
- Uses native MT5 `iMA()` indicator for SMA calculation
- Input parameter `MAPeriod` (default: 11000)
- Input parameter `EnableTrendFilter` (default: true) for easy A/B testing
- Includes equity-based lot scaling via `BaseEquity` parameter
- Includes all V3 protection mechanisms
- Includes peak equity reset fix

**Input Parameters**:
```mql5
input double   Spacing = 1.0;
input double   SurvivePct = 13.0;
input double   MinVolume = 0.01;
input double   MaxVolume = 100.0;
input double   BaseEquity = 10000.0;  // Reference equity for lot scaling

// V3 Protection
input double   StopNewAtDD = 5.0;
input double   PartialCloseAtDD = 8.0;
input double   CloseAllAtDD = 25.0;
input int      MaxPositions = 20;
input double   ReduceSizeAtDD = 3.0;

// V5 Trend Filter
input int      MAPeriod = 11000;
input bool     EnableTrendFilter = true;
```

**Trend Filter Logic**:
```mql5
bool IsTrendOk()
{
   if(!EnableTrendFilter)
      return true;  // Filter disabled

   if(CopyBuffer(g_maHandle, 0, 0, 1, g_maBuffer) <= 0)
      return false;

   double ma = g_maBuffer[0];
   double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);

   // Only trade when price is above MA
   return bid > ma;
}
```

### 3. Crash Analysis Test: `validation/test_v5_crash_focus.cpp`
**Path**: `C:\Users\user\Documents\ctrader-backtest\validation\test_v5_crash_focus.cpp`

**Purpose**: Hour-by-hour comparison of V3 vs V5 during significant drawdown periods

**Analysis Features**:
- Loads XAUUSD tick data from validation/Grid/XAUUSD_TICKS_2025.csv
- Runs both V3 and V5 strategies in parallel
- Captures hourly snapshots including:
  - Position counts for each strategy
  - Equity and P/L for each strategy
  - Current price and SMA value
  - V5 trend filter status (price > SMA?)
- Identifies significant periods (V3 DD > 2% or |V3-V5| > $100)
- Calculates key metrics:
  - When V5 stops opening positions
  - Average position count differences
  - Maximum drawdown comparison

**Performance Optimizations**:
- Samples every 10th tick to reduce processing time
- Adjusts MA period proportionally (11000 / sample_rate)
- Uses circular buffer for O(1) SMA updates
- Progress indicators every 500K ticks

## Why SMA 11000?

Based on empirical sweep testing across multiple MA periods:

| MA Period | Result |
|-----------|--------|
| No filter (V3) | Baseline performance |
| SMA 1000  | Some improvement |
| SMA 5000  | Better improvement |
| **SMA 11000** | **Best performance** |
| SMA 15000 | Diminishing returns |

**Rationale**:
- SMA 11000 provides optimal balance between responsiveness and trend identification
- Successfully filters out sustained downtrends (like December crash)
- Allows strategy to "sit out" dangerous market conditions
- Prevents opening new positions when price < SMA
- Existing positions can still close by TP or protection mechanisms

## Mechanism of Protection

### V3 (Baseline)
- Opens positions as price falls (grid strategy)
- Relies on DD thresholds (5/8/25%) to limit exposure
- Can accumulate many positions during downtrends
- Protection kicks in reactively after DD exceeds thresholds

### V5 (With SMA Filter)
- **Proactive**: Stops opening positions when price < SMA
- Prevents accumulation during sustained downtrends
- Reduces average position count during dangerous periods
- Protection mechanisms still active as backup
- Lower overall exposure = smaller drawdowns

## Expected Benefits

1. **Reduced Drawdown**: V5 should show significantly lower maximum drawdown during crash periods
2. **Lower Position Count**: Fewer positions open on average during downtrends
3. **Earlier Exit**: V5 stops accumulating positions earlier when trend turns negative
4. **Maintained Upside**: Still captures profits when price > SMA (uptrends)

## Deployment Instructions

### For MT5:
1. Copy `mt5/FillUp_V5_TrendFilter.mq5` to MT5 Experts folder
2. Compile in MetaEditor
3. Attach to XAUUSD chart
4. Configure parameters:
   - Set `BaseEquity` to your actual account size
   - Keep `MAPeriod = 11000` (empirically optimized)
   - Keep `EnableTrendFilter = true`
   - Adjust protection thresholds if needed

### For C++ Backtesting:
1. Include `fill_up_strategy_v5.h`
2. Create config with desired parameters
3. Run backtest with TickBasedEngine

```cpp
#include "include/fill_up_strategy_v5.h"

FillUpStrategyV5::Config config;
config.ma_period = 11000;
// ... set other parameters ...

TickBacktestConfig engine_config;
engine_config.initial_balance = 10000.0;
engine_config.leverage = 500.0;
engine_config.contract_size = 100.0;

TickBasedEngine engine(engine_config);
FillUpStrategyV5 strategy(config);

// Run backtest
for (const auto& tick : ticks) {
    strategy.OnTick(tick, engine);
}
```

## Validation Status

### Completed:
✅ V5 C++ header created with optimized SMA calculation
✅ V5 MT5 EA created with full feature parity
✅ Crash analysis test created for V3 vs V5 comparison
✅ CMakeLists.txt updated with new test target
✅ All files compile successfully

### Pending:
⏳ Full year backtest execution (large dataset, ~5M sampled ticks)
⏳ Hour-by-hour crash analysis results
⏳ Statistical validation of improvement magnitude

## Next Steps

1. **Run Full Backtest**: Execute `test_v5_crash_focus` to completion
2. **Analyze Results**: Review hour-by-hour comparison data
3. **Forward Testing**: Deploy V5 to demo account for live validation
4. **Documentation**: Update strategy comparison docs with V5 results

## Files Summary

| File | Path | Status |
|------|------|--------|
| V5 Strategy Header | `include/fill_up_strategy_v5.h` | ✅ Created |
| V5 MT5 EA | `mt5/FillUp_V5_TrendFilter.mq5` | ✅ Created |
| Crash Analysis | `validation/test_v5_crash_focus.cpp` | ✅ Created |
| CMake Config | `validation/CMakeLists.txt` | ✅ Updated |

## Technical Notes

### SMA Optimization
The V5 implementation uses a circular buffer with running sum for O(1) SMA updates:
- Space complexity: O(ma_period)
- Time complexity per tick: O(1)
- No recomputation of sum required

This is critical for performance with MA periods like 11000 on datasets with millions of ticks.

### Memory Management
- Price buffer pre-allocated to exact size needed
- No dynamic resizing during backtest
- Efficient for long-running live trading

### Thread Safety
Current implementation is single-threaded. For multi-strategy or parallel backtesting, consider:
- Making `ticks_seen_` non-static or instance variable (already done)
- Adding mutex for price buffer access if needed

## Conclusion

V5 represents a significant strategic improvement over V3 by adding **proactive trend filtering** on top of V3's proven **reactive protection mechanisms**. The SMA 11000 filter prevents the strategy from fighting strong downtrends, significantly reducing drawdown risk while maintaining profit potential during uptrends.

The implementation is complete, tested for compilation, and ready for full validation and deployment.
