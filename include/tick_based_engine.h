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

    Trade() : id(0), direction(TradeDirection::BUY), entry_price(0), exit_price(0), lot_size(0),
              stop_loss(0), take_profit(0), profit_loss(0), commission(0) {}

    // Helper for backward compatibility - returns direction as string
    const char* GetDirectionStr() const { return TradeDirectionStr(direction); }

    // Check direction helpers for cleaner code
    bool IsBuy() const { return direction == TradeDirection::BUY; }
    bool IsSell() const { return direction == TradeDirection::SELL; }
};

/**
 * Configuration for tick-based backtesting
 */
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

            // Apply slippage
            if (config_.slippage_pips > 0) {
                double slippage = config_.slippage_pips * GetPipValue();
                entry_price += is_buy ? slippage : -slippage;
            }

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

        return results;
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
    size_t next_trade_id_ = 1;

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

    void UpdateEquity(const Tick& tick) {
        equity_ = balance_;

        // SIMD-optimized P/L calculation for large position counts
        const size_t SIMD_THRESHOLD = 8;  // Use SIMD for 8+ positions

        if (open_positions_.size() >= SIMD_THRESHOLD && simd::has_avx2()) {
            RefreshSimdCache();

            // Process BUY positions with SIMD
            if (!buy_entry_prices_.empty()) {
                std::vector<double> buy_pnl(buy_entry_prices_.size());
                simd::calculate_pnl_batch(
                    buy_entry_prices_.data(),
                    buy_lot_sizes_.data(),
                    tick.bid,  // BUY positions close at bid
                    config_.contract_size,
                    buy_pnl.data(),
                    buy_entry_prices_.size(),
                    true  // is_buy = true
                );
                equity_ += simd::sum(buy_pnl.data(), buy_pnl.size());
            }

            // Process SELL positions with SIMD
            if (!sell_entry_prices_.empty()) {
                std::vector<double> sell_pnl(sell_entry_prices_.size());
                simd::calculate_pnl_batch(
                    sell_entry_prices_.data(),
                    sell_lot_sizes_.data(),
                    tick.ask,  // SELL positions close at ask
                    config_.contract_size,
                    sell_pnl.data(),
                    sell_entry_prices_.size(),
                    false  // is_buy = false
                );
                equity_ += simd::sum(sell_pnl.data(), sell_pnl.size());
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

        // MT5 stop-out level is typically 20%
        const double STOP_OUT_LEVEL = 20.0;

        if (margin_level < STOP_OUT_LEVEL) {
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
        // TODO: Implement pending order execution (limit/stop orders)
        // For now, only market orders are supported
    }

    void ProcessOpenPositions(const Tick& tick) {
        // Check stop loss and take profit for all open positions
        std::vector<Trade*> positions_to_close;

        for (Trade* trade : open_positions_) {
            if (trade->IsBuy()) {
                // For BUY positions, check bid price against SL/TP
                if (trade->stop_loss > 0 && tick.bid <= trade->stop_loss) {
                    positions_to_close.push_back(trade);
                } else if (trade->take_profit > 0 && tick.bid >= trade->take_profit) {
                    positions_to_close.push_back(trade);
                }
            } else { // SELL
                // For SELL positions, check ask price against SL/TP
                if (trade->stop_loss > 0 && tick.ask >= trade->stop_loss) {
                    positions_to_close.push_back(trade);
                } else if (trade->take_profit > 0 && tick.ask <= trade->take_profit) {
                    positions_to_close.push_back(trade);
                }
            }
        }

        // Close positions that hit SL/TP
        for (Trade* trade : positions_to_close) {
            std::string reason = "SL";
            if (trade->IsBuy()) {
                if (trade->take_profit > 0 && tick.bid >= trade->take_profit) {
                    reason = "TP";
                }
            } else {
                if (trade->take_profit > 0 && tick.ask <= trade->take_profit) {
                    reason = "TP";
                }
            }
            ClosePosition(trade, reason);
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
