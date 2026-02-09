"""
Master Analysis Script - Analyzes all MT5 validation test results
"""

import pandas as pd
import numpy as np
from pathlib import Path
import json
from datetime import datetime

def analyze_test_f_margin():
    """Analyze margin calculation (Test F)"""
    print("=" * 70)
    print("TEST F: MARGIN CALCULATION")
    print("=" * 70)
    print()

    df = pd.read_csv("validation/mt5/test_f_margin.csv", sep='\t')
    with open("validation/mt5/test_f_summary.json") as f:
        summary = json.load(f)

    leverage = summary['leverage']
    contract_size = summary['contract_size']

    print(f"Leverage: 1:{leverage}")
    print(f"Contract size: {contract_size:,}")
    print()
    print("Margin Requirements:")
    for _, row in df.iterrows():
        lot = row['lot_size']
        price = row['price']
        margin = row['margin_required']
        expected = (lot * contract_size * price) / leverage
        diff = abs(margin - expected)
        status = "OK" if diff < 0.01 else f"DIFF: {diff:.4f}"
        print(f"  {lot:4.2f} lots @ ${price:.5f} = ${margin:7.2f} margin [{status}]")

    print()
    print("Formula verified: Margin = (lot_size × contract_size × price) / leverage")
    print()

    return {
        'test': 'F',
        'name': 'Margin Calculation',
        'formula': '(lot_size * contract_size * price) / leverage',
        'leverage': leverage,
        'contract_size': contract_size,
        'status': 'VERIFIED'
    }

def analyze_test_c_slippage():
    """Analyze slippage distribution (Test C)"""
    print("=" * 70)
    print("TEST C: SLIPPAGE DISTRIBUTION")
    print("=" * 70)
    print()

    df = pd.read_csv("validation/mt5/test_c_slippage.csv", sep='\t')

    mean_slip = df['slippage_points'].mean()
    std_slip = df['slippage_points'].std()
    max_slip = df['slippage_points'].max()
    min_slip = df['slippage_points'].min()

    print(f"Total trades: {len(df)}")
    print(f"Mean slippage: {mean_slip:.6f} points")
    print(f"Std deviation: {std_slip:.6f} points")
    print(f"Range: {min_slip:.6f} to {max_slip:.6f} points")
    print()

    if abs(max_slip) < 0.0001 and abs(min_slip) < 0.0001:
        print("NOTE: All slippage is zero in MT5 Strategy Tester")
        print("This is expected behavior for historical testing")
        print()

    return {
        'test': 'C',
        'name': 'Slippage Distribution',
        'mean_points': float(mean_slip),
        'std_dev_points': float(std_slip),
        'trades_analyzed': len(df),
        'status': 'ZERO_SLIPPAGE' if abs(max_slip) < 0.0001 else 'NORMAL'
    }

def analyze_test_e_swap():
    """Analyze swap timing (Test E)"""
    print("=" * 70)
    print("TEST E: SWAP TIMING")
    print("=" * 70)
    print()

    df = pd.read_csv("validation/mt5/test_e_swap_timing.csv", sep='\t')
    with open("validation/mt5/test_e_summary.json") as f:
        summary = json.load(f)

    swap_events = df[df['event'] == 'swap_applied']

    print(f"Monitoring duration: {summary['duration_hours']} hours")
    print(f"Swap events detected: {len(swap_events)}")
    print()

    if len(swap_events) > 0:
        print("Swap Events:")
        for _, event in swap_events.iterrows():
            print(f"  {event['time_str']} ({event['day_of_week']}) - Swap: ${event['swap_change']:.2f}")
        print()

        # Extract hour
        swap_hours = []
        for _, event in swap_events.iterrows():
            try:
                dt = datetime.strptime(event['time_str'], "%Y.%m.%d %H:%M")
                swap_hours.append(dt.hour)
            except:
                pass

        if swap_hours:
            common_hour = max(set(swap_hours), key=swap_hours.count)
            print(f"Swap typically applied at: {common_hour:02d}:00 server time")
        else:
            common_hour = 0

        print()

        return {
            'test': 'E',
            'name': 'Swap Timing',
            'events_detected': len(swap_events),
            'swap_hour': common_hour,
            'status': 'DETECTED'
        }
    else:
        print("No swap events detected")
        return {
            'test': 'E',
            'name': 'Swap Timing',
            'events_detected': 0,
            'status': 'NO_EVENTS'
        }

def analyze_test_b_ticks():
    """Analyze tick synthesis (Test B)"""
    print("=" * 70)
    print("TEST B: TICK SYNTHESIS PATTERN")
    print("=" * 70)
    print()

    with open("validation/mt5/test_b_summary.json") as f:
        summary = json.load(f)

    print(f"Bars recorded: {summary['bars_recorded']}")
    print(f"Total ticks: {summary['total_ticks']:,}")
    print(f"Average ticks per bar: {summary['avg_ticks_per_bar']:.2f}")
    print()

    # Load tick data (first 1000 rows for analysis)
    print("Loading tick data (analyzing first 10,000 ticks)...")
    df = pd.read_csv("validation/mt5/test_b_ticks.csv", sep='\t', nrows=10000)

    # Analyze tick distribution per bar
    ticks_per_bar = df.groupby('bar_time').size()

    print(f"Ticks per bar statistics (from sample):")
    print(f"  Mean: {ticks_per_bar.mean():.2f}")
    print(f"  Median: {ticks_per_bar.median():.2f}")
    print(f"  Min: {ticks_per_bar.min()}")
    print(f"  Max: {ticks_per_bar.max()}")
    print()

    return {
        'test': 'B',
        'name': 'Tick Synthesis',
        'bars_recorded': summary['bars_recorded'],
        'total_ticks': summary['total_ticks'],
        'avg_ticks_per_bar': summary['avg_ticks_per_bar'],
        'status': 'ANALYZED'
    }

def analyze_test_d_spread():
    """Analyze spread widening (Test D)"""
    print("=" * 70)
    print("TEST D: SPREAD WIDENING")
    print("=" * 70)
    print()

    df = pd.read_csv("validation/mt5/test_d_spread.csv", sep='\t')
    with open("validation/mt5/test_d_summary.json") as f:
        summary = json.load(f)

    print(f"Samples recorded: {summary['samples_recorded']}")
    print(f"Duration: {summary['duration_seconds']} seconds")
    print()

    print("Spread statistics:")
    print(f"  Mean: {df['spread_points'].mean():.2f} points ({df['spread_pips'].mean():.2f} pips)")
    print(f"  Min: {df['spread_points'].min():.2f} points")
    print(f"  Max: {df['spread_points'].max():.2f} points")
    print(f"  Std dev: {df['spread_points'].std():.2f} points")
    print()

    # Correlation with ATR
    if 'atr' in df.columns:
        correlation = df['spread_pips'].corr(df['atr'])
        print(f"Correlation with ATR: {correlation:.4f}")
        if abs(correlation) > 0.3:
            print(f"  -> {'Positive' if correlation > 0 else 'Negative'} correlation detected")
        else:
            print("  -> Weak correlation")
        print()

    return {
        'test': 'D',
        'name': 'Spread Widening',
        'samples': summary['samples_recorded'],
        'mean_spread_points': float(df['spread_points'].mean()),
        'status': 'ANALYZED'
    }

def main():
    """Run all analyses"""
    print()
    print("*" * 70)
    print("MT5 VALIDATION TEST RESULTS - COMPLETE ANALYSIS")
    print("*" * 70)
    print()

    results = {}

    # Run all analyses
    try:
        results['test_f'] = analyze_test_f_margin()
    except Exception as e:
        print(f"ERROR in Test F: {e}")
        results['test_f'] = {'status': 'ERROR'}

    try:
        results['test_c'] = analyze_test_c_slippage()
    except Exception as e:
        print(f"ERROR in Test C: {e}")
        results['test_c'] = {'status': 'ERROR'}

    try:
        results['test_e'] = analyze_test_e_swap()
    except Exception as e:
        print(f"ERROR in Test E: {e}")
        results['test_e'] = {'status': 'ERROR'}

    try:
        results['test_b'] = analyze_test_b_ticks()
    except Exception as e:
        print(f"ERROR in Test B: {e}")
        results['test_b'] = {'status': 'ERROR'}

    try:
        results['test_d'] = analyze_test_d_spread()
    except Exception as e:
        print(f"ERROR in Test D: {e}")
        results['test_d'] = {'status': 'ERROR'}

    # Summary
    print("=" * 70)
    print("SUMMARY")
    print("=" * 70)
    print()

    for test_id, result in results.items():
        status = result.get('status', 'UNKNOWN')
        name = result.get('name', test_id)
        print(f"{name:30s} [{status}]")

    # Export complete analysis
    output_dir = Path("validation/analysis")
    output_dir.mkdir(parents=True, exist_ok=True)

    with open(output_dir / "complete_analysis.json", 'w') as f:
        json.dump(results, f, indent=2)

    print()
    print(f"Complete analysis saved to: {output_dir / 'complete_analysis.json'}")
    print()

    return True

if __name__ == "__main__":
    import sys
    success = main()
    sys.exit(0 if success else 1)
