//+------------------------------------------------------------------+
//|                                        FillUp_V5_TrendFilter.mq5 |
//|                                  Fill-Up Grid Strategy V5/V6     |
//|          V3 Protection + SMA 11000 Trend Filter + V6 Wider TP    |
//+------------------------------------------------------------------+
#property copyright "Fill-Up V5/V6"
#property link      ""
#property version   "6.00"
#property strict

//--- Input parameters
input group "=== Strategy Parameters ==="
input double   Spacing = 1.0;              // Grid spacing in price units
input double   SurvivePct = 13.0;          // Survive percentage for sizing
input double   MinVolume = 0.01;           // Minimum lot size
input double   MaxVolume = 100.0;          // Maximum lot size
input double   BaseEquity = 10000.0;       // Reference equity for lot scaling
input bool     UseSqrtScaling = true;      // Use square root scaling (safer for large accounts)

input group "=== V3 Protection Parameters ==="
input double   StopNewAtDD = 5.0;          // Stop new trades at this DD%
input double   PartialCloseAtDD = 8.0;     // Close 50% of positions at this DD%
input double   CloseAllAtDD = 25.0;        // Close ALL positions at this DD%
input int      MaxPositions = 20;          // Maximum open positions
input double   ReduceSizeAtDD = 3.0;       // Start reducing lot size at this DD%

input group "=== V5 Trend Filter ==="
input int      MAPeriod = 11000;           // MA period for trend filter
input bool     EnableTrendFilter = true;   // Enable trend filter (price > MA)

input group "=== V6 Improvement ==="
input double   TPMultiplier = 2.0;         // Take profit multiplier (2.0 = 2x wider TP)

input group "=== General Settings ==="
input int      MagicNumber = 123457;       // Magic number for this EA
input string   TradeComment = "FillUp_V5"; // Trade comment

//--- Global variables
double g_peakEquity = 0;
double g_lowestBuy = DBL_MAX;
double g_highestBuy = DBL_MIN;
double g_volumeOfOpenTrades = 0;
bool g_partialCloseDone = false;
bool g_allClosed = false;
int g_tradesClosedByProtection = 0;

//--- MA variables
int g_maHandle = INVALID_HANDLE;
double g_maBuffer[];

//+------------------------------------------------------------------+
//| Expert initialization function                                     |
//+------------------------------------------------------------------+
int OnInit()
{
   //--- Initialize MA
   if(EnableTrendFilter)
   {
      g_maHandle = iMA(_Symbol, PERIOD_CURRENT, MAPeriod, 0, MODE_SMA, PRICE_CLOSE);
      if(g_maHandle == INVALID_HANDLE)
      {
         Print("Failed to create MA indicator handle");
         return(INIT_FAILED);
      }
      ArraySetAsSeries(g_maBuffer, true);
   }

   g_peakEquity = AccountInfoDouble(ACCOUNT_EQUITY);
   Print("FillUp V5 TrendFilter initialized");
   Print("Protection: StopNew@", StopNewAtDD, "%, Partial@", PartialCloseAtDD,
         "%, CloseAll@", CloseAllAtDD, "%, MaxPos=", MaxPositions);
   Print("Trend Filter: ", EnableTrendFilter ? "ENABLED" : "DISABLED", ", MA Period=", MAPeriod);
   return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+
//| Expert deinitialization function                                   |
//+------------------------------------------------------------------+
void OnDeinit(const int reason)
{
   if(g_maHandle != INVALID_HANDLE)
      IndicatorRelease(g_maHandle);
   Print("FillUp V5 stopped. Trades closed by protection: ", g_tradesClosedByProtection);
}

//+------------------------------------------------------------------+
//| Expert tick function                                               |
//+------------------------------------------------------------------+
void OnTick()
{
   double equity = AccountInfoDouble(ACCOUNT_EQUITY);
   double balance = AccountInfoDouble(ACCOUNT_BALANCE);

   //--- FIX: Reset peak equity when no positions are open
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

   //--- V5: Check trend filter
   bool trendOk = IsTrendOk();

   //--- Open new positions (only if below DD threshold AND trend is ok)
   if(currentDD < StopNewAtDD && trendOk)
   {
      OpenNewPositions(currentDD);
   }
}

//+------------------------------------------------------------------+
//| V5: Check if trend filter allows trading                         |
//+------------------------------------------------------------------+
bool IsTrendOk()
{
   if(!EnableTrendFilter)
      return true;  // Filter disabled, always OK

   if(g_maHandle == INVALID_HANDLE)
      return false;

   //--- Get MA value
   if(CopyBuffer(g_maHandle, 0, 0, 1, g_maBuffer) <= 0)
      return false;

   double ma = g_maBuffer[0];
   double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);

   //--- Only trade when price is above MA
   return bid > ma;
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
//| Calculate position size based on V3 logic + equity scaling       |
//+------------------------------------------------------------------+
double CalculateLotSize(double currentDD)
{
   //--- Scale lot size based on account equity relative to BaseEquity
   double equity = AccountInfoDouble(ACCOUNT_EQUITY);
   double equityScale = 1.0;

   if(BaseEquity > 0)
   {
      if(UseSqrtScaling)
      {
         //--- Square root scaling: more conservative for large accounts
         //--- 10K -> 1x, 40K -> 2x, 90K -> 3x, 110K -> 3.3x
         equityScale = MathSqrt(equity / BaseEquity);
      }
      else
      {
         //--- Linear scaling: aggressive
         //--- 10K -> 1x, 110K -> 11x
         equityScale = equity / BaseEquity;
      }
   }

   double lotSize = MinVolume * equityScale;

   //--- V3: Reduce size based on drawdown
   if(currentDD > ReduceSizeAtDD && StopNewAtDD > ReduceSizeAtDD)
   {
      double ddRange = StopNewAtDD - ReduceSizeAtDD;
      double ddProgress = (currentDD - ReduceSizeAtDD) / ddRange;
      double sizeReduction = 1.0 - (ddProgress * 0.75); // 100% to 25%
      if(sizeReduction < 0.25) sizeReduction = 0.25;
      lotSize *= sizeReduction;
   }

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

   //--- V3: Check position limit
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
      // V6: Apply TP multiplier for wider take profit
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
   //--- Build array of positions with their P/L
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

   //--- Close worst ones
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
//| Trade transaction handler for TP hits                              |
//+------------------------------------------------------------------+
void OnTradeTransaction(const MqlTradeTransaction& trans,
                        const MqlTradeRequest& request,
                        const MqlTradeResult& result)
{
   //--- When a position is closed by TP, update tracking
   if(trans.type == TRADE_TRANSACTION_DEAL_ADD)
   {
      if(trans.deal_type == DEAL_TYPE_SELL && trans.order_state == ORDER_STATE_FILLED)
      {
         //--- A buy position was closed
         IteratePositions();
      }
   }
}
//+------------------------------------------------------------------+
