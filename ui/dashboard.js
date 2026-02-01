// Global variables
let currentStrategy = 'fillup';
let equityChart = null;

// Strategy configurations with parameters
// These match the actual C++ engine strategies
const strategies = {
    fillup: {
        name: 'FillUp Oscillation',
        description: 'Adaptive Grid Strategy (XAUUSD Optimized)',
        parameters: [
            { name: 'survive_pct', label: 'Survive %', type: 'number', value: 13.0, min: 1, max: 50, step: 0.5 },
            { name: 'base_spacing', label: 'Base Spacing ($)', type: 'number', value: 1.5, min: 0.1, max: 20, step: 0.1 },
            { name: 'lookback_hours', label: 'Lookback Hours', type: 'number', value: 4.0, min: 1, max: 24, step: 0.5 }
        ]
    },
    combined: {
        name: 'Combined Ju',
        description: 'Rubber Band TP + Velocity Filter (Highest Returns)',
        parameters: [
            { name: 'survive_pct', label: 'Survive %', type: 'number', value: 12.0, min: 1, max: 50, step: 0.5 },
            { name: 'base_spacing', label: 'Base Spacing ($)', type: 'number', value: 1.0, min: 0.1, max: 20, step: 0.1 },
            { name: 'tp_multiplier', label: 'TP Multiplier', type: 'number', value: 2.0, min: 0.5, max: 10, step: 0.1 }
        ]
    },
    fillup_xagusd: {
        name: 'FillUp (Silver)',
        description: 'Percentage-based spacing for XAGUSD',
        parameters: [
            { name: 'survive_pct', label: 'Survive %', type: 'number', value: 19.0, min: 1, max: 50, step: 0.5 },
            { name: 'base_spacing_pct', label: 'Spacing %', type: 'number', value: 2.0, min: 0.1, max: 10, step: 0.1 },
            { name: 'lookback_hours', label: 'Lookback Hours', type: 'number', value: 1.0, min: 0.5, max: 24, step: 0.5 }
        ]
    },
    custom: {
        name: 'Custom Strategy',
        description: 'Manual parameter configuration',
        parameters: [
            { name: 'survive_pct', label: 'Survive %', type: 'number', value: 10.0, min: 1, max: 100, step: 0.5 },
            { name: 'base_spacing', label: 'Base Spacing', type: 'number', value: 1.0, min: 0.01, max: 100, step: 0.1 },
            { name: 'min_volume', label: 'Min Volume', type: 'number', value: 0.01, min: 0.01, max: 1, step: 0.01 },
            { name: 'max_volume', label: 'Max Volume', type: 'number', value: 10.0, min: 0.1, max: 100, step: 0.1 }
        ]
    }
};

// Initialize on page load
document.addEventListener('DOMContentLoaded', function() {
    initializeEventListeners();
    setDefaultDates();
    updateStrategyParams();
});

function initializeEventListeners() {
    // Strategy selection
    document.querySelectorAll('.strategy-option').forEach(option => {
        option.addEventListener('click', function() {
            document.querySelectorAll('.strategy-option').forEach(o => o.classList.remove('selected'));
            this.classList.add('selected');
            currentStrategy = this.getAttribute('data-strategy');
            updateStrategyParams();
        });
    });
}

function setDefaultDates() {
    const endDate = new Date();
    const startDate = new Date();
    startDate.setFullYear(startDate.getFullYear() - 1);

    document.getElementById('startDate').valueAsDate = startDate;
    document.getElementById('endDate').valueAsDate = endDate;
}

function updateStrategyParams() {
    const container = document.getElementById('strategyParams');
    const strategy = strategies[currentStrategy];

    if (!strategy || !strategy.parameters.length) {
        container.innerHTML = '';
        return;
    }

    let html = '<div class="form-group"><label>Strategy Parameters</label></div>';
    
    // Group parameters in rows
    for (let i = 0; i < strategy.parameters.length; i += 2) {
        html += '<div class="input-row">';
        
        const param1 = strategy.parameters[i];
        html += `
            <div class="form-group">
                <label>${param1.label}</label>
                <input type="number" id="${param1.name}" value="${param1.value}" 
                       min="${param1.min}" max="${param1.max}" 
                       ${param1.step ? `step="${param1.step}"` : ''}>
            </div>
        `;

        if (i + 1 < strategy.parameters.length) {
            const param2 = strategy.parameters[i + 1];
            html += `
                <div class="form-group">
                    <label>${param2.label}</label>
                    <input type="number" id="${param2.name}" value="${param2.value}" 
                           min="${param2.min}" max="${param2.max}" 
                           ${param2.step ? `step="${param2.step}"` : ''}>
                </div>
            `;
        }

        html += '</div>';
    }

    container.innerHTML = html;
}

function validateForm() {
    const errors = [];

    if (!document.getElementById('dataFile').value.trim()) {
        errors.push('Data file is required');
    }

    if (!document.getElementById('startDate').value) {
        errors.push('Start date is required');
    }

    if (!document.getElementById('endDate').value) {
        errors.push('End date is required');
    }

    if (parseFloat(document.getElementById('lotSize').value) <= 0) {
        errors.push('Lot size must be greater than 0');
    }

    const startDate = new Date(document.getElementById('startDate').value);
    const endDate = new Date(document.getElementById('endDate').value);

    if (startDate >= endDate) {
        errors.push('Start date must be before end date');
    }

    return errors;
}

function showStatus(message, type = 'info') {
    const statusEl = document.getElementById('statusMessage');
    statusEl.textContent = message;
    statusEl.className = `status-message ${type}`;
    
    if (type === 'error') {
        console.error(message);
    } else if (type === 'success') {
        console.log(message);
    }
}

async function runBacktest() {
    const errors = validateForm();

    if (errors.length > 0) {
        showStatus(errors.join('\n'), 'error');
        return;
    }

    showStatus('Starting backtest...', 'info');
    document.getElementById('loadingSpinner').classList.add('active');
    document.getElementById('results').style.display = 'none';

    try {
        // Collect form data
        const backtest = {
            strategy: currentStrategy,
            data_file: document.getElementById('dataFile').value,
            start_date: document.getElementById('startDate').value,
            end_date: document.getElementById('endDate').value,
            testing_mode: document.getElementById('testingMode').value,
            starting_balance: parseFloat(document.getElementById('startingBalance').value) || 10000,
            lot_size: parseFloat(document.getElementById('lotSize').value),
            stop_loss_pips: parseFloat(document.getElementById('stopLoss').value),
            take_profit_pips: parseFloat(document.getElementById('takeProfit').value),
            spread_pips: parseFloat(document.getElementById('spread').value),
            strategy_params: collectStrategyParams()
        };

        // Call API
        const response = await fetch('/api/backtest/run', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(backtest)
        });

        if (!response.ok) {
            throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }

        const results = await response.json();

        // Display results
        displayResults(results);
        document.getElementById('results').style.display = 'block';
        showStatus('Backtest completed successfully!', 'success');

    } catch (error) {
        showStatus(`Error: ${error.message}`, 'error');
        console.error('Backtest error:', error);
    } finally {
        document.getElementById('loadingSpinner').classList.remove('active');
    }
}

function collectStrategyParams() {
    const params = {};
    const strategy = strategies[currentStrategy];

    if (strategy && strategy.parameters) {
        strategy.parameters.forEach(param => {
            const value = document.getElementById(param.name).value;
            params[param.name] = param.type === 'number' ? parseFloat(value) : value;
        });
    }

    return params;
}

function displayResults(results) {
    // Summary metrics
    const pnl = results.total_pnl || 0;
    const returnPct = results.return_percent || 0;

    const pnlEl = document.getElementById('totalPnL');
    pnlEl.textContent = formatCurrency(pnl);
    pnlEl.className = `metric-value ${pnl >= 0 ? 'positive' : 'negative'}`;

    const returnEl = document.getElementById('returnPct');
    returnEl.textContent = `${returnPct.toFixed(2)}%`;
    returnEl.className = `metric-value ${returnPct >= 0 ? 'positive' : 'negative'}`;

    document.getElementById('winRate').textContent = `${(results.win_rate || 0).toFixed(1)}%`;
    document.getElementById('profitFactor').textContent = (results.profit_factor || 0).toFixed(2);
    document.getElementById('sharpeRatio').textContent = (results.sharpe_ratio || 0).toFixed(2);
    document.getElementById('maxDrawdown').textContent = `${(results.max_drawdown || 0).toFixed(2)}%`;
    document.getElementById('totalTrades').textContent = results.total_trades || 0;
    document.getElementById('avgWin').textContent = formatCurrency(results.average_win || 0);

    // Trades table
    displayTrades(results.trades || []);

    // Statistics table
    displayStats(results);

    // Equity curve
    if (results.equity_curve && results.equity_curve.length > 0) {
        drawEquityCurve(results.equity_curve);
    }
}

function displayTrades(trades) {
    const tbody = document.querySelector('#tradesTable tbody');
    tbody.innerHTML = '';

    trades.forEach((trade, index) => {
        const pnl = trade.pnl || 0;
        const row = `
            <tr>
                <td>${index + 1}</td>
                <td>${formatDate(trade.entry_time)}</td>
                <td>${trade.entry_price.toFixed(5)}</td>
                <td>${formatDate(trade.exit_time)}</td>
                <td>${trade.exit_price.toFixed(5)}</td>
                <td>${trade.volume.toFixed(2)}</td>
                <td style="color: ${pnl >= 0 ? '#27ae60' : '#e74c3c'}; font-weight: bold;">
                    ${formatCurrency(pnl)}
                </td>
            </tr>
        `;
        tbody.innerHTML += row;
    });

    if (trades.length === 0) {
        tbody.innerHTML = '<tr><td colspan="7" style="text-align: center; color: #999;">No trades executed</td></tr>';
    }
}

function displayStats(results) {
    const tbody = document.querySelector('#statsTable');
    tbody.innerHTML = '';

    const stats = [
        { label: 'Total Trades', value: results.total_trades || 0 },
        { label: 'Winning Trades', value: results.winning_trades || 0 },
        { label: 'Losing Trades', value: results.losing_trades || 0 },
        { label: 'Win Rate', value: `${(results.win_rate || 0).toFixed(2)}%` },
        { label: 'Average Trade', value: formatCurrency(results.average_trade || 0) },
        { label: 'Largest Win', value: formatCurrency(results.largest_win || 0) },
        { label: 'Largest Loss', value: formatCurrency(results.largest_loss || 0) },
        { label: 'Consecutive Wins', value: results.consecutive_wins || 0 },
        { label: 'Consecutive Losses', value: results.consecutive_losses || 0 },
        { label: 'Profit Factor', value: (results.profit_factor || 0).toFixed(2) },
        { label: 'Sharpe Ratio', value: (results.sharpe_ratio || 0).toFixed(2) },
        { label: 'Sortino Ratio', value: (results.sortino_ratio || 0).toFixed(2) },
        { label: 'Max Drawdown', value: `${(results.max_drawdown || 0).toFixed(2)}%` },
        { label: 'Recovery Factor', value: (results.recovery_factor || 0).toFixed(2) },
        { label: 'Margin Used', value: `${(results.margin_used_percent || 0).toFixed(2)}%` },
        { label: 'Max Margin Used', value: `${(results.max_margin_used_percent || 0).toFixed(2)}%` }
    ];

    stats.forEach(stat => {
        const row = `
            <tr>
                <td style="font-weight: 500;">${stat.label}</td>
                <td style="text-align: right;">${stat.value}</td>
            </tr>
        `;
        tbody.innerHTML += row;
    });
}

function drawEquityCurve(equityCurve) {
    const ctx = document.getElementById('equityChart');

    if (equityChart) {
        equityChart.destroy();
    }

    equityChart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: equityCurve.map((_, i) => i),
            datasets: [{
                label: 'Equity',
                data: equityCurve,
                borderColor: '#2a5298',
                backgroundColor: 'rgba(42, 82, 152, 0.1)',
                borderWidth: 2,
                fill: true,
                tension: 0.4,
                pointRadius: 0,
                pointHoverRadius: 5
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: true,
            plugins: {
                legend: {
                    display: true,
                    position: 'top'
                },
                title: {
                    display: true,
                    text: 'Equity Curve Over Time'
                }
            },
            scales: {
                y: {
                    beginAtZero: true,
                    ticks: {
                        callback: function(value) {
                            return formatCurrency(value);
                        }
                    }
                }
            }
        }
    });
}

function switchTab(tabName, event) {
    // Hide all tabs
    document.querySelectorAll('.tab-content').forEach(tab => {
        tab.classList.remove('active');
    });
    document.querySelectorAll('.tab-btn').forEach(btn => {
        btn.classList.remove('active');
    });

    // Show selected tab
    document.getElementById(tabName).classList.add('active');
    if (event && event.target) {
        event.target.classList.add('active');
    }
}

function resetForm() {
    document.getElementById('dataFile').value = 'data/EURUSD_2023.csv';
    document.getElementById('stopLoss').value = '50';
    document.getElementById('takeProfit').value = '100';
    document.getElementById('lotSize').value = '0.1';
    document.getElementById('spread').value = '2';
    document.getElementById('testingMode').value = 'bar';

    setDefaultDates();
    updateStrategyParams();

    document.getElementById('results').style.display = 'none';
    document.getElementById('statusMessage').className = 'status-message';
    document.getElementById('statusMessage').textContent = '';

    showStatus('Form reset to defaults', 'info');
}

// Utility functions
function formatCurrency(value) {
    return new Intl.NumberFormat('en-US', {
        style: 'currency',
        currency: 'USD',
        minimumFractionDigits: 2,
        maximumFractionDigits: 2
    }).format(value);
}

function formatDate(dateString) {
    if (!dateString) return '-';
    const date = new Date(dateString);
    return date.toLocaleString();
}

// ============================================================================
// Broker API Integration Functions
// ============================================================================

function updateBrokerFields() {
    const brokerType = document.getElementById('brokerType').value;
    
    document.getElementById('ctraderFields').style.display = 
        brokerType === 'ctrader' ? 'block' : 'none';
    document.getElementById('mt5Fields').style.display = 
        brokerType === 'metatrader5' ? 'block' : 'none';
}

function showBrokerStatus(message, type = 'info') {
    const statusEl = document.getElementById('brokerStatusMessage');
    statusEl.textContent = message;
    statusEl.className = `status-message ${type}`;
}

function showChartStatus(message, type = 'info') {
    const statusEl = document.getElementById('chartStatusMessage');
    statusEl.textContent = message;
    statusEl.className = `status-message ${type}`;
    statusEl.style.display = 'block';
}

async function connectBroker() {
    const brokerType = document.getElementById('brokerType').value;
    
    if (!brokerType) {
        showBrokerStatus('Please select a broker', 'error');
        return;
    }

    const brokerData = {
        broker: brokerType,
        account_type: document.getElementById('accountType').value,
        account_id: document.getElementById('accountId').value,
        leverage: document.getElementById('leverage').value,
        account_currency: document.getElementById('accountCurrency').value
    };

    // Add credentials if provided
    if (brokerType === 'ctrader') {
        const apiKey = document.getElementById('apiKey').value;
        const apiSecret = document.getElementById('apiSecret').value;
        
        if (!apiKey || !apiSecret) {
            showBrokerStatus('API Key and Secret are required for cTrader', 'error');
            return;
        }
        
        brokerData.api_key = apiKey;
        brokerData.api_secret = apiSecret;
    }

    // Add MT5 path if provided
    if (brokerType === 'metatrader5' || brokerType === 'mt5') {
        const mt5Path = document.getElementById('mt5Path').value;
        if (mt5Path) {
            brokerData.mt5_path = mt5Path;
        }
    }

    showBrokerStatus('Connecting to broker...', 'info');

    try {
        const response = await fetch('/api/broker/connect', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(brokerData)
        });

        const result = await response.json();

        if (response.ok) {
            showBrokerStatus(`✓ Connected to ${brokerType}`, 'success');
            updateBrokerStatusDisplay('online', `${brokerType} - ${brokerData.account_id}`);
            await loadConnectedBrokers();
        } else {
            // Build detailed error message
            let errorMsg = result.message || 'Connection failed';
            
            // For MT5, add helpful tips
            if (brokerType === 'metatrader5' || brokerType === 'mt5') {
                if (result.message && result.message.includes('module')) {
                    errorMsg += '\n\nFix: Open terminal and run:\npip install MetaTrader5';
                } else if (result.message && result.message.includes('terminal not running')) {
                    errorMsg += '\n\nFix: Make sure MetaTrader5 is running and logged in';
                }
            }
            
            // Show tip if available
            if (result.tip) {
                errorMsg += '\n\n' + result.tip;
            }
            
            showBrokerStatus(`✗ ${errorMsg}`, 'error');
            updateBrokerStatusDisplay('offline', 'Connection failed');
        }
    } catch (error) {
        console.error('Broker connection error:', error);
        showBrokerStatus(
            `Error: ${error.message}\n\nMake sure Flask server is running and responding with JSON. ` +
            `Check browser console (F12) for details.`,
            'error'
        );
        updateBrokerStatusDisplay('offline', 'Error');
    }
}

async function loadConnectedBrokers() {
    try {
        const response = await fetch('/api/broker/list');
        const result = await response.json();
        
        if (response.ok && result.active_broker) {
            updateBrokerStatusDisplay('online', result.active_broker);
        }
    } catch (error) {
        console.error('Error loading brokers:', error);
    }
}

function updateBrokerStatusDisplay(status, brokerInfo) {
    const statusDot = document.querySelector('.status-dot');
    const statusText = document.getElementById('brokerStatusText');
    
    if (status === 'online') {
        statusDot.classList.remove('offline');
        statusDot.classList.add('online');
        statusText.textContent = brokerInfo || 'Connected';
    } else {
        statusDot.classList.remove('online');
        statusDot.classList.add('offline');
        statusText.textContent = brokerInfo || 'Disconnected';
    }
}

async function fetchInstrumentSpecs() {
    const dataFile = document.getElementById('dataFile').value;
    
    // Extract symbol from filename (e.g., "data/EURUSD_2023.csv" -> "EURUSD")
    const match = dataFile.match(/([A-Z]{6})/);
    const symbol = match ? match[1] : null;
    
    if (!symbol) {
        showBrokerStatus('Could not extract symbol from data file name', 'error');
        return;
    }

    await fetchSymbols([symbol]);
}

async function fetchMultipleSpecs() {
    // Popular forex, indices, and commodities
    const popularSymbols = [
        'EURUSD', 'GBPUSD', 'USDJPY', 'USDCHF', 'AUDUSD', 'NZDUSD',
        'EURJPY', 'EURGBP', 'GBPJPY',
        'DAX', 'SPX', 'FTSE',
        'XAUUSD', 'XAGUSD', 'XPTUSD',
        'BRENT', 'WTI'
    ];

    showBrokerStatus(`Fetching specs for ${popularSymbols.length} instruments...`, 'info');
    await fetchSymbols(popularSymbols);
}

async function fetchAllInstruments() {
    showBrokerStatus('Fetching ALL available instruments from broker...', 'info');
    
    try {
        const response = await fetch('/api/broker/symbols', {
            method: 'GET',
            headers: { 'Content-Type': 'application/json' }
        });

        const result = await response.json();

        if (response.ok && result.symbols && result.symbols.length > 0) {
            showBrokerStatus(`Found ${result.symbols.length} instruments. Fetching specs...`, 'info');
            // Fetch specs for all symbols (do in batches to avoid overload)
            const batchSize = 50;
            for (let i = 0; i < result.symbols.length; i += batchSize) {
                const batch = result.symbols.slice(i, i + batchSize);
                await fetchSymbols(batch);
                showBrokerStatus(`Fetching batch ${Math.floor(i/batchSize) + 1}/${Math.ceil(result.symbols.length/batchSize)}...`, 'info');
            }
            showBrokerStatus(`✓ Loaded all ${result.symbols.length} instruments`, 'success');
        } else {
            showBrokerStatus(`Could not fetch instruments list`, 'error');
        }
    } catch (error) {
        console.error('Error fetching all instruments:', error);
        showBrokerStatus(`Error: ${error.message}`, 'error');
    }
}

async function fetchSymbols(symbols) {
    try {
        const response = await fetch('/api/broker/specs', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ symbols: symbols })
        });

        const result = await response.json();

        if (response.ok && result.specs && Object.keys(result.specs).length > 0) {
            const fetchedCount = Object.keys(result.specs).length;
            showBrokerStatus(`✓ Loaded specs for ${fetchedCount} instruments`, 'success');
            updateSpecsList(Object.keys(result.specs), result.specs);
            updateInstrumentSelector(result.specs);
            console.log('Loaded instrument specs:', result.specs);
        } else {
            showBrokerStatus(`Could not fetch specs`, 'error');
        }
    } catch (error) {
        console.error('Error fetching specs:', error);
        showBrokerStatus(`Error: ${error.message}`, 'error');
    }
}

function updateSpecsList(symbols, specs) {
    const list = document.getElementById('specsList');
    
    if (!specs || Object.keys(specs).length === 0) {
        list.innerHTML = '<p style="color: #999; font-size: 0.9em;">No specs loaded</p>';
        return;
    }

    let html = '<div style="font-size: 0.9em; line-height: 1.6;">';
    
    for (const [symbol, spec] of Object.entries(specs)) {
        html += `
            <div style="padding: 8px; border-bottom: 1px solid #ddd;">
                <strong>${symbol}</strong>
                <div style="color: #666; font-size: 0.85em;">
                    • Lot Size: ${spec.contract_size}<br>
                    • Margin: ${(spec.margin_requirement * 100).toFixed(1)}%<br>
                    • Pip Value: ${spec.pip_value}
                </div>
            </div>
        `;
    }
    
    html += '</div>';
    list.innerHTML = html;
}

function updateInstrumentSelector(specs) {
    const selector = document.getElementById('instrumentSelector');
    
    if (!specs || Object.keys(specs).length === 0) {
        selector.innerHTML = '<option value="">-- No specs loaded --</option>';
        return;
    }

    let html = '<option value="">-- Select Instrument --</option>';
    
    for (const symbol of Object.keys(specs).sort()) {
        html += `<option value="${symbol}">${symbol}</option>`;
    }
    
    selector.innerHTML = html;
    
    // Store specs globally for later access
    window.loadedSpecs = specs;
}

function updateSelectedInstrument() {
    const selector = document.getElementById('instrumentSelector');
    const symbol = selector.value;
    const infoDiv = document.getElementById('instrumentInfo');
    
    if (!symbol || !window.loadedSpecs) {
        infoDiv.style.display = 'none';
        return;
    }
    
    const spec = window.loadedSpecs[symbol];
    if (!spec) {
        infoDiv.style.display = 'none';
        return;
    }
    
    // Display instrument details
    document.getElementById('infoSymbol').textContent = spec.symbol;
    document.getElementById('infoContractSize').textContent = spec.contract_size.toFixed(0);
    document.getElementById('infoPipValue').textContent = spec.pip_value.toFixed(5);
    document.getElementById('infoSwap').textContent = `${spec.swap_buy.toFixed(2)} / ${spec.swap_sell.toFixed(2)}`;
    
    infoDiv.style.display = 'block';
    
    // Also update the data file field if it doesn't match
    const dataFileInput = document.getElementById('dataFile');
    if (!dataFileInput.value.includes(symbol)) {
        dataFileInput.value = `data/${symbol}_2023.csv`;
    }
}

// Diagnostic functions for troubleshooting
async function checkBrokerDiagnostics() {
    try {
        const response = await fetch('/api/broker/diagnose');
        const diagnostics = await response.json();
        
        console.group('🔍 Broker Diagnostics');
        console.log('System:', diagnostics.system);
        console.log('Modules:', diagnostics.modules);
        console.log('Brokers:', diagnostics.brokers);
        console.log('MetaTrader5:', diagnostics.metatrader5_details);
        console.groupEnd();
        
        return diagnostics;
    } catch (error) {
        console.error('Failed to fetch diagnostics:', error);
        return null;
    }
}

// Make diagnostic function available in console
window.checkDiagnostics = checkBrokerDiagnostics;

// Price history and candlestick chart functions
let priceHistoryChart = null;

async function fetchPriceHistory() {
    const symbolSelect = document.getElementById('instrumentSelector');
    const symbol = symbolSelect.value;
    
    if (!symbol) {
        showChartStatus('Please select an instrument first', 'error');
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
    
    // Prepare data for candlestick chart
    // We'll use a custom Chart.js plugin or display as a line chart with candlestick overlay
    
    // Extract OHLC data
    const timestamps = [];
    const opens = [];
    const highs = [];
    const lows = [];
    const closes = [];
    
    priceData.forEach(candle => {
        // Parse timestamp
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
            interaction: {
                intersect: false,
                mode: 'index'
            },
            plugins: {
                title: {
                    display: true,
                    text: `${symbol} - ${timeframe} Chart`,
                    font: { size: 16, weight: 'bold' }
                },
                legend: {
                    display: true,
                    position: 'top'
                }
            },
            scales: {
                y: {
                    type: 'linear',
                    display: true,
                    position: 'left',
                    title: {
                        display: true,
                        text: 'Price'
                    }
                },
                x: {
                    ticks: {
                        maxTicksLimit: 10
                    }
                }
            }
        }
    });
}

