"""
Check MT5 status and help diagnose why result file wasn't created
"""

import MetaTrader5 as mt5
from pathlib import Path

print("=" * 70)
print("MT5 DIAGNOSTIC CHECK")
print("=" * 70)
print()

# Initialize
if not mt5.initialize():
    print("Failed to initialize MT5")
    exit(1)

print("MT5 Connected Successfully")
print()

# Check terminal info
terminal = mt5.terminal_info()
print("Terminal Information:")
print(f"  Path: {terminal.path}")
print(f"  Data Path: {terminal.data_path}")
print(f"  Build: {terminal.build}")
print()

# Check Files directory
files_dir = Path(terminal.data_path) / "MQL5" / "Files"
print(f"Files Directory: {files_dir}")
print(f"  Exists: {files_dir.exists()}")

if files_dir.exists():
    files = list(files_dir.glob("*"))
    print(f"  File count: {len(files)}")
    if files:
        print("  Files found:")
        for f in files[:10]:  # Show first 10
            print(f"    - {f.name} ({f.stat().st_size} bytes)")
    else:
        print("  Directory is empty")
else:
    print("  ERROR: Directory does not exist!")

print()

# Check Experts directory
experts_dir = Path(terminal.data_path) / "MQL5" / "Experts"
print(f"Experts Directory: {experts_dir}")
if experts_dir.exists():
    ea_file = experts_dir / "test_a_sl_tp_order.mq5"
    ex_file = experts_dir / "test_a_sl_tp_order.ex5"

    print(f"  test_a_sl_tp_order.mq5 exists: {ea_file.exists()}")
    print(f"  test_a_sl_tp_order.ex5 exists: {ex_file.exists()}")

    if not ex_file.exists():
        print()
        print("  WARNING: .ex5 file not found!")
        print("  This means EA hasn't been compiled yet.")
        print("  Action: Compile the EA in MetaEditor (F7)")

print()

# Check account
account = mt5.account_info()
if account:
    print("Account Information:")
    print(f"  Login: {account.login}")
    print(f"  Server: {account.server}")
    print(f"  Balance: {account.balance} {account.currency}")
    print(f"  Leverage: 1:{account.leverage}")

print()

# Check if there are any positions or history
print("Checking Trading Activity:")
positions = mt5.positions_total()
print(f"  Open positions: {positions}")

# Check history (last hour)
from datetime import datetime, timedelta
deals_total = mt5.history_deals_total(
    datetime.now() - timedelta(hours=1),
    datetime.now()
)
print(f"  Recent deals (last hour): {deals_total}")

if deals_total > 0:
    print("  Recent deals found - test may have run!")
else:
    print("  No recent deals - test probably hasn't run yet")

print()
print("=" * 70)
print("NEXT STEPS:")
print("=" * 70)

ex_file_path = Path(terminal.data_path) / "MQL5" / "Experts" / "test_a_sl_tp_order.ex5"
if not ex_file_path.exists():
    print("1. Compile the EA:")
    print("   - Open MetaEditor (F4)")
    print("   - Find test_a_sl_tp_order.mq5")
    print("   - Press F7 to compile")
    print("   - Check for 0 errors")
else:
    print("1. EA is compiled - ready to test")

print()
print("2. Run in Strategy Tester:")
print("   - Press Ctrl+R in MT5")
print("   - Select test_a_sl_tp_order")
print("   - Symbol: EURUSD, Period: H1")
print("   - Mode: Every tick based on real ticks")
print("   - Click Start")
print()
print("3. Check Journal tab for:")
print("   - 'Position opened' message")
print("   - 'POSITION CLOSED' message")
print("   - 'TEST A COMPLETE' message")
print("   - 'RESULTS EXPORTED TO' message")

mt5.shutdown()
