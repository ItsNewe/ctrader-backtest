"""
Analyze VIX from Broker MT5 — download ticks, observe price history,
detect rollover gaps/discontinuities, and characterize the instrument.
"""
import MetaTrader5 as mt5
import pandas as pd
import numpy as np
import sys
from datetime import datetime

if sys.platform == 'win32':
    sys.stdout.reconfigure(encoding='utf-8')

# Try common VIX symbol names on Broker
POSSIBLE_SYMBOLS = ["VIX", "VIX-C", "VIXUSD", "VIX.C", "CBOE-VIX"]

def find_vix_symbol():
    """Search for VIX symbol in Broker"""
    # First try exact matches
    for sym in POSSIBLE_SYMBOLS:
        info = mt5.symbol_info(sym)
        if info is not None:
            return sym, info

    # Search all symbols containing "VIX"
    all_syms = mt5.symbols_get()
    if all_syms:
        vix_matches = [s for s in all_syms if 'VIX' in s.name.upper()]
        if vix_matches:
            return vix_matches[0].name, vix_matches[0]

    return None, None

def main():
    BROKER_PATH = r"C:\Program Files\ MT5 Terminal\terminal64.exe"
    if not mt5.initialize(path=BROKER_PATH):
        print(f"MT5 init failed: {mt5.last_error()}")
        sys.exit(1)

    account = mt5.account_info()
    print(f"Broker: {account.company} | Server: {account.server}")

    # Find VIX
    sym_name, sym_info = find_vix_symbol()
    if sym_name is None:
        print("\nVIX symbol not found. Searching all symbols with 'V'...")
        all_syms = mt5.symbols_get()
        matches = [s.name for s in all_syms if 'V' in s.name.upper() and
                   ('IX' in s.name.upper() or 'OL' in s.name.upper())]
        print(f"Potential matches: {matches[:20]}")
        mt5.shutdown()
        sys.exit(1)

    mt5.symbol_select(sym_name, True)

    # Print full symbol specs
    print(f"\n{'='*60}")
    print(f"Symbol: {sym_name}")
    print(f"{'='*60}")
    print(f"  description:       {sym_info.description}")
    print(f"  path:              {sym_info.path}")
    print(f"  trade_calc_mode:   {sym_info.trade_calc_mode}")
    print(f"  trade_mode:        {sym_info.trade_mode}")
    print(f"  digits:            {sym_info.digits}")
    print(f"  point:             {sym_info.point}")
    print(f"  spread:            {sym_info.spread}")
    print(f"  contract_size:     {sym_info.trade_contract_size}")
    print(f"  volume_min:        {sym_info.volume_min}")
    print(f"  volume_max:        {sym_info.volume_max}")
    print(f"  volume_step:       {sym_info.volume_step}")
    print(f"  swap_long:         {sym_info.swap_long}")
    print(f"  swap_short:        {sym_info.swap_short}")
    print(f"  swap_mode:         {sym_info.swap_mode}")
    print(f"  margin_initial:    {sym_info.margin_initial}")
    print(f"  bid:               {sym_info.bid}")
    print(f"  ask:               {sym_info.ask}")
    print(f"  session_open:      {sym_info.session_open}")
    print(f"  expiration_mode:   {sym_info.expiration_mode}")

    # Try to get margin info
    for direction in [mt5.ORDER_TYPE_BUY, mt5.ORDER_TYPE_SELL]:
        margin = mt5.order_calc_margin(direction, sym_name, sym_info.volume_min, sym_info.ask)
        dir_name = "BUY" if direction == mt5.ORDER_TYPE_BUY else "SELL"
        print(f"  margin_{dir_name} ({sym_info.volume_min} lot): {margin}")

    # Download tick data — full available history
    print(f"\n{'='*60}")
    print(f"Downloading tick data...")
    print(f"{'='*60}")

    start = datetime(2024, 1, 1)
    end = datetime(2026, 2, 28)

    all_ticks = []
    current = start
    while current < end:
        if current.month == 12:
            month_end = datetime(current.year + 1, 1, 1)
        else:
            month_end = datetime(current.year, current.month + 1, 1)
        month_end = min(month_end, end)

        ticks = mt5.copy_ticks_range(sym_name, current, month_end, mt5.COPY_TICKS_ALL)
        if ticks is not None and len(ticks) > 0:
            print(f"  {current.strftime('%Y-%m')}: {len(ticks):>10,} ticks", flush=True)
            all_ticks.append(pd.DataFrame(ticks))
        else:
            print(f"  {current.strftime('%Y-%m')}: no data")
        current = month_end

    mt5.shutdown()

    if not all_ticks:
        print("No tick data available!")
        sys.exit(1)

    df = pd.concat(all_ticks, ignore_index=True)
    df['time'] = pd.to_datetime(df['time'], unit='s')
    df['time_str'] = df['time'].dt.strftime('%Y.%m.%d %H:%M:%S')
    if 'time_msc' in df.columns:
        ms = df['time_msc'] % 1000
        df['time_str'] = df['time_str'] + '.' + ms.astype(str).str.zfill(3)

    print(f"\nTotal ticks: {len(df):,}")
    print(f"Date range: {df['time'].min()} to {df['time'].max()}")

    # Save ticks to CSV
    out_path = f"C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Broker\\{sym_name}_TICKS_FULL.csv"
    out_df = pd.DataFrame({
        'Timestamp': df['time_str'],
        'Bid': df['bid'],
        'Ask': df['ask'],
        'Volume': df['volume'] if 'volume' in df.columns else 0,
        'Flags': df['flags'] if 'flags' in df.columns else 0
    })
    out_df.to_csv(out_path, sep='\t', index=False)
    print(f"Saved to: {out_path}")

    # ===================================================================
    # PRICE ANALYSIS
    # ===================================================================
    print(f"\n{'='*60}")
    print(f"PRICE HISTORY ANALYSIS")
    print(f"{'='*60}")

    # Daily OHLC from bid
    df['date'] = df['time'].dt.date
    daily = df.groupby('date').agg(
        open=('bid', 'first'),
        high=('bid', 'max'),
        low=('bid', 'min'),
        close=('bid', 'last'),
        ticks=('bid', 'count'),
        spread_avg=('ask', lambda x: (x - df.loc[x.index, 'bid']).mean())
    ).reset_index()

    print(f"\nDaily bars: {len(daily)}")
    print(f"Price range: {daily['low'].min():.2f} - {daily['high'].max():.2f}")
    print(f"Current: {daily['close'].iloc[-1]:.2f}")
    print(f"Average spread: {daily['spread_avg'].mean():.4f}")

    # Monthly summary
    print(f"\n--- Monthly Summary ---")
    df['month'] = df['time'].dt.to_period('M')
    monthly = df.groupby('month').agg(
        open=('bid', 'first'),
        high=('bid', 'max'),
        low=('bid', 'min'),
        close=('bid', 'last'),
        ticks=('bid', 'count')
    )
    for idx, row in monthly.iterrows():
        pct_range = (row['high'] - row['low']) / row['open'] * 100
        print(f"  {idx}: O={row['open']:>7.2f}  H={row['high']:>7.2f}  L={row['low']:>7.2f}  "
              f"C={row['close']:>7.2f}  Range={pct_range:>6.1f}%  Ticks={int(row['ticks']):>10,}")

    # ===================================================================
    # DISCONTINUITY / GAP DETECTION (rollover gaps)
    # ===================================================================
    print(f"\n{'='*60}")
    print(f"DISCONTINUITY / GAP DETECTION")
    print(f"{'='*60}")

    # Compute close-to-open gaps between consecutive daily bars
    daily['prev_close'] = daily['close'].shift(1)
    daily['gap'] = daily['open'] - daily['prev_close']
    daily['gap_pct'] = daily['gap'] / daily['prev_close'] * 100

    # Filter significant gaps (> 1% or > 0.50 absolute)
    gaps = daily[daily['gap'].abs() > 0.5].copy()
    if len(gaps) == 0:
        gaps = daily.nlargest(20, 'gap', keep='all')

    print(f"\nSignificant gaps (|gap| > $0.50 or top 20):")
    print(f"{'Date':<12} {'PrevClose':>10} {'Open':>10} {'Gap':>10} {'Gap%':>8}")
    print("-" * 52)
    gaps_sorted = gaps.sort_values('date')
    for _, row in gaps_sorted.iterrows():
        if pd.notna(row['gap']):
            print(f"{row['date']}  {row['prev_close']:>10.2f} {row['open']:>10.2f} "
                  f"{row['gap']:>10.2f} {row['gap_pct']:>7.2f}%")

    # ===================================================================
    # INTRADAY TICK GAPS — detect within-session discontinuities
    # ===================================================================
    print(f"\n--- Large Intraday Tick-to-Tick Jumps (top 30) ---")
    df_sorted = df.sort_values('time').reset_index(drop=True)
    df_sorted['bid_change'] = df_sorted['bid'].diff()
    df_sorted['bid_change_pct'] = df_sorted['bid_change'] / df_sorted['bid'].shift(1) * 100

    big_jumps = df_sorted.nlargest(30, 'bid_change', keep='first')[['time', 'bid', 'bid_change', 'bid_change_pct']]
    big_drops = df_sorted.nsmallest(30, 'bid_change', keep='first')[['time', 'bid', 'bid_change', 'bid_change_pct']]

    print("\nLargest UP jumps:")
    for _, row in big_jumps.iterrows():
        print(f"  {row['time']}  bid={row['bid']:>8.2f}  jump={row['bid_change']:>+8.2f} ({row['bid_change_pct']:>+6.2f}%)")

    print("\nLargest DOWN jumps:")
    for _, row in big_drops.iterrows():
        print(f"  {row['time']}  bid={row['bid']:>8.2f}  jump={row['bid_change']:>+8.2f} ({row['bid_change_pct']:>+6.2f}%)")

    # ===================================================================
    # VIX CHARACTERISTICS FOR STRATEGY RECOMMENDATION
    # ===================================================================
    print(f"\n{'='*60}")
    print(f"VIX CHARACTERISTICS")
    print(f"{'='*60}")

    # Daily returns distribution
    daily['return_pct'] = daily['close'].pct_change() * 100
    print(f"\nDaily returns distribution:")
    print(f"  Mean:   {daily['return_pct'].mean():>+.3f}%")
    print(f"  Stdev:  {daily['return_pct'].std():>.3f}%")
    print(f"  Min:    {daily['return_pct'].min():>+.3f}%")
    print(f"  Max:    {daily['return_pct'].max():>+.3f}%")
    print(f"  Skew:   {daily['return_pct'].skew():>+.3f}")
    print(f"  Kurt:   {daily['return_pct'].kurtosis():>+.3f}")

    # Mean reversion test — autocorrelation of returns
    returns = daily['return_pct'].dropna()
    if len(returns) > 5:
        ac1 = returns.autocorr(lag=1)
        ac2 = returns.autocorr(lag=2)
        ac5 = returns.autocorr(lag=5)
        print(f"\nAutocorrelation (mean-reversion signal if negative):")
        print(f"  Lag-1: {ac1:>+.4f}")
        print(f"  Lag-2: {ac2:>+.4f}")
        print(f"  Lag-5: {ac5:>+.4f}")

    # Time above/below levels
    prices = daily['close'].dropna()
    median_price = prices.median()
    mean_price = prices.mean()
    print(f"\nPrice levels:")
    print(f"  Median: {median_price:.2f}")
    print(f"  Mean:   {mean_price:.2f}")
    print(f"  % time below median: {(prices < median_price).mean()*100:.1f}%")
    print(f"  % time below 20: {(prices < 20).mean()*100:.1f}%")
    print(f"  % time below 15: {(prices < 15).mean()*100:.1f}%")
    print(f"  % time above 25: {(prices > 25).mean()*100:.1f}%")
    print(f"  % time above 30: {(prices > 30).mean()*100:.1f}%")
    print(f"  % time above 40: {(prices > 40).mean()*100:.1f}%")

    # Spike decay analysis — how fast VIX drops after a spike
    print(f"\nSpike decay analysis (days to drop 50% from local highs):")
    spike_threshold = prices.quantile(0.9)
    print(f"  90th percentile level: {spike_threshold:.2f}")

    in_spike = False
    spike_start_price = 0
    spike_start_idx = 0
    decays = []

    for i in range(len(prices)):
        p = prices.iloc[i]
        if not in_spike and p >= spike_threshold:
            in_spike = True
            spike_start_price = p
            spike_start_idx = i
        elif in_spike and p <= spike_start_price * 0.5:
            days_to_half = i - spike_start_idx
            decays.append((daily['date'].iloc[spike_start_idx], spike_start_price, days_to_half))
            in_spike = False

    if decays:
        for d in decays:
            print(f"  {d[0]}: spike to {d[1]:.2f}, 50% decay in {d[2]} days")
        print(f"  Average decay: {np.mean([d[2] for d in decays]):.0f} days")
    else:
        print("  No 50% decays found in data range")

    print(f"\n{'='*60}")
    print(f"DONE")
    print(f"{'='*60}")

if __name__ == '__main__':
    main()
