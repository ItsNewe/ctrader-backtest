"""
Download extended January 2026 tick data to match MT5 test period (2025.01.01 - 2026.01.29)
- XAUUSD: Extend to Jan 29
- XAGUSD: Extend to Jan 29
Then merge with 2025 data files
"""

from datetime import datetime, timedelta
import pandas as pd
import MetaTrader5 as mt5
import numpy as np
import os
import sys

def download_ticks(symbol, start_date, end_date):
    """Download ticks for a symbol in weekly chunks"""
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
                print("no data (market closed)")
            else:
                print(f"error: {error}")

        current_start = current_end + timedelta(seconds=1)

    if not all_ticks:
        return None

    return np.concatenate(all_ticks)

def ticks_to_dataframe(ticks, symbol):
    """Convert MT5 ticks to DataFrame in standard format"""
    df = pd.DataFrame(ticks)

    # Convert time
    df['time'] = pd.to_datetime(df['time'], unit='s')
    df['time_str'] = df['time'].dt.strftime('%Y.%m.%d %H:%M:%S')

    # Add milliseconds
    if 'time_msc' in df.columns:
        df['ms'] = df['time_msc'] % 1000
        df['time_str'] = df['time_str'] + '.' + df['ms'].astype(str).str.zfill(3)

    # XAUUSD format: Time, Bid, Ask, Last, Volume, Flags
    # XAGUSD format: Timestamp, Bid, Ask, Volume, Flags
    if symbol == "XAUUSD":
        output_df = pd.DataFrame({
            'Time': df['time_str'],
            'Bid': df['bid'],
            'Ask': df['ask'],
            'Last': df['last'] if 'last' in df.columns else 0.0,
            'Volume': df['volume'] if 'volume' in df.columns else 0,
            'Flags': df['flags'] if 'flags' in df.columns else 0
        })
    else:
        output_df = pd.DataFrame({
            'Timestamp': df['time_str'],
            'Bid': df['bid'],
            'Ask': df['ask'],
            'Volume': df['volume'] if 'volume' in df.columns else 0,
            'Flags': df['flags'] if 'flags' in df.columns else 0
        })

    return output_df

def main():
    print("=" * 70)
    print("Extended Tick Data Download - January 2026")
    print("Target end date: 2026.01.29 (matching MT5 test)")
    print("=" * 70)

    # Connect to MT5
    if not mt5.initialize():
        print("Failed to initialize MT5, error:", mt5.last_error())
        print("\nMake sure MT5 is running and logged in.")
        return

    terminal_info = mt5.terminal_info()
    if terminal_info:
        print(f"Connected to: {terminal_info.name}")
        print(f"Company: {terminal_info.company}")

    # XAUUSD - Download Jan 27-29 to extend existing Jan 2026 data
    print("\n" + "=" * 70)
    print("XAUUSD: Downloading Jan 27-29, 2026")
    print("=" * 70)

    symbol_info = mt5.symbol_info("XAUUSD")
    if symbol_info:
        print(f"Current price: {symbol_info.bid:.2f}")

    xauusd_ticks = download_ticks(
        "XAUUSD",
        datetime(2026, 1, 27, 23, 0, 0),  # Start after existing data
        datetime(2026, 1, 29, 23, 59, 59)
    )

    if xauusd_ticks is not None and len(xauusd_ticks) > 0:
        xauusd_df = ticks_to_dataframe(xauusd_ticks, "XAUUSD")
        print(f"\nXAUUSD extension: {len(xauusd_df):,} ticks")
        print(f"Date range: {xauusd_df['Time'].iloc[0]} to {xauusd_df['Time'].iloc[-1]}")

        # Save extension
        ext_file = "validation/Grid/XAUUSD_TICKS_JAN2026_EXT.csv"
        os.makedirs(os.path.dirname(ext_file), exist_ok=True)
        xauusd_df.to_csv(ext_file, sep='\t', index=False)
        print(f"Saved extension to: {ext_file}")

        # Create combined file (2025 + all Jan 2026)
        base_file = "validation/Grid/XAUUSD_TICKS_2025.csv"
        jan_file = "validation/Grid/XAUUSD_TICKS_JAN2026.csv"
        combined_file = "validation/Grid/XAUUSD_TICKS_2025_EXTENDED.csv"

        if os.path.exists(base_file) and os.path.exists(jan_file):
            print(f"\nCreating combined XAUUSD file...")
            import shutil
            shutil.copy2(base_file, combined_file)

            # Append Jan 2026 data (skip header)
            with open(combined_file, 'a') as outf:
                with open(jan_file, 'r') as inf:
                    next(inf)  # Skip header
                    for line in inf:
                        outf.write(line)

                # Append extension (skip header)
                for _, row in xauusd_df.iterrows():
                    outf.write(f"{row['Time']}\t{row['Bid']}\t{row['Ask']}\t{row['Last']}\t{row['Volume']}\t{row['Flags']}\n")

            # Count lines
            with open(combined_file, 'r') as f:
                total = sum(1 for _ in f) - 1
            print(f"Combined XAUUSD: {total:,} ticks -> {combined_file}")
    else:
        print("No XAUUSD extension data available")

    # XAGUSD - Download Jan 23-29 to extend existing data
    print("\n" + "=" * 70)
    print("XAGUSD: Downloading Jan 23-29, 2026")
    print("=" * 70)

    symbol_info = mt5.symbol_info("XAGUSD")
    if symbol_info:
        print(f"Current price: {symbol_info.bid:.3f}")

    xagusd_ticks = download_ticks(
        "XAGUSD",
        datetime(2026, 1, 23, 23, 0, 0),  # Start after existing data
        datetime(2026, 1, 29, 23, 59, 59)
    )

    if xagusd_ticks is not None and len(xagusd_ticks) > 0:
        xagusd_df = ticks_to_dataframe(xagusd_ticks, "XAGUSD")
        print(f"\nXAGUSD extension: {len(xagusd_df):,} ticks")
        print(f"Date range: {xagusd_df['Timestamp'].iloc[0]} to {xagusd_df['Timestamp'].iloc[-1]}")

        # Save extension
        ext_file = "validation/XAGUSD/XAGUSD_TICKS_JAN2026_EXT.csv"
        os.makedirs(os.path.dirname(ext_file), exist_ok=True)
        xagusd_df.to_csv(ext_file, sep='\t', index=False)
        print(f"Saved extension to: {ext_file}")

        # Append to combined file
        combined_file = "validation/XAGUSD/XAGUSD_TICKS_2025_EXTENDED.csv"
        if os.path.exists(combined_file):
            print(f"\nAppending to combined XAGUSD file...")
            with open(combined_file, 'a') as outf:
                for _, row in xagusd_df.iterrows():
                    outf.write(f"{row['Timestamp']}\t{row['Bid']}\t{row['Ask']}\t{row['Volume']}\t{row['Flags']}\n")

            with open(combined_file, 'r') as f:
                total = sum(1 for _ in f) - 1
            print(f"Combined XAGUSD: {total:,} ticks -> {combined_file}")
    else:
        print("No XAGUSD extension data available")

    mt5.shutdown()

    print("\n" + "=" * 70)
    print("Download complete!")
    print("=" * 70)

if __name__ == "__main__":
    main()
