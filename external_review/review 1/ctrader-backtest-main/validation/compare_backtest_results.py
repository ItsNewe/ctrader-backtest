"""
Compare Backtest Results: Our Engine vs MT5
Validates that implementation matches MT5 within tolerance
"""

import json
from pathlib import Path

def compare_results(our_results, mt5_results, tolerance_percent=1.0):
    """
    Compare backtest results between our engine and MT5

    Args:
        our_results: dict with our engine results
        mt5_results: dict with MT5 results
        tolerance_percent: acceptable difference percentage
    """

    print("="*70)
    print("BACKTEST COMPARISON: Our Engine vs MT5")
    print("="*70)
    print()

    differences = {}
    passed = True

    # Compare final balance
    if 'final_balance' in our_results and 'final_balance' in mt5_results:
        our_bal = our_results['final_balance']
        mt5_bal = mt5_results['final_balance']
        diff = abs(our_bal - mt5_bal)
        diff_pct = (diff / mt5_bal) * 100 if mt5_bal != 0 else 0

        print(f"Final Balance:")
        print(f"  MT5:        ${mt5_bal:,.2f}")
        print(f"  Our Engine: ${our_bal:,.2f}")
        print(f"  Difference: ${diff:,.2f} ({diff_pct:.3f}%)")

        if diff_pct <= tolerance_percent:
            print(f"  [OK] Within {tolerance_percent}% tolerance")
        else:
            print(f"  [FAIL] Exceeds {tolerance_percent}% tolerance")
            passed = False

        differences['final_balance'] = {
            'mt5': mt5_bal,
            'ours': our_bal,
            'diff': diff,
            'diff_pct': diff_pct,
            'passed': diff_pct <= tolerance_percent
        }
        print()

    # Compare total trades
    if 'total_trades' in our_results and 'total_trades' in mt5_results:
        our_trades = our_results['total_trades']
        mt5_trades = mt5_results['total_trades']

        print(f"Total Trades:")
        print(f"  MT5:        {mt5_trades}")
        print(f"  Our Engine: {our_trades}")

        if our_trades == mt5_trades:
            print(f"  [OK] Exact match")
        else:
            print(f"  [FAIL] Trade count mismatch")
            passed = False

        differences['total_trades'] = {
            'mt5': mt5_trades,
            'ours': our_trades,
            'passed': our_trades == mt5_trades
        }
        print()

    # Compare profit
    if 'total_profit' in our_results and 'total_profit' in mt5_results:
        our_profit = our_results['total_profit']
        mt5_profit = mt5_results['total_profit']
        diff = abs(our_profit - mt5_profit)

        print(f"Total Profit:")
        print(f"  MT5:        ${mt5_profit:,.2f}")
        print(f"  Our Engine: ${our_profit:,.2f}")
        print(f"  Difference: ${diff:,.2f}")

        if mt5_profit != 0:
            diff_pct = (diff / abs(mt5_profit)) * 100
            print(f"  ({diff_pct:.3f}%)")

        differences['total_profit'] = {
            'mt5': mt5_profit,
            'ours': our_profit,
            'diff': diff
        }
        print()

    # Compare margin usage
    if 'max_margin_used' in our_results and 'max_margin_used' in mt5_results:
        our_margin = our_results['max_margin_used']
        mt5_margin = mt5_results['max_margin_used']
        diff = abs(our_margin - mt5_margin)

        print(f"Max Margin Used:")
        print(f"  MT5:        ${mt5_margin:,.2f}")
        print(f"  Our Engine: ${our_margin:,.2f}")
        print(f"  Difference: ${diff:,.2f}")

        if diff < 1.0:
            print(f"  [OK] Within $1.00")
        else:
            print(f"  [WARN] Margin difference > $1.00")

        differences['max_margin_used'] = {
            'mt5': mt5_margin,
            'ours': our_margin,
            'diff': diff
        }
        print()

    # Summary
    print("="*70)
    print("VALIDATION SUMMARY")
    print("="*70)

    if passed:
        print("[OK] VALIDATION PASSED")
        print(f"Results match MT5 within {tolerance_percent}% tolerance")
    else:
        print("[FAIL] VALIDATION FAILED")
        print("Results exceed tolerance - review implementation")

    return {
        'passed': passed,
        'tolerance_percent': tolerance_percent,
        'differences': differences
    }

def load_results(file_path):
    """Load results from JSON file"""
    with open(file_path) as f:
        return json.load(f)

def main():
    # Example usage
    print("Backtest Results Comparison Tool")
    print()
    print("Usage:")
    print("  1. Run backtest in MT5, save results to mt5_results.json")
    print("  2. Run backtest in our engine, save results to our_results.json")
    print("  3. Run this script to compare")
    print()

    # Check if result files exist
    our_file = Path("validation/our_results.json")
    mt5_file = Path("validation/mt5_results.json")

    if our_file.exists() and mt5_file.exists():
        our_results = load_results(our_file)
        mt5_results = load_results(mt5_file)

        comparison = compare_results(our_results, mt5_results, tolerance_percent=1.0)

        # Save comparison
        output_file = Path("validation/analysis/comparison_results.json")
        with open(output_file, 'w') as f:
            json.dump(comparison, f, indent=2)

        print(f"\nComparison saved to: {output_file}")

    else:
        print("Result files not found. Expected:")
        print(f"  - {our_file}")
        print(f"  - {mt5_file}")
        print()
        print("Example result file format:")
        example = {
            'final_balance': 10250.50,
            'initial_balance': 10000.00,
            'total_profit': 250.50,
            'total_trades': 25,
            'max_margin_used': 240.00,
            'winning_trades': 15,
            'losing_trades': 10
        }
        print(json.dumps(example, indent=2))

if __name__ == "__main__":
    main()
