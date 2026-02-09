"""
Download tick data for all 19 instruments from Broker and Grid MT5 terminals.
Period: 2025.01.01 - 2026.02.28
Saves to validation/Broker/ and validation/Grid/

Broker symbols: CL-OIL, UKOUSD, UKOUSDft, USOUSD, NG-C, GASOIL-C, GAS-C,
                Copper-c, Cotton-c, Soybean-C, Sugar-C
Grid symbols: CORN, COTTON, SOYBEAN, SUGARRAW, WHEAT, XBRUSD, XNGUSD, XTIUSD
"""
import MetaTrader5 as mt5
import numpy as np
import pandas as pd
import sys
import os
from datetime import datetime, timedelta

if sys.platform == 'win32':
    sys.stdout.reconfigure(encoding='utf-8')

# Symbol -> broker mapping
BROKER_SYMBOLS = [
    "CL-OIL", "UKOUSD", "UKOUSDft", "USOUSD", "NG-C", "GASOIL-C", "GAS-C",
    "Copper-c", "Cotton-c", "Soybean-C", "Sugar-C"
]

GRID_SYMBOLS = [
    "CORN", "COTTON", "SOYBEAN", "SUGARRAW", "WHEAT", "XBRUSD", "XNGUSD", "XTIUSD"
]

# Map GASOIL XML name to actual broker symbol
SYMBOL_MAP = {
    "GASOIL": "GASOIL-C",  # Broker uses GASOIL-C
}

START_DATE = datetime(2025, 1, 1)
END_DATE = datetime(2026, 2, 28, 23, 59, 59)


def download_symbol(symbol, start_date, end_date, output_dir):
    """Download tick data for a symbol, month by month"""
    info = mt5.symbol_info(symbol)
    if info is None:
        print(f"  [SKIP] {symbol} - not found in Market Watch")
        return False, 0
    if not info.visible:
        mt5.symbol_select(symbol, True)

    all_ticks = []
    current = start_date

    while current < end_date:
        # Move month by month
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

    # Convert to CSV (MT5 tab-separated format)
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


def download_broker(broker_name, mt5_path, symbols, output_dir):
    """Download all symbols for a single broker"""
    print(f"\n{'=' * 70}")
    print(f"  {broker_name} — {len(symbols)} symbols")
    print(f"  Period: {START_DATE.strftime('%Y.%m.%d')} - {END_DATE.strftime('%Y.%m.%d')}")
    print(f"  Output: {output_dir}")
    print(f"{'=' * 70}")

    if not mt5.initialize(path=mt5_path):
        print(f"[ERROR] Failed to init MT5 at {mt5_path}")
        return {}

    account = mt5.account_info()
    print(f"Account: {account.login} on {account.server}")

    os.makedirs(output_dir, exist_ok=True)

    results = {}
    for symbol in symbols:
        print(f"\n--- {symbol} ---")
        # Check if already downloaded
        outfile = os.path.join(output_dir, f"{symbol}_TICKS_FULL.csv")
        if os.path.exists(outfile):
            size_mb = os.path.getsize(outfile) / (1024 * 1024)
            print(f"  [EXISTS] {outfile} ({size_mb:.1f} MB) — skipping")
            results[symbol] = (True, -1)
            continue

        success, count = download_symbol(symbol, START_DATE, END_DATE, output_dir)
        results[symbol] = (success, count)

    mt5.shutdown()
    return results


def main():
    # Default MT5 terminal paths
    broker_path = r"C:\Program Files\ MT5 Terminal\terminal64.exe"
    grid_path = r"C:\Program Files\ MetaTrader 5\terminal64.exe"

    # Check command-line overrides
    if len(sys.argv) >= 2:
        broker = sys.argv[1].lower()
        if broker == "broker":
            print("Running BROKER only")
            results = download_broker("", broker_path,
                                       BROKER_SYMBOLS, "validation/Broker")
            print_summary("Broker", BROKER_SYMBOLS, results)
        elif broker == "grid":
            print("Running GRID only")
            results = download_broker("", grid_path,
                                       GRID_SYMBOLS, "validation/Grid")
            print_summary("Grid", GRID_SYMBOLS, results)
        else:
            print(f"Unknown broker: {broker}. Use 'broker' or 'grid'")
        return

    # Download from both brokers
    print("Downloading from BOTH brokers (run with 'broker' or 'grid' arg for single)")

    broker_results = download_broker("", broker_path,
                                      BROKER_SYMBOLS, "validation/Broker")
    grid_results = download_broker("", grid_path,
                                      GRID_SYMBOLS, "validation/Grid")

    print_summary("Broker", BROKER_SYMBOLS, broker_results)
    print_summary("Grid", GRID_SYMBOLS, grid_results)


def print_summary(broker, symbols, results):
    print(f"\n{'=' * 70}")
    print(f"  {broker} DOWNLOAD SUMMARY")
    print(f"{'=' * 70}")
    print(f"{'Symbol':<15} {'Status':<10} {'Ticks':>15}")
    print("-" * 45)
    total = 0
    for sym in symbols:
        if sym in results:
            ok, cnt = results[sym]
            if cnt == -1:
                status = "EXISTS"
                print(f"{sym:<15} {status:<10} {'(skipped)':>15}")
            else:
                status = "OK" if ok else "FAILED"
                print(f"{sym:<15} {status:<10} {cnt:>15,}")
                total += cnt
        else:
            print(f"{sym:<15} {'N/A':<10}")
    print("-" * 45)
    print(f"{'TOTAL':<15} {'':10} {total:>15,}")


if __name__ == "__main__":
    main()
