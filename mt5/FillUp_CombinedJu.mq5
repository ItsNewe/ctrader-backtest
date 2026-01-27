//+------------------------------------------------------------------+
//|                                            FillUp_CombinedJu.mq5 |
//|                        Combined Ju Strategy (Rubber Band + Velocity + Barbell) |
//|                                                                  |
//| Three synergistic concepts:                                       |
//| 1. Rubber Band TP - TP scales with deviation from first entry    |
//| 2. Velocity Zero Filter - Enter only at local minima             |
//| 3. Threshold Barbell - Larger lots after position threshold      |
//|                                                                  |
//| Backtest results (2025 XAUUSD):                                  |
//| - FULL_JU_THR (pos=5, mult=2): 18.17x, 68.1% DD                  |
//| - FULL_JU_THR3 (pos=3, mult=2): 18.61x, 68.5% DD                 |
//| - 2-year sequential: 126x with P1_M3 config                       |
//+------------------------------------------------------------------+
#property copyright "Combined Ju Strategy"
#property link      ""
#property version   "1.00"
#property strict

//+------------------------------------------------------------------+
//| Input Parameters                                                  |
//+------------------------------------------------------------------+
input group "=== Core Strategy Parameters ==="
input double SurvivePct = 13.0;              // Survive % (max adverse move tolerance)
input double BaseSpacing = 1.5;              // Base grid spacing ($)
input double MinVolume = 0.01;               // Minimum lot size
input double MaxVolume = 10.0;               // Maximum lot size

input group "=== Adaptive Spacing ==="
input double VolatilityLookbackHours = 4.0;  // Volatility lookback (hours)
input double TypicalVolPct = 0.55;           // Typical volatility (% of price)
input double MinSpacingMult = 0.5;           // Min spacing multiplier
input double MaxSpacingMult = 3.0;           // Max spacing multiplier

input group "=== Rubber Band TP (SQRT mode) ==="
input bool   EnableRubberBandTP = true;      // Enable Rubber Band TP
input double TPSqrtScale = 0.5;              // TP sqrt scale factor
input double TPMinimum = 1.5;                // Minimum TP ($)

input group "=== Velocity Zero Filter (Wu Wei) ==="
input bool   EnableVelocityFilter = true;    // Enable velocity filter
input int    VelocityWindow = 10;            // Velocity measurement window (ticks)
input double VelocityThresholdPct = 0.01;    // Max velocity for entry (% of price)

input group "=== Threshold Barbell Sizing ==="
input bool   EnableBarbellSizing = true;     // Enable barbell sizing
input int    BarbellThresholdPos = 5;        // Position # when barbell kicks in
input double BarbellMultiplier = 2.0;        // Lot multiplier after threshold

input group "=== Risk Management ==="
input int    MagicNumber = 789012;           // Magic number for this EA

//+------------------------------------------------------------------+
//| Global Variables                                                  |
//+------------------------------------------------------------------+
// Adaptive spacing
double g_currentSpacing;
double g_recentHigh;
double g_recentLow;
datetime g_lastVolatilityReset;

// Rubber band TP
double g_firstEntryPrice;

// Velocity tracking
double g_priceWindow[];
int    g_priceWindowIndex;
double g_currentVelocityPct;

// Symbol info
double g_contractSize;
double g_leverage;
int    g_digits;
double g_point;

// Statistics
int    g_velocityBlocks;
int    g_entriesAllowed;
int    g_spacingChanges;

//+------------------------------------------------------------------+
//| Expert initialization function                                    |
//+------------------------------------------------------------------+
int OnInit()
{
    // Initialize symbol info
    g_contractSize = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);
    g_leverage = (double)AccountInfoInteger(ACCOUNT_LEVERAGE);
    g_digits = (int)SymbolInfoInteger(_Symbol, SYMBOL_DIGITS);
    g_point = SymbolInfoDouble(_Symbol, SYMBOL_POINT);

    // Initialize adaptive spacing
    g_currentSpacing = BaseSpacing;
    g_recentHigh = 0;
    g_recentLow = DBL_MAX;
    g_lastVolatilityReset = 0;

    // Initialize rubber band
    g_firstEntryPrice = 0;

    // Initialize velocity window
    ArrayResize(g_priceWindow, VelocityWindow);
    ArrayInitialize(g_priceWindow, 0);
    g_priceWindowIndex = 0;
    g_currentVelocityPct = 0;

    // Statistics
    g_velocityBlocks = 0;
    g_entriesAllowed = 0;
    g_spacingChanges = 0;

    Print("=== FillUp Combined Ju EA Initialized ===");
    Print("Symbol: ", _Symbol);
    Print("Contract Size: ", g_contractSize);
    Print("Leverage: 1:", g_leverage);
    Print("Survive %: ", SurvivePct);
    Print("Base Spacing: $", BaseSpacing);
    Print("--- Components ---");
    Print("Rubber Band TP: ", EnableRubberBandTP ? "ON" : "OFF",
          " (sqrt_scale=", TPSqrtScale, ")");
    Print("Velocity Filter: ", EnableVelocityFilter ? "ON" : "OFF",
          " (threshold=", VelocityThresholdPct, "%)");
    Print("Barbell Sizing: ", EnableBarbellSizing ? "ON" : "OFF",
          " (pos=", BarbellThresholdPos, ", mult=", BarbellMultiplier, "x)");

    return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+
//| Expert deinitialization function                                  |
//+------------------------------------------------------------------+
void OnDeinit(const int reason)
{
    Print("=== Combined Ju EA Stopped ===");
    Print("Velocity blocks: ", g_velocityBlocks);
    Print("Entries allowed: ", g_entriesAllowed);
    Print("Spacing changes: ", g_spacingChanges);
}

//+------------------------------------------------------------------+
//| Expert tick function                                              |
//+------------------------------------------------------------------+
void OnTick()
{
    double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
    double spread = ask - bid;

    // Update tracking systems
    UpdateVolatility(bid);
    UpdateAdaptiveSpacing(bid);
    UpdateVelocity(bid);

    // Get position info
    double lowestBuy = DBL_MAX;
    double highestBuy = -DBL_MAX;
    double totalVolume = 0;
    int positionCount = 0;

    for(int i = PositionsTotal() - 1; i >= 0; i--)
    {
        ulong ticket = PositionGetTicket(i);
        if(ticket > 0)
        {
            if(PositionGetString(POSITION_SYMBOL) == _Symbol &&
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
    }

    // Reset first entry price when all positions closed
    if(positionCount == 0)
    {
        g_firstEntryPrice = 0;
    }

    // Determine if we should open a new position
    bool shouldOpen = false;
    bool isFirstPosition = false;

    if(positionCount == 0)
    {
        // First position - always open (no velocity filter)
        shouldOpen = true;
        isFirstPosition = true;
    }
    else
    {
        // Additional positions - check spacing
        if(lowestBuy >= ask + g_currentSpacing)
        {
            // Price dropped enough - check velocity filter
            if(EnableVelocityFilter)
            {
                if(!CheckVelocityZero())
                {
                    g_velocityBlocks++;
                    return;  // Skip this entry
                }
            }
            shouldOpen = true;
        }
    }

    if(shouldOpen)
    {
        // Calculate base lot size
        double lots = CalculateLotSize(ask, positionCount, totalVolume, highestBuy);

        if(lots >= MinVolume)
        {
            // Apply barbell sizing (with safety factor)
            if(EnableBarbellSizing && !isFirstPosition)
            {
                lots = ApplyBarbellSizing(lots, positionCount);
            }

            // Calculate TP (rubber band or fixed)
            double tp = CalculateTP(ask, spread, isFirstPosition);

            if(OpenBuyOrder(lots, ask, tp, positionCount))
            {
                // Set first entry price on first position
                if(isFirstPosition)
                {
                    g_firstEntryPrice = ask;
                }
                g_entriesAllowed++;
            }
        }
    }
}

//+------------------------------------------------------------------+
//| Update volatility tracking                                        |
//+------------------------------------------------------------------+
void UpdateVolatility(double bid)
{
    datetime currentTime = TimeCurrent();
    int lookbackSeconds = (int)(VolatilityLookbackHours * 3600);

    if(g_lastVolatilityReset == 0 ||
       currentTime - g_lastVolatilityReset >= lookbackSeconds)
    {
        g_recentHigh = bid;
        g_recentLow = bid;
        g_lastVolatilityReset = currentTime;
    }

    g_recentHigh = MathMax(g_recentHigh, bid);
    g_recentLow = MathMin(g_recentLow, bid);
}

//+------------------------------------------------------------------+
//| Update adaptive spacing based on volatility                       |
//+------------------------------------------------------------------+
void UpdateAdaptiveSpacing(double bid)
{
    double range = g_recentHigh - g_recentLow;

    if(range > 0 && g_recentHigh > 0 && bid > 0)
    {
        // Typical volatility as % of price
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
//| Update velocity tracking                                          |
//+------------------------------------------------------------------+
void UpdateVelocity(double bid)
{
    // Store price in circular buffer
    g_priceWindow[g_priceWindowIndex] = bid;
    g_priceWindowIndex = (g_priceWindowIndex + 1) % VelocityWindow;

    // Calculate velocity from oldest to newest
    int oldestIndex = g_priceWindowIndex;  // Next to be overwritten = oldest
    double oldPrice = g_priceWindow[oldestIndex];

    if(oldPrice > 0)
    {
        g_currentVelocityPct = (bid - oldPrice) / oldPrice * 100.0;
    }
}

//+------------------------------------------------------------------+
//| Check if velocity is near zero (local minima/maxima)              |
//+------------------------------------------------------------------+
bool CheckVelocityZero()
{
    return MathAbs(g_currentVelocityPct) < VelocityThresholdPct;
}

//+------------------------------------------------------------------+
//| Calculate TP using Rubber Band (SQRT) or Fixed mode               |
//+------------------------------------------------------------------+
double CalculateTP(double ask, double spread, bool isFirstPosition)
{
    double tp;

    if(!EnableRubberBandTP || g_firstEntryPrice <= 0 || isFirstPosition)
    {
        // Fixed TP: spread + spacing
        tp = ask + spread + g_currentSpacing;
    }
    else
    {
        // Rubber Band TP: scales with deviation from first entry
        double deviation = MathAbs(g_firstEntryPrice - ask);
        double tpAddition = TPSqrtScale * MathSqrt(deviation);
        double tpDistance = MathMax(TPMinimum, tpAddition);
        tp = ask + spread + tpDistance;
    }

    return NormalizeDouble(tp, g_digits);
}

//+------------------------------------------------------------------+
//| Apply barbell sizing with safety factor                           |
//+------------------------------------------------------------------+
double ApplyBarbellSizing(double baseLots, int positionCount)
{
    if(positionCount < BarbellThresholdPos)
    {
        return baseLots;  // Below threshold - no multiplier
    }

    // Apply multiplier with safety factor
    // Safety factor decreases as position count increases to prevent margin call
    double safetyFactor = 1.0 / (1.0 + positionCount * 0.05);
    double adjustedMult = 1.0 + (BarbellMultiplier - 1.0) * safetyFactor;

    double newLots = baseLots * adjustedMult;

    // Ensure within bounds
    newLots = MathMax(newLots, MinVolume);
    newLots = MathMin(newLots, MaxVolume);

    // Round to lot step
    double lotStep = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_STEP);
    newLots = MathFloor(newLots / lotStep) * lotStep;

    return newLots;
}

//+------------------------------------------------------------------+
//| Calculate base lot size (survive percentage method)               |
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

    // Calculate worst-case price
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

    // Calculate potential equity at target
    double equityAtTarget = equity - volumeOfOpenTrades * distance * g_contractSize;

    // Check margin safety
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

    // Find maximum safe multiplier
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

    // Round to valid lot step
    double lotStep = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_STEP);
    tradeSize = MathFloor(tradeSize / lotStep) * lotStep;

    return MathMin(tradeSize, MaxVolume);
}

//+------------------------------------------------------------------+
//| Open a buy order                                                  |
//+------------------------------------------------------------------+
bool OpenBuyOrder(double lots, double ask, double tp, int positionCount)
{
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
    request.comment = "CombinedJu #" + IntegerToString(positionCount + 1);
    request.type_filling = ORDER_FILLING_IOC;

    if(!OrderSend(request, result))
    {
        Print("OrderSend failed: ", GetLastError(), " RetCode: ", result.retcode);
        return false;
    }

    if(result.retcode == TRADE_RETCODE_DONE || result.retcode == TRADE_RETCODE_PLACED)
    {
        double tpDist = tp - ask;
        string barbellInfo = "";
        if(EnableBarbellSizing && positionCount >= BarbellThresholdPos)
        {
            barbellInfo = " [BARBELL]";
        }

        Print("BUY ", DoubleToString(lots, 2), " @ ", DoubleToString(ask, g_digits),
              " TP: ", DoubleToString(tp, g_digits), " ($", DoubleToString(tpDist, 2), ")",
              " Vel: ", DoubleToString(g_currentVelocityPct, 4), "%",
              barbellInfo);
        return true;
    }

    return false;
}
//+------------------------------------------------------------------+
