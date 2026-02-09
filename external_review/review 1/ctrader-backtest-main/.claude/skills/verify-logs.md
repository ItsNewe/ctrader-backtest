# /verify-logs

Verifies C++ backtest output against MT5 SQL-style logs tick-by-tick.

## Usage
```
/verify-logs <mt5_log> [--verbose]
```

## Parameters
- `mt5_log`: Path to MT5 log file (from `fill_up_sql_logger.mq5`)
- `--verbose`: Show all discrepancies, not just summary

## What it does
1. Reads MT5 CSV log file
2. Runs C++ backtest with same tick data
3. Compares events tick-by-tick:
   - Trade opens (price, lot size, TP)
   - Trade closes (exit price, profit)
   - Day changes (position counts)
   - Swap charges (amount, lots)
4. Reports first discrepancy with full context

## MT5 Log Format (CSV)
```
event_type,timestamp,bid,ask,balance,equity,open_positions,total_lots,trade_id,...
TICK,2025.01.02 00:00:15,2634.50,2634.75,110000.00,110000.00,0,0,...
TRADE_OPEN,2025.01.02 00:00:15,2634.50,2634.75,110000.00,109998.00,1,0.01,1,BUY,0.01,2634.75,...
DAY_CHANGE,2025.01.03 00:00:00,2640.00,2640.25,110050.00,110045.00,5,0.05,...,Wed
```

## Output
```
=== Log Verification ===

Processed: 5,000,000 ticks
Events compared:
  - Trade opens:  131,177 (100% match)
  - Trade closes: 130,685 (100% match)
  - Day changes:  365 (100% match)
  - Swap charges: 365 (98.6% match - 5 discrepancies)

First discrepancy at 2025.03.15 00:00:00:
  MT5:  Swap = -$45.67 for 2.5 lots
  C++:  Swap = -$44.89 for 2.45 lots
  Diff: Lot count mismatch (trade closed just before midnight?)
```

## Related files
- `example/fill_up_sql_logger.mq5` (generates MT5 logs)
- C++ verification script (to be created)
