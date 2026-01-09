"""
Retrieve MT5 Test Results
Run this after completing a test in MT5 Strategy Tester
"""

import sys
import shutil
from pathlib import Path

def find_latest_result_files(tester_base, result_filenames):
    """Find the most recently modified result files across all Agent directories"""

    agent_dirs = list(tester_base.glob("Agent-*/MQL5/Files"))

    if not agent_dirs:
        print(f"ERROR: No Agent directories found in {tester_base}")
        return None

    print(f"Searching {len(agent_dirs)} Agent directories...")

    latest_files = {}

    for filename in result_filenames:
        newest_file = None
        newest_time = 0

        for agent_dir in agent_dirs:
            file_path = agent_dir / filename
            if file_path.exists():
                mtime = file_path.stat().st_mtime
                if mtime > newest_time:
                    newest_time = mtime
                    newest_file = file_path

        if newest_file:
            latest_files[filename] = newest_file
            print(f"  Found: {filename} in {newest_file.parent.parent.parent.name}")
        else:
            print(f"  NOT FOUND: {filename}")

    return latest_files if latest_files else None

def retrieve_test_results(test_name):
    """Retrieve results for a specific test"""

    # Tester directory
    tester_base = Path(r"C:\Users\user\AppData\Roaming\MetaQuotes\Tester\930119AA53207C8778B41171FBFFB46F")

    # Map test names to result files
    result_files = {
        'test_a': ['test_a_result.csv'],
        'test_b': ['test_b_ticks.csv', 'test_b_summary.json'],
        'test_c': ['test_c_slippage.csv', 'test_c_summary.json'],
        'test_d': ['test_d_spread.csv', 'test_d_summary.json'],
        'test_e': ['test_e_swap_timing.csv', 'test_e_summary.json'],
        'test_f': ['test_f_margin.csv', 'test_f_summary.json']
    }

    if test_name not in result_files:
        print(f"ERROR: Unknown test '{test_name}'")
        print(f"Valid tests: {', '.join(result_files.keys())}")
        return False

    print("=" * 70)
    print(f"RETRIEVING RESULTS: {test_name.upper()}")
    print("=" * 70)
    print()

    # Find latest result files
    filenames = result_files[test_name]
    latest_files = find_latest_result_files(tester_base, filenames)

    if not latest_files:
        print()
        print("ERROR: No result files found")
        print()
        print("Make sure you have:")
        print("  1. Run the test in MT5 Strategy Tester")
        print("  2. Waited for the test to complete")
        print("  3. The EA successfully wrote the output files")
        return False

    # Copy files to validation/mt5 directory
    dest_dir = Path("validation/mt5")
    dest_dir.mkdir(parents=True, exist_ok=True)

    print()
    print("Copying results...")

    for filename, source_path in latest_files.items():
        dest_path = dest_dir / filename
        shutil.copy2(source_path, dest_path)

        size = dest_path.stat().st_size
        print(f"  [OK] {filename} ({size:,} bytes)")

    print()
    print("=" * 70)
    print("SUCCESS: Results retrieved")
    print("=" * 70)
    print(f"Results saved to: {dest_dir.absolute()}")

    return True

def main():
    if len(sys.argv) < 2:
        print("Usage: python retrieve_results.py <test_name>")
        print()
        print("Available tests:")
        print("  test_a - SL/TP execution order")
        print("  test_b - Tick synthesis")
        print("  test_c - Slippage distribution")
        print("  test_d - Spread widening")
        print("  test_e - Swap timing")
        print("  test_f - Margin calculation")
        print()
        print("Example: python retrieve_results.py test_f")
        return False

    test_name = sys.argv[1].lower()
    return retrieve_test_results(test_name)

if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)
