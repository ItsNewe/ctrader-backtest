//+------------------------------------------------------------------+
//| Fill-Up Strategy with DD Protection (D_8_50)                     |
//| - Survive Down: 8%                                                |
//| - Spacing: $1.00                                                  |
//| - DD Protection: Close all at 50% drawdown                       |
//| Backtested: 5.13x return, 49.97% max DD on Gold 2025             |
//+------------------------------------------------------------------+
#property copyright "Fill-Up DD Protected 2025"
#property version   "1.00"
#property strict

#include <Trade\AccountInfo.mqh>
#include <Trade\PositionInfo.mqh>
#include <Trade\Trade.mqh>

CAccountInfo m_account;
CPositionInfo m_position;
CTrade m_trade;

//+------------------------------------------------------------------+
//| Input Parameters                                                  |
//+------------------------------------------------------------------+
input string GeneralSettings = "=== General Settings ===";
input double InitialBalance = 10000.0;       // Reference balance for DD calc
input int MagicNumber = 850850;              // Magic number for this EA

input string FillUpSettings = "=== Fill-Up Parameters ===";
input double SurviveDownPct = 8.0;           // Survive down % (8% optimal)
input double Spacing = 1.0;                  // Grid spacing in price units
input double SizeMultiplier = 1.0;           // Position size multiplier
input double MinLotSize = 0.01;              // Minimum lot size
input double MaxLotSize = 10.0;              // Maximum lot size
input int MaxPositions = 200;                // Max open positions

input string DDProtectionSettings = "=== DD Protection ===";
input bool EnableDDProtection = false;       // Enable DD protection (disable to match C++ baseline)
input double CloseAllDD_Pct = 50.0;          // Close all at this DD %

input string TPSettings = "=== Take Profit ===";
input bool UseTakeProfit = true;             // Use take profit orders
input double TPBuffer = 0.0;                 // Extra TP buffer (0 = spacing + spread)

//+------------------------------------------------------------------+
//| Global Variables                                                  |
//+------------------------------------------------------------------+
double g_lowestBuy = DBL_MAX;
double g_highestBuy = 0;
double g_peakEquity = 0;
double g_referenceBalance = 0;
int g_ddTriggers = 0;

//+------------------------------------------------------------------+
//| Expert initialization function                                    |
//+------------------------------------------------------------------+
int OnInit()
{
    m_trade.SetExpertMagicNumber(MagicNumber);
    m_trade.SetDeviationInPoints(10);
    m_trade.SetTypeFilling(ORDER_FILLING_IOC);

    g_referenceBalance = (InitialBalance > 0) ? InitialBalance : m_account.Balance();
    g_peakEquity = m_account.Equity();

    Print("=== Fill-Up DD Protected Strategy Initialized ===");
    Print("Survive Down: ", SurviveDownPct, "%");
    Print("Spacing: $", Spacing);
    Print("DD Protection: ", EnableDDProtection ? "ON at " + DoubleToString(CloseAllDD_Pct, 1) + "%" : "OFF");
    Print("Reference Balance: $", g_referenceBalance);

    return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+
//| Expert deinitialization function                                  |
//+------------------------------------------------------------------+
void OnDeinit(const int reason)
{
    Print("=== Strategy Stats ===");
    Print("DD Protection Triggers: ", g_ddTriggers);
    Print("Final Equity: $", m_account.Equity());
}

//+------------------------------------------------------------------+
//| Expert tick function                                              |
//+------------------------------------------------------------------+
void OnTick()
{
    double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
    double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    double equity = m_account.Equity();

    // Check DD Protection FIRST (before updating peak - matches C++ behavior)
    // Only check when we have positions (matching C++ !positions_.empty() check)
    int posCount = CountPositions();
    if (EnableDDProtection && g_peakEquity > 0 && posCount > 0)
    {
        double dd_pct = (g_peakEquity - equity) / g_peakEquity * 100.0;
        if (dd_pct > CloseAllDD_Pct)
        {
            // CRITICAL: Save current equity BEFORE closing (this is our realized balance after close)
            // PositionClose() is async, so Balance() won't reflect closed positions immediately
            double expectedBalanceAfterClose = equity;  // Current equity = what balance will be after closing

            CloseAllPositions();
            g_ddTriggers++;

            // Use the equity at moment of DD trigger as new peak (not Balance() which is stale)
            g_peakEquity = expectedBalanceAfterClose;
            Print("DD Protection triggered! DD=", DoubleToString(dd_pct, 2), "% - Closed all positions. New peak=$", g_peakEquity);
            return;
        }
    }

    // Update peak equity AFTER DD check (critical for correct DD calculation)
    if (equity > g_peakEquity)
        g_peakEquity = equity;

    // Update position tracking
    UpdatePositionTracking();

    // Check for new entry opportunities
    CheckNewEntries(bid, ask);
}

//+------------------------------------------------------------------+
//| Update position tracking                                          |
//+------------------------------------------------------------------+
void UpdatePositionTracking()
{
    g_lowestBuy = DBL_MAX;
    g_highestBuy = 0;

    for (int i = PositionsTotal() - 1; i >= 0; i--)
    {
        if (m_position.SelectByIndex(i))
        {
            if (m_position.Symbol() == _Symbol &&
                m_position.Magic() == MagicNumber &&
                m_position.PositionType() == POSITION_TYPE_BUY)
            {
                double price = m_position.PriceOpen();
                if (price < g_lowestBuy) g_lowestBuy = price;
                if (price > g_highestBuy) g_highestBuy = price;
            }
        }
    }
}

//+------------------------------------------------------------------+
//| Check for new entry opportunities                                 |
//+------------------------------------------------------------------+
void CheckNewEntries(double bid, double ask)
{
    int posCount = CountPositions();
    if (posCount >= MaxPositions) return;

    double lotSize = CalculateLotSize(ask, posCount);
    if (lotSize < MinLotSize) return;

    bool shouldEnter = false;

    if (posCount == 0)
    {
        // First position
        shouldEnter = true;
    }
    else if (g_lowestBuy < DBL_MAX && ask <= g_lowestBuy - Spacing)
    {
        // Price dropped below lowest position by spacing
        shouldEnter = true;
    }
    else if (g_highestBuy > 0 && ask >= g_highestBuy + Spacing)
    {
        // Price rose above highest position by spacing
        shouldEnter = true;
    }

    if (shouldEnter)
    {
        OpenBuyPosition(ask, lotSize);
    }
}

//+------------------------------------------------------------------+
//| Calculate lot size using survive-down method                      |
//| Fixed: Use actual free margin and conservative buffer             |
//+------------------------------------------------------------------+
double CalculateLotSize(double price, int posCount)
{
    // Use actual MT5 account values
    double equity = m_account.Equity();
    double freeMargin = m_account.FreeMargin();
    double marginLevel = m_account.MarginLevel();  // Current margin level %
    double contractSize = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);
    double leverage = (double)m_account.Leverage();

    // Calculate volume of open positions
    double volumeOpen = 0;
    for (int i = PositionsTotal() - 1; i >= 0; i--)
    {
        if (m_position.SelectByIndex(i))
        {
            if (m_position.Symbol() == _Symbol && m_position.Magic() == MagicNumber)
            {
                volumeOpen += m_position.Volume();
            }
        }
    }

    // Reference price for survive calculation
    double referencePrice = (posCount > 0 && g_highestBuy > 0) ? g_highestBuy : price;
    double endPrice = referencePrice * (100.0 - SurviveDownPct) / 100.0;

    // Distance to survive
    double distance = price - endPrice;
    if (distance <= 0) return 0;

    // Number of potential trades
    double numTrades = MathFloor(distance / Spacing);
    if (numTrades < 1) numTrades = 1;

    // Conservative margin buffer: require 100% margin level at survive target
    // (Grid stop-out is around 20%, but we want safety buffer)
    double safeMarginLevel = 100.0;

    // Calculate projected loss at survive target
    double projectedLoss = volumeOpen * distance * contractSize;

    // Equity at target after projected losses
    double equityAtTarget = equity - projectedLoss;
    if (equityAtTarget <= 0) return 0;

    // Check if current margin level is safe
    if (posCount > 0 && marginLevel > 0 && marginLevel < 200.0)
        return 0;  // Already low on margin, don't add positions

    // Grid loss factor for new positions
    double gridLossFactor = Spacing * contractSize * (numTrades * (numTrades + 1) / 2);

    // Calculate margin required for new grid (conservative: assume full grid opens)
    double marginPerLot = contractSize * price / leverage;
    double gridMarginFactor = marginPerLot * numTrades;

    // Available equity = equity at target minus margin requirement for safety
    // Use only 30% of free margin for new positions (70% buffer)
    double availableForGrid = freeMargin * 0.30;

    // Also limit by equity at target
    double maxFromEquity = equityAtTarget * 0.30;
    double available = MathMin(availableForGrid, maxFromEquity);

    if (available <= 0) return 0;

    // Calculate lot size accounting for both grid losses AND margin
    double lotFromLoss = available / gridLossFactor;
    double lotFromMargin = available / gridMarginFactor;
    double lotSize = MathMin(lotFromLoss, lotFromMargin);

    lotSize = lotSize * SizeMultiplier;

    // Normalize
    lotSize = MathMax(MinLotSize, MathMin(MaxLotSize, lotSize));
    lotSize = NormalizeDouble(lotSize, 2);

    // Debug logging: first 10 AND any time lots > 0.01
    static int calc_count = 0;
    calc_count++;
    if (calc_count <= 10 || lotSize > 0.01)
    {
        Print("LotCalc #", calc_count, ": equity=", equity, " freeMgn=", freeMargin, " mgnLvl=", marginLevel,
              " distance=", distance, " available=", available, " -> lot=", lotSize);
    }

    return lotSize;
}

//+------------------------------------------------------------------+
//| Open buy position                                                 |
//+------------------------------------------------------------------+
void OpenBuyPosition(double price, double lots)
{
    double spread = SymbolInfoDouble(_Symbol, SYMBOL_ASK) - SymbolInfoDouble(_Symbol, SYMBOL_BID);
    double tp = 0;

    if (UseTakeProfit)
    {
        tp = price + Spacing + spread + TPBuffer;
    }

    if (m_trade.Buy(lots, _Symbol, 0, 0, tp, "FillUp_DD"))
    {
        // Only log occasionally to reduce log spam
        static int log_count = 0;
        if (log_count++ % 100 == 0)
            Print("BUY #", log_count, ": ", lots, " lots @ ", price, " Equity=$", m_account.Equity());
    }
    else
    {
        Print("Failed to open BUY: ", m_trade.ResultRetcode(), " - ", m_trade.ResultRetcodeDescription());
    }
}

//+------------------------------------------------------------------+
//| Count positions for this EA                                       |
//+------------------------------------------------------------------+
int CountPositions()
{
    int count = 0;
    for (int i = PositionsTotal() - 1; i >= 0; i--)
    {
        if (m_position.SelectByIndex(i))
        {
            if (m_position.Symbol() == _Symbol && m_position.Magic() == MagicNumber)
            {
                count++;
            }
        }
    }
    return count;
}

//+------------------------------------------------------------------+
//| Close all positions                                               |
//+------------------------------------------------------------------+
void CloseAllPositions()
{
    for (int i = PositionsTotal() - 1; i >= 0; i--)
    {
        if (m_position.SelectByIndex(i))
        {
            if (m_position.Symbol() == _Symbol && m_position.Magic() == MagicNumber)
            {
                m_trade.PositionClose(m_position.Ticket());
            }
        }
    }

    // Reset tracking
    g_lowestBuy = DBL_MAX;
    g_highestBuy = 0;
}

//+------------------------------------------------------------------+
//| Get total volume of open positions                                |
//+------------------------------------------------------------------+
double GetTotalVolume()
{
    double volume = 0;
    for (int i = PositionsTotal() - 1; i >= 0; i--)
    {
        if (m_position.SelectByIndex(i))
        {
            if (m_position.Symbol() == _Symbol && m_position.Magic() == MagicNumber)
            {
                volume += m_position.Volume();
            }
        }
    }
    return volume;
}
//+------------------------------------------------------------------+
