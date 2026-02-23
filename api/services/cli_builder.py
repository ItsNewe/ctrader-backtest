"""
Shared CLI command builder for dashboard_cli.exe.
Uses --param key=value format for strategy parameters.
Both backtest_service and sweep_service import from here.
"""

import logging
from pathlib import Path
from typing import Optional

from api.config import get_settings
from api.services.strategy_registry import get_strategy, validate_strategy_params

logger = logging.getLogger(__name__)


def build_backtest_command(
    strategy_id: str,
    symbol: str,
    start_date: str,
    end_date: str,
    initial_balance: float,
    tick_file: str,
    contract_size: float,
    leverage: float,
    pip_size: float,
    swap_long: float,
    swap_short: float,
    strategy_params: dict,
    max_equity_samples: int = 2000,
    max_trades: int = 10000,
    include_trades: bool = True,
    verbose: bool = False,
) -> list[str]:
    """Build the full CLI command for dashboard_cli.exe.

    Uses --param key=value for all strategy parameters, which maps
    directly to the C++ ParamMap without needing per-param CLI flag mappings.
    """
    settings = get_settings()
    exe = str(settings.backtest_exe_path)

    # Resolve strategy CLI name
    strategy_info = get_strategy(strategy_id)
    cli_name = strategy_info["cli_name"] if strategy_info else strategy_id

    cmd = [
        exe,
        "--strategy", cli_name,
        "--symbol", symbol,
        "--start", start_date,
        "--end", end_date,
        "--balance", str(initial_balance),
        "--data", tick_file,
        "--contract-size", str(contract_size),
        "--leverage", str(leverage),
        "--swap-long", str(swap_long),
        "--swap-short", str(swap_short),
        "--pip-size", str(pip_size),
        "--max-equity-samples", str(max_equity_samples),
        "--max-trades", str(max_trades),
    ]

    if not include_trades:
        cmd.append("--no-trades")

    if verbose:
        cmd.append("--verbose")

    # Strategy parameters via --param key=value
    for key, value in strategy_params.items():
        if isinstance(value, bool):
            cmd.extend(["--param", f"{key}={'true' if value else 'false'}"])
        else:
            cmd.extend(["--param", f"{key}={value}"])

    return cmd
