#!/usr/bin/env python3
"""
Export MT5 Tick Data to CSV
Downloads tick data from MT5 and saves in format compatible with C++ backtest engine.

Requirements:
- MT5 terminal must be running and logged in to broker account
- MetaTrader5 Python package: pip install MetaTrader5

Usage:
    python export_mt5_ticks.py XAGUSD 2025-01-01 2026-01-29
"""

import MetaTrader5 as mt5
import pandas as pd
from datetime import datetime
import sys
import os

def export_ticks(symbol: str, start_date: str, end_date: str, output_path: str = None):
    """Export tick data from MT5 to CSV file"""

    # Initialize MT5 connection
    if not mt5.initialize():
        print("Failed to initialize MT5. Is the terminal running?")
        print(f"Error: {mt5.last_error()}")
        sys.exit(1)

    print(f"MT5 initialized successfully")

    # Get account info
    account = mt5.account_info()
    if account:
        print(f"Broker: {account.company}")
        print(f"Server: {account.server}")

    # Ensure symbol is visible
    if not mt5.symbol_select(symbol, True):
        print(f"Failed to select {symbol}")
        mt5.shutdown()
        sys.exit(1)

    # Parse dates
    start_dt = datetime.strptime(start_date, "%Y-%m-%d")
    end_dt = datetime.strptime(end_date, "%Y-%m-%d")

    print(f"\nExporting {symbol} ticks from {start_date} to {end_date}...")
    print("This may take several minutes for large date ranges...")

    # Get ticks - using COPY_TICKS_ALL to get all tick data
    ticks = mt5.copy_ticks_range(symbol, start_dt, end_dt, mt5.COPY_TICKS_ALL)

    if ticks is None or len(ticks) == 0:
        print(f"No tick data received for {symbol}")
        print(f"Error: {mt5.last_error()}")
        mt5.shutdown()
        sys.exit(1)

    print(f"Received {len(ticks):,} ticks")

    # Convert to DataFrame
    df = pd.DataFrame(ticks)

    # Convert timestamp from milliseconds to datetime string
    df['time'] = pd.to_datetime(df['time'], unit='s')
    df['time_str'] = df['time'].dt.strftime('%Y.%m.%d %H:%M:%S')

    # Add milliseconds if available
    if 'time_msc' in df.columns:
        df['ms'] = df['time_msc'] % 1000
        df['time_str'] = df['time_str'] + '.' + df['ms'].astype(str).str.zfill(3)

    # Select and rename columns for C++ format
    # Format: Timestamp\tBid\tAsk\tVolume\tFlags
    output_df = pd.DataFrame({
        'Timestamp': df['time_str'],
        'Bid': df['bid'],
        'Ask': df['ask'],
        'Volume': df['volume'] if 'volume' in df.columns else 0,
        'Flags': df['flags'] if 'flags' in df.columns else 0
    })

    # Generate output path
    if output_path is None:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        output_dir = os.path.join(script_dir, '..', 'validation', symbol)
        os.makedirs(output_dir, exist_ok=True)
        output_path = os.path.join(output_dir, f'{symbol}_TICKS_MT5_EXPORT.csv')

    # Save to CSV with tab separator
    output_df.to_csv(output_path, sep='\t', index=False)

    print(f"\nExported to: {output_path}")
    print(f"File size: {os.path.getsize(output_path) / (1024*1024):.2f} MB")

    # Show sample of data
    print(f"\nFirst few ticks:")
    print(output_df.head())
    print(f"\nLast few ticks:")
    print(output_df.tail())

    # Show price range
    print(f"\nPrice range:")
    print(f"  Bid: {output_df['Bid'].min():.5f} - {output_df['Bid'].max():.5f}")
    print(f"  Ask: {output_df['Ask'].min():.5f} - {output_df['Ask'].max():.5f}")

    # Shutdown
    mt5.shutdown()
    print("\nMT5 connection closed.")

    return output_path

def main():
    if len(sys.argv) < 4:
        print("Usage: python export_mt5_ticks.py SYMBOL START_DATE END_DATE [OUTPUT_PATH]")
        print("Example: python export_mt5_ticks.py XAGUSD 2025-01-01 2026-01-29")
        sys.exit(1)

    symbol = sys.argv[1]
    start_date = sys.argv[2]
    end_date = sys.argv[3]
    output_path = sys.argv[4] if len(sys.argv) > 4 else None

    export_ticks(symbol, start_date, end_date, output_path)

if __name__ == '__main__':
    main()
