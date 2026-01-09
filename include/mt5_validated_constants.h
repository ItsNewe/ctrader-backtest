// AUTO-GENERATED: MT5 Validated Constants
// Generated from MT5 test data collected on 2026-01-07
// DO NOT EDIT MANUALLY

#ifndef MT5_VALIDATED_CONSTANTS_H
#define MT5_VALIDATED_CONSTANTS_H

namespace MT5Validated {
    // Margin calculation (from Test F)
    constexpr int LEVERAGE = 500;
    constexpr double CONTRACT_SIZE = 100000.0;

    // Swap timing (from Test E)
    constexpr int SWAP_HOUR = 0;  // Midnight
    constexpr int TRIPLE_SWAP_DAY = 3;  // Wednesday

    // Slippage (from Test C)
    constexpr double SLIPPAGE_POINTS = 0.000000;  // Zero in MT5 Tester

    // Spread (from Test D)
    constexpr double MEAN_SPREAD_POINTS = 7.08;
    constexpr double MEAN_SPREAD_PIPS = 0.71;

    // Tick generation (from Test B)
    constexpr int AVG_TICKS_PER_H1_BAR = 1914;
}

#endif // MT5_VALIDATED_CONSTANTS_H
