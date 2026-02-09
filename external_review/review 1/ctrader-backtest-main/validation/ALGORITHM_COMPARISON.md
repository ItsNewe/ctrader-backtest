# Fill-Up Strategy: MT5 vs C++ Algorithm Comparison

## Position Sizing Algorithm Line-by-Line Comparison

### MT5 EA (fill_up.mq5) - Lines 293-390

#### Initialization
```mql5
void sizing_buy() {
    trade_size_buy = 0;

    // Get symbol/account info
    static int calc_mode = SymbolInfoInteger(_Symbol, SYMBOL_TRADE_CALC_MODE);
    static double contract_size = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);
    static long leverage = AccountInfoInteger(ACCOUNT_LEVERAGE);
    static double initial_margin_rate = 0;
    static double maintenance_margin_rate = 0;
    SymbolInfoMarginRate(_Symbol, ORDER_TYPE_BUY, initial_margin_rate, maintenance_margin_rate);

    double current_margin_level = AccountInfoDouble(ACCOUNT_MARGIN_LEVEL);
    double used_margin = AccountInfoDouble(ACCOUNT_MARGIN);
    double margin_stop_out_level = AccountInfoDouble(ACCOUNT_MARGIN_SO_SO);
    double equity = AccountInfoDouble(ACCOUNT_EQUITY);
```

**C++ Equivalent**: ✅ Have all except `initial_margin_rate` (using 1.0 as default)

#### Equity Calculations
```mql5
    double equity_at_target = equity * margin_stop_out_level / current_margin_level;
    double equity_difference = equity - equity_at_target;
    double price_difference = equity_difference / (volume_of_open_trades * contract_size);
```

**C++**: ✅ Lines 143-154 - MATCHES

#### End Price Calculation
```mql5
    double end_price;
    if (PositionsTotal() == 0) {
        end_price = current_ask * ((100 - survive) / 100);
    } else {
        end_price = highest_buy * ((100 - survive) / 100);
    }
    double distance = current_ask - end_price;
    double number_of_trades = MathFloor(distance / spacing_buy);
```

**C++**: ✅ Lines 159-168 - MATCHES

#### Main Sizing Logic
```mql5
    if((PositionsTotal() == 0) || ((current_ask - price_difference) < end_price)) {
        equity_at_target = equity - volume_of_open_trades * MathAbs(distance) * contract_size;
        double margin_level = equity_at_target / used_margin * 100;
        double trade_size = min_volume_alg;
        double local_used_margin = 0;
        double starting_price = current_ask;
```

**C++**: ✅ Lines 171-176 - MATCHES

#### Grid Equity Calculation
```mql5
        if(margin_level > margin_stop_out_level) {
            double d_equity = contract_size * trade_size * (number_of_trades * (number_of_trades + 1) / 2);
            double d_spread = number_of_trades * trade_size * current_spread * contract_size;
            d_equity += d_spread;
```

**C++**: ✅ Lines 178-182 - MATCHES

**Formula Explanation**:
- `n×(n+1)/2` = sum of distances (1+2+3+...+n)
- Represents total price movement exposure for grid
- Each position further from current price has more potential loss

#### Margin Calculation (FOREX mode)
```mql5
            case SYMBOL_CALC_MODE_FOREX:
                local_used_margin += trade_size * contract_size / leverage * initial_margin_rate;
                local_used_margin += trade_size * contract_size / leverage * initial_margin_rate;
                break;
```

**C++**: ❌ **DIFFERENCE FOUND!**
- MT5: Uses FOREX formula (no price multiplication)
- C++: Uses CFD_LEVERAGE formula (with price)

**But wait** - MT5 reported calc_mode = 0 (FOREX), but actual margin matched CFD_LEVERAGE formula!

This suggests MT5 might be:
1. Reporting wrong calc_mode, OR
2. Using hybrid calculation, OR
3. Using initial_margin_rate to compensate

#### Margin Averaging
```mql5
            local_used_margin = local_used_margin / 2;
            local_used_margin = number_of_trades * local_used_margin;
```

**C++**: ✅ Lines 187-188 - MATCHES

**Explanation**:
- Averages margin between starting_price and end_price
- Multiplies by number_of_trades for total grid margin

#### Binary Search for Multiplier
```mql5
            double multiplier = 0;
            double equity_backup = equity_at_target;
            double used_margin_backup = used_margin;
            double max = max_volume_alg / min_volume_alg;

            // Try maximum first
            equity_at_target -= max * d_equity;
            used_margin += max * local_used_margin;
            if(margin_stop_out_level < equity_at_target / used_margin * 100) {
                multiplier = max;
            } else {
                // Binary search
                used_margin = used_margin_backup;
                equity_at_target = equity_backup;
                for(double increment = max; increment >= 1; increment = increment / 10) {
                    while(margin_stop_out_level < equity_at_target / used_margin * 100) {
                        equity_backup = equity_at_target;
                        used_margin_backup = used_margin;
                        multiplier += increment;
                        equity_at_target -= increment * d_equity;
                        used_margin += increment * local_used_margin;
                    }
                    multiplier -= increment;
                    used_margin = used_margin_backup;
                    equity_at_target = equity_backup;
                }
            }
```

**C++**: ✅ Lines 191-218 - MATCHES

#### Final Sizing
```mql5
            multiplier = MathMax(1, multiplier);
            trade_size_buy = multiplier * min_volume_alg;
            max_trade_size = MathMax(max_trade_size, trade_size_buy);
```

**C++**: ✅ Lines 220-223 - MATCHES

## Critical Difference Identified

### Issue: Margin Calculation Mode Mismatch

**MT5 Symbol Info Says**:
- `Trade Calc Mode: 0` (FOREX)
- But actual margin = $890.82 for 1 lot @ $4454.11

**FOREX Formula Would Give**:
```
Margin = 1.0 × 100 / 500 × initial_margin_rate
       = 0.2 × initial_margin_rate
```
To get $890.82, we'd need: `initial_margin_rate = 4454.1`

**CFD_LEVERAGE Formula Gives**:
```
Margin = 1.0 × 100 × 4454.11 / 500 × initial_margin_rate
       = 890.822 × initial_margin_rate
```
With `initial_margin_rate = 1.0`, we get exactly $890.82 ✅

### Conclusion

MT5 is likely using:
1. **CFD_LEVERAGE calculation** (includes price) for actual trades
2. **initial_margin_rate = 1.0** (confirmed by calculation match)
3. But **reports FOREX mode** in symbol properties (misleading!)

My C++ implementation uses CFD_LEVERAGE formula with margin_rate=1.0, which **matches actual MT5 behavior**.

## Why Position Sizes Still Explode?

Since margin calculations match, the issue must be in:

### Hypothesis 1: Equity-Based Acceleration
- When equity grows large, `equity_at_target` grows proportionally
- Larger `equity_at_target` allows larger multiplier
- Formula allows exponential growth once equity exceeds certain threshold

### Hypothesis 2: Missing Balance-Based Cap
MT5 might have additional constraint:
```mql5
trade_size = min(trade_size, balance / some_factor);
```

### Hypothesis 3: Initial Margin Rate is Actually Much Higher
If `initial_margin_rate = 100.0` (not 1.0), then:
- Required margin would be 100× higher
- This would severely limit position sizes
- Would prevent exponential growth

**Need to verify**: What is actual initial_margin_rate value in MT5?

## Testing Plan

1. **Print debug values from MT5**:
   - Add `Print("initial_margin_rate: ", initial_margin_rate);` to MT5 EA
   - Run short test to see actual value

2. **Match C++ margin calculation exactly**:
   - Update C++ to use correct calc_mode logic
   - Update margin_rate parameter to match MT5

3. **Add conservative caps**:
   - Max trade size = min(calculated, balance / 1000)
   - Prevent unrealistic leverage

---

**Status**: Margin formula is correct, but need to verify initial_margin_rate value from MT5.
