"""Broker connection and instrument endpoints."""

import logging
from fastapi import APIRouter

from api.models.broker import BrokerConnectRequest, FetchSpecsRequest
from api.services import broker_service

logger = logging.getLogger(__name__)
router = APIRouter(prefix="/api/broker", tags=["broker"])


@router.post("/connect")
async def connect_broker(req: BrokerConnectRequest):
    """Connect to a broker (MT5 or cTrader)."""
    try:
        success, result = broker_service.connect_broker(
            broker=req.broker,
            account_type=req.account_type,
            account_id=req.account_id,
            leverage=req.leverage,
            account_currency=req.account_currency,
            mt5_path=req.mt5_path,
            password=req.password,
            server=req.server,
        )

        if success:
            return {"status": "success", "message": f"Connected to {req.broker}", "broker_key": result}
        else:
            return {"status": "error", "message": result}

    except Exception as e:
        logger.error(f"Broker connection error: {str(e)}", exc_info=True)
        return {"status": "error", "message": str(e)}


@router.get("/status")
async def connection_status():
    """Get current broker connection status."""
    return broker_service.get_connection_status()


@router.get("/symbols")
async def get_all_symbols():
    """Get all available symbols from active broker."""
    symbols = broker_service.get_all_symbols()
    return {"status": "success", "symbols": symbols, "count": len(symbols)}


@router.post("/specs")
async def fetch_specs(req: FetchSpecsRequest):
    """Fetch instrument specifications for given symbols."""
    specs = broker_service.fetch_specs(req.symbols)
    if not specs:
        return {"status": "error", "message": "Failed to fetch specs"}
    return {"status": "success", "specs": specs}


@router.get("/specs/{symbol}")
async def get_spec(symbol: str):
    """Get cached instrument spec for a single symbol."""
    specs = broker_service.fetch_specs([symbol])
    if symbol not in specs:
        return {"status": "error", "message": f"Spec not found for {symbol}"}
    return {"status": "success", "spec": specs[symbol]}


@router.get("/account")
async def get_account_info():
    """Get account info from active broker."""
    info = broker_service.fetch_account_info()
    if not info:
        return {"status": "error", "message": "No active broker or failed to fetch"}
    return {"status": "success", "account": info}


@router.get("/price_history/{symbol}")
async def get_price_history(symbol: str, timeframe: str = "H1", limit: int = 500):
    """Get OHLCV price history for a symbol."""
    history = broker_service.fetch_price_history(symbol, timeframe, limit)
    if history is None:
        return {"status": "error", "message": f"Failed to fetch price history for {symbol}"}
    return {"status": "success", "symbol": symbol, "timeframe": timeframe, "candles": history}
