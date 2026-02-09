"""
Fetch NAS100 symbol parameters and tick data from MT5 API
"""

import MetaTrader5 as mt5
import pandas as pd
from datetime import datetime, timedelta
import json
import os

def fetch_symbol_info(symbol):
    """Fetch all relevant symbol parameters"""
    info = mt5.symbol_info(symbol)
    if info is None:
        print(f"Symbol {symbol} not found, trying alternatives...")
        # Try common NAS100 symbol names
        alternatives = ["NAS100", "USTEC", "US100", "NASDAQ", "NAS100.cash", "USTEC.cash"]
        for alt in alternatives:
            info = mt5.symbol_info(alt)
            if info is not None:
                symbol = alt
                print(f"Found symbol: {symbol}")
                break

    if info is None:
        return None, None

    # Get margin rates
    margin_info = mt5.symbol_info(symbol)

    params = {
        "symbol": symbol,
        "description": info.description,
        "contract_size": info.trade_contract_size,
        "volume_min": info.volume_min,
        "volume_max": info.volume_max,
        "volume_step": info.volume_step,
        "digits": info.digits,
        "point": info.point,
        "spread": info.spread,
        "trade_calc_mode": info.trade_calc_mode,
        "trade_calc_mode_name": get_calc_mode_name(info.trade_calc_mode),
        "margin_initial": info.margin_initial,
        "margin_maintenance": info.margin_maintenance,
        "swap_long": info.swap_long,
        "swap_short": info.swap_short,
        "swap_mode": info.swap_mode,
        "trade_tick_size": info.trade_tick_size,
        "trade_tick_value": info.trade_tick_value,
        "currency_base": info.currency_base,
        "currency_profit": info.currency_profit,
        "currency_margin": info.currency_margin,
    }

    # Get account info
    account = mt5.account_info()
    if account:
        params["account_leverage"] = account.leverage
        params["account_margin_so_so"] = account.margin_so_so
        params["account_margin_so_call"] = account.margin_so_call

    return symbol, params

def get_calc_mode_name(mode):
    modes = {
        0: "SYMBOL_CALC_MODE_FOREX",
        1: "SYMBOL_CALC_MODE_FOREX_NO_LEVERAGE",
        2: "SYMBOL_CALC_MODE_FUTURES",
        3: "SYMBOL_CALC_MODE_CFD",
        4: "SYMBOL_CALC_MODE_CFDINDEX",
        5: "SYMBOL_CALC_MODE_CFDLEVERAGE",
        32: "SYMBOL_CALC_MODE_EXCH_STOCKS",
        33: "SYMBOL_CALC_MODE_EXCH_FUTURES",
        34: "SYMBOL_CALC_MODE_EXCH_OPTIONS",
    }
    return modes.get(mode, f"UNKNOWN_{mode}")

def fetch_ticks(symbol, days=365):
    """Fetch tick data for the specified number of days"""
    end_time = datetime.now()
    start_time = end_time - timedelta(days=days)

    print(f"Fetching ticks from {start_time} to {end_time}...")

    ticks = mt5.copy_ticks_range(symbol, start_time, end_time, mt5.COPY_TICKS_ALL)

    if ticks is None or len(ticks) == 0:
        print("No ticks retrieved")
        return None

    df = pd.DataFrame(ticks)
    df['time'] = pd.to_datetime(df['time'], unit='s')

    return df

def main():
    # Initialize MT5
    if not mt5.initialize():
        print(f"MT5 initialization failed: {mt5.last_error()}")
        return

    print("MT5 initialized successfully")
    print(f"MT5 Version: {mt5.version()}")

    # Try to find NAS100 symbol
    symbol, params = fetch_symbol_info("NAS100")

    if params is None:
        print("Could not find NAS100 symbol. Available symbols:")
        symbols = mt5.symbols_get()
        indices = [s.name for s in symbols if any(x in s.name.upper() for x in ["NAS", "US100", "USTEC", "NDX"])]
        print(indices[:20])
        mt5.shutdown()
        return

    # Save symbol parameters
    output_dir = "validation/NAS100"
    os.makedirs(output_dir, exist_ok=True)

    params_file = f"{output_dir}/symbol_params.json"
    with open(params_file, 'w') as f:
        json.dump(params, f, indent=2)
    print(f"Symbol parameters saved to {params_file}")
    print(json.dumps(params, indent=2))

    # Fetch tick data (start with 30 days, can increase)
    print("\nFetching tick data...")
    ticks_df = fetch_ticks(symbol, days=365)

    if ticks_df is not None:
        ticks_file = f"{output_dir}/{symbol}_TICKS_2025.csv"

        # Format similar to XAUUSD ticks
        ticks_df['datetime'] = ticks_df['time'].dt.strftime('%Y.%m.%d %H:%M:%S.%f').str[:-3]
        export_df = ticks_df[['datetime', 'bid', 'ask']].copy()
        export_df.columns = ['', 'Bid', 'Ask']

        export_df.to_csv(ticks_file, sep='\t', index=False)
        print(f"Ticks saved to {ticks_file}")
        print(f"Total ticks: {len(ticks_df):,}")
        print(f"Date range: {ticks_df['time'].min()} to {ticks_df['time'].max()}")
        print(f"Price range: {ticks_df['bid'].min():.2f} to {ticks_df['bid'].max():.2f}")

        # Calculate trend
        first_price = ticks_df['bid'].iloc[0]
        last_price = ticks_df['bid'].iloc[-1]
        change_pct = (last_price - first_price) / first_price * 100
        print(f"\nPrice change: {first_price:.2f} -> {last_price:.2f} ({change_pct:+.1f}%)")

        if change_pct > 5:
            print("Market direction: UPWARD TREND")
        elif change_pct < -5:
            print("Market direction: DOWNWARD TREND")
        else:
            print("Market direction: SIDEWAYS")

    mt5.shutdown()
    print("\nDone!")

if __name__ == "__main__":
    main()
