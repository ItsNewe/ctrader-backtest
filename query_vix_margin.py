"""Query VIX margin calculation mode and full symbol specs from Broker MT5"""
import MetaTrader5 as mt5
import sys

BROKER_PATH = r"C:\Program Files\ MT5 Terminal\terminal64.exe"

if not mt5.initialize(path=BROKER_PATH):
    print(f"MT5 init failed: {mt5.last_error()}")
    sys.exit(1)

info = mt5.symbol_info("VIX")
if info is None:
    print("VIX symbol not found")
    mt5.shutdown()
    sys.exit(1)

print("=== VIX Full Symbol Specification ===")
print(f"name:              {info.name}")
print(f"description:       {info.description}")
print(f"path:              {info.path}")
print()
print("--- MARGIN CALCULATION ---")
print(f"trade_calc_mode:   {info.trade_calc_mode}  (0=FOREX, 1=CFD, 2=CFD_INDEX, 3=CFD_LEVERAGE, 4=FUTURES)")
print(f"margin_initial:    {info.margin_initial}")
print(f"margin_maintenance:{info.margin_maintenance}")
print()
print("--- CONTRACT ---")
print(f"contract_size:     {info.trade_contract_size}")
print(f"digits:            {info.digits}")
print(f"point:             {info.point}")
print(f"tick_size:         {info.trade_tick_size}")
print(f"tick_value:        {info.trade_tick_value}")
print(f"tick_value_profit: {info.trade_tick_value_profit}")
print(f"tick_value_loss:   {info.trade_tick_value_loss}")
print()
print("--- VOLUMES ---")
print(f"volume_min:        {info.volume_min}")
print(f"volume_max:        {info.volume_max}")
print(f"volume_step:       {info.volume_step}")
print()
print("--- SWAP ---")
print(f"swap_mode:         {info.swap_mode}  (0=DISABLED, 1=POINTS, 2=CURRENCY_SYMBOL)")
print(f"swap_long:         {info.swap_long}")
print(f"swap_short:        {info.swap_short}")
print(f"swap_rollover3days:{info.swap_rollover3days}")
print()
print("--- SPREAD ---")
print(f"spread:            {info.spread}")
print(f"spread_float:      {info.spread_float}")
print()
print("--- PRICES ---")
print(f"bid:               {info.bid}")
print(f"ask:               {info.ask}")
print(f"last:              {info.last}")
print()
print("--- LEVERAGE TEST ---")
# Calculate what MT5 would charge for 0.01 lot
# For CFD: margin = (lots * contract_size * price) / leverage
# But we need to check if there's a special margin requirement
price = info.ask
cs = info.trade_contract_size
print(f"Notional (0.01 lot): {0.01 * cs * price:.2f}")
print(f"Margin at 1:500:     {0.01 * cs * price / 500:.2f}")
print(f"Margin at 1:100:     {0.01 * cs * price / 100:.2f}")
print(f"Margin at 1:50:      {0.01 * cs * price / 50:.2f}")

# Check actual margin requirement
margin_check = mt5.order_calc_margin(mt5.ORDER_TYPE_BUY, "VIX", 0.01, price)
print(f"MT5 order_calc_margin(BUY, 0.01): ${margin_check:.4f}" if margin_check else "order_calc_margin failed")

margin_check2 = mt5.order_calc_margin(mt5.ORDER_TYPE_SELL, "VIX", 0.01, info.bid)
print(f"MT5 order_calc_margin(SELL, 0.01): ${margin_check2:.4f}" if margin_check2 else "order_calc_margin failed")

margin_check3 = mt5.order_calc_margin(mt5.ORDER_TYPE_BUY, "VIX", 0.10, price)
print(f"MT5 order_calc_margin(BUY, 0.10): ${margin_check3:.4f}" if margin_check3 else "order_calc_margin failed")

margin_check4 = mt5.order_calc_margin(mt5.ORDER_TYPE_BUY, "VIX", 1.00, price)
print(f"MT5 order_calc_margin(BUY, 1.00): ${margin_check4:.4f}" if margin_check4 else "order_calc_margin failed")

# Reverse-engineer leverage from margin
if margin_check and margin_check > 0:
    implied_leverage = (0.01 * cs * price) / margin_check
    print(f"\nImplied leverage: 1:{implied_leverage:.1f}")
    print(f"Implied margin rate: {1.0/implied_leverage*100:.4f}%")

# Check profit calculation
profit_check = mt5.order_calc_profit(mt5.ORDER_TYPE_BUY, "VIX", 0.01, price, price + 1.0)
print(f"\nProfit for +$1.00 move (0.01 lot BUY): ${profit_check:.2f}" if profit_check else "profit calc failed")

profit_check2 = mt5.order_calc_profit(mt5.ORDER_TYPE_SELL, "VIX", 0.01, info.bid, info.bid - 1.0)
print(f"Profit for -$1.00 move (0.01 lot SELL): ${profit_check2:.2f}" if profit_check2 else "profit calc failed")

mt5.shutdown()
