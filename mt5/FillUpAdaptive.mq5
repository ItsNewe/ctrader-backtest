//+------------------------------------------------------------------+
//|                                              FillUpAdaptive.mq5  |
//|                                    Oscillation-Stabilized Grid   |
//|                                         Adaptive Spacing Mode    |
//+------------------------------------------------------------------+
#property copyright "Backtest Framework"
#property link      ""
#property version   "1.00"
#property strict

//+------------------------------------------------------------------+
//| Input Parameters                                                  |
//+------------------------------------------------------------------+
input group "=== Core Strategy Parameters ==="
input double SurvivePct = 13.0;           // Survive % (max price drop before margin call)
input double BaseSpacing = 1.0;           // Base grid spacing in price units ($)
input double MinVolume = 0.01;            // Minimum lot size
input double MaxVolume = 10.0;            // Maximum lot size

input group "=== Adaptive Spacing Parameters ==="
input double VolatilityLookbackHours = 1.0;  // Lookback period for volatility (hours)
input double TypicalVolatility = 10.0;       // Typical volatility range ($) for normalization
input double MinSpacingMult = 0.5;           // Minimum spacing multiplier
input double MaxSpacingMult = 3.0;           // Maximum spacing multiplier

input group "=== Risk Management ==="
input int MagicNumber = 123456;           // Magic number for this EA

//+------------------------------------------------------------------+
//| Global Variables                                                  |
//+------------------------------------------------------------------+
double g_currentSpacing;
double g_recentHigh;
double g_recentLow;
datetime g_lastVolatilityReset;
int g_spacingChanges;

double g_contractSize;
double g_leverage;
int g_digits;
double g_point;

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
    g_spacingChanges = 0;

    Print("=== FillUp Adaptive Spacing EA Initialized ===");
    Print("Symbol: ", _Symbol);
    Print("Contract Size: ", g_contractSize);
    Print("Leverage: 1:", g_leverage);
    Print("Survive %: ", SurvivePct);
    Print("Base Spacing: $", BaseSpacing);
    Print("Volatility Lookback: ", VolatilityLookbackHours, " hours");

    return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+
//| Expert deinitialization function                                  |
//+------------------------------------------------------------------+
void OnDeinit(const int reason)
{
    Print("=== EA Stopped ===");
    Print("Total spacing changes: ", g_spacingChanges);
}

//+------------------------------------------------------------------+
//| Expert tick function                                              |
//+------------------------------------------------------------------+
void OnTick()
{
    // Update volatility tracking
    UpdateVolatility();

    // Update adaptive spacing
    UpdateAdaptiveSpacing();

    // Get current market state
    double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
    double spread = ask - bid;

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

    // Determine if we should open a new position
    bool shouldOpen = false;

    if(positionCount == 0)
    {
        // No positions - open first one
        shouldOpen = true;
    }
    else
    {
        // Check if price moved enough from existing positions
        if(lowestBuy >= ask + g_currentSpacing)
        {
            // Price dropped - open new position below
            shouldOpen = true;
        }
        else if(highestBuy <= ask - g_currentSpacing)
        {
            // Price rose - open new position above
            shouldOpen = true;
        }
    }

    if(shouldOpen)
    {
        double lots = CalculateLotSize(ask, positionCount, totalVolume, highestBuy);
        if(lots >= MinVolume)
        {
            OpenBuyOrder(lots, ask, spread);
        }
    }
}

//+------------------------------------------------------------------+
//| Update volatility tracking                                        |
//+------------------------------------------------------------------+
void UpdateVolatility()
{
    double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
    datetime currentTime = TimeCurrent();

    // Reset high/low every lookback period
    int lookbackSeconds = (int)(VolatilityLookbackHours * 3600);

    if(g_lastVolatilityReset == 0 ||
       currentTime - g_lastVolatilityReset >= lookbackSeconds)
    {
        g_recentHigh = bid;
        g_recentLow = bid;
        g_lastVolatilityReset = currentTime;
    }

    // Track high/low
    g_recentHigh = MathMax(g_recentHigh, bid);
    g_recentLow = MathMin(g_recentLow, bid);
}

//+------------------------------------------------------------------+
//| Update adaptive spacing based on volatility                       |
//+------------------------------------------------------------------+
void UpdateAdaptiveSpacing()
{
    double range = g_recentHigh - g_recentLow;

    if(range > 0 && g_recentHigh > 0)
    {
        // Volatility ratio: current range vs typical
        double volRatio = range / TypicalVolatility;
        volRatio = MathMax(MinSpacingMult, MathMin(MaxSpacingMult, volRatio));

        double newSpacing = BaseSpacing * volRatio;
        newSpacing = MathMax(0.5, MathMin(5.0, newSpacing));  // Clamp $0.50 to $5

        if(MathAbs(newSpacing - g_currentSpacing) > 0.1)
        {
            g_currentSpacing = newSpacing;
            g_spacingChanges++;

            // Log spacing change
            Print("Spacing adjusted to $", DoubleToString(g_currentSpacing, 2),
                  " (range=$", DoubleToString(range, 2),
                  ", ratio=", DoubleToString(volRatio, 2), ")");
        }
    }
}

//+------------------------------------------------------------------+
//| Calculate lot size based on survive percentage                    |
//+------------------------------------------------------------------+
double CalculateLotSize(double currentAsk, int positionsTotal,
                        double volumeOfOpenTrades, double highestBuy)
{
    double equity = AccountInfoDouble(ACCOUNT_EQUITY);
    double balance = AccountInfoDouble(ACCOUNT_BALANCE);

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

    // Calculate end price (worst case)
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

    // Check if already at margin limit
    double marginStopOut = 20.0;  // 20% stop-out level
    if(usedMargin > 0 && (equityAtTarget / usedMargin * 100.0) < marginStopOut)
    {
        return 0.0;
    }

    // Calculate lot size
    double tradeSize = MinVolume;
    double dEquity = g_contractSize * tradeSize * g_currentSpacing *
                     (numberOfTrades * (numberOfTrades + 1) / 2);
    double dMargin = numberOfTrades * tradeSize * g_contractSize / g_leverage;

    // Find maximum multiplier that maintains margin safety
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
//| Open a buy order with TP                                          |
//+------------------------------------------------------------------+
bool OpenBuyOrder(double lots, double ask, double spread)
{
    // Calculate TP: spread + spacing above entry
    double tp = ask + spread + g_currentSpacing;
    tp = NormalizeDouble(tp, g_digits);

    MqlTradeRequest request = {};
    MqlTradeResult result = {};

    request.action = TRADE_ACTION_DEAL;
    request.symbol = _Symbol;
    request.volume = lots;
    request.type = ORDER_TYPE_BUY;
    request.price = ask;
    request.sl = 0;  // No stop loss - strategy relies on oscillation recovery
    request.tp = tp;
    request.deviation = 10;
    request.magic = MagicNumber;
    request.comment = "FillUp Adaptive";
    request.type_filling = ORDER_FILLING_IOC;

    if(!OrderSend(request, result))
    {
        Print("OrderSend failed: ", GetLastError(),
              " RetCode: ", result.retcode);
        return false;
    }

    if(result.retcode == TRADE_RETCODE_DONE || result.retcode == TRADE_RETCODE_PLACED)
    {
        Print("BUY ", DoubleToString(lots, 2), " @ ", DoubleToString(ask, g_digits),
              " TP: ", DoubleToString(tp, g_digits),
              " Spacing: $", DoubleToString(g_currentSpacing, 2));
        return true;
    }

    return false;
}

//+------------------------------------------------------------------+
//| Get total open positions for this EA                              |
//+------------------------------------------------------------------+
int GetOpenPositionCount()
{
    int count = 0;
    for(int i = PositionsTotal() - 1; i >= 0; i--)
    {
        ulong ticket = PositionGetTicket(i);
        if(ticket > 0)
        {
            if(PositionGetString(POSITION_SYMBOL) == _Symbol &&
               PositionGetInteger(POSITION_MAGIC) == MagicNumber)
            {
                count++;
            }
        }
    }
    return count;
}

//+------------------------------------------------------------------+
//| Calculate current unrealized P/L                                  |
//+------------------------------------------------------------------+
double GetUnrealizedPL()
{
    double pl = 0;
    for(int i = PositionsTotal() - 1; i >= 0; i--)
    {
        ulong ticket = PositionGetTicket(i);
        if(ticket > 0)
        {
            if(PositionGetString(POSITION_SYMBOL) == _Symbol &&
               PositionGetInteger(POSITION_MAGIC) == MagicNumber)
            {
                pl += PositionGetDouble(POSITION_PROFIT);
            }
        }
    }
    return pl;
}
//+------------------------------------------------------------------+
