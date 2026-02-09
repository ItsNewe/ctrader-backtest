//+------------------------------------------------------------------+
//| Test A: SL/TP Execution Order Discovery                         |
//| Purpose: Determine which executes first when both SL and TP     |
//|          are triggered on the same tick                         |
//| Method: Create scenario where price movement hits both levels   |
//+------------------------------------------------------------------+

#property copyright "MT5 Validation Framework"
#property version   "1.00"
#property strict

#include <Trade\Trade.mqh>
#include <Trade\PositionInfo.mqh>
#include <Trade\AccountInfo.mqh>

CTrade trade;
CPositionInfo position;
CAccountInfo account;

//--- Input parameters
input double InpLotSize = 0.01;           // Position size
input int    InpSLPoints = 100;           // Stop Loss in points
input int    InpTPPoints = 100;           // Take Profit in points
input int    InpMagicNumber = 20241206;   // Magic number for identification

//--- Global variables
ulong g_test_ticket = 0;
bool g_position_opened = false;
bool g_result_logged = false;
datetime g_entry_time = 0;
double g_entry_price = 0;
double g_sl_price = 0;
double g_tp_price = 0;

//+------------------------------------------------------------------+
//| Expert initialization function                                   |
//+------------------------------------------------------------------+
int OnInit()
{
    Print("========================================");
    Print("TEST A: SL/TP EXECUTION ORDER DISCOVERY");
    Print("========================================");
    Print("Symbol: ", _Symbol);
    Print("Broker: ", account.Company());
    Print("Account: ", account.Login());
    Print("Balance: ", account.Balance());
    Print("");
    Print("Test Parameters:");
    Print("  Lot Size: ", InpLotSize);
    Print("  SL Points: ", InpSLPoints);
    Print("  TP Points: ", InpTPPoints);
    Print("");
    Print("Objective: Discover which executes first when both triggered");
    Print("Method: Wait for volatile price movement that hits both levels");
    Print("========================================");

    trade.SetExpertMagicNumber(InpMagicNumber);
    trade.SetDeviationInPoints(10);

    return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+
//| Expert deinitialization function                                 |
//+------------------------------------------------------------------+
void OnDeinit(const int reason)
{
    Print("========================================");
    Print("TEST A COMPLETE");
    Print("Reason: ", reason);

    if (!g_result_logged)
    {
        Print("WARNING: Test did not complete successfully");
        Print("Position may not have been closed or result not logged");
    }

    Print("========================================");
}

//+------------------------------------------------------------------+
//| Expert tick function                                             |
//+------------------------------------------------------------------+
void OnTick()
{
    //--- Phase 1: Open position if not already open
    if (!g_position_opened)
    {
        OpenTestPosition();
        return;
    }

    //--- Phase 2: Monitor position and log result when closed
    if (g_position_opened && !g_result_logged)
    {
        CheckPositionStatus();
    }
}

//+------------------------------------------------------------------+
//| Open test position with SL and TP                                |
//+------------------------------------------------------------------+
void OpenTestPosition()
{
    //--- Get current prices
    double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
    double point = SymbolInfoDouble(_Symbol, SYMBOL_POINT);
    int digits = (int)SymbolInfoInteger(_Symbol, SYMBOL_DIGITS);

    //--- Calculate SL and TP levels
    double sl = NormalizeDouble(bid - InpSLPoints * point, digits);
    double tp = NormalizeDouble(bid + InpTPPoints * point, digits);

    //--- Open BUY position
    Print("Opening test position...");
    Print("  Entry (ASK): ", ask);
    Print("  Stop Loss:   ", sl, " (", InpSLPoints, " points below BID)");
    Print("  Take Profit: ", tp, " (", InpTPPoints, " points above BID)");

    if (trade.Buy(InpLotSize, _Symbol, ask, sl, tp, "TestA_SL_TP_Order"))
    {
        g_test_ticket = trade.ResultOrder();
        g_position_opened = true;
        g_entry_time = TimeCurrent();
        g_entry_price = ask;
        g_sl_price = sl;
        g_tp_price = tp;

        Print("SUCCESS: Position opened");
        Print("  Ticket: ", g_test_ticket);
        Print("  Entry Time: ", TimeToString(g_entry_time));
        Print("");
        Print("Now waiting for position to close via SL or TP...");
    }
    else
    {
        Print("ERROR: Failed to open position");
        Print("  Error code: ", GetLastError());
        Print("  Error desc: ", trade.ResultRetcodeDescription());
    }
}

//+------------------------------------------------------------------+
//| Check if position still exists, log result if closed            |
//+------------------------------------------------------------------+
void CheckPositionStatus()
{
    //--- Check if position still exists
    if (PositionSelectByTicket(g_test_ticket))
    {
        //--- Position still open - could add monitoring here
        return;
    }

    //--- Position closed - analyze why
    Print("");
    Print("========================================");
    Print("POSITION CLOSED - ANALYZING RESULT");
    Print("========================================");

    AnalyzeClosedPosition();
    g_result_logged = true;
}

//+------------------------------------------------------------------+
//| Analyze closed position from history                            |
//+------------------------------------------------------------------+
void AnalyzeClosedPosition()
{
    //--- Select history for analysis
    if (!HistorySelectByPosition(g_test_ticket))
    {
        Print("ERROR: Cannot select position history");
        return;
    }

    //--- Find the exit deal
    int total_deals = HistoryDealsTotal();
    Print("Total deals in history: ", total_deals);

    for (int i = 0; i < total_deals; i++)
    {
        ulong deal_ticket = HistoryDealGetTicket(i);

        if (deal_ticket > 0)
        {
            ulong deal_position = HistoryDealGetInteger(deal_ticket, DEAL_POSITION_ID);

            //--- Check if this deal belongs to our position
            if (deal_position == g_test_ticket)
            {
                ENUM_DEAL_ENTRY deal_entry = (ENUM_DEAL_ENTRY)HistoryDealGetInteger(deal_ticket, DEAL_ENTRY);

                //--- We want the exit deal
                if (deal_entry == DEAL_ENTRY_OUT)
                {
                    LogDealDetails(deal_ticket);
                    DetermineExitReason(deal_ticket);
                    ExportResults(deal_ticket);
                    return;
                }
            }
        }
    }

    Print("WARNING: Exit deal not found in history");
}

//+------------------------------------------------------------------+
//| Log detailed information about the exit deal                     |
//+------------------------------------------------------------------+
void LogDealDetails(ulong deal_ticket)
{
    datetime deal_time = (datetime)HistoryDealGetInteger(deal_ticket, DEAL_TIME);
    double deal_price = HistoryDealGetDouble(deal_ticket, DEAL_PRICE);
    double deal_profit = HistoryDealGetDouble(deal_ticket, DEAL_PROFIT);
    double deal_commission = HistoryDealGetDouble(deal_ticket, DEAL_COMMISSION);
    double deal_swap = HistoryDealGetDouble(deal_ticket, DEAL_SWAP);
    string deal_comment = HistoryDealGetString(deal_ticket, DEAL_COMMENT);

    Print("Exit Deal Details:");
    Print("  Deal Ticket: ", deal_ticket);
    Print("  Exit Time: ", TimeToString(deal_time));
    Print("  Exit Price: ", deal_price);
    Print("  Profit: ", deal_profit);
    Print("  Commission: ", deal_commission);
    Print("  Swap: ", deal_swap);
    Print("  Comment: ", deal_comment);
    Print("");
    Print("Position Details:");
    Print("  Entry Time: ", TimeToString(g_entry_time));
    Print("  Entry Price: ", g_entry_price);
    Print("  Stop Loss: ", g_sl_price);
    Print("  Take Profit: ", g_tp_price);
    Print("  Duration: ", (int)(deal_time - g_entry_time), " seconds");
}

//+------------------------------------------------------------------+
//| Determine exit reason from deal data                            |
//+------------------------------------------------------------------+
void DetermineExitReason(ulong deal_ticket)
{
    double deal_price = HistoryDealGetDouble(deal_ticket, DEAL_PRICE);
    double deal_profit = HistoryDealGetDouble(deal_ticket, DEAL_PROFIT);
    string deal_comment = HistoryDealGetString(deal_ticket, DEAL_COMMENT);

    string exit_reason = "UNKNOWN";
    string confidence = "LOW";

    Print("");
    Print("DETERMINING EXIT REASON:");
    Print("----------------------------------------");

    //--- Method 1: Check comment for SL/TP keywords
    string comment_lower = deal_comment;
    StringToLower(comment_lower);

    bool comment_has_sl = (StringFind(comment_lower, "sl") >= 0 ||
                          StringFind(comment_lower, "stop") >= 0);
    bool comment_has_tp = (StringFind(comment_lower, "tp") >= 0 ||
                          StringFind(comment_lower, "profit") >= 0 ||
                          StringFind(comment_lower, "take") >= 0);

    Print("Comment Analysis:");
    Print("  Comment: '", deal_comment, "'");
    Print("  Contains SL keywords: ", comment_has_sl ? "YES" : "NO");
    Print("  Contains TP keywords: ", comment_has_tp ? "YES" : "NO");

    //--- Method 2: Check profit sign
    bool profit_negative = (deal_profit < -0.01);  // Allow small tolerance
    bool profit_positive = (deal_profit > 0.01);

    Print("");
    Print("Profit Analysis:");
    Print("  Profit: ", deal_profit);
    Print("  Negative (suggests SL): ", profit_negative ? "YES" : "NO");
    Print("  Positive (suggests TP): ", profit_positive ? "YES" : "NO");

    //--- Method 3: Check price proximity to SL/TP
    double sl_distance = MathAbs(deal_price - g_sl_price);
    double tp_distance = MathAbs(deal_price - g_tp_price);
    double point = SymbolInfoDouble(_Symbol, SYMBOL_POINT);

    bool price_near_sl = (sl_distance < 10 * point);  // Within 10 points
    bool price_near_tp = (tp_distance < 10 * point);

    Print("");
    Print("Price Analysis:");
    Print("  Exit Price: ", deal_price);
    Print("  Distance from SL: ", sl_distance, " (", sl_distance/point, " points)");
    Print("  Distance from TP: ", tp_distance, " (", tp_distance/point, " points)");
    Print("  Near SL (<10pts): ", price_near_sl ? "YES" : "NO");
    Print("  Near TP (<10pts): ", price_near_tp ? "YES" : "NO");

    //--- Determine exit reason with confidence
    Print("");
    Print("CONCLUSION:");
    Print("----------------------------------------");

    int sl_votes = 0;
    int tp_votes = 0;

    if (comment_has_sl) sl_votes++;
    if (comment_has_tp) tp_votes++;
    if (profit_negative) sl_votes++;
    if (profit_positive) tp_votes++;
    if (price_near_sl) sl_votes++;
    if (price_near_tp) tp_votes++;

    if (sl_votes > tp_votes)
    {
        exit_reason = "SL";
        confidence = (sl_votes >= 3) ? "HIGH" : "MEDIUM";
    }
    else if (tp_votes > sl_votes)
    {
        exit_reason = "TP";
        confidence = (tp_votes >= 3) ? "HIGH" : "MEDIUM";
    }
    else
    {
        exit_reason = "UNKNOWN";
        confidence = "LOW";
        Print("WARNING: Ambiguous exit reason (SL votes: ", sl_votes, ", TP votes: ", tp_votes, ")");
    }

    Print("");
    Print("*** EXIT REASON: ", exit_reason, " ***");
    Print("*** CONFIDENCE: ", confidence, " ***");
    Print("*** SL Votes: ", sl_votes, ", TP Votes: ", tp_votes, " ***");
    Print("");

    //--- Store for export
    GlobalVariableSet("TestA_ExitReason", exit_reason == "SL" ? 1 : (exit_reason == "TP" ? 2 : 0));
    GlobalVariableSet("TestA_Confidence", confidence == "HIGH" ? 3 : (confidence == "MEDIUM" ? 2 : 1));
}

//+------------------------------------------------------------------+
//| Export results to file for validation framework                  |
//+------------------------------------------------------------------+
void ExportResults(ulong deal_ticket)
{
    string filename = "test_a_mt5_result.csv";
    int file = FileOpen(filename, FILE_WRITE|FILE_CSV|FILE_ANSI);

    if (file == INVALID_HANDLE)
    {
        Print("ERROR: Cannot create result file: ", filename);
        Print("Error code: ", GetLastError());
        return;
    }

    //--- Get all data
    datetime deal_time = (datetime)HistoryDealGetInteger(deal_ticket, DEAL_TIME);
    double deal_price = HistoryDealGetDouble(deal_ticket, DEAL_PRICE);
    double deal_profit = HistoryDealGetDouble(deal_ticket, DEAL_PROFIT);
    string deal_comment = HistoryDealGetString(deal_ticket, DEAL_COMMENT);

    double exit_reason_code = GlobalVariableGet("TestA_ExitReason");
    string exit_reason = (exit_reason_code == 1) ? "SL" : ((exit_reason_code == 2) ? "TP" : "UNKNOWN");

    //--- Write header
    FileWrite(file, "ticket", "entry_time", "entry_price", "exit_time", "exit_price",
              "sl_price", "tp_price", "profit", "comment", "exit_reason", "broker", "account");

    //--- Write data
    FileWrite(file, g_test_ticket, TimeToString(g_entry_time), g_entry_price,
              TimeToString(deal_time), deal_price, g_sl_price, g_tp_price,
              deal_profit, deal_comment, exit_reason,
              account.Company(), account.Login());

    FileClose(file);

    Print("========================================");
    Print("RESULTS EXPORTED TO: ", filename);
    Print("========================================");
    Print("");
    Print("Next Step: Copy this file to validation/mt5/ directory");
    Print("Then run our C++ backtest with same conditions");
    Print("Finally run validation comparison script");
}
//+------------------------------------------------------------------+
