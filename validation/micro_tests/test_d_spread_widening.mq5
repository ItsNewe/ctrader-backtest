//+------------------------------------------------------------------+
//| Test D: Spread Widening Pattern Discovery                       |
//| Purpose: Monitor spread behavior during different market conditions |
//| Method: Log spread continuously and correlate with volatility   |
//+------------------------------------------------------------------+

#property copyright "MT5 Validation Framework"
#property version   "1.00"
#property strict

//--- Input parameters
input int    InpRecordingDuration = 3600;  // Recording duration (seconds) - 1 hour
input int    InpSamplingInterval = 5;      // Sample every N seconds

//--- Global variables
datetime g_start_time = 0;
datetime g_last_sample_time = 0;
int g_file_handle = INVALID_HANDLE;
int g_samples_recorded = 0;
int g_atr_handle = INVALID_HANDLE;

//+------------------------------------------------------------------+
//| Expert initialization function                                   |
//+------------------------------------------------------------------+
int OnInit()
{
    Print("========================================");
    Print("TEST D: SPREAD WIDENING PATTERN DISCOVERY");
    Print("========================================");
    Print("Symbol: ", _Symbol);
    Print("Duration: ", InpRecordingDuration, " seconds");
    Print("Sampling interval: ", InpSamplingInterval, " seconds");
    Print("");
    Print("Objective: Discover spread widening patterns");
    Print("Method: Monitor spread vs volatility over time");
    Print("========================================");

    // Create ATR indicator for volatility measurement
    g_atr_handle = iATR(_Symbol, PERIOD_M1, 14);
    if (g_atr_handle == INVALID_HANDLE)
    {
        Print("ERROR: Cannot create ATR indicator");
        return(INIT_FAILED);
    }

    // Open CSV file
    string filename = "test_d_spread.csv";
    g_file_handle = FileOpen(filename, FILE_WRITE|FILE_CSV|FILE_ANSI);

    if (g_file_handle == INVALID_HANDLE)
    {
        Print("ERROR: Cannot create file");
        return(INIT_FAILED);
    }

    // Write header
    FileWrite(g_file_handle, "timestamp", "time_str", "spread_points", "spread_pips",
              "bid", "ask", "atr", "tick_volume", "price_change");

    g_start_time = TimeCurrent();
    g_last_sample_time = 0;

    Print("Spread monitoring started");
    Print("Recording for ", InpRecordingDuration, " seconds...");
    Print("");

    return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+
//| Expert deinitialization function                                 |
//+------------------------------------------------------------------+
void OnDeinit(const int reason)
{
    if (g_atr_handle != INVALID_HANDLE)
    {
        IndicatorRelease(g_atr_handle);
    }

    if (g_file_handle != INVALID_HANDLE)
    {
        FileClose(g_file_handle);
        Print("");
        Print("========================================");
        Print("TEST D COMPLETE");
        Print("========================================");
        Print("Samples recorded: ", g_samples_recorded);
        Print("Duration: ", (int)(TimeCurrent() - g_start_time), " seconds");
        Print("Results saved to: test_d_spread.csv");
        Print("========================================");
    }

    ExportSummary();
}

//+------------------------------------------------------------------+
//| Expert tick function                                             |
//+------------------------------------------------------------------+
void OnTick()
{
    // Check if recording duration exceeded
    if (TimeCurrent() - g_start_time >= InpRecordingDuration)
    {
        Print("Recording duration reached");
        ExpertRemove();
        return;
    }

    // Sample at intervals
    if (TimeCurrent() - g_last_sample_time < InpSamplingInterval)
    {
        return;
    }

    RecordSpreadSample();
    g_last_sample_time = TimeCurrent();
}

//+------------------------------------------------------------------+
//| Record a spread sample                                          |
//+------------------------------------------------------------------+
void RecordSpreadSample()
{
    // Get current spread
    double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
    double spread = ask - bid;
    double point = SymbolInfoDouble(_Symbol, SYMBOL_POINT);

    int spread_points = (int)MathRound(spread / point);
    double spread_pips = spread_points / 10.0;

    // Get ATR (volatility)
    double atr_buffer[];
    ArraySetAsSeries(atr_buffer, true);

    double atr_value = 0;
    if (CopyBuffer(g_atr_handle, 0, 0, 1, atr_buffer) > 0)
    {
        atr_value = atr_buffer[0];
    }

    // Get tick volume
    long tick_volume = iVolume(_Symbol, PERIOD_M1, 0);

    // Calculate price change (last bar close vs current)
    static double last_close = 0;
    double current_close = iClose(_Symbol, PERIOD_M1, 0);
    double price_change = (last_close != 0) ? (current_close - last_close) : 0;
    last_close = current_close;

    g_samples_recorded++;

    // Record to file
    if (g_file_handle != INVALID_HANDLE)
    {
        FileWrite(g_file_handle,
                  TimeCurrent(),
                  TimeToString(TimeCurrent()),
                  spread_points,
                  spread_pips,
                  bid,
                  ask,
                  atr_value,
                  tick_volume,
                  price_change);
    }

    // Periodic progress
    if (g_samples_recorded % 20 == 0)
    {
        int elapsed = (int)(TimeCurrent() - g_start_time);
        int remaining = InpRecordingDuration - elapsed;
        Print("Samples: ", g_samples_recorded,
              " | Elapsed: ", elapsed, "s",
              " | Remaining: ", remaining, "s",
              " | Current spread: ", spread_points, " points");
    }
}

//+------------------------------------------------------------------+
//| Export summary                                                   |
//+------------------------------------------------------------------+
void ExportSummary()
{
    string filename = "test_d_summary.json";
    int file = FileOpen(filename, FILE_WRITE|FILE_TXT|FILE_ANSI);

    if (file == INVALID_HANDLE) return;

    int duration = (int)(TimeCurrent() - g_start_time);

    FileWriteString(file, "{\n");
    FileWriteString(file, "  \"test\": \"Test D - Spread Widening\",\n");
    FileWriteString(file, "  \"symbol\": \"" + _Symbol + "\",\n");
    FileWriteString(file, "  \"samples_recorded\": " + IntegerToString(g_samples_recorded) + ",\n");
    FileWriteString(file, "  \"duration_seconds\": " + IntegerToString(duration) + ",\n");
    FileWriteString(file, "  \"sampling_interval\": " + IntegerToString(InpSamplingInterval) + ",\n");
    FileWriteString(file, "  \"broker\": \"" + AccountInfoString(ACCOUNT_COMPANY) + "\",\n");
    FileWriteString(file, "  \"account\": " + IntegerToString(AccountInfoInteger(ACCOUNT_LOGIN)) + "\n");
    FileWriteString(file, "}\n");

    FileClose(file);
}
//+------------------------------------------------------------------+
