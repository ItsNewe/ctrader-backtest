# Fill-Up Building Blocks Combinatoric Test Results

## Gold 2025 Data

### Building Blocks Tested
- **HEDGE**: Open shorts when price drops to hedge long exposure
- **DD_PROT**: Close all positions when drawdown exceeds threshold
- **VEL_FILTER**: Pause trading during rapid price movements

---

## All Results Summary

| Config | Return | Max DD | DD Trig | Vel Pause | Margin |
|--------|--------|--------|---------|-----------|--------|
| **BASELINE** |||||
| BASE_5 | 0.00x | 99.98% | 0 | 0 | YES |
| BASE_6 | **9.19x** | 94.65% | 0 | 0 | NO |
| BASE_8 | 5.67x | 57.02% | 0 | 0 | NO |
| BASE_13 | 5.62x | 57.89% | 0 | 0 | NO |
| **HEDGE ONLY** |||||
| H_6_01 | 7.69x | 78.47% | 0 | 0 | NO |
| H_6_02 | 5.80x | 72.11% | 0 | 0 | NO |
| H_6_03 | 4.82x | 68.61% | 0 | 0 | NO |
| **DD PROTECTION ONLY** |||||
| D_6_50 | 4.18x | **49.99%** | 3 | 0 | NO |
| D_6_60 | 4.36x | 59.99% | 2 | 0 | NO |
| D_8_40 | 4.75x | **40.00%** | 3 | 0 | NO |
| D_8_50 | 5.13x | 49.97% | 1 | 0 | NO |
| D_13_40 | 2.09x | 40.00% | 7 | 0 | NO |
| **VELOCITY FILTER ONLY** |||||
| V_6_10 | 9.18x | 94.65% | 0 | 7 | NO |
| **HEDGE + DD PROTECTION** |||||
| HD_6_01_60 | 4.71x | 59.96% | 1 | 0 | NO |
| HD_6_02_60 | 4.37x | 60.00% | 1 | 0 | NO |
| HD_8_01_50 | 4.26x | 50.00% | 2 | 0 | NO |
| **HEDGE + VELOCITY** |||||
| HV_6_01_10 | 7.70x | 78.47% | 0 | 7 | NO |
| **DD + VELOCITY** |||||
| DV_6_60_10 | 4.36x | 59.99% | 2 | 7 | NO |
| **TRIPLE COMBO** |||||
| HDV_6_01_60 | 4.70x | 59.96% | 1 | 7 | NO |
| **AGGRESSIVE (survive=5%)** |||||
| H_5_02 | 0.00x | 99.98% | 0 | 0 | YES |
| HD_5_02_50 | 0.00x | 50.00% | 30 | 0 | NO |

---

## Key Findings

### 1. DD Protection is Most Effective for Risk Reduction
- DD Protection at 50% cuts max drawdown from 94.65% to 49.99%
- Cost: ~55% reduction in returns
- **Best risk-adjusted**: D_8_50 (5.13x return, 49.97% DD)

### 2. Hedging Reduces Both Return AND Drawdown
- Hedge ratio 0.1: -16% DD, -16% return
- Hedge ratio 0.2: -23% DD, -37% return
- Hedge ratio 0.3: -26% DD, -47% return
- **Diminishing returns**: Higher hedge ratio hurts more than it helps

### 3. Velocity Filter Has Minimal Impact
- Only triggers 7 times across the entire year
- Does not significantly change returns or drawdown
- Gold 2025 didn't have sustained rapid crashes

### 4. survive=5% Cannot Be Saved
- Hedge alone: Still margin calls
- DD protection alone: Survives but 0% return (30 triggers wipes all profit)
- The fundamental position sizing is too aggressive

### 5. Combinations Don't Stack Well
- Adding DD to Hedge doesn't improve much over DD alone
- Adding Velocity to anything doesn't help
- **Best combo**: HD_6_01_60 (4.71x, 59.96%) - but D_8_50 is simpler and better

---

## Recommended Configurations

### For Maximum Return (High Risk Tolerance)
```
survive=6%, no protection
Return: 9.19x, Max DD: 94.65%
```

### For Balanced Risk/Return
```
survive=8%, DD Protection 50%
Return: 5.13x, Max DD: 49.97%
```

### For Conservative (Low DD)
```
survive=8%, DD Protection 40%
Return: 4.75x, Max DD: 40.00%
```

### For Pure Fill-Up (No Protection)
```
survive=8% or 13%
Return: ~5.6x, Max DD: ~57%
```

---

## Conclusion

1. **DD Protection is the winner** - Simple, effective, easy to implement
2. **Hedging has limited value** on Gold 2025 (uptrending market)
3. **Velocity filter is not useful** for this dataset
4. **survive=6% is optimal** for max return, but risky
5. **survive=8%** provides good return with natural DD protection
6. **Combining protections** doesn't significantly improve over single protection

The best strategy depends on risk tolerance:
- Aggressive: BASE_6 (9.19x / 95% DD)
- Balanced: D_8_50 (5.13x / 50% DD)
- Conservative: D_8_40 (4.75x / 40% DD)
