# V6 Combined Exit Improvements Test

## Overview

This test evaluates how three individual exit improvements stack together:
1. **Time-based exit** at 50k ticks
2. **Close all at 15% DD** (instead of 25%)
3. **Wider TP at 2.0x spacing**

## Test Configurations

The test includes 13 configurations to evaluate:

### Core Comparisons
1. **V5 Baseline** - SMA 11000, TP=1.0x, CloseAll=25%, No Time Exit
2. **V6a** - V5 + Time Exit 50k only
3. **V6b** - V5 + CloseAll 15% only
4. **V6c** - V5 + Wider TP 2.0x only
5. **V6d** - V5 + Time Exit + CloseAll 15%
6. **V6e** - V5 + Time Exit + Wider TP
7. **V6f** - V5 + CloseAll 15% + Wider TP
8. **V6 Full** - V5 + All three improvements combined

### Parameter Variations
9. **Time Exit 30k** - Faster time exit
10. **Time Exit 75k** - Slower time exit
11. **CloseAll 18%** - Middle ground DD threshold
12. **CloseAll 20%** - Middle ground DD threshold
13. **TP 1.5x** - Middle ground TP spacing

## Running the Test

### Compilation
```bash
cd C:\Users\user\Documents\ctrader-backtest
cmake --build build --target test_v6_combined --config Release
```

### Execution
```bash
cd build\validation
test_v6_combined.exe > v6_combined_results.txt 2>&1
```

### Expected Runtime
- Data: ~5.5M ticks (Jan-Jun 2025 from XAUUSD_TICKS_2025.csv)
- Configurations: 13
- Estimated time: 30-60 minutes per configuration
- Total: **6-12 hours**

## Data Requirements

**File:** `C:/Users/user/Documents/ctrader-backtest/validation/Grid/XAUUSD_TICKS_2025.csv`
- Size: 2.4 GB
- Format: MT5 tick data (TAB-delimited)
- Date range: 2025.01.01 - 2025.07.01 (filtered in code)

## Expected Results Format

```
================================================================================
RESULTS SUMMARY - V6 COMBINED EXIT IMPROVEMENTS
================================================================================
Configuration                            Final Balance   Total P/L    Trades    Win Rate    Max DD          Stop Out
------------------------------------------------------------------------------------------------------------------------
V5 Baseline                             $XXXXX.XX       $XXXX.XX     XXX       XX.X%       $XXX.XX (X.X%)  NO
V6a: Time Exit 50k                      $XXXXX.XX       $XXXX.XX     XXX       XX.X%       $XXX.XX (X.X%)  NO [+X.X%]
V6b: CloseAll 15%                       $XXXXX.XX       $XXXX.XX     XXX       XX.X%       $XXX.XX (X.X%)  NO [+X.X%]
V6c: Wider TP 2.0x                      $XXXXX.XX       $XXXX.XX     XXX       XX.X%       $XXX.XX (X.X%)  NO [+X.X%]
V6d: Time 50k + CloseAll 15%            $XXXXX.XX       $XXXX.XX     XXX       XX.X%       $XXX.XX (X.X%)  NO [+X.X%]
V6e: Time 50k + TP 2.0x                 $XXXXX.XX       $XXXX.XX     XXX       XX.X%       $XXX.XX (X.X%)  NO [+X.X%]
V6f: CloseAll 15% + TP 2.0x             $XXXXX.XX       $XXXX.XX     XXX       XX.X%       $XXX.XX (X.X%)  NO [+X.X%]
V6 FULL: All three improvements         $XXXXX.XX       $XXXX.XX     XXX       XX.X%       $XXX.XX (X.X%)  NO [+X.X%]
...

================================================================================
ANALYSIS
================================================================================

Individual Improvements vs V5 Baseline:
  Time Exit 50k:    +X.X%
  CloseAll 15%:     +X.X%
  Wider TP 2.0x:    +X.X%

Stacking Analysis:
  Expected (Additive):         +X.X%
  Expected (Multiplicative):   +X.X%
  Actual (V6 Full):            +X.X%

  Stacking Behavior: [ADDITIVE|MULTIPLICATIVE]

================================================================================
RECOMMENDATION
================================================================================

Best Configuration: [Configuration Name]
  Final Balance:  $XXXXX.XX
  Total P/L:      $XXXX.XX
  Trades:         XXX
  Win Rate:       XX.X%
  Max Drawdown:   $XXX.XX (X.X%)
  Improvement:    +X.X%

================================================================================
```

## Critical Questions to Answer

1. **Stacking Behavior**: Do the improvements stack:
   - Additively (sum of individual improvements)?
   - Multiplicatively (product of individual improvements)?
   - Or do they interfere with each other?

2. **Optimal Combination**: Which combination provides the best risk-adjusted returns?

3. **Parameter Sensitivity**: Are the intermediate values (30k/75k time exit, 18%/20% DD, 1.5x TP) better than the extremes?

4. **V6 Final Parameters**: What should be the recommended configuration for V6?

## Troubleshooting

If the test hangs or crashes:

1. **Memory**: Ensure at least 8GB RAM available
2. **Data File**: Verify XAUUSD_TICKS_2025.csv exists and is readable
3. **Disk Space**: Ensure sufficient space for streaming I/O
4. **Antivirus**: May need to exclude the executable from real-time scanning

If the test runs too slowly:

1. Reduce date range in code (e.g., test only Jan-Mar 2025)
2. Reduce number of configurations
3. Enable compiler optimizations (`-O3`)

## Next Steps After Results

1. **Analyze stacking behavior** - Determine if improvements are independent
2. **Identify best configuration** - Compare risk-adjusted returns
3. **Validate on out-of-sample data** - Test on July-Dec 2025
4. **Forward test** - Test on live/demo account before production

## Files

- **Source**: `validation/test_v6_combined.cpp`
- **Executable**: `build/validation/test_v6_combined.exe`
- **CMake**: `validation/CMakeLists.txt`
- **Data**: `validation/Grid/XAUUSD_TICKS_2025.csv`
