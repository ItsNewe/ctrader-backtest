"""
Download XAGUSD tick data for January 1-23, 2026 from MT5
Appends to existing XAGUSD_TICKS_2025.csv to extend coverage

Usage:
    python download_xagusd_jan2026.py
"""

from datetime import datetime, timedelta
import pandas as pd
import MetaTrader5 as mt5
import numpy as np
import os

def main():
    symbol = "XAGUSD"
    start_date = datetime(2026, 1, 1, 0, 0, 0)
    end_date = datetime(2026, 1, 23, 23, 59, 59)

    print("=" * 60)
    print(f"XAGUSD Tick Data Download - January 2026")
    print(f"Date range: {start_date} to {end_date}")
    print("=" * 60)

    # Connect to MT5
    if not mt5.initialize():
        print("Failed to initialize MT5, error:", mt5.last_error())
        print("\nMake sure MT5 is running and logged in.")
        return

    terminal_info = mt5.terminal_info()
    if terminal_info:
        print(f"Connected to: {terminal_info.name}")
        print(f"Company: {terminal_info.company}")

    # Check symbol
    symbol_info = mt5.symbol_info(symbol)
    if symbol_info is None:
        print(f"Symbol {symbol} not found. Trying to add it...")
        if not mt5.symbol_select(symbol, True):
            print(f"Failed to select {symbol}")
            mt5.shutdown()
            return
        symbol_info = mt5.symbol_info(symbol)

    print(f"\nSymbol: {symbol}")
    print(f"Contract size: {symbol_info.trade_contract_size}")
    print(f"Current bid: {symbol_info.bid}")
    print(f"Current ask: {symbol_info.ask}")

    # Download in weekly chunks
    all_ticks = []
    current_start = start_date

    while current_start < end_date:
        current_end = min(current_start + timedelta(days=7), end_date)

        print(f"  {current_start.strftime('%Y-%m-%d')} to {current_end.strftime('%Y-%m-%d')}...", end=" ", flush=True)

        ticks = mt5.copy_ticks_range(symbol, current_start, current_end, mt5.COPY_TICKS_ALL)

        if ticks is not None and len(ticks) > 0:
            print(f"{len(ticks):,} ticks")
            all_ticks.append(ticks)
        else:
            error = mt5.last_error()
            if error[0] == -2:
                print("no data (market closed or not available)")
            else:
                print(f"error: {error}")

        current_start = current_end + timedelta(seconds=1)

    mt5.shutdown()

    if not all_ticks:
        print("No tick data available for January 2026")
        return

    # Combine all ticks
    combined_ticks = np.concatenate(all_ticks)
    print(f"\nTotal ticks downloaded: {len(combined_ticks):,}")

    # Convert to DataFrame
    df = pd.DataFrame(combined_ticks)

    # Convert time from Unix timestamp to datetime string
    df['time'] = pd.to_datetime(df['time'], unit='s')
    df['time_str'] = df['time'].dt.strftime('%Y.%m.%d %H:%M:%S')

    # Add milliseconds
    if 'time_msc' in df.columns:
        df['ms'] = df['time_msc'] % 1000
        df['time_str'] = df['time_str'] + '.' + df['ms'].astype(str).str.zfill(3)

    # Create output in MT5 tick export format (TSV)
    output_df = pd.DataFrame({
        'Timestamp': df['time_str'],
        'Bid': df['bid'],
        'Ask': df['ask'],
        'Volume': df['volume'] if 'volume' in df.columns else 0,
        'Flags': df['flags'] if 'flags' in df.columns else 0
    })

    # Print stats
    print(f"Date range: {output_df['Timestamp'].iloc[0]} to {output_df['Timestamp'].iloc[-1]}")
    print(f"Bid range: {output_df['Bid'].min():.3f} - {output_df['Bid'].max():.3f}")

    # Save standalone file
    output_dir = os.path.join("validation", "XAGUSD")
    os.makedirs(output_dir, exist_ok=True)

    standalone_file = os.path.join(output_dir, "XAGUSD_TICKS_JAN2026.csv")
    output_df.to_csv(standalone_file, sep='\t', index=False)
    print(f"\nSaved standalone: {standalone_file}")

    # Also create combined file (2025 + Jan 2026)
    existing_file = os.path.join(output_dir, "XAGUSD_TICKS_2025.csv")
    if os.path.exists(existing_file):
        print(f"\nAppending to existing 2025 data...")
        combined_file = os.path.join(output_dir, "XAGUSD_TICKS_2025_EXTENDED.csv")

        # Read existing file header
        with open(existing_file, 'r') as f:
            header = f.readline().strip()

        # Write combined: copy existing + append new (without header)
        import shutil
        shutil.copy2(existing_file, combined_file)

        # Append new data without header
        output_df.to_csv(combined_file, sep='\t', index=False, mode='a', header=False)
        print(f"Saved combined: {combined_file}")

        # Count total lines
        with open(combined_file, 'r') as f:
            total_lines = sum(1 for _ in f) - 1  # minus header
        print(f"Total ticks in combined file: {total_lines:,}")
    else:
        print(f"\nWarning: {existing_file} not found. Only standalone file created.")

    print("\n[SUCCESS] Download complete!")
    print(f"\nTo use extended data in backtests, update tick_config.file_path to:")
    print(f"  {os.path.abspath(combined_file) if os.path.exists(existing_file) else os.path.abspath(standalone_file)}")

if __name__ == "__main__":
    main()
