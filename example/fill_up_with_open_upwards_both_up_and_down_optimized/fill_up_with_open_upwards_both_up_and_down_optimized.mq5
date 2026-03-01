//+------------------------------------------------------------------+
//| Combined_Strategies_Optimized_Fixed.mq5                          |
//| Combined: Open Upwards + Fill Up                                 |
//| Optimization: Single-pass position scanning per tick             |
//+------------------------------------------------------------------+
#property copyright "Combined EA Optimized"
#property version   "1.03"

#include <Trade\Trade.mqh>
#include <Trade\PositionInfo.mqh>
#include <Trade\AccountInfo.mqh>

//+------------------------------------------------------------------+
//| Inputs                                                           |
//+------------------------------------------------------------------+
input group "=== Shared Settings ==="
input double Inp_Base_Survive       = 5.0;   // Base Survive % (Reference)

input group "=== Survive Multipliers ==="
input double Inp_Mult_OU_Down       = 1.0;   // Multiplier for OU (Grid Down)
input double Inp_Mult_OU_Up         = 2.0;   // Multiplier for OU (Upwards)
input double Inp_Mult_FU            = 0.5;   // Multiplier for Fill Up

//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
input group "=== Strategy 1: Open Upwards (OU) ==="
input int    OU_Sizing              = 0;     // OU Sizing: 0=constant, 1=incremental
input int    OU_Closing_Mode        = 0;     // OU Closing Mode

//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
input group "=== Strategy 2: Fill Up (FU) ==="
input double FU_Size                = 1.0;   // FU Size multiplier
input double FU_Spacing             = 1.0;   // FU Spacing

//+------------------------------------------------------------------+
//| Global Objects                                                   |
//+------------------------------------------------------------------+
CAccountInfo m_account;
CPositionInfo m_position;
CTrade m_trade;

//+------------------------------------------------------------------+
//| Global Constants & Cache                                         |
//+------------------------------------------------------------------+
double g_min_volume, g_max_volume, g_point, g_contract_size;
double g_initial_margin_rate, g_maintenance_margin_rate;
double g_initial_margin;  // SYMBOL_MARGIN_INITIAL override
long   g_leverage;
int    g_calc_mode, g_volume_digits;
ENUM_ORDER_TYPE_FILLING g_filling_mode;
string g_symbol_currency_base, g_symbol_currency_profit, g_account_currency;

// Survive Percentages
double g_survive_ou_down;
double g_survive_ou_up;
double g_survive_fu;

// Current Tick Data
double g_ask, g_bid, g_spread;
double g_equity, g_balance, g_margin, g_margin_level, g_margin_so;

//+------------------------------------------------------------------+
//| STRATEGY STATE VARIABLES (Aggregated)                            |
//+------------------------------------------------------------------+
// OU - Down (Grid with comments)
double OU_value_of_buy_trades;       // Sum of profit
double OU_lowest_buy, OU_highest_buy;
double OU_volume_of_open_trades;     // Volume of Down trades
double OU_trade_size_buy, OU_spacing_buy;
int    OU_count_buy;
bool   OU_is_there_unprofitable_buy;

// OU - Up (No comments, TP=0)
double OU_volume_of_open_trades_up;  // FIXED: Renamed for consistency
double OU_up_checked_last_open_price;

// OU - Logic Flags
int    OU_direction, OU_direction_change;
double OU_bid_at_turn_up, OU_ask_at_turn_up;
double OU_bid_at_turn_down, OU_ask_at_turn_down;
bool   OU_close_all_buy_flag, OU_close_all_profitable_buy_flag;

// FU (Fill Up, TP > 0)
double FU_lowest_buy, FU_highest_buy;
double FU_closest_above, FU_closest_below;
double FU_volume_open;
double FU_trade_size_buy;
double FU_spacing_val; // Internal cache for spacing
int    FU_pos_count;

// Stats
double FU_max_balance, FU_max_used_funds;
int    FU_max_positions;

//+------------------------------------------------------------------+
//| Strategy Identification Enum                                     |
//+------------------------------------------------------------------+
enum E_STRAT_TYPE {
    STRAT_NONE,
    STRAT_FU,      // TP > 0
    STRAT_OU_DOWN, // TP = 0, Comment has ";"
    STRAT_OU_UP    // TP = 0, Comment empty or no ";"
};

//+------------------------------------------------------------------+
//| Forward Declarations                                             |
//+------------------------------------------------------------------+
double OU_CalculateMargin(double volume, double start_p, double end_p);
bool OU_SendOrder(double local_unit, bool is_down);
void FU_CalcLotSize();
bool FU_OpenBuy();

//+------------------------------------------------------------------+
//| Helpers                                                          |
//+------------------------------------------------------------------+
double AccountCurrencyToInstrumentCost(double cost) {
    if(g_symbol_currency_base == g_account_currency) return cost;
    if(g_symbol_currency_profit == g_account_currency) return cost * g_bid;

    string base = g_account_currency + g_symbol_currency_base;
    double r = SymbolInfoDouble(base, SYMBOL_BID);
    if(r > 0) return cost / r;

    base = g_symbol_currency_base + g_account_currency;
    r = SymbolInfoDouble(base, SYMBOL_BID);
    if(r > 0) return cost * r;

    return 0.0; }

//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
double GetSwap(ulong ticket) {
    return AccountCurrencyToInstrumentCost(PositionGetDouble(POSITION_SWAP)); }

//+------------------------------------------------------------------+
//| Init                                                             |
//+------------------------------------------------------------------+
int OnInit() {
    g_point = SymbolInfoDouble(_Symbol, SYMBOL_POINT);
    g_min_volume = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN);
    g_max_volume = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MAX);
    g_contract_size = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);
    g_leverage = AccountInfoInteger(ACCOUNT_LEVERAGE);
    g_calc_mode = (int)SymbolInfoInteger(_Symbol, SYMBOL_TRADE_CALC_MODE);
    SymbolInfoMarginRate(_Symbol, ORDER_TYPE_BUY, g_initial_margin_rate, g_maintenance_margin_rate);
    g_initial_margin = SymbolInfoDouble(_Symbol, SYMBOL_MARGIN_INITIAL);

    g_volume_digits = (g_min_volume == 0.01) ? 2 : (g_min_volume == 0.1) ? 1 : 0;

    long filling = SymbolInfoInteger(_Symbol, SYMBOL_FILLING_MODE);
    g_filling_mode = (filling == SYMBOL_FILLING_FOK) ? ORDER_FILLING_FOK :
                     (filling == SYMBOL_FILLING_IOC) ? ORDER_FILLING_IOC :
                     (filling == 4) ? ORDER_FILLING_BOC : ORDER_FILLING_FOK;

    g_symbol_currency_base = SymbolInfoString(_Symbol, SYMBOL_CURRENCY_BASE);
    g_symbol_currency_profit = SymbolInfoString(_Symbol, SYMBOL_CURRENCY_PROFIT);
    g_account_currency = AccountInfoString(ACCOUNT_CURRENCY);

// Calc Survive
    g_survive_ou_down = Inp_Base_Survive * Inp_Mult_OU_Down;
    g_survive_ou_up   = Inp_Base_Survive * Inp_Mult_OU_Up;
    g_survive_fu      = Inp_Base_Survive * Inp_Mult_FU;

// Init FU Spacing
    FU_spacing_val = FU_Spacing;

// Trade Object Setup
    m_trade.SetExpertMagicNumber(0);
    m_trade.SetTypeFilling(g_filling_mode);
    m_trade.SetDeviationInPoints(10);
    m_trade.SetAsyncMode(true);
    m_trade.LogLevel(LOG_LEVEL_NO);

// Init State
    OU_ClearFlags();
    OU_ClearTurnMarks();

// Initial Scan
    RefreshMarketData();
    ScanPositions();

    return INIT_SUCCEEDED; }

//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
void OnDeinit(const int reason) {
    Print("=== Deinit Stats ===");
    Print("FU Max Balance: ", FU_max_balance);
    Print("FU Max Positions: ", FU_max_positions); }

//+------------------------------------------------------------------+
//| Main OnTick                                                      |
//+------------------------------------------------------------------+
void OnTick() {
// 1. Update Market Data
    RefreshMarketData();

// 2. Scan All Positions (One pass optimization)
    ScanPositions();

// 3. Logic & Execution

// --- OU Logic ---
    OU_DirectionCheck();      // Check turn points
    OU_ProcessClosures();     // Close if flags set

    OU_OpenNewUpWUp();        // Entry Up logic
    OU_OpenNewUpWDown();      // Entry Down logic

// --- FU Logic ---
    FU_OnTickLogic(); }

//+------------------------------------------------------------------+
//| Optimized Scanning Logic                                         |
//+------------------------------------------------------------------+
void RefreshMarketData() {
    g_ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    g_bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
    g_spread = g_point * SymbolInfoInteger(_Symbol, SYMBOL_SPREAD);

    g_equity = AccountInfoDouble(ACCOUNT_EQUITY);
    g_balance = AccountInfoDouble(ACCOUNT_BALANCE);
    g_margin = AccountInfoDouble(ACCOUNT_MARGIN);
    g_margin_level = AccountInfoDouble(ACCOUNT_MARGIN_LEVEL);
    g_margin_so = AccountInfoDouble(ACCOUNT_MARGIN_SO_SO);

    if(g_margin_level <= 0) g_margin_level = 10000; }

//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
void ScanPositions() {
// Reset Aggregators
    OU_value_of_buy_trades = 0;
    OU_count_buy = 0;
    OU_volume_of_open_trades = 0;
    OU_lowest_buy = DBL_MAX;
    OU_highest_buy = DBL_MIN;
    OU_is_there_unprofitable_buy = false;
    if(OU_Sizing == 1) OU_trade_size_buy = 0;

    OU_volume_of_open_trades_up = 0; // FIXED: Using correct variable name

    FU_lowest_buy = DBL_MAX;
    FU_highest_buy = DBL_MIN;
    FU_closest_above = DBL_MAX;
    FU_closest_below = DBL_MAX;
    FU_volume_open = 0;
    FU_pos_count = 0;

// Single Loop
    int total = PositionsTotal();
    for(int i = total - 1; i >= 0; i--) {
        ulong ticket = PositionGetTicket(i);
        if(ticket == 0) continue;
        if(PositionGetString(POSITION_SYMBOL) != _Symbol) continue;

        // Fetch raw data once
        double tp = PositionGetDouble(POSITION_TP);
        double open_price = PositionGetDouble(POSITION_PRICE_OPEN);
        double lots = PositionGetDouble(POSITION_VOLUME);

        // Determine Type
        E_STRAT_TYPE type = STRAT_NONE;
        string cmnt = "";

        if(tp > DBL_EPSILON) {
            type = STRAT_FU; }
        else {
            cmnt = PositionGetString(POSITION_COMMENT);
            // StringFind is faster than StringSplit for simple existence check
            if(StringFind(cmnt, ";") >= 0) type = STRAT_OU_DOWN;
            else type = STRAT_OU_UP; }

        // --- Aggregation ---

        if (type == STRAT_FU) {
            FU_pos_count++;
            FU_volume_open += lots;
            if(open_price < FU_lowest_buy) FU_lowest_buy = open_price;
            if(open_price > FU_highest_buy) FU_highest_buy = open_price;

            // Distance tracking
            if(open_price >= g_ask) FU_closest_above = MathMin(FU_closest_above, open_price - g_ask);
            if(open_price <= g_ask) FU_closest_below = MathMin(FU_closest_below, g_ask - open_price);

        }
        else if (type == STRAT_OU_DOWN) {
            double profit = m_position.Profit() + GetSwap(ticket);

            OU_value_of_buy_trades += profit;
            OU_count_buy++;
            OU_volume_of_open_trades += lots;

            if(open_price < OU_lowest_buy) OU_lowest_buy = open_price;
            if(open_price > OU_highest_buy) OU_highest_buy = open_price;

            if(profit < 0) OU_is_there_unprofitable_buy = true;
            if(OU_Sizing == 1) OU_trade_size_buy = MathMax(OU_trade_size_buy, lots);

        }
        else if (type == STRAT_OU_UP) {
            OU_volume_of_open_trades_up += lots; // FIXED: Using correct variable name
        } }

// Post-Scan Adjustments
    if(OU_Sizing == 1) OU_trade_size_buy += g_min_volume;

// Stats Update
    FU_max_balance = MathMax(FU_max_balance, g_balance);
    FU_max_positions = MathMax(FU_max_positions, FU_pos_count);
    FU_max_used_funds = MathMax(FU_max_used_funds, g_balance - g_equity + g_margin); }


//+------------------------------------------------------------------+
//| OU Logic Sections                                                |
//+------------------------------------------------------------------+
void OU_ClearFlags() {
    OU_close_all_buy_flag = false;
    OU_close_all_profitable_buy_flag = false; }

//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
void OU_ClearTurnMarks() {
    OU_bid_at_turn_up = DBL_MAX;
    OU_ask_at_turn_up = DBL_MAX;
    OU_ask_at_turn_down = DBL_MIN;
    OU_bid_at_turn_down = DBL_MIN; }

//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
void OU_DirectionCheck() {
    OU_direction_change = 0;

    OU_bid_at_turn_down = MathMax(g_bid, OU_bid_at_turn_down);
    OU_bid_at_turn_up   = MathMin(g_bid, OU_bid_at_turn_up);
    OU_ask_at_turn_down = MathMax(g_ask, OU_ask_at_turn_down);
    OU_ask_at_turn_up   = MathMin(g_ask, OU_ask_at_turn_up);

// Note: OU_Iterate logic is already done in ScanPositions

    if((OU_direction != -1) && (OU_ask_at_turn_down >= g_ask + g_spread) && (OU_bid_at_turn_down >= g_bid + g_spread)) {
        OU_direction = -1;
        OU_direction_change = -1;

        switch(OU_Closing_Mode) {
        case 0:
            if(OU_value_of_buy_trades > 0) {
                OU_close_all_profitable_buy_flag = true; }
            break;
        case 1:
            if(!OU_is_there_unprofitable_buy) {
                OU_close_all_buy_flag = true; }
            break; }
        OU_ClearTurnMarks(); }

    if((OU_direction != 1) && (OU_bid_at_turn_up <= g_bid - g_spread) && (OU_ask_at_turn_up <= g_ask - g_spread)) {
        OU_direction = 1;
        OU_direction_change = 1;
        OU_ClearTurnMarks(); } }

//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
void OU_ProcessClosures() {
    if(!OU_close_all_buy_flag && !OU_close_all_profitable_buy_flag) return;

// We only loop again if we actually need to close something
    int total = PositionsTotal();
    for(int i = total - 1; i >= 0; i--) {
        ulong ticket = PositionGetTicket(i);
        if(ticket == 0) continue;
        if(!m_position.SelectByTicket(ticket)) continue;
        if(PositionGetString(POSITION_SYMBOL) != _Symbol) continue;

        // Re-identify strictly for closure to be safe
        double tp = PositionGetDouble(POSITION_TP);
        if(tp > DBL_EPSILON) continue; // Skip FU

        string cmnt = PositionGetString(POSITION_COMMENT);
        if(StringFind(cmnt, ";") == -1) continue; // Skip OU UP

        // It's OU DOWN
        double profit = m_position.Profit() + GetSwap(ticket);

        bool close_me = false;
        if(OU_close_all_buy_flag) close_me = true;
        else if(OU_close_all_profitable_buy_flag && profit > 0) close_me = true;

        if(close_me) {
            m_trade.PositionClose(ticket); } }

// Reset flags after processing
    OU_ClearFlags();
// Re-scan to update volumes after closure for immediate accuracy in Sizing logic
    ScanPositions(); }

//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
void OU_SizingBuy() {
    OU_trade_size_buy = 0;
    OU_spacing_buy = 0;

    double equity_at_target = g_equity * g_margin_so / g_margin_level;
    double equity_difference = g_equity - equity_at_target;

    double total_vol = (OU_volume_of_open_trades + OU_volume_of_open_trades_up); // FIXED
    double price_difference = (total_vol > 0) ? equity_difference / (total_vol * g_contract_size) : 0;

    double end_price = g_ask * ((100 - g_survive_ou_down) / 100);
    double distance = end_price - g_ask;

    if((OU_count_buy == 0) || ((g_ask - price_difference) < end_price)) {
        equity_at_target = g_equity - total_vol * MathAbs(distance) * g_contract_size;
        double margin_level_local = (g_margin > 0) ? equity_at_target / g_margin * 100 : 99999;

        if(margin_level_local > g_margin_so) {
            double trade_size = g_min_volume;
            double d_equity = g_contract_size * trade_size * (g_ask - end_price) + trade_size * g_spread * g_contract_size;
            double local_used_margin = OU_CalculateMargin(trade_size, g_ask, end_price);

            double number_of_trades;
            if(g_margin == 0)
                number_of_trades = MathFloor(equity_at_target / (g_margin_so / 100 * local_used_margin + d_equity));
            else
                number_of_trades = MathFloor((equity_at_target - g_margin_so / 100 * g_margin) / (g_margin_so / 100 * local_used_margin + d_equity));

            double len = g_ask - end_price;
            double proportion = (len != 0) ? number_of_trades / len : 0;

            switch(OU_Sizing) {
            case 0:
                if(proportion >= 1) {
                    OU_spacing_buy = 1;
                    OU_trade_size_buy = MathFloor(proportion) * g_min_volume; }
                else {
                    OU_trade_size_buy = g_min_volume;
                    if(number_of_trades > 0)
                        OU_spacing_buy = MathRound((len / number_of_trades) * 100) / 100;
                    else
                        OU_spacing_buy = len; }
                break;
            case 1: {
                double temp = (-1 + MathSqrt(1 + 8 * number_of_trades)) / 2;
                temp = MathFloor(temp);
                if(temp > 1) OU_spacing_buy = len / (temp - 1);
                else OU_spacing_buy = len;
                OU_trade_size_buy = g_min_volume; }
            break; } } } }

//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
double OU_CalculateMargin(double volume, double start_p, double end_p) {
    double res = 0;
    switch(g_calc_mode) {
    case SYMBOL_CALC_MODE_CFDLEVERAGE:
        res = ((volume * g_contract_size * start_p) / g_leverage * g_initial_margin_rate +
               (volume * g_contract_size * end_p) / g_leverage * g_initial_margin_rate) / 2;
        break;
    case SYMBOL_CALC_MODE_FOREX:
        res = (volume * (g_initial_margin > 0 ? g_initial_margin : g_contract_size / g_leverage) * g_initial_margin_rate * 2) / 2;
        break;
    case SYMBOL_CALC_MODE_FOREX_NO_LEVERAGE:
        res = (volume * g_contract_size * g_initial_margin_rate * 2) / 2;
        break;
    case SYMBOL_CALC_MODE_CFD:
        res = ((volume * g_contract_size * start_p * g_initial_margin_rate) +
               (volume * g_contract_size * end_p * g_initial_margin_rate)) / 2;
        break; }
    return res; }

//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
void OU_OpenNewUpWDown() {
    if(OU_count_buy == 0) OU_SizingBuy();

    if(OU_count_buy == 0) {
        OU_SendOrder(OU_trade_size_buy, true); }
    else {
        if(OU_spacing_buy <= 0) return;

        double temp, temp1, temp2, temp3 = 0, temp4, temp5, temp6;

        switch(OU_Sizing) {
        case 0:
            if(OU_lowest_buy > g_ask) {
                temp = (OU_highest_buy - g_ask) / OU_spacing_buy;
                temp1 = MathFloor(temp);
                temp2 = temp - temp1;
                temp3 = temp1;
                double factor = (OU_trade_size_buy / g_min_volume);
                if(factor == 0) factor = 1;
                temp4 = OU_spacing_buy / factor;
                temp5 = MathFloor(temp2 / temp4);
                temp6 = temp3 * OU_trade_size_buy + temp5 * g_min_volume - OU_volume_of_open_trades;
                if(temp6 > 0) {
                    OU_SendOrder(temp6, true); } }
            break;
        case 1:
            if(OU_lowest_buy > g_ask) {
                temp = (OU_highest_buy - g_ask) / OU_spacing_buy;
                temp1 = MathFloor(temp);
                temp2 = temp - temp1;
                if(temp >= 1) {
                    for(int i = 1; i <= temp1 + 1; i++) temp3 += i; }
                temp4 = OU_spacing_buy / (temp1 + 1);
                temp5 = MathFloor(temp2 / temp4);
                temp6 = (temp3 + temp5) * g_min_volume - OU_volume_of_open_trades;
                if(temp6 > 0) {
                    OU_SendOrder(temp6, true); } }
            break; } } }

//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
bool OU_SendOrder(double local_unit, bool is_down) {
    if(local_unit < g_min_volume) return false;
    double final_unit = NormalizeDouble(MathMin(local_unit, g_max_volume), g_volume_digits);

    MqlTradeRequest request;
    MqlTradeResult result;
    ZeroMemory(request);
    request.action = TRADE_ACTION_DEAL;
    request.symbol = _Symbol;
    request.volume = final_unit;
    request.type = ORDER_TYPE_BUY;
    request.price = g_ask;
    request.deviation = 10;
    request.magic = 0;
    request.tp = 0;
    request.type_filling = g_filling_mode;

    if(is_down) {
        string cmt;
        StringConcatenate(cmt, DoubleToString(OU_spacing_buy, 2), ";", DoubleToString(OU_trade_size_buy, 2));
        request.comment = cmt; }
    else {
        request.comment = ""; }

    return m_trade.OrderSend(request, result); }

//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
void OU_OpenNewUpWUp() {
    OU_up_checked_last_open_price = g_ask;

// Use correct aggregated volume variable
    double equity_at_target = g_equity * g_margin_so / g_margin_level;
    double equity_difference = g_equity - equity_at_target;
    double price_difference = (OU_volume_of_open_trades_up > 0) ? equity_difference / (OU_volume_of_open_trades_up * g_contract_size) : 0;

    double end_price = g_ask * ((100 - g_survive_ou_up) / 100);
    double distance = g_ask - end_price;

    if((OU_volume_of_open_trades_up == 0) || ((g_ask - price_difference) < end_price)) {
        double trade_size = 0;
        double num = 0; double den = 0;

        // Helper vars
        double c_size = g_contract_size;
        double dist_abs = MathAbs(distance);
        double spread_cost = g_spread;

        switch(g_calc_mode) {
        case SYMBOL_CALC_MODE_CFDLEVERAGE:
            num = (100 * g_equity * g_leverage - 100 * c_size * dist_abs * OU_volume_of_open_trades_up * g_leverage - g_leverage * g_margin_so * g_margin);
            den = (c_size * (100 * dist_abs * g_leverage + 100 * spread_cost * g_leverage + g_ask * g_initial_margin_rate * g_margin_so));
            break;
        case SYMBOL_CALC_MODE_FOREX:
            num = (100 * g_leverage * g_equity - 100 * c_size * dist_abs * g_leverage * OU_volume_of_open_trades_up - g_leverage * g_margin_so * g_margin);
            den = (c_size * (100 * dist_abs * g_leverage + 100 * spread_cost * g_leverage + g_initial_margin_rate * g_margin_so));
            break;
        case SYMBOL_CALC_MODE_FOREX_NO_LEVERAGE:
            num = (100 * g_equity - 100 * c_size * dist_abs * OU_volume_of_open_trades_up - g_margin_so * g_margin);
            den = (c_size * (100 * dist_abs + 100 * spread_cost + g_initial_margin_rate * g_margin_so));
            break;
        case SYMBOL_CALC_MODE_CFD:
            num = (100 * g_equity - 100 * c_size * dist_abs * OU_volume_of_open_trades_up - g_margin_so * g_margin);
            den = (c_size * (100 * dist_abs + 100 * spread_cost + g_ask * g_initial_margin_rate * g_margin_so));
            break; }

        if (den != 0) trade_size = NormalizeDouble(num / den, g_volume_digits);

        if(trade_size >= g_min_volume) {
            if(OU_SendOrder(trade_size, false)) {
                OU_up_checked_last_open_price = g_ask; } } } }


//+------------------------------------------------------------------+
//| FU Logic Sections                                                |
//+------------------------------------------------------------------+
void FU_OnTickLogic() {
// Scan is already done. FU_pos_count and Geometry vars are populated.

    if(FU_pos_count == 0) {
        FU_CalcLotSize();
        if(FU_OpenBuy()) {
            FU_highest_buy = FU_lowest_buy = g_ask; } }
    else if(FU_lowest_buy >= g_ask + FU_spacing_val) {
        FU_CalcLotSize();
        if(FU_OpenBuy()) FU_lowest_buy = g_ask; }
    else if(FU_highest_buy <= g_ask - FU_spacing_val) {
        FU_CalcLotSize();
        if(FU_OpenBuy()) FU_highest_buy = g_ask; }
    else if((FU_closest_above >= FU_spacing_val) && (FU_closest_below >= FU_spacing_val)) {
        FU_CalcLotSize();
        FU_OpenBuy(); } }

//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
bool FU_OpenBuy() {
    if(FU_trade_size_buy < g_min_volume) return false;

    double lots = NormalizeDouble(MathMin(FU_trade_size_buy, g_max_volume), g_volume_digits);
    double tp = g_ask + g_spread + FU_spacing_val;

    return m_trade.Buy(lots, _Symbol, g_ask, 0, tp, "FillUp"); }

//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
void FU_CalcLotSize() {
    FU_trade_size_buy = 0;

    double ref_price = (FU_pos_count == 0) ? g_ask : FU_highest_buy;
    double end_price = ref_price * (100.0 - g_survive_fu) / 100.0;
    double distance = g_ask - end_price;
    if(distance <= 0) return;

    double num_trades = MathFloor(distance / FU_spacing_val);
    if(num_trades < 1) num_trades = 1;

    double eq_at_target = g_equity - FU_volume_open * distance * g_contract_size;
    if(g_margin > 0 && (eq_at_target / g_margin * 100) <= g_margin_so) return;

    double unit = g_min_volume;
    double d_equity = g_contract_size * unit * (num_trades * (num_trades + 1) / 2);
    d_equity += num_trades * unit * g_spread * g_contract_size;

    double unit_margin = OU_CalculateMargin(unit, g_ask, end_price); // Reusing helper
    unit_margin *= num_trades;

    double S = g_margin_so / 100.0;
    double denominator = d_equity + S * unit_margin;

    if(denominator <= 0) return;

    double m = (eq_at_target - S * g_margin) / denominator;
    m = MathMax(1.0, m);
    double max_mult = g_max_volume / g_min_volume;
    if(m > max_mult) m = max_mult;

    FU_trade_size_buy = NormalizeDouble(MathFloor(m) * g_min_volume, g_volume_digits); }
//+------------------------------------------------------------------+
