//+------------------------------------------------------------------+
//|                                           FillUpAdaptive_v4.mq5  |
//|                                    Oscillation-Stabilized Grid   |
//|                                   Percentage-Based Grid Spacing  |
//|                        All spacing params scale with price level  |
//+------------------------------------------------------------------+
#property copyright "Backtest Framework"
#property link      ""
#property version   "4.00"
#property strict

//+------------------------------------------------------------------+
//| Input Parameters                                                  |
//+------------------------------------------------------------------+
input group "=== Core Strategy Parameters ==="
input double SurvivePct = 19.0;           // Survive % (max price drop tolerance)
input double BaseSpacingPct = 2.0;        // Base grid spacing (% of price)
input double MinVolume = 0.01;            // Minimum lot size
input double MaxVolume = 100.0;           // Maximum lot size

input group "=== Adaptive Spacing ==="
input double VolatilityLookbackHours = 1.0;  // Volatility lookback (hours)
input double TypicalVolPct = 0.45;           // Typical volatility (% of price) [XAGUSD 1h=0.45, 4h=0.97]
input double MinSpacingMult = 0.5;           // Min spacing multiplier
input double MaxSpacingMult = 3.0;           // Max spacing multiplier
input double MinSpacingPct = 0.05;           // Min absolute spacing (% of price)
input double MaxSpacingPct = 15.0;           // Max absolute spacing (% of price)
input double SpacingChangeThresholdPct = 0.2; // Spacing change threshold (% of price)

input group "=== Display & Safety ==="
input bool ShowDashboard = true;          // Show info panel
input bool EnableTrading = true;          // Enable trading (false = monitor only)
input int MagicNumber = 123458;           // Magic number

//+------------------------------------------------------------------+
//| Global Variables                                                  |
//+------------------------------------------------------------------+
// Adaptive spacing
double g_currentSpacing;
double g_recentHigh;
double g_recentLow;
datetime g_lastVolatilityReset;
int g_spacingChanges;
double g_typicalVol;
double g_effectiveBase;

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

    // Initialize adaptive spacing from current price
    double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
    if(bid > 0)
    {
        g_currentSpacing = bid * (BaseSpacingPct / 100.0);
        g_effectiveBase = g_currentSpacing;
    }
    else
    {
        g_currentSpacing = 1.0;  // Fallback
        g_effectiveBase = 1.0;
    }

    g_recentHigh = 0;
    g_recentLow = DBL_MAX;
    g_lastVolatilityReset = 0;
    g_spacingChanges = 0;
    g_typicalVol = 0;

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

    Print("=== FillUp Adaptive v4 Initialized ===");
    Print("=== Percentage-Based Grid Spacing ===");
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
//| Update adaptive spacing (fully percentage-based)                  |
//+------------------------------------------------------------------+
void UpdateAdaptiveSpacing()
{
    double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
    double range = g_recentHigh - g_recentLow;

    if(range > 0 && g_recentHigh > 0 && bid > 0)
    {
        // All parameters scale with current price
        g_typicalVol = bid * (TypicalVolPct / 100.0);
        g_effectiveBase = bid * (BaseSpacingPct / 100.0);
        double minAbs = bid * (MinSpacingPct / 100.0);
        double maxAbs = bid * (MaxSpacingPct / 100.0);
        double changeThresh = bid * (SpacingChangeThresholdPct / 100.0);

        // Calculate vol ratio and clamp to multiplier range
        double volRatio = range / g_typicalVol;
        volRatio = MathMax(MinSpacingMult, MathMin(MaxSpacingMult, volRatio));

        // Calculate new spacing
        double newSpacing = g_effectiveBase * volRatio;
        newSpacing = MathMax(minAbs, MathMin(maxAbs, newSpacing));

        // Only update if change exceeds threshold
        if(MathAbs(newSpacing - g_currentSpacing) > changeThresh)
        {
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

    // Calculate lot size using triangular position sizing
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
    request.comment = "FillUp v4 " + DoubleToString(BaseSpacingPct, 1) + "%";
    request.type_filling = ORDER_FILLING_IOC;

    if(!OrderSend(request, result))
    {
        Print("OrderSend error: ", GetLastError());
        return false;
    }

    if(result.retcode == TRADE_RETCODE_DONE || result.retcode == TRADE_RETCODE_PLACED)
    {
        double spacingPct = (ask > 0) ? (g_currentSpacing / ask * 100.0) : 0;
        Print("BUY ", DoubleToString(lots, 2),
              " @ ", DoubleToString(ask, g_digits),
              " TP: ", DoubleToString(tp, g_digits),
              " Sp: $", DoubleToString(g_currentSpacing, g_digits),
              " (", DoubleToString(spacingPct, 2), "%)");
        return true;
    }

    return false;
}

//+------------------------------------------------------------------+
//| Create dashboard objects                                          |
//+------------------------------------------------------------------+
void CreateDashboard()
{
    int x = 10, y = 30, width = 260, height = 360;

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
    CreateLabel("FillUp_Title", x + 10, y + 10, "FillUp Adaptive v4", clrGold, 12);
    CreateLabel("FillUp_Sub", x + 10, y + 28, "%-Based Grid Spacing", clrDarkGray, 8);

    // Labels
    int lineHeight = 18;
    int startY = y + 50;

    CreateLabel("FillUp_L1", x + 10, startY, "Spacing:", clrWhite, 9);
    CreateLabel("FillUp_V1", x + 140, startY, "-", clrLime, 9);

    CreateLabel("FillUp_L13", x + 10, startY + lineHeight, "Spacing %:", clrWhite, 9);
    CreateLabel("FillUp_V13", x + 140, startY + lineHeight, "-", clrCyan, 9);

    CreateLabel("FillUp_L2", x + 10, startY + lineHeight*2, "Volatility:", clrWhite, 9);
    CreateLabel("FillUp_V2", x + 140, startY + lineHeight*2, "-", clrLime, 9);

    CreateLabel("FillUp_L12", x + 10, startY + lineHeight*3, "Typical Vol:", clrWhite, 9);
    CreateLabel("FillUp_V12", x + 140, startY + lineHeight*3, "-", clrCyan, 9);

    CreateLabel("FillUp_L3", x + 10, startY + lineHeight*4, "Positions:", clrWhite, 9);
    CreateLabel("FillUp_V3", x + 140, startY + lineHeight*4, "-", clrLime, 9);

    CreateLabel("FillUp_L4", x + 10, startY + lineHeight*5, "Volume:", clrWhite, 9);
    CreateLabel("FillUp_V4", x + 140, startY + lineHeight*5, "-", clrLime, 9);

    CreateLabel("FillUp_L5", x + 10, startY + lineHeight*6, "Unrealized:", clrWhite, 9);
    CreateLabel("FillUp_V5", x + 140, startY + lineHeight*6, "-", clrLime, 9);

    CreateLabel("FillUp_L6", x + 10, startY + lineHeight*7, "Equity:", clrWhite, 9);
    CreateLabel("FillUp_V6", x + 140, startY + lineHeight*7, "-", clrLime, 9);

    CreateLabel("FillUp_L7", x + 10, startY + lineHeight*8, "Drawdown:", clrWhite, 9);
    CreateLabel("FillUp_V7", x + 140, startY + lineHeight*8, "-", clrOrange, 9);

    CreateLabel("FillUp_L8", x + 10, startY + lineHeight*9, "Max DD:", clrWhite, 9);
    CreateLabel("FillUp_V8", x + 140, startY + lineHeight*9, "-", clrOrange, 9);

    CreateLabel("FillUp_L9", x + 10, startY + lineHeight*10, "Trades:", clrWhite, 9);
    CreateLabel("FillUp_V9", x + 140, startY + lineHeight*10, "-", clrLime, 9);

    CreateLabel("FillUp_L10", x + 10, startY + lineHeight*11, "Profit:", clrWhite, 9);
    CreateLabel("FillUp_V10", x + 140, startY + lineHeight*11, "-", clrLime, 9);

    CreateLabel("FillUp_L11", x + 10, startY + lineHeight*12, "Win Rate:", clrWhite, 9);
    CreateLabel("FillUp_V11", x + 140, startY + lineHeight*12, "-", clrLime, 9);

    CreateLabel("FillUp_L14", x + 10, startY + lineHeight*13, "Sp Changes:", clrWhite, 9);
    CreateLabel("FillUp_V14", x + 140, startY + lineHeight*13, "-", clrLime, 9);

    // Status
    string status = EnableTrading ? "TRADING" : "MONITOR ONLY";
    color statusColor = EnableTrading ? clrLime : clrYellow;
    CreateLabel("FillUp_Status", x + 10, startY + lineHeight*15, status, statusColor, 10);
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
    double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
    double drawdown = g_peakEquity - equity;
    double drawdownPct = (g_peakEquity > 0) ? (drawdown / g_peakEquity * 100) : 0;
    double range = g_recentHigh - g_recentLow;
    double spacingPct = (bid > 0) ? (g_currentSpacing / bid * 100.0) : 0;

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
    ObjectSetString(0, "FillUp_V1", OBJPROP_TEXT, "$" + DoubleToString(g_currentSpacing, g_digits));
    ObjectSetString(0, "FillUp_V13", OBJPROP_TEXT, DoubleToString(spacingPct, 3) + "%");
    ObjectSetString(0, "FillUp_V2", OBJPROP_TEXT, "$" + DoubleToString(range, 2));
    ObjectSetString(0, "FillUp_V12", OBJPROP_TEXT, "$" + DoubleToString(g_typicalVol, 2));
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
    ObjectSetString(0, "FillUp_V14", OBJPROP_TEXT, IntegerToString(g_spacingChanges));

    ChartRedraw();
}

//+------------------------------------------------------------------+
//| Print symbol info on init                                         |
//+------------------------------------------------------------------+
void PrintSymbolInfo()
{
    double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);

    Print("Symbol: ", _Symbol);
    Print("Contract Size: ", g_contractSize);
    Print("Leverage: 1:", g_leverage);
    Print("Digits: ", g_digits);
    Print("Lot Step: ", g_lotStep);
    Print("Min Lot: ", g_minLot);
    Print("Max Lot: ", g_maxLot);
    Print("---");
    Print("Survive %: ", SurvivePct);
    Print("Base Spacing: ", BaseSpacingPct, "% of price");
    Print("  At current price $", DoubleToString(bid, g_digits),
          " = $", DoubleToString(bid * BaseSpacingPct / 100.0, g_digits));
    Print("Lookback: ", VolatilityLookbackHours, " hours");
    Print("Typical Vol: ", TypicalVolPct, "% of price");
    Print("Min Spacing: ", MinSpacingPct, "% ($", DoubleToString(bid * MinSpacingPct / 100.0, g_digits), ")");
    Print("Max Spacing: ", MaxSpacingPct, "% ($", DoubleToString(bid * MaxSpacingPct / 100.0, g_digits), ")");
    Print("Change Threshold: ", SpacingChangeThresholdPct, "% ($", DoubleToString(bid * SpacingChangeThresholdPct / 100.0, g_digits), ")");
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
