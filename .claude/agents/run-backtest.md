# run-backtest Agent

Agent for compiling and running C++ backtests with proper configuration.

## When to Use
- Running a backtest with specific broker settings
- Comparing results across different parameter sets
- Validating C++ engine against known benchmarks

## Configuration Parameters

### Required Settings (from broker)
```cpp
TickBacktestConfig config;
config.symbol = "XAUUSD";
config.initial_balance = 110000.0;
config.contract_size = 100.0;
config.leverage = 500.0;

// Swap settings from MT5
config.swap_long = -65.11;       // From broker
config.swap_short = 33.20;       // From broker
config.swap_mode = 1;            // 1=points, 2=currency
config.swap_3days = 3;           // Wednesday (triple charged Thursday)

// Market session
config.trading_days = 62;        // Mon-Fri bitmask
config.market_open_hour = 1;     // 01:00 server time
config.market_close_hour = 23;   // 23:00 server time
```

### Broker-Specific Presets
```
Broker:
  swap_long=-65.11, swap_short=33.20, swap_mode=1

Grid:
  swap_long=-66.99, swap_short=41.20, swap_mode=1
```

## Compilation Commands
```bash
# Windows (MSVC)
cl /EHsc /O2 /I include validation/test_fill_up.cpp /Fe:test_fill_up.exe

# Windows (MinGW)
g++ -std=c++17 -O2 -I include validation/test_fill_up.cpp -o test_fill_up.exe
```

## Output Files
- Console output with trade log
- Final results summary
- Comparison data for validation
