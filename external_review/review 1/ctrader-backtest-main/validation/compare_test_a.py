"""
Test A Validation: Compare MT5 vs Our Engine Results
Checks if SL/TP execution order matches between systems
"""

import pandas as pd
import os
import sys

def compare_test_a_results():
    """Compare Test A results from MT5 and our engine"""

    mt5_file = "validation/mt5/test_a_mt5_result.csv"
    our_file = "validation/ours/test_a_our_result.csv"

    print("=" * 70)
    print("TEST A VALIDATION: SL/TP EXECUTION ORDER")
    print("=" * 70)
    print()

    # Check if files exist
    if not os.path.exists(mt5_file):
        print(f"❌ ERROR: MT5 result file not found: {mt5_file}")
        print()
        print("Steps to create:")
        print("1. Open MetaEditor in MT5")
        print("2. Open validation/micro_tests/test_a_sl_tp_order.mq5")
        print("3. Compile the EA (F7)")
        print("4. Open MT5 Strategy Tester (Ctrl+R)")
        print("5. Select test_a_sl_tp_order EA")
        print("6. Configure:")
        print("   - Symbol: EURUSD")
        print("   - Period: H1")
        print("   - Date range: Recent volatile period (e.g., last month)")
        print("   - Execution: Every tick based on real ticks")
        print("7. Click Start")
        print("8. After completion, find test_a_mt5_result.csv in:")
        print("   C:\\Users\\<YourUser>\\AppData\\Roaming\\MetaQuotes\\Terminal\\<ID>\\MQL5\\Files\\")
        print("9. Copy it to validation/mt5/test_a_mt5_result.csv")
        return False

    if not os.path.exists(our_file):
        print(f"❌ ERROR: Our result file not found: {our_file}")
        print()
        print("Steps to create:")
        print("1. Export EURUSD H1 data from MT5 to CSV")
        print("2. Save as validation/configs/test_a_data.csv")
        print("3. Compile our test: g++ -std=c++17 -O3 validation/test_a_our_backtest.cpp \\")
        print("      src/backtest_engine.cpp -I include -o validation/test_a_backtest")
        print("4. Run: validation/test_a_backtest")
        return False

    # Load results
    print("Loading results...")
    try:
        mt5_result = pd.read_csv(mt5_file)
        our_result = pd.read_csv(our_file)
    except Exception as e:
        print(f"❌ ERROR loading CSV files: {e}")
        return False

    print("✓ MT5 result loaded")
    print("✓ Our result loaded")
    print()

    # Display results
    print("=" * 70)
    print("MT5 RESULT")
    print("=" * 70)
    print(mt5_result.to_string(index=False))
    print()

    print("=" * 70)
    print("OUR ENGINE RESULT")
    print("=" * 70)
    print(our_result.to_string(index=False))
    print()

    # Compare exit reasons
    print("=" * 70)
    print("COMPARISON")
    print("=" * 70)

    mt5_exit_reason = mt5_result['exit_reason'].iloc[0]
    our_exit_reason = our_result['exit_reason'].iloc[0]

    print(f"MT5 Exit Reason:  {mt5_exit_reason}")
    print(f"Our Exit Reason:  {our_exit_reason}")
    print()

    if mt5_exit_reason == our_exit_reason:
        print("✓ PASS: Exit reasons match!")
        print()
        print("CONCLUSION:")
        print(f"  Both systems executed {mt5_exit_reason} first")
        print("  Our engine correctly matches MT5 behavior")
        result = True
    else:
        print("❌ FAIL: Exit reasons DO NOT match!")
        print()
        print("CONCLUSION:")
        print(f"  MT5 executed: {mt5_exit_reason}")
        print(f"  Our engine executed: {our_exit_reason}")
        print()
        print("ACTION REQUIRED:")
        print("  Our engine needs to be updated to match MT5 behavior")
        print("  File to modify: src/backtest_engine.cpp, lines 66-72")
        print()

        if mt5_exit_reason == "SL":
            print("  MT5 checks Stop Loss FIRST, then Take Profit")
            print("  Current code checks in this order - might be correct")
            print("  Issue might be in price proximity or timing")
        elif mt5_exit_reason == "TP":
            print("  MT5 checks Take Profit FIRST, then Stop Loss")
            print("  Need to reverse order in CheckStopLoss/CheckTakeProfit")
        else:
            print("  MT5 result is UNKNOWN - test may need to be re-run")

        result = False

    # Additional comparisons
    print()
    print("=" * 70)
    print("ADDITIONAL METRICS")
    print("=" * 70)

    # Compare prices
    mt5_exit_price = mt5_result['exit_price'].iloc[0]
    our_exit_price = our_result['exit_price'].iloc[0]
    price_diff = abs(mt5_exit_price - our_exit_price)

    print(f"Exit Price Difference: {price_diff:.5f}")

    if price_diff < 0.0001:  # Within 1 pip
        print("  ✓ Prices match within tolerance (1 pip)")
    else:
        print(f"  ⚠ Prices differ by {price_diff/0.0001:.1f} pips")

    # Compare profit
    mt5_profit = mt5_result['profit'].iloc[0]
    our_profit = our_result['profit'].iloc[0]
    profit_diff = abs(mt5_profit - our_profit)
    profit_diff_pct = (profit_diff / abs(mt5_profit)) * 100 if mt5_profit != 0 else 0

    print(f"Profit Difference: ${profit_diff:.2f} ({profit_diff_pct:.2f}%)")

    if profit_diff_pct < 1.0:  # Within 1%
        print("  ✓ Profit matches within tolerance (1%)")
    else:
        print(f"  ⚠ Profit differs by {profit_diff_pct:.1f}%")

    # Final summary
    print()
    print("=" * 70)
    print("FINAL RESULT")
    print("=" * 70)

    if result:
        print("✓✓✓ TEST A PASSED ✓✓✓")
        print()
        print("Our engine correctly reproduces MT5 SL/TP execution order!")
        print("Methodology validated. Proceeding to Test B recommended.")
    else:
        print("❌❌❌ TEST A FAILED ❌❌❌")
        print()
        print("Our engine does NOT match MT5 behavior.")
        print("This is expected - it shows us what needs to be fixed!")
        print()
        print("Next steps:")
        print("1. Analyze the difference")
        print("2. Update our engine logic in src/backtest_engine.cpp")
        print("3. Re-run test until it passes")
        print("4. Document the fix in VALIDATION_RESULTS.md")

    print("=" * 70)

    return result

if __name__ == "__main__":
    success = compare_test_a_results()
    sys.exit(0 if success else 1)
