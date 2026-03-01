//+------------------------------------------------------------------+
//| open_upwards_both_up_and_down_optimized.mq5                      |
//| Runtime optimized version - Combined BUY strategies              |
//+------------------------------------------------------------------+

#include <Trade\Trade.mqh>
#include <Trade\PositionInfo.mqh>
#include <Trade\AccountInfo.mqh>

CAccountInfo m_account;
CPositionInfo m_position;
CTrade m_trade;

//+------------------------------------------------------------------+
//| Input parameters                                                 |
//+------------------------------------------------------------------+
input double survive = 5;
input double commission = 0;
input int sizing = 0; // sizing: constant, incremental
input int closing_mode = 0;

// Cached constants (set once in OnInit)
double g_min_volume, g_max_volume, g_point, g_contract_size;
double g_initial_margin_rate, g_maintenance_margin_rate;
double g_initial_margin;  // SYMBOL_MARGIN_INITIAL override
long g_leverage;
int g_calc_mode, g_volume_digits;
ENUM_ORDER_TYPE_FILLING g_filling_mode;
double g_cm;
string g_first, g_second, g_currency;

// State variables for direction-based (up_w_down)
double g_current_spread, g_current_commission, g_current_spread_and_commission;
double g_current_equity, g_current_balance;
double g_current_ask, g_current_bid;
double g_bid_at_turn_up, g_ask_at_turn_up;
double g_bid_at_turn_down, g_ask_at_turn_down;

double g_value_of_buy_trades;
double g_lowest_buy, g_highest_buy;
double g_volume_of_open_trades;
double g_volume_of_open_trades_up;  // For up_w_up positions
double g_trade_size_buy, g_spacing_buy;

int g_direction, g_direction_change;
int g_count_buy;

bool g_close_all_buy_flag, g_close_all_profitable_buy_flag;
bool g_is_there_unprofitable_buy;

// Reusable request/result
MqlTradeRequest g_req;
MqlTradeResult g_res;

// State for up_w_up
double g_up_volume_of_open_trades;
double g_up_checked_last_open_price;
double g_up_local_survive_down;

//+------------------------------------------------------------------+
double AccountCurrencyToInstrumentCost(double cost) {
    if(g_first == g_currency) return cost;
    if(g_second == g_currency) return cost * g_current_bid;

    string base = g_currency + g_first;
    double r = SymbolInfoDouble(base, SYMBOL_BID);
    if(r > 0) return cost / r;

    base = g_first + g_currency;
    r = SymbolInfoDouble(base, SYMBOL_BID);
    if(r > 0) return cost * r;

    return 0.0;
}

//+------------------------------------------------------------------+
double OrderSwap() {
    double D = PositionGetDouble(POSITION_SWAP);
    return AccountCurrencyToInstrumentCost(D);
}

//+------------------------------------------------------------------+
void ClearFlags() {
    g_close_all_buy_flag = false;
    g_close_all_profitable_buy_flag = false;
}

//+------------------------------------------------------------------+
// Iterate for up_w_down (positions with comments)
void Iterate() {
    g_value_of_buy_trades = 0;
    g_count_buy = 0;
    g_volume_of_open_trades = 0;
    g_lowest_buy = DBL_MAX;
    g_highest_buy = DBL_MIN;
    g_is_there_unprofitable_buy = false;

    if(sizing == 1) g_trade_size_buy = 0;

    string cmnt;
    ushort u_sep = StringGetCharacter(";", 0);
    string result[];

    for(int i = PositionsTotal() - 1; i >= 0; i--) {
        ulong ticket = PositionGetTicket(i);
        if(ticket == 0) continue;
        if(!m_position.SelectByTicket(ticket)) continue;

        double lots = PositionGetDouble(POSITION_VOLUME);
        double swap = OrderSwap();
        double profit = m_position.Profit() + swap;
        double open_price = PositionGetDouble(POSITION_PRICE_OPEN);
        cmnt = PositionGetString(POSITION_COMMENT);

        if(PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY) {
            int k = StringSplit(cmnt, u_sep, result);
            if(k > 0) {  // Only positions with comments (up_w_down)
                if(g_close_all_buy_flag) {
                    Close(ticket);
                    continue;
                }
                if((profit > 0) && g_close_all_profitable_buy_flag) {
                    Close(ticket);
                    continue;
                }
                if(profit < 0) g_is_there_unprofitable_buy = true;

                g_value_of_buy_trades += profit;
                g_lowest_buy = MathMin(g_lowest_buy, open_price);
                g_highest_buy = MathMax(g_highest_buy, open_price);
                g_count_buy++;
                g_volume_of_open_trades += lots;

                if(sizing == 1) g_trade_size_buy = MathMax(g_trade_size_buy, lots);
            }
        }
    }

    if(sizing == 1) g_trade_size_buy += g_min_volume;
    ClearFlags();
}

//+------------------------------------------------------------------+
// Open for up_w_down (with comment)
bool OpenUpWDown(double local_unit, int local_direction) {
    if(local_unit < g_min_volume) return false;

    double final_unit = MathMin(local_unit, g_max_volume);
    final_unit = NormalizeDouble(final_unit, g_volume_digits);

    g_req.volume = final_unit;
    g_req.type = ORDER_TYPE_BUY;
    g_req.price = g_current_ask;
    StringConcatenate(g_req.comment, DoubleToString(g_spacing_buy, 2), ";", DoubleToString(g_trade_size_buy, 2));

    ZeroMemory(g_res);
    if(!OrderSend(g_req, g_res)) {
        ResetLastError();
        return false;
    }
    return true;
}

//+------------------------------------------------------------------+
// Open for up_w_up (no comment)
double OpenUpWUp(double local_unit, double current_ask) {
    if(local_unit < g_min_volume) return 0;

    double lots = MathMin(local_unit, g_max_volume);
    lots = NormalizeDouble(lots, g_volume_digits);

    g_req.volume = lots;
    g_req.type = ORDER_TYPE_BUY;
    g_req.price = current_ask;
    g_req.comment = "";

    ZeroMemory(g_res);
    if(!OrderSend(g_req, g_res)) {
        ResetLastError();
        return 0;
    }
    return lots;
}

//+------------------------------------------------------------------+
void Close(ulong ticket) {
    if(m_position.SelectByTicket(ticket)) {
        if(!m_trade.PositionClose(ticket)) {
            ResetLastError();
        }
    }
}

//+------------------------------------------------------------------+
void SizingBuy() {
    g_trade_size_buy = 0;
    g_spacing_buy = 0;

    double current_margin_level = AccountInfoDouble(ACCOUNT_MARGIN_LEVEL);
    double used_margin = AccountInfoDouble(ACCOUNT_MARGIN);
    double margin_stop_out_level = AccountInfoDouble(ACCOUNT_MARGIN_SO_SO);
    double equity = AccountInfoDouble(ACCOUNT_EQUITY);

    double equity_at_target = equity * margin_stop_out_level / current_margin_level;
    double equity_difference = equity - equity_at_target;
    double price_difference = equity_difference / ((g_volume_of_open_trades + g_volume_of_open_trades_up) * g_contract_size);

    double end_price = g_current_ask * ((100 - survive) / 100);
    double distance = end_price - g_current_ask;

    if((g_count_buy == 0) || ((g_current_ask - price_difference) < end_price)) {
        equity_at_target = equity - (g_volume_of_open_trades + g_volume_of_open_trades_up) * MathAbs(distance) * g_contract_size;
        double margin_level = equity_at_target / used_margin * 100;
        double trade_size = g_min_volume;
        double local_used_margin = 0;
        double starting_price = g_current_ask;
        double len = starting_price - end_price;

        if(margin_level > margin_stop_out_level) {
            double d_equity = g_contract_size * trade_size * (starting_price - end_price) + trade_size * g_current_spread_and_commission * g_contract_size;

            switch(g_calc_mode) {
            case SYMBOL_CALC_MODE_CFDLEVERAGE:
                local_used_margin = ((trade_size * g_contract_size * starting_price) / g_leverage * g_initial_margin_rate +
                                     (trade_size * g_contract_size * end_price) / g_leverage * g_initial_margin_rate) / 2;
                break;
            case SYMBOL_CALC_MODE_FOREX:
                local_used_margin = (trade_size * (g_initial_margin > 0 ? g_initial_margin : g_contract_size / g_leverage) * g_initial_margin_rate * 2) / 2;
                break;
            case SYMBOL_CALC_MODE_FOREX_NO_LEVERAGE:
                local_used_margin = (trade_size * g_contract_size * g_initial_margin_rate * 2) / 2;
                break;
            case SYMBOL_CALC_MODE_CFD:
                local_used_margin = ((trade_size * g_contract_size * starting_price * g_initial_margin_rate) +
                                     (trade_size * g_contract_size * end_price * g_initial_margin_rate)) / 2;
                break;
            }

            double number_of_trades;
            if(used_margin == 0)
                number_of_trades = MathFloor(equity_at_target / (margin_stop_out_level / 100 * local_used_margin + d_equity));
            else
                number_of_trades = MathFloor((equity_at_target - margin_stop_out_level / 100 * used_margin) / (margin_stop_out_level / 100 * local_used_margin + d_equity));

            double proportion = number_of_trades / len;

            switch(sizing) {
            case 0:
                if(proportion >= 1) {
                    g_spacing_buy = 1;
                    g_trade_size_buy = MathFloor(proportion) * g_min_volume;
                } else {
                    g_trade_size_buy = g_min_volume;
                    g_spacing_buy = MathRound((len / number_of_trades) * 100) / 100;
                }
                break;
            case 1:
                double temp = (-1 + MathSqrt(1 + 8 * number_of_trades)) / 2;
                temp = MathFloor(temp);
                g_spacing_buy = len / (temp - 1);
                g_trade_size_buy = g_min_volume;
                break;
            }
        }
    }
}

//+------------------------------------------------------------------+
void OpenNewUpWDown() {
    if(g_count_buy == 0) SizingBuy();

    if(g_count_buy == 0) {
        if(OpenUpWDown(g_trade_size_buy, 1)) {
            g_highest_buy = g_current_ask;
            g_lowest_buy = g_current_ask;
            g_count_buy++;
            if(sizing == 1) g_trade_size_buy += g_min_volume;
        }
    } else {
        double temp, temp1, temp2, temp3 = 0, temp4, temp5, temp6;

        switch(sizing) {
        case 0:
            if(g_lowest_buy > g_current_ask) {
                temp = (g_highest_buy - g_current_ask) / g_spacing_buy;
                temp1 = MathFloor(temp);
                temp2 = temp - temp1;
                temp3 = temp1;
                temp4 = g_spacing_buy / (g_trade_size_buy / g_min_volume);
                temp5 = MathFloor(temp2 / temp4);
                temp6 = temp3 * g_trade_size_buy + temp5 * g_min_volume - g_volume_of_open_trades;
                if(temp6 > 0) {
                    if(OpenUpWDown(temp6, 1)) {
                        g_lowest_buy = g_current_ask;
                        g_count_buy++;
                    }
                }
            }
            break;
        case 1:
            if(g_lowest_buy > g_current_ask) {
                temp = (g_highest_buy - g_current_ask) / g_spacing_buy;
                temp1 = MathFloor(temp);
                temp2 = temp - temp1;
                if(temp >= 1) {
                    for(int i = 1; i <= temp1 + 1; i++) temp3 += i;
                }
                temp4 = g_spacing_buy / (temp1 + 1);
                temp5 = MathFloor(temp2 / temp4);
                temp6 = (temp3 + temp5) * g_min_volume - g_volume_of_open_trades;
                if(temp6 > 0) {
                    if(OpenUpWDown(temp6, 1)) {
                        g_lowest_buy = g_current_ask;
                        g_count_buy++;
                    }
                }
            }
            break;
        }
    }
}

//+------------------------------------------------------------------+
void OpenNewUpWUp() {
    string cmnt;
    ushort u_sep = StringGetCharacter(";", 0);
    string result[];

    double current_spread = g_point * SymbolInfoInteger(_Symbol, SYMBOL_SPREAD);
    double current_spread_and_commission = current_spread + (g_point * g_cm);
    double current_ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);

    double equity = AccountInfoDouble(ACCOUNT_EQUITY);
    double current_margin_level = AccountInfoDouble(ACCOUNT_MARGIN_LEVEL);
    double used_margin = AccountInfoDouble(ACCOUNT_MARGIN);
    double margin_stop_out_level = AccountInfoDouble(ACCOUNT_MARGIN_SO_SO);

    // Update checked price
    g_up_checked_last_open_price = current_ask;

    // Scan positions without comment (up_w_up)
    g_up_volume_of_open_trades = 0;
    for(int i = PositionsTotal() - 1; i >= 0; i--) {
        ulong ticket = PositionGetTicket(i);
        if(ticket == 0) continue;
        if(!PositionSelectByTicket(ticket)) continue;
        cmnt = PositionGetString(POSITION_COMMENT);
        if((PositionGetString(POSITION_SYMBOL) == _Symbol) && (PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY)) {
            int k = StringSplit(cmnt, u_sep, result);
            if(k == 0) {  // No comment = up_w_up position
                g_up_volume_of_open_trades += PositionGetDouble(POSITION_VOLUME);
            }
        }
    }

    double equity_at_target = equity * margin_stop_out_level / current_margin_level;
    double equity_difference = equity - equity_at_target;
    double price_difference = equity_difference / (g_up_volume_of_open_trades * g_contract_size);

    double end_price = current_ask * ((100 - g_up_local_survive_down) / 100);
    double distance = current_ask - end_price;

    if((g_up_volume_of_open_trades == 0) || ((current_ask - price_difference) < end_price)) {
        equity_at_target = equity - g_up_volume_of_open_trades * MathAbs(distance) * g_contract_size;
        double trade_size = 0;
        double starting_price = current_ask;

        switch(g_calc_mode) {
        case SYMBOL_CALC_MODE_CFDLEVERAGE:
            trade_size = NormalizeDouble((100 * equity * g_leverage - 100 * g_contract_size * MathAbs(distance) * g_up_volume_of_open_trades * g_leverage - g_leverage * margin_stop_out_level * used_margin) / (g_contract_size * (100 * MathAbs(distance) * g_leverage + 100 * current_spread_and_commission * g_leverage + starting_price * g_initial_margin_rate * margin_stop_out_level)), g_volume_digits);
            break;
        case SYMBOL_CALC_MODE_FOREX:
            trade_size = NormalizeDouble((100 * g_leverage * equity - 100 * g_contract_size * MathAbs(distance) * g_leverage * g_up_volume_of_open_trades - g_leverage * margin_stop_out_level * used_margin) / (g_contract_size * (100 * MathAbs(distance) * g_leverage + 100 * current_spread_and_commission * g_leverage + (g_initial_margin > 0 ? g_initial_margin * g_leverage / g_contract_size : 1.0) * g_initial_margin_rate * margin_stop_out_level)), g_volume_digits);
            break;
        case SYMBOL_CALC_MODE_FOREX_NO_LEVERAGE:
            trade_size = NormalizeDouble((100 * equity - 100 * g_contract_size * MathAbs(distance) * g_up_volume_of_open_trades - margin_stop_out_level * used_margin) / (g_contract_size * (100 * MathAbs(distance) + 100 * current_spread_and_commission + (g_initial_margin > 0 ? g_initial_margin / g_contract_size : 1.0) * g_initial_margin_rate * margin_stop_out_level)), g_volume_digits);
            break;
        case SYMBOL_CALC_MODE_CFD:
            trade_size = NormalizeDouble((100 * equity - 100 * g_contract_size * MathAbs(distance) * g_up_volume_of_open_trades - margin_stop_out_level * used_margin) / (g_contract_size * (100 * MathAbs(distance) + 100 * current_spread_and_commission + starting_price * g_initial_margin_rate * margin_stop_out_level)), g_volume_digits);
            break;
        }

        if(trade_size >= g_min_volume) {
            g_up_volume_of_open_trades += OpenUpWUp(trade_size, current_ask);
            g_up_checked_last_open_price = current_ask;
        }
    }
    g_volume_of_open_trades_up = g_up_volume_of_open_trades;
}

//+------------------------------------------------------------------+
void ClearTurnMarks() {
    g_bid_at_turn_up = DBL_MAX;
    g_ask_at_turn_up = DBL_MAX;
    g_ask_at_turn_down = DBL_MIN;
    g_bid_at_turn_down = DBL_MIN;
}

//+------------------------------------------------------------------+
void NewTickValues() {
    g_current_spread = g_point * SymbolInfoInteger(_Symbol, SYMBOL_SPREAD);
    g_current_commission = g_point * g_cm;
    g_current_spread_and_commission = g_current_spread + g_current_commission;
    g_current_equity = m_account.Equity();
    g_current_balance = m_account.Balance();
    g_current_ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    g_current_bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
}

//+------------------------------------------------------------------+
void DirectionCheck() {
    g_direction_change = 0;

    g_bid_at_turn_down = MathMax(g_current_bid, g_bid_at_turn_down);
    g_bid_at_turn_up = MathMin(g_current_bid, g_bid_at_turn_up);
    g_ask_at_turn_down = MathMax(g_current_ask, g_ask_at_turn_down);
    g_ask_at_turn_up = MathMin(g_current_ask, g_ask_at_turn_up);

    Iterate();

    if((g_direction != -1) && (g_ask_at_turn_down >= g_current_ask + g_current_spread_and_commission) && (g_bid_at_turn_down >= g_current_bid + g_current_spread_and_commission)) {
        g_direction = -1;
        g_direction_change = -1;

        switch(closing_mode) {
        case 0:
            if(g_value_of_buy_trades > 0) {
                g_close_all_profitable_buy_flag = true;
                Iterate();
            }
            break;
        case 1:
            if(!g_is_there_unprofitable_buy) {
                g_close_all_buy_flag = true;
                Iterate();
            }
            break;
        }
        ClearTurnMarks();
    }

    if((g_direction != 1) && (g_bid_at_turn_up <= g_current_bid - g_current_spread_and_commission) && (g_ask_at_turn_up <= g_current_ask - g_current_spread_and_commission)) {
        g_direction = 1;
        g_direction_change = 1;
        ClearTurnMarks();
    }
}

//+------------------------------------------------------------------+
void OnTick() {
    NewTickValues();
    DirectionCheck();
    OpenNewUpWUp();
    OpenNewUpWDown();
}

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

    ZeroMemory(g_req);
    g_req.action = TRADE_ACTION_DEAL;
    g_req.symbol = _Symbol;
    g_req.deviation = 1;
    g_req.type_filling = g_filling_mode;

    m_trade.LogLevel(LOG_LEVEL_NO);
    m_trade.SetAsyncMode(true);

    g_cm = commission * 100;
    g_first = SymbolInfoString(_Symbol, SYMBOL_CURRENCY_BASE);
    g_second = SymbolInfoString(_Symbol, SYMBOL_CURRENCY_PROFIT);
    g_currency = AccountInfoString(ACCOUNT_CURRENCY);

    // Initialize state for up_w_down
    g_value_of_buy_trades = 0;
    g_count_buy = 0;
    g_volume_of_open_trades = 0;
    g_lowest_buy = DBL_MAX;
    g_highest_buy = DBL_MIN;
    if(sizing == 1) g_trade_size_buy = 0;

    // Initialize state for up_w_up
    g_up_checked_last_open_price = DBL_MIN;
    g_up_volume_of_open_trades = 0;
    g_up_local_survive_down = 2 * survive;

    string cmnt;
    ushort u_sep = StringGetCharacter(";", 0);
    string result[];

    // Scan existing positions
    for(int i = PositionsTotal() - 1; i >= 0; i--) {
        ulong ticket = PositionGetTicket(i);
        if(ticket == 0) continue;
        if(!m_position.SelectByTicket(ticket)) continue;

        double lots = PositionGetDouble(POSITION_VOLUME);
        double swap = OrderSwap();
        double profit = m_position.Profit() + swap;
        double open_price = PositionGetDouble(POSITION_PRICE_OPEN);
        cmnt = PositionGetString(POSITION_COMMENT);

        if(PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY) {
            int k = StringSplit(cmnt, u_sep, result);
            if(k > 0) {
                // up_w_down position (has comment)
                g_value_of_buy_trades += profit;
                g_lowest_buy = MathMin(g_lowest_buy, open_price);
                g_highest_buy = MathMax(g_highest_buy, open_price);
                g_count_buy++;
                g_volume_of_open_trades += lots;
                if(sizing == 1) g_trade_size_buy = MathMax(g_trade_size_buy, lots);
            } else {
                // up_w_up position (no comment)
                g_up_volume_of_open_trades += lots;
                g_up_checked_last_open_price = MathMax(g_up_checked_last_open_price, open_price);
            }
        }
    }

    if(sizing == 1) g_trade_size_buy += g_min_volume;
    ClearFlags();

    Print("init: checked_last_open_price ", g_up_checked_last_open_price);

    return INIT_SUCCEEDED;
}

//+------------------------------------------------------------------+
void OnDeinit(const int reason) {}
//+------------------------------------------------------------------+
