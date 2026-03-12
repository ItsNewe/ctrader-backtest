"""Backtest execution and history endpoints."""

import asyncio
import logging
from typing import Optional
from fastapi import APIRouter, WebSocket, WebSocketDisconnect

from api.models.backtest import BacktestConfig, BacktestProgress
from api.services import backtest_service
from api.services.backtest_service import start_backtest, get_backtest_progress, get_backtest_result
from api.services.history_service import save_result, list_history, get_history_entry, delete_history_entry
from api.ws.manager import ws_manager

logger = logging.getLogger(__name__)
router = APIRouter(prefix="/api/backtest", tags=["backtest"])


# ── Async backtest with WebSocket progress ─────────────────────────

@router.post("/start")
async def api_start_backtest(config: BacktestConfig):
    """Start an async backtest. Returns backtest_id to track progress via WebSocket."""
    logger.info(
        f"Starting async backtest: {config.strategy} on {config.symbol} "
        f"({config.start_date} - {config.end_date})"
    )

    async def progress_callback(backtest_id: str, progress: BacktestProgress):
        await ws_manager.broadcast(backtest_id, progress.model_dump())

    backtest_id = await start_backtest(config, progress_callback=progress_callback)
    progress = get_backtest_progress(backtest_id)

    return {
        "status": "ok",
        "backtest_id": backtest_id,
        "progress": progress.model_dump() if progress else None,
    }


@router.get("/{backtest_id}/status")
async def api_backtest_status(backtest_id: str):
    """Get current backtest progress (polling fallback)."""
    progress = get_backtest_progress(backtest_id)
    if not progress:
        return {"status": "error", "message": f"Backtest {backtest_id} not found"}
    return {"status": "ok", "progress": progress.model_dump()}


@router.websocket("/ws/{backtest_id}")
async def websocket_backtest(websocket: WebSocket, backtest_id: str):
    """WebSocket endpoint for real-time backtest progress updates."""
    await ws_manager.connect(backtest_id, websocket)
    try:
        # Send current progress immediately on connect
        progress = get_backtest_progress(backtest_id)
        if progress:
            await websocket.send_json(progress.model_dump())

        # Keep connection alive
        while True:
            try:
                await asyncio.wait_for(websocket.receive_text(), timeout=30.0)
            except asyncio.TimeoutError:
                # Send heartbeat / current status
                progress = get_backtest_progress(backtest_id)
                if progress:
                    await websocket.send_json(progress.model_dump())
                    if progress.status in ("completed", "error"):
                        break
    except WebSocketDisconnect:
        pass
    finally:
        ws_manager.disconnect(backtest_id, websocket)


# ── Synchronous backtest (backward compat) ─────────────────────────

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
