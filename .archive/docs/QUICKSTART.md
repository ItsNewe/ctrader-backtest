# Quick Start Guide - Parallel Backtesting System

## Prerequisites

- Python 3.12+ (with Flask 3.1.2)
- CMake 3.28+ (for C++ build)
- MinGW or MSVC (for C++ compilation)
- Windows (tested on Windows 11)

## Setup

### 1. Build C++ Engine (if not already built)

```bash
cd c:\Users\user\Documents\ctrader-backtest
cmake -B build
cmake --build build --config Release
```

The executable will be at: `build\bin\backtest.exe`

### 2. Install Python Dependencies

```bash
pip install flask==3.1.2
# Other dependencies should already be installed (sqlite3, json, etc.)
```

### 3. Verify Installation

Test that the backtest.exe accepts parameters:

```bash
cd c:\Users\user\Documents\ctrader-backtest\build\bin
.\backtest.exe --survive=2.0 --size=1.5 --spacing=1.0 --json
```

You should see JSON output with metrics.

## Usage Methods

### Method 1: Command-Line Interface (sweep_cli.py)

Run a parameter sweep from command line:

```bash
cd c:\Users\user\Documents\ctrader-backtest

# Simple grid search with defaults
python sweep_cli.py --type grid --workers 4

# Custom ranges
python sweep_cli.py --type grid \
  --survive 1.0,3.0,0.5 \
  --size 0.5,2.0,0.5 \
  --spacing 1.0,3.0,1.0 \
  --workers 4 \
  --output results.json

# Random search for large parameter spaces
python sweep_cli.py --type random \
  --num-combinations 1000 \
  --workers 8 \
  --verbose
```

### Method 2: Flask REST API (server.py)

Start the server:

```bash
cd c:\Users\user\Documents\ctrader-backtest
python server.py
# Server runs on http://localhost:5000
```

Submit a sweep via API:

```bash
# Start a sweep
curl -X POST http://localhost:5000/api/sweep/start \
  -H "Content-Type: application/json" \
  -d '{
    "type": "grid",
    "survive_range": [0.5, 3.0, 0.5],
    "size_range": [0.1, 2.0, 0.5],
    "spacing_range": [0.5, 3.0, 0.5],
    "max_workers": 4
  }'

# Get sweep ID from response
# {"status": "started", "sweep_id": "sweep_20250106T120530"}

# Check results
curl http://localhost:5000/api/sweep/sweep_20250106T120530

# Get best strategy
curl http://localhost:5000/api/sweep/sweep_20250106T120530/best

# Get top 10 strategies
curl http://localhost:5000/api/sweep/sweep_20250106T120530/top?limit=10

# List all sweeps
curl http://localhost:5000/api/sweep/list
```

### Method 3: Python Programmatic API

```python
from backtest_sweep import SweepExecutor, ParameterGenerator
from metrics_calculator import MetricsCalculator

# Create executor
executor = SweepExecutor(
    backtest_exe=r'build\bin\backtest.exe',
    data_file=r'data\EURUSD_2023.csv',
    max_workers=4
)

# Generate parameters
gen = ParameterGenerator.grid_search(
    survive_range=(0.5, 3.0, 0.5),
    size_range=(0.1, 2.0, 0.5),
    spacing_range=(0.5, 3.0, 0.5)
)

# Run sweep with progress callback
def show_progress(completed, total, result):
    profit = result.metrics.get('profit_loss', 0)
    print(f"[{completed}/{total}] Profit: {profit:.2f}")

results = executor.execute_sweep(gen, progress_callback=show_progress)

# Access results
best = results['statistics']['best_result']
print(f"\nBest Strategy:")
print(f"  Parameters: {best['parameters']}")
print(f"  Profit: {best['metrics']['profit_loss']:.2f}")
print(f"  Sharpe Ratio: {best['metrics']['sharpe_ratio']:.2f}")
print(f"  Win Rate: {best['metrics']['win_rate']:.1f}%")
print(f"  Max Drawdown: {best['metrics']['max_drawdown_pct']:.2f}%")
```

### Method 4: AI Agent (Autonomous Optimization)

```bash
cd c:\Users\user\Documents\ctrader-backtest
python ai_agent.py --iterations 3 --report optimization_report.json
```

The AI agent will:
1. Run a wide grid search (Iteration 1)
2. Analyze results and refine parameter ranges
3. Run focused searches on best performers (Iterations 2-3)
4. Save detailed report to JSON

Output:
```
==================================================
AI STRATEGY OPTIMIZATION LOOP
==================================================

--- Iteration 1/3 ---
Search type: grid
[OK] Iteration 1: Profit=500.25, Sharpe=1.82, MaxDD=1.5%

Parameter Adaptation:
  survive: 1.0 - 2.5
  size: 0.5 - 1.5
  spacing: 0.5 - 2.0

--- Iteration 2/3 ---
[OK] Iteration 2: Profit=650.50, Sharpe=2.14, MaxDD=1.2%

[OK] Iteration 3: Profit=725.75, Sharpe=2.31, MaxDD=1.1%

==================================================
OPTIMIZATION COMPLETE
==================================================

Optimization History:
Iteration 1: Profit=500.25, Sharpe=1.82, MaxDD=1.5%
Iteration 2: Profit=650.50, Sharpe=2.14, MaxDD=1.2%
Iteration 3: Profit=725.75, Sharpe=2.31, MaxDD=1.1%

Best Overall: Iteration 3
Parameters: {"survive": 1.5, "size": 0.8, "spacing": 1.2}
...
```

## Test the System

Run the included tests to verify everything works:

```bash
# Test 1: Basic components
python test_sweep.py
# [OK] Parameter generation
# [OK] Executor initialization
# [OK] Single backtest execution
# [OK] All tests passed!

# Test 2: Parallel sweep (12 combinations, 2 workers)
python test_parallel_sweep.py
# [OK] Parallel sweep test passed!
```

## Database

Results are persisted in SQLite:

- **Location**: `results/sweeps.db`
- **Tables**:
  - `sweeps`: Sweep metadata (ID, status, progress)
  - `results`: Individual backtest results with metrics

Query results:

```python
import sqlite3
import json

conn = sqlite3.connect('results/sweeps.db')
cursor = conn.cursor()

# Get all sweeps
cursor.execute('SELECT sweep_id, created_at, status FROM sweeps')
for row in cursor.fetchall():
    print(row)

# Get results from a specific sweep
sweep_id = 'sweep_20250106T120530'
cursor.execute('''
    SELECT parameters, metrics FROM results 
    WHERE sweep_id = ? 
    ORDER BY json_extract(metrics, '$.profit_loss') DESC 
    LIMIT 10
''', (sweep_id,))

for params_json, metrics_json in cursor.fetchall():
    params = json.loads(params_json)
    metrics = json.loads(metrics_json)
    print(f"{params} -> Profit: {metrics['profit_loss']:.2f}")

conn.close()
```

## Performance Tips

1. **Parallel Workers**: Set `max_workers` equal to your CPU core count (usually 4-8)
2. **Grid vs Random**: Use grid for narrow ranges (<100 combinations), random for large spaces (>1000)
3. **Data Files**: Ensure `data_file` path is correct and file exists
4. **Timeout**: Each backtest has 5-minute timeout (increase if needed)
5. **Night Mode**: Run 10000+ combination sweeps overnight with 8 workers

## Troubleshooting

**Issue: backtest.exe not found**
```
Solution: Ensure build succeeded: cmake --build build --config Release
```

**Issue: JSON parsing errors**
```
Solution: Run backtest.exe manually to verify JSON output:
build\bin\backtest.exe --survive=1.0 --size=1.0 --spacing=1.0 --json
```

**Issue: Slow execution**
```
Solution: Increase max_workers (if CPU allows) or use random search with fewer combinations
```

**Issue: Results not persisting**
```
Solution: Check that results/ directory exists and is writable
mkdir results
```

## Next Steps

1. Connect UI to API endpoints for real-time sweep control
2. Implement WebSocket for live progress updates
3. Add strategy comparison charts and heatmaps
4. Build AI loop for autonomous strategy generation
5. Optimize for 10000+ combination sweeps

## References

- [Parallel Sweep README](PARALLEL_SWEEP_README.md) - Detailed documentation
- [Metrics Calculator](metrics_calculator.py) - 17 financial metrics
- [Backtest Engine](src/main.cpp) - C++ implementation

---

**Ready to optimize!** Try:

```bash
# Test with 100 combinations
python sweep_cli.py --type random --num-combinations 100 --workers 4
```

This will complete in ~30-40 seconds and save results to `results/sweeps.db`.
