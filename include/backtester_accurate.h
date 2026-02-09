#pragma once
/**
 * backtester_accurate.h
 *
 * Accurate backtester that properly models MT5 behavior:
 * 1. Real margin calculation (Used Margin, Free Margin, Margin Level)
 * 2. Stop-out when margin level falls below threshold
 * 3. Spread costs on every entry AND exit
 * 4. Position sizing based on margin requirements
 *
 * This backtester should produce results matching MT5 Strategy Tester.
 */

#include <vector>
#include <cmath>
#include <algorithm>
#include <string>

struct AccuratePosition {
    double entry_price;      // Price at entry (ask for buy)
    double lots;
    double peak_price;       // For trailing stop
    double trailing_stop;
    bool trailing_active;
};

struct BacktestConfig {
    // === Core Parameters ===
    double survive_down_pct = 30.0;     // % price can drop (use 30% for safety!)
    double min_entry_spacing = 50.0;    // Minimum price units between entries

    // === ATR Trailing Stop ===
    bool enable_trailing = false;       // DEFAULT OFF - trailing kills this strategy!
    double atr_multiplier = 2.0;
    int atr_period = 14;

    // === Margin Settings (MT5-like) ===
    double leverage = 500.0;
    double stop_out_level = 50.0;       // Margin level % that triggers stop-out
    double contract_size = 1.0;         // NAS100=1, Gold=100
    double spread = 1.0;                // Full spread in price units

    // === Portfolio Protection ===
    double max_portfolio_dd = 100.0;    // 100 = disabled
};

struct BacktestResult {
    double final_equity;
    double final_balance;
    double max_drawdown_pct;
    double max_equity;
    int total_trades;
    int winning_trades;
    int losing_trades;
    int stop_outs;
    int trailing_exits;
    double total_spread_cost;
    bool margin_call_occurred;
    std::string stop_reason;
};

class AccurateBacktester {
private:
    BacktestConfig config_;
    std::vector<AccuratePosition> positions_;

    // Account state
    double balance_;           // Realized P&L
    double initial_balance_;
    double peak_equity_;

    // Tracking
    double all_time_high_;
    double last_entry_price_;

    // ATR
    std::vector<double> tr_values_;
    double current_atr_;
    double prev_close_;

    // Statistics
    BacktestResult result_;

public:
    void configure(const BacktestConfig& cfg) {
        config_ = cfg;
    }

    void reset(double starting_balance) {
        positions_.clear();
        balance_ = starting_balance;
        initial_balance_ = starting_balance;
        peak_equity_ = starting_balance;
        all_time_high_ = 0.0;
        last_entry_price_ = 0.0;

        tr_values_.clear();
        current_atr_ = 0.0;
        prev_close_ = 0.0;

        result_ = BacktestResult{};
        result_.max_equity = starting_balance;
    }

    // Main tick processing
    void on_tick(double bid, double ask) {
        double mid = (bid + ask) / 2.0;

        // Update ATR using mid price
        update_atr(mid);

        // 1. Check margin level - stop out if too low
        if (check_margin_stop_out(bid)) {
            return;  // Account blown
        }

        // 2. Check portfolio DD limit
        if (check_portfolio_dd(bid)) {
            return;
        }

        // 3. Process trailing stops (if enabled)
        if (config_.enable_trailing) {
            process_trailing_stops(bid);
        }

        // 4. Check for new entry
        check_new_entry(ask);

        // Update peak equity
        double equity = get_equity(bid);
        if (equity > result_.max_equity) {
            result_.max_equity = equity;
        }
        if (equity > peak_equity_) {
            peak_equity_ = equity;
        }

        // Update max drawdown
        if (peak_equity_ > 0) {
            double dd = 100.0 * (peak_equity_ - equity) / peak_equity_;
            if (dd > result_.max_drawdown_pct) {
                result_.max_drawdown_pct = dd;
            }
        }
    }

    // Convenience: process with single price (assumes spread from config)
    void on_tick(double price) {
        double half_spread = config_.spread / 2.0;
        on_tick(price - half_spread, price + half_spread);
    }

    // Get current equity (mark-to-market at bid price for longs)
    double get_equity(double bid) const {
        double unrealized_pnl = 0.0;
        for (const auto& pos : positions_) {
            // For long positions, we'd exit at bid
            unrealized_pnl += (bid - pos.entry_price) * pos.lots * config_.contract_size;
        }
        return balance_ + unrealized_pnl;
    }

    // Get used margin
    double get_used_margin(double price) const {
        double margin = 0.0;
        for (const auto& pos : positions_) {
            // Margin = lots * contract_size * price / leverage
            margin += pos.lots * config_.contract_size * price / config_.leverage;
        }
        return margin;
    }

    // Get margin level (%)
    double get_margin_level(double bid) const {
        double used = get_used_margin(bid);
        if (used <= 0) return 9999.0;  // No positions = infinite margin level
        double equity = get_equity(bid);
        return (equity / used) * 100.0;
    }

    // Get free margin
    double get_free_margin(double bid) const {
        return get_equity(bid) - get_used_margin(bid);
    }

    double get_balance() const { return balance_; }
    int get_position_count() const { return positions_.size(); }

    BacktestResult get_result(double final_bid) {
        result_.final_equity = get_equity(final_bid);
        result_.final_balance = balance_;
        return result_;
    }

private:
    void update_atr(double price) {
        if (prev_close_ > 0) {
            double tr = std::abs(price - prev_close_);
            tr_values_.push_back(tr);

            if (tr_values_.size() > (size_t)config_.atr_period) {
                tr_values_.erase(tr_values_.begin());
            }

            if (tr_values_.size() >= (size_t)config_.atr_period) {
                double sum = 0.0;
                for (double v : tr_values_) sum += v;
                current_atr_ = sum / tr_values_.size();
            }
        }
        prev_close_ = price;
    }

    bool check_margin_stop_out(double bid) {
        if (positions_.empty()) return false;

        double margin_level = get_margin_level(bid);

        if (margin_level < config_.stop_out_level) {
            // STOP OUT! Close all positions at current bid
            result_.margin_call_occurred = true;
            result_.stop_outs++;
            result_.stop_reason = "Margin call at " + std::to_string(margin_level) + "% margin level";

            // Close all positions (they're already underwater)
            while (!positions_.empty()) {
                close_position(0, bid, false);
            }
            return true;
        }
        return false;
    }

    bool check_portfolio_dd(double bid) {
        if (config_.max_portfolio_dd >= 100.0) return false;
        if (positions_.empty()) return false;

        double equity = get_equity(bid);
        double dd_pct = 100.0 * (peak_equity_ - equity) / peak_equity_;

        if (dd_pct >= config_.max_portfolio_dd) {
            result_.stop_reason = "Portfolio DD limit at " + std::to_string(dd_pct) + "%";
            close_all_positions(bid);
            return true;
        }
        return false;
    }

    void process_trailing_stops(double bid) {
        if (current_atr_ <= 0) return;

        std::vector<int> to_close;

        for (size_t i = 0; i < positions_.size(); i++) {
            AccuratePosition& pos = positions_[i];

            // Update peak and trailing stop
            if (bid > pos.peak_price) {
                pos.peak_price = bid;
                pos.trailing_stop = bid - (current_atr_ * config_.atr_multiplier);
                pos.trailing_active = true;
            }

            // Check if trailing stop hit
            if (pos.trailing_active && bid <= pos.trailing_stop) {
                to_close.push_back(i);
            }
        }

        // Close in reverse order
        for (int i = to_close.size() - 1; i >= 0; i--) {
            close_position(to_close[i], bid, true);
            result_.trailing_exits++;
        }

        // After trailing closes, allow new entries at next new high
        // This models the "churning" behavior that kills MT5 accounts
        if (!to_close.empty()) {
            reset_entry_after_trailing_close();
        }
    }

    void check_new_entry(double ask) {
        // Only enter on new all-time highs
        if (ask <= all_time_high_) return;

        // IMPORTANT: When trailing is enabled, check spacing from LAST ENTRY
        // not from last closed position. This models MT5 behavior where
        // after a trailing close, a new position opens at the next new high.
        if (last_entry_price_ > 0 &&
            (ask - last_entry_price_) < config_.min_entry_spacing) {
            all_time_high_ = ask;
            return;
        }

        // Calculate position size
        double lots = calculate_lot_size(ask);
        if (lots < 0.01) return;

        // Check if we have enough free margin
        double required_margin = lots * config_.contract_size * ask / config_.leverage;
        double bid = ask - config_.spread;
        double free_margin = get_free_margin(bid);

        if (required_margin > free_margin * 0.9) {  // Leave 10% buffer
            return;  // Not enough margin
        }

        // Open position at ASK price (includes spread cost)
        AccuratePosition pos;
        pos.entry_price = ask;  // Buy at ask
        pos.lots = lots;
        pos.peak_price = ask;
        pos.trailing_stop = ask - (current_atr_ > 0 ? current_atr_ * config_.atr_multiplier : ask * 0.01);
        pos.trailing_active = false;

        positions_.push_back(pos);

        // Track spread cost (entry side)
        result_.total_spread_cost += (config_.spread / 2.0) * lots * config_.contract_size;

        all_time_high_ = ask;
        last_entry_price_ = ask;
        result_.total_trades++;
    }

    // Reset entry tracking when trailing closes a position (allows re-entry)
    void reset_entry_after_trailing_close() {
        // After trailing close, reset last_entry_price to 0
        // This allows new entries at the next new high
        // This models the "churning" behavior in MT5
        if (positions_.empty()) {
            last_entry_price_ = 0;
        }
    }

    double calculate_lot_size(double price) {
        // Use the original MT5-style calculation
        // Based on: how much can we lose if price drops by survive_down_pct?

        double equity = get_equity(price - config_.spread);
        if (equity <= 0) return 0;

        // Calculate the price drop we need to survive
        double survive_drop = price * (config_.survive_down_pct / 100.0);

        // For existing positions, calculate how much we'd lose
        double existing_loss = 0.0;
        for (const auto& pos : positions_) {
            existing_loss += survive_drop * pos.lots * config_.contract_size;
        }

        // Available equity after existing position losses
        double available = equity - existing_loss;
        if (available <= 0) return 0;

        // Margin required per lot
        double margin_per_lot = price * config_.contract_size / config_.leverage;

        // Loss per lot if price drops by survive_drop
        double loss_per_lot = survive_drop * config_.contract_size;

        // We need: margin + potential_loss < available * safety_factor
        // Solve for lots: lots * (margin_per_lot + loss_per_lot) < available * 0.5
        double total_per_lot = margin_per_lot + loss_per_lot + config_.spread * config_.contract_size;

        if (total_per_lot <= 0) return 0.01;

        double lots = (available * 0.5) / total_per_lot;

        // Apply limits
        if (lots < 0.01) lots = 0.01;
        if (lots > 100.0) lots = 100.0;

        // Round to 2 decimal places
        lots = std::floor(lots * 100.0) / 100.0;

        return lots;
    }

    void close_position(int index, double bid, bool is_trailing) {
        if (index < 0 || index >= (int)positions_.size()) return;

        AccuratePosition& pos = positions_[index];

        // P&L: exit at bid, entered at ask
        double pnl = (bid - pos.entry_price) * pos.lots * config_.contract_size;

        // Track spread cost (exit side)
        result_.total_spread_cost += (config_.spread / 2.0) * pos.lots * config_.contract_size;

        balance_ += pnl;

        if (pnl > 0) {
            result_.winning_trades++;
        } else {
            result_.losing_trades++;
        }

        positions_.erase(positions_.begin() + index);
    }

    void close_all_positions(double bid) {
        while (!positions_.empty()) {
            close_position(0, bid, false);
        }
        last_entry_price_ = 0;
    }
};

// Convenience function to run a complete backtest
inline BacktestResult run_accurate_backtest(
    const std::vector<std::pair<double, double>>& ticks,  // bid, ask pairs
    const BacktestConfig& config,
    double starting_balance
) {
    AccurateBacktester bt;
    bt.configure(config);
    bt.reset(starting_balance);

    for (const auto& tick : ticks) {
        bt.on_tick(tick.first, tick.second);
    }

    return bt.get_result(ticks.back().first);
}

// Overload for single-price data (uses spread from config)
inline BacktestResult run_accurate_backtest(
    const std::vector<double>& prices,
    const BacktestConfig& config,
    double starting_balance
) {
    AccurateBacktester bt;
    bt.configure(config);
    bt.reset(starting_balance);

    for (double price : prices) {
        bt.on_tick(price);
    }

    double final_bid = prices.back() - config.spread / 2.0;
    return bt.get_result(final_bid);
}
