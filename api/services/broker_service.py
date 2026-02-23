"""
Broker service - wraps existing broker_api.py BrokerManager.
"""

import sys
import logging
from pathlib import Path
from typing import Optional

# Import from project root
sys.path.insert(0, str(Path(__file__).parent.parent.parent))
from broker_api import BrokerManager, BrokerAccount, InstrumentSpec

logger = logging.getLogger(__name__)

# Singleton broker manager
_broker_manager: Optional[BrokerManager] = None


def get_broker_manager() -> BrokerManager:
    """Get or create the singleton BrokerManager."""
    global _broker_manager
    if _broker_manager is None:
        _broker_manager = BrokerManager()
    return _broker_manager


def connect_broker(
    broker: str,
    account_type: str,
    account_id: str,
    leverage: int,
    account_currency: str,
    mt5_path: Optional[str] = None,
    password: Optional[str] = None,
    server: Optional[str] = None,
) -> tuple[bool, str]:
    """
    Connect to a broker. Returns (success, message).
    """
    manager = get_broker_manager()

    account = BrokerAccount(
        broker=broker,
        account_type=account_type,
        account_id=account_id,
        leverage=leverage,
        account_currency=account_currency,
        password=password,
    )

    if not manager.add_broker(account, mt5_path=mt5_path):
        broker_type = broker.lower()
        if broker_type in ("metatrader5", "mt5"):
            return False, (
                "Failed to connect to MetaTrader5. "
                "Check: 1) MT5 terminal is running, "
                "2) Account is logged in, "
                f"3) Account {account_id} matches the logged-in account."
            )
        return False, f"Failed to connect to {broker}"

    broker_key = f"{account.broker}_{account.account_id}"
    manager.set_active_broker(broker_key)
    logger.info(f"Connected to {account.broker} - {account.account_id}")

    return True, broker_key


def get_all_symbols() -> list[str]:
    """Get all available symbols from active broker."""
    manager = get_broker_manager()
    if not manager.active_broker:
        return []
    return manager.get_all_symbols()


def fetch_specs(symbols: list[str]) -> dict[str, dict]:
    """Fetch instrument specifications."""
    manager = get_broker_manager()
    if not manager.active_broker:
        return {}

    specs = manager.fetch_specs(symbols)
    return {sym: spec.to_dict() for sym, spec in specs.items()}


def fetch_account_info() -> dict:
    """Fetch account info from active broker."""
    manager = get_broker_manager()
    if not manager.active_broker:
        return {}
    return manager.fetch_account_info()


def fetch_price_history(
    symbol: str, timeframe: str = "H1", limit: int = 500
) -> Optional[list[dict]]:
    """Fetch OHLCV price history from active broker."""
    manager = get_broker_manager()
    if not manager.active_broker:
        return None
    return manager.fetch_price_history(symbol, timeframe, limit)


def get_connection_status() -> dict:
    """Get current broker connection status."""
    manager = get_broker_manager()
    return {
        "connected": manager.active_broker is not None,
        "active_broker": manager.active_broker,
        "brokers": manager.list_brokers(),
    }
