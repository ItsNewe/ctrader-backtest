"""
Explore available instruments on MT5 and analyze their characteristics
for potential FillUp strategy adaptation.

Features:
1. List all available instruments by category
2. Download recent tick data (1 day) to analyze characteristics
3. Rank instruments by capital efficiency potential

Usage:
    python explore_instruments.py              # List all instruments
    python explore_instruments.py --analyze    # Analyze top candidates
    python explore_instruments.py --download SYMBOL  # Download full year data
"""

from datetime import datetime, timedelta
import pandas as pd
import MetaTrader5 as mt5
import sys
import os
import numpy as np

def get_all_symbols():
    """Get all available symbols grouped by category"""
    symbols = mt5.symbols_get()
    if symbols is None:
        return {}

    categories = {}
    for sym in symbols:
        # Skip symbols that can't be traded
        if not sym.visible:
            continue

        # Categorize by path or name
        path = sym.path
        if path:
            category = path.split('\\')[0]
        else:
            # Guess from name
            name = sym.name
            if any(x in name for x in ['XAU', 'XAG', 'XPT', 'XPD']):
                category = 'Metals'
            elif any(x in name for x in ['USD', 'EUR', 'GBP', 'JPY', 'CHF', 'AUD', 'NZD', 'CAD']):
                category = 'Forex'
            elif any(x in name for x in ['NAS', 'SPX', 'DJI', 'DAX', 'FTSE', 'US30', 'US500', 'US100']):
                category = 'Indices'
            elif any(x in name for x in ['BTC', 'ETH', 'LTC', 'XRP']):
                category = 'Crypto'
            elif any(x in name for x in ['OIL', 'BRENT', 'WTI', 'GAS', 'NG']):
                category = 'Energy'
            else:
                category = 'Other'

        if category not in categories:
            categories[category] = []

        categories[category].append({
            'name': sym.name,
            'description': sym.description,
            'spread': sym.spread,
            'point': sym.point,
            'contract_size': sym.trade_contract_size,
            'digits': sym.digits,
            'trade_mode': sym.trade_mode,
            'calc_mode': sym.trade_calc_mode
        })

    return categories

def analyze_symbol_quick(symbol_name, hours=24):
    """Quick analysis of a symbol's characteristics"""

    symbol_info = mt5.symbol_info(symbol_name)
    if symbol_info is None:
        return None

    # Make sure symbol is visible
    if not symbol_info.visible:
        mt5.symbol_select(symbol_name, True)

    # Get recent tick data
    end_time = datetime.now()
    start_time = end_time - timedelta(hours=hours)

    ticks = mt5.copy_ticks_range(symbol_name, start_time, end_time, mt5.COPY_TICKS_ALL)

    if ticks is None or len(ticks) < 100:
        return None

    df = pd.DataFrame(ticks)
    df['mid'] = (df['bid'] + df['ask']) / 2
    df['spread'] = df['ask'] - df['bid']

    # Calculate characteristics
    avg_price = df['mid'].mean()
    avg_spread = df['spread'].mean()
    spread_pct = (avg_spread / avg_price) * 100

    # Volatility (high-low range over 1 hour windows)
    df['time'] = pd.to_datetime(df['time'], unit='s')
    df['hour'] = df['time'].dt.floor('H')
    hourly = df.groupby('hour').agg({
        'mid': ['max', 'min'],
        'spread': 'mean'
    })
    hourly.columns = ['high', 'low', 'spread']
    hourly['range'] = hourly['high'] - hourly['low']
    hourly['range_pct'] = (hourly['range'] / hourly['high']) * 100

    avg_hourly_range_pct = hourly['range_pct'].mean()

    # Count oscillations (swings > 0.1%)
    threshold_pct = 0.1
    threshold = avg_price * threshold_pct / 100

    swings = 0
    local_high = df['mid'].iloc[0]
    local_low = df['mid'].iloc[0]
    looking_for_peak = True

    for price in df['mid']:
        if looking_for_peak:
            if price > local_high:
                local_high = price
            if price < local_high - threshold:
                swings += 1
                local_low = price
                looking_for_peak = False
        else:
            if price < local_low:
                local_low = price
            if price > local_low + threshold:
                swings += 1
                local_high = price
                looking_for_peak = True

    # Capital efficiency metrics
    oscillations_per_hour = swings / hours if hours > 0 else 0

    # Profit per oscillation (assuming TP = hourly range median)
    median_range = hourly['range'].median()
    profit_per_osc = median_range - avg_spread  # After spread cost

    # Daily potential (24h extrapolation)
    daily_oscillations = oscillations_per_hour * 24
    daily_profit = profit_per_osc * daily_oscillations if profit_per_osc > 0 else 0
    daily_profit_pct = (daily_profit / avg_price) * 100

    return {
        'symbol': symbol_name,
        'avg_price': avg_price,
        'avg_spread': avg_spread,
        'spread_pct': spread_pct,
        'hourly_range_pct': avg_hourly_range_pct,
        'oscillations_per_hour': oscillations_per_hour,
        'profit_per_osc': profit_per_osc,
        'daily_profit_pct': daily_profit_pct,
        'tick_count': len(df),
        'contract_size': symbol_info.trade_contract_size,
        'swap_long': symbol_info.swap_long,
        'swap_short': symbol_info.swap_short,
        'description': symbol_info.description
    }

def download_year_data(symbol, year, output_dir):
    """Download tick data for a specific year"""

    start_date = datetime(year, 1, 1, 0, 0, 0)
    end_date = datetime(year, 12, 31, 23, 59, 59)

    print(f"\nDownloading {symbol} ticks for {year}")
    print(f"Date range: {start_date} to {end_date}")

    # Make sure symbol is visible
    symbol_info = mt5.symbol_info(symbol)
    if symbol_info is None:
        print(f"Symbol {symbol} not found")
        return False, 0

    if not symbol_info.visible:
        mt5.symbol_select(symbol, True)

    # Download in monthly chunks
    all_ticks = []
    current_start = start_date

    while current_start < end_date:
        current_end = min(current_start + timedelta(days=31), end_date)

        print(f"  {current_start.strftime('%Y-%m-%d')} to {current_end.strftime('%Y-%m-%d')}...", end=" ", flush=True)

        ticks = mt5.copy_ticks_range(symbol, current_start, current_end, mt5.COPY_TICKS_ALL)

        if ticks is not None and len(ticks) > 0:
            print(f"{len(ticks):,} ticks")
            all_ticks.append(ticks)
        else:
            error = mt5.last_error()
            if error[0] == -2:
                print("no data")
            else:
                print(f"error: {error}")

        current_start = current_end + timedelta(seconds=1)

    if not all_ticks:
        print(f"No tick data available for {year}")
        return False, 0

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

    # Create output
    output_df = pd.DataFrame({
        'Timestamp': df['time_str'],
        'Bid': df['bid'],
        'Ask': df['ask'],
        'Volume': df['volume'] if 'volume' in df.columns else 0,
        'Flags': df['flags'] if 'flags' in df.columns else 0
    })

    # Save
    output_file = os.path.join(output_dir, f"{symbol}_TICKS_{year}.csv")
    output_df.to_csv(output_file, sep='\t', index=False)
    print(f"Saved to: {output_file}")

    return True, len(combined_ticks)

def main():
    # Connect to MT5
    if not mt5.initialize():
        print("Failed to initialize MT5, error:", mt5.last_error())
        print("\nMake sure MT5 is running and logged in.")
        sys.exit(1)

    # Get terminal info
    terminal_info = mt5.terminal_info()
    if terminal_info:
        print(f"Connected to: {terminal_info.name}")
        print(f"Company: {terminal_info.company}")

        if "Broker" in terminal_info.company:
            broker = "Broker"
            output_dir = "validation/Broker"
        elif "Grid" in terminal_info.company:
            broker = "Grid"
            output_dir = "validation/Grid"
        else:
            broker = terminal_info.company
            output_dir = "validation/Other"
    else:
        broker = "Unknown"
        output_dir = "validation/Other"

    os.makedirs(output_dir, exist_ok=True)
    os.makedirs(os.path.join(output_dir, "historical"), exist_ok=True)

    # Parse command line
    if len(sys.argv) > 1 and sys.argv[1] == "--download":
        # Download mode
        if len(sys.argv) < 3:
            print("Usage: python explore_instruments.py --download SYMBOL [YEAR]")
            mt5.shutdown()
            sys.exit(1)

        symbol = sys.argv[2]
        year = int(sys.argv[3]) if len(sys.argv) > 3 else datetime.now().year

        success, count = download_year_data(symbol, year, os.path.join(output_dir, "historical"))
        mt5.shutdown()

        if success:
            print(f"\n[SUCCESS] Downloaded {count:,} ticks for {symbol} {year}")
        else:
            print(f"\n[FAILED] Could not download data for {symbol} {year}")
        sys.exit(0 if success else 1)

    elif len(sys.argv) > 1 and sys.argv[1] == "--analyze":
        # Analyze mode - check promising instruments
        print("\n" + "="*70)
        print("INSTRUMENT ANALYSIS - Capital Efficiency Ranking")
        print("="*70)

        # Symbols to analyze (commonly available)
        candidates = [
            # Metals
            'XAUUSD', 'XAGUSD', 'XPTUSD', 'XPDUSD',
            # Indices
            'NAS100', 'US100', 'USTEC', 'US30', 'DJ30', 'US500', 'SPX500',
            'GER40', 'DAX40', 'UK100', 'JPN225',
            # Energy
            'XTIUSD', 'XBRUSD', 'UKOIL', 'USOIL', 'WTI', 'BRENT',
            # Crypto
            'BTCUSD', 'ETHUSD',
            # Major forex
            'EURUSD', 'GBPUSD', 'USDJPY', 'USDCHF', 'AUDUSD',
        ]

        results = []
        for symbol in candidates:
            print(f"Analyzing {symbol}...", end=" ", flush=True)
            result = analyze_symbol_quick(symbol)
            if result:
                results.append(result)
                print(f"OK ({result['tick_count']:,} ticks)")
            else:
                print("not available or no data")

        if not results:
            print("\nNo instruments could be analyzed")
            mt5.shutdown()
            sys.exit(1)

        # Sort by daily profit potential
        results.sort(key=lambda x: x['daily_profit_pct'], reverse=True)

        # Print results
        print("\n" + "="*100)
        print(f"{'Symbol':<12} {'Price':>10} {'Spread%':>8} {'Range%/hr':>10} {'Osc/hr':>8} {'Daily%':>8} {'Contract':>10} {'Description'}")
        print("="*100)

        for r in results:
            print(f"{r['symbol']:<12} {r['avg_price']:>10.2f} {r['spread_pct']:>7.3f}% {r['hourly_range_pct']:>9.3f}% "
                  f"{r['oscillations_per_hour']:>8.1f} {r['daily_profit_pct']:>7.2f}% {r['contract_size']:>10.0f} {r['description'][:30]}")

        # Top recommendations
        print("\n" + "="*70)
        print("TOP RECOMMENDATIONS (by capital efficiency)")
        print("="*70)

        for i, r in enumerate(results[:5]):
            spread_cost_ratio = (r['avg_spread'] / r['profit_per_osc'] * 100) if r['profit_per_osc'] > 0 else 999
            print(f"\n{i+1}. {r['symbol']}")
            print(f"   Daily profit potential: {r['daily_profit_pct']:.2f}%")
            print(f"   Oscillations/hour: {r['oscillations_per_hour']:.1f}")
            print(f"   Spread cost ratio: {spread_cost_ratio:.1f}%")
            print(f"   Swap long/short: {r['swap_long']:.2f} / {r['swap_short']:.2f}")
            print(f"   Contract size: {r['contract_size']:.0f}")

            if spread_cost_ratio < 20:
                print(f"   >> EXCELLENT - Low spread cost, high oscillation")
            elif spread_cost_ratio < 50:
                print(f"   >> GOOD - Moderate spread cost")
            else:
                print(f"   >> CAUTION - High spread cost ratio")

        mt5.shutdown()
        sys.exit(0)

    else:
        # List mode - show all available instruments
        print("\n" + "="*70)
        print(f"AVAILABLE INSTRUMENTS ON {broker}")
        print("="*70)

        categories = get_all_symbols()

        for category in sorted(categories.keys()):
            symbols = categories[category]
            print(f"\n{category} ({len(symbols)} instruments):")
            print("-" * 60)

            for sym in sorted(symbols, key=lambda x: x['name']):
                spread_info = f"spread={sym['spread']}" if sym['spread'] else ""
                contract_info = f"contract={sym['contract_size']:.0f}" if sym['contract_size'] else ""
                print(f"  {sym['name']:<15} {sym['description'][:35]:<35} {spread_info} {contract_info}")

        print("\n" + "="*70)
        print("Usage:")
        print("  python explore_instruments.py --analyze           # Analyze top candidates")
        print("  python explore_instruments.py --download SYMBOL   # Download year data")
        print("="*70)

        mt5.shutdown()

if __name__ == "__main__":
    main()
