"""
Analysis endpoints: walk-forward, Monte Carlo, trade export, risk metrics.
"""

import csv
import io
import json
import logging
import math
from typing import Optional

from fastapi import APIRouter, Query
from fastapi.responses import StreamingResponse

from api.services.walkforward_service import start_walkforward, get_walkforward_status
from api.services.multisymbol_service import run_multisymbol_backtest
from api.services.montecarlo_service import run_monte_carlo
from api.services.history_service import get_history_entry

logger = logging.getLogger(__name__)
router = APIRouter(prefix="/api/analysis", tags=["analysis"])


# -- Trade CSV Export ----------------------------------------------------------

@router.get("/export/trades/{history_id}")
async def export_trades_csv(history_id: int):
    """Export full trade list as CSV for a history entry."""
    entry = get_history_entry(history_id)
    if not entry:
        return {"status": "error", "message": f"Entry {history_id} not found"}

    full_result = entry.get("full_result", {})
    trades = full_result.get("trades", [])

    if not trades:
        return {"status": "error", "message": "No trade data available for this entry"}

    # Generate CSV
    output = io.StringIO()
    writer = csv.writer(output)
    writer.writerow(["#", "Direction", "Entry Time", "Exit Time", "Entry Price", "Exit Price", "Lots", "P/L", "Commission", "Exit Reason"])

    for t in trades:
        writer.writerow([
            t.get("id", ""),
            t.get("direction", ""),
            t.get("entry_time", ""),
            t.get("exit_time", ""),
            t.get("entry_price", ""),
            t.get("exit_price", ""),
            t.get("lot_size", ""),
            t.get("profit_loss", ""),
            t.get("commission", ""),
            t.get("exit_reason", ""),
        ])

    output.seek(0)
    strategy = entry.get("strategy", "unknown")
    symbol = entry.get("symbol", "unknown")

    return StreamingResponse(
        iter([output.getvalue()]),
        media_type="text/csv",
        headers={"Content-Disposition": f'attachment; filename="trades_{strategy}_{symbol}_{history_id}.csv"'},
    )


# -- Risk Metrics Computation -------------------------------------------------

@router.get("/risk/{history_id}")
async def get_risk_metrics(history_id: int):
    """Compute advanced risk metrics from equity curve."""
    entry = get_history_entry(history_id)
    if not entry:
        return {"status": "error", "message": f"Entry {history_id} not found"}

    equity = entry.get("equity_curve", [])
    timestamps = entry.get("equity_timestamps", [])
    initial = entry.get("initial_balance", 10000)

    if len(equity) < 10:
        return {"status": "error", "message": "Insufficient equity data for risk analysis"}

    # Calculate returns
    returns = []
    for i in range(1, len(equity)):
        if equity[i-1] > 0:
            returns.append((equity[i] - equity[i-1]) / equity[i-1])

    if not returns:
        return {"status": "error", "message": "No valid returns data"}

    # Drawdown series
    peak = equity[0]
    drawdown_series = []
    drawdown_pct_series = []
    for val in equity:
        if val > peak:
            peak = val
        dd = peak - val
        dd_pct = (dd / peak * 100) if peak > 0 else 0
        drawdown_series.append(round(dd, 2))
        drawdown_pct_series.append(round(dd_pct, 2))

    # Rolling Sharpe (50-sample window)
    window = min(50, len(returns) // 2)
    rolling_sharpe = []
    for i in range(window, len(returns)):
        chunk = returns[i-window:i]
        mean_r = sum(chunk) / len(chunk)
        std_r = (sum((r - mean_r)**2 for r in chunk) / len(chunk)) ** 0.5
        sharpe = (mean_r / std_r * math.sqrt(252)) if std_r > 0 else 0
        rolling_sharpe.append(round(sharpe, 3))

    # Monthly returns (approximate by splitting into 20-day chunks)
    chunk_size = max(1, len(returns) // 12)
    monthly_returns = []
    for i in range(0, len(returns), chunk_size):
        chunk = returns[i:i+chunk_size]
        cumulative = 1.0
        for r in chunk:
            cumulative *= (1 + r)
        monthly_returns.append(round((cumulative - 1) * 100, 2))

    # Calmar ratio
    max_dd_pct = max(drawdown_pct_series) if drawdown_pct_series else 0
    total_return = ((equity[-1] - initial) / initial * 100) if initial > 0 else 0
    calmar = round(total_return / max_dd_pct, 2) if max_dd_pct > 0 else 0

    # Ulcer index
    sq_drawdowns = [d**2 for d in drawdown_pct_series]
    ulcer_index = round((sum(sq_drawdowns) / len(sq_drawdowns)) ** 0.5, 2) if sq_drawdowns else 0

    # Tail ratio (95th percentile gain / 5th percentile loss)
    sorted_returns = sorted(returns)
    n = len(sorted_returns)
    p5 = sorted_returns[int(0.05 * n)] if n > 20 else sorted_returns[0]
    p95 = sorted_returns[int(0.95 * n)] if n > 20 else sorted_returns[-1]
    tail_ratio = round(abs(p95 / p5), 2) if p5 != 0 else 0

    # Build time-indexed drawdown series for charts
    dd_with_time = []
    for i, dd in enumerate(drawdown_pct_series):
        if i < len(timestamps) and timestamps[i]:
            dd_with_time.append({"time": timestamps[i], "value": dd})

    # Build time-indexed rolling sharpe series for charts
    sharpe_with_time = []
    for i, s in enumerate(rolling_sharpe):
        ts_idx = i + window  # offset because rolling window starts later
        if ts_idx < len(timestamps) and timestamps[ts_idx]:
            sharpe_with_time.append({"time": timestamps[ts_idx], "value": s})

    # Build monthly returns with month labels
    months_labels = []
    for i in range(len(monthly_returns)):
        month_idx = i + 1
        months_labels.append({"month": f"M{month_idx}", "return_pct": monthly_returns[i]})

    # Return flat structure matching frontend RiskMetrics interface
    return {
        "status": "success",
        "calmar_ratio": calmar,
        "ulcer_index": ulcer_index,
        "tail_ratio": tail_ratio,
        "max_consecutive_losses": _max_consecutive(returns, lambda r: r < 0),
        "max_consecutive_wins": _max_consecutive(returns, lambda r: r > 0),
        "avg_dd_duration_hours": _avg_drawdown_duration(drawdown_pct_series),
        "drawdown_series": dd_with_time,
        "rolling_sharpe": sharpe_with_time,
        "monthly_returns": months_labels,
    }


def _max_consecutive(returns, condition):
    max_c = 0
    current = 0
    for r in returns:
        if condition(r):
            current += 1
            max_c = max(max_c, current)
        else:
            current = 0
    return max_c


def _avg_drawdown_duration(dd_series):
    """Average number of samples spent in drawdown."""
    if not dd_series:
        return 0
    durations = []
    current = 0
    for dd in dd_series:
        if dd > 0.1:  # In drawdown (>0.1%)
            current += 1
        else:
            if current > 0:
                durations.append(current)
            current = 0
    if current > 0:
        durations.append(current)
    return round(sum(durations) / len(durations), 1) if durations else 0


# -- Walk-Forward Analysis -----------------------------------------------------

@router.post("/walkforward/start")
async def api_start_walkforward(config: dict):
    """Start walk-forward analysis."""
    wf_id = await start_walkforward(config)
    status = get_walkforward_status(wf_id)
    return {"status": "ok", "wf_id": wf_id, "progress": status}


@router.get("/walkforward/{wf_id}")
async def api_walkforward_status(wf_id: str):
    """Get walk-forward analysis status and results."""
    raw = get_walkforward_status(wf_id)
    if not raw:
        return {"status": "error", "message": f"Walk-forward {wf_id} not found"}

    wf_status = raw.get("status", "running")

    # Calculate progress percentage
    total = raw.get("windows_total", 1)
    completed = raw.get("windows_completed", 0)
    progress_pct = round(completed / total * 100, 1) if total > 0 else 0

    # Convert internal results to frontend-expected window format
    windows = []
    for i, r in enumerate(raw.get("results", [])):
        w = r.get("window", {})
        oos = r.get("out_sample_result") or {}
        windows.append({
            "window_index": i,
            "is_start": w.get("in_sample_start", ""),
            "is_end": w.get("in_sample_end", ""),
            "oos_start": w.get("out_sample_start", ""),
            "oos_end": w.get("out_sample_end", ""),
            "best_params": r.get("in_sample_best_params") or {},
            "is_score": r.get("in_sample_score", 0),
            "oos_return": oos.get("return_percent", 0),
            "oos_sharpe": oos.get("sharpe_ratio", 0),
            "oos_max_dd": oos.get("max_drawdown_pct", 0),
        })

    # Build summary matching frontend interface
    summary_raw = raw.get("summary", {})
    summary = {
        "avg_oos_return": summary_raw.get("avg_oos_return", 0),
        "oos_win_rate": summary_raw.get("oos_win_rate", 0),
        "best_oos_return": summary_raw.get("max_oos_return", 0),
        "worst_oos_return": summary_raw.get("min_oos_return", 0),
        "total_windows": summary_raw.get("total_windows", total),
    }

    return {
        "status": wf_status,
        "wf_id": wf_id,
        "progress": progress_pct,
        "windows": windows,
        "summary": summary,
    }


# -- Monte Carlo ---------------------------------------------------------------

@router.post("/montecarlo/{history_id}")
async def api_monte_carlo(
    history_id: int,
    num_simulations: int = Query(default=1000, ge=100, le=10000),
):
    """Run Monte Carlo simulation on a completed backtest."""
    entry = get_history_entry(history_id)
    if not entry:
        return {"status": "error", "message": f"Entry {history_id} not found"}

    full_result = entry.get("full_result", {})
    trades = full_result.get("trades", [])
    initial = entry.get("initial_balance", 10000)

    if not trades:
        return {"status": "error", "message": "No trade data available. Run backtest with trade list enabled."}

    result = await run_monte_carlo(trades, initial, num_simulations)
    return result


# -- Sweep Guardrails ----------------------------------------------------------

@router.post("/sweep/validate")
async def validate_sweep_config(config: dict):
    """Validate sweep configuration and return combination count with warnings."""
    ranges = config.get("parameter_ranges", [])

    total = 1
    per_param = {}
    for r in ranges:
        steps = int(round((r["max"] - r["min"]) / r["step"])) + 1
        per_param[r["name"]] = steps
        total *= steps

    warnings = []
    if total > 100000:
        warnings.append(f"Grid would generate {total:,} combinations. This will take a very long time. Consider using Random search or reducing ranges.")
    elif total > 10000:
        warnings.append(f"Grid will generate {total:,} combinations. This may take 30+ minutes.")
    elif total > 1000:
        warnings.append(f"Grid will generate {total:,} combinations.")

    if total == 0:
        warnings.append("No valid combinations. Check that min < max for all parameters.")

    # Estimate time (rough: ~2 sec per backtest on average)
    est_seconds = total * 2 / max(config.get("max_workers", 16), 1)

    return {
        "status": "ok",
        "total_combinations": total,
        "per_parameter": per_param,
        "estimated_seconds": round(est_seconds),
        "warnings": warnings,
    }


# -- Multi-Symbol Backtest -----------------------------------------------------

@router.post("/multisymbol")
async def api_multisymbol_backtest(config: dict):
    """Run the same strategy on multiple symbols and get portfolio-level results."""
    result = await run_multisymbol_backtest(config)
    return result
