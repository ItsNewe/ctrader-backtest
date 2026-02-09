//+------------------------------------------------------------------+
//| fill_up_verbose.mq5 - Verbose logging version for debugging     |
//| Logs: swap charges, position states at midnight, trade events   |
//+------------------------------------------------------------------+

#include <Trade\AccountInfo.mqh>
#include <Trade\PositionInfo.mqh>
#include <Trade\Trade.mqh>

CAccountInfo m_account;
CPositionInfo m_position;
CTrade m_trade;

//+------------------------------------------------------------------+
//| Input parameters                                                  |
//+------------------------------------------------------------------+
input double survive = 2.5;
input double size = 1;
input double spacing = 1;
input bool verbose_logging = true;  // Enable detailed logging

double min_volume_alg, max_volume_alg;

double current_spread = 0, point = 0;
double current_equity, current_balance;

double current_ask = 0;
double current_bid = 0;

double lowest_buy;
double highest_buy;

double trade_size_buy;
double spacing_buy;

double closest_above, closest_below;

double volume_of_open_trades = 0;

int digit = 2;
MqlTradeResult trade_result = { };
MqlTradeRequest req = { };

// Tracking variables
datetime last_date = 0;
double last_equity = 0;
double total_swap_charged = 0;
int total_trades_opened = 0;
int total_trades_closed = 0;

//+------------------------------------------------------------------+
//| Log position state (call at midnight for swap analysis)          |
//+------------------------------------------------------------------+
void LogPositionState(string context) {
    if (!verbose_logging) return;

    double total_lots = 0;
    double total_profit = 0;
    double total_swap = 0;
    int position_count = 0;

    for(int i = PositionsTotal() - 1; i >= 0; i--) {
        ulong ticket = PositionGetTicket(i);
        if(PositionSelectByTicket(ticket)) {
            total_lots += PositionGetDouble(POSITION_VOLUME);
            total_profit += PositionGetDouble(POSITION_PROFIT);
            total_swap += PositionGetDouble(POSITION_SWAP);
            position_count++;
        }
    }

    PrintFormat("[%s] %s | Positions: %d | Total Lots: %.4f | Profit: $%.2f | Swap: $%.2f | Equity: $%.2f | Balance: $%.2f",
                TimeToString(TimeCurrent(), TIME_DATE|TIME_MINUTES),
                context,
                position_count,
                total_lots,
                total_profit,
                total_swap,
                current_equity,
                current_balance);
}

//+------------------------------------------------------------------+
//| Check for day change and log swap info                           |
//+------------------------------------------------------------------+
void CheckDayChange() {
    datetime current_time = TimeCurrent();
    MqlDateTime dt;
    TimeToStruct(current_time, dt);

    // Create date-only value (midnight)
    datetime current_date = StringToTime(StringFormat("%04d.%02d.%02d", dt.year, dt.mon, dt.day));

    if (last_date != 0 && current_date != last_date) {
        // Day changed - this is when swap is charged
        double equity_change = current_equity - last_equity;

        // Get current total swap from all positions
        double current_total_swap = 0;
        double total_lots = 0;
        for(int i = PositionsTotal() - 1; i >= 0; i--) {
            ulong ticket = PositionGetTicket(i);
            if(PositionSelectByTicket(ticket)) {
                current_total_swap += PositionGetDouble(POSITION_SWAP);
                total_lots += PositionGetDouble(POSITION_VOLUME);
            }
        }

        // Determine day of week (for triple swap detection)
        string day_names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        string day_name = day_names[dt.day_of_week];

        PrintFormat("=== DAY CHANGE: %s (%s) ===", TimeToString(current_date, TIME_DATE), day_name);
        PrintFormat("  Open positions: %d", PositionsTotal());
        PrintFormat("  Total lots: %.4f", total_lots);
        PrintFormat("  Cumulative swap: $%.2f", current_total_swap);
        PrintFormat("  Equity change overnight: $%.2f", equity_change);
        PrintFormat("  Current Balance: $%.2f | Equity: $%.2f", current_balance, current_equity);

        LogPositionState("OVERNIGHT");
    }

    last_date = current_date;
    last_equity = current_equity;
}

//+------------------------------------------------------------------+
//| Iterate through positions                                         |
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
                        if(open_price >= current_ask) {
                            closest_above = MathMin(closest_above, open_price - current_ask); }
                        if(open_price <= current_ask) {
                            closest_below = MathMin(closest_below, current_ask - open_price); } } } } } } }

//+------------------------------------------------------------------+
//| Open position                                                     |
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
            local_unit = 0; }
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
        req.deviation = 1;
        long filling = SymbolInfoInteger(_Symbol, SYMBOL_FILLING_MODE);
        switch((int)filling) {
        case SYMBOL_FILLING_FOK:
            req.type_filling = ORDER_FILLING_FOK;
            break;
        case SYMBOL_FILLING_IOC:
            req.type_filling = ORDER_FILLING_IOC;
            break;
        case 4:
            req.type_filling = ORDER_FILLING_BOC;
            break;
        default:
            break; }
        if(!OrderSend(req, trade_result)) {
            Print(" Error: ", GetLastError());
            Print(" __FUNCTION__ = ", __FUNCTION__, "  __LINE__ = ", __LINE__);
            ResetLastError();
            return false; }
        else {
            total_trades_opened++;
            if (verbose_logging && (total_trades_opened % 1000 == 0)) {
                PrintFormat("[%s] MILESTONE: %d trades opened | Balance: $%.2f | Equity: $%.2f",
                           TimeToString(TimeCurrent(), TIME_DATE|TIME_MINUTES),
                           total_trades_opened, current_balance, current_equity);
            }
        }
    }
    return true; }

//+------------------------------------------------------------------+
//| Close position                                                    |
//+------------------------------------------------------------------+
void close(ulong ticket) {
    if(m_position.SelectByTicket(ticket)) {
        if(!m_trade.PositionClose(ticket)) {
            Print("Order Close failed, order number: ", ticket, " Error: ", GetLastError());
            Print(" __FUNCTION__ = ", __FUNCTION__, "  __LINE__ = ", __LINE__);
            ResetLastError(); }
        else {
            total_trades_closed++;
        }
    }
}

//+------------------------------------------------------------------+
//| Open new positions based on grid logic                           |
//+------------------------------------------------------------------+
void open_new() {

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
        else if((closest_above >= spacing_buy) && (closest_below >= spacing_buy)) {
            sizing_buy();
            open(trade_size_buy, 1); } } }

//+------------------------------------------------------------------+
//| Update tick values                                                |
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
//| OnTick - Main tick handler                                        |
//+------------------------------------------------------------------+
void OnTick() {

    new_tick_values();

    // Check for day change (swap logging)
    CheckDayChange();

    // iterate
    iterate();

    // check for event
    open_new(); }

//+------------------------------------------------------------------+
//| Expert initialization function                                   |
//+------------------------------------------------------------------+
int OnInit() {

    point = SymbolInfoDouble(_Symbol, SYMBOL_POINT);

    m_trade.LogLevel(LOG_LEVEL_NO);

    min_volume_alg = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN);
    max_volume_alg = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MAX);
    m_trade.SetAsyncMode(true);

    if(min_volume_alg == 0.01) {
        digit = 2; }
    else if(min_volume_alg == 0.1) {
        digit = 1; }
    else {
        digit = 0; }

    new_tick_values();

    lowest_buy = DBL_MAX;
    highest_buy = DBL_MIN;

    closest_above = DBL_MAX;
    closest_below = DBL_MIN;

    volume_of_open_trades = 0;

    // Initialize tracking
    last_date = 0;
    last_equity = current_equity;
    total_swap_charged = 0;
    total_trades_opened = 0;
    total_trades_closed = 0;

    double open_price, lots;
    ulong ticket;
    string cmnt;
    ushort u_sep = StringGetCharacter(";", 0);
    string result[];

    for(int PositionIndex = PositionsTotal() - 1; PositionIndex >= 0; PositionIndex--) {
        ticket = PositionGetTicket(PositionIndex);
        if(!PositionSelectByTicket(ticket)) {
            continue; }
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
                    volume_of_open_trades += lots;
                    lowest_buy = MathMin(lowest_buy, open_price);
                    highest_buy = MathMax(highest_buy, open_price);
                    if(open_price >= current_ask) {
                        closest_above = MathMin(closest_above, open_price - current_ask); }
                    if(open_price <= current_ask) {
                        closest_below = MathMin(closest_below, current_ask - open_price); } } } } }

    spacing_buy = spacing;
    trade_size_buy = size * min_volume_alg;

    // Log initial settings
    if (verbose_logging) {
        Print("=== FILL_UP VERBOSE LOGGING ENABLED ===");
        PrintFormat("Symbol: %s", _Symbol);
        PrintFormat("Point: %.5f", point);
        PrintFormat("Contract Size: %.0f", SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE));
        PrintFormat("Swap Long: %.2f", SymbolInfoDouble(_Symbol, SYMBOL_SWAP_LONG));
        PrintFormat("Swap Short: %.2f", SymbolInfoDouble(_Symbol, SYMBOL_SWAP_SHORT));
        PrintFormat("Swap Mode: %d", SymbolInfoInteger(_Symbol, SYMBOL_SWAP_MODE));
        PrintFormat("Swap 3-Day: %d (day of week)", SymbolInfoInteger(_Symbol, SYMBOL_SWAP_ROLLOVER3DAYS));
        PrintFormat("Initial Balance: $%.2f", current_balance);
        PrintFormat("Initial Equity: $%.2f", current_equity);
        PrintFormat("Spacing: %.2f | Size: %.2f", spacing, size);
        Print("========================================");
    }

    return(INIT_SUCCEEDED); }

//+------------------------------------------------------------------+
//| Expert deinitialization function                                 |
//+------------------------------------------------------------------+
double max_balance = 0;
double max_number_of_open = 0;
double max_used_funds = 0;
double max_trade_size = 0;

void OnDeinit(const int reason) {
    // Get final swap total from history
    double final_swap = 0;
    HistorySelect(0, TimeCurrent());
    int deals = HistoryDealsTotal();
    for(int i = 0; i < deals; i++) {
        ulong ticket = HistoryDealGetTicket(i);
        final_swap += HistoryDealGetDouble(ticket, DEAL_SWAP);
    }

    Print("=== FINAL SUMMARY ===");
    PrintFormat("Total Trades Opened: %d", total_trades_opened);
    PrintFormat("Total Deals in History: %d", deals);
    PrintFormat("Total Swap from Deals: $%.2f", final_swap);
    PrintFormat("Max Balance: $%.2f", max_balance);
    PrintFormat("Max Number of Open: %.0f", max_number_of_open);
    PrintFormat("Max Used Funds: $%.2f", max_used_funds);
    PrintFormat("Max Trade Size: %.4f", max_trade_size);
    PrintFormat("Final Balance: $%.2f", m_account.Balance());
    PrintFormat("Final Equity: $%.2f", m_account.Equity());
    Print("=====================");
}

//+------------------------------------------------------------------+
//| Sizing calculation                                                |
//+------------------------------------------------------------------+
void sizing_buy() {

    trade_size_buy = 0;

    static int calc_mode = SymbolInfoInteger(_Symbol, SYMBOL_TRADE_CALC_MODE);
    static double contract_size = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);
    static long leverage = AccountInfoInteger(ACCOUNT_LEVERAGE);
    static double initial_margin_rate = 0;
    static double maintenance_margin_rate = 0;
    SymbolInfoMarginRate(_Symbol, ORDER_TYPE_BUY, initial_margin_rate, maintenance_margin_rate);
    double current_margin_level = AccountInfoDouble(ACCOUNT_MARGIN_LEVEL);
    double used_margin = AccountInfoDouble(ACCOUNT_MARGIN);
    double margin_stop_out_level = AccountInfoDouble(ACCOUNT_MARGIN_SO_SO);
    double equity = AccountInfoDouble(ACCOUNT_EQUITY);

    double equity_at_target = equity * margin_stop_out_level / current_margin_level;
    double equity_difference = equity - equity_at_target;
    double price_difference = equity_difference / (volume_of_open_trades * contract_size);

    double end_price;
    if (PositionsTotal() == 0) {
        end_price = current_ask * ((100 - survive) / 100); }
    else {
        end_price = highest_buy * ((100 - survive) / 100); }
    double distance = current_ask - end_price;
    double number_of_trades = MathFloor(distance / spacing_buy);

    if((PositionsTotal() == 0) || ((current_ask - price_difference) < end_price)) {
        equity_at_target = equity - volume_of_open_trades * MathAbs(distance) * contract_size;
        double margin_level = equity_at_target / used_margin * 100;
        double trade_size = min_volume_alg;
        double local_used_margin = 0;
        double starting_price = current_ask;

        if(margin_level > margin_stop_out_level) {
            double d_equity = contract_size * trade_size * (number_of_trades * (number_of_trades + 1) / 2);
            double d_spread = number_of_trades * trade_size * current_spread * contract_size;
            d_equity += d_spread;
            switch(calc_mode) {
            case SYMBOL_CALC_MODE_CFDLEVERAGE:
                local_used_margin += (trade_size * contract_size * starting_price) / leverage * initial_margin_rate;
                local_used_margin += (trade_size * contract_size * end_price) / leverage * initial_margin_rate;
                break;
            case SYMBOL_CALC_MODE_FOREX:
                local_used_margin += trade_size * contract_size / leverage * initial_margin_rate;
                local_used_margin += trade_size * contract_size / leverage * initial_margin_rate;
                break;
            case SYMBOL_CALC_MODE_FOREX_NO_LEVERAGE:
                local_used_margin += trade_size * contract_size * initial_margin_rate;
                local_used_margin += trade_size * contract_size * initial_margin_rate;
                break;
            case SYMBOL_CALC_MODE_CFD:
                local_used_margin += trade_size * contract_size * starting_price * initial_margin_rate;
                local_used_margin += trade_size * contract_size * end_price * initial_margin_rate;
                break;
            default:
                break; }
            local_used_margin = local_used_margin / 2;
            local_used_margin = number_of_trades * local_used_margin;
            Print("@!@ required balance: ", d_equity + local_used_margin);
            double multiplier = 0;

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

            multiplier = MathMax(1, multiplier);

            trade_size_buy = multiplier * min_volume_alg;
            max_trade_size = MathMax(max_trade_size, trade_size_buy); } } }

//+------------------------------------------------------------------+
//| Tester result                                                     |
//+------------------------------------------------------------------+
double OnTester() {
    return max_number_of_open; }

//+------------------------------------------------------------------+
//| Modify positions                                                  |
//+------------------------------------------------------------------+
void Modify() {
    MqlTradeRequest request;
    MqlTradeResult  result;
    for(int i = 0; i < PositionsTotal(); i++) {
        ulong  position_ticket = PositionGetTicket(i);
        string position_symbol = PositionGetString(POSITION_SYMBOL);
        int    digits = (int)SymbolInfoInteger(position_symbol, SYMBOL_DIGITS);
        ulong  magic = PositionGetInteger(POSITION_MAGIC);
        double volume = PositionGetDouble(POSITION_VOLUME);
        double sl = PositionGetDouble(POSITION_SL);
        double tp = PositionGetDouble(POSITION_TP);
        double open_price = PositionGetDouble(POSITION_PRICE_OPEN);
        ENUM_POSITION_TYPE type = (ENUM_POSITION_TYPE)PositionGetInteger(POSITION_TYPE);
        PrintFormat("#%I64u %s  %s  %.2f  %s  sl: %s  tp: %s  [%I64d]",
                    position_ticket,
                    position_symbol,
                    EnumToString(type),
                    volume,
                    DoubleToString(open_price, digits),
                    DoubleToString(sl, digits),
                    DoubleToString(tp, digits),
                    magic);
        if(sl == 0 && tp == 0) {
            double bid = SymbolInfoDouble(position_symbol, SYMBOL_BID);
            double ask = SymbolInfoDouble(position_symbol, SYMBOL_ASK);
            int    stop_level = (int)SymbolInfoInteger(position_symbol, SYMBOL_TRADE_STOPS_LEVEL);
            if(type == POSITION_TYPE_BUY) {
                tp = NormalizeDouble(open_price + spacing_buy + current_spread, digits); }
            ZeroMemory(request);
            ZeroMemory(result);
            request.action  = TRADE_ACTION_SLTP;
            request.position = position_ticket;
            request.symbol = position_symbol;
            request.sl      = sl;
            request.tp      = tp;
            PrintFormat("Modify #%I64d %s %s", position_ticket, position_symbol, EnumToString(type));
            if(!OrderSend(request, result))
                PrintFormat("OrderSend error %d", GetLastError());
            PrintFormat("retcode=%u  deal=%I64u  order=%I64u", result.retcode, result.deal, result.order); } } }
//+------------------------------------------------------------------+
