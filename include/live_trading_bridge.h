#ifndef LIVE_TRADING_BRIDGE_H
#define LIVE_TRADING_BRIDGE_H

/**
 * @file live_trading_bridge.h
 * @brief Abstract interface for unified backtest/live trading
 *
 * STATUS: CONCEPTUAL ONLY - The live trading implementations are NOT complete.
 *
 * This file provides:
 * - IMarketInterface: Abstract interface for market operations
 * - BacktestMarket: Working implementation for backtesting
 * - MT5Market/CTraderMarket: STUB implementations (not functional)
 *
 * @warning The MT5Market and CTraderMarket classes are placeholders.
 * For live trading, use the MQL5 Expert Advisors in the /mt5/ folder.
 *
 * The vision was to enable the same strategy code for backtest and live,
 * but implementing proper broker connectivity requires:
 * - Platform-specific socket code
 * - TLS encryption
 * - Broker-specific API protocols
 * - Order routing and execution handling
 *
 * Current recommendation:
 * 1. Develop and validate strategies using TickBasedEngine (backtesting)
 * 2. Translate to MQL5 for live MT5 trading (see /mt5/*.mq5 files)
 * 3. Or use cTrader Automate for cTrader live trading
 */

#include "tick_data.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>

namespace backtest {

// Forward declarations
class IMarketInterface;

/**
 * Order types
 */
enum class OrderType {
    MARKET_BUY,
    MARKET_SELL,
    LIMIT_BUY,
    LIMIT_SELL,
    STOP_BUY,
    STOP_SELL
};

/**
 * Order status
 */
enum class OrderStatus {
    PENDING,
    FILLED,
    PARTIALLY_FILLED,
    REJECTED,
    CANCELLED,
    EXPIRED
};

/**
 * Position information
 */
struct Position {
    uint64_t ticket;
    std::string symbol;
    bool is_buy;
    double lots;
    double open_price;
    double stop_loss;
    double take_profit;
    double current_profit;
    std::string open_time;
    std::string comment;
};

/**
 * Order request
 */
struct OrderRequest {
    std::string symbol;
    OrderType type;
    double lots;
    double price;           // For pending orders
    double stop_loss;
    double take_profit;
    std::string comment;
    uint64_t magic_number;  // EA identifier
};

/**
 * Order result
 */
struct OrderResult {
    bool success;
    OrderStatus status;
    uint64_t ticket;
    double filled_price;
    double filled_lots;
    std::string error_message;
    int error_code;

    static OrderResult Filled(uint64_t ticket, double price, double lots) {
        return {true, OrderStatus::FILLED, ticket, price, lots, "", 0};
    }

    static OrderResult Rejected(const std::string& reason, int code = 0) {
        return {false, OrderStatus::REJECTED, 0, 0, 0, reason, code};
    }
};

/**
 * Account information
 */
struct AccountInfo {
    double balance;
    double equity;
    double margin;
    double free_margin;
    double margin_level;    // Equity/Margin * 100
    std::string currency;
    int leverage;
};

/**
 * Market interface - abstract base for backtest and live trading
 *
 * Strategy code uses this interface, making it agnostic to whether
 * it's running in backtest or live mode.
 */
class IMarketInterface {
public:
    virtual ~IMarketInterface() = default;

    // Market data
    virtual Tick GetCurrentTick(const std::string& symbol) = 0;
    virtual double GetBid(const std::string& symbol) = 0;
    virtual double GetAsk(const std::string& symbol) = 0;
    virtual double GetSpread(const std::string& symbol) = 0;

    // Trading operations
    virtual OrderResult SendOrder(const OrderRequest& order) = 0;
    virtual bool ModifyPosition(uint64_t ticket, double sl, double tp) = 0;
    virtual bool ClosePosition(uint64_t ticket, double lots = 0) = 0;  // 0 = close all
    virtual bool CloseAllPositions(const std::string& symbol = "") = 0;

    // Position queries
    virtual std::vector<Position> GetPositions(const std::string& symbol = "") = 0;
    virtual Position GetPosition(uint64_t ticket) = 0;
    virtual int PositionCount(const std::string& symbol = "") = 0;
    virtual double TotalLots(const std::string& symbol = "") = 0;

    // Account information
    virtual AccountInfo GetAccountInfo() = 0;
    virtual double Balance() = 0;
    virtual double Equity() = 0;
    virtual double FreeMargin() = 0;

    // Time
    virtual std::string CurrentTime() = 0;
    virtual int DayOfWeek() = 0;
    virtual int Hour() = 0;
    virtual int Minute() = 0;

    // Symbol information
    virtual double PointValue(const std::string& symbol) = 0;
    virtual double ContractSize(const std::string& symbol) = 0;
    virtual double MinLot(const std::string& symbol) = 0;
    virtual double MaxLot(const std::string& symbol) = 0;
    virtual double LotStep(const std::string& symbol) = 0;

    // Connection status (for live trading)
    virtual bool IsConnected() = 0;
    virtual std::string GetLastError() = 0;
};

/**
 * Kill switch for emergency stop
 */
class KillSwitch {
public:
    KillSwitch() : triggered_(false), reason_("") {}

    void Trigger(const std::string& reason) {
        std::lock_guard<std::mutex> lock(mutex_);
        triggered_ = true;
        reason_ = reason;
        trigger_time_ = std::chrono::system_clock::now();
    }

    bool IsTriggered() const {
        return triggered_.load();
    }

    std::string GetReason() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return reason_;
    }

    void Reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        triggered_ = false;
        reason_ = "";
    }

private:
    std::atomic<bool> triggered_;
    std::string reason_;
    std::chrono::system_clock::time_point trigger_time_;
    mutable std::mutex mutex_;
};

/**
 * Trading session manager
 *
 * Wraps IMarketInterface with safety features:
 * - Kill switch support
 * - Max position limits
 * - Drawdown limits
 * - Error handling
 */
class TradingSession {
public:
    struct Config {
        int max_positions = 100;
        double max_lots_per_symbol = 10.0;
        double max_drawdown_pct = 50.0;
        double max_daily_loss = 0.0;      // 0 = disabled
        bool close_on_kill_switch = true;
        int max_consecutive_errors = 5;
    };

    TradingSession(std::shared_ptr<IMarketInterface> market)
        : market_(market), config_(), consecutive_errors_(0), daily_pnl_(0),
          session_start_balance_(0), is_running_(false) {}

    TradingSession(std::shared_ptr<IMarketInterface> market, const Config& config)
        : market_(market), config_(config), consecutive_errors_(0), daily_pnl_(0),
          session_start_balance_(0), is_running_(false) {}

    void Start() {
        session_start_balance_ = market_->Balance();
        daily_pnl_ = 0;
        consecutive_errors_ = 0;
        is_running_ = true;
    }

    void Stop() {
        is_running_ = false;
        if (config_.close_on_kill_switch && kill_switch_.IsTriggered()) {
            market_->CloseAllPositions();
        }
    }

    bool IsRunning() const { return is_running_ && !kill_switch_.IsTriggered(); }

    KillSwitch& GetKillSwitch() { return kill_switch_; }

    // Wrapped trading operations with safety checks
    OrderResult SendOrder(const OrderRequest& order) {
        if (!IsRunning()) {
            return OrderResult::Rejected("Session not running");
        }

        // Check max positions
        if (market_->PositionCount() >= config_.max_positions) {
            return OrderResult::Rejected("Max positions reached");
        }

        // Check max lots per symbol
        if (market_->TotalLots(order.symbol) + order.lots > config_.max_lots_per_symbol) {
            return OrderResult::Rejected("Max lots per symbol exceeded");
        }

        // Check drawdown
        double current_dd = (session_start_balance_ - market_->Equity()) / session_start_balance_ * 100;
        if (current_dd > config_.max_drawdown_pct) {
            kill_switch_.Trigger("Max drawdown exceeded: " + std::to_string(current_dd) + "%");
            return OrderResult::Rejected("Max drawdown exceeded");
        }

        // Execute order
        auto result = market_->SendOrder(order);

        if (!result.success) {
            consecutive_errors_++;
            if (consecutive_errors_ >= config_.max_consecutive_errors) {
                kill_switch_.Trigger("Max consecutive errors: " + std::to_string(consecutive_errors_));
            }
        } else {
            consecutive_errors_ = 0;
        }

        return result;
    }

    bool ModifyPosition(uint64_t ticket, double sl, double tp) {
        if (!IsRunning()) return false;
        return market_->ModifyPosition(ticket, sl, tp);
    }

    bool ClosePosition(uint64_t ticket, double lots = 0) {
        if (!IsRunning()) return false;
        return market_->ClosePosition(ticket, lots);
    }

    // Pass-through methods
    Tick GetCurrentTick(const std::string& symbol) { return market_->GetCurrentTick(symbol); }
    std::vector<Position> GetPositions(const std::string& symbol = "") { return market_->GetPositions(symbol); }
    AccountInfo GetAccountInfo() { return market_->GetAccountInfo(); }
    double Balance() { return market_->Balance(); }
    double Equity() { return market_->Equity(); }

private:
    std::shared_ptr<IMarketInterface> market_;
    Config config_;
    KillSwitch kill_switch_;
    int consecutive_errors_;
    double daily_pnl_;
    double session_start_balance_;
    bool is_running_;
};

/**
 * Backtest market implementation
 *
 * Implements IMarketInterface for backtesting.
 * Ticks are replayed from historical data.
 */
class BacktestMarket : public IMarketInterface {
public:
    struct Config {
        std::string symbol;
        double initial_balance;
        double contract_size;
        double leverage;
        double pip_size;
        double swap_long;
        double swap_short;
    };

    BacktestMarket(const Config& config)
        : config_(config), balance_(config.initial_balance),
          current_tick_idx_(0), next_ticket_(1) {}

    void SetTicks(const std::vector<Tick>& ticks) {
        ticks_ = ticks;
        current_tick_idx_ = 0;
    }

    bool HasMoreTicks() const {
        return current_tick_idx_ < ticks_.size();
    }

    void NextTick() {
        if (current_tick_idx_ < ticks_.size()) {
            current_tick_idx_++;
            UpdatePositions();
        }
    }

    // IMarketInterface implementation
    Tick GetCurrentTick(const std::string& /*symbol*/) override {
        if (current_tick_idx_ > 0 && current_tick_idx_ <= ticks_.size()) {
            return ticks_[current_tick_idx_ - 1];
        }
        return Tick{};
    }

    double GetBid(const std::string& symbol) override {
        return GetCurrentTick(symbol).bid;
    }

    double GetAsk(const std::string& symbol) override {
        return GetCurrentTick(symbol).ask;
    }

    double GetSpread(const std::string& symbol) override {
        auto tick = GetCurrentTick(symbol);
        return tick.ask - tick.bid;
    }

    OrderResult SendOrder(const OrderRequest& order) override {
        auto tick = GetCurrentTick(order.symbol);

        double fill_price;
        if (order.type == OrderType::MARKET_BUY) {
            fill_price = tick.ask;
        } else if (order.type == OrderType::MARKET_SELL) {
            fill_price = tick.bid;
        } else {
            return OrderResult::Rejected("Only market orders supported in backtest");
        }

        Position pos;
        pos.ticket = next_ticket_++;
        pos.symbol = order.symbol;
        pos.is_buy = (order.type == OrderType::MARKET_BUY);
        pos.lots = order.lots;
        pos.open_price = fill_price;
        pos.stop_loss = order.stop_loss;
        pos.take_profit = order.take_profit;
        pos.current_profit = 0;
        pos.open_time = tick.timestamp;
        pos.comment = order.comment;

        positions_.push_back(pos);

        return OrderResult::Filled(pos.ticket, fill_price, order.lots);
    }

    bool ModifyPosition(uint64_t ticket, double sl, double tp) override {
        for (auto& pos : positions_) {
            if (pos.ticket == ticket) {
                pos.stop_loss = sl;
                pos.take_profit = tp;
                return true;
            }
        }
        return false;
    }

    bool ClosePosition(uint64_t ticket, double lots = 0) override {
        auto tick = GetCurrentTick(config_.symbol);

        for (auto it = positions_.begin(); it != positions_.end(); ++it) {
            if (it->ticket == ticket) {
                double close_price = it->is_buy ? tick.bid : tick.ask;
                double close_lots = (lots > 0 && lots < it->lots) ? lots : it->lots;

                // Calculate profit
                double profit_points = it->is_buy
                    ? (close_price - it->open_price)
                    : (it->open_price - close_price);
                double profit = profit_points * close_lots * config_.contract_size;

                balance_ += profit;

                if (close_lots >= it->lots) {
                    positions_.erase(it);
                } else {
                    it->lots -= close_lots;
                }

                return true;
            }
        }
        return false;
    }

    bool CloseAllPositions(const std::string& symbol = "") override {
        std::vector<uint64_t> tickets;
        for (const auto& pos : positions_) {
            if (symbol.empty() || pos.symbol == symbol) {
                tickets.push_back(pos.ticket);
            }
        }
        for (auto ticket : tickets) {
            ClosePosition(ticket);
        }
        return true;
    }

    std::vector<Position> GetPositions(const std::string& symbol = "") override {
        if (symbol.empty()) return positions_;

        std::vector<Position> result;
        for (const auto& pos : positions_) {
            if (pos.symbol == symbol) {
                result.push_back(pos);
            }
        }
        return result;
    }

    Position GetPosition(uint64_t ticket) override {
        for (const auto& pos : positions_) {
            if (pos.ticket == ticket) return pos;
        }
        return Position{};
    }

    int PositionCount(const std::string& symbol = "") override {
        return static_cast<int>(GetPositions(symbol).size());
    }

    double TotalLots(const std::string& symbol = "") override {
        double total = 0;
        for (const auto& pos : GetPositions(symbol)) {
            total += pos.lots;
        }
        return total;
    }

    AccountInfo GetAccountInfo() override {
        AccountInfo info;
        info.balance = balance_;
        info.equity = CalculateEquity();
        info.margin = CalculateMargin();
        info.free_margin = info.equity - info.margin;
        info.margin_level = info.margin > 0 ? (info.equity / info.margin * 100) : 0;
        info.currency = "USD";
        info.leverage = static_cast<int>(config_.leverage);
        return info;
    }

    double Balance() override { return balance_; }
    double Equity() override { return CalculateEquity(); }
    double FreeMargin() override { return Equity() - CalculateMargin(); }

    std::string CurrentTime() override {
        auto tick = GetCurrentTick(config_.symbol);
        return tick.timestamp;
    }

    int DayOfWeek() override { return 0; }  // Simplified
    int Hour() override { return 0; }
    int Minute() override { return 0; }

    double PointValue(const std::string& /*symbol*/) override { return config_.pip_size; }
    double ContractSize(const std::string& /*symbol*/) override { return config_.contract_size; }
    double MinLot(const std::string& /*symbol*/) override { return 0.01; }
    double MaxLot(const std::string& /*symbol*/) override { return 100.0; }
    double LotStep(const std::string& /*symbol*/) override { return 0.01; }

    bool IsConnected() override { return true; }
    std::string GetLastError() override { return ""; }

private:
    void UpdatePositions() {
        auto tick = GetCurrentTick(config_.symbol);

        // Update profit and check SL/TP
        std::vector<uint64_t> to_close;

        for (auto& pos : positions_) {
            double close_price = pos.is_buy ? tick.bid : tick.ask;
            double profit_points = pos.is_buy
                ? (close_price - pos.open_price)
                : (pos.open_price - close_price);
            pos.current_profit = profit_points * pos.lots * config_.contract_size;

            // Check SL
            if (pos.stop_loss > 0) {
                if (pos.is_buy && tick.bid <= pos.stop_loss) {
                    to_close.push_back(pos.ticket);
                } else if (!pos.is_buy && tick.ask >= pos.stop_loss) {
                    to_close.push_back(pos.ticket);
                }
            }

            // Check TP
            if (pos.take_profit > 0) {
                if (pos.is_buy && tick.bid >= pos.take_profit) {
                    to_close.push_back(pos.ticket);
                } else if (!pos.is_buy && tick.ask <= pos.take_profit) {
                    to_close.push_back(pos.ticket);
                }
            }
        }

        for (auto ticket : to_close) {
            ClosePosition(ticket);
        }
    }

    double CalculateEquity() {
        double equity = balance_;
        for (const auto& pos : positions_) {
            equity += pos.current_profit;
        }
        return equity;
    }

    double CalculateMargin() {
        double margin = 0;
        auto tick = GetCurrentTick(config_.symbol);
        for (const auto& pos : positions_) {
            margin += (pos.lots * config_.contract_size * tick.bid) / config_.leverage;
        }
        return margin;
    }

    Config config_;
    std::vector<Tick> ticks_;
    std::vector<Position> positions_;
    double balance_;
    size_t current_tick_idx_;
    uint64_t next_ticket_;
};

/**
 * Strategy runner - executes strategy using IMarketInterface
 *
 * This allows the same strategy code to run in backtest or live mode.
 */
template<typename StrategyT>
class StrategyRunner {
public:
    using TickCallback = std::function<void(const Tick&, IMarketInterface&)>;

    StrategyRunner(std::shared_ptr<IMarketInterface> market, StrategyT strategy)
        : market_(market), strategy_(strategy) {}

    /**
     * Run strategy on backtest market
     */
    void RunBacktest(BacktestMarket& backtest_market) {
        while (backtest_market.HasMoreTicks()) {
            backtest_market.NextTick();
            auto tick = backtest_market.GetCurrentTick("");
            strategy_.OnTick(tick, backtest_market);
        }
    }

    /**
     * Process single tick (for live trading)
     */
    void ProcessTick(const Tick& tick) {
        strategy_.OnTick(tick, *market_);
    }

    IMarketInterface& GetMarket() { return *market_; }
    StrategyT& GetStrategy() { return strategy_; }

private:
    std::shared_ptr<IMarketInterface> market_;
    StrategyT strategy_;
};

} // namespace backtest

#endif // LIVE_TRADING_BRIDGE_H
