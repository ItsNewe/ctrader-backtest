"""
Compute daily return covariance and correlation matrix across all downloaded instruments.
Resamples tick data to daily closing prices, computes log returns, then correlation.
"""
import pandas as pd
import numpy as np
import os
import sys
from datetime import datetime

if sys.platform == 'win32':
    sys.stdout.reconfigure(encoding='utf-8')

# All available instruments with their tick data paths
INSTRUMENTS = {
    # Broker — Crude Oil
    "CL-OIL":    "validation/Broker/CL-OIL_TICKS_FULL.csv",
    "UKOUSD":    "validation/Broker/UKOUSD_TICKS_FULL.csv",
    "UKOUSDft":  "validation/Broker/UKOUSDft_TICKS_FULL.csv",
    "USOUSD":    "validation/Broker/USOUSD_TICKS_FULL.csv",
    # Broker — Energy/Refined
    "NG-C":      "validation/Broker/NG-C_TICKS_FULL.csv",
    "GASOIL-C":  "validation/Broker/GASOIL-C_TICKS_FULL.csv",
    "GAS-C":     "validation/Broker/GAS-C_TICKS_FULL.csv",
    # Broker — Agriculture
    "Soybean-C": "validation/Broker/Soybean-C_TICKS_FULL.csv",
    "Sugar-C":   "validation/Broker/Sugar-C_TICKS_FULL.csv",
    "COPPER-C":  "validation/Broker/COPPER-C_TICKS_FULL.csv",
    "Cotton-C":  "validation/Broker/Cotton-C_TICKS_FULL.csv",
    "Cocoa-C":   "validation/Broker/Cocoa-C_TICKS_FULL.csv",
    "Coffee-C":  "validation/Broker/Coffee-C_TICKS_FULL.csv",
    "OJ-C":      "validation/Broker/OJ-C_TICKS_FULL.csv",
    "Wheat-C":   "validation/Broker/Wheat-C_TICKS_FULL.csv",
    # Broker — Precious Metals
    "XPTUSD-M":  "validation/Broker/XPTUSD_TICKS_FULL.csv",
    "XPDUSD":    "validation/Broker/XPDUSD_TICKS_FULL.csv",
    # Grid — Energy
    "XBRUSD":    "validation/Grid/XBRUSD_TICKS_FULL.csv",
    "XNGUSD":    "validation/Grid/XNGUSD_TICKS_FULL.csv",
    "XTIUSD":    "validation/Grid/XTIUSD_TICKS_FULL.csv",
    # Grid — Soft Commodities
    "CORN":      "validation/Grid/CORN_TICKS_FULL.csv",
    "COTTON":    "validation/Grid/COTTON_TICKS_FULL.csv",
    "SOYBEAN":   "validation/Grid/SOYBEAN_TICKS_FULL.csv",
    "SUGARRAW":  "validation/Grid/SUGARRAW_TICKS_FULL.csv",
    "WHEAT":     "validation/Grid/WHEAT_TICKS_FULL.csv",
    "COFARA":    "validation/Grid/COFARA_TICKS_FULL.csv",
    "COFROB":    "validation/Grid/COFROB_TICKS_FULL.csv",
    "OJ":        "validation/Grid/OJ_TICKS_FULL.csv",
    "SUGAR":     "validation/Grid/SUGAR_TICKS_FULL.csv",
    "UKCOCOA":   "validation/Grid/UKCOCOA_TICKS_FULL.csv",
    "USCOCOA":   "validation/Grid/USCOCOA_TICKS_FULL.csv",
    # Grid — Metals
    "XCUUSD":    "validation/Grid/XCUUSD_TICKS_FULL.csv",
    "XALUSD":    "validation/Grid/XALUSD_TICKS_FULL.csv",
    "XPTUSD-F":  "validation/Grid/XPTUSD_TICKS_FULL.csv",
}

def load_daily_closes(filepath, symbol):
    """Load tick data and resample to daily closing mid-price."""
    print(f"  Loading {symbol}...", end=" ", flush=True)

    try:
        df = pd.read_csv(filepath, sep='\t', usecols=['Timestamp', 'Bid', 'Ask'],
                         dtype={'Bid': float, 'Ask': float})
    except Exception as e:
        print(f"FAILED ({e})")
        return None

    # Parse timestamp - format: 2025.01.02 00:00:03.456
    df['datetime'] = pd.to_datetime(df['Timestamp'], format='mixed')
    df['mid'] = (df['Bid'] + df['Ask']) / 2.0
    df['date'] = df['datetime'].dt.date

    # Take last tick of each day as closing price
    daily = df.groupby('date')['mid'].last()
    daily.index = pd.to_datetime(daily.index)

    print(f"{len(df):,} ticks -> {len(daily)} daily closes "
          f"(${daily.iloc[0]:.4f} -> ${daily.iloc[-1]:.4f})")

    return daily


def main():
    print("=" * 100)
    print("  INSTRUMENT COVARIANCE & CORRELATION ANALYSIS")
    print(f"  {len(INSTRUMENTS)} instruments | Period: 2025.01 - 2026.02")
    print("=" * 100)

    # Load all daily close series
    daily_prices = {}
    for symbol, path in INSTRUMENTS.items():
        if not os.path.exists(path):
            print(f"  [SKIP] {symbol} - file not found: {path}")
            continue
        series = load_daily_closes(path, symbol)
        if series is not None and len(series) > 20:
            daily_prices[symbol] = series

    print(f"\n  Loaded {len(daily_prices)} instruments")

    # Build aligned DataFrame of daily closes
    price_df = pd.DataFrame(daily_prices)

    # Forward-fill for days where some instruments don't trade
    price_df = price_df.ffill()

    # Drop rows where any instrument has NaN (start alignment)
    price_df = price_df.dropna()

    print(f"  Aligned period: {price_df.index[0].date()} to {price_df.index[-1].date()}")
    print(f"  Common trading days: {len(price_df)}")

    # Compute log returns
    returns_df = np.log(price_df / price_df.shift(1)).dropna()

    print(f"  Return observations: {len(returns_df)}")

    # =========================================================================
    # SECTION 1: Daily return statistics
    # =========================================================================
    print(f"\n{'=' * 100}")
    print("  SECTION 1: DAILY RETURN STATISTICS")
    print(f"{'=' * 100}")

    stats = pd.DataFrame({
        'Mean%': returns_df.mean() * 100,
        'Std%': returns_df.std() * 100,
        'Sharpe': returns_df.mean() / returns_df.std() * np.sqrt(252),
        'Min%': returns_df.min() * 100,
        'Max%': returns_df.max() * 100,
        'Cum%': (np.exp(returns_df.sum()) - 1) * 100,
    }).sort_values('Sharpe', ascending=False)

    print(f"\n{'Symbol':<12} {'Mean%':>8} {'Std%':>8} {'Sharpe':>8} {'Min%':>8} {'Max%':>8} {'CumRet%':>10}")
    print("-" * 70)
    for sym, row in stats.iterrows():
        print(f"{sym:<12} {row['Mean%']:>8.3f} {row['Std%']:>8.3f} {row['Sharpe']:>8.2f} "
              f"{row['Min%']:>8.2f} {row['Max%']:>8.2f} {row['Cum%']:>10.1f}")

    # =========================================================================
    # SECTION 2: Correlation Matrix
    # =========================================================================
    print(f"\n{'=' * 100}")
    print("  SECTION 2: CORRELATION MATRIX (Daily Log Returns)")
    print(f"{'=' * 100}")

    corr = returns_df.corr()

    # Print correlation matrix with formatting
    symbols = list(corr.columns)

    # Abbreviated headers
    abbrev = {s: s[:7] for s in symbols}

    # Print header
    header = f"{'':>12} " + " ".join(f"{abbrev[s]:>7}" for s in symbols)
    print(header)
    print("-" * len(header))

    for sym in symbols:
        row_str = f"{sym:>12} "
        for sym2 in symbols:
            val = corr.loc[sym, sym2]
            if sym == sym2:
                row_str += f"{'1.00':>7} "
            elif abs(val) >= 0.7:
                row_str += f"{val:>7.2f}*"  # High correlation marker
            else:
                row_str += f"{val:>7.2f} "
        print(row_str)

    # =========================================================================
    # SECTION 3: Cluster Analysis (highly correlated groups)
    # =========================================================================
    print(f"\n{'=' * 100}")
    print("  SECTION 3: CORRELATION CLUSTERS")
    print(f"{'=' * 100}")

    # Find all pairs with |corr| > 0.5
    print("\n  HIGH CORRELATION PAIRS (|r| > 0.5):")
    print(f"  {'Pair':<25} {'Corr':>8} {'Interpretation'}")
    print("  " + "-" * 70)

    pairs = []
    for i, s1 in enumerate(symbols):
        for j, s2 in enumerate(symbols):
            if j > i:
                r = corr.loc[s1, s2]
                if abs(r) > 0.5:
                    pairs.append((s1, s2, r))

    pairs.sort(key=lambda x: abs(x[2]), reverse=True)
    for s1, s2, r in pairs:
        direction = "move together" if r > 0 else "move opposite"
        strength = "VERY HIGH" if abs(r) > 0.8 else "HIGH" if abs(r) > 0.7 else "MODERATE"
        print(f"  {s1+'/'+s2:<25} {r:>8.3f}   {strength} — {direction}")

    # Find pairs with low correlation (good for diversification)
    print(f"\n  LOW CORRELATION PAIRS (|r| < 0.2) — Best for diversification:")
    print(f"  {'Pair':<25} {'Corr':>8}")
    print("  " + "-" * 40)

    low_pairs = []
    for i, s1 in enumerate(symbols):
        for j, s2 in enumerate(symbols):
            if j > i:
                r = corr.loc[s1, s2]
                if abs(r) < 0.2:
                    low_pairs.append((s1, s2, r))

    low_pairs.sort(key=lambda x: abs(x[2]))
    for s1, s2, r in low_pairs[:30]:  # Limit to 30
        print(f"  {s1+'/'+s2:<25} {r:>8.3f}")
    if len(low_pairs) > 30:
        print(f"  ... and {len(low_pairs) - 30} more")

    # =========================================================================
    # SECTION 4: Covariance Matrix (annualized)
    # =========================================================================
    print(f"\n{'=' * 100}")
    print("  SECTION 4: ANNUALIZED COVARIANCE MATRIX (×10000 for readability)")
    print(f"{'=' * 100}")

    cov = returns_df.cov() * 252  # Annualize
    cov_display = cov * 10000     # Scale for readability

    header = f"{'':>12} " + " ".join(f"{abbrev[s]:>7}" for s in symbols)
    print(header)
    print("-" * len(header))

    for sym in symbols:
        row_str = f"{sym:>12} "
        for sym2 in symbols:
            val = cov_display.loc[sym, sym2]
            row_str += f"{val:>7.1f} "
        print(row_str)

    # =========================================================================
    # SECTION 5: Portfolio implications
    # =========================================================================
    print(f"\n{'=' * 100}")
    print("  SECTION 5: PORTFOLIO DIVERSIFICATION IMPLICATIONS")
    print(f"{'=' * 100}")

    # Group by cluster
    # Crude oil cluster: instruments with avg correlation > 0.6 to each other
    crude_syms = [s for s in symbols if s in ['CL-OIL', 'UKOUSD', 'UKOUSDft', 'USOUSD', 'XBRUSD', 'XTIUSD']]
    natgas_syms = [s for s in symbols if s in ['NG-C', 'XNGUSD']]
    agri_syms = [s for s in symbols if s in ['CORN', 'COTTON', 'SOYBEAN', 'WHEAT', 'SUGARRAW', 'Soybean-C', 'Sugar-C']]
    other_syms = [s for s in symbols if s in ['GASOIL-C', 'GAS-C']]

    def avg_intra_corr(syms):
        if len(syms) < 2:
            return float('nan')
        vals = []
        for i, s1 in enumerate(syms):
            for j, s2 in enumerate(syms):
                if j > i and s1 in corr.columns and s2 in corr.columns:
                    vals.append(corr.loc[s1, s2])
        return np.mean(vals) if vals else float('nan')

    def avg_inter_corr(group1, group2):
        vals = []
        for s1 in group1:
            for s2 in group2:
                if s1 in corr.columns and s2 in corr.columns:
                    vals.append(corr.loc[s1, s2])
        return np.mean(vals) if vals else float('nan')

    print(f"\n  Cluster average correlations:")
    print(f"    Crude Oil internal ({', '.join(crude_syms)}): {avg_intra_corr(crude_syms):.3f}")
    if len(natgas_syms) >= 2:
        print(f"    Natural Gas internal ({', '.join(natgas_syms)}): {avg_intra_corr(natgas_syms):.3f}")
    print(f"    Agriculture internal ({', '.join(agri_syms)}): {avg_intra_corr(agri_syms):.3f}")

    print(f"\n  Cross-cluster correlations:")
    print(f"    Crude Oil vs Natural Gas: {avg_inter_corr(crude_syms, natgas_syms):.3f}")
    print(f"    Crude Oil vs Agriculture: {avg_inter_corr(crude_syms, agri_syms):.3f}")
    print(f"    Natural Gas vs Agriculture: {avg_inter_corr(natgas_syms, agri_syms):.3f}")
    if other_syms:
        print(f"    Crude Oil vs Other ({', '.join(other_syms)}): {avg_inter_corr(crude_syms, other_syms):.3f}")
        print(f"    Agriculture vs Other ({', '.join(other_syms)}): {avg_inter_corr(agri_syms, other_syms):.3f}")

    # Best diversification pair for each crude oil instrument
    print(f"\n  Best diversification partner for each instrument (lowest |corr|):")
    for sym in symbols:
        min_corr = 999
        min_partner = ""
        for sym2 in symbols:
            if sym2 != sym:
                r = abs(corr.loc[sym, sym2])
                if r < min_corr:
                    min_corr = r
                    min_partner = sym2
        print(f"    {sym:<12} <-> {min_partner:<12} (r = {corr.loc[sym, min_partner]:+.3f})")

    print(f"\n{'=' * 100}")
    print("  ANALYSIS COMPLETE")
    print(f"{'=' * 100}")


if __name__ == "__main__":
    main()
