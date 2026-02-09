//+------------------------------------------------------------------+
//|                                      AdaptiveRegimeGrid_EA.mq5   |
//|                                          Backtest Framework       |
//|                                                                    |
//| Adaptive Regime Grid - MT5 Expert Advisor                         |
//| Combines: Regime Detection, Circuit Breaker, Smart TP,            |
//|           Multi-Window Velocity Filter, Asymmetric Spacing        |
//|                                                                    |
//| C++ Backtest: 26.9x return, 51% DD (XAUUSD 2025)                 |
//+------------------------------------------------------------------+
#property copyright "Backtest Framework"
#property link      ""
#property version   "1.00"
#property strict

#include <Trade\Trade.mqh>

CTrade trade;

//--- Input Parameters ---

input group "=== Core Grid Parameters ==="
input double SurvivePct = 13.0;                // Survive % for margin sizing
input double BaseSpacing = 1.50;               // Base grid spacing ($)
input double MinVolume = 0.01;                 // Minimum lot size
input double MaxVolume = 100.0;                // Maximum lot size

input group "=== Adaptive Spacing ==="
input double VolatilityLookbackHours = 4.0;    // Volatility lookback (hours)
input double TypicalVolPct = 0.55;             // Typical volatility (% of price)
input double MinSpacingMult = 0.5;             // Min spacing multiplier
input double MaxSpacingMult = 3.0;             // Max spacing multiplier
input double MinSpacingAbs = 0.50;             // Min spacing ($)
input double MaxSpacingAbs = 5.00;             // Max spacing ($)
input double SpacingChangeThreshold = 0.10;    // Change threshold ($)

input group "=== Regime Detection (Efficiency Ratio) ==="
input int    RegimeLookbackTicks = 500;        // Tick lookback for ER calc
input double ER_TrendingThreshold = 0.25;      // ER > this = trending
input double ER_RangingThreshold = 0.10;       // ER < this = ranging

input group "=== Drawdown Circuit Breaker ==="
input double DD_ReduceThreshold = 40.0;        // DD% to reduce sizing by 50%
input double DD_HaltThreshold = 55.0;          // DD% to halt new entries
input double DD_LiquidateThreshold = 65.0;     // DD% to liquidate worst 25%

input group "=== TP Management ==="
input double TPLinearScale = 0.3;              // LINEAR TP scale factor
input double TPMin = 1.50;                     // Minimum TP distance ($)
input double TrailingDistance = 2.0;           // Trailing stop distance ($)
input double TrailingActivation = 1.5;         // Profit to activate trailing ($)
input bool   UseTrailingInTrends = true;       // Use trailing TP in trending regime

input group "=== Multi-Window Velocity Filter ==="
input bool   EnableVelocityFilter = true;      // Enable velocity filter
input int    VelocityFastWindow = 5;           // Fast window (ticks)
input int    VelocityMidWindow = 20;           // Medium window (ticks)
input int    VelocitySlowWindow = 100;         // Slow window (ticks)
input double VelocityThresholdPct = 0.02;      // Max velocity % per window

input group "=== Asymmetric Spacing ==="
input bool   EnableAsymmetric = true;          // Enable directional bias
input double TrendSpacingTighten = 0.7;        // Multiply spacing in uptrend
input double TrendSpacingWiden = 1.5;          // Multiply spacing in downtrend

input group "=== Safety ==="
input bool   ForceMinVolumeEntry = false;      // Force entry at MinVolume when margin tight
input int    Slippage = 10;                    // Order slippage (points)

input group "=== Display & Control ==="
input bool   ShowDashboard = true;             // Show on-chart dashboard
input bool   EnableTrading = true;             // Master trading switch
input int    MagicNumber = 202601;             // EA magic number
input string TradeComment = "ARG_v1";         // Trade comment prefix

//--- Regime enum ---
enum ENUM_REGIME
{
    REGIME_RANGING = 0,
    REGIME_TRENDING_UP = 1,
    REGIME_TRENDING_DOWN = 2
};

//--- Global Variables ---

// Symbol info (cached at init)
double g_contractSize;
double g_leverage;
int    g_digits;
double g_point;
double g_tickSize;
double g_lotStep;
double g_minLot;
double g_maxLot;

// Position cache
int    g_posCount;
double g_lowestBuy;
double g_highestBuy;
double g_totalVolume;
double g_usedMargin;

// Strategy state
double g_currentSpacing;
double g_firstEntryPrice;
double g_peakEquity;
ENUM_REGIME g_currentRegime;
double g_currentER;
bool   g_liquidationTriggered;

// Volatility tracking
double g_recentHigh;
double g_recentLow;
datetime g_lastVolResetTime;

// Regime detection (circular buffer for efficiency ratio)
double g_regimePrices[];
int    g_regimePriceIdx;
int    g_regimePriceCount;

// Multi-window velocity (circular buffers)
double g_velFast[];
int    g_velFastIdx;
double g_velMid[];
int    g_velMidIdx;
double g_velSlow[];
int    g_velSlowIdx;
int    g_velTickCount;  // Total ticks received (for window fill detection)

// Statistics
long   g_velocityBlocks;
long   g_entriesAllowed;
long   g_lotZeroBlocks;
long   g_regimeChanges;
long   g_trendingEntries;
long   g_rangingEntries;
long   g_trailingTPEntries;
long   g_fixedTPEntries;
long   g_ddLiquidations;
long   g_positionsLiquidated;
int    g_maxPosCount;
double g_peakDDPct;

// Dashboard throttle
datetime g_lastDashboardUpdate;

//+------------------------------------------------------------------+
//| Expert initialization                                             |
//+------------------------------------------------------------------+
int OnInit()
{
    // Configure CTrade
    trade.SetExpertMagicNumber(MagicNumber);
    trade.SetDeviationInPoints(Slippage);
    trade.SetTypeFilling(ORDER_FILLING_IOC);

    // Cache symbol info
    g_contractSize = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);
    g_leverage = (double)AccountInfoInteger(ACCOUNT_LEVERAGE);
    g_digits = (int)SymbolInfoInteger(_Symbol, SYMBOL_DIGITS);
    g_point = SymbolInfoDouble(_Symbol, SYMBOL_POINT);
    g_tickSize = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_TICK_SIZE);
    g_lotStep = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_STEP);
    g_minLot = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN);
    g_maxLot = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MAX);

    // Initialize strategy state
    g_currentSpacing = BaseSpacing;
    g_firstEntryPrice = 0;
    g_peakEquity = AccountInfoDouble(ACCOUNT_EQUITY);
    g_currentRegime = REGIME_RANGING;
    g_currentER = 0;
    g_liquidationTriggered = false;

    // Position cache
    g_posCount = 0;
    g_lowestBuy = DBL_MAX;
    g_highestBuy = -DBL_MAX;
    g_totalVolume = 0;
    g_usedMargin = 0;

    // Volatility
    g_recentHigh = 0;
    g_recentLow = DBL_MAX;
    g_lastVolResetTime = 0;

    // Regime detection buffer
    ArrayResize(g_regimePrices, RegimeLookbackTicks);
    ArrayInitialize(g_regimePrices, 0);
    g_regimePriceIdx = 0;
    g_regimePriceCount = 0;

    // Velocity buffers
    ArrayResize(g_velFast, VelocityFastWindow);
    ArrayInitialize(g_velFast, 0);
    g_velFastIdx = 0;
    ArrayResize(g_velMid, VelocityMidWindow);
    ArrayInitialize(g_velMid, 0);
    g_velMidIdx = 0;
    ArrayResize(g_velSlow, VelocitySlowWindow);
    ArrayInitialize(g_velSlow, 0);
    g_velSlowIdx = 0;
    g_velTickCount = 0;

    // Statistics
    g_velocityBlocks = 0;
    g_entriesAllowed = 0;
    g_lotZeroBlocks = 0;
    g_regimeChanges = 0;
    g_trendingEntries = 0;
    g_rangingEntries = 0;
    g_trailingTPEntries = 0;
    g_fixedTPEntries = 0;
    g_ddLiquidations = 0;
    g_positionsLiquidated = 0;
    g_maxPosCount = 0;
    g_peakDDPct = 0;
    g_lastDashboardUpdate = 0;

    // Reconstruct volatility from bars on startup
    ReconstructVolatilityFromBars();

    // Dashboard
    if(ShowDashboard)
        CreateDashboard();

    // Print settings
    Print("=== AdaptiveRegimeGrid EA v1.0 ===");
    Print("Survive: ", SurvivePct, "%, Spacing: $", BaseSpacing);
    Print("Regime: ER trending>", ER_TrendingThreshold, " ranging<", ER_RangingThreshold);
    Print("Circuit Breaker: reduce@", DD_ReduceThreshold, "% halt@", DD_HaltThreshold, "% liquidate@", DD_LiquidateThreshold, "%");
    Print("Velocity Filter: ", EnableVelocityFilter ? "ON" : "OFF", " threshold: ", VelocityThresholdPct, "%");
    Print("Asymmetric: ", EnableAsymmetric ? "ON" : "OFF", " tighten: ", TrendSpacingTighten, " widen: ", TrendSpacingWiden);
    Print("ForceMinVolume: ", ForceMinVolumeEntry ? "ON (RISKY)" : "OFF (safe)");

    return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+
//| Expert deinitialization                                           |
//+------------------------------------------------------------------+
void OnDeinit(const int reason)
{
    if(ShowDashboard)
        ObjectsDeleteAll(0, "ARG_");

    Print("=== AdaptiveRegimeGrid EA Stats ===");
    Print("Entries: ", g_entriesAllowed, " VelBlocks: ", g_velocityBlocks);
    Print("Regime Changes: ", g_regimeChanges, " Trending: ", g_trendingEntries, " Ranging: ", g_rangingEntries);
    Print("Trailing TP: ", g_trailingTPEntries, " Fixed TP: ", g_fixedTPEntries);
    Print("DD Liquidations: ", g_ddLiquidations, " Pos Liquidated: ", g_positionsLiquidated);
    Print("Peak DD: ", DoubleToString(g_peakDDPct, 2), "% Max Positions: ", g_maxPosCount);
}

//+------------------------------------------------------------------+
//| Expert tick function                                              |
//+------------------------------------------------------------------+
void OnTick()
{
    double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
    double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    double equity = AccountInfoDouble(ACCOUNT_EQUITY);
    double spread = ask - bid;
    datetime tickTime = TimeCurrent();

    if(bid <= 0 || ask <= 0) return;

    // 1. Update peak equity and DD
    if(equity > g_peakEquity)
        g_peakEquity = equity;

    double ddPct = 0;
    if(g_peakEquity > 0)
        ddPct = (g_peakEquity - equity) / g_peakEquity * 100.0;
    if(ddPct > g_peakDDPct)
        g_peakDDPct = ddPct;

    // 2. Scan positions
    ScanPositions();

    // 3. Circuit breaker: liquidate worst positions at extreme DD
    if(ddPct >= DD_LiquidateThreshold && !g_liquidationTriggered)
    {
        LiquidateWorstPositions(0.25);
        g_liquidationTriggered = true;
    }
    if(ddPct < DD_HaltThreshold)
        g_liquidationTriggered = false;

    // 4. Update models
    UpdateVolatility(bid, tickTime);
    UpdateAdaptiveSpacing(bid);
    UpdateRegime(bid);
    UpdateVelocity(bid);

    // 5. Dashboard (throttled to 1/sec)
    if(ShowDashboard && tickTime != g_lastDashboardUpdate)
    {
        UpdateDashboard(bid, ask, equity, ddPct);
        g_lastDashboardUpdate = tickTime;
    }

    // 6. Trading gate
    if(!EnableTrading) return;

    // 7. Circuit breaker: halt new entries at high DD
    if(ddPct >= DD_HaltThreshold) return;

    // 8. Calculate effective spacing with asymmetric bias
    double effectiveSpacing = g_currentSpacing;
    if(EnableAsymmetric)
    {
        if(g_currentRegime == REGIME_TRENDING_UP)
            effectiveSpacing *= TrendSpacingTighten;
        else if(g_currentRegime == REGIME_TRENDING_DOWN)
            effectiveSpacing *= TrendSpacingWiden;
    }

    // 9. Trading logic
    OpenNew(ask, spread, effectiveSpacing, ddPct);
}

//+------------------------------------------------------------------+
//| Position scanning                                                 |
//+------------------------------------------------------------------+
void ScanPositions()
{
    g_posCount = 0;
    g_lowestBuy = DBL_MAX;
    g_highestBuy = -DBL_MAX;
    g_totalVolume = 0;
    g_usedMargin = 0;

    for(int i = PositionsTotal() - 1; i >= 0; i--)
    {
        ulong ticket = PositionGetTicket(i);
        if(ticket == 0) continue;
        if(PositionGetString(POSITION_SYMBOL) != _Symbol) continue;
        if(PositionGetInteger(POSITION_MAGIC) != MagicNumber) continue;

        if(PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY)
        {
            double entryPrice = PositionGetDouble(POSITION_PRICE_OPEN);
            double lots = PositionGetDouble(POSITION_VOLUME);

            g_lowestBuy = MathMin(g_lowestBuy, entryPrice);
            g_highestBuy = MathMax(g_highestBuy, entryPrice);
            g_totalVolume += lots;
            g_usedMargin += lots * g_contractSize * entryPrice / g_leverage;
            g_posCount++;
        }
    }

    // Reset equilibrium when all positions closed
    if(g_posCount == 0)
        g_firstEntryPrice = 0;

    if(g_posCount > g_maxPosCount)
        g_maxPosCount = g_posCount;
}

//+------------------------------------------------------------------+
//| Volatility tracking                                               |
//+------------------------------------------------------------------+
void UpdateVolatility(double bid, datetime tickTime)
{
    int lookbackSec = (int)(VolatilityLookbackHours * 3600);

    if(g_lastVolResetTime == 0 || tickTime - g_lastVolResetTime >= lookbackSec)
    {
        g_recentHigh = bid;
        g_recentLow = bid;
        g_lastVolResetTime = tickTime;
    }
    g_recentHigh = MathMax(g_recentHigh, bid);
    g_recentLow = MathMin(g_recentLow, bid);
}

//+------------------------------------------------------------------+
//| Reconstruct volatility from M1 bars on startup                    |
//+------------------------------------------------------------------+
void ReconstructVolatilityFromBars()
{
    double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
    if(bid <= 0) return;

    int barsNeeded = (int)(VolatilityLookbackHours * 60);
    barsNeeded = MathMax(1, MathMin(1440, barsNeeded));

    MqlRates rates[];
    int copied = CopyRates(_Symbol, PERIOD_M1, 0, barsNeeded, rates);

    if(copied > 0)
    {
        double high = 0;
        double low = DBL_MAX;

        for(int i = 0; i < copied; i++)
        {
            high = MathMax(high, rates[i].high);
            low = MathMin(low, rates[i].low);
        }

        g_recentHigh = high;
        g_recentLow = low;
        g_lastVolResetTime = TimeCurrent();

        // Apply adaptive spacing from reconstructed data
        double range = high - low;
        if(range > 0)
        {
            double typicalVol = bid * (TypicalVolPct / 100.0);
            double volRatio = range / typicalVol;
            volRatio = MathMax(MinSpacingMult, MathMin(MaxSpacingMult, volRatio));
            double newSpacing = BaseSpacing * volRatio;
            g_currentSpacing = MathMax(MinSpacingAbs, MathMin(MaxSpacingAbs, newSpacing));
        }

        Print("Volatility reconstructed from ", copied, " bars. Range: $",
              DoubleToString(range, 2), " Spacing: $", DoubleToString(g_currentSpacing, 2));
    }
}

//+------------------------------------------------------------------+
//| Adaptive spacing                                                  |
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
        newSpacing = MathMax(MinSpacingAbs, MathMin(MaxSpacingAbs, newSpacing));

        if(MathAbs(newSpacing - g_currentSpacing) > SpacingChangeThreshold)
            g_currentSpacing = newSpacing;
    }
}

//+------------------------------------------------------------------+
//| Regime detection via Efficiency Ratio                             |
//+------------------------------------------------------------------+
void UpdateRegime(double bid)
{
    // Push into circular buffer
    g_regimePrices[g_regimePriceIdx] = bid;
    g_regimePriceIdx = (g_regimePriceIdx + 1) % RegimeLookbackTicks;
    if(g_regimePriceCount < RegimeLookbackTicks)
        g_regimePriceCount++;

    if(g_regimePriceCount < RegimeLookbackTicks)
        return; // Not enough data

    // Calculate ER: |net change| / sum(|individual changes|)
    // Oldest price is at g_regimePriceIdx (circular), newest at idx-1
    int oldestIdx = g_regimePriceIdx;  // Points to oldest (just overwritten was newest)
    int newestIdx = (g_regimePriceIdx - 1 + RegimeLookbackTicks) % RegimeLookbackTicks;

    double netChange = g_regimePrices[newestIdx] - g_regimePrices[oldestIdx];
    double sumAbsChanges = 0;

    for(int i = 0; i < RegimeLookbackTicks - 1; i++)
    {
        int idx1 = (oldestIdx + i) % RegimeLookbackTicks;
        int idx2 = (oldestIdx + i + 1) % RegimeLookbackTicks;
        sumAbsChanges += MathAbs(g_regimePrices[idx2] - g_regimePrices[idx1]);
    }

    if(sumAbsChanges < 1e-10)
    {
        g_currentER = 0;
        return;
    }

    g_currentER = MathAbs(netChange) / sumAbsChanges;

    // Determine regime with hysteresis
    ENUM_REGIME newRegime = g_currentRegime;
    if(g_currentER > ER_TrendingThreshold)
        newRegime = (netChange > 0) ? REGIME_TRENDING_UP : REGIME_TRENDING_DOWN;
    else if(g_currentER < ER_RangingThreshold)
        newRegime = REGIME_RANGING;

    if(newRegime != g_currentRegime)
    {
        g_currentRegime = newRegime;
        g_regimeChanges++;
    }
}

//+------------------------------------------------------------------+
//| Multi-window velocity filter                                      |
//+------------------------------------------------------------------+
void UpdateVelocity(double bid)
{
    g_velFast[g_velFastIdx] = bid;
    g_velFastIdx = (g_velFastIdx + 1) % VelocityFastWindow;
    g_velMid[g_velMidIdx] = bid;
    g_velMidIdx = (g_velMidIdx + 1) % VelocityMidWindow;
    g_velSlow[g_velSlowIdx] = bid;
    g_velSlowIdx = (g_velSlowIdx + 1) % VelocitySlowWindow;
    g_velTickCount++;
}

double GetVelocity(const double &buf[], int bufSize, int idx)
{
    if(g_velTickCount < bufSize) return 0;
    double oldest = buf[idx];  // idx points to oldest (just overwritten)
    int newestIdx = (idx - 1 + bufSize) % bufSize;
    double newest = buf[newestIdx];
    if(oldest <= 0) return 0;
    return (newest - oldest) / oldest * 100.0;
}

bool CheckVelocityFilter()
{
    if(!EnableVelocityFilter) return true;

    double vFast = GetVelocity(g_velFast, VelocityFastWindow, g_velFastIdx);
    double vMid = GetVelocity(g_velMid, VelocityMidWindow, g_velMidIdx);
    double vSlow = GetVelocity(g_velSlow, VelocitySlowWindow, g_velSlowIdx);

    return MathAbs(vFast) < VelocityThresholdPct &&
           MathAbs(vMid) < VelocityThresholdPct &&
           MathAbs(vSlow) < VelocityThresholdPct;
}

//+------------------------------------------------------------------+
//| Circuit breaker: liquidate worst 25% of positions                 |
//+------------------------------------------------------------------+
void LiquidateWorstPositions(double fraction)
{
    // Collect positions with unrealized P/L
    int total = PositionsTotal();
    if(total == 0) return;

    double plArr[];
    ulong ticketArr[];
    int count = 0;

    ArrayResize(plArr, total);
    ArrayResize(ticketArr, total);

    double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);

    for(int i = total - 1; i >= 0; i--)
    {
        ulong ticket = PositionGetTicket(i);
        if(ticket == 0) continue;
        if(PositionGetString(POSITION_SYMBOL) != _Symbol) continue;
        if(PositionGetInteger(POSITION_MAGIC) != MagicNumber) continue;

        double entryPrice = PositionGetDouble(POSITION_PRICE_OPEN);
        double lots = PositionGetDouble(POSITION_VOLUME);
        double pl = (bid - entryPrice) * lots * g_contractSize;

        plArr[count] = pl;
        ticketArr[count] = ticket;
        count++;
    }

    if(count == 0) return;

    // Bubble sort by P/L ascending (worst first) - small array, sort is fine
    for(int i = 0; i < count - 1; i++)
    {
        for(int j = 0; j < count - i - 1; j++)
        {
            if(plArr[j] > plArr[j + 1])
            {
                double tmpPL = plArr[j];
                plArr[j] = plArr[j + 1];
                plArr[j + 1] = tmpPL;

                ulong tmpTicket = ticketArr[j];
                ticketArr[j] = ticketArr[j + 1];
                ticketArr[j + 1] = tmpTicket;
            }
        }
    }

    // Close worst fraction
    int toClose = MathMax(1, (int)(count * fraction));
    for(int i = 0; i < toClose && i < count; i++)
    {
        if(PositionSelectByTicket(ticketArr[i]))
        {
            trade.PositionClose(ticketArr[i]);
            g_positionsLiquidated++;
        }
    }
    g_ddLiquidations++;

    Print("CIRCUIT BREAKER: Liquidated ", toClose, " of ", count, " positions");
}

//+------------------------------------------------------------------+
//| LINEAR TP calculation (from CombinedJu)                           |
//+------------------------------------------------------------------+
double CalculateTP(double ask, double spread)
{
    if(g_firstEntryPrice <= 0)
        return ask + spread + g_currentSpacing;

    double deviation = MathAbs(g_firstEntryPrice - ask);
    double tpAddition = TPLinearScale * deviation;
    double tp = ask + spread + MathMax(TPMin, tpAddition);

    return NormalizeDouble(tp, g_digits);
}

//+------------------------------------------------------------------+
//| Lot sizing with margin safety (binary search)                     |
//+------------------------------------------------------------------+
double CalculateLotSize(double ask, double ddPct)
{
    double equity = AccountInfoDouble(ACCOUNT_EQUITY);
    double marginStopOut = 20.0;

    double endPrice = (g_posCount == 0)
        ? ask * ((100.0 - SurvivePct) / 100.0)
        : g_highestBuy * ((100.0 - SurvivePct) / 100.0);

    double distance = ask - endPrice;
    double numberOfTrades = MathFloor(distance / g_currentSpacing);
    if(numberOfTrades <= 0) numberOfTrades = 1;

    double equityAtTarget = equity - g_totalVolume * distance * g_contractSize;
    if(g_usedMargin > 0 && (equityAtTarget / g_usedMargin * 100.0) < marginStopOut)
        return 0;

    double tradeSize = MinVolume;
    double dEquity = g_contractSize * tradeSize * g_currentSpacing *
                     (numberOfTrades * (numberOfTrades + 1) / 2);
    double dMargin = numberOfTrades * tradeSize * g_contractSize / g_leverage;

    // Binary search for largest valid multiplier
    double maxMult = MaxVolume / MinVolume;
    double lo = 1.0, hi = maxMult;
    double bestMult = 1.0;

    while(hi - lo > 0.05)
    {
        double mid = (lo + hi) / 2.0;
        double testEquity = equityAtTarget - mid * dEquity;
        double testMargin = g_usedMargin + mid * dMargin;
        if(testMargin > 0 && (testEquity / testMargin * 100.0) > marginStopOut)
        {
            bestMult = mid;
            lo = mid;
        }
        else
        {
            hi = mid;
        }
    }
    tradeSize = bestMult * MinVolume;

    // DD reduction: at threshold, cut sizing by 50%
    if(ddPct >= DD_ReduceThreshold)
        tradeSize *= 0.5;

    return MathMin(tradeSize, MaxVolume);
}

//+------------------------------------------------------------------+
//| Normalize lot size to broker constraints                          |
//+------------------------------------------------------------------+
double NormalizeLots(double lots)
{
    lots = MathMax(g_minLot, lots);
    lots = MathMin(g_maxLot, lots);
    lots = MathRound(lots / g_lotStep) * g_lotStep;
    return lots;
}

//+------------------------------------------------------------------+
//| Open a position                                                   |
//+------------------------------------------------------------------+
bool OpenPosition(double lots, double tp, bool useTrailing)
{
    bool wasForced = false;

    if(lots < MinVolume)
    {
        g_lotZeroBlocks++;
        if(!ForceMinVolumeEntry)
            return false;
        lots = MinVolume;
        wasForced = true;
    }

    lots = NormalizeLots(lots);
    if(lots < g_minLot) return false;

    double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);

    // Normalize TP to tick size
    tp = MathRound(tp / g_tickSize) * g_tickSize;

    string comment = TradeComment;
    if(wasForced) comment += "_F";

    if(useTrailing && UseTrailingInTrends)
    {
        // Open without TP, set trailing stop after
        if(trade.Buy(lots, _Symbol, ask, 0, 0, comment + "_T"))
        {
            g_entriesAllowed++;
            g_trailingTPEntries++;

            // Set trailing stop on the new position
            // Find our just-opened position and set trailing
            ulong deal = trade.ResultDeal();
            if(deal > 0)
            {
                // Trailing is managed in OnTick by checking position profit
                // MT5 native trailing: we use the position SL update approach
            }
            return true;
        }
    }
    else
    {
        if(trade.Buy(lots, _Symbol, ask, 0, tp, comment))
        {
            g_entriesAllowed++;
            g_fixedTPEntries++;
            return true;
        }
    }

    Print("Order failed: ", GetLastError());
    return false;
}

//+------------------------------------------------------------------+
//| Main entry logic                                                  |
//+------------------------------------------------------------------+
void OpenNew(double ask, double spread, double effectiveSpacing, double ddPct)
{
    if(g_posCount == 0)
    {
        // First position: set equilibrium
        double lots = CalculateLotSize(ask, ddPct);
        double tp = ask + spread + g_currentSpacing;
        tp = NormalizeDouble(tp, g_digits);

        if(OpenPosition(lots, tp, false))
        {
            g_firstEntryPrice = ask;
            g_highestBuy = ask;
            g_lowestBuy = ask;
            g_rangingEntries++;
        }
    }
    else
    {
        // Additional positions: check spacing condition
        if(g_lowestBuy >= ask + effectiveSpacing)
        {
            // Multi-window velocity filter
            if(!CheckVelocityFilter())
            {
                g_velocityBlocks++;
                return;
            }

            double lots = CalculateLotSize(ask, ddPct);
            bool useTrailing = (g_currentRegime == REGIME_TRENDING_UP);
            double tp = CalculateTP(ask, spread);

            if(OpenPosition(lots, tp, useTrailing))
            {
                g_lowestBuy = ask;
                if(g_currentRegime == REGIME_RANGING)
                    g_rangingEntries++;
                else
                    g_trendingEntries++;
            }
        }
        else if(g_highestBuy <= ask - effectiveSpacing)
        {
            // Price moved up past highest grid level
            if(!CheckVelocityFilter())
            {
                g_velocityBlocks++;
                return;
            }

            double lots = CalculateLotSize(ask, ddPct);
            bool useTrailing = (g_currentRegime == REGIME_TRENDING_UP);
            double tp = CalculateTP(ask, spread);

            if(OpenPosition(lots, tp, useTrailing))
            {
                g_highestBuy = ask;
                if(g_currentRegime == REGIME_RANGING)
                    g_rangingEntries++;
                else
                    g_trendingEntries++;
            }
        }
    }

    // Manage trailing stops on existing positions
    if(UseTrailingInTrends)
        ManageTrailingStops();
}

//+------------------------------------------------------------------+
//| Manage trailing stops on positions opened in trending mode        |
//+------------------------------------------------------------------+
void ManageTrailingStops()
{
    double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);

    for(int i = PositionsTotal() - 1; i >= 0; i--)
    {
        ulong ticket = PositionGetTicket(i);
        if(ticket == 0) continue;
        if(PositionGetString(POSITION_SYMBOL) != _Symbol) continue;
        if(PositionGetInteger(POSITION_MAGIC) != MagicNumber) continue;
        if(PositionGetInteger(POSITION_TYPE) != POSITION_TYPE_BUY) continue;

        // Only manage positions with trailing comment
        string comment = PositionGetString(POSITION_COMMENT);
        if(StringFind(comment, "_T") < 0) continue;

        double entryPrice = PositionGetDouble(POSITION_PRICE_OPEN);
        double currentSL = PositionGetDouble(POSITION_SL);
        double profit = bid - entryPrice;

        // Only activate trailing after minimum profit
        if(profit < TrailingActivation) continue;

        // Calculate new trailing SL
        double newSL = bid - TrailingDistance;
        newSL = NormalizeDouble(newSL, g_digits);

        // Only move SL up, never down
        if(newSL > currentSL + g_tickSize)
        {
            trade.PositionModify(ticket, newSL, 0);
        }
    }
}

//+------------------------------------------------------------------+
//| Dashboard creation                                                |
//+------------------------------------------------------------------+
void CreateDashboard()
{
    int x = 10, y = 30;
    int width = 300, height = 480;

    // Background
    ObjectCreate(0, "ARG_BG", OBJ_RECTANGLE_LABEL, 0, 0, 0);
    ObjectSetInteger(0, "ARG_BG", OBJPROP_XDISTANCE, x);
    ObjectSetInteger(0, "ARG_BG", OBJPROP_YDISTANCE, y);
    ObjectSetInteger(0, "ARG_BG", OBJPROP_XSIZE, width);
    ObjectSetInteger(0, "ARG_BG", OBJPROP_YSIZE, height);
    ObjectSetInteger(0, "ARG_BG", OBJPROP_BGCOLOR, clrBlack);
    ObjectSetInteger(0, "ARG_BG", OBJPROP_BORDER_TYPE, BORDER_FLAT);
    ObjectSetInteger(0, "ARG_BG", OBJPROP_COLOR, clrDodgerBlue);

    // Title
    CreateLabel("ARG_Title", x + 10, y + 8, "AdaptiveRegimeGrid v1.0", clrDodgerBlue, 11);
    CreateLabel("ARG_Sub", x + 10, y + 26, "Regime + CircuitBreaker + SmartTP", clrDarkGray, 8);

    // Metric rows
    int lh = 18;
    int sy = y + 50;

    CreateLabel("ARG_L0", x + 10, sy + lh * 0, "Regime:", clrWhite, 9);
    CreateLabel("ARG_V0", x + 160, sy + lh * 0, "-", clrCyan, 9);
    CreateLabel("ARG_L1", x + 10, sy + lh * 1, "Eff. Ratio:", clrWhite, 9);
    CreateLabel("ARG_V1", x + 160, sy + lh * 1, "-", clrCyan, 9);
    CreateLabel("ARG_L2", x + 10, sy + lh * 2, "Spacing:", clrWhite, 9);
    CreateLabel("ARG_V2", x + 160, sy + lh * 2, "-", clrCyan, 9);

    CreateLabel("ARG_Sep1", x + 10, sy + lh * 3, "--- Account ---", clrDarkGray, 8);
    CreateLabel("ARG_L3", x + 10, sy + lh * 4, "Equity:", clrWhite, 9);
    CreateLabel("ARG_V3", x + 160, sy + lh * 4, "-", clrCyan, 9);
    CreateLabel("ARG_L4", x + 10, sy + lh * 5, "Peak Equity:", clrWhite, 9);
    CreateLabel("ARG_V4", x + 160, sy + lh * 5, "-", clrCyan, 9);
    CreateLabel("ARG_L5", x + 10, sy + lh * 6, "Drawdown:", clrWhite, 9);
    CreateLabel("ARG_V5", x + 160, sy + lh * 6, "-", clrCyan, 9);

    CreateLabel("ARG_Sep2", x + 10, sy + lh * 7, "--- Positions ---", clrDarkGray, 8);
    CreateLabel("ARG_L6", x + 10, sy + lh * 8, "Open Positions:", clrWhite, 9);
    CreateLabel("ARG_V6", x + 160, sy + lh * 8, "-", clrCyan, 9);
    CreateLabel("ARG_L7", x + 10, sy + lh * 9, "Total Volume:", clrWhite, 9);
    CreateLabel("ARG_V7", x + 160, sy + lh * 9, "-", clrCyan, 9);
    CreateLabel("ARG_L8", x + 10, sy + lh * 10, "Grid Range:", clrWhite, 9);
    CreateLabel("ARG_V8", x + 160, sy + lh * 10, "-", clrCyan, 9);

    CreateLabel("ARG_Sep3", x + 10, sy + lh * 11, "--- Circuit Breaker ---", clrDarkGray, 8);
    CreateLabel("ARG_L9", x + 10, sy + lh * 12, "Status:", clrWhite, 9);
    CreateLabel("ARG_V9", x + 160, sy + lh * 12, "-", clrLime, 9);
    CreateLabel("ARG_L10", x + 10, sy + lh * 13, "Liquidations:", clrWhite, 9);
    CreateLabel("ARG_V10", x + 160, sy + lh * 13, "-", clrCyan, 9);

    CreateLabel("ARG_Sep4", x + 10, sy + lh * 14, "--- Statistics ---", clrDarkGray, 8);
    CreateLabel("ARG_L11", x + 10, sy + lh * 15, "Entries:", clrWhite, 9);
    CreateLabel("ARG_V11", x + 160, sy + lh * 15, "-", clrCyan, 9);
    CreateLabel("ARG_L12", x + 10, sy + lh * 16, "Vel Blocks:", clrWhite, 9);
    CreateLabel("ARG_V12", x + 160, sy + lh * 16, "-", clrCyan, 9);
    CreateLabel("ARG_L13", x + 10, sy + lh * 17, "Trend/Range:", clrWhite, 9);
    CreateLabel("ARG_V13", x + 160, sy + lh * 17, "-", clrCyan, 9);
    CreateLabel("ARG_L14", x + 10, sy + lh * 18, "Trail/Fixed:", clrWhite, 9);
    CreateLabel("ARG_V14", x + 160, sy + lh * 18, "-", clrCyan, 9);
    CreateLabel("ARG_L15", x + 10, sy + lh * 19, "Peak DD:", clrWhite, 9);
    CreateLabel("ARG_V15", x + 160, sy + lh * 19, "-", clrCyan, 9);
    CreateLabel("ARG_L16", x + 10, sy + lh * 20, "Max Positions:", clrWhite, 9);
    CreateLabel("ARG_V16", x + 160, sy + lh * 20, "-", clrCyan, 9);

    CreateLabel("ARG_L17", x + 10, sy + lh * 22, "Regime Changes:", clrWhite, 9);
    CreateLabel("ARG_V17", x + 160, sy + lh * 22, "-", clrCyan, 9);
}

//+------------------------------------------------------------------+
//| Dashboard update                                                  |
//+------------------------------------------------------------------+
void UpdateDashboard(double bid, double ask, double equity, double ddPct)
{
    // Regime
    string regimeStr;
    color regimeClr;
    if(g_currentRegime == REGIME_TRENDING_UP)      { regimeStr = "TRENDING UP";   regimeClr = clrLime; }
    else if(g_currentRegime == REGIME_TRENDING_DOWN){ regimeStr = "TRENDING DOWN"; regimeClr = clrOrangeRed; }
    else                                            { regimeStr = "RANGING";       regimeClr = clrYellow; }

    ObjectSetString(0, "ARG_V0", OBJPROP_TEXT, regimeStr);
    ObjectSetInteger(0, "ARG_V0", OBJPROP_COLOR, regimeClr);
    ObjectSetString(0, "ARG_V1", OBJPROP_TEXT, DoubleToString(g_currentER, 3));
    ObjectSetString(0, "ARG_V2", OBJPROP_TEXT, "$" + DoubleToString(g_currentSpacing, 2));

    // Account
    ObjectSetString(0, "ARG_V3", OBJPROP_TEXT, "$" + DoubleToString(equity, 2));
    ObjectSetString(0, "ARG_V4", OBJPROP_TEXT, "$" + DoubleToString(g_peakEquity, 2));
    ObjectSetString(0, "ARG_V5", OBJPROP_TEXT, DoubleToString(ddPct, 2) + "%");
    ObjectSetInteger(0, "ARG_V5", OBJPROP_COLOR,
        ddPct >= DD_LiquidateThreshold ? clrRed :
        ddPct >= DD_HaltThreshold ? clrOrangeRed :
        ddPct >= DD_ReduceThreshold ? clrYellow : clrLime);

    // Positions
    ObjectSetString(0, "ARG_V6", OBJPROP_TEXT, IntegerToString(g_posCount));
    ObjectSetString(0, "ARG_V7", OBJPROP_TEXT, DoubleToString(g_totalVolume, 2) + " lots");

    string gridStr = "-";
    if(g_posCount > 0)
        gridStr = "$" + DoubleToString(g_lowestBuy, g_digits) + " - $" + DoubleToString(g_highestBuy, g_digits);
    ObjectSetString(0, "ARG_V8", OBJPROP_TEXT, gridStr);

    // Circuit breaker status
    string cbStatus;
    color cbClr;
    if(ddPct >= DD_LiquidateThreshold) { cbStatus = "LIQUIDATING";  cbClr = clrRed; }
    else if(ddPct >= DD_HaltThreshold) { cbStatus = "HALTED";       cbClr = clrOrangeRed; }
    else if(ddPct >= DD_ReduceThreshold){ cbStatus = "REDUCED 50%"; cbClr = clrYellow; }
    else                                { cbStatus = "NORMAL";       cbClr = clrLime; }

    ObjectSetString(0, "ARG_V9", OBJPROP_TEXT, cbStatus);
    ObjectSetInteger(0, "ARG_V9", OBJPROP_COLOR, cbClr);
    ObjectSetString(0, "ARG_V10", OBJPROP_TEXT, IntegerToString(g_ddLiquidations) + " (" + IntegerToString(g_positionsLiquidated) + " pos)");

    // Statistics
    ObjectSetString(0, "ARG_V11", OBJPROP_TEXT, IntegerToString(g_entriesAllowed));
    ObjectSetString(0, "ARG_V12", OBJPROP_TEXT, IntegerToString(g_velocityBlocks));
    ObjectSetString(0, "ARG_V13", OBJPROP_TEXT, IntegerToString(g_trendingEntries) + " / " + IntegerToString(g_rangingEntries));
    ObjectSetString(0, "ARG_V14", OBJPROP_TEXT, IntegerToString(g_trailingTPEntries) + " / " + IntegerToString(g_fixedTPEntries));
    ObjectSetString(0, "ARG_V15", OBJPROP_TEXT, DoubleToString(g_peakDDPct, 2) + "%");
    ObjectSetString(0, "ARG_V16", OBJPROP_TEXT, IntegerToString(g_maxPosCount));
    ObjectSetString(0, "ARG_V17", OBJPROP_TEXT, IntegerToString(g_regimeChanges));
}

//+------------------------------------------------------------------+
//| Helper: create a chart label                                      |
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
}
//+------------------------------------------------------------------+
