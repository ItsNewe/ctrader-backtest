//+------------------------------------------------------------------+
//| open_both_directions_optimized.mq5                               |
//| Based on fill_up_optimized.mq5                                   |
//| Opens both Long and Short positions using the Fill Up logic      |
//+------------------------------------------------------------------+
#include <Trade\Trade.mqh>

CTrade m_trade;

//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
input double survive = 2.5; // Survival percentage (e.g., 2.5% move against)
input double size = 1;      // Base size multiplier
input double spacing = 1;   // Grid spacing
input bool long_grid = true;
input bool short_grid = true;

// Cached constants
double g_min_volume, g_max_volume, g_point, g_contract_size;
double g_initial_margin_rate, g_maintenance_margin_rate;
long g_leverage;
int g_calc_mode, g_volume_digits;
ENUM_ORDER_TYPE_FILLING g_filling_mode;

// --- BUY State Variables ---
double g_lowest_buy, g_highest_buy;
double g_closest_buy_above, g_closest_buy_below;
double g_trade_size_buy;
double g_volume_open_buy;

// --- SELL State Variables ---
double g_lowest_sell, g_highest_sell;
double g_closest_sell_above, g_closest_sell_below;
double g_trade_size_sell;
double g_volume_open_sell;

// Shared
double g_spacing; // Applied to both

// Stats
double g_max_balance, g_max_used_funds, g_max_trade_size;
int g_max_positions;

// Reusable request/result
MqlTradeRequest g_req;
MqlTradeResult g_res;

//+------------------------------------------------------------------+
int OnInit() {
// Cache symbol info
    g_point = SymbolInfoDouble(_Symbol, SYMBOL_POINT);
    g_min_volume = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN);
    g_max_volume = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MAX);
    g_contract_size = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);
    g_leverage = AccountInfoInteger(ACCOUNT_LEVERAGE);
    g_calc_mode = (int)SymbolInfoInteger(_Symbol, SYMBOL_TRADE_CALC_MODE);
    SymbolInfoMarginRate(_Symbol, ORDER_TYPE_BUY, g_initial_margin_rate, g_maintenance_margin_rate);

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
    g_req.deviation = 5;
    g_req.type_filling = g_filling_mode;

    m_trade.SetAsyncMode(true);
    m_trade.LogLevel(LOG_LEVEL_NO);

// Initialize state
    g_spacing = spacing;
    g_trade_size_buy = size * g_min_volume;
    g_trade_size_sell = size * g_min_volume;

// Scan current state
    ScanPositions();

    return INIT_SUCCEEDED; }

//+------------------------------------------------------------------+
void OnTick() {
    double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);

// Update stats
    double bal = AccountInfoDouble(ACCOUNT_BALANCE);
    double eq = AccountInfoDouble(ACCOUNT_EQUITY);
    double margin = AccountInfoDouble(ACCOUNT_MARGIN);
    int posTotal = PositionsTotal();

    g_max_balance = MathMax(g_max_balance, bal);
    g_max_positions = MathMax(g_max_positions, posTotal);
    g_max_used_funds = MathMax(g_max_used_funds, bal - eq + margin);

// Refresh grid state
    ScanPositions();

// --- BUY LOGIC ---
    if(long_grid) {
        if(g_volume_open_buy == 0) {
            CalcLotSizeBuy(ask);
            if(OpenBuy(ask)) {
                g_highest_buy = ask;
                g_lowest_buy = ask; } }
        else {
            // Price dropped below grid -> Buy lower
            if(g_lowest_buy >= ask + g_spacing) {
                CalcLotSizeBuy(ask);
                if(OpenBuy(ask)) g_lowest_buy = ask; }
            // Price rose above grid -> Buy higher
            else if(g_highest_buy <= ask - g_spacing) {
                CalcLotSizeBuy(ask);
                if(OpenBuy(ask)) g_highest_buy = ask; }
            // Fill internal gap
            else if((g_closest_buy_above >= g_spacing) && (g_closest_buy_below >= g_spacing)) {
                CalcLotSizeBuy(ask);
                OpenBuy(ask); } } }

// --- SELL LOGIC ---
    if(short_grid) {
        if(g_volume_open_sell == 0) {
            CalcLotSizeSell(bid);
            if(OpenSell(bid)) {
                g_highest_sell = bid;
                g_lowest_sell = bid; } }
        else {
            // Price rose above grid -> Sell higher
            if(g_highest_sell <= bid - g_spacing) {
                CalcLotSizeSell(bid);
                if(OpenSell(bid)) g_highest_sell = bid; }
            // Price dropped below grid -> Sell lower
            else if(g_lowest_sell >= bid + g_spacing) {
                CalcLotSizeSell(bid);
                if(OpenSell(bid)) g_lowest_sell = bid; }
            // Fill internal gap
            else if((g_closest_sell_above >= g_spacing) && (g_closest_sell_below >= g_spacing)) {
                CalcLotSizeSell(bid);
                OpenSell(bid); } } } }

//+------------------------------------------------------------------+
void ScanPositions() {
// Reset Buy State
    g_lowest_buy = DBL_MAX;
    g_highest_buy = DBL_MIN;
    g_closest_buy_above = DBL_MAX;
    g_closest_buy_below = DBL_MAX;
    g_volume_open_buy = 0;

// Reset Sell State
    g_lowest_sell = DBL_MAX;
    g_highest_sell = DBL_MIN;
    g_closest_sell_above = DBL_MAX;
    g_closest_sell_below = DBL_MAX;
    g_volume_open_sell = 0;

    double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);

    int total = PositionsTotal();
    for(int i = total - 1; i >= 0; i--) {
        ulong ticket = PositionGetTicket(i);
        if(ticket == 0) continue;
        if(PositionGetString(POSITION_SYMBOL) != _Symbol) continue;

        long type = PositionGetInteger(POSITION_TYPE);
        double price = PositionGetDouble(POSITION_PRICE_OPEN);
        double lots = PositionGetDouble(POSITION_VOLUME);

        if(type == POSITION_TYPE_BUY) {
            g_volume_open_buy += lots;
            if(price < g_lowest_buy) g_lowest_buy = price;
            if(price > g_highest_buy) g_highest_buy = price;

            if(price >= ask) g_closest_buy_above = MathMin(g_closest_buy_above, price - ask);
            if(price <= ask) g_closest_buy_below = MathMin(g_closest_buy_below, ask - price); }
        else if(type == POSITION_TYPE_SELL) {
            g_volume_open_sell += lots;
            if(price < g_lowest_sell) g_lowest_sell = price;
            if(price > g_highest_sell) g_highest_sell = price;

            // For sells, compare against Bid
            if(price >= bid) g_closest_sell_above = MathMin(g_closest_sell_above, price - bid);
            if(price <= bid) g_closest_sell_below = MathMin(g_closest_sell_below, bid - price); } } }

//+------------------------------------------------------------------+
bool OpenBuy(double ask) {
//Print("OpenBuy: ", g_trade_size_buy);
    if(g_trade_size_buy < g_min_volume) return false;
    //if(g_trade_size_buy < g_min_volume) g_trade_size_buy = g_min_volume;

    double lots = MathMin(g_trade_size_buy, g_max_volume);
    lots = NormalizeDouble(lots, g_volume_digits);
    double spread = g_point * SymbolInfoInteger(_Symbol, SYMBOL_SPREAD);

    g_req.type = ORDER_TYPE_BUY;
    g_req.price = ask;
    g_req.volume = lots;
    g_req.tp = ask + spread + g_spacing;

    ZeroMemory(g_res);
    return OrderSend(g_req, g_res); }

//+------------------------------------------------------------------+
bool OpenSell(double bid) {
//Print("OpenSell: ", g_trade_size_sell);
    if(g_trade_size_sell < g_min_volume) return false;
    //if(g_trade_size_sell < g_min_volume) g_trade_size_sell = g_min_volume;

    double lots = MathMin(g_trade_size_sell, g_max_volume);
    lots = NormalizeDouble(lots, g_volume_digits);
    double spread = g_point * SymbolInfoInteger(_Symbol, SYMBOL_SPREAD);

    g_req.type = ORDER_TYPE_SELL;
    g_req.price = bid;
    g_req.volume = lots;
// TP for sell is below price
    g_req.tp = bid - spread - g_spacing;

    ZeroMemory(g_res);
    return OrderSend(g_req, g_res); }

//+------------------------------------------------------------------+
void CalcLotSizeBuy(double ask) {
    g_trade_size_buy = 0;

    double equity = AccountInfoDouble(ACCOUNT_EQUITY);
    double used_margin = AccountInfoDouble(ACCOUNT_MARGIN);
    double stopout_level = AccountInfoDouble(ACCOUNT_MARGIN_SO_SO);

// Target Price: Price Drops X%
    double ref_price = (g_volume_open_buy == 0) ? ask : g_highest_buy;
    double end_price = ref_price * (100.0 - survive) / 100.0;
    double distance = ask - end_price;
    if(distance <= 0) return;

// Equity check at target
    double eq_at_target = equity - g_volume_open_buy * distance * g_contract_size;
// Note: This logic assumes Sell profits offset Buy losses or are calculated separately.
// For safety in a dual grid, we treat the grid in isolation for survival calculation.
    if(used_margin > 0 && (eq_at_target / used_margin * 100) <= stopout_level) return;

    double num_trades = MathFloor(distance / g_spacing);
    if(num_trades < 1) num_trades = 1;

    double unit = g_min_volume;
    double d_equity = g_contract_size * unit * (num_trades * (num_trades + 1) / 2);
    double spread = g_point * SymbolInfoInteger(_Symbol, SYMBOL_SPREAD);
    d_equity += num_trades * unit * spread * g_contract_size;

    double unit_margin = CalcUnitMargin(ask, end_price, num_trades);

    double S = stopout_level / 100.0;
    double denominator = d_equity + S * unit_margin;
    if(denominator <= 0) return;

    double m = (eq_at_target - S * used_margin) / denominator;
    m = MathMax(1.0, m);

    double max_mult = g_max_volume / g_min_volume;
    if(m > max_mult) m = max_mult;

    g_trade_size_buy = NormalizeDouble(MathFloor(m) * g_min_volume, g_volume_digits);
    g_max_trade_size = MathMax(g_max_trade_size, g_trade_size_buy); }

//+------------------------------------------------------------------+
void CalcLotSizeSell(double bid) {
    g_trade_size_sell = 0;

    double equity = AccountInfoDouble(ACCOUNT_EQUITY);
    double used_margin = AccountInfoDouble(ACCOUNT_MARGIN);
    double stopout_level = AccountInfoDouble(ACCOUNT_MARGIN_SO_SO);

// Target Price: Price Rises X%
    double ref_price = (g_volume_open_sell == 0) ? bid : g_lowest_sell;
    double end_price = ref_price * (100.0 + survive) / 100.0;
    double distance = end_price - bid;
    if(distance <= 0) return;

// Equity check at target (Loss calculation for Shorts)
    double eq_at_target = equity - g_volume_open_sell * distance * g_contract_size;
    if(used_margin > 0 && (eq_at_target / used_margin * 100) <= stopout_level) return;

    double num_trades = MathFloor(distance / g_spacing);
    if(num_trades < 1) num_trades = 1;

    double unit = g_min_volume;
    double d_equity = g_contract_size * unit * (num_trades * (num_trades + 1) / 2);
    double spread = g_point * SymbolInfoInteger(_Symbol, SYMBOL_SPREAD);
    d_equity += num_trades * unit * spread * g_contract_size;

    double unit_margin = CalcUnitMargin(bid, end_price, num_trades);

    double S = stopout_level / 100.0;
    double denominator = d_equity + S * unit_margin;
    if(denominator <= 0) return;

    double m = (eq_at_target - S * used_margin) / denominator;
    m = MathMax(1.0, m);

    double max_mult = g_max_volume / g_min_volume;
    if(m > max_mult) m = max_mult;

    g_trade_size_sell = NormalizeDouble(MathFloor(m) * g_min_volume, g_volume_digits);
    g_max_trade_size = MathMax(g_max_trade_size, g_trade_size_sell); }

//+------------------------------------------------------------------+
// Helper to calculate margin per unit batch
//+------------------------------------------------------------------+
double CalcUnitMargin(double start_price, double end_price, double num_trades) {
    double unit = g_min_volume;
    double unit_margin = 0;

    switch(g_calc_mode) {
    case SYMBOL_CALC_MODE_CFDLEVERAGE:
        unit_margin = (unit * g_contract_size * (start_price + end_price) / 2) / g_leverage * g_initial_margin_rate;
        break;
    case SYMBOL_CALC_MODE_FOREX:
        unit_margin = unit * g_contract_size / g_leverage * g_initial_margin_rate;
        break;
    case SYMBOL_CALC_MODE_FOREX_NO_LEVERAGE:
        unit_margin = unit * g_contract_size * g_initial_margin_rate;
        break;
    case SYMBOL_CALC_MODE_CFD:
        unit_margin = (unit * g_contract_size * (start_price + end_price) / 2) * g_initial_margin_rate;
        break; }
    return unit_margin * num_trades; }

//+------------------------------------------------------------------+
void OnDeinit(const int reason) {
    Print("@!@ max balance: ", g_max_balance);
    Print("@!@ max positions: ", g_max_positions);
    Print("@!@ max used funds: ", g_max_used_funds);
    Print("@!@ max trade size: ", g_max_trade_size); }

//+------------------------------------------------------------------+
double OnTester() {
    return g_max_positions; }
//+------------------------------------------------------------------+
