"""
Analyze ALL correlated instrument pairs for carry trade viability.

For each pair with correlation > 0.6:
  1. Check if one has positive swap_long AND the other has positive swap_short
  2. Also check: LONG A + SHORT A of SAME product across brokers (swap rate difference)
  3. Calculate hedge ratio: lots_A/lots_B = (cs_B * price_B) / (cs_A * price_A)
  4. Calculate net daily swap at matched exposure
  5. Account for min lot constraints

Uses real broker data from MT5.
"""
import MetaTrader5 as mt5
import sys
import itertools

if sys.platform == 'win32':
    sys.stdout.reconfigure(encoding='utf-8')

# All instruments with their specs
instruments = {
    # Broker
    "CL-OIL":    {"broker": "Broker", "cs": 1000,   "swap_l": 0.00,    "swap_s": 0.00,    "pip": 0.001,   "min_lot": 0.01, "swap_mode": 0},
    "UKOUSD":    {"broker": "Broker", "cs": 1000,   "swap_l": 9.38,    "swap_s": -30.54,  "pip": 0.001,   "min_lot": 0.01, "swap_mode": 1},
    "UKOUSDft":  {"broker": "Broker", "cs": 1000,   "swap_l": 56.16,   "swap_s": -102.46, "pip": 0.001,   "min_lot": 0.01, "swap_mode": 0},
    "USOUSD":    {"broker": "Broker", "cs": 1000,   "swap_l": -4.83,   "swap_s": -13.03,  "pip": 0.001,   "min_lot": 0.01, "swap_mode": 1},
    "NG-C":      {"broker": "Broker", "cs": 10000,  "swap_l": 32.86,   "swap_s": -49.52,  "pip": 0.001,   "min_lot": 0.10, "swap_mode": 1},
    "GASOIL-C":  {"broker": "Broker", "cs": 100,    "swap_l": 14.51,   "swap_s": -28.61,  "pip": 0.01,    "min_lot": 0.10, "swap_mode": 1},
    "GAS-C":     {"broker": "Broker", "cs": 42000,  "swap_l": -6.65,   "swap_s": 3.17,    "pip": 0.0001,  "min_lot": 0.10, "swap_mode": 1},
    "Soybean-C": {"broker": "Broker", "cs": 500,    "swap_l": -3.27,   "swap_s": 1.24,    "pip": 0.001,   "min_lot": 0.10, "swap_mode": 1},
    "Sugar-C":   {"broker": "Broker", "cs": 112000, "swap_l": 2.27,    "swap_s": -4.80,   "pip": 0.00001, "min_lot": 0.10, "swap_mode": 1},
    "COPPER-C":  {"broker": "Broker", "cs": 25000,  "swap_l": -15.93,  "swap_s": 5.80,    "pip": 0.0001,  "min_lot": 0.10, "swap_mode": 1},
    "Cotton-C":  {"broker": "Broker", "cs": 50000,  "swap_l": -32.29,  "swap_s": 17.52,   "pip": 0.00001, "min_lot": 0.10, "swap_mode": 1},
    "Cocoa-C":   {"broker": "Broker", "cs": 10,     "swap_l": -6.91,   "swap_s": 1.80,    "pip": 0.1,     "min_lot": 0.10, "swap_mode": 1},
    "Coffee-C":  {"broker": "Broker", "cs": 37500,  "swap_l": 15.56,   "swap_s": -26.39,  "pip": 0.0001,  "min_lot": 0.10, "swap_mode": 1},
    "OJ-C":      {"broker": "Broker", "cs": 15000,  "swap_l": 15.66,   "swap_s": -25.17,  "pip": 0.0001,  "min_lot": 0.10, "swap_mode": 1},
    "Wheat-C":   {"broker": "Broker", "cs": 1000,   "swap_l": -2.28,   "swap_s": 1.18,    "pip": 0.001,   "min_lot": 0.10, "swap_mode": 1},
    "XPTUSD":    {"broker": "Broker", "cs": 10,     "swap_l": -60.64,  "swap_s": -7.13,   "pip": 0.01,    "min_lot": 0.10, "swap_mode": 1},
    "XPDUSD":    {"broker": "Broker", "cs": 10,     "swap_l": -29.61,  "swap_s": -19.32,  "pip": 0.01,    "min_lot": 0.10, "swap_mode": 1},
    # Grid
    "XBRUSD":    {"broker": "Grid", "cs": 1000,   "swap_l": 13.29,   "swap_s": -31.79,  "pip": 0.001,   "min_lot": 0.01, "swap_mode": 1},
    "XNGUSD":    {"broker": "Grid", "cs": 10000,  "swap_l": 112.84,  "swap_s": -617.55, "pip": 0.0001,  "min_lot": 0.01, "swap_mode": 1},
    "XTIUSD":    {"broker": "Grid", "cs": 1000,   "swap_l": 0.58,    "swap_s": -8.47,   "pip": 0.001,   "min_lot": 0.01, "swap_mode": 1},
    "CORN":      {"broker": "Grid", "cs": 2,      "swap_l": -21.66,  "swap_s": 6.67,    "pip": 0.01,    "min_lot": 1.00, "swap_mode": 1},
    "COTTON":    {"broker": "Grid", "cs": 10,     "swap_l": -24.36,  "swap_s": 8.58,    "pip": 0.001,   "min_lot": 1.00, "swap_mode": 1},
    "SOYBEAN":   {"broker": "Grid", "cs": 1,      "swap_l": -34.31,  "swap_s": 8.39,    "pip": 0.01,    "min_lot": 1.00, "swap_mode": 1},
    "SUGARRAW":  {"broker": "Grid", "cs": 50,     "swap_l": -6.96,   "swap_s": 2.69,    "pip": 0.001,   "min_lot": 1.00, "swap_mode": 1},
    "WHEAT":     {"broker": "Grid", "cs": 1,      "swap_l": -25.02,  "swap_s": 9.69,    "pip": 0.01,    "min_lot": 1.00, "swap_mode": 1},
    "COFARA":    {"broker": "Grid", "cs": 10,     "swap_l": 11.22,   "swap_s": -25.91,  "pip": 0.01,    "min_lot": 1.00, "swap_mode": 1},
    "COFROB":    {"broker": "Grid", "cs": 1,      "swap_l": 129.94,  "swap_s": -300.48, "pip": 0.01,    "min_lot": 1.00, "swap_mode": 1},
    "OJ":        {"broker": "Grid", "cs": 10,     "swap_l": 1.28,    "swap_s": -52.65,  "pip": 0.01,    "min_lot": 1.00, "swap_mode": 1},
    "SUGAR":     {"broker": "Grid", "cs": 2,      "swap_l": -1.61,   "swap_s": -5.59,   "pip": 0.01,    "min_lot": 1.00, "swap_mode": 1},
    "UKCOCOA":   {"broker": "Grid", "cs": 1,      "swap_l": -7.09,   "swap_s": -0.11,   "pip": 0.01,    "min_lot": 1.00, "swap_mode": 1},
    "USCOCOA":   {"broker": "Grid", "cs": 1,      "swap_l": -139.91, "swap_s": 2.81,    "pip": 0.01,    "min_lot": 1.00, "swap_mode": 1},
    "XCUUSD":    {"broker": "Grid", "cs": 100,    "swap_l": -2199.12,"swap_s": 0.00,    "pip": 0.001,   "min_lot": 0.01, "swap_mode": 1},
    "XALUSD":    {"broker": "Grid", "cs": 100,    "swap_l": -394.20, "swap_s": 0.00,    "pip": 0.001,   "min_lot": 0.01, "swap_mode": 1},
    "XPTUSD-F":  {"broker": "Grid", "cs": 100,    "swap_l": -48.87,  "swap_s": -3.52,   "pip": 0.01,    "min_lot": 0.01, "swap_mode": 1},
    # FX pairs (Broker)
    "AUDCHF":    {"broker": "Broker", "cs": 100000, "swap_l": 3.54,    "swap_s": -8.16,   "pip": 0.00001, "min_lot": 0.01, "swap_mode": 1},
    "USDCHF":    {"broker": "Broker", "cs": 100000, "swap_l": 4.73,    "swap_s": -12.12,  "pip": 0.00001, "min_lot": 0.01, "swap_mode": 1},
    "GBPCHF":    {"broker": "Broker", "cs": 100000, "swap_l": 4.89,    "swap_s": -19.06,  "pip": 0.00001, "min_lot": 0.01, "swap_mode": 1},
    "EURGBP":    {"broker": "Broker", "cs": 100000, "swap_l": -7.84,   "swap_s": 1.18,    "pip": 0.00001, "min_lot": 0.01, "swap_mode": 1},
    "AUDNZD":    {"broker": "Broker", "cs": 100000, "swap_l": 0.54,    "swap_s": -5.78,   "pip": 0.00001, "min_lot": 0.01, "swap_mode": 1},
}

# Approximate prices (from latest ticks)
prices = {
    "CL-OIL": 71.15, "UKOUSD": 68.29, "UKOUSDft": 68.36, "USOUSD": 65.07,
    "NG-C": 3.50, "GASOIL-C": 641.0, "GAS-C": 1.9402,
    "Soybean-C": 1053.0, "Sugar-C": 0.1770, "COPPER-C": 4.55,
    "Cotton-C": 0.6580, "Cocoa-C": 7500.0, "Coffee-C": 3.95,
    "OJ-C": 2.82, "Wheat-C": 546.0, "XPTUSD": 1000.0, "XPDUSD": 980.0,
    "XBRUSD": 69.30, "XNGUSD": 3.52, "XTIUSD": 65.10,
    "CORN": 460.0, "COTTON": 66.0, "SOYBEAN": 1050.0, "SUGARRAW": 17.50,
    "WHEAT": 546.0, "COFARA": 395.0, "COFROB": 5500.0, "OJ": 282.0,
    "SUGAR": 540.0, "UKCOCOA": 5900.0, "USCOCOA": 7500.0,
    "XCUUSD": 4.55, "XALUSD": 2.63, "XPTUSD-F": 1000.0,
    "AUDCHF": 0.5441, "USDCHF": 0.8966, "GBPCHF": 1.1216,
    "EURGBP": 0.8388, "AUDNZD": 1.1080,
}

# Known high correlations (from our covariance analysis)
correlated_pairs = [
    # Same product different broker (r > 0.88)
    ("UKOUSD", "XBRUSD", 0.98),
    ("USOUSD", "XTIUSD", 0.98),
    ("CL-OIL", "XTIUSD", 0.98),
    ("CL-OIL", "XBRUSD", 0.98),
    ("NG-C", "XNGUSD", 0.99),
    ("Sugar-C", "SUGARRAW", 0.88),
    ("Cotton-C", "COTTON", 0.95),
    ("Soybean-C", "SOYBEAN", 0.95),
    ("Wheat-C", "WHEAT", 0.95),
    ("Coffee-C", "COFARA", 0.999),
    ("Cocoa-C", "USCOCOA", 0.997),
    ("Cocoa-C", "UKCOCOA", 0.88),
    ("COPPER-C", "XCUUSD", 0.67),
    # Cross-commodity (moderate correlation)
    ("UKOUSD", "GAS-C", 0.85),    # Brent vs Gasoline
    ("XTIUSD", "GAS-C", 0.85),    # WTI vs Gasoline
    ("UKOUSD", "GASOIL-C", 0.90), # Brent vs Gasoil
    ("CL-OIL", "GAS-C", 0.85),    # Crude vs Gasoline
    ("CL-OIL", "GASOIL-C", 0.90), # Crude vs Gasoil
    ("XBRUSD", "GAS-C", 0.85),    # Brent(F) vs Gasoline
    # FX carries
    ("AUDCHF", "EURGBP", 0.10),   # Low corr = diversification
    ("USDCHF", "EURGBP", 0.10),
]

def calc_swap_per_day(sym, direction, lots):
    """Calculate daily swap income in USD for given lots."""
    info = instruments[sym]
    if info["swap_mode"] == 0:
        return 0.0
    swap_pts = info["swap_l"] if direction == "LONG" else info["swap_s"]
    # swap in points: value = swap_pts * pip_size * contract_size * lots
    return swap_pts * info["pip"] * info["cs"] * lots

def calc_notional(sym, lots):
    """Calculate notional exposure in USD."""
    return instruments[sym]["cs"] * lots * prices.get(sym, 0)

print("=" * 140)
print("  CARRY HEDGE ANALYSIS — All Correlated Pairs")
print("  Looking for: LONG A (earn swap) + SHORT B (earn swap), matched notional exposure")
print("=" * 140)
print()

results = []

for sym_a, sym_b, corr in correlated_pairs:
    if sym_a not in instruments or sym_b not in instruments:
        continue
    if sym_a not in prices or sym_b not in prices:
        continue

    info_a = instruments[sym_a]
    info_b = instruments[sym_b]

    # Try both directions:
    # Option 1: LONG A + SHORT B
    # Option 2: LONG B + SHORT A

    for (long_sym, short_sym) in [(sym_a, sym_b), (sym_b, sym_a)]:
        long_info = instruments[long_sym]
        short_info = instruments[short_sym]

        swap_long_val = long_info["swap_l"]   # Points for going long
        swap_short_val = short_info["swap_s"]  # Points for going short

        # Skip if either swap is zero/disabled
        if long_info["swap_mode"] == 0 and short_info["swap_mode"] == 0:
            continue

        # Both sides must earn (or at least net positive)
        # Calculate at min lot first
        swap_l_per_lot = calc_swap_per_day(long_sym, "LONG", 1.0)
        swap_s_per_lot = calc_swap_per_day(short_sym, "SHORT", 1.0)

        # Hedge ratio: match notional exposure
        # notional_A = cs_A * lots_A * price_A
        # notional_B = cs_B * lots_B * price_B
        # For lots_B = 1.0: lots_A = (cs_B * price_B) / (cs_A * price_A)
        notional_b_per_lot = short_info["cs"] * prices[short_sym]
        notional_a_per_lot = long_info["cs"] * prices[long_sym]

        if notional_a_per_lot == 0:
            continue

        hedge_ratio = notional_b_per_lot / notional_a_per_lot  # lots_A per 1 lot_B

        # Calculate at matched notional
        lots_long = hedge_ratio  # lots of long_sym per 1 lot of short_sym
        lots_short = 1.0

        daily_swap_long = calc_swap_per_day(long_sym, "LONG", lots_long)
        daily_swap_short = calc_swap_per_day(short_sym, "SHORT", lots_short)
        daily_net = daily_swap_long + daily_swap_short

        # Scale to practical minimum lots
        min_lots_short = short_info["min_lot"]
        min_lots_long_raw = hedge_ratio * min_lots_short
        # Round to min_lot step
        lot_step_a = long_info["min_lot"]
        min_lots_long = max(lot_step_a, round(min_lots_long_raw / lot_step_a) * lot_step_a)

        # Recalculate at practical lots
        prac_swap_long = calc_swap_per_day(long_sym, "LONG", min_lots_long)
        prac_swap_short = calc_swap_per_day(short_sym, "SHORT", min_lots_short)
        prac_net = prac_swap_long + prac_swap_short
        prac_notional_long = calc_notional(long_sym, min_lots_long)
        prac_notional_short = calc_notional(short_sym, min_lots_short)
        notional_mismatch = abs(prac_notional_long - prac_notional_short) / max(prac_notional_long, prac_notional_short) * 100

        if daily_net > 0:
            annual_pct = (daily_net * 365) / notional_b_per_lot * 100
            results.append({
                "long": long_sym,
                "short": short_sym,
                "corr": corr,
                "hedge_ratio": hedge_ratio,
                "daily_net": daily_net,
                "annual_pct": annual_pct,
                "swap_long": daily_swap_long,
                "swap_short": daily_swap_short,
                "prac_lots_long": min_lots_long,
                "prac_lots_short": min_lots_short,
                "prac_net": prac_net,
                "prac_notional_long": prac_notional_long,
                "prac_notional_short": prac_notional_short,
                "notional_mismatch": notional_mismatch,
                "broker_long": instruments[long_sym]["broker"],
                "broker_short": instruments[short_sym]["broker"],
            })

# Sort by daily net swap
results.sort(key=lambda x: -x["daily_net"])

print(f"{'LONG':<12} {'SHORT':<12} {'Corr':>5} {'Ratio':>8} {'SwapL$/d':>10} {'SwapS$/d':>10} "
      f"{'Net$/d':>10} {'Ann%':>8} {'BrkrL':<8} {'BrkrS':<8}")
print("-" * 120)

for r in results:
    print(f"{r['long']:<12} {r['short']:<12} {r['corr']:>5.2f} {r['hedge_ratio']:>8.3f} "
          f"{r['swap_long']:>10.2f} {r['swap_short']:>10.2f} "
          f"{r['daily_net']:>10.2f} {r['annual_pct']:>7.2f}% "
          f"{r['broker_long']:<8} {r['broker_short']:<8}")

print(f"\n{'='*140}")
print(f"  PRACTICAL EXECUTION — Minimum Lot Sizes")
print(f"{'='*140}")

print(f"\n{'LONG':<12} {'SHORT':<12} {'LotsL':>8} {'LotsS':>8} {'NotionalL':>12} {'NotionalS':>12} "
      f"{'Mismatch':>10} {'Net$/d':>10} {'Net$/yr':>10} {'Ann%':>8}")
print("-" * 130)

for r in results:
    annual = r['prac_net'] * 365
    avg_notional = (r['prac_notional_long'] + r['prac_notional_short']) / 2
    ann_pct = annual / avg_notional * 100 if avg_notional > 0 else 0
    print(f"{r['long']:<12} {r['short']:<12} "
          f"{r['prac_lots_long']:>8.2f} {r['prac_lots_short']:>8.2f} "
          f"${r['prac_notional_long']:>11,.0f} ${r['prac_notional_short']:>11,.0f} "
          f"{r['notional_mismatch']:>9.1f}% "
          f"${r['prac_net']:>9.2f} ${annual:>9.0f} {ann_pct:>7.2f}%")
