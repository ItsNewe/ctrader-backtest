//+------------------------------------------------------------------+
//|                                    FillUp_V7_VolatilityFilter.mq5 |
//|                                    Fill-Up Grid Strategy V7       |
//|                    V3 Protection + Low Volatility Filter          |
//+------------------------------------------------------------------+
#property copyright "Fill-Up V7"
#property link      ""
#property version   "7.00"
#property strict

//--- Input parameters
input group "=== Strategy Parameters ==="
input double   Spacing = 1.0;              // Grid spacing in price units
input double   MinVolume = 0.01;           // Minimum lot size
input double   MaxVolume = 100.0;          // Maximum lot size
input double   BaseEquity = 10000.0;       // Reference equity for lot scaling
input bool     UseSqrtScaling = true;      // Use square root scaling (safer for large accounts)
input double   TPMultiplier = 2.0;         // Take profit multiplier (1.0 = normal, 2.0 = wider)

input group "=== V3 Protection Parameters ==="
input double   StopNewAtDD = 5.0;          // Stop new trades at this DD%
input double   PartialCloseAtDD = 8.0;     // Close 50% of positions at this DD%
input double   CloseAllAtDD = 25.0;        // Close ALL positions at this DD%
input int      MaxPositions = 20;          // Maximum open positions

input group "=== V7 Volatility Filter ==="
input int      ATRShortPeriod = 50;        // Short-term ATR period (bars)
input int      ATRLongPeriod = 1000;       // Long-term ATR period (bars)
input double   VolatilityThreshold = 0.6;  // Trade when short ATR < long ATR * this (0.6 = 40% below avg)
input bool     EnableVolatilityFilter = true; // Enable volatility filter

input group "=== General Settings ==="
input int      MagicNumber = 123459;       // Magic number for this EA
input string   TradeComment = "FillUp_V7"; // Trade comment

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

   g_peakEquity = AccountInfoDouble(ACCOUNT_EQUITY);

   Print("================================================================");
   Print("FillUp V7 VolatilityFilter initialized");
   Print("Protection: StopNew@", StopNewAtDD, "%, Partial@", PartialCloseAtDD,
         "%, CloseAll@", CloseAllAtDD, "%, MaxPos=", MaxPositions);
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

   Print("FillUp V7 stopped. Trades closed by protection: ", g_tradesClosedByProtection);
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
//| Expert tick function                                               |
//+------------------------------------------------------------------+
void OnTick()
{
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

   //--- Open new positions (only if DD < threshold AND volatility is low)
   if(currentDD < StopNewAtDD && volatilityOK)
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
//| Calculate position size with equity scaling                        |
//+------------------------------------------------------------------+
double CalculateLotSize()
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

   double lotSize = MinVolume * equityScale;

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
      double lotSize = CalculateLotSize();
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
