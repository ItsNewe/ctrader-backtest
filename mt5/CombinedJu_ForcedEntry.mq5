//+------------------------------------------------------------------+
//|                                     CombinedJu_ForcedEntry.mq5   |
//|              Test: Force MinVolume entries when lot sizing = 0   |
//|                                                                   |
//| HYPOTHESIS: C++ backtest showed 28.59x vs 15.11x when we force   |
//| entries at MinVolume even when margin protection returns 0.      |
//| This EA tests if MT5 can also benefit from this approach.        |
//+------------------------------------------------------------------+
#property copyright "ForcedEntry Test"
#property link      ""
#property version   "1.00"
#property strict

//+------------------------------------------------------------------+
//| Input Parameters                                                  |
//+------------------------------------------------------------------+
input group "=== Core Parameters ==="
input double SurvivePct = 13.0;              // Survive %
input double BaseSpacing = 1.5;              // Base grid spacing ($)
input double MinVolume = 0.01;               // Minimum lot size
input double MaxVolume = 10.0;               // Maximum lot size

input group "=== FORCE ENTRY TEST ==="
input bool   ForceMinVolumeEntry = true;     // Force MinVolume when lot=0

input group "=== Adaptive Spacing ==="
input double VolatilityLookbackHours = 4.0;  // Volatility lookback (hours)
input double TypicalVolPct = 0.55;           // Typical volatility (% of price)
input double MinSpacingMult = 0.5;           // Min spacing multiplier
input double MaxSpacingMult = 3.0;           // Max spacing multiplier

input group "=== Rubber Band TP ==="
input bool   EnableRubberBandTP = true;      // Enable Rubber Band TP
input double TPSqrtScale = 0.5;              // TP sqrt scale factor
input double TPMinimum = 1.5;                // Minimum TP ($)

input group "=== Velocity Filter ==="
input bool   EnableVelocityFilter = true;    // Enable velocity filter
input int    VelocityWindow = 10;            // Velocity window (ticks)
input double VelocityThresholdPct = 0.01;    // Max velocity (% of price)

input group "=== Barbell Sizing ==="
input bool   EnableBarbellSizing = true;     // Enable barbell sizing
input int    BarbellThresholdPos = 1;        // Position # for barbell (P1 = 1)
input double BarbellMultiplier = 3.0;        // Lot multiplier (M3 = 3.0)

input group "=== Other ==="
input int    MagicNumber = 789013;           // Magic number

//+------------------------------------------------------------------+
//| Global Variables                                                  |
//+------------------------------------------------------------------+
double g_currentSpacing;
double g_recentHigh;
double g_recentLow;
datetime g_lastVolatilityReset;
double g_firstEntryPrice;
double g_priceWindow[];
int    g_priceWindowIndex;
double g_currentVelocityPct;
double g_contractSize;
double g_leverage;
int    g_digits;
double g_point;

// DIAGNOSTIC STATISTICS
long   g_totalOnTickCalls;
int    g_velocityBlocks;
int    g_entriesAllowed;
int    g_forcedEntries;         // NEW: Count entries forced at MinVolume
int    g_lotSizeZeroBlocks;     // NEW: Count when lot sizing returned 0 (without force)
int    g_maxPositions;

//+------------------------------------------------------------------+
//| Expert initialization function                                    |
//+------------------------------------------------------------------+
int OnInit()
{
    g_contractSize = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);
    g_leverage = (double)AccountInfoInteger(ACCOUNT_LEVERAGE);
    g_digits = (int)SymbolInfoInteger(_Symbol, SYMBOL_DIGITS);
    g_point = SymbolInfoDouble(_Symbol, SYMBOL_POINT);

    g_currentSpacing = BaseSpacing;
    g_recentHigh = 0;
    g_recentLow = DBL_MAX;
    g_lastVolatilityReset = 0;
    g_firstEntryPrice = 0;

    ArrayResize(g_priceWindow, VelocityWindow);
    ArrayInitialize(g_priceWindow, 0);
    g_priceWindowIndex = 0;
    g_currentVelocityPct = 0;

    // DIAGNOSTIC INIT
    g_totalOnTickCalls = 0;
    g_velocityBlocks = 0;
    g_entriesAllowed = 0;
    g_forcedEntries = 0;
    g_lotSizeZeroBlocks = 0;
    g_maxPositions = 0;

    Print("========================================");
    Print("=== FORCED ENTRY TEST - CombinedJu ===");
    Print("========================================");
    Print("Symbol: ", _Symbol);
    Print("Contract Size: ", g_contractSize);
    Print("Leverage: 1:", g_leverage);
    Print("--- KEY SETTING ---");
    Print("ForceMinVolumeEntry: ", ForceMinVolumeEntry ? "ON" : "OFF");
    if(ForceMinVolumeEntry)
        Print(">>> Will force entries at ", MinVolume, " lots when margin says 0");
    Print("========================================");

    return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+
//| Expert deinitialization function                                  |
//+------------------------------------------------------------------+
void OnDeinit(const int reason)
{
    Print("========================================");
    Print("=== FORCED ENTRY SUMMARY ===");
    Print("========================================");
    Print("Total OnTick() calls: ", g_totalOnTickCalls);
    Print("Velocity blocks: ", g_velocityBlocks);
    Print("Entries allowed: ", g_entriesAllowed);
    Print("FORCED entries (lot=0 but proceeded): ", g_forcedEntries);
    Print("Lot sizing returned 0: ", g_lotSizeZeroBlocks + g_forcedEntries);
    Print("Max concurrent positions: ", g_maxPositions);
    Print("========================================");
    Print("=== COMPARISON DATA ===");
    Print("ForceMinVolumeEntry=", ForceMinVolumeEntry ? "ON" : "OFF");
    Print("EntriesAllowed=", g_entriesAllowed);
    Print("ForcedEntries=", g_forcedEntries);
    Print("VelocityBlocks=", g_velocityBlocks);
    Print("MaxPositions=", g_maxPositions);
    Print("========================================");
}

//+------------------------------------------------------------------+
//| Expert tick function                                              |
//+------------------------------------------------------------------+
void OnTick()
{
    g_totalOnTickCalls++;

    double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
    double spread = ask - bid;

    // Update systems
    UpdateVolatility(bid);
    UpdateVelocity(bid);
    UpdateAdaptiveSpacing(bid);

    // Count current positions
    int positionCount = 0;
    double totalVolume = 0;
    double highestBuy = 0;
    double lowestBuy = DBL_MAX;

    for(int i = PositionsTotal() - 1; i >= 0; i--)
    {
        ulong ticket = PositionGetTicket(i);
        if(ticket > 0)
        {
            if(PositionGetString(POSITION_SYMBOL) == _Symbol &&
               PositionGetInteger(POSITION_MAGIC) == MagicNumber &&
               PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY)
            {
                positionCount++;
                totalVolume += PositionGetDouble(POSITION_VOLUME);
                double entryPrice = PositionGetDouble(POSITION_PRICE_OPEN);
                highestBuy = MathMax(highestBuy, entryPrice);
                lowestBuy = MathMin(lowestBuy, entryPrice);
            }
        }
    }

    g_maxPositions = MathMax(g_maxPositions, positionCount);

    // Entry logic
    bool isFirstPosition = (positionCount == 0);
    bool shouldOpen = false;

    if(isFirstPosition)
    {
        shouldOpen = true;
    }
    else
    {
        if(lowestBuy >= ask + g_currentSpacing)
        {
            if(EnableVelocityFilter && !CheckVelocityZero())
            {
                g_velocityBlocks++;
                return;
            }
            shouldOpen = true;
        }
    }

    if(shouldOpen)
    {
        double lots = CalculateLotSize(ask, positionCount, totalVolume, highestBuy);
        bool wasForced = false;

        // KEY CHANGE: Force MinVolume if lot sizing returned 0
        if(lots < MinVolume)
        {
            if(ForceMinVolumeEntry)
            {
                lots = MinVolume;  // Force entry anyway!
                wasForced = true;
            }
            else
            {
                g_lotSizeZeroBlocks++;
                return;  // Original behavior: skip entry
            }
        }

        if(EnableBarbellSizing && !isFirstPosition)
        {
            lots = ApplyBarbellSizing(lots, positionCount);
        }

        double tp = CalculateTP(ask, spread, isFirstPosition);

        if(OpenBuyOrder(lots, ask, tp, positionCount))
        {
            if(isFirstPosition)
            {
                g_firstEntryPrice = ask;
            }
            g_entriesAllowed++;
            if(wasForced)
            {
                g_forcedEntries++;
            }
        }
    }
}

//+------------------------------------------------------------------+
//| Volatility update                                                 |
//+------------------------------------------------------------------+
void UpdateVolatility(double bid)
{
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
//| Adaptive spacing update                                           |
//+------------------------------------------------------------------+
void UpdateAdaptiveSpacing(double bid)
{
    double range = g_recentHigh - g_recentLow;

    if(range > 0 && g_recentHigh > 0 && bid > 0)
    {
        double typicalVol = bid * (TypicalVolPct / 100.0);
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
//| Velocity update                                                   |
//+------------------------------------------------------------------+
void UpdateVelocity(double bid)
{
    g_priceWindow[g_priceWindowIndex] = bid;
    g_priceWindowIndex = (g_priceWindowIndex + 1) % VelocityWindow;

    int oldestIndex = g_priceWindowIndex;
    double oldPrice = g_priceWindow[oldestIndex];

    if(oldPrice > 0)
    {
        g_currentVelocityPct = (bid - oldPrice) / oldPrice * 100.0;
    }
}

//+------------------------------------------------------------------+
//| Velocity check                                                    |
//+------------------------------------------------------------------+
bool CheckVelocityZero()
{
    return MathAbs(g_currentVelocityPct) < VelocityThresholdPct;
}

//+------------------------------------------------------------------+
//| Calculate TP                                                      |
//+------------------------------------------------------------------+
double CalculateTP(double ask, double spread, bool isFirstPosition)
{
    double tp;

    if(!EnableRubberBandTP || g_firstEntryPrice <= 0 || isFirstPosition)
    {
        tp = ask + spread + g_currentSpacing;
    }
    else
    {
        double deviation = MathAbs(g_firstEntryPrice - ask);
        double tpAddition = TPSqrtScale * MathSqrt(deviation);
        double tpDistance = MathMax(TPMinimum, tpAddition);
        tp = ask + spread + tpDistance;
    }

    return NormalizeDouble(tp, g_digits);
}

//+------------------------------------------------------------------+
//| Apply barbell sizing                                              |
//+------------------------------------------------------------------+
double ApplyBarbellSizing(double baseLots, int positionCount)
{
    if(positionCount < BarbellThresholdPos)
    {
        return baseLots;
    }

    double safetyFactor = 1.0 / (1.0 + positionCount * 0.05);
    double adjustedMult = 1.0 + (BarbellMultiplier - 1.0) * safetyFactor;

    double newLots = baseLots * adjustedMult;
    newLots = MathMax(newLots, MinVolume);
    newLots = MathMin(newLots, MaxVolume);

    double lotStep = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_STEP);
    newLots = MathFloor(newLots / lotStep) * lotStep;

    return newLots;
}

//+------------------------------------------------------------------+
//| Calculate lot size                                                |
//+------------------------------------------------------------------+
double CalculateLotSize(double currentAsk, int positionsTotal,
                        double volumeOfOpenTrades, double highestBuy)
{
    double equity = AccountInfoDouble(ACCOUNT_EQUITY);

    double usedMargin = 0;
    for(int i = PositionsTotal() - 1; i >= 0; i--)
    {
        ulong ticket = PositionGetTicket(i);
        if(ticket > 0)
        {
            if(PositionGetString(POSITION_SYMBOL) == _Symbol &&
               PositionGetInteger(POSITION_MAGIC) == MagicNumber)
            {
                double lots = PositionGetDouble(POSITION_VOLUME);
                double entryPrice = PositionGetDouble(POSITION_PRICE_OPEN);
                usedMargin += lots * g_contractSize * entryPrice / g_leverage;
            }
        }
    }

    double endPrice;
    if(positionsTotal == 0)
    {
        endPrice = currentAsk * ((100.0 - SurvivePct) / 100.0);
    }
    else
    {
        endPrice = highestBuy * ((100.0 - SurvivePct) / 100.0);
    }

    double distance = currentAsk - endPrice;
    double numberOfTrades = MathFloor(distance / g_currentSpacing);
    if(numberOfTrades <= 0) numberOfTrades = 1;

    double equityAtTarget = equity - volumeOfOpenTrades * distance * g_contractSize;

    double marginStopOut = 20.0;
    if(usedMargin > 0 && (equityAtTarget / usedMargin * 100.0) < marginStopOut)
    {
        return 0.0;  // Return 0 - caller will force MinVolume if enabled
    }

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

    double lotStep = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_STEP);
    tradeSize = MathFloor(tradeSize / lotStep) * lotStep;

    return MathMin(tradeSize, MaxVolume);
}

//+------------------------------------------------------------------+
//| Open buy order                                                    |
//+------------------------------------------------------------------+
bool OpenBuyOrder(double lots, double price, double tp, int posNum)
{
    MqlTradeRequest request = {};
    MqlTradeResult  result  = {};

    request.action    = TRADE_ACTION_DEAL;
    request.symbol    = _Symbol;
    request.volume    = lots;
    request.type      = ORDER_TYPE_BUY;
    request.price     = price;
    request.tp        = tp;
    request.deviation = 50;
    request.magic     = MagicNumber;
    request.comment   = "CombJu_Force_" + IntegerToString(posNum);

    if(!OrderSend(request, result))
    {
        return false;
    }

    return (result.retcode == TRADE_RETCODE_DONE);
}
//+------------------------------------------------------------------+
