/**
 * MQL5 Trade Context Implementation
 */

#include "mql5_trade_context.h"
#include <algorithm>

namespace mql5 {

// =============================================================================
// TradeContext Implementation
// =============================================================================

TradeContext::TradeContext()
    : m_balance(10000.0)
    , m_equity(10000.0)
    , m_margin(0.0)
    , m_leverage(100)
    , m_currency("USD")
    , m_nextTicket(1)
{
}

void TradeContext::UpdateTick(const std::string& symbol, const MqlTick& tick)
{
    m_lastTicks[symbol] = tick;

    // Update symbol data if registered
    auto it = m_symbols.find(symbol);
    if (it != m_symbols.end()) {
        it->second.bid = tick.bid;
        it->second.ask = tick.ask;
    }
}

MqlTick TradeContext::GetLastTick(const std::string& symbol) const
{
    auto it = m_lastTicks.find(symbol);
    if (it != m_lastTicks.end()) {
        return it->second;
    }
    return MqlTick{};
}

void TradeContext::RegisterSymbol(const std::string& symbol,
                                   double contract_size,
                                   double point,
                                   int digits,
                                   double swap_long,
                                   double swap_short,
                                   double spread)
{
    SymbolData data;
    data.contract_size = contract_size;
    data.point = point;
    data.digits = digits;
    data.swap_long = swap_long;
    data.swap_short = swap_short;
    data.spread = spread;
    data.bid = 0;
    data.ask = 0;
    m_symbols[symbol] = data;
}

void TradeContext::SetAccountBalance(double balance) { m_balance = balance; }
void TradeContext::SetAccountEquity(double equity) { m_equity = equity; }
void TradeContext::SetAccountMargin(double margin) { m_margin = margin; }
void TradeContext::SetAccountLeverage(int leverage) { m_leverage = leverage; }
void TradeContext::SetAccountCurrency(const std::string& currency) { m_currency = currency; }

ulong TradeContext::AddPosition(const Position& pos)
{
    Position p = pos;
    p.ticket = m_nextTicket++;
    m_positions[p.ticket] = p;
    return p.ticket;
}

bool TradeContext::ClosePosition(ulong ticket, double volume)
{
    auto it = m_positions.find(ticket);
    if (it == m_positions.end()) return false;

    Position& pos = it->second;

    // Get current prices
    MqlTick tick = GetLastTick(pos.symbol);
    double close_price = (pos.type == POSITION_TYPE_BUY) ? tick.bid : tick.ask;

    // Calculate profit
    auto sym_it = m_symbols.find(pos.symbol);
    double contract_size = (sym_it != m_symbols.end()) ?
        sym_it->second.contract_size : 100.0;

    double close_volume = (volume > 0 && volume < pos.volume) ? volume : pos.volume;
    double profit;

    if (pos.type == POSITION_TYPE_BUY) {
        profit = (close_price - pos.price_open) * close_volume * contract_size;
    } else {
        profit = (pos.price_open - close_price) * close_volume * contract_size;
    }

    // Create deal record
    Deal deal;
    deal.ticket = m_nextTicket++;
    deal.symbol = pos.symbol;
    deal.type = (pos.type == POSITION_TYPE_BUY) ? DEAL_TYPE_SELL : DEAL_TYPE_BUY;
    deal.entry = DEAL_ENTRY_OUT;
    deal.volume = close_volume;
    deal.price = close_price;
    deal.commission = 0;
    deal.swap = pos.swap;
    deal.profit = profit;
    deal.time = TimeCurrent();
    deal.magic = pos.magic;
    deal.position_id = pos.ticket;
    deal.order_id = 0;
    deal.comment = "close";

    AddDeal(deal);

    // Update balance
    m_balance += profit + pos.swap;

    // Notify
    if (m_onTrade) {
        m_onTrade(deal);
    }

    // Remove or reduce position
    if (close_volume >= pos.volume) {
        m_positions.erase(it);
    } else {
        pos.volume -= close_volume;
    }

    return true;
}

bool TradeContext::ModifyPosition(ulong ticket, double sl, double tp)
{
    auto it = m_positions.find(ticket);
    if (it == m_positions.end()) return false;

    it->second.sl = sl;
    it->second.tp = tp;
    return true;
}

TradeContext::Position* TradeContext::GetPosition(ulong ticket)
{
    auto it = m_positions.find(ticket);
    return (it != m_positions.end()) ? &it->second : nullptr;
}

std::vector<TradeContext::Position*> TradeContext::GetPositions(const std::string& symbol)
{
    std::vector<Position*> result;
    for (auto& [ticket, pos] : m_positions) {
        if (symbol.empty() || pos.symbol == symbol) {
            result.push_back(&pos);
        }
    }
    return result;
}

int TradeContext::PositionsTotal() const
{
    return static_cast<int>(m_positions.size());
}

ulong TradeContext::AddOrder(const Order& order)
{
    Order o = order;
    o.ticket = m_nextTicket++;
    m_orders[o.ticket] = o;
    return o.ticket;
}

bool TradeContext::DeleteOrder(ulong ticket)
{
    return m_orders.erase(ticket) > 0;
}

bool TradeContext::ModifyOrder(ulong ticket, double price, double sl, double tp, double stoplimit)
{
    auto it = m_orders.find(ticket);
    if (it == m_orders.end()) return false;

    it->second.price = price;
    it->second.sl = sl;
    it->second.tp = tp;
    it->second.price_stoplimit = stoplimit;
    return true;
}

TradeContext::Order* TradeContext::GetOrder(ulong ticket)
{
    auto it = m_orders.find(ticket);
    return (it != m_orders.end()) ? &it->second : nullptr;
}

std::vector<TradeContext::Order*> TradeContext::GetOrders(const std::string& symbol)
{
    std::vector<Order*> result;
    for (auto& [ticket, order] : m_orders) {
        if (symbol.empty() || order.symbol == symbol) {
            result.push_back(&order);
        }
    }
    return result;
}

int TradeContext::OrdersTotal() const
{
    return static_cast<int>(m_orders.size());
}

void TradeContext::AddDeal(const Deal& deal)
{
    m_deals.push_back(deal);
}

std::vector<TradeContext::Deal> TradeContext::GetDeals(datetime from, datetime to) const
{
    if (from == 0 && to == 0) {
        return m_deals;
    }

    std::vector<Deal> result;
    for (const auto& deal : m_deals) {
        if ((from == 0 || deal.time >= from) && (to == 0 || deal.time <= to)) {
            result.push_back(deal);
        }
    }
    return result;
}

int TradeContext::HistoryDealsTotal() const
{
    return static_cast<int>(m_deals.size());
}

double TradeContext::SymbolInfoDouble(const std::string& symbol, ENUM_SYMBOL_INFO_DOUBLE prop) const
{
    auto it = m_symbols.find(symbol);
    if (it == m_symbols.end()) return 0;

    const SymbolData& sym = it->second;
    switch (prop) {
        case SYMBOL_BID: return sym.bid;
        case SYMBOL_ASK: return sym.ask;
        case SYMBOL_POINT: return sym.point;
        case SYMBOL_TRADE_CONTRACT_SIZE: return sym.contract_size;
        case SYMBOL_SWAP_LONG: return sym.swap_long;
        case SYMBOL_SWAP_SHORT: return sym.swap_short;
        default: return 0;
    }
}

long TradeContext::SymbolInfoInteger(const std::string& symbol, ENUM_SYMBOL_INFO_INTEGER prop) const
{
    auto it = m_symbols.find(symbol);
    if (it == m_symbols.end()) return 0;

    const SymbolData& sym = it->second;
    switch (prop) {
        case SYMBOL_DIGITS: return sym.digits;
        case SYMBOL_SPREAD: return static_cast<long>(sym.spread / sym.point);
        default: return 0;
    }
}

void TradeContext::ProcessTick(const std::string& symbol, const MqlTick& tick)
{
    UpdateTick(symbol, tick);

    // Process pending orders
    ProcessPendingOrders(symbol, tick.bid, tick.ask);

    // Check stop-loss and take-profit
    for (auto& [ticket, pos] : m_positions) {
        if (pos.symbol != symbol) continue;

        bool should_close = false;

        if (pos.type == POSITION_TYPE_BUY) {
            if (pos.sl > 0 && tick.bid <= pos.sl) should_close = true;
            if (pos.tp > 0 && tick.bid >= pos.tp) should_close = true;
        } else {
            if (pos.sl > 0 && tick.ask >= pos.sl) should_close = true;
            if (pos.tp > 0 && tick.ask <= pos.tp) should_close = true;
        }

        if (should_close) {
            ClosePosition(ticket);
        }
    }

    // Update floating P/L
    UpdateFloatingPnL();

    // Notify callback
    if (m_onTick) {
        m_onTick(tick);
    }
}

void TradeContext::ProcessPendingOrders(const std::string& symbol, double bid, double ask)
{
    std::vector<ulong> orders_to_execute;

    for (auto& [ticket, order] : m_orders) {
        if (order.symbol != symbol) continue;

        bool should_execute = false;
        double execution_price = 0;

        switch (order.type) {
            case ORDER_TYPE_BUY_LIMIT:
                if (ask <= order.price) {
                    should_execute = true;
                    execution_price = order.price;
                }
                break;
            case ORDER_TYPE_SELL_LIMIT:
                if (bid >= order.price) {
                    should_execute = true;
                    execution_price = order.price;
                }
                break;
            case ORDER_TYPE_BUY_STOP:
                if (ask >= order.price) {
                    should_execute = true;
                    execution_price = ask;
                }
                break;
            case ORDER_TYPE_SELL_STOP:
                if (bid <= order.price) {
                    should_execute = true;
                    execution_price = bid;
                }
                break;
            default:
                break;
        }

        if (should_execute) {
            // Create position from order
            Position pos;
            pos.ticket = 0;  // Will be assigned
            pos.symbol = order.symbol;
            pos.type = (order.type == ORDER_TYPE_BUY_LIMIT || order.type == ORDER_TYPE_BUY_STOP) ?
                       POSITION_TYPE_BUY : POSITION_TYPE_SELL;
            pos.volume = order.volume;
            pos.price_open = execution_price;
            pos.sl = order.sl;
            pos.tp = order.tp;
            pos.time_open = TimeCurrent();
            pos.magic = order.magic;
            pos.comment = order.comment;
            pos.swap = 0;
            pos.profit = 0;

            AddPosition(pos);
            orders_to_execute.push_back(ticket);
        }
    }

    // Remove executed orders
    for (ulong ticket : orders_to_execute) {
        DeleteOrder(ticket);
    }
}

void TradeContext::UpdateFloatingPnL()
{
    double total_pnl = 0;
    double total_margin = 0;

    for (auto& [ticket, pos] : m_positions) {
        MqlTick tick = GetLastTick(pos.symbol);

        auto sym_it = m_symbols.find(pos.symbol);
        double contract_size = (sym_it != m_symbols.end()) ?
            sym_it->second.contract_size : 100.0;

        double current_price = (pos.type == POSITION_TYPE_BUY) ? tick.bid : tick.ask;
        double price_diff = (pos.type == POSITION_TYPE_BUY) ?
            (current_price - pos.price_open) : (pos.price_open - current_price);

        pos.profit = price_diff * pos.volume * contract_size;
        total_pnl += pos.profit;

        // Calculate margin
        double notional = pos.volume * contract_size * current_price;
        total_margin += notional / m_leverage;
    }

    m_equity = m_balance + total_pnl;
    m_margin = total_margin;
}

void TradeContext::ChargeSwap()
{
    for (auto& [ticket, pos] : m_positions) {
        auto sym_it = m_symbols.find(pos.symbol);
        if (sym_it == m_symbols.end()) continue;

        double swap_rate = (pos.type == POSITION_TYPE_BUY) ?
            sym_it->second.swap_long : sym_it->second.swap_short;

        // Swap in points * point value * volume
        double swap_charge = swap_rate * sym_it->second.point *
                            sym_it->second.contract_size * pos.volume;

        pos.swap += swap_charge;
    }
}

void TradeContext::Reset()
{
    m_lastTicks.clear();
    m_positions.clear();
    m_orders.clear();
    m_deals.clear();
    m_nextTicket = 1;
    m_balance = 10000.0;
    m_equity = 10000.0;
    m_margin = 0.0;
}

// =============================================================================
// CTrade Implementation
// =============================================================================

CTrade::CTrade()
    : m_magic(0)
    , m_deviation(10)
    , m_filling(ORDER_FILLING_FOK)
{
}

void CTrade::SetTypeFillingBySymbol(const std::string& symbol)
{
    m_symbol = symbol;
    // In real implementation, would check symbol's allowed filling modes
    m_filling = ORDER_FILLING_IOC;
}

bool CTrade::Buy(double volume, const std::string& symbol,
                 double price, double sl, double tp,
                 const std::string& comment)
{
    std::string sym = symbol.empty() ? m_symbol : symbol;
    MqlTick tick = TradeContext::Instance().GetLastTick(sym);

    if (price == 0) price = tick.ask;

    // Create position
    TradeContext::Position pos;
    pos.symbol = sym;
    pos.type = POSITION_TYPE_BUY;
    pos.volume = volume;
    pos.price_open = price;
    pos.sl = sl;
    pos.tp = tp;
    pos.time_open = TimeCurrent();
    pos.magic = m_magic;
    pos.comment = comment;
    pos.swap = 0;
    pos.profit = 0;

    ulong ticket = TradeContext::Instance().AddPosition(pos);

    // Record deal
    TradeContext::Deal deal;
    deal.ticket = ticket + 10000;
    deal.symbol = sym;
    deal.type = DEAL_TYPE_BUY;
    deal.entry = DEAL_ENTRY_IN;
    deal.volume = volume;
    deal.price = price;
    deal.commission = 0;
    deal.swap = 0;
    deal.profit = 0;
    deal.time = TimeCurrent();
    deal.magic = m_magic;
    deal.position_id = ticket;
    deal.order_id = 0;
    deal.comment = comment;

    TradeContext::Instance().AddDeal(deal);

    // Set result
    m_result.retcode = TRADE_RETCODE_DONE;
    m_result.deal = deal.ticket;
    m_result.order = ticket;
    m_result.volume = volume;
    m_result.price = price;
    m_result.bid = tick.bid;
    m_result.ask = tick.ask;
    m_result.comment = "";

    return true;
}

bool CTrade::Sell(double volume, const std::string& symbol,
                  double price, double sl, double tp,
                  const std::string& comment)
{
    std::string sym = symbol.empty() ? m_symbol : symbol;
    MqlTick tick = TradeContext::Instance().GetLastTick(sym);

    if (price == 0) price = tick.bid;

    // Create position
    TradeContext::Position pos;
    pos.symbol = sym;
    pos.type = POSITION_TYPE_SELL;
    pos.volume = volume;
    pos.price_open = price;
    pos.sl = sl;
    pos.tp = tp;
    pos.time_open = TimeCurrent();
    pos.magic = m_magic;
    pos.comment = comment;
    pos.swap = 0;
    pos.profit = 0;

    ulong ticket = TradeContext::Instance().AddPosition(pos);

    // Record deal
    TradeContext::Deal deal;
    deal.ticket = ticket + 10000;
    deal.symbol = sym;
    deal.type = DEAL_TYPE_SELL;
    deal.entry = DEAL_ENTRY_IN;
    deal.volume = volume;
    deal.price = price;
    deal.commission = 0;
    deal.swap = 0;
    deal.profit = 0;
    deal.time = TimeCurrent();
    deal.magic = m_magic;
    deal.position_id = ticket;
    deal.order_id = 0;
    deal.comment = comment;

    TradeContext::Instance().AddDeal(deal);

    // Set result
    m_result.retcode = TRADE_RETCODE_DONE;
    m_result.deal = deal.ticket;
    m_result.order = ticket;
    m_result.volume = volume;
    m_result.price = price;
    m_result.bid = tick.bid;
    m_result.ask = tick.ask;
    m_result.comment = "";

    return true;
}

bool CTrade::BuyLimit(double volume, double price, const std::string& symbol,
                      double sl, double tp, ENUM_ORDER_TYPE_TIME type_time,
                      datetime expiration, const std::string& comment)
{
    std::string sym = symbol.empty() ? m_symbol : symbol;

    TradeContext::Order order;
    order.symbol = sym;
    order.type = ORDER_TYPE_BUY_LIMIT;
    order.volume = volume;
    order.price = price;
    order.price_stoplimit = 0;
    order.sl = sl;
    order.tp = tp;
    order.time_setup = TimeCurrent();
    order.expiration = expiration;
    order.magic = m_magic;
    order.comment = comment;

    ulong ticket = TradeContext::Instance().AddOrder(order);

    m_result.retcode = TRADE_RETCODE_PLACED;
    m_result.order = ticket;
    return true;
}

bool CTrade::SellLimit(double volume, double price, const std::string& symbol,
                       double sl, double tp, ENUM_ORDER_TYPE_TIME type_time,
                       datetime expiration, const std::string& comment)
{
    std::string sym = symbol.empty() ? m_symbol : symbol;

    TradeContext::Order order;
    order.symbol = sym;
    order.type = ORDER_TYPE_SELL_LIMIT;
    order.volume = volume;
    order.price = price;
    order.price_stoplimit = 0;
    order.sl = sl;
    order.tp = tp;
    order.time_setup = TimeCurrent();
    order.expiration = expiration;
    order.magic = m_magic;
    order.comment = comment;

    ulong ticket = TradeContext::Instance().AddOrder(order);

    m_result.retcode = TRADE_RETCODE_PLACED;
    m_result.order = ticket;
    return true;
}

bool CTrade::BuyStop(double volume, double price, const std::string& symbol,
                     double sl, double tp, ENUM_ORDER_TYPE_TIME type_time,
                     datetime expiration, const std::string& comment)
{
    std::string sym = symbol.empty() ? m_symbol : symbol;

    TradeContext::Order order;
    order.symbol = sym;
    order.type = ORDER_TYPE_BUY_STOP;
    order.volume = volume;
    order.price = price;
    order.price_stoplimit = 0;
    order.sl = sl;
    order.tp = tp;
    order.time_setup = TimeCurrent();
    order.expiration = expiration;
    order.magic = m_magic;
    order.comment = comment;

    ulong ticket = TradeContext::Instance().AddOrder(order);

    m_result.retcode = TRADE_RETCODE_PLACED;
    m_result.order = ticket;
    return true;
}

bool CTrade::SellStop(double volume, double price, const std::string& symbol,
                      double sl, double tp, ENUM_ORDER_TYPE_TIME type_time,
                      datetime expiration, const std::string& comment)
{
    std::string sym = symbol.empty() ? m_symbol : symbol;

    TradeContext::Order order;
    order.symbol = sym;
    order.type = ORDER_TYPE_SELL_STOP;
    order.volume = volume;
    order.price = price;
    order.price_stoplimit = 0;
    order.sl = sl;
    order.tp = tp;
    order.time_setup = TimeCurrent();
    order.expiration = expiration;
    order.magic = m_magic;
    order.comment = comment;

    ulong ticket = TradeContext::Instance().AddOrder(order);

    m_result.retcode = TRADE_RETCODE_PLACED;
    m_result.order = ticket;
    return true;
}

bool CTrade::PositionClose(ulong ticket, ulong deviation)
{
    bool result = TradeContext::Instance().ClosePosition(ticket);
    m_result.retcode = result ? TRADE_RETCODE_DONE : TRADE_RETCODE_REJECT;
    return result;
}

bool CTrade::PositionClosePartial(ulong ticket, double volume, ulong deviation)
{
    bool result = TradeContext::Instance().ClosePosition(ticket, volume);
    m_result.retcode = result ? TRADE_RETCODE_DONE : TRADE_RETCODE_REJECT;
    return result;
}

bool CTrade::PositionModify(ulong ticket, double sl, double tp)
{
    bool result = TradeContext::Instance().ModifyPosition(ticket, sl, tp);
    m_result.retcode = result ? TRADE_RETCODE_DONE : TRADE_RETCODE_REJECT;
    return result;
}

bool CTrade::OrderDelete(ulong ticket)
{
    bool result = TradeContext::Instance().DeleteOrder(ticket);
    m_result.retcode = result ? TRADE_RETCODE_DONE : TRADE_RETCODE_REJECT;
    return result;
}

bool CTrade::OrderModify(ulong ticket, double price, double sl, double tp,
                         ENUM_ORDER_TYPE_TIME type_time, datetime expiration,
                         double stoplimit)
{
    bool result = TradeContext::Instance().ModifyOrder(ticket, price, sl, tp, stoplimit);
    m_result.retcode = result ? TRADE_RETCODE_DONE : TRADE_RETCODE_REJECT;
    return result;
}

// =============================================================================
// CPositionInfo Implementation
// =============================================================================

CPositionInfo::CPositionInfo()
    : m_position(nullptr)
{
}

bool CPositionInfo::Select(const std::string& symbol)
{
    auto positions = TradeContext::Instance().GetPositions(symbol);
    if (!positions.empty()) {
        m_position = positions[0];
        return true;
    }
    m_position = nullptr;
    return false;
}

bool CPositionInfo::SelectByIndex(int index)
{
    auto positions = TradeContext::Instance().GetPositions();
    if (index >= 0 && index < static_cast<int>(positions.size())) {
        m_position = positions[index];
        return true;
    }
    m_position = nullptr;
    return false;
}

bool CPositionInfo::SelectByTicket(ulong ticket)
{
    m_position = TradeContext::Instance().GetPosition(ticket);
    return m_position != nullptr;
}

ulong CPositionInfo::Ticket() const { return m_position ? m_position->ticket : 0; }
datetime CPositionInfo::Time() const { return m_position ? m_position->time_open : 0; }
ENUM_POSITION_TYPE CPositionInfo::PositionType() const { return m_position ? m_position->type : POSITION_TYPE_BUY; }
ulong CPositionInfo::Magic() const { return m_position ? m_position->magic : 0; }
long CPositionInfo::Identifier() const { return m_position ? static_cast<long>(m_position->ticket) : 0; }
double CPositionInfo::Volume() const { return m_position ? m_position->volume : 0; }
double CPositionInfo::PriceOpen() const { return m_position ? m_position->price_open : 0; }
double CPositionInfo::StopLoss() const { return m_position ? m_position->sl : 0; }
double CPositionInfo::TakeProfit() const { return m_position ? m_position->tp : 0; }

double CPositionInfo::PriceCurrent() const {
    if (!m_position) return 0;
    MqlTick tick = TradeContext::Instance().GetLastTick(m_position->symbol);
    return (m_position->type == POSITION_TYPE_BUY) ? tick.bid : tick.ask;
}

double CPositionInfo::Commission() const { return 0; }
double CPositionInfo::Swap() const { return m_position ? m_position->swap : 0; }
double CPositionInfo::Profit() const { return m_position ? m_position->profit : 0; }
std::string CPositionInfo::Symbol() const { return m_position ? m_position->symbol : ""; }
std::string CPositionInfo::Comment() const { return m_position ? m_position->comment : ""; }

std::string CPositionInfo::TypeDescription() const {
    return TypeDescription(m_position ? m_position->type : POSITION_TYPE_BUY);
}

std::string CPositionInfo::TypeDescription(ENUM_POSITION_TYPE type) {
    return (type == POSITION_TYPE_BUY) ? "buy" : "sell";
}

// =============================================================================
// CSymbolInfo Implementation
// =============================================================================

CSymbolInfo::CSymbolInfo()
{
}

bool CSymbolInfo::Name(const std::string& symbol)
{
    m_symbol = symbol;
    return RefreshRates();
}

bool CSymbolInfo::RefreshRates()
{
    if (m_symbol.empty()) return false;
    m_tick = TradeContext::Instance().GetLastTick(m_symbol);
    return true;
}

double CSymbolInfo::Bid() const { return m_tick.bid; }
double CSymbolInfo::Ask() const { return m_tick.ask; }
double CSymbolInfo::Last() const { return m_tick.last; }

double CSymbolInfo::Point() const {
    return TradeContext::Instance().SymbolInfoDouble(m_symbol, SYMBOL_POINT);
}

int CSymbolInfo::Digits() const {
    return static_cast<int>(TradeContext::Instance().SymbolInfoInteger(m_symbol, SYMBOL_DIGITS));
}

double CSymbolInfo::ContractSize() const {
    return TradeContext::Instance().SymbolInfoDouble(m_symbol, SYMBOL_TRADE_CONTRACT_SIZE);
}

double CSymbolInfo::LotsMin() const { return 0.01; }
double CSymbolInfo::LotsMax() const { return 100.0; }
double CSymbolInfo::LotsStep() const { return 0.01; }

double CSymbolInfo::SwapLong() const {
    return TradeContext::Instance().SymbolInfoDouble(m_symbol, SYMBOL_SWAP_LONG);
}

double CSymbolInfo::SwapShort() const {
    return TradeContext::Instance().SymbolInfoDouble(m_symbol, SYMBOL_SWAP_SHORT);
}

int CSymbolInfo::SwapMode() const { return 1; }
int CSymbolInfo::SwapRollover3Days() const { return 3; }

double CSymbolInfo::MarginInitial() const { return 0; }
double CSymbolInfo::MarginMaintenance() const { return 0; }

int CSymbolInfo::Spread() const {
    return static_cast<int>(TradeContext::Instance().SymbolInfoInteger(m_symbol, SYMBOL_SPREAD));
}

bool CSymbolInfo::SpreadFloat() const { return true; }
bool CSymbolInfo::TradeAllowed() const { return true; }
int CSymbolInfo::FreezeLevel() const { return 0; }
int CSymbolInfo::StopsLevel() const { return 0; }

double CSymbolInfo::NormalizePrice(double price) const {
    return NormalizeDouble(price, Digits());
}

bool CSymbolInfo::CheckVolume(double volume) const {
    return volume >= LotsMin() && volume <= LotsMax();
}

// =============================================================================
// CAccountInfo Implementation
// =============================================================================

CAccountInfo::CAccountInfo()
{
}

long CAccountInfo::Login() const { return 0; }
long CAccountInfo::Leverage() const { return TradeContext::Instance().AccountLeverage(); }
bool CAccountInfo::TradeAllowed() const { return true; }
bool CAccountInfo::TradeExpert() const { return true; }
int CAccountInfo::LimitOrders() const { return 500; }

double CAccountInfo::Balance() const { return TradeContext::Instance().AccountBalance(); }
double CAccountInfo::Credit() const { return 0; }
double CAccountInfo::Profit() const { return Equity() - Balance(); }
double CAccountInfo::Equity() const { return TradeContext::Instance().AccountEquity(); }
double CAccountInfo::Margin() const { return TradeContext::Instance().AccountMargin(); }
double CAccountInfo::FreeMargin() const { return TradeContext::Instance().AccountFreeMargin(); }

double CAccountInfo::MarginLevel() const {
    double margin = Margin();
    return margin > 0 ? Equity() / margin * 100.0 : 0;
}

double CAccountInfo::MarginCall() const { return 100; }
double CAccountInfo::MarginStopOut() const { return 20; }

std::string CAccountInfo::Name() const { return "Backtest Account"; }
std::string CAccountInfo::Server() const { return "Local"; }
std::string CAccountInfo::Currency() const { return TradeContext::Instance().AccountCurrency(); }
std::string CAccountInfo::Company() const { return "BacktestPro"; }

double CAccountInfo::OrderProfitCheck(const std::string& symbol, ENUM_ORDER_TYPE type,
                                      double volume, double price_open, double price_close) const
{
    double contract_size = TradeContext::Instance().SymbolInfoDouble(symbol, SYMBOL_TRADE_CONTRACT_SIZE);
    double diff = (type == ORDER_TYPE_BUY) ?
        (price_close - price_open) : (price_open - price_close);
    return diff * volume * contract_size;
}

double CAccountInfo::MarginCheck(const std::string& symbol, ENUM_ORDER_TYPE type,
                                 double volume, double price) const
{
    double contract_size = TradeContext::Instance().SymbolInfoDouble(symbol, SYMBOL_TRADE_CONTRACT_SIZE);
    return volume * contract_size * price / Leverage();
}

double CAccountInfo::FreeMarginCheck(const std::string& symbol, ENUM_ORDER_TYPE type,
                                     double volume, double price) const
{
    return FreeMargin() - MarginCheck(symbol, type, volume, price);
}

double CAccountInfo::MaxLotCheck(const std::string& symbol, ENUM_ORDER_TYPE type,
                                 double price, double percent) const
{
    double contract_size = TradeContext::Instance().SymbolInfoDouble(symbol, SYMBOL_TRADE_CONTRACT_SIZE);
    double available = FreeMargin() * percent / 100.0;
    double margin_per_lot = contract_size * price / Leverage();
    return margin_per_lot > 0 ? available / margin_per_lot : 0;
}

} // namespace mql5
