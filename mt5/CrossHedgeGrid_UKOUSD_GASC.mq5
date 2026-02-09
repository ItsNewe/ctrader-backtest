//+------------------------------------------------------------------+
//| CrossHedgeGrid_UKOUSD_GASC.mq5                                  |
//|                                                                  |
//| Cross-Hedge Carry Grid: LONG UKOUSD + SHORT GAS-C               |
//|                                                                  |
//| RATIONALE:                                                       |
//|   UKOUSD swap_long = +9.38 pts/day (+5.01%/yr annualized)       |
//|   GAS-C  swap_short = +3.17 pts/day (+5.96%/yr annualized)      |
//|   Both earn positive swap. Crude and gasoline are ~85-90%        |
//|   correlated so price moves partially hedge each other.          |
//|                                                                  |
//| MECHANISM:                                                       |
//|   Instrument A (UKOUSD): BUY grid — fills up as price drops     |
//|   Instrument B (GAS-C):  SELL grid — fills down as price rises   |
//|   Each grid runs independently with survive% lot sizing.         |
//|   The grids do NOT split capital — each uses full equity          |
//|   (matching the fill_both.mq5 approach).                         |
//|                                                                  |
//| HEDGE CALIBRATION:                                               |
//|   UKOUSD: cs=1000, price~$68.30 → 1 lot notional = $68,300     |
//|   GAS-C:  cs=42000, price~$1.94 → 1 lot notional = $81,480     |
//|   Ratio: GAS-C / UKOUSD ≈ 1.19:1 (nearly matched at 1:1 lots)  |
//|   However, each grid independently sizes lots to survive%.       |
//|                                                                  |
//| DEPLOYMENT: Attach to UKOUSD chart on .            |
//|   The EA trades both UKOUSD and GAS-C from one chart.            |
//+------------------------------------------------------------------+
#include <Trade\Trade.mqh>

CTrade m_trade;

//+------------------------------------------------------------------+
//| Input Parameters                                                 |
//+------------------------------------------------------------------+
input string   InpSymbolA        = "UKOUSD";    // Symbol A (LONG grid)
input string   InpSymbolB        = "GAS-C";     // Symbol B (SHORT grid)
input double   InpSurviveA       = 34.0;        // Survive% for Symbol A (BUY grid)
input double   InpSurviveB       = 34.0;        // Survive% for Symbol B (SELL grid)
input double   InpSpacingA       = 0.3;         // Grid spacing for Symbol A ($)
input double   InpSpacingB       = 0.01;        // Grid spacing for Symbol B ($)
input double   InpSizeMultA      = 1.0;         // Lot size multiplier A
input double   InpSizeMultB      = 1.0;         // Lot size multiplier B
input bool     InpEnableA        = true;        // Enable LONG grid (Symbol A)
input bool     InpEnableB        = true;        // Enable SHORT grid (Symbol B)
input int      InpMagicA         = 55501;       // Magic number for Symbol A
input int      InpMagicB         = 55502;       // Magic number for Symbol B

//+------------------------------------------------------------------+
//| Per-Symbol State                                                 |
//+------------------------------------------------------------------+
struct SymbolState {
    string   symbol;
    int      magic;
    bool     enabled;
    double   survive;
    double   spacing;
    double   size_mult;

    // Cached symbol info
    double   point;
    double   min_volume;
    double   max_volume;
    double   contract_size;
    long     leverage;
    int      calc_mode;
    int      volume_digits;
    double   initial_margin_rate;
    double   maintenance_margin_rate;
    ENUM_ORDER_TYPE_FILLING filling_mode;

    // Grid state
    double   lowest_price;
    double   highest_price;
    double   closest_above;
    double   closest_below;
    double   volume_open;
    double   trade_size;
    int      position_count;
};

SymbolState g_symA, g_symB;

// Stats
double g_max_balance, g_max_used_funds, g_max_trade_size;
int    g_max_positions;

// Reusable request/result
MqlTradeRequest g_req;
MqlTradeResult  g_res;

//+------------------------------------------------------------------+
//| Initialize one symbol's state                                    |
//+------------------------------------------------------------------+
bool InitSymbol(SymbolState &s, string sym, int magic, bool enabled,
                double survive, double spacing, double size_mult) {
    s.symbol = sym;
    s.magic = magic;
    s.enabled = enabled;
    s.survive = survive;
    s.spacing = spacing;
    s.size_mult = size_mult;

    if(!enabled) return true;

    // Ensure symbol is in Market Watch
    if(!SymbolSelect(sym, true)) {
        Print("[ERROR] Cannot select symbol: ", sym);
        return false;
    }

    // Wait for symbol data
    for(int i = 0; i < 50; i++) {
        if(SymbolInfoDouble(sym, SYMBOL_BID) > 0) break;
        Sleep(100);
    }

    s.point = SymbolInfoDouble(sym, SYMBOL_POINT);
    s.min_volume = SymbolInfoDouble(sym, SYMBOL_VOLUME_MIN);
    s.max_volume = SymbolInfoDouble(sym, SYMBOL_VOLUME_MAX);
    s.contract_size = SymbolInfoDouble(sym, SYMBOL_TRADE_CONTRACT_SIZE);
    s.leverage = AccountInfoInteger(ACCOUNT_LEVERAGE);
    s.calc_mode = (int)SymbolInfoInteger(sym, SYMBOL_TRADE_CALC_MODE);
    SymbolInfoMarginRate(sym, ORDER_TYPE_BUY, s.initial_margin_rate, s.maintenance_margin_rate);

    s.volume_digits = (s.min_volume == 0.01) ? 2 : (s.min_volume == 0.1) ? 1 : 0;

    long filling = SymbolInfoInteger(sym, SYMBOL_FILLING_MODE);
    s.filling_mode = (filling == SYMBOL_FILLING_FOK) ? ORDER_FILLING_FOK :
                     (filling == SYMBOL_FILLING_IOC) ? ORDER_FILLING_IOC :
                     (filling == 4) ? ORDER_FILLING_BOC : ORDER_FILLING_FOK;

    Print("[INIT] ", sym, ": cs=", s.contract_size, " lev=", s.leverage,
          " min=", s.min_volume, " mode=", s.calc_mode,
          " survive=", s.survive, "% spacing=", s.spacing);

    return true;
}

//+------------------------------------------------------------------+
int OnInit() {
    m_trade.SetAsyncMode(true);
    m_trade.LogLevel(LOG_LEVEL_NO);

    if(!InitSymbol(g_symA, InpSymbolA, InpMagicA, InpEnableA,
                    InpSurviveA, InpSpacingA, InpSizeMultA))
        return INIT_FAILED;

    if(!InitSymbol(g_symB, InpSymbolB, InpMagicB, InpEnableB,
                    InpSurviveB, InpSpacingB, InpSizeMultB))
        return INIT_FAILED;

    // Scan existing positions
    ScanPositions(g_symA);
    ScanPositions(g_symB);

    Print("[INIT] CrossHedgeGrid ready: LONG ", InpSymbolA, " + SHORT ", InpSymbolB);
    return INIT_SUCCEEDED;
}

//+------------------------------------------------------------------+
void OnTick() {
    double bal = AccountInfoDouble(ACCOUNT_BALANCE);
    double eq  = AccountInfoDouble(ACCOUNT_EQUITY);
    double margin = AccountInfoDouble(ACCOUNT_MARGIN);
    int posTotal = PositionsTotal();

    g_max_balance = MathMax(g_max_balance, bal);
    g_max_positions = MathMax(g_max_positions, posTotal);
    g_max_used_funds = MathMax(g_max_used_funds, bal - eq + margin);

    // --- Symbol A: LONG (BUY) grid ---
    if(g_symA.enabled) {
        ScanPositions(g_symA);
        double ask = SymbolInfoDouble(g_symA.symbol, SYMBOL_ASK);
        if(ask > 0) ProcessBuyGrid(g_symA, ask);
    }

    // --- Symbol B: SHORT (SELL) grid ---
    if(g_symB.enabled) {
        ScanPositions(g_symB);
        double bid = SymbolInfoDouble(g_symB.symbol, SYMBOL_BID);
        if(bid > 0) ProcessSellGrid(g_symB, bid);
    }
}

//+------------------------------------------------------------------+
//| Process BUY grid for Symbol A                                    |
//+------------------------------------------------------------------+
void ProcessBuyGrid(SymbolState &s, double ask) {
    if(s.volume_open == 0) {
        CalcLotSizeBuy(s, ask);
        if(OpenBuy(s, ask)) {
            s.highest_price = ask;
            s.lowest_price = ask;
        }
    }
    else {
        // Price dropped below grid -> Buy lower
        if(s.lowest_price >= ask + s.spacing) {
            CalcLotSizeBuy(s, ask);
            if(OpenBuy(s, ask)) s.lowest_price = ask;
        }
        // Price rose above grid -> Buy higher
        else if(s.highest_price <= ask - s.spacing) {
            CalcLotSizeBuy(s, ask);
            if(OpenBuy(s, ask)) s.highest_price = ask;
        }
        // Fill internal gap
        else if((s.closest_above >= s.spacing) && (s.closest_below >= s.spacing)) {
            CalcLotSizeBuy(s, ask);
            OpenBuy(s, ask);
        }
    }
}

//+------------------------------------------------------------------+
//| Process SELL grid for Symbol B                                   |
//+------------------------------------------------------------------+
void ProcessSellGrid(SymbolState &s, double bid) {
    if(s.volume_open == 0) {
        CalcLotSizeSell(s, bid);
        if(OpenSell(s, bid)) {
            s.highest_price = bid;
            s.lowest_price = bid;
        }
    }
    else {
        // Price rose above grid -> Sell higher
        if(s.highest_price <= bid - s.spacing) {
            CalcLotSizeSell(s, bid);
            if(OpenSell(s, bid)) s.highest_price = bid;
        }
        // Price dropped below grid -> Sell lower
        else if(s.lowest_price >= bid + s.spacing) {
            CalcLotSizeSell(s, bid);
            if(OpenSell(s, bid)) s.lowest_price = bid;
        }
        // Fill internal gap
        else if((s.closest_above >= s.spacing) && (s.closest_below >= s.spacing)) {
            CalcLotSizeSell(s, bid);
            OpenSell(s, bid);
        }
    }
}

//+------------------------------------------------------------------+
//| Scan positions for a specific symbol + magic                     |
//+------------------------------------------------------------------+
void ScanPositions(SymbolState &s) {
    s.lowest_price  = DBL_MAX;
    s.highest_price = DBL_MIN;
    s.closest_above = DBL_MAX;
    s.closest_below = DBL_MAX;
    s.volume_open   = 0;
    s.position_count = 0;

    double ref_price;
    // For buy grid use ask, for sell grid use bid
    // Symbol A = buy, Symbol B = sell
    if(s.magic == InpMagicA)
        ref_price = SymbolInfoDouble(s.symbol, SYMBOL_ASK);
    else
        ref_price = SymbolInfoDouble(s.symbol, SYMBOL_BID);

    int total = PositionsTotal();
    for(int i = total - 1; i >= 0; i--) {
        ulong ticket = PositionGetTicket(i);
        if(ticket == 0) continue;
        if(PositionGetString(POSITION_SYMBOL) != s.symbol) continue;
        if(PositionGetInteger(POSITION_MAGIC) != s.magic) continue;

        double price = PositionGetDouble(POSITION_PRICE_OPEN);
        double lots  = PositionGetDouble(POSITION_VOLUME);

        s.volume_open += lots;
        s.position_count++;
        if(price < s.lowest_price)  s.lowest_price = price;
        if(price > s.highest_price) s.highest_price = price;

        if(ref_price > 0) {
            if(price >= ref_price)
                s.closest_above = MathMin(s.closest_above, price - ref_price);
            if(price <= ref_price)
                s.closest_below = MathMin(s.closest_below, ref_price - price);
        }
    }
}

//+------------------------------------------------------------------+
//| Calculate lot size for BUY grid (survive% approach)              |
//+------------------------------------------------------------------+
void CalcLotSizeBuy(SymbolState &s, double ask) {
    s.trade_size = 0;

    double equity = AccountInfoDouble(ACCOUNT_EQUITY);
    double used_margin = AccountInfoDouble(ACCOUNT_MARGIN);
    double stopout_level = AccountInfoDouble(ACCOUNT_MARGIN_SO_SO);

    // Target: price drops survive% from reference
    double ref_price = (s.volume_open == 0) ? ask : s.highest_price;
    double end_price = ref_price * (100.0 - s.survive) / 100.0;
    double distance = ask - end_price;
    if(distance <= 0) return;

    // Equity at target (only this grid's loss)
    double eq_at_target = equity - s.volume_open * distance * s.contract_size;
    if(used_margin > 0 && (eq_at_target / used_margin * 100) <= stopout_level) return;

    double num_trades = MathFloor(distance / s.spacing);
    if(num_trades < 1) num_trades = 1;

    double unit = s.min_volume;
    double d_equity = s.contract_size * unit * s.spacing * (num_trades * (num_trades + 1) / 2);
    double spread = s.point * SymbolInfoInteger(s.symbol, SYMBOL_SPREAD);
    d_equity += num_trades * unit * spread * s.contract_size;

    double unit_margin = CalcUnitMargin(s, ask, end_price, num_trades);

    double S = stopout_level / 100.0;
    double denominator = d_equity + S * unit_margin;
    if(denominator <= 0) return;

    double m = (eq_at_target - S * used_margin) / denominator;
    m = MathMax(1.0, m);

    double max_mult = s.max_volume / s.min_volume;
    if(m > max_mult) m = max_mult;

    s.trade_size = NormalizeDouble(MathFloor(m * s.size_mult) * s.min_volume, s.volume_digits);
    g_max_trade_size = MathMax(g_max_trade_size, s.trade_size);
}

//+------------------------------------------------------------------+
//| Calculate lot size for SELL grid (survive% approach)             |
//+------------------------------------------------------------------+
void CalcLotSizeSell(SymbolState &s, double bid) {
    s.trade_size = 0;

    double equity = AccountInfoDouble(ACCOUNT_EQUITY);
    double used_margin = AccountInfoDouble(ACCOUNT_MARGIN);
    double stopout_level = AccountInfoDouble(ACCOUNT_MARGIN_SO_SO);

    // Target: price rises survive% from reference
    double ref_price = (s.volume_open == 0) ? bid : s.lowest_price;
    double end_price = ref_price * (100.0 + s.survive) / 100.0;
    double distance = end_price - bid;
    if(distance <= 0) return;

    // Equity at target (only this grid's loss)
    double eq_at_target = equity - s.volume_open * distance * s.contract_size;
    if(used_margin > 0 && (eq_at_target / used_margin * 100) <= stopout_level) return;

    double num_trades = MathFloor(distance / s.spacing);
    if(num_trades < 1) num_trades = 1;

    double unit = s.min_volume;
    double d_equity = s.contract_size * unit * s.spacing * (num_trades * (num_trades + 1) / 2);
    double spread = s.point * SymbolInfoInteger(s.symbol, SYMBOL_SPREAD);
    d_equity += num_trades * unit * spread * s.contract_size;

    double unit_margin = CalcUnitMargin(s, bid, end_price, num_trades);

    double S = stopout_level / 100.0;
    double denominator = d_equity + S * unit_margin;
    if(denominator <= 0) return;

    double m = (eq_at_target - S * used_margin) / denominator;
    m = MathMax(1.0, m);

    double max_mult = s.max_volume / s.min_volume;
    if(m > max_mult) m = max_mult;

    s.trade_size = NormalizeDouble(MathFloor(m * s.size_mult) * s.min_volume, s.volume_digits);
    g_max_trade_size = MathMax(g_max_trade_size, s.trade_size);
}

//+------------------------------------------------------------------+
//| Open a BUY order                                                 |
//+------------------------------------------------------------------+
bool OpenBuy(SymbolState &s, double ask) {
    if(s.trade_size < s.min_volume) return false;

    double lots = MathMin(s.trade_size, s.max_volume);
    lots = NormalizeDouble(lots, s.volume_digits);
    double spread = s.point * SymbolInfoInteger(s.symbol, SYMBOL_SPREAD);

    ZeroMemory(g_req);
    g_req.action       = TRADE_ACTION_DEAL;
    g_req.symbol       = s.symbol;
    g_req.deviation    = 5;
    g_req.type_filling = s.filling_mode;
    g_req.magic        = s.magic;
    g_req.type         = ORDER_TYPE_BUY;
    g_req.price        = ask;
    g_req.volume       = lots;
    g_req.tp           = ask + spread + s.spacing;

    ZeroMemory(g_res);
    return OrderSend(g_req, g_res);
}

//+------------------------------------------------------------------+
//| Open a SELL order                                                |
//+------------------------------------------------------------------+
bool OpenSell(SymbolState &s, double bid) {
    if(s.trade_size < s.min_volume) return false;

    double lots = MathMin(s.trade_size, s.max_volume);
    lots = NormalizeDouble(lots, s.volume_digits);
    double spread = s.point * SymbolInfoInteger(s.symbol, SYMBOL_SPREAD);

    ZeroMemory(g_req);
    g_req.action       = TRADE_ACTION_DEAL;
    g_req.symbol       = s.symbol;
    g_req.deviation    = 5;
    g_req.type_filling = s.filling_mode;
    g_req.magic        = s.magic;
    g_req.type         = ORDER_TYPE_SELL;
    g_req.price        = bid;
    g_req.volume       = lots;
    g_req.tp           = bid - spread - s.spacing;

    ZeroMemory(g_res);
    return OrderSend(g_req, g_res);
}

//+------------------------------------------------------------------+
//| Margin calculation per unit batch                                |
//+------------------------------------------------------------------+
double CalcUnitMargin(const SymbolState &s, double start_price, double end_price,
                      double num_trades) {
    double unit = s.min_volume;
    double unit_margin = 0;

    switch(s.calc_mode) {
    case SYMBOL_CALC_MODE_CFDLEVERAGE:
        unit_margin = (unit * s.contract_size * (start_price + end_price) / 2)
                      / s.leverage * s.initial_margin_rate;
        break;
    case SYMBOL_CALC_MODE_FOREX:
        unit_margin = unit * s.contract_size / s.leverage * s.initial_margin_rate;
        break;
    case SYMBOL_CALC_MODE_FOREX_NO_LEVERAGE:
        unit_margin = unit * s.contract_size * s.initial_margin_rate;
        break;
    case SYMBOL_CALC_MODE_CFD:
        unit_margin = (unit * s.contract_size * (start_price + end_price) / 2)
                      * s.initial_margin_rate;
        break;
    }
    return unit_margin * num_trades;
}

//+------------------------------------------------------------------+
void OnDeinit(const int reason) {
    Print("@!@ CrossHedge Results:");
    Print("@!@ max balance: ", g_max_balance);
    Print("@!@ max positions: ", g_max_positions);
    Print("@!@ max used funds: ", g_max_used_funds);
    Print("@!@ max trade size: ", g_max_trade_size);
    Print("@!@ ", g_symA.symbol, " final positions: ", g_symA.position_count,
          " vol: ", g_symA.volume_open);
    Print("@!@ ", g_symB.symbol, " final positions: ", g_symB.position_count,
          " vol: ", g_symB.volume_open);
}

//+------------------------------------------------------------------+
double OnTester() {
    return g_max_positions;
}
//+------------------------------------------------------------------+
