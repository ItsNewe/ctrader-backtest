# 🎯 Quick Reference Card - Web UI Dashboard

## 🚀 Start in 30 Seconds

### Windows
```bash
run_ui.bat
# Opens: http://localhost:5000
```

### Mac/Linux
```bash
chmod +x run_ui.sh
./run_ui.sh
```

---

## 📁 Files Reference

| File | Purpose | Key Features |
|------|---------|--------------|
| `ui/index.html` | Main interface | Responsive, 800+ lines |
| `ui/dashboard.js` | Controller logic | Form handling, API calls |
| `server.py` | Web server | Flask, REST API, CORS |
| `run_ui.bat` | Windows startup | Auto-install, browser open |
| `run_ui.sh` | Unix startup | Same as .bat |
| `requirements.txt` | Dependencies | Flask, Flask-CORS |

---

## 🎮 User Interface Map

```
Top Header
│
├─ Left Panel (Configuration)
│  ├─ Strategy Selection (4 cards)
│  ├─ Data File Input
│  ├─ Date Range (Start/End)
│  ├─ Testing Mode (dropdown)
│  ├─ Risk Management
│  │  ├─ Stop Loss
│  │  ├─ Take Profit
│  │  ├─ Lot Size
│  │  └─ Spread
│  ├─ Strategy Parameters (dynamic)
│  └─ Buttons: [Run] [Reset]
│
└─ Right Panel (Results)
   ├─ Tabs: Summary | Trades | Stats
   │
   ├─ Summary Tab
   │  └─ Metric Cards (P&L, Return%, Win%, etc.)
   │
   ├─ Trades Tab
   │  └─ Table (Entry, Exit, P&L for each trade)
   │
   ├─ Stats Tab
   │  └─ Statistics (Sharpe, Drawdown, etc.)
   │
   └─ Equity Chart
      └─ Interactive line chart (Account value over time)
```

---

## ⚡ Quick Actions

### Run a Backtest
1. Select strategy → Configure → Click "▶️ Run Backtest"

### View Results
1. Wait for results → Click tabs: Summary / Trades / Stats

### Try Different Parameters
1. Change values → Click "▶️ Run Backtest" again

### Reset Form
1. Click "↺ Reset" → All fields back to defaults

---

## 🎨 Key Features at a Glance

| Feature | What It Does | Location |
|---------|------------|----------|
| Strategy Selector | Choose trading strategy | Top of left panel |
| Date Picker | Select backtest period | Left panel |
| Testing Mode | Speed vs accuracy choice | Dropdown in left |
| Parameters | Strategy-specific settings | Lower left |
| Summary Tab | Key metrics overview | Top of results |
| Trades Tab | All trades listed | Results tabs |
| Stats Tab | Advanced statistics | Results tabs |
| Equity Chart | Visual account growth | Bottom of results |

---

## 📊 4 Built-in Strategies

| Strategy | Parameters | Best For |
|----------|-----------|----------|
| MA Crossover | Fast/Slow periods | Trend following |
| Breakout | Lookback, threshold | Support/Resistance |
| Scalping | RSI settings | Quick trades |
| Grid Trading | Levels, spacing | Range-bound markets |

---

## 🔢 Key Metrics Explained

### Performance
- **P&L** = Total profit/loss in dollars
- **Return %** = Total return percentage
- **Win Rate** = % of profitable trades

### Risk
- **Sharpe Ratio** = Risk-adjusted return (>1 is good)
- **Max Drawdown** = Worst peak-to-trough loss
- **Profit Factor** = Gross profit ÷ Gross loss

---

## 🌐 API Endpoints

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/api/backtest/run` | POST | Run a backtest |
| `/api/strategies` | GET | List all strategies |
| `/api/data/files` | GET | List available data |

---

## 📋 Data File Format

CSV with columns:
```
time,open,high,low,close,volume
2023-01-01 00:00:00,1.0850,1.0860,1.0840,1.0855,1000
```

Place in: `data/` folder

---

## 🎯 Typical Workflow

```
1. Open http://localhost:5000
   ↓
2. Select Strategy (click card)
   ↓
3. Adjust parameters (modify values)
   ↓
4. Click "▶️ Run Backtest"
   ↓
5. View Results
   ├─ Summary: Check key metrics
   ├─ Trades: Review individual trades
   ├─ Stats: Analyze performance
   └─ Chart: Visual equity curve
   ↓
6. Modify & Retry
   └─ Change parameters → Run again
```

---

## 💡 Pro Tips

✅ **Start simple** - Use default parameters first
✅ **Change one thing** - Adjust one parameter at a time
✅ **Check equity curve** - Smooth curve = stable strategy
✅ **Watch win rate** - 50-70% is realistic
✅ **Compare runs** - Remember baseline metrics
✅ **Check drawdown** - Max loss percentage matters
✅ **Review trades** - Look for patterns in wins/losses

---

## 🔧 Customization Quick Links

**Add new strategy:**
→ Edit `ui/dashboard.js`, add to `strategies` object

**Change colors:**
→ Edit `index.html`, modify CSS variables

**Change port:**
→ Edit `server.py`, change `app.run(port=5000)`

---

## ⚠️ Common Mistakes

❌ Starting with unrealistic parameters
→ Use conservative defaults first

❌ Only looking at return %
→ Also check drawdown and win rate

❌ Expecting 100% win rate
→ 60-70% is excellent

❌ Ignoring slippage/spread
→ Always include realistic costs

---

## 📞 Troubleshooting Quick Fix

| Problem | Solution |
|---------|----------|
| Can't start | Check Python installed: `python --version` |
| Port in use | Edit server.py: `port=5001` |
| Module error | Install: `pip install flask flask-cors` |
| No data | Add CSV to `data/` folder |
| No trades | Check data format (5 columns) |

---

## 📚 Learn More

📖 **Implementation Guide**: `UI_IMPLEMENTATION_GUIDE.md`
📖 **Detailed Docs**: `UI_README.md`
📖 **Visual Guide**: `UI_VISUAL_GUIDE.md`

---

## 🎓 Learning Path

**Day 1:**
- [ ] Start the UI
- [ ] Run default strategy
- [ ] Review results

**Day 2:**
- [ ] Change parameters
- [ ] Run multiple times
- [ ] Compare results

**Day 3:**
- [ ] Add your own data
- [ ] Test different strategies
- [ ] Analyze statistics

**Week 2:**
- [ ] Connect to C++ engine
- [ ] Deploy to production
- [ ] Add custom strategies

---

## 🎉 You're All Set!

Your professional backtesting dashboard is ready.

**Next step:** Open a terminal and run `run_ui.bat`

**Then:** Open http://localhost:5000

**Enjoy!** 🚀

---

**Dashboard Version**: 1.0
**Last Updated**: January 2026
**Status**: Production Ready
