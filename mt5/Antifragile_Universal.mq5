//+------------------------------------------------------------------+
//| Anti-Fragile Universal Strategy                                   |
//| Works on both XAUUSD (Gold) and NAS100                           |
//| Auto-detects asset and applies optimal settings                   |
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
// Mode selection
input bool AutoDetectAsset = true;         // Auto-detect asset type
input int ManualAssetType = 0;             // Manual: 0=Gold, 1=NAS100

// Anti-fragile sizing (can override auto)
input double BaseLotSize_Override = 0;     // Base lot override (0=auto)
input double EntrySpacing_Override = 0;    // Spacing override (0=auto)
input double SizingExponent = 1.5;         // Sizing exponent
input int MaxPositions = 20;               // Maximum positions

// Profit taking
input double TakeProfitPct = 2.0;          // Take profit (% of avg entry)
input double PartialTakePct = 0.5;         // Partial close percentage

// Crash detection (Gold only by default)
input bool EnableCrashDetection_Override = false;  // Force crash detection
input double CrashVelocityThreshold = -0.4;// Crash velocity threshold (%)
input int CrashLookback = 500;             // Crash lookback (ticks)
input double CrashExitPct = 0.5;           // Exit percentage on crash
input int CooldownAfterCrash = 1000;       // Cooldown ticks after crash
input double ReentryBouncePct = 0.5;       // Bounce % to re-enter

// Risk management
input double MaxEquityRisk = 0.3;          // Max equity at risk
input double MaxLotSize = 1.0;             // Maximum lot size

//+------------------------------------------------------------------+
//| Derived Parameters (set in OnInit based on asset)                 |
//+------------------------------------------------------------------+
double g_baseLotSize;
double g_entrySpacing;
bool g_enableCrashDetection;
bool g_isGold;

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
double g_maxStressLevel = 0;

//+------------------------------------------------------------------+
//| Expert initialization function                                    |
//+------------------------------------------------------------------+
int OnInit() {
    m_trade.LogLevel(LOG_LEVEL_ERRORS);
    m_trade.SetAsyncMode(false);

    // Detect asset type
    string symbol = _Symbol;
    g_isGold = (StringFind(symbol, "XAU") >= 0 || StringFind(symbol, "GOLD") >= 0);

    if(!AutoDetectAsset) {
        g_isGold = (ManualAssetType == 0);
    }

    // Set parameters based on asset
    if(g_isGold) {
        g_baseLotSize = (BaseLotSize_Override > 0) ? BaseLotSize_Override : 0.02;
        g_entrySpacing = (EntrySpacing_Override > 0) ? EntrySpacing_Override : 5.0;
        g_enableCrashDetection = true;  // Crash detection helps Gold
        Print("Asset detected: GOLD - Using Hybrid (Anti-Fragile + Crash Detection)");
    } else {
        g_baseLotSize = (BaseLotSize_Override > 0) ? BaseLotSize_Override : 0.01;
        g_entrySpacing = (EntrySpacing_Override > 0) ? EntrySpacing_Override : 50.0;
        g_enableCrashDetection = false;  // Crash detection hurts NAS100
        Print("Asset detected: INDEX - Using Pure Anti-Fragile");
    }

    // Allow override
    if(EnableCrashDetection_Override) {
        g_enableCrashDetection = true;
    }

    // Initialize price history array
    ArrayResize(g_priceHistory, CrashLookback + 100);
    ArrayInitialize(g_priceHistory, 0);

    // Initialize ATH from current price
    g_allTimeHigh = SymbolInfoDouble(_Symbol, SYMBOL_ASK);

    // Scan existing positions
    ScanExistingPositions();

    Print("Settings: BaseLot=", g_baseLotSize, " Spacing=", g_entrySpacing,
          " CrashDetect=", g_enableCrashDetection);
    Print("ATH: ", g_allTimeHigh, " LastEntry: ", g_lastEntryPrice);

    return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+
//| Scan existing positions on init                                   |
//+------------------------------------------------------------------+
void ScanExistingPositions() {
    g_lastEntryPrice = 0;
    double lowestEntry = DBL_MAX;

    for(int i = PositionsTotal() - 1; i >= 0; i--) {
        if(PositionSelectByTicket(PositionGetTicket(i))) {
            if(PositionGetString(POSITION_SYMBOL) == _Symbol &&
               PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY) {
                double openPrice = PositionGetDouble(POSITION_PRICE_OPEN);
                if(openPrice < lowestEntry) {
                    lowestEntry = openPrice;
                }
                if(openPrice > g_allTimeHigh) {
                    g_allTimeHigh = openPrice;
                }
            }
        }
    }

    if(lowestEntry < DBL_MAX) {
        g_lastEntryPrice = lowestEntry;
    }
}

//+------------------------------------------------------------------+
//| Expert deinitialization function                                  |
//+------------------------------------------------------------------+
void OnDeinit(const int reason) {
    Print("=== Anti-Fragile Universal Strategy Statistics ===");
    Print("Asset: ", g_isGold ? "GOLD" : "INDEX");
    Print("Total Entries: ", g_totalEntries);
    Print("Crash Exits: ", g_crashExits);
    Print("Profit Takes: ", g_profitTakes);
    Print("Re-entries: ", g_reentries);
    Print("Max Stress Level: ", g_maxStressLevel);
}

//+------------------------------------------------------------------+
//| Expert tick function                                              |
//+------------------------------------------------------------------+
void OnTick() {
    double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
    double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    double mid = (bid + ask) / 2.0;

    // Update price history (for crash detection)
    if(g_enableCrashDetection) {
        UpdatePriceHistory(mid);
    }

    // Track all-time high and stress level
    if(mid > g_allTimeHigh) {
        g_allTimeHigh = mid;
        g_currentStressLevel = 0;
    } else {
        double dropPct = (g_allTimeHigh - mid) / g_allTimeHigh * 100.0;
        g_currentStressLevel = (int)(dropPct / 5.0);
    }

    if(g_currentStressLevel > g_maxStressLevel) {
        g_maxStressLevel = g_currentStressLevel;
    }

    // Crash detection (Gold only)
    if(g_enableCrashDetection) {
        UpdateCrashState(mid);

        if(!g_inCrashMode) {
            CheckCrashExit(bid);
        }

        if(g_crashCooldown > 0) {
            g_crashCooldown--;
        }
    }

    // Check profit taking
    CheckProfitTaking(bid);

    // Check for new entries
    bool canEnter = !g_enableCrashDetection || !g_inCrashMode || g_crashCooldown <= 0;
    if(canEnter) {
        CheckNewEntries(bid, ask);
    }
}

//+------------------------------------------------------------------+
//| Update price history                                              |
//+------------------------------------------------------------------+
void UpdatePriceHistory(double price) {
    g_priceHistory[g_priceHistoryIndex] = price;
    g_priceHistoryIndex = (g_priceHistoryIndex + 1) % ArraySize(g_priceHistory);
    if(g_priceHistoryCount < ArraySize(g_priceHistory)) {
        g_priceHistoryCount++;
    }
}

//+------------------------------------------------------------------+
//| Calculate velocity                                                |
//+------------------------------------------------------------------+
double CalculateVelocity() {
    if(g_priceHistoryCount < CrashLookback) return 0.0;

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
        if(mid < g_crashLow) g_crashLow = mid;

        double bouncePct = (mid - g_crashLow) / g_crashLow * 100.0;
        if(bouncePct >= ReentryBouncePct) {
            g_inCrashMode = false;
            g_reentries++;
            Print("Exited crash mode. Bounce: ", DoubleToString(bouncePct, 2), "%");
        }
    }
}

//+------------------------------------------------------------------+
//| Check crash exit                                                  |
//+------------------------------------------------------------------+
void CheckCrashExit(double bid) {
    double velocity = CalculateVelocity();

    if(velocity <= CrashVelocityThreshold) {
        g_inCrashMode = true;
        g_crashCooldown = CooldownAfterCrash;
        g_crashLow = bid;

        Print("CRASH DETECTED! Velocity: ", DoubleToString(velocity, 2), "%");

        int posCount = CountPositions();
        int toClose = (int)(posCount * CrashExitPct);
        if(toClose < 1 && posCount > 0) toClose = 1;

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
    double totalCost = 0, totalLots = 0;

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

        Print("PROFIT TAKE! Profit: ", DoubleToString(profitPct, 2), "%");

        for(int i = 0; i < toClose; i++) {
            CloseBestPosition(bid);
            g_profitTakes++;
        }
    }
}

//+------------------------------------------------------------------+
//| Check new entries                                                 |
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

    // Enter at new ATH
    if(ask >= g_allTimeHigh && posCount == 0) {
        shouldEnter = true;
    }
    // Enter on dip
    else if(ask <= entryRef - g_entrySpacing) {
        shouldEnter = true;
    }

    if(shouldEnter) {
        double stressMultiplier = MathPow(1.0 + g_currentStressLevel, SizingExponent);
        double lotSize = g_baseLotSize * stressMultiplier;
        lotSize = MathMin(lotSize, MaxLotSize);

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
//| Open position                                                     |
//+------------------------------------------------------------------+
void OpenPosition(double price, double lots) {
    double minLot = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN);
    double maxLot = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MAX);
    double lotStep = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_STEP);

    lots = MathMax(minLot, MathMin(maxLot, lots));
    lots = MathFloor(lots / lotStep) * lotStep;

    if(lots < minLot) return;

    if(m_trade.Buy(lots, _Symbol, price, 0, 0, "AF_S" + IntegerToString(g_currentStressLevel))) {
        g_lastEntryPrice = price;
        g_totalEntries++;
        Print("ENTRY: Lots=", DoubleToString(lots, 2), " @ ", DoubleToString(price, 2),
              " Stress=", g_currentStressLevel);
    }
}

//+------------------------------------------------------------------+
//| Count positions                                                   |
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
//| Close worst position                                              |
//+------------------------------------------------------------------+
void CloseWorstPosition(double bid) {
    ulong worstTicket = 0;
    double worstPnL = DBL_MAX;

    for(int i = PositionsTotal() - 1; i >= 0; i--) {
        ulong ticket = PositionGetTicket(i);
        if(PositionSelectByTicket(ticket)) {
            if(PositionGetString(POSITION_SYMBOL) == _Symbol &&
               PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY) {
                double pnl = bid - PositionGetDouble(POSITION_PRICE_OPEN);
                if(pnl < worstPnL) {
                    worstPnL = pnl;
                    worstTicket = ticket;
                }
            }
        }
    }

    if(worstTicket > 0) m_trade.PositionClose(worstTicket);
}

//+------------------------------------------------------------------+
//| Close best position                                               |
//+------------------------------------------------------------------+
void CloseBestPosition(double bid) {
    ulong bestTicket = 0;
    double bestPnL = -DBL_MAX;

    for(int i = PositionsTotal() - 1; i >= 0; i--) {
        ulong ticket = PositionGetTicket(i);
        if(PositionSelectByTicket(ticket)) {
            if(PositionGetString(POSITION_SYMBOL) == _Symbol &&
               PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY) {
                double pnl = bid - PositionGetDouble(POSITION_PRICE_OPEN);
                if(pnl > bestPnL) {
                    bestPnL = pnl;
                    bestTicket = ticket;
                }
            }
        }
    }

    if(bestTicket > 0) m_trade.PositionClose(bestTicket);
}
//+------------------------------------------------------------------+
