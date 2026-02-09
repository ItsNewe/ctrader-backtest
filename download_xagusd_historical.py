"""
Download historical XAGUSD tick data from MT5 ()
Downloads 2023 and 2024 data for out-of-sample parameter optimization.

Silver price context:
  - 2023: ~$20-$26 (range-bound, +20%)
  - 2024: ~$22-$32 (moderate uptrend, +30%)
  - 2025: ~$29-$99 (extreme bull, +241%) ← current data

The goal is to find parameters on the non-extreme years (2023-2024)
and forward-test them on 2025 to ensure they work in all regimes.

Usage:
    python download_xagusd_historical.py           # Download 2024
    python download_xagusd_historical.py 2023      # Download 2023
    python download_xagusd_historical.py 2023 2024 # Download 2023 and 2024
"""

from datetime import datetime, timedelta
import pandas as pd
import MetaTrader5 as mt5
import numpy as np
import sys
import os


def download_year(symbol, year, output_dir):
    """Download tick data for a specific year"""

    start_date = datetime(year, 1, 1, 0, 0, 0)
    end_date = datetime(year, 12, 31, 23, 59, 59)

    print(f"\n{'='*60}")
    print(f"Downloading {symbol} ticks for {year}")
    print(f"Date range: {start_date} to {end_date}")
    print(f"{'='*60}")

    # Download in weekly chunks to avoid memory issues
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

    if not all_ticks:
        print(f"No tick data available for {year}")
        return False, 0

    # Combine all ticks
    combined_ticks = np.concatenate(all_ticks)
    print(f"\nTotal ticks for {year}: {len(combined_ticks):,}")

    # Convert to DataFrame
    df = pd.DataFrame(combined_ticks)

    # Convert time from Unix timestamp to datetime string
    df['time'] = pd.to_datetime(df['time'], unit='s')
    df['time_str'] = df['time'].dt.strftime('%Y.%m.%d %H:%M:%S')

    # Add milliseconds if available
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

    # Save to CSV
    output_file = os.path.join(output_dir, f"XAGUSD_TICKS_{year}.csv")
    output_df.to_csv(output_file, sep='\t', index=False)
    print(f"Saved to: {output_file}")

    # Print stats
    print(f"Date range: {output_df['Timestamp'].iloc[0]} to {output_df['Timestamp'].iloc[-1]}")
    print(f"Bid range: {output_df['Bid'].min():.3f} - {output_df['Bid'].max():.3f}")
    print(f"Ask range: {output_df['Ask'].min():.3f} - {output_df['Ask'].max():.3f}")

    file_size = os.path.getsize(output_file)
    print(f"File size: {file_size / (1024*1024):.1f} MB")

    return True, len(combined_ticks)


def main():
    # Parse years from command line
    if len(sys.argv) < 2:
        years = [2024]  # Default: just 2024
    elif len(sys.argv) == 2:
        years = [int(sys.argv[1])]
    elif len(sys.argv) == 3:
        start_year = int(sys.argv[1])
        end_year = int(sys.argv[2])
        years = list(range(start_year, end_year + 1))
    else:
        years = [int(y) for y in sys.argv[1:]]

    symbol = "XAGUSD"

    print("=" * 60)
    print(f"XAGUSD Historical Tick Data Downloader")
    print(f"Years to download: {years}")
    print("=" * 60)

    # Connect to MT5
    if not mt5.initialize():
        print("Failed to initialize MT5, error:", mt5.last_error())
        print("\nMake sure MT5 is running and logged in.")
        sys.exit(1)

    terminal_info = mt5.terminal_info()
    if terminal_info:
        print(f"\nConnected to: {terminal_info.name}")
        print(f"Company: {terminal_info.company}")

    # Check symbol
    symbol_info = mt5.symbol_info(symbol)
    if symbol_info is None:
        print(f"Symbol {symbol} not found. Trying to add it...")
        if not mt5.symbol_select(symbol, True):
            print(f"Failed to select {symbol}")
            mt5.shutdown()
            sys.exit(1)
        symbol_info = mt5.symbol_info(symbol)

    print(f"\nSymbol: {symbol}")
    print(f"Contract size: {symbol_info.trade_contract_size}")
    print(f"Current bid: {symbol_info.bid:.3f}")
    print(f"Current ask: {symbol_info.ask:.3f}")

    # Output directory
    output_dir = os.path.join("validation", "XAGUSD")
    os.makedirs(output_dir, exist_ok=True)
    print(f"Output directory: {output_dir}")

    # Download each year
    results = {}
    total_ticks = 0

    for year in years:
        success, tick_count = download_year(symbol, year, output_dir)
        results[year] = (success, tick_count)
        total_ticks += tick_count

    # Shutdown MT5
    mt5.shutdown()

    # Summary
    print("\n" + "=" * 60)
    print("DOWNLOAD SUMMARY")
    print("=" * 60)

    for year, (success, count) in results.items():
        status = "[OK]" if success else "[FAIL]"
        print(f"  {year}: {status} {count:,} ticks")

    print(f"\nTotal: {total_ticks:,} ticks")

    if all(r[0] for r in results.values()):
        print("\n[SUCCESS] All downloads completed!")
        print("\nNext steps:")
        print("  1. Run the parameter optimization on the downloaded data")
        print("  2. Forward-test best parameters on 2025 bull data")
    else:
        failed = [y for y, r in results.items() if not r[0]]
        print(f"\n[WARNING] Some years had no data: {failed}")
        print("  This may be normal if the broker doesn't have tick data for those years.")
        print("   typically has 1-2 years of tick history.")
        print("  Try downloading just 2024 if older years aren't available.")


if __name__ == "__main__":
    main()
