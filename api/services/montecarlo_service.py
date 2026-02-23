"""
Monte Carlo simulation service.
Shuffles trade returns to test strategy robustness.
"""

import asyncio
import logging
import random
import math
import uuid
from typing import Dict, List, Optional

logger = logging.getLogger(__name__)

_active_simulations: Dict[str, dict] = {}


def _run_monte_carlo(trades: list, initial_balance: float, num_simulations: int, seed: Optional[int] = None) -> dict:
    """Run Monte Carlo by shuffling trade P&L sequence."""
    if not trades:
        return {"status": "error", "message": "No trades to simulate"}

    if seed is not None:
        random.seed(seed)

    pnls = [t.get("profit_loss", 0) for t in trades if isinstance(t, dict)]
    if not pnls:
        return {"status": "error", "message": "No valid P&L values in trades"}

    # Run simulations
    final_balances = []
    max_drawdowns = []
    equity_curves_sample = []  # Keep first 50 for visualization

    for sim in range(num_simulations):
        shuffled = pnls[:]
        random.shuffle(shuffled)

        balance = initial_balance
        peak = initial_balance
        max_dd = 0.0
        curve = [initial_balance]

        for pnl in shuffled:
            balance += pnl
            if balance > peak:
                peak = balance
            dd = (peak - balance) / peak * 100 if peak > 0 else 0
            if dd > max_dd:
                max_dd = dd
            curve.append(balance)

        final_balances.append(balance)
        max_drawdowns.append(max_dd)

        if sim < 50:
            # Downsample curve for visualization
            step = max(1, len(curve) // 200)
            equity_curves_sample.append(curve[::step])

    # Calculate percentiles
    final_balances.sort()
    max_drawdowns.sort()
    n = len(final_balances)

    def percentile(arr, p):
        idx = int(p / 100 * len(arr))
        idx = min(idx, len(arr) - 1)
        return arr[idx]

    # Probability of profit
    prob_profit = sum(1 for b in final_balances if b > initial_balance) / n * 100

    # Probability of ruin (losing 90%+)
    ruin_threshold = initial_balance * 0.1
    prob_ruin = sum(1 for b in final_balances if b < ruin_threshold) / n * 100

    return {
        "status": "success",
        "num_simulations": num_simulations,
        "num_trades": len(pnls),
        "initial_balance": initial_balance,
        "statistics": {
            "mean_final": round(sum(final_balances) / n, 2),
            "median_final": round(percentile(final_balances, 50), 2),
            "p5_final": round(percentile(final_balances, 5), 2),
            "p25_final": round(percentile(final_balances, 25), 2),
            "p75_final": round(percentile(final_balances, 75), 2),
            "p95_final": round(percentile(final_balances, 95), 2),
            "min_final": round(final_balances[0], 2),
            "max_final": round(final_balances[-1], 2),
            "std_dev": round((sum((b - sum(final_balances)/n)**2 for b in final_balances) / n) ** 0.5, 2),
            # Frontend expects these exact field names
            "prob_of_profit": round(prob_profit, 1),
            "prob_of_ruin": round(prob_ruin, 1),
            "mean_max_dd": round(sum(max_drawdowns) / n, 1),
            "median_max_dd": round(percentile(max_drawdowns, 50), 1),
            "p95_max_dd": round(percentile(max_drawdowns, 95), 1),
            "worst_max_dd": round(max_drawdowns[-1], 1),
        },
        # Frontend expects flat array of final balances for histogram rendering
        "distribution": [round(b, 2) for b in final_balances],
        # Frontend expects "sample_curves" key
        "sample_curves": equity_curves_sample,
    }


def _histogram(values: list, bins: int) -> dict:
    """Create histogram data."""
    if not values:
        return {"edges": [], "counts": []}
    lo, hi = min(values), max(values)
    if lo == hi:
        return {"edges": [lo, hi], "counts": [len(values)]}
    step = (hi - lo) / bins
    edges = [lo + i * step for i in range(bins + 1)]
    counts = [0] * bins
    for v in values:
        idx = min(int((v - lo) / step), bins - 1)
        counts[idx] += 1
    return {"edges": [round(e, 2) for e in edges], "counts": counts}


async def run_monte_carlo(
    trades: list,
    initial_balance: float = 10000.0,
    num_simulations: int = 1000,
    seed: Optional[int] = None,
) -> dict:
    """Run Monte Carlo simulation asynchronously."""
    return await asyncio.to_thread(_run_monte_carlo, trades, initial_balance, num_simulations, seed)
