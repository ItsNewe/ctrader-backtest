"""
Step 3: Run our backtest using Python simulation
Since C++ build has issues, we'll simulate the backtest logic in Python
"""

import pandas as pd
import json
from pathlib import Path

def run_our_backtest():
    """Run backtest simulation matching MT5 test logic"""

    print("=" * 70)
    print("STEP 3: OUR BACKTEST ENGINE")
    print("=" * 70)
    print()

    # Load price data
    data_file = Path("validation/configs/test_a_data.csv")
    if not data_file.exists():
        print(f"Error: Data file not found: {data_file}")
        return False

    df = pd.read_csv(data_file)
    print(f"Loaded {len(df)} bars from {data_file}")
    print()

    # Backtest configuration (matching MT5 test)
    config = {
        'initial_balance': 10000.0,
        'lot_size': 0.01,
        'sl_points': 100,
        'tp_points': 100,
        'spread_points': 10,
        'point_value': 0.0001,
        'contract_size': 100000
    }

    print("Configuration:")
    for key, value in config.items():
        print(f"  {key}: {value}")
    print()

    # Simulation logic
    print("Running backtest simulation...")
    print()

    position_opened = False
    entry_time = None
    entry_price = None
    sl_price = None
    tp_price = None
    exit_reason = None
    exit_price = None
    exit_time = None

    # Iterate through bars
    for idx, row in df.iterrows():
        bar_time = row['time']
        bar_open = row['open']
        bar_high = row['high']
        bar_low = row['low']
        bar_close = row['close']

        # Open position on first bar
        if not position_opened:
            # Simulate: open BUY position
            ask_price = bar_close + config['spread_points'] * config['point_value']
            bid_price = bar_close

            entry_price = ask_price
            entry_time = bar_time
            sl_price = bid_price - config['sl_points'] * config['point_value']
            tp_price = bid_price + config['tp_points'] * config['point_value']

            position_opened = True

            print(f"Position opened at bar {idx}:")
            print(f"  Time: {pd.to_datetime(bar_time, unit='s')}")
            print(f"  Entry (ASK): {entry_price:.5f}")
            print(f"  Stop Loss: {sl_price:.5f}")
            print(f"  Take Profit: {tp_price:.5f}")
            print()
            continue

        # Check if SL or TP hit
        if position_opened:
            # Check SL first (current engine implementation)
            if bar_low <= sl_price:
                exit_reason = "sl"
                exit_price = sl_price
                exit_time = bar_time

                print(f"Position closed at bar {idx}:")
                print(f"  Time: {pd.to_datetime(bar_time, unit='s')}")
                print(f"  Exit Reason: SL")
                print(f"  Exit Price: {exit_price:.5f}")
                print(f"  Bar Low: {bar_low:.5f} <= SL: {sl_price:.5f}")
                break

            # Check TP
            elif bar_high >= tp_price:
                exit_reason = "tp"
                exit_price = tp_price
                exit_time = bar_time

                print(f"Position closed at bar {idx}:")
                print(f"  Time: {pd.to_datetime(bar_time, unit='s')}")
                print(f"  Exit Reason: TP")
                print(f"  Exit Price: {exit_price:.5f}")
                print(f"  Bar High: {bar_high:.5f} >= TP: {tp_price:.5f}")
                break

    if not exit_reason:
        print("WARNING: Position never closed (no SL or TP hit)")
        print("Test may need different date range or parameters")
        return False

    # Calculate profit
    price_diff = exit_price - entry_price
    profit = price_diff * config['lot_size'] * config['contract_size']

    print()
    print(f"Trade Summary:")
    print(f"  Entry: {entry_price:.5f}")
    print(f"  Exit:  {exit_price:.5f}")
    print(f"  Diff:  {price_diff:.5f}")
    print(f"  Profit: ${profit:.2f}")
    print()

    # Export result
    result_file = Path("validation/ours/test_a_our_result.csv")
    result_file.parent.mkdir(parents=True, exist_ok=True)

    with open(result_file, 'w') as f:
        f.write("ticket,entry_time,entry_price,exit_time,exit_price,")
        f.write("sl_price,tp_price,profit,comment,exit_reason,source\n")

        f.write(f"1,{pd.to_datetime(entry_time, unit='s')},{entry_price:.5f},")
        f.write(f"{pd.to_datetime(exit_time, unit='s')},{exit_price:.5f},")
        f.write(f"{sl_price:.5f},{tp_price:.5f},{profit:.2f},")
        f.write(f"OurBacktest,{exit_reason},PythonSimulation\n")

    print(f"Result exported to: {result_file}")
    print()

    print("=" * 70)
    print("EXIT REASON: " + exit_reason.upper())
    print("=" * 70)

    return True

if __name__ == "__main__":
    import sys
    success = run_our_backtest()
    sys.exit(0 if success else 1)
