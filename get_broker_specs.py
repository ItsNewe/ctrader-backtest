"""Query  MT5 for instrument specifications."""
import MetaTrader5 as mt5
import sys
import json

if sys.platform == 'win32':
    sys.stdout.reconfigure(encoding='utf-8')

broker_path = r"C:\Program Files\ MT5 Terminal\terminal64.exe"

symbols_to_check = [
    "COPPER-C", "Cotton-C", "Cocoa-C", "Coffee-C", "OJ-C", "Wheat-C",
    "XPTUSD", "XPDUSD",
    # Also re-check existing ones for completeness
    "CL-OIL", "UKOUSD", "UKOUSDft", "USOUSD", "NG-C", "GASOIL-C", "GAS-C",
    "Soybean-C", "Sugar-C",
]

if not mt5.initialize(path=broker_path):
    print("[ERROR] Failed to init Broker")
    sys.exit(1)

acc = mt5.account_info()
print(f"Account: {acc.login} on {acc.server}")
print()

print(f"{'Symbol':<14} {'ContractSz':>10} {'PipSize':>10} {'Digits':>6} "
      f"{'SwapLong':>10} {'SwapShort':>10} {'SwapMode':>8} {'Swap3d':>6} "
      f"{'VolMin':>8} {'VolStep':>8} {'Description'}")
print("-" * 130)

for sym in symbols_to_check:
    info = mt5.symbol_info(sym)
    if info is None:
        print(f"  {sym}: NOT FOUND")
        continue
    print(f"{info.name:<14} {info.trade_contract_size:>10.1f} {info.point:>10.5f} {info.digits:>6} "
          f"{info.swap_long:>10.2f} {info.swap_short:>10.2f} {info.swap_mode:>8} {info.swap_rollover3days:>6} "
          f"{info.volume_min:>8.2f} {info.volume_step:>8.2f} {info.description}")

mt5.shutdown()
