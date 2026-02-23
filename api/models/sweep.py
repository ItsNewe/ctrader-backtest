"""Pydantic models for parameter sweep API."""

from pydantic import BaseModel
from typing import Optional


class ParameterRange(BaseModel):
    name: str
    min: float
    max: float
    step: float = 0.5


class SweepConfig(BaseModel):
    strategy: str = "FillUpOscillation"
    symbol: str = "XAUUSD"
    start_date: str = "2025.01.01"
    end_date: str = "2025.12.30"
    initial_balance: float = 10000.0

    # Broker settings
    contract_size: float = 100.0
    leverage: float = 500.0
    pip_size: float = 0.01
    swap_long: float = -66.99
    swap_short: float = 41.2

    # Tick data
    tick_file_path: Optional[str] = None

    # Sweep type
    sweep_type: str = "grid"  # "grid" or "random"
    num_combinations: int = 100  # For random search

    # Parameter ranges
    parameter_ranges: list[ParameterRange] = []


class SweepProgress(BaseModel):
    sweep_id: str
    status: str  # "running", "completed", "error", "cancelled"
    completed: int = 0
    total: int = 0
    percent: float = 0.0
    current_result: Optional[dict] = None
    best_so_far: Optional[dict] = None
    message: Optional[str] = None


class SweepResultEntry(BaseModel):
    parameters: dict
    final_balance: float = 0
    return_percent: float = 0
    sharpe_ratio: float = 0
    max_drawdown: float = 0
    profit_factor: float = 0
    win_rate: float = 0
    total_trades: int = 0
    sortino_ratio: float = 0
    recovery_factor: float = 0
    max_open_positions: int = 0
    stop_out: bool = False
