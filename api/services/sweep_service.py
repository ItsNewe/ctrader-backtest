"""
Parameter sweep service.
Runs grid/random parameter sweeps using dashboard_cli.exe in parallel.
Broadcasts progress via WebSocket.
"""

import asyncio
import json
import logging
import subprocess
import uuid
import itertools
import random
from concurrent.futures import ThreadPoolExecutor, as_completed
from typing import Dict, List, Optional, Callable

from api.config import get_settings
from api.models.sweep import SweepConfig, SweepProgress, SweepResultEntry, ParameterRange
from api.services.strategy_registry import get_strategy, validate_strategy_params
from api.services.tick_file_service import find_tick_file
from api.services.cli_builder import build_backtest_command

logger = logging.getLogger(__name__)

# Active sweeps storage
_active_sweeps: Dict[str, SweepProgress] = {}
_sweep_results: Dict[str, List[SweepResultEntry]] = {}
_sweep_cancel: Dict[str, bool] = {}


def generate_grid_combinations(ranges: List[ParameterRange]) -> List[Dict[str, float]]:
    """Generate all grid combinations from parameter ranges."""
    param_values = {}
    for r in ranges:
        steps = int(round((r.max - r.min) / r.step)) + 1
        param_values[r.name] = [round(r.min + i * r.step, 6) for i in range(steps)]

    # Cartesian product
    keys = list(param_values.keys())
    combos = []
    for vals in itertools.product(*[param_values[k] for k in keys]):
        combos.append(dict(zip(keys, vals)))
    return combos


def generate_random_combinations(ranges: List[ParameterRange], n: int) -> List[Dict[str, float]]:
    """Generate random parameter combinations."""
    combos = []
    for _ in range(n):
        combo = {}
        for r in ranges:
            steps = int(round((r.max - r.min) / r.step))
            step_idx = random.randint(0, steps)
            combo[r.name] = round(r.min + step_idx * r.step, 6)
        combos.append(combo)
    return combos


def _run_single_backtest(
    config: SweepConfig,
    tick_file: str,
    params: Dict[str, float],
) -> Optional[SweepResultEntry]:
    """Run a single backtest with given parameters. Returns result entry or None."""
    try:
        # Merge sweep params with any fixed params from config
        full_params = dict(params)

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
            strategy_params=full_params,
            max_equity_samples=50,  # Minimal for sweep
            include_trades=False,   # No trade list in sweep
        )

        result = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
        if result.returncode != 0:
            logger.debug(f"Backtest failed (rc={result.returncode}): {result.stderr[:200]}")
            return None

        data = json.loads(result.stdout)
        if data.get("status") != "success":
            return None

        return SweepResultEntry(
            parameters=params,
            final_balance=data.get("final_balance", 0),
            return_percent=data.get("return_percent", 0),
            sharpe_ratio=data.get("sharpe_ratio", 0),
            max_drawdown=data.get("max_drawdown_pct", 0),
            profit_factor=data.get("profit_factor", 0),
            win_rate=data.get("win_rate", 0),
            total_trades=data.get("total_trades", 0),
            sortino_ratio=data.get("sortino_ratio", 0),
            recovery_factor=data.get("recovery_factor", 0),
            max_open_positions=data.get("max_open_positions", 0),
            stop_out=data.get("stop_out_occurred", False),
        )
    except Exception as e:
        logger.error(f"Sweep backtest error: {e}")
        return None


async def start_sweep(
    config: SweepConfig,
    progress_callback: Optional[Callable] = None,
) -> str:
    """Start a parameter sweep. Returns sweep_id."""
    sweep_id = str(uuid.uuid4())[:8]

    # Generate combinations
    if config.sweep_type == "random":
        combinations = generate_random_combinations(config.parameter_ranges, config.num_combinations)
    else:
        combinations = generate_grid_combinations(config.parameter_ranges)

    total = len(combinations)
    if total == 0:
        _active_sweeps[sweep_id] = SweepProgress(
            sweep_id=sweep_id, status="error", message="No parameter combinations generated"
        )
        return sweep_id

    # Find tick file using shared service
    tick_file = config.tick_file_path or find_tick_file(config.symbol)
    if not tick_file:
        _active_sweeps[sweep_id] = SweepProgress(
            sweep_id=sweep_id, status="error",
            message=f"No tick data found for {config.symbol}"
        )
        return sweep_id

    # Initialize progress
    _active_sweeps[sweep_id] = SweepProgress(
        sweep_id=sweep_id, status="running", total=total
    )
    _sweep_results[sweep_id] = []
    _sweep_cancel[sweep_id] = False

    # Capture the event loop for cross-thread callback scheduling
    loop = asyncio.get_running_loop()

    # Run sweep in background thread pool
    async def _run():
        best_entry = None
        completed = 0

        def run_batch():
            nonlocal completed, best_entry
            settings = get_settings()
            max_workers = min(settings.max_sweep_workers, total)

            with ThreadPoolExecutor(max_workers=max_workers) as pool:
                futures = {}
                for combo in combinations:
                    if _sweep_cancel.get(sweep_id, False):
                        break
                    f = pool.submit(
                        _run_single_backtest, config, tick_file, combo
                    )
                    futures[f] = combo

                for future in as_completed(futures):
                    if _sweep_cancel.get(sweep_id, False):
                        break

                    result = future.result()
                    completed += 1

                    if result:
                        _sweep_results[sweep_id].append(result)

                        if best_entry is None or result.return_percent > best_entry.return_percent:
                            best_entry = result

                    # Update progress
                    progress = SweepProgress(
                        sweep_id=sweep_id,
                        status="running",
                        completed=completed,
                        total=total,
                        percent=round(completed / total * 100, 1),
                        current_result=result.model_dump() if result else None,
                        best_so_far=best_entry.model_dump() if best_entry else None,
                    )
                    _active_sweeps[sweep_id] = progress

                    # Schedule WebSocket broadcast from worker thread
                    if progress_callback:
                        asyncio.run_coroutine_threadsafe(
                            progress_callback(sweep_id, progress), loop
                        )

        await asyncio.to_thread(run_batch)

        # Finalize
        status = "cancelled" if _sweep_cancel.get(sweep_id, False) else "completed"
        final_progress = SweepProgress(
            sweep_id=sweep_id,
            status=status,
            completed=completed,
            total=total,
            percent=round(completed / total * 100, 1),
            best_so_far=best_entry.model_dump() if best_entry else None,
            message=f"{completed}/{total} completed, {len(_sweep_results.get(sweep_id, []))} successful",
        )
        _active_sweeps[sweep_id] = final_progress

        # Broadcast final status
        if progress_callback:
            await progress_callback(sweep_id, final_progress)

        # Clean up cancel flag
        _sweep_cancel.pop(sweep_id, None)

    asyncio.create_task(_run())
    return sweep_id


def get_sweep_progress(sweep_id: str) -> Optional[SweepProgress]:
    return _active_sweeps.get(sweep_id)


def get_sweep_results(sweep_id: str) -> List[SweepResultEntry]:
    return _sweep_results.get(sweep_id, [])


def cancel_sweep(sweep_id: str) -> bool:
    if sweep_id in _active_sweeps and _active_sweeps[sweep_id].status == "running":
        _sweep_cancel[sweep_id] = True
        return True
    return False


def list_sweeps() -> Dict[str, SweepProgress]:
    return dict(_active_sweeps)
