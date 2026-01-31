# MT5 Comparison Test Strategy

**Phase 8: MT5 Validation**
**Date:** 2026-01-07
**Purpose:** Validate C++ backtest engine accuracy against MT5 Strategy Tester

---

## 🎯 Strategy Overview

### Strategy Name
**Simple Price Level Breakout (SPLB)**

### Why This Strategy?
- **Deterministic**: No indicators, just price levels
- **Verifiable**: Easy to check entry/exit points manually
- **Simple**: Minimal complexity reduces implementation differences
- **Clear Rules**: No ambiguity in entry/exit conditions

---

## 📋 Strategy Specification

### Entry Rules

**Long Entry:**
1. Price breaks above a fixed level: 1.2000 (for EURUSD)
2. Entry executes on the bar close after breakout
3. Lot size: 0.10 (fixed)
4. Stop Loss: 50 pips below entry
5. Take Profit: 100 pips above entry

**Short Entry:**
1. Price breaks below a fixed level: 1.1900 (for EURUSD)
2. Entry executes on the bar close after breakout
3. Lot size: 0.10 (fixed)
4. Stop Loss: 50 pips above entry
5. Take Profit: 100 pips below entry

### Exit Rules
- Exit at Stop Loss
- Exit at Take Profit
- One trade at a time (no pyramiding)

### Position Management
- Maximum 1 position open at any time
- After SL or TP, wait for opposite breakout to trade again
- No trailing stops
- No position modification after entry

---

## 🔧 Test Configuration

### Symbol
**EURUSD** (EUR/USD)

### Timeframe
**H1** (1-hour bars)

### Test Period
**2024-01-01 to 2024-01-31** (1 month of data)

### Account Settings
- Initial Balance: $10,000
- Currency: USD
- Leverage: 1:100
- Commission: $0 (to simplify comparison)
- Swap: Disabled (to avoid overnight interest differences)

### Entry Levels
- Long Trigger: 1.2000
- Short Trigger: 1.1900
- These levels are chosen to ensure some trades occur during January 2024

---

## 📊 Expected Behavior

### Trade Sequence (Approximate)
Based on EURUSD movement in January 2024:

1. **Trade 1**: Long entry around 1.2000
   - Entry: ~1.2000
   - SL: ~1.1950
   - TP: ~1.2100

2. **Trade 2**: May trigger based on market movement

### Success Criteria
1. **Trade Count**: MT5 and C++ engine must have identical trade count
2. **Entry Prices**: Must match within 1 pip (0.0001)
3. **Exit Prices**: Must match within 1 pip (0.0001)
4. **Final Balance**: Must match within 1% or $10
5. **Trade P/L**: Individual trades must match within $1

---

## 💻 Implementation Details

### MT5 MQL5 Implementation

```mql5
//+------------------------------------------------------------------+
//|                                        SimplePriceLevelBreakout.mq5 |
//|                                                                     |
//+------------------------------------------------------------------+
#property copyright "MT5 Comparison Test"
#property version   "1.00"
#property strict

// Input parameters
input double LongTriggerLevel = 1.2000;   // Long entry level
input double ShortTriggerLevel = 1.1900;  // Short entry level
input double LotSize = 0.10;              // Fixed lot size
input int StopLossPips = 50;              // Stop loss in pips
input int TakeProfitPips = 100;           // Take profit in pips

// Global variables
bool hasPosition = false;
ulong currentTicket = 0;

//+------------------------------------------------------------------+
//| Expert initialization function                                   |
//+------------------------------------------------------------------+
int OnInit()
{
   return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+
//| Expert tick function                                             |
//+------------------------------------------------------------------+
void OnTick()
{
   // Get current price
   double close = iClose(_Symbol, PERIOD_H1, 0);
   double prevClose = iClose(_Symbol, PERIOD_H1, 1);

   // Check if we have a position
   hasPosition = (PositionsTotal() > 0);

   if (!hasPosition)
   {
      // Check for long entry (breakout above level)
      if (prevClose <= LongTriggerLevel && close > LongTriggerLevel)
      {
         OpenLongPosition();
      }
      // Check for short entry (breakout below level)
      else if (prevClose >= ShortTriggerLevel && close < ShortTriggerLevel)
      {
         OpenShortPosition();
      }
   }
}

//+------------------------------------------------------------------+
//| Open long position                                                |
//+------------------------------------------------------------------+
void OpenLongPosition()
{
   double price = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
   double sl = price - StopLossPips * _Point * 10;
   double tp = price + TakeProfitPips * _Point * 10;

   MqlTradeRequest request = {};
   MqlTradeResult result = {};

   request.action = TRADE_ACTION_DEAL;
   request.symbol = _Symbol;
   request.volume = LotSize;
   request.type = ORDER_TYPE_BUY;
   request.price = price;
   request.sl = sl;
   request.tp = tp;
   request.deviation = 10;
   request.magic = 123456;
   request.comment = "SPLB Long";

   if (OrderSend(request, result))
   {
      Print("Long position opened at ", price, " SL:", sl, " TP:", tp);
   }
}

//+------------------------------------------------------------------+
//| Open short position                                               |
//+------------------------------------------------------------------+
void OpenShortPosition()
{
   double price = SymbolInfoDouble(_Symbol, SYMBOL_BID);
   double sl = price + StopLossPips * _Point * 10;
   double tp = price - TakeProfitPips * _Point * 10;

   MqlTradeRequest request = {};
   MqlTradeResult result = {};

   request.action = TRADE_ACTION_DEAL;
   request.symbol = _Symbol;
   request.volume = LotSize;
   request.type = ORDER_TYPE_SELL;
   request.price = price;
   request.sl = sl;
   request.tp = tp;
   request.deviation = 10;
   request.magic = 123456;
   request.comment = "SPLB Short";

   if (OrderSend(request, result))
   {
      Print("Short position opened at ", price, " SL:", sl, " TP:", tp);
   }
}
```

### C++ Backtest Engine Implementation

The C++ implementation will use our existing backtest engine components:
- Price data from MT5 history
- Position validation via PositionValidator
- Currency conversion via CurrencyConverter
- Margin checks via validated components

Implementation file: `strategies/simple_price_level_breakout.h`

---

## 🔍 Validation Checklist

### Pre-Execution
- [ ] MT5 Strategy Tester configured correctly
- [ ] Historical data downloaded for EURUSD H1 (Jan 2024)
- [ ] Commission set to $0 on both platforms
- [ ] Swap disabled on both platforms
- [ ] Initial balance set to $10,000 on both platforms

### Execution
- [ ] Run MT5 Strategy Tester
- [ ] Capture MT5 results (trade list, final balance)
- [ ] Run C++ backtest engine
- [ ] Capture C++ results (trade list, final balance)

### Comparison
- [ ] Trade count matches exactly
- [ ] Entry prices match within 1 pip
- [ ] Exit prices match within 1 pip
- [ ] Individual P/L matches within $1
- [ ] Final balance matches within 1% or $10

### Documentation
- [ ] Screenshot MT5 results
- [ ] Export MT5 trade list
- [ ] Export C++ trade list
- [ ] Create comparison table
- [ ] Document any discrepancies

---

## 📈 Success Metrics

| Metric | Target | MT5 Result | C++ Result | Match? |
|--------|--------|------------|------------|--------|
| Trade Count | TBD | - | - | - |
| Final Balance | $10,000 ± trades | - | - | - |
| Balance Difference | <1% or <$10 | - | - | - |
| Entry Price Accuracy | Within 1 pip | - | - | - |
| Exit Price Accuracy | Within 1 pip | - | - | - |
| Individual P/L | Within $1 | - | - | - |

---

## 📝 Notes

### Why These Levels?
- 1.2000 and 1.1900 provide 100-pip range
- EURUSD typically trades in this range, ensuring some breakouts
- Round numbers make manual verification easier

### Why No Indicators?
- Indicators can have implementation differences (averaging, rounding)
- Price levels are unambiguous
- Makes debugging easier if results don't match

### Why H1 Timeframe?
- Enough bars for meaningful test (744 bars in January)
- Not too many trades (manageable for manual verification)
- Standard timeframe used in forex trading

---

## 🚀 Next Steps

1. **Implement MQL5 EA** - Create SimplePriceLevelBreakout.mq5
2. **Implement C++ Strategy** - Create simple_price_level_breakout.h
3. **Download Historical Data** - Ensure EURUSD H1 data for January 2024
4. **Run MT5 Test** - Execute in Strategy Tester
5. **Run C++ Test** - Execute with our backtest engine
6. **Compare Results** - Create detailed comparison table
7. **Document Findings** - Create MT5_COMPARISON_RESULTS.md

---

**Created:** 2026-01-07
**Status:** Strategy Designed ✅
**Next:** Implement in MQL5 and C++
