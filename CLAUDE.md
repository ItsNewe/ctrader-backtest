# Strategy Backtesting Framework (Minimal)

> **Note**: This is a BACKTESTING framework. Live trading components (ctrader_connector.h,
> metatrader_connector.h, live_trading_bridge.h) are incomplete stubs. For live trading,
> use the MQL5 EAs in `/mt5/` directly in MetaTrader 5.

## Build Template

```cpp
#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
using namespace backtest;

int main() {
    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;

    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = 10000.0;
    config.contract_size = 100.0;
    config.leverage = 500.0;
    config.pip_size = 0.01;
    config.swap_long = -66.99;
    config.swap_short = 41.2;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = "2025.01.01";
    config.end_date = "2025.12.30";
    config.tick_data_config = tick_config;

    TickBasedEngine engine(config);
    FillUpOscillation strategy(13.0, 1.5, 0.01, 10.0, 100.0, 500.0,
        FillUpOscillation::ADAPTIVE_SPACING, 0.1, 30.0, 4.0);

    engine.Run([&strategy](const Tick& tick, TickBasedEngine& engine) {
        strategy.OnTick(tick, engine);
    });

    auto results = engine.GetResults();
    std::cout << "Final: $" << results.final_balance << std::endl;
    return 0;
}
```

Build: `cd build && cmake .. -G "MinGW Makefiles" && mingw32-make test_name`

---

## Broker Settings

| Setting | XAUUSD | XAGUSD |
|---------|--------|--------|
| contract_size | 100.0 | 5000.0 |
| leverage | 500.0 | 500.0 |
| swap_long | -66.99 | -15.0 |
| swap_short | +41.2 | +13.72 |
| pip_size | 0.01 | 0.001 |

Tick data: `validation/Grid/XAUUSD_TICKS_2025.csv` (51.7M), `XAGUSD_TESTER_TICKS.csv` (29.3M)

---

## Best Parameters

### XAUUSD (FillUpOscillation ADAPTIVE)
```
survive_pct = 13.0%, base_spacing = $1.50, lookback = 4h, typical_vol = 0.55%
Result: 6.57x (2025), 12.83x (2-year), 67% DD
```

### XAGUSD (Requires pct_spacing=true)
```
survive_pct = 19.0%, base_spacing_pct = 2.0%, lookback = 1h, typical_vol = 0.45%
Result: 43.4x (2025), 29% DD
```

### CombinedJu (Highest Returns, Crash-Safe)
```
survive = 12%, spacing = $1.0, tp_mode = LINEAR, sizing = UNIFORM, velocity = ON
ForceMinVolumeEntry = false  # CRITICAL for crash survival
Result: 22.11x (2025), 83% DD
```

---

## Critical Rules

1. **Always use TickBasedEngine** - Never standalone strategy classes
2. **ForceMinVolumeEntry = false** for production (crash survival)
3. **ADAPTIVE_SPACING mode only** for forced entry (other modes crash)
4. **pct_spacing = true** required for XAGUSD (price tripled)
5. **LINEAR sizing, not THRESHOLD** for crash safety

---

## Parallel Parameter Sweeps

**DO NOT use std::thread for parallel testing.** Use the built-in `RunWithTicks()` pattern:

```cpp
// 1. Load ticks ONCE into memory
std::vector<Tick> ticks = LoadTicks(file_path, start_date, end_date);

// 2. For each config, create fresh engine and use RunWithTicks()
for (const auto& params : configs) {
    TickBasedEngine engine(config);
    MyStrategy strategy(params);

    engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    results.push_back(engine.GetResults());
}
```

Key points:
- `config.verbose = false` to suppress logging during sweeps
- Ticks are shared read-only across all tests (no data races)
- See `test_parallel_dual_sweep.cpp` for full example with tick loading

---

## Key Files

| Purpose | File |
|---------|------|
| Primary strategy | `include/fill_up_oscillation.h` |
| CombinedJu | `include/strategy_combined_ju.h` |
| Engine | `include/tick_based_engine.h` |
| MT5 EA | `mt5/CombinedJu_EA.mq5`, `mt5/FillUpAdaptive_v4.mq5` |

---

## Archives (Full Documentation)

Detailed logs in `.claude/memory/`:
- `ARCHIVE_INVESTIGATIONS.md` - Research findings, parameter sweeps
- `ARCHIVE_METHODOLOGY.md` - 6-step parameter selection process
- `ARCHIVE_PHILOSOPHY.md` - Theoretical foundations
