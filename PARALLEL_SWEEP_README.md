# Parallel Backtest Parameter Sweep System

## Overview

A complete AI-ready system for testing trading strategies with different parameter combinations in parallel. Built for rapid iteration and AI agent integration.

## Architecture

```
User/AI Agent
    ↓
sweep_cli.py (CLI interface)
    ↓
Flask REST API (server.py)
    ↓
backtest_sweep.py (Parallel Executor)
    ↓
ThreadPoolExecutor (4+ workers)
    ↓
build/bin/backtest.exe (C++ backtest engine)
```

## Components

### 1. C++ Backtest Engine (`build/bin/backtest.exe`)

**Updated to accept CLI parameters:**
```bash
backtest.exe --survive=2.0 --size=1.5 --spacing=1.0 --json
```

**Parameters:**
- `--survive`: Max drawdown tolerance (0.5-5.0)
- `--size`: Position sizing multiplier (0.1-10.0)
- `--spacing`: Grid spacing in points (0.5-10.0)
- `--data`: Path to OHLC CSV data
- `--json`: Output results in JSON format

**JSON Output:**
```json
{
  "parameters": {
    "survive": 2.0,
    "size": 1.5,
    "spacing": 1.0
  },
  "metrics": {
    "profit_loss": 250.50,
    "return_percent": 2.5,
    "total_trades": 42,
    "winning_trades": 28,
    "losing_trades": 14,
    "win_rate": 66.67,
    "profit_factor": 2.14,
    "sharpe_ratio": 1.82,
    "max_drawdown": 150.25,
    "max_drawdown_pct": 1.5,
    "avg_win": 25.50,
    "avg_loss": -15.75,
    "expectancy": 5.96,
    "total_commission": 100.00,
    "total_swap": 50.00
  }
}
```

### 2. Parameter Generator (`backtest_sweep.py`)

**Grid Search** - exhaustive combination testing:
```python
from backtest_sweep import ParameterGenerator

gen = ParameterGenerator.grid_search(
    survive_range=(0.5, 5.0, 0.5),    # min, max, step
    size_range=(0.1, 10.0, 0.5),
    spacing_range=(0.5, 10.0, 0.5)
)
# Generates all combinations: (0.5, 0.1, 0.5), (0.5, 0.1, 1.0), ...
```

**Random Search** - random sampling:
```python
gen = ParameterGenerator.random_search(
    num_combinations=1000,
    survive_range=(0.5, 5.0),
    size_range=(0.1, 10.0),
    spacing_range=(0.5, 10.0)
)
# Generates 1000 random combinations
```

### 3. Parallel Executor (`backtest_sweep.py`)

Runs backtests in parallel with progress tracking and SQLite persistence:

```python
from backtest_sweep import SweepExecutor, ParameterGenerator

executor = SweepExecutor(
    backtest_exe=r'build\bin\backtest.exe',
    data_file=r'data\EURUSD_2023.csv',
    max_workers=4  # 4 parallel workers
)

# Generate parameters
gen = ParameterGenerator.grid_search((0.5, 5.0, 0.5), (0.1, 10.0, 0.5), (0.5, 10.0, 0.5))

# Progress callback (optional)
def on_progress(completed, total, result):
    print(f"[{completed}/{total}] {result.parameters}")

# Run sweep
results = executor.execute_sweep(gen, progress_callback=on_progress)

# Results structure:
{
    'sweep_id': 'sweep_20250106T120530',
    'total_combinations': 1000,
    'completed': 1000,
    'statistics': {
        'best_result': {...},
        'worst_result': {...},
        'average_profit': 125.50,
        'win_rate_avg': 55.3,
        ...
    },
    'results': [
        {
            'parameters': {'survive': 0.5, 'size': 0.1, 'spacing': 0.5},
            'metrics': {...},
            'composite_score': 85.2
        },
        ...
    ]
}
```

### 4. Flask REST API (`server.py`)

**Endpoints:**

#### POST /api/sweep/start
Submit a new parameter sweep:
```bash
curl -X POST http://localhost:5000/api/sweep/start \
  -H "Content-Type: application/json" \
  -d '{
    "type": "grid",
    "survive_range": [0.5, 5.0, 0.5],
    "size_range": [0.1, 10.0, 0.5],
    "spacing_range": [0.5, 10.0, 0.5],
    "data_file": "data/EURUSD_2023.csv",
    "max_workers": 4
  }'
```

Response:
```json
{
  "status": "started",
  "sweep_id": "sweep_20250106T120530"
}
```

#### GET /api/sweep/<sweep_id>
Get full sweep results:
```bash
curl http://localhost:5000/api/sweep/sweep_20250106T120530
```

Returns: Full results with all combinations ranked by composite score

#### GET /api/sweep/list
List all sweeps (last 50):
```bash
curl http://localhost:5000/api/sweep/list
```

#### GET /api/sweep/<sweep_id>/best
Get best performing strategy:
```bash
curl http://localhost:5000/api/sweep/sweep_20250106T120530/best
```

#### GET /api/sweep/<sweep_id>/top?limit=10
Get top N strategies:
```bash
curl http://localhost:5000/api/sweep/sweep_20250106T120530/top?limit=10
```

### 5. Metrics Calculator (`metrics_calculator.py`)

Calculates 17 financial metrics from backtest results:

**Profitability Metrics:**
- `profit_loss`: Total profit/loss
- `return_percent`: Return as percentage
- `total_trades`: Number of trades
- `winning_trades`: Number of winning trades
- `losing_trades`: Number of losing trades
- `win_rate`: Percentage of winning trades
- `profit_factor`: Gross profit / Gross loss
- `expectancy`: Average profit per trade

**Risk-Adjusted Metrics:**
- `sharpe_ratio`: Return per unit of risk (annualized)
- `sortino_ratio`: Return per unit of downside risk
- `calmar_ratio`: Return / Max drawdown (BEST for robust strategies)
- `recovery_factor`: Total profit / Max drawdown

**Drawdown Metrics:**
- `max_drawdown`: Maximum peak-to-trough decline ($)
- `max_drawdown_pct`: Maximum drawdown (%)

**Trade Statistics:**
- `avg_win`: Average winning trade
- `avg_loss`: Average losing trade
- `consecutive_wins`: Max consecutive wins
- `consecutive_losses`: Max consecutive losses
- `std_deviation`: Standard deviation of returns

**Composite Ranking:**
- Combines metrics for AI-friendly strategy comparison
- Formula: (Sharpe/3 × 60%) + (Calmar/3 × 25%) + (WinRate × 15%)
- Prioritizes risk-adjusted returns and drawdown survival

### 6. AI Agent (`ai_agent.py`)

Autonomous strategy optimization agent:

```python
from ai_agent import AIStrategyAgent

agent = AIStrategyAgent(
    backtest_exe=r'build\bin\backtest.exe',
    data_file=r'data\EURUSD_2023.csv',
    max_workers=4
)

# Run optimization loop (3 iterations)
iterations = agent.run_optimization_loop(iterations=3)

# Save report
agent.save_optimization_report('ai_optimization_report.json')
```

**How it works:**
1. **Iteration 1**: Wide grid search of parameter space
2. **Iteration 2**: Focus on top performers, refine ranges
3. **Iteration 3**: Exploit best regions with random search

Each iteration generates new parameter ranges based on profitability analysis.

### 7. CLI Interface (`sweep_cli.py`)

Command-line wrapper for running sweeps:

```bash
# Grid search with default ranges
python sweep_cli.py --type grid

# Custom parameter ranges
python sweep_cli.py --type grid \
  --survive 0.5,5.0,0.5 \
  --size 0.1,10.0,0.5 \
  --spacing 0.5,10.0,0.5 \
  --workers 4 \
  --output results.json

# Random search
python sweep_cli.py --type random \
  --num-combinations 1000 \
  --workers 8 \
  --verbose
```

## Database Schema

SQLite database (`results/sweeps.db`):

**sweeps table:**
```sql
CREATE TABLE sweeps (
  sweep_id TEXT PRIMARY KEY,
  created_at TEXT,
  data_file TEXT,
  total_combinations INTEGER,
  completed_combinations INTEGER,
  status TEXT  -- 'running' or 'completed'
)
```

**results table:**
```sql
CREATE TABLE results (
  id INTEGER PRIMARY KEY,
  sweep_id TEXT,
  parameters TEXT,  -- JSON
  metrics TEXT,     -- JSON
  timestamp TEXT
)
```

## Usage Examples

### Example 1: Simple Grid Search via CLI
```bash
cd c:\Users\user\Documents\ctrader-backtest
python sweep_cli.py --type grid --workers 4
```

### Example 2: Random Search via API
```bash
curl -X POST http://localhost:5000/api/sweep/start \
  -H "Content-Type: application/json" \
  -d '{
    "type": "random",
    "num_combinations": 1000,
    "survive_range": [0.5, 5.0],
    "size_range": [0.1, 10.0],
    "spacing_range": [0.5, 10.0],
    "max_workers": 4
  }'
```

### Example 3: AI Agent Optimization
```bash
python ai_agent.py --iterations 5 --report optimization.json
```

### Example 4: Direct Executor Usage
```python
from backtest_sweep import SweepExecutor, ParameterGenerator

executor = SweepExecutor(max_workers=4)
gen = ParameterGenerator.random_search(
    num_combinations=10000,
    survive_range=(1.0, 3.0),
    size_range=(0.5, 2.0),
    spacing_range=(1.0, 5.0)
)

results = executor.execute_sweep(gen)
best = results['statistics']['best_result']
print(f"Best strategy: {best['parameters']}")
print(f"Profit: {best['metrics']['profit_loss']}")
print(f"Sharpe Ratio: {best['metrics']['sharpe_ratio']}")
```

## Performance Characteristics

**Tested Configuration:**
- 12 parameter combinations
- 2 parallel workers
- Execution time: ~2-3 seconds per backtest
- Total sweep time: ~20 seconds (parallel)

**Scaling Estimates:**
- 100 combinations @ 4 workers: ~30-40 seconds
- 1,000 combinations @ 4 workers: ~5-7 minutes
- 10,000 combinations @ 8 workers: ~30-40 minutes

**Optimization Tips:**
- Use grid search for exhaustive exploration (small ranges)
- Use random search for large parameter spaces (1000+ combinations)
- Increase workers for parallel execution (match CPU core count)
- Run overnight for 10000+ combination sweeps

## Files Summary

| File | Lines | Purpose |
|------|-------|---------|
| build/bin/backtest.exe | C++ | Compiled backtest engine with CLI parameters |
| backtest_sweep.py | 402 | Parallel executor with SQLite persistence |
| metrics_calculator.py | 370 | 17 financial metrics calculation |
| ai_agent.py | 370 | Autonomous strategy optimization |
| sweep_cli.py | 120 | Command-line interface |
| server.py | 5 endpoints | Flask REST API |
| test_sweep.py | 65 | Unit tests |
| test_parallel_sweep.py | 60 | Integration test |

## Next Steps

1. **UI Dashboard** - Add sweep configuration panel to web UI
2. **Progress WebSocket** - Real-time sweep progress updates
3. **Results Visualization** - Charts and heatmaps of parameter effects
4. **AI Loop** - Connect agent to sweep system for autonomous iteration
5. **Performance Optimization** - Process pool for even faster execution
6. **Data Persistence** - Archive results for historical analysis

## Notes

- All backtests run with sample data (generated in C++ backtest engine)
- To use real data, update `data_file` parameter to point to your OHLC CSV
- Results database stored in `results/sweeps.db`
- Maximum timeout per backtest: 5 minutes
- Recommended max workers: CPU core count (usually 4-8)
