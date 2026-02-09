//+------------------------------------------------------------------+
//|                                            FillUp_V3_Verbose.mq5 |
//|                                    Fill-Up Grid Strategy V3       |
//|                         Verbose logging for backtest comparison   |
//+------------------------------------------------------------------+
#property copyright "Fill-Up V3"
#property link      ""
#property version   "3.00"
#property strict

//--- Input parameters
input group "=== Strategy Parameters ==="
input double   Spacing = 1.0;              // Grid spacing in price units
input double   MinVolume = 0.01;           // Minimum lot size
input double   MaxVolume = 100.0;          // Maximum lot size
input double   BaseEquity = 10000.0;       // Reference equity for lot scaling
input bool     UseSqrtScaling = true;      // Use square root scaling (safer for large accounts)

input group "=== V3 Protection Parameters ==="
input double   StopNewAtDD = 5.0;          // Stop new trades at this DD%
input double   PartialCloseAtDD = 8.0;     // Close 50% of positions at this DD%
input double   CloseAllAtDD = 25.0;        // Close ALL positions at this DD%
input int      MaxPositions = 20;          // Maximum open positions

input group "=== Logging ==="
input bool     VerboseLogging = true;      // Enable detailed logging
input int      LogEveryNTicks = 1000;      // Log status every N ticks

input group "=== General Settings ==="
input int      MagicNumber = 123458;       // Magic number for this EA
input string   TradeComment = "FillUp_V3"; // Trade comment

//--- Global variables
double g_peakEquity = 0;
double g_lowestBuy = DBL_MAX;
double g_highestBuy = DBL_MIN;
double g_volumeOfOpenTrades = 0;
bool g_partialCloseDone = false;
bool g_allClosed = false;
int g_tradesOpenedTotal = 0;
int g_tradesClosedByTP = 0;
int g_tradesClosedByProtection = 0;
long g_tickCount = 0;
double g_maxDD = 0;

//+------------------------------------------------------------------+
//| Expert initialization function                                     |
//+------------------------------------------------------------------+
int OnInit()
{
   g_peakEquity = AccountInfoDouble(ACCOUNT_EQUITY);

   Print("================================================================");
   Print("FillUp V3 Verbose initialized");
   Print("Protection: StopNew@", StopNewAtDD, "%, Partial@", PartialCloseAtDD,
         "%, CloseAll@", CloseAllAtDD, "%, MaxPos=", MaxPositions);
   Print("Initial Equity: ", g_peakEquity);
   Print("================================================================");

   return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+
//| Expert deinitialization function                                   |
//+------------------------------------------------------------------+
void OnDeinit(const int reason)
{
   Print("================================================================");
   Print("FillUp V3 Final Report:");
   Print("  Total ticks processed: ", g_tickCount);
   Print("  Trades opened: ", g_tradesOpenedTotal);
   Print("  Trades closed by TP: ", g_tradesClosedByTP);
   Print("  Trades closed by protection: ", g_tradesClosedByProtection);
   Print("  Max drawdown seen: ", DoubleToString(g_maxDD, 2), "%");
   Print("  Final equity: ", AccountInfoDouble(ACCOUNT_EQUITY));
   Print("  Final balance: ", AccountInfoDouble(ACCOUNT_BALANCE));
   Print("================================================================");
}

//+------------------------------------------------------------------+
//| Expert tick function                                               |
//+------------------------------------------------------------------+
void OnTick()
{
   g_tickCount++;

   double equity = AccountInfoDouble(ACCOUNT_EQUITY);
   double balance = AccountInfoDouble(ACCOUNT_BALANCE);

   //--- FIX: Reset peak equity when no positions are open
   //--- This prevents the strategy from getting stuck when DD > StopNewAtDD
   //--- but < CloseAllAtDD after all positions close naturally
   int posCount = CountPositions();
   if(posCount == 0)
   {
      //--- No open positions = reset to current balance
      //--- This allows fresh trading after positions close by TP
      if(g_peakEquity != balance)
      {
         if(VerboseLogging)
         {
            Print("[PEAK RESET] No positions open. Old peak=", DoubleToString(g_peakEquity, 2),
                  " New peak=", DoubleToString(balance, 2));
         }
         g_peakEquity = balance;
         g_partialCloseDone = false;
         g_allClosed = false;
      }
   }

   //--- Update peak equity (only when we have positions or equity exceeds peak)
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

   if(currentDD > g_maxDD)
      g_maxDD = currentDD;

   //--- Periodic logging
   if(VerboseLogging && g_tickCount % LogEveryNTicks == 0)
   {
      Print("[Tick ", g_tickCount, "] Equity=", DoubleToString(equity, 2),
            " DD=", DoubleToString(currentDD, 2), "%",
            " Positions=", CountPositions(),
            " Peak=", DoubleToString(g_peakEquity, 2));
   }

   //--- V3 Protection: Close ALL positions if DD exceeds threshold
   if(currentDD > CloseAllAtDD && !g_allClosed)
   {
      int posCount = CountPositions();
      if(posCount > 0)
      {
         Print(">>> PROTECTION TRIGGERED: CloseAll at DD=", DoubleToString(currentDD, 2), "%");
         int closed = CloseAllPositions();
         if(closed > 0)
         {
            g_allClosed = true;
            g_tradesClosedByProtection += closed;
            g_peakEquity = AccountInfoDouble(ACCOUNT_EQUITY);
            Print(">>> Closed ", closed, " positions. New peak equity: ", g_peakEquity);
         }
         return;
      }
   }

   //--- V3 Protection: Partial close (50%) if DD exceeds threshold
   if(currentDD > PartialCloseAtDD && !g_partialCloseDone)
   {
      int totalPos = CountPositions();
      if(totalPos > 1)
      {
         Print(">>> PROTECTION TRIGGERED: PartialClose at DD=", DoubleToString(currentDD, 2), "%");
         int toClose = totalPos / 2;
         int closed = CloseWorstPositions(toClose);
         if(closed > 0)
         {
            g_partialCloseDone = true;
            g_tradesClosedByProtection += closed;
            Print(">>> Closed ", closed, " worst positions");
         }
      }
   }

   //--- Update position tracking
   IteratePositions();

   //--- Open new positions (only if below DD threshold)
   if(currentDD < StopNewAtDD)
   {
      OpenNewPositions();
   }
   else if(VerboseLogging && g_tickCount % LogEveryNTicks == 0)
   {
      Print("[Tick ", g_tickCount, "] NOT opening - DD ", DoubleToString(currentDD, 2),
            "% > StopNew ", StopNewAtDD, "%");
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
//| Open new positions based on grid logic                            |
//+------------------------------------------------------------------+
void OpenNewPositions()
{
   int positionsTotal = CountPositions();

   //--- V3: Check position limit
   if(positionsTotal >= MaxPositions)
      return;

   double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
   double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
   double spread = ask - bid;

   bool shouldOpen = false;
   string reason = "";

   if(positionsTotal == 0)
   {
      shouldOpen = true;
      reason = "First position";
   }
   else
   {
      if(g_lowestBuy >= ask + Spacing)
      {
         shouldOpen = true;
         reason = "Below grid (lowest=" + DoubleToString(g_lowestBuy, 2) + ")";
      }
      else if(g_highestBuy <= ask - Spacing)
      {
         shouldOpen = true;
         reason = "Above grid (highest=" + DoubleToString(g_highestBuy, 2) + ")";
      }
   }

   if(shouldOpen)
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

      //--- Normalize lot size
      double minLot = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN);
      double maxLot = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MAX);
      double lotStep = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_STEP);
      lotSize = MathMax(minLot, lotSize);
      lotSize = MathMin(maxLot, lotSize);
      lotSize = MathMin(MaxVolume, lotSize);
      lotSize = MathFloor(lotSize / lotStep) * lotStep;

      double tp = ask + spread + Spacing;

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

      if(OrderSend(request, result))
      {
         g_tradesOpenedTotal++;
         if(VerboseLogging)
         {
            Print("[OPEN] #", result.order, " @ ", DoubleToString(ask, 2),
                  " TP=", DoubleToString(tp, 2), " Lot=", lotSize,
                  " Reason: ", reason, " TotalPos=", positionsTotal + 1);
         }
      }
      else
      {
         Print("OrderSend error: ", GetLastError());
      }
   }
}

//+------------------------------------------------------------------+
//| Close all positions                                                |
//+------------------------------------------------------------------+
int CloseAllPositions()
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
      if(VerboseLogging)
      {
         Print("[CLOSE WORST] Ticket=", positions[i].ticket,
               " P/L=", DoubleToString(positions[i].profit, 2));
      }
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
         //--- Check if this was a TP close
         HistoryDealSelect(trans.deal);
         ENUM_DEAL_REASON reason = (ENUM_DEAL_REASON)HistoryDealGetInteger(trans.deal, DEAL_REASON);
         if(reason == DEAL_REASON_TP)
         {
            g_tradesClosedByTP++;
            if(VerboseLogging)
            {
               double profit = HistoryDealGetDouble(trans.deal, DEAL_PROFIT);
               Print("[TP HIT] Deal=", trans.deal, " Profit=", DoubleToString(profit, 2));
            }
         }
         IteratePositions();
      }
   }
}
//+------------------------------------------------------------------+
