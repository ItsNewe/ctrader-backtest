"""
Run All Validation Tests (B-F)
Automates the complete test suite
"""

import MetaTrader5 as mt5
import sys
from pathlib import Path
from datetime import datetime, timedelta
import shutil

class ValidationTestRunner:
    """Runs all MT5 validation tests"""

    def __init__(self):
        self.mt5_initialized = False
        self.tester_files_dir = None

    def initialize(self):
        """Initialize MT5 connection"""
        print("=" * 70)
        print("MT5 VALIDATION TEST SUITE")
        print("=" * 70)
        print()

        if not mt5.initialize():
            print("Failed to initialize MT5:", mt5.last_error())
            return False

        self.mt5_initialized = True

        # Determine tester files directory
        terminal_info = mt5.terminal_info()
        data_path = Path(terminal_info.data_path)

        # Look for tester directory
        tester_base = data_path.parent / "Tester" / data_path.name
        if tester_base.exists():
            # Find agent directory
            agent_dirs = list(tester_base.glob("Agent-*/MQL5/Files"))
            if agent_dirs:
                self.tester_files_dir = agent_dirs[0]
                print(f"Found tester files: {self.tester_files_dir}")

        account = mt5.account_info()
        print(f"Connected: Account {account.login} on {account.server}")
        print()

        return True

    def export_price_data(self, symbol="EURUSD", days=30):
        """Export historical price data"""
        print(f"Exporting {symbol} data...")

        end_date = datetime.now()
        start_date = end_date - timedelta(days=days)

        rates = mt5.copy_rates_range(symbol, mt5.TIMEFRAME_H1, start_date, end_date)

        if rates is None or len(rates) == 0:
            print(f"  Failed to get rates")
            return False

        output_file = Path("validation/configs/price_data.csv")
        output_file.parent.mkdir(parents=True, exist_ok=True)

        with open(output_file, 'w') as f:
            f.write("time,open,high,low,close,volume,tick_volume\n")
            for rate in rates:
                f.write(f"{rate['time']},{rate['open']},{rate['high']},{rate['low']},")
                f.write(f"{rate['close']},{rate['tick_volume']},{rate['tick_volume']}\n")

        print(f"  Exported {len(rates)} bars to {output_file}")
        return True

    def retrieve_test_results(self, test_name):
        """Retrieve test results from MT5 tester directory"""
        if not self.tester_files_dir:
            print(f"  Warning: Tester directory not found")
            return False

        # Look for result files
        result_files = {
            'b': 'test_b_ticks.csv',
            'c': 'test_c_slippage.csv',
            'd': 'test_d_spread.csv',
            'e': 'test_e_swap_timing.csv',
            'f': 'test_f_margin.csv'
        }

        if test_name not in result_files:
            print(f"  Unknown test: {test_name}")
            return False

        source_file = self.tester_files_dir / result_files[test_name]
        dest_file = Path(f"validation/mt5/test_{test_name}_result.csv")

        if not source_file.exists():
            print(f"  Result file not found: {source_file}")
            print(f"  Make sure MT5 test has been run")
            return False

        dest_file.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source_file, dest_file)

        print(f"  Retrieved: {dest_file}")

        # Also copy summary if exists
        summary_file = self.tester_files_dir / f"test_{test_name}_summary.json"
        if summary_file.exists():
            summary_dest = Path(f"validation/mt5/test_{test_name}_summary.json")
            shutil.copy2(summary_file, summary_dest)
            print(f"  Retrieved: {summary_dest}")

        return True

    def print_test_instructions(self, test_name, test_description):
        """Print instructions for running test in MT5"""
        print()
        print("=" * 70)
        print(f"TEST {test_name.upper()}: {test_description}")
        print("=" * 70)
        print()
        print("Please run this test in MT5 Strategy Tester:")
        print()
        print("1. Open MT5 Strategy Tester (Ctrl+R)")
        print(f"2. Select EA: test_{test_name}_{test_description.lower().replace(' ', '_')}")
        print("3. Symbol: EURUSD")
        print("4. Period: H1")
        print("5. Mode: Every tick based on real ticks")
        print("6. Click Start")
        print("7. Wait for completion")
        print()
        print("When complete, press Enter here to continue...")
        input()

    def run_test_suite(self):
        """Run complete test suite"""

        tests = [
            ('b', 'Tick Synthesis'),
            ('c', 'Slippage Distribution'),
            ('d', 'Spread Widening'),
            ('e', 'Swap Timing'),
            ('f', 'Margin Calculation')
        ]

        results = {}

        # Export price data once
        if not self.export_price_data():
            print("Failed to export price data")
            return False

        for test_id, test_name in tests:
            print()
            print("-" * 70)
            print(f"PROCESSING TEST {test_id.upper()}")
            print("-" * 70)

            # Prompt user to run MT5 test
            self.print_test_instructions(test_id, test_name)

            # Retrieve results
            success = self.retrieve_test_results(test_id)
            results[test_id] = success

            if success:
                print(f"  Test {test_id.upper()} results retrieved successfully")
            else:
                print(f"  Test {test_id.upper()} results NOT retrieved")

        # Summary
        print()
        print("=" * 70)
        print("TEST SUITE COMPLETE")
        print("=" * 70)
        print()
        print("Results:")
        for test_id, test_name in tests:
            status = "PASS" if results.get(test_id) else "FAIL"
            print(f"  Test {test_id.upper()} ({test_name}): {status}")

        print()
        print("Next step: Analyze results and update engine based on findings")

        return True

    def shutdown(self):
        """Shutdown MT5"""
        if self.mt5_initialized:
            mt5.shutdown()

def main():
    runner = ValidationTestRunner()

    try:
        if not runner.initialize():
            print("Failed to initialize")
            return False

        runner.run_test_suite()

    finally:
        runner.shutdown()

    return True

if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)
