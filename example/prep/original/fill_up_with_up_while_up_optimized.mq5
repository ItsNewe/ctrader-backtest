//+------------------------------------------------------------------+
//| fill_up_with_up_while_up_optimized.mq5                           |
//| Runtime optimized version                                        |
//+------------------------------------------------------------------+

#include <Trade\AccountInfo.mqh>
#include <Trade\PositionInfo.mqh>
#include <Trade\Trade.mqh>

CAccountInfo m_account;
CPositionInfo m_position;
CTrade m_trade;

//+------------------------------------------------------------------+
//| Input parameters                                                 |
//+------------------------------------------------------------------+
input double survive_down = 1;
input double size = 1;
input double spacing = 1;

// Cached constants (set once in OnInit)
double g_min_volume, g_max_volume, g_point, g_contract_size;
double g_initial_margin_rate, g_maintenance_margin_rate;
double g_initial_margin;  // SYMBOL_MARGIN_INITIAL override
double g_margin_stop_out_level;
long g_leverage;
int g_calc_mode, g_volume_digits;
ENUM_ORDER_TYPE_FILLING g_filling_mode;

// State variables - fill_up strategy (positions WITH comments)
double g_lowest_buy, g_highest_buy;
double g_closest_above, g_closest_below;
double g_volume_of_open_trades_fillup;
double g_trade_size_buy, g_spacing_buy;

// State variables - up_while_up strategy (positions WITHOUT comments)
double g_volume_of_open_trades_upup;
double g_checked_last_open_price;

// Tracking variables
double g_max_balance = 0;
double g_max_number_of_open = 0;
double g_max_used_funds = 0;
double g_max_trade_size = 0;

// Reusable request/result for fill_up
MqlTradeRequest g_req_fillup;
MqlTradeResult g_res_fillup;

// Reusable request/result for up_while_up
MqlTradeRequest g_req_upup;
MqlTradeResult g_res_upup;

// Comment separator
ushort g_separator;

//+------------------------------------------------------------------+
int OnInit() {
    // Cache all static symbol/account info
    g_point = SymbolInfoDouble(_Symbol, SYMBOL_POINT);
    g_min_volume = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN);
    g_max_volume = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MAX);
    g_contract_size = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);
    g_leverage = AccountInfoInteger(ACCOUNT_LEVERAGE);
    g_calc_mode = (int)SymbolInfoInteger(_Symbol, SYMBOL_TRADE_CALC_MODE);
    g_margin_stop_out_level = AccountInfoDouble(ACCOUNT_MARGIN_SO_SO);
    SymbolInfoMarginRate(_Symbol, ORDER_TYPE_BUY, g_initial_margin_rate, g_maintenance_margin_rate);
    g_initial_margin = SymbolInfoDouble(_Symbol, SYMBOL_MARGIN_INITIAL);

    // Volume digits from min_volume
    g_volume_digits = (g_min_volume == 0.01) ? 2 : (g_min_volume == 0.1) ? 1 : 0;

    // Cache filling mode
    long filling = SymbolInfoInteger(_Symbol, SYMBOL_FILLING_MODE);
    g_filling_mode = (filling == SYMBOL_FILLING_FOK) ? ORDER_FILLING_FOK :
                     (filling == SYMBOL_FILLING_IOC) ? ORDER_FILLING_IOC :
                     (filling == 4) ? ORDER_FILLING_BOC : ORDER_FILLING_FOK;

    // Cache separator character
    g_separator = StringGetCharacter(";", 0);

    // Pre-fill static request fields for fill_up strategy
    ZeroMemory(g_req_fillup);
    g_req_fillup.action = TRADE_ACTION_DEAL;
    g_req_fillup.symbol = _Symbol;
    g_req_fillup.type = ORDER_TYPE_BUY;
    g_req_fillup.deviation = 1;
    g_req_fillup.type_filling = g_filling_mode;

    // Pre-fill static request fields for up_while_up strategy
    ZeroMemory(g_req_upup);
    g_req_upup.action = TRADE_ACTION_DEAL;
    g_req_upup.symbol = _Symbol;
    g_req_upup.type = ORDER_TYPE_BUY;
    g_req_upup.deviation = 1;
    g_req_upup.type_filling = g_filling_mode;

    m_trade.LogLevel(LOG_LEVEL_NO);
    m_trade.SetAsyncMode(true);

    // Get initial tick values for position scan
    double current_ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);

    // Initialize state
    g_lowest_buy = DBL_MAX;
    g_highest_buy = DBL_MIN;
    g_closest_above = DBL_MAX;
    g_closest_below = DBL_MIN;
    g_volume_of_open_trades_fillup = 0;

    g_checked_last_open_price = DBL_MIN;
    g_volume_of_open_trades_upup = 0;

    // Scan existing positions (matching original behavior: no symbol filter, ALL BUY positions)
    string result[];
    for(int i = PositionsTotal() - 1; i >= 0; i--) {
        ulong ticket = PositionGetTicket(i);
        if(ticket == 0) continue;
        if(!PositionSelectByTicket(ticket)) continue;
        // Note: Original has symbol check commented out, so we include all symbols
        if(!m_position.SelectByTicket(ticket)) continue;

        if(PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY) {
            double lots = PositionGetDouble(POSITION_VOLUME);
            double open_price = PositionGetDouble(POSITION_PRICE_OPEN);
            string cmnt = PositionGetString(POSITION_COMMENT);
            int k = StringSplit(cmnt, g_separator, result);

            if(k > 0) {
                // Debug print (matching original)
                PrintFormat("result[%d]=\"%s\"", 0, result[0]);
                PrintFormat("result[%d]=\"%s\"", 1, result[1]);
            }

            // ALL BUY positions update these (outside k>0 check, matching original)
            g_volume_of_open_trades_fillup += lots;
            g_lowest_buy = MathMin(g_lowest_buy, open_price);
            g_highest_buy = MathMax(g_highest_buy, open_price);
            if(open_price >= current_ask) {
                g_closest_above = MathMin(g_closest_above, open_price - current_ask);
            }
            if(open_price <= current_ask) {
                g_closest_below = MathMin(g_closest_below, current_ask - open_price);
            }
        }
    }

    // Always use input parameters (matching original lines 276-277)
    g_spacing_buy = spacing;
    g_trade_size_buy = size * g_min_volume;

    // Scan for up_while_up positions (separate from above, matching original OnTick first_run)
    g_checked_last_open_price = DBL_MIN;
    g_volume_of_open_trades_upup = 0;
    for(int i = PositionsTotal() - 1; i >= 0; i--) {
        ulong ticket = PositionGetTicket(i);
        if(ticket == 0) continue;
        if(!PositionSelectByTicket(ticket)) continue;
        if(PositionGetString(POSITION_SYMBOL) != _Symbol) continue;  // This one HAS symbol check
        if(PositionGetInteger(POSITION_TYPE) != POSITION_TYPE_BUY) continue;

        string cmnt = PositionGetString(POSITION_COMMENT);
        string res[];
        int k = StringSplit(cmnt, g_separator, res);
        if(k == 0) {
            if(m_position.SelectByTicket(ticket)) {
                g_volume_of_open_trades_upup += PositionGetDouble(POSITION_VOLUME);
                double open_price = PositionGetDouble(POSITION_PRICE_OPEN);
                g_checked_last_open_price = MathMax(g_checked_last_open_price, open_price);
            }
        }
    }

    Print("init: checked_last_open_price ", g_checked_last_open_price);

    return INIT_SUCCEEDED;
}

//+------------------------------------------------------------------+
void Iterate() {
    g_lowest_buy = DBL_MAX;
    g_highest_buy = DBL_MIN;
    g_closest_above = DBL_MAX;
    g_closest_below = DBL_MIN;
    g_volume_of_open_trades_fillup = 0;

    double current_ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    string result[];

    for(int i = PositionsTotal() - 1; i >= 0; i--) {
        ulong ticket = PositionGetTicket(i);
        if(ticket == 0) continue;
        if(!PositionSelectByTicket(ticket)) continue;
        // Note: Original iterate() has no symbol filter (commented out)
        if(!m_position.SelectByTicket(ticket)) continue;

        if(PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY) {
            string cmnt = PositionGetString(POSITION_COMMENT);
            int k = StringSplit(cmnt, g_separator, result);
            if(k > 0) {
                // Fill_up position (has comment)
                double lots = PositionGetDouble(POSITION_VOLUME);
                double open_price = PositionGetDouble(POSITION_PRICE_OPEN);
                g_volume_of_open_trades_fillup += lots;
                g_lowest_buy = MathMin(g_lowest_buy, open_price);
                g_highest_buy = MathMax(g_highest_buy, open_price);
                if(open_price >= current_ask) {
                    g_closest_above = MathMin(g_closest_above, open_price - current_ask);
                }
                if(open_price <= current_ask) {
                    g_closest_below = MathMin(g_closest_below, current_ask - open_price);
                }
            }
        }
    }
}

//+------------------------------------------------------------------+
bool OpenFillup(double local_unit) {
    if(local_unit < g_min_volume) return false;

    double final_unit = MathMin(local_unit, g_max_volume);
    final_unit = NormalizeDouble(final_unit, g_volume_digits);

    double current_ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    double current_spread = g_point * SymbolInfoInteger(_Symbol, SYMBOL_SPREAD);

    g_req_fillup.volume = final_unit;
    g_req_fillup.price = current_ask;
    g_req_fillup.tp = current_ask + current_spread + g_spacing_buy;
    StringConcatenate(g_req_fillup.comment, DoubleToString(g_spacing_buy, 2), ";", DoubleToString(g_trade_size_buy, 2));

    ZeroMemory(g_res_fillup);
    if(!OrderSend(g_req_fillup, g_res_fillup)) {
        ResetLastError();
        return false;
    }
    return true;
}

//+------------------------------------------------------------------+
double OpenUpUp(double local_unit, double current_ask) {
    if(local_unit < g_min_volume) return 0;

    double lots = MathMin(local_unit, g_max_volume);
    lots = NormalizeDouble(lots, g_volume_digits);

    g_req_upup.volume = lots;
    g_req_upup.price = current_ask;

    ZeroMemory(g_res_upup);
    if(!OrderSend(g_req_upup, g_res_upup)) {
        ResetLastError();
        return 0;
    }
    return lots;
}

//+------------------------------------------------------------------+
void SizingBuy() {
    g_trade_size_buy = 0;

    double current_margin_level = AccountInfoDouble(ACCOUNT_MARGIN_LEVEL);
    double used_margin = AccountInfoDouble(ACCOUNT_MARGIN);
    double equity = AccountInfoDouble(ACCOUNT_EQUITY);
    double current_ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    double current_spread = g_point * SymbolInfoInteger(_Symbol, SYMBOL_SPREAD);

    double equity_at_target = equity * g_margin_stop_out_level / current_margin_level;
    double equity_difference = equity - equity_at_target;
    double price_difference = equity_difference / (g_volume_of_open_trades_fillup * g_contract_size);

    double end_price = current_ask * ((100 - survive_down) / 100);
    double distance = current_ask - end_price;
    double number_of_trades = MathFloor(distance / g_spacing_buy);

    if((PositionsTotal() == 0) || ((current_ask - price_difference) < end_price)) {
        equity_at_target = equity - g_volume_of_open_trades_fillup * MathAbs(distance) * g_contract_size;
        double margin_level = equity_at_target / used_margin * 100;
        double trade_size = g_min_volume;
        double local_used_margin = 0;
        double starting_price = current_ask;

        if(margin_level > g_margin_stop_out_level) {
            double d_equity = g_contract_size * trade_size * (number_of_trades * (number_of_trades + 1) / 2);
            double d_spread = number_of_trades * trade_size * current_spread * g_contract_size;
            d_equity += d_spread;

            switch(g_calc_mode) {
            case SYMBOL_CALC_MODE_CFDLEVERAGE:
                local_used_margin += (trade_size * g_contract_size * starting_price) / g_leverage * g_initial_margin_rate;
                local_used_margin += (trade_size * g_contract_size * end_price) / g_leverage * g_initial_margin_rate;
                break;
            case SYMBOL_CALC_MODE_FOREX:
                local_used_margin += trade_size * (g_initial_margin > 0 ? g_initial_margin : g_contract_size / g_leverage) * g_initial_margin_rate;
                local_used_margin += trade_size * (g_initial_margin > 0 ? g_initial_margin : g_contract_size / g_leverage) * g_initial_margin_rate;
                break;
            case SYMBOL_CALC_MODE_FOREX_NO_LEVERAGE:
                local_used_margin += trade_size * g_contract_size * g_initial_margin_rate;
                local_used_margin += trade_size * g_contract_size * g_initial_margin_rate;
                break;
            case SYMBOL_CALC_MODE_CFD:
                local_used_margin += trade_size * g_contract_size * starting_price * g_initial_margin_rate;
                local_used_margin += trade_size * g_contract_size * end_price * g_initial_margin_rate;
                break;
            }
            local_used_margin = local_used_margin / 2;
            local_used_margin = number_of_trades * local_used_margin;

            double multiplier = 0;
            double equity_backup = equity_at_target;
            double used_margin_backup = used_margin;
            double max = g_max_volume / g_min_volume;

            equity_at_target -= max * d_equity;
            used_margin += max * local_used_margin;
            if(g_margin_stop_out_level < equity_at_target / used_margin * 100) {
                multiplier = max;
            } else {
                used_margin = used_margin_backup;
                equity_at_target = equity_backup;
                for(double increment = max; increment >= 1; increment = increment / 10) {
                    while(g_margin_stop_out_level < equity_at_target / used_margin * 100) {
                        equity_backup = equity_at_target;
                        used_margin_backup = used_margin;
                        multiplier += increment;
                        equity_at_target -= increment * d_equity;
                        used_margin += increment * local_used_margin;
                    }
                    multiplier -= increment;
                    used_margin = used_margin_backup;
                    equity_at_target = equity_backup;
                }
            }

            Print("@!@ multiplier: ", multiplier);
            multiplier = MathMax(1, multiplier);
            g_trade_size_buy = multiplier * g_min_volume;
            g_max_trade_size = MathMax(g_max_trade_size, g_trade_size_buy);
        }
    }
}

//+------------------------------------------------------------------+
void OpenNew() {
    double current_ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);

    if(PositionsTotal() == 0) {
        SizingBuy();
        if(OpenFillup(g_trade_size_buy)) {
            g_highest_buy = current_ask;
            g_lowest_buy = current_ask;
        }
    } else {
        if(g_lowest_buy >= current_ask + g_spacing_buy) {
            SizingBuy();
            if(OpenFillup(g_trade_size_buy)) {
                g_lowest_buy = current_ask;
            }
        } else if(g_highest_buy <= current_ask - g_spacing_buy) {
            SizingBuy();
            if(OpenFillup(g_trade_size_buy)) {
                g_highest_buy = current_ask;
            }
        } else if((g_closest_above >= g_spacing_buy) && (g_closest_below >= g_spacing_buy)) {
            SizingBuy();
            OpenFillup(g_trade_size_buy);
        }
    }
}

//+------------------------------------------------------------------+
void NewTickValues() {
    double current_balance = m_account.Balance();
    double current_equity = m_account.Equity();

    g_max_balance = MathMax(g_max_balance, current_balance);
    g_max_number_of_open = MathMax(g_max_number_of_open, PositionsTotal());
    g_max_used_funds = MathMax(g_max_used_funds, current_balance - current_equity + AccountInfoDouble(ACCOUNT_MARGIN));
}

//+------------------------------------------------------------------+
void OnTick() {
    double current_spread = g_point * SymbolInfoInteger(_Symbol, SYMBOL_SPREAD);
    double current_ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    double equity = AccountInfoDouble(ACCOUNT_EQUITY);
    double current_margin_level = AccountInfoDouble(ACCOUNT_MARGIN_LEVEL);
    double used_margin = AccountInfoDouble(ACCOUNT_MARGIN);

    // Up_while_up strategy logic (positions WITHOUT comments)
    {
        g_checked_last_open_price = current_ask;

        // Scan for up_while_up positions
        g_volume_of_open_trades_upup = 0;
        string result[];
        for(int i = PositionsTotal() - 1; i >= 0; i--) {
            ulong ticket = PositionGetTicket(i);
            if(ticket == 0) continue;
            if(!PositionSelectByTicket(ticket)) continue;
            if(PositionGetString(POSITION_SYMBOL) != _Symbol) continue;
            if(PositionGetInteger(POSITION_TYPE) != POSITION_TYPE_BUY) continue;

            string cmnt = PositionGetString(POSITION_COMMENT);
            int k = StringSplit(cmnt, g_separator, result);
            if(k == 0) {
                // Up_while_up position (no comment)
                if(m_position.SelectByTicket(ticket)) {
                    g_volume_of_open_trades_upup += PositionGetDouble(POSITION_VOLUME);
                }
            }
        }

        double equity_at_target = equity * g_margin_stop_out_level / current_margin_level;
        double equity_difference = equity - equity_at_target;
        double price_difference = equity_difference / (g_volume_of_open_trades_upup * g_contract_size);

        double end_price = current_ask * ((100 - survive_down) / 100);
        double distance = current_ask - end_price;

        if((g_volume_of_open_trades_upup == 0) || ((current_ask - price_difference) < end_price)) {
            equity_at_target = equity - g_volume_of_open_trades_upup * MathAbs(distance) * g_contract_size;
            double trade_size = 0;
            double starting_price = current_ask;

            switch(g_calc_mode) {
            case SYMBOL_CALC_MODE_CFDLEVERAGE:
                trade_size = NormalizeDouble((100 * equity * g_leverage - 100 * g_contract_size * MathAbs(distance) * g_volume_of_open_trades_upup * g_leverage - g_leverage * g_margin_stop_out_level * used_margin) / (g_contract_size * (100 * MathAbs(distance) * g_leverage + 100 * current_spread * g_leverage + starting_price * g_initial_margin_rate * g_margin_stop_out_level)), g_volume_digits);
                break;
            case SYMBOL_CALC_MODE_FOREX:
                trade_size = NormalizeDouble((100 * g_leverage * equity - 100 * g_contract_size * MathAbs(distance) * g_leverage * g_volume_of_open_trades_upup - g_leverage * g_margin_stop_out_level * used_margin) / (g_contract_size * (100 * MathAbs(distance) * g_leverage + 100 * current_spread * g_leverage + (g_initial_margin > 0 ? g_initial_margin * g_leverage / g_contract_size : 1.0) * g_initial_margin_rate * g_margin_stop_out_level)), g_volume_digits);
                break;
            case SYMBOL_CALC_MODE_FOREX_NO_LEVERAGE:
                trade_size = NormalizeDouble((100 * equity - 100 * g_contract_size * MathAbs(distance) * g_volume_of_open_trades_upup - g_margin_stop_out_level * used_margin) / (g_contract_size * (100 * MathAbs(distance) + 100 * current_spread + (g_initial_margin > 0 ? g_initial_margin / g_contract_size : 1.0) * g_initial_margin_rate * g_margin_stop_out_level)), g_volume_digits);
                break;
            case SYMBOL_CALC_MODE_CFD:
                trade_size = NormalizeDouble((100 * equity - 100 * g_contract_size * MathAbs(distance) * g_volume_of_open_trades_upup - g_margin_stop_out_level * used_margin) / (g_contract_size * (100 * MathAbs(distance) + 100 * current_spread + starting_price * g_initial_margin_rate * g_margin_stop_out_level)), g_volume_digits);
                break;
            }

            if(trade_size >= g_min_volume) {
                g_volume_of_open_trades_upup += OpenUpUp(trade_size, current_ask);
                g_checked_last_open_price = current_ask;
            }
        }
    }

    // Fill_up strategy logic (positions WITH comments)
    NewTickValues();
    Iterate();
    OpenNew();
}

//+------------------------------------------------------------------+
void OnDeinit(const int reason) {
    Print("@!@ max balance: ", g_max_balance);
    Print("@!@ max number of open: ", g_max_number_of_open);
    Print("@!@ max used funds: ", g_max_used_funds);
    Print("@!@ max trade size: ", g_max_trade_size);
}

//+------------------------------------------------------------------+
double OnTester() {
    HistorySelect(0, TimeCurrent());
    long total = HistoryDealsTotal();
    long ticket = 0;
    long reason;

    for(long i = total - 1; i >= 0; i--) {
        if((ticket = HistoryDealGetTicket(i)) > 0) {
            reason = HistoryDealGetInteger(ticket, DEAL_REASON);
            if(reason == DEAL_REASON_SO) {
                return 0.0;
            }
        }
    }
    return g_max_balance;
}
//+------------------------------------------------------------------+
