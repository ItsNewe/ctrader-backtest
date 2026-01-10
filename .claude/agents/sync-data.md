# sync-data Agent

Agent for synchronizing tick data and broker settings between MT5 and C++.

## When to Use
- Setting up a new broker in the backtesting system
- Refreshing tick data for a symbol
- Extracting broker-specific settings from MT5

## Data Sync Tasks

### 1. Tick Data Export
```mql5
// Use HistoryExport EA to export ticks from MT5
// Output: validation/<broker>/XAUUSD_TICKS_2025.csv
```

### 2. Broker Settings Extraction
Extract from MT5:
- swap_long, swap_short
- swap_mode (1=points, 2=currency)
- swap_3day (triple swap day)
- contract_size
- leverage
- commission structure

### 3. Terminal ID Mapping
Track which MT5 terminal corresponds to which broker:
- grid: 930119AA53207C8778B41171FBFFB46F
- broker: 5EC2F58E016C45833021192C9165146B

### 4. Data Validation
- Check tick data completeness
- Verify date ranges
- Compare bid/ask spreads
- Validate trading hours

## Directory Structure
```
validation/
  Grid/
    fill_up/
      fill_up_log_XAUUSD.csv    # SQL logger output
      ReportTester-*.xlsx       # MT5 backtest report
    XAUUSD_TICKS_2025.csv       # Tick data
  Broker/
    fill_up/
      fill_up_log_XAUUSD.csv
      ReportTester-*.xlsx
    XAUUSD_TICKS_2025.csv
```
