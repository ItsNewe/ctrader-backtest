/**
 * @file trade_types.h
 * @brief Trading enums, structures, and object pools for backtest engine.
 *
 * This file provides core trading types used throughout the backtesting system:
 * - TradeDirection, PendingOrderType, AccountMode enums
 * - Trade and PendingOrder structures
 * - TradePool for efficient object allocation
 *
 * @author ctrader-backtest project
 * @version 1.0
 * @date 2025
 */

#ifndef TRADE_TYPES_H
#define TRADE_TYPES_H

#include <string>
#include <vector>
#include <cstdint>

namespace backtest {

/**
 * @brief Trade direction enum.
 *
 * Replaces string comparison ("BUY"/"SELL") for 10x+ speedup in hot paths.
 * Single byte storage for cache efficiency.
 */
enum class TradeDirection : uint8_t {
    BUY = 0,   ///< Long position (profit when price rises)
    SELL = 1   ///< Short position (profit when price falls)
};

// Helper to convert enum to string for output
inline const char* TradeDirectionStr(TradeDirection dir) {
    return dir == TradeDirection::BUY ? "BUY" : "SELL";
}

/**
 * @brief Pending order types matching MT5 ORDER_TYPE_* enumeration.
 *
 * @details
 * - **Limit orders**: Execute at specified price or better
 * - **Stop orders**: Execute when price reaches trigger level (market order)
 * - **Stop-Limit orders**: Two-phase execution - stop triggers, then limit waits
 *
 * @see PlacePendingOrder()
 */
enum class PendingOrderType : uint8_t {
    BUY_LIMIT = 0,       ///< Buy when ask <= trigger_price (waiting for dip)
    BUY_STOP = 1,        ///< Buy when ask >= trigger_price (breakout entry)
    SELL_LIMIT = 2,      ///< Sell when bid >= trigger_price (waiting for spike)
    SELL_STOP = 3,       ///< Sell when bid <= trigger_price (breakdown entry)
    BUY_STOP_LIMIT = 4,  ///< Activates BUY_LIMIT when price reaches stop level
    SELL_STOP_LIMIT = 5  ///< Activates SELL_LIMIT when price reaches stop level
};

inline const char* PendingOrderTypeStr(PendingOrderType type) {
    switch (type) {
        case PendingOrderType::BUY_LIMIT: return "BUY_LIMIT";
        case PendingOrderType::BUY_STOP: return "BUY_STOP";
        case PendingOrderType::SELL_LIMIT: return "SELL_LIMIT";
        case PendingOrderType::SELL_STOP: return "SELL_STOP";
        case PendingOrderType::BUY_STOP_LIMIT: return "BUY_STOP_LIMIT";
        case PendingOrderType::SELL_STOP_LIMIT: return "SELL_STOP_LIMIT";
        default: return "UNKNOWN";
    }
}

/**
 * Account mode (MT5 hedging vs netting)
 */
enum class AccountMode : uint8_t {
    HEDGING = 0,   // Multiple positions per symbol allowed (default)
    NETTING = 1    // One aggregated position per symbol
};

/**
 * Commission calculation mode
 */
enum class CommissionMode : uint8_t {
    PER_LOT = 0,           // Fixed amount per lot (default)
    PERCENT_OF_VOLUME = 1, // Percentage of deal volume
    PER_DEAL = 2           // Fixed amount per deal (entry + exit)
};

/**
 * Swap calculation mode (matches MT5 SYMBOL_SWAP_MODE_*)
 */
enum class SwapMode : uint8_t {
    DISABLED = 0,           // No swap
    POINTS = 1,             // In points (converted to account currency)
    CURRENCY_SYMBOL = 2,    // In account currency per lot (default)
    INTEREST = 3,           // Annual interest rate as percentage
    MARGIN_CURRENCY = 4     // In margin currency per lot
};

/**
 * Order filling type (matches MT5)
 */
enum class FillingType : uint8_t {
    FOK = 0,    // Fill or Kill - complete fill or cancel
    IOC = 1,    // Immediate or Cancel - fill what's available
    RETURN = 2  // Return - partial fill allowed, remainder stays
};

/**
 * Order expiration type
 */
enum class ExpirationType : uint8_t {
    GTC = 0,        // Good Till Cancelled
    DAY = 1,        // Valid for current trading day
    SPECIFIED = 2,  // Valid until specified datetime
    SPECIFIED_DAY = 3  // Valid until specified day
};

/**
 * Slippage model types for realistic execution simulation
 */
enum class SlippageModel : uint8_t {
    FIXED = 0,           // Fixed slippage in pips (classic model)
    VOLUME_BASED = 1,    // Slippage increases with position size
    VOLATILITY_BASED = 2 // Slippage increases during high volatility
};

/**
 * Pending order structure for limit/stop/stop-limit orders
 */
struct PendingOrder {
    int id;
    std::string symbol;
    PendingOrderType type;
    double trigger_price;      // Price at which order activates (stop level for stop-limit)
    double limit_price;        // Limit price for stop-limit orders (0 = use trigger_price)
    double lot_size;
    double stop_loss;
    double take_profit;
    std::string created_time;
    std::string expiry_time;   // Optional: order expiration (empty = GTC)
    ExpirationType expiry_type = ExpirationType::GTC;
    bool stop_limit_activated = false;  // For stop-limit: has the stop been triggered?

    PendingOrder() : id(0), type(PendingOrderType::BUY_LIMIT),
                     trigger_price(0), limit_price(0), lot_size(0),
                     stop_loss(0), take_profit(0), stop_limit_activated(false) {}

    bool IsBuyOrder() const {
        return type == PendingOrderType::BUY_LIMIT ||
               type == PendingOrderType::BUY_STOP ||
               type == PendingOrderType::BUY_STOP_LIMIT;
    }
    bool IsSellOrder() const {
        return type == PendingOrderType::SELL_LIMIT ||
               type == PendingOrderType::SELL_STOP ||
               type == PendingOrderType::SELL_STOP_LIMIT;
    }
    bool IsLimitOrder() const {
        return type == PendingOrderType::BUY_LIMIT || type == PendingOrderType::SELL_LIMIT;
    }
    bool IsStopOrder() const {
        return type == PendingOrderType::BUY_STOP || type == PendingOrderType::SELL_STOP;
    }
    bool IsStopLimitOrder() const {
        return type == PendingOrderType::BUY_STOP_LIMIT || type == PendingOrderType::SELL_STOP_LIMIT;
    }
};

/**
 * @brief Trade structure representing an open or closed position.
 *
 * Contains all information about a trading position including entry/exit
 * prices, timing, P/L, and optional trailing stop parameters.
 *
 * @note Trades are allocated from TradePool for performance. Do not use
 *       `new Trade()` directly - use OpenMarketOrder() instead.
 */
struct Trade {
    int id;                         ///< Unique trade identifier
    std::string symbol;             ///< Trading symbol (e.g., "XAUUSD")
    TradeDirection direction;       ///< BUY or SELL (single byte comparison)
    double entry_price;             ///< Price at which position was opened
    std::string entry_time;         ///< Timestamp of entry (YYYY.MM.DD HH:MM:SS.mmm)
    double exit_price;              ///< Price at which position was closed (0 if open)
    std::string exit_time;          ///< Timestamp of exit (empty if open)
    double lot_size;                ///< Position size in lots
    double stop_loss;               ///< Stop loss price (0 = no SL)
    double take_profit;             ///< Take profit price (0 = no TP)
    double profit_loss;             ///< Realized P/L after close (includes commission)
    double commission;              ///< Commission charged for this trade
    std::string exit_reason;        ///< Why trade was closed (Manual, SL, TP, StopOut)

    /// @name Trailing Stop Parameters
    /// @{
    double trailing_stop_distance = 0.0;    ///< Distance in price units (0 = disabled)
    double trailing_stop_activation = 0.0;  ///< Profit threshold before trailing activates (0 = immediate)
    double highest_profit_price = 0.0;      ///< Best price seen for trailing calculation
    bool trailing_activated = false;        ///< Whether trailing stop is currently active
    /// @}

    Trade() : id(0), direction(TradeDirection::BUY), entry_price(0), exit_price(0), lot_size(0),
              stop_loss(0), take_profit(0), profit_loss(0), commission(0),
              trailing_stop_distance(0), trailing_stop_activation(0), highest_profit_price(0),
              trailing_activated(false) {}

    /// @brief Get direction as string for display ("BUY" or "SELL")
    const char* GetDirectionStr() const { return TradeDirectionStr(direction); }

    /// @brief Check if this is a long position
    bool IsBuy() const { return direction == TradeDirection::BUY; }

    /// @brief Check if this is a short position
    bool IsSell() const { return direction == TradeDirection::SELL; }

    /// @brief Check if trailing stop is enabled for this trade
    bool HasTrailingStop() const { return trailing_stop_distance > 0; }
};

/**
 * @brief Object pool for Trade allocation.
 *
 * Reduces allocation overhead by reusing Trade objects instead of new/delete.
 * Especially beneficial when strategies open/close thousands of trades.
 *
 * @note Thread-safe version would require mutex around Allocate/Release.
 */
class TradePool {
public:
    explicit TradePool(size_t initial_capacity = 256) {
        pool_.reserve(initial_capacity);
        free_list_.reserve(initial_capacity);
    }

    ~TradePool() {
        for (Trade* t : pool_) {
            delete t;
        }
    }

    Trade* Allocate() {
        if (!free_list_.empty()) {
            Trade* t = free_list_.back();
            free_list_.pop_back();
            return t;
        }
        Trade* t = new Trade();
        pool_.push_back(t);
        return t;
    }

    void Release(Trade* t) {
        if (t) {
            // Reset trade state for reuse
            t->id = 0;
            t->symbol.clear();
            t->direction = TradeDirection::BUY;
            t->entry_price = 0;
            t->exit_price = 0;
            t->lot_size = 0;
            t->stop_loss = 0;
            t->take_profit = 0;
            t->profit_loss = 0;
            t->commission = 0;
            t->entry_time.clear();
            t->exit_time.clear();
            t->exit_reason.clear();
            t->trailing_stop_distance = 0;
            t->trailing_stop_activation = 0;
            t->highest_profit_price = 0;
            t->trailing_activated = false;
            free_list_.push_back(t);
        }
    }

    size_t GetPoolSize() const { return pool_.size(); }
    size_t GetFreeCount() const { return free_list_.size(); }
    size_t GetActiveCount() const { return pool_.size() - free_list_.size(); }

private:
    std::vector<Trade*> pool_;
    std::vector<Trade*> free_list_;
};

} // namespace backtest

#endif // TRADE_TYPES_H
