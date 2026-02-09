//+------------------------------------------------------------------+
//| ParallelDual_Unified_EA.mq5                                        |
//| Unified Dynamic Survival Strategy                                  |
//| Floor tracks price, margin survival check before each entry        |
//| NO TAKE PROFIT - pure accumulation                                 |
//+------------------------------------------------------------------+
#property copyright "Backtest Framework"
#property version   "1.00"
#property strict

//--- Input Parameters
input group "=== Core Survival ==="
input double InpSurvivePct = 15.0;           // Survive % (floor = price * (1 - survive%))

input group "=== Spacing ==="
input double InpMinSpacing = 1.0;            // Min Spacing for upward entries ($)
input double InpMaxSpacing = 10.0;           // Max Adaptive Spacing ($)
input double InpBaseSpacing = 1.50;          // Base Spacing for downward entries ($)
input int    InpTargetTrades = 20;           // Target trades in range (for spacing calc)

input group "=== Volume Limits ==="
input double InpMinVolume = 0.01;            // Minimum Volume (lots)
input double InpMaxVolume = 10.0;            // Maximum Volume (lots)

input group "=== Safety ==="
input double InpMarginStopOut = 20.0;        // Margin Stop-Out Level %
input double InpSafetyBuffer = 1.5;          // Safety Buffer (multiplier)

input group "=== EA Settings ==="
input int    InpMagicNumber = 20260131;      // Magic Number
input string InpComment = "PDual_Unified";   // Order Comment

//--- Global Variables
bool g_initialized = false;
double g_highestEntry = 0.0;
double g_lowestEntry = DBL_MAX;
double g_currentAdaptiveSpacing = 0.0;
double g_contractSize = 0.0;
double g_leverage = 0.0;

// Stats
int g_upwardEntries = 0;
int g_downwardEntries = 0;
int g_totalEntries = 0;
int g_skippedByMargin = 0;

//+------------------------------------------------------------------+
//| Expert initialization function                                     |
//+------------------------------------------------------------------+
int OnInit()
{
    g_currentAdaptiveSpacing = InpBaseSpacing;

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
        g_leverage = 500.0;
    }

    Print("ParallelDual Unified EA initialized");
    Print("Contract Size: ", g_contractSize, " Leverage: ", g_leverage);
    Print("Survive %: ", InpSurvivePct, " Safety Buffer: ", InpSafetyBuffer);

    return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+
//| Expert deinitialization function                                   |
//+------------------------------------------------------------------+
void OnDeinit(const int reason)
{
    Print("=== ParallelDual Unified Stats ===");
    Print("Upward Entries: ", g_upwardEntries);
    Print("Downward Entries: ", g_downwardEntries);
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

    // Calculate current floor (tracks current price)
    double floor = ask * (1.0 - InpSurvivePct / 100.0);

    // UPWARD: price rose above highest entry + min_spacing
    if(ask > g_highestEntry + InpMinSpacing)
    {
        TryUpwardEntry(ask, floor);
    }
    // DOWNWARD: price dropped below lowest entry - adaptive spacing
    else if(ask < g_lowestEntry - g_currentAdaptiveSpacing)
    {
        TryDownwardEntry(ask, floor);
    }
}

//+------------------------------------------------------------------+
//| Initialize on first tick                                           |
//+------------------------------------------------------------------+
void Initialize(double ask)
{
    double floor = ask * (1.0 - InpSurvivePct / 100.0);

    // Calculate initial lot sized to survive drop to floor
    double lot = CalculateInitialLot(ask, floor);

    if(lot >= InpMinVolume)
    {
        if(OpenBuyOrder(lot, 0.0))  // No TP
        {
            g_highestEntry = ask;
            g_lowestEntry = ask;
            g_totalEntries++;
            g_upwardEntries++;
        }
    }

    // Calculate initial adaptive spacing
    UpdateAdaptiveSpacing(ask, floor);

    g_initialized = true;
    Print("Initialized at price: ", ask, " Floor: ", floor, " Spacing: ", g_currentAdaptiveSpacing);
}

//+------------------------------------------------------------------+
//| Try Upward Entry (price > highest + min_spacing)                   |
//+------------------------------------------------------------------+
void TryUpwardEntry(double ask, double floor)
{
    double lot = CalculateLotForEntry(ask, floor);

    if(lot < InpMinVolume)
    {
        g_skippedByMargin++;
        return;
    }

    // Verify we can survive a drop to floor with this new trade
    if(!CanSurvive(ask, lot, floor))
    {
        g_skippedByMargin++;
        return;
    }

    if(OpenBuyOrder(lot, 0.0))  // No TP
    {
        g_highestEntry = ask;
        g_upwardEntries++;
        g_totalEntries++;

        // Recalculate adaptive spacing after entry
        UpdateAdaptiveSpacing(ask, floor);
    }
}

//+------------------------------------------------------------------+
//| Try Downward Entry (price < lowest - adaptive_spacing)             |
//+------------------------------------------------------------------+
void TryDownwardEntry(double ask, double floor)
{
    // Recalculate adaptive spacing for current conditions
    UpdateAdaptiveSpacing(ask, floor);

    double lot = CalculateLotForEntry(ask, floor);

    if(lot < InpMinVolume)
    {
        g_skippedByMargin++;
        return;
    }

    // Verify we can survive a drop to floor with this new trade
    if(!CanSurvive(ask, lot, floor))
    {
        g_skippedByMargin++;
        return;
    }

    if(OpenBuyOrder(lot, 0.0))  // No TP
    {
        g_lowestEntry = ask;
        g_downwardEntries++;
        g_totalEntries++;

        // Recalculate adaptive spacing after entry
        UpdateAdaptiveSpacing(ask, floor);
    }
}

//+------------------------------------------------------------------+
//| Calculate Initial Lot Size                                         |
//+------------------------------------------------------------------+
double CalculateInitialLot(double price, double floor)
{
    double equity = AccountInfoDouble(ACCOUNT_EQUITY);
    double distance = price - floor;

    if(distance <= 0)
        distance = price * (InpSurvivePct / 100.0);

    // Calculate how much loss 1.0 lot would have at floor
    double lossPerLot = distance * g_contractSize;

    // Calculate margin required for 1.0 lot at floor price
    double marginPerLot = 1.0 * g_contractSize * floor / g_leverage;

    // Target margin level = stop_out * safety_buffer
    double targetMarginLevel = InpMarginStopOut * InpSafetyBuffer;
    double costPerLot = lossPerLot + marginPerLot * targetMarginLevel / 100.0;

    if(costPerLot <= 0) return InpMinVolume;

    double maxLots = equity / costPerLot;

    // Scale down to leave room for future entries (use 5% of capacity)
    double lot = maxLots * 0.05;

    lot = NormalizeLot(lot);
    return MathMax(lot, InpMinVolume);
}

//+------------------------------------------------------------------+
//| Calculate Lot Size for Entry                                       |
//+------------------------------------------------------------------+
double CalculateLotForEntry(double price, double floor)
{
    double equity = AccountInfoDouble(ACCOUNT_EQUITY);

    // Calculate current P/L at floor and margin requirements
    double currentPnlAtFloor = 0.0;
    double currentMarginAtFloor = 0.0;
    double currentPnl = 0.0;

    for(int i = PositionsTotal() - 1; i >= 0; i--)
    {
        ulong ticket = PositionGetTicket(i);
        if(ticket == 0) continue;

        if(PositionGetInteger(POSITION_MAGIC) != InpMagicNumber) continue;
        if(PositionGetString(POSITION_SYMBOL) != _Symbol) continue;

        double lots = PositionGetDouble(POSITION_VOLUME);
        double entryPrice = PositionGetDouble(POSITION_PRICE_OPEN);

        double pnlAtFloor = (floor - entryPrice) * lots * g_contractSize;
        currentPnlAtFloor += pnlAtFloor;

        double margin = lots * g_contractSize * floor / g_leverage;
        currentMarginAtFloor += margin;

        double pnlCurrent = (price - entryPrice) * lots * g_contractSize;
        currentPnl += pnlCurrent;
    }

    // Equity at floor = current equity + P/L change from current to floor
    double equityAtFloor = equity + (currentPnlAtFloor - currentPnl);

    if(equityAtFloor <= 0) return 0.0;

    double targetMarginLevel = InpMarginStopOut * InpSafetyBuffer;

    // Check if current margin level at floor allows new positions
    if(currentMarginAtFloor > 0)
    {
        double currentMarginLevel = (equityAtFloor / currentMarginAtFloor) * 100.0;
        if(currentMarginLevel < targetMarginLevel * 2.0)
            return 0.0;
    }

    // For new trade at current price, dropping to floor
    double distance = price - floor;
    if(distance <= 0) distance = price * (InpSurvivePct / 100.0);

    double lossPerLot = distance * g_contractSize;
    double marginPerLot = 1.0 * g_contractSize * floor / g_leverage;

    // Available equity for new trade
    double requiredReserve = currentMarginAtFloor * targetMarginLevel / 100.0;
    double available = equityAtFloor - requiredReserve;

    if(available <= 0) return 0.0;

    // Cost per lot = loss + margin reserve needed
    double costPerLot = lossPerLot + marginPerLot * targetMarginLevel / 100.0;
    if(costPerLot <= 0) return InpMinVolume;

    double maxLots = available / costPerLot;

    // Scale down to leave room for future entries (20% of capacity)
    maxLots = maxLots * 0.20;

    double lot = NormalizeLot(maxLots);
    return MathMin(lot, InpMaxVolume);
}

//+------------------------------------------------------------------+
//| Check if we can survive a drop to floor                            |
//+------------------------------------------------------------------+
bool CanSurvive(double ask, double newLot, double floor)
{
    double equity = AccountInfoDouble(ACCOUNT_EQUITY);

    // Calculate total unrealized loss at floor for all existing trades + new trade
    double totalLoss = 0.0;
    double totalMargin = 0.0;

    for(int i = PositionsTotal() - 1; i >= 0; i--)
    {
        ulong ticket = PositionGetTicket(i);
        if(ticket == 0) continue;

        if(PositionGetInteger(POSITION_MAGIC) != InpMagicNumber) continue;
        if(PositionGetString(POSITION_SYMBOL) != _Symbol) continue;

        double lots = PositionGetDouble(POSITION_VOLUME);
        double entryPrice = PositionGetDouble(POSITION_PRICE_OPEN);

        double loss = (entryPrice - floor) * lots * g_contractSize;
        totalLoss += loss;

        double margin = lots * g_contractSize * floor / g_leverage;
        totalMargin += margin;
    }

    // Add new trade
    double newLoss = (ask - floor) * newLot * g_contractSize;
    double newMargin = newLot * g_contractSize * floor / g_leverage;

    totalLoss += newLoss;
    totalMargin += newMargin;

    // Calculate margin level at floor
    double equityAtFloor = equity - totalLoss;
    if(equityAtFloor <= 0) return false;

    if(totalMargin <= 0) return true;

    double marginLevel = (equityAtFloor / totalMargin) * 100.0;

    // Must stay above stop_out level with safety buffer
    return marginLevel > InpMarginStopOut * InpSafetyBuffer;
}

//+------------------------------------------------------------------+
//| Update Adaptive Spacing                                            |
//+------------------------------------------------------------------+
void UpdateAdaptiveSpacing(double currentPrice, double floor)
{
    double remainingDistance = currentPrice - floor;

    if(remainingDistance <= 0)
    {
        g_currentAdaptiveSpacing = InpMinSpacing;
        return;
    }

    double equity = AccountInfoDouble(ACCOUNT_EQUITY);
    double currentLossAtFloor = 0.0;
    double currentMarginAtFloor = 0.0;

    for(int i = PositionsTotal() - 1; i >= 0; i--)
    {
        ulong ticket = PositionGetTicket(i);
        if(ticket == 0) continue;

        if(PositionGetInteger(POSITION_MAGIC) != InpMagicNumber) continue;
        if(PositionGetString(POSITION_SYMBOL) != _Symbol) continue;

        double lots = PositionGetDouble(POSITION_VOLUME);
        double entryPrice = PositionGetDouble(POSITION_PRICE_OPEN);

        double loss = (entryPrice - floor) * lots * g_contractSize;
        currentLossAtFloor += loss;

        double margin = lots * g_contractSize * floor / g_leverage;
        currentMarginAtFloor += margin;
    }

    double equityAtFloor = equity - currentLossAtFloor;
    double targetMarginLevel = InpMarginStopOut * InpSafetyBuffer;

    double usableEquity = equityAtFloor - (currentMarginAtFloor * targetMarginLevel / 100.0);

    if(usableEquity <= 0)
    {
        g_currentAdaptiveSpacing = InpMaxSpacing;
        return;
    }

    // Calculate cost per trade at min_volume
    double lossPerTrade = (remainingDistance / InpTargetTrades) * InpMinVolume * g_contractSize;
    double marginPerTrade = InpMinVolume * g_contractSize * floor / g_leverage;
    double costPerTrade = lossPerTrade + marginPerTrade * targetMarginLevel / 100.0;

    if(costPerTrade <= 0)
    {
        g_currentAdaptiveSpacing = InpBaseSpacing;
        return;
    }

    // How many trades can we afford?
    int maxTrades = (int)(usableEquity / costPerTrade);
    maxTrades = MathMax(1, maxTrades);

    // Calculate spacing to spread trades evenly
    double spacing = remainingDistance / maxTrades;

    // Clamp to reasonable bounds
    g_currentAdaptiveSpacing = MathMax(InpMinSpacing, MathMin(spacing, InpMaxSpacing));
}

//+------------------------------------------------------------------+
//| Open Buy Order (no TP for pure accumulation)                       |
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
