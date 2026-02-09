"""Query both brokers for FX carry trade pair specifications."""
import MetaTrader5 as mt5
import sys

if sys.platform == 'win32':
    sys.stdout.reconfigure(encoding='utf-8')

fx_pairs = [
    "AUDNZD", "EURGBP", "AUDCHF", "GBPCHF", "USDCHF", "NZDCHF", "CADCHF",
    "AUDJPY", "NZDJPY", "GBPJPY", "USDJPY", "EURJPY", "CADJPY",
    "EURCHF", "AUDCAD", "NZDCAD",
    "USDMXN", "USDZAR", "EURTRY", "USDTRY",
    # Also the commodity pair we need for the EA
    "UKOUSD", "GAS-C",
]

def query_broker(name, path):
    if not mt5.initialize(path=path):
        print(f"[ERROR] Failed to init {name}")
        return
    acc = mt5.account_info()
    print(f"\n{'='*130}")
    print(f"  {name} — Account: {acc.login} on {acc.server}")
    print(f"{'='*130}")
    print(f"{'Symbol':<12} {'ContractSz':>10} {'PipSize':>10} {'Digits':>6} "
          f"{'SwapLong':>12} {'SwapShort':>12} {'SwapMode':>8} {'Swap3d':>6} "
          f"{'VolMin':>8} {'Spread':>8} {'Description'}")
    print("-" * 130)

    for sym in fx_pairs:
        info = mt5.symbol_info(sym)
        if info is None:
            # Try with suffix
            for suffix in ['', '.s', 'm', '.i']:
                info = mt5.symbol_info(sym + suffix)
                if info: break
            if info is None:
                print(f"  {sym:<12} NOT FOUND")
                continue
        if not info.visible:
            mt5.symbol_select(info.name, True)
            info = mt5.symbol_info(info.name)

        # Get current tick for spread
        tick = mt5.symbol_info_tick(info.name)
        spread_pts = 0
        if tick:
            spread_pts = round((tick.ask - tick.bid) / info.point)

        print(f"{info.name:<12} {info.trade_contract_size:>10.0f} {info.point:>10.5f} {info.digits:>6} "
              f"{info.swap_long:>12.2f} {info.swap_short:>12.2f} {info.swap_mode:>8} {info.swap_rollover3days:>6} "
              f"{info.volume_min:>8.2f} {spread_pts:>8.0f} {info.description}")

    # Calculate daily swap income per 1.0 lot for key pairs
    print(f"\n  --- Daily Swap Income per 1.0 Lot (points × pip_value) ---")
    for sym in fx_pairs:
        info = mt5.symbol_info(sym)
        if info is None:
            for suffix in ['', '.s', 'm', '.i']:
                info = mt5.symbol_info(sym + suffix)
                if info: break
        if info is None:
            continue

        # pip_value depends on account currency (USD)
        # For xxx/USD pairs: pip_value = contract_size * point
        # For xxx/yyy pairs: pip_value = contract_size * point / ask_yyy_usd (approximately)
        tick = mt5.symbol_info_tick(info.name)
        if not tick:
            continue

        # Approximate pip value in USD for 1 lot
        # swap_mode 1 = points: swap_value = swap * point * contract_size
        # swap_mode 2 = money: swap = direct USD amount
        if info.swap_mode == 1:  # Points
            pip_val = info.trade_contract_size * info.point
            swap_long_usd = info.swap_long * pip_val
            swap_short_usd = info.swap_short * pip_val
        elif info.swap_mode == 2:  # Money
            swap_long_usd = info.swap_long
            swap_short_usd = info.swap_short
        else:
            swap_long_usd = info.swap_long * info.trade_contract_size * info.point
            swap_short_usd = info.swap_short * info.trade_contract_size * info.point

        if abs(swap_long_usd) > 0.001 or abs(swap_short_usd) > 0.001:
            annual_long = swap_long_usd * 365
            annual_short = swap_short_usd * 365
            notional = info.trade_contract_size * tick.bid
            pct_long = (annual_long / notional * 100) if notional > 0 else 0
            pct_short = (annual_short / notional * 100) if notional > 0 else 0
            print(f"  {info.name:<12} Long: ${swap_long_usd:>8.2f}/day ({pct_long:>+6.2f}%/yr) | "
                  f"Short: ${swap_short_usd:>8.2f}/day ({pct_short:>+6.2f}%/yr) | "
                  f"Notional: ${notional:>12,.0f}")

    mt5.shutdown()

# Query both brokers
query_broker("BROKER MARKETS",
    r"C:\Program Files\ MT5 Terminal\terminal64.exe")
query_broker("GRID MARKETS",
    r"C:\Program Files\ MetaTrader 5\terminal64.exe")
