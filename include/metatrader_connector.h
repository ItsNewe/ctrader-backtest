#pragma once

#ifndef METATRADER_CONNECTOR_H
#define METATRADER_CONNECTOR_H

/**
 * @file metatrader_connector.h
 * @brief MetaTrader connectivity interfaces
 *
 * STATUS: INCOMPLETE - This file contains interface definitions but the
 * actual live trading implementation is NOT functional.
 *
 * The structures and enums here are used for:
 * - Parsing exported tick data from MT5 for backtesting
 * - Validating backtest results against MT5 Strategy Tester
 * - Defining compatible data formats
 *
 * For LIVE TRADING:
 * - Use MT5's built-in MQL5 language and Strategy Tester
 * - Export your strategy to MQL5 using the generated EA files in /mt5/
 * - The MQL5 EAs (e.g., FillUpAdaptive_v4.mq5, CombinedJu_EA.mq5) are
 *   the production-ready implementations
 *
 * For BACKTESTING:
 * - Use TickBasedEngine with exported MT5 tick data (MT5_CSV format)
 * - Export tick data from MT5: View -> Symbols -> Properties -> Ticks
 * - The C++ backtester produces MT5-compatible results
 *
 * @warning Do NOT use this file for live trading with real money.
 */

#include "backtest_engine.h"
#include <map>
#include <set>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <numeric>

// ============================================================================
// MetaTrader Broker Connectivity & History Data Feed
// ============================================================================
// Interfaces with MT4/MT5 for:
// - Historical OHLC data parsing
// - Tick data format compatibility
// - MT5-compatible result validation
// NOTE: Live order execution is NOT implemented - use MQL5 EAs instead
// ============================================================================

namespace mt {

// MT4/MT5 Timeframe Enumeration
enum class MTTimeframe {
    M1 = 1,          // 1 minute
    M5 = 5,          // 5 minutes
    M15 = 15,        // 15 minutes
    M30 = 30,        // 30 minutes
    H1 = 60,         // 1 hour
    H4 = 240,        // 4 hours
    D1 = 1440,       // Daily
    W1 = 10080,      // Weekly
    MN1 = 43200      // Monthly
};

// MT Account Types
enum class MTAccountType {
    DEMO,
    REAL_MICRO,
    REAL_STANDARD,
    REAL_ECN
};

// MT Order Types
enum class MTOrderType {
    BUY = 0,
    SELL = 1,
    BUY_LIMIT = 2,
    SELL_LIMIT = 3,
    BUY_STOP = 4,
    SELL_STOP = 5
};

// MT Connection Status
enum class MTConnectionStatus {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    AUTHENTICATED,
    ERROR_TIMEOUT,
    ERROR_INVALID_CREDENTIALS
};

// ============================================================================
// Data Structures
// ============================================================================

struct MTConfig {
    std::string broker_name;      // "ICMarkets", "Alpari", "FXPrimus", etc.
    std::string server;           // "mt4-01.mybroker.com" or "localhost"
    int port;                     // Usually 443 (HTTPS) or 1443 (FIX)
    std::string login;            // MT4/MT5 account number
    std::string password;         // MT4/MT5 password
    MTAccountType account_type;
    bool use_ssl;
    
    // Factory methods
    static MTConfig Demo(const std::string& broker) {
        MTConfig cfg;
        cfg.broker_name = broker;
        cfg.account_type = MTAccountType::DEMO;
        cfg.use_ssl = true;
        cfg.port = 443;
        return cfg;
    }
    
    static MTConfig Live(const std::string& broker) {
        MTConfig cfg;
        cfg.broker_name = broker;
        cfg.account_type = MTAccountType::REAL_STANDARD;
        cfg.use_ssl = true;
        cfg.port = 443;
        return cfg;
    }
};

struct MTAccount {
    std::string account_number;
    std::string currency;
    double balance;
    double equity;
    double margin_used;
    double margin_free;
    double margin_level;
    int total_orders;

    // Account leverage and limits
    int leverage;                 // Account leverage (e.g., 500 for 1:500)
    double margin_call_level;     // Margin call level % (e.g., 100.0)
    double stop_out_level;        // Stop out level % (e.g., 50.0)
    bool margin_mode_retail_hedging;  // Retail hedging mode

    // Account type info
    std::string company;          // Broker company name
    std::string name;             // Account holder name
    std::string server;           // Server name

    double GetFreeMargin() const { return margin_free; }
    double GetMarginLevel() const { return margin_level; }
    bool IsMarginCallRisk() const { return margin_level < 100.0; }
    bool IsStopOutRisk() const { return margin_level < stop_out_level; }
};

// MT Margin Calculation Modes (from ENUM_SYMBOL_CALC_MODE)
enum class MTMarginMode {
    FOREX = 0,              // Forex: (lots × contract_size × price) / leverage
    CFD = 1,                // CFD: Same as Forex
    CFD_INDEX = 2,          // CFD Index: (lots × contract_size × price) / leverage
    CFD_LEVERAGE = 3,       // CFD with custom leverage per symbol
    FUTURES = 4,            // Futures: Fixed margin per contract
    EXCHANGE_STOCKS = 5,    // Stocks: Full contract value required
    FOREX_NO_LEVERAGE = 6   // Forex without leverage
};

// MT Swap Calculation Modes (from ENUM_SYMBOL_SWAP_MODE)
enum class MTSwapMode {
    POINTS = 0,             // Swap in points
    BASE_CURRENCY = 1,      // Swap in base currency
    INTEREST = 2,           // Swap as annual interest %
    MARGIN_CURRENCY = 3,    // Swap in margin currency
    DEPOSIT_CURRENCY = 4,   // Swap in deposit currency
    PERCENT = 5,            // Swap as % of price
    REOPEN_CURRENT = 6,     // Reopen at current price
    REOPEN_BID = 7          // Reopen at bid price
};

struct MTSymbol {
    std::string name;             // "EURUSD", "GBPUSD", etc.
    int digits;                   // Decimal places (4 or 5)
    double bid;
    double ask;
    double point;                 // 0.0001 or 0.00001
    int spread;
    double tick_value;            // Profit per pip
    double tick_size;
    double contract_size;         // 100000 for standard lot

    // Margin calculation properties
    MTMarginMode margin_mode;     // How margin is calculated
    double margin_initial;        // Initial margin requirement
    double margin_maintenance;    // Maintenance margin requirement
    double margin_hedged;         // Hedged margin (for offsetting positions)

    // Swap properties
    MTSwapMode swap_mode;         // How swap is calculated
    double swap_long;             // Long position swap (per day)
    double swap_short;            // Short position swap (per day)
    int swap_rollover3days;       // Day for triple swap (0=Sun, 1=Mon, ..., 5=Fri)

    // Trading properties
    double volume_min;            // Minimum lot size (e.g., 0.01)
    double volume_max;            // Maximum lot size
    double volume_step;           // Lot size step (e.g., 0.01)
    int stops_level;              // Minimum distance for SL/TP in points

    // Symbol type info
    std::string description;      // Full name "Euro vs US Dollar"
    std::string currency_base;    // "EUR"
    std::string currency_profit;  // "USD"
    std::string currency_margin;  // "USD"

    double GetSpread() const { return (ask - bid) / point; }
    double GetMid() const { return (bid + ask) / 2.0; }
    int GetTripleSwapDay() const { return swap_rollover3days; }  // 0-6 (Sun-Sat)
};

struct MTBar {
    time_t time;
    double open;
    double high;
    double low;
    double close;
    unsigned long volume;
    int spread;
    
    MTBar() = default;
    MTBar(time_t t, double o, double h, double l, double c, unsigned long v, int s = 0)
        : time(t), open(o), high(h), low(l), close(c), volume(v), spread(s) {}
};

struct MTOrder {
    int order_ticket;
    MTOrderType type;
    time_t open_time;
    double open_price;
    double current_price;
    double stop_loss;
    double take_profit;
    double commission;
    double swap;
    double profit;
    std::string comment;
    
    double GetProfit() const { return profit; }
    bool IsLong() const { return type == MTOrderType::BUY || type == MTOrderType::BUY_LIMIT || type == MTOrderType::BUY_STOP; }
    bool IsShort() const { return type == MTOrderType::SELL || type == MTOrderType::SELL_LIMIT || type == MTOrderType::SELL_STOP; }
};

struct MTHistoryRecord {
    int ticket;
    int type;
    time_t open_time;
    time_t close_time;
    double open_price;
    double close_price;
    double high_price;
    double low_price;
    unsigned long volume;
    double commission;
    double swap;
    double profit;
    std::string symbol;
    std::string comment;
};

// ============================================================================
// MT History Data Loader
// ============================================================================

class MTHistoryLoader {
public:
    MTHistoryLoader() = default;
    
    // Load historical bars from MT4/MT5 history files
    // Path format: "C:\\Users\\..\\AppData\\Roaming\\MetaTrader 4\\profiles\\default\\tester\\history"
    std::vector<MTBar> LoadBarsFromHistory(
        const std::string& history_path,
        const std::string& symbol,
        MTTimeframe timeframe,
        time_t start_time,
        time_t end_time
    );
    
    // Load from CSV export (more portable)
    std::vector<MTBar> LoadBarsFromCSV(
        const std::string& csv_path,
        const std::string& symbol,
        time_t start_time,
        time_t end_time
    );
    
    // Load tick data from MT4 HST files (binary format)
    std::vector<backtest::Tick> LoadTicksFromHST(
        const std::string& hst_file_path,
        time_t start_time,
        time_t end_time
    );
    
    // Parse MT4 "Orders" history export
    std::vector<MTHistoryRecord> LoadTradeHistory(
        const std::string& history_csv_path
    );
    
private:
    // HST file format parsing (MT4 binary tick data)
    struct HSTHeader {
        char version[4];
        char reserved[60];
        int period;           // Timeframe in minutes
        int digits;
        time_t timesign;
        time_t lastsync;
        char copyright[128];
    };
    
    HSTHeader ParseHSTHeader(std::ifstream& file);
    MTBar ParseHSTRecord(std::ifstream& file, const HSTHeader& header);
};

// ============================================================================
// MT Broker Connection (Reverse-Engineered Protocol)
// ============================================================================

class MTConnection {
public:
    explicit MTConnection(const MTConfig& config);
    ~MTConnection();
    
    // Connection lifecycle
    bool Connect();
    bool Authenticate();
    void Disconnect();
    MTConnectionStatus GetStatus() const { return status_; }
    
    // Account operations
    MTAccount GetAccountInfo();
    std::vector<MTSymbol> GetSymbolList();
    MTSymbol GetSymbolInfo(const std::string& symbol);
    
    // Historical data
    std::vector<MTBar> GetBars(
        const std::string& symbol,
        MTTimeframe timeframe,
        int num_bars
    );
    
    std::vector<backtest::Tick> GetTicks(
        const std::string& symbol,
        time_t start_time,
        time_t end_time
    );
    
    // Order management
    int SendOrder(
        const std::string& symbol,
        MTOrderType type,
        double volume,
        double price,
        int slippage,
        double stoploss,
        double takeprofit,
        const std::string& comment
    );
    
    bool ModifyOrder(
        int ticket,
        double price,
        double stoploss,
        double takeprofit
    );
    
    bool CloseOrder(int ticket, double price, int slippage);
    bool DeleteOrder(int ticket);
    
    // Position queries
    std::vector<MTOrder> GetOpenOrders();
    MTOrder GetOrderInfo(int ticket);
    
    // Quote updates (real-time or cached)
    double GetBid(const std::string& symbol);
    double GetAsk(const std::string& symbol);
    
private:
    MTConfig config_;
    MTConnectionStatus status_;
    void* socket_;  // Placeholder for actual socket (would use Boost.Asio or WinSocket)
    std::map<std::string, MTSymbol> symbol_cache_;
    
    // Protocol implementation (reverse-engineered MT4/MT5)
    bool SendMessage(const std::string& message);
    std::string ReceiveMessage();
    bool ValidateResponse(const std::string& response);
};

// ============================================================================
// MT Data Feed Integration with Backtest Engine
// ============================================================================

class MTDataFeed {
public:
    explicit MTDataFeed(std::shared_ptr<MTConnection> connection);
    
    // Integrate with BacktestEngine
    void FeedToBacktest(
        backtest::BacktestEngine& engine,
        const std::string& symbol,
        MTTimeframe timeframe,
        time_t start_time,
        time_t end_time
    );
    
    // Download and cache historical data
    void CacheHistoricalData(
        const std::string& symbol,
        MTTimeframe timeframe,
        time_t start_time,
        time_t end_time
    );
    
    // Get cached data
    std::vector<MTBar> GetCachedBars(const std::string& symbol) const;
    std::vector<backtest::Tick> GetCachedTicks(const std::string& symbol) const;
    
    // Convert MT timeframe to seconds
    static int TimeframeToSeconds(MTTimeframe tf) {
        return static_cast<int>(tf) * 60;
    }
    
    // Generate realistic ticks from MT bars
    std::vector<backtest::Tick> GenerateTicksFromBars(
        const std::vector<MTBar>& bars,
        const MTSymbol& symbol_info,
        int ticks_per_bar = 100
    );
    
private:
    std::shared_ptr<MTConnection> connection_;
    std::map<std::string, std::vector<MTBar>> bar_cache_;
    std::map<std::string, std::vector<backtest::Tick>> tick_cache_;
    
    // Interpolate tick prices between OHLC
    std::vector<double> InterpolatePrices(
        double open, double high, double low, double close, int num_points
    );
};

// ============================================================================
// MT History Data Parser
// ============================================================================

class MTHistoryParser {
public:
    // Parse MT4 trade export (CSV format from "Orders" history)
    static std::vector<MTHistoryRecord> ParseOrdersHistory(const std::string& csv_content);
    
    // Parse MT OHLC data (CSV export)
    static std::vector<MTBar> ParseOHLCData(const std::string& csv_content);
    
    // Calculate statistics from history
    struct HistoryStats {
        int total_trades;
        int winning_trades;
        int losing_trades;
        double win_rate;
        double gross_profit;
        double gross_loss;
        double largest_win;
        double largest_loss;
        double avg_win;
        double avg_loss;
        double profit_factor;
        
        HistoryStats() = default;
        explicit HistoryStats(const std::vector<MTHistoryRecord>& records);
    };
    
    static HistoryStats CalculateStats(const std::vector<MTHistoryRecord>& records);
};

// ============================================================================
// MT4/MT5 Platform Detector
// ============================================================================

class MetaTraderDetector {
public:
    // Auto-detect MT4/MT5 installations on system
    static std::vector<std::string> DetectInstallations();
    
    // Get path to MT history folder
    static std::string GetMT4HistoryPath();
    static std::string GetMT5HistoryPath();
    
    // List available history files
    static std::vector<std::string> ListAvailableSymbols(const std::string& history_path);
};

} // namespace mt

#endif // METATRADER_CONNECTOR_H
