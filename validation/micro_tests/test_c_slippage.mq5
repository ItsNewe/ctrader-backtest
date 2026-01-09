//+------------------------------------------------------------------+
//| Test C: Slippage Distribution Analysis                          |
//| Purpose: Measure actual slippage on multiple trades             |
//| Method: Execute many trades and record slippage statistics      |
//+------------------------------------------------------------------+

#property copyright "MT5 Validation Framework"
#property version   "1.00"
#property strict

#include <Trade\Trade.mqh>

CTrade trade;

//--- Input parameters
input double InpLotSize = 0.01;           // Lot size
input int    InpTradesToExecute = 50;     // Number of trades to execute
input int    InpDelayBetweenTrades = 1;   // Delay between trades (seconds)

//--- Global variables
int g_trades_executed = 0;
datetime g_last_trade_time = 0;
int g_file_handle = INVALID_HANDLE;
bool g_test_complete = false;

//+------------------------------------------------------------------+
//| Expert initialization function                                   |
//+------------------------------------------------------------------+
int OnInit()
{
    Print("========================================");
    Print("TEST C: SLIPPAGE DISTRIBUTION ANALYSIS");
    Print("========================================");
    Print("Symbol: ", _Symbol);
    Print("Lot size: ", InpLotSize);
    Print("Trades to execute: ", InpTradesToExecute);
    Print("");
    Print("Objective: Measure actual slippage distribution");
    Print("Method: Execute multiple trades and measure price difference");
    Print("========================================");

    trade.SetExpertMagicNumber(20241206);
    trade.SetDeviationInPoints(50);  // Allow up to 50 points deviation

    // Open CSV file
    string filename = "test_c_slippage.csv";
    g_file_handle = FileOpen(filename, FILE_WRITE|FILE_CSV|FILE_ANSI);

    if (g_file_handle == INVALID_HANDLE)
    {
        Print("ERROR: Cannot create file");
        return(INIT_FAILED);
    }

    // Write header
    FileWrite(g_file_handle, "trade_num", "time", "type", "requested_price",
              "executed_price", "slippage_points", "slippage_pips",
              "spread", "comment");

    Print("Slippage recording started");
    Print("");

    return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+
//| Expert deinitialization function                                 |
//+------------------------------------------------------------------+
void OnDeinit(const int reason)
{
    if (g_file_handle != INVALID_HANDLE)
    {
        FileClose(g_file_handle);
        Print("");
        Print("========================================");
        Print("TEST C COMPLETE");
        Print("========================================");
        Print("Trades executed: ", g_trades_executed);
        Print("Results saved to: test_c_slippage.csv");
        Print("========================================");
    }

    ExportSummary();
}

//+------------------------------------------------------------------+
//| Expert tick function                                             |
//+------------------------------------------------------------------+
void OnTick()
{
    if (g_test_complete) return;

    // Check if we've executed enough trades
    if (g_trades_executed >= InpTradesToExecute)
    {
        g_test_complete = true;
        Print("Target number of trades reached");
        ExpertRemove();
        return;
    }

    // Delay between trades
    if (TimeCurrent() - g_last_trade_time < InpDelayBetweenTrades)
    {
        return;
    }

    // Alternate between BUY and SELL
    bool is_buy = (g_trades_executed % 2 == 0);

    ExecuteTrade(is_buy);
}

//+------------------------------------------------------------------+
//| Execute a trade and measure slippage                            |
//+------------------------------------------------------------------+
void ExecuteTrade(bool is_buy)
{
    double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
    double point = SymbolInfoDouble(_Symbol, SYMBOL_POINT);
    int digits = (int)SymbolInfoInteger(_Symbol, SYMBOL_DIGITS);

    double requested_price = is_buy ? ask : bid;
    double spread = ask - bid;

    string type_str = is_buy ? "BUY" : "SELL";

    // Execute trade
    bool success;
    if (is_buy)
    {
        success = trade.Buy(InpLotSize, _Symbol, ask, 0, 0, "TestC_Slippage");
    }
    else
    {
        success = trade.Sell(InpLotSize, _Symbol, bid, 0, 0, "TestC_Slippage");
    }

    if (!success)
    {
        Print("Trade ", g_trades_executed + 1, " FAILED: ", trade.ResultRetcodeDescription());
        return;
    }

    // Get actual execution price
    double executed_price = trade.ResultPrice();
    ulong order_ticket = trade.ResultOrder();

    // Calculate slippage
    double slippage = executed_price - requested_price;
    if (!is_buy) slippage = -slippage;  // For sells, reverse sign

    double slippage_points = slippage / point;
    double slippage_pips = slippage_points / 10.0;  // For 5-digit broker

    g_trades_executed++;

    Print("Trade ", g_trades_executed, " (", type_str, "): ",
          "Requested=", DoubleToString(requested_price, digits),
          " Executed=", DoubleToString(executed_price, digits),
          " Slippage=", DoubleToString(slippage_points, 1), " points");

    // Record to file
    if (g_file_handle != INVALID_HANDLE)
    {
        FileWrite(g_file_handle,
                  g_trades_executed,
                  TimeToString(TimeCurrent()),
                  type_str,
                  requested_price,
                  executed_price,
                  slippage_points,
                  slippage_pips,
                  spread,
                  trade.ResultComment());
    }

    // Close position immediately
    if (PositionSelectByTicket(order_ticket))
    {
        trade.PositionClose(order_ticket);
    }

    g_last_trade_time = TimeCurrent();
}

//+------------------------------------------------------------------+
//| Export summary statistics                                        |
//+------------------------------------------------------------------+
void ExportSummary()
{
    string filename = "test_c_summary.json";
    int file = FileOpen(filename, FILE_WRITE|FILE_TXT|FILE_ANSI);

    if (file == INVALID_HANDLE) return;

    FileWriteString(file, "{\n");
    FileWriteString(file, "  \"test\": \"Test C - Slippage Analysis\",\n");
    FileWriteString(file, "  \"symbol\": \"" + _Symbol + "\",\n");
    FileWriteString(file, "  \"trades_executed\": " + IntegerToString(g_trades_executed) + ",\n");
    FileWriteString(file, "  \"lot_size\": " + DoubleToString(InpLotSize, 2) + ",\n");
    FileWriteString(file, "  \"broker\": \"" + AccountInfoString(ACCOUNT_COMPANY) + "\",\n");
    FileWriteString(file, "  \"account\": " + IntegerToString(AccountInfoInteger(ACCOUNT_LOGIN)) + "\n");
    FileWriteString(file, "}\n");

    FileClose(file);
}
//+------------------------------------------------------------------+
