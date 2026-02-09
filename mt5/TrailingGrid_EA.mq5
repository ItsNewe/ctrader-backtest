//+------------------------------------------------------------------+
//|                                              TrailingGrid_EA.mq5 |
//|                                    Trailing Stop Grid Strategy   |
//|                          Based on C++ backtest optimization      |
//+------------------------------------------------------------------+
#property copyright "Backtest Framework"
#property version   "1.00"
#property strict

//+------------------------------------------------------------------+
//| Input Parameters                                                  |
//+------------------------------------------------------------------+
input group "=== Grid Settings ==="
input int      MaxPositions     = 20;      // Maximum concurrent positions
input double   Spacing          = 1.0;     // Grid spacing in price ($)
input double   SurvivePct       = 13.0;    // Survive % (position sizing)
input double   StopOpeningPct   = 13.0;    // Stop opening after X% drop from peak

input group "=== Trailing Stop Settings ==="
input double   MinProfitPoints  = 0.30;    // Min price rise ($) to activate trailing
input double   TrailDistance    = 15.0;    // Trailing stop distance ($) from highest
input double   UpdateThreshold  = 1.0;     // Update trail when price rises by ($)

input group "=== Risk Management ==="
input double   RiskPercent      = 80.0;    // % of equity to risk
input double   MaxLotSize       = 10.0;    // Maximum lot size per position
input double   MinLotSize       = 0.01;    // Minimum lot size

input group "=== General ==="
input int      MagicNumber      = 123456;  // EA Magic Number
input string   TradeComment     = "TrailingGrid"; // Trade comment

//+------------------------------------------------------------------+
//| Global Variables                                                  |
//+------------------------------------------------------------------+
double g_pricePeak = 0;           // Highest price seen
double g_lowestBuy = DBL_MAX;     // Lowest entry price among open positions
double g_contractSize = 0;        // Symbol contract size
int    g_digits = 0;              // Symbol digits

// Position tracking structure
struct PositionInfo {
    ulong  ticket;
    double entryPrice;
    double lotSize;
    double highestPrice;
    double trailStop;
    bool   trailingActive;
};

PositionInfo g_positions[];

//+------------------------------------------------------------------+
//| Expert initialization function                                    |
//+------------------------------------------------------------------+
int OnInit()
{
    // Get symbol information
    g_contractSize = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);
    g_digits = (int)SymbolInfoInteger(_Symbol, SYMBOL_DIGITS);

    if(g_contractSize <= 0)
    {
        Print("Error: Invalid contract size for ", _Symbol);
        return INIT_FAILED;
    }

    Print("=== TrailingGrid EA Initialized ===");
    Print("Symbol: ", _Symbol);
    Print("Contract Size: ", g_contractSize);
    Print("Max Positions: ", MaxPositions);
    Print("Spacing: $", Spacing);
    Print("Survive %: ", SurvivePct);
    Print("Stop Opening %: ", StopOpeningPct);
    Print("Min Profit Points: $", MinProfitPoints);
    Print("Trail Distance: $", TrailDistance);
    Print("Update Threshold: $", UpdateThreshold);

    // Initialize price peak with current price
    g_pricePeak = SymbolInfoDouble(_Symbol, SYMBOL_BID);

    // Load existing positions
    LoadExistingPositions();

    return INIT_SUCCEEDED;
}

//+------------------------------------------------------------------+
//| Expert deinitialization function                                  |
//+------------------------------------------------------------------+
void OnDeinit(const int reason)
{
    Print("TrailingGrid EA stopped. Reason: ", reason);
}

//+------------------------------------------------------------------+
//| Expert tick function                                              |
//+------------------------------------------------------------------+
void OnTick()
{
    double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
    double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);

    // Update price peak
    if(bid > g_pricePeak)
        g_pricePeak = bid;

    // Process existing positions (trailing stops)
    ProcessPositions(bid);

    // Check if we should open new positions
    CheckNewEntry(bid, ask);
}

//+------------------------------------------------------------------+
//| Load existing positions opened by this EA                         |
//+------------------------------------------------------------------+
void LoadExistingPositions()
{
    ArrayResize(g_positions, 0);
    g_lowestBuy = DBL_MAX;

    for(int i = PositionsTotal() - 1; i >= 0; i--)
    {
        ulong ticket = PositionGetTicket(i);
        if(ticket == 0) continue;

        if(PositionGetString(POSITION_SYMBOL) != _Symbol) continue;
        if(PositionGetInteger(POSITION_MAGIC) != MagicNumber) continue;
        if(PositionGetInteger(POSITION_TYPE) != POSITION_TYPE_BUY) continue;

        PositionInfo pos;
        pos.ticket = ticket;
        pos.entryPrice = PositionGetDouble(POSITION_PRICE_OPEN);
        pos.lotSize = PositionGetDouble(POSITION_VOLUME);
        pos.highestPrice = SymbolInfoDouble(_Symbol, SYMBOL_BID);
        pos.trailStop = 0;
        pos.trailingActive = false;

        // Check if position is already in profit for trailing (based on price move)
        double currentBid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
        double priceMove = currentBid - pos.entryPrice;
        if(priceMove >= MinProfitPoints)
        {
            pos.trailingActive = true;
            pos.highestPrice = SymbolInfoDouble(_Symbol, SYMBOL_BID);
            pos.trailStop = pos.highestPrice - TrailDistance;
        }

        int size = ArraySize(g_positions);
        ArrayResize(g_positions, size + 1);
        g_positions[size] = pos;

        if(pos.entryPrice < g_lowestBuy)
            g_lowestBuy = pos.entryPrice;
    }

    Print("Loaded ", ArraySize(g_positions), " existing positions");
}

//+------------------------------------------------------------------+
//| Process positions - update trailing stops and close if hit        |
//+------------------------------------------------------------------+
void ProcessPositions(double bid)
{
    g_lowestBuy = DBL_MAX;

    for(int i = ArraySize(g_positions) - 1; i >= 0; i--)
    {
        // Verify position still exists
        if(!PositionSelectByTicket(g_positions[i].ticket))
        {
            // Position was closed externally, remove from tracking
            ArrayRemove(g_positions, i, 1);
            continue;
        }

        double priceMove = bid - g_positions[i].entryPrice;

        // Check if trailing should activate (based on price move, not dollar profit)
        if(!g_positions[i].trailingActive && priceMove >= MinProfitPoints)
        {
            g_positions[i].trailingActive = true;
            g_positions[i].highestPrice = bid;
            g_positions[i].trailStop = bid - TrailDistance;
            Print("Trailing activated for ticket ", g_positions[i].ticket,
                  " at price ", bid, ", trail stop: ", g_positions[i].trailStop);
        }

        // Update trailing stop if active
        if(g_positions[i].trailingActive)
        {
            // Update if price has risen by threshold
            if(bid >= g_positions[i].highestPrice + UpdateThreshold)
            {
                g_positions[i].highestPrice = bid;
                g_positions[i].trailStop = bid - TrailDistance;
                Print("Trail updated for ticket ", g_positions[i].ticket,
                      " new stop: ", g_positions[i].trailStop);
            }

            // Check if trail stop hit
            if(bid <= g_positions[i].trailStop)
            {
                Print("Trail stop hit for ticket ", g_positions[i].ticket,
                      " at ", bid, " (stop was ", g_positions[i].trailStop, ")");

                if(ClosePosition(g_positions[i].ticket))
                {
                    ArrayRemove(g_positions, i, 1);
                    continue;
                }
            }
        }

        // Track lowest buy price
        if(g_positions[i].entryPrice < g_lowestBuy)
            g_lowestBuy = g_positions[i].entryPrice;
    }
}

//+------------------------------------------------------------------+
//| Check if we should open a new position                            |
//+------------------------------------------------------------------+
void CheckNewEntry(double bid, double ask)
{
    int posCount = ArraySize(g_positions);

    // Check max positions
    if(posCount >= MaxPositions)
        return;

    // Check stop-opening band (don't open if price dropped too far from peak)
    double dropFromPeakPct = (g_pricePeak - bid) / g_pricePeak * 100.0;
    if(dropFromPeakPct >= StopOpeningPct)
    {
        // Optionally log this
        // Print("Skipping entry - price dropped ", dropFromPeakPct, "% from peak");
        return;
    }

    // Check entry conditions
    bool shouldOpen = false;

    if(posCount == 0)
    {
        // No positions - always open first one
        shouldOpen = true;
    }
    else if(ask <= g_lowestBuy - Spacing)
    {
        // Price dropped by spacing from lowest buy
        shouldOpen = true;
    }

    if(shouldOpen)
    {
        double lot = CalculateLotSize(ask);
        if(lot >= MinLotSize)
        {
            OpenBuyPosition(lot);
        }
    }
}

//+------------------------------------------------------------------+
//| Calculate lot size based on survive_pct                           |
//+------------------------------------------------------------------+
double CalculateLotSize(double price)
{
    double equity = AccountInfoDouble(ACCOUNT_EQUITY);

    // How far price can drop before we want to survive
    double priceDrop = price * SurvivePct / 100.0;

    // How many positions we'll accumulate during this drop
    double numPositions = MathMin((double)MaxPositions, priceDrop / Spacing);
    if(numPositions < 1) numPositions = 1;

    // Average loss per lot across grid
    double avgLossPerLot = (priceDrop / 2.0) * g_contractSize;

    // Target risk
    double targetRisk = equity * RiskPercent / 100.0;

    // Calculate lot size
    double lot = targetRisk / (numPositions * avgLossPerLot);

    // Normalize lot size
    double lotStep = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_STEP);
    double minLot = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN);
    double maxLot = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MAX);

    lot = MathFloor(lot / lotStep) * lotStep;
    lot = MathMax(minLot, MathMin(maxLot, lot));
    lot = MathMin(lot, MaxLotSize);

    return lot;
}

//+------------------------------------------------------------------+
//| Open a buy position                                               |
//+------------------------------------------------------------------+
bool OpenBuyPosition(double lot)
{
    MqlTradeRequest request = {};
    MqlTradeResult result = {};

    request.action = TRADE_ACTION_DEAL;
    request.symbol = _Symbol;
    request.volume = lot;
    request.type = ORDER_TYPE_BUY;
    request.price = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    request.deviation = 10;
    request.magic = MagicNumber;
    request.comment = TradeComment;

    // No SL/TP - we manage trailing manually
    request.sl = 0;
    request.tp = 0;

    if(!OrderSend(request, result))
    {
        Print("OrderSend error: ", GetLastError(), " - ", result.comment);
        return false;
    }

    if(result.retcode != TRADE_RETCODE_DONE)
    {
        Print("Order failed: ", result.retcode, " - ", result.comment);
        return false;
    }

    // Add to our tracking array
    PositionInfo pos;
    pos.ticket = result.deal;
    pos.entryPrice = result.price;
    pos.lotSize = lot;
    pos.highestPrice = SymbolInfoDouble(_Symbol, SYMBOL_BID);
    pos.trailStop = 0;
    pos.trailingActive = false;

    int size = ArraySize(g_positions);
    ArrayResize(g_positions, size + 1);
    g_positions[size] = pos;

    // Update lowest buy
    if(pos.entryPrice < g_lowestBuy)
        g_lowestBuy = pos.entryPrice;

    Print("Opened BUY ", lot, " lots at ", result.price,
          " | Positions: ", ArraySize(g_positions), "/", MaxPositions);

    return true;
}

//+------------------------------------------------------------------+
//| Close a position by ticket                                        |
//+------------------------------------------------------------------+
bool ClosePosition(ulong ticket)
{
    if(!PositionSelectByTicket(ticket))
    {
        Print("Position not found: ", ticket);
        return false;
    }

    MqlTradeRequest request = {};
    MqlTradeResult result = {};

    request.action = TRADE_ACTION_DEAL;
    request.symbol = _Symbol;
    request.volume = PositionGetDouble(POSITION_VOLUME);
    request.type = ORDER_TYPE_SELL;  // Close buy with sell
    request.price = SymbolInfoDouble(_Symbol, SYMBOL_BID);
    request.deviation = 10;
    request.magic = MagicNumber;
    request.position = ticket;
    request.comment = "Trail Stop";

    if(!OrderSend(request, result))
    {
        Print("Close error: ", GetLastError(), " - ", result.comment);
        return false;
    }

    if(result.retcode != TRADE_RETCODE_DONE)
    {
        Print("Close failed: ", result.retcode, " - ", result.comment);
        return false;
    }

    double profit = PositionGetDouble(POSITION_PROFIT);
    Print("Closed position ", ticket, " | Profit: $", DoubleToString(profit, 2));

    return true;
}

//+------------------------------------------------------------------+
//| Trade transaction handler (for position tracking)                 |
//+------------------------------------------------------------------+
void OnTradeTransaction(const MqlTradeTransaction& trans,
                        const MqlTradeRequest& request,
                        const MqlTradeResult& result)
{
    // Reload positions when trades happen externally
    if(trans.type == TRADE_TRANSACTION_DEAL_ADD)
    {
        // A deal was added - could be our order or external
        // Reload to stay in sync
        LoadExistingPositions();
    }
}

//+------------------------------------------------------------------+
//| Timer function (optional - for periodic checks)                   |
//+------------------------------------------------------------------+
void OnTimer()
{
    // Can be used for periodic position sync if needed
}

//+------------------------------------------------------------------+
//| Helper: Remove element from array                                 |
//+------------------------------------------------------------------+
template<typename T>
void ArrayRemove(T &array[], int index, int count = 1)
{
    int size = ArraySize(array);
    if(index < 0 || index >= size) return;

    for(int i = index; i < size - count; i++)
    {
        array[i] = array[i + count];
    }
    ArrayResize(array, size - count);
}
