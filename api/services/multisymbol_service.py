"""
Multi-symbol backtest service.
Runs the same strategy on multiple symbols and aggregates portfolio results.
"""

import asyncio
import json
import logging
import subprocess
from typing import Dict, List, Optional

from api.config import get_settings
from api.services.cli_builder import build_backtest_command
from api.services.tick_file_service import find_tick_file
from api.services.strategy_registry import validate_strategy_params

logger = logging.getLogger(__name__)

# Broker defaults per symbol
SYMBOL_BROKER_DEFAULTS = {
    "XAUUSD": {"contract_size": 100.0, "leverage": 500.0, "pip_size": 0.01, "swap_long": -66.99, "swap_short": 41.2},
    "XAGUSD": {"contract_size": 5000.0, "leverage": 500.0, "pip_size": 0.001, "swap_long": -15.0, "swap_short": 13.72},
}


def _run_single_symbol(
    strategy_id: str, symbol: str, start_date: str, end_date: str,
    initial_balance: float, strategy_params: dict, broker_overrides: dict = None,
) -> Optional[dict]:
    """Run backtest for a single symbol."""
    tick_file = find_tick_file(symbol)
    if not tick_file:
        logger.warning(f"No tick data for {symbol}, skipping")
        return None

    broker = SYMBOL_BROKER_DEFAULTS.get(symbol, SYMBOL_BROKER_DEFAULTS["XAUUSD"])
    if broker_overrides:
        broker.update(broker_overrides)

    try:
        cmd = build_backtest_command(
            strategy_id=strategy_id,
            symbol=symbol,
            start_date=start_date,
            end_date=end_date,
            initial_balance=initial_balance,
            tick_file=tick_file,
            contract_size=broker["contract_size"],
            leverage=broker["leverage"],
            pip_size=broker["pip_size"],
            swap_long=broker["swap_long"],
            swap_short=broker["swap_short"],
            strategy_params=strategy_params,
        )

        result = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
        if result.returncode != 0:
            return None

        data = json.loads(result.stdout)
        if data.get("status") != "success":
            return None

        data["symbol"] = symbol
        return data
    except Exception as e:
        logger.error(f"Multi-symbol backtest error for {symbol}: {e}")
        return None


async def run_multisymbol_backtest(config: dict) -> dict:
    """Run strategy on multiple symbols and aggregate results."""
    symbols = config.get("symbols", [])
    strategy_id = config.get("strategy", "FillUpOscillation")
    start_date = config.get("start_date", "2025.01.01")
    end_date = config.get("end_date", "2025.12.30")
    initial_balance = config.get("initial_balance", 10000.0)
    strategy_params = config.get("strategy_params", {})
    allocation_mode = config.get("allocation_mode", "equal")  # equal, custom

    if not symbols:
        return {"status": "error", "message": "No symbols specified"}

    # Determine per-symbol allocation
    if allocation_mode == "equal":
        per_symbol_balance = initial_balance / len(symbols)
    else:
        per_symbol_balance = initial_balance / len(symbols)

    # Run all symbols in parallel
    async def run_symbol(sym):
        return await asyncio.to_thread(
            _run_single_symbol, strategy_id, sym, start_date, end_date,
            per_symbol_balance, strategy_params
        )

    tasks = [run_symbol(sym) for sym in symbols]
    results = await asyncio.gather(*tasks)

    # Aggregate results
    symbol_results = []
    total_final = 0
    total_trades = 0
    total_pnl = 0
    max_dd_all = 0
    sharpe_values = []

    for sym, result in zip(symbols, results):
        if result is None:
            symbol_results.append({"symbol": sym, "status": "error", "message": "No data or execution failed"})
            total_final += per_symbol_balance  # Assume flat
            continue

        symbol_results.append({
            "symbol": sym,
            "status": "success",
            "return_percent": result.get("return_percent", 0),
            "final_balance": result.get("final_balance", 0),
            "sharpe_ratio": result.get("sharpe_ratio", 0),
            "max_drawdown_pct": result.get("max_drawdown_pct", 0),
            "profit_factor": result.get("profit_factor", 0),
            "total_trades": result.get("total_trades", 0),
            "win_rate": result.get("win_rate", 0),
            "equity_curve": result.get("equity_curve", []),
            "equity_timestamps": result.get("equity_timestamps", []),
        })
        total_final += result.get("final_balance", per_symbol_balance)
        total_trades += result.get("total_trades", 0)
        total_pnl += result.get("total_pnl", 0)
        dd = result.get("max_drawdown_pct", 0)
        if dd > max_dd_all:
            max_dd_all = dd
        if result.get("sharpe_ratio"):
            sharpe_values.append(result["sharpe_ratio"])

    portfolio_return = ((total_final - initial_balance) / initial_balance * 100) if initial_balance > 0 else 0
    avg_sharpe = sum(sharpe_values) / len(sharpe_values) if sharpe_values else 0

    return {
        "status": "success",
        "portfolio_summary": {
            "initial_balance": initial_balance,
            "final_balance": round(total_final, 2),
            "return_percent": round(portfolio_return, 2),
            "total_pnl": round(total_pnl, 2),
            "total_trades": total_trades,
            "max_drawdown_pct": round(max_dd_all, 1),
            "avg_sharpe": round(avg_sharpe, 2),
            "symbols_count": len(symbols),
            "successful_symbols": sum(1 for r in symbol_results if r.get("status") == "success"),
        },
        "symbol_results": symbol_results,
    }
