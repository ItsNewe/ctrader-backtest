#!/usr/bin/env python3
"""
Query MT5 Symbol Information
Gets actual broker limits for XAUUSD and XAGUSD including max_volume

Requirements:
- MT5 terminal must be running and logged in to broker account
- MetaTrader5 Python package: pip install MetaTrader5
"""

import MetaTrader5 as mt5
import sys

def query_symbol(symbol: str) -> dict:
    """Query symbol specifications from MT5"""
    info = mt5.symbol_info(symbol)
    if info is None:
        return None

    return {
        'name': info.name,
        'volume_min': info.volume_min,
        'volume_max': info.volume_max,
        'volume_step': info.volume_step,
        'contract_size': info.trade_contract_size,
        'tick_size': info.trade_tick_size,
        'tick_value': info.trade_tick_value,
        'point': info.point,
        'digits': info.digits,
        'swap_long': info.swap_long,
        'swap_short': info.swap_short,
        'swap_mode': info.swap_mode,
        'swap_rollover3days': info.swap_rollover3days,
        'margin_initial': info.margin_initial,
        'spread': info.spread,
        'bid': info.bid,
        'ask': info.ask,
    }

def main():
    # Initialize MT5 connection
    if not mt5.initialize():
        print("Failed to initialize MT5. Is the terminal running?")
        print(f"Error: {mt5.last_error()}")
        sys.exit(1)

    print("=" * 60)
    print("MT5 BROKER SYMBOL SPECIFICATIONS")
    print("=" * 60)

    # Get account info
    account = mt5.account_info()
    if account:
        print(f"\nBroker: {account.company}")
        print(f"Server: {account.server}")
        print(f"Account: {account.login}")
        print(f"Leverage: 1:{account.leverage}")

    # Query symbols
    symbols = ['XAUUSD', 'XAGUSD']
    results = {}

    for symbol in symbols:
        # Ensure symbol is visible
        if not mt5.symbol_select(symbol, True):
            print(f"\nWARNING: {symbol} not available on this broker")
            continue

        info = query_symbol(symbol)
        if info is None:
            print(f"\nWARNING: Could not get info for {symbol}")
            continue

        results[symbol] = info

        print(f"\n{'='*40}")
        print(f"  {symbol}")
        print(f"{'='*40}")
        print(f"Volume Min:     {info['volume_min']:.2f} lots")
        print(f"Volume Max:     {info['volume_max']:.2f} lots")
        print(f"Volume Step:    {info['volume_step']:.2f} lots")
        print(f"Contract Size:  {info['contract_size']:.1f}")
        print(f"Tick Size:      {info['tick_size']}")
        print(f"Tick Value:     {info['tick_value']:.2f}")
        print(f"Point:          {info['point']}")
        print(f"Digits:         {info['digits']}")
        print(f"Spread:         {info['spread']} points")
        print(f"Swap Long:      {info['swap_long']:.2f}")
        print(f"Swap Short:     {info['swap_short']:.2f}")
        print(f"Swap Mode:      {info['swap_mode']}")
        print(f"Swap 3-day:     Day {info['swap_rollover3days']}")
        print(f"Current Bid:    {info['bid']:.{info['digits']}f}")
        print(f"Current Ask:    {info['ask']:.{info['digits']}f}")

    # Output for C++ config
    print("\n" + "=" * 60)
    print("C++ CONFIGURATION (copy to test file)")
    print("=" * 60)

    for symbol, info in results.items():
        pip_size = info['point'] * (10 if info['digits'] in [3, 5] else 1)
        print(f"""
// {symbol} Broker Settings (from MT5)
// Volume Max: {info['volume_max']:.2f} lots
config.symbol = "{symbol}";
config.contract_size = {info['contract_size']:.1f};
config.pip_size = {pip_size};
config.swap_long = {info['swap_long']:.2f};
config.swap_short = {info['swap_short']:.2f};
double max_volume_{symbol.lower()} = {info['volume_max']:.2f};  // BROKER LIMIT
""")

    # Shutdown
    mt5.shutdown()
    print("\nMT5 connection closed.")

if __name__ == '__main__':
    main()
