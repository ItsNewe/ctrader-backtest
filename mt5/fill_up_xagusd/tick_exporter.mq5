//+------------------------------------------------------------------+
//| tick_exporter.mq5 - Export exact tester tick data to CSV          |
//| Run as EA in Strategy Tester with "Every tick based on real ticks"|
//| Exports the EXACT tick data the tester uses internally            |
//+------------------------------------------------------------------+
#property copyright "TickExporter"
#property version   "1.00"
#property strict

input string OutputFileName = "XAGUSD_TESTER_TICKS.csv";  // Output file name

int file_handle = INVALID_HANDLE;
long tick_count = 0;
long flush_interval = 100000;  // Flush every 100k ticks

//+------------------------------------------------------------------+
int OnInit()
{
    // Open file for writing in the tester's Files folder
    file_handle = FileOpen(OutputFileName, FILE_WRITE | FILE_CSV | FILE_ANSI, '\t');
    if (file_handle == INVALID_HANDLE)
    {
        Print("ERROR: Cannot open file ", OutputFileName, " Error: ", GetLastError());
        return(INIT_FAILED);
    }

    // Write header
    FileWrite(file_handle, "Timestamp", "Bid", "Ask", "Volume", "Flags");

    Print("=== TICK EXPORTER STARTED ===");
    Print("Symbol: ", _Symbol);
    Print("Output: ", OutputFileName);
    Print("Digits: ", (int)SymbolInfoInteger(_Symbol, SYMBOL_DIGITS));
    Print("Point: ", SymbolInfoDouble(_Symbol, SYMBOL_POINT));
    Print("============================");

    return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+
void OnDeinit(const int reason)
{
    if (file_handle != INVALID_HANDLE)
    {
        FileFlush(file_handle);
        FileClose(file_handle);
    }

    Print("=== TICK EXPORTER FINISHED ===");
    Print("Total ticks exported: ", tick_count);
    Print("File: ", OutputFileName);
    Print("==============================");
}

//+------------------------------------------------------------------+
void OnTick()
{
    MqlTick tick;
    if (!SymbolInfoTick(_Symbol, tick))
        return;

    tick_count++;

    // Format timestamp with milliseconds
    // tick.time_msc contains milliseconds since epoch
    datetime tick_time = (datetime)(tick.time_msc / 1000);
    int ms = (int)(tick.time_msc % 1000);

    string timestamp = TimeToString(tick_time, TIME_DATE | TIME_SECONDS);
    // Replace date separators: "2025.01.02 01:00:02" format
    timestamp += "." + IntegerToString(ms, 3, '0');

    // Write tick data
    int digits = (int)SymbolInfoInteger(_Symbol, SYMBOL_DIGITS);
    FileWrite(file_handle,
              timestamp,
              DoubleToString(tick.bid, digits),
              DoubleToString(tick.ask, digits),
              IntegerToString(tick.volume),
              IntegerToString(tick.flags));

    // Periodic flush to prevent data loss
    if (tick_count % flush_interval == 0)
    {
        FileFlush(file_handle);
        Print("Exported ", tick_count, " ticks... Bid=", tick.bid, " Ask=", tick.ask);
    }
}
//+------------------------------------------------------------------+
