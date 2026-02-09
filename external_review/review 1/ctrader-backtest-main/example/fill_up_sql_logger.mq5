//+------------------------------------------------------------------+
//| fill_up_sql_logger.mq5 - SQL-style logging for C++ verification |
//| Outputs CSV-like logs that C++ can read and verify against      |
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
input string log_file_prefix = "fill_up_log";  // Log file prefix

double min_volume_alg, max_volume_alg;
double current_spread = 0, point = 0;
double current_equity, current_balance;
double current_ask = 0, current_bid = 0;
double lowest_buy, highest_buy;
double trade_size_buy, spacing_buy;
double closest_above, closest_below;
double volume_of_open_trades = 0;

int digit = 2;
MqlTradeResult trade_result = { };
MqlTradeRequest req = { };

// Tracking
datetime last_date = 0;
int trade_counter = 0;
int file_handle = INVALID_HANDLE;

//+------------------------------------------------------------------+
//| Initialize log file                                               |
//+------------------------------------------------------------------+
void InitLogFile() {
    string filename = log_file_prefix + "_" + _Symbol + ".csv";
    file_handle = FileOpen(filename, FILE_WRITE|FILE_CSV|FILE_COMMON, ',');
    if (file_handle != INVALID_HANDLE) {
        // Write header
        FileWrite(file_handle,
            "event_type",      // TICK, TRADE_OPEN, TRADE_CLOSE, SWAP, DAY_CHANGE
            "timestamp",       // YYYY.MM.DD HH:MM:SS
            "bid",
            "ask",
            "balance",
            "equity",
            "open_positions",
            "total_lots",
            "trade_id",
            "direction",
            "lot_size",
            "entry_price",
            "exit_price",
            "take_profit",
            "profit",
            "swap",
            "cumulative_swap",
            "extra_info"
        );
        Print("Log file opened: ", filename);
    } else {
        Print("ERROR: Could not open log file: ", filename);
    }
}

//+------------------------------------------------------------------+
//| Get total lots and swap from open positions                      |
//+------------------------------------------------------------------+
void GetPositionStats(double &total_lots, double &total_swap, int &count) {
    total_lots = 0;
    total_swap = 0;
    count = 0;
    for(int i = PositionsTotal() - 1; i >= 0; i--) {
        ulong ticket = PositionGetTicket(i);
        if(PositionSelectByTicket(ticket)) {
            total_lots += PositionGetDouble(POSITION_VOLUME);
            total_swap += PositionGetDouble(POSITION_SWAP);
            count++;
        }
    }
}

//+------------------------------------------------------------------+
//| Log a tick event (every N ticks to avoid huge files)             |
//+------------------------------------------------------------------+
int tick_counter = 0;
void LogTick(string extra = "") {
    tick_counter++;
    // Only log every 10000th tick to keep file size manageable
    // But always log first tick of each day
    if (tick_counter % 10000 != 0 && extra == "") return;

    if (file_handle == INVALID_HANDLE) return;

    double total_lots, total_swap;
    int count;
    GetPositionStats(total_lots, total_swap, count);

    FileWrite(file_handle,
        "TICK",
        TimeToString(TimeCurrent(), TIME_DATE|TIME_SECONDS),
        DoubleToString(current_bid, 5),
        DoubleToString(current_ask, 5),
        DoubleToString(current_balance, 2),
        DoubleToString(current_equity, 2),
        count,
        DoubleToString(total_lots, 4),
        "", "", "", "", "", "", "", "",
        DoubleToString(total_swap, 2),
        extra
    );
}

//+------------------------------------------------------------------+
//| Log trade open event                                              |
//+------------------------------------------------------------------+
void LogTradeOpen(ulong ticket, string direction, double lots, double price, double tp) {
    if (file_handle == INVALID_HANDLE) return;

    double total_lots, total_swap;
    int count;
    GetPositionStats(total_lots, total_swap, count);

    FileWrite(file_handle,
        "TRADE_OPEN",
        TimeToString(TimeCurrent(), TIME_DATE|TIME_SECONDS),
        DoubleToString(current_bid, 5),
        DoubleToString(current_ask, 5),
        DoubleToString(current_balance, 2),
        DoubleToString(current_equity, 2),
        count,
        DoubleToString(total_lots, 4),
        IntegerToString(ticket),
        direction,
        DoubleToString(lots, 4),
        DoubleToString(price, 5),
        "",
        DoubleToString(tp, 5),
        "",
        "",
        DoubleToString(total_swap, 2),
        ""
    );
}

//+------------------------------------------------------------------+
//| Log trade close event                                             |
//+------------------------------------------------------------------+
void LogTradeClose(ulong ticket, string direction, double lots, double entry_price, double exit_price, double profit, double swap) {
    if (file_handle == INVALID_HANDLE) return;

    double total_lots, total_swap;
    int count;
    GetPositionStats(total_lots, total_swap, count);

    FileWrite(file_handle,
        "TRADE_CLOSE",
        TimeToString(TimeCurrent(), TIME_DATE|TIME_SECONDS),
        DoubleToString(current_bid, 5),
        DoubleToString(current_ask, 5),
        DoubleToString(current_balance, 2),
        DoubleToString(current_equity, 2),
        count,
        DoubleToString(total_lots, 4),
        IntegerToString(ticket),
        direction,
        DoubleToString(lots, 4),
        DoubleToString(entry_price, 5),
        DoubleToString(exit_price, 5),
        "",
        DoubleToString(profit, 2),
        DoubleToString(swap, 2),
        DoubleToString(total_swap, 2),
        ""
    );
}

//+------------------------------------------------------------------+
//| Log day change / swap event                                       |
//+------------------------------------------------------------------+
void LogDayChange(string day_name) {
    if (file_handle == INVALID_HANDLE) return;

    double total_lots, total_swap;
    int count;
    GetPositionStats(total_lots, total_swap, count);

    FileWrite(file_handle,
        "DAY_CHANGE",
        TimeToString(TimeCurrent(), TIME_DATE|TIME_SECONDS),
        DoubleToString(current_bid, 5),
        DoubleToString(current_ask, 5),
        DoubleToString(current_balance, 2),
        DoubleToString(current_equity, 2),
        count,
        DoubleToString(total_lots, 4),
        "", "", "", "", "", "", "", "",
        DoubleToString(total_swap, 2),
        day_name
    );
}

//+------------------------------------------------------------------+
//| Check for day change                                              |
//+------------------------------------------------------------------+
void CheckDayChange() {
    datetime current_time = TimeCurrent();
    MqlDateTime dt;
    TimeToStruct(current_time, dt);
    datetime current_date = StringToTime(StringFormat("%04d.%02d.%02d", dt.year, dt.mon, dt.day));

    if (last_date != 0 && current_date != last_date) {
        string day_names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        LogDayChange(day_names[dt.day_of_week]);
        LogTick("FIRST_TICK_OF_DAY");
    }

    last_date = current_date;
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
    string cmnt;
    ushort u_sep = StringGetCharacter(";", 0);
    string result[];
    ulong ticket;

    for(int PositionIndex = PositionsTotal() - 1; PositionIndex >= 0; PositionIndex--) {
        ticket = PositionGetTicket(PositionIndex);
        if(!PositionSelectByTicket(ticket)) continue;
        if(m_position.SelectByTicket(ticket)) {
            lots = PositionGetDouble(POSITION_VOLUME);
            open_price = PositionGetDouble(POSITION_PRICE_OPEN);
            cmnt = PositionGetString(POSITION_COMMENT);
            if(PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY) {
                int k = StringSplit(cmnt, u_sep, result);
                if(k > 0) {
                    volume_of_open_trades += lots;
                    lowest_buy = MathMin(lowest_buy, open_price);
                    highest_buy = MathMax(highest_buy, open_price);
                    if(open_price >= current_ask)
                        closest_above = MathMin(closest_above, open_price - current_ask);
                    if(open_price <= current_ask)
                        closest_below = MathMin(closest_below, current_ask - open_price);
                }
            }
        }
    }
}

//+------------------------------------------------------------------+
//| Open position                                                     |
//+------------------------------------------------------------------+
bool open(double local_unit, int local_direction) {
    double final_unit = min_volume_alg;
    while(local_unit > 0) {
        if((local_unit >= min_volume_alg) && (local_unit <= max_volume_alg)) {
            final_unit = NormalizeDouble(local_unit, digit);
            local_unit = 0;
        } else if(local_unit < min_volume_alg) {
            return false;
        } else if(local_unit > max_volume_alg) {
            final_unit = NormalizeDouble(max_volume_alg, digit);
            local_unit = 0;
        }

        ZeroMemory(req);
        ZeroMemory(trade_result);
        req.action = TRADE_ACTION_DEAL;
        req.symbol = _Symbol;
        req.volume = final_unit;

        switch(local_direction) {
        case 1:
            req.type = ORDER_TYPE_BUY;
            req.price = current_ask;
            req.tp = current_ask + current_spread + spacing_buy;
            StringConcatenate(req.comment, DoubleToString(spacing_buy, 2), ";", DoubleToString(trade_size_buy, 2));
            break;
        default:
            break;
        }

        req.deviation = 1;
        long filling = SymbolInfoInteger(_Symbol, SYMBOL_FILLING_MODE);
        switch((int)filling) {
        case SYMBOL_FILLING_FOK: req.type_filling = ORDER_FILLING_FOK; break;
        case SYMBOL_FILLING_IOC: req.type_filling = ORDER_FILLING_IOC; break;
        case 4: req.type_filling = ORDER_FILLING_BOC; break;
        default: break;
        }

        if(!OrderSend(req, trade_result)) {
            Print(" Error: ", GetLastError());
            ResetLastError();
            return false;
        } else {
            trade_counter++;
            // Log the trade open
            LogTradeOpen(trade_result.order, "BUY", final_unit, req.price, req.tp);
        }
    }
    return true;
}

//+------------------------------------------------------------------+
//| Open new positions based on grid logic                           |
//+------------------------------------------------------------------+
void open_new() {
    if(PositionsTotal() == 0) {
        sizing_buy();
        if(open(trade_size_buy, 1)) {
            highest_buy = current_ask;
            lowest_buy = current_ask;
        }
    } else {
        if(lowest_buy >= current_ask + spacing_buy) {
            sizing_buy();
            if(open(trade_size_buy, 1)) {
                lowest_buy = current_ask;
            }
        } else if(highest_buy <= current_ask - spacing_buy) {
            sizing_buy();
            if(open(trade_size_buy, 1)) {
                highest_buy = current_ask;
            }
        } else if((closest_above >= spacing_buy) && (closest_below >= spacing_buy)) {
            sizing_buy();
            open(trade_size_buy, 1);
        }
    }
}

//+------------------------------------------------------------------+
//| Update tick values                                                |
//+------------------------------------------------------------------+
double max_balance = 0, max_number_of_open = 0, max_used_funds = 0, max_trade_size = 0;

void new_tick_values() {
    current_spread = point * SymbolInfoInteger(_Symbol, SYMBOL_SPREAD);
    current_equity = m_account.Equity();
    current_balance = m_account.Balance();
    max_balance = MathMax(max_balance, current_balance);
    max_number_of_open = MathMax(max_number_of_open, PositionsTotal());
    max_used_funds = MathMax(max_used_funds, current_balance - current_equity + AccountInfoDouble(ACCOUNT_MARGIN));
    current_ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    current_bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
}

//+------------------------------------------------------------------+
//| OnTick                                                            |
//+------------------------------------------------------------------+
void OnTick() {
    new_tick_values();
    CheckDayChange();
    LogTick();
    iterate();
    open_new();
}

//+------------------------------------------------------------------+
//| OnInit                                                            |
//+------------------------------------------------------------------+
int OnInit() {
    InitLogFile();

    point = SymbolInfoDouble(_Symbol, SYMBOL_POINT);
    m_trade.LogLevel(LOG_LEVEL_NO);
    min_volume_alg = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN);
    max_volume_alg = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MAX);
    m_trade.SetAsyncMode(true);

    if(min_volume_alg == 0.01) digit = 2;
    else if(min_volume_alg == 0.1) digit = 1;
    else digit = 0;

    new_tick_values();
    lowest_buy = DBL_MAX;
    highest_buy = DBL_MIN;
    closest_above = DBL_MAX;
    closest_below = DBL_MIN;
    volume_of_open_trades = 0;
    last_date = 0;
    trade_counter = 0;

    // Log initial state
    if (file_handle != INVALID_HANDLE) {
        FileWrite(file_handle,
            "INIT",
            TimeToString(TimeCurrent(), TIME_DATE|TIME_SECONDS),
            DoubleToString(current_bid, 5),
            DoubleToString(current_ask, 5),
            DoubleToString(current_balance, 2),
            DoubleToString(current_equity, 2),
            0, "0", "", "", "", "", "", "", "", "", "",
            StringFormat("swap_long=%.2f;swap_short=%.2f;swap_mode=%d;swap_3day=%d",
                SymbolInfoDouble(_Symbol, SYMBOL_SWAP_LONG),
                SymbolInfoDouble(_Symbol, SYMBOL_SWAP_SHORT),
                SymbolInfoInteger(_Symbol, SYMBOL_SWAP_MODE),
                SymbolInfoInteger(_Symbol, SYMBOL_SWAP_ROLLOVER3DAYS))
        );
    }

    spacing_buy = spacing;
    trade_size_buy = size * min_volume_alg;

    return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+
//| OnDeinit                                                          |
//+------------------------------------------------------------------+
void OnDeinit(const int reason) {
    // Log final state
    if (file_handle != INVALID_HANDLE) {
        double total_lots, total_swap;
        int count;
        GetPositionStats(total_lots, total_swap, count);

        // Get total swap from history
        double history_swap = 0;
        HistorySelect(0, TimeCurrent());
        int deals = HistoryDealsTotal();
        for(int i = 0; i < deals; i++) {
            ulong ticket = HistoryDealGetTicket(i);
            history_swap += HistoryDealGetDouble(ticket, DEAL_SWAP);
        }

        FileWrite(file_handle,
            "DEINIT",
            TimeToString(TimeCurrent(), TIME_DATE|TIME_SECONDS),
            DoubleToString(current_bid, 5),
            DoubleToString(current_ask, 5),
            DoubleToString(m_account.Balance(), 2),
            DoubleToString(m_account.Equity(), 2),
            count,
            DoubleToString(total_lots, 4),
            "", "", "", "", "", "",
            DoubleToString(m_account.Balance() - 110000, 2),  // Net profit
            "",
            DoubleToString(history_swap, 2),
            StringFormat("trades=%d;max_open=%.0f;max_balance=%.2f", trade_counter, max_number_of_open, max_balance)
        );

        FileClose(file_handle);
        Print("Log file closed. Total trades logged: ", trade_counter);
    }
}

//+------------------------------------------------------------------+
//| Sizing calculation (same as original)                            |
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
    if (PositionsTotal() == 0)
        end_price = current_ask * ((100 - survive) / 100);
    else
        end_price = highest_buy * ((100 - survive) / 100);

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
            default: break;
            }

            local_used_margin = local_used_margin / 2;
            local_used_margin = number_of_trades * local_used_margin;

            double multiplier = 0;
            double equity_backup = equity_at_target;
            double used_margin_backup = used_margin;
            double max = max_volume_alg / min_volume_alg;

            equity_at_target -= max * d_equity;
            used_margin += max * local_used_margin;

            if(margin_stop_out_level < equity_at_target / used_margin * 100) {
                multiplier = max;
            } else {
                used_margin = used_margin_backup;
                equity_at_target = equity_backup;
                for(double increment = max; increment >= 1; increment = increment / 10) {
                    while(margin_stop_out_level < equity_at_target / used_margin * 100) {
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

            multiplier = MathMax(1, multiplier);
            trade_size_buy = multiplier * min_volume_alg;
            max_trade_size = MathMax(max_trade_size, trade_size_buy);
        }
    }
}

//+------------------------------------------------------------------+
double OnTester() {
    return max_number_of_open;
}
//+------------------------------------------------------------------+
