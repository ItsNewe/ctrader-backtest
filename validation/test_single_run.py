"""
Quick test of automated MT5 execution - Test F only
"""

import sys
from pathlib import Path

# Add parent directory to path
sys.path.insert(0, str(Path(__file__).parent))

from run_mt5_tester import MT5StrategyTesterRunner

def main():
    print("=" * 70)
    print("RUNNING TEST F - MARGIN CALCULATION")
    print("=" * 70)
    print()

    runner = MT5StrategyTesterRunner()

    # Test F configuration
    test_config = {
        'symbol': 'EURUSD',
        'period': 'H1',
        'model': 0,  # Every tick
        'from_date': '2025.12.01',
        'to_date': '2025.12.05',
    }

    # Run test
    success = runner.run_test('test_f', 'test_f_margin_calc', test_config)

    if success:
        print()
        print("Waiting for test completion...")
        import time
        time.sleep(5)

        # Retrieve results
        print()
        retrieved = runner.retrieve_test_results('test_f')

        if retrieved:
            print()
            print("SUCCESS: Test F completed and results retrieved")

            # Show what we got
            import os
            result_file = Path("validation/mt5/test_f_margin.csv")
            if result_file.exists():
                size = os.path.getsize(result_file)
                print(f"  Result file: {result_file} ({size} bytes)")

            summary_file = Path("validation/mt5/test_f_summary.json")
            if summary_file.exists():
                size = os.path.getsize(summary_file)
                print(f"  Summary file: {summary_file} ({size} bytes)")
        else:
            print()
            print("WARNING: Results not retrieved")
    else:
        print()
        print("ERROR: Test execution failed")

    return success

if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)
