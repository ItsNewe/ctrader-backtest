//+------------------------------------------------------------------+
//|                                       FillUp_V11_Bidirectional.mq5|
//|                            Fill-Up Grid Strategy V11             |
//|                 Bidirectional Trading + Inverse Volatility TP    |
//+------------------------------------------------------------------+
#property copyright "Fill-Up Strategy V11"
#property link      ""
#property version   "11.00"
#property strict

//+------------------------------------------------------------------+
//| Input Parameters                                                  |
//+------------------------------------------------------------------+
// Core Grid Parameters
input double   Spacing = 0.75;              // Grid Spacing (price units)
input double   LotSize = 0.01;              // Lot Size per position
input int      MaxLongPositions = 15;       // Max Long Positions
input int      MaxShortPositions = 15;      // Max Short Positions
input int      MagicNumber = 1111111;       // Magic Number

// Bidirectional Trading
input bool     EnableBidirectional = true;  // Enable Short Grid

// Inverse Volatility TP
input bool     EnableInverseVolTP = true;   // Enable Inverse Volatility TP
input double   TPBase = 2.0;                // TP Base Multiplier
input double   TPScale = 2.0;               // TP Scale Factor
input double   TPMin = 0.5;                 // TP Minimum Multiplier
input double   TPMax = 4.0;                 // TP Maximum Multiplier

// ATR Volatility Filter
input int      ATRShortPeriod = 50;         // ATR Short Period
input int      ATRLongPeriod = 1000;        // ATR Long Period
input double   VolatilityThreshold = 0.6;   // Volatility Threshold

// Mean Reversion Filter
input bool     EnableMeanReversion = true;  // Enable Mean Reversion Filter
input int      SMAPeriod = 500;             // SMA Period for Mean Reversion
input double   MRThresholdLong = -0.04;     // Mean Reversion Threshold Long (%)
input double   MRThresholdShort = 0.04;     // Mean Reversion Threshold Short (%)

// Session Filter
input bool     EnableSessionFilter = true;  // Enable Session Filter
input int      SessionAvoidStart = 14;      // Avoid Session Start Hour (UTC)
input int      SessionAvoidEnd = 18;        // Avoid Session End Hour (UTC)

// Protection Levels
input double   StopNewAtDD = 3.0;           // Stop New Positions at DD (%)
input double   PartialCloseAtDD = 5.0;      // Partial Close at DD (%)
input double   CloseAllAtDD = 15.0;         // Close All at DD (%)

// DD-based Lot Scaling
input bool     EnableDDLotScaling = true;   // Enable DD Lot Scaling
input double   LotScaleStartDD = 1.0;       // Start Scaling at DD (%)
input double   LotScaleMinFactor = 0.25;    // Minimum Lot Factor

//+------------------------------------------------------------------+
//| Global Variables                                                  |
//+------------------------------------------------------------------+
double PeakEquity;
bool PartialCloseDone;
bool AllClosed;

// ATR calculation buffers
double ATRShortBuffer[];
double ATRLongBuffer[];
int ATRShortHandle;
int ATRLongHandle;

// SMA buffer
double SMABuffer[];
int SMAHandle;

//+------------------------------------------------------------------+
//| Expert initialization function                                   |
//+------------------------------------------------------------------+
int OnInit()
{
    // Initialize peak equity
    PeakEquity = AccountInfoDouble(ACCOUNT_EQUITY);
    PartialCloseDone = false;
    AllClosed = false;

    // Create ATR indicators
    ATRShortHandle = iATR(_Symbol, PERIOD_CURRENT, ATRShortPeriod);
    ATRLongHandle = iATR(_Symbol, PERIOD_CURRENT, ATRLongPeriod);

    // Create SMA indicator
    SMAHandle = iMA(_Symbol, PERIOD_CURRENT, SMAPeriod, 0, MODE_SMA, PRICE_CLOSE);

    if(ATRShortHandle == INVALID_HANDLE || ATRLongHandle == INVALID_HANDLE || SMAHandle == INVALID_HANDLE)
    {
        Print("Error creating indicators");
        return INIT_FAILED;
    }

    Print("FillUp V11 Bidirectional initialized");
    Print("Settings: Spacing=", Spacing, " MaxLong=", MaxLongPositions, " MaxShort=", MaxShortPositions);
    Print("Bidirectional: ", EnableBidirectional ? "ON" : "OFF");
    Print("Inverse Vol TP: ", EnableInverseVolTP ? "ON" : "OFF");

    return INIT_SUCCEEDED;
}

//+------------------------------------------------------------------+
//| Expert deinitialization function                                 |
//+------------------------------------------------------------------+
void OnDeinit(const int reason)
{
    if(ATRShortHandle != INVALID_HANDLE) IndicatorRelease(ATRShortHandle);
    if(ATRLongHandle != INVALID_HANDLE) IndicatorRelease(ATRLongHandle);
    if(SMAHandle != INVALID_HANDLE) IndicatorRelease(SMAHandle);
}

//+------------------------------------------------------------------+
//| Get ATR values                                                    |
//+------------------------------------------------------------------+
double GetATRShort()
{
    double buffer[];
    ArraySetAsSeries(buffer, true);
    if(CopyBuffer(ATRShortHandle, 0, 0, 1, buffer) > 0)
        return buffer[0];
    return 0;
}

double GetATRLong()
{
    double buffer[];
    ArraySetAsSeries(buffer, true);
    if(CopyBuffer(ATRLongHandle, 0, 0, 1, buffer) > 0)
        return buffer[0];
    return 0;
}

double GetSMA()
{
    double buffer[];
    ArraySetAsSeries(buffer, true);
    if(CopyBuffer(SMAHandle, 0, 0, 1, buffer) > 0)
        return buffer[0];
    return 0;
}

//+------------------------------------------------------------------+
//| Check volatility filter                                          |
//+------------------------------------------------------------------+
bool IsVolatilityLow()
{
    double atrShort = GetATRShort();
    double atrLong = GetATRLong();

    if(atrLong <= 0) return true;

    return atrShort < atrLong * VolatilityThreshold;
}

//+------------------------------------------------------------------+
//| Check session filter                                             |
//+------------------------------------------------------------------+
bool IsSessionAllowed()
{
    if(!EnableSessionFilter) return true;

    MqlDateTime dt;
    TimeToStruct(TimeGMT(), dt);

    if(dt.hour >= SessionAvoidStart && dt.hour < SessionAvoidEnd)
        return false;

    return true;
}

//+------------------------------------------------------------------+
//| Calculate price deviation from SMA                               |
//+------------------------------------------------------------------+
double GetPriceDeviation()
{
    double sma = GetSMA();
    if(sma <= 0) return 0;

    double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
    return (bid - sma) / sma * 100.0;
}

//+------------------------------------------------------------------+
//| Check mean reversion filter for long                             |
//+------------------------------------------------------------------+
bool IsMeanReversionLongOK()
{
    if(!EnableMeanReversion) return true;

    double deviation = GetPriceDeviation();
    return deviation < MRThresholdLong;
}

//+------------------------------------------------------------------+
//| Check mean reversion filter for short                            |
//+------------------------------------------------------------------+
bool IsMeanReversionShortOK()
{
    if(!EnableMeanReversion) return true;

    double deviation = GetPriceDeviation();
    return deviation > MRThresholdShort;
}

//+------------------------------------------------------------------+
//| Calculate TP multiplier based on volatility                      |
//+------------------------------------------------------------------+
double GetTPMultiplier()
{
    if(!EnableInverseVolTP) return TPBase;

    double atrShort = GetATRShort();
    double atrLong = GetATRLong();

    if(atrLong <= 0) return TPBase;

    double volRatio = atrShort / atrLong;

    // Inverse: Low vol = wider TP, High vol = tighter TP
    double mult = TPBase + TPScale * (1.0 - volRatio);

    // Clamp to limits
    if(mult < TPMin) mult = TPMin;
    if(mult > TPMax) mult = TPMax;

    return mult;
}

//+------------------------------------------------------------------+
//| Calculate DD-based lot scale                                     |
//+------------------------------------------------------------------+
double CalculateLotScale(double ddPct)
{
    if(!EnableDDLotScaling) return 1.0;

    if(ddPct <= LotScaleStartDD) return 1.0;
    if(ddPct >= StopNewAtDD) return LotScaleMinFactor;

    double ddRange = StopNewAtDD - LotScaleStartDD;
    double scaleRange = 1.0 - LotScaleMinFactor;
    double ddProgress = (ddPct - LotScaleStartDD) / ddRange;

    return 1.0 - (ddProgress * scaleRange);
}

//+------------------------------------------------------------------+
//| Count positions by direction                                     |
//+------------------------------------------------------------------+
void CountPositions(int& longCount, int& shortCount,
                   double& lowestLong, double& highestLong,
                   double& lowestShort, double& highestShort)
{
    longCount = 0;
    shortCount = 0;
    lowestLong = DBL_MAX;
    highestLong = -DBL_MAX;
    lowestShort = DBL_MAX;
    highestShort = -DBL_MAX;

    for(int i = PositionsTotal() - 1; i >= 0; i--)
    {
        ulong ticket = PositionGetTicket(i);
        if(ticket <= 0) continue;

        if(PositionGetString(POSITION_SYMBOL) != _Symbol) continue;
        if(PositionGetInteger(POSITION_MAGIC) != MagicNumber) continue;

        double openPrice = PositionGetDouble(POSITION_PRICE_OPEN);

        if(PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY)
        {
            longCount++;
            if(openPrice < lowestLong) lowestLong = openPrice;
            if(openPrice > highestLong) highestLong = openPrice;
        }
        else if(PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_SELL)
        {
            shortCount++;
            if(openPrice < lowestShort) lowestShort = openPrice;
            if(openPrice > highestShort) highestShort = openPrice;
        }
    }
}

//+------------------------------------------------------------------+
//| Close all positions                                              |
//+------------------------------------------------------------------+
void CloseAllPositions()
{
    for(int i = PositionsTotal() - 1; i >= 0; i--)
    {
        ulong ticket = PositionGetTicket(i);
        if(ticket <= 0) continue;

        if(PositionGetString(POSITION_SYMBOL) != _Symbol) continue;
        if(PositionGetInteger(POSITION_MAGIC) != MagicNumber) continue;

        MqlTradeRequest request;
        MqlTradeResult result;
        ZeroMemory(request);
        ZeroMemory(result);

        request.action = TRADE_ACTION_DEAL;
        request.symbol = _Symbol;
        request.volume = PositionGetDouble(POSITION_VOLUME);
        request.deviation = 10;
        request.magic = MagicNumber;
        request.position = ticket;

        if(PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY)
        {
            request.type = ORDER_TYPE_SELL;
            request.price = SymbolInfoDouble(_Symbol, SYMBOL_BID);
        }
        else
        {
            request.type = ORDER_TYPE_BUY;
            request.price = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
        }

        OrderSend(request, result);
    }
}

//+------------------------------------------------------------------+
//| Close worst positions (partial close)                            |
//+------------------------------------------------------------------+
void CloseWorstPositions(double pct)
{
    int totalPositions = 0;
    for(int i = PositionsTotal() - 1; i >= 0; i--)
    {
        ulong ticket = PositionGetTicket(i);
        if(ticket <= 0) continue;
        if(PositionGetString(POSITION_SYMBOL) != _Symbol) continue;
        if(PositionGetInteger(POSITION_MAGIC) != MagicNumber) continue;
        totalPositions++;
    }

    if(totalPositions <= 1) return;

    int toClose = (int)MathMax(1, totalPositions * pct);

    // Get all positions with P/L
    struct PosInfo
    {
        ulong ticket;
        double profit;
    };

    PosInfo positions[];
    ArrayResize(positions, 0);

    for(int i = PositionsTotal() - 1; i >= 0; i--)
    {
        ulong ticket = PositionGetTicket(i);
        if(ticket <= 0) continue;
        if(PositionGetString(POSITION_SYMBOL) != _Symbol) continue;
        if(PositionGetInteger(POSITION_MAGIC) != MagicNumber) continue;

        PosInfo info;
        info.ticket = ticket;
        info.profit = PositionGetDouble(POSITION_PROFIT);

        int size = ArraySize(positions);
        ArrayResize(positions, size + 1);
        positions[size] = info;
    }

    // Sort by profit (ascending - worst first)
    for(int i = 0; i < ArraySize(positions) - 1; i++)
    {
        for(int j = i + 1; j < ArraySize(positions); j++)
        {
            if(positions[j].profit < positions[i].profit)
            {
                PosInfo temp = positions[i];
                positions[i] = positions[j];
                positions[j] = temp;
            }
        }
    }

    // Close worst positions
    for(int i = 0; i < toClose && i < ArraySize(positions); i++)
    {
        PositionSelectByTicket(positions[i].ticket);

        MqlTradeRequest request;
        MqlTradeResult result;
        ZeroMemory(request);
        ZeroMemory(result);

        request.action = TRADE_ACTION_DEAL;
        request.symbol = _Symbol;
        request.volume = PositionGetDouble(POSITION_VOLUME);
        request.deviation = 10;
        request.magic = MagicNumber;
        request.position = positions[i].ticket;

        if(PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY)
        {
            request.type = ORDER_TYPE_SELL;
            request.price = SymbolInfoDouble(_Symbol, SYMBOL_BID);
        }
        else
        {
            request.type = ORDER_TYPE_BUY;
            request.price = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
        }

        OrderSend(request, result);
    }
}

//+------------------------------------------------------------------+
//| Open long position                                               |
//+------------------------------------------------------------------+
bool OpenLong(double lots)
{
    double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    double spread = SymbolInfoDouble(_Symbol, SYMBOL_ASK) - SymbolInfoDouble(_Symbol, SYMBOL_BID);
    double tpMult = GetTPMultiplier();
    double tp = ask + spread + Spacing * tpMult;

    MqlTradeRequest request;
    MqlTradeResult result;
    ZeroMemory(request);
    ZeroMemory(result);

    request.action = TRADE_ACTION_DEAL;
    request.symbol = _Symbol;
    request.volume = lots;
    request.type = ORDER_TYPE_BUY;
    request.price = ask;
    request.tp = tp;
    request.deviation = 10;
    request.magic = MagicNumber;
    request.comment = "V11 Long";

    return OrderSend(request, result);
}

//+------------------------------------------------------------------+
//| Open short position                                              |
//+------------------------------------------------------------------+
bool OpenShort(double lots)
{
    double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
    double spread = SymbolInfoDouble(_Symbol, SYMBOL_ASK) - SymbolInfoDouble(_Symbol, SYMBOL_BID);
    double tpMult = GetTPMultiplier();
    double tp = bid - spread - Spacing * tpMult;

    MqlTradeRequest request;
    MqlTradeResult result;
    ZeroMemory(request);
    ZeroMemory(result);

    request.action = TRADE_ACTION_DEAL;
    request.symbol = _Symbol;
    request.volume = lots;
    request.type = ORDER_TYPE_SELL;
    request.price = bid;
    request.tp = tp;
    request.deviation = 10;
    request.magic = MagicNumber;
    request.comment = "V11 Short";

    return OrderSend(request, result);
}

//+------------------------------------------------------------------+
//| Expert tick function                                             |
//+------------------------------------------------------------------+
void OnTick()
{
    double equity = AccountInfoDouble(ACCOUNT_EQUITY);
    double balance = AccountInfoDouble(ACCOUNT_BALANCE);

    // Count positions
    int longCount, shortCount;
    double lowestLong, highestLong, lowestShort, highestShort;
    CountPositions(longCount, shortCount, lowestLong, highestLong, lowestShort, highestShort);

    // Reset peak equity when no positions
    if(longCount == 0 && shortCount == 0)
    {
        if(PeakEquity != balance)
        {
            PeakEquity = balance;
            PartialCloseDone = false;
            AllClosed = false;
        }
    }

    // Track peak equity
    if(equity > PeakEquity)
    {
        PeakEquity = equity;
        PartialCloseDone = false;
        AllClosed = false;
    }

    // Calculate drawdown
    double ddPct = 0;
    if(PeakEquity > 0)
        ddPct = (PeakEquity - equity) / PeakEquity * 100.0;

    // Protection: Close ALL
    if(ddPct > CloseAllAtDD && !AllClosed && (longCount > 0 || shortCount > 0))
    {
        Print("V11: Emergency close at DD=", ddPct, "%");
        CloseAllPositions();
        AllClosed = true;
        PeakEquity = AccountInfoDouble(ACCOUNT_BALANCE);
        return;
    }

    // Protection: Partial close
    if(ddPct > PartialCloseAtDD && !PartialCloseDone && (longCount > 1 || shortCount > 1))
    {
        Print("V11: Partial close at DD=", ddPct, "%");
        CloseWorstPositions(0.5);
        PartialCloseDone = true;
    }

    // Check filters
    bool volatilityOK = IsVolatilityLow();
    bool sessionOK = IsSessionAllowed();

    // Don't open new positions if filters fail or DD too high
    if(ddPct >= StopNewAtDD || !volatilityOK || !sessionOK)
        return;

    // Calculate lot size with DD scaling
    double ddScale = CalculateLotScale(ddPct);
    double lots = NormalizeDouble(LotSize * ddScale, 2);
    if(lots < 0.01) lots = 0.01;

    double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);

    // === LONG GRID ===
    bool mrLongOK = IsMeanReversionLongOK();

    if(mrLongOK && longCount < MaxLongPositions)
    {
        bool shouldOpenLong = false;

        if(longCount == 0)
        {
            shouldOpenLong = true;
        }
        else if(lowestLong >= ask + Spacing)
        {
            // Price dropped - buy lower
            shouldOpenLong = true;
        }
        else if(highestLong <= ask - Spacing)
        {
            // Price rose - fill gap
            shouldOpenLong = true;
        }

        if(shouldOpenLong)
        {
            OpenLong(lots);
        }
    }

    // === SHORT GRID ===
    if(EnableBidirectional)
    {
        bool mrShortOK = IsMeanReversionShortOK();

        if(mrShortOK && shortCount < MaxShortPositions)
        {
            bool shouldOpenShort = false;

            if(shortCount == 0)
            {
                shouldOpenShort = true;
            }
            else if(highestShort <= bid - Spacing)
            {
                // Price rose - sell higher
                shouldOpenShort = true;
            }
            else if(lowestShort >= bid + Spacing)
            {
                // Price dropped - fill gap
                shouldOpenShort = true;
            }

            if(shouldOpenShort)
            {
                OpenShort(lots);
            }
        }
    }
}
//+------------------------------------------------------------------+
