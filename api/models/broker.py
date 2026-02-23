"""Pydantic models for broker API requests and responses."""

from pydantic import BaseModel
from typing import Optional


class BrokerConnectRequest(BaseModel):
    broker: str = "metatrader5"
    account_type: str = "demo"
    account_id: str
    leverage: int = 500
    account_currency: str = "USD"
    mt5_path: Optional[str] = None
    password: Optional[str] = None
    server: Optional[str] = None


class BrokerConnectResponse(BaseModel):
    status: str
    message: str
    broker_key: Optional[str] = None


class InstrumentSpecResponse(BaseModel):
    symbol: str
    broker: str
    contract_size: float
    margin_requirement: float
    pip_value: float
    pip_size: float
    commission_per_lot: float
    swap_buy: float
    swap_sell: float
    min_volume: float
    max_volume: float
    fetched_at: str


class FetchSpecsRequest(BaseModel):
    symbols: list[str]


class AccountInfoResponse(BaseModel):
    balance: float = 0
    equity: float = 0
    free_margin: float = 0
    leverage: int = 0
    currency: str = ""
