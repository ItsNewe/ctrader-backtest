"""
Walk-forward analysis service.
Splits date range into windows, runs in-sample optimization + out-of-sample validation.
"""

import asyncio
import json
import logging
import subprocess
import uuid
import itertools
from concurrent.futures import ThreadPoolExecutor
from typing import Dict, List, Optional

from api.config import get_settings
from api.services.cli_builder import build_backtest_command
from api.services.tick_file_service import find_tick_file
from api.services.strategy_registry import validate_strategy_params

logger = logging.getLogger(__name__)

_active_walkforwards: Dict[str, dict] = {}


def _parse_date(date_str: str) -> tuple:
    """Parse '2025.01.01' to (2025, 1, 1)."""
    parts = date_str.replace("-", ".").split(".")
    return int(parts[0]), int(parts[1]), int(parts[2])


def _format_date(y: int, m: int, d: int) -> str:
    return f"{y:04d}.{m:02d}.{d:02d}"


def _add_months(y: int, m: int, d: int, months: int) -> tuple:
    m += months
    while m > 12:
        m -= 12
        y += 1
    while m < 1:
        m += 12
        y -= 1
    # Clamp day
    import calendar
    max_day = calendar.monthrange(y, m)[1]
    d = min(d, max_day)
    return y, m, d


def generate_windows(start_date: str, end_date: str, in_sample_months: int, out_sample_months: int) -> list:
    """Generate rolling walk-forward windows."""
    sy, sm, sd = _parse_date(start_date)
    ey, em, ed = _parse_date(end_date)

    windows = []
    window_start = (sy, sm, sd)

    while True:
        is_end = _add_months(*window_start, in_sample_months)
        oos_start = is_end
        oos_end = _add_months(*oos_start, out_sample_months)

        # Check if OOS end exceeds overall end date
        if (oos_end[0], oos_end[1], oos_end[2]) > (ey, em, ed):
            break

        windows.append({
            "in_sample_start": _format_date(*window_start),
            "in_sample_end": _format_date(*is_end),
            "out_sample_start": _format_date(*oos_start),
            "out_sample_end": _format_date(*oos_end),
        })

        # Slide forward by out_sample_months
        window_start = oos_start

    return windows


def _run_backtest_for_params(strategy_id, symbol, start_date, end_date, balance, tick_file, broker, params):
    """Run a single backtest and return metrics dict or None."""
    try:
        cmd = build_backtest_command(
            strategy_id=strategy_id,
            symbol=symbol,
            start_date=start_date,
            end_date=end_date,
            initial_balance=balance,
            tick_file=tick_file,
            contract_size=broker["contract_size"],
            leverage=broker["leverage"],
            pip_size=broker["pip_size"],
            swap_long=broker["swap_long"],
            swap_short=broker["swap_short"],
            strategy_params=params,
            max_equity_samples=50,
            include_trades=False,
        )
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
        if result.returncode != 0:
            return None
        data = json.loads(result.stdout)
        if data.get("status") != "success":
            return None
        return data
    except Exception as e:
        logger.debug(f"WF backtest error: {e}")
        return None


def _optimize_window(strategy_id, symbol, window, balance, tick_file, broker, param_ranges, optimize_metric):
    """Find best params in-sample for one window."""
    # Generate grid
    param_values = {}
    for pr in param_ranges:
        steps = int(round((pr["max"] - pr["min"]) / pr["step"])) + 1
        param_values[pr["name"]] = [round(pr["min"] + i * pr["step"], 6) for i in range(steps)]

    keys = list(param_values.keys())
    best_params = None
    best_score = float("-inf")

    settings = get_settings()
    all_combos = list(itertools.product(*[param_values[k] for k in keys]))

    with ThreadPoolExecutor(max_workers=min(settings.max_sweep_workers, len(all_combos))) as pool:
        futures = {}
        for vals in all_combos:
            combo = dict(zip(keys, vals))
            f = pool.submit(
                _run_backtest_for_params, strategy_id, symbol,
                window["in_sample_start"], window["in_sample_end"],
                balance, tick_file, broker, combo
            )
            futures[f] = combo

        for f in futures:
            result = f.result()
            if result is None:
                continue
            score = result.get(optimize_metric, 0)
            if score > best_score:
                best_score = score
                best_params = futures[f]

    return best_params, best_score


async def start_walkforward(config: dict) -> str:
    """Start walk-forward analysis. Returns wf_id."""
    wf_id = str(uuid.uuid4())[:8]

    windows = generate_windows(
        config["start_date"], config["end_date"],
        config.get("in_sample_months", 3),
        config.get("out_sample_months", 1),
    )

    if not windows:
        _active_walkforwards[wf_id] = {"status": "error", "message": "Date range too short for walk-forward windows"}
        return wf_id

    tick_file = config.get("tick_file_path") or find_tick_file(config["symbol"])
    if not tick_file:
        _active_walkforwards[wf_id] = {"status": "error", "message": f"No tick data for {config['symbol']}"}
        return wf_id

    broker = {
        "contract_size": config.get("contract_size", 100),
        "leverage": config.get("leverage", 500),
        "pip_size": config.get("pip_size", 0.01),
        "swap_long": config.get("swap_long", -66.99),
        "swap_short": config.get("swap_short", 41.2),
    }

    _active_walkforwards[wf_id] = {
        "status": "running",
        "windows_total": len(windows),
        "windows_completed": 0,
        "results": [],
    }

    async def _run():
        optimize_metric = config.get("optimization_metric", config.get("optimize_metric", "return_percent"))
        results = []

        for i, window in enumerate(windows):
            if _active_walkforwards.get(wf_id, {}).get("status") == "cancelled":
                break

            # 1. Optimize in-sample
            best_params, is_score = await asyncio.to_thread(
                _optimize_window,
                config["strategy"], config["symbol"], window,
                config.get("initial_balance", 10000), tick_file, broker,
                config.get("parameter_ranges", []),
                optimize_metric,
            )

            if best_params is None:
                results.append({
                    "window": window,
                    "in_sample_best_params": None,
                    "in_sample_score": 0,
                    "out_sample_result": None,
                })
                continue

            # 2. Run OOS with best params
            oos_result = await asyncio.to_thread(
                _run_backtest_for_params,
                config["strategy"], config["symbol"],
                window["out_sample_start"], window["out_sample_end"],
                config.get("initial_balance", 10000), tick_file, broker, best_params
            )

            results.append({
                "window": window,
                "in_sample_best_params": best_params,
                "in_sample_score": is_score,
                "out_sample_result": {
                    "return_percent": oos_result.get("return_percent", 0) if oos_result else 0,
                    "sharpe_ratio": oos_result.get("sharpe_ratio", 0) if oos_result else 0,
                    "max_drawdown_pct": oos_result.get("max_drawdown_pct", 0) if oos_result else 0,
                    "profit_factor": oos_result.get("profit_factor", 0) if oos_result else 0,
                    "total_trades": oos_result.get("total_trades", 0) if oos_result else 0,
                } if oos_result else None,
            })

            _active_walkforwards[wf_id]["windows_completed"] = i + 1
            _active_walkforwards[wf_id]["results"] = results

        # Compute summary
        oos_returns = [r["out_sample_result"]["return_percent"] for r in results if r["out_sample_result"]]
        avg_oos = sum(oos_returns) / len(oos_returns) if oos_returns else 0
        positive_oos = sum(1 for r in oos_returns if r > 0)

        _active_walkforwards[wf_id].update({
            "status": "completed",
            "results": results,
            "summary": {
                "total_windows": len(windows),
                "successful_windows": len(oos_returns),
                "avg_oos_return": round(avg_oos, 2),
                "oos_win_rate": round(positive_oos / len(oos_returns) * 100, 1) if oos_returns else 0,
                "min_oos_return": round(min(oos_returns), 2) if oos_returns else 0,
                "max_oos_return": round(max(oos_returns), 2) if oos_returns else 0,
            },
        })

    asyncio.create_task(_run())
    return wf_id


def get_walkforward_status(wf_id: str) -> Optional[dict]:
    return _active_walkforwards.get(wf_id)
