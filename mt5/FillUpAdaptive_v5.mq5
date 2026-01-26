//+------------------------------------------------------------------+
//|                                           FillUpAdaptive_v5.mq5  |
//|                                    Floating Attractor Grid       |
//|                  Grid follows EMA - Chaos Theory Investigation   |
//|                          Best config: EMA-200, TP mult 2.0       |
//+------------------------------------------------------------------+
#property copyright "Backtest Framework"
#property link      ""
#property version   "5.00"
#property strict

//+------------------------------------------------------------------+
//| Input Parameters                                                  |
//+------------------------------------------------------------------+
input group "=== Core Strategy Parameters ==="
input double SurvivePct = 12.0;           // Survive % (edge of chaos optimal)
input double BaseSpacingPct = 0.06;       // Base grid spacing (% of price)
input double MinVolume = 0.01;            // Minimum lot size
input double MaxVolume = 100.0;           // Maximum lot size

input group "=== Floating Attractor (Chaos) ==="
input int    AttractorPeriod = 200;       // EMA period (ticks) - 200 optimal
input double TPMultiplier = 2.0;          // TP multiplier toward attractor - 2.0 optimal
input bool   OnlyBuyBelowAttractor = true; // Only buy when price < attractor

input group "=== Adaptive Spacing ==="
input double VolatilityLookbackHours = 8.0;  // Volatility lookback (8h optimal)
input double TypicalVolPct = 0.55;           // Typical volatility (% of price)
input double MinSpacingMult = 0.5;           // Min spacing multiplier
input double MaxSpacingMult = 3.0;           // Max spacing multiplier
input double MinSpacingPct = 0.02;           // Min absolute spacing (% of price)
input double MaxSpacingPct = 1.0;            // Max absolute spacing (% of price)
input double SpacingChangeThresholdPct = 0.01; // Spacing change threshold (% of price)

input group "=== Display & Safety ==="
input bool ShowDashboard = true;          // Show info panel
input bool EnableTrading = true;          // Enable trading (false = monitor only)
input int MagicNumber = 123459;           // Magic number

//+------------------------------------------------------------------+
//| Global Variables                                                  |
//+------------------------------------------------------------------+
// Floating attractor
double g_attractor;
double g_emaAlpha;
int    g_attractorCrossings;

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

// Persistence
datetime g_lastSaveTime;
string g_stateFileName;

// Per-tick cache
double g_tickBid;
double g_tickAsk;
double g_tickEquity;
datetime g_tickTime;

// Dashboard throttle
datetime g_lastDashboardUpdate;

// Cached position state
int g_cachedPosCount;
double g_cachedLowestBuy;
double g_cachedHighestBuy;
double g_cachedTotalVolume;
double g_cachedUnrealizedPL;
double g_cachedUsedMargin;

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

    // EMA alpha: 2 / (period + 1)
    g_emaAlpha = 2.0 / (AttractorPeriod + 1.0);

    // State file
    g_stateFileName = "FillUp_v5_" + _Symbol + "_" + IntegerToString(MagicNumber) + ".state";
    g_lastSaveTime = 0;

    // Try to restore state
    bool stateRestored = LoadState();

    if(!stateRestored)
    {
        double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
        if(bid > 0)
        {
            g_attractor = bid;  // Initialize attractor at current price
            g_currentSpacing = bid * (BaseSpacingPct / 100.0);
            g_effectiveBase = g_currentSpacing;
        }
        else
        {
            g_attractor = 0;
            g_currentSpacing = 1.0;
            g_effectiveBase = 1.0;
        }

        g_recentHigh = 0;
        g_recentLow = DBL_MAX;
        g_lastVolatilityReset = 0;
        g_spacingChanges = 0;
        g_typicalVol = 0;
        g_attractorCrossings = 0;
        g_startTime = TimeCurrent();

        ReconstructVolatilityFromBars();
        Print("State: Initialized fresh");
    }
    else
    {
        Print("State: Restored from file");
    }

    ReconstructStatistics();

    double equity = AccountInfoDouble(ACCOUNT_EQUITY);
    if(equity > g_peakEquity)
        g_peakEquity = equity;

    if(ShowDashboard)
        CreateDashboard();

    PrintSettings();

    return INIT_SUCCEEDED;
}

//+------------------------------------------------------------------+
//| Expert deinitialization                                           |
//+------------------------------------------------------------------+
void OnDeinit(const int reason)
{
    SaveState();

    if(ShowDashboard)
    {
        ObjectsDeleteAll(0, "FillUp_");
    }

    PrintStatistics();
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
    g_tickTime = TimeCurrent();

    // Initialize attractor on first valid tick
    if(g_attractor == 0 && g_tickBid > 0)
    {
        g_attractor = g_tickBid;
    }

    // Scan positions
    ScanPositions();

    // Update attractor (EMA)
    UpdateAttractor();

    // Update volatility and spacing
    UpdateVolatility();
    UpdateAdaptiveSpacing();

    // Update statistics
    UpdateStatistics();

    // Periodic state save
    if(g_tickTime - g_lastSaveTime >= 60)
    {
        SaveState();
        g_lastSaveTime = g_tickTime;
    }

    // Dashboard update
    if(ShowDashboard && g_tickTime != g_lastDashboardUpdate)
    {
        UpdateDashboard();
        g_lastDashboardUpdate = g_tickTime;
    }

    if(!EnableTrading) return;

    // Trading logic with floating attractor
    double spread = g_tickAsk - g_tickBid;
    double deviation = g_tickBid - g_attractor;  // Negative when below attractor
    bool shouldOpen = false;
    double tp = 0;

    if(g_cachedPosCount == 0)
    {
        // First position: buy when price is below attractor by half spacing
        if(deviation < -g_currentSpacing * 0.5)
        {
            shouldOpen = true;
            // TP targets the attractor plus multiplier
            tp = g_attractor + spread + g_currentSpacing * TPMultiplier;
        }
    }
    else
    {
        // Subsequent positions
        if(g_cachedLowestBuy >= g_tickAsk + g_currentSpacing)
        {
            // Standard grid: price dropped by spacing
            if(!OnlyBuyBelowAttractor || deviation < 0)
            {
                shouldOpen = true;
                double tp_target = MathMax(g_attractor, g_tickAsk + g_currentSpacing);
                tp = tp_target + spread + g_currentSpacing * (TPMultiplier - 1.0);
            }
        }
        else if(g_cachedHighestBuy <= g_tickAsk - g_currentSpacing)
        {
            // Price moved up: add position only if still below attractor
            if(deviation < 0)
            {
                shouldOpen = true;
                tp = g_attractor + spread + g_currentSpacing * TPMultiplier;
            }
        }
    }

    if(shouldOpen && tp > 0)
    {
        double lots = CalculateLotSize(g_tickAsk, g_cachedPosCount, g_cachedTotalVolume, g_cachedHighestBuy);
        if(lots >= g_minLot)
        {
            if(OpenBuyOrder(lots, g_tickAsk, tp))
            {
                g_totalTrades++;
                SaveState();
            }
        }
    }
}

//+------------------------------------------------------------------+
//| Update attractor (EMA)                                            |
//+------------------------------------------------------------------+
void UpdateAttractor()
{
    if(g_tickBid <= 0 || g_attractor <= 0) return;

    double prevAttractor = g_attractor;

    // EMA update: alpha * price + (1-alpha) * prev_ema
    g_attractor = g_emaAlpha * g_tickBid + (1.0 - g_emaAlpha) * g_attractor;

    // Count crossings (price crossing the attractor)
    bool wasAbove = g_tickBid > prevAttractor;
    bool nowAbove = g_tickBid > g_attractor;
    if(wasAbove != nowAbove)
    {
        g_attractorCrossings++;
    }
}

//+------------------------------------------------------------------+
//| Single-pass position scanner                                      |
//+------------------------------------------------------------------+
void ScanPositions()
{
    g_cachedPosCount = 0;
    g_cachedLowestBuy = DBL_MAX;
    g_cachedHighestBuy = -DBL_MAX;
    g_cachedTotalVolume = 0;
    g_cachedUnrealizedPL = 0;
    g_cachedUsedMargin = 0;

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

                g_cachedLowestBuy = MathMin(g_cachedLowestBuy, entryPrice);
                g_cachedHighestBuy = MathMax(g_cachedHighestBuy, entryPrice);
                g_cachedTotalVolume += lots;
                g_cachedUnrealizedPL += PositionGetDouble(POSITION_PROFIT);
                g_cachedUsedMargin += lots * g_contractSize * entryPrice / g_leverage;
                g_cachedPosCount++;
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
        g_typicalVol = g_tickBid * (TypicalVolPct / 100.0);
        g_effectiveBase = g_tickBid * (BaseSpacingPct / 100.0);
        double minAbs = g_tickBid * (MinSpacingPct / 100.0);
        double maxAbs = g_tickBid * (MaxSpacingPct / 100.0);
        double changeThresh = g_tickBid * (SpacingChangeThresholdPct / 100.0);

        double volRatio = range / g_typicalVol;
        volRatio = MathMax(MinSpacingMult, MathMin(MaxSpacingMult, volRatio));

        double newSpacing = g_effectiveBase * volRatio;
        newSpacing = MathMax(minAbs, MathMin(maxAbs, newSpacing));

        if(MathAbs(newSpacing - g_currentSpacing) > changeThresh)
        {
            g_currentSpacing = newSpacing;
            g_spacingChanges++;
        }
    }
}

//+------------------------------------------------------------------+
//| Update statistics                                                 |
//+------------------------------------------------------------------+
void UpdateStatistics()
{
    if(g_tickEquity > g_peakEquity)
    {
        g_peakEquity = g_tickEquity;
    }

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
double CalculateLotSize(double currentAsk, int positionsTotal,
                        double volumeOfOpenTrades, double highestBuy)
{
    double usedMargin = g_cachedUsedMargin;

    double endPrice = (positionsTotal == 0)
        ? currentAsk * ((100.0 - SurvivePct) / 100.0)
        : highestBuy * ((100.0 - SurvivePct) / 100.0);

    double distance = currentAsk - endPrice;
    double numberOfTrades = MathFloor(distance / g_currentSpacing);
    if(numberOfTrades <= 0) numberOfTrades = 1;

    double equityAtTarget = g_tickEquity - volumeOfOpenTrades * distance * g_contractSize;

    double marginStopOut = 20.0;
    if(usedMargin > 0 && (equityAtTarget / usedMargin * 100.0) < marginStopOut)
    {
        return 0.0;
    }

    double dEquityUnit = g_contractSize * MinVolume * g_currentSpacing *
                         (numberOfTrades * (numberOfTrades + 1) / 2);
    double dMarginUnit = numberOfTrades * MinVolume * g_contractSize / g_leverage;
    double stopOutFrac = marginStopOut / 100.0;

    double maxMult = MaxVolume / MinVolume;
    double mult = 1.0;

    if(dMarginUnit > 0)
    {
        double numerator = equityAtTarget - stopOutFrac * usedMargin;
        double denominator = dEquityUnit + stopOutFrac * dMarginUnit;

        if(denominator > 0 && numerator > 0)
        {
            mult = numerator / denominator;
            mult = MathMin(mult, maxMult);
        }
        else if(numerator <= 0)
        {
            return 0.0;
        }
        else
        {
            mult = maxMult;
        }
    }
    else
    {
        if(dEquityUnit > 0)
        {
            mult = equityAtTarget / dEquityUnit;
            mult = MathMin(mult, maxMult);
        }
        else
        {
            mult = maxMult;
        }
    }

    mult = MathMax(1.0, mult);

    double tradeSize = mult * MinVolume;
    tradeSize = MathFloor(tradeSize / g_lotStep) * g_lotStep;
    tradeSize = MathMax(g_minLot, MathMin(g_maxLot, tradeSize));

    return MathMin(tradeSize, MaxVolume);
}

//+------------------------------------------------------------------+
//| Open buy order with specified TP                                  |
//+------------------------------------------------------------------+
bool OpenBuyOrder(double lots, double ask, double tp)
{
    tp = NormalizeDouble(tp, g_digits);

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
    request.comment = "FillUp v5 FA-" + IntegerToString(AttractorPeriod);
    request.type_filling = ORDER_FILLING_IOC;

    if(!OrderSend(request, result))
    {
        Print("OrderSend error: ", GetLastError());
        return false;
    }

    if(result.retcode == TRADE_RETCODE_DONE || result.retcode == TRADE_RETCODE_PLACED)
    {
        double spacingPct = (ask > 0) ? (g_currentSpacing / ask * 100.0) : 0;
        double deviation = g_tickBid - g_attractor;
        Print("BUY ", DoubleToString(lots, 2),
              " @ ", DoubleToString(ask, g_digits),
              " TP: ", DoubleToString(tp, g_digits),
              " Att: ", DoubleToString(g_attractor, g_digits),
              " Dev: $", DoubleToString(deviation, 2));
        return true;
    }

    return false;
}

//+------------------------------------------------------------------+
//| Save state to file                                                |
//+------------------------------------------------------------------+
void SaveState()
{
    if(MQLInfoInteger(MQL_TESTER) || MQLInfoInteger(MQL_OPTIMIZATION))
        return;

    int handle = FileOpen(g_stateFileName, FILE_WRITE | FILE_TXT | FILE_ANSI);
    if(handle == INVALID_HANDLE) return;

    FileWriteString(handle, "# FillUp v5 Floating Attractor State\n");
    FileWriteString(handle, "version=5.0\n");
    FileWriteString(handle, "symbol=" + _Symbol + "\n");
    FileWriteString(handle, "magic=" + IntegerToString(MagicNumber) + "\n");
    FileWriteString(handle, "savedAt=" + IntegerToString((long)TimeCurrent()) + "\n");

    FileWriteString(handle, "attractor=" + DoubleToString(g_attractor, 8) + "\n");
    FileWriteString(handle, "attractorCrossings=" + IntegerToString(g_attractorCrossings) + "\n");
    FileWriteString(handle, "currentSpacing=" + DoubleToString(g_currentSpacing, 8) + "\n");
    FileWriteString(handle, "recentHigh=" + DoubleToString(g_recentHigh, 8) + "\n");
    FileWriteString(handle, "recentLow=" + DoubleToString(g_recentLow, 8) + "\n");
    FileWriteString(handle, "lastVolReset=" + IntegerToString((long)g_lastVolatilityReset) + "\n");
    FileWriteString(handle, "spacingChanges=" + IntegerToString(g_spacingChanges) + "\n");
    FileWriteString(handle, "typicalVol=" + DoubleToString(g_typicalVol, 8) + "\n");
    FileWriteString(handle, "effectiveBase=" + DoubleToString(g_effectiveBase, 8) + "\n");
    FileWriteString(handle, "peakEquity=" + DoubleToString(g_peakEquity, 2) + "\n");
    FileWriteString(handle, "maxDrawdown=" + DoubleToString(g_maxDrawdown, 2) + "\n");
    FileWriteString(handle, "maxDrawdownPct=" + DoubleToString(g_maxDrawdownPct, 2) + "\n");
    FileWriteString(handle, "startTime=" + IntegerToString((long)g_startTime) + "\n");

    FileClose(handle);
}

//+------------------------------------------------------------------+
//| Load state from file                                              |
//+------------------------------------------------------------------+
bool LoadState()
{
    if(MQLInfoInteger(MQL_TESTER) || MQLInfoInteger(MQL_OPTIMIZATION))
        return false;

    if(!FileIsExist(g_stateFileName))
        return false;

    int handle = FileOpen(g_stateFileName, FILE_READ | FILE_TXT | FILE_ANSI);
    if(handle == INVALID_HANDLE)
        return false;

    datetime savedAt = 0;
    string savedSymbol = "";
    int savedMagic = 0;
    bool hasAttractor = false;

    while(!FileIsEnding(handle))
    {
        string line = FileReadString(handle);
        if(StringLen(line) == 0 || StringGetCharacter(line, 0) == '#')
            continue;

        int eqPos = StringFind(line, "=");
        if(eqPos < 0) continue;

        string key = StringSubstr(line, 0, eqPos);
        string value = StringSubstr(line, eqPos + 1);

        if(key == "symbol") savedSymbol = value;
        else if(key == "magic") savedMagic = (int)StringToInteger(value);
        else if(key == "savedAt") savedAt = (datetime)StringToInteger(value);
        else if(key == "attractor") { g_attractor = StringToDouble(value); hasAttractor = true; }
        else if(key == "attractorCrossings") g_attractorCrossings = (int)StringToInteger(value);
        else if(key == "currentSpacing") g_currentSpacing = StringToDouble(value);
        else if(key == "recentHigh") g_recentHigh = StringToDouble(value);
        else if(key == "recentLow") g_recentLow = StringToDouble(value);
        else if(key == "lastVolReset") g_lastVolatilityReset = (datetime)StringToInteger(value);
        else if(key == "spacingChanges") g_spacingChanges = (int)StringToInteger(value);
        else if(key == "typicalVol") g_typicalVol = StringToDouble(value);
        else if(key == "effectiveBase") g_effectiveBase = StringToDouble(value);
        else if(key == "peakEquity") g_peakEquity = StringToDouble(value);
        else if(key == "maxDrawdown") g_maxDrawdown = StringToDouble(value);
        else if(key == "maxDrawdownPct") g_maxDrawdownPct = StringToDouble(value);
        else if(key == "startTime") g_startTime = (datetime)StringToInteger(value);
    }

    FileClose(handle);

    if(savedSymbol != _Symbol || savedMagic != MagicNumber)
        return false;

    datetime now = TimeCurrent();
    if(savedAt > 0 && now - savedAt > 7 * 24 * 3600)
    {
        Print("State file too old. Starting fresh.");
        return false;
    }

    int lookbackSeconds = (int)(VolatilityLookbackHours * 3600);
    if(savedAt > 0 && now - savedAt > lookbackSeconds)
    {
        Print("Lookback window stale. Reconstructing from bars.");
        ReconstructVolatilityFromBars();
    }

    return hasAttractor;
}

//+------------------------------------------------------------------+
//| Reconstruct volatility from M1 bars                               |
//+------------------------------------------------------------------+
void ReconstructVolatilityFromBars()
{
    double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
    if(bid <= 0) return;

    int barsNeeded = (int)(VolatilityLookbackHours * 60);
    if(barsNeeded < 1) barsNeeded = 60;
    if(barsNeeded > 1440) barsNeeded = 1440;

    double high = 0;
    double low = DBL_MAX;

    MqlRates rates[];
    int copied = CopyRates(_Symbol, PERIOD_M1, 0, barsNeeded, rates);

    if(copied > 0)
    {
        for(int i = 0; i < copied; i++)
        {
            high = MathMax(high, rates[i].high);
            low = MathMin(low, rates[i].low);
        }

        g_recentHigh = high;
        g_recentLow = low;
        g_lastVolatilityReset = TimeCurrent();

        double range = high - low;
        if(range > 0)
        {
            g_typicalVol = bid * (TypicalVolPct / 100.0);
            g_effectiveBase = bid * (BaseSpacingPct / 100.0);
            double volRatio = range / g_typicalVol;
            volRatio = MathMax(MinSpacingMult, MathMin(MaxSpacingMult, volRatio));
            double newSpacing = g_effectiveBase * volRatio;
            double minAbs = bid * (MinSpacingPct / 100.0);
            double maxAbs = bid * (MaxSpacingPct / 100.0);
            g_currentSpacing = MathMax(minAbs, MathMin(maxAbs, newSpacing));
        }

        // Also reconstruct attractor from recent close prices
        double sum = 0;
        for(int i = MathMax(0, copied - AttractorPeriod); i < copied; i++)
        {
            sum += rates[i].close;
        }
        int count = MathMin(copied, AttractorPeriod);
        if(count > 0)
        {
            g_attractor = sum / count;
            Print("Reconstructed attractor: ", DoubleToString(g_attractor, g_digits));
        }
    }
    else
    {
        g_recentHigh = bid;
        g_recentLow = bid;
        g_lastVolatilityReset = TimeCurrent();
        g_currentSpacing = bid * (BaseSpacingPct / 100.0);
        g_attractor = bid;
    }
}

//+------------------------------------------------------------------+
//| Reconstruct statistics from trade history                         |
//+------------------------------------------------------------------+
void ReconstructStatistics()
{
    datetime startTime = g_startTime > 0 ? g_startTime : TimeCurrent() - 365 * 24 * 3600;

    if(HistorySelect(startTime, TimeCurrent()))
    {
        int totalTrades = 0;
        int winningTrades = 0;
        double totalProfit = 0;

        int dealsTotal = HistoryDealsTotal();
        for(int i = 0; i < dealsTotal; i++)
        {
            ulong dealTicket = HistoryDealGetTicket(i);
            if(dealTicket > 0)
            {
                if(HistoryDealGetString(dealTicket, DEAL_SYMBOL) == _Symbol &&
                   HistoryDealGetInteger(dealTicket, DEAL_MAGIC) == MagicNumber)
                {
                    int dealEntry = (int)HistoryDealGetInteger(dealTicket, DEAL_ENTRY);
                    if(dealEntry == DEAL_ENTRY_IN)
                    {
                        totalTrades++;
                    }
                    else if(dealEntry == DEAL_ENTRY_OUT)
                    {
                        double profit = HistoryDealGetDouble(dealTicket, DEAL_PROFIT);
                        totalProfit += profit;
                        if(profit > 0) winningTrades++;
                    }
                }
            }
        }

        g_totalTrades = totalTrades;
        g_winningTrades = winningTrades;
        g_totalProfit = totalProfit;
    }

    if(g_startTime == 0)
        g_startTime = TimeCurrent();
}

//+------------------------------------------------------------------+
//| Create dashboard                                                  |
//+------------------------------------------------------------------+
void CreateDashboard()
{
    int x = 10, y = 30, width = 280, height = 420;

    ObjectCreate(0, "FillUp_BG", OBJ_RECTANGLE_LABEL, 0, 0, 0);
    ObjectSetInteger(0, "FillUp_BG", OBJPROP_XDISTANCE, x);
    ObjectSetInteger(0, "FillUp_BG", OBJPROP_YDISTANCE, y);
    ObjectSetInteger(0, "FillUp_BG", OBJPROP_XSIZE, width);
    ObjectSetInteger(0, "FillUp_BG", OBJPROP_YSIZE, height);
    ObjectSetInteger(0, "FillUp_BG", OBJPROP_BGCOLOR, clrBlack);
    ObjectSetInteger(0, "FillUp_BG", OBJPROP_BORDER_TYPE, BORDER_FLAT);
    ObjectSetInteger(0, "FillUp_BG", OBJPROP_COLOR, clrCyan);
    ObjectSetInteger(0, "FillUp_BG", OBJPROP_CORNER, CORNER_LEFT_UPPER);

    CreateLabel("FillUp_Title", x + 10, y + 10, "FillUp v5 Floating Attractor", clrCyan, 11);
    CreateLabel("FillUp_Sub", x + 10, y + 28, "EMA-" + IntegerToString(AttractorPeriod) + " | Chaos", clrDarkGray, 8);

    int lineHeight = 18;
    int startY = y + 50;

    CreateLabel("FillUp_L0", x + 10, startY, "Attractor:", clrWhite, 9);
    CreateLabel("FillUp_V0", x + 150, startY, "-", clrCyan, 9);

    CreateLabel("FillUp_L0b", x + 10, startY + lineHeight, "Deviation:", clrWhite, 9);
    CreateLabel("FillUp_V0b", x + 150, startY + lineHeight, "-", clrYellow, 9);

    CreateLabel("FillUp_L1", x + 10, startY + lineHeight*2, "Spacing:", clrWhite, 9);
    CreateLabel("FillUp_V1", x + 150, startY + lineHeight*2, "-", clrLime, 9);

    CreateLabel("FillUp_L2", x + 10, startY + lineHeight*3, "Volatility:", clrWhite, 9);
    CreateLabel("FillUp_V2", x + 150, startY + lineHeight*3, "-", clrLime, 9);

    CreateLabel("FillUp_L3", x + 10, startY + lineHeight*4, "Positions:", clrWhite, 9);
    CreateLabel("FillUp_V3", x + 150, startY + lineHeight*4, "-", clrLime, 9);

    CreateLabel("FillUp_L4", x + 10, startY + lineHeight*5, "Volume:", clrWhite, 9);
    CreateLabel("FillUp_V4", x + 150, startY + lineHeight*5, "-", clrLime, 9);

    CreateLabel("FillUp_L5", x + 10, startY + lineHeight*6, "Unrealized:", clrWhite, 9);
    CreateLabel("FillUp_V5", x + 150, startY + lineHeight*6, "-", clrLime, 9);

    CreateLabel("FillUp_L6", x + 10, startY + lineHeight*7, "Equity:", clrWhite, 9);
    CreateLabel("FillUp_V6", x + 150, startY + lineHeight*7, "-", clrLime, 9);

    CreateLabel("FillUp_L7", x + 10, startY + lineHeight*8, "Drawdown:", clrWhite, 9);
    CreateLabel("FillUp_V7", x + 150, startY + lineHeight*8, "-", clrOrange, 9);

    CreateLabel("FillUp_L8", x + 10, startY + lineHeight*9, "Max DD:", clrWhite, 9);
    CreateLabel("FillUp_V8", x + 150, startY + lineHeight*9, "-", clrRed, 9);

    CreateLabel("FillUp_L9", x + 10, startY + lineHeight*10, "Total Trades:", clrWhite, 9);
    CreateLabel("FillUp_V9", x + 150, startY + lineHeight*10, "-", clrLime, 9);

    CreateLabel("FillUp_L10", x + 10, startY + lineHeight*11, "Total Profit:", clrWhite, 9);
    CreateLabel("FillUp_V10", x + 150, startY + lineHeight*11, "-", clrLime, 9);

    CreateLabel("FillUp_L11", x + 10, startY + lineHeight*12, "Crossings:", clrWhite, 9);
    CreateLabel("FillUp_V11", x + 150, startY + lineHeight*12, "-", clrCyan, 9);

    CreateLabel("FillUp_L12", x + 10, startY + lineHeight*13, "Sp Changes:", clrWhite, 9);
    CreateLabel("FillUp_V12", x + 150, startY + lineHeight*13, "-", clrCyan, 9);

    CreateLabel("FillUp_L14", x + 10, startY + lineHeight*15, "Grid Range:", clrWhite, 9);
    CreateLabel("FillUp_V14", x + 150, startY + lineHeight*15, "-", clrGray, 9);
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
    double deviation = g_tickBid - g_attractor;
    double deviationPct = (g_attractor > 0) ? (deviation / g_attractor * 100.0) : 0;

    ObjectSetString(0, "FillUp_V0", OBJPROP_TEXT, "$" + DoubleToString(g_attractor, g_digits));

    string devStr = (deviation >= 0 ? "+" : "") + DoubleToString(deviation, 2) +
                    " (" + (deviation >= 0 ? "+" : "") + DoubleToString(deviationPct, 3) + "%)";
    ObjectSetString(0, "FillUp_V0b", OBJPROP_TEXT, devStr);
    ObjectSetInteger(0, "FillUp_V0b", OBJPROP_COLOR, deviation >= 0 ? clrLime : clrYellow);

    double spacingPct = (g_tickBid > 0) ? (g_currentSpacing / g_tickBid * 100.0) : 0;
    ObjectSetString(0, "FillUp_V1", OBJPROP_TEXT, "$" + DoubleToString(g_currentSpacing, g_digits) +
                    " (" + DoubleToString(spacingPct, 3) + "%)");

    double volPct = (g_tickBid > 0) ? (range / g_tickBid * 100.0) : 0;
    ObjectSetString(0, "FillUp_V2", OBJPROP_TEXT, "$" + DoubleToString(range, g_digits) +
                    " (" + DoubleToString(volPct, 2) + "%)");

    ObjectSetString(0, "FillUp_V3", OBJPROP_TEXT, IntegerToString(g_cachedPosCount));
    ObjectSetString(0, "FillUp_V4", OBJPROP_TEXT, DoubleToString(g_cachedTotalVolume, 2) + " lots");

    ObjectSetString(0, "FillUp_V5", OBJPROP_TEXT, "$" + DoubleToString(g_cachedUnrealizedPL, 2));
    ObjectSetInteger(0, "FillUp_V5", OBJPROP_COLOR, g_cachedUnrealizedPL >= 0 ? clrLime : clrRed);

    ObjectSetString(0, "FillUp_V6", OBJPROP_TEXT, "$" + DoubleToString(g_tickEquity, 2));

    double currentDD = g_peakEquity - g_tickEquity;
    double currentDDPct = (g_peakEquity > 0) ? (currentDD / g_peakEquity * 100.0) : 0;
    ObjectSetString(0, "FillUp_V7", OBJPROP_TEXT, "$" + DoubleToString(currentDD, 2) +
                    " (" + DoubleToString(currentDDPct, 1) + "%)");
    ObjectSetInteger(0, "FillUp_V7", OBJPROP_COLOR, currentDDPct > 30 ? clrRed : clrOrange);

    ObjectSetString(0, "FillUp_V8", OBJPROP_TEXT, "$" + DoubleToString(g_maxDrawdown, 2) +
                    " (" + DoubleToString(g_maxDrawdownPct, 1) + "%)");

    ObjectSetString(0, "FillUp_V9", OBJPROP_TEXT, IntegerToString(g_totalTrades));

    ObjectSetString(0, "FillUp_V10", OBJPROP_TEXT, "$" + DoubleToString(g_totalProfit, 2));
    ObjectSetInteger(0, "FillUp_V10", OBJPROP_COLOR, g_totalProfit >= 0 ? clrLime : clrRed);

    ObjectSetString(0, "FillUp_V11", OBJPROP_TEXT, IntegerToString(g_attractorCrossings));
    ObjectSetString(0, "FillUp_V12", OBJPROP_TEXT, IntegerToString(g_spacingChanges));

    if(g_cachedPosCount > 0 && g_cachedLowestBuy < DBL_MAX && g_cachedHighestBuy > -DBL_MAX)
    {
        ObjectSetString(0, "FillUp_V14", OBJPROP_TEXT,
            "$" + DoubleToString(g_cachedLowestBuy, g_digits) + " - $" + DoubleToString(g_cachedHighestBuy, g_digits));
    }
    else
    {
        ObjectSetString(0, "FillUp_V14", OBJPROP_TEXT, "No positions");
    }
}

//+------------------------------------------------------------------+
//| Print settings                                                    |
//+------------------------------------------------------------------+
void PrintSettings()
{
    double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
    Print("=== FillUp v5 Floating Attractor Settings ===");
    Print("Symbol: ", _Symbol, " | Contract: ", g_contractSize, " | Leverage: ", g_leverage);
    Print("Survive: ", SurvivePct, "% | Attractor Period: ", AttractorPeriod, " | TP Mult: ", TPMultiplier);
    Print("Base Spacing: ", BaseSpacingPct, "% ($", DoubleToString(bid * BaseSpacingPct / 100.0, g_digits), ")");
    Print("Lookback: ", VolatilityLookbackHours, "h | Typical Vol: ", TypicalVolPct, "%");
    Print("Only Buy Below Attractor: ", OnlyBuyBelowAttractor ? "Yes" : "No");
}

//+------------------------------------------------------------------+
//| Print statistics                                                  |
//+------------------------------------------------------------------+
void PrintStatistics()
{
    Print("=== FillUp v5 Final Statistics ===");
    Print("Total Trades: ", g_totalTrades);
    Print("Winning Trades: ", g_winningTrades);
    Print("Total Profit: $", DoubleToString(g_totalProfit, 2));
    Print("Max Drawdown: ", DoubleToString(g_maxDrawdownPct, 2), "%");
    Print("Attractor Crossings: ", g_attractorCrossings);
    Print("Spacing Changes: ", g_spacingChanges);
}
//+------------------------------------------------------------------+
