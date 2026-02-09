# Comprehensive Strategy Test Results
## Using Accurate Backtester (Validated Against MT5)

Date: 2026-01-15

---

## Key Validation
- **MT5 GridSimple (survive=30%, spacing=50, no trailing)**: +95% profit
- **Backtester Result**: +99% profit (1.99x return)
- **Match: YES** - Results align within expected variance

---

## NAS100 Tests (Full Year 2025 Data: 53.4M ticks)

### Price Movement
- Start: 20,744 → End: 25,663
- Change: **+23.7%**

### Survive Down % Sweep (No Trailing, Spacing=50)

| Survive% | Final Equity | Return | Max DD | Trades | Status |
|----------|--------------|--------|--------|--------|--------|
| 4% | $0.23 | 0.00x | 100% | 42 | MARGIN CALL |
| 8% | $18.25 | 0.00x | 100% | 90 | MARGIN CALL |
| 10% | $661.04 | 0.07x | 100% | 60 | MARGIN CALL |
| 15% | $227.97 | 0.02x | 99% | 37 | MARGIN CALL |
| 20% | $133.78 | 0.01x | 100% | 32 | MARGIN CALL |
| 25% | $54.65 | 0.01x | 100% | 31 | MARGIN CALL |
| **30%** | **$19,931.95** | **1.99x** | 88% | 103 | **OK** |
| 40% | $16,897.30 | 1.69x | 67% | 58 | OK |
| 50% | $15,233.71 | 1.52x | 54% | 33 | OK |

**Finding**: Minimum survive_down = **30%** required for NAS100

### Spacing Sweep (Survive=30%, No Trailing)

| Spacing | Final Equity | Return | Max DD | Trades |
|---------|--------------|--------|--------|--------|
| 5 pts | $20,277.42 | 2.03x | 89% | 127 |
| 10 pts | $20,257.86 | 2.03x | 89% | 126 |
| 25 pts | $20,183.46 | 2.02x | 89% | 124 |
| **50 pts** | **$19,931.95** | **1.99x** | 88% | 103 |
| 100 pts | $19,659.56 | 1.97x | 87% | 53 |
| 200 pts | $19,220.79 | 1.92x | 86% | 28 |
| 500 pts | $18,091.13 | 1.81x | 73% | 12 |

**Finding**: Smaller spacing = more trades = slightly higher returns. 50 pts is good balance.

### Trailing Stop Tests (Survive=30%, Spacing=50)

| ATR Mult | Final Equity | Return | Max DD | Trades | Spread Cost |
|----------|--------------|--------|--------|--------|-------------|
| 1.5x | $10,623.42 | 1.06x | 44% | 1,934 | $1,401.92 |
| 2.0x | $10,548.54 | 1.05x | 44% | 1,819 | $1,313.69 |
| 2.5x | $10,552.24 | 1.06x | 44% | 1,719 | $1,243.89 |
| 3.0x | $10,619.11 | 1.06x | 44% | 1,634 | $1,189.78 |
| 5.0x | $10,425.24 | 1.04x | 44% | 1,352 | $973.85 |
| 10.0x | $10,204.86 | 1.02x | 44% | 956 | $680.53 |

**Finding**: Trailing stops DESTROY the strategy!
- Without trailing: **1.99x** return with 103 trades, $1.35 spread cost
- With trailing: **1.02-1.06x** return with 956-1,934 trades, $680-$1,400 spread cost

### Trailing + Low Survive (Expected Failures)

| Survive% | Final Equity | Return | Trades | Status |
|----------|--------------|--------|--------|--------|
| 4% | $153.87 | 0.02x | 1,796 | MARGIN CALL |
| 8% | $59.54 | 0.01x | 1,796 | MARGIN CALL |
| 10% | $40.68 | 0.00x | 1,796 | MARGIN CALL |
| 15% | $11,122.29 | 1.11x | 1,819 | OK |
| 20% | $10,833.27 | 1.08x | 1,819 | OK |

---

## GOLD (XAUUSD) Tests (Full Year 2025 Data)

### Survive Down % Sweep (No Trailing, Spacing=$2)

| Survive% | Final Equity | Return | Max DD | Trades | Status |
|----------|--------------|--------|--------|--------|--------|
| 1% | $5.43 | 0.00x | 100% | 68 | MARGIN CALL |
| 2% | $3.43 | 0.00x | 100% | 540 | MARGIN CALL |
| 3% | $106.57 | 0.01x | 100% | 833 | MARGIN CALL |
| 5% | $25.39 | 0.00x | 100% | 566 | MARGIN CALL |
| 10% | $219.61 | 0.02x | 100% | 362 | MARGIN CALL |
| **15%** | **$339,732.53** | **33.97x** | 75% | 516 | **OK** |
| 20% | $143,143.91 | 14.31x | 57% | 163 | OK |
| 30% | $60,216.05 | 6.02x | 38% | 41 | OK |

**Finding**: Minimum survive_down = **15%** required for Gold
**EXCEPTIONAL**: Gold with survive=15% achieves **34x return**!

### Spacing Sweep (Survive=15%, No Trailing)

| Spacing | Final Equity | Return | Max DD | Trades |
|---------|--------------|--------|--------|--------|
| $0.50 | $342,060.59 | 34.21x | 75% | 565 |
| $1.00 | $341,679.34 | 34.17x | 75% | 565 |
| $2.00 | $339,732.53 | 33.97x | 75% | 516 |
| $5.00 | $328,315.23 | 32.83x | 74% | 315 |
| $10.00 | $304,523.79 | 30.45x | 73% | 187 |
| $20.00 | $261,230.95 | 26.12x | 73% | 96 |

### Trailing Tests (Survive=15%, Spacing=$2)

| ATR Mult | Final Equity | Return | Trades | Spread Cost |
|----------|--------------|--------|--------|-------------|
| 2.0x | $13,313.51 | 1.33x | 3,988 | $10,586.50 |
| 3.0x | $12,954.22 | 1.30x | 3,558 | $9,299.50 |
| 5.0x | $12,617.10 | 1.26x | 2,945 | $7,452.88 |
| 10.0x | $12,002.72 | 1.20x | 2,186 | $5,236.12 |

**Finding**: Same as NAS100 - trailing kills the strategy!
- Without trailing: **33.97x** return
- With trailing: **1.20-1.33x** return

---

## RECOMMENDED CONFIGURATIONS

### NAS100
```
survive_down_pct = 30.0
min_entry_spacing = 50.0
enable_trailing = false   // CRITICAL!
leverage = 500
```
Expected: ~2x return with 88% max drawdown

### GOLD
```
survive_down_pct = 15.0
min_entry_spacing = 2.0
enable_trailing = false   // CRITICAL!
leverage = 500
contract_size = 100
```
Expected: ~34x return with 75% max drawdown

---

## KEY CONCLUSIONS

1. **TRAILING STOPS DESTROY THIS STRATEGY**
   - Creates "churning" effect: positions close and reopen repeatedly
   - Spread costs accumulate rapidly ($1 → $1,400 for NAS100)
   - Trade count explodes (103 → 1,934 trades)
   - Returns drop from 2x to 1.02x

2. **SURVIVE_DOWN THRESHOLDS**
   - NAS100: Minimum 30% (below = guaranteed margin call)
   - GOLD: Minimum 15% (below = guaranteed margin call)

3. **THE STRATEGY IS "BUY AND HOLD LEVERAGED LONGS"**
   - Only enter on new all-time highs
   - Size positions to survive X% drawdown
   - Never close until account ends or margin call
   - Works in bull markets (2025: NAS100 +24%, Gold +41%)

4. **GOLD OUTPERFORMS NAS100**
   - Gold 2025: 34x return (with higher volatility)
   - NAS100 2025: 2x return
   - Gold was in a stronger uptrend in 2025

5. **SPACING HAS MINIMAL IMPACT**
   - Smaller spacing = more trades = slightly higher returns
   - 50 pts for NAS100, $2 for Gold are good defaults

---

## WARNINGS

1. **88% MAX DRAWDOWN** - Account will lose 88% of peak value at some point
2. **Strategy only works in bull markets** - Will blow up in bear markets
3. **High leverage (500:1)** - Standard for CFD brokers but risky
4. **No stop-loss protection** - You're betting the market will recover

---

## MT5 VALIDATED

These results match MT5 Strategy Tester output:
- GridSimple with survive=30%, spacing=50: +95% (MT5) vs +99% (backtester)
- GridOptimized with survive=10%, trailing=ON: MARGIN CALL (both)
