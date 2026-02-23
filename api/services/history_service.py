"""
Backtest history service — SQLite persistence for backtest results.
Auto-saves every backtest run. Supports listing, filtering, and retrieval.
"""

import json
import logging
import sqlite3
import time
from pathlib import Path
from typing import Dict, List, Optional, Any

from api.config import get_settings

logger = logging.getLogger(__name__)

_DB_PATH: Optional[Path] = None


def _get_db_path() -> Path:
    global _DB_PATH
    if _DB_PATH is None:
        settings = get_settings()
        _DB_PATH = settings.results_dir / "backtest_history.db"
    return _DB_PATH


def _get_connection() -> sqlite3.Connection:
    db_path = _get_db_path()
    db_path.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(str(db_path))
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA journal_mode=WAL")
    return conn


def init_db():
    """Create tables if they don't exist."""
    conn = _get_connection()
    try:
        conn.execute("""
            CREATE TABLE IF NOT EXISTS backtest_history (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp REAL NOT NULL,
                strategy TEXT NOT NULL,
                symbol TEXT NOT NULL,
                start_date TEXT NOT NULL,
                end_date TEXT NOT NULL,
                initial_balance REAL NOT NULL,

                -- Summary metrics
                final_balance REAL DEFAULT 0,
                return_percent REAL DEFAULT 0,
                total_trades INTEGER DEFAULT 0,
                win_rate REAL DEFAULT 0,
                sharpe_ratio REAL DEFAULT 0,
                sortino_ratio REAL DEFAULT 0,
                max_drawdown_pct REAL DEFAULT 0,
                profit_factor REAL DEFAULT 0,
                recovery_factor REAL DEFAULT 0,
                max_open_positions INTEGER DEFAULT 0,
                stop_out_occurred INTEGER DEFAULT 0,

                -- Full result JSON (compressed)
                strategy_params TEXT,  -- JSON string of parameters
                broker_settings TEXT,  -- JSON string
                full_result TEXT,      -- Full result JSON (without trades/equity for size)
                equity_curve TEXT,     -- JSON array of equity values
                equity_timestamps TEXT, -- JSON array of timestamps

                -- Metadata
                notes TEXT DEFAULT ''
            )
        """)

        conn.execute("""
            CREATE INDEX IF NOT EXISTS idx_history_strategy ON backtest_history(strategy)
        """)
        conn.execute("""
            CREATE INDEX IF NOT EXISTS idx_history_symbol ON backtest_history(symbol)
        """)
        conn.execute("""
            CREATE INDEX IF NOT EXISTS idx_history_timestamp ON backtest_history(timestamp DESC)
        """)

        conn.commit()
    finally:
        conn.close()


def save_result(result: Dict[str, Any], strategy_params: Optional[Dict] = None) -> int:
    """Save a backtest result. Returns the row ID."""
    conn = _get_connection()
    try:
        # Extract summary metrics
        broker_settings = result.get("broker_settings", {})

        # Strip heavy data for the stored full_result (save space)
        slim_result = {k: v for k, v in result.items()
                       if k not in ("trades", "equity_curve", "equity_timestamps")}

        row_id = conn.execute("""
            INSERT INTO backtest_history (
                timestamp, strategy, symbol, start_date, end_date, initial_balance,
                final_balance, return_percent, total_trades, win_rate,
                sharpe_ratio, sortino_ratio, max_drawdown_pct, profit_factor,
                recovery_factor, max_open_positions, stop_out_occurred,
                strategy_params, broker_settings, full_result,
                equity_curve, equity_timestamps
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """, (
            time.time(),
            result.get("strategy", ""),
            result.get("symbol", ""),
            result.get("start_date", ""),
            result.get("end_date", ""),
            result.get("initial_balance", 0),
            result.get("final_balance", 0),
            result.get("return_percent", 0),
            result.get("total_trades", 0),
            result.get("win_rate", 0),
            result.get("sharpe_ratio", 0),
            result.get("sortino_ratio", 0),
            result.get("max_drawdown_pct", 0),
            result.get("profit_factor", 0),
            result.get("recovery_factor", 0),
            result.get("max_open_positions", 0),
            1 if result.get("stop_out_occurred", False) else 0,
            json.dumps(strategy_params) if strategy_params else None,
            json.dumps(broker_settings) if broker_settings else None,
            json.dumps(slim_result),
            json.dumps(result.get("equity_curve", [])),
            json.dumps(result.get("equity_timestamps", [])),
        )).lastrowid

        conn.commit()
        logger.info(f"Saved backtest history #{row_id}: {result.get('strategy')} {result.get('symbol')} → {result.get('return_percent', 0):.1f}%")
        return row_id
    finally:
        conn.close()


def list_history(
    strategy: Optional[str] = None,
    symbol: Optional[str] = None,
    limit: int = 50,
    offset: int = 0,
    sort_by: str = "timestamp",
    ascending: bool = False,
) -> Dict[str, Any]:
    """List backtest history entries (without heavy data)."""
    conn = _get_connection()
    try:
        # Build query
        conditions = []
        params: list = []

        if strategy:
            conditions.append("strategy = ?")
            params.append(strategy)
        if symbol:
            conditions.append("symbol = ?")
            params.append(symbol)

        where = f"WHERE {' AND '.join(conditions)}" if conditions else ""

        # Validate sort column
        allowed_sorts = {
            "timestamp", "return_percent", "sharpe_ratio", "max_drawdown_pct",
            "profit_factor", "total_trades", "win_rate", "final_balance",
            "sortino_ratio", "recovery_factor",
        }
        if sort_by not in allowed_sorts:
            sort_by = "timestamp"
        order = "ASC" if ascending else "DESC"

        # Count
        total = conn.execute(f"SELECT COUNT(*) FROM backtest_history {where}", params).fetchone()[0]

        # Fetch entries (lightweight — no equity curve or full result)
        rows = conn.execute(f"""
            SELECT id, timestamp, strategy, symbol, start_date, end_date, initial_balance,
                   final_balance, return_percent, total_trades, win_rate,
                   sharpe_ratio, sortino_ratio, max_drawdown_pct, profit_factor,
                   recovery_factor, max_open_positions, stop_out_occurred,
                   strategy_params, notes
            FROM backtest_history {where}
            ORDER BY {sort_by} {order}
            LIMIT ? OFFSET ?
        """, params + [limit, offset]).fetchall()

        entries = []
        for row in rows:
            entry = dict(row)
            entry["stop_out_occurred"] = bool(entry["stop_out_occurred"])
            if entry["strategy_params"]:
                entry["strategy_params"] = json.loads(entry["strategy_params"])
            entries.append(entry)

        return {
            "entries": entries,
            "total": total,
            "limit": limit,
            "offset": offset,
        }
    finally:
        conn.close()


def get_history_entry(entry_id: int) -> Optional[Dict[str, Any]]:
    """Get a full history entry including equity curve."""
    conn = _get_connection()
    try:
        row = conn.execute("SELECT * FROM backtest_history WHERE id = ?", (entry_id,)).fetchone()
        if not row:
            return None

        entry = dict(row)
        entry["stop_out_occurred"] = bool(entry["stop_out_occurred"])

        # Parse JSON fields
        for field in ("strategy_params", "broker_settings", "full_result", "equity_curve", "equity_timestamps"):
            if entry.get(field):
                try:
                    entry[field] = json.loads(entry[field])
                except (json.JSONDecodeError, TypeError):
                    pass

        return entry
    finally:
        conn.close()


def delete_history_entry(entry_id: int) -> bool:
    """Delete a history entry."""
    conn = _get_connection()
    try:
        cursor = conn.execute("DELETE FROM backtest_history WHERE id = ?", (entry_id,))
        conn.commit()
        return cursor.rowcount > 0
    finally:
        conn.close()


# Initialize DB on import
init_db()
