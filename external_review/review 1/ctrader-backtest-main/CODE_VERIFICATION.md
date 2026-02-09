# Price History Implementation - Code Verification

## Quick Reference: Key Code Changes

### 1. Broker API - Abstract Method
**File:** [broker_api.py](broker_api.py#L98)
```python
@abstractmethod
def fetch_price_history(self, symbol: str, timeframe: str = 'H1', limit: int = 500) -> Optional[List[dict]]:
    """Fetch price history (OHLC data) for a symbol"""
    pass
```

### 2. CTraderAPI Implementation
**File:** [broker_api.py](broker_api.py#L317)
```python
def fetch_price_history(self, symbol: str, timeframe: str = 'H1', limit: int = 500) -> Optional[List[dict]]:
    """Fetch price history from cTrader"""
    if not self.access_token:
        if not self.connect():
            return None
    
    try:
        headers = {
            'Authorization': f'Bearer {self.access_token}',
            'Content-Type': 'application/json'
        }
        
        # Map timeframe to cTrader format
        tf_map = {
            'M1': 'MINUTE', 'M5': 'MINUTE_5', 'M15': 'MINUTE_15',
            'M30': 'MINUTE_30', 'H1': 'HOUR', 'H4': 'HOUR_4',
            'D1': 'DAILY', 'W1': 'WEEKLY', 'MN1': 'MONTHLY'
        }
        ct_timeframe = tf_map.get(timeframe, 'HOUR')
        
        url = f"{self.base_url}/api/v1/symbols/{symbol}/quotes"
        params = {'timeframe': ct_timeframe, 'count': min(limit, 1000)}
        
        response = requests.get(url, headers=headers, params=params, timeout=10)
        
        if response.status_code == 200:
            quotes = response.json().get('data', [])
            history = []
            for quote in quotes:
                history.append({
                    'timestamp': quote.get('utcTime'),
                    'open': float(quote.get('bid', 0)),
                    'high': float(quote.get('maxBid', 0)),
                    'low': float(quote.get('minBid', 0)),
                    'close': float(quote.get('bidClose', 0)),
                    'volume': float(quote.get('volume', 0))
                })
            
            logger.info(f"[cTrader] Fetched {len(history)} candles for {symbol} {timeframe}")
            return history
    except Exception as e:
        logger.error(f"Error fetching price history from cTrader: {e}")
    
    return None
```

### 3. MetaTrader5API Implementation
**File:** [broker_api.py](broker_api.py#L595)
```python
def fetch_price_history(self, symbol: str, timeframe: str = 'H1', limit: int = 500) -> Optional[List[dict]]:
    """Fetch price history from MetaTrader 5"""
    if not self.mt5_available or not self.connect():
        return None
    
    try:
        # Map timeframe to MT5 constants
        tf_map = {
            'M1': self.mt5.TIMEFRAME_M1, 'M5': self.mt5.TIMEFRAME_M5,
            'M15': self.mt5.TIMEFRAME_M15, 'M30': self.mt5.TIMEFRAME_M30,
            'H1': self.mt5.TIMEFRAME_H1, 'H4': self.mt5.TIMEFRAME_H4,
            'D1': self.mt5.TIMEFRAME_D1, 'W1': self.mt5.TIMEFRAME_W1,
            'MN1': self.mt5.TIMEFRAME_MN1
        }
        
        mt5_timeframe = tf_map.get(timeframe, self.mt5.TIMEFRAME_H1)
        
        # Fetch rates from MT5
        rates = self.mt5.copy_rates_from_pos(symbol, mt5_timeframe, 0, limit)
        
        if rates is None or len(rates) == 0:
            logger.warning(f"No rates found for {symbol}")
            return []
        
        # Transform to standard format
        history = []
        for rate in rates:
            history.append({
                'timestamp': datetime.fromtimestamp(rate['time']).isoformat(),
                'open': float(rate['open']),
                'high': float(rate['high']),
                'low': float(rate['low']),
                'close': float(rate['close']),
                'volume': float(rate['tick_volume'])
            })
        
        logger.info(f"[MT5] Fetched {len(history)} candles for {symbol} {timeframe}")
        
        self.mt5.shutdown()
        return history
    except Exception as e:
        logger.error(f"Error fetching price history from MT5: {e}")
        try:
            self.mt5.shutdown()
        except:
            pass
        return None
```

### 4. BrokerManager Method
**File:** [broker_api.py](broker_api.py#L752)
```python
def fetch_price_history(self, symbol: str, timeframe: str = 'H1', limit: int = 500) -> Optional[List[dict]]:
    """Fetch price history from active broker"""
    if not self.active_broker or self.active_broker not in self.brokers:
        logger.error("No active broker")
        return None
    
    return self.brokers[self.active_broker].fetch_price_history(symbol, timeframe, limit)
```

### 5. REST API Endpoint
**File:** [server.py](server.py#L767)
```python
@app.route('/api/broker/price_history/<symbol>', methods=['GET'])
def get_price_history(symbol):
    """
    Get price history (OHLC candlestick data) for a symbol
    Query params: timeframe (default H1), limit (default 500, max 1000)
    """
    try:
        if not broker_manager.active_broker:
            return jsonify({'status': 'error', 'message': 'No active broker'}), 400
        
        timeframe = request.args.get('timeframe', 'H1')
        limit = min(int(request.args.get('limit', 500)), 1000)
        
        if timeframe not in ['M1', 'M5', 'M15', 'M30', 'H1', 'H4', 'D1', 'W1', 'MN1']:
            return jsonify({'status': 'error', 'message': f'Invalid timeframe: {timeframe}'}), 400
        
        logger.info(f"Fetching price history: {symbol} {timeframe} x{limit}")
        history = broker_manager.fetch_price_history(symbol, timeframe, limit)
        
        if history is None:
            return jsonify({'status': 'error', 'message': f'Failed to fetch price history for {symbol}'}), 500
        
        return jsonify({
            'status': 'success',
            'symbol': symbol,
            'timeframe': timeframe,
            'count': len(history),
            'data': history
        }), 200
        
    except Exception as e:
        logger.error(f"Error fetching price history for {symbol}: {str(e)}", exc_info=True)
        return jsonify({'status': 'error', 'message': str(e)}), 500
```

### 6. HTML Chart Panel
**File:** [ui/index.html](ui/index.html#L693)
```html
<!-- Price History Chart Panel -->
<div class="card results-section">
    <h2>📊 Price History Chart</h2>
    
    <div class="form-group">
        <label>Timeframe</label>
        <div class="input-row">
            <select id="priceTimeframe" style="flex: 1; margin-right: 10px;">
                <option value="M1">1 Minute</option>
                <option value="M5">5 Minutes</option>
                <option value="M15">15 Minutes</option>
                <option value="M30">30 Minutes</option>
                <option value="H1" selected>1 Hour</option>
                <option value="H4">4 Hours</option>
                <option value="D1">Daily</option>
                <option value="W1">Weekly</option>
                <option value="MN1">Monthly</option>
            </select>
            <button class="btn-primary" onclick="fetchPriceHistory()" style="flex: 0.5;">Load Chart</button>
        </div>
    </div>

    <div class="status-message" id="chartStatusMessage"></div>

    <div id="chartPlaceholder" style="color: #999; text-align: center; padding: 40px; font-size: 0.95em;">
        Select an instrument and click "Load Chart" to display price history
    </div>
    
    <div class="chart-container" id="priceChartContainer" style="display: none;">
        <canvas id="priceChart"></canvas>
    </div>
</div>
```

### 7. JavaScript Functions
**File:** [ui/dashboard.js](ui/dashboard.js#L773)
```javascript
let priceHistoryChart = null;

async function fetchPriceHistory() {
    const symbolSelect = document.getElementById('instrumentSelector');
    const symbol = symbolSelect.value;
    
    if (!symbol) {
        showMessage('chartStatusMessage', 'Please select an instrument first', 'error');
        return;
    }
    
    const timeframe = document.getElementById('priceTimeframe').value;
    const statusMsg = document.getElementById('chartStatusMessage');
    const chartContainer = document.getElementById('priceChartContainer');
    const placeholder = document.getElementById('chartPlaceholder');
    
    try {
        statusMsg.className = 'status-message info';
        statusMsg.textContent = `Loading price history for ${symbol} (${timeframe})...`;
        statusMsg.style.display = 'block';
        
        const response = await fetch(`/api/broker/price_history/${symbol}?timeframe=${timeframe}&limit=500`);
        
        if (!response.ok) {
            const error = await response.json();
            throw new Error(error.message || `HTTP ${response.status}`);
        }
        
        const data = await response.json();
        
        if (!data.data || data.data.length === 0) {
            throw new Error('No price history data available');
        }
        
        // Render candlestick chart
        renderCandlestickChart(data.data, symbol, timeframe);
        
        statusMsg.className = 'status-message success';
        statusMsg.textContent = `✓ Loaded ${data.count} candles for ${symbol}`;
        
        chartContainer.style.display = 'block';
        placeholder.style.display = 'none';
        
    } catch (error) {
        console.error('Error fetching price history:', error);
        statusMsg.className = 'status-message error';
        statusMsg.textContent = `✗ Error: ${error.message}`;
        statusMsg.style.display = 'block';
        
        chartContainer.style.display = 'none';
        placeholder.style.display = 'block';
    }
}

function renderCandlestickChart(priceData, symbol, timeframe) {
    const ctx = document.getElementById('priceChart').getContext('2d');
    
    // Extract OHLC data
    const timestamps = [];
    const opens = [];
    const highs = [];
    const lows = [];
    const closes = [];
    
    priceData.forEach(candle => {
        const dateStr = typeof candle.timestamp === 'string' 
            ? new Date(candle.timestamp).toLocaleString('en-US', { month: '2-digit', day: '2-digit', hour: '2-digit', minute: '2-digit' })
            : candle.timestamp;
        
        timestamps.push(dateStr);
        opens.push(parseFloat(candle.open));
        highs.push(parseFloat(candle.high));
        lows.push(parseFloat(candle.low));
        closes.push(parseFloat(candle.close));
    });
    
    // Destroy existing chart if it exists
    if (priceHistoryChart) {
        priceHistoryChart.destroy();
    }
    
    // Create multi-line chart showing OHLC
    priceHistoryChart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: timestamps,
            datasets: [
                {
                    label: 'High',
                    data: highs,
                    borderColor: 'rgba(75, 192, 75, 0.8)',
                    backgroundColor: 'rgba(75, 192, 75, 0.05)',
                    borderWidth: 1,
                    pointRadius: 0,
                    tension: 0.1
                },
                {
                    label: 'Low',
                    data: lows,
                    borderColor: 'rgba(255, 99, 99, 0.8)',
                    backgroundColor: 'rgba(255, 99, 99, 0.05)',
                    borderWidth: 1,
                    pointRadius: 0,
                    tension: 0.1
                },
                {
                    label: 'Close',
                    data: closes,
                    borderColor: 'rgba(42, 82, 152, 1)',
                    backgroundColor: 'rgba(42, 82, 152, 0.1)',
                    borderWidth: 2,
                    pointRadius: 2,
                    pointBackgroundColor: 'rgba(42, 82, 152, 0.8)',
                    tension: 0.2
                }
            ]
        },
        options: {
            responsive: true,
            maintainAspectRatio: true,
            interaction: { intersect: false, mode: 'index' },
            plugins: {
                title: {
                    display: true,
                    text: `${symbol} - ${timeframe} Chart`,
                    font: { size: 16, weight: 'bold' }
                },
                legend: { display: true, position: 'top' }
            },
            scales: {
                y: {
                    type: 'linear',
                    display: true,
                    position: 'left',
                    title: { display: true, text: 'Price' }
                },
                x: { ticks: { maxTicksLimit: 10 } }
            }
        }
    });
}
```

## Summary

**Total Changes:**
- ✅ 1 abstract method added to BrokerAPI
- ✅ 1 implementation in CTraderAPI (45 lines)
- ✅ 1 implementation in MetaTrader5API (50 lines)
- ✅ 1 method added to BrokerManager
- ✅ 1 REST API endpoint (40 lines)
- ✅ 1 HTML panel with chart components (50 lines)
- ✅ 2 JavaScript functions (160 lines)
- ✅ 2 Documentation files
- ✅ 1 Test utility

**Files Modified:**
1. [broker_api.py](broker_api.py) - +115 lines
2. [server.py](server.py) - +40 lines
3. [ui/index.html](ui/index.html) - +50 lines
4. [ui/dashboard.js](ui/dashboard.js) - +160 lines

**Files Created:**
5. [PRICE_HISTORY_GUIDE.md](PRICE_HISTORY_GUIDE.md)
6. [PRICE_HISTORY_IMPLEMENTATION.md](PRICE_HISTORY_IMPLEMENTATION.md)
7. [test_price_history.py](test_price_history.py)

**Test Status:** ✅ All 4 components verified
**Code Quality:** ✅ No syntax errors
**Integration:** ✅ Fully integrated with existing system

---
