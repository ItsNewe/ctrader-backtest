"""
Fetch all broker-specific settings for  XAUUSD

Usage:
    python fetch_broker_settings.py [path_to_terminal64.exe]

If no path provided, will connect to any running MT5 terminal.
"""

import MetaTrader5 as mt5
import sys
import os

# Fix encoding for Windows console
if sys.platform == 'win32':
    sys.stdout.reconfigure(encoding='utf-8')

print("=" * 70)
print("BROKER MARKETS - BROKER SETTINGS FETCHER")
print("=" * 70)

# Initialize MT5
mt5_path = None
if len(sys.argv) > 1:
    mt5_path = sys.argv[1]
    if not os.path.exists(mt5_path):
        print(f"[ERROR] Specified path does not exist: {mt5_path}")
        exit(1)
    print(f"[INFO] Using specified terminal: {mt5_path}")
    if not mt5.initialize(path=mt5_path):
        print("[ERROR] Failed to initialize MT5 with specified path")
        print("   Make sure the terminal is running and accessible!")
        exit(1)
    print("[OK] MT5 initialized successfully")
else:
    # Try to connect to any running MT5 terminal
    print("[INFO] No path specified - connecting to running MT5 terminal...")
    if not mt5.initialize():
        print("[ERROR] Failed to initialize MT5")
        print("   Make sure a MetaTrader5 terminal is running!")
        print("   Or provide the path: python fetch_broker_settings.py <path_to_terminal64.exe>")
        exit(1)
    print("[OK] MT5 initialized successfully")

# Check which account is logged in
account_info = mt5.account_info()
if account_info is None:
    print("[ERROR] No account logged in")
    mt5.shutdown()
    exit(1)

print(f"\nLogged in account: {account_info.login} on {account_info.server}")
print(f"Expected: 000000 on BrokerServer")

if account_info.login != 000000 or "Broker" not in account_info.server:
    print("\n[WARNING] You may not be logged into  account!")
    print(f"Currently logged in: {account_info.login} on {account_info.server}")
    print("[INFO] Proceeding with current account...")

# Get symbol info for XAUUSD
symbol = "XAUUSD"
symbol_info = mt5.symbol_info(symbol)

if symbol_info is None:
    print(f"[ERROR] Failed to get symbol info for {symbol}")
    print(f"   Error: {mt5.last_error()}")
    mt5.shutdown()
    exit(1)

print(f"\n{'=' * 70}")
print(f"BROKER MARKETS - {symbol} SPECIFICATIONS")
print("=" * 70)

# Basic symbol info
print(f"\n[BASIC INFORMATION]")
print(f"Symbol:                   {symbol_info.name}")
print(f"Description:              {symbol_info.description}")
print(f"Currency Base:            {symbol_info.currency_base}")
print(f"Currency Profit:          {symbol_info.currency_profit}")
print(f"Currency Margin:          {symbol_info.currency_margin}")

# Contract specifications
print(f"\n[CONTRACT SPECIFICATIONS]")
print(f"Contract Size:            {symbol_info.trade_contract_size}")
print(f"Digits:                   {symbol_info.digits}")
print(f"Point:                    {symbol_info.point}")
print(f"Tick Size:                {symbol_info.trade_tick_size}")
print(f"Tick Value:               {symbol_info.trade_tick_value}")

# Trading restrictions
print(f"\n[TRADING RESTRICTIONS]")
print(f"Min Volume:               {symbol_info.volume_min}")
print(f"Max Volume:               {symbol_info.volume_max}")
print(f"Volume Step:              {symbol_info.volume_step}")

# Margin requirements
print(f"\n[MARGIN REQUIREMENTS]")
calc_mode = symbol_info.trade_calc_mode
calc_mode_names = {
    0: "FOREX",
    1: "FUTURES",
    2: "CFD",
    3: "CFDINDEX",
    4: "CFDLEVERAGE",
    5: "FOREX_NO_LEVERAGE"
}
calc_mode_name = calc_mode_names.get(calc_mode, f"UNKNOWN ({calc_mode})")
print(f"Calculation Mode:         {calc_mode} ({calc_mode_name})")

# Get margin rates
initial_margin = 0.0
maintenance_margin = 0.0
if hasattr(mt5, 'symbol_info_margin_rate'):
    rates = mt5.symbol_info_margin_rate(symbol, mt5.ORDER_TYPE_BUY)
    if rates:
        initial_margin, maintenance_margin = rates

print(f"Initial Margin Rate:      {initial_margin}")
print(f"Maintenance Margin Rate:  {maintenance_margin}")

# Calculate sample margin
current_price = symbol_info.ask
leverage = account_info.leverage
print(f"\n[SAMPLE MARGIN CALCULATION]")
print(f"Current Price:            ${current_price:.2f}")
print(f"Account Leverage:         1:{leverage}")

# Calculate margin for 1.00 lot
if calc_mode == 0:  # FOREX
    margin_1_lot = symbol_info.trade_contract_size / leverage * initial_margin if initial_margin else symbol_info.trade_contract_size / leverage
elif calc_mode == 4:  # CFDLEVERAGE
    margin_1_lot = symbol_info.trade_contract_size * current_price / leverage * initial_margin if initial_margin else symbol_info.trade_contract_size * current_price / leverage
else:
    margin_1_lot = 0
    print(f"[WARNING] Unsupported calc mode for margin calculation")

print(f"Margin for 1.00 lot:      ${margin_1_lot:.2f}")
print(f"Margin for 0.01 lot:      ${margin_1_lot * 0.01:.2f}")

# Swap information
print(f"\n[SWAP/ROLLOVER FEES]")
swap_long = symbol_info.swap_long
swap_short = symbol_info.swap_short
swap_mode = symbol_info.swap_mode
swap_3days = symbol_info.swap_rollover3days  # Day for triple swap

swap_mode_names = {
    0: "DISABLED",
    1: "POINTS",
    2: "CURRENCY_SYMBOL",
    3: "CURRENCY_MARGIN",
    4: "CURRENCY_DEPOSIT",
    5: "INTEREST_CURRENT",
    6: "INTEREST_OPEN",
    7: "REOPEN_CURRENT",
    8: "REOPEN_BID"
}
swap_mode_name = swap_mode_names.get(swap_mode, f"UNKNOWN ({swap_mode})")

day_names = {0: "Sunday", 1: "Monday", 2: "Tuesday", 3: "Wednesday", 4: "Thursday", 5: "Friday", 6: "Saturday"}
swap_3days_name = day_names.get(swap_3days, f"UNKNOWN ({swap_3days})")

print(f"Swap Long:                {swap_long}")
print(f"Swap Short:               {swap_short}")
print(f"Swap Mode:                {swap_mode} ({swap_mode_name})")
print(f"Triple Swap Day:          {swap_3days} ({swap_3days_name})")

# Calculate swap in USD for POINTS mode
if swap_mode == 1:  # POINTS
    swap_usd_1_lot = swap_long * symbol_info.point * symbol_info.trade_contract_size
    print(f"\nSwap in USD (1.00 lot):   ${swap_usd_1_lot:.2f} per day")
    print(f"Swap in USD (0.01 lot):   ${swap_usd_1_lot * 0.01:.2f} per day")
    print(f"Triple swap (Wednesday):  ${swap_usd_1_lot * 3:.2f} for 1.00 lot")

# Spread information
print(f"\n[SPREAD]")
spread_points = symbol_info.spread
spread_dollars = spread_points * symbol_info.point
print(f"Current Spread:           {spread_points} points (${spread_dollars:.2f})")

# Write to file
print(f"\n{'=' * 70}")
print("[OK] Writing settings to file...")
print("=" * 70)

with open("validation/Broker/XAUUSD_SETTINGS.txt", "w") as f:
    f.write("=" * 70 + "\n")
    f.write("BROKER MARKETS - XAUUSD SPECIFICATIONS\n")
    f.write(f"Fetched: {account_info.server} - Account {account_info.login}\n")
    f.write("=" * 70 + "\n\n")

    f.write("[BASIC INFORMATION]\n")
    f.write(f"Symbol:                   {symbol_info.name}\n")
    f.write(f"Description:              {symbol_info.description}\n")
    f.write(f"Contract Size:            {symbol_info.trade_contract_size}\n")
    f.write(f"Digits:                   {symbol_info.digits}\n")
    f.write(f"Point:                    {symbol_info.point}\n\n")

    f.write("[MARGIN]\n")
    f.write(f"Calculation Mode:         {calc_mode} ({calc_mode_name})\n")
    f.write(f"Initial Margin Rate:      {initial_margin}\n")
    f.write(f"Leverage:                 1:{leverage}\n")
    f.write(f"Margin for 1.00 lot:      ${margin_1_lot:.2f}\n\n")

    f.write("[SWAP RATES]\n")
    f.write(f"Swap Long:                {swap_long}\n")
    f.write(f"Swap Short:               {swap_short}\n")
    f.write(f"Swap Mode:                {swap_mode} ({swap_mode_name})\n")
    f.write(f"Triple Swap Day:          {swap_3days} ({swap_3days_name})\n")
    if swap_mode == 1:
        f.write(f"Swap in USD (1.00 lot):   ${swap_usd_1_lot:.2f} per day\n")
    f.write("\n")

    f.write("=" * 70 + "\n")
    f.write("FOR C++ CONFIGURATION\n")
    f.write("=" * 70 + "\n")
    f.write(f"config.contract_size = {symbol_info.trade_contract_size};\n")
    f.write(f"config.leverage = {leverage};\n")
    f.write(f"config.margin_rate = {initial_margin if initial_margin else 1.0};\n")
    f.write(f"config.swap_long = {swap_long};\n")
    f.write(f"config.swap_short = {swap_short};\n")
    f.write(f"config.swap_mode = {swap_mode};  // {swap_mode_name}\n")
    f.write(f"config.swap_3days = {swap_3days};  // Triple swap on {swap_3days_name}\n")

print("[OK] Settings written to: validation/Broker/XAUUSD_SETTINGS.txt")

mt5.shutdown()
print(f"\n{'=' * 70}")
print("DONE")
print("=" * 70)
