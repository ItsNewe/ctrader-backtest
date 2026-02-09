"""
Broker API Integration Module
Fetches live instrument specifications from brokers
Supports: cTrader, MetaTrader 4/5
"""

from abc import ABC, abstractmethod
from dataclasses import dataclass, asdict
from typing import Dict, List, Optional
import json
import logging
from datetime import datetime, timedelta
from pathlib import Path
import requests

logger = logging.getLogger(__name__)

# ============================================================================
# Data Models
# ============================================================================

@dataclass
class InstrumentSpec:
    """Instrument specification with broker-specific parameters"""
    symbol: str
    broker: str
    contract_size: float          # Lot size (e.g., 100,000 for forex)
    margin_requirement: float     # As decimal (e.g., 0.02 = 2%)
    pip_value: float              # Value per pip
    pip_size: float               # Minimum price movement (e.g., 0.0001)
    commission_per_lot: float     # Commission per standard lot
    swap_buy: float               # Overnight financing (buy)
    swap_sell: float              # Overnight financing (sell)
    min_volume: float             # Minimum trade volume
    max_volume: float             # Maximum trade volume
    fetched_at: str               # When specs were fetched
    
    def to_dict(self):
        return asdict(self)
    
    @classmethod
    def from_dict(cls, data):
        return cls(**data)


@dataclass
class BrokerAccount:
    """Broker account credentials and settings"""
    broker: str                   # 'ctrader' or 'metatrader4' or 'metatrader5'
    account_type: str             # 'demo' or 'live'
    account_id: str               # Account number
    leverage: int                 # e.g., 500
    account_currency: str         # e.g., 'USD'
    
    # Credentials (store securely in production!)
    api_key: Optional[str] = None
    api_secret: Optional[str] = None
    email: Optional[str] = None
    password: Optional[str] = None
    
    def to_dict(self):
        return asdict(self)


# ============================================================================
# Abstract Broker Base Class
# ============================================================================

class BrokerAPI(ABC):
    """Abstract base class for broker API implementations"""
    
    def __init__(self, account: BrokerAccount):
        self.account = account
        self.specs_cache: Dict[str, InstrumentSpec] = {}
        self.cache_file = Path(f"cache/{account.broker}_{account.account_id}.json")
        self.cache_file.parent.mkdir(exist_ok=True)
        self.load_cache()
    
    @abstractmethod
    def connect(self) -> bool:
        """Establish connection to broker"""
        pass
    
    @abstractmethod
    def fetch_instrument_specs(self, symbols: List[str]) -> Dict[str, InstrumentSpec]:
        """Fetch instrument specifications from broker"""
        pass
    
    @abstractmethod
    def fetch_account_info(self) -> dict:
        """Fetch account information"""
        pass
    
    @abstractmethod
    def get_all_symbols(self) -> List[str]:
        """Get all available tradeable symbols"""
        pass
    
    @abstractmethod
    def fetch_price_history(self, symbol: str, timeframe: str = 'H1', limit: int = 500) -> Optional[List[dict]]:
        """
        Fetch price history (OHLC data) for a symbol
        
        Args:
            symbol: Trading symbol (e.g., 'EURUSD')
            timeframe: Candle timeframe ('M1', 'M5', 'M15', 'H1', 'H4', 'D1', 'W1', 'MN1')
            limit: Number of candles to fetch (max typically 500-1000)
            
        Returns:
            List of dicts with keys: timestamp, open, high, low, close, volume
            None if fetch fails
        """
        pass
    
    def get_instrument_spec(self, symbol: str) -> Optional[InstrumentSpec]:
        """Get cached instrument spec, fetch if missing"""
        if symbol in self.specs_cache:
            spec = self.specs_cache[symbol]
            # Check if cache is stale (24 hours)
            fetched = datetime.fromisoformat(spec.fetched_at)
            if datetime.now() - fetched < timedelta(hours=24):
                return spec
        
        # Fetch fresh specs
        specs = self.fetch_instrument_specs([symbol])
        if symbol in specs:
            self.specs_cache[symbol] = specs[symbol]
            self.save_cache()
            return specs[symbol]
        return None
    
    def load_cache(self):
        """Load cached instrument specs from disk"""
        if self.cache_file.exists():
            try:
                with open(self.cache_file, 'r') as f:
                    data = json.load(f)
                    self.specs_cache = {
                        symbol: InstrumentSpec.from_dict(spec)
                        for symbol, spec in data.items()
                    }
                logger.info(f"Loaded {len(self.specs_cache)} specs from cache")
            except Exception as e:
                logger.error(f"Error loading cache: {e}")
    
    def save_cache(self):
        """Save instrument specs to disk cache"""
        try:
            with open(self.cache_file, 'w') as f:
                data = {
                    symbol: spec.to_dict()
                    for symbol, spec in self.specs_cache.items()
                }
                json.dump(data, f, indent=2)
            logger.info(f"Saved {len(self.specs_cache)} specs to cache")
        except Exception as e:
            logger.error(f"Error saving cache: {e}")


# ============================================================================
# cTrader API Implementation
# ============================================================================

class CTraderAPI(BrokerAPI):
    """cTrader Open API integration"""
    
    API_URL = "https://api.spotware.com"
    DEMO_URL = "https://demo-api.spotware.com"
    
    def __init__(self, account: BrokerAccount):
        super().__init__(account)
        self.base_url = self.DEMO_URL if account.account_type == 'demo' else self.API_URL
        self.access_token = None
    
    def connect(self) -> bool:
        """Authenticate with cTrader API"""
        try:
            auth_url = f"{self.base_url}/auth/oauth/token"
            
            if not self.account.api_key or not self.account.api_secret:
                logger.error("Missing API credentials for cTrader")
                return False
            
            payload = {
                'grant_type': 'client_credentials',
                'client_id': self.account.api_key,
                'client_secret': self.account.api_secret
            }
            
            response = requests.post(auth_url, data=payload, timeout=10)
            
            if response.status_code == 200:
                self.access_token = response.json()['access_token']
                logger.info("Connected to cTrader API")
                return True
            else:
                logger.error(f"cTrader auth failed: {response.status_code}")
                return False
                
        except Exception as e:
            logger.error(f"cTrader connection error: {e}")
            return False
    
    def fetch_instrument_specs(self, symbols: List[str]) -> Dict[str, InstrumentSpec]:
        """Fetch specs from cTrader"""
        specs = {}
        
        if not self.access_token:
            if not self.connect():
                return specs
        
        try:
            headers = {
                'Authorization': f'Bearer {self.access_token}',
                'Content-Type': 'application/json'
            }
            
            # Fetch tradingInstruments
            url = f"{self.base_url}/api/v1/tradingInstruments"
            response = requests.get(url, headers=headers, timeout=10)
            
            if response.status_code == 200:
                instruments = response.json().get('data', [])
                
                for symbol in symbols:
                    instr = next(
                        (i for i in instruments if i.get('symbol') == symbol),
                        None
                    )
                    
                    if instr:
                        specs[symbol] = InstrumentSpec(
                            symbol=symbol,
                            broker='ctrader',
                            contract_size=float(instr.get('lotSize', 100000)),
                            margin_requirement=float(instr.get('marginFactor', 0.02)),
                            pip_value=float(instr.get('pipValue', 10)),
                            pip_size=float(instr.get('pipSize', 0.0001)),
                            commission_per_lot=float(instr.get('commission', 0)),
                            swap_buy=float(instr.get('swapBuy', 0)),
                            swap_sell=float(instr.get('swapSell', 0)),
                            min_volume=float(instr.get('minVolume', 0.01)),
                            max_volume=float(instr.get('maxVolume', 1000)),
                            fetched_at=datetime.now().isoformat()
                        )
            
        except Exception as e:
            logger.error(f"Error fetching cTrader specs: {e}")
        
        return specs
    
    def fetch_account_info(self) -> dict:
        """Fetch account information from cTrader"""
        if not self.access_token:
            if not self.connect():
                return {}
        
        try:
            headers = {
                'Authorization': f'Bearer {self.access_token}',
                'Content-Type': 'application/json'
            }
            
            url = f"{self.base_url}/api/v1/accounts/{self.account.account_id}"
            response = requests.get(url, headers=headers, timeout=10)
            
            if response.status_code == 200:
                return response.json().get('data', {})
        
        except Exception as e:
            logger.error(f"Error fetching cTrader account info: {e}")
        
        return {}
    
    def get_all_symbols(self) -> List[str]:
        """Get all available symbols from cTrader"""
        if not self.access_token:
            if not self.connect():
                return []
        
        try:
            headers = {
                'Authorization': f'Bearer {self.access_token}',
                'Content-Type': 'application/json'
            }
            
            # Get symbols endpoint (cTrader OpenAPI)
            url = f"{self.base_url}/api/v1/symbols"
            response = requests.get(url, headers=headers, timeout=10)
            
            if response.status_code == 200:
                symbols_data = response.json().get('data', [])
                symbols = [sym.get('name', '') for sym in symbols_data if sym.get('name')]
                symbols.sort()
                logger.info(f"[cTrader] Found {len(symbols)} available symbols")
                return symbols
            else:
                logger.warning(f"cTrader API returned status {response.status_code}")
        
        except Exception as e:
            logger.error(f"Error getting all symbols from cTrader: {e}")
        
        return []
    
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
            
            # Map timeframe to cTrader format if needed
            tf_map = {
                'M1': 'MINUTE', 'M5': 'MINUTE_5', 'M15': 'MINUTE_15',
                'M30': 'MINUTE_30', 'H1': 'HOUR', 'H4': 'HOUR_4',
                'D1': 'DAILY', 'W1': 'WEEKLY', 'MN1': 'MONTHLY'
            }
            ct_timeframe = tf_map.get(timeframe, 'HOUR')
            
            # cTrader OpenAPI endpoint for quotes
            url = f"{self.base_url}/api/v1/symbols/{symbol}/quotes"
            params = {
                'timeframe': ct_timeframe,
                'count': min(limit, 1000)
            }
            
            response = requests.get(url, headers=headers, params=params, timeout=10)
            
            if response.status_code == 200:
                quotes = response.json().get('data', [])
                # Transform cTrader format to standard format
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
            else:
                logger.warning(f"cTrader price history API returned status {response.status_code}")
        
        except Exception as e:
            logger.error(f"Error fetching price history from cTrader: {e}")
        
        return None


# ============================================================================
# MetaTrader 5 API Implementation (via Spotware Gateway or similar)
# ============================================================================

class MetaTrader5API(BrokerAPI):
    """MetaTrader 5 integration (simplified - requires MT5 terminal running)"""
    
    def __init__(self, account: BrokerAccount, mt5_path: Optional[str] = None):
        super().__init__(account)
        self.mt5_available = False
        self.mt5 = None
        self.import_error = None
        self.mt5_path = mt5_path  # Path to MT5 terminal.exe or folder
        self._try_import_mt5()
    
    def _try_import_mt5(self):
        """Try to import MetaTrader5 module"""
        try:
            import MetaTrader5 as mt5
            self.mt5 = mt5
            self.mt5_available = True
            self.import_error = None
            logger.info("MetaTrader5 module available")
        except ImportError as e:
            import_error_msg = f"MetaTrader5 module not available: {str(e)}"
            logger.warning(import_error_msg + " - install with: pip install MetaTrader5")
            self.mt5_available = False
            self.import_error = import_error_msg
        except Exception as e:
            error_msg = f"Unexpected error importing MetaTrader5: {str(e)}"
            logger.error(error_msg)
            self.mt5_available = False
            self.import_error = error_msg
    
    def connect(self) -> bool:
        """Connect to MetaTrader 5 terminal"""
        if not self.mt5_available:
            error_msg = self.import_error or "MetaTrader5 module not installed"
            logger.error(f"Cannot connect to MT5: {error_msg}")
            return False
        
        try:
            # Initialize MT5 connection with optional custom path
            init_kwargs = {}
            if self.mt5_path:
                # Path can be either the terminal.exe file or the terminal folder
                init_kwargs['path'] = self.mt5_path
                logger.info(f"Using custom MT5 path: {self.mt5_path}")
            
            logger.info(f"[MT5] Attempting to initialize MT5... (init_kwargs: {init_kwargs})")
            if not self.mt5.initialize(**init_kwargs):
                error_code = self.mt5.last_error()
                logger.error(f"[MT5] INIT FAILED with code: {error_code}")
                if self.mt5_path:
                    logger.error(f"[MT5] Failed to initialize MT5 at path: {self.mt5_path}")
                return False
            
            logger.info(f"[MT5] MT5 initialized successfully")
            
            # Verify account exists and matches
            try:
                logger.info(f"[MT5] Requesting account info from MT5...")
                account_info = self.mt5.account_info()
                
                if account_info is None:
                    logger.error(f"[MT5] VALIDATION FAILED: account_info is None")
                    logger.error(f"[MT5] Requested account ID: {self.account.account_id}")
                    logger.error(f"[MT5] This means no account is logged in to MT5 terminal")
                    self.mt5.shutdown()
                    return False
                
                # Get the logged-in account details
                logged_in_account = int(account_info.login)
                requested_account = int(self.account.account_id)
                
                logger.info(f"[MT5] Account info received:")
                logger.info(f"[MT5]   - Logged-in account: {logged_in_account}")
                logger.info(f"[MT5]   - Requested account: {requested_account}")
                logger.info(f"[MT5]   - Server: {account_info.server}")
                logger.info(f"[MT5]   - Company: {account_info.company}")
                
                # CRITICAL: Validate account matches
                if logged_in_account != requested_account:
                    logger.error(f"[MT5] ❌ VALIDATION FAILED: Account mismatch!")
                    logger.error(f"[MT5]   - MT5 terminal is logged in as: {logged_in_account}")
                    logger.error(f"[MT5]   - But we requested: {requested_account}")
                    logger.error(f"[MT5]   - User must log into account {requested_account} in MT5 terminal")
                    self.mt5.shutdown()
                    return False
                
                logger.info(f"[MT5] ✓ VALIDATION SUCCESS: Account {logged_in_account} matches!")
                logger.info(f"[MT5] Successfully connected to MT5 account {logged_in_account}")
                return True
                
            except Exception as e:
                logger.error(f"[MT5] EXCEPTION during account validation: {str(e)}", exc_info=True)
                try:
                    self.mt5.shutdown()
                except:
                    pass
                return False
        
        except Exception as e:
            logger.error(f"[MT5] CRITICAL ERROR in connect(): {str(e)}", exc_info=True)
            return False
    
    def fetch_instrument_specs(self, symbols: List[str]) -> Dict[str, InstrumentSpec]:
        """Fetch specs from MetaTrader 5"""
        specs = {}
        
        if not self.mt5_available:
            logger.error("MetaTrader5 module not available")
            return specs
        
        if not self.connect():
            logger.error("Failed to connect to MetaTrader5")
            return specs
        
        try:
            logger.info(f"Fetching specs for symbols: {symbols}")
            
            for symbol in symbols:
                try:
                    # Get symbol info
                    symbol_info = self.mt5.symbol_info(symbol)
                    
                    if symbol_info is None:
                        logger.warning(f"Symbol '{symbol}' not found in MT5 - check symbol name and broker")
                        continue
                    
                    # Validate required fields exist
                    if (not hasattr(symbol_info, 'trade_contract_size') or 
                        not hasattr(symbol_info, 'trade_tick_value') or
                        not hasattr(symbol_info, 'point')):
                        logger.warning(f"Symbol {symbol} missing required fields in MT5")
                        continue
                    
                    # Create spec with safe conversions
                    try:
                        margin_calc = (
                            float(symbol_info.margin_initial / symbol_info.ask) 
                            if symbol_info.ask and symbol_info.ask > 0 
                            else 0.02
                        )
                    except (ZeroDivisionError, TypeError):
                        margin_calc = 0.02
                    
                    specs[symbol] = InstrumentSpec(
                        symbol=symbol,
                        broker='metatrader5',
                        contract_size=float(symbol_info.trade_contract_size),
                        margin_requirement=margin_calc,
                        pip_value=float(symbol_info.trade_tick_value),
                        pip_size=float(symbol_info.point),
                        commission_per_lot=float(getattr(symbol_info, 'commission', 0.0)),
                        swap_buy=float(symbol_info.swap_long),
                        swap_sell=float(symbol_info.swap_short),
                        min_volume=float(symbol_info.volume_min),
                        max_volume=float(symbol_info.volume_max),
                        fetched_at=datetime.now().isoformat()
                    )
                    logger.info(f"Successfully fetched spec for {symbol}")
                    
                except (AttributeError, TypeError, ValueError) as e:
                    logger.error(f"Error processing symbol {symbol}: {str(e)}")
                    continue
            
            logger.info(f"Fetched {len(specs)} out of {len(symbols)} symbols")
            self.mt5.shutdown()
        
        except Exception as e:
            logger.error(f"Error fetching MT5 specs: {str(e)}", exc_info=True)
            try:
                self.mt5.shutdown()
            except:
                pass
        
        return specs
    
    def fetch_account_info(self) -> dict:
        """Fetch account information from MetaTrader 5"""
        if not self.mt5_available or not self.connect():
            return {}
        
        try:
            account_info = self.mt5.account_info()
            
            if account_info is None:
                logger.error(f"Failed to get account info: {self.mt5.last_error()}")
                return {}
            
            self.mt5.shutdown()
            
            return {
                'balance': account_info.balance,
                'equity': account_info.equity,
                'free_margin': account_info.free_margin,
                'leverage': account_info.leverage,
                'currency': account_info.currency
            }
        
        except Exception as e:
            logger.error(f"Error fetching MT5 account info: {e}")
        
        return {}
    
    def get_all_symbols(self) -> List[str]:
        """Get all available symbols from MetaTrader 5"""
        if not self.mt5_available or not self.connect():
            logger.warning("MT5 not available, returning empty symbol list")
            return []
        
        try:
            # Get all symbols from MT5
            symbols = self.mt5.symbols_get()
            
            if symbols is None or len(symbols) == 0:
                logger.warning("No symbols found in MT5")
                return []
            
            symbol_names = [sym.name for sym in symbols]
            logger.info(f"[MT5] Found {len(symbol_names)} available symbols")
            
            # Sort for display
            symbol_names.sort()
            
            self.mt5.shutdown()
            return symbol_names
        
        except Exception as e:
            logger.error(f"Error getting all symbols from MT5: {e}")
            try:
                self.mt5.shutdown()
            except:
                pass
            return []
    
    def fetch_price_history(self, symbol: str, timeframe: str = 'H1', limit: int = 500) -> Optional[List[dict]]:
        """Fetch price history from MetaTrader 5"""
        if not self.mt5_available or not self.connect():
            return None
        
        try:
            # Map timeframe to MT5 constants
            tf_map = {
                'M1': self.mt5.TIMEFRAME_M1,
                'M5': self.mt5.TIMEFRAME_M5,
                'M15': self.mt5.TIMEFRAME_M15,
                'M30': self.mt5.TIMEFRAME_M30,
                'H1': self.mt5.TIMEFRAME_H1,
                'H4': self.mt5.TIMEFRAME_H4,
                'D1': self.mt5.TIMEFRAME_D1,
                'W1': self.mt5.TIMEFRAME_W1,
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


# ============================================================================
# Broker Factory
# ============================================================================

class BrokerFactory:
    """Factory for creating broker API instances"""
    
    BROKERS = {
        'ctrader': CTraderAPI,
        'metatrader5': MetaTrader5API,
        'mt5': MetaTrader5API
    }
    
    @staticmethod
    def create(account: BrokerAccount, mt5_path: Optional[str] = None) -> Optional[BrokerAPI]:
        """Create broker API instance"""
        broker_class = BrokerFactory.BROKERS.get(account.broker.lower())
        
        if not broker_class:
            logger.error(f"Unknown broker: {account.broker}")
            return None
        
        # Pass mt5_path only to MetaTrader5API
        if broker_class == MetaTrader5API:
            return broker_class(account, mt5_path=mt5_path)
        else:
            return broker_class(account)


# ============================================================================
# Broker Manager (High-level interface)
# ============================================================================

class BrokerManager:
    """Manages multiple broker connections and caches"""
    
    def __init__(self):
        self.brokers: Dict[str, BrokerAPI] = {}
        self.active_broker: Optional[str] = None
        self.cache_file = Path("broker_connections.json")
    
    def add_broker(self, account: BrokerAccount, mt5_path: Optional[str] = None) -> bool:
        """Add and test broker connection"""
        logger.info(f"[ADD_BROKER] Creating broker for {account.broker}, account {account.account_id}")
        broker = BrokerFactory.create(account, mt5_path=mt5_path)
        
        if not broker:
            logger.error(f"[ADD_BROKER] ✗ Failed to create broker instance for {account.broker}")
            return False
        
        logger.info(f"[ADD_BROKER] Calling broker.connect() for account {account.account_id}")
        connect_result = broker.connect()
        logger.info(f"[ADD_BROKER] broker.connect() returned: {connect_result}")
        
        if not connect_result:
            logger.error(f"[ADD_BROKER] ✗ Failed to connect to {account.broker} account {account.account_id}")
            return False
        
        key = f"{account.broker}_{account.account_id}"
        self.brokers[key] = broker
        logger.info(f"[ADD_BROKER] ✓ Successfully added broker: {key}")
        return True
    
    def set_active_broker(self, key: str) -> bool:
        """Set active broker for spec fetching"""
        if key not in self.brokers:
            logger.error(f"Broker not found: {key}")
            return False
        
        self.active_broker = key
        logger.info(f"Active broker: {key}")
        return True
    
    def get_instrument_spec(self, symbol: str) -> Optional[InstrumentSpec]:
        """Get instrument spec from active broker"""
        if not self.active_broker or self.active_broker not in self.brokers:
            logger.error("No active broker")
            return None
        
        return self.brokers[self.active_broker].get_instrument_spec(symbol)
    
    def fetch_specs(self, symbols: List[str]) -> Dict[str, InstrumentSpec]:
        """Fetch multiple instrument specs"""
        if not self.active_broker or self.active_broker not in self.brokers:
            logger.error("No active broker")
            return {}
        
        broker = self.brokers[self.active_broker]
        specs = broker.fetch_instrument_specs(symbols)
        broker.specs_cache.update(specs)
        broker.save_cache()
        return specs
    
    def get_all_symbols(self) -> List[str]:
        """Get all available symbols from active broker"""
        if not self.active_broker or self.active_broker not in self.brokers:
            logger.error("No active broker")
            return []
        
        return self.brokers[self.active_broker].get_all_symbols()
    
    def fetch_price_history(self, symbol: str, timeframe: str = 'H1', limit: int = 500) -> Optional[List[dict]]:
        """Fetch price history from active broker"""
        if not self.active_broker or self.active_broker not in self.brokers:
            logger.error("No active broker")
            return None
        
        return self.brokers[self.active_broker].fetch_price_history(symbol, timeframe, limit)
    
    def list_brokers(self) -> List[str]:
        """List all configured brokers"""
        return list(self.brokers.keys())
    
    def save_config(self):
        """Save broker configuration (credentials stored securely in production!)"""
        try:
            data = {
                'active_broker': self.active_broker,
                'brokers': {
                    key: asdict(broker.account)
                    for key, broker in self.brokers.items()
                }
            }
            with open(self.cache_file, 'w') as f:
                json.dump(data, f, indent=2)
            logger.info("Broker configuration saved")
        except Exception as e:
            logger.error(f"Error saving config: {e}")


# Global instance
broker_manager = BrokerManager()
