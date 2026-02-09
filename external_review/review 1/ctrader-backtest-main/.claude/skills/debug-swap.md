# /debug-swap

Analyzes swap calculation differences between C++ and MT5.

## Usage
```
/debug-swap [broker]
```

## Parameters
- `broker`: Optional broker name (default: "Broker"). Options: "Broker", "Grid"

## What it does
1. Reads swap settings from MT5 log (INIT line)
2. Parses DAY_CHANGE events to extract daily swap amounts
3. Calculates per-lot swap and identifies triple swap days
4. Compares against C++ engine calculations
5. Identifies discrepancies in swap timing or amounts

## Key calculations
- **Swap mode 1 (SYMBOL_SWAP_MODE_POINTS)**: `swap_USD = swap_points × point × contract_size × lot_size`
  - For XAUUSD: point = 0.01, contract_size = 100
  - Example: -65.11 × 0.01 × 100 × 0.10 = -6.511 USD per 0.10 lot
- **Swap mode 2 (SYMBOL_SWAP_MODE_CURRENCY_SYMBOL)**: `swap_USD = swap_per_lot × lot_size`
- **Triple swap**: Applied on the day AFTER swap_3day (e.g., Thursday morning for swap_3day=3 Wednesday)
  - MT5 swap_3day=3 means: "charge 3x swap when crossing Wednesday night into Thursday"

## MT5 DAY_CHANGE Column Reference
The fill_up_sql_logger outputs DAY_CHANGE events at 01:00 each trading day:
- Column 8: total_lots (open position lots at swap time)
- Column 17: daily_swap (swap charged for that day)
- Column 18: day_name (Mon, Tue, Wed, Thu, Fri)

## Common issues
1. Triple swap charged on wrong day (Wed vs Thu) - **FIXED**: Now charges on (swap_3day + 1) % 7
2. Swap mode calculation mismatch
3. Point value differences (XAUUSD point = 0.01)
4. Contract size discrepancies (XAUUSD = 100)

## Broker Settings
| Broker | swap_long | swap_short | swap_mode | Terminal ID |
|--------|-----------|------------|-----------|-------------|
| Broker | -65.11    | 33.20      | 1 (pts)   | 5EC2F58E... |
| Grid | -66.99    | 41.20      | 1 (pts)   | 930119AA... |

## Key checks
- Is swap calculated at market open (~01:00)?
- Is triple swap applied on THURSDAY (day after swap_3day=3)?
- Are position lots correct at each rollover?
- Is swap mode correct (POINTS vs CURRENCY)?

## Output
```
=== Swap Analysis: Broker ===

Broker settings:
  swap_long: -65.11
  swap_short: 33.20
  swap_mode: 1 (POINTS)
  swap_3day: 3 (Wednesday close → Thursday charge)

Day-by-day analysis:
  2025.01.09 Thu: 0.08 lots, -15.60 swap, -195.00/lot (3.00x) *** TRIPLE
  2025.01.10 Fri: 0.09 lots, -5.85 swap, -65.00/lot (1.00x)
```

## Related files
- `validation/analyze_swap.py`
- `include/tick_based_engine.h` (ProcessSwap function)
- `example/fill_up_sql_logger.mq5` (DAY_CHANGE logging)
