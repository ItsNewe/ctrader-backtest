# Broker API Integration - Installation & Setup Guide

## 📦 What's New

Your cTrader Backtest Engine now includes **Broker API Integration** for production-grade backtesting with real broker specifications.

---

## ⚡ Quick Start (5 Minutes)

### Step 1: Dependencies ✓

Already installed:
- ✅ Flask & Flask-CORS
- ✅ requests (for API calls)
- ✅ Python 3.13.6

Optional (for MetaTrader5):
```bash
pip install MetaTrader5
```

### Step 2: Start the Server

```bash
# Windows
.\run_ui.bat

# Or manually
python server.py
```

Server starts at: `http://localhost:5000`

### Step 3: Connect Broker

**In Dashboard → Broker Settings:**

#### For cTrader:
1. Get API Key/Secret from https://spotware.com/open-api
2. Select "cTrader" in Broker dropdown
3. Enter Account ID and Leverage
4. Paste API credentials
5. Click "🔗 Connect Broker"

#### For MetaTrader 5:
1. Launch MetaTrader 5 & log in
2. Install: `pip install MetaTrader5`
3. Select "MetaTrader 5" in Broker dropdown
4. Enter Account ID
5. Click "🔗 Connect Broker"

### Step 4: Fetch Specs

1. Load data file (e.g., "data/EURUSD_2023.csv")
2. Click "📥 Fetch Specs"
3. See specs populate in "Loaded Instrument Specs"

### Step 5: Run Backtest

Now uses **REAL broker parameters**:
- ✅ Actual margin requirements
- ✅ Real contract sizes
- ✅ Live commission rates
- ✅ Genuine swap costs

---

## 🏗️ File Structure

### New Files Created

```
ctrader-backtest/
├── broker_api.py                           # NEW: Core broker integration
├── BROKER_API_INTEGRATION.md               # NEW: Full technical docs
├── BROKER_QUICK_START.md                   # NEW: 5-min setup guide
├── BROKER_API_IMPLEMENTATION.md            # NEW: Implementation summary
└── cache/                                  # NEW: Spec caching directory
    ├── ctrader_12345678.json               # (created on first fetch)
    └── metatrader5_87654321.json           # (created on first fetch)
```

### Modified Files

```
server.py                                   # +100 lines: Broker API endpoints
ui/index.html                               # +120 lines: Broker settings UI
ui/dashboard.js                             # +140 lines: Broker functions
requirements.txt                            # Added: requests==2.31.0
```

---

## 🔧 Configuration

### Environment Variables (Recommended for Production)

Create `.env` file in project root:

```bash
# .env
CTRADER_API_KEY=your_api_key_here
CTRADER_API_SECRET=your_api_secret_here
FLASK_ENV=production
FLASK_DEBUG=0
```

Update `server.py` to use:

```python
import os
from dotenv import load_dotenv

load_dotenv()

API_KEY = os.getenv('CTRADER_API_KEY')
API_SECRET = os.getenv('CTRADER_API_SECRET')
```

### Broker Settings Reference

#### cTrader Account

```
Broker:              cTrader
Account Type:        Demo (recommended for testing)
Account ID:          Your numeric account number
Leverage:            500 (typical)
Account Currency:    USD
API Key:             From Spotware Developer Portal
API Secret:          From Spotware Developer Portal
```

#### MetaTrader 5 Account

```
Broker:              MetaTrader 5
Account Type:        Demo or Live
Account ID:          Your numeric account number
Leverage:            Auto-detected from MT5
Account Currency:    Auto-detected from MT5
Python Module:       pip install MetaTrader5
```

---

## 🚀 Deployment

### Development Environment

```bash
# Terminal 1: Start Flask server
python server.py

# Terminal 2: Open browser
start http://localhost:5000
```

### Production Environment

Recommended setup:

```bash
# Use gunicorn for production
pip install gunicorn

# Run with multiple workers
gunicorn -w 4 -b 0.0.0.0:5000 server:app

# Or use Azure/Heroku deployment
# Instructions in DEPLOYMENT.md (coming soon)
```

### Docker Deployment

```dockerfile
# Dockerfile
FROM python:3.13-slim

WORKDIR /app
COPY requirements.txt .
RUN pip install -r requirements.txt

COPY . .
ENV FLASK_APP=server.py

CMD ["gunicorn", "-b", "0.0.0.0:5000", "server:app"]
```

```bash
docker build -t ctrader-backtest .
docker run -p 5000:5000 ctrader-backtest
```

---

## 🔐 Security Checklist

### ✅ Currently Implemented

- [x] Input validation on all endpoints
- [x] CORS protection (Flask-CORS)
- [x] Error masking (no system details exposed)
- [x] Credential validation
- [x] Logging of API calls

### 🔄 Recommended Additions

- [ ] Use environment variables for credentials (see above)
- [ ] Enable HTTPS in production
- [ ] Implement rate limiting:

```python
from flask_limiter import Limiter

limiter = Limiter(app)

@app.route('/api/broker/specs', methods=['POST'])
@limiter.limit("10 per minute")
def fetch_instrument_specs():
    # ...
```

- [ ] Add authentication/login
- [ ] Audit logging for compliance
- [ ] Secrets manager integration (AWS Secrets, HashiCorp Vault, etc.)

---

## 📊 Data Storage

### Cache Location

```
cache/
├── ctrader_12345678.json        # Auto-created
├── ctrader_87654321.json        # Auto-created
└── metatrader5_11111111.json    # Auto-created
```

### Cache Format

```json
{
  "EURUSD": {
    "symbol": "EURUSD",
    "broker": "ctrader",
    "contract_size": 100000,
    "margin_requirement": 0.02,
    "pip_value": 10,
    "pip_size": 0.0001,
    "commission_per_lot": 0,
    "swap_buy": -0.5,
    "swap_sell": -0.2,
    "min_volume": 0.01,
    "max_volume": 1000,
    "fetched_at": "2025-01-06T15:30:00"
  },
  "GBPUSD": { ... }
}
```

### Cache Behavior

- **Auto-refresh**: Every 24 hours
- **Manual refresh**: Click "Fetch Specs" button
- **Offline**: Uses cached data if broker unavailable
- **Persistence**: Survives server restart

---

## 🐛 Troubleshooting

### Issue: "ModuleNotFoundError: No module named 'requests'"

**Solution:**
```bash
python -m pip install requests
```

### Issue: "Failed to connect to cTrader"

**Check:**
- ✓ API Key copied correctly (not Secret)
- ✓ API Secret copied correctly (not Key)
- ✓ Account Type matches broker (Demo/Live)
- ✓ Internet connection active
- ✓ cTrader API isn't rate-limiting

**Solution:**
```python
# Test connection manually
python
>>> from broker_api import BrokerAccount, BrokerFactory
>>> account = BrokerAccount(
...     broker='ctrader',
...     account_type='demo',
...     account_id='12345678',
...     leverage=500,
...     account_currency='USD',
...     api_key='your_key',
...     api_secret='your_secret'
... )
>>> broker = BrokerFactory.create(account)
>>> broker.connect()
True  # Success!
```

### Issue: "MetaTrader5 module not found"

**Solution:**
```bash
pip install MetaTrader5
# Restart server after install
python server.py
```

### Issue: "Spec not found for EURUSD"

**Check:**
- ✓ Broker is connected (green status dot)
- ✓ Symbol format correct: "EURUSD" not "EUR/USD"
- ✓ Broker offers this instrument
- ✓ Check Flask console for error details

**Solution:**
```python
# Test available instruments
python
>>> from broker_api import broker_manager
>>> broker = broker_manager.brokers['ctrader_12345678']
>>> # Check what symbols are available in your broker
```

### Issue: "Cache directory not writable"

**Solution:**
```bash
# Windows
icacls "cache" /grant:r "%username%":F

# Linux/Mac
chmod 755 cache
```

---

## 📈 Performance Optimization

### Cache Tuning

Modify cache duration in `broker_api.py`:

```python
# Line ~80 in broker_api.py
CACHE_DURATION = timedelta(hours=24)  # Change as needed

# For aggressive updates (every 12 hours):
CACHE_DURATION = timedelta(hours=12)

# For longer caching (48 hours):
CACHE_DURATION = timedelta(hours=48)
```

### Batch Operations

```python
# Good: Fetch multiple symbols at once
specs = broker_manager.fetch_specs([
    'EURUSD', 'GBPUSD', 'USDJPY', 'AUDUSD'
])

# Avoid: Fetch one at a time
for symbol in symbols:
    spec = broker_manager.get_instrument_spec(symbol)
```

### Async Execution (Production)

For better performance with slow APIs:

```bash
pip install APScheduler
```

Then in `server.py`:

```python
from apscheduler.schedulers.background import BackgroundScheduler

scheduler = BackgroundScheduler()

@scheduler.scheduled_job('interval', hours=24)
def refresh_all_specs():
    for broker_key in broker_manager.list_brokers():
        broker_manager.set_active_broker(broker_key)
        # Fetch specs...

scheduler.start()
```

---

## 🧪 Testing

### Unit Test Example

```python
# tests/test_broker_api.py
import unittest
from broker_api import BrokerAccount, InstrumentSpec

class TestBrokerAPI(unittest.TestCase):
    
    def test_instrument_spec_creation(self):
        spec = InstrumentSpec(
            symbol='EURUSD',
            broker='ctrader',
            contract_size=100000,
            margin_requirement=0.02,
            pip_value=10,
            pip_size=0.0001,
            commission_per_lot=0,
            swap_buy=-0.5,
            swap_sell=-0.2,
            min_volume=0.01,
            max_volume=1000,
            fetched_at='2025-01-06T15:30:00'
        )
        self.assertEqual(spec.symbol, 'EURUSD')
        self.assertEqual(spec.margin_requirement, 0.02)

if __name__ == '__main__':
    unittest.main()
```

Run tests:
```bash
python -m unittest tests/test_broker_api.py -v
```

### Integration Test

```bash
# Start server
python server.py &

# Test endpoints
curl -X POST http://localhost:5000/api/broker/list

curl -X POST http://localhost:5000/api/broker/connect \
  -H "Content-Type: application/json" \
  -d '{
    "broker": "ctrader",
    "account_type": "demo",
    "account_id": "12345678",
    "leverage": 500,
    "account_currency": "USD",
    "api_key": "key",
    "api_secret": "secret"
  }'
```

---

## 📚 Documentation

All documentation located in project root:

| File | Purpose |
|------|---------|
| [BROKER_QUICK_START.md](./BROKER_QUICK_START.md) | 5-minute setup guide |
| [BROKER_API_INTEGRATION.md](./BROKER_API_INTEGRATION.md) | Comprehensive technical docs |
| [BROKER_API_IMPLEMENTATION.md](./BROKER_API_IMPLEMENTATION.md) | Architecture & features |
| [broker_api.py](./broker_api.py) | Source code (well-commented) |
| [server.py](./server.py) | REST API implementation |
| [ui/dashboard.js](./ui/dashboard.js) | Frontend integration |

---

## 🔄 Upgrade Path

### From Previous Versions

If upgrading from manual config:

1. **Backup** old config files
2. **Update** Flask server: `pip install -r requirements.txt`
3. **Copy** new files:
   - `broker_api.py`
   - Documentation files
4. **Restart** server: `python server.py`
5. **Re-configure** brokers in dashboard
6. **Test** with demo account first

### Version Compatibility

- Python: 3.8+ (tested on 3.13.6)
- Flask: 2.3.3+
- Requests: 2.31.0+
- Browsers: Chrome/Edge/Firefox (ES6+)

---

## 🎯 Next Steps

### Today
- [ ] Restart Flask server: `python server.py`
- [ ] Test broker connection (demo account)
- [ ] Fetch specs for EURUSD
- [ ] Verify specs display correctly

### This Week
- [ ] Test both cTrader and MT5 (if available)
- [ ] Compare specs between brokers
- [ ] Run backtest with live specs
- [ ] Validate against manual trades

### This Month
- [ ] Integrate with C++ backtest engine
- [ ] Set up automated spec refresh
- [ ] Configure production deployment
- [ ] Implement secure credential storage

---

## 📞 Support

### Common Questions

**Q: Do I need both cTrader and MT5?**
A: No, either one works. Pick the broker you prefer.

**Q: Can I change leverage after connecting?**
A: Yes, reconnect with new leverage value.

**Q: Are my credentials stored securely?**
A: Currently in session memory. Use environment variables for production.

**Q: What if broker API is down?**
A: Dashboard uses cached specs from last successful fetch.

**Q: Can I use multiple brokers?**
A: Yes! Connect each one separately and switch between them.

### Getting Help

1. Check [BROKER_QUICK_START.md](./BROKER_QUICK_START.md) for common issues
2. Review [broker_api.py](./broker_api.py) comments for implementation details
3. Check Flask console output for error messages
4. Verify broker API status (cTrader OpenAPI, MT5 terminal)

---

## ✅ Verification Checklist

After setup, verify everything works:

- [ ] Server starts without errors
- [ ] Web dashboard loads at localhost:5000
- [ ] Broker Settings panel visible
- [ ] Can select broker from dropdown
- [ ] Can enter account credentials
- [ ] Connection attempt shows status message
- [ ] Fetch Specs button responds
- [ ] Specs display in UI (if connected)
- [ ] Can run backtest normally
- [ ] No JavaScript console errors
- [ ] Flask console shows healthy logs

---

## 🎉 You're All Set!

Your cTrader Backtest Engine now has **production-grade broker API integration**.

**Next: Connect a broker and fetch some specs!** 🚀

See [BROKER_QUICK_START.md](./BROKER_QUICK_START.md) for detailed walkthrough.
