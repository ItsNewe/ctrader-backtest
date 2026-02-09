//+------------------------------------------------------------------+
//|                                           FillUpAdaptive_v2.mq5  |
//|                                    Oscillation-Stabilized Grid   |
//|                                         Adaptive Spacing Mode    |
//|                                          With Dashboard & Stats  |
//+------------------------------------------------------------------+
#property copyright "Backtest Framework"
#property link      ""
#property version   "2.00"
#property strict

//+------------------------------------------------------------------+
//| Input Parameters                                                  |
//+------------------------------------------------------------------+
input group "=== Core Strategy Parameters ==="
input double SurvivePct = 13.0;           // Survive % (max price drop tolerance)
input double BaseSpacing = 1.0;           // Base grid spacing ($)
input double MinVolume = 0.01;            // Minimum lot size
input double MaxVolume = 10.0;            // Maximum lot size

input group "=== Adaptive Spacing ==="
input double VolatilityLookbackHours = 1.0;  // Volatility lookback (hours)
input double TypicalVolatility = 10.0;       // Typical range ($) for gold
input double MinSpacingMult = 0.5;           // Min spacing multiplier
input double MaxSpacingMult = 3.0;           // Max spacing multiplier

input group "=== Display & Safety ==="
input bool ShowDashboard = true;          // Show info panel
input bool EnableTrading = true;          // Enable trading (false = monitor only)
input int MagicNumber = 123456;           // Magic number

//+------------------------------------------------------------------+
//| Global Variables                                                  |
//+------------------------------------------------------------------+
// Adaptive spacing
double g_currentSpacing;
double g_recentHigh;
double g_recentLow;
datetime g_lastVolatilityReset;
int g_spacingChanges;

// Symbol info
double g_contractSize;
double g_leverage;
int g_digits;
double g_point;
double g_tickSize;
double g_lotStep;
double g_minLot;
double g_maxLot;

// Statistics
int g_totalTrades;
int g_winningTrades;
double g_totalProfit;
double g_peakEquity;
double g_maxDrawdown;
double g_maxDrawdownPct;
datetime g_startTime;

//+------------------------------------------------------------------+
//| Expert initialization                                             |
//+------------------------------------------------------------------+
int OnInit()
{
    // Get symbol info
    g_contractSize = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);
    g_leverage = (double)AccountInfoInteger(ACCOUNT_LEVERAGE);
    g_digits = (int)SymbolInfoInteger(_Symbol, SYMBOL_DIGITS);
    g_point = SymbolInfoDouble(_Symbol, SYMBOL_POINT);
    g_tickSize = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_TICK_SIZE);
    g_lotStep = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_STEP);
    g_minLot = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN);
    g_maxLot = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MAX);

    // Initialize adaptive spacing
    g_currentSpacing = BaseSpacing;
    g_recentHigh = 0;
    g_recentLow = DBL_MAX;
    g_lastVolatilityReset = 0;
    g_spacingChanges = 0;

    // Initialize statistics
    g_totalTrades = 0;
    g_winningTrades = 0;
    g_totalProfit = 0;
    g_peakEquity = AccountInfoDouble(ACCOUNT_EQUITY);
    g_maxDrawdown = 0;
    g_maxDrawdownPct = 0;
    g_startTime = TimeCurrent();

    // Create dashboard
    if(ShowDashboard)
    {
        CreateDashboard();
    }

    Print("=== FillUp Adaptive v2 Initialized ===");
    PrintSymbolInfo();

    return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+
//| Expert deinitialization                                           |
//+------------------------------------------------------------------+
void OnDeinit(const int reason)
{
    // Remove dashboard objects
    ObjectsDeleteAll(0, "FillUp_");

    Print("=== EA Stopped ===");
    PrintStatistics();
}

//+------------------------------------------------------------------+
//| Expert tick function                                              |
//+------------------------------------------------------------------+
void OnTick()
{
    // Update volatility and spacing
    UpdateVolatility();
    UpdateAdaptiveSpacing();

    // Update statistics
    UpdateStatistics();

    // Update dashboard
    if(ShowDashboard)
    {
        UpdateDashboard();
    }

    // Skip trading if disabled
    if(!EnableTrading) return;

    // Get market data
    double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
    double spread = ask - bid;

    // Analyze current positions
    double lowestBuy = DBL_MAX;
    double highestBuy = -DBL_MAX;
    double totalVolume = 0;
    int positionCount = 0;

    for(int i = PositionsTotal() - 1; i >= 0; i--)
    {
        ulong ticket = PositionGetTicket(i);
        if(ticket > 0 && PositionGetString(POSITION_SYMBOL) == _Symbol &&
           PositionGetInteger(POSITION_MAGIC) == MagicNumber)
        {
            if(PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY)
            {
                double entryPrice = PositionGetDouble(POSITION_PRICE_OPEN);
                double lots = PositionGetDouble(POSITION_VOLUME);

                lowestBuy = MathMin(lowestBuy, entryPrice);
                highestBuy = MathMax(highestBuy, entryPrice);
                totalVolume += lots;
                positionCount++;
            }
        }
    }

    // Determine if new position needed
    bool shouldOpen = false;

    if(positionCount == 0)
    {
        shouldOpen = true;  // First position
    }
    else if(lowestBuy >= ask + g_currentSpacing)
    {
        shouldOpen = true;  // Price dropped - buy the dip
    }
    else if(highestBuy <= ask - g_currentSpacing)
    {
        shouldOpen = true;  // Price rose - new higher entry
    }

    if(shouldOpen)
    {
        double lots = CalculateLotSize(ask, positionCount, totalVolume, highestBuy);
        if(lots >= g_minLot)
        {
            if(OpenBuyOrder(lots, ask, spread))
            {
                g_totalTrades++;
            }
        }
    }
}

//+------------------------------------------------------------------+
//| Handle trade events (for statistics)                              |
//+------------------------------------------------------------------+
void OnTrade()
{
    // Check for closed trades
    static int lastDealsTotal = 0;
    int currentDealsTotal = HistoryDealsTotal();

    if(currentDealsTotal > lastDealsTotal)
    {
        // New deal detected - check if it's our closed position
        HistorySelect(g_startTime, TimeCurrent());

        for(int i = HistoryDealsTotal() - 1; i >= lastDealsTotal; i--)
        {
            ulong dealTicket = HistoryDealGetTicket(i);
            if(dealTicket > 0)
            {
                if(HistoryDealGetString(dealTicket, DEAL_SYMBOL) == _Symbol &&
                   HistoryDealGetInteger(dealTicket, DEAL_MAGIC) == MagicNumber &&
                   HistoryDealGetInteger(dealTicket, DEAL_ENTRY) == DEAL_ENTRY_OUT)
                {
                    double profit = HistoryDealGetDouble(dealTicket, DEAL_PROFIT);
                    g_totalProfit += profit;
                    if(profit > 0) g_winningTrades++;
                }
            }
        }
    }
    lastDealsTotal = currentDealsTotal;
}

//+------------------------------------------------------------------+
//| Update volatility tracking                                        |
//+------------------------------------------------------------------+
void UpdateVolatility()
{
    double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
    datetime currentTime = TimeCurrent();
    int lookbackSeconds = (int)(VolatilityLookbackHours * 3600);

    if(g_lastVolatilityReset == 0 || currentTime - g_lastVolatilityReset >= lookbackSeconds)
    {
        g_recentHigh = bid;
        g_recentLow = bid;
        g_lastVolatilityReset = currentTime;
    }

    g_recentHigh = MathMax(g_recentHigh, bid);
    g_recentLow = MathMin(g_recentLow, bid);
}

//+------------------------------------------------------------------+
//| Update adaptive spacing                                           |
//+------------------------------------------------------------------+
void UpdateAdaptiveSpacing()
{
    double range = g_recentHigh - g_recentLow;

    if(range > 0 && g_recentHigh > 0)
    {
        double volRatio = range / TypicalVolatility;
        volRatio = MathMax(MinSpacingMult, MathMin(MaxSpacingMult, volRatio));

        double newSpacing = BaseSpacing * volRatio;
        newSpacing = MathMax(0.5, MathMin(5.0, newSpacing));

        if(MathAbs(newSpacing - g_currentSpacing) > 0.1)
        {
            Print("Spacing: $", DoubleToString(g_currentSpacing, 2),
                  " -> $", DoubleToString(newSpacing, 2),
                  " (range=$", DoubleToString(range, 2), ")");
            g_currentSpacing = newSpacing;
            g_spacingChanges++;
        }
    }
}

//+------------------------------------------------------------------+
//| Update drawdown statistics                                        |
//+------------------------------------------------------------------+
void UpdateStatistics()
{
    double equity = AccountInfoDouble(ACCOUNT_EQUITY);

    if(equity > g_peakEquity)
    {
        g_peakEquity = equity;
    }

    double drawdown = g_peakEquity - equity;
    double drawdownPct = (g_peakEquity > 0) ? (drawdown / g_peakEquity * 100) : 0;

    if(drawdown > g_maxDrawdown)
    {
        g_maxDrawdown = drawdown;
        g_maxDrawdownPct = drawdownPct;
    }
}

//+------------------------------------------------------------------+
//| Calculate lot size                                                |
//+------------------------------------------------------------------+
double CalculateLotSize(double currentAsk, int positionsTotal,
                        double volumeOfOpenTrades, double highestBuy)
{
    double equity = AccountInfoDouble(ACCOUNT_EQUITY);

    // Calculate used margin
    double usedMargin = 0;
    for(int i = PositionsTotal() - 1; i >= 0; i--)
    {
        ulong ticket = PositionGetTicket(i);
        if(ticket > 0 && PositionGetString(POSITION_SYMBOL) == _Symbol &&
           PositionGetInteger(POSITION_MAGIC) == MagicNumber)
        {
            double lots = PositionGetDouble(POSITION_VOLUME);
            double entryPrice = PositionGetDouble(POSITION_PRICE_OPEN);
            usedMargin += lots * g_contractSize * entryPrice / g_leverage;
        }
    }

    // Calculate worst-case price
    double endPrice = (positionsTotal == 0)
        ? currentAsk * ((100.0 - SurvivePct) / 100.0)
        : highestBuy * ((100.0 - SurvivePct) / 100.0);

    double distance = currentAsk - endPrice;
    double numberOfTrades = MathFloor(distance / g_currentSpacing);
    if(numberOfTrades <= 0) numberOfTrades = 1;

    // Potential equity at worst case
    double equityAtTarget = equity - volumeOfOpenTrades * distance * g_contractSize;

    // Margin stop-out check (20% level)
    double marginStopOut = 20.0;
    if(usedMargin > 0 && (equityAtTarget / usedMargin * 100.0) < marginStopOut)
    {
        return 0.0;
    }

    // Calculate lot size
    double tradeSize = MinVolume;
    double dEquity = g_contractSize * tradeSize * g_currentSpacing *
                     (numberOfTrades * (numberOfTrades + 1) / 2);
    double dMargin = numberOfTrades * tradeSize * g_contractSize / g_leverage;

    double maxMult = MaxVolume / MinVolume;
    for(double mult = maxMult; mult >= 1.0; mult -= 0.1)
    {
        double testEquity = equityAtTarget - mult * dEquity;
        double testMargin = usedMargin + mult * dMargin;

        if(testMargin > 0 && (testEquity / testMargin * 100.0) > marginStopOut)
        {
            tradeSize = mult * MinVolume;
            break;
        }
    }

    // Normalize to lot step
    tradeSize = MathFloor(tradeSize / g_lotStep) * g_lotStep;
    tradeSize = MathMax(g_minLot, MathMin(g_maxLot, tradeSize));

    return MathMin(tradeSize, MaxVolume);
}

//+------------------------------------------------------------------+
//| Open buy order                                                    |
//+------------------------------------------------------------------+
bool OpenBuyOrder(double lots, double ask, double spread)
{
    double tp = NormalizeDouble(ask + spread + g_currentSpacing, g_digits);

    MqlTradeRequest request = {};
    MqlTradeResult result = {};

    request.action = TRADE_ACTION_DEAL;
    request.symbol = _Symbol;
    request.volume = lots;
    request.type = ORDER_TYPE_BUY;
    request.price = ask;
    request.sl = 0;
    request.tp = tp;
    request.deviation = 10;
    request.magic = MagicNumber;
    request.comment = "FillUp v2";
    request.type_filling = ORDER_FILLING_IOC;

    if(!OrderSend(request, result))
    {
        Print("OrderSend error: ", GetLastError());
        return false;
    }

    if(result.retcode == TRADE_RETCODE_DONE || result.retcode == TRADE_RETCODE_PLACED)
    {
        Print("BUY ", DoubleToString(lots, 2),
              " @ ", DoubleToString(ask, g_digits),
              " TP: ", DoubleToString(tp, g_digits));
        return true;
    }

    return false;
}

//+------------------------------------------------------------------+
//| Create dashboard objects                                          |
//+------------------------------------------------------------------+
void CreateDashboard()
{
    int x = 10, y = 30, width = 220, height = 280;

    // Background
    ObjectCreate(0, "FillUp_BG", OBJ_RECTANGLE_LABEL, 0, 0, 0);
    ObjectSetInteger(0, "FillUp_BG", OBJPROP_XDISTANCE, x);
    ObjectSetInteger(0, "FillUp_BG", OBJPROP_YDISTANCE, y);
    ObjectSetInteger(0, "FillUp_BG", OBJPROP_XSIZE, width);
    ObjectSetInteger(0, "FillUp_BG", OBJPROP_YSIZE, height);
    ObjectSetInteger(0, "FillUp_BG", OBJPROP_BGCOLOR, clrBlack);
    ObjectSetInteger(0, "FillUp_BG", OBJPROP_BORDER_TYPE, BORDER_FLAT);
    ObjectSetInteger(0, "FillUp_BG", OBJPROP_COLOR, clrGold);
    ObjectSetInteger(0, "FillUp_BG", OBJPROP_CORNER, CORNER_LEFT_UPPER);

    // Title
    CreateLabel("FillUp_Title", x + 10, y + 10, "FillUp Adaptive v2", clrGold, 12);

    // Labels
    int lineHeight = 18;
    int startY = y + 40;

    CreateLabel("FillUp_L1", x + 10, startY, "Spacing:", clrWhite, 9);
    CreateLabel("FillUp_V1", x + 120, startY, "-", clrLime, 9);

    CreateLabel("FillUp_L2", x + 10, startY + lineHeight, "Volatility:", clrWhite, 9);
    CreateLabel("FillUp_V2", x + 120, startY + lineHeight, "-", clrLime, 9);

    CreateLabel("FillUp_L3", x + 10, startY + lineHeight*2, "Positions:", clrWhite, 9);
    CreateLabel("FillUp_V3", x + 120, startY + lineHeight*2, "-", clrLime, 9);

    CreateLabel("FillUp_L4", x + 10, startY + lineHeight*3, "Volume:", clrWhite, 9);
    CreateLabel("FillUp_V4", x + 120, startY + lineHeight*3, "-", clrLime, 9);

    CreateLabel("FillUp_L5", x + 10, startY + lineHeight*4, "Unrealized:", clrWhite, 9);
    CreateLabel("FillUp_V5", x + 120, startY + lineHeight*4, "-", clrLime, 9);

    CreateLabel("FillUp_L6", x + 10, startY + lineHeight*5, "Equity:", clrWhite, 9);
    CreateLabel("FillUp_V6", x + 120, startY + lineHeight*5, "-", clrLime, 9);

    CreateLabel("FillUp_L7", x + 10, startY + lineHeight*6, "Drawdown:", clrWhite, 9);
    CreateLabel("FillUp_V7", x + 120, startY + lineHeight*6, "-", clrOrange, 9);

    CreateLabel("FillUp_L8", x + 10, startY + lineHeight*7, "Max DD:", clrWhite, 9);
    CreateLabel("FillUp_V8", x + 120, startY + lineHeight*7, "-", clrOrange, 9);

    CreateLabel("FillUp_L9", x + 10, startY + lineHeight*8, "Trades:", clrWhite, 9);
    CreateLabel("FillUp_V9", x + 120, startY + lineHeight*8, "-", clrLime, 9);

    CreateLabel("FillUp_L10", x + 10, startY + lineHeight*9, "Profit:", clrWhite, 9);
    CreateLabel("FillUp_V10", x + 120, startY + lineHeight*9, "-", clrLime, 9);

    CreateLabel("FillUp_L11", x + 10, startY + lineHeight*10, "Win Rate:", clrWhite, 9);
    CreateLabel("FillUp_V11", x + 120, startY + lineHeight*10, "-", clrLime, 9);

    // Status
    string status = EnableTrading ? "TRADING" : "MONITOR ONLY";
    color statusColor = EnableTrading ? clrLime : clrYellow;
    CreateLabel("FillUp_Status", x + 10, startY + lineHeight*12, status, statusColor, 10);
}

//+------------------------------------------------------------------+
//| Create a text label                                               |
//+------------------------------------------------------------------+
void CreateLabel(string name, int x, int y, string text, color clr, int fontSize)
{
    ObjectCreate(0, name, OBJ_LABEL, 0, 0, 0);
    ObjectSetInteger(0, name, OBJPROP_XDISTANCE, x);
    ObjectSetInteger(0, name, OBJPROP_YDISTANCE, y);
    ObjectSetString(0, name, OBJPROP_TEXT, text);
    ObjectSetInteger(0, name, OBJPROP_COLOR, clr);
    ObjectSetInteger(0, name, OBJPROP_FONTSIZE, fontSize);
    ObjectSetString(0, name, OBJPROP_FONT, "Consolas");
    ObjectSetInteger(0, name, OBJPROP_CORNER, CORNER_LEFT_UPPER);
}

//+------------------------------------------------------------------+
//| Update dashboard values                                           |
//+------------------------------------------------------------------+
void UpdateDashboard()
{
    double equity = AccountInfoDouble(ACCOUNT_EQUITY);
    double drawdown = g_peakEquity - equity;
    double drawdownPct = (g_peakEquity > 0) ? (drawdown / g_peakEquity * 100) : 0;
    double range = g_recentHigh - g_recentLow;

    // Get position info
    int posCount = 0;
    double totalVol = 0;
    double unrealizedPL = 0;

    for(int i = PositionsTotal() - 1; i >= 0; i--)
    {
        ulong ticket = PositionGetTicket(i);
        if(ticket > 0 && PositionGetString(POSITION_SYMBOL) == _Symbol &&
           PositionGetInteger(POSITION_MAGIC) == MagicNumber)
        {
            posCount++;
            totalVol += PositionGetDouble(POSITION_VOLUME);
            unrealizedPL += PositionGetDouble(POSITION_PROFIT);
        }
    }

    // Update labels
    ObjectSetString(0, "FillUp_V1", OBJPROP_TEXT, "$" + DoubleToString(g_currentSpacing, 2));
    ObjectSetString(0, "FillUp_V2", OBJPROP_TEXT, "$" + DoubleToString(range, 2));
    ObjectSetString(0, "FillUp_V3", OBJPROP_TEXT, IntegerToString(posCount));
    ObjectSetString(0, "FillUp_V4", OBJPROP_TEXT, DoubleToString(totalVol, 2) + " lots");

    // Unrealized P/L with color
    ObjectSetString(0, "FillUp_V5", OBJPROP_TEXT, "$" + DoubleToString(unrealizedPL, 2));
    ObjectSetInteger(0, "FillUp_V5", OBJPROP_COLOR, unrealizedPL >= 0 ? clrLime : clrRed);

    ObjectSetString(0, "FillUp_V6", OBJPROP_TEXT, "$" + DoubleToString(equity, 2));
    ObjectSetString(0, "FillUp_V7", OBJPROP_TEXT, DoubleToString(drawdownPct, 1) + "%");
    ObjectSetString(0, "FillUp_V8", OBJPROP_TEXT, DoubleToString(g_maxDrawdownPct, 1) + "%");
    ObjectSetString(0, "FillUp_V9", OBJPROP_TEXT, IntegerToString(g_totalTrades));
    ObjectSetString(0, "FillUp_V10", OBJPROP_TEXT, "$" + DoubleToString(g_totalProfit, 2));
    ObjectSetInteger(0, "FillUp_V10", OBJPROP_COLOR, g_totalProfit >= 0 ? clrLime : clrRed);

    double winRate = (g_totalTrades > 0) ? (g_winningTrades * 100.0 / g_totalTrades) : 0;
    ObjectSetString(0, "FillUp_V11", OBJPROP_TEXT, DoubleToString(winRate, 1) + "%");

    ChartRedraw();
}

//+------------------------------------------------------------------+
//| Print symbol info on init                                         |
//+------------------------------------------------------------------+
void PrintSymbolInfo()
{
    Print("Symbol: ", _Symbol);
    Print("Contract Size: ", g_contractSize);
    Print("Leverage: 1:", g_leverage);
    Print("Digits: ", g_digits);
    Print("Lot Step: ", g_lotStep);
    Print("Min Lot: ", g_minLot);
    Print("Max Lot: ", g_maxLot);
    Print("---");
    Print("Survive %: ", SurvivePct);
    Print("Base Spacing: $", BaseSpacing);
    Print("Lookback: ", VolatilityLookbackHours, " hours");
}

//+------------------------------------------------------------------+
//| Print final statistics                                            |
//+------------------------------------------------------------------+
void PrintStatistics()
{
    Print("=== Final Statistics ===");
    Print("Total Trades: ", g_totalTrades);
    Print("Winning Trades: ", g_winningTrades);
    Print("Total Profit: $", DoubleToString(g_totalProfit, 2));
    Print("Max Drawdown: ", DoubleToString(g_maxDrawdownPct, 2), "%");
    Print("Spacing Changes: ", g_spacingChanges);
}
//+------------------------------------------------------------------+
