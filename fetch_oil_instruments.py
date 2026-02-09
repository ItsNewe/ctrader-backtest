"""
Fetch all Oil, Gas, and Energy instrument specifications from  MT5

Usage:
    python fetch_oil_instruments.py "C:\Program Files\ MT5 Terminal\terminal64.exe"
"""

import MetaTrader5 as mt5
import sys
import os

if sys.platform == 'win32':
    sys.stdout.reconfigure(encoding='utf-8')

print("=" * 80)
print("BROKER MARKETS - OIL / GAS / ENERGY INSTRUMENT SCANNER")
print("=" * 80)

# Initialize MT5
mt5_path = None
if len(sys.argv) > 1:
    mt5_path = sys.argv[1]
    if not os.path.exists(mt5_path):
        print(f"[ERROR] Path does not exist: {mt5_path}")
        exit(1)
    print(f"[INFO] Using terminal: {mt5_path}")
    if not mt5.initialize(path=mt5_path):
        print("[ERROR] Failed to initialize MT5")
        exit(1)
else:
    print("[INFO] Connecting to running MT5 terminal...")
    if not mt5.initialize():
        print("[ERROR] Failed to initialize MT5. Provide path as argument.")
        exit(1)

print("[OK] MT5 initialized")

account_info = mt5.account_info()
if account_info is None:
    print("[ERROR] No account logged in")
    mt5.shutdown()
    exit(1)

print(f"Account: {account_info.login} on {account_info.server}")
print(f"Leverage: 1:{account_info.leverage}")

# Search for all symbols containing oil/gas/energy keywords
keywords = ["OIL", "BRENT", "WTI", "CRUDE", "CL", "NGAS", "NATGAS", "GAS",
            "UKOIL", "USOIL", "XNG", "HEATING", "PETROL", "ENERGY"]

print(f"\n{'=' * 80}")
print("SEARCHING FOR OIL / GAS / ENERGY INSTRUMENTS...")
print("=" * 80)

# Get ALL symbols from the broker
all_symbols = mt5.symbols_get()
if all_symbols is None:
    print("[ERROR] Failed to get symbols")
    mt5.shutdown()
    exit(1)

print(f"Total symbols available: {len(all_symbols)}")

# Filter for oil/gas/energy instruments
found_symbols = []
for sym in all_symbols:
    name_upper = sym.name.upper()
    desc_upper = sym.description.upper() if sym.description else ""
    path_upper = sym.path.upper() if sym.path else ""

    for kw in keywords:
        if kw in name_upper or kw in desc_upper or kw in path_upper:
            found_symbols.append(sym)
            break

    # Also check path for "Energy" or "Commodities" category
    if "ENERGY" in path_upper or ("COMMODIT" in path_upper and ("OIL" in name_upper or "GAS" in name_upper or "CL" in name_upper)):
        if sym not in found_symbols:
            found_symbols.append(sym)

# Also search for common futures/CFD oil tickers
extra_tickers = ["XTIUSD", "XBRUSD", "XNGUSD", "USOUSD", "UKOUSD",
                 "CLF", "QMF", "BZF", "NGF", "HOF", "RBF",
                 "CL.F", "NG.F", "BRN", "CRUDEOIL"]
for ticker in extra_tickers:
    sym_info = mt5.symbol_info(ticker)
    if sym_info is not None and sym_info not in found_symbols:
        found_symbols.append(sym_info)

# Sort by name
found_symbols.sort(key=lambda s: s.name)

print(f"\nFound {len(found_symbols)} oil/gas/energy instruments:")
print("-" * 80)

# Lookup tables
swap_mode_names = {
    0: "DISABLED", 1: "POINTS", 2: "CURRENCY_SYMBOL", 3: "CURRENCY_MARGIN",
    4: "CURRENCY_DEPOSIT", 5: "INTEREST_CURRENT", 6: "INTEREST_OPEN",
    7: "REOPEN_CURRENT", 8: "REOPEN_BID"
}
calc_mode_names = {
    0: "FOREX", 1: "FUTURES", 2: "CFD", 3: "CFDINDEX",
    4: "CFDLEVERAGE", 5: "FOREX_NO_LEVERAGE"
}
day_names = {0: "Sun", 1: "Mon", 2: "Tue", 3: "Wed", 4: "Thu", 5: "Fri", 6: "Sat"}

# Collect detailed info
output_lines = []

for sym in found_symbols:
    print(f"\n{'=' * 80}")
    print(f"  {sym.name}  -  {sym.description}")
    print(f"  Path: {sym.path}")
    print(f"{'=' * 80}")

    print(f"  Contract Size:     {sym.trade_contract_size}")
    print(f"  Digits:            {sym.digits}")
    print(f"  Point:             {sym.point}")
    print(f"  Tick Size:         {sym.trade_tick_size}")
    print(f"  Tick Value:        {sym.trade_tick_value}")
    print(f"  Min Volume:        {sym.volume_min}")
    print(f"  Max Volume:        {sym.volume_max}")
    print(f"  Volume Step:       {sym.volume_step}")
    print(f"  Spread (current):  {sym.spread} pts (${sym.spread * sym.point:.4f})")

    calc_mode = sym.trade_calc_mode
    print(f"  Margin Mode:       {calc_mode} ({calc_mode_names.get(calc_mode, 'UNKNOWN')})")

    # Calculate margin for 1 lot
    price = sym.ask if sym.ask > 0 else sym.bid
    leverage = account_info.leverage
    if calc_mode == 4:  # CFDLEVERAGE
        margin_1lot = sym.trade_contract_size * price / leverage
    elif calc_mode == 0:  # FOREX
        margin_1lot = sym.trade_contract_size / leverage
    elif calc_mode == 2:  # CFD
        margin_1lot = sym.trade_contract_size * price
    elif calc_mode == 1:  # FUTURES
        margin_1lot = sym.margin_initial if sym.margin_initial > 0 else sym.trade_contract_size * price / leverage
    else:
        margin_1lot = 0

    print(f"  Current Price:     {price}")
    print(f"  Margin (1 lot):    ${margin_1lot:.2f}")
    print(f"  Margin (0.01 lot): ${margin_1lot * 0.01:.2f}")

    swap_mode = sym.swap_mode
    print(f"  Swap Mode:         {swap_mode} ({swap_mode_names.get(swap_mode, 'UNKNOWN')})")
    print(f"  Swap Long:         {sym.swap_long}")
    print(f"  Swap Short:        {sym.swap_short}")
    print(f"  Triple Swap Day:   {sym.swap_rollover3days} ({day_names.get(sym.swap_rollover3days, '?')})")

    # Swap cost in USD
    if swap_mode == 1 and price > 0:
        swap_usd = sym.swap_long * sym.point * sym.trade_contract_size
        print(f"  Swap Long $/lot:   ${swap_usd:.4f}/day")

    # Trading sessions
    print(f"  Trade Allowed:     {'Yes' if sym.trade_mode > 0 else 'NO'}")
    print(f"  Visible:           {'Yes' if sym.visible else 'No'}")
    print(f"  Spread Float:      {'Yes' if sym.spread_float else 'No'}")

    # Check if tradeable (can we actually use it?)
    tradeable = sym.trade_mode > 0 and price > 0
    category = "TRADEABLE" if tradeable else "NOT TRADEABLE"

    # Volatility hint: daily price range from recent bars
    daily_range_pct = 0
    try:
        rates = mt5.copy_rates_from_pos(sym.name, mt5.TIMEFRAME_D1, 0, 30)
        if rates is not None and len(rates) > 5:
            ranges = [(r['high'] - r['low']) / r['low'] * 100 for r in rates if r['low'] > 0]
            if ranges:
                daily_range_pct = sum(ranges) / len(ranges)
                print(f"  Avg Daily Range:   {daily_range_pct:.2f}% (last {len(ranges)} days)")
    except:
        pass

    # Strategy suitability assessment
    print(f"\n  --- STRATEGY SUITABILITY ---")
    # Grid strategies work best with:
    # 1. Mean-reverting behavior (oscillation)
    # 2. Reasonable spread relative to daily range
    # 3. Manageable swap costs
    # 4. Sufficient liquidity (tight spread)

    spread_pct = (sym.spread * sym.point / price * 100) if price > 0 else 999
    print(f"  Spread % of Price: {spread_pct:.4f}%")

    if tradeable:
        score = 0
        notes = []

        # Spread check
        if spread_pct < 0.05:
            score += 3
            notes.append("Excellent spread")
        elif spread_pct < 0.1:
            score += 2
            notes.append("Good spread")
        elif spread_pct < 0.3:
            score += 1
            notes.append("Moderate spread")
        else:
            notes.append("Wide spread WARNING")

        # Daily volatility check
        if 1.0 < daily_range_pct < 4.0:
            score += 3
            notes.append("Good volatility for grid")
        elif 0.5 < daily_range_pct <= 1.0:
            score += 2
            notes.append("Low volatility")
        elif daily_range_pct >= 4.0:
            score += 1
            notes.append("High volatility - wider spacing needed")
        else:
            notes.append("Insufficient volatility data")

        # Swap check (negative swap on longs hurts grid strategies)
        if sym.swap_long >= 0:
            score += 2
            notes.append("Positive/zero swap on longs!")
        elif abs(sym.swap_long) < 10:
            score += 1
            notes.append("Small negative swap")
        else:
            notes.append(f"Significant negative swap: {sym.swap_long}")

        # Contract size / margin check
        if margin_1lot > 0 and margin_1lot < 5000:
            score += 1
            notes.append("Reasonable margin")

        rating = "EXCELLENT" if score >= 8 else "GOOD" if score >= 6 else "MODERATE" if score >= 4 else "POOR"
        print(f"  Grid Suitability:  {rating} (score {score}/9)")
        for note in notes:
            print(f"    - {note}")
    else:
        print(f"  Grid Suitability:  N/A (not tradeable)")

    # C++ config block
    output_lines.append({
        'name': sym.name,
        'description': sym.description,
        'contract_size': sym.trade_contract_size,
        'digits': sym.digits,
        'point': sym.point,
        'tick_size': sym.trade_tick_size,
        'min_volume': sym.volume_min,
        'max_volume': sym.volume_max,
        'volume_step': sym.volume_step,
        'calc_mode': calc_mode,
        'margin_1lot': margin_1lot,
        'price': price,
        'swap_long': sym.swap_long,
        'swap_short': sym.swap_short,
        'swap_mode': swap_mode,
        'swap_3days': sym.swap_rollover3days,
        'spread_pts': sym.spread,
        'spread_pct': spread_pct,
        'daily_range_pct': daily_range_pct,
        'tradeable': sym.trade_mode > 0 and price > 0,
        'path': sym.path
    })

# Write results to file
output_path = "validation/Broker/OIL_GAS_INSTRUMENTS.txt"
os.makedirs(os.path.dirname(output_path), exist_ok=True)

with open(output_path, "w") as f:
    f.write("=" * 80 + "\n")
    f.write("BROKER MARKETS - OIL / GAS / ENERGY INSTRUMENTS\n")
    f.write(f"Account: {account_info.login} on {account_info.server}\n")
    f.write(f"Leverage: 1:{account_info.leverage}\n")
    f.write("=" * 80 + "\n\n")

    f.write(f"Found {len(found_symbols)} instruments\n\n")

    # Summary table
    f.write(f"{'Symbol':<12} {'Description':<30} {'Contract':>10} {'Spread%':>10} {'DailyRng%':>10} {'SwapL':>8} {'Margin$':>10} {'Trade':>6}\n")
    f.write("-" * 100 + "\n")

    for item in output_lines:
        f.write(f"{item['name']:<12} {str(item['description'])[:28]:<30} {item['contract_size']:>10.1f} "
                f"{item['spread_pct']:>9.4f}% {item['daily_range_pct']:>9.2f}% "
                f"{item['swap_long']:>8.2f} {item['margin_1lot']:>10.2f} "
                f"{'YES' if item['tradeable'] else 'NO':>6}\n")

    f.write("\n\n")

    # C++ configs for each tradeable instrument
    f.write("=" * 80 + "\n")
    f.write("C++ CONFIGURATION BLOCKS\n")
    f.write("=" * 80 + "\n\n")

    for item in output_lines:
        if not item['tradeable']:
            continue
        f.write(f"// {item['name']} - {item['description']}\n")
        f.write(f"// Daily Range: {item['daily_range_pct']:.2f}%, Spread: {item['spread_pct']:.4f}%\n")
        f.write(f"config.symbol = \"{item['name']}\";\n")
        f.write(f"config.contract_size = {item['contract_size']};\n")
        f.write(f"config.leverage = {account_info.leverage};\n")
        f.write(f"config.pip_size = {item['point']};\n")
        f.write(f"config.swap_long = {item['swap_long']};\n")
        f.write(f"config.swap_short = {item['swap_short']};\n")
        f.write(f"config.swap_mode = {item['swap_mode']};\n")
        f.write(f"config.swap_3days = {item['swap_3days']};\n")
        f.write(f"\n")

print(f"\n{'=' * 80}")
print(f"Results written to: {output_path}")
print("=" * 80)

mt5.shutdown()
print("DONE")
