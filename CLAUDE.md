# Strategy Backtesting Framework

## Quality Standards

```
Thorough but concise. Detailed but readable. Technical but accessible.
Precise and correct. Evidence-based. Assumptions stated.
```

---

## Table of Contents

1. [Architecture & Build Guide](#1-architecture--build-guide)
2. [Instrument Reference](#2-instrument-reference)
3. [Strategy Reference](#3-strategy-reference)
4. [Production Strategies](#4-production-strategies)
5. [Critical Findings](#5-critical-findings)
6. [Validated Dead Ends](#6-validated-dead-ends)
7. [MT5 Reference](#7-mt5-reference)
8. [Test Files](#8-test-files)
9. [Advanced Features](#9-advanced-features)
10. [Quick Reference](#10-quick-reference)

**Archives** (detailed investigation logs):
- `.claude/memory/ARCHIVE_INVESTIGATIONS.md` - Research findings
- `.claude/memory/ARCHIVE_METHODOLOGY.md` - Parameter selection process
- `.claude/memory/ARCHIVE_PHILOSOPHY.md` - Theoretical foundations

---

## 1. Architecture & Build Guide

### Architecture Overview

```
TickBasedEngine (include/tick_based_engine.h)
├── Swap calculation (daily, triple on Wed/Thu)
├── Margin stop-out (20% level)
├── Date filtering (MT5-style)
└── Position tracking & P/L
         ↑
    Strategy Callback (OnTick)
         ↑
Strategy Class (entry/exit/sizing/DD protection)
```

**NEVER** create standalone strategy classes. Always use `TickBasedEngine`.

### Core Files (DO NOT MODIFY)

```
include/tick_based_engine.h      # Main engine
include/tick_data.h              # Tick structure
include/tick_data_manager.h      # Streaming tick reader
```

### Test Template

```cpp
#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"

using namespace backtest;

int main() {
    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

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

### Build & Run

```bash
cd build
cmake .. -G "MinGW Makefiles"
mingw32-make test_my_strategy
./validation/test_my_strategy.exe
```

---

## 2. Instrument Reference

### Tick Data Available

```
validation/Grid/XAUUSD_TICKS_2025.csv     # 51.7M ticks
validation/Grid/XAUUSD_TICKS_2024.csv     # Full year 2024
mt5/fill_up_xagusd/XAGUSD_TESTER_TICKS.csv  # 29.3M ticks, $28.91-$96.17
validation/NAS100/NAS100_TICKS_2025.csv     # NAS100 2025
```

### Broker Settings

| Setting | XAUUSD | XAGUSD | XPTUSD | XPDUSD |
|---------|--------|--------|--------|--------|
| contract_size | 100.0 | 5000.0 | 100.0 | 100.0 |
| leverage | 500.0 | 500.0 | 500.0 | 500.0 |
| swap_long | -66.99 | -15.0 | -43.73 | -17.64 |
| swap_short | +41.2 | +13.72 | +3.11 | -11.60 |
| pip_size | 0.01 | 0.001 | 0.01 | 0.01 |

### Instrument Suitability

| Instrument | Suitability | Notes |
|------------|-------------|-------|
| **XAUUSD** | EXCELLENT | Primary - 6.57x baseline |
| **XAGUSD** | EXCELLENT | Best 2-year (73.68x), requires pct_spacing |
| XPTUSD | VIABLE | High regime dependence (16.7x ratio) |
| XPDUSD | MARGINAL | Only 25% survive works |
| ETHUSD | NOT VIABLE | 100% stop-out |
| USDJPY | POOR | Insufficient oscillation |

---

## 3. Strategy Reference

### FillUpOscillation (Primary Strategy)

```cpp
FillUpOscillation strategy(
    13.0,   // survive_pct - must exceed max adverse move (~11-12%)
    1.5,    // base_spacing
    0.01,   // min_volume
    10.0,   // max_volume
    100.0,  // contract_size
    500.0,  // leverage
    FillUpOscillation::ADAPTIVE_SPACING,  // RECOMMENDED
    0.1,    // antifragile_scale (unused)
    30.0,   // velocity_threshold (unused)
    4.0     // volatility_lookback_hours
);
```

### Available Modes

| Mode | Return | Status |
|------|--------|--------|
| BASELINE | ~4.5x | Reference only |
| **ADAPTIVE_SPACING** | **6.57x** | **RECOMMENDED** |
| VELOCITY_FILTER | ~5.5x | Helps flash crashes |
| TREND_ADAPTIVE | 8.13x | Bull-market bias |

### Performance Summary (2025)

| Metric | Value |
|--------|-------|
| Return | 6.57x ($65,700) |
| Max DD | 66.89% |
| Sharpe | 11.39 |
| Win Rate | 100% |
| Trades | ~10,334 |
| Swap Cost | -$6,795 |

---

## 4. Production Strategies

### CombinedJu (Highest Returns)

Combines: Rubber Band TP + Velocity Zero Filter + Threshold Barbell Sizing

**Best configs (2025, force=OFF for crash safety):**

| Preset | Return | DD | Use Case |
|--------|--------|-----|----------|
| CombinedJu_XAUUSD_Best.set | 22.11x | 83.4% | Maximum return |
| CombinedJu_XAUUSD_THR3.set | 20.88x | 83.8% | Balanced |
| CombinedJu_XAUUSD_Conservative.set | 13.05x | 66.6% | Lowest DD |

**CRITICAL:** Use `ForceMinVolumeEntry=false` for crash survival.

### FillUpOscillation ADAPTIVE (Most Robust)

| Config | 2024 | 2025 | 2-Year | DD |
|--------|------|------|--------|-----|
| s13, sp$1.50, lb4h | 2.10x | 6.59x | 12.83x | ~67% |

### XAGUSD (Percentage Spacing Required)

| Config | Return | DD |
|--------|--------|-----|
| s19_sp2.0%_lb1 | 43.4x | 29.3% |
| s18_sp2.0%_lb1 | 115.2x | 36.9% |

---

## 5. Critical Findings

### Forced Entry: Mode-Specific

✅ **ENABLE forced entry for:**
- `ADAPTIVE_SPACING` mode (+144% to +204%)
- `VELOCITY_FILTER` mode

❌ **DO NOT enable for:**
- `BASELINE`, `ADAPT_LB`, `ANTIFRAG`, `TREND` - causes 99%+ loss

### Forced Entry vs Crash Safety

| force | Return | Crash Survival |
|-------|--------|----------------|
| ON | 20.04x avg | 100% FAIL |
| OFF | 11.14x avg | 100% SURVIVE |

**44% profit sacrifice = survival insurance**

### The DD Trade-off

Every -1% DD reduction costs ~3% return. This is fundamental - the grid's profit mechanism IS its DD mechanism.

### Key Performance Metrics

| Period | Return | Oscillations |
|--------|--------|--------------|
| 2024 | 2.10x | ~92,000 |
| 2025 | 6.59x | ~281,000 |
| 2-Year | 12.83x | - |

---

## 6. Validated Dead Ends

| Approach | Result | Lesson |
|----------|--------|--------|
| Entry filters (MomentumReversal) | 0% H2 survival | Pure overfitting |
| 4h time exit | 1.32x (vs 6.57x) | Kills profitable positions |
| DD reduction (pause/cap/stop) | -38% return per -14% DD | DD IS profit mechanism |
| Trailing stops (768 configs) | None beat simple TP | Unnecessary complexity |
| Auto-calibration | -6% to -47% vs manual | Early data doesn't generalize |
| Shannon's Demon | 0.90x | Rebalancing costs > benefits |
| Stochastic Resonance | 0.37x | Too selective |
| 5 dynamic models | All failed | Simple heuristics win |
| USDJPY | 0.06x | Insufficient oscillation |
| force_min_volume_entry in crashes | 98% loss | Bypasses margin safety |
| LINEAR barbell sizing | 97% loss | Never use |

---

## 7. MT5 Reference

### Canonical EA Files

| EA | Purpose |
|----|---------|
| **`mt5/CombinedJu_EA.mq5`** | PRODUCTION - CombinedJu |
| `mt5/FillUpAdaptive_v4.mq5` | XAUUSD/XAGUSD ADAPTIVE |
| `mt5/CombinedJu_ForcedEntry.mq5` | Testing only |

### MT5 Validation Checklist

- [ ] Uses `TickBasedEngine` (not standalone)
- [ ] Swap rates correct for broker
- [ ] `swap_mode = 1` for points-based
- [ ] `swap_3days = 3` for Wednesday triple
- [ ] `contract_size` correct (100 for XAUUSD, 5000 for XAGUSD)
- [ ] H1/H2 split validation performed
- [ ] `ForceMinVolumeEntry = false` for crash safety

### Swap Calculation

- Charged at market open (first tick of new trading day)
- Triple swap on Thursday when `swap_3days=3`
- XAUUSD: `-66.99 * 0.01 * 100 * lot_size = -$66.99/lot/day`
- Expected: $3,000-$7,000/year at typical exposure

---

## 8. Test Files

### Primary Tests

| Test | File | Purpose |
|------|------|---------|
| Reference | test_d8_50.cpp | survive=8%, spacing=$1, DD=50% |
| H1/H2 | test_h1h2_validation.cpp | Out-of-sample |
| Multi-year | test_multi_year.cpp | 2024+2025 sequential |
| CombinedJu | test_combinedju_sweep.cpp | 164-config sweep |
| Crash survival | test_combinedju_noforce_sweep.cpp | force=ON vs OFF |

### Parallel Sweep Infrastructure

```cpp
// Load ticks ONCE into shared memory
std::vector<Tick> g_shared_ticks;
LoadTickDataOnce(path);  // ~60s for 52M ticks

// 16 threads process configs in parallel
// 135 configs in 132s (vs ~45min sequential)
```

---

## 9. Advanced Features

### Walk-Forward Optimization

Prevents overfitting by testing on out-of-sample data windows.

```cpp
#include "walk_forward.h"

WalkForwardConfig wf_config;
wf_config.total_start = "2024.01.01";
wf_config.total_end = "2025.12.31";
wf_config.is_window_days = 90;      // In-sample: optimize
wf_config.oos_window_days = 30;     // Out-of-sample: validate
wf_config.step_days = 30;           // Roll forward monthly
wf_config.num_threads = 16;

// Define parameter ranges to optimize
std::vector<ParamRange> ranges = {
    {"survive_pct", 10.0, 15.0, 1.0},
    {"base_spacing", 1.0, 2.0, 0.25}
};

WalkForwardOptimizer<FillUpOscillation> optimizer(wf_config, ranges);
auto result = optimizer.Run(ticks, backtest_config);

// result.robustness_score: 0-100 (>70 = robust)
// result.oos_total_profit: realistic expectation
// result.degradation_pct: IS vs OOS gap
```

**Interpretation:**
- Robustness >70: Strategy generalizes well
- Degradation <30%: Minimal overfitting
- OOS profit: Use this for realistic expectations

### Monte Carlo Simulation

Assesses strategy robustness through randomization.

```cpp
#include "monte_carlo.h"

MonteCarloConfig mc_config;
mc_config.num_simulations = 1000;
mc_config.mode = MonteCarloMode::COMBINED;
mc_config.enable_shuffle = true;
mc_config.enable_slippage = true;
mc_config.slippage_stddev_points = 0.3;

MonteCarloSimulator sim(mc_config);
auto result = sim.Run(trades, initial_balance, true);

// Key outputs:
// result.profit_5th_percentile  - Worst-case (use for sizing)
// result.probability_of_loss    - Risk metric
// result.confidence_level       - HIGH/MEDIUM/LOW
```

**Modes:**
| Mode | Purpose |
|------|---------|
| SHUFFLE_TRADES | Test sequence dependence |
| SKIP_TRADES | Simulate missed entries (10%) |
| VARY_SLIPPAGE | Execution quality sensitivity |
| BOOTSTRAP | Statistical significance |
| COMBINED | Worst realistic scenario |

### Report Generation

Generate professional HTML/CSV/JSON reports.

```cpp
#include "report_generator.h"

ReportConfig report_cfg;
report_cfg.title = "FillUpOscillation Backtest";
report_cfg.output_dir = "./reports";
report_cfg.formats = {ReportFormat::HTML, ReportFormat::CSV};
report_cfg.include_trades = true;
report_cfg.include_equity_curve = true;

ReportGenerator generator(report_cfg);
generator.Generate(backtest_results, trades, equity_curve);
// Creates: reports/report.html, reports/trades.csv
```

**HTML Report Includes:**
- Interactive equity curve (Chart.js)
- Monthly breakdown table
- Key metrics summary
- Trade list (sortable)
- Monte Carlo results (if provided)

### Strategy Interface

Clean API for implementing new strategies.

```cpp
#include "strategy_interface.h"

class MyStrategy : public IStrategy {
public:
    void OnInit(StrategyContext& ctx) override {
        // Initialize state
    }

    void OnTick(const Tick& tick, StrategyContext& ctx) override {
        if (should_buy(tick)) {
            ctx.Buy(0.01, tick.ask, tick.ask + 2.0);
        }
    }

    std::string GetName() const override { return "MyStrategy"; }
};

// Register for factory loading
REGISTER_STRATEGY(MyStrategy);
```

### Live Trading Bridge

Run the **exact same strategy code** in backtest and live trading.

```cpp
#include "live_trading_bridge.h"

// Strategy uses IMarketInterface - works with both:
class GridStrategy {
public:
    void OnTick(const Tick& tick, IMarketInterface& market) {
        if (should_buy(tick)) {
            OrderRequest order;
            order.symbol = "XAUUSD";
            order.type = OrderType::MARKET_BUY;
            order.lots = 0.01;
            order.take_profit = tick.ask + 2.0;

            auto result = market.SendOrder(order);
        }
    }
};

// BACKTEST mode:
BacktestMarket::Config cfg;
cfg.initial_balance = 10000.0;
cfg.contract_size = 100.0;
cfg.leverage = 500.0;
BacktestMarket market(cfg);
market.SetTicks(ticks);

while (market.HasMoreTicks()) {
    market.NextTick();
    strategy.OnTick(market.GetCurrentTick("XAUUSD"), market);
}

// LIVE mode (same strategy code!):
// MT5Market market("localhost:5555");  // Python bridge
// strategy.OnTick(tick, market);
```

**TradingSession Safety Wrapper:**

```cpp
TradingSession::Config session_cfg;
session_cfg.max_positions = 10;
session_cfg.max_lots_per_symbol = 1.0;
session_cfg.max_drawdown_pct = 25.0;
session_cfg.max_consecutive_errors = 5;

auto market_ptr = std::make_shared<BacktestMarket>(market);
TradingSession session(market_ptr, session_cfg);
session.Start();

// Orders rejected if limits exceeded
auto result = session.SendOrder(order);
if (!result.success) {
    // "Max positions reached" or "Max lots exceeded"
}

// Emergency stop
session.GetKillSwitch().Trigger("Manual stop");
```

**Python MT5 Bridge (`bridge/mt5_bridge.py`):**

```bash
# Start bridge server
python bridge/mt5_bridge.py --port 5555

# Commands: connect, get_tick, get_account, get_positions,
#           send_order, close_position, modify_position, close_all
```

**Key Files:**
- `include/live_trading_bridge.h` - IMarketInterface, BacktestMarket, TradingSession
- `bridge/mt5_bridge.py` - ZMQ bridge to MT5 terminal
- `validation/test_live_bridge.cpp` - Usage examples

### Incremental Results Writer

Crash-safe parameter sweeps with resume capability.

```cpp
#include "sweep_results_writer.h"

SweepResultsWriter writer("sweep_results.csv");
writer.WriteHeader({"survive", "spacing", "profit", "dd"});

for (auto& config : configs) {
    auto result = run_backtest(config);
    writer.WriteRow({config.survive, config.spacing,
                     result.profit, result.max_dd});
    // Results written immediately - safe to interrupt
}

// Resume after crash:
SweepCheckpoint checkpoint("sweep_results.csv");
size_t completed = checkpoint.GetCompletedCount();
// Skip first 'completed' configs
```

### Portfolio Backtester

Multi-symbol backtesting with synchronized tick processing.

```cpp
#include "portfolio_backtester.h"

PortfolioConfig config;
config.initial_balance = 100000.0;
config.max_drawdown_pct = 30.0;
config.max_total_positions = 100;

// Add symbols
config.symbols.push_back({"XAUUSD", "path/to/xauusd_ticks.csv", 100.0, 0.01});
config.symbols.push_back({"XAGUSD", "path/to/xagusd_ticks.csv", 5000.0, 0.001});

PortfolioBacktester backtester(config);
backtester.LoadTickData();

backtester.Run([](const MultiSymbolTick& tick, PortfolioBacktester& bt) {
    // Strategy logic - access all symbols
    if (tick.HasSymbol("XAUUSD") && bt.PositionCount("XAUUSD") == 0) {
        bt.OpenPosition("XAUUSD", true, 0.01, 0, tick.Get("XAUUSD").ask + 2.0);
    }
});

auto results = backtester.GetResults();
results.Print();  // Shows per-symbol breakdown
```

### Correlation Analyzer

Analyze strategy/symbol correlations for portfolio diversification.

```cpp
#include "correlation_analyzer.h"

// Build correlation matrix from return series
std::map<std::string, std::vector<double>> returns;
returns["XAUUSD"] = xauusd_returns;
returns["XAGUSD"] = xagusd_returns;

auto matrix = CorrelationAnalyzer::BuildMatrix(returns);
matrix.Print();

// Find highly correlated pairs
auto pairs = CorrelationAnalyzer::FindCorrelatedPairs(matrix, 0.7, true);

// Diversification metrics
std::map<std::string, double> weights = {{"XAUUSD", 0.6}, {"XAGUSD", 0.4}};
auto div = CorrelationAnalyzer::CalculateDiversification(returns, weights);
// div.diversification_ratio, div.effective_n, div.risk_contributions

// Beta/Alpha analysis
double beta = CorrelationAnalyzer::CalculateBeta(strategy_returns, benchmark);
double alpha = CorrelationAnalyzer::CalculateAlpha(strategy_returns, benchmark);
```

### Risk Metrics Calculator

Comprehensive risk analysis: VaR, CVaR, drawdown, tail risk.

```cpp
#include "risk_metrics.h"

std::vector<double> daily_returns = {...};
auto report = RiskMetricsCalculator::Calculate(returns, 0.02);  // 2% risk-free rate
report.Print();

// Key metrics:
// report.var_95, report.var_99         - Value at Risk
// report.cvar_95, report.cvar_99       - Expected Shortfall (CVaR)
// report.max_drawdown                  - Maximum drawdown
// report.sharpe_ratio, report.sortino_ratio
// report.skewness, report.kurtosis     - Tail risk indicators

// Individual VaR methods
double hist_var = RiskMetricsCalculator::HistoricalVaR(returns, 0.95);
double param_var = RiskMetricsCalculator::ParametricVaR(returns, 0.95);
double es = RiskMetricsCalculator::ExpectedShortfall(returns, 0.95);
```

**Key Files:**
- `include/portfolio_backtester.h` - Multi-symbol backtesting
- `include/correlation_analyzer.h` - Correlation and diversification
- `include/risk_metrics.h` - VaR, CVaR, drawdown, ratios
- `validation/test_portfolio_risk.cpp` - Usage examples

---

## 10. Quick Reference

### XAUUSD Parameters

```
survive_pct      = 13.0%
base_spacing     = $1.50 (or 0.06% for EA v4)
lookback         = 4 hours
typical_vol_pct  = 0.55%
min_spacing_mult = 0.5
max_spacing_mult = 3.0
contract_size    = 100.0
leverage         = 500
```

### XAGUSD Parameters

```
survive_pct      = 19.0%
base_spacing_pct = 2.0%
lookback         = 1 hour
typical_vol_pct  = 0.45%
contract_size    = 5000.0
leverage         = 500
pct_spacing      = true  # REQUIRED
```

### CombinedJu Best Config

```
survive_pct         = 12%
base_spacing        = $1.0
tp_mode             = LINEAR (not SQRT for crash safety)
sizing_mode         = UNIFORM (not THRESHOLD for safety)
velocity_filter     = ON
force_min_volume    = false  # CRITICAL for crash survival
```

### The Fundamental Bet

> "Gold will continue to oscillate at small scales, and those oscillations will complete before exceeding our survive distance."

### FillUp's Edge

- **Structure:** Gold oscillates due to market microstructure
- **Risk Premium:** We hold through 67% DD that others exit

---

## Archives

For detailed investigation logs:
- **ARCHIVE_INVESTIGATIONS.md** - All research findings, parameter sweeps
- **ARCHIVE_METHODOLOGY.md** - 6-step parameter selection process
- **ARCHIVE_PHILOSOPHY.md** - Theoretical foundations, Musk principles, discomfort premiums

Location: `.claude/memory/`
