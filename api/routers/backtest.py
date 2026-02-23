"""Backtest execution and history endpoints."""

import logging
from typing import Optional
from fastapi import APIRouter

from api.models.backtest import BacktestConfig
from api.services import backtest_service
from api.services.history_service import save_result, list_history, get_history_entry, delete_history_entry

logger = logging.getLogger(__name__)
router = APIRouter(prefix="/api/backtest", tags=["backtest"])


@router.post("/run")
async def run_backtest(config: BacktestConfig):
    """
    Run a backtest with the given configuration.
    Executes the C++ backtest engine, auto-saves to history, and returns results.
    """
    logger.info(
        f"Running backtest: {config.strategy} on {config.symbol} "
        f"({config.start_date} - {config.end_date})"
    )
    result = await backtest_service.run_backtest(config)
    result_dict = result.model_dump()

    # Auto-save to history if successful
    if result_dict.get("status") == "success":
        try:
            entry_id = save_result(result_dict, strategy_params=config.strategy_params)
            result_dict["history_id"] = entry_id
        except Exception as e:
            logger.error(f"Failed to save to history: {e}")

    return result_dict


# ── History endpoints ──────────────────────────────────────

@router.get("/history")
async def api_list_history(
    strategy: Optional[str] = None,
    symbol: Optional[str] = None,
    limit: int = 50,
    offset: int = 0,
    sort_by: str = "timestamp",
    ascending: bool = False,
):
    """List backtest history entries."""
    data = list_history(
        strategy=strategy,
        symbol=symbol,
        limit=limit,
        offset=offset,
        sort_by=sort_by,
        ascending=ascending,
    )
    return {"status": "ok", **data}


@router.get("/history/{entry_id}")
async def api_get_history_entry(entry_id: int):
    """Get a full history entry including equity curve."""
    entry = get_history_entry(entry_id)
    if not entry:
        return {"status": "error", "message": f"Entry {entry_id} not found"}
    return {"status": "ok", "entry": entry}


@router.delete("/history/{entry_id}")
async def api_delete_history_entry(entry_id: int):
    """Delete a history entry."""
    success = delete_history_entry(entry_id)
    if success:
        return {"status": "ok", "message": f"Entry {entry_id} deleted"}
    return {"status": "error", "message": f"Entry {entry_id} not found"}
