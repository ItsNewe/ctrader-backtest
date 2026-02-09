//+------------------------------------------------------------------+
//| QuerySymbolInfo.mq5                                               |
//| Script to query and display broker symbol specifications          |
//+------------------------------------------------------------------+
#property copyright "Backtest Framework"
#property version   "1.00"
#property script_show_inputs

input string InpSymbol1 = "XAUUSD";  // Symbol 1
input string InpSymbol2 = "XAGUSD";  // Symbol 2

void OnStart()
{
    Print("========================================");
    Print("     BROKER SYMBOL SPECIFICATIONS      ");
    Print("========================================");

    QuerySymbol(InpSymbol1);
    QuerySymbol(InpSymbol2);

    Print("========================================");
    Print("Copy these values to your backtest config");
}

void QuerySymbol(string symbol)
{
    if(!SymbolSelect(symbol, true))
    {
        Print("ERROR: Symbol ", symbol, " not available");
        return;
    }

    Print("");
    Print("=== ", symbol, " ===");

    // Volume limits
    double vol_min = SymbolInfoDouble(symbol, SYMBOL_VOLUME_MIN);
    double vol_max = SymbolInfoDouble(symbol, SYMBOL_VOLUME_MAX);
    double vol_step = SymbolInfoDouble(symbol, SYMBOL_VOLUME_STEP);

    Print("Volume Min:  ", DoubleToString(vol_min, 2), " lots");
    Print("Volume Max:  ", DoubleToString(vol_max, 2), " lots");
    Print("Volume Step: ", DoubleToString(vol_step, 2), " lots");

    // Contract specs
    double contract_size = SymbolInfoDouble(symbol, SYMBOL_TRADE_CONTRACT_SIZE);
    double tick_size = SymbolInfoDouble(symbol, SYMBOL_TRADE_TICK_SIZE);
    double tick_value = SymbolInfoDouble(symbol, SYMBOL_TRADE_TICK_VALUE);
    double point = SymbolInfoDouble(symbol, SYMBOL_POINT);
    int digits = (int)SymbolInfoInteger(symbol, SYMBOL_DIGITS);

    Print("Contract Size: ", DoubleToString(contract_size, 2));
    Print("Tick Size:     ", DoubleToString(tick_size, digits));
    Print("Tick Value:    ", DoubleToString(tick_value, 2), " (per lot)");
    Print("Point:         ", DoubleToString(point, digits));
    Print("Digits:        ", digits);

    // Margin
    double margin_initial = SymbolInfoDouble(symbol, SYMBOL_MARGIN_INITIAL);
    double margin_maint = SymbolInfoDouble(symbol, SYMBOL_MARGIN_MAINTENANCE);

    // Calculate effective leverage
    double ask = SymbolInfoDouble(symbol, SYMBOL_ASK);
    double margin_required = 0.0;
    if(OrderCalcMargin(ORDER_TYPE_BUY, symbol, 1.0, ask, margin_required))
    {
        double effective_leverage = (ask * contract_size) / margin_required;
        Print("Effective Leverage: ", DoubleToString(effective_leverage, 1), ":1");
        Print("Margin per 1 lot:   $", DoubleToString(margin_required, 2));
    }

    // Swap rates
    double swap_long = SymbolInfoDouble(symbol, SYMBOL_SWAP_LONG);
    double swap_short = SymbolInfoDouble(symbol, SYMBOL_SWAP_SHORT);
    ENUM_SYMBOL_SWAP_MODE swap_mode = (ENUM_SYMBOL_SWAP_MODE)SymbolInfoInteger(symbol, SYMBOL_SWAP_MODE);
    int swap_3days = (int)SymbolInfoInteger(symbol, SYMBOL_SWAP_ROLLOVER3DAYS);

    Print("Swap Long:   ", DoubleToString(swap_long, 2));
    Print("Swap Short:  ", DoubleToString(swap_short, 2));
    Print("Swap Mode:   ", EnumToString(swap_mode));
    Print("Swap 3-day:  ", DayOfWeekToString(swap_3days));

    // Current price
    Print("Current Ask: ", DoubleToString(ask, digits));
    Print("Current Bid: ", DoubleToString(SymbolInfoDouble(symbol, SYMBOL_BID), digits));

    // Output format for C++ config
    Print("");
    Print("// C++ Config for ", symbol, ":");
    Print("config.symbol = \"", symbol, "\";");
    Print("config.contract_size = ", DoubleToString(contract_size, 1), ";");
    Print("config.pip_size = ", DoubleToString(point * (digits == 3 || digits == 5 ? 10 : 1), digits > 2 ? 3 : 2), ";");
    Print("config.swap_long = ", DoubleToString(swap_long, 2), ";");
    Print("config.swap_short = ", DoubleToString(swap_short, 2), ";");
    Print("double max_volume = ", DoubleToString(vol_max, 2), ";  // Broker limit");
}

string DayOfWeekToString(int day)
{
    switch(day)
    {
        case 0: return "Sunday";
        case 1: return "Monday";
        case 2: return "Tuesday";
        case 3: return "Wednesday";
        case 4: return "Thursday";
        case 5: return "Friday";
        case 6: return "Saturday";
        default: return "Unknown";
    }
}
//+------------------------------------------------------------------+
