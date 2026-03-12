"""
Backtest service - runs C++ dashboard_cli.exe and parses JSON results.
Uses subprocess.run in a thread to avoid Windows asyncio subprocess issues.
"""

import asyncio
import json
import logging
import subprocess
import uuid
from typing import Optional, Dict, Callable

from api.models.backtest import BacktestConfig, BacktestResult, BacktestProgress
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

# Active backtest state (mirrors sweep_service pattern)
_active_backtests: Dict[str, BacktestProgress] = {}
_backtest_results: Dict[str, BacktestResult] = {}


def get_backtest_progress(backtest_id: str) -> Optional[BacktestProgress]:
    return _active_backtests.get(backtest_id)


def get_backtest_result(backtest_id: str) -> Optional[BacktestResult]:
    return _backtest_results.get(backtest_id)


async def _try_ctrader_download_with_progress(
    symbol: str,
    start_date: str,
    end_date: str,
    progress_callback: Optional[Callable] = None,
) -> Optional[str]:
    """Download tick data from cTrader with progress reporting.
    Returns file path on success, None on failure or if not configured.
    """
    try:
        from api.services.ctrader_data_service import is_ctrader_configured, _download_ticks_sync
    except ImportError:
        return None

    if not is_ctrader_configured():
        return None

    dl_start = start_date.replace(".", "-")
    dl_end = end_date.replace(".", "-")

    loop = asyncio.get_running_loop()

    def sync_progress(msg: str):
        """Thread-safe progress callback that bridges sync→async."""
        if progress_callback:
            asyncio.run_coroutine_threadsafe(progress_callback(msg), loop)

    logger.info(f"Auto-downloading {symbol} tick data from cTrader (with progress)...")
    result = await asyncio.to_thread(
        _download_ticks_sync,
        symbol,
        dl_start,
        dl_end,
        None,  # output_dir
        None,  # account_id
        sync_progress,
    )

    if result.get("status") == "success":
        logger.info(f"cTrader download complete: {result['tick_count']:,} ticks")
        return result["path"]

    logger.warning(f"cTrader download failed: {result.get('message', 'unknown error')}")
    return None


async def start_backtest(
    config: BacktestConfig,
    progress_callback: Optional[Callable] = None,
) -> str:
    """Start an async backtest. Returns backtest_id immediately.

    progress_callback(backtest_id, progress) is called for each phase change.
    """
    backtest_id = str(uuid.uuid4())[:8]

    initial_progress = BacktestProgress(
        backtest_id=backtest_id,
        status="running",
        phase="checking_data",
        message="Checking tick data availability...",
    )
    _active_backtests[backtest_id] = initial_progress

    async def _run_pipeline():
        try:
            async def broadcast(phase: str, message: str, result_dict: Optional[dict] = None, status: str = "running"):
                progress = BacktestProgress(
                    backtest_id=backtest_id,
                    status=status,
                    phase=phase,
                    message=message,
                    result=result_dict,
                )
                _active_backtests[backtest_id] = progress
                if progress_callback:
                    await progress_callback(backtest_id, progress)

            # Give the frontend time to connect WebSocket before broadcasting
            await asyncio.sleep(0.5)

            # Phase 1: checking_data
            await broadcast("checking_data", "Checking tick data availability...")

            tick_file = config.tick_file_path
            if not tick_file:
                tick_file = find_tick_file(config.symbol)

            # Phase 2: downloading (if needed)
            if not tick_file:
                await broadcast("downloading", f"Downloading {config.symbol} tick data from cTrader...")

                async def download_progress(msg: str):
                    await broadcast("downloading", msg)

                downloaded_path = await _try_ctrader_download_with_progress(
                    config.symbol, config.start_date, config.end_date,
                    progress_callback=download_progress,
                )
                if downloaded_path:
                    config.tick_file_path = downloaded_path
                    tick_file = downloaded_path
                    await broadcast("download_complete", f"Download complete: {downloaded_path}")

            # Phase 3: running_engine
            await broadcast("running_engine", "Running backtest engine...")
            result = await asyncio.to_thread(_run_backtest_sync, config)
            _backtest_results[backtest_id] = result
            result_dict = result.model_dump()

            if result.status == "error":
                await broadcast("error", result.message or "Backtest failed", result_dict, status="error")
                return

            # Phase 4: saving_history
            await broadcast("saving_history", "Saving to history...")
            try:
                from api.services.history_service import save_result
                entry_id = save_result(result_dict, strategy_params=config.strategy_params)
                result_dict["history_id"] = entry_id
            except Exception as e:
                logger.error(f"Failed to save to history: {e}")

            # Phase 5: completed
            await broadcast("completed", "Complete", result_dict, status="completed")

        except Exception as e:
            logger.error(f"Backtest pipeline error: {e}", exc_info=True)
            error_progress = BacktestProgress(
                backtest_id=backtest_id,
                status="error",
                phase="error",
                message=str(e),
            )
            _active_backtests[backtest_id] = error_progress
            if progress_callback:
                await progress_callback(backtest_id, error_progress)

    asyncio.create_task(_run_pipeline())
    return backtest_id


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
