//+------------------------------------------------------------------+
//|                                           FillUp_V1_Original.mq5 |
//|                                    Fill-Up Grid Strategy V1       |
//|                              Original version (NO protection)     |
//+------------------------------------------------------------------+
#property copyright "Fill-Up V1"
#property link      ""
#property version   "1.00"
#property strict

//--- Input parameters
input group "=== Strategy Parameters ==="
input double   Spacing = 1.0;              // Grid spacing in price units
input double   SurvivePct = 13.0;          // Survive percentage for sizing
input double   MinVolume = 0.01;           // Minimum lot size
input double   MaxVolume = 100.0;          // Maximum lot size

input group "=== General Settings ==="
input int      MagicNumber = 123457;       // Magic number for this EA
input string   TradeComment = "FillUp_V1"; // Trade comment

//--- Global variables
double g_lowestBuy = DBL_MAX;
double g_highestBuy = DBL_MIN;
double g_volumeOfOpenTrades = 0;

//+------------------------------------------------------------------+
//| Expert initialization function                                     |
//+------------------------------------------------------------------+
int OnInit()
{
   Print("FillUp V1 Original initialized (NO PROTECTION)");
   Print("WARNING: This version has no drawdown protection!");
   return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+
//| Expert deinitialization function                                   |
//+------------------------------------------------------------------+
void OnDeinit(const int reason)
{
   Print("FillUp V1 stopped");
}

//+------------------------------------------------------------------+
//| Expert tick function                                               |
//+------------------------------------------------------------------+
void OnTick()
{
   //--- Update position tracking
   IteratePositions();

   //--- Open new positions (no restrictions)
   OpenNewPositions();
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
      double lotSize = MinVolume;
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

      if(!OrderSend(request, result))
      {
         Print("OrderSend error: ", GetLastError());
      }
   }
}
//+------------------------------------------------------------------+
