"""
Step 1: Export MT5 Data (Non-interactive)
Just exports the data and checks for MT5 results - no prompts
"""

import MetaTrader5 as mt5
import sys
from datetime import datetime, timedelta
from pathlib import Path

def export_data():
    """Export EURUSD H1 data from MT5"""

    print("=" * 70)
    print("STEP 1: MT5 DATA EXPORT")
    print("=" * 70)
    print()

    # Initialize MT5
    print("Initializing MT5...")
    if not mt5.initialize():
        error = mt5.last_error()
        print(f"Failed to initialize MT5: {error}")
        print()
        print("Make sure:")
        print("  1. MT5 terminal is running")
        print("  2. Logged into account 000000")
        return False

    print("MT5 initialized successfully")

    # Get account info
    account = mt5.account_info()
    if account:
        print(f"  Account: {account.login}")
        print(f"  Server: {account.server}")
        print(f"  Balance: {account.balance} {account.currency}")

    # Set date range (last 30 days)
    end_date = datetime.now()
    start_date = end_date - timedelta(days=30)

    print()
    print(f"Exporting EURUSD H1 data...")
    print(f"  From: {start_date.strftime('%Y-%m-%d')}")
    print(f"  To:   {end_date.strftime('%Y-%m-%d')}")

    # Get rates
    rates = mt5.copy_rates_range("EURUSD", mt5.TIMEFRAME_H1, start_date, end_date)

    if rates is None or len(rates) == 0:
        print(f"Failed to get rates")
        print(f"  Error: {mt5.last_error()}")
        mt5.shutdown()
        return False

    print(f"  Retrieved: {len(rates)} bars")

    # Create directory
    output_file = Path("validation/configs/test_a_data.csv")
    output_file.parent.mkdir(parents=True, exist_ok=True)

    # Write CSV
    with open(output_file, 'w') as f:
        f.write("time,open,high,low,close,volume,tick_volume\n")
        for rate in rates:
            f.write(f"{rate['time']},{rate['open']},{rate['high']},{rate['low']},")
            f.write(f"{rate['close']},{rate['tick_volume']},{rate['tick_volume']}\n")

    print(f"  Saved to: {output_file}")
    print(f"  Size: {output_file.stat().st_size:,} bytes")

    # Check for MT5 result file
    print()
    print("Checking for MT5 test results...")

    data_path = mt5.terminal_info().data_path
    files_dir = Path(data_path) / "MQL5" / "Files"
    result_file = files_dir / "test_a_mt5_result.csv"

    if result_file.exists():
        print(f"  Found: {result_file}")

        # Copy to validation directory
        dest = Path("validation/mt5/test_a_mt5_result.csv")
        dest.parent.mkdir(parents=True, exist_ok=True)

        import shutil
        shutil.copy2(result_file, dest)
        print(f"  Copied to: {dest}")
        print()
        print("MT5 test results found and copied!")
    else:
        print(f"  Not found: {result_file}")
        print()
        print("MT5 test not run yet. To run:")
        print("  1. Open MT5 Strategy Tester (Ctrl+R)")
        print("  2. Select EA: test_a_sl_tp_order")
        print("  3. Symbol: EURUSD, Period: H1")
        print("  4. Mode: Every tick based on real ticks")
        print("  5. Click Start")
        print("  6. Re-run this script to copy results")

    mt5.shutdown()
    return True

if __name__ == "__main__":
    try:
        success = export_data()
        sys.exit(0 if success else 1)
    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
