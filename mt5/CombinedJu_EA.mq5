//+------------------------------------------------------------------+
//|                                                 CombinedJu_EA.mq5 |
//|                                    Combined Ju Strategy EA        |
//|                                                                    |
//| Combines three concepts:                                           |
//| 1. Rubber Band TP (SQRT/LINEAR mode)                              |
//| 2. Velocity Zero Filter (Wu Wei - enter at local minima)          |
//| 3. Barbell/Threshold Sizing (larger lots at deep deviations)      |
//|                                                                    |
//| CRITICAL: Uses THRESHOLD sizing, NOT LINEAR (LINEAR destroys!)    |
//+------------------------------------------------------------------+
#property copyright "CombinedJu Strategy"
#property version   "1.00"
#property strict

#include <Trade\Trade.mqh>

//--- Input Parameters
input group "=== Core Grid Parameters ==="
input double SurvivePct = 12.0;              // Survive % (12% optimal for 2025)
input double BaseSpacing = 1.0;              // Base grid spacing in $ (1.0 optimal)
input double MinVolume = 0.01;               // Minimum lot size
input double MaxVolume = 10.0;               // Maximum lot size

input group "=== Volatility Adaptation ==="
input double VolatilityLookbackHours = 4.0;  // Lookback period for volatility (hours)
input double TypicalVolPct = 0.55;           // Typical volatility as % of price
input double MinSpacingMult = 0.5;           // Minimum spacing multiplier
input double MaxSpacingMult = 3.0;           // Maximum spacing multiplier
input double MinSpacingAbs = 0.5;            // Minimum absolute spacing ($)
input double MaxSpacingAbs = 5.0;            // Maximum absolute spacing ($)
input double SpacingChangeThreshold = 0.1;   // Min change to update spacing ($)

input group "=== Take Profit Mode ==="
input int TPMode = 1;                        // TP Mode: 0=FIXED, 1=SQRT, 2=LINEAR
input double TPSqrtScale = 0.5;              // SQRT mode: TP scale factor
input double TPLinearScale = 0.3;            // LINEAR mode: TP scale factor
input double TPMin = 1.0;                    // Minimum TP distance ($)

input group "=== Velocity Filter (Wu Wei) ==="
input bool EnableVelocityFilter = false;     // Enable velocity filter
input int VelocityWindow = 10;               // Velocity calculation window (ticks)
input double VelocityThresholdPct = 0.01;    // Max velocity % to allow entry

input group "=== Position Sizing ==="
input int SizingMode = 2;                    // 0=UNIFORM, 1=LINEAR(DANGER!), 2=THRESHOLD
input double SizingLinearScale = 0.5;        // LINEAR mode: scale per position
input int SizingThresholdPos = 5;            // THRESHOLD mode: position count threshold
input double SizingThresholdMult = 2.0;      // THRESHOLD mode: lot multiplier after threshold

input group "=== Safety Settings ==="
input bool ForceMinVolumeEntry = false;      // Force entry at MinVolume when lot sizing = 0
                                              // WARNING: force=true can cause stop-out during crashes!

input group "=== Trading Settings ==="
input int MagicNumber = 20260127;            // Magic number for this EA
input string TradeComment = "CombinedJu";    // Trade comment
input int Slippage = 30;                     // Maximum slippage (points)

//--- Global Variables
CTrade trade;
double currentSpacing;
double firstEntryPrice;
double lowestBuy;
double highestBuy;
double volumeOfOpenTrades;
int positionCount;

// Volatility tracking
double recentHigh;
double recentLow;
datetime lastVolResetTime;

// Velocity tracking
double priceWindow[];
int priceWindowIndex;
double currentVelocityPct;

// Statistics
long velocityBlocks;
long entriesAllowed;
long lotZeroForced;

//+------------------------------------------------------------------+
//| Expert initialization function                                     |
//+------------------------------------------------------------------+
int OnInit()
{
    trade.SetExpertMagicNumber(MagicNumber);
    trade.SetDeviationInPoints(Slippage);
    trade.SetTypeFilling(ORDER_FILLING_IOC);

    currentSpacing = BaseSpacing;
    firstEntryPrice = 0;
    lowestBuy = DBL_MAX;
    highestBuy = DBL_MIN;
    volumeOfOpenTrades = 0;
    positionCount = 0;

    recentHigh = 0;
    recentLow = DBL_MAX;
    lastVolResetTime = 0;

    ArrayResize(priceWindow, VelocityWindow);
    ArrayInitialize(priceWindow, 0);
    priceWindowIndex = 0;
    currentVelocityPct = 0;

    velocityBlocks = 0;
    entriesAllowed = 0;
    lotZeroForced = 0;

    // Validate sizing mode
    if(SizingMode == 1)
    {
        Print("WARNING: LINEAR sizing mode can destroy the strategy! Consider THRESHOLD (2) instead.");
    }

    Print("CombinedJu EA initialized");
    Print("Survive: ", SurvivePct, "%, Spacing: $", BaseSpacing);
    Print("TP Mode: ", TPMode == 0 ? "FIXED" : (TPMode == 1 ? "SQRT" : "LINEAR"));
    Print("Sizing Mode: ", SizingMode == 0 ? "UNIFORM" : (SizingMode == 1 ? "LINEAR" : "THRESHOLD"));
    Print("Velocity Filter: ", EnableVelocityFilter ? "ON" : "OFF");

    return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+
//| Expert deinitialization function                                   |
//+------------------------------------------------------------------+
void OnDeinit(const int reason)
{
    Print("CombinedJu EA stopped");
    Print("Velocity blocks: ", velocityBlocks);
    Print("Entries allowed: ", entriesAllowed);
    Print("Lot zero forced: ", lotZeroForced);
}

//+------------------------------------------------------------------+
//| Expert tick function                                               |
//+------------------------------------------------------------------+
void OnTick()
{
    double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
    double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    double spread = ask - bid;

    // Update velocity tracking
    UpdateVelocity(bid);

    // Update volatility and adaptive spacing
    UpdateVolatility(bid);
    UpdateAdaptiveSpacing(bid);

    // Iterate through existing positions
    IteratePositions();

    // Open new positions
    OpenNew(ask, spread);
}

//+------------------------------------------------------------------+
//| Update velocity calculation                                        |
//+------------------------------------------------------------------+
void UpdateVelocity(double bid)
{
    // Shift window
    priceWindow[priceWindowIndex] = bid;
    priceWindowIndex = (priceWindowIndex + 1) % VelocityWindow;

    // Calculate velocity (oldest price is at current index after shift)
    double oldPrice = priceWindow[priceWindowIndex];
    if(oldPrice > 0)
    {
        currentVelocityPct = (bid - oldPrice) / oldPrice * 100.0;
    }
}

//+------------------------------------------------------------------+
//| Update volatility tracking                                         |
//+------------------------------------------------------------------+
void UpdateVolatility(double bid)
{
    datetime currentTime = TimeCurrent();
    long lookbackSeconds = (long)(VolatilityLookbackHours * 3600);

    if(lastVolResetTime == 0 || currentTime - lastVolResetTime >= lookbackSeconds)
    {
        recentHigh = bid;
        recentLow = bid;
        lastVolResetTime = currentTime;
    }

    if(bid > recentHigh) recentHigh = bid;
    if(bid < recentLow) recentLow = bid;
}

//+------------------------------------------------------------------+
//| Update adaptive spacing based on volatility                        |
//+------------------------------------------------------------------+
void UpdateAdaptiveSpacing(double bid)
{
    double range = recentHigh - recentLow;

    if(range > 0 && recentHigh > 0 && bid > 0)
    {
        double typicalVol = bid * (TypicalVolPct / 100.0);
        double volRatio = range / typicalVol;
        volRatio = MathMax(MinSpacingMult, MathMin(MaxSpacingMult, volRatio));

        double newSpacing = BaseSpacing * volRatio;
        newSpacing = MathMax(MinSpacingAbs, MathMin(MaxSpacingAbs, newSpacing));

        if(MathAbs(newSpacing - currentSpacing) > SpacingChangeThreshold)
        {
            currentSpacing = newSpacing;
        }
    }
}

//+------------------------------------------------------------------+
//| Check velocity filter (Wu Wei - enter at quiet moments)            |
//+------------------------------------------------------------------+
bool CheckVelocityZero()
{
    if(!EnableVelocityFilter) return true;
    return MathAbs(currentVelocityPct) < VelocityThresholdPct;
}

//+------------------------------------------------------------------+
//| Calculate take profit based on mode                                |
//+------------------------------------------------------------------+
double CalculateTP(double ask, double spread)
{
    if(firstEntryPrice <= 0)
    {
        return ask + spread + currentSpacing;
    }

    double deviation = MathAbs(firstEntryPrice - ask);
    double tpAddition;

    switch(TPMode)
    {
        case 0: // FIXED
            return ask + spread + currentSpacing;

        case 1: // SQRT
            tpAddition = TPSqrtScale * MathSqrt(deviation);
            return ask + spread + MathMax(TPMin, tpAddition);

        case 2: // LINEAR
            tpAddition = TPLinearScale * deviation;
            return ask + spread + MathMax(TPMin, tpAddition);

        default:
            return ask + spread + currentSpacing;
    }
}

//+------------------------------------------------------------------+
//| Calculate lot multiplier based on sizing mode                      |
//+------------------------------------------------------------------+
double CalculateLotMultiplier()
{
    switch(SizingMode)
    {
        case 0: // UNIFORM
            return 1.0;

        case 1: // LINEAR (DANGEROUS!)
            return 1.0 + positionCount * SizingLinearScale;

        case 2: // THRESHOLD
            if(positionCount >= SizingThresholdPos)
                return SizingThresholdMult;
            return 1.0;

        default:
            return 1.0;
    }
}

//+------------------------------------------------------------------+
//| Iterate through open positions                                     |
//+------------------------------------------------------------------+
void IteratePositions()
{
    lowestBuy = DBL_MAX;
    highestBuy = DBL_MIN;
    volumeOfOpenTrades = 0;
    positionCount = 0;

    for(int i = PositionsTotal() - 1; i >= 0; i--)
    {
        ulong ticket = PositionGetTicket(i);
        if(ticket == 0) continue;

        if(PositionGetString(POSITION_SYMBOL) != _Symbol) continue;
        if(PositionGetInteger(POSITION_MAGIC) != MagicNumber) continue;

        if(PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY)
        {
            double openPrice = PositionGetDouble(POSITION_PRICE_OPEN);
            double lots = PositionGetDouble(POSITION_VOLUME);

            volumeOfOpenTrades += lots;
            if(openPrice < lowestBuy) lowestBuy = openPrice;
            if(openPrice > highestBuy) highestBuy = openPrice;
            positionCount++;
        }
    }

    // Reset first entry price when all positions closed
    if(positionCount == 0)
    {
        firstEntryPrice = 0;
    }
}

//+------------------------------------------------------------------+
//| Calculate lot size based on margin safety                          |
//+------------------------------------------------------------------+
double CalculateLotSize(double ask)
{
    double equity = AccountInfoDouble(ACCOUNT_EQUITY);
    double usedMargin = AccountInfoDouble(ACCOUNT_MARGIN);
    double contractSize = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);
    double leverage = (double)AccountInfoInteger(ACCOUNT_LEVERAGE);

    double marginStopOut = 20.0; // 20% margin level

    double endPrice;
    if(positionCount == 0)
        endPrice = ask * ((100.0 - SurvivePct) / 100.0);
    else
        endPrice = highestBuy * ((100.0 - SurvivePct) / 100.0);

    double distance = ask - endPrice;
    double numberOfTrades = MathFloor(distance / currentSpacing);
    if(numberOfTrades <= 0) numberOfTrades = 1;

    double equityAtTarget = equity - volumeOfOpenTrades * distance * contractSize;
    if(usedMargin > 0 && (equityAtTarget / usedMargin * 100.0) < marginStopOut)
    {
        return 0.0; // Will be forced to MinVolume
    }

    double tradeSize = MinVolume;
    double dEquity = contractSize * tradeSize * currentSpacing * (numberOfTrades * (numberOfTrades + 1) / 2);
    double dMargin = numberOfTrades * tradeSize * contractSize / leverage;

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

    return MathMin(tradeSize, MaxVolume);
}

//+------------------------------------------------------------------+
//| Normalize lot size to broker requirements                          |
//+------------------------------------------------------------------+
double NormalizeLots(double lots)
{
    double minLot = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN);
    double maxLot = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MAX);
    double lotStep = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_STEP);

    lots = MathMax(minLot, lots);
    lots = MathMin(maxLot, lots);
    lots = MathRound(lots / lotStep) * lotStep;

    return lots;
}

//+------------------------------------------------------------------+
//| Open a new position                                                |
//+------------------------------------------------------------------+
bool OpenPosition(double lots, double tp)
{
    bool wasForced = false;

    // Handle when lot sizing returns 0 (margin protection triggered)
    if(lots < MinVolume)
    {
        lotZeroForced++;

        if(!ForceMinVolumeEntry)
        {
            // Respect lot sizing safety - skip this entry
            return false;
        }

        // Force entry at MinVolume (WARNING: can cause crash stop-out!)
        lots = MinVolume;
        wasForced = true;
    }

    lots = NormalizeLots(lots);

    double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);

    // Normalize TP to tick size
    double tickSize = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_TICK_SIZE);
    tp = MathRound(tp / tickSize) * tickSize;

    string comment = TradeComment;
    if(wasForced) comment += "_F";

    if(trade.Buy(lots, _Symbol, ask, 0, tp, comment))
    {
        entriesAllowed++;
        return true;
    }
    else
    {
        Print("Order failed: ", GetLastError());
        return false;
    }
}

//+------------------------------------------------------------------+
//| Open new positions based on grid logic                             |
//+------------------------------------------------------------------+
void OpenNew(double ask, double spread)
{
    if(positionCount == 0)
    {
        // First position - set equilibrium, no velocity filter
        double lots = CalculateLotSize(ask);
        double tp = ask + spread + currentSpacing;

        if(OpenPosition(lots, tp))
        {
            firstEntryPrice = ask;
            highestBuy = ask;
            lowestBuy = ask;
        }
    }
    else
    {
        // Additional positions - check spacing condition
        if(lowestBuy >= ask + currentSpacing)
        {
            // Check velocity filter (Wu Wei)
            if(!CheckVelocityZero())
            {
                velocityBlocks++;
                return;
            }

            // Calculate base lot size
            double lots = CalculateLotSize(ask);

            // Apply sizing multiplier with safety factor
            double lotMult = CalculateLotMultiplier();
            if(lotMult > 1.0)
            {
                // Scale down multiplier based on position count for safety
                double safetyFactor = 1.0 / (1.0 + positionCount * 0.05);
                lotMult = 1.0 + (lotMult - 1.0) * safetyFactor;
            }
            lots *= lotMult;

            // Ensure within bounds
            lots = MathMax(lots, MinVolume);
            lots = MathMin(lots, MaxVolume);

            // Calculate rubber band TP
            double tp = CalculateTP(ask, spread);

            if(OpenPosition(lots, tp))
            {
                lowestBuy = ask;
            }
        }
    }
}
//+------------------------------------------------------------------+
