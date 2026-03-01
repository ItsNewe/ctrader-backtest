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
input double survive = 5;
input double commission = 0;
input double density_distortion = 1;
input int sizing = 0; // sizing: constant, incremental, density
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

double value_of_buy_trades;

double lowest_buy;
double highest_buy;

double volume_of_open_trades;

double trade_size_buy;
double spacing_buy;
//

int digit = 2;

int direction = 0;
int direction_change = 0;

int count_buy = 0;

bool commission_set = false;

bool close_all_buy_flag = false;
bool close_all_profitable_buy_flag = false;

bool is_there_unprofitable_buy = false;

string first;
string second;
string currency;
//
MqlTradeResult trade_result = { };
MqlTradeRequest req = { };

double density_spacing[];

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
    close_all_buy_flag = false;
    close_all_profitable_buy_flag = false; }
//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
void iterate() {
    value_of_buy_trades = 0;

    count_buy = 0;

    volume_of_open_trades = 0;

    lowest_buy = DBL_MAX;

    highest_buy = DBL_MIN;

    is_there_unprofitable_buy = false;

    if(sizing == 1) {
        trade_size_buy = 0; }

    double open_price, profit, /*swap,*/ lots;

    ulong ticket;

    for(int PositionIndex = PositionsTotal() - 1; PositionIndex >= 0; PositionIndex--) {
        ticket = PositionGetTicket(PositionIndex);
        if(!PositionSelectByTicket(ticket)) {
            continue; } // if failed to select
        //if (PositionGetString(POSITION_SYMBOL) == _Symbol)
        {
            if(m_position.SelectByTicket(ticket)) {
                lots = PositionGetDouble(POSITION_VOLUME);
                //swap = OrderSwap();
                profit = m_position.Profit();// + swap;// - commission_local;
                open_price = PositionGetDouble(POSITION_PRICE_OPEN);
                if (PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY) {
                    if (PositionsTotal() == 1) {
                        if ((close_all_buy_flag) || (close_all_profitable_buy_flag)) {
                            if(open_price + current_spread_and_commission < current_ask) {
                                close(ticket); } } }
                    else {
                        if(close_all_buy_flag) {
                            close(ticket);
                            continue; }
                        if((profit > 0) && (close_all_profitable_buy_flag)) {
                            close(ticket);
                            continue; } }
                    // check for open, unless it got closed
                    if(profit < 0) {
                        is_there_unprofitable_buy = true; }
                    value_of_buy_trades += profit;
                    lowest_buy = MathMin(lowest_buy, open_price);
                    highest_buy = MathMax(highest_buy, open_price);
                    count_buy++;
                    volume_of_open_trades += lots;
                    if(sizing == 1) {
                        trade_size_buy = MathMax(trade_size_buy, lots); } } } } }
    if(sizing == 1) {
        trade_size_buy += min_volume_alg; }
// clear flags
    clear_flags(); }
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
void open_new() {

    if(count_buy == 0) {
        sizing_buy(); }
// open new

    if (count_buy == 0) {
        if (open(trade_size_buy, 1)) {
            highest_buy = current_ask;
            lowest_buy = current_ask;
            count_buy++;
            if(sizing == 1) {
                trade_size_buy += min_volume_alg; } } }
    else {
        double temp, temp1, temp2, temp3 = 0, temp4, temp5, temp6;
        switch (sizing) {
        case 0: // constant
            if(lowest_buy > current_ask) {
                temp = ((highest_buy - current_ask) / spacing_buy);
                temp1 = MathFloor(temp);
                temp2 = temp - temp1;
                temp3 = temp1;
                temp4 = spacing_buy / (trade_size_buy / min_volume_alg);
                temp5 = MathFloor(temp2 / temp4);
                //temp6 = temp3 * trade_size_buy + temp5 * min_volume_alg - volume_of_open_trades;
                temp6 = temp3 * trade_size_buy + temp5 * min_volume_alg - (volume_of_open_trades - trade_size_buy);
                if (temp6 > 0) {
                    if (open(temp6, 1)) {
                        lowest_buy = current_ask;
                        count_buy++; } } }
            break;
        case 1: // incremental
            if(lowest_buy > current_ask) {
                temp = ((highest_buy - current_ask) / spacing_buy);
                temp1 = MathFloor(temp);
                temp2 = temp - temp1;
                for(int i = 1; i <= temp1; i++) {
                    temp3 += i; }
                temp4 = spacing_buy / (temp1 + 1);
                temp5 = MathFloor(temp2 / temp4);
                temp6 = (temp3 + temp5) * min_volume_alg - (volume_of_open_trades - min_volume_alg);
                if (temp6 > 0) {
                    if (open(temp6, 1)) {
                        lowest_buy = current_ask;
                        count_buy++; } } }
            break;
        case 2: { // density
            int count = MathMin(MathMax(0, PositionsTotal()), AccountInfoInteger(ACCOUNT_LIMIT_ORDERS));
            if((density_spacing[count] > 0) && (lowest_buy > current_ask + density_spacing[count])) {
                if (open(trade_size_buy, 1)) {
                    lowest_buy = current_ask;
                    count_buy++; } } }
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

    max_balance = MathMax(max_balance, current_balance);

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
        switch (closing_mode) {
        case 0:
            if(value_of_buy_trades > 0) {
                close_all_profitable_buy_flag = true;
                iterate(); }
            break;
        case 1:
            if(!is_there_unprofitable_buy) {
                close_all_buy_flag = true;
                iterate(); }
            break;
        default:
            break; }
        clear_turn_marks(); }
    if((direction != 1) && (bid_at_the_time_of_turn_up <= current_bid - current_spread_and_commission) && (ask_at_the_time_of_turn_up <= current_ask - current_spread_and_commission)) {
        direction = 1;
        direction_change = 1;
        clear_turn_marks(); } }

//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
void OnTick() {
    new_tick_values();
// direction
    direction_check();
// check for event

    open_new(); }

//+------------------------------------------------------------------+
//| Expert initialization function                                   |
//+------------------------------------------------------------------+
int OnInit() {

    if ((sizing != 2) && (density_distortion != 1)) {
        return(INIT_PARAMETERS_INCORRECT); }

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

    ArrayResize(density_spacing, AccountInfoInteger(ACCOUNT_LIMIT_ORDERS) + 1, 0);
    ArrayInitialize(density_spacing, 0);

    if (min_volume_alg == 0.01) {
        digit = 2; }
    else if (min_volume_alg == 0.1) {
        digit = 1; }
    else {
        digit = 0; }
//
    value_of_buy_trades = 0;

    count_buy = 0;

    volume_of_open_trades = 0;

    lowest_buy = DBL_MAX;
    highest_buy = DBL_MIN;

    if(sizing == 1) {
        trade_size_buy = 0; }

    double open_price, profit, /*swap,*/ lots;

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
                //swap = OrderSwap();
                profit = m_position.Profit();// + swap;// - commission_local;
                open_price = PositionGetDouble(POSITION_PRICE_OPEN);
                cmnt = PositionGetString(POSITION_COMMENT);
                if (PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY) {
                    int k = StringSplit(cmnt, u_sep, result);
                    if(k > 0) {
                        PrintFormat("result[%d]=\"%s\"", 0, result[0]);
                        spacing_buy = StringToDouble(result[0]);
                        PrintFormat("result[%d]=\"%s\"", 1, result[1]);
                        if(sizing == 0)
                            trade_size_buy = StringToDouble(result[1]); }
                    // check for open, unless it got closed
                    value_of_buy_trades += profit;
                    lowest_buy = MathMin(lowest_buy, open_price);
                    highest_buy = MathMax(highest_buy, open_price);
                    count_buy++;
                    volume_of_open_trades += lots;
                    if(sizing == 1) {
                        trade_size_buy = MathMax(trade_size_buy, lots); } } } } }
    if(sizing == 1) {
        trade_size_buy += min_volume_alg; }
// clear flags
    clear_flags();
//

    return(INIT_SUCCEEDED); }
//+------------------------------------------------------------------+
//| Expert deinitialization function                                 |
//+------------------------------------------------------------------+
void OnDeinit(const int reason) {}
//+------------------------------------------------------------------+
void sizing_buy() {

    trade_size_buy = 0;
    spacing_buy = 0;
    static int calc_mode = SymbolInfoInteger(_Symbol, SYMBOL_TRADE_CALC_MODE);
    static double contract_size = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);
    static long leverage = AccountInfoInteger(ACCOUNT_LEVERAGE);
    static double initial_margin_rate = 0;     // initial margin rate
    static double maintenance_margin_rate = 0; // maintenance margin rate
    static double initial_margin = 0;  // SYMBOL_MARGIN_INITIAL override
    SymbolInfoMarginRate(_Symbol, ORDER_TYPE_BUY, initial_margin_rate, maintenance_margin_rate);
    initial_margin = SymbolInfoDouble(_Symbol, SYMBOL_MARGIN_INITIAL);
    double current_margin_level = AccountInfoDouble(ACCOUNT_MARGIN_LEVEL);
    double used_margin = AccountInfoDouble(ACCOUNT_MARGIN);
    double margin_stop_out_level = AccountInfoDouble(ACCOUNT_MARGIN_SO_SO);
    double equity = AccountInfoDouble(ACCOUNT_EQUITY);

    double equity_at_target = equity * margin_stop_out_level / current_margin_level;
    double equity_difference = equity - equity_at_target;
    double price_difference = equity_difference / (volume_of_open_trades * contract_size);

    double end_price = current_ask * ((100 - survive) / 100);
    double distance = end_price - current_ask;

    if((count_buy == 0) || ((current_ask - price_difference) < end_price)) { // if we can't survive anyway, there is no point checking
        equity_at_target = equity;// - volume_of_open_trades * MathAbs(distance);
        double margin_level = equity_at_target / used_margin * 100;
        double trade_size = min_volume_alg;
        double local_used_margin = 0;
        double starting_price = current_ask;
        double len = starting_price - end_price;

        if(margin_level > margin_stop_out_level) {
            // quantize the fees for 1 trade
            double d_equity = contract_size * trade_size * (starting_price - end_price) + trade_size * current_spread_and_commission * contract_size;
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

            //number_of_trades = MathMin(number_of_trades, AccountInfoInteger(ACCOUNT_LIMIT_ORDERS));

            double proportion = number_of_trades / len;
            double temp;
            switch (sizing) {
            case 0: // constant
                if(proportion >= 1) {
                    spacing_buy = 1;
                    trade_size_buy = MathFloor(proportion) * min_volume_alg; }
                else {
                    trade_size_buy = min_volume_alg;
                    spacing_buy = MathRound((len / number_of_trades) * 100) / 100; }
                break;
            case 1: // incremental
                temp = (-1 + MathSqrt(1 + 8 * number_of_trades)) / 2;
                temp = MathFloor(temp);
                spacing_buy = len / (temp - 1);
                trade_size_buy = min_volume_alg;
                break;
            case 2: { // density
                if(proportion >= 1) {
                    spacing_buy = 1;
                    trade_size_buy = MathFloor(proportion) * min_volume_alg; }
                else {
                    trade_size_buy = min_volume_alg;
                    spacing_buy = MathRound((len / number_of_trades) * 100) / 100; }

                double sum = ((number_of_trades - 1) * number_of_trades / 2) * spacing_buy;
                double sum_2 = 0;
                double unit = 0;
                double count = 0;
                while (sum_2 < sum) {
                    sum_2 = 0;
                    unit += 0.01;
                    count = 0;
                    for (int i = number_of_trades - 1; i > 0; i--) {
                        sum_2 += i * unit * MathPow(density_distortion, count);
                        count ++; } }
                unit -= 0.01;
                count = 0;
                ArrayInitialize(density_spacing, 0);
                Print ("@!@ --");
                double total_spacing = 0;
                for (int i = number_of_trades; i > 0; i--) {
                    density_spacing[i] = unit * MathPow(density_distortion, count);
                    total_spacing += density_spacing[i];
                    Print ("@!@ :", density_spacing[i]);
                    count ++; }
                double ep = current_ask - total_spacing;
                Print("room: ",  ep / current_ask * 100, "%"); }
            break;
            default:
                break; }
            //Print("@!@ spacing for buy: ", spacing_buy, " trade size: ", trade_size_buy);
        } } }

//+------------------------------------------------------------------+

																	  
double max_balance = 0;
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
    return max_balance; }
//+------------------------------------------------------------------+
