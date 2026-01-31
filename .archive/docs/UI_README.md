# Web UI Setup Guide

## Overview

This directory contains a modern, user-friendly web dashboard for the cTrader Backtesting Engine. The UI provides an intuitive interface to configure strategies, run backtests, and analyze results without writing code.

## Components

### Frontend
- **index.html** - Main dashboard UI with responsive design
- **dashboard.js** - JavaScript controller handling user interactions
- Built with vanilla HTML/CSS/JavaScript + Chart.js for visualizations

### Backend
- **server.py** - Flask web server providing REST API
- Bridges the web UI with the C++ backtesting engine
- Handles backtest execution and results processing

## Features

### Strategy Configuration
- 4 built-in strategies:
  - **MA Crossover** - Moving Average Crossover trading
  - **Breakout** - Support/Resistance breakout detection
  - **Scalping** - High-frequency quick trades
  - **Grid Trading** - Multi-level grid orders

### Backtest Parameters
- Multiple testing modes (Bar-by-Bar, Tick-by-Tick, Every Tick OHLC)
- Risk management (Stop Loss, Take Profit, Lot Size)
- Data file selection and date range filtering
- Strategy-specific parameter tuning

### Results & Analysis
Three comprehensive result tabs:
- **Summary** - Key metrics and performance indicators
- **Trades** - Detailed list of all executed trades
- **Statistics** - Extended statistics (Sharpe ratio, max drawdown, etc.)
- **Equity Curve** - Visual representation of account growth

## Installation

### Prerequisites
- Python 3.8 or higher
- pip (Python package manager)
- (Optional) A running cTrader Backtest Engine executable

### Step 1: Install Python Dependencies

```bash
# Navigate to project root
cd c:\Users\user\Documents\ctrader-backtest

# Install required packages
pip install flask flask-cors
```

### Step 2: Prepare Data Files

Place your historical OHLC data in the `data/` directory as CSV files with the following format:

```csv
time,open,high,low,close,volume
2023-01-01 00:00:00,1.0850,1.0860,1.0840,1.0855,1000
2023-01-01 01:00:00,1.0855,1.0870,1.0850,1.0865,1200
...
```

Example files can be generated or downloaded from various financial data providers.

## Running the Web Server

### Start the Server

```bash
python server.py
```

Output:
```
 * Running on http://0.0.0.0:5000
 * Debug mode: off
```

### Access the Dashboard

Open your web browser and navigate to:
```
http://localhost:5000
```

## Usage Guide

### 1. Select a Strategy
Click on one of the four strategy cards to select your trading strategy.

### 2. Configure Basic Parameters
- **Data File**: Path to your CSV data file
- **Date Range**: Select backtest period
- **Testing Mode**: Choose between speed and accuracy
- **Risk Management**: Set stop loss, take profit, and lot size

### 3. Adjust Strategy Parameters
Strategy-specific parameters appear after selection:
- **MA Crossover**: Fast MA period, Slow MA period
- **Breakout**: Lookback period, breakout threshold
- **Scalping**: RSI period, overbought/oversold levels
- **Grid Trading**: Grid levels, grid spacing

### 4. Run Backtest
Click **"▶️ Run Backtest"** to execute the backtest.

### 5. Analyze Results
- **Summary Tab**: Overview of total profit, return %, win rate, etc.
- **Trades Tab**: Detailed list of all trades with entry/exit prices
- **Statistics Tab**: Advanced metrics (Sharpe ratio, Sortino ratio, etc.)
- **Equity Curve**: Visual chart showing account equity over time

### 6. Reset or Adjust
Click **"↺ Reset"** to clear the form and start over, or modify parameters and run again.

## API Endpoints

### POST /api/backtest/run
Execute a backtest with given configuration.

**Request:**
```json
{
  "strategy": "ma_crossover",
  "data_file": "data/EURUSD_2023.csv",
  "start_date": "2023-01-01",
  "end_date": "2023-12-31",
  "testing_mode": "bar",
  "lot_size": 0.1,
  "stop_loss_pips": 50,
  "take_profit_pips": 100,
  "spread_pips": 2,
  "strategy_params": {
    "fastPeriod": 10,
    "slowPeriod": 20
  }
}
```

**Response:**
```json
{
  "status": "success",
  "total_trades": 45,
  "total_pnl": 2500.50,
  "return_percent": 25.5,
  "win_rate": 60.5,
  "sharpe_ratio": 1.45,
  "max_drawdown": -12.3,
  "equity_curve": [...],
  "trades": [...]
}
```

### GET /api/strategies
Get list of available strategies and their parameters.

### GET /api/data/files
Get list of available data files in the data directory.

## Customization

### Adding New Strategies

Edit `dashboard.js` and add to the `strategies` object:

```javascript
your_strategy: {
    name: 'Your Strategy Name',
    description: 'Description',
    parameters: [
        { name: 'param1', label: 'Parameter 1', type: 'number', value: 10 },
        { name: 'param2', label: 'Parameter 2', type: 'number', value: 20 }
    ]
}
```

Then add corresponding implementation in the C++ engine.

### Styling

Modify the CSS in `index.html` to customize colors, fonts, and layout. The design uses CSS Grid for responsive layouts.

### Connecting to C++ Engine

The `server.py` currently uses mock results for demonstration. To connect to your actual C++ backtest executable:

1. Uncomment the actual executable call in `run_backtest_sync()`
2. Implement parameter serialization to JSON or command-line arguments
3. Parse C++ output results
4. Handle error cases and timeouts

## Troubleshooting

### Port Already in Use
If port 5000 is in use, modify `server.py`:
```python
app.run(host='0.0.0.0', port=5001, debug=False)
```

### CORS Errors
The server includes CORS headers for development. In production, restrict origins:
```python
CORS(app, origins=['https://yourdomain.com'])
```

### Missing Dependencies
```bash
pip install --upgrade flask flask-cors
```

### Data File Not Found
Ensure CSV files are in the `data/` directory and use relative paths like `data/EURUSD_2023.csv`

## Architecture

```
┌─────────────────────────────────────────┐
│         Web Browser (Dashboard)          │
│    index.html + dashboard.js + Chart.js │
└────────────┬────────────────────────────┘
             │ HTTP REST API
             ↓
┌──────────────────────────────┐
│  Flask Web Server (server.py) │
│   - Routing                   │
│   - Validation                │
│   - Result Processing         │
└────────────┬─────────────────┘
             │ CLI Args / JSON
             ↓
┌──────────────────────────────┐
│   C++ Backtest Engine (exe)   │
│   - Strategy Implementation   │
│   - Tick/Bar Processing       │
│   - Performance Calculation   │
└──────────────────────────────┘
```

## Performance

- **Bar-by-Bar Mode**: 1000s of bars per second (fastest)
- **Tick-by-Tick Mode**: 100s of ticks per second (most realistic)
- **Every Tick OHLC**: 1000s of synthetic ticks per second (balanced)

Web interface provides near-instant feedback for typical backtests.

## Security Notes

- This is a development/local deployment setup
- For production:
  - Add authentication
  - Validate all user inputs
  - Restrict API access
  - Use HTTPS
  - Implement rate limiting
  - Add CSRF protection

## Support

For issues or enhancements:
1. Check the browser console for JavaScript errors
2. Check Flask console for Python errors
3. Verify C++ executable is accessible
4. Ensure data files are in correct format
5. Review backtest configuration for validation errors

## License

Same license as main cTrader Backtest Engine project
