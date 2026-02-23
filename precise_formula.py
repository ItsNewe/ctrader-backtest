import math

print("=" * 70)
print("PRECISE FORMULA FOR SINGLE-SHOT STRATEGIES")
print("=" * 70)
print()

C = 100.0
current_ask = 2700.0
survive = 4.0
end_price = current_ask * (100 - survive) / 100
distance = current_ask - end_price

open_prices = [2600 + i*10 for i in range(10)]
vol_per = 0.5
V = len(open_prices) * vol_per

loss_approx = V * distance * C
print(f"Current ask: {current_ask}, end_price: {end_price}, distance: {distance}")
print(f"Open prices: {open_prices}")
print(f"V = {V} lots")
print()

W = sum(vol_per * p for p in open_prices)
loss_exact = C * (W - V * end_price)
print(f"Loss (approx): ${loss_approx:.2f}")
print(f"Loss (exact):  ${loss_exact:.2f}")
print(f"Overestimate:  ${loss_approx - loss_exact:.2f} ({(loss_approx/loss_exact - 1)*100:.1f}%)")
print()

equity = 10000.0
used_margin = 0
SO = 0
spread = 0.30
margin_rate = 0
leverage = 500

eq_at_target_approx = equity - loss_approx
eq_at_target_exact = equity - loss_exact

print(f"Equity at target (approx): ${eq_at_target_approx:.2f}")
print(f"Equity at target (exact):  ${eq_at_target_exact:.2f}")
print()

ts_approx = eq_at_target_approx / (C * (distance + spread))
ts_exact = eq_at_target_exact / (C * (distance + spread))
print(f"Trade size (approx): {ts_approx:.4f} lots")
print(f"Trade size (exact):  {ts_exact:.4f} lots")
print(f"Difference: {ts_exact - ts_approx:.4f} lots ({(ts_exact/ts_approx - 1)*100:.1f}% more)")
print()

new_trade_loss = ts_exact * distance * C
new_spread_cost = ts_exact * spread * C
remaining = equity - loss_exact - new_trade_loss - new_spread_cost
print(f"Verification with exact sizing:")
print(f"  Existing loss: ${loss_exact:.2f}")
print(f"  New trade loss: ${new_trade_loss:.2f}")
print(f"  Spread cost: ${new_spread_cost:.2f}")
print(f"  Remaining equity: ${remaining:.2f}")
ok_str = "OK" if abs(remaining) < 1 else "PROBLEM"
print(f"  Should be ~0 (stop-out): {ok_str}")
print()

new_trade_loss_a = ts_approx * distance * C
new_spread_cost_a = ts_approx * spread * C
remaining_a = equity - loss_exact - new_trade_loss_a - new_spread_cost_a
print(f"Verification with approximate sizing:")
print(f"  Remaining equity: ${remaining_a:.2f}")
print(f"  Wasted: ${remaining_a:.2f}")
print()

print("=" * 70)
print("IMPLEMENTATION: WHAT NEEDS TO CHANGE")
print("=" * 70)
print()
print("Current code tracks: g_volume_of_open_trades (V)")
print("Need to also track: g_value_weighted_price (W = sum(vol_i * price_i))")
print()
print("In the position scan loop, add:")
print("  g_value_weighted_price += lots * open_price;")
print()
print("Then the precise equity_at_target becomes:")
print("  equity_at_target = equity - (g_value_weighted_price - V * end_price) * C;")
print("which simplifies to:")
print("  equity_at_target = equity - g_value_weighted_price * C + V * end_price * C;")
print()
print("And in the trade_size formula, the numerator changes from:")
print("  100*equity*lev - 100*C*distance*V*lev - lev*SO*used_margin")
print("to:")
print("  100*equity*lev - 100*C*(W - V*end_price)*lev - lev*SO*used_margin")
print("which is:")
print("  100*lev*(equity - C*(W - V*end_price)) - lev*SO*used_margin")
print()

print("The re-sizing check (line 137) also uses the approximate:")
print("  price_difference = equity_difference / (V * C)")
print("This is fine conceptually -- it's computing at what price existing positions")
print("would hit stop-out. But it would be more precise to directly check:")
print("  exact_loss_at_end = C * (W - V * end_price)")
print("  can_survive = (equity - exact_loss_at_end) > SO/100 * used_margin")
print()

print("=" * 70)
print("ALSO: SELL-SIDE SINGLE-SHOT (open_downwards_while_going_downwards)")
print("=" * 70)
print()
print("For sell positions, the loss at end_price (price going UP) is:")
print("  Loss = sum(v_i * (end_price - open_price_i) * C)")
print("       = C * (V * end_price - W)")
print("where W = sum(v_i * open_price_i)")
print()
print("Current formula uses: V * distance * C where distance = end_price - current_bid")
print("Since open_prices > current_bid (sells opened at higher prices):")
print("  open_price_i - end_price > current_bid - end_price  ... no, that's wrong for sells")
print()
print("Wait -- for sells going DOWN:")
print("  Sells are opened as price FALLS. Each sell opened at progressively lower price.")
print("  end_price = current_bid * (1 + survive_up/100) -- price RISES to stop-out")
print("  Loss on sell at price p_i when price rises to end_price: (end_price - p_i) * v_i * C")
print("  Since sells opened at HIGHER prices (price was falling, so earliest sells at top):")
print("  p_i > current_bid, so (end_price - p_i) < (end_price - current_bid) = distance")
print("  Same overestimation pattern!")
print()

current_bid = 2700.0
survive_up = 4.0
end_price_sell = current_bid * (100 + survive_up) / 100
distance_sell = end_price_sell - current_bid

sell_prices = [2800 - i*10 for i in range(10)]
print(f"Sell prices: {sell_prices}")
print(f"Current bid: {current_bid}, end_price: {end_price_sell}")

W_sell = sum(vol_per * p for p in sell_prices)
V_sell = len(sell_prices) * vol_per

loss_approx_sell = V_sell * distance_sell * C
loss_exact_sell = C * (V_sell * end_price_sell - W_sell)
print(f"Loss (approx): ${loss_approx_sell:.2f}")
print(f"Loss (exact):  ${loss_exact_sell:.2f}")
print(f"Overestimate:  ${loss_approx_sell - loss_exact_sell:.2f} ({(loss_approx_sell/loss_exact_sell - 1)*100:.1f}%)")
