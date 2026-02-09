//+------------------------------------------------------------------+
//| Hybrid + Fill-Up Combined Strategy                                |
//| - Hybrid (Anti-Fragile) during uptrends                          |
//| - Fill-Up (Grid with TP) during consolidation/crash periods      |
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
//| Strategy Mode                                                     |
//+------------------------------------------------------------------+
enum STRATEGY_MODE {
    MODE_HYBRID,    // Anti-fragile uptrend mode
    MODE_FILLUP     // Grid mode during consolidation
};

//+------------------------------------------------------------------+
//| Input Parameters - General                                        |
//+------------------------------------------------------------------+
input string GeneralSettings = "=== General Settings ===";
input int MaxPositions = 20;               // Maximum total positions
input double MaxEquityRisk = 0.3;          // Max equity at risk
input double MaxLotSize = 1.0;             // Maximum lot size

//+------------------------------------------------------------------+
//| Input Parameters - Hybrid Mode (Uptrends)                         |
//+------------------------------------------------------------------+
input string HybridSettings = "=== Hybrid Mode (Uptrends) ===";
input double Hybrid_BaseLotSize = 0.02;    // Base lot size
input double Hybrid_EntrySpacing = 5.0;    // Entry spacing (points)
input double Hybrid_SizingExponent = 1.5;  // Sizing exponent
input double Hybrid_TakeProfitPct = 2.0;   // Take profit (% of avg)
input double Hybrid_PartialTakePct = 0.5;  // Partial close %

//+------------------------------------------------------------------+
//| Input Parameters - Crash Detection                                |
//+------------------------------------------------------------------+
input string CrashSettings = "=== Crash Detection ===";
input bool EnableCrashDetection = true;    // Enable crash detection
input double CrashVelocityThreshold = -0.4;// Crash velocity (%)
input int CrashLookback = 500;             // Lookback ticks
input double CrashExitPct = 0.5;           // Exit % on crash
input int CooldownAfterCrash = 1000;       // Cooldown ticks

//+------------------------------------------------------------------+
//| Input Parameters - Fill-Up Mode (Consolidation)                   |
//+------------------------------------------------------------------+
input string FillUpSettings = "=== Fill-Up Mode (Consolidation) ===";
input double FillUp_SurviveDown = 2.5;     // Survive down %
input double FillUp_Spacing = 5.0;         // Grid spacing (points)
input double FillUp_SizeMultiplier = 1.0;  // Size multiplier
input bool FillUp_UseTakeProfit = true;    // Use take profit orders

//+------------------------------------------------------------------+
//| Input Parameters - Mode Switching                                 |
//+------------------------------------------------------------------+
input string ModeSettings = "=== Mode Switching ===";
input int TrendLookback = 2000;            // Ticks to detect trend
input double UptrendThreshold = 0.5;       // % gain to be "uptrend"
input int ConsolidationTicks = 5000;       // Ticks without new ATH = consolidation

//+------------------------------------------------------------------+
//| Global Variables                                                  |
//+------------------------------------------------------------------+
STRATEGY_MODE g_currentMode = MODE_HYBRID;

// ATH and trend tracking
double g_allTimeHigh = 0;
double g_lastEntryPrice = 0;
int g_currentStressLevel = 0;
int g_ticksSinceATH = 0;

// Crash state
bool g_inCrashMode = false;
int g_crashCooldown = 0;
double g_crashLow = 0;

// Price history
double g_priceHistory[];
int g_priceHistoryIndex = 0;
int g_priceHistoryCount = 0;

// Fill-Up tracking
double g_lowestBuy = DBL_MAX;
double g_highestBuy = 0;
double g_fillUpTradeSize = 0;

// Statistics
int g_hybridEntries = 0;
int g_fillUpEntries = 0;
int g_crashExits = 0;
int g_profitTakes = 0;
int g_tpHits = 0;
int g_modeChanges = 0;

//+------------------------------------------------------------------+
//| Expert initialization function                                    |
//+------------------------------------------------------------------+
int OnInit() {
    m_trade.LogLevel(LOG_LEVEL_ERRORS);
    m_trade.SetAsyncMode(false);

    // Initialize price history
    ArrayResize(g_priceHistory, MathMax(CrashLookback, TrendLookback) + 100);
    ArrayInitialize(g_priceHistory, 0);

    // Initialize ATH
    g_allTimeHigh = SymbolInfoDouble(_Symbol, SYMBOL_ASK);

    // Scan existing positions
    ScanExistingPositions();

    Print("=== Hybrid + Fill-Up Combined Strategy ===");
    Print("Starting in ", EnumToString(g_currentMode), " mode");
    Print("ATH: ", g_allTimeHigh);

    return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+
//| Scan existing positions                                           |
//+------------------------------------------------------------------+
void ScanExistingPositions() {
    g_lowestBuy = DBL_MAX;
    g_highestBuy = 0;
    g_lastEntryPrice = 0;

    for(int i = PositionsTotal() - 1; i >= 0; i--) {
        if(PositionSelectByTicket(PositionGetTicket(i))) {
            if(PositionGetString(POSITION_SYMBOL) == _Symbol &&
               PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY) {
                double openPrice = PositionGetDouble(POSITION_PRICE_OPEN);
                g_lowestBuy = MathMin(g_lowestBuy, openPrice);
                g_highestBuy = MathMax(g_highestBuy, openPrice);
                if(g_lastEntryPrice == 0 || openPrice < g_lastEntryPrice) {
                    g_lastEntryPrice = openPrice;
                }
                if(openPrice > g_allTimeHigh) {
                    g_allTimeHigh = openPrice;
                }
            }
        }
    }
}

//+------------------------------------------------------------------+
//| Expert deinitialization function                                  |
//+------------------------------------------------------------------+
void OnDeinit(const int reason) {
    Print("=== Combined Strategy Statistics ===");
    Print("Hybrid Entries: ", g_hybridEntries);
    Print("Fill-Up Entries: ", g_fillUpEntries);
    Print("Crash Exits: ", g_crashExits);
    Print("Profit Takes: ", g_profitTakes);
    Print("TP Hits: ", g_tpHits);
    Print("Mode Changes: ", g_modeChanges);
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

    // Track ATH and stress
    if(mid > g_allTimeHigh) {
        g_allTimeHigh = mid;
        g_currentStressLevel = 0;
        g_ticksSinceATH = 0;
    } else {
        double dropPct = (g_allTimeHigh - mid) / g_allTimeHigh * 100.0;
        g_currentStressLevel = (int)(dropPct / 5.0);
        g_ticksSinceATH++;
    }

    // Update crash state
    if(EnableCrashDetection) {
        UpdateCrashState(mid);
        if(!g_inCrashMode) {
            CheckCrashExit(bid);
        }
        if(g_crashCooldown > 0) {
            g_crashCooldown--;
        }
    }

    // Determine strategy mode
    DetermineMode(mid);

    // Execute based on mode
    if(g_currentMode == MODE_HYBRID) {
        ExecuteHybridMode(bid, ask);
    } else {
        ExecuteFillUpMode(bid, ask);
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
double CalculateVelocity(int lookback) {
    if(g_priceHistoryCount < lookback) return 0.0;

    int startIdx = (g_priceHistoryIndex - lookback + ArraySize(g_priceHistory)) % ArraySize(g_priceHistory);
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
        if(bouncePct >= 0.5) {  // 0.5% bounce to exit crash mode
            g_inCrashMode = false;
            Print("Exited crash mode");
        }
    }
}

//+------------------------------------------------------------------+
//| Check crash exit                                                  |
//+------------------------------------------------------------------+
void CheckCrashExit(double bid) {
    double velocity = CalculateVelocity(CrashLookback);

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
//| Determine strategy mode                                           |
//+------------------------------------------------------------------+
void DetermineMode(double mid) {
    STRATEGY_MODE newMode = g_currentMode;

    // If in crash mode or cooldown -> Fill-Up mode
    if(g_inCrashMode || g_crashCooldown > 0) {
        newMode = MODE_FILLUP;
    }
    // If consolidating (no new ATH for a while) -> Fill-Up mode
    else if(g_ticksSinceATH > ConsolidationTicks) {
        newMode = MODE_FILLUP;
    }
    // If trending up -> Hybrid mode
    else {
        double trendVelocity = CalculateVelocity(TrendLookback);
        if(trendVelocity >= UptrendThreshold) {
            newMode = MODE_HYBRID;
        } else if(trendVelocity < -UptrendThreshold) {
            newMode = MODE_FILLUP;  // Downtrend -> use Fill-Up
        }
        // Otherwise keep current mode
    }

    if(newMode != g_currentMode) {
        g_currentMode = newMode;
        g_modeChanges++;
        Print("Mode changed to: ", EnumToString(g_currentMode));
    }
}

//+------------------------------------------------------------------+
//| Execute Hybrid Mode (Anti-Fragile)                                |
//+------------------------------------------------------------------+
void ExecuteHybridMode(double bid, double ask) {
    // Check profit taking
    CheckHybridProfitTaking(bid);

    // Check for new entries
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
    else if(ask <= entryRef - Hybrid_EntrySpacing) {
        shouldEnter = true;
    }

    if(shouldEnter) {
        // Anti-fragile sizing
        double stressMultiplier = MathPow(1.0 + g_currentStressLevel, Hybrid_SizingExponent);
        double lotSize = Hybrid_BaseLotSize * stressMultiplier;
        lotSize = MathMin(lotSize, MaxLotSize);

        if(CanOpenPosition(ask, lotSize)) {
            OpenPosition(ask, lotSize, "HYB_S" + IntegerToString(g_currentStressLevel), 0);
            g_lastEntryPrice = ask;
            g_hybridEntries++;
        }
    }
}

//+------------------------------------------------------------------+
//| Execute Fill-Up Mode (Grid with TP)                               |
//+------------------------------------------------------------------+
void ExecuteFillUpMode(double bid, double ask) {
    int posCount = CountPositions();
    if(posCount >= MaxPositions) return;

    // Calculate Fill-Up trade size based on survive_down
    CalculateFillUpSize(ask);

    bool shouldEnter = false;
    double spread = SymbolInfoDouble(_Symbol, SYMBOL_SPREAD) * _Point;

    if(posCount == 0) {
        shouldEnter = true;
    }
    else if(g_lowestBuy != DBL_MAX && ask <= g_lowestBuy - FillUp_Spacing) {
        // Price dropped below lowest position
        shouldEnter = true;
    }
    else if(g_highestBuy > 0 && ask >= g_highestBuy + FillUp_Spacing) {
        // Price rose above highest position
        shouldEnter = true;
    }

    if(shouldEnter && g_fillUpTradeSize > 0) {
        double tp = 0;
        if(FillUp_UseTakeProfit) {
            tp = ask + FillUp_Spacing + spread;
        }

        if(CanOpenPosition(ask, g_fillUpTradeSize)) {
            OpenPosition(ask, g_fillUpTradeSize, "FU_" + DoubleToString(FillUp_Spacing, 1), tp);
            g_lowestBuy = MathMin(g_lowestBuy, ask);
            g_highestBuy = MathMax(g_highestBuy, ask);
            g_fillUpEntries++;
        }
    }
}

//+------------------------------------------------------------------+
//| Calculate Fill-Up position size                                   |
//+------------------------------------------------------------------+
void CalculateFillUpSize(double currentPrice) {
    double minLot = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN);
    double contractSize = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);
    long leverage = AccountInfoInteger(ACCOUNT_LEVERAGE);
    double equity = AccountInfoDouble(ACCOUNT_EQUITY);
    double usedMargin = AccountInfoDouble(ACCOUNT_MARGIN);
    double marginLevel = AccountInfoDouble(ACCOUNT_MARGIN_LEVEL);
    double stopOutLevel = AccountInfoDouble(ACCOUNT_MARGIN_SO_SO);

    if(marginLevel == 0) marginLevel = 100000;  // No positions

    // Calculate based on survive_down %
    double endPrice = (g_highestBuy > 0) ?
                      g_highestBuy * (100 - FillUp_SurviveDown) / 100 :
                      currentPrice * (100 - FillUp_SurviveDown) / 100;
    double distance = currentPrice - endPrice;
    double numTrades = MathFloor(distance / FillUp_Spacing);
    if(numTrades < 1) numTrades = 1;

    // Simple sizing: spread equity across potential grid levels
    double availableEquity = equity * (1 - MaxEquityRisk);
    double perTradeEquity = availableEquity / (numTrades * 2);  // Conservative

    // Calculate lot size
    double lotSize = (perTradeEquity * leverage) / (contractSize * currentPrice);
    lotSize = MathMax(minLot, lotSize * FillUp_SizeMultiplier);
    lotSize = MathMin(lotSize, MaxLotSize);

    g_fillUpTradeSize = NormalizeLots(lotSize);
}

//+------------------------------------------------------------------+
//| Check Hybrid profit taking                                        |
//+------------------------------------------------------------------+
void CheckHybridProfitTaking(double bid) {
    double totalCost = 0, totalLots = 0;

    for(int i = PositionsTotal() - 1; i >= 0; i--) {
        if(PositionSelectByTicket(PositionGetTicket(i))) {
            if(PositionGetString(POSITION_SYMBOL) == _Symbol &&
               PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY) {
                // Only count Hybrid positions (no TP set)
                if(PositionGetDouble(POSITION_TP) == 0) {
                    double lots = PositionGetDouble(POSITION_VOLUME);
                    double entry = PositionGetDouble(POSITION_PRICE_OPEN);
                    totalCost += entry * lots;
                    totalLots += lots;
                }
            }
        }
    }

    if(totalLots == 0) return;

    double avgEntry = totalCost / totalLots;
    double profitPct = (bid - avgEntry) / avgEntry * 100.0;

    if(profitPct >= Hybrid_TakeProfitPct) {
        int posCount = CountHybridPositions();
        int toClose = (int)(posCount * Hybrid_PartialTakePct);
        if(toClose < 1) toClose = 1;

        Print("HYBRID PROFIT TAKE! Profit: ", DoubleToString(profitPct, 2), "%");

        for(int i = 0; i < toClose; i++) {
            CloseBestHybridPosition(bid);
            g_profitTakes++;
        }
    }
}

//+------------------------------------------------------------------+
//| Can open position (margin check)                                  |
//+------------------------------------------------------------------+
bool CanOpenPosition(double price, double lots) {
    double freeMargin = AccountInfoDouble(ACCOUNT_MARGIN_FREE);
    double contractSize = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);
    long leverage = AccountInfoInteger(ACCOUNT_LEVERAGE);
    double marginNeeded = lots * contractSize * price / leverage;

    return marginNeeded < freeMargin * (1.0 - MaxEquityRisk);
}

//+------------------------------------------------------------------+
//| Open position                                                     |
//+------------------------------------------------------------------+
void OpenPosition(double price, double lots, string comment, double tp) {
    lots = NormalizeLots(lots);
    double minLot = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN);
    if(lots < minLot) return;

    MqlTradeRequest request = {};
    MqlTradeResult result = {};

    request.action = TRADE_ACTION_DEAL;
    request.symbol = _Symbol;
    request.volume = lots;
    request.type = ORDER_TYPE_BUY;
    request.price = price;
    request.tp = tp;
    request.deviation = 10;
    request.comment = comment;

    // Set filling mode
    long filling = SymbolInfoInteger(_Symbol, SYMBOL_FILLING_MODE);
    if(filling == SYMBOL_FILLING_FOK)
        request.type_filling = ORDER_FILLING_FOK;
    else if(filling == SYMBOL_FILLING_IOC)
        request.type_filling = ORDER_FILLING_IOC;

    if(OrderSend(request, result)) {
        Print("ENTRY: ", comment, " Lots=", DoubleToString(lots, 2),
              " @ ", DoubleToString(price, 2),
              (tp > 0 ? " TP=" + DoubleToString(tp, 2) : ""));
    } else {
        Print("Order failed: ", GetLastError());
    }
}

//+------------------------------------------------------------------+
//| Normalize lots                                                    |
//+------------------------------------------------------------------+
double NormalizeLots(double lots) {
    double minLot = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN);
    double maxLot = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MAX);
    double lotStep = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_STEP);

    lots = MathMax(minLot, MathMin(maxLot, lots));
    lots = MathFloor(lots / lotStep) * lotStep;
    return lots;
}

//+------------------------------------------------------------------+
//| Count all positions                                               |
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
//| Count Hybrid positions (no TP)                                    |
//+------------------------------------------------------------------+
int CountHybridPositions() {
    int count = 0;
    for(int i = PositionsTotal() - 1; i >= 0; i--) {
        if(PositionSelectByTicket(PositionGetTicket(i))) {
            if(PositionGetString(POSITION_SYMBOL) == _Symbol &&
               PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY &&
               PositionGetDouble(POSITION_TP) == 0) {
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
//| Close best Hybrid position                                        |
//+------------------------------------------------------------------+
void CloseBestHybridPosition(double bid) {
    ulong bestTicket = 0;
    double bestPnL = -DBL_MAX;

    for(int i = PositionsTotal() - 1; i >= 0; i--) {
        ulong ticket = PositionGetTicket(i);
        if(PositionSelectByTicket(ticket)) {
            if(PositionGetString(POSITION_SYMBOL) == _Symbol &&
               PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY &&
               PositionGetDouble(POSITION_TP) == 0) {  // Hybrid = no TP
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
