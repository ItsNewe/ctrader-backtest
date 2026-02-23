"""
Backtest service - runs C++ dashboard_cli.exe and parses JSON results.
Uses subprocess.run in a thread to avoid Windows asyncio subprocess issues.
"""

import asyncio
import json
import logging
import subprocess
from pathlib import Path
from typing import Optional

from api.config import get_settings
from api.models.backtest import BacktestConfig, BacktestResult
from api.services.strategy_registry import get_strategy

logger = logging.getLogger(__name__)


def _find_tick_file(symbol: str) -> Optional[str]:
    """Find tick data file for a symbol."""
    settings = get_settings()
    data_dir = settings.data_dir

    # Search patterns
    suffixes = ["_TICKS_2025.csv", "_TESTER_TICKS.csv", "_TICKS_MT5_EXPORT.csv", "_TICKS.csv",
                "_TICKS_FULL.csv"]
    search_dirs = [
        data_dir / "Grid",
        data_dir / symbol,
        data_dir,
        data_dir / "Broker",
    ]

    for d in search_dirs:
        if not d.exists():
            continue
        for suffix in suffixes:
            path = d / f"{symbol}{suffix}"
            if path.exists():
                return str(path)

    return None


def _build_command(config: BacktestConfig) -> list[str]:
    """Build the command line for dashboard_cli.exe."""
    settings = get_settings()
    exe = str(settings.backtest_exe_path)

    # Resolve strategy CLI name
    strategy_info = get_strategy(config.strategy)
    cli_name = strategy_info["cli_name"] if strategy_info else config.strategy

    # Find tick file
    tick_file = config.tick_file_path
    if not tick_file:
        tick_file = _find_tick_file(config.symbol)
    if not tick_file:
        raise FileNotFoundError(
            f"No tick data file found for {config.symbol}. "
            "Download tick data first via Settings > Data Manager."
        )

    cmd = [
        exe,
        "--strategy", cli_name,
        "--symbol", config.symbol,
        "--start", config.start_date,
        "--end", config.end_date,
        "--balance", str(config.initial_balance),
        "--data", tick_file,
        "--contract-size", str(config.contract_size),
        "--leverage", str(config.leverage),
        "--swap-long", str(config.swap_long),
        "--swap-short", str(config.swap_short),
        "--pip-size", str(config.pip_size),
        "--max-equity-samples", "2000",
        "--max-trades", "10000",
    ]

    # Strategy-specific params (survive_pct -> --survive, base_spacing -> --spacing, etc.)
    param_map = {
        "survive_pct": "survive",
        "base_spacing": "spacing",
        "lookback_hours": "lookback",
        "antifragile_scale": "antifragile",
        "velocity_threshold": "velocity",
        "max_spacing_mult": "max-spacing-mult",
        "tp_mode": "tp-mode",
        "sizing_mode": "sizing-mode",
    }
    for key, value in config.strategy_params.items():
        cli_key = param_map.get(key, key.replace("_", "-"))
        # Boolean flags
        if key == "pct_spacing" and value:
            cmd.append("--pct-spacing")
        elif key == "force_min_volume_entry" and value:
            cmd.append("--force-min-volume")
        elif key == "enable_velocity_filter" and not value:
            cmd.append("--no-velocity-filter")
        elif key not in ("pct_spacing", "force_min_volume_entry", "enable_velocity_filter"):
            cmd.extend([f"--{cli_key}", str(value)])

    if config.verbose:
        cmd.append("--verbose")

    return cmd


def _run_backtest_sync(config: BacktestConfig) -> BacktestResult:
    """Synchronous backtest execution (runs in thread pool)."""
    try:
        cmd = _build_command(config)
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
                return BacktestResult(
                    status="error",
                    message=error_data.get("message", stderr_text),
                )
            except json.JSONDecodeError:
                return BacktestResult(
                    status="error",
                    message=f"Backtest process failed: {stderr_text or stdout_text}",
                )

        # Parse JSON output
        try:
            data = json.loads(stdout_text)
        except json.JSONDecodeError as e:
            logger.error(f"Failed to parse JSON: {e}\nOutput: {stdout_text[:500]}")
            return BacktestResult(
                status="error",
                message=f"Invalid JSON from backtest engine: {str(e)}",
            )

        # Dashboard CLI JSON maps directly to BacktestResult model
        return BacktestResult.model_validate(data)

    except FileNotFoundError as e:
        return BacktestResult(status="error", message=str(e))
    except subprocess.TimeoutExpired:
        return BacktestResult(status="error", message="Backtest timed out (10 min limit)")
    except Exception as e:
        logger.error(f"Backtest error: {str(e)}", exc_info=True)
        return BacktestResult(status="error", message=str(e))


async def run_backtest(config: BacktestConfig) -> BacktestResult:
    """Run a backtest via the C++ CLI executable (async wrapper)."""
    return await asyncio.to_thread(_run_backtest_sync, config)
