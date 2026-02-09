"""
DD Reduction Parallel Parameter Sweep

Uses ThreadPoolExecutor to run multiple DD-reduction configurations in parallel.
Each configuration runs the test_dd_reduction.exe with different parameters.

Usage:
    python scripts/sweep_dd_reduction.py [--workers N]
"""

import json
import subprocess
import sys
import os
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from itertools import product
from pathlib import Path

# Configuration
BACKTEST_EXE = str(Path(__file__).parent.parent / "build" / "validation" / "test_dd_reduction.exe")
MAX_WORKERS = max(1, os.cpu_count() - 1) if os.cpu_count() else 4  # Leave 1 core free

# DD Reduction parameter grid
PARAM_GRID = {
    # Mechanism 1: DD-based entry pause (pause when DD exceeds threshold, resume at half)
    "dd_pause": [0, 20, 30, 40, 50, 60],

    # Mechanism 2: Max concurrent positions
    "max_positions": [0, 5, 10, 15, 20, 30, 50],

    # Mechanism 3: Equity hard stop (close all above threshold)
    "equity_stop": [0, 40, 50, 60, 70],
}

def generate_combinations():
    """Generate all parameter combinations to test"""
    combinations = []

    # 1. Baseline (no DD reduction)
    combinations.append({
        "dd_pause": 0, "max_positions": 0, "dd_spacing_mult": 1.0,
        "dd_lot_scale": 0, "equity_stop": 0, "resume_below": 0,
        "label": "BASELINE"
    })

    # 2. DD pause sweep (single mechanism)
    for dd_pause in [20, 30, 40, 50, 60]:
        combinations.append({
            "dd_pause": dd_pause, "max_positions": 0, "dd_spacing_mult": 1.0,
            "dd_lot_scale": 0, "equity_stop": 0, "resume_below": dd_pause * 0.5,
            "label": f"PAUSE_{dd_pause}"
        })

    # 3. DD pause with different resume levels
    for dd_pause in [30, 40, 50]:
        for resume_pct in [0.3, 0.5, 0.7]:
            resume = dd_pause * resume_pct
            combinations.append({
                "dd_pause": dd_pause, "max_positions": 0, "dd_spacing_mult": 1.0,
                "dd_lot_scale": 0, "equity_stop": 0, "resume_below": resume,
                "label": f"PAUSE_{dd_pause}_RES_{int(resume)}"
            })

    # 4. Max positions sweep (single mechanism)
    for max_pos in [3, 5, 8, 10, 15, 20, 30, 50]:
        combinations.append({
            "dd_pause": 0, "max_positions": max_pos, "dd_spacing_mult": 1.0,
            "dd_lot_scale": 0, "equity_stop": 0, "resume_below": 0,
            "label": f"MAXPOS_{max_pos}"
        })

    # 5. Equity stop sweep (single mechanism)
    for eq_stop in [30, 40, 50, 60, 70, 80]:
        combinations.append({
            "dd_pause": 0, "max_positions": 0, "dd_spacing_mult": 1.0,
            "dd_lot_scale": 0, "equity_stop": eq_stop, "resume_below": 0,
            "label": f"EQSTOP_{eq_stop}"
        })

    # 6. Combined: DD pause + max positions
    for dd_pause in [30, 40, 50]:
        for max_pos in [5, 10, 15, 20]:
            combinations.append({
                "dd_pause": dd_pause, "max_positions": max_pos, "dd_spacing_mult": 1.0,
                "dd_lot_scale": 0, "equity_stop": 0, "resume_below": dd_pause * 0.5,
                "label": f"PAUSE_{dd_pause}_MAXPOS_{max_pos}"
            })

    # 7. Combined: DD pause + equity stop
    for dd_pause in [30, 40, 50]:
        for eq_stop in [50, 60, 70]:
            if eq_stop > dd_pause:  # Equity stop must be above pause threshold
                combinations.append({
                    "dd_pause": dd_pause, "max_positions": 0, "dd_spacing_mult": 1.0,
                    "dd_lot_scale": 0, "equity_stop": eq_stop, "resume_below": dd_pause * 0.5,
                    "label": f"PAUSE_{dd_pause}_EQSTOP_{eq_stop}"
                })

    # 8. Combined: All three
    for dd_pause in [30, 40, 50]:
        for max_pos in [10, 20]:
            for eq_stop in [60, 70]:
                if eq_stop > dd_pause:
                    combinations.append({
                        "dd_pause": dd_pause, "max_positions": max_pos, "dd_spacing_mult": 1.0,
                        "dd_lot_scale": 0, "equity_stop": eq_stop,
                        "resume_below": dd_pause * 0.5,
                        "label": f"ALL_{dd_pause}_{max_pos}_{eq_stop}"
                    })

    return combinations


def run_single_backtest(params):
    """Run a single backtest with given DD-reduction parameters"""
    cmd = [
        BACKTEST_EXE,
        f"--dd_pause={params['dd_pause']}",
        f"--max_positions={params['max_positions']}",
        f"--dd_spacing_mult={params['dd_spacing_mult']}",
        f"--dd_lot_scale={params['dd_lot_scale']}",
        f"--equity_stop={params['equity_stop']}",
        f"--resume_below={params['resume_below']}",
        "--json"
    ]

    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=300  # 5 minute timeout
        )

        if result.returncode == 0:
            try:
                output = json.loads(result.stdout)
                output["label"] = params.get("label", "")
                return output
            except json.JSONDecodeError:
                return {"error": "JSON parse error", "stdout": result.stdout[:200], "label": params.get("label", "")}
        else:
            return {"error": f"Exit code {result.returncode}", "stderr": result.stderr[:200], "label": params.get("label", "")}
    except subprocess.TimeoutExpired:
        return {"error": "Timeout", "label": params.get("label", "")}
    except Exception as e:
        return {"error": str(e), "label": params.get("label", "")}


def main():
    # Parse args
    workers = MAX_WORKERS
    for arg in sys.argv[1:]:
        if arg.startswith("--workers="):
            workers = int(arg.split("=")[1])

    # Check executable exists
    if not os.path.exists(BACKTEST_EXE):
        print(f"ERROR: Executable not found: {BACKTEST_EXE}")
        print("Build it first: cd build && mingw32-make test_dd_reduction")
        sys.exit(1)

    combinations = generate_combinations()
    total = len(combinations)

    print(f"=" * 80)
    print(f"  DD REDUCTION PARALLEL SWEEP")
    print(f"  Configurations: {total}")
    print(f"  Workers: {workers}")
    print(f"  Executable: {BACKTEST_EXE}")
    print(f"=" * 80)

    results = []
    completed = 0
    start_time = time.time()

    with ThreadPoolExecutor(max_workers=workers) as executor:
        futures = {
            executor.submit(run_single_backtest, params): params
            for params in combinations
        }

        for future in as_completed(futures):
            result = future.result()
            results.append(result)
            completed += 1

            # Progress output
            if "metrics" in result:
                m = result["metrics"]
                label = result.get("label", "?")
                elapsed = time.time() - start_time
                eta = (elapsed / completed) * (total - completed) if completed > 0 else 0
                print(f"[{completed:3d}/{total}] {label:30s} | "
                      f"Return={m.get('return_mult', 0):5.2f}x  "
                      f"DD={m.get('max_dd_pct', 0):5.1f}%  "
                      f"Trades={m.get('total_trades', 0):6d}  "
                      f"Sharpe={m.get('sharpe_proxy', 0):5.2f}  "
                      f"(ETA: {eta:.0f}s)")
            else:
                print(f"[{completed:3d}/{total}] {result.get('label', '?'):30s} | ERROR: {result.get('error', 'unknown')}")

    elapsed = time.time() - start_time
    print(f"\n{'=' * 80}")
    print(f"  COMPLETED in {elapsed:.1f}s ({elapsed/total:.1f}s per config, {workers} workers)")
    print(f"{'=' * 80}")

    # Sort by sharpe_proxy (return/DD ratio)
    valid_results = [r for r in results if "metrics" in r]
    valid_results.sort(key=lambda r: r["metrics"].get("sharpe_proxy", 0), reverse=True)

    # Print top results table
    print(f"\n{'=' * 120}")
    print(f"  TOP 20 CONFIGURATIONS (sorted by Sharpe proxy = (Return-1) / DD)")
    print(f"{'=' * 120}")
    print(f"{'Rank':>4} {'Label':>35} {'Return':>8} {'MaxDD%':>7} {'Trades':>7} "
          f"{'Swap$':>8} {'Sharpe':>7} {'Pauses':>7} {'Stopped':>8}")
    print("-" * 120)

    for i, r in enumerate(valid_results[:20]):
        m = r["metrics"]
        print(f"{i+1:4d} {r.get('label', '?'):>35} "
              f"{m.get('return_mult', 0):7.2f}x "
              f"{m.get('max_dd_pct', 0):6.1f}% "
              f"{m.get('total_trades', 0):7d} "
              f"{m.get('total_swap', 0):8.0f} "
              f"{m.get('sharpe_proxy', 0):7.2f} "
              f"{m.get('pause_count', 0):7d} "
              f"{'YES' if m.get('equity_stopped') else 'no':>8}")

    # Print baseline for comparison
    print("-" * 120)
    baseline = next((r for r in valid_results if r.get("label") == "BASELINE"), None)
    if baseline:
        m = baseline["metrics"]
        rank = valid_results.index(baseline) + 1
        print(f"{'BASE':>4} {'BASELINE (no DD reduction)':>35} "
              f"{m.get('return_mult', 0):7.2f}x "
              f"{m.get('max_dd_pct', 0):6.1f}% "
              f"{m.get('total_trades', 0):7d} "
              f"{m.get('total_swap', 0):8.0f} "
              f"{m.get('sharpe_proxy', 0):7.2f} "
              f"{m.get('pause_count', 0):7d} "
              f"{'YES' if m.get('equity_stopped') else 'no':>8}")
        print(f"  (Baseline rank: #{rank} of {len(valid_results)})")

    # Print DD reduction analysis
    print(f"\n{'=' * 120}")
    print(f"  DD REDUCTION ANALYSIS: Configs that reduce DD by >10% with <20% return loss")
    print(f"{'=' * 120}")

    if baseline and "metrics" in baseline:
        base_return = baseline["metrics"]["return_mult"]
        base_dd = baseline["metrics"]["max_dd_pct"]

        good_configs = []
        for r in valid_results:
            m = r["metrics"]
            dd_reduction = base_dd - m["max_dd_pct"]
            return_loss = (base_return - m["return_mult"]) / base_return * 100

            if dd_reduction > 10 and return_loss < 20 and m["return_mult"] > 1.0:
                good_configs.append((r, dd_reduction, return_loss))

        if good_configs:
            good_configs.sort(key=lambda x: x[1], reverse=True)  # Sort by DD reduction
            print(f"{'Label':>35} {'Return':>8} {'MaxDD%':>7} {'DD Saved':>9} {'Ret Loss':>9} {'Sharpe':>7}")
            print("-" * 90)
            for r, dd_red, ret_loss in good_configs[:15]:
                m = r["metrics"]
                print(f"{r.get('label', '?'):>35} "
                      f"{m['return_mult']:7.2f}x "
                      f"{m['max_dd_pct']:6.1f}% "
                      f"{dd_red:+8.1f}% "
                      f"{ret_loss:8.1f}% "
                      f"{m['sharpe_proxy']:7.2f}")
        else:
            print("  No configs found that reduce DD >10% with <20% return loss")

    # Save full results to JSON
    output_file = Path(__file__).parent.parent / "validation" / "dd_reduction_results.json"
    with open(output_file, "w") as f:
        json.dump(valid_results, f, indent=2)
    print(f"\nFull results saved to: {output_file}")


if __name__ == "__main__":
    main()
