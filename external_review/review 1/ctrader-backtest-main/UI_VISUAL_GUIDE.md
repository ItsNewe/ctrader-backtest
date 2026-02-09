# 🎨 cTrader Backtest Engine - Web UI Dashboard

## 📸 User Interface Overview

### Main Dashboard Layout

```
┌─────────────────────────────────────────────────────────────────┐
│ 📊 cTrader Backtest Engine                                      │
│ Interactive Backtesting Dashboard - Configure, Run & Analyze    │
└─────────────────────────────────────────────────────────────────┘

┌──────────────────────────────┐  ┌──────────────────────────────┐
│ ⚙️  CONFIGURATION             │  │ 📈 RESULTS & ANALYSIS        │
│                              │  │                              │
│ Strategy Selection:          │  │ Summary | Trades | Stats     │
│ ┌────────┬────────┐          │  │                              │
│ │MA Cross│Breakout│          │  │ Total P&L: $2,500.50        │
│ ├────────┼────────┤          │  │ Return %: 25.5%             │
│ │Scalping│Grid    │          │  │ Win Rate: 60.5%             │
│ └────────┴────────┘          │  │ Sharpe: 1.45                │
│                              │  │ Max DD: -12.3%              │
│ Data File: [............]   │  │ Total Trades: 45            │
│ Start Date: [2023-01-01]    │  │                              │
│ End Date:   [2023-12-31]    │  │ ┌─────────────────────────┐  │
│                              │  │ │  Equity Curve Chart     │  │
│ Testing Mode: ▼ Bar-by-Bar  │  │ │  ┌─────────────────────┐│  │
│                              │  │ │  │ ╱  ╱   ╱  ╱  ╱╱      ││  │
│ Risk Management:             │  │ │  │╱  ╱   ╱  ╱╱╱        ││  │
│ Stop Loss: [50] pips        │  │ │  │  ╱   ╱  ╱          ││  │
│ Take Profit: [100] pips     │  │ │  │╱   ╱  ╱           ││  │
│ Lot Size: [0.1]            │  │ │  │   ╱  ╱            ││  │
│ Spread: [2] pips           │  │ │  │  ╱  ╱             ││  │
│                              │  │ │  └─────────────────────┘│  │
│ Strategy Parameters:         │  │ └─────────────────────────┘  │
│ Fast MA Period: [10]        │  │                              │
│ Slow MA Period: [20]        │  │ Detailed Trade List:        │
│                              │  │ #  Entry    Exit    P&L     │
│ [▶️ Run Backtest] [↺ Reset] │  │ 1  1.0850  1.0875  +25.00   │
│                              │  │ 2  1.0875  1.0860  -15.00   │
└──────────────────────────────┘  │ 3  1.0860  1.0890  +30.00   │
                                   │ ...                         │
                                   └──────────────────────────────┘
```

## 🎯 Features Breakdown

### 1️⃣ Strategy Selection

```
┌─ Select Strategy ─────────────────────┐
│ ┌──────────┐  ┌──────────┐           │
│ │ MA       │  │ Breakout │           │
│ │Crossover │  │Support/  │           │
│ └──────────┘  │Resistance│           │
│               └──────────┘           │
│ ┌──────────┐  ┌──────────┐           │
│ │ Scalping │  │   Grid   │           │
│ │Quick     │  │   Multi- │           │
│ │Entry/Exit│  │  Level   │           │
│ └──────────┘  └──────────┘           │
└───────────────────────────────────────┘
```

### 2️⃣ Configuration Section

**Data & Date:**
- Data File input with file picker
- Start/End date selectors
- Date validation

**Testing Mode:**
- Bar-by-Bar: ⚡ Fastest, ⚠️ Least realistic
- Tick-by-Tick: 🐢 Slowest, ✅ Most realistic
- Every Tick OHLC: ⚖️ Balanced

**Risk Management:**
```
Stop Loss (pips)     [50]
Take Profit (pips)   [100]
Lot Size             [0.1]
Spread (pips)        [2]
```

**Strategy Parameters:**
Dynamically shown based on selected strategy
```
MA Crossover:           Breakout:
├─ Fast MA Period       ├─ Lookback Period
└─ Slow MA Period       └─ Breakout Threshold

Scalping:               Grid Trading:
├─ RSI Period           ├─ Grid Levels
├─ Overbought Level     └─ Grid Spacing %
└─ Oversold Level
```

### 3️⃣ Results Dashboard

#### Summary Tab
```
┌─────────────────────────────────────────┐
│ Total P&L: $2,500.50 ✅  Return %: 25.5% │
├─────────────────────────────────────────┤
│ Win Rate: 60.5%           Profit Factor: 1.52
│ Sharpe Ratio: 1.45        Max Drawdown: -12.3%
│ Total Trades: 45          Avg Win: $125.50
└─────────────────────────────────────────┘
```

#### Trades Tab
```
┌──┬──────────────┬────────┬──────────────┬────────┬────────┬──────────┐
│# │ Entry Time   │ Entry  │ Exit Time    │ Exit   │ Volume │ P&L      │
├──┼──────────────┼────────┼──────────────┼────────┼────────┼──────────┤
│1 │ 2023-01-01   │ 1.0850 │ 2023-01-05   │ 1.0875 │ 0.1    │ +$25.00 ✅│
│2 │ 2023-01-06   │ 1.0875 │ 2023-01-08   │ 1.0860 │ 0.1    │ -$15.00 ❌│
│3 │ 2023-01-09   │ 1.0860 │ 2023-01-12   │ 1.0890 │ 0.1    │ +$30.00 ✅│
│...
└──┴──────────────┴────────┴──────────────┴────────┴────────┴──────────┘
```

#### Statistics Tab
```
Winning Trades:              30
Losing Trades:               15
Win Rate:                    66.7%
Avg Trade P&L:              $55.56
Largest Win:                $350.00
Largest Loss:              -$120.00
Consecutive Wins:           7
Consecutive Losses:         3
Profit Factor:              2.15
Sharpe Ratio:              1.87
Sortino Ratio:             2.45
Recovery Factor:           20.83
```

## 🎨 Visual Design Elements

### Color Scheme
```
Primary Blue:     #2a5298 (dark actions, borders)
Accent Blue:      #1e3c72 (headers, text)
Success Green:    #27ae60 (positive values)
Danger Red:       #e74c3c (negative values)
Background:       Linear gradient (blue gradient)
Cards:            White with subtle shadows
```

### Responsive Grid System
```
Desktop (1920px):        Configuration (50%) | Results (50%)
Tablet (768px):          Full width, stacked vertically
Mobile (375px):          Full width, optimized touch targets
```

### Interactive Elements
- Strategy cards: Highlight on hover, visual selection
- Form inputs: Blue focus outline, smooth transitions
- Buttons: Gradient background, elevation on hover
- Charts: Interactive with hover tooltips
- Tabs: Smooth content switching
- Status messages: Color-coded alerts

## 📊 Data Flow

```
User Input
    ↓
[UI Validation]
    ↓
Backtest Configuration (JSON)
    ↓
[HTTP POST /api/backtest/run]
    ↓
Flask Server
    ↓
C++ Backtest Engine
    ↓
Results (JSON)
    ↓
[Parse & Visualize]
    ↓
Dashboard Display
    ├─ Metric Cards
    ├─ Trade Table
    ├─ Statistics
    └─ Equity Chart
```

## 🔧 Technology Stack

### Frontend
- **HTML5**: Semantic structure, accessibility
- **CSS3**: Grid/Flexbox layouts, animations, gradients
- **JavaScript ES6+**: Vanilla JS, no frameworks
- **Chart.js**: Professional data visualization

### Backend
- **Flask**: Lightweight Python web framework
- **Flask-CORS**: Cross-origin request handling
- **Python 3.8+**: Async-ready, clean syntax

### Integration
- **REST API**: Standard JSON endpoints
- **HTTP**: Cross-platform compatibility
- **CLI Args**: C++ engine integration

## 📱 Browser Compatibility

- ✅ Chrome 90+
- ✅ Firefox 88+
- ✅ Safari 14+
- ✅ Edge 90+
- ✅ Mobile browsers

## 🚀 Getting Started

### Quick Start (Windows)
```bash
cd c:\Users\user\Documents\ctrader-backtest
run_ui.bat
```

### Manual Start
```bash
pip install -r requirements.txt
python server.py
# Open: http://localhost:5000
```

## 📈 Example Screenshots

### Configuration
```
Shows form with strategy selector, date pickers,
risk parameters, and strategy-specific tuning options
```

### Running Backtest
```
Shows loading spinner with "Running backtest..." message
```

### Results - Summary
```
Shows metric cards with color-coded P&L, percentages,
and key statistics in a responsive grid
```

### Results - Equity Curve
```
Shows interactive line chart of account equity
over time with smooth animations
```

### Results - Trades
```
Shows sortable table of all trades with colors
for wins (green) and losses (red)
```

## 🎯 User Workflows

### Workflow 1: Strategy Evaluation
```
1. Select Strategy → 2. Set Defaults → 3. Run → 4. Check Summary
   Performance → 5. Review Trades → 6. Analyze Stats
```

### Workflow 2: Parameter Optimization
```
1. Select Strategy → 2. Note Baseline → 3. Adjust Parameter
   → 4. Run → 5. Compare Results → 6. Iterate
```

### Workflow 3: Risk Analysis
```
1. Run Backtest → 2. Check Max Drawdown → 3. Review Sharpe Ratio
   → 4. Analyze Consecutive Losses → 5. Adjust SL/TP
```

## 🔐 User Experience Features

- **Instant Feedback**: Status messages for every action
- **Input Validation**: Prevents invalid configurations
- **Helpful Hints**: Parameter descriptions below fields
- **Progressive Enhancement**: Works even if charts don't load
- **Error Handling**: Clear error messages, no cryptic codes
- **Accessibility**: Proper labels, keyboard navigation, color contrast

## 📊 Metrics Included

### Performance Metrics
- Total P&L (Profit/Loss)
- Return % (percentage return)
- Win Rate (% of profitable trades)
- Profit Factor (gross profit / gross loss)

### Risk Metrics
- Max Drawdown (largest peak-to-trough decline)
- Sharpe Ratio (risk-adjusted return)
- Sortino Ratio (downside risk-adjusted)
- Recovery Factor (profit / max drawdown)

### Trade Statistics
- Total trades
- Winning/losing trades
- Consecutive wins/losses
- Average trade
- Largest win/loss

## 🎓 Learning & Improvement

The dashboard makes it easy to:
- ✅ Understand strategy performance instantly
- ✅ Identify which parameters work best
- ✅ Spot over-fitting (too good to be true)
- ✅ See equity curve stability
- ✅ Compare strategies side-by-side
- ✅ Export results for further analysis

---

**Created with ❤️ for traders and developers**

Ready to start? → Run `run_ui.bat` and open http://localhost:5000
