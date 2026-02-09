"""
Download January 2026 tick data from  via MT5 API
Appends to existing 2025 tick data files
"""

import MetaTrader5 as mt5
from datetime import datetime, timedelta
import pandas as pd
import os

def download_ticks(symbol, start_date, end_date, output_file):
    """Download ticks for a symbol and save to file"""

    print(f"\nDownloading {symbol} ticks from {start_date} to {end_date}...")

    # Download ticks
    ticks = mt5.copy_ticks_range(symbol, start_date, end_date, mt5.COPY_TICKS_ALL)

    if ticks is None or len(ticks) == 0:
        print(f"  No ticks received for {symbol}")
        return 0

    print(f"  Received {len(ticks)} ticks")

    # Convert to DataFrame
    df = pd.DataFrame(ticks)

    # Convert timestamp to datetime string format matching existing files
    df['time'] = pd.to_datetime(df['time'], unit='s')
    df['time_str'] = df['time'].dt.strftime('%Y.%m.%d %H:%M:%S') + '.' + (df['time_msc'] % 1000).astype(str).str.zfill(3)

    # Format for output (matching existing CSV format)
    # XAUUSD format: Time\tBid\tAsk\tLast\tVolume\tFlags
    # XAGUSD format: Timestamp\tBid\tAsk\tVolume\tFlags

    if symbol == "XAUUSD":
        output_df = pd.DataFrame({
            'Time': df['time_str'],
            'Bid': df['bid'],
            'Ask': df['ask'],
            'Last': df['last'],
            'Volume': df['volume'],
            'Flags': df['flags']
        })
    else:  # XAGUSD
        output_df = pd.DataFrame({
            'Timestamp': df['time_str'],
            'Bid': df['bid'],
            'Ask': df['ask'],
            'Volume': df['volume'],
            'Flags': df['flags']
        })

    # Check if file exists to determine if we need header
    file_exists = os.path.exists(output_file)

    # Append to file
    output_df.to_csv(output_file, sep='\t', index=False, header=not file_exists, mode='a')

    print(f"  Saved to {output_file}")
    return len(ticks)

def main():
    # Initialize MT5
    if not mt5.initialize():
        print(f"MT5 initialization failed: {mt5.last_error()}")
        return

    print(f"MT5 initialized: {mt5.terminal_info().name}")
    print(f"Account: {mt5.account_info().login} @ {mt5.account_info().server}")

    # Date range for January 2026
    start_date = datetime(2025, 12, 30, 0, 0, 0)  # Start from Dec 30 to ensure overlap
    end_date = datetime(2026, 1, 29, 23, 59, 59)

    # Output files (new files for Jan 2026 data)
    au_file = r"C:\Users\user\Documents\ctrader-backtest\validation\Grid\XAUUSD_TICKS_JAN2026.csv"
    ag_file = r"C:\Users\user\Documents\ctrader-backtest\validation\XAGUSD\XAGUSD_TICKS_JAN2026.csv"

    # Download XAUUSD
    au_count = download_ticks("XAUUSD", start_date, end_date, au_file)

    # Download XAGUSD
    ag_count = download_ticks("XAGUSD", start_date, end_date, ag_file)

    print(f"\n=== Download Complete ===")
    print(f"XAUUSD: {au_count} ticks")
    print(f"XAGUSD: {ag_count} ticks")

    # Shutdown MT5
    mt5.shutdown()

if __name__ == "__main__":
    main()
