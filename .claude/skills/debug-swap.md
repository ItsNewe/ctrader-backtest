# /debug-swap

Analyzes swap calculation differences between C++ and MT5.

## Usage
```
/debug-swap [broker]
```

## Parameters
- `broker`: Optional broker name (default: "Broker")

## What it does
1. Extracts total swap from MT5 report using `extract_swap_total.py`
2. Runs C++ backtest and captures swap log output
3. Compares:
   - Total swap amount
   - Swap per day
   - Position-days (lots × days held)
   - Triple swap days
4. Identifies root cause of discrepancy

## Key checks
- Is swap calculated at market open (00:00)?
- Is triple swap applied on correct day (Wednesday)?
- Are position lots correct at each rollover?
- Is swap mode correct (POINTS vs CURRENCY)?

## Output
```
=== Swap Debug Analysis ===

MT5 Total Swap:  -$86,296
C++ Total Swap:  -$49,607
Difference:      -$36,689 (42.5% less in C++)

Per-day analysis:
- MT5 avg daily swap:  -$234.50
- C++ avg daily swap:  -$134.75
- Difference ratio:    1.74x

Root cause: C++ has fewer position-lots at rollover time
Recommendation: Compare position counts at each midnight
```

## Related files
- `validation/extract_swap_total.py`
- `include/tick_based_engine.h` (ProcessSwap function)
