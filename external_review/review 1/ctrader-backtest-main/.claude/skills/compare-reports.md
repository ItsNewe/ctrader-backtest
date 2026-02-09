# /compare-reports

Compares two MT5 backtest reports side-by-side.

## Usage
```
/compare-reports <report1> <report2>
```

## Parameters
- `report1`: Path to first MT5 xlsx report
- `report2`: Path to second MT5 xlsx report

## What it does
1. Extracts key metrics from both reports:
   - Net profit
   - Total trades
   - Win rate
   - Total swap
   - Max drawdown
   - Gross profit/loss
2. Shows side-by-side comparison
3. Highlights significant differences

## Example
```
/compare-reports validation/Broker/fill_up/ReportTester-000000.xlsx validation/Grid/fill_up/ReportTester-000000.xlsx
```

## Output
```
=== Report Comparison ===

| Metric          | Broker        | Grid        | Diff      |
|-----------------|---------------|---------------|-----------|
| Net Profit      | $320,579.99   | $417,150.88   | +30.1%    |
| Total Trades    | 130,685       | 145,468       | +11.3%    |
| Win Rate        | 97.81%        | 98.05%        | +0.24%    |
| Total Swap      | -$86,296      | -$90,631      | -5.0%     |
| Gross Profit    | $352,239.15   | $456,782.88   | +29.7%    |
| Gross Loss      | -$31,659.16   | -$39,632.00   | -25.2%    |

Key Differences:
- Grid has 11% more trades (different tick data?)
- Grid profits 30% more despite higher swap
- Both have similar win rates (~98%)
```

## Related files
- `validation/extract_swap_total.py`
- `validation/extract_mt5_report.py`
