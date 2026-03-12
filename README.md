# ctrader-backtest

Tick-based backtesting engine in C++ with a React dashboard and FastAPI backend.

## Prerequisites

- C++17 compiler (GCC, Clang, or MSVC)
- CMake 3.15+
- Python 3.10+
- Node.js 18+
- [uv](https://docs.astral.sh/uv/) (Python package manager)

## Quick start

```bash
make install    # install Python + Node dependencies
make build      # CMake configure + build
make dev        # start API (port 8000) + dashboard (port 5173)
```

Open http://localhost:5173.

Run `make help` for all targets.

## Project layout

```
include/              C++ headers (strategies, engine, risk, margin)
validation/           C++ test sources
api/                  FastAPI backend (backtest orchestration, sweeps, data)
dashboard/            React + Vite frontend
mt5/                  MetaTrader 5 Expert Advisors for live trading
tests/                Parameter sweep scripts
```

### Key files

| File | Purpose |
|------|---------|
| `include/tick_based_engine.h` | Core backtest engine |
| `include/fill_up_oscillation.h` | Primary strategy (adaptive grid) |
| `include/strategy_combined_ju.h` | CombinedJu strategy |
| `mt5/FillUpAdaptive_v4.mq5` | MT5 EA for live trading |
| `mt5/CombinedJu_EA.mq5` | MT5 EA for live trading |

## Strategies

**FillUpOscillation** — adaptive grid that sizes positions to survive a configurable drawdown, with volatility-based spacing.

**CombinedJu** — rubber band TP + velocity filter + barbell sizing. Highest raw returns but aggressive drawdown.

Both strategies are header-only and run inside `TickBasedEngine` via a callback:

```cpp
engine.Run([&strategy](const Tick& tick, TickBasedEngine& engine) {
    strategy.OnTick(tick, engine);
});
```

See `CLAUDE.md` for build templates, broker settings, and best-known parameters.

## Tick data

Place MT5-exported tick CSVs in `validation/Grid/` (e.g. `XAUUSD_TICKS_2025.csv`). The engine auto-detects files matching this naming convention, or set `BACKTEST_DATA_DIR`.

## License

MIT
