//+------------------------------------------------------------------+
//|                                                 GridSimple.mq5 |
//|                     Simplified Grid - Original + Spacing Only  |
//|                                                                  |
//| Based on original grid_open_upwards_while_going_upwards        |
//| Only change: Added minimum spacing between entries               |
//| NO trailing stops - holds positions like original                |
//+------------------------------------------------------------------+
#property copyright "Grid Simple"
#property version   "1.00"
#property strict

#include <Trade\AccountInfo.mqh>
#include <Trade\PositionInfo.mqh>
#include <Trade\Trade.mqh>

CAccountInfo m_account;
CPositionInfo m_position;
CTrade m_trade;

//+------------------------------------------------------------------+
//| Input Parameters                                                  |
//+------------------------------------------------------------------+
input double InpSurviveDown      = 4.0;      // Survive Down % (original default)
input double InpMinSpacing       = 0.0;      // Min Spacing (price units, 0=original behavior)
input double InpCommission       = 0.0;      // Commission
input int    InpDigit            = 2;        // Volume digits

//+------------------------------------------------------------------+
//| Global Variables                                                  |
//+------------------------------------------------------------------+
double volume_of_open_trades;
double checked_last_open_price;
double last_entry_price;  // For spacing check

double cm;
double current_commission;
double min_volume_alg;
double max_volume_alg;
double margin_stop_out_level;
double local_survive_down;
double contract_size;
double initial_margin_rate = 0;
double maintenance_margin_rate = 0;
long leverage;
int calc_mode;
bool first_run = true;

//+------------------------------------------------------------------+
//| Open position function (from original)                            |
//+------------------------------------------------------------------+
double OpenPosition(double local_unit, double current_ask) {
    static MqlTradeResult trade_result = {};
    static MqlTradeRequest req = {};
    static bool open_first_run = true;

    if(open_first_run) {
        req.symbol = _Symbol;
        req.action = TRADE_ACTION_DEAL;
        req.type = ORDER_TYPE_BUY;
        req.deviation = 10;

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
                req.type_filling = ORDER_FILLING_IOC;
                break;
        }
        open_first_run = false;
    }

    req.volume = 0;
    if(local_unit < min_volume_alg) {
        return 0;
    }
    else if(local_unit <= max_volume_alg) {
        req.volume = NormalizeDouble(local_unit, InpDigit);
    }
    else {
        req.volume = NormalizeDouble(max_volume_alg, InpDigit);
    }

    req.price = current_ask;

    if(!OrderSend(req, trade_result)) {
        Print("Error opening: ", GetLastError());
        ResetLastError();
        return 0;
    }

    Print("Opened ", req.volume, " lots at ", current_ask,
          " | Total volume: ", volume_of_open_trades + req.volume);
    return req.volume;
}

//+------------------------------------------------------------------+
//| Expert initialization function                                    |
//+------------------------------------------------------------------+
int OnInit() {
    m_trade.LogLevel(LOG_LEVEL_ERRORS);
    m_trade.SetAsyncMode(true);

    first_run = true;

    Print("GridSimple initialized");
    Print("Survive Down: ", InpSurviveDown, "% | Min Spacing: ", InpMinSpacing);

    return INIT_SUCCEEDED;
}

//+------------------------------------------------------------------+
//| Expert tick function                                              |
//+------------------------------------------------------------------+
void OnTick() {
    // First run initialization (from original)
    if(first_run) {
        checked_last_open_price = DBL_MIN;
        last_entry_price = 0;
        volume_of_open_trades = 0;

        // Count existing positions
        for(int i = PositionsTotal() - 1; i >= 0; i--) {
            ulong ticket = PositionGetTicket(i);
            if(ticket == 0) continue;
            if(!PositionSelectByTicket(ticket)) continue;

            if(PositionGetString(POSITION_SYMBOL) == _Symbol &&
               PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY) {
                volume_of_open_trades += PositionGetDouble(POSITION_VOLUME);
                double open_price = PositionGetDouble(POSITION_PRICE_OPEN);
                checked_last_open_price = MathMax(checked_last_open_price, open_price);
                last_entry_price = MathMax(last_entry_price, open_price);
            }
        }

        Print("Init: checked_last_open_price = ", checked_last_open_price);

        cm = InpCommission * 100;
        current_commission = _Point * cm;
        min_volume_alg = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN);
        max_volume_alg = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MAX);
        margin_stop_out_level = AccountInfoDouble(ACCOUNT_MARGIN_SO_SO);
        SymbolInfoMarginRate(_Symbol, ORDER_TYPE_BUY, initial_margin_rate, maintenance_margin_rate);
        contract_size = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);
        calc_mode = (int)SymbolInfoInteger(_Symbol, SYMBOL_TRADE_CALC_MODE);
        leverage = AccountInfoInteger(ACCOUNT_LEVERAGE);
        local_survive_down = InpSurviveDown;

        first_run = false;
    }

    // Current market data
    double current_spread = _Point * SymbolInfoInteger(_Symbol, SYMBOL_SPREAD);
    double current_spread_and_commission = current_spread + current_commission;
    double current_ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);

    double equity = AccountInfoDouble(ACCOUNT_EQUITY);
    double current_margin_level = AccountInfoDouble(ACCOUNT_MARGIN_LEVEL);
    double used_margin = AccountInfoDouble(ACCOUNT_MARGIN);

    // Check if we should open (new high)
    if(volume_of_open_trades == 0 || checked_last_open_price < current_ask) {

        // === SPACING CHECK (only addition to original) ===
        if(InpMinSpacing > 0 && last_entry_price > 0) {
            if((current_ask - last_entry_price) < InpMinSpacing) {
                // Update ATH tracking but don't enter
                checked_last_open_price = current_ask;
                return;
            }
        }
        // === END SPACING CHECK ===

        checked_last_open_price = current_ask;

        // Recalculate total volume
        volume_of_open_trades = 0;
        for(int i = PositionsTotal() - 1; i >= 0; i--) {
            ulong ticket = PositionGetTicket(i);
            if(ticket == 0) continue;
            if(!PositionSelectByTicket(ticket)) continue;

            if(PositionGetString(POSITION_SYMBOL) == _Symbol &&
               PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY) {
                volume_of_open_trades += PositionGetDouble(POSITION_VOLUME);
            }
        }

        // Calculate position size (original formula)
        double equity_at_target = (current_margin_level > 0) ?
            equity * margin_stop_out_level / current_margin_level : equity;
        double equity_difference = equity - equity_at_target;
        double price_difference = (volume_of_open_trades > 0) ?
            equity_difference / (volume_of_open_trades * contract_size) : DBL_MAX;

        double end_price = current_ask * ((100 - local_survive_down) / 100);
        double distance = current_ask - end_price;

        if(volume_of_open_trades == 0 || (current_ask - price_difference) < end_price) {
            equity_at_target = equity - volume_of_open_trades * MathAbs(distance) * contract_size;

            double trade_size = 0;
            double starting_price = current_ask;

            // Original position sizing formulas
            switch(calc_mode) {
                case SYMBOL_CALC_MODE_CFDLEVERAGE:
                    trade_size = NormalizeDouble(
                        (100 * equity * leverage - 100 * contract_size * MathAbs(distance) * volume_of_open_trades * leverage - leverage * margin_stop_out_level * used_margin) /
                        (contract_size * (100 * MathAbs(distance) * leverage + 100 * current_spread_and_commission * leverage + starting_price * initial_margin_rate * margin_stop_out_level)),
                        InpDigit);
                    break;

                case SYMBOL_CALC_MODE_FOREX:
                    trade_size = NormalizeDouble(
                        (100 * leverage * equity - 100 * contract_size * MathAbs(distance) * leverage * volume_of_open_trades - leverage * margin_stop_out_level * used_margin) /
                        (contract_size * (100 * MathAbs(distance) * leverage + 100 * current_spread_and_commission * leverage + initial_margin_rate * margin_stop_out_level)),
                        InpDigit);
                    break;

                case SYMBOL_CALC_MODE_FOREX_NO_LEVERAGE:
                    trade_size = NormalizeDouble(
                        (100 * equity - 100 * contract_size * MathAbs(distance) * volume_of_open_trades - margin_stop_out_level * used_margin) /
                        (contract_size * (100 * MathAbs(distance) + 100 * current_spread_and_commission + initial_margin_rate * margin_stop_out_level)),
                        InpDigit);
                    break;

                case SYMBOL_CALC_MODE_CFD:
                    trade_size = NormalizeDouble(
                        (100 * equity - 100 * contract_size * MathAbs(distance) * volume_of_open_trades - margin_stop_out_level * used_margin) /
                        (contract_size * (100 * MathAbs(distance) + 100 * current_spread_and_commission + starting_price * initial_margin_rate * margin_stop_out_level)),
                        InpDigit);
                    break;

                default:
                    // Fallback simple calculation
                    trade_size = NormalizeDouble(equity * 0.01 / contract_size, InpDigit);
                    break;
            }

            if(trade_size >= min_volume_alg) {
                // Check position limit
                if(PositionsTotal() < AccountInfoInteger(ACCOUNT_LIMIT_ORDERS)) {
                    double opened = OpenPosition(trade_size, current_ask);
                    if(opened > 0) {
                        volume_of_open_trades += opened;
                        last_entry_price = current_ask;
                    }
                }
            }
        }
    }
}

//+------------------------------------------------------------------+
//| Expert deinitialization function                                  |
//+------------------------------------------------------------------+
void OnDeinit(const int reason) {
    Print("GridSimple stopped. Total positions: ", PositionsTotal());
}
//+------------------------------------------------------------------+
