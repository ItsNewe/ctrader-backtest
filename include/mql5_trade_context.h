/**
 * MQL5 Trade Context for C++
 *
 * Provides a complete trading context similar to MQL5 for strategy development.
 * Integrates with TickBasedEngine for seamless operation.
 */

#ifndef MQL5_TRADE_CONTEXT_H
#define MQL5_TRADE_CONTEXT_H

#include "mql5_compat.h"
#include <map>
#include <vector>
#include <functional>
#include <memory>
#include <string>

namespace mql5 {

// Forward declarations
class CTrade;
class CPositionInfo;
class CSymbolInfo;
class CAccountInfo;

/**
 * Global trading context (simulates MQL5 global state)
 */
class TradeContext {
public:
    // Singleton access
    static TradeContext& Instance() {
        static TradeContext instance;
        return instance;
    }

    // Market data
    void UpdateTick(const std::string& symbol, const MqlTick& tick);
    MqlTick GetLastTick(const std::string& symbol) const;

    // Symbol management
    void RegisterSymbol(const std::string& symbol,
                        double contract_size,
                        double point,
                        int digits,
                        double swap_long,
                        double swap_short,
                        double spread);

    // Account settings
    void SetAccountBalance(double balance);
    void SetAccountEquity(double equity);
    void SetAccountMargin(double margin);
    void SetAccountLeverage(int leverage);
    void SetAccountCurrency(const std::string& currency);

    // Position tracking
    struct Position {
        ulong ticket;
        std::string symbol;
        ENUM_POSITION_TYPE type;
        double volume;
        double price_open;
        double sl;
        double tp;
        datetime time_open;
        ulong magic;
        std::string comment;
        double swap;
        double profit;
    };

    // Order tracking
    struct Order {
        ulong ticket;
        std::string symbol;
        ENUM_ORDER_TYPE type;
        double volume;
        double price;
        double price_stoplimit;
        double sl;
        double tp;
        datetime time_setup;
        datetime expiration;
        ulong magic;
        std::string comment;
    };

    // Position management
    ulong AddPosition(const Position& pos);
    bool ClosePosition(ulong ticket, double volume = 0);
    bool ModifyPosition(ulong ticket, double sl, double tp);
    Position* GetPosition(ulong ticket);
    std::vector<Position*> GetPositions(const std::string& symbol = "");
    int PositionsTotal() const;

    // Order management (pending orders)
    ulong AddOrder(const Order& order);
    bool DeleteOrder(ulong ticket);
    bool ModifyOrder(ulong ticket, double price, double sl, double tp, double stoplimit = 0);
    Order* GetOrder(ulong ticket);
    std::vector<Order*> GetOrders(const std::string& symbol = "");
    int OrdersTotal() const;

    // Deal history
    struct Deal {
        ulong ticket;
        std::string symbol;
        ENUM_DEAL_TYPE type;
        ENUM_DEAL_ENTRY entry;
        double volume;
        double price;
        double commission;
        double swap;
        double profit;
        datetime time;
        ulong magic;
        ulong position_id;
        ulong order_id;
        std::string comment;
    };

    void AddDeal(const Deal& deal);
    std::vector<Deal> GetDeals(datetime from = 0, datetime to = 0) const;
    int HistoryDealsTotal() const;

    // Account access
    double AccountBalance() const { return m_balance; }
    double AccountEquity() const { return m_equity; }
    double AccountMargin() const { return m_margin; }
    double AccountFreeMargin() const { return m_balance - m_margin; }
    int AccountLeverage() const { return m_leverage; }
    std::string AccountCurrency() const { return m_currency; }

    // Symbol access
    double SymbolInfoDouble(const std::string& symbol, ENUM_SYMBOL_INFO_DOUBLE prop) const;
    long SymbolInfoInteger(const std::string& symbol, ENUM_SYMBOL_INFO_INTEGER prop) const;

    // Event callbacks (for strategy integration)
    using OnTickCallback = std::function<void(const MqlTick&)>;
    using OnTradeCallback = std::function<void(const Deal&)>;

    void SetOnTickCallback(OnTickCallback callback) { m_onTick = callback; }
    void SetOnTradeCallback(OnTradeCallback callback) { m_onTrade = callback; }

    // Processing
    void ProcessTick(const std::string& symbol, const MqlTick& tick);
    void ProcessPendingOrders(const std::string& symbol, double bid, double ask);
    void UpdateFloatingPnL();
    void ChargeSwap();

    // Reset for new backtest
    void Reset();

private:
    TradeContext();

    // Market state
    std::map<std::string, MqlTick> m_lastTicks;

    // Symbol info
    struct SymbolData {
        double contract_size;
        double point;
        int digits;
        double swap_long;
        double swap_short;
        double spread;
        double bid;
        double ask;
    };
    std::map<std::string, SymbolData> m_symbols;

    // Account state
    double m_balance;
    double m_equity;
    double m_margin;
    int m_leverage;
    std::string m_currency;

    // Positions and orders
    ulong m_nextTicket;
    std::map<ulong, Position> m_positions;
    std::map<ulong, Order> m_orders;
    std::vector<Deal> m_deals;

    // Callbacks
    OnTickCallback m_onTick;
    OnTradeCallback m_onTrade;
};

/**
 * CTrade - Trade execution class (like MQL5 CTrade)
 */
class CTrade {
public:
    CTrade();

    // Setup
    void SetExpertMagicNumber(ulong magic) { m_magic = magic; }
    void SetDeviationInPoints(ulong deviation) { m_deviation = deviation; }
    void SetTypeFilling(ENUM_ORDER_TYPE_FILLING filling) { m_filling = filling; }
    void SetTypeFillingBySymbol(const std::string& symbol);

    // Market orders
    bool Buy(double volume, const std::string& symbol = "",
             double price = 0, double sl = 0, double tp = 0,
             const std::string& comment = "");

    bool Sell(double volume, const std::string& symbol = "",
              double price = 0, double sl = 0, double tp = 0,
              const std::string& comment = "");

    // Pending orders
    bool BuyLimit(double volume, double price, const std::string& symbol = "",
                  double sl = 0, double tp = 0, ENUM_ORDER_TYPE_TIME type_time = ORDER_TIME_GTC,
                  datetime expiration = 0, const std::string& comment = "");

    bool SellLimit(double volume, double price, const std::string& symbol = "",
                   double sl = 0, double tp = 0, ENUM_ORDER_TYPE_TIME type_time = ORDER_TIME_GTC,
                   datetime expiration = 0, const std::string& comment = "");

    bool BuyStop(double volume, double price, const std::string& symbol = "",
                 double sl = 0, double tp = 0, ENUM_ORDER_TYPE_TIME type_time = ORDER_TIME_GTC,
                 datetime expiration = 0, const std::string& comment = "");

    bool SellStop(double volume, double price, const std::string& symbol = "",
                  double sl = 0, double tp = 0, ENUM_ORDER_TYPE_TIME type_time = ORDER_TIME_GTC,
                  datetime expiration = 0, const std::string& comment = "");

    // Position operations
    bool PositionClose(ulong ticket, ulong deviation = ULONG_MAX);
    bool PositionClosePartial(ulong ticket, double volume, ulong deviation = ULONG_MAX);
    bool PositionModify(ulong ticket, double sl, double tp);

    // Order operations
    bool OrderDelete(ulong ticket);
    bool OrderModify(ulong ticket, double price, double sl, double tp,
                     ENUM_ORDER_TYPE_TIME type_time = ORDER_TIME_GTC,
                     datetime expiration = 0, double stoplimit = 0);

    // Result access
    uint ResultRetcode() const { return m_result.retcode; }
    ulong ResultDeal() const { return m_result.deal; }
    ulong ResultOrder() const { return m_result.order; }
    double ResultVolume() const { return m_result.volume; }
    double ResultPrice() const { return m_result.price; }
    double ResultBid() const { return m_result.bid; }
    double ResultAsk() const { return m_result.ask; }
    std::string ResultComment() const { return m_result.comment; }

    // Request access
    MqlTradeRequest Request() const { return m_request; }
    MqlTradeResult Result() const { return m_result; }

private:
    bool SendOrder(const MqlTradeRequest& request);

    ulong m_magic;
    ulong m_deviation;
    ENUM_ORDER_TYPE_FILLING m_filling;
    std::string m_symbol;

    MqlTradeRequest m_request;
    MqlTradeResult m_result;
};

/**
 * CPositionInfo - Position information class
 */
class CPositionInfo {
public:
    CPositionInfo();

    // Selection
    bool Select(const std::string& symbol);
    bool SelectByIndex(int index);
    bool SelectByTicket(ulong ticket);

    // Properties
    ulong Ticket() const;
    datetime Time() const;
    ENUM_POSITION_TYPE PositionType() const;
    ulong Magic() const;
    long Identifier() const;
    double Volume() const;
    double PriceOpen() const;
    double StopLoss() const;
    double TakeProfit() const;
    double PriceCurrent() const;
    double Commission() const;
    double Swap() const;
    double Profit() const;
    std::string Symbol() const;
    std::string Comment() const;

    // Type conversion
    std::string TypeDescription() const;
    static std::string TypeDescription(ENUM_POSITION_TYPE type);

private:
    TradeContext::Position* m_position;
};

/**
 * CSymbolInfo - Symbol information class
 */
class CSymbolInfo {
public:
    CSymbolInfo();

    // Selection
    bool Name(const std::string& symbol);
    std::string Name() const { return m_symbol; }

    // Refresh rates
    bool RefreshRates();

    // Prices
    double Bid() const;
    double Ask() const;
    double Last() const;
    double Point() const;
    int Digits() const;

    // Contract
    double ContractSize() const;
    double LotsMin() const;
    double LotsMax() const;
    double LotsStep() const;

    // Swap
    double SwapLong() const;
    double SwapShort() const;
    int SwapMode() const;
    int SwapRollover3Days() const;

    // Margin
    double MarginInitial() const;
    double MarginMaintenance() const;

    // Spread
    int Spread() const;
    bool SpreadFloat() const;

    // Trading
    bool TradeAllowed() const;
    int FreezeLevel() const;
    int StopsLevel() const;

    // Calculated
    double NormalizePrice(double price) const;
    bool CheckVolume(double volume) const;

private:
    std::string m_symbol;
    MqlTick m_tick;
};

/**
 * CAccountInfo - Account information class
 */
class CAccountInfo {
public:
    CAccountInfo();

    // Properties
    long Login() const;
    long Leverage() const;
    bool TradeAllowed() const;
    bool TradeExpert() const;
    int LimitOrders() const;

    double Balance() const;
    double Credit() const;
    double Profit() const;
    double Equity() const;
    double Margin() const;
    double FreeMargin() const;
    double MarginLevel() const;
    double MarginCall() const;
    double MarginStopOut() const;

    std::string Name() const;
    std::string Server() const;
    std::string Currency() const;
    std::string Company() const;

    // Calculations
    double OrderProfitCheck(const std::string& symbol, ENUM_ORDER_TYPE type,
                           double volume, double price_open, double price_close) const;
    double MarginCheck(const std::string& symbol, ENUM_ORDER_TYPE type,
                      double volume, double price) const;
    double FreeMarginCheck(const std::string& symbol, ENUM_ORDER_TYPE type,
                          double volume, double price) const;
    double MaxLotCheck(const std::string& symbol, ENUM_ORDER_TYPE type,
                      double price, double percent = 100) const;
};

// =============================================================================
// Global Functions (MQL5 style)
// =============================================================================

// Position functions
inline int PositionsTotal() {
    return TradeContext::Instance().PositionsTotal();
}

inline bool PositionSelect(const std::string& symbol) {
    auto positions = TradeContext::Instance().GetPositions(symbol);
    return !positions.empty();
}

inline bool PositionSelectByTicket(ulong ticket) {
    return TradeContext::Instance().GetPosition(ticket) != nullptr;
}

inline ulong PositionGetTicket(int index) {
    auto positions = TradeContext::Instance().GetPositions();
    if (index >= 0 && index < static_cast<int>(positions.size())) {
        return positions[index]->ticket;
    }
    return 0;
}

inline std::string PositionGetString(long property) {
    // Simplified - would need current selected position
    return "";
}

inline double PositionGetDouble(long property) {
    return 0;
}

inline long PositionGetInteger(long property) {
    return 0;
}

// Order functions
inline int OrdersTotal() {
    return TradeContext::Instance().OrdersTotal();
}

inline bool OrderSelect(ulong ticket) {
    return TradeContext::Instance().GetOrder(ticket) != nullptr;
}

inline ulong OrderGetTicket(int index) {
    auto orders = TradeContext::Instance().GetOrders();
    if (index >= 0 && index < static_cast<int>(orders.size())) {
        return orders[index]->ticket;
    }
    return 0;
}

// History functions
inline int HistoryDealsTotal() {
    return TradeContext::Instance().HistoryDealsTotal();
}

inline bool HistorySelect(datetime from, datetime to) {
    // History is always available in our implementation
    return true;
}

// Account functions
inline double AccountInfoDouble(ENUM_ACCOUNT_INFO_DOUBLE property) {
    auto& ctx = TradeContext::Instance();
    switch (property) {
        case ACCOUNT_BALANCE: return ctx.AccountBalance();
        case ACCOUNT_EQUITY: return ctx.AccountEquity();
        case ACCOUNT_MARGIN: return ctx.AccountMargin();
        case ACCOUNT_MARGIN_FREE: return ctx.AccountFreeMargin();
        default: return 0;
    }
}

inline long AccountInfoInteger(ENUM_ACCOUNT_INFO_INTEGER property) {
    auto& ctx = TradeContext::Instance();
    switch (property) {
        case ACCOUNT_LEVERAGE: return ctx.AccountLeverage();
        default: return 0;
    }
}

inline std::string AccountInfoString(int property) {
    return TradeContext::Instance().AccountCurrency();
}

// Symbol functions
inline double SymbolInfoDouble(const std::string& symbol, ENUM_SYMBOL_INFO_DOUBLE property) {
    return TradeContext::Instance().SymbolInfoDouble(symbol, property);
}

inline long SymbolInfoInteger(const std::string& symbol, ENUM_SYMBOL_INFO_INTEGER property) {
    return TradeContext::Instance().SymbolInfoInteger(symbol, property);
}

inline bool SymbolInfoTick(const std::string& symbol, MqlTick& tick) {
    tick = TradeContext::Instance().GetLastTick(symbol);
    return true;
}

// Trade functions
inline bool OrderSend(const MqlTradeRequest& request, MqlTradeResult& result) {
    CTrade trade;
    trade.SetExpertMagicNumber(request.magic);

    bool success = false;
    switch (request.action) {
        case TRADE_ACTION_DEAL:
            if (request.type == ORDER_TYPE_BUY) {
                success = trade.Buy(request.volume, request.symbol,
                                   request.price, request.sl, request.tp,
                                   request.comment);
            } else if (request.type == ORDER_TYPE_SELL) {
                success = trade.Sell(request.volume, request.symbol,
                                    request.price, request.sl, request.tp,
                                    request.comment);
            }
            break;
        default:
            break;
    }

    result = trade.Result();
    return success;
}

inline bool OrderCheck(const MqlTradeRequest& request, MqlTradeCheckResult& result) {
    result.retcode = TRADE_RETCODE_DONE;
    result.balance = AccountInfoDouble(ACCOUNT_BALANCE);
    result.equity = AccountInfoDouble(ACCOUNT_EQUITY);
    result.margin = AccountInfoDouble(ACCOUNT_MARGIN);
    result.margin_free = AccountInfoDouble(ACCOUNT_MARGIN_FREE);
    return true;
}

} // namespace mql5

#endif // MQL5_TRADE_CONTEXT_H
