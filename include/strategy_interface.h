#ifndef STRATEGY_INTERFACE_H
#define STRATEGY_INTERFACE_H

#include "tick_data.h"
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <memory>

namespace backtest {

// Forward declaration
class TickBasedEngine;

/**
 * Strategy Context
 *
 * Provides a clean interface for strategies to interact with the market
 * without exposing engine internals. Supports both backtest and live trading.
 */
class StrategyContext {
public:
    explicit StrategyContext(TickBasedEngine& engine) : engine_(engine) {}

    // ==================== Market Data ====================

    /**
     * Get current tick for primary symbol
     */
    const Tick& GetTick() const;

    /**
     * Get current bid price
     */
    double Bid() const;

    /**
     * Get current ask price
     */
    double Ask() const;

    /**
     * Get mid price
     */
    double Mid() const;

    /**
     * Get current spread in pips
     */
    double SpreadPips() const;

    /**
     * Get current timestamp
     */
    const std::string& Timestamp() const;

    // ==================== Account ====================

    /**
     * Get current balance (realized P/L)
     */
    double Balance() const;

    /**
     * Get current equity (balance + unrealized P/L)
     */
    double Equity() const;

    /**
     * Get used margin
     */
    double UsedMargin() const;

    /**
     * Get free margin
     */
    double FreeMargin() const;

    /**
     * Get margin level percentage
     */
    double MarginLevel() const;

    // ==================== Positions ====================

    /**
     * Get count of open positions
     */
    int PositionCount() const;

    /**
     * Get total open lot size (for primary symbol)
     */
    double TotalLots() const;

    /**
     * Check if any positions are open
     */
    bool HasPositions() const;

    /**
     * Get lowest buy entry price among open positions
     */
    double LowestBuyPrice() const;

    /**
     * Get highest sell entry price among open positions
     */
    double HighestSellPrice() const;

    /**
     * Get average entry price of open positions
     */
    double AverageEntryPrice() const;

    /**
     * Get unrealized P/L of all open positions
     */
    double UnrealizedPL() const;

    // ==================== Trading ====================

    /**
     * Open a buy position at market price
     * @param lots Position size in lots
     * @param sl Stop loss price (0 = no SL)
     * @param tp Take profit price (0 = no TP)
     * @return Position ID or -1 on failure
     */
    int Buy(double lots, double sl = 0.0, double tp = 0.0);

    /**
     * Open a sell position at market price
     */
    int Sell(double lots, double sl = 0.0, double tp = 0.0);

    /**
     * Close a specific position by ID
     * @return true if closed successfully
     */
    bool ClosePosition(int position_id, const std::string& reason = "Strategy");

    /**
     * Close all open positions
     */
    void CloseAllPositions(const std::string& reason = "Strategy");

    /**
     * Modify position SL/TP
     */
    bool ModifyPosition(int position_id, double new_sl, double new_tp);

    // ==================== Parameters ====================

    /**
     * Get a strategy parameter by name
     */
    double GetParam(const std::string& name) const;

    /**
     * Set parameters (called by optimizer)
     */
    void SetParams(const std::map<std::string, double>& params);

    // ==================== Utilities ====================

    /**
     * Get contract size for symbol
     */
    double ContractSize() const;

    /**
     * Get pip size for symbol
     */
    double PipSize() const;

    /**
     * Get leverage
     */
    double Leverage() const;

    /**
     * Log a message (for debugging)
     */
    void Log(const std::string& message) const;

private:
    TickBasedEngine& engine_;
    std::map<std::string, double> params_;
};

/**
 * Strategy Interface
 *
 * Base class for all strategies. Implement this to create a new strategy
 * that works with both backtesting and live trading.
 */
class IStrategy {
public:
    virtual ~IStrategy() = default;

    /**
     * Called once when strategy is initialized
     */
    virtual void OnInit(StrategyContext& ctx) {}

    /**
     * Called for each tick
     */
    virtual void OnTick(const Tick& tick, StrategyContext& ctx) = 0;

    /**
     * Called when a trade is opened
     */
    virtual void OnTradeOpened(int position_id, StrategyContext& ctx) {}

    /**
     * Called when a trade is closed
     */
    virtual void OnTradeClosed(int position_id, double profit_loss, StrategyContext& ctx) {}

    /**
     * Called at end of trading day
     */
    virtual void OnDayEnd(const std::string& date, StrategyContext& ctx) {}

    /**
     * Called when backtest/session ends
     */
    virtual void OnDeinit(StrategyContext& ctx) {}

    /**
     * Get strategy name
     */
    virtual std::string GetName() const = 0;

    /**
     * Get strategy version
     */
    virtual std::string GetVersion() const { return "1.0"; }

    /**
     * Get parameter definitions for optimization
     */
    virtual std::map<std::string, std::pair<double, double>> GetParamRanges() const {
        return {};  // Override to define optimizable parameters
    }

    /**
     * Get default parameter values
     */
    virtual std::map<std::string, double> GetDefaultParams() const {
        return {};
    }
};

/**
 * Strategy Factory
 *
 * Creates strategy instances. Used by the engine and optimizer.
 */
using StrategyFactory = std::function<std::unique_ptr<IStrategy>(const std::map<std::string, double>& params)>;

/**
 * Strategy Registry
 *
 * Global registry for strategy types. Allows loading strategies by name.
 */
class StrategyRegistry {
public:
    static StrategyRegistry& Instance() {
        static StrategyRegistry instance;
        return instance;
    }

    void Register(const std::string& name, StrategyFactory factory) {
        factories_[name] = factory;
    }

    std::unique_ptr<IStrategy> Create(const std::string& name, const std::map<std::string, double>& params = {}) {
        auto it = factories_.find(name);
        if (it == factories_.end()) {
            return nullptr;
        }
        return it->second(params);
    }

    std::vector<std::string> GetRegisteredStrategies() const {
        std::vector<std::string> names;
        for (const auto& [name, _] : factories_) {
            names.push_back(name);
        }
        return names;
    }

private:
    std::map<std::string, StrategyFactory> factories_;
    StrategyRegistry() = default;
};

/**
 * Helper macro to register a strategy
 *
 * Usage:
 *   REGISTER_STRATEGY(MyStrategy, "my_strategy");
 */
#define REGISTER_STRATEGY(StrategyClass, name) \
    static bool _registered_##StrategyClass = []() { \
        backtest::StrategyRegistry::Instance().Register(name, \
            [](const std::map<std::string, double>& params) { \
                auto strategy = std::make_unique<StrategyClass>(); \
                return strategy; \
            }); \
        return true; \
    }()

// ==================== Implementation ====================

// Note: These implementations require TickBasedEngine to be fully defined
// They would typically be in a .cpp file, but for header-only, we inline them

inline const Tick& StrategyContext::GetTick() const {
    return engine_.GetCurrentTick();
}

inline double StrategyContext::Bid() const {
    return engine_.GetCurrentTick().bid;
}

inline double StrategyContext::Ask() const {
    return engine_.GetCurrentTick().ask;
}

inline double StrategyContext::Mid() const {
    return engine_.GetCurrentTick().mid();
}

inline double StrategyContext::SpreadPips() const {
    const auto& tick = engine_.GetCurrentTick();
    // Assuming pip size is accessible from config
    return (tick.ask - tick.bid) / 0.01;  // For XAUUSD
}

inline const std::string& StrategyContext::Timestamp() const {
    return engine_.GetCurrentTick().timestamp;
}

inline double StrategyContext::Balance() const {
    return engine_.GetBalance();
}

inline double StrategyContext::Equity() const {
    return engine_.GetEquity();
}

inline int StrategyContext::PositionCount() const {
    return static_cast<int>(engine_.GetOpenPositions().size());
}

inline double StrategyContext::TotalLots() const {
    double total = 0.0;
    for (const auto* pos : engine_.GetOpenPositions()) {
        total += pos->lot_size;
    }
    return total;
}

inline bool StrategyContext::HasPositions() const {
    return !engine_.GetOpenPositions().empty();
}

inline double StrategyContext::LowestBuyPrice() const {
    double lowest = 1e18;
    for (const auto* pos : engine_.GetOpenPositions()) {
        if (pos->direction == "BUY" && pos->entry_price < lowest) {
            lowest = pos->entry_price;
        }
    }
    return (lowest < 1e17) ? lowest : 0.0;
}

inline double StrategyContext::HighestSellPrice() const {
    double highest = 0.0;
    for (const auto* pos : engine_.GetOpenPositions()) {
        if (pos->direction == "SELL" && pos->entry_price > highest) {
            highest = pos->entry_price;
        }
    }
    return highest;
}

inline double StrategyContext::AverageEntryPrice() const {
    if (engine_.GetOpenPositions().empty()) return 0.0;

    double sum = 0.0;
    double total_lots = 0.0;
    for (const auto* pos : engine_.GetOpenPositions()) {
        sum += pos->entry_price * pos->lot_size;
        total_lots += pos->lot_size;
    }
    return (total_lots > 0) ? sum / total_lots : 0.0;
}

inline int StrategyContext::Buy(double lots, double sl, double tp) {
    auto* trade = engine_.OpenMarketOrder("BUY", lots, sl, tp);
    return trade ? trade->id : -1;
}

inline int StrategyContext::Sell(double lots, double sl, double tp) {
    auto* trade = engine_.OpenMarketOrder("SELL", lots, sl, tp);
    return trade ? trade->id : -1;
}

inline bool StrategyContext::ClosePosition(int position_id, const std::string& reason) {
    for (auto* pos : engine_.GetOpenPositions()) {
        if (pos->id == position_id) {
            return engine_.ClosePosition(pos, reason);
        }
    }
    return false;
}

inline void StrategyContext::CloseAllPositions(const std::string& reason) {
    // Make a copy since we're modifying during iteration
    auto positions = engine_.GetOpenPositions();
    for (auto* pos : positions) {
        engine_.ClosePosition(pos, reason);
    }
}

inline double StrategyContext::GetParam(const std::string& name) const {
    auto it = params_.find(name);
    return (it != params_.end()) ? it->second : 0.0;
}

inline void StrategyContext::SetParams(const std::map<std::string, double>& params) {
    params_ = params;
}

inline void StrategyContext::Log(const std::string& message) const {
    std::cout << "[" << Timestamp() << "] " << message << std::endl;
}

} // namespace backtest

#endif // STRATEGY_INTERFACE_H
