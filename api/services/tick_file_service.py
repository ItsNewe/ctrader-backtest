"""
Shared tick data file discovery.
Single source of truth for finding tick CSV files across the project.
Used by both backtest_service and sweep_service.
"""

import logging
from pathlib import Path
from typing import Optional

from api.config import get_settings

logger = logging.getLogger(__name__)

# Ordered list of filename suffixes to search for
TICK_SUFFIXES = [
    "_TICKS_2025.csv",
    "_TESTER_TICKS.csv",
    "_TICKS_MT5_EXPORT.csv",
    "_TICKS.csv",
    "_TICKS_FULL.csv",
]


def find_tick_file(symbol: str) -> Optional[str]:
    """Find tick data file for a symbol.

    Searches in order:
    1. validation/Grid/<SYMBOL>_*.csv
    2. validation/<SYMBOL>/<SYMBOL>_*.csv
    3. validation/<SYMBOL>_*.csv
    4. validation/Broker/<SYMBOL>_*.csv
    5. data/<SYMBOL>_*.csv

    Returns absolute path string or None if not found.
    """
    settings = get_settings()
    data_dir = settings.data_dir  # typically: project_root/validation

    search_dirs = [
        data_dir / "Grid",
        data_dir / symbol,
        data_dir,
        data_dir / "Broker",
        settings.project_root / "data",
    ]

    for d in search_dirs:
        if not d.exists():
            continue
        for suffix in TICK_SUFFIXES:
            path = d / f"{symbol}{suffix}"
            if path.exists():
                logger.debug(f"Found tick file: {path}")
                return str(path)

    logger.warning(f"No tick data file found for {symbol}")
    return None


def list_tick_files() -> list[dict]:
    """List all available tick data files with metadata."""
    settings = get_settings()
    data_dir = settings.data_dir
    files = []

    search_dirs = [
        data_dir / "Grid",
        data_dir / "Broker",
        data_dir,
    ]

    seen = set()
    for d in search_dirs:
        if not d.exists():
            continue
        for f in d.glob("*_TICKS*.csv"):
            if f.name in seen:
                continue
            seen.add(f.name)
            # Extract symbol from filename
            symbol = f.stem.split("_TICKS")[0].split("_TESTER")[0]
            files.append({
                "symbol": symbol,
                "filename": f.name,
                "path": str(f),
                "size_mb": round(f.stat().st_size / (1024 * 1024), 1),
                "directory": d.name,
            })

    return sorted(files, key=lambda x: x["symbol"])
