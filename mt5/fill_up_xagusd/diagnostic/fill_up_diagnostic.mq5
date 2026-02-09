//+------------------------------------------------------------------+
//| fill_up_diagnostic.mq5 - Trade-by-trade logging version          |
//| Run as single test (NOT optimization) to get detailed trade log  |
//| Compare output with C++ backtester trade-by-trade                |
//+------------------------------------------------------------------+
#property copyright "Diagnostic"
#property version   "1.00"
#property strict

#include <Trade\Trade.mqh>

input double survive = 18.0;     // Survive percentage
input double size = 1.0;         // Size multiplier (unused, kept for compatibility)
input double spacing = 1.95;     // Spacing in dollars

// Internal variables
double lowest_buy, highest_buy;
double closest_above, closest_below;
double volume_of_open_trades;
double trade_size_buy;
double current_ask, current_bid, current_spread;
double spacing_buy;
double min_volume_alg, max_volume_alg;

int trade_number = 0;
int tp_count = 0;
int last_positions_count = 0;

CTrade trade;

//+------------------------------------------------------------------+
int OnInit()
{
    spacing_buy = spacing;
    min_volume_alg = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN);
    max_volume_alg = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MAX);

    Print("=== FILL_UP DIAGNOSTIC EA ===");
    Print("Symbol: ", _Symbol);
    Print("Survive: ", survive, "%");
    Print("Spacing: $", spacing);
    Print("Min Volume: ", min_volume_alg);
    Print("Max Volume: ", max_volume_alg);
    Print("Contract Size: ", SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE));
    Print("Leverage: ", AccountInfoInteger(ACCOUNT_LEVERAGE));
    Print("Trade Calc Mode: ", SymbolInfoInteger(_Symbol, SYMBOL_TRADE_CALC_MODE));
    Print("Point: ", SymbolInfoDouble(_Symbol, SYMBOL_POINT));
    Print("Digits: ", (int)SymbolInfoInteger(_Symbol, SYMBOL_DIGITS));
    Print("Initial Balance: $", AccountInfoDouble(ACCOUNT_BALANCE));
    Print("=============================");

    return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+
void OnDeinit(const int reason)
{
    Print("=== FINAL SUMMARY ===");
    Print("Total Entries: ", trade_number);
    Print("Total TP Hits: ", tp_count);
    Print("Final Balance: $", AccountInfoDouble(ACCOUNT_BALANCE));
    Print("Final Equity: $", AccountInfoDouble(ACCOUNT_EQUITY));
    Print("=====================");
}

//+------------------------------------------------------------------+
void OnTick()
{
    new_tick_values();

    // Detect TP hits (positions decreased)
    int current_positions = PositionsTotal();
    if (current_positions < last_positions_count)
    {
        int closed = last_positions_count - current_positions;
        tp_count += closed;
        // Log each TP
        Print("TP_HIT #", tp_count, " | Bid=", current_bid,
              " | Equity=$", DoubleToString(AccountInfoDouble(ACCOUNT_EQUITY), 2),
              " | Balance=$", DoubleToString(AccountInfoDouble(ACCOUNT_BALANCE), 2),
              " | Remaining=", current_positions);
    }
    last_positions_count = current_positions;

    iterate();
    open_new();

    last_positions_count = PositionsTotal();
}

//+------------------------------------------------------------------+
void new_tick_values()
{
    current_ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    current_bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
    current_spread = current_ask - current_bid;
}

//+------------------------------------------------------------------+
void iterate()
{
    lowest_buy = DBL_MAX;
    highest_buy = DBL_MIN;
    closest_above = DBL_MAX;
    closest_below = DBL_MAX;
    volume_of_open_trades = 0;

    for (int i = PositionsTotal() - 1; i >= 0; i--)
    {
        ulong ticket = PositionGetTicket(i);
        if (ticket > 0 && PositionGetString(POSITION_SYMBOL) == _Symbol)
        {
            if (PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY)
            {
                double open_price = PositionGetDouble(POSITION_PRICE_OPEN);
                double lots = PositionGetDouble(POSITION_VOLUME);
                volume_of_open_trades += lots;
                if (open_price < lowest_buy) lowest_buy = open_price;
                if (open_price > highest_buy) highest_buy = open_price;
                if (open_price >= current_ask)
                {
                    double dist = open_price - current_ask;
                    if (dist < closest_above) closest_above = dist;
                }
                if (open_price <= current_ask)
                {
                    double dist = current_ask - open_price;
                    if (dist < closest_below) closest_below = dist;
                }
            }
        }
    }
}

//+------------------------------------------------------------------+
void sizing_buy()
{
    trade_size_buy = 0;
    int positions_total = PositionsTotal();

    double equity = AccountInfoDouble(ACCOUNT_EQUITY);
    double used_margin = AccountInfoDouble(ACCOUNT_MARGIN);
    double margin_stop_out_level = 20.0;
    double current_margin_level = (used_margin > 0) ? (equity / used_margin * 100.0) : 10000.0;

    double equity_at_target = equity * margin_stop_out_level / current_margin_level;
    double equity_difference = equity - equity_at_target;
    double price_difference = 0;
    if (volume_of_open_trades > 0)
    {
        double contract_size = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);
        price_difference = equity_difference / (volume_of_open_trades * contract_size);
    }

    double contract_size = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);
    double end_price = (positions_total == 0)
        ? current_ask * ((100.0 - survive) / 100.0)
        : highest_buy * ((100.0 - survive) / 100.0);

    double distance = current_ask - end_price;
    double number_of_trades = MathFloor(distance / spacing_buy);

    if ((positions_total == 0) || ((current_ask - price_difference) < end_price))
    {
        equity_at_target = equity - volume_of_open_trades * MathAbs(distance) * contract_size;
        double margin_level = (used_margin > 0) ? (equity_at_target / used_margin * 100.0) : 10000.0;
        double trade_size = min_volume_alg;

        if (margin_level > margin_stop_out_level)
        {
            double d_equity = contract_size * trade_size * (number_of_trades * (number_of_trades + 1) / 2.0);
            double d_spread = number_of_trades * trade_size * current_spread * contract_size;
            d_equity += d_spread;

            // Margin calculation based on trade calc mode
            double local_used_margin = 0;
            ENUM_SYMBOL_CALC_MODE calc_mode = (ENUM_SYMBOL_CALC_MODE)SymbolInfoInteger(_Symbol, SYMBOL_TRADE_CALC_MODE);
            double leverage = (double)AccountInfoInteger(ACCOUNT_LEVERAGE);

            if (calc_mode == SYMBOL_CALC_MODE_CFDLEVERAGE)
            {
                double starting_price = current_ask;
                double margin_at_start = (trade_size * contract_size * starting_price) / leverage;
                double margin_at_end = (trade_size * contract_size * end_price) / leverage;
                local_used_margin = (margin_at_start + margin_at_end) / 2.0;
                local_used_margin = number_of_trades * local_used_margin;
            }
            else // FOREX or other
            {
                local_used_margin = trade_size * contract_size / leverage;
                local_used_margin = number_of_trades * local_used_margin;
            }

            // Binary search for multiplier
            double multiplier = 0;
            double equity_backup = equity_at_target;
            double used_margin_backup = used_margin;
            double max = max_volume_alg / min_volume_alg;

            equity_at_target -= max * d_equity;
            used_margin += max * local_used_margin;
            if (margin_stop_out_level < (equity_at_target / used_margin * 100.0))
            {
                multiplier = max;
            }
            else
            {
                used_margin = used_margin_backup;
                equity_at_target = equity_backup;
                for (double increment = max; increment >= 1; increment = increment / 10)
                {
                    while (margin_stop_out_level < (equity_at_target / used_margin * 100.0))
                    {
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

            multiplier = MathMax(1.0, multiplier);
            trade_size_buy = multiplier * min_volume_alg;
            trade_size_buy = MathMin(trade_size_buy, max_volume_alg);
        }
    }
}

//+------------------------------------------------------------------+
void open_new()
{
    int positions_total = PositionsTotal();
    bool should_open = false;

    if (positions_total == 0)
    {
        should_open = true;
    }
    else if (lowest_buy >= current_ask + spacing_buy)
    {
        should_open = true;
    }
    else if (highest_buy <= current_ask - spacing_buy)
    {
        should_open = true;
    }
    else if ((closest_above >= spacing_buy) && (closest_below >= spacing_buy))
    {
        should_open = true;
    }

    if (should_open)
    {
        sizing_buy();
        if (trade_size_buy >= min_volume_alg)
        {
            double takeProfit = current_ask + current_spread + spacing_buy;
            // Round to symbol step
            double vol_step = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_STEP);
            trade_size_buy = MathFloor(trade_size_buy / vol_step) * vol_step;

            trade_number++;
            Print("ENTRY #", trade_number,
                  " | Time=", TimeToString(TimeCurrent(), TIME_DATE|TIME_SECONDS),
                  " | Ask=", DoubleToString(current_ask, (int)SymbolInfoInteger(_Symbol, SYMBOL_DIGITS)),
                  " | Bid=", DoubleToString(current_bid, (int)SymbolInfoInteger(_Symbol, SYMBOL_DIGITS)),
                  " | Lots=", DoubleToString(trade_size_buy, 2),
                  " | TP=", DoubleToString(takeProfit, (int)SymbolInfoInteger(_Symbol, SYMBOL_DIGITS)),
                  " | Spread=", DoubleToString(current_spread, (int)SymbolInfoInteger(_Symbol, SYMBOL_DIGITS)),
                  " | Equity=$", DoubleToString(AccountInfoDouble(ACCOUNT_EQUITY), 2),
                  " | PosN=", positions_total);

            trade.Buy(trade_size_buy, _Symbol, current_ask, 0.0, takeProfit, "FillUp #" + IntegerToString(trade_number));
        }
    }
}
//+------------------------------------------------------------------+
