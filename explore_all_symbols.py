"""Explore all available symbols on both Broker and Grid brokers."""
import MetaTrader5 as mt5
import sys

if sys.platform == 'win32':
    sys.stdout.reconfigure(encoding='utf-8')

def explore_broker(name, mt5_path):
    if not mt5.initialize(path=mt5_path):
        print(f"[ERROR] Failed to init {name}")
        return

    acc = mt5.account_info()
    print(f"\n{'='*100}")
    print(f"  {name} — Account {acc.login} on {acc.server}")
    print(f"{'='*100}")

    # Try missing symbols
    missing = ['Copper-c', 'COPPER-C', 'Copper-C', 'COPPER', 'HG-C', 'XCUUSD', 'XCU',
               'Cotton-c', 'COTTON-C', 'Cotton-C', 'CT-C', 'COTTONC', 'XCTUSD']
    print("\n  Searching for missing symbols:")
    for sym in missing:
        info = mt5.symbol_info(sym)
        if info is not None:
            print(f"    FOUND: {sym} -> {info.description}, cs={info.trade_contract_size}")

    # Get all symbols
    symbols = mt5.symbols_get()
    if symbols is None:
        print("  No symbols found")
        mt5.shutdown()
        return

    # Categorize
    categories = {}
    for s in symbols:
        path = s.path
        sep = "\\"
        parts = path.split(sep)
        cat = parts[0] if len(parts) > 1 else "Other"
        if cat not in categories:
            categories[cat] = []
        categories[cat].append(s)

    print(f"\n  Total symbols: {len(symbols)}")
    print(f"  Categories: {', '.join(sorted(categories.keys()))}")

    # Print all categories
    for cat in sorted(categories.keys()):
        syms = sorted(categories[cat], key=lambda x: x.name)
        print(f"\n  [{cat}] ({len(syms)} symbols):")
        for s in syms:
            desc = s.description[:45] if s.description else ""
            cs = s.trade_contract_size
            vm = s.volume_min
            swap_l = s.swap_long
            swap_s = s.swap_short
            spread = s.spread
            print(f"    {s.name:<16} {desc:<47} cs={cs:<10.1f} vol_min={vm:<6} "
                  f"swap_l={swap_l:<10.2f} swap_s={swap_s:<10.2f} spread={spread}")

    mt5.shutdown()


def main():
    broker_path = r"C:\Program Files\ MT5 Terminal\terminal64.exe"
    grid_path = r"C:\Program Files\ MetaTrader 5\terminal64.exe"

    if len(sys.argv) >= 2:
        broker = sys.argv[1].lower()
        if broker == "broker":
            explore_broker("", broker_path)
        elif broker == "grid":
            explore_broker("", grid_path)
        return

    explore_broker("", broker_path)
    explore_broker("", grid_path)


if __name__ == "__main__":
    main()
