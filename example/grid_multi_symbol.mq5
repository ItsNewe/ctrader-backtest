//+------------------------------------------------------------------+
//| grid_multi_symbol.mq5                                          |
//| Multi-symbol version - trades multiple instruments simultaneously |
//| Run on any chart - it will trade configured symbols              |
//+------------------------------------------------------------------+

#include <Trade\Trade.mqh>
#include <Trade\PositionInfo.mqh>

CTrade m_trade;
CPositionInfo m_position;

//+------------------------------------------------------------------+
//| Input parameters                                                 |
//+------------------------------------------------------------------+
input group "=== Symbol 1 (Gold) ==="
input string   Symbol1 = "XAUUSD";       // Symbol 1 name
input bool     Symbol1_Enabled = true;   // Enable Symbol 1
input double   Symbol1_SurviveDown = 4;  // Symbol 1 survive down %

input group "=== Symbol 2 (Silver) ==="
input string   Symbol2 = "XAGUSD";       // Symbol 2 name
input bool     Symbol2_Enabled = true;   // Enable Symbol 2
input double   Symbol2_SurviveDown = 4;  // Symbol 2 survive down %

input group "=== General Settings ==="
input double   commission = 0;           // Commission (points)

//+------------------------------------------------------------------+
//| Per-symbol cached data structure                                 |
//+------------------------------------------------------------------+
struct SymbolData {
    string symbol;
    bool   enabled;
    double survive_down;
    
    // Cached constants
    double min_volume;
    double max_volume;
    double point;
    double contract_size;
    double initial_margin_rate;
    double   initial_margin;  // SYMBOL_MARGIN_INITIAL override
    double maintenance_margin_rate;
    double current_commission;
    int    calc_mode;
    int    volume_digits;
    ENUM_ORDER_TYPE_FILLING filling_mode;
    
    // State variables
    double volume_of_open_trades;
    double checked_last_open_price;
    
    // Validity flag
    bool   valid;
};

// Array of symbol data
SymbolData g_symbols[2];
int g_num_symbols = 0;

// Account-level cached values
double g_margin_stop_out_level;
long   g_leverage;
long   g_account_limit_orders;

// Reusable request/result
MqlTradeRequest g_req;
MqlTradeResult g_res;

//+------------------------------------------------------------------+
//| Initialize a single symbol's data                                |
//+------------------------------------------------------------------+
bool InitSymbol(SymbolData &data, string symbolName, bool enabled, double surviveDown) {
    data.symbol = symbolName;
    data.enabled = enabled;
    data.survive_down = surviveDown;
    data.valid = false;
    
    if(!enabled) return true;  // Not an error, just disabled
    
    // Check if symbol exists and is available
    if(!SymbolSelect(symbolName, true)) {
        Print("Warning: Symbol ", symbolName, " not available in Market Watch");
        return false;
    }
    
    // Wait briefly for symbol data
    int attempts = 0;
    while(SymbolInfoDouble(symbolName, SYMBOL_BID) == 0 && attempts < 10) {
        Sleep(100);
        attempts++;
    }
    
    if(SymbolInfoDouble(symbolName, SYMBOL_BID) == 0) {
        Print("Warning: No price data for ", symbolName);
        return false;
    }
    
    // Cache symbol info
    data.point = SymbolInfoDouble(symbolName, SYMBOL_POINT);
    data.min_volume = SymbolInfoDouble(symbolName, SYMBOL_VOLUME_MIN);
    data.max_volume = SymbolInfoDouble(symbolName, SYMBOL_VOLUME_MAX);
    data.contract_size = SymbolInfoDouble(symbolName, SYMBOL_TRADE_CONTRACT_SIZE);
    data.calc_mode = (int)SymbolInfoInteger(symbolName, SYMBOL_TRADE_CALC_MODE);
    SymbolInfoMarginRate(symbolName, ORDER_TYPE_BUY, data.initial_margin_rate, data.maintenance_margin_rate);
    data.initial_margin = SymbolInfoDouble(symbolName, SYMBOL_MARGIN_INITIAL);
    
    // Volume digits
    data.volume_digits = (data.min_volume == 0.01) ? 2 : (data.min_volume == 0.1) ? 1 : 0;
    
    // Filling mode
    long filling = SymbolInfoInteger(symbolName, SYMBOL_FILLING_MODE);
    data.filling_mode = (filling == SYMBOL_FILLING_FOK) ? ORDER_FILLING_FOK :
                        (filling == SYMBOL_FILLING_IOC) ? ORDER_FILLING_IOC :
                        (filling == 4) ? ORDER_FILLING_BOC : ORDER_FILLING_FOK;
    
    // Commission
    data.current_commission = data.point * commission * 100;
    
    // Initialize state
    data.checked_last_open_price = DBL_MIN;
    data.volume_of_open_trades = 0;
    
    // Scan existing positions for this symbol
    for(int i = PositionsTotal() - 1; i >= 0; i--) {
        ulong ticket = PositionGetTicket(i);
        if(ticket == 0) continue;
        if(PositionGetString(POSITION_SYMBOL) != symbolName) continue;
        if(PositionGetInteger(POSITION_TYPE) != POSITION_TYPE_BUY) continue;
        
        data.volume_of_open_trades += PositionGetDouble(POSITION_VOLUME);
        double open_price = PositionGetDouble(POSITION_PRICE_OPEN);
        data.checked_last_open_price = MathMax(data.checked_last_open_price, open_price);
    }
    
    data.valid = true;
    Print("Initialized ", symbolName, ": survive=", surviveDown, "%, last_open=", data.checked_last_open_price);
    return true;
}

//+------------------------------------------------------------------+
int OnInit() {
    // Cache account-level info
    g_leverage = AccountInfoInteger(ACCOUNT_LEVERAGE);
    g_margin_stop_out_level = AccountInfoDouble(ACCOUNT_MARGIN_SO_SO);
    g_account_limit_orders = AccountInfoInteger(ACCOUNT_LIMIT_ORDERS);
    
    // Initialize symbols
    g_num_symbols = 0;
    
    if(InitSymbol(g_symbols[0], Symbol1, Symbol1_Enabled, Symbol1_SurviveDown)) {
        if(g_symbols[0].enabled && g_symbols[0].valid) g_num_symbols++;
    }
    
    if(InitSymbol(g_symbols[1], Symbol2, Symbol2_Enabled, Symbol2_SurviveDown)) {
        if(g_symbols[1].enabled && g_symbols[1].valid) g_num_symbols++;
    }
    
    if(g_num_symbols == 0) {
        Print("Error: No valid symbols configured");
        return INIT_FAILED;
    }
    
    // Pre-fill static request fields
    ZeroMemory(g_req);
    g_req.action = TRADE_ACTION_DEAL;
    g_req.type = ORDER_TYPE_BUY;
    g_req.deviation = 1;
    
    m_trade.SetAsyncMode(true);
    m_trade.LogLevel(LOG_LEVEL_ALL);
    
    Print("Multi-symbol EA initialized with ", g_num_symbols, " symbol(s)");
    
    // Set timer to process all symbols
    EventSetMillisecondTimer(100);
    
    return INIT_SUCCEEDED;
}

//+------------------------------------------------------------------+
double OpenBuy(SymbolData &data, double local_unit, double current_ask) {
    if(local_unit < data.min_volume) return 0;
    
    double lots = MathMin(local_unit, data.max_volume);
    lots = NormalizeDouble(lots, data.volume_digits);
    
    g_req.symbol = data.symbol;
    g_req.volume = lots;
    g_req.price = current_ask;
    g_req.type_filling = data.filling_mode;
    
    ZeroMemory(g_res);
    if(!OrderSend(g_req, g_res)) {
        Print("OrderSend failed for ", data.symbol, ": ", GetLastError());
        ResetLastError();
        return 0;
    }
    return lots;
}

//+------------------------------------------------------------------+
void ProcessSymbol(SymbolData &data) {
    if(!data.enabled || !data.valid) return;
    
    double current_spread = data.point * SymbolInfoInteger(data.symbol, SYMBOL_SPREAD);
    double current_spread_and_commission = current_spread + data.current_commission;
    
    double current_ask = SymbolInfoDouble(data.symbol, SYMBOL_ASK);
    if(current_ask == 0) return;  // No price data
    
    double equity = AccountInfoDouble(ACCOUNT_EQUITY);
    double current_margin_level = AccountInfoDouble(ACCOUNT_MARGIN_LEVEL);
    double used_margin = AccountInfoDouble(ACCOUNT_MARGIN);
    
    // Open new if price is rising
    if((data.volume_of_open_trades == 0) || (data.checked_last_open_price < current_ask)) {
        data.checked_last_open_price = current_ask;
        
        // Scan positions for this symbol
        data.volume_of_open_trades = 0;
        for(int i = PositionsTotal() - 1; i >= 0; i--) {
            ulong ticket = PositionGetTicket(i);
            if(ticket == 0) continue;
            if(PositionGetString(POSITION_SYMBOL) != data.symbol) continue;
            if(PositionGetInteger(POSITION_TYPE) != POSITION_TYPE_BUY) continue;
            data.volume_of_open_trades += PositionGetDouble(POSITION_VOLUME);
        }
        
        // Calculate total volume across ALL symbols for margin calculations
        double total_volume_value = 0;
        for(int s = 0; s < 2; s++) {
            if(!g_symbols[s].enabled || !g_symbols[s].valid) continue;
            
            double sym_vol = 0;
            for(int i = PositionsTotal() - 1; i >= 0; i--) {
                ulong ticket = PositionGetTicket(i);
                if(ticket == 0) continue;
                if(PositionGetString(POSITION_SYMBOL) != g_symbols[s].symbol) continue;
                if(PositionGetInteger(POSITION_TYPE) != POSITION_TYPE_BUY) continue;
                sym_vol += PositionGetDouble(POSITION_VOLUME);
            }
            total_volume_value += sym_vol * g_symbols[s].contract_size;
        }
        
        double equity_at_target = (current_margin_level > 0) ? 
            equity * g_margin_stop_out_level / current_margin_level : equity;
        double equity_difference = equity - equity_at_target;
        double price_difference = (data.volume_of_open_trades > 0) ? 
            equity_difference / (data.volume_of_open_trades * data.contract_size) : DBL_MAX;
        
        double end_price = current_ask * ((100 - data.survive_down) / 100);
        double distance = current_ask - end_price;
        
        if((data.volume_of_open_trades == 0) || ((current_ask - price_difference) < end_price)) {
            double trade_size = 0;
            double starting_price = current_ask;
            
            switch(data.calc_mode) {
            case SYMBOL_CALC_MODE_CFDLEVERAGE:
                trade_size = NormalizeDouble((100 * equity * g_leverage - 100 * data.contract_size * MathAbs(distance) * data.volume_of_open_trades * g_leverage - g_leverage * g_margin_stop_out_level * used_margin) / (data.contract_size * (100 * MathAbs(distance) * g_leverage + 100 * current_spread_and_commission * g_leverage + starting_price * data.initial_margin_rate * g_margin_stop_out_level)), data.volume_digits);
                break;
            case SYMBOL_CALC_MODE_FOREX:
                trade_size = NormalizeDouble((100 * g_leverage * equity - 100 * data.contract_size * MathAbs(distance) * g_leverage * data.volume_of_open_trades - g_leverage * g_margin_stop_out_level * used_margin) / (data.contract_size * (100 * MathAbs(distance) * g_leverage + 100 * current_spread_and_commission * g_leverage + (data.initial_margin > 0 ? data.initial_margin * g_leverage / data.contract_size : 1.0) * data.initial_margin_rate * g_margin_stop_out_level)), data.volume_digits);
                break;
            case SYMBOL_CALC_MODE_FOREX_NO_LEVERAGE:
                trade_size = NormalizeDouble((100 * equity - 100 * data.contract_size * MathAbs(distance) * data.volume_of_open_trades - g_margin_stop_out_level * used_margin) / (data.contract_size * (100 * MathAbs(distance) + 100 * current_spread_and_commission + (data.initial_margin > 0 ? data.initial_margin / data.contract_size : 1.0) * data.initial_margin_rate * g_margin_stop_out_level)), data.volume_digits);
                break;
            case SYMBOL_CALC_MODE_CFD:
                trade_size = NormalizeDouble((100 * equity - 100 * data.contract_size * MathAbs(distance) * data.volume_of_open_trades - g_margin_stop_out_level * used_margin) / (data.contract_size * (100 * MathAbs(distance) + 100 * current_spread_and_commission + starting_price * data.initial_margin_rate * g_margin_stop_out_level)), data.volume_digits);
                break;
            }
            
            if(trade_size >= data.min_volume) {
                // Check if at order limit - close smallest profitable position (any symbol)
                if(PositionsTotal() == g_account_limit_orders) {
                    double min_volume = DBL_MAX;
                    ulong min_ticket = 0;
                    
                    for(int i = PositionsTotal() - 1; i >= 0; i--) {
                        ulong ticket = PositionGetTicket(i);
                        if(ticket == 0) continue;
                        if(PositionGetInteger(POSITION_TYPE) != POSITION_TYPE_BUY) continue;
                        
                        if(m_position.SelectByTicket(ticket)) {
                            double lots = PositionGetDouble(POSITION_VOLUME);
                            double profit = m_position.Profit();
                            if((profit > 0) && (lots < min_volume)) {
                                min_volume = lots;
                                min_ticket = ticket;
                            }
                        }
                    }
                    
                    if(min_ticket > 0 && m_position.SelectByTicket(min_ticket)) {
                        if(!m_trade.PositionClose(min_ticket)) {
                            ResetLastError();
                        }
                    }
                } else {
                    data.volume_of_open_trades += OpenBuy(data, trade_size, current_ask);
                    data.checked_last_open_price = current_ask;
                }
            }
        }
    }
}

//+------------------------------------------------------------------+
void OnTick() {
    // Process all enabled symbols on every tick
    for(int i = 0; i < 2; i++) {
        ProcessSymbol(g_symbols[i]);
    }
}

//+------------------------------------------------------------------+
void OnTimer() {
    // Also process on timer for symbols that might not be on current chart
    for(int i = 0; i < 2; i++) {
        ProcessSymbol(g_symbols[i]);
    }
}

//+------------------------------------------------------------------+
void OnDeinit(const int reason) {
    EventKillTimer();
}
//+------------------------------------------------------------------+
