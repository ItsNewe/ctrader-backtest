//+------------------------------------------------------------------+
//| fill_up_optimized.mq5 - Runtime optimized version               |
//+------------------------------------------------------------------+
#include <Trade\Trade.mqh>

CTrade m_trade;

input double survive = 2.5;
input double size = 1;
input double spacing = 1;

// Cached constants (set once in OnInit)
double g_min_volume, g_max_volume, g_point, g_contract_size;
double g_initial_margin_rate, g_maintenance_margin_rate;
double g_initial_margin;  // SYMBOL_MARGIN_INITIAL override
long g_leverage;
int g_calc_mode, g_volume_digits;
ENUM_ORDER_TYPE_FILLING g_filling_mode;

// State variables
double g_lowest_buy, g_highest_buy;
double g_closest_above, g_closest_below;  // Distance to nearest position
double g_spacing_buy, g_trade_size_buy;
double g_volume_open;

// Stats
double g_max_balance, g_max_used_funds, g_max_trade_size;
int g_max_positions;

// Reusable request/result
MqlTradeRequest g_req;
MqlTradeResult g_res;

//+------------------------------------------------------------------+
int OnInit() {
    // Cache all static symbol/account info
    g_point = SymbolInfoDouble(_Symbol, SYMBOL_POINT);
    g_min_volume = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN);
    g_max_volume = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MAX);
    g_contract_size = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);
    g_leverage = AccountInfoInteger(ACCOUNT_LEVERAGE);
    g_calc_mode = (int)SymbolInfoInteger(_Symbol, SYMBOL_TRADE_CALC_MODE);
    SymbolInfoMarginRate(_Symbol, ORDER_TYPE_BUY, g_initial_margin_rate, g_maintenance_margin_rate);
    g_initial_margin = SymbolInfoDouble(_Symbol, SYMBOL_MARGIN_INITIAL);

    // Volume digits
    g_volume_digits = (g_min_volume == 0.01) ? 2 : (g_min_volume == 0.1) ? 1 : 0;

    // Cache filling mode
    long filling = SymbolInfoInteger(_Symbol, SYMBOL_FILLING_MODE);
    g_filling_mode = (filling == SYMBOL_FILLING_FOK) ? ORDER_FILLING_FOK :
                     (filling == SYMBOL_FILLING_IOC) ? ORDER_FILLING_IOC :
                     (filling == 4) ? ORDER_FILLING_BOC : ORDER_FILLING_FOK;

    // Pre-fill static request fields
    ZeroMemory(g_req);
    g_req.action = TRADE_ACTION_DEAL;
    g_req.symbol = _Symbol;
    g_req.deviation = 1;
    g_req.type_filling = g_filling_mode;

    m_trade.SetAsyncMode(true);
    m_trade.LogLevel(LOG_LEVEL_NO);

    // Initialize state
    g_spacing_buy = spacing;
    g_trade_size_buy = size * g_min_volume;
    g_lowest_buy = DBL_MAX;
    g_highest_buy = DBL_MIN;
    g_volume_open = 0;

    // Scan existing positions (need current ask for gap tracking)
    double init_ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    ScanPositions(init_ask);

    return INIT_SUCCEEDED;
}

//+------------------------------------------------------------------+
void OnTick() {
    double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);

    // Update stats
    double bal = AccountInfoDouble(ACCOUNT_BALANCE);
    double eq = AccountInfoDouble(ACCOUNT_EQUITY);
    double margin = AccountInfoDouble(ACCOUNT_MARGIN);
    int posTotal = PositionsTotal();

    g_max_balance = MathMax(g_max_balance, bal);
    g_max_positions = MathMax(g_max_positions, posTotal);
    g_max_used_funds = MathMax(g_max_used_funds, bal - eq + margin);

    // Scan positions
    ScanPositions(ask);

    // Check for new trade
    if(posTotal == 0) {
        CalcLotSize(ask);
        if(OpenBuy(ask)) {
            g_highest_buy = g_lowest_buy = ask;
        }
    }
    else if(g_lowest_buy >= ask + g_spacing_buy) {
        CalcLotSize(ask);
        if(OpenBuy(ask)) g_lowest_buy = ask;
    }
    else if(g_highest_buy <= ask - g_spacing_buy) {
        CalcLotSize(ask);
        if(OpenBuy(ask)) g_highest_buy = ask;
    }
    // Gap fill: if there's a hole in the middle of the grid
    else if((g_closest_above >= g_spacing_buy) && (g_closest_below >= g_spacing_buy)) {
        CalcLotSize(ask);
        OpenBuy(ask);
    }
}

//+------------------------------------------------------------------+
void ScanPositions(double ask) {
    g_lowest_buy = DBL_MAX;
    g_highest_buy = DBL_MIN;
    g_closest_above = DBL_MAX;
    g_closest_below = DBL_MAX;
    g_volume_open = 0;

    int total = PositionsTotal();
    for(int i = total - 1; i >= 0; i--) {
        ulong ticket = PositionGetTicket(i);
        if(ticket == 0) continue;
        if(PositionGetInteger(POSITION_TYPE) != POSITION_TYPE_BUY) continue;

        double price = PositionGetDouble(POSITION_PRICE_OPEN);
        double lots = PositionGetDouble(POSITION_VOLUME);

        g_volume_open += lots;
        if(price < g_lowest_buy) g_lowest_buy = price;
        if(price > g_highest_buy) g_highest_buy = price;

        // Track distance to nearest positions above/below current ask
        if(price >= ask) {
            g_closest_above = MathMin(g_closest_above, price - ask);
        }
        if(price <= ask) {
            g_closest_below = MathMin(g_closest_below, ask - price);
        }
    }
}

//+------------------------------------------------------------------+
bool OpenBuy(double ask) {
    if(g_trade_size_buy < g_min_volume) return false;

    double lots = MathMin(g_trade_size_buy, g_max_volume);
    lots = NormalizeDouble(lots, g_volume_digits);

    double spread = g_point * SymbolInfoInteger(_Symbol, SYMBOL_SPREAD);

    g_req.type = ORDER_TYPE_BUY;
    g_req.price = ask;
    g_req.volume = lots;
    g_req.tp = ask + spread + g_spacing_buy;

    ZeroMemory(g_res);
    if(!OrderSend(g_req, g_res)) {
        ResetLastError();
        return false;
    }
    return true;
}

//+------------------------------------------------------------------+
void CalcLotSize(double ask) {
    g_trade_size_buy = 0;

    double equity = AccountInfoDouble(ACCOUNT_EQUITY);
    double used_margin = AccountInfoDouble(ACCOUNT_MARGIN);
    double margin_level = AccountInfoDouble(ACCOUNT_MARGIN_LEVEL);
    double stopout_level = AccountInfoDouble(ACCOUNT_MARGIN_SO_SO);

    if(margin_level <= 0) margin_level = 10000;

    // Calculate survive distance
    double ref_price = (PositionsTotal() == 0) ? ask : g_highest_buy;
    double end_price = ref_price * (100.0 - survive) / 100.0;
    double distance = ask - end_price;
    if(distance <= 0) return;

    double num_trades = MathFloor(distance / g_spacing_buy);
    if(num_trades < 1) num_trades = 1;

    // Equity at target price with current positions
    double eq_at_target = equity - g_volume_open * distance * g_contract_size;
    if(used_margin > 0 && (eq_at_target / used_margin * 100) <= stopout_level) return;

    // Calculate per-unit costs
    double unit = g_min_volume;
    double d_equity = g_contract_size * unit * g_spacing_buy * (num_trades * (num_trades + 1) / 2);
    double spread = g_point * SymbolInfoInteger(_Symbol, SYMBOL_SPREAD);
    d_equity += num_trades * unit * spread * g_contract_size;

    // Calculate margin per unit based on calc mode
    double unit_margin = 0;
    switch(g_calc_mode) {
        case SYMBOL_CALC_MODE_CFDLEVERAGE:
            unit_margin = (unit * g_contract_size * (ask + end_price) / 2) / g_leverage * g_initial_margin_rate;
            break;
        case SYMBOL_CALC_MODE_FOREX:
            unit_margin = unit * (g_initial_margin > 0 ? g_initial_margin : g_contract_size / g_leverage) * g_initial_margin_rate;
            break;
        case SYMBOL_CALC_MODE_FOREX_NO_LEVERAGE:
            unit_margin = unit * g_contract_size * g_initial_margin_rate;
            break;
        case SYMBOL_CALC_MODE_CFD:
            unit_margin = (unit * g_contract_size * (ask + end_price) / 2) * g_initial_margin_rate;
            break;
    }
    unit_margin *= num_trades;

    // Direct formula: m = (E - S×U) / (d + S×u)
    double S = stopout_level / 100.0;
    double denominator = d_equity + S * unit_margin;

    if(denominator <= 0) return;  // Degenerate case

    double m = (eq_at_target - S * used_margin) / denominator;

    // Clamp to valid range
    // Force at least 1 to match original behavior
    m = MathMax(1.0, m);

    double max_mult = g_max_volume / g_min_volume;
    if(m > max_mult) m = max_mult;
    m = MathFloor(m);  // Integer multiplier

    g_trade_size_buy = NormalizeDouble(m * g_min_volume, g_volume_digits);
    g_max_trade_size = MathMax(g_max_trade_size, g_trade_size_buy);
}

//+------------------------------------------------------------------+
void OnDeinit(const int reason) {
    Print("@!@ max balance: ", g_max_balance);
    Print("@!@ max positions: ", g_max_positions);
    Print("@!@ max used funds: ", g_max_used_funds);
    Print("@!@ max trade size: ", g_max_trade_size);
}

//+------------------------------------------------------------------+
double OnTester() {
    return g_max_balance;
}
//+------------------------------------------------------------------+
