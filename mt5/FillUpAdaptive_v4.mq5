//+------------------------------------------------------------------+
//|                                           FillUpAdaptive_v4.mq5  |
//|                                    Oscillation-Stabilized Grid   |
//|                                   Percentage-Based Grid Spacing  |
//|                        All spacing params scale with price level  |
//+------------------------------------------------------------------+
#property copyright "Backtest Framework"
#property link      ""
#property version   "4.30"
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
input double VolatilityLookbackHours = 4.0;  // Volatility lookback (hours)
input double TypicalVolPct = 0.55;           // Typical volatility (% of price)
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

// Persistence
datetime g_lastSaveTime;
string g_stateFileName;

// Per-tick cache (avoid redundant API calls)
double g_tickBid;
double g_tickAsk;
double g_tickEquity;
datetime g_tickTime;

// Dashboard throttle
datetime g_lastDashboardUpdate;

// Cached position state (updated once per tick)
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

    // State file name unique per symbol + magic
    g_stateFileName = "FillUp_v4_" + _Symbol + "_" + IntegerToString(MagicNumber) + ".state";
    g_lastSaveTime = 0;

    // Try to restore state from file
    bool stateRestored = LoadState();

    if(!stateRestored)
    {
        // No valid saved state - initialize fresh
        double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
        if(bid > 0)
        {
            g_currentSpacing = bid * (BaseSpacingPct / 100.0);
            g_effectiveBase = g_currentSpacing;
        }
        else
        {
            g_currentSpacing = 1.0;
            g_effectiveBase = 1.0;
        }

        g_recentHigh = 0;
        g_recentLow = DBL_MAX;
        g_lastVolatilityReset = 0;
        g_spacingChanges = 0;
        g_typicalVol = 0;
        g_startTime = TimeCurrent();

        // Try to reconstruct volatility from bar data
        ReconstructVolatilityFromBars();

        Print("State: Initialized fresh (no saved state found)");
    }
    else
    {
        Print("State: Restored from file (", g_stateFileName, ")");
    }

    // Always reconstruct statistics from trade history
    ReconstructStatistics();

    // Initialize peak equity
    double equity = AccountInfoDouble(ACCOUNT_EQUITY);
    if(equity > g_peakEquity)
        g_peakEquity = equity;

    // Create dashboard
    if(ShowDashboard)
    {
        CreateDashboard();
    }

    Print("=== FillUp Adaptive v4.3 Initialized ===");
    Print("=== Restart-Safe + Pct-Based ===");
    PrintSymbolInfo();
    Print("Current spacing: $", DoubleToString(g_currentSpacing, g_digits));
    Print("Spacing changes: ", g_spacingChanges);
    Print("Restored trades: ", g_totalTrades, " (", g_winningTrades, " wins)");
    Print("Restored profit: $", DoubleToString(g_totalProfit, 2));

    return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+
//| Expert deinitialization                                           |
//+------------------------------------------------------------------+
void OnDeinit(const int reason)
{
    // Save state (may not execute on crash, but helps for clean shutdowns)
    SaveState();

    // Remove dashboard objects
    ObjectsDeleteAll(0, "FillUp_");

    Print("=== EA Stopped (reason=", reason, ") ===");
    PrintStatistics();
}

//+------------------------------------------------------------------+
//| Expert tick function                                              |
//+------------------------------------------------------------------+
void OnTick()
{
    // Cache market data once per tick (avoid redundant API calls)
    g_tickBid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
    g_tickAsk = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    g_tickEquity = AccountInfoDouble(ACCOUNT_EQUITY);
    g_tickTime = TimeCurrent();

    // Single position scan - collect all needed data in one pass
    ScanPositions();

    // Update volatility and spacing
    UpdateVolatility();
    UpdateAdaptiveSpacing();

    // Update statistics
    UpdateStatistics();

    // Periodic state save (every 60 seconds)
    if(g_tickTime - g_lastSaveTime >= 60)
    {
        SaveState();
        g_lastSaveTime = g_tickTime;
    }

    // Dashboard throttle: update at most once per second
    if(ShowDashboard && g_tickTime != g_lastDashboardUpdate)
    {
        UpdateDashboard();
        g_lastDashboardUpdate = g_tickTime;
    }

    // Skip trading if disabled
    if(!EnableTrading) return;

    // Determine if new position needed
    double spread = g_tickAsk - g_tickBid;
    bool shouldOpen = false;

    if(g_cachedPosCount == 0)
    {
        shouldOpen = true;  // First position
    }
    else if(g_cachedLowestBuy >= g_tickAsk + g_currentSpacing)
    {
        shouldOpen = true;  // Price dropped - buy the dip
    }
    else if(g_cachedHighestBuy <= g_tickAsk - g_currentSpacing)
    {
        shouldOpen = true;  // Price rose - new higher entry
    }

    if(shouldOpen)
    {
        double lots = CalculateLotSize(g_tickAsk, g_cachedPosCount, g_cachedTotalVolume, g_cachedHighestBuy);
        if(lots >= g_minLot)
        {
            if(OpenBuyOrder(lots, g_tickAsk, spread))
            {
                g_totalTrades++;
                SaveState();  // Save after opening trade
            }
        }
    }
}

//+------------------------------------------------------------------+
//| Single-pass position scanner (replaces multiple loops)            |
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
//| Save state to file (called periodically + on events)             |
//+------------------------------------------------------------------+
void SaveState()
{
    if(MQLInfoInteger(MQL_TESTER) || MQLInfoInteger(MQL_OPTIMIZATION))
        return;

    int handle = FileOpen(g_stateFileName, FILE_WRITE | FILE_TXT | FILE_ANSI);
    if(handle == INVALID_HANDLE)
    {
        return;  // Silent fail - will retry next time
    }

    FileWriteString(handle, "# FillUp v4 State File - DO NOT EDIT\n");
    FileWriteString(handle, "version=4.1\n");
    FileWriteString(handle, "symbol=" + _Symbol + "\n");
    FileWriteString(handle, "magic=" + IntegerToString(MagicNumber) + "\n");
    FileWriteString(handle, "savedAt=" + IntegerToString((long)TimeCurrent()) + "\n");

    // Critical trading state
    FileWriteString(handle, "currentSpacing=" + DoubleToString(g_currentSpacing, 8) + "\n");
    FileWriteString(handle, "recentHigh=" + DoubleToString(g_recentHigh, 8) + "\n");
    FileWriteString(handle, "recentLow=" + DoubleToString(g_recentLow, 8) + "\n");
    FileWriteString(handle, "lastVolReset=" + IntegerToString((long)g_lastVolatilityReset) + "\n");
    FileWriteString(handle, "spacingChanges=" + IntegerToString(g_spacingChanges) + "\n");
    FileWriteString(handle, "typicalVol=" + DoubleToString(g_typicalVol, 8) + "\n");
    FileWriteString(handle, "effectiveBase=" + DoubleToString(g_effectiveBase, 8) + "\n");

    // Statistics
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
    bool hasSpacing = false;

    while(!FileIsEnding(handle))
    {
        string line = FileReadString(handle);
        if(StringLen(line) == 0 || StringGetCharacter(line, 0) == '#')
            continue;

        // Parse key=value
        int eqPos = StringFind(line, "=");
        if(eqPos < 0) continue;

        string key = StringSubstr(line, 0, eqPos);
        string value = StringSubstr(line, eqPos + 1);

        if(key == "symbol") savedSymbol = value;
        else if(key == "magic") savedMagic = (int)StringToInteger(value);
        else if(key == "savedAt") savedAt = (datetime)StringToInteger(value);
        else if(key == "currentSpacing") { g_currentSpacing = StringToDouble(value); hasSpacing = true; }
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

    // Validate: must match symbol and magic
    if(savedSymbol != _Symbol || savedMagic != MagicNumber)
    {
        Print("State file mismatch: symbol=", savedSymbol, " magic=", savedMagic);
        return false;
    }

    // Validate: file must not be too old (max 7 days stale)
    datetime now = TimeCurrent();
    if(savedAt > 0 && now - savedAt > 7 * 24 * 3600)
    {
        Print("State file too old (", (now - savedAt) / 3600, " hours). Starting fresh.");
        return false;
    }

    // If lookback window is stale, reconstruct it from bars
    int lookbackSeconds = (int)(VolatilityLookbackHours * 3600);
    if(savedAt > 0 && now - savedAt > lookbackSeconds)
    {
        Print("Lookback window stale (gap=", (now - savedAt) / 60, " min). Reconstructing from bars.");
        ReconstructVolatilityFromBars();
        // Keep other state (spacing, statistics)
    }

    return hasSpacing;
}

//+------------------------------------------------------------------+
//| Reconstruct volatility window from recent bar data               |
//+------------------------------------------------------------------+
void ReconstructVolatilityFromBars()
{
    double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
    if(bid <= 0) return;

    // Use M1 bars to reconstruct the lookback window
    int barsNeeded = (int)(VolatilityLookbackHours * 60);  // 1 bar per minute
    if(barsNeeded < 1) barsNeeded = 60;
    if(barsNeeded > 1440) barsNeeded = 1440;  // Cap at 1 day

    double high = 0;
    double low = DBL_MAX;

    // Get recent M1 bars
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

        Print("Reconstructed volatility from ", copied, " M1 bars: High=",
              DoubleToString(high, g_digits), " Low=", DoubleToString(low, g_digits),
              " Range=$", DoubleToString(high - low, g_digits));

        // Recalculate spacing from reconstructed range
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
    }
    else
    {
        // No bar data available - use defaults
        g_recentHigh = bid;
        g_recentLow = bid;
        g_lastVolatilityReset = TimeCurrent();
        g_currentSpacing = bid * (BaseSpacingPct / 100.0);
        Print("No bar data available. Using default spacing.");
    }
}

//+------------------------------------------------------------------+
//| Reconstruct statistics from trade history                         |
//+------------------------------------------------------------------+
void ReconstructStatistics()
{
    // Find earliest trade with our magic number
    datetime earliest = TimeCurrent() - 365 * 24 * 3600;  // Look back 1 year max
    if(g_startTime > 0 && g_startTime > earliest)
        earliest = g_startTime;

    if(!HistorySelect(earliest, TimeCurrent()))
        return;

    int totalTrades = 0;
    int winningTrades = 0;
    double totalProfit = 0;

    for(int i = 0; i < HistoryDealsTotal(); i++)
    {
        ulong dealTicket = HistoryDealGetTicket(i);
        if(dealTicket == 0) continue;

        if(HistoryDealGetString(dealTicket, DEAL_SYMBOL) != _Symbol) continue;
        if(HistoryDealGetInteger(dealTicket, DEAL_MAGIC) != MagicNumber) continue;

        int entry = (int)HistoryDealGetInteger(dealTicket, DEAL_ENTRY);

        if(entry == DEAL_ENTRY_IN)
        {
            totalTrades++;
            // Set start time from earliest trade if not already set
            if(g_startTime == 0)
            {
                g_startTime = (datetime)HistoryDealGetInteger(dealTicket, DEAL_TIME);
            }
        }
        else if(entry == DEAL_ENTRY_OUT)
        {
            double profit = HistoryDealGetDouble(dealTicket, DEAL_PROFIT)
                          + HistoryDealGetDouble(dealTicket, DEAL_SWAP)
                          + HistoryDealGetDouble(dealTicket, DEAL_COMMISSION);
            totalProfit += profit;
            if(profit > 0) winningTrades++;
        }
    }

    // Also count currently open positions
    for(int i = PositionsTotal() - 1; i >= 0; i--)
    {
        ulong ticket = PositionGetTicket(i);
        if(ticket > 0 && PositionGetString(POSITION_SYMBOL) == _Symbol &&
           PositionGetInteger(POSITION_MAGIC) == MagicNumber)
        {
            totalTrades++;
        }
    }

    g_totalTrades = totalTrades;
    g_winningTrades = winningTrades;
    g_totalProfit = totalProfit;

    if(g_startTime == 0)
        g_startTime = TimeCurrent();
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
//| Update adaptive spacing (fully percentage-based)                  |
//+------------------------------------------------------------------+
void UpdateAdaptiveSpacing()
{
    double range = g_recentHigh - g_recentLow;

    if(range > 0 && g_recentHigh > 0 && g_tickBid > 0)
    {
        // All parameters scale with current price
        g_typicalVol = g_tickBid * (TypicalVolPct / 100.0);
        g_effectiveBase = g_tickBid * (BaseSpacingPct / 100.0);
        double minAbs = g_tickBid * (MinSpacingPct / 100.0);
        double maxAbs = g_tickBid * (MaxSpacingPct / 100.0);
        double changeThresh = g_tickBid * (SpacingChangeThresholdPct / 100.0);

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
    // Use cached values (no redundant API calls or position loops)
    double usedMargin = g_cachedUsedMargin;

    // Calculate worst-case price
    double endPrice = (positionsTotal == 0)
        ? currentAsk * ((100.0 - SurvivePct) / 100.0)
        : highestBuy * ((100.0 - SurvivePct) / 100.0);

    double distance = currentAsk - endPrice;
    double numberOfTrades = MathFloor(distance / g_currentSpacing);
    if(numberOfTrades <= 0) numberOfTrades = 1;

    // Potential equity at worst case
    double equityAtTarget = g_tickEquity - volumeOfOpenTrades * distance * g_contractSize;

    // Margin stop-out check (20% level)
    double marginStopOut = 20.0;
    if(usedMargin > 0 && (equityAtTarget / usedMargin * 100.0) < marginStopOut)
    {
        return 0.0;
    }

    // Calculate lot size using triangular position sizing
    // Solve analytically: find max mult where (equityAtTarget - mult*dEquity) / (usedMargin + mult*dMargin) > stopOut/100
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

    // Match C++ behavior: if full-grid survival requires sub-minimum lots,
    // still open at minimum. The full grid rarely fills completely; positions
    // close via TP during oscillations. Engine margin stop-out protects worst case.
    mult = MathMax(1.0, mult);

    double tradeSize = mult * MinVolume;

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
    request.comment = "FillUp v4 " + DoubleToString(BaseSpacingPct, 2) + "%";
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
    CreateLabel("FillUp_Title", x + 10, y + 10, "FillUp Adaptive v4.3", clrGold, 12);
    CreateLabel("FillUp_Sub", x + 10, y + 28, "%-Based | Restart-Safe", clrDarkGray, 8);

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
    // All values from per-tick cache (no API calls or position loops)
    double drawdown = g_peakEquity - g_tickEquity;
    double drawdownPct = (g_peakEquity > 0) ? (drawdown / g_peakEquity * 100) : 0;
    double range = g_recentHigh - g_recentLow;
    double spacingPct = (g_tickBid > 0) ? (g_currentSpacing / g_tickBid * 100.0) : 0;

    // Update labels
    ObjectSetString(0, "FillUp_V1", OBJPROP_TEXT, "$" + DoubleToString(g_currentSpacing, g_digits));
    ObjectSetString(0, "FillUp_V13", OBJPROP_TEXT, DoubleToString(spacingPct, 3) + "% [base:" + DoubleToString(BaseSpacingPct, 2) + "%]");
    ObjectSetString(0, "FillUp_V2", OBJPROP_TEXT, "$" + DoubleToString(range, 2) +
                    " /" + DoubleToString(VolatilityLookbackHours, 1) + "h");
    ObjectSetString(0, "FillUp_V12", OBJPROP_TEXT, DoubleToString(TypicalVolPct, 3) + "%");
    ObjectSetString(0, "FillUp_V3", OBJPROP_TEXT, IntegerToString(g_cachedPosCount));
    ObjectSetString(0, "FillUp_V4", OBJPROP_TEXT, DoubleToString(g_cachedTotalVolume, 2) + " lots");

    // Unrealized P/L with color
    ObjectSetString(0, "FillUp_V5", OBJPROP_TEXT, "$" + DoubleToString(g_cachedUnrealizedPL, 2));
    ObjectSetInteger(0, "FillUp_V5", OBJPROP_COLOR, g_cachedUnrealizedPL >= 0 ? clrLime : clrRed);

    ObjectSetString(0, "FillUp_V6", OBJPROP_TEXT, "$" + DoubleToString(g_tickEquity, 2));
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
    Print("Base Spacing: ", BaseSpacingPct, "% of price (",
          DoubleToString(SurvivePct / BaseSpacingPct, 0), " grid levels)");
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
