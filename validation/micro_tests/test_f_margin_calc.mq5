//+------------------------------------------------------------------+
//| Test F: Margin Calculation Verification                         |
//| Purpose: Verify margin calculations for different position sizes|
//| Method: Open positions of varying sizes and record margin values|
//+------------------------------------------------------------------+

#property copyright "MT5 Validation Framework"
#property version   "1.00"
#property strict

#include <Trade\Trade.mqh>

CTrade trade;

//--- Input parameters
input int InpNumTests = 5;  // Number of lot sizes to test

//--- Global variables
double g_lot_sizes[] = {0.01, 0.05, 0.1, 0.5, 1.0};  // Lot sizes to test
int g_current_test = 0;
int g_file_handle = INVALID_HANDLE;
bool g_test_complete = false;

//+------------------------------------------------------------------+
//| Expert initialization function                                   |
//+------------------------------------------------------------------+
int OnInit()
{
    Print("========================================");
    Print("TEST F: MARGIN CALCULATION VERIFICATION");
    Print("========================================");
    Print("Symbol: ", _Symbol);
    Print("Lot sizes to test: ", ArraySize(g_lot_sizes));
    Print("");
    Print("Objective: Verify margin calculation formulas");
    Print("Method: Test different position sizes and record margin");
    Print("========================================");

    trade.SetExpertMagicNumber(20241206);

    // Get symbol calculation mode
    ENUM_SYMBOL_CALC_MODE calc_mode = (ENUM_SYMBOL_CALC_MODE)SymbolInfoInteger(_Symbol, SYMBOL_TRADE_CALC_MODE);
    Print("Calculation mode: ", EnumToString(calc_mode));

    // Get symbol specs
    double contract_size = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);
    int leverage = (int)AccountInfoInteger(ACCOUNT_LEVERAGE);
    double margin_rate = SymbolInfoDouble(_Symbol, SYMBOL_MARGIN_INITIAL);

    Print("Contract size: ", contract_size);
    Print("Leverage: 1:", leverage);
    Print("Margin rate: ", margin_rate);
    Print("");

    // Open CSV file
    string filename = "test_f_margin.csv";
    g_file_handle = FileOpen(filename, FILE_WRITE|FILE_CSV|FILE_ANSI);

    if (g_file_handle == INVALID_HANDLE)
    {
        Print("ERROR: Cannot create file");
        return(INIT_FAILED);
    }

    // Write header
    FileWrite(g_file_handle, "lot_size", "price", "margin_required",
              "margin_free_before", "margin_free_after", "margin_level",
              "calc_mode", "contract_size", "leverage");

    Print("Margin testing started");
    Print("");

    return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+
//| Expert deinitialization function                                 |
//+------------------------------------------------------------------+
void OnDeinit(const int reason)
{
    // Close all positions
    for(int i = PositionsTotal() - 1; i >= 0; i--)
    {
        ulong ticket = PositionGetTicket(i);
        if(PositionSelectByTicket(ticket))
        {
            if(PositionGetInteger(POSITION_MAGIC) == 20241206)
            {
                trade.PositionClose(ticket);
            }
        }
    }

    if (g_file_handle != INVALID_HANDLE)
    {
        FileClose(g_file_handle);
        Print("");
        Print("========================================");
        Print("TEST F COMPLETE");
        Print("========================================");
        Print("Tests executed: ", g_current_test);
        Print("Results saved to: test_f_margin.csv");
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

    // Check if all tests done
    if (g_current_test >= ArraySize(g_lot_sizes))
    {
        g_test_complete = true;
        Print("All margin tests complete");
        ExpertRemove();
        return;
    }

    // Wait a bit for market to settle
    static datetime last_test_time = 0;
    if (TimeCurrent() - last_test_time < 5) return;

    // Run next test
    double lot_size = g_lot_sizes[g_current_test];
    TestMarginForLotSize(lot_size);

    g_current_test++;
    last_test_time = TimeCurrent();
}

//+------------------------------------------------------------------+
//| Test margin for specific lot size                               |
//+------------------------------------------------------------------+
void TestMarginForLotSize(double lot_size)
{
    Print("Testing lot size: ", lot_size);

    // Get current margin state
    double margin_free_before = AccountInfoDouble(ACCOUNT_MARGIN_FREE);
    double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);

    // Open position
    if (!trade.Buy(lot_size, _Symbol, ask, 0, 0, "TestF_Margin"))
    {
        Print("  ERROR: Failed to open position");
        Print("  ", trade.ResultRetcodeDescription());
        return;
    }

    ulong ticket = trade.ResultOrder();

    // Wait for position to be established
    Sleep(1000);

    // Get margin info
    if (PositionSelectByTicket(ticket))
    {
        double margin_free_after = AccountInfoDouble(ACCOUNT_MARGIN_FREE);
        double margin_level = AccountInfoDouble(ACCOUNT_MARGIN_LEVEL);

        double margin_required = margin_free_before - margin_free_after;

        ENUM_SYMBOL_CALC_MODE calc_mode = (ENUM_SYMBOL_CALC_MODE)SymbolInfoInteger(_Symbol, SYMBOL_TRADE_CALC_MODE);
        double contract_size = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);
        int leverage = (int)AccountInfoInteger(ACCOUNT_LEVERAGE);

        Print("  Margin required: ", margin_required);
        Print("  Free margin: ", margin_free_before, " -> ", margin_free_after);
        Print("  Margin level: ", margin_level, "%");

        // Record to file
        if (g_file_handle != INVALID_HANDLE)
        {
            FileWrite(g_file_handle,
                      lot_size,
                      ask,
                      margin_required,
                      margin_free_before,
                      margin_free_after,
                      margin_level,
                      EnumToString(calc_mode),
                      contract_size,
                      leverage);
        }

        // Close position
        trade.PositionClose(ticket);
        Sleep(1000);

        Print("  Test complete");
    }

    Print("");
}

//+------------------------------------------------------------------+
//| Export summary                                                   |
//+------------------------------------------------------------------+
void ExportSummary()
{
    string filename = "test_f_summary.json";
    int file = FileOpen(filename, FILE_WRITE|FILE_TXT|FILE_ANSI);

    if (file == INVALID_HANDLE) return;

    ENUM_SYMBOL_CALC_MODE calc_mode = (ENUM_SYMBOL_CALC_MODE)SymbolInfoInteger(_Symbol, SYMBOL_TRADE_CALC_MODE);
    double contract_size = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);
    int leverage = (int)AccountInfoInteger(ACCOUNT_LEVERAGE);

    FileWriteString(file, "{\n");
    FileWriteString(file, "  \"test\": \"Test F - Margin Calculation\",\n");
    FileWriteString(file, "  \"symbol\": \"" + _Symbol + "\",\n");
    FileWriteString(file, "  \"tests_executed\": " + IntegerToString(g_current_test) + ",\n");
    FileWriteString(file, "  \"calculation_mode\": \"" + EnumToString(calc_mode) + "\",\n");
    FileWriteString(file, "  \"contract_size\": " + DoubleToString(contract_size, 0) + ",\n");
    FileWriteString(file, "  \"leverage\": " + IntegerToString(leverage) + ",\n");
    FileWriteString(file, "  \"broker\": \"" + AccountInfoString(ACCOUNT_COMPANY) + "\",\n");
    FileWriteString(file, "  \"account\": " + IntegerToString(AccountInfoInteger(ACCOUNT_LOGIN)) + "\n");
    FileWriteString(file, "}\n");

    FileClose(file);
}
//+------------------------------------------------------------------+
