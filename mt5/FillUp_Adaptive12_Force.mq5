//+------------------------------------------------------------------+
//|                                      FillUp_Adaptive12_Force.mq5 |
//|                      ADAPTIVE_SPACING 12% + Forced Entry         |
//|          Best config from C++ validation: 100.72x 2-year return  |
//+------------------------------------------------------------------+
#property copyright "Backtest Framework"
#property link      ""
#property version   "1.00"
#property strict

//+------------------------------------------------------------------+
//| Input Parameters                                                  |
//+------------------------------------------------------------------+
input group "=== Core Strategy (Validated Best Config) ==="
input double SurvivePct = 12.0;              // Survive % (12% = highest return)
input double BaseSpacing = 1.5;              // Base grid spacing ($)
input double MinVolume = 0.01;               // Minimum lot size
input double MaxVolume = 10.0;               // Maximum lot size

input group "=== FORCED ENTRY (Critical Discovery) ==="
input bool   ForceMinVolumeEntry = true;     // Force MinVolume when lot=0 (MUST BE ON)

input group "=== Adaptive Spacing (ADAPTIVE_SPACING mode) ==="
input double VolatilityLookbackHours = 4.0;  // Volatility lookback (hours)
input double TypicalVolPct = 0.55;           // Typical volatility (% of price)
input double MinSpacingMult = 0.5;           // Min spacing multiplier
input double MaxSpacingMult = 3.0;           // Max spacing multiplier

input group "=== Safety Mechanisms ==="
input int    MaxPositions = 0;               // Max positions (0 = unlimited, 200 recommended)
input double MarginLevelFloor = 0;           // Skip entry if margin < this % (0 = disabled)

input group "=== Other ==="
input bool   ShowDashboard = true;           // Show info panel
input int    MagicNumber = 120012;           // Magic number

//+------------------------------------------------------------------+
//| Global Variables                                                  |
//+------------------------------------------------------------------+
double g_currentSpacing;
double g_recentHigh;
double g_recentLow;
datetime g_lastVolatilityReset;

// Symbol info
double g_contractSize;
double g_leverage;
int    g_digits;
double g_point;
double g_lotStep;
double g_minLot;
double g_maxLot;

// Statistics
int    g_totalTrades;
int    g_forcedEntries;
int    g_maxPosBlocks;
int    g_marginBlocks;
int    g_peakPositions;
double g_peakEquity;
double g_maxDrawdown;
double g_maxDrawdownPct;

// Per-tick cache
double g_tickBid;
double g_tickAsk;
double g_tickEquity;
double g_tickBalance;
datetime g_tickTime;

// Position cache
int    g_posCount;
double g_lowestBuy;
double g_highestBuy;
double g_totalVolume;
double g_unrealizedPL;
double g_usedMargin;

// Dashboard throttle
datetime g_lastDashboardUpdate;

//+------------------------------------------------------------------+
//| Expert initialization                                             |
//+------------------------------------------------------------------+
int OnInit()
{
    g_contractSize = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);
    g_leverage = (double)AccountInfoInteger(ACCOUNT_LEVERAGE);
    g_digits = (int)SymbolInfoInteger(_Symbol, SYMBOL_DIGITS);
    g_point = SymbolInfoDouble(_Symbol, SYMBOL_POINT);
    g_lotStep = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_STEP);
    g_minLot = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN);
    g_maxLot = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MAX);

    g_currentSpacing = BaseSpacing;
    g_recentHigh = 0;
    g_recentLow = DBL_MAX;
    g_lastVolatilityReset = 0;

    g_totalTrades = 0;
    g_forcedEntries = 0;
    g_maxPosBlocks = 0;
    g_marginBlocks = 0;
    g_peakPositions = 0;
    g_peakEquity = AccountInfoDouble(ACCOUNT_EQUITY);
    g_maxDrawdown = 0;
    g_maxDrawdownPct = 0;

    if(ShowDashboard)
        CreateDashboard();

    Print("========================================");
    Print("=== FillUp ADAPTIVE 12% + FORCE ON ===");
    Print("========================================");
    Print("Symbol: ", _Symbol);
    Print("Contract Size: ", g_contractSize);
    Print("Leverage: 1:", g_leverage);
    Print("Survive: ", SurvivePct, "%");
    Print("Base Spacing: $", BaseSpacing);
    Print("ForceMinVolumeEntry: ", ForceMinVolumeEntry ? "ON" : "OFF");
    Print("MaxPositions: ", MaxPositions == 0 ? "Unlimited" : IntegerToString(MaxPositions));
    Print("========================================");

    if(!ForceMinVolumeEntry)
    {
        Print("WARNING: ForceMinVolumeEntry is OFF!");
        Print("This configuration REQUIRES forced entry to work properly.");
        Print("Expected performance will be significantly lower.");
    }

    return INIT_SUCCEEDED;
}

//+------------------------------------------------------------------+
//| Expert deinitialization                                           |
//+------------------------------------------------------------------+
void OnDeinit(const int reason)
{
    if(ShowDashboard)
        ObjectsDeleteAll(0, "FA12_");

    Print("========================================");
    Print("=== FINAL STATISTICS ===");
    Print("========================================");
    Print("Total Trades: ", g_totalTrades);
    Print("Forced Entries: ", g_forcedEntries, " (",
          g_totalTrades > 0 ? DoubleToString((double)g_forcedEntries/g_totalTrades*100, 1) : "0", "%)");
    Print("Max Position Blocks: ", g_maxPosBlocks);
    Print("Margin Level Blocks: ", g_marginBlocks);
    Print("Peak Positions: ", g_peakPositions);
    Print("Max Drawdown: ", DoubleToString(g_maxDrawdownPct, 2), "%");
    Print("========================================");
}

//+------------------------------------------------------------------+
//| Expert tick function                                              |
//+------------------------------------------------------------------+
void OnTick()
{
    // Cache market data
    g_tickBid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
    g_tickAsk = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    g_tickEquity = AccountInfoDouble(ACCOUNT_EQUITY);
    g_tickBalance = AccountInfoDouble(ACCOUNT_BALANCE);
    g_tickTime = TimeCurrent();

    // Scan positions
    ScanPositions();

    // Track peak positions
    if(g_posCount > g_peakPositions)
        g_peakPositions = g_posCount;

    // Update volatility and adaptive spacing
    UpdateVolatility();
    UpdateAdaptiveSpacing();

    // Update statistics
    UpdateStatistics();

    // Dashboard update (throttled to once per second)
    if(ShowDashboard && g_tickTime != g_lastDashboardUpdate)
    {
        UpdateDashboard();
        g_lastDashboardUpdate = g_tickTime;
    }

    // Safety: max positions cap
    if(MaxPositions > 0 && g_posCount >= MaxPositions)
    {
        g_maxPosBlocks++;
        return;
    }

    // Safety: margin level floor
    if(MarginLevelFloor > 0)
    {
        double marginLevel = AccountInfoDouble(ACCOUNT_MARGIN_LEVEL);
        if(marginLevel > 0 && marginLevel < MarginLevelFloor)
        {
            g_marginBlocks++;
            return;
        }
    }

    // Entry logic
    double spread = g_tickAsk - g_tickBid;
    bool shouldOpen = false;

    if(g_posCount == 0)
    {
        // First position: always open
        shouldOpen = true;
    }
    else
    {
        // Subsequent positions: when price drops by spacing
        if(g_lowestBuy >= g_tickAsk + g_currentSpacing)
        {
            shouldOpen = true;
        }
    }

    if(shouldOpen)
    {
        double lots = CalculateLotSize();
        bool wasForced = false;

        // CRITICAL: Force MinVolume when lot sizing returns 0
        if(lots < MinVolume)
        {
            if(ForceMinVolumeEntry)
            {
                lots = MinVolume;
                wasForced = true;
            }
            else
            {
                return;  // Skip entry (will hurt performance!)
            }
        }

        double tp = g_tickAsk + spread + g_currentSpacing;

        if(OpenBuyOrder(lots, tp))
        {
            g_totalTrades++;
            if(wasForced)
                g_forcedEntries++;
        }
    }
}

//+------------------------------------------------------------------+
//| Scan positions                                                    |
//+------------------------------------------------------------------+
void ScanPositions()
{
    g_posCount = 0;
    g_lowestBuy = DBL_MAX;
    g_highestBuy = -DBL_MAX;
    g_totalVolume = 0;
    g_unrealizedPL = 0;
    g_usedMargin = 0;

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

                g_lowestBuy = MathMin(g_lowestBuy, entryPrice);
                g_highestBuy = MathMax(g_highestBuy, entryPrice);
                g_totalVolume += lots;
                g_unrealizedPL += PositionGetDouble(POSITION_PROFIT);
                g_usedMargin += lots * g_contractSize * entryPrice / g_leverage;
                g_posCount++;
            }
        }
    }
}

//+------------------------------------------------------------------+
//| Update volatility tracking                                        |
//+------------------------------------------------------------------+
void UpdateVolatility()
{
    int lookbackSeconds = (int)(VolatilityLookbackHours * 3600);

    if(g_lastVolatilityReset == 0 || g_tickTime - g_lastVolatilityReset >= lookbackSeconds)
    {
        g_recentHigh = g_tickBid;
        g_recentLow = g_tickBid;
        g_lastVolatilityReset = g_tickTime;
    }

    g_recentHigh = MathMax(g_recentHigh, g_tickBid);
    g_recentLow = MathMin(g_recentLow, g_tickBid);
}

//+------------------------------------------------------------------+
//| Update adaptive spacing                                           |
//+------------------------------------------------------------------+
void UpdateAdaptiveSpacing()
{
    double range = g_recentHigh - g_recentLow;

    if(range > 0 && g_recentHigh > 0 && g_tickBid > 0)
    {
        double typicalVol = g_tickBid * (TypicalVolPct / 100.0);
        double volRatio = range / typicalVol;
        volRatio = MathMax(MinSpacingMult, MathMin(MaxSpacingMult, volRatio));

        double newSpacing = BaseSpacing * volRatio;
        newSpacing = MathMax(0.5, MathMin(5.0, newSpacing));

        if(MathAbs(newSpacing - g_currentSpacing) > 0.1)
        {
            g_currentSpacing = newSpacing;
        }
    }
}

//+------------------------------------------------------------------+
//| Update statistics                                                 |
//+------------------------------------------------------------------+
void UpdateStatistics()
{
    if(g_tickEquity > g_peakEquity)
        g_peakEquity = g_tickEquity;

    double drawdown = g_peakEquity - g_tickEquity;
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
double CalculateLotSize()
{
    double endPrice = (g_posCount == 0)
        ? g_tickAsk * ((100.0 - SurvivePct) / 100.0)
        : g_highestBuy * ((100.0 - SurvivePct) / 100.0);

    double distance = g_tickAsk - endPrice;
    double numberOfTrades = MathFloor(distance / g_currentSpacing);
    if(numberOfTrades <= 0) numberOfTrades = 1;

    double equityAtTarget = g_tickEquity - g_totalVolume * distance * g_contractSize;

    double marginStopOut = 20.0;
    if(g_usedMargin > 0 && (equityAtTarget / g_usedMargin * 100.0) < marginStopOut)
    {
        return 0.0;  // Will be forced to MinVolume if ForceMinVolumeEntry is ON
    }

    double tradeSize = MinVolume;
    double dEquity = g_contractSize * tradeSize * g_currentSpacing *
                     (numberOfTrades * (numberOfTrades + 1) / 2);
    double dMargin = numberOfTrades * tradeSize * g_contractSize / g_leverage;

    double maxMult = MaxVolume / MinVolume;
    for(double mult = maxMult; mult >= 1.0; mult -= 0.1)
    {
        double testEquity = equityAtTarget - mult * dEquity;
        double testMargin = g_usedMargin + mult * dMargin;

        if(testMargin > 0 && (testEquity / testMargin * 100.0) > marginStopOut)
        {
            tradeSize = mult * MinVolume;
            break;
        }
    }

    tradeSize = MathFloor(tradeSize / g_lotStep) * g_lotStep;
    return MathMin(tradeSize, MaxVolume);
}

//+------------------------------------------------------------------+
//| Open buy order                                                    |
//+------------------------------------------------------------------+
bool OpenBuyOrder(double lots, double tp)
{
    tp = NormalizeDouble(tp, g_digits);

    MqlTradeRequest request = {};
    MqlTradeResult result = {};

    request.action = TRADE_ACTION_DEAL;
    request.symbol = _Symbol;
    request.volume = lots;
    request.type = ORDER_TYPE_BUY;
    request.price = g_tickAsk;
    request.sl = 0;
    request.tp = tp;
    request.deviation = 10;
    request.magic = MagicNumber;
    request.comment = "FA12_" + IntegerToString(g_posCount);
    request.type_filling = ORDER_FILLING_IOC;

    if(!OrderSend(request, result))
    {
        Print("OrderSend error: ", GetLastError());
        return false;
    }

    return (result.retcode == TRADE_RETCODE_DONE || result.retcode == TRADE_RETCODE_PLACED);
}

//+------------------------------------------------------------------+
//| Create dashboard                                                  |
//+------------------------------------------------------------------+
void CreateDashboard()
{
    int x = 10, y = 30, width = 260, height = 320;

    ObjectCreate(0, "FA12_BG", OBJ_RECTANGLE_LABEL, 0, 0, 0);
    ObjectSetInteger(0, "FA12_BG", OBJPROP_XDISTANCE, x);
    ObjectSetInteger(0, "FA12_BG", OBJPROP_YDISTANCE, y);
    ObjectSetInteger(0, "FA12_BG", OBJPROP_XSIZE, width);
    ObjectSetInteger(0, "FA12_BG", OBJPROP_YSIZE, height);
    ObjectSetInteger(0, "FA12_BG", OBJPROP_BGCOLOR, clrBlack);
    ObjectSetInteger(0, "FA12_BG", OBJPROP_BORDER_TYPE, BORDER_FLAT);
    ObjectSetInteger(0, "FA12_BG", OBJPROP_COLOR, clrLime);
    ObjectSetInteger(0, "FA12_BG", OBJPROP_CORNER, CORNER_LEFT_UPPER);

    CreateLabel("FA12_Title", x + 10, y + 10, "ADAPTIVE 12% + FORCE", clrLime, 11);
    CreateLabel("FA12_Sub", x + 10, y + 28, "Best Config: 100.72x 2yr", clrGray, 8);

    int lineHeight = 18;
    int startY = y + 50;

    CreateLabel("FA12_L1", x + 10, startY, "Spacing:", clrWhite, 9);
    CreateLabel("FA12_V1", x + 130, startY, "-", clrLime, 9);

    CreateLabel("FA12_L2", x + 10, startY + lineHeight, "Volatility:", clrWhite, 9);
    CreateLabel("FA12_V2", x + 130, startY + lineHeight, "-", clrLime, 9);

    CreateLabel("FA12_L3", x + 10, startY + lineHeight*2, "Positions:", clrWhite, 9);
    CreateLabel("FA12_V3", x + 130, startY + lineHeight*2, "-", clrLime, 9);

    CreateLabel("FA12_L4", x + 10, startY + lineHeight*3, "Volume:", clrWhite, 9);
    CreateLabel("FA12_V4", x + 130, startY + lineHeight*3, "-", clrLime, 9);

    CreateLabel("FA12_L5", x + 10, startY + lineHeight*4, "Unrealized:", clrWhite, 9);
    CreateLabel("FA12_V5", x + 130, startY + lineHeight*4, "-", clrLime, 9);

    CreateLabel("FA12_L6", x + 10, startY + lineHeight*5, "Equity:", clrWhite, 9);
    CreateLabel("FA12_V6", x + 130, startY + lineHeight*5, "-", clrLime, 9);

    CreateLabel("FA12_L7", x + 10, startY + lineHeight*6, "Drawdown:", clrWhite, 9);
    CreateLabel("FA12_V7", x + 130, startY + lineHeight*6, "-", clrOrange, 9);

    CreateLabel("FA12_L8", x + 10, startY + lineHeight*7, "Max DD:", clrWhite, 9);
    CreateLabel("FA12_V8", x + 130, startY + lineHeight*7, "-", clrRed, 9);

    CreateLabel("FA12_L9", x + 10, startY + lineHeight*8, "Trades:", clrWhite, 9);
    CreateLabel("FA12_V9", x + 130, startY + lineHeight*8, "-", clrLime, 9);

    CreateLabel("FA12_L10", x + 10, startY + lineHeight*9, "Forced:", clrWhite, 9);
    CreateLabel("FA12_V10", x + 130, startY + lineHeight*9, "-", clrYellow, 9);

    CreateLabel("FA12_L11", x + 10, startY + lineHeight*10, "Peak Pos:", clrWhite, 9);
    CreateLabel("FA12_V11", x + 130, startY + lineHeight*10, "-", clrCyan, 9);

    CreateLabel("FA12_L12", x + 10, startY + lineHeight*11, "Max Pos Blk:", clrWhite, 9);
    CreateLabel("FA12_V12", x + 130, startY + lineHeight*11, "-", clrGray, 9);
}

//+------------------------------------------------------------------+
//| Create label helper                                               |
//+------------------------------------------------------------------+
void CreateLabel(string name, int x, int y, string text, color clr, int fontSize)
{
    ObjectCreate(0, name, OBJ_LABEL, 0, 0, 0);
    ObjectSetInteger(0, name, OBJPROP_XDISTANCE, x);
    ObjectSetInteger(0, name, OBJPROP_YDISTANCE, y);
    ObjectSetInteger(0, name, OBJPROP_COLOR, clr);
    ObjectSetInteger(0, name, OBJPROP_FONTSIZE, fontSize);
    ObjectSetString(0, name, OBJPROP_FONT, "Consolas");
    ObjectSetString(0, name, OBJPROP_TEXT, text);
    ObjectSetInteger(0, name, OBJPROP_CORNER, CORNER_LEFT_UPPER);
}

//+------------------------------------------------------------------+
//| Update dashboard                                                  |
//+------------------------------------------------------------------+
void UpdateDashboard()
{
    double range = g_recentHigh - g_recentLow;
    double volPct = (g_tickBid > 0) ? (range / g_tickBid * 100.0) : 0;

    ObjectSetString(0, "FA12_V1", OBJPROP_TEXT, "$" + DoubleToString(g_currentSpacing, 2));
    ObjectSetString(0, "FA12_V2", OBJPROP_TEXT, "$" + DoubleToString(range, 2) + " (" + DoubleToString(volPct, 2) + "%)");
    ObjectSetString(0, "FA12_V3", OBJPROP_TEXT, IntegerToString(g_posCount));
    ObjectSetString(0, "FA12_V4", OBJPROP_TEXT, DoubleToString(g_totalVolume, 2) + " lots");

    ObjectSetString(0, "FA12_V5", OBJPROP_TEXT, "$" + DoubleToString(g_unrealizedPL, 2));
    ObjectSetInteger(0, "FA12_V5", OBJPROP_COLOR, g_unrealizedPL >= 0 ? clrLime : clrRed);

    ObjectSetString(0, "FA12_V6", OBJPROP_TEXT, "$" + DoubleToString(g_tickEquity, 2));

    double currentDD = g_peakEquity - g_tickEquity;
    double currentDDPct = (g_peakEquity > 0) ? (currentDD / g_peakEquity * 100.0) : 0;
    ObjectSetString(0, "FA12_V7", OBJPROP_TEXT, DoubleToString(currentDDPct, 1) + "%");
    ObjectSetInteger(0, "FA12_V7", OBJPROP_COLOR, currentDDPct > 50 ? clrRed : (currentDDPct > 30 ? clrOrange : clrYellow));

    ObjectSetString(0, "FA12_V8", OBJPROP_TEXT, DoubleToString(g_maxDrawdownPct, 1) + "%");

    ObjectSetString(0, "FA12_V9", OBJPROP_TEXT, IntegerToString(g_totalTrades));
    ObjectSetString(0, "FA12_V10", OBJPROP_TEXT, IntegerToString(g_forcedEntries));
    ObjectSetString(0, "FA12_V11", OBJPROP_TEXT, IntegerToString(g_peakPositions));
    ObjectSetString(0, "FA12_V12", OBJPROP_TEXT, IntegerToString(g_maxPosBlocks));
}
//+------------------------------------------------------------------+
