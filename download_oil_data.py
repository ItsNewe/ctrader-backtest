"""
Download tick data for all oil/gas commodity instruments from  MT5.
Downloads 2025 data for: CL-OIL, UKOUSD, UKOUSDft, USOUSD, NG-C, GASOIL-C, GAS-C
"""
import MetaTrader5 as mt5
import numpy as np
import pandas as pd
import sys
import os
from datetime import datetime, timedelta

if sys.platform == 'win32':
    sys.stdout.reconfigure(encoding='utf-8')

# Instruments to download
INSTRUMENTS = ["CL-OIL", "UKOUSD", "UKOUSDft", "USOUSD", "NG-C", "GASOIL-C", "GAS-C"]

def download_symbol(symbol, year, output_dir):
    """Download tick data for a symbol, month by month"""
    start_date = datetime(year, 1, 1)
    end_date = datetime(year, 12, 31, 23, 59, 59)

    # Ensure symbol is in Market Watch
    info = mt5.symbol_info(symbol)
    if info is None:
        print(f"  [SKIP] {symbol} - not found")
        return False, 0
    if not info.visible:
        mt5.symbol_select(symbol, True)

    all_ticks = []
    current = start_date

    while current < end_date:
        month_end = min(current + timedelta(days=31), end_date)
        month_end = month_end.replace(day=1) if month_end.month < 12 else end_date
        if month_end <= current:
            month_end = current + timedelta(days=31)

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

    # Convert to CSV (MT5 format matching existing data)
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

    outfile = os.path.join(output_dir, f"{symbol}_TICKS_2025.csv")
    output_df.to_csv(outfile, sep='\t', index=False)

    size_mb = os.path.getsize(outfile) / (1024*1024)
    print(f"  Saved: {outfile} ({size_mb:.1f} MB)")
    return True, len(combined)


def main():
    mt5_path = r"C:\Program Files\ MT5 Terminal\terminal64.exe"
    if len(sys.argv) > 1:
        mt5_path = sys.argv[1]

    print("=" * 70)
    print("OIL/GAS TICK DATA DOWNLOADER - ")
    print("=" * 70)

    if not mt5.initialize(path=mt5_path):
        print(f"[ERROR] Failed to init MT5 at {mt5_path}")
        sys.exit(1)

    account = mt5.account_info()
    print(f"Account: {account.login} on {account.server}")

    output_dir = "validation/Broker"
    os.makedirs(output_dir, exist_ok=True)

    results = {}
    for symbol in INSTRUMENTS:
        print(f"\n{'='*50}")
        print(f"Downloading {symbol}...")
        print(f"{'='*50}")
        success, count = download_symbol(symbol, 2025, output_dir)
        results[symbol] = (success, count)

    # Summary
    print(f"\n{'='*70}")
    print("DOWNLOAD SUMMARY")
    print(f"{'='*70}")
    print(f"{'Symbol':<12} {'Status':<10} {'Ticks':>12}")
    print("-" * 40)
    total = 0
    for sym in INSTRUMENTS:
        ok, cnt = results[sym]
        status = "OK" if ok else "FAILED"
        print(f"{sym:<12} {status:<10} {cnt:>12,}")
        total += cnt
    print("-" * 40)
    print(f"{'TOTAL':<12} {'':10} {total:>12,}")

    mt5.shutdown()
    print("\nDone!")

if __name__ == "__main__":
    main()
