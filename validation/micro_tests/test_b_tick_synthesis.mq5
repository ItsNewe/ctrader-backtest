//+------------------------------------------------------------------+
//| Test B: Tick Synthesis Pattern Discovery                        |
//| Purpose: Record how MT5 generates synthetic ticks from OHLC     |
//| Method: Log all ticks during bar formation for pattern analysis |
//+------------------------------------------------------------------+

#property copyright "MT5 Validation Framework"
#property version   "1.00"
#property strict

//--- Input parameters
input int    InpBarsToRecord = 100;      // Number of bars to record
input string InpSymbol = "EURUSD";        // Symbol to test
input ENUM_TIMEFRAMES InpTimeframe = PERIOD_H1;  // Timeframe

//--- Global variables
int g_bars_recorded = 0;
int g_file_handle = INVALID_HANDLE;
datetime g_current_bar_time = 0;
int g_tick_count = 0;
int g_total_ticks = 0;

//+------------------------------------------------------------------+
//| Expert initialization function                                   |
//+------------------------------------------------------------------+
int OnInit()
{
    Print("========================================");
    Print("TEST B: TICK SYNTHESIS PATTERN DISCOVERY");
    Print("========================================");
    Print("Symbol: ", _Symbol);
    Print("Timeframe: ", EnumToString(InpTimeframe));
    Print("Bars to record: ", InpBarsToRecord);
    Print("");
    Print("Objective: Discover MT5's tick generation pattern from OHLC");
    Print("Method: Record every tick during bar formation");
    Print("========================================");

    // Open CSV file for tick recording
    string filename = "test_b_ticks.csv";
    g_file_handle = FileOpen(filename, FILE_WRITE|FILE_CSV|FILE_ANSI);

    if (g_file_handle == INVALID_HANDLE)
    {
        Print("ERROR: Cannot create file: ", filename);
        Print("Error code: ", GetLastError());
        return(INIT_FAILED);
    }

    // Write header
    FileWrite(g_file_handle, "bar_time", "bar_index", "tick_index", "time_msc",
              "bid", "ask", "last", "volume", "flags",
              "bar_open", "bar_high", "bar_low", "bar_close");

    Print("Tick recording started");
    Print("File: ", filename);
    Print("");
    Print("Recording ticks for ", InpBarsToRecord, " bars...");

    g_current_bar_time = iTime(_Symbol, InpTimeframe, 0);

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
        Print("TEST B COMPLETE");
        Print("========================================");
        Print("Bars recorded: ", g_bars_recorded);
        Print("Total ticks recorded: ", g_total_ticks);
        Print("Average ticks per bar: ", (g_bars_recorded > 0 ? g_total_ticks / g_bars_recorded : 0));
        Print("Results saved to: test_b_ticks.csv");
        Print("========================================");
    }

    // Export summary
    ExportSummary();
}

//+------------------------------------------------------------------+
//| Expert tick function                                             |
//+------------------------------------------------------------------+
void OnTick()
{
    // Stop after recording enough bars
    if (g_bars_recorded >= InpBarsToRecord)
    {
        if (g_file_handle != INVALID_HANDLE)
        {
            FileClose(g_file_handle);
            g_file_handle = INVALID_HANDLE;
            Print("Recording complete - ", InpBarsToRecord, " bars recorded");
            ExpertRemove();  // Stop EA
        }
        return;
    }

    // Get current tick
    MqlTick tick;
    if (!SymbolInfoTick(_Symbol, tick))
    {
        Print("ERROR: Cannot get tick data");
        return;
    }

    // Detect new bar
    datetime bar_time = iTime(_Symbol, InpTimeframe, 0);
    if (bar_time != g_current_bar_time)
    {
        // New bar started
        if (g_current_bar_time != 0)  // Skip first bar detection
        {
            Print("Bar ", g_bars_recorded, " complete: ", g_tick_count, " ticks");
        }

        g_current_bar_time = bar_time;
        g_bars_recorded++;
        g_tick_count = 0;

        if (g_bars_recorded <= InpBarsToRecord)
        {
            Print("Recording bar ", g_bars_recorded, " / ", InpBarsToRecord,
                  " (", TimeToString(bar_time), ")");
        }
    }

    // Get current bar OHLC
    double bar_open = iOpen(_Symbol, InpTimeframe, 0);
    double bar_high = iHigh(_Symbol, InpTimeframe, 0);
    double bar_low = iLow(_Symbol, InpTimeframe, 0);
    double bar_close = iClose(_Symbol, InpTimeframe, 0);

    // Record tick to file
    if (g_file_handle != INVALID_HANDLE && g_bars_recorded <= InpBarsToRecord)
    {
        FileWrite(g_file_handle,
                  TimeToString(bar_time),
                  g_bars_recorded - 1,  // 0-indexed
                  g_tick_count,
                  tick.time_msc,
                  tick.bid,
                  tick.ask,
                  tick.last,
                  tick.volume,
                  tick.flags,
                  bar_open,
                  bar_high,
                  bar_low,
                  bar_close);

        g_tick_count++;
        g_total_ticks++;
    }
}

//+------------------------------------------------------------------+
//| Export summary statistics                                        |
//+------------------------------------------------------------------+
void ExportSummary()
{
    string filename = "test_b_summary.json";
    int file = FileOpen(filename, FILE_WRITE|FILE_TXT|FILE_ANSI);

    if (file == INVALID_HANDLE)
    {
        Print("ERROR: Cannot create summary file");
        return;
    }

    // Write JSON summary
    FileWriteString(file, "{\n");
    FileWriteString(file, "  \"test\": \"Test B - Tick Synthesis\",\n");
    FileWriteString(file, "  \"symbol\": \"" + _Symbol + "\",\n");
    FileWriteString(file, "  \"timeframe\": \"" + EnumToString(InpTimeframe) + "\",\n");
    FileWriteString(file, "  \"bars_recorded\": " + IntegerToString(g_bars_recorded) + ",\n");
    FileWriteString(file, "  \"total_ticks\": " + IntegerToString(g_total_ticks) + ",\n");

    double avg_ticks = (g_bars_recorded > 0) ? ((double)g_total_ticks / g_bars_recorded) : 0;
    FileWriteString(file, "  \"avg_ticks_per_bar\": " + DoubleToString(avg_ticks, 2) + ",\n");

    FileWriteString(file, "  \"broker\": \"" + AccountInfoString(ACCOUNT_COMPANY) + "\",\n");
    FileWriteString(file, "  \"account\": " + IntegerToString(AccountInfoInteger(ACCOUNT_LOGIN)) + ",\n");
    FileWriteString(file, "  \"mt5_build\": " + IntegerToString(TerminalInfoInteger(TERMINAL_BUILD)) + "\n");
    FileWriteString(file, "}\n");

    FileClose(file);

    Print("Summary exported to: ", filename);
}
//+------------------------------------------------------------------+
