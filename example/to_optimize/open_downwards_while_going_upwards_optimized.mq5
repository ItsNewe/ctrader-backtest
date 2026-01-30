//+------------------------------------------------------------------+
//| open_downwards_while_going_upwards_optimized.mq5                 |
//| Runtime optimized version                                        |
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
long g_leverage;
int g_calc_mode, g_volume_digits;
ENUM_ORDER_TYPE_FILLING g_filling_mode;
double g_cm;
string g_first, g_second, g_currency;

// State variables
double g_current_spread, g_current_commission, g_current_spread_and_commission;
double g_current_equity, g_current_balance;
double g_current_ask, g_current_bid;
double g_bid_at_turn_up, g_ask_at_turn_up;
double g_bid_at_turn_down, g_ask_at_turn_down;

double g_value_of_sell_trades;
double g_lowest_sell, g_highest_sell;
double g_volume_of_open_trades;
double g_trade_size_sell, g_spacing_sell;

int g_direction, g_direction_change;
int g_count_sell;

bool g_close_all_sell_flag, g_close_all_profitable_sell_flag;
bool g_is_there_unprofitable_sell;

// Reusable request/result
MqlTradeRequest g_req;
MqlTradeResult g_res;

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
    g_close_all_sell_flag = false;
    g_close_all_profitable_sell_flag = false;
}

//+------------------------------------------------------------------+
void Iterate() {
    g_value_of_sell_trades = 0;
    g_count_sell = 0;
    g_volume_of_open_trades = 0;
    g_lowest_sell = DBL_MAX;
    g_highest_sell = DBL_MIN;
    g_is_there_unprofitable_sell = false;

    if(sizing == 1) g_trade_size_sell = 0;

    for(int i = PositionsTotal() - 1; i >= 0; i--) {
        ulong ticket = PositionGetTicket(i);
        if(ticket == 0) continue;
        if(!m_position.SelectByTicket(ticket)) continue;

        double lots = PositionGetDouble(POSITION_VOLUME);
        double swap = OrderSwap();
        double profit = m_position.Profit() + swap;
        double open_price = PositionGetDouble(POSITION_PRICE_OPEN);

        if(PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_SELL) {
            if(g_close_all_sell_flag) {
                Close(ticket);
                continue;
            }
            if((profit > 0) && g_close_all_profitable_sell_flag) {
                Close(ticket);
                continue;
            }
            if(profit < 0) g_is_there_unprofitable_sell = true;

            g_value_of_sell_trades += profit;
            g_lowest_sell = MathMin(g_lowest_sell, open_price);
            g_highest_sell = MathMax(g_highest_sell, open_price);
            g_count_sell++;
            g_volume_of_open_trades += lots;

            if(sizing == 1) g_trade_size_sell = MathMax(g_trade_size_sell, lots);
        }
    }

    if(sizing == 1) g_trade_size_sell += g_min_volume;
    ClearFlags();
}

//+------------------------------------------------------------------+
bool OpenSell(double local_unit, int local_direction) {
    if(local_unit < g_min_volume) return false;

    double final_unit = MathMin(local_unit, g_max_volume);
    final_unit = NormalizeDouble(final_unit, g_volume_digits);

    g_req.volume = final_unit;
    g_req.type = ORDER_TYPE_SELL;
    g_req.price = g_current_bid;

    ZeroMemory(g_res);
    if(!OrderSend(g_req, g_res)) {
        ResetLastError();
        return false;
    }
    return true;
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
void SizingSell() {
    g_trade_size_sell = 0;
    g_spacing_sell = 0;

    double current_margin_level = AccountInfoDouble(ACCOUNT_MARGIN_LEVEL);
    double used_margin = AccountInfoDouble(ACCOUNT_MARGIN);
    double margin_stop_out_level = AccountInfoDouble(ACCOUNT_MARGIN_SO_SO);
    double equity = AccountInfoDouble(ACCOUNT_EQUITY);

    double equity_at_target = equity * margin_stop_out_level / current_margin_level;
    double equity_difference = equity - equity_at_target;
    double price_difference = equity_difference / (g_volume_of_open_trades * g_contract_size);

    double end_price = g_current_bid * ((100 + survive) / 100);
    double distance = end_price - g_current_bid;

    if((g_count_sell == 0) || ((g_current_bid + price_difference) > end_price)) {
        equity_at_target = equity;
        double margin_level = equity_at_target / used_margin * 100;
        double trade_size = g_min_volume;
        double local_used_margin = 0;
        double starting_price = g_current_bid;
        double len = end_price - starting_price;

        if(margin_level > margin_stop_out_level) {
            double d_equity = g_contract_size * trade_size * (end_price - starting_price) + trade_size * g_current_spread_and_commission * g_contract_size;

            switch(g_calc_mode) {
            case SYMBOL_CALC_MODE_CFDLEVERAGE:
                local_used_margin = ((trade_size * g_contract_size * starting_price) / g_leverage * g_initial_margin_rate +
                                     (trade_size * g_contract_size * end_price) / g_leverage * g_initial_margin_rate) / 2;
                break;
            case SYMBOL_CALC_MODE_FOREX:
                local_used_margin = (trade_size * g_contract_size / g_leverage * g_initial_margin_rate * 2) / 2;
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
                    g_spacing_sell = 1;
                    g_trade_size_sell = MathFloor(proportion) * g_min_volume;
                } else {
                    g_trade_size_sell = g_min_volume;
                    g_spacing_sell = MathRound((len / number_of_trades) * 100) / 100;
                }
                break;
            case 1:
                double temp = (-1 + MathSqrt(1 + 8 * number_of_trades)) / 2;
                temp = MathFloor(temp);
                g_spacing_sell = len / (temp - 1);
                g_trade_size_sell = g_min_volume;
                break;
            }
        }
    }
}

//+------------------------------------------------------------------+
void OpenNew() {
    if(g_count_sell == 0) SizingSell();

    if(g_count_sell == 0) {
        if(OpenSell(g_trade_size_sell, -1)) {
            g_highest_sell = g_current_bid;
            g_lowest_sell = g_current_bid;
            g_count_sell++;
            if(sizing == 1) g_trade_size_sell += g_min_volume;
        }
    } else {
        double temp, temp1, temp2, temp3 = 0, temp4, temp5, temp6;

        switch(sizing) {
        case 0:
            if(g_highest_sell < g_current_bid) {
                temp = (g_current_bid - g_lowest_sell) / g_spacing_sell;
                temp1 = MathFloor(temp);
                temp2 = temp - temp1;
                temp3 = temp1;
                temp4 = g_spacing_sell / (g_trade_size_sell / g_min_volume);
                temp5 = MathFloor(temp2 / temp4);
                temp6 = temp3 * g_trade_size_sell + temp5 * g_min_volume - g_volume_of_open_trades;
                if(temp6 > 0) {
                    if(OpenSell(temp6, -1)) {
                        g_highest_sell = g_current_bid;
                        g_count_sell++;
                    }
                }
            }
            break;
        case 1:
            if(g_highest_sell < g_current_bid) {
                temp = (g_current_bid - g_lowest_sell) / g_spacing_sell;
                temp1 = MathFloor(temp);
                temp2 = temp - temp1;
                for(int i = 1; i <= temp1; i++) temp3 += i;
                temp4 = g_spacing_sell / (temp1 + 1);
                temp5 = MathFloor(temp2 / temp4);
                temp6 = (temp3 + temp5) * g_min_volume - (g_volume_of_open_trades - g_min_volume);
                if(temp6 > 0) {
                    if(OpenSell(temp6, -1)) {
                        g_highest_sell = g_current_bid;
                        g_count_sell++;
                    }
                }
            }
            break;
        }
    }
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
        ClearTurnMarks();
    }

    if((g_direction != 1) && (g_bid_at_turn_up <= g_current_bid - g_current_spread_and_commission) && (g_ask_at_turn_up <= g_current_ask - g_current_spread_and_commission)) {
        g_direction = 1;
        g_direction_change = 1;

        switch(closing_mode) {
        case 0:
            if(g_value_of_sell_trades > 0) {
                g_close_all_profitable_sell_flag = true;
                Iterate();
            }
            break;
        case 1:
            if(!g_is_there_unprofitable_sell) {
                g_close_all_sell_flag = true;
                Iterate();
            }
            break;
        }
        ClearTurnMarks();
    }
}

//+------------------------------------------------------------------+
void OnTick() {
    NewTickValues();
    DirectionCheck();
    OpenNew();
}

//+------------------------------------------------------------------+
int OnInit() {
    g_point = SymbolInfoDouble(_Symbol, SYMBOL_POINT);
    g_min_volume = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN);
    g_max_volume = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MAX);
    g_contract_size = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);
    g_leverage = AccountInfoInteger(ACCOUNT_LEVERAGE);
    g_calc_mode = (int)SymbolInfoInteger(_Symbol, SYMBOL_TRADE_CALC_MODE);
    SymbolInfoMarginRate(_Symbol, ORDER_TYPE_SELL, g_initial_margin_rate, g_maintenance_margin_rate);

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

    // Initialize state
    g_value_of_sell_trades = 0;
    g_count_sell = 0;
    g_volume_of_open_trades = 0;
    g_lowest_sell = DBL_MAX;
    g_highest_sell = DBL_MIN;
    if(sizing == 1) g_trade_size_sell = 0;

    // Scan existing positions
    for(int i = PositionsTotal() - 1; i >= 0; i--) {
        ulong ticket = PositionGetTicket(i);
        if(ticket == 0) continue;
        if(!m_position.SelectByTicket(ticket)) continue;

        double lots = PositionGetDouble(POSITION_VOLUME);
        double swap = OrderSwap();
        double profit = m_position.Profit() + swap;
        double open_price = PositionGetDouble(POSITION_PRICE_OPEN);

        if(PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_SELL) {
            g_value_of_sell_trades += profit;
            g_lowest_sell = MathMin(g_lowest_sell, open_price);
            g_highest_sell = MathMax(g_highest_sell, open_price);
            g_count_sell++;
            g_volume_of_open_trades += lots;
            if(sizing == 1) g_trade_size_sell = MathMax(g_trade_size_sell, lots);
        }
    }

    if(sizing == 1) g_trade_size_sell += g_min_volume;
    ClearFlags();

    return INIT_SUCCEEDED;
}

//+------------------------------------------------------------------+
void OnDeinit(const int reason) {}
//+------------------------------------------------------------------+
