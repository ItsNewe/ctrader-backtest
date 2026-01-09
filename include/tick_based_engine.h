#ifndef TICK_BASED_ENGINE_H
#define TICK_BASED_ENGINE_H

#include "tick_data.h"
#include "tick_data_manager.h"
#include "position_validator.h"
#include "currency_converter.h"
#include "currency_rate_manager.h"
#include <vector>
#include <memory>
#include <functional>
#include <iostream>
#include <cstring>
#include <iomanip>

namespace backtest {

/**
 * Simple trade structure for tick-based engine
 */
struct Trade {
    int id;
    std::string symbol;
    std::string direction;  // "BUY" or "SELL"
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

    Trade() : id(0), entry_price(0), exit_price(0), lot_size(0),
              stop_loss(0), take_profit(0), profit_loss(0), commission(0) {}
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

    // Date filtering (MT5 behavior: start_date inclusive, end_date exclusive)
    std::string start_date = "";      // Format: YYYY.MM.DD (empty = no filter)
    std::string end_date = "";        // Format: YYYY.MM.DD (empty = no filter)

    // Swap/Rollover fees
    double swap_long = 0.0;           // Swap for long positions (per lot per day)
    double swap_short = 0.0;          // Swap for short positions (per lot per day)
    int swap_mode = 2;                // 2 = SYMBOL_SWAP_MODE_CURRENCY_SYMBOL (in account currency)
    int swap_3days = 3;               // Day of week for triple swap (0=Sun, 1=Mon, 2=Tue, 3=Wed, 4=Thu, 5=Fri, 6=Sat)

    // Market session settings
    // For Gold (XAUUSD): trades Mon-Fri only (trading_days = 0b0111110 = 62)
    // For Crypto: trades 7 days (trading_days = 0b1111111 = 127)
    int trading_days = 62;            // Bitmask: bit 0=Sun, bit 1=Mon, ... bit 6=Sat (default: Mon-Fri)
    int market_close_hour = 23;       // Hour when market closes (server time)
    int market_open_hour = 0;         // Hour when market opens (server time)

    // Tick data source
    TickDataConfig tick_data_config;

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
          tick_manager_(config.tick_data_config) {
    }

    // Strategy callback signature
    // Called for each tick with current tick data
    using StrategyCallback = std::function<void(const Tick& tick, TickBasedEngine& engine)>;

    // Run backtest with strategy
    void Run(StrategyCallback strategy) {
        std::cout << "=== Tick-Based Backtest Started ===" << std::endl;
        std::cout << "Symbol: " << config_.symbol << std::endl;
        std::cout << "Initial Balance: $" << balance_ << std::endl;

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

            // Update equity with current tick prices
            UpdateEquity(tick);

            // Check for margin stop-out (must be done after UpdateEquity)
            CheckMarginStopOut(tick);

            // Process swap/rollover fees (must be done before strategy to affect equity)
            ProcessSwap(tick);

            // Check pending orders and stop losses / take profits
            ProcessPendingOrders(tick);
            ProcessOpenPositions(tick);

            // Call strategy callback
            strategy(tick, *this);

            // Progress indicator
            if (tick_count % progress_interval == 0) {
                std::cout << "Processed " << tick_count << " ticks... Equity: $" << equity_ << std::endl;
            }
        }

        std::cout << "\n=== Backtest Complete ===" << std::endl;
        std::cout << "Total Ticks Processed: " << tick_count << std::endl;
        PrintResults();
    }

    // Trading operations

    /**
     * Open market order at current tick price
     */
    Trade* OpenMarketOrder(const std::string& direction, double lot_size,
                           double stop_loss = 0.0, double take_profit = 0.0) {
        if (!current_tick_.timestamp.empty()) {
            // Use bid for SELL, ask for BUY
            double entry_price = (direction == "BUY") ? current_tick_.ask : current_tick_.bid;

            // Apply slippage
            if (config_.slippage_pips > 0) {
                double slippage = config_.slippage_pips * GetPipValue();
                entry_price += (direction == "BUY" ? slippage : -slippage);
            }

            Trade* trade = CreateTrade(direction, entry_price, lot_size, stop_loss, take_profit);
            open_positions_.push_back(trade);

            // Uncomment for per-trade logging:
            // std::cout << current_tick_.timestamp << " - OPEN " << direction
            //           << " " << lot_size << " lots @ " << entry_price << std::endl;

            return trade;
        }
        return nullptr;
    }

    /**
     * Close position at current tick price
     */
    bool ClosePosition(Trade* trade, const std::string& reason = "Manual") {
        if (!trade || current_tick_.timestamp.empty()) {
            return false;
        }

        // Use ask for closing SELL, bid for closing BUY
        double exit_price = (trade->direction == "BUY") ? current_tick_.bid : current_tick_.ask;

        // Apply slippage
        if (config_.slippage_pips > 0) {
            double slippage = config_.slippage_pips * GetPipValue();
            exit_price += (trade->direction == "BUY" ? -slippage : slippage);
        }

        trade->exit_price = exit_price;
        trade->exit_time = current_tick_.timestamp;
        trade->exit_reason = reason;

        CalculateProfitLoss(trade);
        balance_ += trade->profit_loss;

        closed_trades_.push_back(*trade);

        // Remove from open positions
        open_positions_.erase(
            std::remove(open_positions_.begin(), open_positions_.end(), trade),
            open_positions_.end()
        );

        std::cout << current_tick_.timestamp << " - CLOSE " << trade->direction
                  << " @ " << exit_price << " | P/L: $" << trade->profit_loss
                  << " | Reason: " << reason << std::endl;

        return true;
    }

    // Getters
    double GetBalance() const { return balance_; }
    double GetEquity() const { return equity_; }
    const Tick& GetCurrentTick() const { return current_tick_; }
    const std::vector<Trade*>& GetOpenPositions() const { return open_positions_; }
    const std::vector<Trade>& GetClosedTrades() const { return closed_trades_; }
    size_t GetTotalTrades() const { return closed_trades_.size(); }

    // Results
    struct BacktestResults {
        double initial_balance;
        double final_balance;
        double total_profit_loss;
        size_t total_trades;
        size_t winning_trades;
        size_t losing_trades;
        double win_rate;
        double average_win;
        double average_loss;
        double largest_win;
        double largest_loss;
        double max_drawdown;
        double total_swap_charged;
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

        // TODO: Calculate max drawdown
        results.max_drawdown = 0.0;

        results.total_swap_charged = total_swap_charged_;

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

    double GetPipValue() const {
        // For 5-digit brokers (0.00001)
        return 0.00001;
    }

    Trade* CreateTrade(const std::string& direction, double entry_price,
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
        if (trade->direction == "SELL") {
            price_diff = -price_diff;
        }

        // P/L = price_diff * lot_size * contract_size
        // For XAUUSD: contract_size = 100, for Forex pairs = 100,000
        double profit = price_diff * trade->lot_size * config_.contract_size;
        trade->profit_loss = profit - trade->commission;
    }

    void UpdateEquity(const Tick& tick) {
        equity_ = balance_;

        // Add unrealized P/L from open positions
        for (Trade* trade : open_positions_) {
            double current_price = (trade->direction == "BUY") ? tick.bid : tick.ask;
            double price_diff = current_price - trade->entry_price;
            if (trade->direction == "SELL") {
                price_diff = -price_diff;
            }
            // Use actual contract size (100 for XAUUSD, 100000 for Forex)
            double unrealized_pl = price_diff * trade->lot_size * config_.contract_size;
            equity_ += unrealized_pl;
        }
    }

    void CheckMarginStopOut(const Tick& tick) {
        // Calculate current margin level
        // Margin Level = (Equity / Used Margin) × 100%
        // Stop out occurs when margin level falls below stop_out_level (typically 20%)

        if (open_positions_.empty()) {
            return;  // No positions, no margin risk
        }

        // Calculate used margin (required margin for all open positions)
        double used_margin = 0.0;
        for (Trade* trade : open_positions_) {
            double current_price = (trade->direction == "BUY") ? tick.ask : tick.bid;
            // Margin = Lots × Contract_Size × Price / Leverage × Margin_Rate
            double position_margin = trade->lot_size * config_.contract_size * current_price / config_.leverage * config_.margin_rate;
            used_margin += position_margin;
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
            std::cout << "\n!!! MARGIN STOP OUT !!!" << std::endl;
            std::cout << "Margin Level: " << margin_level << "%" << std::endl;
            std::cout << "Equity: $" << equity_ << std::endl;
            std::cout << "Used Margin: $" << used_margin << std::endl;
            std::cout << "Closing all " << open_positions_.size() << " positions..." << std::endl;

            // Close all positions
            while (!open_positions_.empty()) {
                ClosePosition(open_positions_[0], "STOP OUT");
            }

            std::cout << "STOP OUT complete. Final Balance: $" << balance_ << std::endl;
            std::cout << "\n=== Test FAILED due to margin stop-out ===" << std::endl;
            exit(1);  // Exit the backtest
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
            if (trade->direction == "BUY") {
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
            if (trade->direction == "BUY") {
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
    int GetDayOfWeek(const std::string& date_str) {
        // Parse YYYY.MM.DD format
        int year = std::stoi(date_str.substr(0, 4));
        int month = std::stoi(date_str.substr(5, 2));
        int day = std::stoi(date_str.substr(8, 2));

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
        // Swap is charged at market open (00:00 server time) when market reopens
        // This happens BEFORE any TP checks, so positions that hit TP still pay swap
        // Triple swap is charged on a specific day (usually Wednesday) to cover weekend
        //
        // Market schedule:
        // - Gold: typically closes ~23:00, reopens ~00:00, Mon-Fri only
        // - Crypto: 7 days a week
        //
        // Swap timing: charged when market OPENS, not closes

        std::string current_date = tick.timestamp.substr(0, 10);  // Get "YYYY.MM.DD"
        int current_hour = GetHourFromTimestamp(tick.timestamp);

        // Detect market open: first tick at/after market_open_hour on a new trading day
        bool is_market_open_tick = false;

        if (current_date != last_swap_date_ && !last_swap_date_.empty()) {
            int day_of_week = GetDayOfWeek(current_date);

            // Check if this is a trading day
            if (IsMarketOpen(day_of_week)) {
                // First tick of a new trading day - this is market open
                is_market_open_tick = true;
            }
        }

        // Charge swap at market open
        if (is_market_open_tick && !open_positions_.empty()) {
            int day_of_week = GetDayOfWeek(current_date);

            // Calculate swap days to charge
            // Normal: 1 day
            // Triple swap day: 3 days (covers weekend for Mon-Fri instruments)
            int swap_multiplier = (day_of_week == config_.swap_3days) ? 3 : 1;

            double daily_swap = 0.0;

            for (Trade* trade : open_positions_) {
                double swap_per_lot = 0.0;

                if (trade->direction == "BUY") {
                    swap_per_lot = config_.swap_long;
                } else {
                    swap_per_lot = config_.swap_short;
                }

                // Calculate swap based on swap mode
                double position_swap = 0.0;

                if (config_.swap_mode == 1) {
                    // SYMBOL_SWAP_MODE_POINTS: swap in points, need to convert to currency
                    // Swap in USD = swap_points × point × contract_size × lot_size
                    // For XAUUSD: point = 0.01, contract_size = 100
                    double point = 0.01;  // XAUUSD point value
                    position_swap = swap_per_lot * point * config_.contract_size * trade->lot_size;
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
            if (daily_swap != 0.0) {
                std::cout << current_date << " " << tick.timestamp.substr(11, 8)
                          << " - SWAP at market open: $" << std::fixed << std::setprecision(2) << daily_swap
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
