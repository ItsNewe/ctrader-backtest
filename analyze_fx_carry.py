"""
FX Carry Trade Analysis — Exposure-Matched Sizing

For FX, carry analysis differs from commodity hedges:
1. SINGLE-PAIR CARRY: Go LONG on pairs where swap_long > 0 (e.g., AUDCHF)
   or SHORT on pairs where swap_short > 0 (e.g., EURGBP)
2. CROSS-BROKER ARBITRAGE: LONG pair on BrokerA + SHORT same pair on BrokerB
   if net swap is positive (swap_long_A + swap_short_B > 0)
3. CARRY + HEDGE PAIRS: LONG one pair + SHORT a correlated pair = net carry
   (e.g., LONG AUDCHF + SHORT EURCHF = capture AUD-EUR rate diff + both earn CHF carry)

Uses backtest results to show grid profit + carry income combined.
"""
import sys
if sys.platform == 'win32':
    sys.stdout.reconfigure(encoding='utf-8')

# ==============================================================================
# FX PAIR SPECIFICATIONS — from live broker queries
# ==============================================================================
fx_pairs = {
    # 
    "AUDCHF_M":  {"broker": "Broker", "symbol": "AUDCHF",  "cs": 100000, "swap_l":  3.54, "swap_s":  -8.16, "pip": 0.00001, "min_lot": 0.01, "price": 0.5441},
    "USDCHF_M":  {"broker": "Broker", "symbol": "USDCHF",  "cs": 100000, "swap_l":  4.73, "swap_s": -12.12, "pip": 0.00001, "min_lot": 0.01, "price": 0.8966},
    "GBPCHF_M":  {"broker": "Broker", "symbol": "GBPCHF",  "cs": 100000, "swap_l":  4.89, "swap_s": -19.06, "pip": 0.00001, "min_lot": 0.01, "price": 1.1216},
    "NZDCHF_M":  {"broker": "Broker", "symbol": "NZDCHF",  "cs": 100000, "swap_l":  1.89, "swap_s":  -5.91, "pip": 0.00001, "min_lot": 0.01, "price": 0.4898},
    "CADCHF_M":  {"broker": "Broker", "symbol": "CADCHF",  "cs": 100000, "swap_l":  1.60, "swap_s":  -6.02, "pip": 0.00001, "min_lot": 0.01, "price": 0.6217},
    "EURCHF_M":  {"broker": "Broker", "symbol": "EURCHF",  "cs": 100000, "swap_l":  1.95, "swap_s":  -9.58, "pip": 0.00001, "min_lot": 0.01, "price": 0.9400},
    "AUDJPY_M":  {"broker": "Broker", "symbol": "AUDJPY",  "cs": 100000, "swap_l":  2.04, "swap_s": -10.70, "pip": 0.001,   "min_lot": 0.01, "price": 95.50},
    "USDJPY_M":  {"broker": "Broker", "symbol": "USDJPY",  "cs": 100000, "swap_l":  7.28, "swap_s": -16.73, "pip": 0.001,   "min_lot": 0.01, "price": 155.20},
    "GBPJPY_M":  {"broker": "Broker", "symbol": "GBPJPY",  "cs": 100000, "swap_l":  5.79, "swap_s": -23.04, "pip": 0.001,   "min_lot": 0.01, "price": 193.60},
    "NZDJPY_M":  {"broker": "Broker", "symbol": "NZDJPY",  "cs": 100000, "swap_l":  0.91, "swap_s":  -7.01, "pip": 0.001,   "min_lot": 0.01, "price": 85.90},
    "EURJPY_M":  {"broker": "Broker", "symbol": "EURJPY",  "cs": 100000, "swap_l":  1.48, "swap_s":  -9.67, "pip": 0.001,   "min_lot": 0.01, "price": 162.40},
    "CADJPY_M":  {"broker": "Broker", "symbol": "CADJPY",  "cs": 100000, "swap_l":  1.78, "swap_s":  -6.69, "pip": 0.001,   "min_lot": 0.01, "price": 107.80},
    "AUDNZD_M":  {"broker": "Broker", "symbol": "AUDNZD",  "cs": 100000, "swap_l":  0.54, "swap_s":  -5.78, "pip": 0.00001, "min_lot": 0.01, "price": 1.1080},
    "EURGBP_M":  {"broker": "Broker", "symbol": "EURGBP",  "cs": 100000, "swap_l": -7.84, "swap_s":   1.18, "pip": 0.00001, "min_lot": 0.01, "price": 0.8388},
    "AUDCAD_M":  {"broker": "Broker", "symbol": "AUDCAD",  "cs": 100000, "swap_l":  1.82, "swap_s":  -6.44, "pip": 0.00001, "min_lot": 0.01, "price": 0.8835},
    "NZDCAD_M":  {"broker": "Broker", "symbol": "NZDCAD",  "cs": 100000, "swap_l": -0.52, "swap_s":  -3.43, "pip": 0.00001, "min_lot": 0.01, "price": 0.7975},
    # 
    "AUDCHF_F":  {"broker": "Grid", "symbol": "AUDCHF",  "cs": 100000, "swap_l":  2.80, "swap_s":  -7.50, "pip": 0.00001, "min_lot": 0.01, "price": 0.5441},
    "USDCHF_F":  {"broker": "Grid", "symbol": "USDCHF",  "cs": 100000, "swap_l":  4.10, "swap_s": -10.80, "pip": 0.00001, "min_lot": 0.01, "price": 0.8966},
    "GBPCHF_F":  {"broker": "Grid", "symbol": "GBPCHF",  "cs": 100000, "swap_l":  3.90, "swap_s": -17.20, "pip": 0.00001, "min_lot": 0.01, "price": 1.1216},
    "NZDCHF_F":  {"broker": "Grid", "symbol": "NZDCHF",  "cs": 100000, "swap_l":  1.40, "swap_s":  -5.20, "pip": 0.00001, "min_lot": 0.01, "price": 0.4898},
    "CADCHF_F":  {"broker": "Grid", "symbol": "CADCHF",  "cs": 100000, "swap_l":  1.20, "swap_s":  -5.40, "pip": 0.00001, "min_lot": 0.01, "price": 0.6217},
    "EURCHF_F":  {"broker": "Grid", "symbol": "EURCHF",  "cs": 100000, "swap_l":  1.50, "swap_s":  -8.60, "pip": 0.00001, "min_lot": 0.01, "price": 0.9400},
    "AUDJPY_F":  {"broker": "Grid", "symbol": "AUDJPY",  "cs": 100000, "swap_l":  1.60, "swap_s":  -9.80, "pip": 0.001,   "min_lot": 0.01, "price": 95.50},
    "USDJPY_F":  {"broker": "Grid", "symbol": "USDJPY",  "cs": 100000, "swap_l":  6.40, "swap_s": -15.10, "pip": 0.001,   "min_lot": 0.01, "price": 155.20},
    "GBPJPY_F":  {"broker": "Grid", "symbol": "GBPJPY",  "cs": 100000, "swap_l":  4.80, "swap_s": -21.00, "pip": 0.001,   "min_lot": 0.01, "price": 193.60},
    "NZDJPY_F":  {"broker": "Grid", "symbol": "NZDJPY",  "cs": 100000, "swap_l":  0.60, "swap_s":  -6.20, "pip": 0.001,   "min_lot": 0.01, "price": 85.90},
    "EURJPY_F":  {"broker": "Grid", "symbol": "EURJPY",  "cs": 100000, "swap_l":  1.10, "swap_s":  -8.80, "pip": 0.001,   "min_lot": 0.01, "price": 162.40},
    "CADJPY_F":  {"broker": "Grid", "symbol": "CADJPY",  "cs": 100000, "swap_l":  1.40, "swap_s":  -5.90, "pip": 0.001,   "min_lot": 0.01, "price": 107.80},
    "AUDNZD_F":  {"broker": "Grid", "symbol": "AUDNZD",  "cs": 100000, "swap_l":  0.30, "swap_s":  -5.10, "pip": 0.00001, "min_lot": 0.01, "price": 1.1080},
    "EURGBP_F":  {"broker": "Grid", "symbol": "EURGBP",  "cs": 100000, "swap_l": -7.20, "swap_s":   0.80, "pip": 0.00001, "min_lot": 0.01, "price": 0.8388},
    "AUDCAD_F":  {"broker": "Grid", "symbol": "AUDCAD",  "cs": 100000, "swap_l":  1.50, "swap_s":  -5.80, "pip": 0.00001, "min_lot": 0.01, "price": 0.8835},
    "NZDCAD_F":  {"broker": "Grid", "symbol": "NZDCAD",  "cs": 100000, "swap_l": -0.80, "swap_s":  -3.10, "pip": 0.00001, "min_lot": 0.01, "price": 0.7975},
}

# Backtest results from our 48-test run (NaiveBidi = fill_both)
backtest_results = {
    "AUDCHF":  {"fillup": 1.27, "filldown": 1.26, "naivebidi": 1.56, "bidi_dd": 27.89},
    "USDCHF":  {"fillup": 1.20, "filldown": 1.21, "naivebidi": 1.44, "bidi_dd": 52.76},
    "GBPCHF":  {"fillup": 1.10, "filldown": 1.15, "naivebidi": 1.27, "bidi_dd": 12.66},
    "NZDCHF":  {"fillup": 1.23, "filldown": 1.19, "naivebidi": 1.45, "bidi_dd": 32.30},
    "CADCHF":  {"fillup": 1.12, "filldown": 1.15, "naivebidi": 1.27, "bidi_dd": 23.83},
    "EURCHF":  {"fillup": 1.06, "filldown": 1.08, "naivebidi": 1.15, "bidi_dd":  3.04},
    "AUDJPY":  {"fillup": 0.00, "filldown": 0.00, "naivebidi": 0.01, "bidi_dd": 99.00, "stopout": True},
    "USDJPY":  {"fillup": 0.01, "filldown": 2.71, "naivebidi": 2.71, "bidi_dd":  4.91},
    "GBPJPY":  {"fillup": 0.00, "filldown": 0.01, "naivebidi": 3.85, "bidi_dd":  9.95},
    "NZDJPY":  {"fillup": 1.59, "filldown": 1.70, "naivebidi": 2.42, "bidi_dd": 45.18},
    "EURJPY":  {"fillup": 3.32, "filldown": 0.01, "naivebidi": 4.21, "bidi_dd": 30.70},
    "CADJPY":  {"fillup": 1.56, "filldown": 1.71, "naivebidi": 2.27, "bidi_dd":  6.01},
    "AUDNZD":  {"fillup": 1.10, "filldown": 1.09, "naivebidi": 1.18, "bidi_dd": 36.16},
    "EURGBP":  {"fillup": 1.12, "filldown": 1.09, "naivebidi": 1.20, "bidi_dd": 14.52},
    "AUDCAD":  {"fillup": 1.27, "filldown": 1.23, "naivebidi": 1.52, "bidi_dd": 57.54},
    "NZDCAD":  {"fillup": 1.26, "filldown": 1.17, "naivebidi": 1.49, "bidi_dd":  7.12},
}

def calc_swap_per_day_usd(info, direction, lots):
    """Calculate daily swap in USD."""
    swap_pts = info["swap_l"] if direction == "LONG" else info["swap_s"]
    return swap_pts * info["pip"] * info["cs"] * lots

def calc_notional(info, lots):
    return info["cs"] * lots * info["price"]

# ==============================================================================
# SECTION 1: SINGLE-PAIR CARRY (simple directional + grid)
# ==============================================================================
print("=" * 140)
print("  SECTION 1: SINGLE-PAIR CARRY — Direction that earns swap")
print("  (Grid profit + swap income, $10k account, 0.01 lots)")
print("=" * 140)

print(f"\n{'Symbol':<10} {'Broker':<8} {'Direction':<6} {'Swap/lot/day':>14} {'Swap/yr/lot':>14} "
      f"{'NaiveBidi':>10} {'BidiDD%':>8} {'CarryAnn%':>10} {'GridProfit':>10} "
      f"{'Combo$':>10}")
print("-" * 140)

single_results = []
for key, info in fx_pairs.items():
    sym = info["symbol"]
    broker = info["broker"]

    # Which direction earns carry?
    swap_l_day = calc_swap_per_day_usd(info, "LONG", 1.0)
    swap_s_day = calc_swap_per_day_usd(info, "SHORT", 1.0)

    if swap_l_day > 0:
        direction = "LONG"
        swap_day = swap_l_day
    elif swap_s_day > 0:
        direction = "SHORT"
        swap_day = swap_s_day
    else:
        direction = "NONE"
        swap_day = max(swap_l_day, swap_s_day)

    swap_yr = swap_day * 365
    notional = calc_notional(info, 0.01)
    carry_ann_pct = (swap_yr / notional * 100) if notional > 0 else 0

    bt = backtest_results.get(sym, {})
    bidi_ret = bt.get("naivebidi", 0)
    bidi_dd = bt.get("bidi_dd", 0)
    stopout = bt.get("stopout", False)

    # Grid profit (on $10k, zero swap)
    grid_profit = (bidi_ret - 1.0) * 10000 if bidi_ret > 0 else -10000

    # Approximate combo: grid profit + carry for 1 year
    # NaiveBidi uses ~5-20 positions avg, assume avg 8 positions open
    # Carry is earned on open position volume, not just min lot
    # Conservative estimate: avg 0.05 lots open throughout the year
    avg_lots_open = 0.05
    combo_carry = calc_swap_per_day_usd(info, direction, avg_lots_open) * 365
    combo_total = grid_profit + combo_carry

    single_results.append({
        "key": key, "symbol": sym, "broker": broker, "direction": direction,
        "swap_day": swap_day, "swap_yr": swap_yr, "carry_pct": carry_ann_pct,
        "bidi_ret": bidi_ret, "bidi_dd": bidi_dd, "stopout": stopout,
        "grid_profit": grid_profit, "combo_carry": combo_carry, "combo_total": combo_total,
    })

# Sort by combo total
single_results.sort(key=lambda x: -x["combo_total"])

for r in single_results:
    so_mark = " [SO]" if r["stopout"] else ""
    print(f"{r['symbol']:<10} {r['broker']:<8} {r['direction']:<6} "
          f"${r['swap_day']:>12.2f} ${r['swap_yr']:>12.0f} "
          f"{r['bidi_ret']:>9.2f}x {r['bidi_dd']:>7.1f}% "
          f"{r['carry_pct']:>9.2f}% "
          f"${r['grid_profit']:>9.0f} "
          f"${r['combo_total']:>9.0f}{so_mark}")

# ==============================================================================
# SECTION 2: CROSS-BROKER ARBITRAGE (same pair, opposite sides)
# ==============================================================================
print(f"\n{'='*140}")
print("  SECTION 2: CROSS-BROKER ARBITRAGE — LONG on BrokerA + SHORT on BrokerB (same pair)")
print("  If swap_long(A) + swap_short(B) > 0, earn net carry with zero directional risk")
print(f"{'='*140}")

print(f"\n{'Pair':<10} {'LONG on':<10} {'SHORT on':<10} {'SwapL$/d':>10} {'SwapS$/d':>10} "
      f"{'Net$/d':>10} {'Net$/yr':>10} {'Notional':>12} {'Ann%':>8}")
print("-" * 120)

arb_results = []
symbols_seen = set()
for key_a, info_a in fx_pairs.items():
    for key_b, info_b in fx_pairs.items():
        if key_a == key_b: continue
        if info_a["symbol"] != info_b["symbol"]: continue
        if info_a["broker"] == info_b["broker"]: continue

        pair_id = tuple(sorted([key_a, key_b]))
        if pair_id in symbols_seen: continue

        # Try LONG A + SHORT B
        swap_l = calc_swap_per_day_usd(info_a, "LONG", 1.0)
        swap_s = calc_swap_per_day_usd(info_b, "SHORT", 1.0)
        net1 = swap_l + swap_s

        # Try LONG B + SHORT A
        swap_l2 = calc_swap_per_day_usd(info_b, "LONG", 1.0)
        swap_s2 = calc_swap_per_day_usd(info_a, "SHORT", 1.0)
        net2 = swap_l2 + swap_s2

        if net1 >= net2 and net1 > 0:
            long_key, short_key = key_a, key_b
            long_info, short_info = info_a, info_b
            net = net1
            swap_long_d = swap_l
            swap_short_d = swap_s
        elif net2 > net1 and net2 > 0:
            long_key, short_key = key_b, key_a
            long_info, short_info = info_b, info_a
            net = net2
            swap_long_d = swap_l2
            swap_short_d = swap_s2
        else:
            # Both negative = no arb
            symbols_seen.add(pair_id)
            continue

        symbols_seen.add(pair_id)
        notional = calc_notional(long_info, 1.0)
        ann_pct = (net * 365) / notional * 100 if notional > 0 else 0

        arb_results.append({
            "symbol": info_a["symbol"],
            "long_broker": long_info["broker"],
            "short_broker": short_info["broker"],
            "swap_long_d": swap_long_d,
            "swap_short_d": swap_short_d,
            "net_d": net,
            "net_yr": net * 365,
            "notional": notional,
            "ann_pct": ann_pct,
        })

arb_results.sort(key=lambda x: -x["net_d"])

for r in arb_results:
    print(f"{r['symbol']:<10} {r['long_broker']:<10} {r['short_broker']:<10} "
          f"${r['swap_long_d']:>9.2f} ${r['swap_short_d']:>9.2f} "
          f"${r['net_d']:>9.2f} ${r['net_yr']:>9.0f} "
          f"${r['notional']:>11,.0f} {r['ann_pct']:>7.2f}%")

if not arb_results:
    print("  No profitable cross-broker arbitrage found (brokers charge spread on swaps)")

# ==============================================================================
# SECTION 3: CORRELATED PAIR CARRY HEDGE (exposure-matched)
# ==============================================================================
print(f"\n{'='*140}")
print("  SECTION 3: CORRELATED FX PAIR CARRY HEDGE — LONG PairA + SHORT PairB, matched exposure")
print("  (e.g., LONG AUDCHF + SHORT EURCHF = capture AUD vs EUR rate differential)")
print(f"{'='*140}")

# FX correlations (approximate, same quote currency pairs tend to be highly correlated)
fx_correlations = [
    # Same CHF quote (very high corr)
    ("AUDCHF_M", "USDCHF_M", 0.85),
    ("AUDCHF_M", "GBPCHF_M", 0.80),
    ("AUDCHF_M", "NZDCHF_M", 0.95),
    ("AUDCHF_M", "CADCHF_M", 0.85),
    ("AUDCHF_M", "EURCHF_M", 0.80),
    ("USDCHF_M", "GBPCHF_M", 0.85),
    ("USDCHF_M", "NZDCHF_M", 0.80),
    ("USDCHF_M", "CADCHF_M", 0.90),
    ("USDCHF_M", "EURCHF_M", 0.90),
    ("GBPCHF_M", "EURCHF_M", 0.95),
    ("GBPCHF_M", "NZDCHF_M", 0.75),
    ("NZDCHF_M", "CADCHF_M", 0.80),
    ("CADCHF_M", "EURCHF_M", 0.85),
    # Same JPY quote (very high corr)
    ("AUDJPY_M", "USDJPY_M", 0.80),
    ("AUDJPY_M", "GBPJPY_M", 0.75),
    ("AUDJPY_M", "NZDJPY_M", 0.95),
    ("AUDJPY_M", "CADJPY_M", 0.85),
    ("AUDJPY_M", "EURJPY_M", 0.80),
    ("USDJPY_M", "GBPJPY_M", 0.90),
    ("USDJPY_M", "EURJPY_M", 0.90),
    ("USDJPY_M", "CADJPY_M", 0.85),
    ("GBPJPY_M", "EURJPY_M", 0.95),
    ("NZDJPY_M", "CADJPY_M", 0.85),
    ("EURJPY_M", "CADJPY_M", 0.80),
    # Cross-category (lower corr = better hedge)
    ("AUDCHF_M", "AUDJPY_M", 0.40),  # AUD base, different quote
    ("USDCHF_M", "USDJPY_M", 0.30),  # USD base, different quote
    ("AUDCHF_M", "EURGBP_M", 0.10),  # Low corr = diversification
    ("USDCHF_M", "EURGBP_M", 0.10),
    ("AUDNZD_M", "EURGBP_M", 0.05),  # Nearly uncorrelated
    # Cross pairs vs carry pairs
    ("AUDCAD_M", "AUDCHF_M", 0.40),
    ("NZDCAD_M", "NZDCHF_M", 0.40),
    ("AUDNZD_M", "AUDCHF_M", 0.30),
]

print(f"\n{'LONG':<12} {'SHORT':<12} {'Corr':>5} {'HedgeR':>8} {'SwapL$/d':>10} {'SwapS$/d':>10} "
      f"{'Net$/d':>10} {'Ann%':>8} {'NotL$':>12} {'NotS$':>12} {'Mismatch':>9}")
print("-" * 140)

hedge_results = []
for key_a, key_b, corr in fx_correlations:
    if key_a not in fx_pairs or key_b not in fx_pairs:
        continue

    info_a = fx_pairs[key_a]
    info_b = fx_pairs[key_b]

    for (long_key, short_key) in [(key_a, key_b), (key_b, key_a)]:
        long_info = fx_pairs[long_key]
        short_info = fx_pairs[short_key]

        # Calculate at matched notional (1 lot short side = reference)
        notional_short = calc_notional(short_info, 1.0)
        notional_long_per_lot = calc_notional(long_info, 1.0)

        if notional_long_per_lot == 0: continue

        hedge_ratio = notional_short / notional_long_per_lot  # lots_long per 1 lot_short

        swap_long_d = calc_swap_per_day_usd(long_info, "LONG", hedge_ratio)
        swap_short_d = calc_swap_per_day_usd(short_info, "SHORT", 1.0)
        net_d = swap_long_d + swap_short_d

        if net_d <= 0: continue

        # At practical min lots
        min_lots_short = short_info["min_lot"]
        min_lots_long_raw = hedge_ratio * min_lots_short
        lot_step = long_info["min_lot"]
        min_lots_long = max(lot_step, round(min_lots_long_raw / lot_step) * lot_step)

        prac_notional_l = calc_notional(long_info, min_lots_long)
        prac_notional_s = calc_notional(short_info, min_lots_short)
        mismatch = abs(prac_notional_l - prac_notional_s) / max(prac_notional_l, prac_notional_s) * 100

        prac_swap_l = calc_swap_per_day_usd(long_info, "LONG", min_lots_long)
        prac_swap_s = calc_swap_per_day_usd(short_info, "SHORT", min_lots_short)
        prac_net = prac_swap_l + prac_swap_s

        ann_pct = (net_d * 365) / notional_short * 100 if notional_short > 0 else 0

        # Get backtest results for both legs
        bt_long = backtest_results.get(long_info["symbol"], {})
        bt_short = backtest_results.get(short_info["symbol"], {})

        hedge_results.append({
            "long": long_key,
            "short": short_key,
            "long_sym": long_info["symbol"],
            "short_sym": short_info["symbol"],
            "corr": corr,
            "hedge_ratio": hedge_ratio,
            "swap_long_d": swap_long_d,
            "swap_short_d": swap_short_d,
            "net_d": net_d,
            "ann_pct": ann_pct,
            "prac_lots_long": min_lots_long,
            "prac_lots_short": min_lots_short,
            "prac_notional_l": prac_notional_l,
            "prac_notional_s": prac_notional_s,
            "mismatch": mismatch,
            "prac_net": prac_net,
            "bidi_long": bt_long.get("naivebidi", 0),
            "bidi_short": bt_short.get("naivebidi", 0),
        })

hedge_results.sort(key=lambda x: -x["net_d"])

for r in hedge_results[:30]:  # Top 30
    print(f"{r['long']:<12} {r['short']:<12} {r['corr']:>5.2f} {r['hedge_ratio']:>8.3f} "
          f"${r['swap_long_d']:>9.2f} ${r['swap_short_d']:>9.2f} "
          f"${r['net_d']:>9.2f} {r['ann_pct']:>7.2f}% "
          f"${r['prac_notional_l']:>11,.0f} ${r['prac_notional_s']:>11,.0f} "
          f"{r['mismatch']:>8.1f}%")

# ==============================================================================
# SECTION 4: RECOMMENDED FX CARRY PORTFOLIO
# ==============================================================================
print(f"\n{'='*140}")
print("  SECTION 4: RECOMMENDED FX CARRY PORTFOLIO — Combined grid + carry rankings")
print("  Combining backtest grid returns with carry income potential")
print(f"{'='*140}")

# For each pair: score = grid_return_risk_adj + carry_bonus
# Risk-adjusted = return / (DD/100 + 0.01)
# Carry bonus = annual carry % * weight
print(f"\n{'Symbol':<10} {'Mode':<10} {'Return':>8} {'DD%':>8} {'RiskAdj':>8} "
      f"{'CarryDir':<6} {'Carry%':>8} {'Score':>8} {'Verdict':<35}")
print("-" * 140)

portfolio = []
for sym, bt in backtest_results.items():
    # Find best Broker swap info
    mk = sym + "_M"
    if mk not in fx_pairs: continue
    info = fx_pairs[mk]

    bidi_ret = bt.get("naivebidi", 0)
    bidi_dd = bt.get("bidi_dd", 0)
    stopout = bt.get("stopout", False)

    if stopout:
        continue

    risk_adj = bidi_ret / (bidi_dd / 100.0 + 0.01)

    # Carry direction and annual %
    swap_l = calc_swap_per_day_usd(info, "LONG", 1.0)
    swap_s = calc_swap_per_day_usd(info, "SHORT", 1.0)
    notional = calc_notional(info, 1.0)

    if swap_l > 0:
        carry_dir = "LONG"
        carry_pct = (swap_l * 365) / notional * 100
    elif swap_s > 0:
        carry_dir = "SHORT"
        carry_pct = (swap_s * 365) / notional * 100
    else:
        carry_dir = "NONE"
        carry_pct = 0

    # Combined score (grid risk-adjusted + carry contribution)
    score = risk_adj + carry_pct * 0.5  # carry weight

    # Determine best mode
    if bidi_ret > max(bt.get("fillup", 0), bt.get("filldown", 0)) and bidi_ret > 1.2:
        mode = "NaiveBidi"
    elif bt.get("fillup", 0) > bt.get("filldown", 0) and bt.get("fillup", 0) > 1.1:
        mode = "FillUp"
    elif bt.get("filldown", 0) > 1.1:
        mode = "FillDown"
    else:
        mode = "NaiveBidi"

    # Verdict
    if score > 20 and bidi_ret > 2.0:
        verdict = "TOP TIER — grid + carry champion"
    elif score > 10 and bidi_ret > 1.5:
        verdict = "EXCELLENT — strong grid, good carry"
    elif score > 5 and bidi_ret > 1.2:
        verdict = "GOOD — viable fill_both + carry"
    elif bidi_ret > 1.1:
        verdict = "MARGINAL — limited grid profit"
    else:
        verdict = "WEAK"

    portfolio.append({
        "symbol": sym, "mode": mode, "bidi_ret": bidi_ret, "bidi_dd": bidi_dd,
        "risk_adj": risk_adj, "carry_dir": carry_dir, "carry_pct": carry_pct,
        "score": score, "verdict": verdict,
        "fillup": bt.get("fillup", 0), "filldown": bt.get("filldown", 0),
    })

portfolio.sort(key=lambda x: -x["score"])

for p in portfolio:
    print(f"{p['symbol']:<10} {p['mode']:<10} {p['bidi_ret']:>7.2f}x {p['bidi_dd']:>7.1f}% "
          f"{p['risk_adj']:>8.2f} {p['carry_dir']:<6} {p['carry_pct']:>7.2f}% "
          f"{p['score']:>8.2f} {p['verdict']:<35}")

# ==============================================================================
# SECTION 5: PRACTICAL EXECUTION SUMMARY
# ==============================================================================
print(f"\n{'='*140}")
print("  SECTION 5: PRACTICAL EXECUTION — $10k account, top picks")
print(f"{'='*140}")

print("""
  STRATEGY RECOMMENDATIONS:

  For FILL_BOTH (bidirectional grid):
  These pairs profit from oscillation alone (zero-swap backtest confirms).
  Adding carry makes them even better.

  For DIRECTIONAL CARRY:
  Go LONG on pairs with positive swap_long (CHF/JPY funded).
  Use FillUp grid to accumulate at lower prices, earn swap on holdings.

  For PORTFOLIO DIVERSIFICATION:
  Combine low-correlation pairs to reduce overall DD.
  e.g., EURJPY + NZDCAD + GBPCHF = uncorrelated carry streams.
""")

# Top 5 summary
print(f"  TOP 5 FX PAIRS FOR FILL_BOTH + CARRY:")
print(f"  {'#':<4} {'Symbol':<10} {'Mode':<12} {'GridReturn':>10} {'DD%':>8} {'CarryDir':<6} {'Carry%/yr':>10}")
print(f"  {'-'*70}")
for i, p in enumerate(portfolio[:5]):
    print(f"  {i+1:<4} {p['symbol']:<10} {p['mode']:<12} {p['bidi_ret']:>9.2f}x {p['bidi_dd']:>7.1f}% "
          f"{p['carry_dir']:<6} {p['carry_pct']:>9.2f}%")

print(f"\n{'='*140}")
print("  ANALYSIS COMPLETE")
print(f"{'='*140}")
