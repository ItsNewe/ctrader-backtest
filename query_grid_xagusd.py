"""
Query XAGUSD instrument settings from  MT5
and download tick data for full backtest period (2025 + Jan 2026)
"""

import MetaTrader5 as mt5
from datetime import datetime, timedelta
import numpy as np
import pandas as pd
import os
import sys

GRID_PATH = r"C:\Program Files\ MetaTrader 5\terminal64.exe"

def main():
    print("=" * 60)
    print(" XAGUSD - Settings Query & Tick Download")
    print("=" * 60)

    # Connect specifically to  terminal
    if not mt5.initialize(path=GRID_PATH):
        print(f"Failed to initialize Grid MT5 at: {GRID_PATH}")
        print(f"Error: {mt5.last_error()}")
        print("\nMake sure  MT5 is running and logged in.")
        sys.exit(1)

    terminal_info = mt5.terminal_info()
    print(f"Connected to: {terminal_info.name}")
    print(f"Company: {terminal_info.company}")
    print(f"Path: {terminal_info.path}")

    # Ensure XAGUSD is selected
    if not mt5.symbol_select("XAGUSD", True):
        print("Failed to select XAGUSD")
        mt5.shutdown()
        sys.exit(1)

    # ========================================
    # PART 1: Query all instrument settings
    # ========================================
    print("\n" + "=" * 60)
    print("XAGUSD INSTRUMENT SETTINGS ()")
    print("=" * 60)

    info = mt5.symbol_info("XAGUSD")
    if info is None:
        print("Failed to get symbol info")
        mt5.shutdown()
        sys.exit(1)

    # Key trading parameters
    print(f"\n--- Trading Parameters ---")
    print(f"Symbol: {info.name}")
    print(f"Description: {info.description}")
    print(f"Path: {info.path}")
    print(f"Currency Base: {info.currency_base}")
    print(f"Currency Profit: {info.currency_profit}")
    print(f"Currency Margin: {info.currency_margin}")

    print(f"\n--- Contract Specification ---")
    print(f"Contract Size: {info.trade_contract_size}")
    print(f"Volume Min: {info.volume_min}")
    print(f"Volume Max: {info.volume_max}")
    print(f"Volume Step: {info.volume_step}")
    print(f"Digits: {info.digits}")
    print(f"Point: {info.point}")

    print(f"\n--- Margin Settings ---")
    print(f"Trade Calc Mode: {info.trade_calc_mode}")
    # 0=FOREX, 1=CFD, 2=Futures, 3=CFD_INDEX, 4=CFD_LEVERAGE
    calc_mode_names = {0: "FOREX", 1: "CFD", 2: "FUTURES", 3: "CFD_INDEX", 4: "CFD_LEVERAGE"}
    print(f"Trade Calc Mode Name: {calc_mode_names.get(info.trade_calc_mode, 'UNKNOWN')}")
    print(f"Margin Initial: {info.margin_initial}")
    print(f"Margin Maintenance: {info.margin_maintenance}")

    # Account info
    account_info = mt5.account_info()
    if account_info:
        print(f"\n--- Account Settings ---")
        print(f"Leverage: 1:{account_info.leverage}")
        print(f"Margin Mode: {account_info.margin_mode}")
        print(f"Stop Out Level: {account_info.margin_so_so}")
        print(f"Stop Out Mode: {account_info.margin_so_mode}")

    print(f"\n--- Swap Settings ---")
    print(f"Swap Long: {info.swap_long}")
    print(f"Swap Short: {info.swap_short}")
    print(f"Swap Mode: {info.swap_mode}")
    # 0=disabled, 1=points, 2=currency_symbol, 3=currency_margin, 4=currency_deposit
    swap_mode_names = {0: "DISABLED", 1: "POINTS", 2: "CURRENCY_SYMBOL",
                       3: "CURRENCY_MARGIN", 4: "CURRENCY_DEPOSIT"}
    print(f"Swap Mode Name: {swap_mode_names.get(info.swap_mode, 'UNKNOWN')}")
    print(f"Swap Rollover 3 Days: {info.swap_rollover3days}")
    # 0=Sunday, 1=Monday, ..., 6=Saturday
    day_names = {0: "Sunday", 1: "Monday", 2: "Tuesday", 3: "Wednesday",
                 4: "Thursday", 5: "Friday", 6: "Saturday"}
    print(f"Swap 3-Day Name: {day_names.get(info.swap_rollover3days, 'UNKNOWN')}")

    print(f"\n--- Current Prices ---")
    print(f"Bid: {info.bid}")
    print(f"Ask: {info.ask}")
    print(f"Spread (points): {info.spread}")
    print(f"Spread ($): {(info.ask - info.bid):.5f}")

    print(f"\n--- Session ---")
    print(f"Trade Mode: {info.trade_mode}")

    # Print C++ config block
    print("\n" + "=" * 60)
    print("C++ CONFIG BLOCK (copy to test file):")
    print("=" * 60)
    leverage = account_info.leverage if account_info else 500
    print(f"""
    config.symbol = "XAGUSD";
    config.initial_balance = 10000.0;
    config.account_currency = "USD";
    config.contract_size = {info.trade_contract_size};
    config.leverage = {leverage}.0;
    config.margin_rate = {info.margin_initial if info.margin_initial > 0 else 1.0};
    config.pip_size = {info.point};
    config.swap_long = {info.swap_long};
    config.swap_short = {info.swap_short};
    config.swap_mode = {info.swap_mode};
    config.swap_3days = {info.swap_rollover3days};
    // Trade Calc Mode: {calc_mode_names.get(info.trade_calc_mode, 'UNKNOWN')} ({info.trade_calc_mode})
    // Volume: min={info.volume_min}, max={info.volume_max}, step={info.volume_step}
    // Digits: {info.digits}
    """)

    # ========================================
    # PART 2: Download tick data
    # ========================================
    print("\n" + "=" * 60)
    print("DOWNLOADING TICK DATA FROM GRID MARKETS")
    print("=" * 60)

    output_dir = os.path.join("validation", "XAGUSD")
    os.makedirs(output_dir, exist_ok=True)

    # Download 2025 + Jan 2026 in monthly chunks
    start_date = datetime(2024, 12, 31, 0, 0, 0)  # Start just before 2025
    end_date = datetime(2026, 1, 23, 23, 59, 59)

    all_ticks = []
    current_start = start_date

    while current_start < end_date:
        current_end = min(current_start + timedelta(days=31), end_date)
        print(f"  {current_start.strftime('%Y-%m-%d')} to {current_end.strftime('%Y-%m-%d')}...", end=" ", flush=True)

        ticks = mt5.copy_ticks_range("XAGUSD", current_start, current_end, mt5.COPY_TICKS_ALL)

        if ticks is not None and len(ticks) > 0:
            print(f"{len(ticks):,} ticks")
            all_ticks.append(ticks)
        else:
            error = mt5.last_error()
            print(f"no data ({error})")

        current_start = current_end + timedelta(seconds=1)

    mt5.shutdown()

    if not all_ticks:
        print("No tick data available!")
        return

    # Combine all ticks
    combined_ticks = np.concatenate(all_ticks)
    print(f"\nTotal ticks: {len(combined_ticks):,}")

    # Convert to DataFrame
    df = pd.DataFrame(combined_ticks)
    df['time'] = pd.to_datetime(df['time'], unit='s')
    df['time_str'] = df['time'].dt.strftime('%Y.%m.%d %H:%M:%S')

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

    # Stats
    print(f"Date range: {output_df['Timestamp'].iloc[0]} to {output_df['Timestamp'].iloc[-1]}")
    print(f"Bid range: {output_df['Bid'].min():.3f} - {output_df['Bid'].max():.3f}")
    print(f"Ask range: {output_df['Ask'].min():.3f} - {output_df['Ask'].max():.3f}")

    # Save
    output_file = os.path.join(output_dir, "XAGUSD_TICKS_GRID.csv")
    output_df.to_csv(output_file, sep='\t', index=False)
    print(f"\nSaved to: {output_file}")
    print(f"File size: {os.path.getsize(output_file) / 1024 / 1024:.1f} MB")

    print("\n[SUCCESS] Done!")

if __name__ == "__main__":
    main()
