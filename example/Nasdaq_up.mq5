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
input double prcnt_manual_stop_out_loss = 74; // prcnt_manual_stop_out loss
input double commission = 0; // no commission on indicies
input double multiplier = 0.1;
input double power = 0.1;
input int digit = 2;
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

    static double local_prcnt_manual_stop_out_loss;
    static double local_starting_room;
    static double room;
    static double starting_x;

    static double volume_of_open_trades;
    static double checked_last_open_price;

    static double cm;
    static double current_commission;

    static double min_volume_alg;
    static double max_volume_alg;

    static double contract_size;

    static long leverage;

    static bool first_run = true;

    if(first_run) {
        checked_last_open_price = DBL_MIN;
        volume_of_open_trades = 0;
        // not needed for testing
        for(int PositionIndex = PositionsTotal() - 1; PositionIndex >= 0; PositionIndex--) {
            if(!PositionGetTicket(PositionIndex)) {
                continue; } // if failed to select
            if((PositionGetString(POSITION_SYMBOL) == _Symbol) && (PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY)) {
                if(m_position.SelectByTicket(PositionGetTicket(PositionIndex))) {
                    volume_of_open_trades += PositionGetDouble(POSITION_VOLUME);
                    double open_price = PositionGetDouble(POSITION_PRICE_OPEN);
                    checked_last_open_price = MathMax(checked_last_open_price, open_price); } } }
        Print("init: last_open_price ", checked_last_open_price);
        //
        cm = commission * 100;
        current_commission = _Point * cm;
        min_volume_alg = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN);
        max_volume_alg = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MAX);
        leverage = AccountInfoInteger(ACCOUNT_LEVERAGE);
        contract_size = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);
        local_prcnt_manual_stop_out_loss = prcnt_manual_stop_out_loss;
        first_run = false; }

//new_tick_values();
    double current_spread = _Point * SymbolInfoInteger(_Symbol, SYMBOL_SPREAD);
    double current_spread_and_commission = (current_spread + current_commission) * contract_size;

    double current_ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
//current_bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);

    double balance  = AccountInfoDouble(ACCOUNT_BALANCE);
    double equity = AccountInfoDouble(ACCOUNT_EQUITY);
    double current_margin_level = AccountInfoDouble(ACCOUNT_MARGIN_LEVEL);
    double used_margin = AccountInfoDouble(ACCOUNT_MARGIN);
// close
    if((current_margin_level < local_prcnt_manual_stop_out_loss) && (current_margin_level != 0)) {
        //iterate_and_close();
        for(int PositionIndex = PositionsTotal() - 1; PositionIndex >= 0; PositionIndex--) {
            // not needed for testing
            if(!PositionGetTicket(PositionIndex)) {
                continue; } // if failed to select
            //
            if((PositionGetString(POSITION_SYMBOL) == _Symbol) && (PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY)) {
                if(m_position.SelectByTicket(PositionGetTicket(PositionIndex))) {
                    if(!m_trade.PositionClose(m_position.Ticket())) {
                        Print("Order Close failed, order number: ", m_position.Ticket(), " Error: ", GetLastError());
                        Print(" __FUNCTION__ = ", __FUNCTION__, "  __LINE__ = ", __LINE__);
                        ResetLastError(); } } } }
        //
        checked_last_open_price = DBL_MIN;
        volume_of_open_trades = 0;
        balance  = AccountInfoDouble(ACCOUNT_BALANCE);
        equity = AccountInfoDouble(ACCOUNT_EQUITY);
        current_margin_level = AccountInfoDouble(ACCOUNT_MARGIN_LEVEL);
        used_margin = AccountInfoDouble(ACCOUNT_MARGIN);  }

// open new
    if(volume_of_open_trades == 0) {
        //local_starting_room = multiplier * MathPow(1, power); // 1 on the power of anything is 1
        //local_starting_room = multiplier;
        local_starting_room = current_ask * multiplier / 100;
        double temp = MathFloor((100 * balance * leverage) / (100 * local_starting_room * leverage + 100 * current_spread_and_commission * leverage + local_prcnt_manual_stop_out_loss * current_ask));// / min_volume_alg;
        temp = temp / contract_size;
        //temp = temp * min_volume_alg;
        if (temp > 0) {
            double margin, temp2;
            OrderCalcMargin(ORDER_TYPE_BUY, _Symbol, min_volume_alg, current_ask, margin);
            temp2 = AccountInfoDouble(ACCOUNT_MARGIN_FREE) / margin;
            temp2 = temp2 * min_volume_alg;

            volume_of_open_trades += open(MathMin(temp, temp2), current_ask);
            checked_last_open_price = current_ask;
            room = local_starting_room;
            starting_x = current_ask;
            Print("Room: ", room); } }
    else if(checked_last_open_price < current_ask) {
        //double room_temp = room - local_slope * (current_ask - checked_last_open_price);
        //double room_temp = multiplier * MathPow(current_ask - starting_x + 1, power);
        double room_temp = local_starting_room * MathPow(current_ask - starting_x, power);
        double temp = MathFloor((100 * equity * leverage - leverage * local_prcnt_manual_stop_out_loss * used_margin - 100 * room_temp * leverage * volume_of_open_trades) / (100 * room_temp * leverage + 100 * current_spread_and_commission * leverage + local_prcnt_manual_stop_out_loss * current_ask));// / min_volume_alg;
        temp = temp / contract_size;
        //temp = temp * min_volume_alg;
        if(temp >= min_volume_alg) {
            double margin, temp2;
            OrderCalcMargin(ORDER_TYPE_BUY, _Symbol, min_volume_alg, current_ask, margin);
            temp2 = AccountInfoDouble(ACCOUNT_MARGIN_FREE) / margin;
            temp2 = temp2 * min_volume_alg;

            volume_of_open_trades += open(MathMin(temp, temp2), current_ask);
            checked_last_open_price = current_ask;
            room = room_temp;
            Print("Room: ", room); } } }
//+------------------------------------------------------------------+
//| Expert initialization function                                   |
//+------------------------------------------------------------------+
int OnInit() {
    m_trade.LogLevel(LOG_LEVEL_NO); // LOG_LEVEL_ALL LOG_LEVEL_ERRORS LOG_LEVEL_NO
//m_trade.SetAsyncMode(true);
    return(INIT_SUCCEEDED); }
//+------------------------------------------------------------------+
