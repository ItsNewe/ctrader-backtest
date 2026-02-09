"""
Query ALL commodity instrument margin specifications from both Broker and Grid.

Queries per instrument:
- trade_calc_mode (FOREX=0, FUTURES=1, CFD=2, CFD_INDEX=3, CFD_LEVERAGE=4, FOREX_NO_LEVERAGE=5)
- margin_initial, margin_maintenance
- contract_size, volume_min, volume_step, point, digits
- swap_long, swap_short, swap_mode, swap_rollover3days
- bid, ask (current price)
- order_calc_margin() for actual margin$ verification

Derives effective margin_rate per calc_mode formula.
Outputs C++ struct array for test_fill_both_grid.cpp.
"""
import MetaTrader5 as mt5
import sys
import json

if sys.platform == 'win32':
    sys.stdout.reconfigure(encoding='utf-8')

CALC_MODE_NAMES = {
    0: "FOREX",
    1: "FUTURES",
    2: "CFD",
    3: "CFD_INDEX",
    4: "CFD_LEVERAGE",
    5: "FOREX_NO_LEVERAGE",
}

BROKER_PATH = r"C:\Program Files\ MT5 Terminal\terminal64.exe"
GRID_PATH = r"C:\Program Files\ MetaTrader 5\terminal64.exe"

BROKER_SYMBOLS = [
    "CL-OIL", "UKOUSD", "UKOUSDft", "USOUSD",
    "NG-C", "GASOIL-C", "GAS-C",
    "Soybean-C", "Sugar-C", "COPPER-C", "Cotton-C",
    "Cocoa-C", "Coffee-C", "OJ-C", "Wheat-C",
    "XPTUSD", "XPDUSD",
]

GRID_SYMBOLS = [
    "XBRUSD", "XNGUSD", "XTIUSD",
    "CORN", "COTTON", "SOYBEAN", "SUGARRAW", "WHEAT",
    "COFARA", "COFROB", "OJ", "SUGAR",
    "UKCOCOA", "USCOCOA",
    "XCUUSD", "XALUSD", "XPTUSD",
    "XAUUSD", "XAGUSD",
]


def derive_margin_rate(calc_mode, margin_actual, lot_size, contract_size, price, leverage):
    """Derive effective margin_rate from actual margin$ and calc_mode formula."""
    if margin_actual is None or margin_actual <= 0:
        return 1.0  # fallback

    if calc_mode == 0:  # FOREX: margin = lots * cs / leverage * rate
        denominator = lot_size * contract_size / leverage
        if denominator > 0:
            return margin_actual / denominator
    elif calc_mode == 1:  # FUTURES: margin = lots * margin_initial
        # margin_rate not used; margin_initial_fixed = margin_actual / lots
        return margin_actual / lot_size if lot_size > 0 else 0.0
    elif calc_mode in (2, 3):  # CFD / CFD_INDEX: margin = lots * cs * price * rate
        denominator = lot_size * contract_size * price
        if denominator > 0:
            return margin_actual / denominator
    elif calc_mode == 4:  # CFD_LEVERAGE: margin = lots * cs * price / leverage * rate
        denominator = lot_size * contract_size * price / leverage
        if denominator > 0:
            return margin_actual / denominator
    elif calc_mode == 5:  # FOREX_NO_LEVERAGE: margin = lots * cs * rate
        denominator = lot_size * contract_size
        if denominator > 0:
            return margin_actual / denominator

    return 1.0  # fallback


def query_broker(mt5_path, broker_name, symbols):
    """Query one broker for all symbols. Returns list of dicts."""
    results = []

    if not mt5.initialize(path=mt5_path):
        print(f"[ERROR] Failed to init {broker_name}: {mt5.last_error()}")
        return results

    acc = mt5.account_info()
    leverage = acc.leverage if acc else 500
    print(f"\n{'='*120}")
    print(f"  {broker_name} — Account: {acc.login} on {acc.server}, Leverage: 1:{leverage}")
    print(f"{'='*120}\n")

    header = (f"{'Symbol':<14} {'CalcMode':<16} {'CS':>10} {'Point':>12} {'Digits':>6} "
              f"{'VolMin':>8} {'VolStep':>8} {'Bid':>12} {'Ask':>12} "
              f"{'MargInit':>10} {'MargMaint':>10} {'Margin$':>12} {'Rate':>10}")
    print(header)
    print("-" * len(header))

    for sym in symbols:
        info = mt5.symbol_info(sym)
        if info is None:
            print(f"  {sym}: NOT FOUND")
            continue

        # Ensure symbol is visible for margin queries
        if not info.visible:
            mt5.symbol_select(sym, True)
            info = mt5.symbol_info(sym)

        # Query actual margin for min_lot at ask price
        ask_price = info.ask if info.ask > 0 else info.bid
        margin_actual = mt5.order_calc_margin(
            mt5.ORDER_TYPE_BUY,
            sym,
            info.volume_min,
            ask_price
        )

        calc_mode = info.trade_calc_mode
        calc_mode_name = CALC_MODE_NAMES.get(calc_mode, f"UNKNOWN({calc_mode})")

        # Derive margin_rate
        if calc_mode == 1:  # FUTURES
            margin_initial_fixed = margin_actual / info.volume_min if (margin_actual and info.volume_min > 0) else info.margin_initial
            margin_rate = 1.0  # not used for FUTURES
        else:
            margin_initial_fixed = 0.0
            margin_rate = derive_margin_rate(
                calc_mode, margin_actual,
                info.volume_min, info.trade_contract_size,
                ask_price, leverage
            )

        rec = {
            "symbol": sym,
            "broker": broker_name,
            "calc_mode": calc_mode,
            "calc_mode_name": calc_mode_name,
            "contract_size": info.trade_contract_size,
            "point": info.point,
            "digits": info.digits,
            "volume_min": info.volume_min,
            "volume_step": info.volume_step,
            "volume_max": info.volume_max,
            "leverage": leverage,
            "bid": info.bid,
            "ask": info.ask,
            "margin_initial": info.margin_initial,
            "margin_maintenance": info.margin_maintenance,
            "margin_actual": margin_actual,
            "margin_rate": margin_rate,
            "margin_initial_fixed": margin_initial_fixed,
            "swap_long": info.swap_long,
            "swap_short": info.swap_short,
            "swap_mode": info.swap_mode,
            "swap_rollover3days": info.swap_rollover3days,
        }
        results.append(rec)

        margin_str = f"${margin_actual:.2f}" if margin_actual else "FAIL"
        print(f"{sym:<14} {calc_mode_name:<16} {info.trade_contract_size:>10.1f} {info.point:>12.6f} {info.digits:>6} "
              f"{info.volume_min:>8.2f} {info.volume_step:>8.2f} {info.bid:>12.4f} {info.ask:>12.4f} "
              f"{info.margin_initial:>10.2f} {info.margin_maintenance:>10.2f} {margin_str:>12} {margin_rate:>10.6f}")

    mt5.shutdown()
    return results


def print_cpp_output(all_results):
    """Print C++ struct array for test_fill_both_grid.cpp."""
    print(f"\n\n{'='*120}")
    print("  C++ InstrumentConfig array — paste into test_fill_both_grid.cpp")
    print(f"{'='*120}\n")

    # Map symbols to tick file paths
    broker_tick_files = {
        "CL-OIL": "Broker/CL-OIL_TICKS_FULL.csv",
        "UKOUSD": "Broker/UKOUSD_TICKS_FULL.csv",
        "UKOUSDft": "Broker/UKOUSDft_TICKS_FULL.csv",
        "USOUSD": "Broker/USOUSD_TICKS_FULL.csv",
        "NG-C": "Broker/NG-C_TICKS_FULL.csv",
        "GASOIL-C": "Broker/GASOIL-C_TICKS_FULL.csv",
        "GAS-C": "Broker/GAS-C_TICKS_FULL.csv",
        "Soybean-C": "Broker/Soybean-C_TICKS_FULL.csv",
        "Sugar-C": "Broker/Sugar-C_TICKS_FULL.csv",
        "COPPER-C": "Broker/COPPER-C_TICKS_FULL.csv",
        "Cotton-C": "Broker/Cotton-C_TICKS_FULL.csv",
        "Cocoa-C": "Broker/Cocoa-C_TICKS_FULL.csv",
        "Coffee-C": "Broker/Coffee-C_TICKS_FULL.csv",
        "OJ-C": "Broker/OJ-C_TICKS_FULL.csv",
        "Wheat-C": "Broker/Wheat-C_TICKS_FULL.csv",
        "XPTUSD": "Broker/XPTUSD_TICKS_FULL.csv",
        "XPDUSD": "Broker/XPDUSD_TICKS_FULL.csv",
    }
    grid_tick_files = {
        "XBRUSD": "Grid/XBRUSD_TICKS_FULL.csv",
        "XNGUSD": "Grid/XNGUSD_TICKS_FULL.csv",
        "XTIUSD": "Grid/XTIUSD_TICKS_FULL.csv",
        "CORN": "Grid/CORN_TICKS_FULL.csv",
        "COTTON": "Grid/COTTON_TICKS_FULL.csv",
        "SOYBEAN": "Grid/SOYBEAN_TICKS_FULL.csv",
        "SUGARRAW": "Grid/SUGARRAW_TICKS_FULL.csv",
        "WHEAT": "Grid/WHEAT_TICKS_FULL.csv",
        "COFARA": "Grid/COFARA_TICKS_FULL.csv",
        "COFROB": "Grid/COFROB_TICKS_FULL.csv",
        "OJ": "Grid/OJ_TICKS_FULL.csv",
        "SUGAR": "Grid/SUGAR_TICKS_FULL.csv",
        "UKCOCOA": "Grid/UKCOCOA_TICKS_FULL.csv",
        "USCOCOA": "Grid/USCOCOA_TICKS_FULL.csv",
        "XCUUSD": "Grid/XCUUSD_TICKS_FULL.csv",
        "XALUSD": "Grid/XALUSD_TICKS_FULL.csv",
        "XPTUSD": "Grid/XPTUSD_TICKS_FULL.csv",
        "XAUUSD": "Grid/XAUUSD_TICKS_2025.csv",
        "XAGUSD": "Grid/XAGUSD_TICKS_FULL.csv",  # may not exist
    }

    all_tick_files = {**broker_tick_files, **grid_tick_files}

    print("const InstrumentConfig INSTRUMENTS[] = {")
    for rec in all_results:
        sym = rec["symbol"]
        broker = rec["broker"]

        # Resolve tick file — for Grid XPTUSD, use the Grid path
        if broker == "Grid":
            tick_file = grid_tick_files.get(sym, f"Grid/{sym}_TICKS_FULL.csv")
        else:
            tick_file = broker_tick_files.get(sym, f"Broker/{sym}_TICKS_FULL.csv")

        cm = rec["calc_mode"]
        mr = rec["margin_rate"]
        mif = rec["margin_initial_fixed"]
        cs = rec["contract_size"]
        pip = rec["point"]
        vmin = rec["volume_min"]
        vstep = rec["volume_step"]
        digits = rec["digits"]
        lev = rec["leverage"]

        print(f'    {{"{sym}", "{broker}", "{tick_file}", '
              f'{cs}, {pip}, {vmin}, {vstep}, {digits}, {lev}.0, '
              f'TradeCalcMode({cm}), {mr:.8f}, {mif:.2f}}},')

    print("};")
    print(f"constexpr size_t NUM_INSTRUMENTS = sizeof(INSTRUMENTS) / sizeof(INSTRUMENTS[0]);")

    # Also print JSON for reference
    print(f"\n\n{'='*120}")
    print("  JSON reference data")
    print(f"{'='*120}\n")
    print(json.dumps(all_results, indent=2))


def main():
    print("=" * 120)
    print("  QUERY ALL COMMODITY INSTRUMENT MARGIN SPECS")
    print("  For fill_both grid retest — need calc_mode, margin_rate, margin_initial per instrument")
    print("=" * 120)

    all_results = []

    # Query Broker first
    broker_results = query_broker(BROKER_PATH, "Broker", BROKER_SYMBOLS)
    all_results.extend(broker_results)

    # Query Grid
    grid_results = query_broker(GRID_PATH, "Grid", GRID_SYMBOLS)
    all_results.extend(grid_results)

    # Print summary
    print(f"\n\n{'='*120}")
    print(f"  SUMMARY: {len(all_results)} instruments queried")
    print(f"{'='*120}\n")

    # Group by calc_mode
    modes = {}
    for rec in all_results:
        mode = rec["calc_mode_name"]
        modes.setdefault(mode, []).append(rec["symbol"])

    for mode, syms in sorted(modes.items()):
        print(f"  {mode}: {', '.join(syms)}")

    # Print C++ output
    print_cpp_output(all_results)


if __name__ == "__main__":
    main()
