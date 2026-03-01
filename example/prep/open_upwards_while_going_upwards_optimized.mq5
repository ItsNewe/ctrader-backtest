//+------------------------------------------------------------------+
//| open_upwards_while_going_upwards_optimized.mq5                   |
//| Runtime optimized version                                        |
//+------------------------------------------------------------------+

#include <Trade\Trade.mqh>

CTrade m_trade;

//+------------------------------------------------------------------+
//| Input parameters                                                 |
//+------------------------------------------------------------------+
input double survive_down = 4; //survive down %

// Cached constants (set once in OnInit)
double g_min_volume, g_max_volume, g_point, g_contract_size;
double g_initial_margin_rate, g_maintenance_margin_rate;
double g_initial_margin;  // SYMBOL_MARGIN_INITIAL override
double g_margin_stop_out_level;
long g_leverage;
int g_calc_mode, g_volume_digits;
ENUM_ORDER_TYPE_FILLING g_filling_mode;

// State variables
double g_volume_of_open_trades;
double g_cost_of_open_trades;  // sum(vol_i * open_price_i) for precise loss calc
double g_checked_last_open_price;

// Reusable request/result
MqlTradeRequest g_req;
MqlTradeResult g_res;

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

    // Volume digits
    g_volume_digits = (g_min_volume == 0.01) ? 2 : (g_min_volume == 0.1) ? 1 : 0;

    // Cache filling mode
    long filling = SymbolInfoInteger(_Symbol, SYMBOL_FILLING_MODE);
    g_filling_mode = (filling == SYMBOL_FILLING_FOK) ? ORDER_FILLING_FOK :
                     (filling == SYMBOL_FILLING_IOC) ? ORDER_FILLING_IOC :
                     (filling == 4) ? ORDER_FILLING_BOC : ORDER_FILLING_FOK;

    // Pre-fill static request fields
    ZeroMemory(g_req);
    g_req.action = TRADE_ACTION_DEAL;
    g_req.symbol = _Symbol;
    g_req.type = ORDER_TYPE_BUY;
    g_req.deviation = 1;
    g_req.type_filling = g_filling_mode;

    m_trade.SetAsyncMode(true);
    m_trade.LogLevel(LOG_LEVEL_NO);

    // Initialize state - scan existing positions
    g_checked_last_open_price = DBL_MIN;
    g_volume_of_open_trades = 0;
    g_cost_of_open_trades = 0;

    for(int i = PositionsTotal() - 1; i >= 0; i--) {
        ulong ticket = PositionGetTicket(i);
        if(ticket == 0) continue;
        if(PositionGetString(POSITION_SYMBOL) != _Symbol) continue;
        if(PositionGetInteger(POSITION_TYPE) != POSITION_TYPE_BUY) continue;

        double lots = PositionGetDouble(POSITION_VOLUME);
        double open_price = PositionGetDouble(POSITION_PRICE_OPEN);
        g_volume_of_open_trades += lots;
        g_cost_of_open_trades += lots * open_price;
        g_checked_last_open_price = MathMax(g_checked_last_open_price, open_price);
    }

    Print("init: checked_last_open_price ", g_checked_last_open_price);

    return INIT_SUCCEEDED;
}

//+------------------------------------------------------------------+
double OpenBuy(double local_unit, double current_ask) {
    if(local_unit < g_min_volume) return 0;

    double lots = MathMin(local_unit, g_max_volume);
    lots = NormalizeDouble(lots, g_volume_digits);

    g_req.volume = lots;
    g_req.price = current_ask;

    ZeroMemory(g_res);
    if(!OrderSend(g_req, g_res)) {
        ResetLastError();
        return 0;
    }
    return lots;
}

//+------------------------------------------------------------------+
void OnTick() {
    double current_spread = g_point * SymbolInfoInteger(_Symbol, SYMBOL_SPREAD);

    double current_ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);

    double equity = AccountInfoDouble(ACCOUNT_EQUITY);
    double used_margin = AccountInfoDouble(ACCOUNT_MARGIN);

    // Open new if price is rising
    if((g_volume_of_open_trades == 0) || (g_checked_last_open_price < current_ask)) {
        g_checked_last_open_price = current_ask;

        // Scan positions — track both volume and cost basis for precise loss calculation
        g_volume_of_open_trades = 0;
        g_cost_of_open_trades = 0;
        for(int i = PositionsTotal() - 1; i >= 0; i--) {
            ulong ticket = PositionGetTicket(i);
            if(ticket == 0) continue;
            if(PositionGetString(POSITION_SYMBOL) != _Symbol) continue;
            if(PositionGetInteger(POSITION_TYPE) != POSITION_TYPE_BUY) continue;
            double lots = PositionGetDouble(POSITION_VOLUME);
            g_volume_of_open_trades += lots;
            g_cost_of_open_trades += lots * PositionGetDouble(POSITION_PRICE_OPEN);
        }

        double end_price = current_ask * ((100 - survive_down) / 100);
        double distance = current_ask - end_price;

        // Equity already contains unrealized PnL at current price.
        // To estimate equity at end_price, subtract only the REMAINING loss
        // from current_price to end_price: V * distance * C
        // (This is exact: equity_at_end = equity - V*(current-end)*C)
        double equity_after_existing = equity - g_volume_of_open_trades * distance * g_contract_size;
        bool need_new_trade = (g_volume_of_open_trades == 0) ||
                              (equity_after_existing > g_margin_stop_out_level / 100 * used_margin);

        if(need_new_trade) {
            double trade_size = 0;
            double starting_price = current_ask;

            switch(g_calc_mode) {
            case SYMBOL_CALC_MODE_CFDLEVERAGE:
                trade_size = (100 * equity * g_leverage - 100 * g_contract_size * MathAbs(distance) * g_volume_of_open_trades * g_leverage - g_leverage * g_margin_stop_out_level * used_margin) / (g_contract_size * (100 * MathAbs(distance) * g_leverage + 100 * current_spread * g_leverage + starting_price * g_initial_margin_rate * g_margin_stop_out_level));
                break;
            case SYMBOL_CALC_MODE_FOREX:
                trade_size = (100 * g_leverage * equity - 100 * g_contract_size * MathAbs(distance) * g_leverage * g_volume_of_open_trades - g_leverage * g_margin_stop_out_level * used_margin) / (g_contract_size * (100 * MathAbs(distance) * g_leverage + 100 * current_spread * g_leverage + (g_initial_margin > 0 ? g_initial_margin * g_leverage / g_contract_size : 1.0) * g_initial_margin_rate * g_margin_stop_out_level));
                break;
            case SYMBOL_CALC_MODE_FOREX_NO_LEVERAGE:
                trade_size = (100 * equity - 100 * g_contract_size * MathAbs(distance) * g_volume_of_open_trades - g_margin_stop_out_level * used_margin) / (g_contract_size * (100 * MathAbs(distance) + 100 * current_spread + (g_initial_margin > 0 ? g_initial_margin / g_contract_size : 1.0) * g_initial_margin_rate * g_margin_stop_out_level));
                break;
            case SYMBOL_CALC_MODE_CFD:
                trade_size = (100 * equity - 100 * g_contract_size * MathAbs(distance) * g_volume_of_open_trades - g_margin_stop_out_level * used_margin) / (g_contract_size * (100 * MathAbs(distance) + 100 * current_spread + starting_price * g_initial_margin_rate * g_margin_stop_out_level));
                break;
            }
            trade_size = MathFloor(trade_size / g_min_volume) * g_min_volume;

            if(trade_size >= g_min_volume) {
                double opened = OpenBuy(trade_size, current_ask);
                g_volume_of_open_trades += opened;
                g_cost_of_open_trades += opened * current_ask;
                g_checked_last_open_price = current_ask;
            }
        }
    }
}

//+------------------------------------------------------------------+
void OnDeinit(const int reason) {}
//+------------------------------------------------------------------+
