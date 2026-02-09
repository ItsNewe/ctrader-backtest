//+------------------------------------------------------------------+
//| Hybrid Strategy: Anti-Fragile + Crash Detection                  |
//| Optimized for XAUUSD (Gold)                                      |
//| Expected: ~5x return, ~28% max DD                                |
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
input double BaseLotSize = 0.02;           // Base lot size
input double MaxLotSize = 1.0;             // Maximum lot size
input double EntrySpacing = 5.0;           // Entry spacing (points)
input double SizingExponent = 1.5;         // Sizing exponent (higher = more aggressive on dips)
input int MaxPositions = 20;               // Maximum positions

// Profit taking
input double TakeProfitPct = 2.0;          // Take profit (% of avg entry)
input double PartialTakePct = 0.5;         // Partial close percentage

// Crash detection
input bool EnableCrashDetection = true;    // Enable crash detection
input double CrashVelocityThreshold = -0.4;// Crash velocity threshold (%)
input int CrashLookback = 500;             // Crash lookback (ticks)
input double CrashExitPct = 0.5;           // Exit percentage on crash

// Re-entry after crash
input int CooldownAfterCrash = 1000;       // Cooldown ticks after crash
input double ReentryBouncePct = 0.5;       // Bounce % to re-enter

// Risk management
input double MaxEquityRisk = 0.3;          // Max equity at risk

//+------------------------------------------------------------------+
//| Global Variables                                                  |
//+------------------------------------------------------------------+
double g_allTimeHigh = 0;
double g_lastEntryPrice = 0;
int g_currentStressLevel = 0;

bool g_inCrashMode = false;
int g_crashCooldown = 0;
double g_crashLow = 0;

// Price history for velocity calculation
double g_priceHistory[];
int g_priceHistoryIndex = 0;
int g_priceHistoryCount = 0;

// Statistics
int g_totalEntries = 0;
int g_crashExits = 0;
int g_profitTakes = 0;
int g_reentries = 0;

//+------------------------------------------------------------------+
//| Expert initialization function                                    |
//+------------------------------------------------------------------+
int OnInit() {
    m_trade.LogLevel(LOG_LEVEL_ERRORS);
    m_trade.SetAsyncMode(false);

    // Initialize price history array
    ArrayResize(g_priceHistory, CrashLookback + 100);
    ArrayInitialize(g_priceHistory, 0);

    // Initialize ATH from current price
    g_allTimeHigh = SymbolInfoDouble(_Symbol, SYMBOL_ASK);

    // Scan existing positions
    g_lastEntryPrice = 0;
    for(int i = PositionsTotal() - 1; i >= 0; i--) {
        if(PositionSelectByTicket(PositionGetTicket(i))) {
            if(PositionGetString(POSITION_SYMBOL) == _Symbol &&
               PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY) {
                double openPrice = PositionGetDouble(POSITION_PRICE_OPEN);
                if(openPrice > g_lastEntryPrice) {
                    g_lastEntryPrice = openPrice;
                }
            }
        }
    }

    Print("Hybrid Strategy initialized. ATH: ", g_allTimeHigh, " LastEntry: ", g_lastEntryPrice);
    return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+
//| Expert deinitialization function                                  |
//+------------------------------------------------------------------+
void OnDeinit(const int reason) {
    Print("=== Hybrid Strategy Statistics ===");
    Print("Total Entries: ", g_totalEntries);
    Print("Crash Exits: ", g_crashExits);
    Print("Profit Takes: ", g_profitTakes);
    Print("Re-entries: ", g_reentries);
}

//+------------------------------------------------------------------+
//| Expert tick function                                              |
//+------------------------------------------------------------------+
void OnTick() {
    double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
    double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    double mid = (bid + ask) / 2.0;

    // Update price history
    UpdatePriceHistory(mid);

    // Track all-time high and stress level
    if(mid > g_allTimeHigh) {
        g_allTimeHigh = mid;
        g_currentStressLevel = 0;
    } else {
        double dropPct = (g_allTimeHigh - mid) / g_allTimeHigh * 100.0;
        g_currentStressLevel = (int)(dropPct / 5.0);  // Every 5% drop = 1 stress level
    }

    // Update crash state
    UpdateCrashState(mid);

    // Check crash exit
    if(EnableCrashDetection && !g_inCrashMode) {
        CheckCrashExit(bid);
    }

    // Check profit taking
    CheckProfitTaking(bid);

    // Check for new entries (only if not in crash cooldown)
    if(!g_inCrashMode || g_crashCooldown <= 0) {
        CheckNewEntries(bid, ask);
    }

    // Decrement cooldown
    if(g_crashCooldown > 0) {
        g_crashCooldown--;
    }
}

//+------------------------------------------------------------------+
//| Update price history for velocity calculation                     |
//+------------------------------------------------------------------+
void UpdatePriceHistory(double price) {
    g_priceHistory[g_priceHistoryIndex] = price;
    g_priceHistoryIndex = (g_priceHistoryIndex + 1) % ArraySize(g_priceHistory);
    if(g_priceHistoryCount < ArraySize(g_priceHistory)) {
        g_priceHistoryCount++;
    }
}

//+------------------------------------------------------------------+
//| Calculate price velocity                                          |
//+------------------------------------------------------------------+
double CalculateVelocity() {
    if(g_priceHistoryCount < CrashLookback) {
        return 0.0;
    }

    int startIdx = (g_priceHistoryIndex - CrashLookback + ArraySize(g_priceHistory)) % ArraySize(g_priceHistory);
    int endIdx = (g_priceHistoryIndex - 1 + ArraySize(g_priceHistory)) % ArraySize(g_priceHistory);

    double startPrice = g_priceHistory[startIdx];
    double endPrice = g_priceHistory[endIdx];

    if(startPrice == 0) return 0.0;

    return (endPrice - startPrice) / startPrice * 100.0;
}

//+------------------------------------------------------------------+
//| Update crash state                                                |
//+------------------------------------------------------------------+
void UpdateCrashState(double mid) {
    if(g_inCrashMode) {
        // Track crash low
        if(mid < g_crashLow) {
            g_crashLow = mid;
        }

        // Check for bounce to exit crash mode
        double bouncePct = (mid - g_crashLow) / g_crashLow * 100.0;
        if(bouncePct >= ReentryBouncePct) {
            g_inCrashMode = false;
            g_reentries++;
            Print("Exited crash mode. Bounce: ", DoubleToString(bouncePct, 2), "%");
        }
    }
}

//+------------------------------------------------------------------+
//| Check for crash exit                                              |
//+------------------------------------------------------------------+
void CheckCrashExit(double bid) {
    double velocity = CalculateVelocity();

    if(velocity <= CrashVelocityThreshold) {
        // Crash detected!
        g_inCrashMode = true;
        g_crashCooldown = CooldownAfterCrash;
        g_crashLow = bid;

        Print("CRASH DETECTED! Velocity: ", DoubleToString(velocity, 2), "% Exiting ",
              DoubleToString(CrashExitPct * 100, 0), "% of positions");

        // Count positions
        int posCount = CountPositions();
        int toClose = (int)(posCount * CrashExitPct);
        if(toClose < 1 && posCount > 0) toClose = 1;

        // Close worst performing positions first
        for(int i = 0; i < toClose; i++) {
            CloseWorstPosition(bid);
            g_crashExits++;
        }
    }
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
    else if(ask <= entryRef - EntrySpacing * _Point * 10) {  // Convert to price
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
        double marginNeeded = lotSize * SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE) * ask /
                             AccountInfoInteger(ACCOUNT_LEVERAGE);

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
                   "Hybrid_Stress" + IntegerToString(g_currentStressLevel))) {
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
//| Close worst performing position                                   |
//+------------------------------------------------------------------+
void CloseWorstPosition(double bid) {
    ulong worstTicket = 0;
    double worstPnL = DBL_MAX;

    for(int i = PositionsTotal() - 1; i >= 0; i--) {
        ulong ticket = PositionGetTicket(i);
        if(PositionSelectByTicket(ticket)) {
            if(PositionGetString(POSITION_SYMBOL) == _Symbol &&
               PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY) {
                double entry = PositionGetDouble(POSITION_PRICE_OPEN);
                double pnl = bid - entry;
                if(pnl < worstPnL) {
                    worstPnL = pnl;
                    worstTicket = ticket;
                }
            }
        }
    }

    if(worstTicket > 0) {
        m_trade.PositionClose(worstTicket);
    }
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
