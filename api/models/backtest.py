"""Pydantic models for backtest API requests and responses."""

from pydantic import BaseModel
from typing import Optional, Any


class BacktestConfig(BaseModel):
    strategy: str = "FillUpOscillation"
    symbol: str = "XAUUSD"
    start_date: str = "2025.01.01"
    end_date: str = "2025.12.30"
    initial_balance: float = 10000.0

    # Broker settings (auto-filled from MT5 specs)
    contract_size: float = 100.0
    leverage: float = 500.0
    pip_size: float = 0.01
    swap_long: float = -66.99
    swap_short: float = 41.2

    # Tick data
    tick_file_path: Optional[str] = None

    # Commission model
    commission_per_lot: float = 0.0
    commission_model: str = "fixed"  # "fixed", "tiered", "ecn_rebate"

    # Strategy-specific parameters
    strategy_params: dict = {}

    # Output control
    max_equity_samples: int = 2000
    verbose: bool = False


class TradeRecord(BaseModel):
    id: int
    direction: str
    entry_price: float
    exit_price: float
    entry_time: str
    exit_time: str
    lot_size: float
    profit_loss: float
    commission: float = 0.0
    exit_reason: str = ""


class BacktestResult(BaseModel):
    status: str
    symbol: str = ""
    strategy: str = ""
    start_date: str = ""
    end_date: str = ""

    # Account metrics
    initial_balance: float = 0
    final_balance: float = 0
    total_pnl: float = 0
    return_percent: float = 0

    # Trade stats
    total_trades: int = 0
    total_trades_opened: int = 0
    winning_trades: int = 0
    losing_trades: int = 0
    win_rate: float = 0

    # Trade metrics
    average_win: float = 0
    average_loss: float = 0
    largest_win: float = 0
    largest_loss: float = 0

    # Risk metrics
    max_drawdown: float = 0
    max_drawdown_pct: float = 0
    sharpe_ratio: float = 0
    sortino_ratio: float = 0
    profit_factor: float = 0
    recovery_factor: float = 0
    total_swap: float = 0

    # Peak metrics
    peak_equity: float = 0
    peak_balance: float = 0
    max_open_positions: int = 0
    max_used_margin: float = 0
    stop_out_occurred: bool = False

    # Equity curve
    equity_curve: list[float] = []
    equity_timestamps: list[str] = []
    equity_curve_sampled: bool = False
    equity_curve_original_size: int = 0

    # Trade list (raw dicts from C++ JSON)
    trades: list[dict[str, Any]] = []
    trades_total: int = 0
    trades_truncated: bool = False

    # Broker settings used
    broker_settings: Optional[dict[str, float]] = None

    # Error info
    message: Optional[str] = None
    error_info: Optional[dict] = None  # Structured error details
