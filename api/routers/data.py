"""Data file management and tick data download endpoints."""

import logging
from pathlib import Path
from datetime import datetime
from fastapi import APIRouter
from pydantic import BaseModel
from typing import Optional

from api.config import get_settings

logger = logging.getLogger(__name__)
router = APIRouter(prefix="/api/data", tags=["data"])


class TickDownloadRequest(BaseModel):
    symbol: str
    start_date: str  # YYYY-MM-DD
    end_date: str    # YYYY-MM-DD
    output_dir: Optional[str] = None


@router.get("/files")
async def list_data_files():
    """List available tick data CSV files."""
    settings = get_settings()
    files = []

    # Search validation/ and subdirectories for tick CSVs
    search_dirs = [
        settings.data_dir,
        settings.data_dir / "Grid",
        settings.data_dir / "Broker",
    ]

    # Also search symbol-named subdirs
    if settings.data_dir.exists():
        for subdir in settings.data_dir.iterdir():
            if subdir.is_dir() and subdir not in search_dirs:
                search_dirs.append(subdir)

    seen = set()
    for d in search_dirs:
        if not d.exists():
            continue
        for f in d.glob("*TICKS*.csv"):
            if f.name in seen:
                continue
            seen.add(f.name)

            # Extract symbol from filename
            symbol = f.stem.split("_")[0]

            files.append({
                "name": f.name,
                "path": str(f),
                "symbol": symbol,
                "size_mb": round(f.stat().st_size / (1024 * 1024), 1),
                "modified": datetime.fromtimestamp(f.stat().st_mtime).isoformat(),
            })

    files.sort(key=lambda x: x["symbol"])
    return {"status": "success", "files": files, "count": len(files)}


@router.get("/files/{symbol}")
async def check_data_file(symbol: str):
    """Check if tick data exists for a specific symbol."""
    settings = get_settings()

    suffixes = ["_TICKS_2025.csv", "_TESTER_TICKS.csv", "_TICKS_MT5_EXPORT.csv", "_TICKS_CTRADER.csv", "_TICKS.csv"]
    search_dirs = [
        settings.data_dir / "Grid",
        settings.data_dir / symbol,
        settings.data_dir,
        settings.data_dir / "Broker",
    ]

    for d in search_dirs:
        if not d.exists():
            continue
        for suffix in suffixes:
            path = d / f"{symbol}{suffix}"
            if path.exists():
                return {
                    "status": "success",
                    "exists": True,
                    "path": str(path),
                    "size_mb": round(path.stat().st_size / (1024 * 1024), 1),
                }

    return {"status": "success", "exists": False}


@router.get("/symbols")
async def list_data_symbols():
    """List symbols that have tick data available (works without MT5)."""
    settings = get_settings()
    symbols = set()

    search_dirs = [
        settings.data_dir,
        settings.data_dir / "Grid",
        settings.data_dir / "Broker",
    ]

    if settings.data_dir.exists():
        for subdir in settings.data_dir.iterdir():
            if subdir.is_dir() and subdir not in search_dirs:
                search_dirs.append(subdir)

    for d in search_dirs:
        if not d.exists():
            continue
        for f in d.glob("*TICKS*.csv"):
            symbol = f.stem.split("_")[0]
            symbols.add(symbol)

    return {"status": "success", "symbols": sorted(symbols)}


@router.post("/download-ticks")
async def download_ticks(req: TickDownloadRequest):
    """
    Download tick data from MT5 for a symbol and date range.
    Requires MT5 terminal to be running.
    """
    try:
        from api.services.mt5_data_service import download_ticks as do_download
        result = await do_download(
            symbol=req.symbol,
            start_date=req.start_date,
            end_date=req.end_date,
            output_dir=req.output_dir,
        )
        return result
    except ImportError:
        return {
            "status": "error",
            "message": "MetaTrader5 package not installed. Install with: pip install MetaTrader5 pandas"
        }
    except Exception as e:
        logger.error(f"Tick download error: {e}", exc_info=True)
        return {"status": "error", "message": str(e)}


@router.post("/download-ctrader-ticks")
async def download_ctrader_ticks(req: TickDownloadRequest):
    """Download tick data from cTrader Open API for a symbol and date range.

    Requires CTRADER_CLIENT_ID, CTRADER_CLIENT_SECRET, CTRADER_ACCESS_TOKEN,
    and CTRADER_ACCOUNT_ID environment variables to be set.
    """
    try:
        from api.services.ctrader_data_service import download_ticks as do_download
        result = await do_download(
            symbol=req.symbol,
            start_date=req.start_date,
            end_date=req.end_date,
            output_dir=req.output_dir,
        )
        return result
    except ImportError:
        return {
            "status": "error",
            "message": "ctrader-open-api package not installed. Install with: pip install ctrader-open-api",
        }
    except Exception as e:
        logger.error(f"cTrader tick download error: {e}", exc_info=True)
        return {"status": "error", "message": str(e)}
