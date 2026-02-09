//+------------------------------------------------------------------+
//| ParallelDual_Original_EA.mq5                                      |
//| Parallel Dual Strategy - Original with Aggressive Compounding     |
//| Grid ("up while down") + Momentum ("up while up") with TP         |
//+------------------------------------------------------------------+
#property copyright "Backtest Framework"
#property version   "1.00"
#property strict

//--- Input Parameters
input group "=== Core Settings ==="
input double InpSurvivePct = 10.0;           // Survive % (for margin calculations)
input double InpGridAllocation = 0.10;       // Grid Allocation (fraction 0.0-1.0)
input double InpMomentumAllocation = 0.90;   // Momentum Allocation (fraction 0.0-1.0)

input group "=== Spacing ==="
input double InpBaseSpacing = 1.50;          // Grid Spacing ($)
input double InpMomentumSpacing = 5.0;       // Momentum Spacing ($)

input group "=== Take Profit ==="
input bool   InpUseTakeProfit = true;        // Use Take Profit
input double InpTPDistance = 5.0;            // TP Distance from entry ($)

input group "=== Volume Limits ==="
input double InpMinVolume = 0.01;            // Minimum Volume (lots)
input double InpMaxVolume = 10.0;            // Maximum Volume (lots)

input group "=== Safety ==="
input double InpMarginStopOut = 20.0;        // Margin Stop-Out Level %
input bool   InpForceMinVolume = false;      // Force Min Volume Entry

input group "=== EA Settings ==="
input int    InpMagicNumber = 20260130;      // Magic Number
input string InpComment = "PDual_Orig";      // Order Comment

//--- Global Variables
bool g_initialized = false;
double g_startPrice = 0.0;
double g_gridCeiling = 0.0;
double g_gridFloor = DBL_MAX;
double g_lastMomentumEntry = 0.0;
double g_contractSize = 0.0;
double g_leverage = 0.0;

// Stats
int g_gridEntries = 0;
int g_momentumEntries = 0;
int g_totalEntries = 0;
int g_skippedByMargin = 0;

//+------------------------------------------------------------------+
//| Expert initialization function                                     |
//+------------------------------------------------------------------+
int OnInit()
{
    // Get symbol info
    g_contractSize = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);

    // Calculate effective leverage
    double marginRequired = 0.0;
    if(OrderCalcMargin(ORDER_TYPE_BUY, _Symbol, 1.0, SymbolInfoDouble(_Symbol, SYMBOL_ASK), marginRequired))
    {
        double price = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
        g_leverage = (price * g_contractSize) / marginRequired;
    }
    else
    {
        g_leverage = 500.0; // Default
    }

    Print("ParallelDual Original EA initialized");
    Print("Contract Size: ", g_contractSize, " Leverage: ", g_leverage);
    Print("Grid Alloc: ", InpGridAllocation * 100, "% Momentum Alloc: ", InpMomentumAllocation * 100, "%");

    return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+
//| Expert deinitialization function                                   |
//+------------------------------------------------------------------+
void OnDeinit(const int reason)
{
    Print("=== ParallelDual Original Stats ===");
    Print("Grid Entries: ", g_gridEntries);
    Print("Momentum Entries: ", g_momentumEntries);
    Print("Total Entries: ", g_totalEntries);
    Print("Skipped by Margin: ", g_skippedByMargin);
}

//+------------------------------------------------------------------+
//| Expert tick function                                               |
//+------------------------------------------------------------------+
void OnTick()
{
    double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);

    if(!g_initialized)
    {
        Initialize(ask);
        return;
    }

    // Update grid ceiling on new highs
    if(ask > g_gridCeiling)
    {
        g_gridCeiling = ask;
    }

    // Try Grid Entry (on dips)
    TryGridEntry(ask);

    // Try Momentum Entry (on new highs)
    TryMomentumEntry(ask);
}

//+------------------------------------------------------------------+
//| Initialize on first tick                                           |
//+------------------------------------------------------------------+
void Initialize(double ask)
{
    g_startPrice = ask;
    g_gridCeiling = ask;
    g_gridFloor = ask;
    g_lastMomentumEntry = ask;

    // Open initial position
    double lot = CalculateGridLotSize(ask);
    if(lot >= InpMinVolume)
    {
        double tp = InpUseTakeProfit ? (ask + InpTPDistance) : 0.0;
        if(OpenBuyOrder(lot, tp))
        {
            g_gridEntries++;
            g_momentumEntries++;
            g_totalEntries++;
        }
    }

    g_initialized = true;
    Print("Initialized at price: ", ask);
}

//+------------------------------------------------------------------+
//| Try Grid Entry (on dips below floor - spacing)                     |
//+------------------------------------------------------------------+
void TryGridEntry(double ask)
{
    // Grid entry triggers when price drops below (grid_floor_ - spacing)
    if(ask >= g_gridFloor - InpBaseSpacing)
        return;

    double lot = CalculateGridLotSize(ask);

    if(lot < InpMinVolume)
    {
        if(InpForceMinVolume)
            lot = InpMinVolume;
        else
        {
            g_skippedByMargin++;
            return;
        }
    }

    // Check margin
    if(!HasSufficientMargin(ask, lot))
    {
        g_skippedByMargin++;
        return;
    }

    double tp = InpUseTakeProfit ? (ask + InpTPDistance) : 0.0;
    if(OpenBuyOrder(lot, tp))
    {
        g_gridFloor = ask;
        g_gridEntries++;
        g_totalEntries++;
    }
}

//+------------------------------------------------------------------+
//| Try Momentum Entry (on new highs above last + spacing)             |
//+------------------------------------------------------------------+
void TryMomentumEntry(double ask)
{
    // Momentum entry triggers when price exceeds last entry by momentum_spacing
    if(ask <= g_lastMomentumEntry + InpMomentumSpacing)
        return;

    double lot = CalculateMomentumLotSize(ask);

    if(lot < InpMinVolume)
    {
        if(InpForceMinVolume)
            lot = InpMinVolume;
        else
        {
            g_skippedByMargin++;
            return;
        }
    }

    // Check margin
    if(!HasSufficientMargin(ask, lot))
    {
        g_skippedByMargin++;
        return;
    }

    double tp = InpUseTakeProfit ? (ask + InpTPDistance) : 0.0;
    if(OpenBuyOrder(lot, tp))
    {
        g_lastMomentumEntry = ask;
        g_momentumEntries++;
        g_totalEntries++;
    }
}

//+------------------------------------------------------------------+
//| Calculate Grid Lot Size - AGGRESSIVE COMPOUNDING                   |
//+------------------------------------------------------------------+
double CalculateGridLotSize(double ask)
{
    // AGGRESSIVE COMPOUNDING: Use fraction of equity for position sizing
    double equity = AccountInfoDouble(ACCOUNT_EQUITY);

    // Calculate margin per lot: price * contract_size / leverage
    double marginPerLot = ask * g_contractSize / g_leverage;

    // Risk factor: 5% of equity * grid allocation
    double riskFactor = 0.05 * InpGridAllocation;

    // Calculate lot size
    double lot = (equity * riskFactor) / marginPerLot;

    // Apply free margin check - don't use more than 50% of free margin
    double usedMargin = GetUsedMargin();
    double freeMargin = equity - usedMargin;
    double maxLotByMargin = (freeMargin * 0.5) / marginPerLot;
    lot = MathMin(lot, maxLotByMargin);

    // Clamp and round
    lot = MathMax(lot, InpMinVolume);
    lot = MathMin(lot, InpMaxVolume);
    lot = NormalizeLot(lot);

    return lot;
}

//+------------------------------------------------------------------+
//| Calculate Momentum Lot Size - AGGRESSIVE COMPOUNDING               |
//+------------------------------------------------------------------+
double CalculateMomentumLotSize(double ask)
{
    double equity = AccountInfoDouble(ACCOUNT_EQUITY);

    double marginPerLot = ask * g_contractSize / g_leverage;
    double riskFactor = 0.05 * InpMomentumAllocation;

    double lot = (equity * riskFactor) / marginPerLot;

    double usedMargin = GetUsedMargin();
    double freeMargin = equity - usedMargin;
    double maxLotByMargin = (freeMargin * 0.5) / marginPerLot;
    lot = MathMin(lot, maxLotByMargin);

    lot = MathMax(lot, InpMinVolume);
    lot = MathMin(lot, InpMaxVolume);
    lot = NormalizeLot(lot);

    return lot;
}

//+------------------------------------------------------------------+
//| Get Used Margin for our positions                                  |
//+------------------------------------------------------------------+
double GetUsedMargin()
{
    double usedMargin = 0.0;

    for(int i = PositionsTotal() - 1; i >= 0; i--)
    {
        ulong ticket = PositionGetTicket(i);
        if(ticket == 0) continue;

        if(PositionGetInteger(POSITION_MAGIC) != InpMagicNumber) continue;
        if(PositionGetString(POSITION_SYMBOL) != _Symbol) continue;

        double lots = PositionGetDouble(POSITION_VOLUME);
        double entryPrice = PositionGetDouble(POSITION_PRICE_OPEN);

        usedMargin += lots * g_contractSize * entryPrice / g_leverage;
    }

    return usedMargin;
}

//+------------------------------------------------------------------+
//| Check if we have sufficient margin                                 |
//+------------------------------------------------------------------+
bool HasSufficientMargin(double ask, double lot)
{
    double equity = AccountInfoDouble(ACCOUNT_EQUITY);
    double usedMargin = GetUsedMargin();

    // Calculate margin for new position
    double newMargin = lot * g_contractSize * ask / g_leverage;
    double totalMargin = usedMargin + newMargin;

    if(totalMargin <= 0) return true;

    // Check margin level
    double marginLevel = (equity / totalMargin) * 100.0;

    // Require at least 2x the stop-out level for safety
    return marginLevel > InpMarginStopOut * 2.0;
}

//+------------------------------------------------------------------+
//| Open Buy Order                                                     |
//+------------------------------------------------------------------+
bool OpenBuyOrder(double lot, double tp)
{
    MqlTradeRequest request = {};
    MqlTradeResult result = {};

    request.action = TRADE_ACTION_DEAL;
    request.symbol = _Symbol;
    request.volume = lot;
    request.type = ORDER_TYPE_BUY;
    request.price = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    request.sl = 0.0;
    request.tp = tp;
    request.deviation = 10;
    request.magic = InpMagicNumber;
    request.comment = InpComment;
    request.type_filling = ORDER_FILLING_IOC;

    if(!OrderSend(request, result))
    {
        Print("OrderSend failed: ", GetLastError(), " RetCode: ", result.retcode);
        return false;
    }

    if(result.retcode != TRADE_RETCODE_DONE)
    {
        Print("Order not executed: ", result.retcode);
        return false;
    }

    return true;
}

//+------------------------------------------------------------------+
//| Normalize lot size to broker requirements                          |
//+------------------------------------------------------------------+
double NormalizeLot(double lot)
{
    double minLot = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN);
    double maxLot = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MAX);
    double lotStep = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_STEP);

    lot = MathFloor(lot / lotStep) * lotStep;
    lot = MathMax(lot, minLot);
    lot = MathMin(lot, maxLot);

    return NormalizeDouble(lot, 2);
}
//+------------------------------------------------------------------+
