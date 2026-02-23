/**
 * @file tick_based_engine.h
 * @brief High-performance tick-based backtesting engine for trading strategies.
 *
 * This file provides the core backtesting engine that simulates trade execution
 * at the tick level, matching MetaTrader 5's "Every tick based on real ticks" mode.
 *
 * @section features Key Features
 * - Tick-by-tick execution with realistic bid/ask spread handling
 * - SIMD-optimized P/L calculations (AVX2)
 * - Multiple order types: Market, Limit, Stop, Stop-Limit
 * - Trailing stops with configurable activation thresholds
 * - Commission and swap (rollover) simulation
 * - Slippage models: Fixed, Volume-based, Volatility-based
 * - Full MT5 compatibility for strategy validation
 *
 * @section usage Basic Usage
 * @code
 * TickBacktestConfig config = TickBacktestConfig::XAUUSD_Grid_Preset();
 * config.start_date = "2025.01.01";
 * config.end_date = "2025.03.31";
 *
 * TickBasedEngine engine(config);
 * MyStrategy strategy;
 *
 * engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
 *     strategy.OnTick(tick, eng);
 * });
 *
 * auto results = engine.GetResults();
 * @endcode
 *
 * @author ctrader-backtest project
 * @version 2.0
 * @date 2025
 */

#ifndef TICK_BASED_ENGINE_H
#define TICK_BASED_ENGINE_H

#include "tick_data.h"
#include "tick_data_manager.h"
#include "trade_types.h"
#include "position_validator.h"
#include "currency_converter.h"
#include "currency_rate_manager.h"
#include "simd_intrinsics.h"
#include "backtest_log.h"
#include "risk_manager.h"
#include <vector>
#include <memory>
#include <functional>
#include <iostream>
#include <cstring>
#include <iomanip>
#include <cstdlib>
#include <stdexcept>

namespace backtest {

/**
 * Symbol specification structure (matches MT5 symbol properties)
 */
struct SymbolSpec {
    std::string name = "";
    std::string description = "";
    double contract_size = 100000.0;
    int digits = 5;                   // Price decimal places
    double volume_min = 0.01;         // Minimum lot size
    double volume_max = 100.0;        // Maximum lot size
    double volume_step = 0.01;        // Lot size increment
    double pip_size = 0.00001;        // Pip value
    int stops_level = 0;              // Minimum SL/TP distance in points
    FillingType filling_type = FillingType::FOK;
};

/**
 * MT5 margin calculation mode — determines how margin is computed per position.
 * Must match MT5's ENUM_SYMBOL_CALC_MODE values.
 */
enum class TradeCalcMode : int {
    FOREX = 0,             // lots * cs / leverage * margin_rate
    FUTURES = 1,           // lots * margin_initial_fixed
    CFD = 2,               // lots * cs * price * margin_rate  (NO leverage)
    CFD_INDEX = 3,         // lots * cs * price * margin_rate  (same as CFD)
    CFD_LEVERAGE = 4,      // lots * cs * price / leverage * margin_rate
    FOREX_NO_LEVERAGE = 5  // lots * cs * margin_rate  (NO leverage, NO price)
};

/**
 * Backtest configuration with comprehensive settings
 */
struct TickBacktestConfig {
    // === Basic Settings ===
    std::string symbol = "EURUSD";
    double initial_balance = 10000.0;
    std::string account_currency = "USD";

    // === Account Mode ===
    AccountMode account_mode = AccountMode::HEDGING;  // Hedging or netting

    // === Commission Settings ===
    // NOTE: These are placeholder values. Query actual values from MT5 using
    // SymbolInfoDouble(symbol, SYMBOL_TRADE_TICK_VALUE) etc. before backtesting.
    CommissionMode commission_mode = CommissionMode::PER_LOT;
    double commission_per_lot = 0.0;        // Query from broker - varies by symbol
    double commission_percent = 0.0;        // For PERCENT_OF_VOLUME mode
    double commission_per_deal = 0.0;       // For PER_DEAL mode

    // === Slippage Settings ===
    // NOTE: Slippage varies by broker, time of day, and liquidity.
    // Use 0 for backtesting, or measure from real trading data.
    double slippage_pips = 0.0;             // Query from broker or measure
    bool use_bid_ask_spread = true;         // Use real bid/ask from ticks
    SlippageModel slippage_model = SlippageModel::FIXED;
    double slippage_volume_factor = 0.1;    // For VOLUME_BASED: extra pips per lot
    double slippage_volatility_factor = 1.0; // For VOLATILITY_BASED: multiplier

    // === Symbol Specifications ===
    double contract_size = 100000.0;        // Contract size (100 for XAUUSD, 100000 for Forex)
    double leverage = 500.0;                // Leverage (1:500 for XAUUSD)
    double margin_rate = 1.0;               // Initial margin rate
    TradeCalcMode trade_calc_mode = TradeCalcMode::CFD_LEVERAGE;  // MT5 margin calc mode
    double margin_initial_fixed = 0.0;      // For FUTURES mode: fixed margin per lot
    double pip_size = 0.00001;              // Pip size (0.00001 for 5-digit, 0.01 for XAUUSD)
    int digits = 5;                         // Price decimal places
    double volume_min = 0.01;               // Minimum lot size
    double volume_max = 100.0;              // Maximum lot size
    double volume_step = 0.01;              // Lot size increment
    int stops_level = 0;                    // Minimum SL/TP distance in points

    // === Risk Settings ===
    double stop_out_level = 20.0;           // Margin level % for stop-out (MT5 default: 20%)
    double margin_call_level = 100.0;       // Margin call level %

    // === Equity Tracking ===
    bool track_equity_curve = true;
    int equity_sample_interval = 1000;      ///< Sample equity every N ticks
    size_t max_equity_samples = 100000;     ///< Max equity samples (0 = unlimited, ~800KB at 8 bytes each)

    // === Date Filtering ===
    std::string start_date = "";            // Format: YYYY.MM.DD (empty = no filter)
    std::string end_date = "";              // Format: YYYY.MM.DD (empty = no filter)

    // === Swap/Rollover Settings ===
    double swap_long = 0.0;                 // Swap for long positions
    double swap_short = 0.0;                // Swap for short positions
    SwapMode swap_mode_enum = SwapMode::CURRENCY_SYMBOL;
    int swap_mode = 2;                      // Legacy: 2 = CURRENCY_SYMBOL
    int swap_3days = 3;                     // Triple swap day (3=Wednesday)
    bool swap_divide_by_price = false;

    // === Market Session ===
    int trading_days = 62;                  // Bitmask: Mon-Fri (0b0111110)
    int market_close_hour = 23;
    int market_open_hour = 0;

    // === Tick Data Source ===
    TickDataConfig tick_data_config;

    // === Output Control ===
    bool verbose = true;

    // === Validation ===
    /**
     * Validate configuration and throw if invalid
     * @throws std::invalid_argument if configuration is invalid
     */
    void Validate() const {
        if (initial_balance <= 0) {
            throw std::invalid_argument("initial_balance must be > 0");
        }
        if (leverage <= 0) {
            throw std::invalid_argument("leverage must be > 0");
        }
        if (contract_size <= 0) {
            throw std::invalid_argument("contract_size must be > 0");
        }
        if (pip_size <= 0) {
            throw std::invalid_argument("pip_size must be > 0");
        }
        if (stop_out_level < 0 || stop_out_level > 100) {
            throw std::invalid_argument("stop_out_level must be 0-100");
        }
        if (volume_min <= 0) {
            throw std::invalid_argument("volume_min must be > 0");
        }
        if (volume_max < volume_min) {
            throw std::invalid_argument("volume_max cannot be less than volume_min");
        }
        if (volume_step <= 0) {
            throw std::invalid_argument("volume_step must be > 0");
        }
        if (commission_per_lot < 0) {
            throw std::invalid_argument("commission_per_lot cannot be negative");
        }
        if (slippage_pips < 0) {
            throw std::invalid_argument("slippage_pips cannot be negative");
        }
    }

    TickBacktestConfig() = default;
};

// ============ Broker Preset Configurations ============

/**
 * Get preset configuration for XAUUSD on 
 */
inline TickBacktestConfig XAUUSD_Grid_Preset() {
    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.contract_size = 100.0;
    config.leverage = 500.0;
    config.pip_size = 0.01;
    config.digits = 2;
    config.swap_long = -66.99;
    config.swap_short = 41.2;
    config.swap_mode = 2;
    config.swap_3days = 3;
    config.commission_per_lot = 0.0;  // Spread only
    config.slippage_pips = 0.5;
    config.volume_min = 0.01;
    config.volume_max = 100.0;
    config.volume_step = 0.01;
    return config;
}

/**
 * Get preset configuration for XAGUSD on 
 */
inline TickBacktestConfig XAGUSD_Grid_Preset() {
    TickBacktestConfig config;
    config.symbol = "XAGUSD";
    config.contract_size = 5000.0;
    config.leverage = 500.0;
    config.pip_size = 0.001;
    config.digits = 3;
    config.swap_long = -15.0;
    config.swap_short = 13.72;
    config.swap_mode = 2;
    config.swap_3days = 3;
    config.commission_per_lot = 0.0;
    config.slippage_pips = 0.5;
    config.volume_min = 0.01;
    config.volume_max = 100.0;
    config.volume_step = 0.01;
    return config;
}

/**
 * Get preset configuration for standard Forex pair
 */
inline TickBacktestConfig ForexPair_Preset(const std::string& symbol = "EURUSD") {
    TickBacktestConfig config;
    config.symbol = symbol;
    config.contract_size = 100000.0;
    config.leverage = 500.0;
    config.pip_size = symbol.find("JPY") != std::string::npos ? 0.001 : 0.00001;
    config.digits = symbol.find("JPY") != std::string::npos ? 3 : 5;
    config.swap_long = 0.0;  // Varies by broker
    config.swap_short = 0.0;
    config.swap_mode = 2;
    config.swap_3days = 3;
    config.commission_per_lot = 7.0;  // Typical ECN commission
    config.slippage_pips = 0.3;
    config.volume_min = 0.01;
    config.volume_max = 100.0;
    config.volume_step = 0.01;
    return config;
}

/**
 * @brief High-precision tick-based backtesting engine.
 *
 * Processes every tick for exact order execution, matching MetaTrader 5's
 * "Every tick based on real ticks" mode. Supports:
 * - Market orders with realistic slippage
 * - Pending orders (Limit, Stop, Stop-Limit)
 * - Trailing stops with activation thresholds
 * - Stop-loss and take-profit execution
 * - Swap/rollover fee calculation
 * - Margin stop-out simulation
 *
 * @section engine_usage Usage Example
 * @code
 * TickBacktestConfig config = XAUUSD_Grid_Preset();
 * config.initial_balance = 10000.0;
 * config.start_date = "2025.01.01";
 * config.end_date = "2025.12.31";
 *
 * TickBasedEngine engine(config);
 * FillUpOscillation strategy(FillUpOscillation::Config::XAUUSD_Default());
 *
 * engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
 *     strategy.OnTick(tick, eng);
 * });
 *
 * auto results = engine.GetResults();
 * std::cout << "Final balance: $" << results.final_balance << std::endl;
 * @endcode
 *
 * @see TickBacktestConfig, Trade, BacktestResult
 */
class TickBasedEngine {
public:
    /**
     * @brief Construct engine with configuration.
     * @param config Backtesting configuration (broker settings, dates, etc.)
     */
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

    /**
     * @brief Strategy callback signature.
     *
     * Called for each tick during backtest. Strategy should analyze the tick
     * and call engine trading methods (OpenMarketOrder, ClosePosition, etc.)
     *
     * @param tick Current tick data (bid, ask, timestamp)
     * @param engine Reference to engine for trading operations
     */
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
            UpdatePeakTrackers();
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
            UpdatePeakTrackers();
            if (stop_out_occurred_) {
                break;
            }

            ProcessPendingOrders(tick);
            ProcessOpenPositions(tick);
            strategy(tick, *this);
        }
    }

    /// @name Trading Operations
    /// @{

    /**
     * @brief Open a market order at current tick price.
     *
     * BUY orders execute at ask price, SELL orders at bid price.
     * Slippage is applied based on the configured slippage model.
     *
     * @param direction TradeDirection::BUY or TradeDirection::SELL
     * @param lot_size Position size in lots (e.g., 0.01 = micro lot)
     * @param stop_loss Stop-loss price (0 = no SL)
     * @param take_profit Take-profit price (0 = no TP)
     * @return Pointer to created Trade, or nullptr if order failed
     *
     * @code
     * // Open 0.01 lot BUY with $10 SL and $20 TP
     * Trade* trade = engine.OpenMarketOrder(
     *     TradeDirection::BUY, 0.01,
     *     engine.GetCurrentTick().bid - 10.0,  // SL
     *     engine.GetCurrentTick().ask + 20.0   // TP
     * );
     * @endcode
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

        // Return trade to pool for reuse instead of delete
        trade_pool_.Release(trade);

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

    // Margin & account state (use these instead of reimplementing margin math)
    double GetUsedMargin() const {
        if (open_positions_.empty()) return 0.0;

        const size_t SIMD_THRESHOLD = 4;
        if (open_positions_.size() >= SIMD_THRESHOLD && simd::has_avx2()) {
            RefreshSimdCache();

            double used_margin = 0.0;
            // BUY positions use ask price for margin
            if (!buy_lot_sizes_.empty()) {
                buy_prices_buffer_.resize(buy_lot_sizes_.size());
                std::fill(buy_prices_buffer_.begin(), buy_prices_buffer_.end(), current_tick_.ask);
                used_margin += simd::total_margin_batch(
                    buy_lot_sizes_.data(), buy_prices_buffer_.data(),
                    buy_lot_sizes_.size(), config_.contract_size, config_.leverage
                ) * config_.margin_rate;
            }
            // SELL positions use bid price for margin
            if (!sell_lot_sizes_.empty()) {
                sell_prices_buffer_.resize(sell_lot_sizes_.size());
                std::fill(sell_prices_buffer_.begin(), sell_prices_buffer_.end(), current_tick_.bid);
                used_margin += simd::total_margin_batch(
                    sell_lot_sizes_.data(), sell_prices_buffer_.data(),
                    sell_lot_sizes_.size(), config_.contract_size, config_.leverage
                ) * config_.margin_rate;
            }
            return used_margin;
        }

        // Scalar fallback
        double used_margin = 0.0;
        for (const Trade* trade : open_positions_) {
            double price = trade->IsBuy() ? current_tick_.ask : current_tick_.bid;
            used_margin += trade->lot_size * config_.contract_size * price
                         / config_.leverage * config_.margin_rate;
        }
        return used_margin;
    }

    double GetFreeMargin() const { return equity_ - GetUsedMargin(); }

    double GetMarginLevel() const {
        double used = GetUsedMargin();
        return (used > 0) ? (equity_ / used * 100.0) : 0.0;
    }

    /// Calculate margin required for a hypothetical trade (for lot sizing decisions)
    double CalculateMarginRequired(double lots, double price) const {
        return lots * config_.contract_size * price / config_.leverage * config_.margin_rate;
    }

    /// Normalize lot size to broker constraints (volume_min, volume_max, volume_step)
    double NormalizeLots(double lots) const {
        lots = std::max(lots, config_.volume_min);
        lots = std::min(lots, config_.volume_max);
        lots = std::floor(lots / config_.volume_step) * config_.volume_step;
        return lots;
    }

    const TickBacktestConfig& GetConfig() const { return config_; }

    /// @name Position Aggregates (maintained incrementally — O(1) per tick)
    /// @{
    double GetBuyVolume() const { return buy_volume_; }
    double GetHighestBuyEntry() const { return highest_buy_entry_; }
    double GetLowestBuyEntry() const { return lowest_buy_entry_; }
    size_t GetBuyPositionCount() const { return buy_position_count_; }
    double GetSellVolume() const { return sell_volume_; }
    double GetHighestSellEntry() const { return highest_sell_entry_; }
    double GetLowestSellEntry() const { return lowest_sell_entry_; }
    size_t GetSellPositionCount() const { return sell_position_count_; }
    /// @}

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

        // Peak tracking metrics (for grid efficiency comparison)
        double max_used_funds;          // Peak(balance - equity + used_margin)
        double max_used_margin;         // Peak margin alone
        double peak_equity;             // Highest equity reached
        double peak_balance;            // Highest realized balance
        int max_open_positions;         // Peak simultaneous open positions

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

        // Peak tracking metrics
        results.max_used_funds = max_used_funds_;
        results.max_used_margin = max_used_margin_;
        results.peak_equity = peak_equity_;
        results.peak_balance = peak_balance_;
        results.max_open_positions = max_open_positions_;

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

    // Trade memory pool for reduced allocation overhead
    TradePool trade_pool_;

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

    // Peak tracking for grid efficiency
    double max_used_funds_ = 0.0;
    double max_used_margin_ = 0.0;
    double peak_balance_ = 0.0;
    int max_open_positions_ = 0;
    double last_used_margin_ = 0.0;    // Stored from CheckMarginStopOut for UpdatePeakTrackers

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
    // Pre-allocated price buffers for margin calculation (avoid per-tick allocation)
    mutable std::vector<double> buy_prices_buffer_;
    mutable std::vector<double> sell_prices_buffer_;

    // SL/TP cache for SIMD batch checking (refreshed alongside SIMD cache)
    mutable std::vector<double> buy_stop_losses_;
    mutable std::vector<double> buy_take_profits_;
    mutable std::vector<double> sell_stop_losses_;
    mutable std::vector<double> sell_take_profits_;

    // Position aggregates — maintained incrementally via RecalcPositionAggregates()
    double buy_volume_ = 0.0;
    double highest_buy_entry_ = -1e308;
    double lowest_buy_entry_ = 1e308;
    size_t buy_position_count_ = 0;
    double sell_volume_ = 0.0;
    double highest_sell_entry_ = -1e308;
    double lowest_sell_entry_ = 1e308;
    size_t sell_position_count_ = 0;

    // Refresh SIMD cache when positions have changed
    void RefreshSimdCache() const {
        if (!simd_cache_dirty_) return;

        buy_entry_prices_.clear();
        buy_lot_sizes_.clear();
        buy_stop_losses_.clear();
        buy_take_profits_.clear();
        sell_entry_prices_.clear();
        sell_lot_sizes_.clear();
        sell_stop_losses_.clear();
        sell_take_profits_.clear();
        buy_positions_.clear();
        sell_positions_.clear();

        for (Trade* trade : open_positions_) {
            if (trade->IsBuy()) {
                buy_entry_prices_.push_back(trade->entry_price);
                buy_lot_sizes_.push_back(trade->lot_size);
                buy_stop_losses_.push_back(trade->stop_loss);
                buy_take_profits_.push_back(trade->take_profit);
                buy_positions_.push_back(trade);
            } else {
                sell_entry_prices_.push_back(trade->entry_price);
                sell_lot_sizes_.push_back(trade->lot_size);
                sell_stop_losses_.push_back(trade->stop_loss);
                sell_take_profits_.push_back(trade->take_profit);
                sell_positions_.push_back(trade);
            }
        }

        simd_cache_dirty_ = false;
    }

    // Mark cache dirty and recalculate position aggregates when positions change
    void InvalidateSimdCache() {
        simd_cache_dirty_ = true;
        RecalcPositionAggregates();
    }

    // Recalculate O(N) aggregates — called only when positions change (open/close)
    void RecalcPositionAggregates() {
        buy_volume_ = 0.0;
        highest_buy_entry_ = -1e308;
        lowest_buy_entry_ = 1e308;
        buy_position_count_ = 0;
        sell_volume_ = 0.0;
        highest_sell_entry_ = -1e308;
        lowest_sell_entry_ = 1e308;
        sell_position_count_ = 0;

        for (const Trade* trade : open_positions_) {
            if (trade->IsBuy()) {
                buy_volume_ += trade->lot_size;
                if (trade->entry_price > highest_buy_entry_) highest_buy_entry_ = trade->entry_price;
                if (trade->entry_price < lowest_buy_entry_) lowest_buy_entry_ = trade->entry_price;
                buy_position_count_++;
            } else {
                sell_volume_ += trade->lot_size;
                if (trade->entry_price > highest_sell_entry_) highest_sell_entry_ = trade->entry_price;
                if (trade->entry_price < lowest_sell_entry_) lowest_sell_entry_ = trade->entry_price;
                sell_position_count_++;
            }
        }
    }

    double GetPipValue() const {
        // Use configurable pip size (0.00001 for forex, 0.01 for gold, 0.001 for JPY)
        return config_.pip_size;
    }

    Trade* CreateTrade(TradeDirection direction, double entry_price,
                       double lot_size, double sl, double tp) {
        // Use trade pool instead of new/delete for better performance
        Trade* trade = trade_pool_.Allocate();
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
                // Respect max_equity_samples limit (0 = unlimited)
                if (config_.max_equity_samples == 0 || equity_curve_.size() < config_.max_equity_samples) {
                    equity_curve_.push_back(equity_);
                    equity_timestamps_.push_back(tick.timestamp);
                }
                equity_sample_counter_ = 0;
            }
        }

        // Track daily returns for Sharpe ratio calculation
        // Optimized: compare first 10 chars directly without substr() allocation
        if (tick.timestamp.size() >= 10) {
            // Compare date portion directly: YYYY.MM.DD (10 chars)
            bool date_changed = last_daily_date_.empty() ||
                (tick.timestamp.compare(0, 10, last_daily_date_, 0, 10) != 0);

            if (date_changed && !last_daily_date_.empty()) {
                // New day - calculate daily return
                if (last_daily_equity_ > 0) {
                    double daily_return = (equity_ - last_daily_equity_) / last_daily_equity_;
                    // Cap at ~10 years of daily data (2500 trading days)
                    if (daily_returns_.size() < 2500) {
                        daily_returns_.push_back(daily_return);
                    }
                }
                last_daily_equity_ = equity_;
            }
            if (last_daily_date_.empty()) {
                last_daily_equity_ = equity_;
            }
            // Only allocate when date actually changes (once per day, not per tick)
            if (date_changed) {
                last_daily_date_ = tick.timestamp.substr(0, 10);
            }
        }

        // Track recent prices for volatility-based slippage
        if (config_.slippage_model == SlippageModel::VOLATILITY_BASED) {
            recent_prices_.push_back(tick.bid);
            if (recent_prices_.size() > VOLATILITY_WINDOW) {
                recent_prices_.erase(recent_prices_.begin());
            }
        }
    }

    // Calculate margin for a single position based on trade_calc_mode
    double CalcPositionMargin(double lot_size, double price) const {
        switch (config_.trade_calc_mode) {
            case TradeCalcMode::FOREX:
                return lot_size * config_.contract_size / config_.leverage * config_.margin_rate;
            case TradeCalcMode::FUTURES:
                return lot_size * config_.margin_initial_fixed;
            case TradeCalcMode::CFD:
            case TradeCalcMode::CFD_INDEX:
                return lot_size * config_.contract_size * price * config_.margin_rate;
            case TradeCalcMode::CFD_LEVERAGE:
                return lot_size * config_.contract_size * price / config_.leverage * config_.margin_rate;
            case TradeCalcMode::FOREX_NO_LEVERAGE:
                return lot_size * config_.contract_size * config_.margin_rate;
            default:
                return lot_size * config_.contract_size * price / config_.leverage * config_.margin_rate;
        }
    }

    // Compute total margin for buy/sell sides using SIMD, dispatched by calc_mode
    double CalcTotalMarginSIMD(const Tick& tick) {
        RefreshSimdCache();
        double margin = 0.0;

        // Fill price buffers for modes that need them
        bool needs_price = (config_.trade_calc_mode == TradeCalcMode::CFD_LEVERAGE ||
                           config_.trade_calc_mode == TradeCalcMode::CFD ||
                           config_.trade_calc_mode == TradeCalcMode::CFD_INDEX);
        if (needs_price) {
            buy_prices_buffer_.resize(buy_lot_sizes_.size());
            std::fill(buy_prices_buffer_.begin(), buy_prices_buffer_.end(), tick.ask);
            sell_prices_buffer_.resize(sell_lot_sizes_.size());
            std::fill(sell_prices_buffer_.begin(), sell_prices_buffer_.end(), tick.bid);
        }

        switch (config_.trade_calc_mode) {
            case TradeCalcMode::CFD_LEVERAGE: {
                if (!buy_lot_sizes_.empty()) {
                    if (simd::has_avx512() && buy_lot_sizes_.size() >= 8)
                        margin += simd::total_margin_cfd_leverage_avx512(
                            buy_lot_sizes_.data(), buy_prices_buffer_.data(), buy_lot_sizes_.size(),
                            config_.contract_size, config_.leverage, config_.margin_rate);
                    else if (simd::has_avx2() && buy_lot_sizes_.size() >= 4)
                        margin += simd::total_margin_cfd_leverage_avx2(
                            buy_lot_sizes_.data(), buy_prices_buffer_.data(), buy_lot_sizes_.size(),
                            config_.contract_size, config_.leverage, config_.margin_rate);
                    else
                        for (size_t i = 0; i < buy_lot_sizes_.size(); ++i)
                            margin += buy_lot_sizes_[i] * buy_prices_buffer_[i] * config_.contract_size / config_.leverage * config_.margin_rate;
                }
                if (!sell_lot_sizes_.empty()) {
                    if (simd::has_avx512() && sell_lot_sizes_.size() >= 8)
                        margin += simd::total_margin_cfd_leverage_avx512(
                            sell_lot_sizes_.data(), sell_prices_buffer_.data(), sell_lot_sizes_.size(),
                            config_.contract_size, config_.leverage, config_.margin_rate);
                    else if (simd::has_avx2() && sell_lot_sizes_.size() >= 4)
                        margin += simd::total_margin_cfd_leverage_avx2(
                            sell_lot_sizes_.data(), sell_prices_buffer_.data(), sell_lot_sizes_.size(),
                            config_.contract_size, config_.leverage, config_.margin_rate);
                    else
                        for (size_t i = 0; i < sell_lot_sizes_.size(); ++i)
                            margin += sell_lot_sizes_[i] * sell_prices_buffer_[i] * config_.contract_size / config_.leverage * config_.margin_rate;
                }
                break;
            }
            case TradeCalcMode::CFD:
            case TradeCalcMode::CFD_INDEX: {
                if (!buy_lot_sizes_.empty()) {
                    if (simd::has_avx512() && buy_lot_sizes_.size() >= 8)
                        margin += simd::total_margin_cfd_avx512(
                            buy_lot_sizes_.data(), buy_prices_buffer_.data(), buy_lot_sizes_.size(),
                            config_.contract_size, config_.margin_rate);
                    else if (simd::has_avx2() && buy_lot_sizes_.size() >= 4)
                        margin += simd::total_margin_cfd_avx2(
                            buy_lot_sizes_.data(), buy_prices_buffer_.data(), buy_lot_sizes_.size(),
                            config_.contract_size, config_.margin_rate);
                    else
                        for (size_t i = 0; i < buy_lot_sizes_.size(); ++i)
                            margin += buy_lot_sizes_[i] * buy_prices_buffer_[i] * config_.contract_size * config_.margin_rate;
                }
                if (!sell_lot_sizes_.empty()) {
                    if (simd::has_avx512() && sell_lot_sizes_.size() >= 8)
                        margin += simd::total_margin_cfd_avx512(
                            sell_lot_sizes_.data(), sell_prices_buffer_.data(), sell_lot_sizes_.size(),
                            config_.contract_size, config_.margin_rate);
                    else if (simd::has_avx2() && sell_lot_sizes_.size() >= 4)
                        margin += simd::total_margin_cfd_avx2(
                            sell_lot_sizes_.data(), sell_prices_buffer_.data(), sell_lot_sizes_.size(),
                            config_.contract_size, config_.margin_rate);
                    else
                        for (size_t i = 0; i < sell_lot_sizes_.size(); ++i)
                            margin += sell_lot_sizes_[i] * sell_prices_buffer_[i] * config_.contract_size * config_.margin_rate;
                }
                break;
            }
            case TradeCalcMode::FOREX: {
                // FOREX: margin = lots * cs / leverage * margin_rate (NO price)
                size_t total = buy_lot_sizes_.size() + sell_lot_sizes_.size();
                // Combine buy + sell lot sizes for a single pass (price doesn't matter)
                if (!buy_lot_sizes_.empty()) {
                    if (simd::has_avx512() && buy_lot_sizes_.size() >= 8)
                        margin += simd::total_margin_forex_avx512(
                            buy_lot_sizes_.data(), buy_lot_sizes_.size(),
                            config_.contract_size, config_.leverage, config_.margin_rate);
                    else if (simd::has_avx2() && buy_lot_sizes_.size() >= 4)
                        margin += simd::total_margin_forex_avx2(
                            buy_lot_sizes_.data(), buy_lot_sizes_.size(),
                            config_.contract_size, config_.leverage, config_.margin_rate);
                    else
                        for (size_t i = 0; i < buy_lot_sizes_.size(); ++i)
                            margin += buy_lot_sizes_[i] * config_.contract_size / config_.leverage * config_.margin_rate;
                }
                if (!sell_lot_sizes_.empty()) {
                    if (simd::has_avx512() && sell_lot_sizes_.size() >= 8)
                        margin += simd::total_margin_forex_avx512(
                            sell_lot_sizes_.data(), sell_lot_sizes_.size(),
                            config_.contract_size, config_.leverage, config_.margin_rate);
                    else if (simd::has_avx2() && sell_lot_sizes_.size() >= 4)
                        margin += simd::total_margin_forex_avx2(
                            sell_lot_sizes_.data(), sell_lot_sizes_.size(),
                            config_.contract_size, config_.leverage, config_.margin_rate);
                    else
                        for (size_t i = 0; i < sell_lot_sizes_.size(); ++i)
                            margin += sell_lot_sizes_[i] * config_.contract_size / config_.leverage * config_.margin_rate;
                }
                break;
            }
            case TradeCalcMode::FUTURES: {
                // FUTURES: margin = lots * margin_initial_fixed
                if (!buy_lot_sizes_.empty()) {
                    if (simd::has_avx512() && buy_lot_sizes_.size() >= 8)
                        margin += simd::total_margin_futures_avx512(
                            buy_lot_sizes_.data(), buy_lot_sizes_.size(), config_.margin_initial_fixed);
                    else if (simd::has_avx2() && buy_lot_sizes_.size() >= 4)
                        margin += simd::total_margin_futures_avx2(
                            buy_lot_sizes_.data(), buy_lot_sizes_.size(), config_.margin_initial_fixed);
                    else
                        for (size_t i = 0; i < buy_lot_sizes_.size(); ++i)
                            margin += buy_lot_sizes_[i] * config_.margin_initial_fixed;
                }
                if (!sell_lot_sizes_.empty()) {
                    if (simd::has_avx512() && sell_lot_sizes_.size() >= 8)
                        margin += simd::total_margin_futures_avx512(
                            sell_lot_sizes_.data(), sell_lot_sizes_.size(), config_.margin_initial_fixed);
                    else if (simd::has_avx2() && sell_lot_sizes_.size() >= 4)
                        margin += simd::total_margin_futures_avx2(
                            sell_lot_sizes_.data(), sell_lot_sizes_.size(), config_.margin_initial_fixed);
                    else
                        for (size_t i = 0; i < sell_lot_sizes_.size(); ++i)
                            margin += sell_lot_sizes_[i] * config_.margin_initial_fixed;
                }
                break;
            }
            case TradeCalcMode::FOREX_NO_LEVERAGE: {
                // FOREX_NO_LEVERAGE: margin = lots * cs * margin_rate
                if (!buy_lot_sizes_.empty()) {
                    if (simd::has_avx512() && buy_lot_sizes_.size() >= 8)
                        margin += simd::total_margin_forex_nolev_avx512(
                            buy_lot_sizes_.data(), buy_lot_sizes_.size(),
                            config_.contract_size, config_.margin_rate);
                    else if (simd::has_avx2() && buy_lot_sizes_.size() >= 4)
                        margin += simd::total_margin_forex_nolev_avx2(
                            buy_lot_sizes_.data(), buy_lot_sizes_.size(),
                            config_.contract_size, config_.margin_rate);
                    else
                        for (size_t i = 0; i < buy_lot_sizes_.size(); ++i)
                            margin += buy_lot_sizes_[i] * config_.contract_size * config_.margin_rate;
                }
                if (!sell_lot_sizes_.empty()) {
                    if (simd::has_avx512() && sell_lot_sizes_.size() >= 8)
                        margin += simd::total_margin_forex_nolev_avx512(
                            sell_lot_sizes_.data(), sell_lot_sizes_.size(),
                            config_.contract_size, config_.margin_rate);
                    else if (simd::has_avx2() && sell_lot_sizes_.size() >= 4)
                        margin += simd::total_margin_forex_nolev_avx2(
                            sell_lot_sizes_.data(), sell_lot_sizes_.size(),
                            config_.contract_size, config_.margin_rate);
                    else
                        for (size_t i = 0; i < sell_lot_sizes_.size(); ++i)
                            margin += sell_lot_sizes_[i] * config_.contract_size * config_.margin_rate;
                }
                break;
            }
        }
        return margin;
    }

    void CheckMarginStopOut(const Tick& tick) {
        // Calculate current margin level
        // Margin Level = (Equity / Used Margin) × 100%
        // Stop out occurs when margin level falls below stop_out_level

        if (open_positions_.empty()) {
            last_used_margin_ = 0.0;
            return;
        }

        // Dispatch margin calculation by trade_calc_mode with SIMD acceleration
        last_used_margin_ = CalcTotalMarginSIMD(tick);

        if (last_used_margin_ <= 0) {
            return;
        }

        // Calculate margin level
        double margin_level = (equity_ / last_used_margin_) * 100.0;

        // Use configurable stop-out level (MT5 default: 20%)
        if (margin_level < config_.stop_out_level) {
            // STOP OUT! Close all positions (MT5 behavior)
            if (config_.verbose) {
                std::cout << "\n!!! MARGIN STOP OUT !!!" << std::endl;
                std::cout << "Margin Level: " << margin_level << "%" << std::endl;
                std::cout << "Equity: $" << equity_ << std::endl;
                std::cout << "Used Margin: $" << last_used_margin_ << std::endl;
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
            stop_out_occurred_ = true;
        }
    }

    // Update peak tracking metrics (called after CheckMarginStopOut each tick)
    void UpdatePeakTrackers() {
        if (balance_ > peak_balance_) peak_balance_ = balance_;
        if (last_used_margin_ > max_used_margin_) max_used_margin_ = last_used_margin_;
        double used_funds = balance_ - equity_ + last_used_margin_;
        if (used_funds > max_used_funds_) max_used_funds_ = used_funds;
        int npos = static_cast<int>(open_positions_.size());
        if (npos > max_open_positions_) max_open_positions_ = npos;
    }

    void ProcessPendingOrders(const Tick& tick) {
        if (pending_orders_.empty()) return;

        std::vector<size_t> orders_to_execute;
        std::vector<size_t> stop_limits_to_activate;

        for (size_t i = 0; i < pending_orders_.size(); ++i) {
            PendingOrder& order = pending_orders_[i];

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
                case PendingOrderType::BUY_STOP_LIMIT:
                    // Two-phase: First check stop trigger, then limit execution
                    if (!order.stop_limit_activated) {
                        // Phase 1: Stop trigger - activates when ask >= trigger_price
                        if (tick.ask >= order.trigger_price) {
                            order.stop_limit_activated = true;
                            if (config_.verbose) {
                                std::cout << tick.timestamp << " - STOP_LIMIT ACTIVATED #" << order.id
                                          << " waiting for limit @ " << order.limit_price << std::endl;
                            }
                        }
                    } else {
                        // Phase 2: Limit execution - triggers when ask <= limit_price
                        double limit = order.limit_price > 0 ? order.limit_price : order.trigger_price;
                        triggered = (tick.ask <= limit);
                    }
                    break;
                case PendingOrderType::SELL_STOP_LIMIT:
                    // Two-phase: First check stop trigger, then limit execution
                    if (!order.stop_limit_activated) {
                        // Phase 1: Stop trigger - activates when bid <= trigger_price
                        if (tick.bid <= order.trigger_price) {
                            order.stop_limit_activated = true;
                            if (config_.verbose) {
                                std::cout << tick.timestamp << " - STOP_LIMIT ACTIVATED #" << order.id
                                          << " waiting for limit @ " << order.limit_price << std::endl;
                            }
                        }
                    } else {
                        // Phase 2: Limit execution - triggers when bid >= limit_price
                        double limit = order.limit_price > 0 ? order.limit_price : order.trigger_price;
                        triggered = (tick.bid >= limit);
                    }
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
        if (open_positions_.empty()) return;

        // First pass: update trailing stops (must be scalar — modifies trade state)
        bool has_trailing = false;
        for (Trade* trade : open_positions_) {
            if (!trade->HasTrailingStop()) continue;
            has_trailing = true;

            if (trade->IsBuy()) {
                double current_price = tick.bid;
                double profit_pips = current_price - trade->entry_price;
                if (!trade->trailing_activated && profit_pips >= trade->trailing_stop_activation) {
                    trade->trailing_activated = true;
                    trade->highest_profit_price = current_price;
                }
                if (trade->trailing_activated && current_price > trade->highest_profit_price) {
                    trade->highest_profit_price = current_price;
                    trade->stop_loss = current_price - trade->trailing_stop_distance;
                }
            } else {
                double current_price = tick.ask;
                double profit_pips = trade->entry_price - current_price;
                if (!trade->trailing_activated && profit_pips >= trade->trailing_stop_activation) {
                    trade->trailing_activated = true;
                    trade->highest_profit_price = current_price;
                }
                if (trade->trailing_activated && current_price < trade->highest_profit_price) {
                    trade->highest_profit_price = current_price;
                    trade->stop_loss = current_price + trade->trailing_stop_distance;
                }
            }
        }

        // If trailing stops changed SL values, invalidate cache so SL arrays are fresh
        if (has_trailing) {
            simd_cache_dirty_ = true;
        }

        // Second pass: batch SL/TP checking with SIMD
        const size_t SIMD_THRESHOLD = 4;
        std::vector<Trade*> positions_to_close;
        std::vector<std::string> close_reasons;

        if (open_positions_.size() >= SIMD_THRESHOLD && simd::has_avx2()) {
            RefreshSimdCache();

            // Pre-allocate hit buffers (max possible = all positions)
            int max_hits = static_cast<int>(std::max(buy_positions_.size(), sell_positions_.size()));
            std::vector<int> hit_indices(max_hits);
            std::vector<int> hit_types(max_hits);

            // Batch check BUY positions (exit at bid)
            if (!buy_positions_.empty()) {
                int hits = simd::check_sl_tp_buy(
                    buy_stop_losses_.data(), buy_take_profits_.data(),
                    tick.bid, buy_positions_.size(),
                    hit_indices.data(), hit_types.data());

                for (int h = 0; h < hits; ++h) {
                    Trade* trade = buy_positions_[hit_indices[h]];
                    positions_to_close.push_back(trade);
                    if (hit_types[h] == 1)
                        close_reasons.push_back(trade->trailing_activated ? "TRAILING_SL" : "SL");
                    else
                        close_reasons.push_back("TP");
                }
            }

            // Batch check SELL positions (exit at ask)
            if (!sell_positions_.empty()) {
                int hits = simd::check_sl_tp_sell(
                    sell_stop_losses_.data(), sell_take_profits_.data(),
                    tick.ask, sell_positions_.size(),
                    hit_indices.data(), hit_types.data());

                for (int h = 0; h < hits; ++h) {
                    Trade* trade = sell_positions_[hit_indices[h]];
                    positions_to_close.push_back(trade);
                    if (hit_types[h] == 1)
                        close_reasons.push_back(trade->trailing_activated ? "TRAILING_SL" : "SL");
                    else
                        close_reasons.push_back("TP");
                }
            }
        } else {
            // Scalar fallback
            for (Trade* trade : open_positions_) {
                bool should_close = false;
                std::string reason;

                if (trade->IsBuy()) {
                    if (trade->stop_loss > 0 && tick.bid <= trade->stop_loss) {
                        should_close = true;
                        reason = trade->trailing_activated ? "TRAILING_SL" : "SL";
                    } else if (trade->take_profit > 0 && tick.bid >= trade->take_profit) {
                        should_close = true;
                        reason = "TP";
                    }
                } else {
                    if (trade->stop_loss > 0 && tick.ask >= trade->stop_loss) {
                        should_close = true;
                        reason = trade->trailing_activated ? "TRAILING_SL" : "SL";
                    } else if (trade->take_profit > 0 && tick.ask <= trade->take_profit) {
                        should_close = true;
                        reason = "TP";
                    }
                }

                if (should_close) {
                    positions_to_close.push_back(trade);
                    close_reasons.push_back(reason);
                }
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

        // Optimized: compare first 10 chars without allocation (hot path!)
        // Detect day change: new date means previous day ended, apply its swap
        bool is_new_day = false;

        if (tick.timestamp.size() >= 10 && !last_swap_date_.empty() &&
            tick.timestamp.compare(0, 10, last_swap_date_, 0, 10) != 0) {
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

                // Calculate swap based on swap mode (matches MT5 SYMBOL_SWAP_MODE_*)
                double position_swap = 0.0;

                switch (config_.swap_mode) {
                    case 0:  // SYMBOL_SWAP_MODE_DISABLED
                        // No swap charged
                        position_swap = 0.0;
                        break;

                    case 1:  // SYMBOL_SWAP_MODE_POINTS
                        // Swap in points, converted to currency
                        // Swap in USD = swap_points × SYMBOL_POINT × contract_size × lot_size
                        position_swap = swap_per_lot * config_.pip_size * config_.contract_size * trade->lot_size;
                        // For pairs where quote currency != account currency
                        if (config_.swap_divide_by_price && tick.bid > 0) {
                            position_swap /= tick.bid;
                        }
                        break;

                    case 2:  // SYMBOL_SWAP_MODE_CURRENCY_SYMBOL (default)
                        // In account currency per lot
                        position_swap = swap_per_lot * trade->lot_size;
                        break;

                    case 3: {  // SYMBOL_SWAP_MODE_INTEREST
                        // Annual interest rate as percentage
                        // Daily swap = (lot_size × contract_size × price × annual_rate%) / 365 / 100
                        double price = trade->IsBuy() ? tick.bid : tick.ask;
                        double position_value = trade->lot_size * config_.contract_size * price;
                        position_swap = (position_value * swap_per_lot) / 365.0 / 100.0;
                        break;
                    }

                    case 4:  // SYMBOL_SWAP_MODE_MARGIN_CURRENCY
                        // In margin currency per lot (similar to mode 2 but may need conversion)
                        // For simplicity, treating same as mode 2 since we assume account=margin currency
                        position_swap = swap_per_lot * trade->lot_size;
                        break;

                    default:
                        // Unknown mode - fallback to mode 2
                        position_swap = swap_per_lot * trade->lot_size;
                        break;
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

        // Only allocate when date changes (once per day, not per tick)
        if (tick.timestamp.size() >= 10 &&
            (last_swap_date_.empty() || tick.timestamp.compare(0, 10, last_swap_date_, 0, 10) != 0)) {
            last_swap_date_ = tick.timestamp.substr(0, 10);
        }
    }
};

} // namespace backtest

#endif // TICK_BASED_ENGINE_H
