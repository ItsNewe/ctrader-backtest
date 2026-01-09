# /validate-strategy

Validates a trading strategy by comparing C++ backtest results against MT5.

## Usage
```
/validate-strategy <strategy_name> [broker]
```

## Parameters
- `strategy_name`: Name of the strategy (e.g., "fill_up", "fill_up_gemini")
- `broker`: Optional broker name (default: "Broker"). Options: "Broker", "Grid"

## What it does
1. Checks for existing MT5 report in `validation/<broker>/<strategy>/`
2. Compiles and runs the C++ test: `validation/test_<strategy>.cpp`
3. Extracts key metrics from both:
   - Total trades
   - Net profit
   - Total swap
   - Win rate
4. Generates comparison report highlighting discrepancies

## Example
```
/validate-strategy fill_up Broker
```

## Output
```
=== Strategy Validation: fill_up (Broker) ===

| Metric        | C++       | MT5       | Diff     |
|---------------|-----------|-----------|----------|
| Total Trades  | 131,177   | 130,685   | +0.4%    |
| Net Profit    | $434,784  | $320,580  | +35.6%   |
| Total Swap    | -$49,607  | -$86,296  | -42.5%   |
| Win Rate      | 100%      | 97.81%    | +2.19%   |

Major discrepancies found in: Swap calculation
```

## Required files
- `validation/<broker>/<strategy>/ReportTester-*.xlsx` (MT5 report)
- `validation/test_<strategy>.cpp` (C++ test)
- `validation/<broker>/XAUUSD_TICKS_2025.csv` (tick data)
