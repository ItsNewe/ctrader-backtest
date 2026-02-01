#ifndef TICK_BASED_ENGINE_H
#define TICK_BASED_ENGINE_H

#include "tick_data.h"
#include "tick_data_manager.h"
#include "position_validator.h"
#include "currency_converter.h"
#include "currency_rate_manager.h"
#include "simd_intrinsics.h"
#include <vector>
#include <memory>
#include <functional>
#include <iostream>
#include <cstring>
#include <iomanip>

namespace backtest {

/**
 * Trade direction enum - replaces string comparison for 10x+ speedup
 */
enum class TradeDirection : uint8_t {
    BUY = 0,
    SELL = 1
};

// Helper to convert enum to string for output
inline const char* TradeDirectionStr(TradeDirection dir) {
    return dir == TradeDirection::BUY ? "BUY" : "SELL";
}

/**
 * Pending order types (limit and stop orders)
 */
enum class PendingOrderType : uint8_t {
    BUY_LIMIT = 0,   // Buy at price below current (waiting for dip)
    BUY_STOP = 1,    // Buy at price above current (breakout)
    SELL_LIMIT = 2,  // Sell at price above current (waiting for spike)
    SELL_STOP = 3    // Sell at price below current (breakdown)
};

inline const char* PendingOrderTypeStr(PendingOrderType type) {
    switch (type) {
        case PendingOrderType::BUY_LIMIT: return "BUY_LIMIT";
        case PendingOrderType::BUY_STOP: return "BUY_STOP";
        case PendingOrderType::SELL_LIMIT: return "SELL_LIMIT";
        case PendingOrderType::SELL_STOP: return "SELL_STOP";
        default: return "UNKNOWN";
    }
}

/**
 * Pending order structure for limit/stop orders
 */
struct PendingOrder {
    int id;
    std::string symbol;
    PendingOrderType type;
    double trigger_price;      // Price at which order activates
    double lot_size;
    double stop_loss;
    double take_profit;
    std::string created_time;
    std::string expiry_time;   // Optional: order expiration (empty = GTC)

    PendingOrder() : id(0), type(PendingOrderType::BUY_LIMIT),
                     trigger_price(0), lot_size(0), stop_loss(0), take_profit(0) {}

    bool IsBuyOrder() const {
        return type == PendingOrderType::BUY_LIMIT || type == PendingOrderType::BUY_STOP;
    }
    bool IsSellOrder() const {
        return type == PendingOrderType::SELL_LIMIT || type == PendingOrderType::SELL_STOP;
    }
    bool IsLimitOrder() const {
        return type == PendingOrderType::BUY_LIMIT || type == PendingOrderType::SELL_LIMIT;
    }
    bool IsStopOrder() const {
        return type == PendingOrderType::BUY_STOP || type == PendingOrderType::SELL_STOP;
    }
};

/**
 * Simple trade structure for tick-based engine
 */
struct Trade {
    int id;
    std::string symbol;
    TradeDirection direction;  // Enum instead of string - single byte comparison
    double entry_price;
    std::string entry_time;
    double exit_price;
    std::string exit_time;
    double lot_size;
    double stop_loss;
    double take_profit;
    double profit_loss;
    double commission;
    std::string exit_reason;

    // Trailing stop support
    double trailing_stop_distance = 0.0;  // Distance in price units (0 = disabled)
    double trailing_stop_activation = 0.0; // Profit in price units before trailing activates (0 = immediate)
    double highest_profit_price = 0.0;    // Tracks best price for trailing (buy: highest bid, sell: lowest ask)
    bool trailing_activated = false;       // Whether trailing stop is currently active

    Trade() : id(0), direction(TradeDirection::BUY), entry_price(0), exit_price(0), lot_size(0),
              stop_loss(0), take_profit(0), profit_loss(0), commission(0),
              trailing_stop_distance(0), trailing_stop_activation(0), highest_profit_price(0),
              trailing_activated(false) {}

    // Helper for backward compatibility - returns direction as string
    const char* GetDirectionStr() const { return TradeDirectionStr(direction); }

    // Check direction helpers for cleaner code
    bool IsBuy() const { return direction == TradeDirection::BUY; }
    bool IsSell() const { return direction == TradeDirection::SELL; }

    // Check if this trade has trailing stop enabled
    bool HasTrailingStop() const { return trailing_stop_distance > 0; }
};

/**
 * Configuration for tick-based backtesting
 */
/**
 * Slippage model types for realistic execution simulation
 */
enum class SlippageModel : uint8_t {
    FIXED = 0,           // Fixed slippage in pips (classic model)
    VOLUME_BASED = 1,    // Slippage increases with position size
    VOLATILITY_BASED = 2 // Slippage increases during high volatility
};

struct TickBacktestConfig {
    std::string symbol = "EURUSD";
    double initial_balance = 10000.0;
    std::string account_currency = "USD";
    double commission_per_lot = 0.0;
    double slippage_pips = 0.0;
    bool use_bid_ask_spread = true;  // Use real bid/ask from ticks
    double contract_size = 100000.0;  // Contract size (100 for XAUUSD, 100000 for Forex)
    double leverage = 500.0;          // Leverage (1:500 for XAUUSD)
    double margin_rate = 1.0;         // Initial margin rate
    double pip_size = 0.00001;        // Pip size (0.00001 for 5-digit pairs, 0.001 for JPY, 0.01 for XAUUSD)
    double stop_out_level = 20.0;     // Margin level % below which stop-out occurs (MT5 default: 20%)

    // Advanced slippage model
    SlippageModel slippage_model = SlippageModel::FIXED;
    double slippage_volume_factor = 0.1;   // For VOLUME_BASED: extra pips per lot
    double slippage_volatility_factor = 1.0; // For VOLATILITY_BASED: multiplier based on recent price movement

    // Equity curve tracking
    bool track_equity_curve = true;        // Enable equity curve recording
    int equity_sample_interval = 1000;      // Sample equity every N ticks (reduces memory for long backtests)

    // Date filtering (MT5 behavior: start_date inclusive, end_date exclusive)
    std::string start_date = "";      // Format: YYYY.MM.DD (empty = no filter)
    std::string end_date = "";        // Format: YYYY.MM.DD (empty = no filter)

    // Swap/Rollover fees
    double swap_long = 0.0;           // Swap for long positions (per lot per day)
    double swap_short = 0.0;          // Swap for short positions (per lot per day)
    int swap_mode = 2;                // 2 = SYMBOL_SWAP_MODE_CURRENCY_SYMBOL (in account currency)
    int swap_3days = 3;               // Day of week for triple swap (0=Sun, 1=Mon, 2=Tue, 3=Wed, 4=Thu, 5=Fri, 6=Sat)
    bool swap_divide_by_price = false; // For pairs where quote currency != account currency (e.g., USDJPY with USD account)

    // Market session settings
    // For Gold (XAUUSD): trades Mon-Fri only (trading_days = 0b0111110 = 62)
    // For Crypto: trades 7 days (trading_days = 0b1111111 = 127)
    int trading_days = 62;            // Bitmask: bit 0=Sun, bit 1=Mon, ... bit 6=Sat (default: Mon-Fri)
    int market_close_hour = 23;       // Hour when market closes (server time)
    int market_open_hour = 0;         // Hour when market opens (server time)

    // Tick data source
    TickDataConfig tick_data_config;

    // Output control
    bool verbose = true;              // Print trade open/close messages

    TickBacktestConfig() = default;
};

/**
 * Tick-based backtest engine for high-precision strategy testing
 * Processes every tick for exact order execution
 */
class TickBasedEngine {
public:
    explicit TickBasedEngine(const TickBacktestConfig& config)
        : config_(config),
          balance_(config.initial_balance),
          equity_(config.initial_balance),
          validator_(),
          converter_(config.account_currency),
          rate_manager_(config.account_currency, 60),
          tick_manager_(config.tick_data_config),
          peak_equity_(config.initial_balance) {
        // Initialize SIMD CPU feature detection
        simd::init();
    }

    // Strategy callback signature
    // Called for each tick with current tick data
    using StrategyCallback = std::function<void(const Tick& tick, TickBasedEngine& engine)>;

    // Run backtest with strategy
    void Run(StrategyCallback strategy) {
        if (config_.verbose) {
            std::cout << "=== Tick-Based Backtest Started ===" << std::endl;
            std::cout << "Symbol: " << config_.symbol << std::endl;
            std::cout << "Initial Balance: $" << balance_ << std::endl;
        }

        tick_manager_.Reset();
        Tick tick;
        size_t tick_count = 0;
        size_t progress_interval = 10000;

        while (tick_manager_.GetNextTick(tick)) {
            current_tick_ = tick;

            // Date filtering (MT5 behavior: start inclusive, end exclusive)
            if (!config_.start_date.empty() && tick.timestamp < config_.start_date) {
                continue;  // Skip ticks before start date
            }
            if (!config_.end_date.empty() && tick.timestamp >= config_.end_date) {
                break;  // Stop at end date (exclusive)
            }

            tick_count++;

            // Process swap/rollover fees FIRST (MT5 applies swap after last tick of day,
            // like a ghost tick - price doesn't move, no TP/SL/trading, only swap applied)
            // By charging swap before UpdateEquity, the new day starts with swap already deducted
            ProcessSwap(tick);

            // Update equity with current tick prices
            UpdateEquity(tick);

            // Check for margin stop-out (must be done after UpdateEquity)
            CheckMarginStopOut(tick);
            if (stop_out_occurred_) {
                break;  // Exit backtest on margin stop-out
            }

            // Check pending orders and stop losses / take profits
            ProcessPendingOrders(tick);
            ProcessOpenPositions(tick);

            // Call strategy callback
            strategy(tick, *this);

            // Progress indicator
            if (config_.verbose && tick_count % progress_interval == 0) {
                std::cout << "Processed " << tick_count << " ticks... Equity: $" << equity_ << std::endl;
            }
        }

        if (config_.verbose) {
            std::cout << "\n=== Backtest Complete ===" << std::endl;
            std::cout << "Total Ticks Processed: " << tick_count << std::endl;
            PrintResults();
        }
    }

    // Run backtest with externally provided ticks (for parallel processing)
    void RunWithTicks(const std::vector<Tick>& ticks, StrategyCallback strategy) {
        size_t tick_count = 0;

        for (const auto& tick : ticks) {
            current_tick_ = tick;

            // Date filtering (MT5 behavior: start inclusive, end exclusive)
            if (!config_.start_date.empty() && tick.timestamp < config_.start_date) {
                continue;
            }
            if (!config_.end_date.empty() && tick.timestamp >= config_.end_date) {
                break;
            }

            tick_count++;

            // Swap first (end-of-previous-day ghost tick model)
            ProcessSwap(tick);

            UpdateEquity(tick);
            CheckMarginStopOut(tick);
            if (stop_out_occurred_) {
                break;
            }

            ProcessPendingOrders(tick);
            ProcessOpenPositions(tick);
            strategy(tick, *this);
        }
    }

    // Trading operations

    /**
     * Open market order at current tick price
     */
    Trade* OpenMarketOrder(TradeDirection direction, double lot_size,
                           double stop_loss = 0.0, double take_profit = 0.0) {
        if (!current_tick_.timestamp.empty()) {
            const bool is_buy = (direction == TradeDirection::BUY);
            // Use bid for SELL, ask for BUY
            double entry_price = is_buy ? current_tick_.ask : current_tick_.bid;

            // Apply slippage based on configured model
            double slippage = CalculateSlippage(lot_size);
            entry_price += is_buy ? slippage : -slippage;

            Trade* trade = CreateTrade(direction, entry_price, lot_size, stop_loss, take_profit);
            open_positions_.push_back(trade);
            InvalidateSimdCache();  // Mark SIMD cache dirty
            total_trades_opened_++;

            // Uncomment for per-trade logging:
            // std::cout << current_tick_.timestamp << " - OPEN " << TradeDirectionStr(direction)
            //           << " " << lot_size << " lots @ " << entry_price << std::endl;

            return trade;
        }
        return nullptr;
    }

    // Backward compatible overload accepting string (converts to enum)
    Trade* OpenMarketOrder(const std::string& direction, double lot_size,
                           double stop_loss = 0.0, double take_profit = 0.0) {
        TradeDirection dir = (direction == "BUY") ? TradeDirection::BUY : TradeDirection::SELL;
        return OpenMarketOrder(dir, lot_size, stop_loss, take_profit);
    }

    /**
     * Place a pending order (limit or stop)
     * BUY_LIMIT: triggers when ask <= trigger_price (waiting for dip)
     * BUY_STOP: triggers when ask >= trigger_price (breakout)
     * SELL_LIMIT: triggers when bid >= trigger_price (waiting for spike)
     * SELL_STOP: triggers when bid <= trigger_price (breakdown)
     */
    int PlacePendingOrder(PendingOrderType type, double trigger_price, double lot_size,
                          double stop_loss = 0.0, double take_profit = 0.0,
                          const std::string& expiry = "") {
        PendingOrder order;
        order.id = next_pending_order_id_++;
        order.symbol = config_.symbol;
        order.type = type;
        order.trigger_price = trigger_price;
        order.lot_size = lot_size;
        order.stop_loss = stop_loss;
        order.take_profit = take_profit;
        order.created_time = current_tick_.timestamp;
        order.expiry_time = expiry;

        pending_orders_.push_back(order);

        if (config_.verbose) {
            std::cout << current_tick_.timestamp << " - PENDING " << PendingOrderTypeStr(type)
                      << " " << lot_size << " lots @ " << trigger_price << std::endl;
        }

        return order.id;
    }

    /**
     * Cancel a pending order by ID
     */
    bool CancelPendingOrder(int order_id) {
        for (size_t i = 0; i < pending_orders_.size(); ++i) {
            if (pending_orders_[i].id == order_id) {
                if (config_.verbose) {
                    std::cout << current_tick_.timestamp << " - CANCEL pending order #" << order_id << std::endl;
                }
                std::swap(pending_orders_[i], pending_orders_.back());
                pending_orders_.pop_back();
                return true;
            }
        }
        return false;
    }

    /**
     * Get all pending orders
     */
    const std::vector<PendingOrder>& GetPendingOrders() const { return pending_orders_; }

    /**
     * Set trailing stop on a trade
     * @param trade The trade to modify
     * @param distance Distance in price units to trail behind best price
     * @param activation_profit Profit (in price units) required before trailing activates (0 = immediate)
     */
    bool SetTrailingStop(Trade* trade, double distance, double activation_profit = 0.0) {
        if (!trade) return false;

        trade->trailing_stop_distance = distance;
        trade->trailing_stop_activation = activation_profit;
        trade->trailing_activated = (activation_profit <= 0);  // Activate immediately if no activation threshold

        // Initialize highest profit price
        if (trade->IsBuy()) {
            trade->highest_profit_price = current_tick_.bid;
        } else {
            trade->highest_profit_price = current_tick_.ask;
        }

        return true;
    }

    /**
     * Open market order with trailing stop
     */
    Trade* OpenMarketOrderWithTrailing(TradeDirection direction, double lot_size,
                                        double trailing_distance, double trailing_activation = 0.0,
                                        double take_profit = 0.0) {
        Trade* trade = OpenMarketOrder(direction, lot_size, 0.0, take_profit);
        if (trade) {
            SetTrailingStop(trade, trailing_distance, trailing_activation);
        }
        return trade;
    }

    /**
     * Close position at current tick price
     */
    bool ClosePosition(Trade* trade, const std::string& reason = "Manual") {
        if (!trade || current_tick_.timestamp.empty()) {
            return false;
        }

        const bool is_buy = trade->IsBuy();
        // Use ask for closing SELL, bid for closing BUY
        double exit_price = is_buy ? current_tick_.bid : current_tick_.ask;

        // Apply slippage
        if (config_.slippage_pips > 0) {
            double slippage = config_.slippage_pips * GetPipValue();
            exit_price += is_buy ? -slippage : slippage;
        }

        trade->exit_price = exit_price;
        trade->exit_time = current_tick_.timestamp;
        trade->exit_reason = reason;

        CalculateProfitLoss(trade);
        balance_ += trade->profit_loss;

        closed_trades_.push_back(*trade);

        // Remove from open positions - O(1) swap-and-pop instead of O(N) erase-remove
        for (size_t i = 0; i < open_positions_.size(); ++i) {
            if (open_positions_[i] == trade) {
                std::swap(open_positions_[i], open_positions_.back());
                open_positions_.pop_back();
                break;
            }
        }
        InvalidateSimdCache();  // Mark SIMD cache dirty

        if (config_.verbose) {
            std::cout << current_tick_.timestamp << " - CLOSE " << trade->GetDirectionStr()
                      << " @ " << exit_price << " | P/L: $" << trade->profit_loss
                      << " | Reason: " << reason << std::endl;
        }

        // Free memory - trade was allocated with new in CreateTrade
        delete trade;

        return true;
    }

    // Getters
    double GetBalance() const { return balance_; }
    double GetEquity() const { return equity_; }
    const Tick& GetCurrentTick() const { return current_tick_; }
    const std::vector<Trade*>& GetOpenPositions() const { return open_positions_; }
    const std::vector<Trade>& GetClosedTrades() const { return closed_trades_; }
    size_t GetTotalTrades() const { return closed_trades_.size(); }
    bool IsStopOutOccurred() const { return stop_out_occurred_; }

    // Get equity curve data
    const std::vector<double>& GetEquityCurve() const { return equity_curve_; }
    const std::vector<std::string>& GetEquityTimestamps() const { return equity_timestamps_; }

    // Results
    struct BacktestResults {
        double initial_balance;
        double final_balance;
        double total_profit_loss;
        size_t total_trades;
        size_t total_trades_opened;
        size_t winning_trades;
        size_t losing_trades;
        double win_rate;
        double average_win;
        double average_loss;
        double largest_win;
        double largest_loss;
        double max_drawdown;
        double max_drawdown_pct;
        double total_swap_charged;
        bool stop_out_occurred;

        // Risk metrics
        double sharpe_ratio;           // Annualized Sharpe ratio
        double sortino_ratio;          // Sortino ratio (downside deviation)
        double profit_factor;          // Gross profit / Gross loss
        double recovery_factor;        // Net profit / Max drawdown

        // Equity curve (if tracking enabled)
        std::vector<double> equity_curve;
        std::vector<std::string> equity_timestamps;
    };

    BacktestResults GetResults() const {
        BacktestResults results;
        results.initial_balance = config_.initial_balance;
        results.final_balance = balance_;
        results.total_profit_loss = balance_ - config_.initial_balance;
        results.total_trades = closed_trades_.size();

        double total_wins = 0.0;
        double total_losses = 0.0;
        results.winning_trades = 0;
        results.losing_trades = 0;
        results.largest_win = 0.0;
        results.largest_loss = 0.0;

        for (const auto& trade : closed_trades_) {
            if (trade.profit_loss > 0) {
                results.winning_trades++;
                total_wins += trade.profit_loss;
                results.largest_win = std::max(results.largest_win, trade.profit_loss);
            } else {
                results.losing_trades++;
                total_losses += trade.profit_loss;
                results.largest_loss = std::min(results.largest_loss, trade.profit_loss);
            }
        }

        results.win_rate = results.total_trades > 0
            ? (double)results.winning_trades / results.total_trades * 100.0
            : 0.0;
        results.average_win = results.winning_trades > 0
            ? total_wins / results.winning_trades
            : 0.0;
        results.average_loss = results.losing_trades > 0
            ? total_losses / results.losing_trades
            : 0.0;

        // Max drawdown is tracked during the backtest in UpdateEquity()
        results.max_drawdown = max_drawdown_;
        results.max_drawdown_pct = max_drawdown_percent_;

        results.total_swap_charged = total_swap_charged_;
        results.total_trades_opened = total_trades_opened_;
        results.stop_out_occurred = stop_out_occurred_;

        // Calculate profit factor
        double gross_profit = total_wins;
        double gross_loss = std::abs(total_losses);
        results.profit_factor = gross_loss > 0 ? gross_profit / gross_loss : 0.0;

        // Calculate recovery factor
        results.recovery_factor = max_drawdown_ > 0 ? results.total_profit_loss / max_drawdown_ : 0.0;

        // Calculate Sharpe ratio from daily returns
        results.sharpe_ratio = CalculateSharpeRatio();
        results.sortino_ratio = CalculateSortinoRatio();

        // Include equity curve if tracking was enabled
        results.equity_curve = equity_curve_;
        results.equity_timestamps = equity_timestamps_;

        return results;
    }

    // Calculate annualized Sharpe ratio from daily returns
    double CalculateSharpeRatio() const {
        if (daily_returns_.size() < 2) return 0.0;

        // Calculate mean daily return
        double sum = 0.0;
        for (double r : daily_returns_) sum += r;
        double mean = sum / daily_returns_.size();

        // Calculate standard deviation
        double variance = 0.0;
        for (double r : daily_returns_) {
            double diff = r - mean;
            variance += diff * diff;
        }
        double stddev = std::sqrt(variance / daily_returns_.size());

        if (stddev < 1e-10) return 0.0;

        // Annualize: multiply by sqrt(252 trading days)
        return (mean / stddev) * std::sqrt(252.0);
    }

    // Calculate Sortino ratio (only considers downside deviation)
    double CalculateSortinoRatio() const {
        if (daily_returns_.size() < 2) return 0.0;

        // Calculate mean daily return
        double sum = 0.0;
        for (double r : daily_returns_) sum += r;
        double mean = sum / daily_returns_.size();

        // Calculate downside deviation (only negative returns)
        double downside_sum = 0.0;
        int negative_count = 0;
        for (double r : daily_returns_) {
            if (r < 0) {
                downside_sum += r * r;
                negative_count++;
            }
        }

        if (negative_count == 0) return 0.0;
        double downside_dev = std::sqrt(downside_sum / negative_count);

        if (downside_dev < 1e-10) return 0.0;

        // Annualize
        return (mean / downside_dev) * std::sqrt(252.0);
    }

    void PrintResults() const {
        auto results = GetResults();

        std::cout << "\n=== Backtest Results ===" << std::endl;
        std::cout << "Initial Balance: $" << results.initial_balance << std::endl;
        std::cout << "Final Balance:   $" << results.final_balance << std::endl;
        std::cout << "Total P/L:       $" << results.total_profit_loss << std::endl;
        std::cout << "Total Trades:    " << results.total_trades << std::endl;
        std::cout << "Winning Trades:  " << results.winning_trades << std::endl;
        std::cout << "Losing Trades:   " << results.losing_trades << std::endl;
        std::cout << "Win Rate:        " << results.win_rate << "%" << std::endl;
        std::cout << "Average Win:     $" << results.average_win << std::endl;
        std::cout << "Average Loss:    $" << results.average_loss << std::endl;
        std::cout << "Largest Win:     $" << results.largest_win << std::endl;
        std::cout << "Largest Loss:    $" << results.largest_loss << std::endl;
        std::cout << "Max Drawdown:    $" << results.max_drawdown << " (" << max_drawdown_percent_ << "%)" << std::endl;
    }

private:
    TickBacktestConfig config_;
    double balance_;
    double equity_;
    PositionValidator validator_;
    CurrencyConverter converter_;
    CurrencyRateManager rate_manager_;
    TickDataManager tick_manager_;

    Tick current_tick_;
    std::vector<Trade*> open_positions_;
    std::vector<Trade> closed_trades_;
    std::vector<PendingOrder> pending_orders_;  // Limit and stop orders
    size_t next_trade_id_ = 1;
    size_t next_pending_order_id_ = 1;

    // Pre-allocated vectors for UpdateEquity (pool allocator pattern)
    mutable std::vector<double> pnl_buffer_buy_;
    mutable std::vector<double> pnl_buffer_sell_;

    // Swap tracking
    std::string last_swap_date_ = "";
    double total_swap_charged_ = 0.0;

    // Trade counting
    size_t total_trades_opened_ = 0;

    // Stop-out tracking
    bool stop_out_occurred_ = false;

    // Drawdown tracking
    double peak_equity_ = 0.0;
    double max_drawdown_ = 0.0;
    double max_drawdown_percent_ = 0.0;

    // Equity curve tracking for visualization and Sharpe calculation
    std::vector<double> equity_curve_;         // Sampled equity values
    std::vector<std::string> equity_timestamps_; // Corresponding timestamps
    size_t equity_sample_counter_ = 0;         // Counter for sampling interval

    // Daily returns for Sharpe ratio calculation
    std::vector<double> daily_returns_;
    std::string last_daily_date_ = "";
    double last_daily_equity_ = 0.0;

    // Volatility tracking for variable slippage
    std::vector<double> recent_prices_;        // Last N prices for volatility calculation
    static constexpr size_t VOLATILITY_WINDOW = 20;

    // SIMD-optimized position cache (updated when positions change)
    // Separates BUY and SELL positions for vectorized operations
    mutable bool simd_cache_dirty_ = true;
    mutable std::vector<double> buy_entry_prices_;
    mutable std::vector<double> buy_lot_sizes_;
    mutable std::vector<double> sell_entry_prices_;
    mutable std::vector<double> sell_lot_sizes_;
    mutable std::vector<Trade*> buy_positions_;
    mutable std::vector<Trade*> sell_positions_;

    // Refresh SIMD cache when positions have changed
    void RefreshSimdCache() const {
        if (!simd_cache_dirty_) return;

        buy_entry_prices_.clear();
        buy_lot_sizes_.clear();
        sell_entry_prices_.clear();
        sell_lot_sizes_.clear();
        buy_positions_.clear();
        sell_positions_.clear();

        for (Trade* trade : open_positions_) {
            if (trade->IsBuy()) {
                buy_entry_prices_.push_back(trade->entry_price);
                buy_lot_sizes_.push_back(trade->lot_size);
                buy_positions_.push_back(trade);
            } else {
                sell_entry_prices_.push_back(trade->entry_price);
                sell_lot_sizes_.push_back(trade->lot_size);
                sell_positions_.push_back(trade);
            }
        }

        simd_cache_dirty_ = false;
    }

    // Mark cache dirty when positions change
    void InvalidateSimdCache() {
        simd_cache_dirty_ = true;
    }

    double GetPipValue() const {
        // Use configurable pip size (0.00001 for forex, 0.01 for gold, 0.001 for JPY)
        return config_.pip_size;
    }

    Trade* CreateTrade(TradeDirection direction, double entry_price,
                       double lot_size, double sl, double tp) {
        Trade* trade = new Trade();
        trade->id = next_trade_id_++;
        trade->symbol = config_.symbol;
        trade->direction = direction;
        trade->entry_price = entry_price;
        trade->entry_time = current_tick_.timestamp;
        trade->lot_size = lot_size;
        trade->stop_loss = sl;
        trade->take_profit = tp;
        trade->commission = config_.commission_per_lot * lot_size;

        return trade;
    }

    void CalculateProfitLoss(Trade* trade) {
        double price_diff = trade->exit_price - trade->entry_price;
        if (trade->IsSell()) {
            price_diff = -price_diff;
        }

        // P/L = price_diff * lot_size * contract_size
        // For XAUUSD: contract_size = 100, for Forex pairs = 100,000
        double profit = price_diff * trade->lot_size * config_.contract_size;
        trade->profit_loss = profit - trade->commission;
    }

    /**
     * Calculate slippage based on configured model
     */
    double CalculateSlippage(double lot_size) const {
        double base_slippage = config_.slippage_pips * GetPipValue();

        switch (config_.slippage_model) {
            case SlippageModel::FIXED:
                return base_slippage;

            case SlippageModel::VOLUME_BASED:
                // Slippage increases with position size
                // Extra slippage = volume_factor * lot_size * pip_value
                return base_slippage + (config_.slippage_volume_factor * lot_size * GetPipValue());

            case SlippageModel::VOLATILITY_BASED:
                // Slippage increases during high volatility
                if (recent_prices_.size() >= 2) {
                    // Calculate recent volatility as average absolute price change
                    double volatility = 0.0;
                    for (size_t i = 1; i < recent_prices_.size(); ++i) {
                        volatility += std::abs(recent_prices_[i] - recent_prices_[i-1]);
                    }
                    volatility /= (recent_prices_.size() - 1);

                    // Normal volatility baseline (e.g., 0.10 for XAUUSD)
                    double normal_volatility = GetPipValue() * 10.0;
                    double volatility_multiplier = volatility / normal_volatility;

                    return base_slippage * (1.0 + config_.slippage_volatility_factor * (volatility_multiplier - 1.0));
                }
                return base_slippage;

            default:
                return base_slippage;
        }
    }

    void UpdateEquity(const Tick& tick) {
        equity_ = balance_;

        // SIMD-optimized P/L calculation for large position counts
        const size_t SIMD_THRESHOLD = 8;  // Use SIMD for 8+ positions

        if (open_positions_.size() >= SIMD_THRESHOLD && simd::has_avx2()) {
            RefreshSimdCache();

            // Process BUY positions with SIMD using pre-allocated buffer (pool allocator pattern)
            if (!buy_entry_prices_.empty()) {
                // Resize buffer only if needed (avoids allocation if already large enough)
                if (pnl_buffer_buy_.size() < buy_entry_prices_.size()) {
                    pnl_buffer_buy_.resize(buy_entry_prices_.size());
                }
                simd::calculate_pnl_batch(
                    buy_entry_prices_.data(),
                    buy_lot_sizes_.data(),
                    tick.bid,  // BUY positions close at bid
                    config_.contract_size,
                    pnl_buffer_buy_.data(),
                    buy_entry_prices_.size(),
                    true  // is_buy = true
                );
                equity_ += simd::sum(pnl_buffer_buy_.data(), buy_entry_prices_.size());
            }

            // Process SELL positions with SIMD using pre-allocated buffer
            if (!sell_entry_prices_.empty()) {
                if (pnl_buffer_sell_.size() < sell_entry_prices_.size()) {
                    pnl_buffer_sell_.resize(sell_entry_prices_.size());
                }
                simd::calculate_pnl_batch(
                    sell_entry_prices_.data(),
                    sell_lot_sizes_.data(),
                    tick.ask,  // SELL positions close at ask
                    config_.contract_size,
                    pnl_buffer_sell_.data(),
                    sell_entry_prices_.size(),
                    false  // is_buy = false
                );
                equity_ += simd::sum(pnl_buffer_sell_.data(), sell_entry_prices_.size());
            }
        } else {
            // Scalar fallback for small position counts
            for (Trade* trade : open_positions_) {
                const bool is_buy = trade->IsBuy();
                double current_price = is_buy ? tick.bid : tick.ask;
                double price_diff = current_price - trade->entry_price;
                if (!is_buy) {
                    price_diff = -price_diff;
                }
                double unrealized_pl = price_diff * trade->lot_size * config_.contract_size;
                equity_ += unrealized_pl;
            }
        }

        // Track max drawdown
        if (equity_ > peak_equity_) {
            peak_equity_ = equity_;
        }
        double current_drawdown = peak_equity_ - equity_;
        if (current_drawdown > max_drawdown_) {
            max_drawdown_ = current_drawdown;
            max_drawdown_percent_ = (peak_equity_ > 0) ? (current_drawdown / peak_equity_) * 100.0 : 0.0;
        }

        // Track equity curve (sampled at intervals to reduce memory)
        if (config_.track_equity_curve) {
            equity_sample_counter_++;
            if (equity_sample_counter_ >= static_cast<size_t>(config_.equity_sample_interval)) {
                equity_curve_.push_back(equity_);
                equity_timestamps_.push_back(tick.timestamp);
                equity_sample_counter_ = 0;
            }
        }

        // Track daily returns for Sharpe ratio calculation
        if (tick.timestamp.size() >= 10) {
            std::string current_date = tick.timestamp.substr(0, 10);  // YYYY.MM.DD
            if (current_date != last_daily_date_ && !last_daily_date_.empty()) {
                // New day - calculate daily return
                if (last_daily_equity_ > 0) {
                    double daily_return = (equity_ - last_daily_equity_) / last_daily_equity_;
                    daily_returns_.push_back(daily_return);
                }
                last_daily_equity_ = equity_;
            }
            if (last_daily_date_.empty()) {
                last_daily_equity_ = equity_;
            }
            last_daily_date_ = current_date;
        }

        // Track recent prices for volatility-based slippage
        if (config_.slippage_model == SlippageModel::VOLATILITY_BASED) {
            recent_prices_.push_back(tick.bid);
            if (recent_prices_.size() > VOLATILITY_WINDOW) {
                recent_prices_.erase(recent_prices_.begin());
            }
        }
    }

    void CheckMarginStopOut(const Tick& tick) {
        // Calculate current margin level
        // Margin Level = (Equity / Used Margin) × 100%
        // Stop out occurs when margin level falls below stop_out_level (typically 20%)

        if (open_positions_.empty()) {
            return;  // No positions, no margin risk
        }

        double used_margin = 0.0;
        const size_t SIMD_THRESHOLD = 8;

        if (open_positions_.size() >= SIMD_THRESHOLD && simd::has_avx2()) {
            // SIMD-optimized margin calculation
            RefreshSimdCache();

            // For margin, we use ask for BUY and bid for SELL
            // Margin = lots * contract_size * price / leverage * margin_rate
            double margin_factor = config_.contract_size / config_.leverage * config_.margin_rate;

            // BUY positions use ask price for margin
            if (!buy_lot_sizes_.empty()) {
                std::vector<double> buy_prices(buy_lot_sizes_.size(), tick.ask);
                used_margin += simd::total_margin_batch_avx2_optimized(
                    buy_lot_sizes_.data(),
                    buy_prices.data(),
                    buy_lot_sizes_.size(),
                    config_.contract_size,
                    config_.leverage
                ) * config_.margin_rate;
            }

            // SELL positions use bid price for margin
            if (!sell_lot_sizes_.empty()) {
                std::vector<double> sell_prices(sell_lot_sizes_.size(), tick.bid);
                used_margin += simd::total_margin_batch_avx2_optimized(
                    sell_lot_sizes_.data(),
                    sell_prices.data(),
                    sell_lot_sizes_.size(),
                    config_.contract_size,
                    config_.leverage
                ) * config_.margin_rate;
            }
        } else {
            // Scalar fallback
            for (Trade* trade : open_positions_) {
                double current_price = trade->IsBuy() ? tick.ask : tick.bid;
                double position_margin = trade->lot_size * config_.contract_size * current_price / config_.leverage * config_.margin_rate;
                used_margin += position_margin;
            }
        }

        if (used_margin <= 0) {
            return;  // No margin used
        }

        // Calculate margin level
        double margin_level = (equity_ / used_margin) * 100.0;

        // Use configurable stop-out level (MT5 default: 20%)
        if (margin_level < config_.stop_out_level) {
            // STOP OUT! Close all positions (MT5 behavior)
            if (config_.verbose) {
                std::cout << "\n!!! MARGIN STOP OUT !!!" << std::endl;
                std::cout << "Margin Level: " << margin_level << "%" << std::endl;
                std::cout << "Equity: $" << equity_ << std::endl;
                std::cout << "Used Margin: $" << used_margin << std::endl;
                std::cout << "Closing all " << open_positions_.size() << " positions..." << std::endl;
            }

            // Close all positions
            while (!open_positions_.empty()) {
                ClosePosition(open_positions_[0], "STOP OUT");
            }

            if (config_.verbose) {
                std::cout << "STOP OUT complete. Final Balance: $" << balance_ << std::endl;
                std::cout << "\n=== Test FAILED due to margin stop-out ===" << std::endl;
            }
            stop_out_occurred_ = true;  // Signal stop-out to caller instead of exit()
        }
    }

    void ProcessPendingOrders(const Tick& tick) {
        if (pending_orders_.empty()) return;

        std::vector<size_t> orders_to_execute;

        for (size_t i = 0; i < pending_orders_.size(); ++i) {
            const PendingOrder& order = pending_orders_[i];

            // Check expiry
            if (!order.expiry_time.empty() && tick.timestamp >= order.expiry_time) {
                orders_to_execute.push_back(i);
                continue;
            }

            bool triggered = false;

            switch (order.type) {
                case PendingOrderType::BUY_LIMIT:
                    // BUY_LIMIT triggers when ask drops to or below trigger price
                    triggered = (tick.ask <= order.trigger_price);
                    break;
                case PendingOrderType::BUY_STOP:
                    // BUY_STOP triggers when ask rises to or above trigger price
                    triggered = (tick.ask >= order.trigger_price);
                    break;
                case PendingOrderType::SELL_LIMIT:
                    // SELL_LIMIT triggers when bid rises to or above trigger price
                    triggered = (tick.bid >= order.trigger_price);
                    break;
                case PendingOrderType::SELL_STOP:
                    // SELL_STOP triggers when bid drops to or below trigger price
                    triggered = (tick.bid <= order.trigger_price);
                    break;
            }

            if (triggered) {
                orders_to_execute.push_back(i);
            }
        }

        // Execute triggered orders (in reverse to maintain indices during removal)
        for (auto it = orders_to_execute.rbegin(); it != orders_to_execute.rend(); ++it) {
            size_t idx = *it;
            const PendingOrder& order = pending_orders_[idx];

            // Check if order expired vs triggered
            bool expired = !order.expiry_time.empty() && tick.timestamp >= order.expiry_time;

            if (!expired) {
                // Convert pending order to market order
                TradeDirection dir = order.IsBuyOrder() ? TradeDirection::BUY : TradeDirection::SELL;
                Trade* trade = OpenMarketOrder(dir, order.lot_size, order.stop_loss, order.take_profit);

                if (trade && config_.verbose) {
                    std::cout << tick.timestamp << " - PENDING TRIGGERED " << PendingOrderTypeStr(order.type)
                              << " -> Trade #" << trade->id << std::endl;
                }
            } else if (config_.verbose) {
                std::cout << tick.timestamp << " - PENDING EXPIRED " << PendingOrderTypeStr(order.type)
                          << " order #" << order.id << std::endl;
            }

            // Remove from pending orders (swap-and-pop)
            std::swap(pending_orders_[idx], pending_orders_.back());
            pending_orders_.pop_back();
        }
    }

    void ProcessOpenPositions(const Tick& tick) {
        // Check stop loss, take profit, and trailing stops for all open positions
        std::vector<Trade*> positions_to_close;
        std::vector<std::string> close_reasons;

        for (Trade* trade : open_positions_) {
            bool should_close = false;
            std::string reason;

            if (trade->IsBuy()) {
                double current_price = tick.bid;

                // Process trailing stop for BUY positions
                if (trade->HasTrailingStop()) {
                    // Check if trailing should activate
                    double profit_pips = current_price - trade->entry_price;
                    if (!trade->trailing_activated && profit_pips >= trade->trailing_stop_activation) {
                        trade->trailing_activated = true;
                        trade->highest_profit_price = current_price;
                    }

                    // Update trailing stop if activated
                    if (trade->trailing_activated) {
                        if (current_price > trade->highest_profit_price) {
                            trade->highest_profit_price = current_price;
                            // Move stop loss up
                            trade->stop_loss = current_price - trade->trailing_stop_distance;
                        }
                    }
                }

                // Check SL/TP
                if (trade->stop_loss > 0 && current_price <= trade->stop_loss) {
                    should_close = true;
                    reason = trade->trailing_activated ? "TRAILING_SL" : "SL";
                } else if (trade->take_profit > 0 && current_price >= trade->take_profit) {
                    should_close = true;
                    reason = "TP";
                }
            } else { // SELL
                double current_price = tick.ask;

                // Process trailing stop for SELL positions
                if (trade->HasTrailingStop()) {
                    // Check if trailing should activate (profit = entry - current for SELL)
                    double profit_pips = trade->entry_price - current_price;
                    if (!trade->trailing_activated && profit_pips >= trade->trailing_stop_activation) {
                        trade->trailing_activated = true;
                        trade->highest_profit_price = current_price;  // Lowest for SELL
                    }

                    // Update trailing stop if activated
                    if (trade->trailing_activated) {
                        if (current_price < trade->highest_profit_price) {
                            trade->highest_profit_price = current_price;
                            // Move stop loss down
                            trade->stop_loss = current_price + trade->trailing_stop_distance;
                        }
                    }
                }

                // Check SL/TP
                if (trade->stop_loss > 0 && current_price >= trade->stop_loss) {
                    should_close = true;
                    reason = trade->trailing_activated ? "TRAILING_SL" : "SL";
                } else if (trade->take_profit > 0 && current_price <= trade->take_profit) {
                    should_close = true;
                    reason = "TP";
                }
            }

            if (should_close) {
                positions_to_close.push_back(trade);
                close_reasons.push_back(reason);
            }
        }

        // Close positions that hit SL/TP/Trailing
        for (size_t i = 0; i < positions_to_close.size(); ++i) {
            ClosePosition(positions_to_close[i], close_reasons[i]);
        }
    }

    // Helper: Get day of week from date string (0=Sunday, 1=Monday, ..., 6=Saturday)
    // Optimized: zero-allocation parsing
    int GetDayOfWeek(const std::string& date_str) {
        if (date_str.size() < 10) return 0;

        // Fast manual parsing - no substr() or stoi() allocations
        int year = (date_str[0] - '0') * 1000 + (date_str[1] - '0') * 100 +
                   (date_str[2] - '0') * 10 + (date_str[3] - '0');
        int month = (date_str[5] - '0') * 10 + (date_str[6] - '0');
        int day = (date_str[8] - '0') * 10 + (date_str[9] - '0');

        // Zeller's congruence algorithm
        if (month < 3) {
            month += 12;
            year--;
        }
        int century = year / 100;
        year = year % 100;
        int day_of_week = (day + (13 * (month + 1)) / 5 + year + year / 4 + century / 4 - 2 * century) % 7;

        // Convert: Zeller's returns (0=Sat, 1=Sun, 2=Mon, ...) -> (0=Sun, 1=Mon, ...)
        day_of_week = (day_of_week + 6) % 7;
        return day_of_week;
    }

    // Helper: Check if market is open for given day of week
    bool IsMarketOpen(int day_of_week) const {
        return (config_.trading_days & (1 << day_of_week)) != 0;
    }

    // Helper: Extract hour from timestamp (format: YYYY.MM.DD HH:MM:SS)
    int GetHourFromTimestamp(const std::string& timestamp) const {
        if (timestamp.length() >= 13) {
            return std::stoi(timestamp.substr(11, 2));
        }
        return 0;
    }

    void ProcessSwap(const Tick& tick) {
        // MT5 swap model: swap is applied AFTER the last tick of each trading day,
        // like a "ghost tick" where price doesn't move but swap is charged.
        // No TP/SL/trading occurs during swap application.
        //
        // Implementation: When we detect a new day (date change), we apply swap
        // for the PREVIOUS day before processing anything on the new day.
        //
        // Triple swap: charged at end of swap_3days (e.g., Wednesday) to cover weekend.
        // swap_3days=3 (Wednesday) means 3x swap is applied after Wednesday's last tick.

        std::string current_date = tick.timestamp.substr(0, 10);  // Get "YYYY.MM.DD"

        // Detect day change: new date means previous day ended, apply its swap
        bool is_new_day = false;

        if (current_date != last_swap_date_ && !last_swap_date_.empty()) {
            int prev_day_of_week = GetDayOfWeek(last_swap_date_);

            // Check if previous day was a trading day (swap is for overnight hold)
            if (IsMarketOpen(prev_day_of_week)) {
                is_new_day = true;
            }
        }

        // Apply end-of-previous-day swap
        if (is_new_day && !open_positions_.empty()) {
            int prev_day_of_week = GetDayOfWeek(last_swap_date_);

            // Calculate swap days to charge
            // Normal: 1 day
            // Triple swap day: 3 days (covers weekend for Mon-Fri instruments)
            // Triple swap is charged at end of swap_3days (e.g., end of Wednesday)
            int swap_multiplier = (prev_day_of_week == config_.swap_3days) ? 3 : 1;

            double daily_swap = 0.0;

            for (Trade* trade : open_positions_) {
                double swap_per_lot = trade->IsBuy() ? config_.swap_long : config_.swap_short;

                // Calculate swap based on swap mode
                double position_swap = 0.0;

                if (config_.swap_mode == 1) {
                    // SYMBOL_SWAP_MODE_POINTS: swap in points, need to convert to currency
                    // Swap in USD = swap_points × SYMBOL_POINT × contract_size × lot_size
                    // SYMBOL_POINT = pip_size from config (0.01 for XAUUSD, 0.001 for XAGUSD, etc.)
                    position_swap = swap_per_lot * config_.pip_size * config_.contract_size * trade->lot_size;
                    // For pairs where quote currency != account currency (e.g., USDJPY with USD account),
                    // the point value is in quote currency and needs to be divided by current price
                    if (config_.swap_divide_by_price && tick.bid > 0) {
                        position_swap /= tick.bid;
                    }
                } else if (config_.swap_mode == 2) {
                    // SYMBOL_SWAP_MODE_CURRENCY_SYMBOL: in account currency per lot
                    position_swap = swap_per_lot * trade->lot_size;
                } else {
                    // Other modes not implemented yet - fallback to mode 2 behavior
                    position_swap = swap_per_lot * trade->lot_size;
                }

                // Apply swap multiplier for triple swap day
                position_swap *= swap_multiplier;

                daily_swap += position_swap;

                // NOTE: Swap is deducted from balance immediately below
                // DO NOT also store in trade->commission or it will be double-counted!
                // trade->commission += position_swap;
            }

            // Deduct swap from balance
            balance_ += daily_swap;  // swap_long is typically negative for XAUUSD long positions
            total_swap_charged_ += daily_swap;

            // Log swap charges for debugging
            if (config_.verbose && daily_swap != 0.0) {
                std::cout << last_swap_date_ << " EOD"
                          << " - SWAP (end of day): $" << std::fixed << std::setprecision(2) << daily_swap
                          << " (" << (swap_multiplier == 3 ? "TRIPLE SWAP" : "normal")
                          << ", Open lots: " << std::setprecision(4);
                double total_lots = 0.0;
                for (Trade* t : open_positions_) total_lots += t->lot_size;
                std::cout << total_lots << ", Total swap: $" << std::setprecision(2) << total_swap_charged_ << ")" << std::endl;
            }
        }

        last_swap_date_ = current_date;
    }
};

} // namespace backtest

#endif // TICK_BASED_ENGINE_H
