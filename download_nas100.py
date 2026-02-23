"""
Download NAS100 tick data from MT5 ()
For testing NasdaqUp strategy on its intended instrument type
"""

from datetime import datetime, timedelta
import pandas as pd
import MetaTrader5 as mt5
import sys
import os

def download_ticks():
    # Connect to MT5 ()
    terminal_path = r"C:\Program Files\ MetaTrader 5\terminal64.exe"

    if not mt5.initialize(path=terminal_path):
        print("Failed to initialize MT5, error:", mt5.last_error())
        print(f"Tried path: {terminal_path}")
        return False

    # Get terminal info
    terminal_info = mt5.terminal_info()
    if terminal_info:
        print(f"Connected to: {terminal_info.name}")
        print(f"Company: {terminal_info.company}")

    # Check symbol - try variations
    symbol_names = ["NAS100", "US100", "USTEC", "NAS100."]
    symbol = None
    symbol_info = None

    for name in symbol_names:
        symbol_info = mt5.symbol_info(name)
        if symbol_info is not None:
            symbol = name
            break

    if symbol is None:
        print(f"Symbol not found. Tried: {symbol_names}")
        # List available symbols that might be NASDAQ
        all_symbols = mt5.symbols_get()
        print("\nAvailable symbols containing 'NAS' or 'US1' or 'TEC':")
        for s in all_symbols:
            if 'NAS' in s.name.upper() or 'US1' in s.name.upper() or 'TEC' in s.name.upper():
                print(f"  {s.name}")
        mt5.shutdown()
        return False

    print(f"\nSymbol: {symbol}")
    print(f"Description: {symbol_info.description if hasattr(symbol_info, 'description') else 'N/A'}")
    print(f"Spread: {symbol_info.spread} points")
    print(f"Digits: {symbol_info.digits}")
    print(f"Contract size: {symbol_info.trade_contract_size}")
    print(f"Swap long: {symbol_info.swap_long}")
    print(f"Swap short: {symbol_info.swap_short}")

    # Date range: April 7, 2025 to October 30, 2025
    start_date = datetime(2025, 4, 7, 0, 0, 0)
    end_date = datetime(2025, 10, 30, 23, 59, 59)

    print(f"\nDownloading ticks from {start_date} to {end_date}...")

    # Download in weekly chunks
    all_ticks = []
    current_start = start_date

    while current_start < end_date:
        # Download one week at a time
        current_end = min(current_start + timedelta(days=7), end_date)

        print(f"  Downloading {current_start.strftime('%Y-%m-%d')} to {current_end.strftime('%Y-%m-%d')}...", end=" ", flush=True)

        ticks = mt5.copy_ticks_range(symbol, current_start, current_end, mt5.COPY_TICKS_ALL)

        if ticks is not None and len(ticks) > 0:
            print(f"{len(ticks):,} ticks")
            all_ticks.append(ticks)
        else:
            print("no data or error:", mt5.last_error())

        # Move to next week
        current_start = current_end + timedelta(seconds=1)

    mt5.shutdown()

    if not all_ticks:
        print("No tick data downloaded!")
        return False

    # Combine all ticks
    import numpy as np
    combined_ticks = np.concatenate(all_ticks)
    print(f"\nTotal ticks downloaded: {len(combined_ticks):,}")

    # Convert to DataFrame
    df = pd.DataFrame(combined_ticks)

    # Convert time from Unix timestamp to datetime string
    df['time'] = pd.to_datetime(df['time'], unit='s')
    df['time_str'] = df['time'].dt.strftime('%Y.%m.%d %H:%M:%S')

    # Add milliseconds if available
    if 'time_msc' in df.columns:
        df['ms'] = df['time_msc'] % 1000
        df['time_str'] = df['time_str'] + '.' + df['ms'].astype(str).str.zfill(3)

    # Create output in MT5 tick export format: Time, Bid, Ask, Last, Volume, Flags
    output_df = pd.DataFrame({
        'Time': df['time_str'],
        'Bid': df['bid'],
        'Ask': df['ask'],
        'Last': df['last'] if 'last' in df.columns else 0,
        'Volume': df['volume'] if 'volume' in df.columns else 0,
        'Flags': df['flags'] if 'flags' in df.columns else 0
    })

    # Save to CSV (tab-separated to match MT5 export format)
    output_file = f"validation/Grid/{symbol}_TICKS_2025.csv"
    os.makedirs(os.path.dirname(output_file), exist_ok=True)
    output_df.to_csv(output_file, sep='\t', index=False)
    print(f"\nSaved to: {output_file}")

    # Print sample
    print("\nFirst 5 ticks:")
    print(output_df.head())

    print("\nLast 5 ticks:")
    print(output_df.tail())

    # Statistics
    print(f"\nDate range: {output_df['Time'].iloc[0]} to {output_df['Time'].iloc[-1]}")
    print(f"Bid range: {output_df['Bid'].min():.2f} - {output_df['Bid'].max():.2f}")
    print(f"Ask range: {output_df['Ask'].min():.2f} - {output_df['Ask'].max():.2f}")

    # Also print the actual symbol name used for reference
    print(f"\nSymbol used: {symbol}")
    print(f"Contract size: {symbol_info.trade_contract_size}")
    print(f"Digits: {symbol_info.digits}")

    return True

if __name__ == "__main__":
    print("=" * 60)
    print("NAS100 Tick Data Downloader - Jan-Apr 2025")
    print("=" * 60)

    success = download_ticks()

    if success:
        print("\n✓ Download completed successfully!")
    else:
        print("\n✗ Download failed!")
        sys.exit(1)
