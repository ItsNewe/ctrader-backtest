"""
Backtest service - runs C++ dashboard_cli.exe and parses JSON results.
Uses subprocess.run in a thread to avoid Windows asyncio subprocess issues.
"""

import asyncio
import json
import logging
import subprocess
from typing import Optional

from api.models.backtest import BacktestConfig, BacktestResult
from api.services.cli_builder import build_backtest_command
from api.services.tick_file_service import find_tick_file
from api.services.strategy_registry import validate_strategy_params
from api.services.error_types import (
    missing_tick_data_error,
    cli_not_found_error,
    timeout_error,
    cli_crash_error,
    validation_error,
    invalid_json_error,
)

logger = logging.getLogger(__name__)


def _run_backtest_sync(config: BacktestConfig) -> BacktestResult:
    """Synchronous backtest execution (runs in thread pool)."""
    try:
        # Validate and fill defaults for strategy parameters
        try:
            cleaned_params = validate_strategy_params(config.strategy, config.strategy_params)
        except ValueError as e:
            err = validation_error(str(e))
            return BacktestResult(status="error", message=str(e), error_info=err.to_dict())

        # Find tick file
        tick_file = config.tick_file_path
        if not tick_file:
            tick_file = find_tick_file(config.symbol)
        if not tick_file:
            err = missing_tick_data_error(config.symbol)
            return BacktestResult(
                status="error",
                message=err.message,
                error_info=err.to_dict(),
            )

        # Build CLI command using shared builder
        cmd = build_backtest_command(
            strategy_id=config.strategy,
            symbol=config.symbol,
            start_date=config.start_date,
            end_date=config.end_date,
            initial_balance=config.initial_balance,
            tick_file=tick_file,
            contract_size=config.contract_size,
            leverage=config.leverage,
            pip_size=config.pip_size,
            swap_long=config.swap_long,
            swap_short=config.swap_short,
            strategy_params=cleaned_params,
            verbose=config.verbose,
        )

        logger.info(f"Running backtest: {' '.join(cmd)}")

        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=600,  # 10 minute timeout
        )

        stdout_text = result.stdout
        stderr_text = result.stderr

        if result.returncode != 0:
            logger.error(f"Backtest failed (exit {result.returncode}): {stderr_text}")
            try:
                error_data = json.loads(stdout_text)
                err = cli_crash_error(stderr_text)
                return BacktestResult(
                    status="error",
                    message=error_data.get("message", stderr_text),
                    error_info=err.to_dict(),
                )
            except json.JSONDecodeError:
                err = cli_crash_error(stderr_text or stdout_text)
                return BacktestResult(
                    status="error",
                    message=f"Backtest process failed: {stderr_text or stdout_text}",
                    error_info=err.to_dict(),
                )

        # Parse JSON output
        try:
            data = json.loads(stdout_text)
        except json.JSONDecodeError as e:
            logger.error(f"Failed to parse JSON: {e}\nOutput: {stdout_text[:500]}")
            err = invalid_json_error(stdout_text)
            return BacktestResult(
                status="error",
                message=f"Invalid JSON from backtest engine: {str(e)}",
                error_info=err.to_dict(),
            )

        # Dashboard CLI JSON maps directly to BacktestResult model
        return BacktestResult.model_validate(data)

    except FileNotFoundError as e:
        err = cli_not_found_error()
        return BacktestResult(status="error", message=str(e), error_info=err.to_dict())
    except subprocess.TimeoutExpired:
        err = timeout_error(600)
        return BacktestResult(status="error", message=err.message, error_info=err.to_dict())
    except Exception as e:
        logger.error(f"Backtest error: {str(e)}", exc_info=True)
        return BacktestResult(status="error", message=str(e))


async def _try_ctrader_download(symbol: str, start_date: str, end_date: str):
    """Attempt to download tick data from cTrader if credentials are configured.
    Returns file path on success, None on failure or if not configured.
    """
    try:
        from api.services.ctrader_data_service import is_ctrader_configured, download_ticks
    except ImportError:
        return None

    if not is_ctrader_configured():
        return None

    # Convert YYYY.MM.DD → YYYY-MM-DD if needed
    dl_start = start_date.replace(".", "-")
    dl_end = end_date.replace(".", "-")

    logger.info(f"Auto-downloading {symbol} tick data from cTrader...")
    result = await download_ticks(symbol=symbol, start_date=dl_start, end_date=dl_end)

    if result.get("status") == "success":
        logger.info(f"cTrader auto-download complete: {result['tick_count']:,} ticks")
        return result["path"]

    logger.warning(f"cTrader auto-download failed: {result.get('message', 'unknown error')}")
    return None


async def run_backtest(config: BacktestConfig) -> BacktestResult:
    """Run a backtest via the C++ CLI executable (async wrapper).

    If no tick file is found for the symbol, attempts to auto-download
    from cTrader Open API before falling back to the error response.
    """
    # Auto-download tick data if needed (before entering sync thread)
    if not config.tick_file_path and not find_tick_file(config.symbol):
        downloaded_path = await _try_ctrader_download(
            config.symbol, config.start_date, config.end_date,
        )
        if downloaded_path:
            config.tick_file_path = downloaded_path

    return await asyncio.to_thread(_run_backtest_sync, config)
