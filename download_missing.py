"""Download missing and new untested instruments from both brokers."""
import MetaTrader5 as mt5
import numpy as np
import pandas as pd
import sys
import os
from datetime import datetime, timedelta

if sys.platform == 'win32':
    sys.stdout.reconfigure(encoding='utf-8')

START_DATE = datetime(2025, 1, 1)
END_DATE = datetime(2026, 2, 28, 23, 59, 59)


def download_symbol(symbol, start_date, end_date, output_dir):
    info = mt5.symbol_info(symbol)
    if info is None:
        print(f"  [SKIP] {symbol} - not found")
        return False, 0
    if not info.visible:
        mt5.symbol_select(symbol, True)

    all_ticks = []
    current = start_date
    while current < end_date:
        if current.month == 12:
            month_end = datetime(current.year + 1, 1, 1)
        else:
            month_end = datetime(current.year, current.month + 1, 1)
        month_end = min(month_end, end_date)

        print(f"  {current.strftime('%Y-%m')}...", end=" ", flush=True)
        ticks = mt5.copy_ticks_range(symbol, current, month_end, mt5.COPY_TICKS_ALL)
        if ticks is not None and len(ticks) > 0:
            print(f"{len(ticks):,} ticks")
            all_ticks.append(ticks)
        else:
            err = mt5.last_error()
            print(f"no data (err={err[0]})")
        current = month_end + timedelta(seconds=1)

    if not all_ticks:
        print(f"  [FAIL] No data for {symbol}")
        return False, 0

    combined = np.concatenate(all_ticks)
    print(f"  Total: {len(combined):,} ticks")

    df = pd.DataFrame(combined)
    df['time_dt'] = pd.to_datetime(df['time'], unit='s')
    df['time_str'] = df['time_dt'].dt.strftime('%Y.%m.%d %H:%M:%S')
    if 'time_msc' in df.columns:
        df['ms'] = df['time_msc'] % 1000
        df['time_str'] = df['time_str'] + '.' + df['ms'].astype(str).str.zfill(3)

    output_df = pd.DataFrame({
        'Timestamp': df['time_str'],
        'Bid': df['bid'],
        'Ask': df['ask'],
        'Volume': df['volume'] if 'volume' in df.columns else 0,
        'Flags': df['flags'] if 'flags' in df.columns else 0
    })

    outfile = os.path.join(output_dir, f"{symbol}_TICKS_FULL.csv")
    output_df.to_csv(outfile, sep='\t', index=False)
    size_mb = os.path.getsize(outfile) / (1024 * 1024)
    print(f"  Saved: {outfile} ({size_mb:.1f} MB)")
    return True, len(combined)


def main():
    broker_path = r"C:\Program Files\ MT5 Terminal\terminal64.exe"
    grid_path = r"C:\Program Files\ MetaTrader 5\terminal64.exe"

    # ---- BROKER: Missing + New ----
    broker_symbols = [
        # Previously missing (case-sensitive fix)
        "COPPER-C",   # Copper (cs=25000)
        "Cotton-C",   # Cotton Cash (cs=50000)
        # NEW untested Broker commodities
        "Cocoa-C",    # US Cocoa Cash (cs=10)
        "Coffee-C",   # Coffee Arabica Cash (cs=37500)
        "OJ-C",       # Orange Juice Cash (cs=15000)
        "Wheat-C",    # US Wheat SRW Cash (cs=1000)
        "XPTUSD",     # Platinum (cs=10)
        "XPDUSD",     # Palladium (cs=10)
    ]

    # ---- GRID: New untested ----
    grid_symbols = [
        # NEW untested Grid soft commodities
        "COFARA",     # Arabica Coffee (cs=10)
        "COFROB",     # Robusta Coffee (cs=1)
        "OJ",         # Orange Juice (cs=10)
        "SUGAR",      # White Sugar (cs=2)
        "UKCOCOA",    # UK Cocoa (cs=1)
        "USCOCOA",    # US Cocoa (cs=1)
        # Grid metals
        "XCUUSD",     # Copper (cs=100)
        "XALUSD",     # Aluminium (cs=100)
        "XPTUSD",     # Platinum (cs=100)
        # Grid indices (potential oscillators)
        # Skipping indices for now - focus on commodities
    ]

    # Download Broker
    print("=" * 70)
    print("  BROKER — Missing + New Instruments")
    print("=" * 70)
    if not mt5.initialize(path=broker_path):
        print("[ERROR] Failed to init Broker")
    else:
        acc = mt5.account_info()
        print(f"Account: {acc.login} on {acc.server}")
        os.makedirs("validation/Broker", exist_ok=True)
        for sym in broker_symbols:
            outfile = f"validation/Broker/{sym}_TICKS_FULL.csv"
            if os.path.exists(outfile):
                size_mb = os.path.getsize(outfile) / (1024 * 1024)
                print(f"\n--- {sym} [EXISTS {size_mb:.1f} MB] --- skipping")
                continue
            print(f"\n--- {sym} ---")
            download_symbol(sym, START_DATE, END_DATE, "validation/Broker")
        mt5.shutdown()

    # Download Grid
    print("\n" + "=" * 70)
    print("  GRID — New Instruments")
    print("=" * 70)
    if not mt5.initialize(path=grid_path):
        print("[ERROR] Failed to init Grid")
    else:
        acc = mt5.account_info()
        print(f"Account: {acc.login} on {acc.server}")
        os.makedirs("validation/Grid", exist_ok=True)
        for sym in grid_symbols:
            outfile = f"validation/Grid/{sym}_TICKS_FULL.csv"
            if os.path.exists(outfile):
                size_mb = os.path.getsize(outfile) / (1024 * 1024)
                print(f"\n--- {sym} [EXISTS {size_mb:.1f} MB] --- skipping")
                continue
            print(f"\n--- {sym} ---")
            download_symbol(sym, START_DATE, END_DATE, "validation/Grid")
        mt5.shutdown()

    print("\nDone!")


if __name__ == "__main__":
    main()
