"""
MT5 tick data download service.
Wraps scripts/export_mt5_ticks.py as an importable async service.
"""

import asyncio
import logging
import os
import sys
from pathlib import Path
from typing import Optional, Callable

logger = logging.getLogger(__name__)

# Add project root to path for importing export_mt5_ticks
_project_root = Path(__file__).parent.parent.parent
sys.path.insert(0, str(_project_root / "scripts"))


def _download_ticks_sync(
    symbol: str,
    start_date: str,
    end_date: str,
    output_dir: Optional[str] = None,
    progress_callback: Optional[Callable[[str], None]] = None,
) -> dict:
    """
    Synchronous tick download using MT5.
    Returns dict with status, path, tick_count, file_size_mb.
    """
    try:
        import MetaTrader5 as mt5
        import pandas as pd
        from datetime import datetime
    except ImportError as e:
        return {"status": "error", "message": f"Missing dependency: {e}. Install with: pip install MetaTrader5 pandas"}

    if progress_callback:
        progress_callback("Initializing MT5 connection...")

    # Initialize MT5
    if not mt5.initialize():
        error = mt5.last_error()
        return {"status": "error", "message": f"Failed to initialize MT5: {error}. Is the terminal running?"}

    try:
        # Select symbol
        if not mt5.symbol_select(symbol, True):
            return {"status": "error", "message": f"Symbol {symbol} not available in MT5"}

        if progress_callback:
            progress_callback(f"Downloading {symbol} ticks from {start_date} to {end_date}...")

        # Parse dates
        start_dt = datetime.strptime(start_date, "%Y-%m-%d")
        end_dt = datetime.strptime(end_date, "%Y-%m-%d")

        # Download ticks
        ticks = mt5.copy_ticks_range(symbol, start_dt, end_dt, mt5.COPY_TICKS_ALL)

        if ticks is None or len(ticks) == 0:
            error = mt5.last_error()
            return {"status": "error", "message": f"No tick data for {symbol}: {error}"}

        tick_count = len(ticks)
        if progress_callback:
            progress_callback(f"Processing {tick_count:,} ticks...")

        # Convert to DataFrame
        df = pd.DataFrame(ticks)
        df['time'] = pd.to_datetime(df['time'], unit='s')
        df['time_str'] = df['time'].dt.strftime('%Y.%m.%d %H:%M:%S')

        if 'time_msc' in df.columns:
            df['ms'] = df['time_msc'] % 1000
            df['time_str'] = df['time_str'] + '.' + df['ms'].astype(str).str.zfill(3)

        # Build output CSV in C++ engine format
        output_df = pd.DataFrame({
            'Timestamp': df['time_str'],
            'Bid': df['bid'],
            'Ask': df['ask'],
            'Volume': df['volume'] if 'volume' in df.columns else 0,
            'Flags': df['flags'] if 'flags' in df.columns else 0,
        })

        # Determine output path
        if output_dir is None:
            output_dir = str(_project_root / "validation" / symbol)

        os.makedirs(output_dir, exist_ok=True)
        output_path = os.path.join(output_dir, f"{symbol}_TICKS_MT5_EXPORT.csv")

        if progress_callback:
            progress_callback(f"Saving to {output_path}...")

        # Save with tab separator (C++ engine format)
        output_df.to_csv(output_path, sep='\t', index=False)
        file_size_mb = round(os.path.getsize(output_path) / (1024 * 1024), 1)

        return {
            "status": "success",
            "symbol": symbol,
            "path": output_path,
            "tick_count": tick_count,
            "file_size_mb": file_size_mb,
            "start_date": start_date,
            "end_date": end_date,
            "price_range": {
                "bid_min": float(output_df['Bid'].min()),
                "bid_max": float(output_df['Bid'].max()),
                "ask_min": float(output_df['Ask'].min()),
                "ask_max": float(output_df['Ask'].max()),
            }
        }

    finally:
        mt5.shutdown()


async def download_ticks(
    symbol: str,
    start_date: str,
    end_date: str,
    output_dir: Optional[str] = None,
) -> dict:
    """
    Async wrapper for tick download.
    Runs the blocking MT5 call in a thread pool.
    """
    logger.info(f"Starting tick download: {symbol} {start_date} to {end_date}")

    result = await asyncio.to_thread(
        _download_ticks_sync,
        symbol,
        start_date,
        end_date,
        output_dir,
    )

    if result["status"] == "success":
        logger.info(
            f"Downloaded {result['tick_count']:,} ticks for {symbol} "
            f"({result['file_size_mb']} MB)"
        )
    else:
        logger.error(f"Tick download failed: {result['message']}")

    return result
