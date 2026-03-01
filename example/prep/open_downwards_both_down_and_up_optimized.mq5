//+------------------------------------------------------------------+
//| open_downwards_both_down_and_up_optimized.mq5                    |
//| Runtime optimized version - Combined SELL strategies             |
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
input int sizing = 0; // sizing: constant, incremental
input int closing_mode = 0;

// Cached constants (set once in OnInit)
double g_min_volume, g_max_volume, g_point, g_contract_size;
double g_initial_margin_rate, g_maintenance_margin_rate;
double g_initial_margin;  // SYMBOL_MARGIN_INITIAL override
long g_leverage;
int g_calc_mode, g_volume_digits;
ENUM_ORDER_TYPE_FILLING g_filling_mode;

// State variables for direction-based (down_w_up)
double g_current_spread;
double g_current_equity, g_current_balance;
double g_current_ask, g_current_bid;
double g_bid_at_turn_up, g_ask_at_turn_up;
double g_bid_at_turn_down, g_ask_at_turn_down;

double g_value_of_sell_trades;
double g_lowest_sell, g_highest_sell;
double g_volume_of_open_trades;
double g_volume_of_open_trades_up;  // For down_w_down positions
double g_trade_size_sell, g_spacing_sell;

int g_direction, g_direction_change;
int g_count_sell;

bool g_close_all_sell_flag, g_close_all_profitable_sell_flag;
bool g_is_there_unprofitable_sell;

// Reusable request/result
MqlTradeRequest g_req;
MqlTradeResult g_res;

// State for down_w_down
double g_down_volume_of_open_trades;
double g_down_cost_of_open_trades;  // sum(vol_i * open_price_i) for precise loss calc
double g_down_checked_last_open_price;
double g_down_local_survive_up;

//+------------------------------------------------------------------+
void ClearFlags() {
    g_close_all_sell_flag = false;
    g_close_all_profitable_sell_flag = false;
}

//+------------------------------------------------------------------+
// Iterate for down_w_up (positions with comments)
void Iterate() {
    g_value_of_sell_trades = 0;
    g_count_sell = 0;
    g_volume_of_open_trades = 0;
    g_lowest_sell = DBL_MAX;
    g_highest_sell = DBL_MIN;
    g_is_there_unprofitable_sell = false;

    if(sizing == 1) g_trade_size_sell = 0;

    string cmnt;
    ushort u_sep = StringGetCharacter(";", 0);
    string result[];

    for(int i = PositionsTotal() - 1; i >= 0; i--) {
        ulong ticket = PositionGetTicket(i);
        if(ticket == 0) continue;
        if(!m_position.SelectByTicket(ticket)) continue;

        double lots = PositionGetDouble(POSITION_VOLUME);
        double swap = PositionGetDouble(POSITION_SWAP);
        double profit = m_position.Profit() + swap;
        double open_price = PositionGetDouble(POSITION_PRICE_OPEN);
        cmnt = PositionGetString(POSITION_COMMENT);

        if(PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_SELL) {
            int k = StringSplit(cmnt, u_sep, result);
            if(k > 0) {  // Only positions with comments (down_w_up)
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
    }

    if(sizing == 1) g_trade_size_sell += g_min_volume;
    ClearFlags();
}

//+------------------------------------------------------------------+
// Open for down_w_up (with comment)
bool OpenDownWUp(double local_unit, int local_direction) {
    if(local_unit < g_min_volume) return false;

    double final_unit = MathMin(local_unit, g_max_volume);
    final_unit = NormalizeDouble(final_unit, g_volume_digits);

    g_req.volume = final_unit;
    g_req.type = ORDER_TYPE_SELL;
    g_req.price = g_current_bid;
    StringConcatenate(g_req.comment, DoubleToString(g_spacing_sell, 2), ";", DoubleToString(g_trade_size_sell, 2));

    ZeroMemory(g_res);
    if(!OrderSend(g_req, g_res)) {
        ResetLastError();
        return false;
    }
    return true;
}

//+------------------------------------------------------------------+
// Open for down_w_down (no comment)
double OpenDownWDown(double local_unit, double current_bid) {
    if(local_unit < g_min_volume) return 0;

    double lots = MathMin(local_unit, g_max_volume);
    lots = NormalizeDouble(lots, g_volume_digits);

    g_req.volume = lots;
    g_req.type = ORDER_TYPE_SELL;
    g_req.price = current_bid;
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
void SizingSell() {
    g_trade_size_sell = 0;
    g_spacing_sell = 0;

    double used_margin = AccountInfoDouble(ACCOUNT_MARGIN);
    double margin_stop_out_level = AccountInfoDouble(ACCOUNT_MARGIN_SO_SO);
    double equity = AccountInfoDouble(ACCOUNT_EQUITY);

    double end_price = g_current_bid * ((100 + survive) / 100);
    double distance = end_price - g_current_bid;

    // Equity already contains unrealized PnL at current price.
    // Grid only accounts for its own positions: V × distance × C
    double equity_at_target = equity - g_volume_of_open_trades * distance * g_contract_size;
    bool need_new_grid = (g_count_sell == 0) ||
                         (equity_at_target > margin_stop_out_level / 100 * used_margin);

    if(need_new_grid) {
        double margin_level = equity_at_target / used_margin * 100;
        double trade_size = g_min_volume;
        double local_used_margin = 0;
        double starting_price = g_current_ask;
        double len = end_price - starting_price;

        if(margin_level > margin_stop_out_level) {
            // Grid loss is triangular: avg loss per unit = len/2 (constant) or len/3 (incremental)
            double C = trade_size * g_contract_size;
            double loss_per_unit;
            if(sizing == 1)
                loss_per_unit = C * len / 3;
            else
                loss_per_unit = C * len / 2;
            double d_equity = loss_per_unit + C * g_current_spread;

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
void OpenNewDownWUp() {
    if(g_count_sell == 0) SizingSell();

    if(g_count_sell == 0) {
        if(OpenDownWUp(g_trade_size_sell, -1)) {
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
                    if(OpenDownWUp(temp6, -1)) {
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
                    if(OpenDownWUp(temp6, -1)) {
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
void OpenNewDownWDown() {
    string cmnt;
    ushort u_sep = StringGetCharacter(";", 0);
    string result[];

    double current_spread = g_point * SymbolInfoInteger(_Symbol, SYMBOL_SPREAD);
    double current_bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);

    double equity = AccountInfoDouble(ACCOUNT_EQUITY);
    double used_margin = AccountInfoDouble(ACCOUNT_MARGIN);
    double margin_stop_out_level = AccountInfoDouble(ACCOUNT_MARGIN_SO_SO);

    // Check if price is falling (for down_w_down positions - no comment)
    if((g_down_volume_of_open_trades == 0) || (g_down_checked_last_open_price > current_bid)) {
        g_down_checked_last_open_price = current_bid;

        // Scan positions without comment (down_w_down) — track both volume and cost basis
        g_down_volume_of_open_trades = 0;
        g_down_cost_of_open_trades = 0;
        for(int i = PositionsTotal() - 1; i >= 0; i--) {
            ulong ticket = PositionGetTicket(i);
            if(ticket == 0) continue;
            if(!PositionSelectByTicket(ticket)) continue;
            cmnt = PositionGetString(POSITION_COMMENT);
            if((PositionGetString(POSITION_SYMBOL) == _Symbol) && (PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_SELL)) {
                int k = StringSplit(cmnt, u_sep, result);
                if(k == 0) {  // No comment = down_w_down position
                    double lots = PositionGetDouble(POSITION_VOLUME);
                    g_down_volume_of_open_trades += lots;
                    g_down_cost_of_open_trades += lots * PositionGetDouble(POSITION_PRICE_OPEN);
                }
            }
        }

        double end_price = current_bid * ((100 + g_down_local_survive_up) / 100);
        double distance = end_price - current_bid;

        // Equity already contains unrealized PnL at current price.
        // Subtract only REMAINING loss from current to end: V * distance * C
        double equity_after_existing = equity - g_down_volume_of_open_trades * distance * g_contract_size;
        bool need_new_trade = (g_down_volume_of_open_trades == 0) ||
                              (equity_after_existing > margin_stop_out_level / 100 * used_margin);

        if(need_new_trade) {
            double trade_size = 0;
            double starting_price = current_bid;

            switch(g_calc_mode) {
            case SYMBOL_CALC_MODE_CFDLEVERAGE:
                trade_size = (100 * equity * g_leverage - 100 * g_contract_size * MathAbs(distance) * g_down_volume_of_open_trades * g_leverage - g_leverage * margin_stop_out_level * used_margin) / (g_contract_size * (100 * MathAbs(distance) * g_leverage + 100 * current_spread * g_leverage + starting_price * g_initial_margin_rate * margin_stop_out_level));
                break;
            case SYMBOL_CALC_MODE_FOREX:
                trade_size = (100 * g_leverage * equity - 100 * g_contract_size * MathAbs(distance) * g_leverage * g_down_volume_of_open_trades - g_leverage * margin_stop_out_level * used_margin) / (g_contract_size * (100 * MathAbs(distance) * g_leverage + 100 * current_spread * g_leverage + (g_initial_margin > 0 ? g_initial_margin * g_leverage / g_contract_size : 1.0) * g_initial_margin_rate * margin_stop_out_level));
                break;
            case SYMBOL_CALC_MODE_FOREX_NO_LEVERAGE:
                trade_size = (100 * equity - 100 * g_contract_size * MathAbs(distance) * g_down_volume_of_open_trades - margin_stop_out_level * used_margin) / (g_contract_size * (100 * MathAbs(distance) + 100 * current_spread + (g_initial_margin > 0 ? g_initial_margin / g_contract_size : 1.0) * g_initial_margin_rate * margin_stop_out_level));
                break;
            case SYMBOL_CALC_MODE_CFD:
                trade_size = (100 * equity - 100 * g_contract_size * MathAbs(distance) * g_down_volume_of_open_trades - margin_stop_out_level * used_margin) / (g_contract_size * (100 * MathAbs(distance) + 100 * current_spread + starting_price * g_initial_margin_rate * margin_stop_out_level));
                break;
            }
            trade_size = MathFloor(trade_size / g_min_volume) * g_min_volume;

            if(trade_size >= g_min_volume) {
                double opened = OpenDownWDown(trade_size, current_bid);
                g_down_volume_of_open_trades += opened;
                g_down_cost_of_open_trades += opened * current_bid;
                g_down_checked_last_open_price = current_bid;
            }
        }
    }
    g_volume_of_open_trades_up = g_down_volume_of_open_trades;
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

    if((g_direction != -1) && (g_ask_at_turn_down >= g_current_ask + g_current_spread) && (g_bid_at_turn_down >= g_current_bid + g_current_spread)) {
        g_direction = -1;
        g_direction_change = -1;
        ClearTurnMarks();
    }

    if((g_direction != 1) && (g_bid_at_turn_up <= g_current_bid - g_current_spread) && (g_ask_at_turn_up <= g_current_ask - g_current_spread)) {
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
    OpenNewDownWDown();
    OpenNewDownWUp();
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

    // Initialize state for down_w_up
    g_value_of_sell_trades = 0;
    g_count_sell = 0;
    g_volume_of_open_trades = 0;
    g_lowest_sell = DBL_MAX;
    g_highest_sell = DBL_MIN;
    if(sizing == 1) g_trade_size_sell = 0;

    // Initialize state for down_w_down
    g_down_checked_last_open_price = DBL_MAX;
    g_down_volume_of_open_trades = 0;
    g_down_cost_of_open_trades = 0;
    g_down_local_survive_up = 2 * survive;

    string cmnt;
    ushort u_sep = StringGetCharacter(";", 0);
    string result[];

    // Scan existing positions
    for(int i = PositionsTotal() - 1; i >= 0; i--) {
        ulong ticket = PositionGetTicket(i);
        if(ticket == 0) continue;
        if(!m_position.SelectByTicket(ticket)) continue;

        double lots = PositionGetDouble(POSITION_VOLUME);
        double swap = PositionGetDouble(POSITION_SWAP);
        double profit = m_position.Profit() + swap;
        double open_price = PositionGetDouble(POSITION_PRICE_OPEN);
        cmnt = PositionGetString(POSITION_COMMENT);

        if(PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_SELL) {
            int k = StringSplit(cmnt, u_sep, result);
            if(k > 0) {
                // down_w_up position (has comment)
                g_value_of_sell_trades += profit;
                g_lowest_sell = MathMin(g_lowest_sell, open_price);
                g_highest_sell = MathMax(g_highest_sell, open_price);
                g_count_sell++;
                g_volume_of_open_trades += lots;
                if(sizing == 1) g_trade_size_sell = MathMax(g_trade_size_sell, lots);
            } else {
                // down_w_down position (no comment)
                g_down_volume_of_open_trades += lots;
                g_down_cost_of_open_trades += lots * open_price;
                g_down_checked_last_open_price = MathMin(g_down_checked_last_open_price, open_price);
            }
        }
    }

    if(sizing == 1) g_trade_size_sell += g_min_volume;
    ClearFlags();

    Print("init: checked_last_open_price ", g_down_checked_last_open_price);

    return INIT_SUCCEEDED;
}

//+------------------------------------------------------------------+
void OnDeinit(const int reason) {}
//+------------------------------------------------------------------+
