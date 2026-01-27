//+------------------------------------------------------------------+
//|                                       CombinedJu_Diagnostic.mq5   |
//|              Diagnostic version with detailed logging             |
//|                                                                    |
//| Run this in Strategy Tester with same settings as production EA   |
//| to capture statistics for C++ backtest comparison                 |
//+------------------------------------------------------------------+
#property copyright "Diagnostic"
#property link      ""
#property version   "1.00"
#property strict

//+------------------------------------------------------------------+
//| Input Parameters - SET THESE TO MATCH YOUR PRODUCTION RUN        |
//+------------------------------------------------------------------+
input group "=== VERIFY THESE MATCH YOUR PRODUCTION RUN ==="
input double SurvivePct = 13.0;              // Survive %
input double BaseSpacing = 1.5;              // Base grid spacing ($)
input double MinVolume = 0.01;               // Minimum lot size
input double MaxVolume = 10.0;               // Maximum lot size

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

input group "=== Barbell Sizing - CRITICAL: CHECK THESE! ==="
input bool   EnableBarbellSizing = true;     // Enable barbell sizing
input int    BarbellThresholdPos = 1;        // Position # for barbell (P1 = 1)
input double BarbellMultiplier = 3.0;        // Lot multiplier (M3 = 3.0)

input group "=== Other ==="
input int    MagicNumber = 789012;           // Magic number

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
int    g_spacingChanges;
double g_totalLotsOpened;
double g_totalTPSet;
double g_sumSpread;
int    g_maxPositions;
double g_minSpacing;
double g_maxSpacing;
int    g_positionsAtEntry;

// NEW: Detailed tracking
long   g_ticksWithPositions;
long   g_spacingConditionTrue;
double g_sumSpacingWhenTrue;   // Sum of spacing when condition is true
double g_sumLowestBuy;         // Sum of lowestBuy when condition is true
double g_sumAsk;               // Sum of ask when condition is true

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
    g_spacingChanges = 0;
    g_totalLotsOpened = 0;
    g_totalTPSet = 0;
    g_sumSpread = 0;
    g_maxPositions = 0;
    g_minSpacing = DBL_MAX;
    g_maxSpacing = 0;
    g_positionsAtEntry = 0;
    g_ticksWithPositions = 0;
    g_spacingConditionTrue = 0;
    g_sumSpacingWhenTrue = 0;
    g_sumLowestBuy = 0;
    g_sumAsk = 0;

    Print("========================================");
    Print("=== DIAGNOSTIC VERSION - CombinedJu ===");
    Print("========================================");
    Print("Symbol: ", _Symbol);
    Print("Contract Size: ", g_contractSize);
    Print("Leverage: 1:", g_leverage);
    Print("--- PARAMETERS (VERIFY THESE!) ---");
    Print("SurvivePct: ", SurvivePct);
    Print("BaseSpacing: $", BaseSpacing);
    Print("RubberBandTP: ", EnableRubberBandTP ? "ON" : "OFF", " scale=", TPSqrtScale);
    Print("VelocityFilter: ", EnableVelocityFilter ? "ON" : "OFF",
          " window=", VelocityWindow, " thresh=", VelocityThresholdPct);
    Print("BarbellSizing: ", EnableBarbellSizing ? "ON" : "OFF",
          " pos=", BarbellThresholdPos, " mult=", BarbellMultiplier);
    Print("========================================");

    return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+
//| Expert deinitialization function                                  |
//+------------------------------------------------------------------+
void OnDeinit(const int reason)
{
    Print("========================================");
    Print("=== DIAGNOSTIC SUMMARY ===");
    Print("========================================");
    Print("Total OnTick() calls: ", g_totalOnTickCalls);
    Print("Velocity blocks: ", g_velocityBlocks);
    Print("Entries allowed: ", g_entriesAllowed);
    Print("Block rate: ", g_entriesAllowed > 0 ?
          DoubleToString(100.0 * g_velocityBlocks / (g_velocityBlocks + g_entriesAllowed), 2) + "%" : "N/A");
    Print("Spacing changes: ", g_spacingChanges);
    Print("Min spacing used: $", g_minSpacing);
    Print("Max spacing used: $", g_maxSpacing);
    Print("Max concurrent positions: ", g_maxPositions);
    Print("Avg lots per entry: ", g_entriesAllowed > 0 ?
          DoubleToString(g_totalLotsOpened / g_entriesAllowed, 4) : "N/A");
    Print("Avg TP distance: $", g_entriesAllowed > 0 ?
          DoubleToString(g_totalTPSet / g_entriesAllowed, 2) : "N/A");
    Print("Avg spread: $", g_totalOnTickCalls > 0 ?
          DoubleToString(g_sumSpread / g_totalOnTickCalls, 4) : "N/A");
    Print("========================================");
    Print("=== NEW: SPACING CONDITION ANALYSIS ===");
    Print("========================================");
    Print("Ticks with positions: ", g_ticksWithPositions);
    Print("Spacing condition true: ", g_spacingConditionTrue);
    Print("Condition true rate: ", g_ticksWithPositions > 0 ?
          DoubleToString(100.0 * g_spacingConditionTrue / g_ticksWithPositions, 4) + "%" : "N/A");
    Print("Avg spacing when true: $", g_spacingConditionTrue > 0 ?
          DoubleToString(g_sumSpacingWhenTrue / g_spacingConditionTrue, 4) : "N/A");
    Print("Avg lowestBuy when true: $", g_spacingConditionTrue > 0 ?
          DoubleToString(g_sumLowestBuy / g_spacingConditionTrue, 2) : "N/A");
    Print("Avg ask when true: $", g_spacingConditionTrue > 0 ?
          DoubleToString(g_sumAsk / g_spacingConditionTrue, 2) : "N/A");
    Print("Avg (lowestBuy - ask) when true: $", g_spacingConditionTrue > 0 ?
          DoubleToString((g_sumLowestBuy - g_sumAsk) / g_spacingConditionTrue, 4) : "N/A");
    Print("========================================");
    Print("=== COPY THIS TO COMPARE WITH C++ ===");
    Print("========================================");
    Print("OnTickCalls=", g_totalOnTickCalls);
    Print("TicksWithPositions=", g_ticksWithPositions);
    Print("SpacingConditionTrue=", g_spacingConditionTrue);
    Print("VelocityBlocks=", g_velocityBlocks);
    Print("EntriesAllowed=", g_entriesAllowed);
    Print("BlockRate=", g_entriesAllowed > 0 ?
          DoubleToString(100.0 * g_velocityBlocks / (g_velocityBlocks + g_entriesAllowed), 2) : "0");
    Print("AvgSpacingWhenTrue=", g_spacingConditionTrue > 0 ?
          DoubleToString(g_sumSpacingWhenTrue / g_spacingConditionTrue, 4) : "0");
    Print("AvgLots=", g_entriesAllowed > 0 ?
          DoubleToString(g_totalLotsOpened / g_entriesAllowed, 4) : "0");
    Print("AvgTP=", g_entriesAllowed > 0 ?
          DoubleToString(g_totalTPSet / g_entriesAllowed, 2) : "0");
    Print("AvgSpread=", g_totalOnTickCalls > 0 ?
          DoubleToString(g_sumSpread / g_totalOnTickCalls, 4) : "0");
    Print("MaxPositions=", g_maxPositions);
    Print("BarbellPos=", BarbellThresholdPos);
    Print("BarbellMult=", BarbellMultiplier);
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

    g_sumSpread += spread;

    // Track spacing range
    if(g_currentSpacing > 0) {
        g_minSpacing = MathMin(g_minSpacing, g_currentSpacing);
        g_maxSpacing = MathMax(g_maxSpacing, g_currentSpacing);
    }

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
        // First position: NO velocity filter (matches original EA and C++)
        shouldOpen = true;
    }
    else
    {
        g_ticksWithPositions++;  // Track ticks where we have positions

        if(lowestBuy >= ask + g_currentSpacing)
        {
            g_spacingConditionTrue++;  // Track when spacing condition is true
            g_sumSpacingWhenTrue += g_currentSpacing;
            g_sumLowestBuy += lowestBuy;
            g_sumAsk += ask;

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

        if(lots >= MinVolume)
        {
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
                g_totalLotsOpened += lots;
                g_totalTPSet += (tp - ask);
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
            g_spacingChanges++;
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
        return 0.0;
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
    request.comment   = "CombinedJu_Diag_" + IntegerToString(posNum);

    if(!OrderSend(request, result))
    {
        return false;
    }

    return (result.retcode == TRADE_RETCODE_DONE);
}
//+------------------------------------------------------------------+
