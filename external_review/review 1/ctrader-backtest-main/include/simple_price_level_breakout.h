#ifndef SIMPLE_PRICE_LEVEL_BREAKOUT_H
#define SIMPLE_PRICE_LEVEL_BREAKOUT_H

#include <vector>
#include <string>
#include <iostream>
#include <iomanip>
#include "position_validator.h"
#include "currency_converter.h"
#include "currency_rate_manager.h"

// Simple bar structure for OHLC data
struct Bar {
    std::string timestamp;
    double open;
    double high;
    double low;
    double close;
};

// Trade record
struct Trade {
    int id;
    std::string direction;  // "BUY" or "SELL"
    std::string entry_time;
    double entry_price;
    double stop_loss;
    double take_profit;
    double lot_size;
    std::string exit_time;
    double exit_price;
    std::string exit_reason;  // "TP", "SL"
    double profit_loss;
    bool is_open;
};

// Backtest results
struct BacktestResults {
    double initial_balance;
    double final_balance;
    double total_profit_loss;
    int total_trades;
    int winning_trades;
    int losing_trades;
    std::vector<Trade> trades;
};

class SimplePriceLevelBreakout {
private:
    // Strategy parameters
    double long_trigger_level_;
    double short_trigger_level_;
    double lot_size_;
    int stop_loss_pips_;
    int take_profit_pips_;

    // Account settings
    double initial_balance_;
    double current_balance_;
    std::string account_currency_;
    int leverage_;

    // Symbol information
    std::string symbol_;
    double min_lot_;
    double max_lot_;
    double lot_step_;
    double tick_size_;
    double tick_value_;
    int stop_level_;

    // Components (initialized in constructor)
    PositionValidator validator_;
    CurrencyConverter converter_;
    CurrencyRateManager rate_manager_;

    // State
    Trade* current_position_;
    std::vector<Trade> trade_history_;
    int next_trade_id_;

    // Price data
    std::vector<Bar> bars_;

public:
    SimplePriceLevelBreakout(
        double long_trigger_level,
        double short_trigger_level,
        double lot_size,
        int stop_loss_pips,
        int take_profit_pips
    ) : long_trigger_level_(long_trigger_level),
        short_trigger_level_(short_trigger_level),
        lot_size_(lot_size),
        stop_loss_pips_(stop_loss_pips),
        take_profit_pips_(take_profit_pips),
        initial_balance_(10000.0),
        current_balance_(10000.0),
        account_currency_("USD"),
        leverage_(100),
        symbol_("EURUSD"),
        validator_(),
        converter_("USD"),
        rate_manager_("USD", 60),
        current_position_(nullptr),
        next_trade_id_(1)
    {
        // EURUSD symbol specifications
        min_lot_ = 0.01;
        max_lot_ = 100.0;
        lot_step_ = 0.01;
        tick_size_ = 0.00001;
        tick_value_ = 1.0;  // For EURUSD, 1 pip = $1 for 0.01 lot
        stop_level_ = 5;    // 5 pips minimum stop distance
    }

    void LoadPriceData(const std::vector<Bar>& bars) {
        bars_ = bars;
        std::cout << "Loaded " << bars_.size() << " bars" << std::endl;
    }

    BacktestResults RunBacktest() {
        std::cout << "\n========================================" << std::endl;
        std::cout << "Simple Price Level Breakout - Backtest" << std::endl;
        std::cout << "========================================\n" << std::endl;

        std::cout << "Strategy Parameters:" << std::endl;
        std::cout << "  Long Trigger: " << std::fixed << std::setprecision(5) << long_trigger_level_ << std::endl;
        std::cout << "  Short Trigger: " << std::fixed << std::setprecision(5) << short_trigger_level_ << std::endl;
        std::cout << "  Lot Size: " << lot_size_ << std::endl;
        std::cout << "  Stop Loss: " << stop_loss_pips_ << " pips" << std::endl;
        std::cout << "  Take Profit: " << take_profit_pips_ << " pips\n" << std::endl;

        std::cout << "Account Settings:" << std::endl;
        std::cout << "  Initial Balance: $" << initial_balance_ << std::endl;
        std::cout << "  Currency: " << account_currency_ << std::endl;
        std::cout << "  Leverage: 1:" << leverage_ << "\n" << std::endl;

        // Process each bar
        for (size_t i = 2; i < bars_.size(); ++i) {
            const Bar& current_bar = bars_[i];
            const Bar& prev_bar = bars_[i - 1];
            const Bar& prev_prev_bar = bars_[i - 2];

            // Check if open position hit SL or TP
            if (current_position_ != nullptr) {
                CheckExitConditions(current_bar);
            }

            // Check for new entry if no position
            if (current_position_ == nullptr) {
                CheckEntryConditions(prev_prev_bar, prev_bar, current_bar);
            }
        }

        // Close any remaining open position
        if (current_position_ != nullptr) {
            std::cout << "\nClosing open position at end of backtest" << std::endl;
            ClosePosition(bars_.back().close, "END", bars_.back().timestamp);
        }

        // Calculate results
        BacktestResults results;
        results.initial_balance = initial_balance_;
        results.final_balance = current_balance_;
        results.total_profit_loss = current_balance_ - initial_balance_;
        results.total_trades = trade_history_.size();
        results.winning_trades = 0;
        results.losing_trades = 0;
        results.trades = trade_history_;

        for (const auto& trade : trade_history_) {
            if (trade.profit_loss > 0) {
                results.winning_trades++;
            } else if (trade.profit_loss < 0) {
                results.losing_trades++;
            }
        }

        PrintResults(results);

        return results;
    }

private:
    void CheckEntryConditions(const Bar& prev_prev_bar, const Bar& prev_bar, const Bar& current_bar) {
        double prev_close = prev_bar.close;
        double prev_prev_close = prev_prev_bar.close;

        // Check for long entry (breakout above level)
        if (prev_prev_close <= long_trigger_level_ && prev_close > long_trigger_level_) {
            std::cout << "\n>>> LONG BREAKOUT DETECTED <<<" << std::endl;
            std::cout << "  Time: " << current_bar.timestamp << std::endl;
            std::cout << "  Prev-Prev Close: " << std::fixed << std::setprecision(5) << prev_prev_close << std::endl;
            std::cout << "  Prev Close: " << prev_close << std::endl;
            std::cout << "  Trigger Level: " << long_trigger_level_ << std::endl;
            OpenLongPosition(current_bar);
        }
        // Check for short entry (breakout below level)
        else if (prev_prev_close >= short_trigger_level_ && prev_close < short_trigger_level_) {
            std::cout << "\n>>> SHORT BREAKOUT DETECTED <<<" << std::endl;
            std::cout << "  Time: " << current_bar.timestamp << std::endl;
            std::cout << "  Prev-Prev Close: " << std::fixed << std::setprecision(5) << prev_prev_close << std::endl;
            std::cout << "  Prev Close: " << prev_close << std::endl;
            std::cout << "  Trigger Level: " << short_trigger_level_ << std::endl;
            OpenShortPosition(current_bar);
        }
    }

    void OpenLongPosition(const Bar& bar) {
        double entry_price = bar.open;  // Enter at next bar open
        double sl = entry_price - (stop_loss_pips_ * 0.0001);
        double tp = entry_price + (take_profit_pips_ * 0.0001);

        Trade* trade = new Trade();
        trade->id = next_trade_id_++;
        trade->direction = "BUY";
        trade->entry_time = bar.timestamp;
        trade->entry_price = entry_price;
        trade->stop_loss = sl;
        trade->take_profit = tp;
        trade->lot_size = lot_size_;
        trade->is_open = true;

        current_position_ = trade;

        std::cout << "=== LONG POSITION OPENED ===" << std::endl;
        std::cout << "  Trade ID: " << trade->id << std::endl;
        std::cout << "  Entry Price: " << std::fixed << std::setprecision(5) << entry_price << std::endl;
        std::cout << "  Stop Loss: " << sl << " (" << stop_loss_pips_ << " pips)" << std::endl;
        std::cout << "  Take Profit: " << tp << " (" << take_profit_pips_ << " pips)" << std::endl;
    }

    void OpenShortPosition(const Bar& bar) {
        double entry_price = bar.open;  // Enter at next bar open
        double sl = entry_price + (stop_loss_pips_ * 0.0001);
        double tp = entry_price - (take_profit_pips_ * 0.0001);

        Trade* trade = new Trade();
        trade->id = next_trade_id_++;
        trade->direction = "SELL";
        trade->entry_time = bar.timestamp;
        trade->entry_price = entry_price;
        trade->stop_loss = sl;
        trade->take_profit = tp;
        trade->lot_size = lot_size_;
        trade->is_open = true;

        current_position_ = trade;

        std::cout << "=== SHORT POSITION OPENED ===" << std::endl;
        std::cout << "  Trade ID: " << trade->id << std::endl;
        std::cout << "  Entry Price: " << std::fixed << std::setprecision(5) << entry_price << std::endl;
        std::cout << "  Stop Loss: " << sl << " (" << stop_loss_pips_ << " pips)" << std::endl;
        std::cout << "  Take Profit: " << tp << " (" << take_profit_pips_ << " pips)" << std::endl;
    }

    void CheckExitConditions(const Bar& bar) {
        if (current_position_ == nullptr) return;

        if (current_position_->direction == "BUY") {
            // Check if low hit stop loss
            if (bar.low <= current_position_->stop_loss) {
                ClosePosition(current_position_->stop_loss, "SL", bar.timestamp);
            }
            // Check if high hit take profit
            else if (bar.high >= current_position_->take_profit) {
                ClosePosition(current_position_->take_profit, "TP", bar.timestamp);
            }
        } else {  // SELL
            // Check if high hit stop loss
            if (bar.high >= current_position_->stop_loss) {
                ClosePosition(current_position_->stop_loss, "SL", bar.timestamp);
            }
            // Check if low hit take profit
            else if (bar.low <= current_position_->take_profit) {
                ClosePosition(current_position_->take_profit, "TP", bar.timestamp);
            }
        }
    }

    void ClosePosition(double exit_price, const std::string& reason, const std::string& time) {
        if (current_position_ == nullptr) return;

        current_position_->exit_price = exit_price;
        current_position_->exit_reason = reason;
        current_position_->exit_time = time;
        current_position_->is_open = false;

        // Calculate P/L
        double price_change = 0.0;
        if (current_position_->direction == "BUY") {
            price_change = exit_price - current_position_->entry_price;
        } else {
            price_change = current_position_->entry_price - exit_price;
        }

        // P/L = lot_size * 100,000 * price_change
        double profit_loss = lot_size_ * 100000.0 * price_change;
        current_position_->profit_loss = profit_loss;

        current_balance_ += profit_loss;

        std::cout << "\n=== POSITION CLOSED ===" << std::endl;
        std::cout << "  Trade ID: " << current_position_->id << std::endl;
        std::cout << "  Direction: " << current_position_->direction << std::endl;
        std::cout << "  Entry: " << std::fixed << std::setprecision(5) << current_position_->entry_price << std::endl;
        std::cout << "  Exit: " << exit_price << std::endl;
        std::cout << "  Reason: " << reason << std::endl;
        std::cout << "  P/L: $" << std::fixed << std::setprecision(2) << profit_loss << std::endl;
        std::cout << "  Balance: $" << current_balance_ << std::endl;

        trade_history_.push_back(*current_position_);
        delete current_position_;
        current_position_ = nullptr;
    }

    void PrintResults(const BacktestResults& results) {
        std::cout << "\n========================================" << std::endl;
        std::cout << "Backtest Results" << std::endl;
        std::cout << "========================================\n" << std::endl;

        std::cout << "Initial Balance: $" << std::fixed << std::setprecision(2) << results.initial_balance << std::endl;
        std::cout << "Final Balance: $" << results.final_balance << std::endl;
        std::cout << "Total P/L: $" << results.total_profit_loss << std::endl;
        std::cout << "Total Trades: " << results.total_trades << std::endl;
        std::cout << "Winning Trades: " << results.winning_trades << std::endl;
        std::cout << "Losing Trades: " << results.losing_trades << std::endl;

        if (results.total_trades > 0) {
            double win_rate = (double)results.winning_trades / results.total_trades * 100.0;
            std::cout << "Win Rate: " << std::fixed << std::setprecision(1) << win_rate << "%" << std::endl;
        }

        std::cout << "\n=== Trade History ===" << std::endl;
        std::cout << std::left
                  << std::setw(5) << "ID"
                  << std::setw(6) << "Dir"
                  << std::setw(20) << "Entry Time"
                  << std::setw(10) << "Entry"
                  << std::setw(10) << "Exit"
                  << std::setw(5) << "Why"
                  << std::setw(12) << "P/L"
                  << std::endl;
        std::cout << std::string(68, '-') << std::endl;

        for (const auto& trade : results.trades) {
            std::cout << std::left
                      << std::setw(5) << trade.id
                      << std::setw(6) << trade.direction
                      << std::setw(20) << trade.entry_time
                      << std::setw(10) << std::fixed << std::setprecision(5) << trade.entry_price
                      << std::setw(10) << trade.exit_price
                      << std::setw(5) << trade.exit_reason
                      << std::setw(12) << std::fixed << std::setprecision(2) << trade.profit_loss
                      << std::endl;
        }

        std::cout << "\n========================================\n" << std::endl;
    }
};

#endif // SIMPLE_PRICE_LEVEL_BREAKOUT_H
