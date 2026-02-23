"""
Sweep router — parameter sweep endpoints + WebSocket progress.
"""

import asyncio
import logging
from fastapi import APIRouter, WebSocket, WebSocketDisconnect

from api.models.sweep import SweepConfig, SweepProgress
from api.services.sweep_service import (
    start_sweep,
    get_sweep_progress,
    get_sweep_results,
    cancel_sweep,
    list_sweeps,
)
from api.ws.manager import ws_manager

router = APIRouter(prefix="/api/sweep", tags=["sweep"])
logger = logging.getLogger(__name__)


@router.post("/start")
async def api_start_sweep(config: SweepConfig):
    """Start a parameter sweep. Returns sweep_id to track progress."""
    async def progress_callback(sweep_id: str, progress: SweepProgress):
        """Broadcast progress via WebSocket."""
        await ws_manager.broadcast(sweep_id, progress.model_dump())

    sweep_id = await start_sweep(config, progress_callback=progress_callback)
    progress = get_sweep_progress(sweep_id)

    return {
        "status": "ok",
        "sweep_id": sweep_id,
        "progress": progress.model_dump() if progress else None,
    }


@router.get("/{sweep_id}/status")
async def api_sweep_status(sweep_id: str):
    """Get current sweep progress."""
    progress = get_sweep_progress(sweep_id)
    if not progress:
        return {"status": "error", "message": f"Sweep {sweep_id} not found"}
    return {"status": "ok", "progress": progress.model_dump()}


@router.get("/{sweep_id}/results")
async def api_sweep_results(sweep_id: str, sort_by: str = "return_percent", ascending: bool = False, limit: int = 500):
    """Get sweep results, sorted by metric."""
    results = get_sweep_results(sweep_id)
    if not results:
        return {"status": "ok", "results": [], "count": 0}

    # Sort results
    result_dicts = [r.model_dump() for r in results]
    reverse = not ascending
    try:
        result_dicts.sort(key=lambda x: x.get(sort_by, 0), reverse=reverse)
    except (TypeError, KeyError):
        pass

    # Limit
    result_dicts = result_dicts[:limit]

    return {
        "status": "ok",
        "results": result_dicts,
        "count": len(results),
        "returned": len(result_dicts),
    }


@router.delete("/{sweep_id}")
async def api_cancel_sweep(sweep_id: str):
    """Cancel a running sweep."""
    success = cancel_sweep(sweep_id)
    if success:
        return {"status": "ok", "message": f"Sweep {sweep_id} cancel requested"}
    return {"status": "error", "message": f"Sweep {sweep_id} not running or not found"}


@router.get("/list/all")
async def api_list_sweeps():
    """List all sweeps (active and completed)."""
    sweeps = list_sweeps()
    return {
        "status": "ok",
        "sweeps": {k: v.model_dump() for k, v in sweeps.items()},
    }


# ── WebSocket endpoint for live progress ──────────────────────────────

@router.websocket("/ws/{sweep_id}")
async def websocket_sweep(websocket: WebSocket, sweep_id: str):
    """WebSocket endpoint for real-time sweep progress updates."""
    await ws_manager.connect(sweep_id, websocket)
    try:
        # Send current progress immediately on connect
        progress = get_sweep_progress(sweep_id)
        if progress:
            await websocket.send_json(progress.model_dump())

        # Keep connection alive and listen for cancel commands
        while True:
            try:
                data = await asyncio.wait_for(websocket.receive_text(), timeout=30.0)
                if data == "cancel":
                    cancel_sweep(sweep_id)
                    await websocket.send_json({"type": "cancel_ack", "sweep_id": sweep_id})
            except asyncio.TimeoutError:
                # Send heartbeat / current status
                progress = get_sweep_progress(sweep_id)
                if progress:
                    await websocket.send_json(progress.model_dump())
                    if progress.status in ("completed", "error", "cancelled"):
                        break
    except WebSocketDisconnect:
        pass
    finally:
        ws_manager.disconnect(sweep_id, websocket)
