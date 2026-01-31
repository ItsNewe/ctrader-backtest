# Broker API Integration Guide

## Overview

The cTrader Backtest Engine now includes **Broker API Integration** for fetching live instrument specifications directly from your broker. This enables **production-grade backtesting** with:

- ✅ Real broker margin requirements
- ✅ Accurate contract sizes and pip values
- ✅ Live commission and swap data
- ✅ Account leverage settings
- ✅ Cached specs for offline use

## Features

### 1. **Supported Brokers**

| Broker | Type | Status | API Required |
|--------|------|--------|--------------|
| cTrader | OpenAPI | ✅ Full Support | Yes - API Key/Secret |
| MetaTrader 5 | Terminal | ✅ Full Support | No - Local Connection |

### 2. **Data Fetching**

Automatically fetches and caches:
- Contract size (lot size)
- Margin requirement percentage
- Pip value and size
- Commission per lot
- Swap rates (buy/sell)
- Min/max volume

### 3. **Intelligent Caching**

- **Auto-refresh**: Specs refresh every 24 hours
- **Persistent storage**: Cached in JSON format locally
- **Per-broker caches**: Separate caches for each broker/account
- **Offline mode**: Uses cached specs if broker unavailable

### 4. **Credential Security**

⚠️ **Current Implementation**: Credentials stored in memory during session
🔒 **Production Recommended**: Use environment variables or secure credential manager

```python
# Environment-based credentials (recommended for production)
CTRADER_API_KEY = os.getenv('CTRADER_API_KEY')
CTRADER_API_SECRET = os.getenv('CTRADER_API_SECRET')
```

## Getting Started

### Step 1: Connect to Broker

#### Option A: cTrader (API-based)

1. **Get API Credentials**:
   - Go to [Spotware Developer Portal](https://spotware.com/open-api)
   - Create an application
   - Get API Key and API Secret

2. **In Dashboard**:
   - Select "cTrader" from Broker dropdown
   - Choose "Demo" or "Live"
   - Enter Account ID
   - Set Leverage (typically 500 for major pairs)
   - Enter API Key and API Secret
   - Click "🔗 Connect Broker"

#### Option B: MetaTrader 5 (Local Terminal)

1. **Setup**:
   - Install MetaTrader5: `pip install MetaTrader5`
   - Launch MetaTrader 5 terminal
   - Log in to your account
   - Keep terminal running

2. **In Dashboard**:
   - Select "MetaTrader 5" from Broker dropdown
   - Enter Account ID
   - Set Leverage
   - Click "🔗 Connect Broker"

### Step 2: Fetch Instrument Specs

1. Load a data file (e.g., "data/EURUSD_2023.csv")
2. Click "📥 Fetch Specs" button
3. Symbol is auto-extracted from filename (e.g., "EURUSD")
4. Specs display in "Loaded Instrument Specs" section

### Step 3: Run Backtest with Live Specs

Your backtest now uses **real broker parameters**:
- Margin calculations based on actual broker requirements
- Position sizing correct for leverage
- Spreads and commission accurate
- Swap rates applied correctly

## Architecture

### Module Structure

```
broker_api.py
├── InstrumentSpec (dataclass)
├── BrokerAccount (dataclass)
├── BrokerAPI (abstract base)
├── CTraderAPI (implementation)
├── MetaTrader5API (implementation)
├── BrokerFactory (factory pattern)
└── BrokerManager (high-level interface)
```

### Data Flow

```
┌─────────────────────┐
│   Web Dashboard     │
│  (index.html +      │
│  dashboard.js)      │
└──────────┬──────────┘
           │ API Calls
           ▼
┌─────────────────────┐
│   Flask Server      │
│  (server.py)        │
│  ↓ REST Endpoints   │
│  • /api/broker/*    │
└──────────┬──────────┘
           │ Orchestration
           ▼
┌─────────────────────┐
│  BrokerManager      │
│  (broker_api.py)    │
└──────────┬──────────┘
           │ Adapter Pattern
         ┌─┴─┐
         ▼   ▼
    ┌─────┐ ┌──────────┐
    │cTAPI│ │MT5 API   │
    └─────┘ └──────────┘
         ↓   ↓
    ┌──────┬──────┐
    │      ↓      │
    │  Broker    │
    │  Servers   │
    └───────────┘
```

## API Endpoints

### 1. Connect Broker

**POST** `/api/broker/connect`

```json
{
  "broker": "ctrader",          // or "metatrader5"
  "account_type": "demo",       // or "live"
  "account_id": "12345678",
  "leverage": 500,
  "account_currency": "USD",
  "api_key": "your-key",        // cTrader only
  "api_secret": "your-secret"   // cTrader only
}
```

**Response:**
```json
{
  "status": "success",
  "message": "Connected to ctrader",
  "broker_key": "ctrader_12345678"
}
```

### 2. List Connected Brokers

**GET** `/api/broker/list`

**Response:**
```json
{
  "brokers": ["ctrader_12345678", "metatrader5_87654321"],
  "active_broker": "ctrader_12345678"
}
```

### 3. Set Active Broker

**POST** `/api/broker/set_active/<broker_key>`

**Response:**
```json
{
  "status": "success",
  "active_broker": "ctrader_12345678"
}
```

### 4. Fetch Instrument Specs

**POST** `/api/broker/specs`

```json
{
  "symbols": ["EURUSD", "GBPUSD", "USDJPY"]
}
```

**Response:**
```json
{
  "status": "success",
  "specs": {
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
      "fetched_at": "2025-01-06T10:30:00"
    }
  }
}
```

### 5. Get Single Spec

**GET** `/api/broker/specs/<symbol>`

**Response:**
```json
{
  "status": "success",
  "spec": { ... }
}
```

## Using Specs in Backtest

### BacktestConfig Integration

Once specs are loaded, update your backtest configuration:

```python
from broker_api import broker_manager

# Get live specs
spec = broker_manager.get_instrument_spec('EURUSD')

# Create backtest config with broker specs
config = BacktestConfig(
    data_file='data/EURUSD_2023.csv',
    strategy='ma_crossover',
    # Use broker specs
    contract_size=spec.contract_size,
    margin_requirement=spec.margin_requirement,
    pip_value=spec.pip_value,
    commission=spec.commission_per_lot,
    swap_buy=spec.swap_buy,
    swap_sell=spec.swap_sell
)
```

### Python Backend Integration

```python
# In your backtest execution
def execute_backtest_with_live_specs(config_dict):
    # Fetch live specs from active broker
    symbol = extract_symbol(config_dict['data_file'])
    spec = broker_manager.get_instrument_spec(symbol)
    
    if spec:
        # Use live specs
        config_dict['margin_requirement'] = spec.margin_requirement
        config_dict['contract_size'] = spec.contract_size
        config_dict['commission'] = spec.commission_per_lot
    
    # Continue with backtest...
```

## Cache Management

### Cache Location

Specs are cached in: `cache/{broker}_{account_id}.json`

Example structure:
```
cache/
├── ctrader_12345678.json
└── metatrader5_87654321.json
```

### Cache Format

```json
{
  "EURUSD": {
    "symbol": "EURUSD",
    "broker": "ctrader",
    "contract_size": 100000,
    ...
  },
  "GBPUSD": {
    ...
  }
}
```

### Refresh Cache

1. **Automatic**: Specs refresh after 24 hours
2. **Manual**: Click "📥 Fetch Specs" in dashboard
3. **Force**: Delete cache file and re-fetch

## cTrader API Setup

### Prerequisites

- cTrader account with OpenAPI access
- API credentials from Spotware

### Step-by-Step

1. **Sign up for OpenAPI**:
   ```
   https://spotware.com/open-api
   ```

2. **Create Application**:
   - Name: "cTrader Backtest"
   - Type: "Desktop"
   - Redirect URL: `http://localhost:5000`

3. **Get Credentials**:
   - Copy Client ID (API Key)
   - Copy Client Secret (API Secret)

4. **Add to Dashboard**:
   - Paste into broker connection form
   - Click "Connect"

### API Limitations

- Rate limits: 100 requests/minute
- Reconnect if token expires (handled automatically)
- Demo/Live accounts have separate APIs

## MetaTrader 5 Setup

### Prerequisites

- MetaTrader 5 terminal installed
- Account logged in
- Python module: `pip install MetaTrader5`

### Configuration

```python
# Automatic detection
# MT5 will connect to the logged-in account
# No credentials needed - uses local terminal
```

### Limitations

- Requires MT5 terminal running
- Only works with logged-in account
- Cannot switch accounts without restarting terminal

## Production Recommendations

### 1. **Secure Credentials**

❌ **Don't do this**:
```javascript
// Don't hardcode credentials
const apiKey = "abc123..."; // SECURITY RISK!
```

✅ **Do this**:
```python
# Use environment variables
import os
API_KEY = os.getenv('CTRADER_API_KEY')
API_SECRET = os.getenv('CTRADER_API_SECRET')
```

### 2. **Credential Management**

```bash
# Set environment variables (Windows)
set CTRADER_API_KEY=your_key
set CTRADER_API_SECRET=your_secret

# Or use .env file with python-dotenv
pip install python-dotenv
```

### 3. **HTTPS Deployment**

When deploying to production:
- Use HTTPS for API communication
- Implement password hashing for UI login
- Use SSL certificates

### 4. **Rate Limiting**

```python
# Add rate limiting
from flask_limiter import Limiter
limiter = Limiter(app)

@app.route('/api/broker/specs', methods=['POST'])
@limiter.limit("10 per minute")
def fetch_instrument_specs():
    # ... 
```

### 5. **Audit Logging**

```python
# Log all broker API calls
logger.info(f"Fetched specs for {symbol} from {broker}")
logger.warning(f"Failed to connect to {broker}: {error}")
```

## Troubleshooting

### Issue: "Failed to connect to broker"

**cTrader**:
- Verify API Key and Secret are correct
- Check if using Demo/Live correctly
- Ensure IP is whitelisted in Spotware dashboard

**MT5**:
- Ensure MetaTrader 5 is running
- Check that account is logged in
- Install MetaTrader5: `pip install MetaTrader5`

### Issue: "Spec not found for symbol"

- Verify symbol exists in the broker (e.g., "EURUSD" not "EUR/USD")
- Check broker supports the instrument
- Manual specs entry option coming soon

### Issue: "Cache file permission denied"

- Ensure `cache/` directory is writable
- Check file permissions: `chmod 755 cache/`
- Try removing old cache files manually

### Issue: "API rate limit exceeded"

- Wait a few minutes
- Reduce frequency of spec fetches
- Consider caching specs longer than 24h

## Advanced Usage

### Custom Broker Integration

Add support for a new broker:

```python
class MyBrokerAPI(BrokerAPI):
    """Custom broker API implementation"""
    
    def connect(self) -> bool:
        # Your connection logic
        pass
    
    def fetch_instrument_specs(self, symbols: List[str]) -> Dict[str, InstrumentSpec]:
        # Your spec fetching logic
        return specs
    
    def fetch_account_info(self) -> dict:
        # Your account info logic
        return account_info

# Register in factory
BrokerFactory.BROKERS['mybroker'] = MyBrokerAPI
```

### Backtesting with Multiple Brokers

```python
# Compare same strategy across brokers
brokers = ['ctrader_demo', 'metatrader5_live']

results = {}
for broker_key in brokers:
    broker_manager.set_active_broker(broker_key)
    
    specs = broker_manager.fetch_specs(['EURUSD'])
    results[broker_key] = run_backtest_with_specs(specs)

# Compare results
compare_results(results)
```

### Spec Update Schedule

Create a scheduled task to update specs:

```python
from apscheduler.schedulers.background import BackgroundScheduler

scheduler = BackgroundScheduler()

@scheduler.scheduled_job('interval', hours=24)
def refresh_all_specs():
    for broker_key in broker_manager.list_brokers():
        broker_manager.set_active_broker(broker_key)
        broker_manager.fetch_specs(['EURUSD', 'GBPUSD', 'USDJPY'])
        logger.info(f"Refreshed specs for {broker_key}")

scheduler.start()
```

## Performance Considerations

### Caching Strategy

- **Default**: 24-hour cache validity
- **Customize**: Modify `BrokerAPI.get_instrument_spec()`

```python
# Update cache duration
CACHE_DURATION = timedelta(hours=12)  # Refresh every 12 hours
```

### Batch Fetching

```python
# Efficient: Fetch multiple symbols in one request
symbols = ['EURUSD', 'GBPUSD', 'USDJPY', 'AUDUSD']
specs = broker_manager.fetch_specs(symbols)

# Inefficient: One request per symbol
for symbol in symbols:
    spec = broker_manager.get_instrument_spec(symbol)
```

### Async Operations

For production, use async fetch to avoid blocking:

```python
import asyncio

async def fetch_specs_async(symbols):
    tasks = [
        asyncio.create_task(
            broker_manager.get_instrument_spec(symbol)
        )
        for symbol in symbols
    ]
    return await asyncio.gather(*tasks)
```

## API Response Times

Expected latencies:

| Operation | Time |
|-----------|------|
| Connect to broker | 1-2 seconds |
| Fetch specs (cached) | ~100ms |
| Fetch specs (live) | 0.5-2 seconds |
| Cache refresh | 1-5 seconds |

## Next Steps

1. ✅ **Configure broker credentials**
2. ✅ **Fetch instrument specs**
3. ✅ **Run backtest with live specs**
4. 📊 **Compare results across brokers**
5. 🔄 **Set up automated spec refresh**
6. 📈 **Integrate with C++ backtest engine**

## Resources

- [cTrader OpenAPI Docs](https://spotware.com/open-api)
- [MetaTrader5 Python Package](https://www.mql5.com/en/docs/integration/python)
- [Broker Settings Schema](./broker_api.py)
- [API Reference](./server.py) (Broker endpoints)

## Support

For issues or questions:
1. Check troubleshooting section above
2. Review logs in Flask console
3. Verify broker API status
4. Test with demo account first
5. Contact broker support if API unavailable
