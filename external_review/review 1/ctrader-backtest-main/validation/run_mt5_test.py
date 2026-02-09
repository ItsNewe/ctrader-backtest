"""
Automated MT5 Strategy Tester Runner
Automates running validation tests in MT5 Strategy Tester
"""

import MetaTrader5 as mt5
import time
import os
import sys
import shutil
from datetime import datetime, timedelta
from pathlib import Path

class MT5TesterAutomation:
    """Automates MT5 Strategy Tester for validation tests"""

    def __init__(self):
        self.mt5_initialized = False
        self.terminal_path = None
        self.data_path = None

    def initialize_mt5(self, path=None):
        """Initialize MT5 connection"""
        print("=" * 70)
        print("MT5 STRATEGY TESTER AUTOMATION")
        print("=" * 70)
        print()

        # Initialize MT5
        if path:
            if not mt5.initialize(path):
                print(f"❌ Failed to initialize MT5 with path: {path}")
                print(f"   Error: {mt5.last_error()}")
                return False
        else:
            if not mt5.initialize():
                print(f"❌ Failed to initialize MT5")
                print(f"   Error: {mt5.last_error()}")
                print()
                print("Make sure:")
                print("  1. MT5 is installed")
                print("  2. MT5 terminal is running")
                print("  3. You're logged into account 000000")
                return False

        self.mt5_initialized = True

        # Get terminal info
        terminal_info = mt5.terminal_info()
        if terminal_info:
            self.terminal_path = terminal_info.path
            self.data_path = terminal_info.data_path
            print(f"✓ MT5 initialized successfully")
            print(f"  Terminal path: {self.terminal_path}")
            print(f"  Data path: {self.data_path}")
        else:
            print("⚠ MT5 initialized but cannot get terminal info")

        # Check account
        account = mt5.account_info()
        if account:
            print(f"  Account: {account.login}")
            print(f"  Server: {account.server}")
            print(f"  Balance: {account.balance} {account.currency}")

            if str(account.login) != "000000":
                print()
                print(f"⚠ WARNING: Expected account 000000, got {account.login}")
                print("  Tests may use wrong broker settings")
        else:
            print("⚠ Cannot get account info")

        print()
        return True

    def shutdown_mt5(self):
        """Shutdown MT5 connection"""
        if self.mt5_initialized:
            mt5.shutdown()
            print("✓ MT5 shutdown complete")

    def export_historical_data(self, symbol="EURUSD", timeframe=mt5.TIMEFRAME_H1,
                               start_date=None, end_date=None,
                               output_file="validation/configs/test_a_data.csv"):
        """Export historical price data to CSV"""
        print("-" * 70)
        print("EXPORTING HISTORICAL DATA")
        print("-" * 70)

        # Default date range: last 30 days
        if end_date is None:
            end_date = datetime.now()
        if start_date is None:
            start_date = end_date - timedelta(days=30)

        print(f"Symbol: {symbol}")
        print(f"Timeframe: {self._timeframe_to_string(timeframe)}")
        print(f"Start: {start_date.strftime('%Y-%m-%d')}")
        print(f"End: {end_date.strftime('%Y-%m-%d')}")
        print()

        # Request rates
        rates = mt5.copy_rates_range(symbol, timeframe, start_date, end_date)

        if rates is None or len(rates) == 0:
            print(f"❌ Failed to get rates for {symbol}")
            print(f"   Error: {mt5.last_error()}")
            return False

        print(f"✓ Retrieved {len(rates)} bars")

        # Create directory if needed
        output_path = Path(output_file)
        output_path.parent.mkdir(parents=True, exist_ok=True)

        # Write to CSV
        try:
            with open(output_file, 'w') as f:
                # Header
                f.write("time,open,high,low,close,volume,tick_volume\n")

                # Data
                for rate in rates:
                    f.write(f"{rate['time']},{rate['open']},{rate['high']},{rate['low']},")
                    f.write(f"{rate['close']},{rate['tick_volume']},{rate['tick_volume']}\n")

            print(f"✓ Data exported to: {output_file}")
            print(f"  File size: {output_path.stat().st_size} bytes")
            print()
            return True

        except Exception as e:
            print(f"❌ Failed to write CSV: {e}")
            return False

    def _timeframe_to_string(self, timeframe):
        """Convert MT5 timeframe constant to string"""
        timeframes = {
            mt5.TIMEFRAME_M1: "M1",
            mt5.TIMEFRAME_M5: "M5",
            mt5.TIMEFRAME_M15: "M15",
            mt5.TIMEFRAME_M30: "M30",
            mt5.TIMEFRAME_H1: "H1",
            mt5.TIMEFRAME_H4: "H4",
            mt5.TIMEFRAME_D1: "D1",
            mt5.TIMEFRAME_W1: "W1",
            mt5.TIMEFRAME_MN1: "MN1"
        }
        return timeframes.get(timeframe, f"Unknown({timeframe})")

    def compile_ea(self, ea_file):
        """
        Compile an MQL5 EA
        Note: This requires the EA to be in the correct MQL5 directory
        """
        print("-" * 70)
        print("COMPILING EA")
        print("-" * 70)

        if not self.data_path:
            print("❌ MT5 data path unknown - cannot compile")
            return False

        # Determine MQL5 directory
        mql5_dir = Path(self.data_path) / "MQL5"
        experts_dir = mql5_dir / "Experts"

        print(f"MQL5 Directory: {mql5_dir}")
        print(f"EA File: {ea_file}")

        # Check if EA exists in MQL5 directory
        ea_path = Path(ea_file)
        if not ea_path.is_absolute():
            # Relative path - assume it's in Experts
            ea_full_path = experts_dir / ea_path
        else:
            ea_full_path = ea_path

        if not ea_full_path.exists():
            print(f"⚠ EA not found in MQL5 directory: {ea_full_path}")
            print()
            print("Manual step required:")
            print(f"  1. Copy {ea_file}")
            print(f"     to {experts_dir}")
            print(f"  2. Open MetaEditor")
            print(f"  3. Compile the EA (F7)")
            return False

        print(f"✓ EA found: {ea_full_path}")
        print()
        print("⚠ Automatic compilation not fully supported via Python")
        print("  Please compile manually in MetaEditor (F7)")
        print()
        return True

    def find_result_file(self, filename="test_a_mt5_result.csv"):
        """Find result file in MT5 Files directory"""
        if not self.data_path:
            print("❌ MT5 data path unknown")
            return None

        files_dir = Path(self.data_path) / "MQL5" / "Files"
        result_file = files_dir / filename

        if result_file.exists():
            return str(result_file)
        else:
            return None

    def copy_result_file(self, source_filename="test_a_mt5_result.csv",
                        dest_path="validation/mt5/test_a_mt5_result.csv"):
        """Copy result file from MT5 Files directory to validation directory"""
        print("-" * 70)
        print("RETRIEVING RESULT FILE")
        print("-" * 70)

        source_file = self.find_result_file(source_filename)

        if not source_file:
            print(f"❌ Result file not found: {source_filename}")
            print()
            if self.data_path:
                files_dir = Path(self.data_path) / "MQL5" / "Files"
                print(f"Expected location: {files_dir / source_filename}")
                print()
                print("This means either:")
                print("  1. MT5 test hasn't been run yet")
                print("  2. Test didn't complete successfully")
                print("  3. EA didn't write the file")
            return False

        print(f"✓ Found result file: {source_file}")

        # Create destination directory
        dest_path_obj = Path(dest_path)
        dest_path_obj.parent.mkdir(parents=True, exist_ok=True)

        # Copy file
        try:
            shutil.copy2(source_file, dest_path)
            print(f"✓ Copied to: {dest_path}")
            print()
            return True
        except Exception as e:
            print(f"❌ Failed to copy file: {e}")
            return False

    def print_instructions_for_manual_test(self, ea_name="test_a_sl_tp_order"):
        """Print instructions for manually running the test in MT5"""
        print()
        print("=" * 70)
        print("MANUAL TEST EXECUTION REQUIRED")
        print("=" * 70)
        print()
        print("Python cannot directly control MT5 Strategy Tester.")
        print("Please follow these steps:")
        print()
        print("1. OPEN MT5 STRATEGY TESTER")
        print("   - Press Ctrl+R in MT5")
        print()
        print("2. CONFIGURE SETTINGS:")
        print(f"   - Expert Advisor: {ea_name}")
        print("   - Symbol: EURUSD")
        print("   - Period: H1")
        print("   - Date: Last month (or any volatile period)")
        print("   - Execution: Every tick based on real ticks")
        print("   - Deposit: 10000")
        print("   - Leverage: 1:100")
        print()
        print("3. CLICK START")
        print("   - Wait for test to complete")
        print("   - Check Journal tab for results")
        print()
        print("4. WHEN COMPLETE:")
        print("   - Press Enter here to continue")
        print("   - This script will automatically retrieve the results")
        print()
        print("=" * 70)
        input("Press Enter after running the MT5 test...")
        print()


def run_test_a_automated():
    """Run Test A with maximum automation"""
    automation = MT5TesterAutomation()

    # Step 1: Initialize MT5
    if not automation.initialize_mt5():
        print()
        print("FALLBACK: Manual execution required")
        print("Follow instructions in validation/TEST_A_INSTRUCTIONS.md")
        return False

    try:
        # Step 2: Export historical data
        print()
        success = automation.export_historical_data(
            symbol="EURUSD",
            timeframe=mt5.TIMEFRAME_H1,
            output_file="validation/configs/test_a_data.csv"
        )

        if not success:
            print("⚠ Data export failed - may need manual export")

        # Step 3: Check if EA is ready
        print()
        automation.compile_ea("validation/micro_tests/test_a_sl_tp_order.mq5")

        # Step 4: Instruct user to run MT5 test
        automation.print_instructions_for_manual_test("test_a_sl_tp_order")

        # Step 5: Retrieve results
        success = automation.copy_result_file(
            source_filename="test_a_mt5_result.csv",
            dest_path="validation/mt5/test_a_mt5_result.csv"
        )

        if success:
            print("=" * 70)
            print("✓✓✓ MT5 TEST RESULTS RETRIEVED ✓✓✓")
            print("=" * 70)
            print()
            print("Next steps:")
            print("  1. Build our backtest: cmake --build build --target test_a_backtest")
            print("  2. Run our test: build/validation/test_a_backtest")
            print("  3. Compare results: python validation/compare_test_a.py")
            print()
            return True
        else:
            print("❌ Could not retrieve results")
            print("   Check if MT5 test completed successfully")
            return False

    finally:
        automation.shutdown_mt5()


if __name__ == "__main__":
    print("=" * 70)
    print("TEST A: AUTOMATED MT5 EXECUTION")
    print("=" * 70)
    print()
    print("This script will:")
    print("  1. Connect to MT5")
    print("  2. Export EURUSD H1 price data")
    print("  3. Guide you through running Strategy Tester")
    print("  4. Retrieve and copy result files")
    print()
    print("Note: MT5 Strategy Tester must be run manually")
    print("      (Python cannot fully automate the GUI)")
    print()

    response = input("Continue? (y/n): ")
    if response.lower() != 'y':
        print("Aborted.")
        sys.exit(0)

    success = run_test_a_automated()
    sys.exit(0 if success else 1)
