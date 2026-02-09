//+------------------------------------------------------------------+
//|                                          FillUp_V10_Ultimate.mq5 |
//|                                    Fill-Up Grid Strategy V10      |
//|          V8 Features + Mean Reversion Filter for Better Entries   |
//|                                                                    |
//| V10 Changes from V8:                                               |
//| - Mean Reversion Filter: Only enter when price < SMA              |
//| - Uses rolling 500-tick SMA of bid prices                         |
//| - Default threshold: -0.04% deviation                              |
//| - Optimal spacing reduced to 0.75 (from 1.0)                       |
//+------------------------------------------------------------------+
#property copyright "Fill-Up V10"
#property link      ""
#property version   "10.00"
#property strict

//--- Input parameters
input group "=== Strategy Parameters ==="
input double   Spacing = 0.75;             // Grid spacing in price units (V10: was 1.0)
input double   MinVolume = 0.01;           // Minimum lot size
input double   MaxVolume = 100.0;          // Maximum lot size
input double   BaseEquity = 10000.0;       // Reference equity for lot scaling
input bool     UseSqrtScaling = true;      // Use square root scaling (safer for large accounts)
input double   TPMultiplier = 2.0;         // Take profit multiplier (1.0 = normal, 2.0 = wider)

input group "=== V10 Mean Reversion Filter ==="
input bool     EnableMeanReversionFilter = true;  // Enable mean reversion filter
input int      MeanReversionSMAPeriod = 500;      // SMA period for mean calculation
input double   MeanReversionThreshold = -0.04;    // Enter when deviation < this %

input group "=== V8 Tighter Protection ==="
input double   StopNewAtDD = 3.0;          // Stop new trades at this DD%
input double   PartialCloseAtDD = 5.0;     // Close 50% of positions at this DD%
input double   CloseAllAtDD = 15.0;        // Close ALL positions at this DD%
input int      MaxPositions = 15;          // Maximum open positions

input group "=== V8 DD-Based Lot Scaling ==="
input bool     EnableDDLotScaling = true;  // Enable lot size reduction based on DD
input double   LotScaleStartDD = 1.0;      // Start reducing lots at this DD%
input double   LotScaleMinFactor = 0.25;   // Minimum lot factor at max DD (0.25 = 25% of normal)

input group "=== V8 Optimized Volatility Filter ==="
input int      ATRShortPeriod = 50;        // Short-term ATR period
input int      ATRLongPeriod = 1000;       // Long-term ATR period
input double   VolatilityThreshold = 0.6;  // Trade when short ATR < long ATR * this
input bool     EnableVolatilityFilter = true; // Enable volatility filter

input group "=== General Settings ==="
input int      MagicNumber = 123461;       // Magic number for this EA
input string   TradeComment = "FillUp_V10"; // Trade comment

//--- Global variables
double g_peakEquity = 0;
double g_lowestBuy = DBL_MAX;
double g_highestBuy = DBL_MIN;
double g_volumeOfOpenTrades = 0;
bool g_partialCloseDone = false;
bool g_allClosed = false;
int g_tradesClosedByProtection = 0;

//--- ATR indicator handles
int g_atrShortHandle = INVALID_HANDLE;
int g_atrLongHandle = INVALID_HANDLE;
double g_atrShortBuffer[];
double g_atrLongBuffer[];

//--- V10: Mean Reversion SMA variables
double g_priceBuffer[];           // Circular buffer for bid prices
int g_priceBufferIndex = 0;       // Current index in circular buffer
int g_priceBufferCount = 0;       // Number of prices stored (up to MeanReversionSMAPeriod)
double g_meanReversionSMA = 0;    // Current SMA value
double g_meanReversionSum = 0;    // Running sum for efficient SMA calculation

//+------------------------------------------------------------------+
//| Expert initialization function                                     |
//+------------------------------------------------------------------+
int OnInit()
{
   //--- Initialize ATR indicators
   if(EnableVolatilityFilter)
   {
      g_atrShortHandle = iATR(_Symbol, PERIOD_CURRENT, ATRShortPeriod);
      g_atrLongHandle = iATR(_Symbol, PERIOD_CURRENT, ATRLongPeriod);

      if(g_atrShortHandle == INVALID_HANDLE || g_atrLongHandle == INVALID_HANDLE)
      {
         Print("Failed to create ATR indicator handles");
         return(INIT_FAILED);
      }

      ArraySetAsSeries(g_atrShortBuffer, true);
      ArraySetAsSeries(g_atrLongBuffer, true);
   }

   //--- V10: Initialize mean reversion price buffer
   if(EnableMeanReversionFilter)
   {
      ArrayResize(g_priceBuffer, MeanReversionSMAPeriod);
      ArrayInitialize(g_priceBuffer, 0);
      g_priceBufferIndex = 0;
      g_priceBufferCount = 0;
      g_meanReversionSMA = 0;
      g_meanReversionSum = 0;
   }

   g_peakEquity = AccountInfoDouble(ACCOUNT_EQUITY);

   Print("================================================================");
   Print("FillUp V10 Ultimate initialized");
   Print("V10 Mean Reversion Filter: ", EnableMeanReversionFilter ? "ENABLED" : "DISABLED");
   if(EnableMeanReversionFilter)
   {
      Print("  SMA Period: ", MeanReversionSMAPeriod, " ticks");
      Print("  Threshold: ", MeanReversionThreshold, "% (enter when deviation < threshold)");
   }
   Print("Spacing: ", Spacing, " (V10 optimal)");
   Print("Protection: StopNew@", StopNewAtDD, "%, Partial@", PartialCloseAtDD,
         "%, CloseAll@", CloseAllAtDD, "%, MaxPos=", MaxPositions);
   Print("DD Lot Scaling: ", EnableDDLotScaling ? "ENABLED" : "DISABLED");
   if(EnableDDLotScaling)
      Print("  Start@", LotScaleStartDD, "% DD, MinFactor=", LotScaleMinFactor);
   Print("Volatility Filter: ", EnableVolatilityFilter ? "ENABLED" : "DISABLED");
   Print("  ATR Short: ", ATRShortPeriod, " | ATR Long: ", ATRLongPeriod);
   Print("  Threshold: ", VolatilityThreshold, " (trade when short < long * threshold)");
   Print("================================================================");

   return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+
//| Expert deinitialization function                                   |
//+------------------------------------------------------------------+
void OnDeinit(const int reason)
{
   if(g_atrShortHandle != INVALID_HANDLE)
      IndicatorRelease(g_atrShortHandle);
   if(g_atrLongHandle != INVALID_HANDLE)
      IndicatorRelease(g_atrLongHandle);

   Print("FillUp V10 stopped. Trades closed by protection: ", g_tradesClosedByProtection);
}

//+------------------------------------------------------------------+
//| Check if volatility is low enough to trade                         |
//+------------------------------------------------------------------+
bool IsVolatilityLow()
{
   if(!EnableVolatilityFilter)
      return true;

   //--- Get ATR values
   if(CopyBuffer(g_atrShortHandle, 0, 0, 1, g_atrShortBuffer) <= 0)
      return true; // Allow trading if can't get data

   if(CopyBuffer(g_atrLongHandle, 0, 0, 1, g_atrLongBuffer) <= 0)
      return true;

   double atrShort = g_atrShortBuffer[0];
   double atrLong = g_atrLongBuffer[0];

   if(atrLong <= 0)
      return true;

   //--- Trade when short-term volatility is below threshold of long-term
   return (atrShort < atrLong * VolatilityThreshold);
}

//+------------------------------------------------------------------+
//| V10: Update mean reversion SMA with new price                      |
//+------------------------------------------------------------------+
void UpdateMeanReversionSMA(double price)
{
   if(!EnableMeanReversionFilter)
      return;

   //--- If buffer is full, subtract oldest value from sum
   if(g_priceBufferCount >= MeanReversionSMAPeriod)
   {
      g_meanReversionSum -= g_priceBuffer[g_priceBufferIndex];
   }

   //--- Add new price to buffer and sum
   g_priceBuffer[g_priceBufferIndex] = price;
   g_meanReversionSum += price;

   //--- Update count if not yet full
   if(g_priceBufferCount < MeanReversionSMAPeriod)
      g_priceBufferCount++;

   //--- Calculate SMA
   if(g_priceBufferCount > 0)
      g_meanReversionSMA = g_meanReversionSum / g_priceBufferCount;

   //--- Move to next position in circular buffer
   g_priceBufferIndex = (g_priceBufferIndex + 1) % MeanReversionSMAPeriod;
}

//+------------------------------------------------------------------+
//| V10: Check if mean reversion condition is met                      |
//+------------------------------------------------------------------+
bool IsMeanReversionConditionMet(double currentPrice)
{
   if(!EnableMeanReversionFilter)
      return true;

   //--- Need at least MeanReversionSMAPeriod prices to calculate SMA
   if(g_priceBufferCount < MeanReversionSMAPeriod)
      return true; // Allow trading while building up history

   if(g_meanReversionSMA <= 0)
      return true;

   //--- Calculate deviation from SMA as percentage
   double deviation = (currentPrice - g_meanReversionSMA) / g_meanReversionSMA * 100.0;

   //--- Only enter when price is below SMA by threshold amount
   //--- Example: threshold = -0.04 means enter when deviation < -0.04%
   //--- This means we buy when price is below the mean (expecting reversion up)
   return (deviation < MeanReversionThreshold);
}

//+------------------------------------------------------------------+
//| Expert tick function                                               |
//+------------------------------------------------------------------+
void OnTick()
{
   double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);

   //--- V10: Update mean reversion SMA with each tick
   UpdateMeanReversionSMA(bid);

   double equity = AccountInfoDouble(ACCOUNT_EQUITY);
   double balance = AccountInfoDouble(ACCOUNT_BALANCE);

   //--- Peak equity reset when no positions (fix from V3)
   int posCount = CountPositions();
   if(posCount == 0)
   {
      if(g_peakEquity != balance)
      {
         g_peakEquity = balance;
         g_partialCloseDone = false;
         g_allClosed = false;
      }
   }

   //--- Update peak equity
   if(equity > g_peakEquity)
   {
      g_peakEquity = equity;
      g_partialCloseDone = false;
      g_allClosed = false;
   }

   //--- Calculate current drawdown
   double currentDD = 0;
   if(g_peakEquity > 0)
      currentDD = (g_peakEquity - equity) / g_peakEquity * 100.0;

   //--- V3 Protection: Close ALL positions if DD exceeds threshold
   if(currentDD > CloseAllAtDD && !g_allClosed)
   {
      int closed = CloseAllPositions("DD>" + DoubleToString(CloseAllAtDD, 1) + "%");
      if(closed > 0)
      {
         g_allClosed = true;
         g_tradesClosedByProtection += closed;
         g_peakEquity = AccountInfoDouble(ACCOUNT_EQUITY);
         Print("PROTECTION: Closed ALL ", closed, " positions at DD=", DoubleToString(currentDD, 2), "%");
      }
      return;
   }

   //--- V3 Protection: Partial close (50%) if DD exceeds threshold
   if(currentDD > PartialCloseAtDD && !g_partialCloseDone)
   {
      int totalPos = CountPositions();
      if(totalPos > 1)
      {
         int toClose = totalPos / 2;
         int closed = CloseWorstPositions(toClose);
         if(closed > 0)
         {
            g_partialCloseDone = true;
            g_tradesClosedByProtection += closed;
            Print("PROTECTION: Closed ", closed, " worst positions at DD=", DoubleToString(currentDD, 2), "%");
         }
      }
   }

   //--- Update position tracking
   IteratePositions();

   //--- V7: Check volatility filter
   bool volatilityOK = IsVolatilityLow();

   //--- V10: Check mean reversion condition
   bool meanReversionOK = IsMeanReversionConditionMet(bid);

   //--- Open new positions (only if DD < threshold AND volatility is low AND mean reversion OK)
   if(currentDD < StopNewAtDD && volatilityOK && meanReversionOK)
   {
      OpenNewPositions(currentDD);
   }
}

//+------------------------------------------------------------------+
//| Count our positions                                                |
//+------------------------------------------------------------------+
int CountPositions()
{
   int count = 0;
   for(int i = PositionsTotal() - 1; i >= 0; i--)
   {
      ulong ticket = PositionGetTicket(i);
      if(ticket > 0 && PositionGetInteger(POSITION_MAGIC) == MagicNumber)
         count++;
   }
   return count;
}

//+------------------------------------------------------------------+
//| Iterate through positions and update tracking variables           |
//+------------------------------------------------------------------+
void IteratePositions()
{
   g_lowestBuy = DBL_MAX;
   g_highestBuy = DBL_MIN;
   g_volumeOfOpenTrades = 0;

   for(int i = PositionsTotal() - 1; i >= 0; i--)
   {
      ulong ticket = PositionGetTicket(i);
      if(ticket > 0 && PositionGetInteger(POSITION_MAGIC) == MagicNumber)
      {
         if(PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY)
         {
            double openPrice = PositionGetDouble(POSITION_PRICE_OPEN);
            double lots = PositionGetDouble(POSITION_VOLUME);

            g_volumeOfOpenTrades += lots;
            if(openPrice < g_lowestBuy) g_lowestBuy = openPrice;
            if(openPrice > g_highestBuy) g_highestBuy = openPrice;
         }
      }
   }
}

//+------------------------------------------------------------------+
//| Calculate DD-based lot scaling factor (V8 feature)                 |
//+------------------------------------------------------------------+
double GetDDLotScaleFactor(double currentDD)
{
   if(!EnableDDLotScaling)
      return 1.0;

   //--- No reduction if DD is below start threshold
   if(currentDD <= LotScaleStartDD)
      return 1.0;

   //--- Linear interpolation from 1.0 at LotScaleStartDD to LotScaleMinFactor at StopNewAtDD
   //--- Beyond StopNewAtDD, no new trades are opened anyway
   double ddRange = StopNewAtDD - LotScaleStartDD;
   if(ddRange <= 0)
      return 1.0;

   double ddProgress = (currentDD - LotScaleStartDD) / ddRange;
   ddProgress = MathMin(ddProgress, 1.0); // Cap at 1.0

   //--- Scale factor goes from 1.0 to LotScaleMinFactor
   double scaleFactor = 1.0 - (1.0 - LotScaleMinFactor) * ddProgress;

   return MathMax(scaleFactor, LotScaleMinFactor);
}

//+------------------------------------------------------------------+
//| Calculate position size with equity scaling and DD scaling (V8)    |
//+------------------------------------------------------------------+
double CalculateLotSize(double currentDD)
{
   double equity = AccountInfoDouble(ACCOUNT_EQUITY);
   double equityScale = 1.0;

   if(BaseEquity > 0)
   {
      if(UseSqrtScaling)
      {
         //--- Square root scaling: more conservative for large accounts
         //--- 10K -> 1x, 40K -> 2x, 90K -> 3x, 110K -> 3.3x, 160K -> 4x
         equityScale = MathSqrt(equity / BaseEquity);
      }
      else
      {
         //--- Linear scaling: aggressive, same % risk at all account sizes
         //--- 10K -> 1x, 40K -> 4x, 110K -> 11x
         equityScale = equity / BaseEquity;
      }
   }

   //--- V8: Apply DD-based lot scaling
   double ddScaleFactor = GetDDLotScaleFactor(currentDD);

   double lotSize = MinVolume * equityScale * ddScaleFactor;

   //--- Normalize lot size
   double minLot = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN);
   double maxLot = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MAX);
   double lotStep = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_STEP);

   lotSize = MathMax(minLot, lotSize);
   lotSize = MathMin(maxLot, lotSize);
   lotSize = MathMin(MaxVolume, lotSize);
   lotSize = MathFloor(lotSize / lotStep) * lotStep;

   return lotSize;
}

//+------------------------------------------------------------------+
//| Open new positions based on grid logic                            |
//+------------------------------------------------------------------+
void OpenNewPositions(double currentDD)
{
   int positionsTotal = CountPositions();

   if(positionsTotal >= MaxPositions)
      return;

   double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
   double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
   double spread = ask - bid;

   bool shouldOpen = false;

   if(positionsTotal == 0)
   {
      shouldOpen = true;
   }
   else
   {
      if(g_lowestBuy >= ask + Spacing)
         shouldOpen = true;
      else if(g_highestBuy <= ask - Spacing)
         shouldOpen = true;
   }

   if(shouldOpen)
   {
      double lotSize = CalculateLotSize(currentDD);
      double tp = ask + spread + (Spacing * TPMultiplier);

      //--- Check margin
      double marginRequired;
      if(!OrderCalcMargin(ORDER_TYPE_BUY, _Symbol, lotSize, ask, marginRequired))
         return;

      double freeMargin = AccountInfoDouble(ACCOUNT_MARGIN_FREE);
      if(freeMargin < marginRequired * 2)
         return;

      //--- Open position
      MqlTradeRequest request = {};
      MqlTradeResult result = {};

      request.action = TRADE_ACTION_DEAL;
      request.symbol = _Symbol;
      request.volume = lotSize;
      request.type = ORDER_TYPE_BUY;
      request.price = ask;
      request.tp = tp;
      request.sl = 0;
      request.deviation = 10;
      request.magic = MagicNumber;
      request.comment = TradeComment;
      request.type_filling = ORDER_FILLING_IOC;

      if(!OrderSend(request, result))
      {
         Print("OrderSend error: ", GetLastError());
      }
   }
}

//+------------------------------------------------------------------+
//| Close all positions                                                |
//+------------------------------------------------------------------+
int CloseAllPositions(string reason)
{
   int closed = 0;

   for(int i = PositionsTotal() - 1; i >= 0; i--)
   {
      ulong ticket = PositionGetTicket(i);
      if(ticket > 0 && PositionGetInteger(POSITION_MAGIC) == MagicNumber)
      {
         if(ClosePosition(ticket))
            closed++;
      }
   }

   return closed;
}

//+------------------------------------------------------------------+
//| Close worst performing positions                                   |
//+------------------------------------------------------------------+
int CloseWorstPositions(int count)
{
   struct PositionPL
   {
      ulong ticket;
      double profit;
   };

   PositionPL positions[];
   ArrayResize(positions, 0);

   for(int i = PositionsTotal() - 1; i >= 0; i--)
   {
      ulong ticket = PositionGetTicket(i);
      if(ticket > 0 && PositionGetInteger(POSITION_MAGIC) == MagicNumber)
      {
         int size = ArraySize(positions);
         ArrayResize(positions, size + 1);
         positions[size].ticket = ticket;
         positions[size].profit = PositionGetDouble(POSITION_PROFIT);
      }
   }

   //--- Sort by profit (worst first)
   int n = ArraySize(positions);
   for(int i = 0; i < n - 1; i++)
   {
      for(int j = i + 1; j < n; j++)
      {
         if(positions[j].profit < positions[i].profit)
         {
            PositionPL temp = positions[i];
            positions[i] = positions[j];
            positions[j] = temp;
         }
      }
   }

   int closed = 0;
   for(int i = 0; i < count && i < n; i++)
   {
      if(ClosePosition(positions[i].ticket))
         closed++;
   }

   return closed;
}

//+------------------------------------------------------------------+
//| Close a single position                                            |
//+------------------------------------------------------------------+
bool ClosePosition(ulong ticket)
{
   if(!PositionSelectByTicket(ticket))
      return false;

   MqlTradeRequest request = {};
   MqlTradeResult result = {};

   request.action = TRADE_ACTION_DEAL;
   request.symbol = PositionGetString(POSITION_SYMBOL);
   request.volume = PositionGetDouble(POSITION_VOLUME);
   request.deviation = 10;
   request.magic = MagicNumber;
   request.position = ticket;
   request.type_filling = ORDER_FILLING_IOC;

   if(PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY)
   {
      request.type = ORDER_TYPE_SELL;
      request.price = SymbolInfoDouble(request.symbol, SYMBOL_BID);
   }
   else
   {
      request.type = ORDER_TYPE_BUY;
      request.price = SymbolInfoDouble(request.symbol, SYMBOL_ASK);
   }

   return OrderSend(request, result);
}

//+------------------------------------------------------------------+
//| Trade transaction handler                                          |
//+------------------------------------------------------------------+
void OnTradeTransaction(const MqlTradeTransaction& trans,
                        const MqlTradeRequest& request,
                        const MqlTradeResult& result)
{
   if(trans.type == TRADE_TRANSACTION_DEAL_ADD)
   {
      if(trans.deal_type == DEAL_TYPE_SELL)
      {
         IteratePositions();
      }
   }
}
//+------------------------------------------------------------------+
