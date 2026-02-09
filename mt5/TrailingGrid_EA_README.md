# TrailingGrid EA - MT5 Expert Advisor

## Strategy Overview

This EA implements a grid trading strategy with trailing stops, optimized through extensive backtesting on XAUUSD 2025 tick data (51.7 million ticks).

**Key Concept**: The strategy profits from price oscillations, not the underlying trend. ~95% of returns come from capturing oscillations through trailing stops.

## Optimized Parameters (XAUUSD)

| Parameter | Value | Description |
|-----------|-------|-------------|
| MaxPositions | 20 | Maximum concurrent positions |
| Spacing | $1.0 | Grid spacing between entries |
| SurvivePct | 13.0% | Position sizing - survive X% price drop |
| StopOpeningPct | 13.0% | Stop opening new positions after X% drop from peak |
| **MinProfitPoints** | **$0.30** | Price rise needed to activate trailing stop |
| **TrailDistance** | **$15.0** | Trailing stop distance from highest price |
| **UpdateThreshold** | **$1.0** | Update trail when price rises by this amount |

**IMPORTANT**: All trailing parameters are in **PRICE** terms (not dollar profit).
- MinProfitPoints=$0.30 means the price must rise $0.30 from entry to activate
- This ensures consistent behavior regardless of position size

## Backtest Results (2025 XAUUSD)

- **Return**: 7.33x ($10,000 → $73,300)
- **Max Drawdown**: ~59%
- **Trades**: ~9,800 trailing stop exits
- **Survival Rate**: 396/1152 configs tested (34%)

### Return Attribution
- Oscillation Profit: ~$65,000 (~95%)
- Trend Contribution: ~$3,000 (~5%)

## How It Works

1. **Entry**: Opens BUY positions when price drops by `Spacing` ($1) from the lowest existing entry
2. **Position Sizing**: Calculates lot size to survive a `SurvivePct` (13%) price drop with full grid
3. **Stop-Opening Band**: Stops opening new positions if price drops `StopOpeningPct` (13%) from peak
4. **Trailing Activation**: When price rises $0.30 from entry, trailing stop activates
5. **Trailing Stop**: Trails at $15 below the highest price seen since activation
6. **Trail Update**: Updates trailing stop when price rises $1 from previous high
7. **Exit**: Closes position when price retraces $15 from highest

## Parameter Logic

### Why $0.30 MinProfitPoints?
- Ensures position is meaningfully in profit before trailing activates
- Too small ($0.05): Trail activates prematurely, gets stopped out on noise
- Too large ($2.00): Many good trades never activate trailing

### Why $15 TrailDistance?
- Gold oscillates $10-$20 in typical 4-hour periods
- $15 gives positions room to ride oscillations without getting stopped
- Too tight ($1): Gets stopped on normal volatility
- Too wide ($30): Gives back too much profit on reversals

### Why $1 UpdateThreshold?
- Reduces "noise updates" to trailing stop
- Only moves stop when a meaningful price advance occurs

## Installation

1. Copy `TrailingGrid_EA.mq5` to `MQL5/Experts/`
2. Compile in MetaEditor
3. Attach to XAUUSD chart (any timeframe - EA uses ticks)
4. Load preset from `Presets/TrailingGrid_XAUUSD_Optimized.set`

## Important Notes

- **Symbol**: Optimized for XAUUSD. Other symbols need parameter adjustment
- **Broker**: Test on demo first - swap rates vary by broker
- **Drawdown**: 59% max DD is substantial - size account accordingly
- **Market Conditions**: Best in oscillating markets; ~95% of profit from oscillations

## Alternative Configuration (Lower Risk)

For lower drawdown, use these settings:
- MaxPositions: 15
- Spacing: $2.0
- TrailDistance: $10.0
- Expected: ~5x return, ~50% max DD

## Disclaimer

Past backtest results do not guarantee future performance. Trade at your own risk.
