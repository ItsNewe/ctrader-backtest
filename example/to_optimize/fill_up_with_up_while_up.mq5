//+------------------------------------------------------------------+
//| ProjectName                                                      |
//| Copyright 2020, CompanyName                                      |
//| http://www.companyname.net                                       |
//+------------------------------------------------------------------+

#include <Trade\AccountInfo.mqh>
//#include <Trade\SymbolInfo.mqh>
//#include <Trade\OrderInfo.mqh>
//#include <Trade\HistoryOrderInfo.mqh>
#include <Trade\PositionInfo.mqh>
//#include <Trade\DealInfo.mqh>
#include <Trade\Trade.mqh>
//#include <Trade\TerminalInfo.mqh>

CAccountInfo m_account;
//CSymbolInfo m_symbol;
//COrderInfo m_order;
//CHistoryOrderInfo m_history_order;
CPositionInfo m_position;
//CDealInfo m_deal;
CTrade m_trade;
//CTerminalInfo m_terminal;

//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
input double survive_down = 1;
input double size = 1;
input double spacing = 1;
//input double bottom_price = 3800;

//
double min_volume_alg, max_volume_alg;
//
double current_spread = 0, point = 0;
double current_equity, current_balance;
//
double current_ask = 0;
double current_bid = 0;
//
double lowest_buy;
double highest_buy;

double trade_size_buy;
double spacing_buy;

double closest_above, closest_below;

double volume_of_open_trades = 0;
//

int digit = 2;

MqlTradeResult trade_result = { };
MqlTradeRequest req = { };
//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
void iterate() {

    lowest_buy = DBL_MAX;
    highest_buy = DBL_MIN;

    closest_above = DBL_MAX;
    closest_below = DBL_MIN;

    volume_of_open_trades = 0;

    double open_price, lots;

    string cmnt; // comment
    ushort u_sep = StringGetCharacter(";", 0);
    string result[];

    ulong ticket;

    for(int PositionIndex = PositionsTotal() - 1; PositionIndex >= 0; PositionIndex--) {
        ticket = PositionGetTicket(PositionIndex);
        if(!PositionSelectByTicket(ticket)) {
            continue; } // if failed to select
        //if (PositionGetString(POSITION_SYMBOL) == _Symbol)
        {
            if(m_position.SelectByTicket(ticket)) {
                lots = PositionGetDouble(POSITION_VOLUME);
                open_price = PositionGetDouble(POSITION_PRICE_OPEN);
                cmnt = PositionGetString(POSITION_COMMENT);
                if(PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY) {
                    int k = StringSplit(cmnt, u_sep, result);
                    if(k > 0) {
                        // check for open, unless it got closed
                        volume_of_open_trades += lots;
                        lowest_buy = MathMin(lowest_buy, open_price);
                        highest_buy = MathMax(highest_buy, open_price);
                        if (open_price >= current_ask) {
                            closest_above = MathMin(closest_above, open_price - current_ask); }
                        if (open_price <= current_ask) {
                            closest_below = MathMin(closest_below, current_ask - open_price); } } } } } } }
//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
bool open(double local_unit, int local_direction) {
    double final_unit = min_volume_alg;
    while(local_unit > 0) {
        if((local_unit >= min_volume_alg) && (local_unit <= max_volume_alg)) {
            final_unit = NormalizeDouble(local_unit, digit);
            local_unit = 0; }
        else if(local_unit < min_volume_alg) {
            return false; }
        else if(local_unit > max_volume_alg) {
            final_unit = NormalizeDouble(max_volume_alg, digit);
            local_unit = 0; } //local_unit - max_volume_alg; // let it top out?
        ZeroMemory(req);
        ZeroMemory(trade_result);
        req.action = TRADE_ACTION_DEAL;
        req.symbol = _Symbol;
        req.volume = final_unit;
        switch(local_direction) {
        case 1: // up
            req.type = ORDER_TYPE_BUY;
            req.price = current_ask;
            req.tp = current_ask + current_spread + spacing_buy;
            StringConcatenate(req.comment, DoubleToString(spacing_buy, 2), ";", DoubleToString(trade_size_buy, 2));
            break;
        default:
            break; }
        req.deviation = 1; // ??
        long filling = SymbolInfoInteger(_Symbol, SYMBOL_FILLING_MODE);
        switch((int)filling) {
        case SYMBOL_FILLING_FOK:
            req.type_filling = ORDER_FILLING_FOK;
            break;
        case SYMBOL_FILLING_IOC:
            req.type_filling = ORDER_FILLING_IOC;
            break;
        case 4: //SYMBOL_FILLING_BOC:
            req.type_filling = ORDER_FILLING_BOC;
            break;
        /*case ??:
        req.type_filling = ORDER_FILLING_RETURN;
        break;*/
        default:
            break; }
        //OrderSendAsync(req,res);
        if(!OrderSend(req, trade_result)) {
            Print(" Error: ", GetLastError());
            Print(" __FUNCTION__ = ", __FUNCTION__, "  __LINE__ = ", __LINE__);
            ResetLastError();
            return false; } }
    return true; }
//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
void close(ulong ticket) {
    if(m_position.SelectByTicket(ticket)) {
        if(!m_trade.PositionClose(ticket)) {
            Print("Order Close failed, order number: ", ticket, " Error: ", GetLastError());
            Print(" __FUNCTION__ = ", __FUNCTION__, "  __LINE__ = ", __LINE__);
            ResetLastError(); } } }
//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
void open_new() {

// open new

    if(PositionsTotal() == 0) {
        sizing_buy();
        if(open(trade_size_buy, 1)) {
            highest_buy = current_ask;
            lowest_buy = current_ask; } }
    else {
        if(lowest_buy >= current_ask + spacing_buy) {
            sizing_buy();
            if(open(trade_size_buy, 1)) {
                lowest_buy = current_ask; } }
        else if(highest_buy <= current_ask - spacing_buy) {
            sizing_buy();
            if(open(trade_size_buy, 1)) {
                highest_buy = current_ask; } }
        else if ((closest_above >= spacing_buy) && (closest_below >= spacing_buy)) {
            sizing_buy();
            open(trade_size_buy, 1); } } }

//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
void new_tick_values() {
    current_spread = point * SymbolInfoInteger(_Symbol, SYMBOL_SPREAD);
    current_equity = m_account.Equity();
    current_balance = m_account.Balance();

    max_balance = MathMax(max_balance, current_balance);
    max_number_of_open = MathMax(max_number_of_open, PositionsTotal());
    max_used_funds = MathMax(max_used_funds, current_balance - current_equity + AccountInfoDouble(ACCOUNT_MARGIN));

    current_ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    current_bid = SymbolInfoDouble(_Symbol, SYMBOL_BID); }

//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
//void OnTick() {
//    new_tick_values();
//// iterate
//    iterate();
//// check for event
//
//    open_new(); }

//+------------------------------------------------------------------+
//| Expert initialization function                                   |
//+------------------------------------------------------------------+
int OnInit() {

    point = SymbolInfoDouble(_Symbol, SYMBOL_POINT);

    m_trade.LogLevel(LOG_LEVEL_NO); // LOG_LEVEL_ALL LOG_LEVEL_ERRORS LOG_LEVEL_NO

    min_volume_alg = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN);
    max_volume_alg = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MAX);
    m_trade.SetAsyncMode(true);

    if(min_volume_alg == 0.01) {
        digit = 2; }
    else if(min_volume_alg == 0.1) {
        digit = 1; }
    else {
        digit = 0; }
//

    new_tick_values();

    lowest_buy = DBL_MAX;
    highest_buy = DBL_MIN;

    closest_above = DBL_MAX;
    closest_below = DBL_MIN;

    volume_of_open_trades = 0;

    double open_price, lots;

    ulong ticket;

    string cmnt; // comment
    ushort u_sep = StringGetCharacter(";", 0);
    string result[];

    for(int PositionIndex = PositionsTotal() - 1; PositionIndex >= 0; PositionIndex--) {
        ticket = PositionGetTicket(PositionIndex);
        if(!PositionSelectByTicket(ticket)) {
            continue; } // if failed to select
        //if (PositionGetString(POSITION_SYMBOL) == _Symbol)
        {
            if(m_position.SelectByTicket(ticket)) {
                lots = PositionGetDouble(POSITION_VOLUME);
                open_price = PositionGetDouble(POSITION_PRICE_OPEN);
                cmnt = PositionGetString(POSITION_COMMENT);
                if(PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY) {
                    int k = StringSplit(cmnt, u_sep, result);
                    if(k > 0) {
                        PrintFormat("result[%d]=\"%s\"", 0, result[0]);
                        spacing_buy = StringToDouble(result[0]);
                        PrintFormat("result[%d]=\"%s\"", 1, result[1]);
                        trade_size_buy = StringToDouble(result[1]); }
                    // check for open, unless it got closed
                    volume_of_open_trades += lots;
                    lowest_buy = MathMin(lowest_buy, open_price);
                    highest_buy = MathMax(highest_buy, open_price);
                    if (open_price >= current_ask) {
                        closest_above = MathMin(closest_above, open_price - current_ask); }
                    if (open_price <= current_ask) {
                        closest_below = MathMin(closest_below, current_ask - open_price); } } } } }
//
    spacing_buy = spacing;
    trade_size_buy = size * min_volume_alg;
//
    new_tick_values();
//Modify();

    return(INIT_SUCCEEDED); }
//+------------------------------------------------------------------+
//| Expert deinitialization function                                 |
//+------------------------------------------------------------------+
double max_balance = 0;
double max_number_of_open = 0;
double max_used_funds = 0;
double max_trade_size = 0;
void OnDeinit(const int reason) {
    Print ("@!@ max balance: ", max_balance);
    Print ("@!@ max number of open: ", max_number_of_open);
    Print ("@!@ max used funds: ", max_used_funds);
    Print ("@!@ max trade size: ", max_trade_size); }
//+------------------------------------------------------------------+
void sizing_buy() {

    trade_size_buy = 0;
//trade_size_buy = min_volume_alg;

    static int calc_mode = SymbolInfoInteger(_Symbol, SYMBOL_TRADE_CALC_MODE);
    static double contract_size = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);
    static long leverage = AccountInfoInteger(ACCOUNT_LEVERAGE);
    static double initial_margin_rate = 0;     // initial margin rate
    static double maintenance_margin_rate = 0; // maintenance margin rate
    SymbolInfoMarginRate(_Symbol, ORDER_TYPE_BUY, initial_margin_rate, maintenance_margin_rate);
    double current_margin_level = AccountInfoDouble(ACCOUNT_MARGIN_LEVEL);
    double used_margin = AccountInfoDouble(ACCOUNT_MARGIN);
    double margin_stop_out_level = AccountInfoDouble(ACCOUNT_MARGIN_SO_SO);
    double equity = AccountInfoDouble(ACCOUNT_EQUITY);

    double equity_at_target = equity * margin_stop_out_level / current_margin_level;
    double equity_difference = equity - equity_at_target;
    double price_difference = equity_difference / (volume_of_open_trades * contract_size);

    double end_price = current_ask * ((100 - survive_down) / 100);
//double distance = end_price - current_ask;
//double end_price = bottom_price;
    double distance = current_ask - end_price;
    double number_of_trades = MathFloor(distance / spacing_buy);

    if((PositionsTotal() == 0) || ((current_ask - price_difference) < end_price)) { // if we can't survive anyway, there is no point checking
        equity_at_target = equity - volume_of_open_trades * MathAbs(distance) * contract_size;
        double margin_level = equity_at_target / used_margin * 100;
        double trade_size = min_volume_alg;
        double local_used_margin = 0;
        double starting_price = current_ask;

        if(margin_level > margin_stop_out_level) {
            // quantize the fees for 1 trade
            //double d_equity = contract_size * trade_size * (starting_price - end_price) + trade_size * current_spread * contract_size;
            double d_equity = contract_size * trade_size * (number_of_trades * (number_of_trades + 1) / 2);
            double d_spread = number_of_trades * trade_size * current_spread * contract_size;
            d_equity += d_spread;
            switch(calc_mode) {
            case SYMBOL_CALC_MODE_CFDLEVERAGE:
                //Margin: (Lots * ContractSize * MarketPrice) / Leverage * Margin_Rate
                local_used_margin += (trade_size * contract_size * starting_price) / leverage * initial_margin_rate;
                local_used_margin += (trade_size * contract_size * end_price) / leverage * initial_margin_rate;
                break;
            case SYMBOL_CALC_MODE_FOREX:
                //Margin:  Lots * Contract_Size / Leverage * Margin_Rate
                local_used_margin += trade_size * contract_size / leverage * initial_margin_rate;
                local_used_margin += trade_size * contract_size / leverage * initial_margin_rate;
                break;
            case SYMBOL_CALC_MODE_FOREX_NO_LEVERAGE:
                local_used_margin += trade_size * contract_size * initial_margin_rate;
                local_used_margin += trade_size * contract_size * initial_margin_rate;
                break;
            case SYMBOL_CALC_MODE_CFD:
                //Margin: Lots * ContractSize * MarketPrice * Margin_Rate
                local_used_margin += trade_size * contract_size * starting_price * initial_margin_rate;
                local_used_margin += trade_size * contract_size * end_price * initial_margin_rate;
                break;
            default:
                break; }
            local_used_margin = local_used_margin / 2;
            local_used_margin = number_of_trades * local_used_margin;

            double multiplier = 0;
            //multiplier = MathFloor((100 * equity_at_target - margin_stop_out_level * used_margin) / (margin_stop_out_level * local_used_margin - 100 * d_equity));
            //multiplier = MathAbs(multiplier);

            double equity_backup = equity_at_target;
            double used_margin_backup = used_margin;

            double max = max_volume_alg / min_volume_alg;

            equity_at_target -= max * d_equity;
            used_margin += max * local_used_margin;
            if(margin_stop_out_level < equity_at_target / used_margin * 100) {
                multiplier = max; }
            else {
                used_margin = used_margin_backup;
                equity_at_target = equity_backup;
                for(double increment = max; increment >= 1; increment = increment / 10) {
                    while(margin_stop_out_level < equity_at_target / used_margin * 100) {
                        equity_backup = equity_at_target;
                        used_margin_backup = used_margin;
                        multiplier += increment;
                        equity_at_target -= increment * d_equity;
                        used_margin += increment * local_used_margin; }
                    multiplier -= increment;
                    used_margin = used_margin_backup;
                    equity_at_target = equity_backup; } }

            Print("@!@ multiplier: ", multiplier);
            multiplier = MathMax(1, multiplier);

            trade_size_buy = multiplier * min_volume_alg;
            max_trade_size = MathMax(max_trade_size, trade_size_buy); } } }

//+------------------------------------------------------------------+
double OnTester() {

//double profit = TesterStatistics(STAT_PROFIT);

    HistorySelect(0, TimeCurrent());
    long total = HistoryDealsTotal();
    long ticket = 0;
    long reason;

    for(long i = total - 1; i >= 0; i--) {
        if((ticket = HistoryDealGetTicket(i)) > 0) {
            reason = HistoryDealGetInteger(ticket, DEAL_REASON);
            if(reason == DEAL_REASON_SO) {
                return(0.0); } } }

//return(profit);
//return max_balance - max_used_funds;
    return max_balance; }
//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
void Modify() {
//--- declare and initialize the trade request and result of trade request
    MqlTradeRequest request;
    MqlTradeResult  result;
//--- iterate over all open positions
    for(int i = 0; i < PositionsTotal(); i++) {
        //--- parameters of the order
        ulong  position_ticket = PositionGetTicket(i); // ticket of the position
        string position_symbol = PositionGetString(POSITION_SYMBOL); // symbol
        int    digits = (int)SymbolInfoInteger(position_symbol, SYMBOL_DIGITS); // number of decimal places
        ulong  magic = PositionGetInteger(POSITION_MAGIC); // MagicNumber of the position
        double volume = PositionGetDouble(POSITION_VOLUME);  // volume of the position
        double sl = PositionGetDouble(POSITION_SL); // Stop Loss of the position
        double tp = PositionGetDouble(POSITION_TP); // Take Profit of the position
        double open_price = PositionGetDouble(POSITION_PRICE_OPEN);
        ENUM_POSITION_TYPE type = (ENUM_POSITION_TYPE)PositionGetInteger(POSITION_TYPE); // type of the position
        //--- output information about the position
        PrintFormat("#%I64u %s  %s  %.2f  %s  sl: %s  tp: %s  [%I64d]",
                    position_ticket,
                    position_symbol,
                    EnumToString(type),
                    volume,
                    DoubleToString(open_price, digits),
                    DoubleToString(sl, digits),
                    DoubleToString(tp, digits),
                    magic);
        //--- if the MagicNumber matches, Stop Loss and Take Profit are not defined
        if(sl == 0 && tp == 0) {
            //--- calculate the current price levels
            double bid = SymbolInfoDouble(position_symbol, SYMBOL_BID);
            double ask = SymbolInfoDouble(position_symbol, SYMBOL_ASK);
            int    stop_level = (int)SymbolInfoInteger(position_symbol, SYMBOL_TRADE_STOPS_LEVEL);
            if(type == POSITION_TYPE_BUY) {
                tp = NormalizeDouble(open_price + spacing_buy + current_spread, digits); }
            //--- zeroing the request and result values
            ZeroMemory(request);
            ZeroMemory(result);
            //--- setting the operation parameters
            request.action  = TRADE_ACTION_SLTP; // type of trade operation
            request.position = position_ticket; // ticket of the position
            request.symbol = position_symbol;   // symbol
            request.sl      = sl;               // Stop Loss of the position
            request.tp      = tp;               // Take Profit of the position
            //--- output information about the modification
            PrintFormat("Modify #%I64d %s %s", position_ticket, position_symbol, EnumToString(type));
            //--- send the request
            if(!OrderSend(request, result))
                PrintFormat("OrderSend error %d", GetLastError()); // if unable to send the request, output the error code
            //--- information about the operation
            PrintFormat("retcode=%u  deal=%I64u  order=%I64u", result.retcode, result.deal, result.order); } } }
//+------------------------------------------------------------------+

//--
//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
double open_up(double local_unit, double current_ask) {
    static MqlTradeResult trade_result = { };
    static MqlTradeRequest req = { };
    static double min_volume_alg;
    static double max_volume_alg;
    static int local_digit;
    static bool first_run = true;
    if(first_run) {
        req.symbol = _Symbol;
        req.action = TRADE_ACTION_DEAL;
        req.type = ORDER_TYPE_BUY;
        req.deviation = 1; // ??
        long filling = SymbolInfoInteger(_Symbol, SYMBOL_FILLING_MODE);
        switch((int)filling) {
        case SYMBOL_FILLING_FOK:
            req.type_filling = ORDER_FILLING_FOK;
            break;
        case SYMBOL_FILLING_IOC:
            req.type_filling = ORDER_FILLING_IOC;
            break;
        case 4: //SYMBOL_FILLING_BOC:
            req.type_filling = ORDER_FILLING_BOC;
            break;
        default:
            break; }
        min_volume_alg = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN);
        max_volume_alg = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MAX);
        local_digit = digit;
        first_run = false; }

    req.volume = 0;
    if(local_unit < min_volume_alg) {
        return 0; }
    else if((local_unit >= min_volume_alg) && (local_unit <= max_volume_alg)) {
        req.volume = NormalizeDouble(local_unit, local_digit); }
    else if(local_unit > max_volume_alg) {
        req.volume = NormalizeDouble(max_volume_alg, local_digit); }

//req.action = TRADE_ACTION_DEAL;
//req.symbol = _Symbol;
//req.volume = ;
//req.type = ORDER_TYPE_BUY;
    req.price = current_ask;
//req.deviation = 1; // ??

//OrderSendAsync(req,res);
    if(!OrderSend(req, trade_result)) {
        Print(" Error: ", GetLastError()); Print(" __FUNCTION__ = ", __FUNCTION__, "  __LINE__ = ", __LINE__);
        ResetLastError();
        //TesterStop();
        return 0; }
    return req.volume; }
//+------------------------------------------------------------------+
//| Expert tick function                                             |
//+------------------------------------------------------------------+
void OnTick() {

    static double volume_of_open_trades;
    static double checked_last_open_price;

    static double min_volume_alg;
    static double max_volume_alg;

    static double margin_stop_out_level;

    static double local_survive_down;

    static double contract_size;
    static double initial_margin_rate = 0;     // initial margin rate
    static double maintenance_margin_rate = 0; // maintenance margin rate
    static long leverage;
    static int calc_mode;

    static bool first_run = true;

    string cmnt; // comment
    ushort u_sep = StringGetCharacter(";", 0);
    string result[];

    if(first_run) {
        checked_last_open_price = DBL_MIN;
        volume_of_open_trades = 0;
        for(int PositionIndex = PositionsTotal() - 1; PositionIndex >= 0; PositionIndex--) {
            if(!PositionSelectByTicket(PositionGetTicket(PositionIndex))) {
                continue; } // if failed to select
            if((PositionGetString(POSITION_SYMBOL) == _Symbol) && (PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY)) {
                cmnt = PositionGetString(POSITION_COMMENT);
                int k = StringSplit(cmnt, u_sep, result);
                if(k == 0) {
                    if(m_position.SelectByTicket(PositionGetTicket(PositionIndex))) {
                        volume_of_open_trades += PositionGetDouble(POSITION_VOLUME);
                        double open_price = PositionGetDouble(POSITION_PRICE_OPEN);
                        checked_last_open_price = MathMax(checked_last_open_price, open_price);  } } } }

        Print("init: checked_last_open_price ", checked_last_open_price);
        //
        min_volume_alg = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN);
        max_volume_alg = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MAX);
        margin_stop_out_level = AccountInfoDouble(ACCOUNT_MARGIN_SO_SO);
        SymbolInfoMarginRate(_Symbol, ORDER_TYPE_BUY, initial_margin_rate, maintenance_margin_rate);
        contract_size = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);
        calc_mode = SymbolInfoInteger(_Symbol, SYMBOL_TRADE_CALC_MODE);
        leverage = AccountInfoInteger(ACCOUNT_LEVERAGE);
        local_survive_down = survive_down;
        first_run = false; }

//new_tick_values();
    double current_spread = _Point * SymbolInfoInteger(_Symbol, SYMBOL_SPREAD);

    double current_ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
//current_bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);

    double balance  = AccountInfoDouble(ACCOUNT_BALANCE);
    double equity = AccountInfoDouble(ACCOUNT_EQUITY);
    double current_margin_level = AccountInfoDouble(ACCOUNT_MARGIN_LEVEL);
    double used_margin = AccountInfoDouble(ACCOUNT_MARGIN);

// open new
//if((volume_of_open_trades == 0) || (checked_last_open_price < current_ask))
    {
        checked_last_open_price = current_ask;

        volume_of_open_trades = 0;
        for(int PositionIndex = PositionsTotal() - 1; PositionIndex >= 0; PositionIndex--) {
            if(!PositionSelectByTicket(PositionGetTicket(PositionIndex))) {
                continue; } // if failed to select
            if((PositionGetString(POSITION_SYMBOL) == _Symbol) && (PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY)) {
                cmnt = PositionGetString(POSITION_COMMENT);
                int k = StringSplit(cmnt, u_sep, result);
                if(k == 0) {
                    if(m_position.SelectByTicket(PositionGetTicket(PositionIndex))) {
                        volume_of_open_trades += PositionGetDouble(POSITION_VOLUME); } } } }

        double equity_at_target = equity * margin_stop_out_level / current_margin_level;
        double equity_difference = equity - equity_at_target;
        double price_difference = equity_difference / (volume_of_open_trades * contract_size);

        //double ep = (1 - (bottom_price / current_ask)) * 1.5 * 100;
        //double end_price = current_ask * ((100 - ep) / 100);

        double end_price = current_ask * ((100 - local_survive_down) / 100);

        double distance = current_ask - end_price;

        if((volume_of_open_trades == 0) || ((current_ask - price_difference) < end_price)) { // if we can't survive anyway, there is no point checking
            equity_at_target = equity - volume_of_open_trades * MathAbs(distance) * contract_size;
            double margin_level = equity_at_target / used_margin * 100;
            double trade_size = 0;
            double local_used_margin = 0;
            double starting_price = current_ask;

            switch(calc_mode) {
            case SYMBOL_CALC_MODE_CFDLEVERAGE:
                //Margin: (Lots * ContractSize * MarketPrice) / Leverage * Margin_Rate
                trade_size = NormalizeDouble((100 * equity * leverage - 100 * contract_size * MathAbs(distance) * volume_of_open_trades * leverage - leverage * margin_stop_out_level * used_margin) / ( contract_size * ( 100 * MathAbs(distance) * leverage + 100 * current_spread * leverage + starting_price * initial_margin_rate * margin_stop_out_level)), 2);
                break;
            case SYMBOL_CALC_MODE_FOREX:
                //Margin:  Lots * Contract_Size / Leverage * Margin_Rate
                trade_size = NormalizeDouble((100 * leverage * equity - 100 * contract_size * MathAbs(distance) * leverage * volume_of_open_trades - leverage * margin_stop_out_level * used_margin) / ( contract_size * ( 100 * MathAbs(distance) * leverage + 100 * current_spread * leverage + initial_margin_rate * margin_stop_out_level)), 2);
                break;
            case SYMBOL_CALC_MODE_FOREX_NO_LEVERAGE:
                // Margin:  Lots * Contract_Size * Margin_Rate
                trade_size = NormalizeDouble((100 * equity - 100 * contract_size * MathAbs(distance) * volume_of_open_trades - margin_stop_out_level * used_margin) / ( contract_size * ( 100 * MathAbs(distance) + 100 * current_spread + initial_margin_rate * margin_stop_out_level)), 2);
                break;
            case SYMBOL_CALC_MODE_CFD:
                //Margin: Lots * ContractSize * MarketPrice * Margin_Rate
                trade_size = NormalizeDouble((100 * equity - 100 * contract_size * MathAbs(distance) * volume_of_open_trades - margin_stop_out_level * used_margin) / ( contract_size * ( 100 * MathAbs(distance) + 100 * current_spread + starting_price * initial_margin_rate * margin_stop_out_level)), 2);
                break;
            default:
                break; }
            if(trade_size >= min_volume_alg) {
                volume_of_open_trades += open_up(trade_size, current_ask);
                checked_last_open_price = current_ask; } } }

//
    new_tick_values();
// iterate
    iterate();
// check for event

    open_new();
//
}

//+------------------------------------------------------------------+
