"""Download tick data for top FX carry trade candidates from both brokers."""
import MetaTrader5 as mt5
import pandas as pd
import sys
import os
from datetime import datetime

if sys.platform == 'win32':
    sys.stdout.reconfigure(encoding='utf-8')

# Top candidates for bidirectional grid (oscillating + carry)
fx_pairs = [
    "AUDNZD", "EURGBP", "AUDCHF", "USDCHF", "NZDCHF", "CADCHF",
    "GBPCHF", "EURCHF", "AUDCAD", "NZDCAD",
    "AUDJPY", "NZDJPY", "GBPJPY", "USDJPY", "EURJPY", "CADJPY",
]

start = datetime(2025, 1, 1)
end = datetime(2026, 2, 28)

def download_from_broker(name, path, output_dir):
    if not mt5.initialize(path=path):
        print(f"[ERROR] Failed to init {name}")
        return

    acc = mt5.account_info()
    print(f"\n{'='*80}")
    print(f"  {name} — Account: {acc.login}")
    print(f"{'='*80}")

    os.makedirs(output_dir, exist_ok=True)

    for sym in fx_pairs:
        outfile = os.path.join(output_dir, f"{sym}_TICKS_FULL.csv")
        if os.path.exists(outfile):
            size_mb = os.path.getsize(outfile) / 1e6
            if size_mb > 1:
                print(f"  {sym}: already exists ({size_mb:.0f}MB), skipping")
                continue

        info = mt5.symbol_info(sym)
        if info is None:
            print(f"  {sym}: NOT FOUND on {name}")
            continue
        if not info.visible:
            mt5.symbol_select(sym, True)

        print(f"  {sym}: downloading...", end="", flush=True)

        # Download in monthly chunks to avoid memory issues
        all_ticks = []
        chunk_start = start
        while chunk_start < end:
            chunk_end = min(
                datetime(chunk_start.year + (1 if chunk_start.month == 12 else 0),
                         (chunk_start.month % 12) + 1, 1),
                end
            )
            ticks = mt5.copy_ticks_range(sym, chunk_start, chunk_end, mt5.COPY_TICKS_ALL)
            if ticks is not None and len(ticks) > 0:
                all_ticks.append(pd.DataFrame(ticks))
            chunk_start = chunk_end

        if not all_ticks:
            print(f" NO DATA")
            continue

        df = pd.concat(all_ticks, ignore_index=True)
        df['time'] = pd.to_datetime(df['time'], unit='s')
        df['datetime'] = df['time'].dt.strftime('%Y.%m.%d %H:%M:%S.') + \
                         df['time_msc'].apply(lambda x: f"{x % 1000:03d}")

        # Save in MT5 tab-separated format
        out = df[['datetime', 'bid', 'ask', 'last', 'volume']].copy()
        out.columns = ['<DATE>', '<BID>', '<ASK>', '<LAST>', '<VOLUME>']
        out.to_csv(outfile, sep='\t', index=False)

        size_mb = os.path.getsize(outfile) / 1e6
        print(f" {len(df):,} ticks ({size_mb:.0f}MB)")

    mt5.shutdown()

# Download from Broker (primary broker with better swap rates)
download_from_broker(
    "BROKER MARKETS",
    r"C:\Program Files\ MT5 Terminal\terminal64.exe",
    r"C:\Users\user\Documents\ctrader-backtest\validation\Broker"
)

# Download from Grid too (for comparison)
download_from_broker(
    "GRID MARKETS",
    r"C:\Program Files\ MetaTrader 5\terminal64.exe",
    r"C:\Users\user\Documents\ctrader-backtest\validation\Grid"
)
