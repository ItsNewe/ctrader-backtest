//+------------------------------------------------------------------+
//| Test E: Swap Application Timing Discovery                       |
//| Purpose: Detect exact moment when swap is applied               |
//| Method: Hold position overnight and monitor balance changes     |
//+------------------------------------------------------------------+

#property copyright "MT5 Validation Framework"
#property version   "1.00"
#property strict

#include <Trade\Trade.mqh>

CTrade trade;

//--- Input parameters
input double InpLotSize = 0.01;           // Lot size for test position
input int    InpMonitoringHours = 48;     // Monitor for 48 hours (2 days)

//--- Global variables
ulong g_position_ticket = 0;
bool g_position_opened = false;
datetime g_position_open_time = 0;
int g_file_handle = INVALID_HANDLE;
datetime g_last_check_time = 0;
double g_last_balance = 0;
double g_last_swap = 0;
int g_swap_events_detected = 0;

//+------------------------------------------------------------------+
//| Expert initialization function                                   |
//+------------------------------------------------------------------+
int OnInit()
{
    Print("========================================");
    Print("TEST E: SWAP TIMING DISCOVERY");
    Print("========================================");
    Print("Symbol: ", _Symbol);
    Print("Monitoring duration: ", InpMonitoringHours, " hours");
    Print("");
    Print("Objective: Detect exact swap application timing");
    Print("Method: Hold position and monitor swap changes");
    Print("========================================");

    trade.SetExpertMagicNumber(20241206);

    // Open CSV file
    string filename = "test_e_swap_timing.csv";
    g_file_handle = FileOpen(filename, FILE_WRITE|FILE_CSV|FILE_ANSI);

    if (g_file_handle == INVALID_HANDLE)
    {
        Print("ERROR: Cannot create file");
        return(INIT_FAILED);
    }

    // Write header
    FileWrite(g_file_handle, "timestamp", "time_str", "day_of_week",
              "balance", "swap", "swap_change", "event");

    g_last_balance = AccountInfoDouble(ACCOUNT_BALANCE);

    Print("Swap monitoring started");
    Print("");

    return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+
//| Expert deinitialization function                                 |
//+------------------------------------------------------------------+
void OnDeinit(const int reason)
{
    // Close position if still open
    if (g_position_opened && g_position_ticket > 0)
    {
        if (PositionSelectByTicket(g_position_ticket))
        {
            trade.PositionClose(g_position_ticket);
            Print("Test position closed");
        }
    }

    if (g_file_handle != INVALID_HANDLE)
    {
        FileClose(g_file_handle);
        Print("");
        Print("========================================");
        Print("TEST E COMPLETE");
        Print("========================================");
        Print("Swap events detected: ", g_swap_events_detected);
        Print("Results saved to: test_e_swap_timing.csv");
        Print("========================================");
    }

    ExportSummary();
}

//+------------------------------------------------------------------+
//| Expert tick function                                             |
//+------------------------------------------------------------------+
void OnTick()
{
    // Open position if not already open
    if (!g_position_opened)
    {
        OpenTestPosition();
        return;
    }

    // Check if monitoring period exceeded
    if (TimeCurrent() - g_position_open_time > InpMonitoringHours * 3600)
    {
        Print("Monitoring period complete");
        ExpertRemove();
        return;
    }

    // Check swap every minute
    if (TimeCurrent() - g_last_check_time >= 60)
    {
        CheckSwapChange();
        g_last_check_time = TimeCurrent();
    }
}

//+------------------------------------------------------------------+
//| Open test position                                              |
//+------------------------------------------------------------------+
void OpenTestPosition()
{
    double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);

    Print("Opening test position...");

    if (trade.Buy(InpLotSize, _Symbol, ask, 0, 0, "TestE_Swap"))
    {
        g_position_ticket = trade.ResultOrder();
        g_position_opened = true;
        g_position_open_time = TimeCurrent();

        Print("Position opened: ticket=", g_position_ticket);
        Print("Monitoring swap for ", InpMonitoringHours, " hours...");
        Print("");

        // Initial record
        RecordSwapState("position_opened");
    }
    else
    {
        Print("ERROR: Failed to open position");
        Print("  ", trade.ResultRetcodeDescription());
    }
}

//+------------------------------------------------------------------+
//| Check for swap changes                                          |
//+------------------------------------------------------------------+
void CheckSwapChange()
{
    if (!PositionSelectByTicket(g_position_ticket))
    {
        Print("ERROR: Position not found");
        return;
    }

    double current_swap = PositionGetDouble(POSITION_SWAP);
    double current_balance = AccountInfoDouble(ACCOUNT_BALANCE);

    // Detect swap change
    if (current_swap != g_last_swap)
    {
        double swap_change = current_swap - g_last_swap;

        g_swap_events_detected++;

        Print("========================================");
        Print("SWAP EVENT DETECTED!");
        Print("========================================");
        Print("Time: ", TimeToString(TimeCurrent()));
        Print("Previous swap: ", g_last_swap);
        Print("Current swap: ", current_swap);
        Print("Change: ", swap_change);
        Print("========================================");

        RecordSwapState("swap_applied");

        g_last_swap = current_swap;
    }

    g_last_balance = current_balance;
}

//+------------------------------------------------------------------+
//| Record current swap state                                       |
//+------------------------------------------------------------------+
void RecordSwapState(string event)
{
    if (!PositionSelectByTicket(g_position_ticket))
    {
        return;
    }

    double current_swap = PositionGetDouble(POSITION_SWAP);
    double current_balance = AccountInfoDouble(ACCOUNT_BALANCE);
    double swap_change = current_swap - g_last_swap;

    datetime now = TimeCurrent();
    MqlDateTime dt;
    TimeToStruct(now, dt);

    string day_names[] = {"Sunday", "Monday", "Tuesday", "Wednesday",
                         "Thursday", "Friday", "Saturday"};
    string day_of_week = day_names[dt.day_of_week];

    if (g_file_handle != INVALID_HANDLE)
    {
        FileWrite(g_file_handle,
                  now,
                  TimeToString(now),
                  day_of_week,
                  current_balance,
                  current_swap,
                  swap_change,
                  event);
    }

    g_last_swap = current_swap;
}

//+------------------------------------------------------------------+
//| Export summary                                                   |
//+------------------------------------------------------------------+
void ExportSummary()
{
    string filename = "test_e_summary.json";
    int file = FileOpen(filename, FILE_WRITE|FILE_TXT|FILE_ANSI);

    if (file == INVALID_HANDLE) return;

    int duration_hours = (int)((TimeCurrent() - g_position_open_time) / 3600);

    FileWriteString(file, "{\n");
    FileWriteString(file, "  \"test\": \"Test E - Swap Timing\",\n");
    FileWriteString(file, "  \"symbol\": \"" + _Symbol + "\",\n");
    FileWriteString(file, "  \"swap_events_detected\": " + IntegerToString(g_swap_events_detected) + ",\n");
    FileWriteString(file, "  \"duration_hours\": " + IntegerToString(duration_hours) + ",\n");
    FileWriteString(file, "  \"broker\": \"" + AccountInfoString(ACCOUNT_COMPANY) + "\",\n");
    FileWriteString(file, "  \"account\": " + IntegerToString(AccountInfoInteger(ACCOUNT_LOGIN)) + ",\n");
    FileWriteString(file, "  \"position_ticket\": " + IntegerToString(g_position_ticket) + "\n");
    FileWriteString(file, "}\n");

    FileClose(file);
}
//+------------------------------------------------------------------+
