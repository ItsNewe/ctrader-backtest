"""
Fetch XAUUSD swap rates from MetaTrader 5
"""

import MetaTrader5 as mt5
import sys

# Fix encoding for Windows console
if sys.platform == 'win32':
    sys.stdout.reconfigure(encoding='utf-8')

print("=" * 70)
print("XAUUSD SWAP RATE FETCHER")
print("=" * 70)

# Initialize MT5
if not mt5.initialize():
    print("[ERROR] Failed to initialize MT5")
    print("   Make sure MetaTrader5 terminal is running!")
    exit(1)

print("[OK] MT5 initialized successfully")

# Get symbol info for XAUUSD
symbol = "XAUUSD"
symbol_info = mt5.symbol_info(symbol)

if symbol_info is None:
    print(f"[ERROR] Failed to get symbol info for {symbol}")
    print(f"   Error: {mt5.last_error()}")
    mt5.shutdown()
    exit(1)

print(f"\n=== {symbol} SWAP INFORMATION ===")
print("-" * 70)

# Extract swap information
swap_long = symbol_info.swap_long
swap_short = symbol_info.swap_short
swap_mode = symbol_info.swap_mode
swap_3days = symbol_info.swap_rollover3days

print(f"Swap Long (per lot/day):  {swap_long}")
print(f"Swap Short (per lot/day): {swap_short}")
print(f"Swap Mode:                {swap_mode}")
print(f"Triple Swap Day:          {swap_3days} (0=Sun, 1=Mon, 2=Tue, 3=Wed, 4=Thu, 5=Fri, 6=Sat)")

# Decode swap mode
swap_mode_names = {
    0: "SYMBOL_SWAP_MODE_DISABLED",
    1: "SYMBOL_SWAP_MODE_POINTS",
    2: "SYMBOL_SWAP_MODE_CURRENCY_SYMBOL",
    3: "SYMBOL_SWAP_MODE_CURRENCY_MARGIN",
    4: "SYMBOL_SWAP_MODE_CURRENCY_DEPOSIT",
    5: "SYMBOL_SWAP_MODE_INTEREST_CURRENT",
    6: "SYMBOL_SWAP_MODE_INTEREST_OPEN",
    7: "SYMBOL_SWAP_MODE_REOPEN_CURRENT",
    8: "SYMBOL_SWAP_MODE_REOPEN_BID"
}

swap_mode_name = swap_mode_names.get(swap_mode, "UNKNOWN")
print(f"Swap Mode Name:           {swap_mode_name}")

# Additional useful info
print(f"\nAdditional Symbol Info:")
print(f"Contract Size:            {symbol_info.trade_contract_size}")
print(f"Digits:                   {symbol_info.digits}")
print(f"Point:                    {symbol_info.point}")

# Calculate example swap for common lot sizes
print(f"\n=== SWAP COST EXAMPLES (per day) ===")
print("-" * 70)
lot_sizes = [0.01, 0.1, 1.0, 10.0]

for lots in lot_sizes:
    swap_cost = swap_long * lots
    print(f"  {lots:5.2f} lots: ${swap_cost:8.2f}")

# Calculate total swap for a year if holding constant position
print(f"\n=== ANNUAL SWAP IMPACT ===")
print("-" * 70)
annual_swap_1_lot = swap_long * 365
print(f"Holding 1.00 lot for 1 year: ${annual_swap_1_lot:.2f}")
print(f"Holding 0.10 lot for 1 year: ${annual_swap_1_lot * 0.1:.2f}")
print(f"Holding 0.01 lot for 1 year: ${annual_swap_1_lot * 0.01:.2f}")

# For the fill_up strategy
print(f"\n=== ESTIMATED IMPACT ON FILL_UP STRATEGY ===")
print("-" * 70)
print(f"Strategy characteristics:")
print(f"  - Grid of long positions held for days/weeks")
print(f"  - Average position size: ~0.02-0.05 lots")
print(f"  - Multiple positions open simultaneously")
print(f"\nEstimated annual swap cost:")
avg_lots = 0.5  # Conservative estimate of average total lots open
days = 365
estimated_annual_swap = swap_long * avg_lots * days
print(f"  With avg {avg_lots} total lots open: ${estimated_annual_swap:.2f}")

print("\n" + "=" * 70)
print("[OK] Swap information retrieved successfully")
print("=" * 70)

# Write to file for C++ config
with open("validation/XAUUSD_SWAP_RATES.txt", "w") as f:
    f.write(f"=== XAUUSD Swap Rates ===\n")
    f.write(f"Fetched: {mt5.account_info().server} - Account {mt5.account_info().login}\n\n")
    f.write(f"Swap Long:  {swap_long}\n")
    f.write(f"Swap Short: {swap_short}\n")
    f.write(f"Swap Mode:  {swap_mode} ({swap_mode_name})\n\n")
    f.write(f"=== For C++ Configuration ===\n")
    f.write(f"config.swap_long = {swap_long};  // USD per lot per day\n")
    f.write(f"config.swap_short = {swap_short};\n")
    f.write(f"config.swap_mode = {swap_mode};  // {swap_mode_name}\n")
    f.write(f"config.swap_3days = {swap_3days};  // Triple swap day (0=Sun, 3=Wed, etc)\n")

print(f"\n[OK] Swap rates written to: validation/XAUUSD_SWAP_RATES.txt")

mt5.shutdown()
