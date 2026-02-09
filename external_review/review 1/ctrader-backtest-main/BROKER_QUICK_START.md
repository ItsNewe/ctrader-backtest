# Broker Configuration Quick Start

## 🚀 5-Minute Setup

### For cTrader Users

#### 1. Get API Credentials (2 min)

```
1. Go to https://spotware.com/open-api
2. Sign in with your cTrader account
3. Create new application
4. Copy API Key and API Secret
```

#### 2. Connect in Dashboard (1 min)

```
Dashboard → Broker Settings
├─ Broker: cTrader
├─ Account Type: Demo (or Live)
├─ Account ID: [Your account number]
├─ Leverage: 500
├─ API Key: [Paste here]
└─ API Secret: [Paste here]
→ Click "🔗 Connect Broker"
```

#### 3. Fetch Specs (1 min)

```
→ Load data file (e.g., data/EURUSD_2023.csv)
→ Click "📥 Fetch Specs"
→ See "Loaded Instrument Specs" populate
```

#### 4. Run Backtest (1 min)

```
→ Configure strategy and parameters
→ Click "▶️ Run Backtest"
→ Results now use REAL broker specs!
```

**Total time: ~5 minutes**

---

### For MetaTrader 5 Users

#### 1. Install Python Module (1 min)

```bash
pip install MetaTrader5
```

#### 2. Launch MetaTrader 5

```
- Start MT5 terminal
- Log in to your account
- Keep it running in background
```

#### 3. Connect in Dashboard (1 min)

```
Dashboard → Broker Settings
├─ Broker: MetaTrader 5
├─ Account ID: [Your account number]
├─ Leverage: [Auto-detected from account]
└─ Click "🔗 Connect Broker"
```

#### 4. Fetch & Run (2 min)

```
→ Load data file
→ Click "📥 Fetch Specs"
→ Run backtest
```

**Total time: ~5 minutes**

---

## What Gets Loaded?

When you fetch specs, these REAL broker parameters are loaded:

```
✓ Contract Size (lot size)        → Affects P&L calculation
✓ Margin Requirement (%)          → Determines max positions
✓ Pip Value                       → Price movement value
✓ Commission per Lot              → Trading costs
✓ Swap Rates (Buy/Sell)           → Overnight holding costs
✓ Min/Max Volume                  → Position limits
```

## Example: EURUSD Specs

```
From cTrader:
  Lot Size: 100,000 units
  Margin: 2% (1:50 effective)
  Pip Value: $10 per pip
  Commission: $0 (STP) or $8/lot (ECN)
  Swap: -0.5 (long) / -0.2 (short)

vs

From MetaTrader 5:
  Lot Size: 100,000 units
  Margin: 5% (varies by broker)
  Pip Value: $10 per pip
  Commission: varies by broker
  Swap: varies by broker

← Your backtest now reflects ACTUAL broker!
```

## Why This Matters

### Without Broker Specs (UNREALISTIC)

```
Your margin check:
  Positions × Fixed margin = Max positions
  Result: May show unrealistic P&L
```

### With Live Specs (PRODUCTION-READY)

```
Your margin check:
  Positions × ACTUAL broker margin = Real max positions
  Commission applied from broker
  Leverage capped to broker limit
  Result: Matches real trading!
```

## Troubleshooting

### "API Key invalid"
- ✓ Check you copied API Key correctly
- ✓ Verify it's not API Secret in wrong field
- ✓ Ensure API app is created in Spotware
- ✓ Check demo vs live account setting

### "MetaTrader5 module not found"
```bash
pip install MetaTrader5
# Then restart Flask server
```

### "Spec not found for symbol"
- ✓ Verify symbol format: "EURUSD" not "EUR/USD"
- ✓ Check broker offers the instrument
- ✓ Try a major pair first: EURUSD, GBPUSD, USDJPY

### "Connection timeout"
- ✓ Check internet connection
- ✓ Verify API is not rate-limited
- ✓ Check firewall allows connection
- ✓ Try demo account (more stable)

## Common Broker Settings

### cTrader Typical Values

| Setting | Value |
|---------|-------|
| Leverage | 100 - 500 |
| Margin (Forex) | 0.2% - 2% |
| Margin (Metals) | 0.5% - 5% |
| Spreads | 0.1 - 0.5 pips |
| Commission | $0 - $8/lot |

### MetaTrader5 Typical Values

| Setting | Value |
|---------|-------|
| Leverage | 100 - 1000 |
| Margin (Forex) | 0.5% - 5% |
| Spreads | 0.5 - 2 pips |
| Commission | 0 - 0.1% |
| Swap | -10 to +10 points |

## Next Steps After Setup

1. **Test with Demo Account First**
   - Lower risk
   - Same specs as live
   - Great for validation

2. **Run Historical Backtest**
   - Load 6-12 months of data
   - Use downloaded live data (CSV)
   - Compare results across timeframes

3. **Validate Against Real Trading**
   - Do a small live trade
   - Compare P&L with backtest
   - Verify margin calculations match

4. **Optimize Strategy**
   - Now that specs are accurate
   - Optimize position sizing
   - Adjust risk parameters

5. **Live Trading**
   - Run backtest before each trade
   - Monitor actual results vs backtest
   - Adjust parameters as needed

## Security Notes

### API Credentials
- ✓ Only share with trusted applications
- ✓ Don't post credentials online
- ✓ Use environment variables in production
- ✓ Regenerate if compromised

### Account Safety
- ✓ Use demo account for testing
- ✓ Start with small position sizes
- ✓ Monitor live trading closely
- ✓ Set account stop-loss rules

## FAQ

**Q: Will my credentials be stored securely?**
A: Currently stored in session memory. For production, use environment variables or credential manager.

**Q: Can I use multiple brokers?**
A: Yes! Connect each one separately and switch between them.

**Q: How often are specs updated?**
A: Automatically every 24 hours. Click "Fetch Specs" to refresh immediately.

**Q: What if broker API goes down?**
A: Dashboard will use cached specs from last fetch. Historical backtests still work.

**Q: Can I run backtest without broker specs?**
A: Yes, but results will be less accurate. Using broker specs is highly recommended.

**Q: Does cTrader or MT5 cost money?**
A: Both free. cTrader API is free. MT5 Python module is free.

## Performance Tips

1. **Fetch specs once**, not before every backtest
2. **Use cached specs** - they update every 24 hours
3. **Batch multiple symbols** in one fetch call
4. **Test with demo account** before live trading

## Example Workflow

```
Morning:
  1. Launch dashboard
  2. Click "Connect Broker" (if not connected)
  3. Click "Fetch Specs" for all symbols you trade
  
During Day:
  4. Load data file
  5. Test strategy with latest specs
  6. Run optimizations
  7. Execute trade if signals confirmed
  
Evening:
  8. Review results vs backtest
  9. Adjust parameters if needed
```

## Still Need Help?

1. Check main [BROKER_API_INTEGRATION.md](./BROKER_API_INTEGRATION.md) for details
2. Review [server.py](./server.py) for API endpoints
3. Check [dashboard.js](./ui/dashboard.js) for frontend code
4. See [broker_api.py](./broker_api.py) for implementation

---

**You're ready! Connect your broker and get accurate backtesting results.** 🎯
