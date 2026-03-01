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

input double survive = 5; //survive down %
input double commission = 0;
input int digit = 2;
input int sizing = 0; // sizing: constant, incremental
input int closing_mode = 0;

double cm;
//
double min_volume_alg, max_volume_alg;
//
double current_spread = 0, current_commission = 0, current_spread_and_commission = 0, point = 0;
double current_equity, current_balance;
//
double current_ask = 0;
double current_bid = 0;
//
double bid_at_the_time_of_turn_up = 0, ask_at_the_time_of_turn_up = 0;
double bid_at_the_time_of_turn_down = 0, ask_at_the_time_of_turn_down = 0;
//

double value_of_sell_trades;

double lowest_sell;
double highest_sell;

double volume_of_open_trades;
double volume_of_open_trades_up;

double trade_size_sell;
double spacing_sell;
//
int direction = 0;
int direction_change = 0;

int count_sell = 0;

bool commission_set = false;

bool close_all_sell_flag = false;
bool close_all_profitable_sell_flag = false;

bool is_there_unprofitable_sell = false;

string first;
string second;
string currency;
//
MqlTradeResult trade_result = { };
MqlTradeRequest req = { };
//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
double account_currency_cost_to_insrument_cost(double cost) {
    if(first == currency) {
        return(cost); } // EURxxx
// in case our account currency is not the base currency of the instrument
    else {
        if(second == currency) {
            return(cost * current_bid); } // xxxEUR
        else {
            //---- check normal cross currencies, search for direct conversion through deposit currency
            string base = currency + first; // USDxxx
            double r = SymbolInfoDouble(base, SYMBOL_BID);
            if(r > 0) {
                return(cost / r); }
            //---- try vice versa
            base = first + currency; // xxxUSD
            r = SymbolInfoDouble(base, SYMBOL_BID);
            if(r > 0) {
                return(cost * r); }
            //---- direct conversion is impossible
            Print("MarginCalculate: can not convert '", _Symbol, "'");
            return(0.0); } }
    return(0.0); }
//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
double OrderSwap() {
    double D = PositionGetDouble(POSITION_SWAP);
    D = account_currency_cost_to_insrument_cost(D);
    return D; }
//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
double OrderCommission() {
    double E = 0.0;
    if(HistorySelectByPosition(PositionGetInteger(POSITION_IDENTIFIER))) {
        int deals = HistoryDealsTotal();
        for(int i = 0; i < deals; i++) {
            ulong deal_ticket = HistoryDealGetTicket(i);
            if(deal_ticket > 0) {
                const double LotsIn = HistoryDealGetDouble(deal_ticket, DEAL_VOLUME);
                if(LotsIn > 0) {
                    E += HistoryDealGetDouble(deal_ticket, DEAL_COMMISSION) * PositionGetDouble(POSITION_VOLUME) / LotsIn; } } } }
    E = account_currency_cost_to_insrument_cost(E);
    return MathAbs(E); }
//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
void clear_flags() {
    close_all_sell_flag = false;
    close_all_profitable_sell_flag = false; }
//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
void iterate() {
    value_of_sell_trades = 0;

    count_sell = 0;

    volume_of_open_trades = 0;

    lowest_sell = DBL_MAX;

    highest_sell = DBL_MIN;

    is_there_unprofitable_sell = false;

    if(sizing == 1) {
        trade_size_sell = 0; }

    double open_price, profit, swap, commission_local, lots;

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
                // commission
                /*if(!commission_set) {
                    commission_local = OrderCommission(); //
                    cm = 100 * (commission_local / (lots / min_volume_alg));
                    if(cm != 0.0) {
                        commission_set = true; }
                    else {
                        cm = commission * 100; } }
                else {
                    commission_local = cm / 100 * (lots / min_volume_alg); }*/
                //
                swap = OrderSwap();
                profit = m_position.Profit() + swap;// - commission_local;
                open_price = PositionGetDouble(POSITION_PRICE_OPEN);
                cmnt = PositionGetString(POSITION_COMMENT);
                if (PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_SELL) {
                    int k = StringSplit(cmnt, u_sep, result);
                    if(k > 0) {
                        if(close_all_sell_flag) {
                            close(ticket);
                            continue; }
                        if((profit > 0) && (close_all_profitable_sell_flag)) {
                            close(ticket);
                            continue; }
                        // check for open, unless it got closed
                        if(profit < 0) {
                            is_there_unprofitable_sell = true; }
                        value_of_sell_trades += profit;
                        lowest_sell = MathMin(lowest_sell, open_price);
                        highest_sell = MathMax(highest_sell, open_price);
                        count_sell++;
                        volume_of_open_trades += lots;
                        if(sizing == 1) {
                            trade_size_sell = MathMax(trade_size_sell, lots); } } } } } }
    if(sizing == 1) {
        trade_size_sell += min_volume_alg; }
// clear flags
    clear_flags(); }
//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
bool open_down_w_up(double local_unit, int local_direction) {
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
        case -1: // up
            req.type = ORDER_TYPE_SELL;
            req.price = current_bid;
            StringConcatenate(req.comment, DoubleToString(spacing_sell, 2), ";", DoubleToString(trade_size_sell, 2));
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
            Print(" Error: ", GetLastError()); Print(" __FUNCTION__ = ", __FUNCTION__, "  __LINE__ = ", __LINE__);
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
void open_new_down_w_up() {

    if(count_sell == 0) {
        sizing_sell(); }
// open new
//if((count_sell == 0) || (lowest_sell > current_ask + spacing_sell)) {
//    if (open(trade_size_sell, 1)) {
//        lowest_sell = current_ask;
//        count_sell++;
//        if(sizing == 1) {
//            trade_size_sell += min_volume_alg; } } }

    if (count_sell == 0) {
        if (open_down_w_up(trade_size_sell, -1)) {
            highest_sell = current_bid;
            lowest_sell = current_bid;
            count_sell++;
            if(sizing == 1) {
                trade_size_sell += min_volume_alg; } } }
    else {
        double temp, temp1, temp2, temp3 = 0, temp4, temp5, temp6;
        switch (sizing) {
        case 0: // constant
            if(highest_sell < current_bid) {
                //temp = MathFloor((highest_sell - current_ask) / spacing_sell) * trade_size_sell - volume_of_open_trades;

                temp = ((current_bid - lowest_sell) / spacing_sell);
                temp1 = MathFloor(temp);
                temp2 = temp - temp1;
                temp3 = temp1;
                temp4 = spacing_sell / (trade_size_sell / min_volume_alg);
                temp5 = MathFloor(temp2 / temp4);
                temp6 = temp3 * trade_size_sell + temp5 * min_volume_alg - volume_of_open_trades;
                if (temp6 > 0) {
                    if (open_down_w_up(temp6, -1)) {
                        highest_sell = current_bid;
                        count_sell++; } } }
            break;
        case 1: // incremental
            if(highest_sell < current_bid) {
                temp = ((current_bid - lowest_sell) / spacing_sell);
                temp1 = MathFloor(temp);
                temp2 = temp - temp1;
                for(int i = 1; i <= temp1; i++) {
                    temp3 += i; }
                temp4 = spacing_sell / (temp1 + 1);
                temp5 = MathFloor(temp2 / temp4);
                temp6 = (temp3 + temp5) * min_volume_alg - (volume_of_open_trades - min_volume_alg);
                if (temp6 > 0) {
                    if (open_down_w_up(temp6, -1)) {
                        highest_sell = current_bid;
                        count_sell++; } } }
            break;
        default:
            break; } }

}
//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
void clear_turn_marks() {
    bid_at_the_time_of_turn_up = DBL_MAX;
    ask_at_the_time_of_turn_up = DBL_MAX;
    ask_at_the_time_of_turn_down = DBL_MIN;
    bid_at_the_time_of_turn_down = DBL_MIN; }
//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
void new_tick_values() {
//nr_digits = SymbolInfoInteger(_Symbol, SYMBOL_DIGITS);
    current_spread = point * SymbolInfoInteger(_Symbol, SYMBOL_SPREAD);
    current_commission = point * cm;
    current_spread_and_commission = (current_spread + current_commission);// * SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);
    current_equity = m_account.Equity();
    current_balance = m_account.Balance();

    current_ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    current_bid = SymbolInfoDouble(_Symbol, SYMBOL_BID); }

//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
void direction_check() {
    direction_change = 0;
// turning points
    bid_at_the_time_of_turn_down = MathMax(current_bid, bid_at_the_time_of_turn_down);
    bid_at_the_time_of_turn_up = MathMin(current_bid, bid_at_the_time_of_turn_up);
    ask_at_the_time_of_turn_down = MathMax(current_ask, ask_at_the_time_of_turn_down);
    ask_at_the_time_of_turn_up = MathMin(current_ask, ask_at_the_time_of_turn_up);
// direction
    iterate();
    if((direction != -1) && (ask_at_the_time_of_turn_down >= current_ask + current_spread_and_commission) && (bid_at_the_time_of_turn_down >= current_bid + current_spread_and_commission)) {
        direction = -1;
        direction_change = -1;
        clear_turn_marks(); }
    if((direction != 1) && (bid_at_the_time_of_turn_up <= current_bid - current_spread_and_commission) && (ask_at_the_time_of_turn_up <= current_ask - current_spread_and_commission)) {
        direction = 1;
        direction_change = 1;
        switch (closing_mode) {
        case 0:
            if(value_of_sell_trades > 0) {
                close_all_profitable_sell_flag = true;
                iterate(); }
            break;
        case 1:
            if(!is_there_unprofitable_sell) {
                close_all_sell_flag = true;
                iterate(); }
            break;
        default:
            break; }
        clear_turn_marks(); } }
//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
void OnTick() {
    new_tick_values();
// direction
    direction_check();
// check for event

    open_new_down_w_down();
    open_new_down_w_up(); }

//+------------------------------------------------------------------+
//| Expert initialization function                                   |
//+------------------------------------------------------------------+
int OnInit() {

    point = SymbolInfoDouble(_Symbol, SYMBOL_POINT);

    m_trade.LogLevel(LOG_LEVEL_NO); // LOG_LEVEL_ALL LOG_LEVEL_ERRORS LOG_LEVEL_NO

    cm = commission * 100;
    min_volume_alg = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN);
    max_volume_alg = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MAX);
    m_trade.SetAsyncMode(true);
    first = SymbolInfoString(_Symbol, SYMBOL_CURRENCY_BASE); // the first symbol, for example, EUR
//first = SymbolInfoString(_Symbol, SYMBOL_CURRENCY_MARGIN); // the first symbol, for example, EUR
    second = SymbolInfoString(_Symbol, SYMBOL_CURRENCY_PROFIT); // the second symbol, for example, USD
    currency = AccountInfoString(ACCOUNT_CURRENCY); // deposit currency, for example, USD

//
    value_of_sell_trades = 0;

    count_sell = 0;

    volume_of_open_trades = 0;

    lowest_sell = DBL_MAX;
    highest_sell = DBL_MIN;

    if(sizing == 1) {
        trade_size_sell = 0; }

    double open_price, profit, swap, commission_local, lots;

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
                // commission
                /*if(!commission_set) {
                    commission_local = OrderCommission(); //
                    cm = 100 * (commission_local / (lots / min_volume_alg));
                    if(cm != 0.0) {
                        commission_set = true; }
                    else {
                        cm = commission * 100; } }
                else {
                    commission_local = cm / 100 * (lots / min_volume_alg); }*/
                //
                swap = OrderSwap();
                profit = m_position.Profit() + swap;// - commission_local;
                open_price = PositionGetDouble(POSITION_PRICE_OPEN);
                cmnt = PositionGetString(POSITION_COMMENT);
                if (PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_SELL) {
                    int k = StringSplit(cmnt, u_sep, result);
                    if(k > 0) {
                        PrintFormat("result[%d]=\"%s\"", 0, result[0]);
                        spacing_sell = StringToDouble(result[0]);
                        PrintFormat("result[%d]=\"%s\"", 1, result[1]);
                        if(sizing == 0) {
                            trade_size_sell = StringToDouble(result[1]); }
                        // check for open, unless it got closed
                        value_of_sell_trades += profit;
                        lowest_sell = MathMin(lowest_sell, open_price);
                        highest_sell = MathMax(highest_sell, open_price);
                        count_sell++;
                        volume_of_open_trades += lots;
                        if(sizing == 1) {
                            trade_size_sell = MathMax(trade_size_sell, lots); } } } } } }
    if(sizing == 1) {
        trade_size_sell += min_volume_alg; }
// clear flags
    clear_flags();
//

    return(INIT_SUCCEEDED); }
//+------------------------------------------------------------------+
//| Expert deinitialization function                                 |
//+------------------------------------------------------------------+
void OnDeinit(const int reason) {}
//+------------------------------------------------------------------+
void sizing_sell() {

    trade_size_sell = 0;
    spacing_sell = 0;
    static int calc_mode = SymbolInfoInteger(_Symbol, SYMBOL_TRADE_CALC_MODE);
    static double contract_size = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);
    static long leverage = AccountInfoInteger(ACCOUNT_LEVERAGE);
    static double initial_margin_rate = 0;     // initial margin rate
    static double maintenance_margin_rate = 0; // maintenance margin rate
    static double initial_margin = 0;  // SYMBOL_MARGIN_INITIAL override
    SymbolInfoMarginRate(_Symbol, ORDER_TYPE_SELL, initial_margin_rate, maintenance_margin_rate);
    initial_margin = SymbolInfoDouble(_Symbol, SYMBOL_MARGIN_INITIAL);
    initial_margin = SymbolInfoDouble(_Symbol, SYMBOL_MARGIN_INITIAL);
    double current_margin_level = AccountInfoDouble(ACCOUNT_MARGIN_LEVEL);
    double used_margin = AccountInfoDouble(ACCOUNT_MARGIN);
    double margin_stop_out_level = AccountInfoDouble(ACCOUNT_MARGIN_SO_SO);
    double equity = AccountInfoDouble(ACCOUNT_EQUITY);

    double equity_at_target = equity * margin_stop_out_level / current_margin_level;
    double equity_difference = equity - equity_at_target;
    double price_difference = equity_difference / ((volume_of_open_trades + volume_of_open_trades_up) * contract_size);

    double end_price = current_bid * ((100 + survive) / 100);
    double distance = end_price - current_bid;

    if((count_sell == 0) || ((current_bid + price_difference) > end_price)) { // if we can't survive anyway, there is no point checking
        //equity_at_target = equity;// - volume_of_open_trades * MathAbs(distance);
        equity_at_target = equity - (volume_of_open_trades + volume_of_open_trades_up) * MathAbs(distance) * contract_size;
        double margin_level = equity_at_target / used_margin * 100;
        double trade_size = min_volume_alg;
        double local_used_margin = 0;
        double starting_price = current_ask;
        double len = end_price - starting_price;

        if(margin_level > margin_stop_out_level) {
            // quantize the fees for 1 trade
            //double d_equity = trade_size * (starting_price - end_price) / 2 + trade_size * current_spread_and_commission;
            //double d_equity = contract_size * trade_size * (starting_price - end_price) + trade_size * current_spread_and_commission;
            double d_equity = contract_size * trade_size * (end_price - starting_price) + trade_size * current_spread_and_commission * contract_size;
            // d_used_margin = ((end_price * trade_size / leverage) + (starting_price * trade_size / leverage))/2
            switch(calc_mode) {
            case SYMBOL_CALC_MODE_CFDLEVERAGE:
                //Margin: (Lots * ContractSize * MarketPrice) / Leverage * Margin_Rate
                local_used_margin += (trade_size * contract_size * starting_price) / leverage * initial_margin_rate;
                local_used_margin += (trade_size * contract_size * end_price) / leverage * initial_margin_rate;
                break;
            case SYMBOL_CALC_MODE_FOREX:
                //Margin:  Lots * Contract_Size / Leverage * Margin_Rate
                local_used_margin += trade_size * (initial_margin > 0 ? initial_margin : contract_size / leverage) * initial_margin_rate;
                local_used_margin += trade_size * (initial_margin > 0 ? initial_margin : contract_size / leverage) * initial_margin_rate;
                break;
            case SYMBOL_CALC_MODE_FOREX_NO_LEVERAGE:
                local_used_margin += trade_size * (initial_margin > 0 ? initial_margin : contract_size) * initial_margin_rate;
                local_used_margin += trade_size * (initial_margin > 0 ? initial_margin : contract_size) * initial_margin_rate;
                break;
            case SYMBOL_CALC_MODE_CFD:
                //Margin: Lots * ContractSize * MarketPrice * Margin_Rate
                local_used_margin += trade_size * contract_size * starting_price * initial_margin_rate;
                local_used_margin += trade_size * contract_size * end_price * initial_margin_rate;
                break;
            default:
                break; }
            local_used_margin = local_used_margin / 2;

            double number_of_trades;
            if(used_margin == 0) {
                number_of_trades = MathFloor( equity_at_target / (margin_stop_out_level / 100 * local_used_margin + d_equity) ); }
            else {
                number_of_trades = MathFloor( (equity_at_target - margin_stop_out_level / 100 * used_margin) / (margin_stop_out_level / 100 * local_used_margin + d_equity) ); }
            //number_of_trades = number_of_trades / contract_size;
            double proportion = number_of_trades / len;
            double temp;
            switch (sizing) {
            case 0: // constant
                if(proportion >= 1) {
                    spacing_sell = 1;
                    trade_size_sell = MathFloor(proportion) * min_volume_alg; }
                else {
                    trade_size_sell = min_volume_alg;
                    spacing_sell = MathRound((len / number_of_trades) * 100) / 100; }
                break;
            case 1: // incremental
                temp = (-1 + MathSqrt(1 + 8 * number_of_trades)) / 2;
                temp = MathFloor(temp);
                spacing_sell = len / (temp - 1);
                trade_size_sell = min_volume_alg;
                break;
            default:
                break; }
            Print("@!@ spacing for sell: ", spacing_sell, " trade size: ", trade_size_sell); } } }

//+------------------------------------------------------------------+

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
double open_down_w_down(double local_unit, double current_bid) {
    static MqlTradeResult trade_result = { };
    static MqlTradeRequest req = { };
    static double min_volume_alg;
    static double max_volume_alg;
    static int local_digit;
    static bool first_run = true;
    if(first_run) {
        req.symbol = _Symbol;
        req.action = TRADE_ACTION_DEAL;
        req.type = ORDER_TYPE_SELL;
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
//req.type = ORDER_TYPE_SELL;
    req.price = current_bid;
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
void open_new_down_w_down() {

    static double volume_of_open_trades;
    static double checked_last_open_price;

    static double cm;
    static double current_commission;

    static double min_volume_alg;
    static double max_volume_alg;

    static double margin_stop_out_level;

    static double local_survive_up;

    static double contract_size;
    static double initial_margin_rate = 0;     // initial margin rate
    static double maintenance_margin_rate = 0; // maintenance margin rate
    static double initial_margin = 0;  // SYMBOL_MARGIN_INITIAL override
    static long leverage;
    static int calc_mode;

    static bool first_run = true;

    string cmnt; // comment
    ushort u_sep = StringGetCharacter(";", 0);
    string result[];

    if(first_run) {
        checked_last_open_price = DBL_MAX;
        volume_of_open_trades = 0;
        for(int PositionIndex = PositionsTotal() - 1; PositionIndex >= 0; PositionIndex--) {
            if(!PositionSelectByTicket(PositionGetTicket(PositionIndex))) {
                continue; } // if failed to select
            cmnt = PositionGetString(POSITION_COMMENT);
            if((PositionGetString(POSITION_SYMBOL) == _Symbol) && (PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_SELL)) {
                int k = StringSplit(cmnt, u_sep, result);
                if(k == 0) {
                    if(m_position.SelectByTicket(PositionGetTicket(PositionIndex))) {
                        volume_of_open_trades += PositionGetDouble(POSITION_VOLUME);
                        double open_price = PositionGetDouble(POSITION_PRICE_OPEN);
                        checked_last_open_price = MathMin(checked_last_open_price, open_price);  } } } }

        Print("init: checked_last_open_price ", checked_last_open_price);
        //
        cm = commission * 100;
        current_commission = _Point * cm;
        min_volume_alg = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN);
        max_volume_alg = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MAX);
        margin_stop_out_level = AccountInfoDouble(ACCOUNT_MARGIN_SO_SO);
        SymbolInfoMarginRate(_Symbol, ORDER_TYPE_SELL, initial_margin_rate, maintenance_margin_rate);
        contract_size = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);
        calc_mode = SymbolInfoInteger(_Symbol, SYMBOL_TRADE_CALC_MODE);
        leverage = AccountInfoInteger(ACCOUNT_LEVERAGE);
        local_survive_up = 2 * survive;
        first_run = false; }

//new_tick_values();
    double current_spread = _Point * SymbolInfoInteger(_Symbol, SYMBOL_SPREAD);
    double current_spread_and_commission = (current_spread + current_commission);// * contract_size;

    //double current_ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    double current_bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);

    double balance  = AccountInfoDouble(ACCOUNT_BALANCE);
    double equity = AccountInfoDouble(ACCOUNT_EQUITY);
    double current_margin_level = AccountInfoDouble(ACCOUNT_MARGIN_LEVEL);
    double used_margin = AccountInfoDouble(ACCOUNT_MARGIN);

// open new
    if((volume_of_open_trades == 0) || (checked_last_open_price > current_bid)) {
        checked_last_open_price = current_bid;

        volume_of_open_trades = 0;
        for(int PositionIndex = PositionsTotal() - 1; PositionIndex >= 0; PositionIndex--) {
            if(!PositionSelectByTicket(PositionGetTicket(PositionIndex))) {
                continue; } // if failed to select
            cmnt = PositionGetString(POSITION_COMMENT);
            if((PositionGetString(POSITION_SYMBOL) == _Symbol) && (PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_SELL)) {
                int k = StringSplit(cmnt, u_sep, result);
                if(k == 0) {
                    if(m_position.SelectByTicket(PositionGetTicket(PositionIndex))) {
                        volume_of_open_trades += PositionGetDouble(POSITION_VOLUME); } } } }

        double equity_at_target = equity * margin_stop_out_level / current_margin_level;
        double equity_difference = equity - equity_at_target;
        double price_difference = equity_difference / (volume_of_open_trades * contract_size);

        double end_price = current_bid * ((100 + local_survive_up) / 100);

        double distance = end_price - current_bid;

        if((volume_of_open_trades == 0) || ((current_bid + price_difference) > end_price)) { // if we can't survive anyway, there is no point checking
            equity_at_target = equity - volume_of_open_trades * MathAbs(distance) * contract_size;
            double margin_level = equity_at_target / used_margin * 100;
            double trade_size = 0;
            double local_used_margin = 0;
            double starting_price = current_bid;

            switch(calc_mode) {
            case SYMBOL_CALC_MODE_CFDLEVERAGE:
                //Margin: (Lots * ContractSize * MarketPrice) / Leverage * Margin_Rate
                trade_size = NormalizeDouble((100 * equity * leverage - 100 * contract_size * MathAbs(distance) * volume_of_open_trades * leverage - leverage * margin_stop_out_level * used_margin) / ( contract_size * ( 100 * MathAbs(distance) * leverage + 100 * current_spread_and_commission * leverage + starting_price * initial_margin_rate * margin_stop_out_level)), 2);
                break;
            case SYMBOL_CALC_MODE_FOREX:
                //Margin:  Lots * Contract_Size / Leverage * Margin_Rate
                trade_size = NormalizeDouble((100 * leverage * equity - 100 * contract_size * MathAbs(distance) * leverage * volume_of_open_trades - leverage * margin_stop_out_level * used_margin) / ( contract_size * ( 100 * MathAbs(distance) * leverage + 100 * current_spread_and_commission * leverage + (initial_margin > 0 ? initial_margin * leverage / contract_size : 1.0) * initial_margin_rate * margin_stop_out_level)), 2);
                break;
            case SYMBOL_CALC_MODE_FOREX_NO_LEVERAGE:
                // Margin:  Lots * Contract_Size * Margin_Rate
                trade_size = NormalizeDouble((100 * equity - 100 * contract_size * MathAbs(distance) * volume_of_open_trades - margin_stop_out_level * used_margin) / ( contract_size * ( 100 * MathAbs(distance) + 100 * current_spread_and_commission + (initial_margin > 0 ? initial_margin / contract_size : 1.0) * initial_margin_rate * margin_stop_out_level)), 2);
                break;
            case SYMBOL_CALC_MODE_CFD:
                //Margin: Lots * ContractSize * MarketPrice * Margin_Rate
                trade_size = NormalizeDouble((100 * equity - 100 * contract_size * MathAbs(distance) * volume_of_open_trades - margin_stop_out_level * used_margin) / ( contract_size * ( 100 * MathAbs(distance) + 100 * current_spread_and_commission + starting_price * initial_margin_rate * margin_stop_out_level)), 2);
                break;
            default:
                break; }
            if(trade_size >= min_volume_alg) {
                volume_of_open_trades += open_down_w_down(trade_size, current_bid);
                checked_last_open_price = current_bid; } } }
    volume_of_open_trades_up = volume_of_open_trades; }

//+------------------------------------------------------------------+
