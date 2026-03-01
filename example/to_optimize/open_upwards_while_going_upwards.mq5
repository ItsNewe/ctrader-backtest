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
input double commission = 0; // no commission on indicies
input double survive_down = 4; //survive down %
input int digit = 2;
//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
double calculate_equity_at_target(double margin_stop_out_level, double current_margin_level, double equity, double volume_of_open_trades, double balance, double current_ask, bool print_info = false) {

    double margin_target_level = margin_stop_out_level; // either manual stop out or margin_stop_out_level
    double equity_at_target = equity * margin_target_level / current_margin_level;
    double equity_difference = equity - equity_at_target;
    double price_difference = equity_difference / volume_of_open_trades;
    double equity_at_target_without_balance = equity_at_target - balance;

    if(print_info) {
        Print("@@ current price: ", current_ask, " price difference: ", price_difference, " lowest survivable price: ", current_ask - price_difference, " equity there: ", equity_at_target); }

    return equity_at_target_without_balance; }
//+------------------------------------------------------------------+
//|                                                                  |
//+------------------------------------------------------------------+
double open(double local_unit, double current_ask) {
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

    static double cm;
    static double current_commission;

    static double min_volume_alg;
    static double max_volume_alg;

    static double margin_stop_out_level;

    static double local_survive_down;

    static double contract_size;
    static double initial_margin_rate = 0;     // initial margin rate
    static double maintenance_margin_rate = 0; // maintenance margin rate
    static double initial_margin = 0;  // SYMBOL_MARGIN_INITIAL override
    static long leverage;
    static int calc_mode;

    static bool first_run = true;

    if(first_run) {
        checked_last_open_price = DBL_MIN;
        volume_of_open_trades = 0;
        for(int PositionIndex = PositionsTotal() - 1; PositionIndex >= 0; PositionIndex--) {
            if(!PositionSelectByTicket(PositionGetTicket(PositionIndex))) {
                continue; } // if failed to select
            if((PositionGetString(POSITION_SYMBOL) == _Symbol) && (PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY)) {
                if(m_position.SelectByTicket(PositionGetTicket(PositionIndex))) {
                    volume_of_open_trades += PositionGetDouble(POSITION_VOLUME);
                    double open_price = PositionGetDouble(POSITION_PRICE_OPEN);
                    checked_last_open_price = MathMax(checked_last_open_price, open_price);  } } }

        Print("init: checked_last_open_price ", checked_last_open_price);
        //
        cm = commission * 100;
        current_commission = _Point * cm;
        min_volume_alg = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN);
        max_volume_alg = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MAX);
        margin_stop_out_level = AccountInfoDouble(ACCOUNT_MARGIN_SO_SO);
        SymbolInfoMarginRate(_Symbol, ORDER_TYPE_BUY, initial_margin_rate, maintenance_margin_rate);
    initial_margin = SymbolInfoDouble(_Symbol, SYMBOL_MARGIN_INITIAL);
        contract_size = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);
        calc_mode = SymbolInfoInteger(_Symbol, SYMBOL_TRADE_CALC_MODE);
        leverage = AccountInfoInteger(ACCOUNT_LEVERAGE);
        local_survive_down = survive_down;
        first_run = false; }

//new_tick_values();
    double current_spread = _Point * SymbolInfoInteger(_Symbol, SYMBOL_SPREAD);
    double current_spread_and_commission = (current_spread + current_commission);// * contract_size;

    double current_ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
//current_bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);

    double balance  = AccountInfoDouble(ACCOUNT_BALANCE);
    double equity = AccountInfoDouble(ACCOUNT_EQUITY);
    double current_margin_level = AccountInfoDouble(ACCOUNT_MARGIN_LEVEL);
    double used_margin = AccountInfoDouble(ACCOUNT_MARGIN);

// open new
    if((volume_of_open_trades == 0) || (checked_last_open_price < current_ask)) {
        checked_last_open_price = current_ask;

        volume_of_open_trades = 0;
        for(int PositionIndex = PositionsTotal() - 1; PositionIndex >= 0; PositionIndex--) {
            if(!PositionSelectByTicket(PositionGetTicket(PositionIndex))) {
                continue; } // if failed to select
            if((PositionGetString(POSITION_SYMBOL) == _Symbol) && (PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY)) {
                if(m_position.SelectByTicket(PositionGetTicket(PositionIndex))) {
                    volume_of_open_trades += PositionGetDouble(POSITION_VOLUME); } } }

        double equity_at_target = equity * margin_stop_out_level / current_margin_level;
        double equity_difference = equity - equity_at_target;
        double price_difference = equity_difference / (volume_of_open_trades * contract_size);

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
                volume_of_open_trades += open(trade_size, current_ask);
                checked_last_open_price = current_ask; } } } }
//+------------------------------------------------------------------+
//| Expert initialization function                                   |
//+------------------------------------------------------------------+
int OnInit() {
    m_trade.LogLevel(LOG_LEVEL_NO); // LOG_LEVEL_ALL LOG_LEVEL_ERRORS LOG_LEVEL_NO
    m_trade.SetAsyncMode(true);
    return(INIT_SUCCEEDED); }
//+------------------------------------------------------------------+
