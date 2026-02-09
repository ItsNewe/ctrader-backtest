"""
Fetch XAUUSD specifications from MT5 including margin rate
"""

import MetaTrader5 as mt5
from pathlib import Path

def main():
    print("=== XAUUSD Specification Fetcher ===\n")

    # Initialize MT5
    mt5_path = r"C:\Program Files\ MetaTrader 5\terminal64.exe"
    if not mt5.initialize(path=mt5_path):
        print(f"MT5 initialization failed: {mt5.last_error()}")
        return

    print("MT5 initialized successfully\n")

    # Get account info
    account_info = mt5.account_info()
    if account_info:
        print(f"Account: {account_info.login}")
        print(f"Server: {account_info.server}")
        print(f"Leverage: 1:{account_info.leverage}")
        print(f"Currency: {account_info.currency}\n")

    # Get XAUUSD symbol info
    symbol = "XAUUSD"
    symbol_info = mt5.symbol_info(symbol)

    if symbol_info is None:
        print(f"Failed to get {symbol} info: {mt5.last_error()}")
        mt5.shutdown()
        return

    # Get margin rate
    initial_margin_rate = 0.0
    maintenance_margin_rate = 0.0

    # Try to get margin rate
    try:
        from ctypes import c_double, byref
        initial = c_double()
        maintenance = c_double()

        # Call SymbolInfoMarginRate equivalent
        # Note: Python MT5 doesn't have direct SymbolInfoMarginRate, need workaround
        print("Note: Python MT5 doesn't expose SymbolInfoMarginRate directly")
        print("Using alternative margin info from symbol_info...\n")
    except:
        pass

    print(f"=== {symbol} Specifications ===\n")
    print(f"Contract Size: {symbol_info.trade_contract_size}")
    print(f"Min Volume: {symbol_info.volume_min}")
    print(f"Max Volume: {symbol_info.volume_max}")
    print(f"Volume Step: {symbol_info.volume_step}")
    print(f"Point: {symbol_info.point}")
    print(f"Digits: {symbol_info.digits}")
    print(f"\n=== Margin Information ===")
    print(f"Trade Calc Mode: {symbol_info.trade_calc_mode}")

    # Trade calc modes:
    # SYMBOL_CALC_MODE_FOREX = 0
    # SYMBOL_CALC_MODE_FUTURES = 1
    # SYMBOL_CALC_MODE_CFD = 2
    # SYMBOL_CALC_MODE_CFDINDEX = 3
    # SYMBOL_CALC_MODE_CFDLEVERAGE = 4
    # SYMBOL_CALC_MODE_FOREX_NO_LEVERAGE = 5

    calc_mode_names = {
        0: "FOREX",
        1: "FUTURES",
        2: "CFD",
        3: "CFD_INDEX",
        4: "CFD_LEVERAGE",
        5: "FOREX_NO_LEVERAGE"
    }

    print(f"Calc Mode Name: {calc_mode_names.get(symbol_info.trade_calc_mode, 'UNKNOWN')}")
    print(f"Margin Initial: {symbol_info.margin_initial}")
    print(f"Margin Maintenance: {symbol_info.margin_maintenance}")
    print(f"Margin Hedged: {symbol_info.margin_hedged}")
    print(f"Margin Limit: {getattr(symbol_info, 'margin_limit', 'N/A')}")

    # Calculate effective margin rate
    # For CFD_LEVERAGE mode: Margin = (Lots * ContractSize * Price) / Leverage * MarginRate
    # For FOREX mode: Margin = Lots * ContractSize / Leverage * MarginRate

    print(f"\n=== Swap Information ===")
    print(f"Swap Long: {symbol_info.swap_long}")
    print(f"Swap Short: {symbol_info.swap_short}")
    print(f"Swap Mode: {symbol_info.swap_mode}")

    print(f"\n=== Price Information ===")
    print(f"Bid: {symbol_info.bid}")
    print(f"Ask: {symbol_info.ask}")
    print(f"Spread: {symbol_info.spread}")

    # Get margin required for 1 lot at current price
    print(f"\n=== Margin Calculation Test ===")

    # Calculate margin for 1 lot BUY order
    margin_required = mt5.order_calc_margin(
        mt5.ORDER_TYPE_BUY,
        symbol,
        1.0,  # 1 lot
        symbol_info.ask
    )

    if margin_required is not None:
        print(f"Margin required for 1.00 lot BUY at {symbol_info.ask}: ${margin_required:.2f}")

        # Calculate implied margin rate
        # For FOREX mode: margin = lots * contract_size / leverage * margin_rate
        # So: margin_rate = margin * leverage / (lots * contract_size)

        if symbol_info.trade_calc_mode == 0:  # FOREX
            implied_rate = (margin_required * account_info.leverage) / (1.0 * symbol_info.trade_contract_size)
            print(f"Implied margin rate (FOREX): {implied_rate:.6f}")
        elif symbol_info.trade_calc_mode == 4:  # CFD_LEVERAGE
            implied_rate = (margin_required * account_info.leverage) / (1.0 * symbol_info.trade_contract_size * symbol_info.ask)
            print(f"Implied margin rate (CFD_LEVERAGE): {implied_rate:.6f}")
    else:
        print(f"Failed to calculate margin: {mt5.last_error()}")

    # Calculate for 0.01 lot
    margin_required_min = mt5.order_calc_margin(
        mt5.ORDER_TYPE_BUY,
        symbol,
        0.01,  # min lot
        symbol_info.ask
    )

    if margin_required_min is not None:
        print(f"Margin required for 0.01 lot BUY at {symbol_info.ask}: ${margin_required_min:.2f}")

    # Save specs to file
    output_file = Path("validation/XAUUSD_MT5_SPECS.txt")
    with open(output_file, 'w') as f:
        f.write(f"=== XAUUSD MT5 Specifications ===\n")
        f.write(f"Fetched: {mt5.account_info().company} - Account {mt5.account_info().login}\n")
        f.write(f"Leverage: 1:{account_info.leverage}\n\n")

        f.write(f"Symbol: {symbol}\n")
        f.write(f"Contract Size: {symbol_info.trade_contract_size}\n")
        f.write(f"Min Volume: {symbol_info.volume_min}\n")
        f.write(f"Max Volume: {symbol_info.volume_max}\n")
        f.write(f"Digits: {symbol_info.digits}\n")
        f.write(f"Point: {symbol_info.point}\n\n")

        f.write(f"Trade Calc Mode: {symbol_info.trade_calc_mode} ({calc_mode_names.get(symbol_info.trade_calc_mode, 'UNKNOWN')})\n")
        f.write(f"Margin Initial: {symbol_info.margin_initial}\n")
        f.write(f"Margin Maintenance: {symbol_info.margin_maintenance}\n\n")

        if margin_required is not None:
            f.write(f"Margin for 1.00 lot: ${margin_required:.2f}\n")
            if symbol_info.trade_calc_mode == 0:
                f.write(f"Implied Margin Rate: {implied_rate:.6f}\n")

    print(f"\n✓ Specifications saved to: {output_file}")

    mt5.shutdown()

if __name__ == "__main__":
    main()
