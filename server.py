"""
Flask Web Server for cTrader Backtest Engine
Provides REST API to run backtests from the web UI
"""

from flask import Flask, render_template, request, jsonify
from flask_cors import CORS
import json
import subprocess
import os
import sys
import logging
from datetime import datetime
from pathlib import Path
import threading
import time

# Import broker API module
from broker_api import BrokerAccount, BrokerManager, InstrumentSpec
from backtest_sweep import SweepExecutor, ParameterGenerator

# Configure logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

app = Flask(__name__, static_folder='ui', static_url_path='')
CORS(app)

# Configuration
BACKTEST_EXE = r'build\backtest.exe'
PROJECT_ROOT = Path(__file__).parent
DATA_DIR = PROJECT_ROOT / 'data'
RESULTS_DIR = PROJECT_ROOT / 'results'

# Ensure results directory exists
RESULTS_DIR.mkdir(exist_ok=True)

# Initialize broker manager
broker_manager = BrokerManager()

# In-memory backtest results cache
backtest_results = {}


@app.route('/')
def index():
    """Serve the main dashboard UI"""
    return app.send_static_file('index.html')


@app.route('/api/backtest/run', methods=['POST'])
def run_backtest():
    """
    API endpoint to run a backtest
    Receives backtest configuration and returns results
    """
    try:
        config = request.get_json()
        logger.info(f"Running backtest with config: {config}")

        # Validate configuration
        errors = validate_backtest_config(config)
        if errors:
            return jsonify({'status': 'error', 'message': 'Validation failed', 'errors': errors}), 400

        # Generate backtest parameters
        backtest_id = datetime.now().strftime('%Y%m%d_%H%M%S')
        backtest_params = generate_backtest_params(config, backtest_id)

        # Run backtest in background thread to avoid blocking
        thread = threading.Thread(
            target=execute_backtest,
            args=(backtest_id, config, backtest_params)
        )
        thread.daemon = True
        thread.start()

        # For now, return placeholder results
        # In production, you'd poll for results or use WebSockets
        results = run_backtest_sync(config)

        return jsonify(results), 200

    except Exception as e:
        logger.error(f"Backtest error: {str(e)}", exc_info=True)
        return jsonify({'status': 'error', 'message': str(e)}), 500


@app.route('/api/backtest/status/<backtest_id>', methods=['GET'])
def get_backtest_status(backtest_id):
    """Get the status of a running backtest"""
    if backtest_id in backtest_results:
        return jsonify(backtest_results[backtest_id]), 200
    return jsonify({'status': 'error', 'message': 'Backtest not found'}), 404


@app.route('/api/strategies', methods=['GET'])
def get_strategies():
    """Get list of available strategies"""
    strategies = {
        'ma_crossover': {
            'name': 'MA Crossover',
            'description': 'Moving Average Crossover Strategy',
            'parameters': [
                {'name': 'fastPeriod', 'label': 'Fast MA Period', 'type': 'number', 'default': 10},
                {'name': 'slowPeriod', 'label': 'Slow MA Period', 'type': 'number', 'default': 20}
            ]
        },
        'breakout': {
            'name': 'Breakout',
            'description': 'Support/Resistance Breakout',
            'parameters': [
                {'name': 'lookback', 'label': 'Lookback Period', 'type': 'number', 'default': 20},
                {'name': 'breakoutThreshold', 'label': 'Breakout %', 'type': 'number', 'default': 0.5}
            ]
        },
        'scalping': {
            'name': 'Scalping',
            'description': 'Quick Entry/Exit Strategy',
            'parameters': [
                {'name': 'rsiPeriod', 'label': 'RSI Period', 'type': 'number', 'default': 14},
                {'name': 'rsiOverbought', 'label': 'Overbought Level', 'type': 'number', 'default': 70},
                {'name': 'rsiOversold', 'label': 'Oversold Level', 'type': 'number', 'default': 30}
            ]
        },
        'grid': {
            'name': 'Grid Trading',
            'description': 'Multi-Level Grid Orders',
            'parameters': [
                {'name': 'gridLevels', 'label': 'Grid Levels', 'type': 'number', 'default': 5},
                {'name': 'gridSpacing', 'label': 'Grid Spacing %', 'type': 'number', 'default': 1}
            ]
        }
    }
    return jsonify(strategies), 200


@app.route('/api/data/files', methods=['GET'])
def get_data_files():
    """Get list of available data files"""
    files = []
    if DATA_DIR.exists():
        for file in DATA_DIR.glob('*.csv'):
            files.append({
                'name': file.name,
                'path': f'data/{file.name}',
                'size': file.stat().st_size,
                'modified': datetime.fromtimestamp(file.stat().st_mtime).isoformat()
            })
    return jsonify({'files': files}), 200


# ============================================================================
# Broker API Endpoints
# ============================================================================

@app.route('/api/broker/connect', methods=['POST'])
def connect_broker():
    """
    Connect to a broker API
    Expects: {broker, account_type, account_id, leverage, account_currency, api_key, api_secret}
    """
    try:
        data = request.get_json()
        
        # Validate required fields
        required = ['broker', 'account_type', 'account_id', 'leverage', 'account_currency']
        if not all(field in data for field in required):
            return jsonify({'status': 'error', 'message': 'Missing required fields'}), 400
        
        broker_type = data['broker'].lower()
        
        # Special validation for MetaTrader5
        if broker_type in ['metatrader5', 'mt5']:
            # Check if MetaTrader5 module is available
            try:
                import MetaTrader5 as mt5
            except ImportError:
                return jsonify({
                    'status': 'error',
                    'message': 'MetaTrader5 Python module not installed. Run: pip install MetaTrader5',
                    'broker': broker_type,
                    'fix': 'MetaTrader5 module required for MT5 connectivity'
                }), 400
        
        # Create account
        account = BrokerAccount(
            broker=data['broker'],
            account_type=data['account_type'],
            account_id=data['account_id'],
            leverage=int(data['leverage']),
            account_currency=data['account_currency'],
            api_key=data.get('api_key'),
            api_secret=data.get('api_secret'),
            email=data.get('email'),
            password=data.get('password')
        )
        
        # Extract MT5 path if provided
        mt5_path = data.get('mt5_path')
        if mt5_path:
            logger.info(f"Using custom MT5 path: {mt5_path}")
        
        # Test connection
        if not broker_manager.add_broker(account, mt5_path=mt5_path):
            error_msg = 'Failed to connect to broker'
            
            # Check if it's a MT5 issue
            if broker_type in ['metatrader5', 'mt5']:
                error_msg = (
                    '❌ Failed to connect to MetaTrader5\n\n'
                    'Check the Flask console logs for detailed error (scroll up, look for [MT5] messages)\n\n'
                    'Common causes:\n'
                    '1. MetaTrader5 terminal NOT running\n'
                    '2. Terminal running but no account logged in\n'
                    '3. ⚠️ ACCOUNT MISMATCH: You entered account ' + str(account.account_id) + '\n'
                    '   but MT5 is logged in with a DIFFERENT account number.\n'
                    '   → Log out from MT5 and log in with account ' + str(account.account_id) + '\n'
                    '4. Python MetaTrader5 module version mismatch'
                )
            
            return jsonify({
                'status': 'error',
                'message': error_msg,
                'broker': broker_type,
                'account_attempted': str(account.account_id),
                'tip': 'Check browser console (F12) and Flask terminal output for [MT5] debug messages'
            }), 400
        
        # Set as active
        broker_key = f"{account.broker}_{account.account_id}"
        broker_manager.set_active_broker(broker_key)
        
        logger.info(f"Connected to {account.broker} - {account.account_id}")
        
        return jsonify({
            'status': 'success',
            'message': f'Connected to {account.broker}',
            'broker_key': broker_key
        }), 200
        
    except json.JSONDecodeError:
        return jsonify({
            'status': 'error',
            'message': 'Invalid JSON in request'
        }), 400
    except Exception as e:
        logger.error(f"Broker connection error: {str(e)}", exc_info=True)
        return jsonify({
            'status': 'error',
            'message': f'Connection error: {str(e)}',
            'tip': 'Check Flask console for detailed error logs'
        }), 500


@app.route('/api/broker/list', methods=['GET'])
def list_brokers():
    """Get list of connected brokers"""
    brokers = broker_manager.list_brokers()
    active = broker_manager.active_broker
    
    return jsonify({
        'brokers': brokers,
        'active_broker': active
    }), 200


@app.route('/api/broker/set_active/<broker_key>', methods=['POST'])
def set_active_broker(broker_key):
    """Set the active broker"""
    if broker_manager.set_active_broker(broker_key):
        return jsonify({'status': 'success', 'active_broker': broker_key}), 200
    return jsonify({'status': 'error', 'message': 'Broker not found'}), 404


@app.route('/api/broker/specs', methods=['POST'])
def fetch_instrument_specs():
    """
    Fetch instrument specifications from active broker
    Expects: {symbols: ['EURUSD', 'GBPUSD', ...]}
    """
    try:
        data = request.get_json()
        symbols = data.get('symbols', [])
        
        if not symbols:
            return jsonify({'status': 'error', 'message': 'No symbols provided'}), 400
        
        if not broker_manager.active_broker:
            return jsonify({'status': 'error', 'message': 'No active broker'}), 400
        
        # Fetch specs
        specs = broker_manager.fetch_specs(symbols)
        
        if not specs:
            return jsonify({'status': 'error', 'message': 'Failed to fetch specs'}), 500
        
        # Convert to JSON-serializable format
        specs_data = {
            symbol: spec.to_dict()
            for symbol, spec in specs.items()
        }
        
        return jsonify({
            'status': 'success',
            'specs': specs_data
        }), 200
        
    except Exception as e:
        logger.error(f"Error fetching specs: {str(e)}", exc_info=True)
        return jsonify({'status': 'error', 'message': str(e)}), 500


@app.route('/api/broker/symbols', methods=['GET'])
def get_all_symbols():
    """
    Get all available symbols from active broker
    Returns: {status: 'success', symbols: ['EURUSD', 'GBPUSD', ...]}
    """
    try:
        if not broker_manager.active_broker:
            return jsonify({'status': 'error', 'message': 'No active broker'}), 400
        
        symbols = broker_manager.get_all_symbols()
        
        return jsonify({
            'status': 'success',
            'symbols': symbols,
            'count': len(symbols)
        }), 200
        
    except Exception as e:
        logger.error(f"Error getting all symbols: {str(e)}", exc_info=True)
        return jsonify({'status': 'error', 'message': str(e)}), 500


@app.route('/api/broker/specs/<symbol>', methods=['GET'])
def get_instrument_spec(symbol):
    """Get cached instrument spec for a symbol"""
    spec = broker_manager.get_instrument_spec(symbol)
    
    if not spec:
        return jsonify({'status': 'error', 'message': f'Spec not found for {symbol}'}), 404
    
    return jsonify({
        'status': 'success',
        'spec': spec.to_dict()
    }), 200


# ============================================================================
# Diagnostic Endpoints
# ============================================================================

@app.route('/api/broker/diagnose', methods=['GET'])
def diagnose_broker():
    """
    Diagnose broker connectivity issues
    Returns system information and module availability
    """
    diagnostics = {
        'system': {
            'python_version': f"{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}",
            'platform': sys.platform,
            'executable': sys.executable
        },
        'modules': {
            'flask': 'available' if 'flask' in sys.modules else 'not loaded',
            'requests': 'installed' if _module_installed('requests') else 'NOT installed',
            'metatrader5': 'installed' if _module_installed('MetaTrader5') else 'NOT installed'
        },
        'brokers': {
            'connected': broker_manager.list_brokers(),
            'active': broker_manager.active_broker
        },
        'metatrader5_details': {}
    }
    
    # Check MetaTrader5 specifically
    try:
        import MetaTrader5 as mt5
        diagnostics['metatrader5_details'] = {
            'module_available': True,
            'version': mt5.__version__ if hasattr(mt5, '__version__') else 'unknown'
        }
    except ImportError as e:
        diagnostics['metatrader5_details'] = {
            'module_available': False,
            'error': str(e),
            'fix': 'Run: pip install MetaTrader5'
        }
    except Exception as e:
        diagnostics['metatrader5_details'] = {
            'module_available': False,
            'error': f"Unexpected error: {str(e)}"
        }
    
    return jsonify(diagnostics), 200


def _module_installed(module_name):
    """Check if a module is installed"""
    try:
        __import__(module_name)
        return True
    except ImportError:
        return False


def validate_backtest_config(config):
    """Validate backtest configuration"""
    errors = []

    required_fields = ['strategy', 'data_file', 'start_date', 'end_date', 'testing_mode']
    for field in required_fields:
        if field not in config or not config[field]:
            errors.append(f"Missing required field: {field}")

    # Validate dates
    try:
        start = datetime.fromisoformat(config.get('start_date', ''))
        end = datetime.fromisoformat(config.get('end_date', ''))
        if start >= end:
            errors.append("Start date must be before end date")
    except (ValueError, TypeError):
        errors.append("Invalid date format")

    # Validate numbers
    numeric_fields = ['lot_size', 'stop_loss_pips', 'take_profit_pips', 'spread_pips']
    for field in numeric_fields:
        try:
            value = float(config.get(field, 0))
            if field == 'lot_size' and value <= 0:
                errors.append("Lot size must be positive")
        except (ValueError, TypeError):
            errors.append(f"Invalid number for {field}")

    return errors


def generate_backtest_params(config, backtest_id):
    """Generate C++ command-line parameters for backtest"""
    params = {
        'id': backtest_id,
        'strategy': config['strategy'],
        'data_file': config['data_file'],
        'start_date': config['start_date'],
        'end_date': config['end_date'],
        'testing_mode': config['testing_mode'],
        'lot_size': config.get('lot_size', 0.1),
        'stop_loss': config.get('stop_loss_pips', 50),
        'take_profit': config.get('take_profit_pips', 100),
        'spread': config.get('spread_pips', 2)
    }

    # Add strategy-specific parameters
    if 'strategy_params' in config:
        params['strategy_params'] = config['strategy_params']

    return params


def execute_backtest(backtest_id, config, params):
    """Execute backtest using C++ engine"""
    try:
        logger.info(f"Starting backtest {backtest_id}")

        # Store initial status
        backtest_results[backtest_id] = {
            'status': 'running',
            'start_time': datetime.now().isoformat(),
            'config': config
        }

        # Run C++ executable with parameters
        # This is a placeholder - actual implementation would serialize params to JSON
        result = run_backtest_sync(config)

        backtest_results[backtest_id].update({
            'status': 'completed',
            'results': result,
            'end_time': datetime.now().isoformat()
        })

        logger.info(f"Backtest {backtest_id} completed")

    except Exception as e:
        logger.error(f"Backtest {backtest_id} failed: {str(e)}", exc_info=True)
        backtest_results[backtest_id] = {
            'status': 'error',
            'error': str(e),
            'end_time': datetime.now().isoformat()
        }


def run_backtest_sync(config):
    """
    Execute backtest synchronously
    Returns mock results for demo
    """
    # This is a demo implementation that returns realistic mock results
    # In production, this would call the C++ executable

    import random
    import math

    # Simulate backtest execution time
    time.sleep(1)

    # Generate realistic mock results
    num_trades = random.randint(20, 100)
    winning_trades = int(num_trades * random.uniform(0.3, 0.7))
    losing_trades = num_trades - winning_trades

    total_pnl = random.uniform(-10000, 50000)
    return_pct = random.uniform(-20, 100)
    win_rate = (winning_trades / num_trades * 100) if num_trades > 0 else 0

    # Generate equity curve
    equity_curve = [10000]  # Starting capital
    for i in range(1, 252):  # Business days in a year
        daily_return = random.gauss(0.0005, 0.02)
        equity_curve.append(equity_curve[-1] * (1 + daily_return))

    max_drawdown = calculate_max_drawdown(equity_curve)

    trades = []
    for i in range(min(10, num_trades)):  # Show first 10 trades
        entry_time = datetime(2023, 1, 1) + __import__('datetime').timedelta(days=i*20)
        exit_time = entry_time + __import__('datetime').timedelta(days=random.randint(1, 10))
        entry_price = 1.0800 + random.uniform(-0.05, 0.05)
        exit_price = entry_price + random.uniform(-0.02, 0.05)
        volume = config.get('lot_size', 0.1)
        pnl = (exit_price - entry_price) * 100000 * volume

        trades.append({
            'entry_time': entry_time.isoformat(),
            'entry_price': entry_price,
            'exit_time': exit_time.isoformat(),
            'exit_price': exit_price,
            'volume': volume,
            'pnl': pnl
        })

    results = {
        'status': 'success',
        'strategy': config['strategy'],
        'testing_mode': config['testing_mode'],
        'total_trades': num_trades,
        'winning_trades': winning_trades,
        'losing_trades': losing_trades,
        'win_rate': win_rate,
        'total_pnl': total_pnl,
        'return_percent': return_pct,
        'average_trade': total_pnl / num_trades if num_trades > 0 else 0,
        'largest_win': random.uniform(1000, 5000),
        'largest_loss': -random.uniform(500, 2000),
        'consecutive_wins': random.randint(1, 10),
        'consecutive_losses': random.randint(1, 5),
        'profit_factor': winning_trades / losing_trades if losing_trades > 0 else 0,
        'average_win': total_pnl / winning_trades if winning_trades > 0 else 0,
        'average_loss': -total_pnl / losing_trades if losing_trades > 0 else 0,
        'sharpe_ratio': random.uniform(-2, 3),
        'sortino_ratio': random.uniform(-1, 4),
        'max_drawdown': max_drawdown,
        'recovery_factor': abs(total_pnl / max_drawdown) if max_drawdown != 0 else 0,
        'equity_curve': equity_curve,
        'trades': trades
    }

    return results


def calculate_max_drawdown(equity_curve):
    """Calculate maximum drawdown percentage"""
    if not equity_curve or len(equity_curve) < 2:
        return 0

    max_equity = equity_curve[0]
    max_drawdown = 0

    for equity in equity_curve:
        if equity > max_equity:
            max_equity = equity
        drawdown = (max_equity - equity) / max_equity * 100
        if drawdown > max_drawdown:
            max_drawdown = drawdown

    return max_drawdown


@app.errorhandler(404)
def not_found(error):
    """Handle 404 errors"""
    return jsonify({'error': 'Not found'}), 404


@app.errorhandler(500)
def server_error(error):
    """Handle 500 errors"""
    logger.error(f"Server error: {str(error)}", exc_info=True)
    return jsonify({'error': 'Internal server error'}), 500


# ============================================================================
# SWEEP BACKTEST API ENDPOINTS
# ============================================================================

@app.route('/api/sweep/start', methods=['POST'])
def start_sweep():
    """
    Start a parameter sweep
    
    Request body:
    {
        "sweep_type": "grid" or "random",
        "survive_range": [0.5, 5.0, 0.5],  # grid: [min, max, step], random: [min, max]
        "size_range": [0.1, 10.0, 0.5],
        "spacing_range": [0.5, 10.0, 0.5],
        "num_combinations": 100,  # for random search only
        "data_file": "data/EURUSD_2023.csv",
        "max_workers": 4
    }
    """
    try:
        data = request.get_json()
        
        sweep_type = data.get('sweep_type', 'grid')
        survive_range = tuple(data.get('survive_range', [0.5, 5.0, 0.5]))
        size_range = tuple(data.get('size_range', [0.1, 10.0, 0.5]))
        spacing_range = tuple(data.get('spacing_range', [0.5, 10.0, 0.5]))
        data_file = data.get('data_file', r'data\EURUSD_2023.csv')
        max_workers = data.get('max_workers', 4)
        
        # Generate parameters
        if sweep_type == 'grid':
            param_gen = ParameterGenerator.grid_search(survive_range, size_range, spacing_range)
        else:
            num_combinations = data.get('num_combinations', 100)
            param_gen = ParameterGenerator.random_search(
                num_combinations,
                (survive_range[0], survive_range[1]),
                (size_range[0], size_range[1]),
                (spacing_range[0], spacing_range[1])
            )
        
        # Create executor and start sweep in background
        executor = SweepExecutor(
            backtest_exe=BACKTEST_EXE,
            data_file=data_file,
            max_workers=max_workers
        )
        
        sweep_id = f"sweep_{int(time.time())}"
        
        # Run sweep in background thread
        def run_sweep():
            try:
                executor.execute_sweep(param_gen, sweep_id)
                logger.info(f"Sweep {sweep_id} completed")
            except Exception as e:
                logger.error(f"Sweep error: {str(e)}")
        
        thread = threading.Thread(target=run_sweep, daemon=True)
        thread.start()
        
        return jsonify({
            'status': 'started',
            'sweep_id': sweep_id,
            'sweep_type': sweep_type,
        }), 200
    
    except Exception as e:
        logger.error(f"Sweep start error: {str(e)}")
        return jsonify({'status': 'error', 'message': str(e)}), 500


@app.route('/api/sweep/<sweep_id>', methods=['GET'])
def get_sweep_results(sweep_id):
    """Get results for a specific sweep"""
    try:
        executor = SweepExecutor()
        results = executor.get_sweep_results(sweep_id)
        
        if not results:
            return jsonify({'status': 'error', 'message': 'Sweep not found'}), 404
        
        return jsonify(results), 200
    
    except Exception as e:
        logger.error(f"Error fetching sweep: {str(e)}")
        return jsonify({'status': 'error', 'message': str(e)}), 500


@app.route('/api/sweep/list', methods=['GET'])
def list_sweeps():
    """List all sweeps"""
    try:
        import sqlite3
        
        executor = SweepExecutor()
        conn = sqlite3.connect(str(executor.results_db))
        cursor = conn.cursor()
        
        cursor.execute('SELECT sweep_id, created_at, status, total_combinations, completed_combinations FROM sweeps ORDER BY created_at DESC LIMIT 50')
        sweeps = cursor.fetchall()
        conn.close()
        
        sweep_list = [
            {
                'sweep_id': s[0],
                'created_at': s[1],
                'status': s[2],
                'total': s[3],
                'completed': s[4],
                'progress': (s[4] / s[3] * 100) if s[3] > 0 else 0,
            }
            for s in sweeps
        ]
        
        return jsonify({'sweeps': sweep_list}), 200
    
    except Exception as e:
        logger.error(f"Error listing sweeps: {str(e)}")
        return jsonify({'status': 'error', 'message': str(e)}), 500


@app.route('/api/sweep/<sweep_id>/best', methods=['GET'])
def get_best_strategy(sweep_id):
    """Get the best performing strategy from a sweep"""
    try:
        executor = SweepExecutor()
        results = executor.get_sweep_results(sweep_id)
        
        if not results or not results.get('best_result'):
            return jsonify({'status': 'error', 'message': 'No results found'}), 404
        
        return jsonify(results['best_result']), 200
    
    except Exception as e:
        logger.error(f"Error fetching best strategy: {str(e)}")
        return jsonify({'status': 'error', 'message': str(e)}), 500


@app.route('/api/sweep/<sweep_id>/top', methods=['GET'])
def get_top_strategies(sweep_id):
    """Get top N strategies from a sweep"""
    try:
        limit = request.args.get('limit', 10, type=int)
        
        executor = SweepExecutor()
        results = executor.get_sweep_results(sweep_id)
        
        if not results:
            return jsonify({'status': 'error', 'message': 'Sweep not found'}), 404
        
        top_results = results.get('results', [])[:limit]
        
        return jsonify({'strategies': top_results}), 200
    
    except Exception as e:
        logger.error(f"Error fetching top strategies: {str(e)}")
        return jsonify({'status': 'error', 'message': str(e)}), 500


@app.route('/api/broker/price_history/<symbol>', methods=['GET'])
def get_price_history(symbol):
    """
    Get price history (OHLC candlestick data) for a symbol
    Query params:
    - timeframe: M1, M5, M15, M30, H1, H4, D1, W1, MN1 (default: H1)
    - limit: number of candles (default: 500)
    
    Returns: {status: 'success', symbol, timeframe, data: [{timestamp, open, high, low, close, volume}, ...]}
    """
    try:
        if not broker_manager.active_broker:
            return jsonify({'status': 'error', 'message': 'No active broker'}), 400
        
        timeframe = request.args.get('timeframe', 'H1')
        limit = min(int(request.args.get('limit', 500)), 1000)  # Cap at 1000
        
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


if __name__ == '__main__':
    logger.info("Starting cTrader Backtest Engine Web Server")
    logger.info(f"Project root: {PROJECT_ROOT}")
    logger.info(f"Data directory: {DATA_DIR}")

    # Check if backtest executable exists
    if not Path(BACKTEST_EXE).exists():
        logger.warning(f"Backtest executable not found at {BACKTEST_EXE}")
        logger.info("Using mock backtest results for demo")

    # Server configuration from environment variables
    # Default to localhost for security (use FLASK_HOST=0.0.0.0 to expose to network)
    server_host = os.environ.get('FLASK_HOST', '127.0.0.1')
    server_port = int(os.environ.get('FLASK_PORT', '5000'))

    logger.info(f"Server binding to {server_host}:{server_port}")

    # Run Flask app
    app.run(
        host=server_host,
        port=server_port,
        debug=False,
        threaded=True
    )
