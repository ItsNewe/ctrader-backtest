# Fill-Up Strategy Building Blocks

## Core Parameters
1. **survive_pct** - Target drawdown survival percentage [5, 6, 7, 8, 10, 13, 15, 20]
2. **spacing** - Grid spacing in dollars [0.5, 1.0, 1.5, 2.0]

## Protection Mechanisms

### A. Hedging (HEDGE)
- Open SHORT positions when price drops below trigger
- Close shorts when price recovers OR hits TP
- Parameters:
  - hedge_trigger_pct: [0.5, 1.0, 2.0] - % drop to activate hedging
  - hedge_ratio: [0, 0.1, 0.2, 0.3] - short size as ratio of long exposure

### B. Trailing Stop (TRAIL)
- Move stop loss as price moves favorably
- Parameters:
  - trail_start_pct: % profit before trailing activates
  - trail_distance_pct: distance behind peak

### C. Drawdown Protection (DD_PROT)
- Close all positions when drawdown exceeds threshold
- Parameters:
  - close_all_dd_pct: [50, 60, 70, 80] - DD% to trigger close all

### D. Time-Based Exit (TIME_EXIT)
- Close positions after X time without TP hit
- Parameters:
  - max_hold_hours: [24, 48, 72, 168]

### E. Velocity Filter (VEL_FILTER)
- Pause trading during rapid price movements
- Parameters:
  - crash_velocity_pct: [-0.5, -1.0, -1.5] - % drop per tick window
  - velocity_window: [100, 500, 1000] ticks

### F. Volatility Filter (VOL_FILTER)
- Adjust sizing or pause trading based on volatility
- Parameters:
  - vol_window: [1000, 5000] ticks
  - vol_threshold: multiple of normal

### G. Trend Filter (TREND)
- Only trade in direction of trend
- Parameters:
  - sma_period: [50, 100, 200] for trend detection
  - trend_mode: [with_trend, counter_trend, both]

### H. Session Filter (SESSION)
- Only trade during certain hours
- Parameters:
  - active_hours: [london, newyork, asian, all]

## Building Block Combinations to Test

### Priority 1: Core + Single Protection
- survive + HEDGE
- survive + DD_PROT
- survive + TRAIL
- survive + VEL_FILTER

### Priority 2: Core + Two Protections
- survive + HEDGE + DD_PROT
- survive + HEDGE + VEL_FILTER
- survive + DD_PROT + VEL_FILTER
- survive + TRAIL + DD_PROT

### Priority 3: Core + Three Protections
- survive + HEDGE + DD_PROT + VEL_FILTER
- survive + HEDGE + DD_PROT + TRAIL

## Test Matrix

| Config | survive | spacing | hedge | dd_prot | vel_filter | trail |
|--------|---------|---------|-------|---------|------------|-------|
| BASE   | 13      | 1.0     | 0     | OFF     | OFF        | OFF   |
| H1     | 6       | 1.0     | 0.1   | OFF     | OFF        | OFF   |
| H2     | 6       | 1.0     | 0.2   | OFF     | OFF        | OFF   |
| D1     | 13      | 1.0     | 0     | 50%     | OFF        | OFF   |
| D2     | 13      | 1.0     | 0     | 70%     | OFF        | OFF   |
| HD1    | 6       | 1.0     | 0.1   | 70%     | OFF        | OFF   |
| HD2    | 6       | 1.0     | 0.2   | 60%     | OFF        | OFF   |
| V1     | 13      | 1.0     | 0     | OFF     | -1.0%      | OFF   |
| HV1    | 6       | 1.0     | 0.1   | OFF     | -1.0%      | OFF   |
| HDV1   | 6       | 1.0     | 0.1   | 70%     | -1.0%      | OFF   |

## Expected Results Format

| Config | Return | Max DD | Margin Call | Trades | Notes |
|--------|--------|--------|-------------|--------|-------|
