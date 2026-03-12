"""WebSocket connection manager for real-time progress (backtests, sweeps)."""

import asyncio
import json
import logging
from typing import Dict, Set
from fastapi import WebSocket

logger = logging.getLogger(__name__)


class ConnectionManager:
    """Manages WebSocket connections grouped by resource ID (backtest, sweep, etc.)."""

    def __init__(self):
        self.active: Dict[str, Set[WebSocket]] = {}

    async def connect(self, sweep_id: str, websocket: WebSocket):
        await websocket.accept()
        if sweep_id not in self.active:
            self.active[sweep_id] = set()
        self.active[sweep_id].add(websocket)
        logger.info(f"WS connected for sweep {sweep_id} ({len(self.active[sweep_id])} clients)")

    def disconnect(self, sweep_id: str, websocket: WebSocket):
        if sweep_id in self.active:
            self.active[sweep_id].discard(websocket)
            if not self.active[sweep_id]:
                del self.active[sweep_id]

    async def broadcast(self, sweep_id: str, data: dict):
        """Send progress update to all clients watching a sweep."""
        if sweep_id not in self.active:
            return
        dead = []
        for ws in self.active[sweep_id]:
            try:
                await ws.send_json(data)
            except Exception:
                dead.append(ws)
        for ws in dead:
            self.active[sweep_id].discard(ws)


ws_manager = ConnectionManager()
