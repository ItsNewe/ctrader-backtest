//+------------------------------------------------------------------+
//| Anti-Fragile Strategy                                            |
//| Optimized for NAS100                                             |
//| Expected: ~1.1x return, ~12% max DD                              |
//+------------------------------------------------------------------+
#property copyright "Strategy Exploration 2025"
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
// Anti-fragile sizing
input double BaseLotSize = 0.01;           // Base lot size
input double MaxLotSize = 1.0;             // Maximum lot size
input double EntrySpacing = 50.0;          // Entry spacing (points for NAS100)
input double SizingExponent = 1.5;         // Sizing exponent (higher = more aggressive on dips)
input int MaxPositions = 20;               // Maximum positions

// Profit taking
input double TakeProfitPct = 2.0;          // Take profit (% of avg entry)
input double PartialTakePct = 0.5;         // Partial close percentage

// Risk management
input double MaxEquityRisk = 0.3;          // Max equity at risk

//+------------------------------------------------------------------+
//| Global Variables                                                  |
//+------------------------------------------------------------------+
double g_allTimeHigh = 0;
double g_lastEntryPrice = 0;
int g_currentStressLevel = 0;

// Statistics
int g_totalEntries = 0;
int g_profitTakes = 0;
double g_maxStressLevel = 0;

//+------------------------------------------------------------------+
//| Expert initialization function                                    |
//+------------------------------------------------------------------+
int OnInit() {
    m_trade.LogLevel(LOG_LEVEL_ERRORS);
    m_trade.SetAsyncMode(false);

    // Initialize ATH from current price
    g_allTimeHigh = SymbolInfoDouble(_Symbol, SYMBOL_ASK);

    // Scan existing positions
    g_lastEntryPrice = 0;
    for(int i = PositionsTotal() - 1; i >= 0; i--) {
        if(PositionSelectByTicket(PositionGetTicket(i))) {
            if(PositionGetString(POSITION_SYMBOL) == _Symbol &&
               PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY) {
                double openPrice = PositionGetDouble(POSITION_PRICE_OPEN);
                if(openPrice > g_lastEntryPrice || g_lastEntryPrice == 0) {
                    // For anti-fragile, we track the lowest entry (where we add most)
                    if(g_lastEntryPrice == 0 || openPrice < g_lastEntryPrice) {
                        g_lastEntryPrice = openPrice;
                    }
                }
                // Update ATH if needed
                if(openPrice > g_allTimeHigh) {
                    g_allTimeHigh = openPrice;
                }
            }
        }
    }

    Print("Anti-Fragile Strategy initialized. ATH: ", g_allTimeHigh, " LastEntry: ", g_lastEntryPrice);
    return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+
//| Expert deinitialization function                                  |
//+------------------------------------------------------------------+
void OnDeinit(const int reason) {
    Print("=== Anti-Fragile Strategy Statistics ===");
    Print("Total Entries: ", g_totalEntries);
    Print("Profit Takes: ", g_profitTakes);
    Print("Max Stress Level: ", g_maxStressLevel);
}

//+------------------------------------------------------------------+
//| Expert tick function                                              |
//+------------------------------------------------------------------+
void OnTick() {
    double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
    double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    double mid = (bid + ask) / 2.0;

    // Track all-time high and stress level
    if(mid > g_allTimeHigh) {
        g_allTimeHigh = mid;
        g_currentStressLevel = 0;
    } else {
        double dropPct = (g_allTimeHigh - mid) / g_allTimeHigh * 100.0;
        g_currentStressLevel = (int)(dropPct / 5.0);  // Every 5% drop = 1 stress level
    }

    if(g_currentStressLevel > g_maxStressLevel) {
        g_maxStressLevel = g_currentStressLevel;
    }

    // Check profit taking
    CheckProfitTaking(bid);

    // Check for new entries
    CheckNewEntries(bid, ask);
}

//+------------------------------------------------------------------+
//| Check profit taking                                               |
//+------------------------------------------------------------------+
void CheckProfitTaking(double bid) {
    double totalCost = 0;
    double totalLots = 0;

    for(int i = PositionsTotal() - 1; i >= 0; i--) {
        if(PositionSelectByTicket(PositionGetTicket(i))) {
            if(PositionGetString(POSITION_SYMBOL) == _Symbol &&
               PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY) {
                double lots = PositionGetDouble(POSITION_VOLUME);
                double entry = PositionGetDouble(POSITION_PRICE_OPEN);
                totalCost += entry * lots;
                totalLots += lots;
            }
        }
    }

    if(totalLots == 0) return;

    double avgEntry = totalCost / totalLots;
    double profitPct = (bid - avgEntry) / avgEntry * 100.0;

    if(profitPct >= TakeProfitPct) {
        int posCount = CountPositions();
        int toClose = (int)(posCount * PartialTakePct);
        if(toClose < 1) toClose = 1;

        Print("PROFIT TAKE! Avg profit: ", DoubleToString(profitPct, 2), "% Closing ", toClose, " positions");

        for(int i = 0; i < toClose; i++) {
            CloseBestPosition(bid);
            g_profitTakes++;
        }
    }
}

//+------------------------------------------------------------------+
//| Check for new entries                                             |
//+------------------------------------------------------------------+
void CheckNewEntries(double bid, double ask) {
    int posCount = CountPositions();
    if(posCount >= MaxPositions) return;

    double entryRef = (g_lastEntryPrice > 0) ? g_lastEntryPrice : g_allTimeHigh;
    if(entryRef == 0) {
        entryRef = ask;
        g_allTimeHigh = ask;
    }

    bool shouldEnter = false;

    // Enter at new ATH (small position)
    if(ask >= g_allTimeHigh && posCount == 0) {
        shouldEnter = true;
    }
    // Enter when price drops by spacing (anti-fragile: larger positions)
    else if(ask <= entryRef - EntrySpacing) {
        shouldEnter = true;
    }

    if(shouldEnter) {
        // Anti-fragile sizing: higher stress = larger position
        double stressMultiplier = MathPow(1.0 + g_currentStressLevel, SizingExponent);
        double lotSize = BaseLotSize * stressMultiplier;
        lotSize = MathMin(lotSize, MaxLotSize);

        // Check margin
        double equity = AccountInfoDouble(ACCOUNT_EQUITY);
        double freeMargin = AccountInfoDouble(ACCOUNT_MARGIN_FREE);
        double contractSize = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);
        long leverage = AccountInfoInteger(ACCOUNT_LEVERAGE);

        double marginNeeded = lotSize * contractSize * ask / leverage;

        if(marginNeeded < freeMargin * (1.0 - MaxEquityRisk)) {
            OpenPosition(ask, lotSize);
        }
    }
}

//+------------------------------------------------------------------+
//| Open a new position                                               |
//+------------------------------------------------------------------+
void OpenPosition(double price, double lots) {
    // Normalize lot size
    double minLot = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN);
    double maxLot = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MAX);
    double lotStep = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_STEP);

    lots = MathMax(minLot, MathMin(maxLot, lots));
    lots = MathFloor(lots / lotStep) * lotStep;

    if(lots < minLot) return;

    if(m_trade.Buy(lots, _Symbol, price, 0, 0,
                   "AF_Stress" + IntegerToString(g_currentStressLevel))) {
        g_lastEntryPrice = price;
        g_totalEntries++;
        Print("ENTRY: Lots=", DoubleToString(lots, 2), " Price=", DoubleToString(price, 2),
              " Stress=", g_currentStressLevel);
    } else {
        Print("Order failed: ", GetLastError());
    }
}

//+------------------------------------------------------------------+
//| Count open positions for this symbol                              |
//+------------------------------------------------------------------+
int CountPositions() {
    int count = 0;
    for(int i = PositionsTotal() - 1; i >= 0; i--) {
        if(PositionSelectByTicket(PositionGetTicket(i))) {
            if(PositionGetString(POSITION_SYMBOL) == _Symbol &&
               PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY) {
                count++;
            }
        }
    }
    return count;
}

//+------------------------------------------------------------------+
//| Close best performing position                                    |
//+------------------------------------------------------------------+
void CloseBestPosition(double bid) {
    ulong bestTicket = 0;
    double bestPnL = -DBL_MAX;

    for(int i = PositionsTotal() - 1; i >= 0; i--) {
        ulong ticket = PositionGetTicket(i);
        if(PositionSelectByTicket(ticket)) {
            if(PositionGetString(POSITION_SYMBOL) == _Symbol &&
               PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY) {
                double entry = PositionGetDouble(POSITION_PRICE_OPEN);
                double pnl = bid - entry;
                if(pnl > bestPnL) {
                    bestPnL = pnl;
                    bestTicket = ticket;
                }
            }
        }
    }

    if(bestTicket > 0) {
        m_trade.PositionClose(bestTicket);
    }
}
//+------------------------------------------------------------------+
