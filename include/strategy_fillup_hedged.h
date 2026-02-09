#pragma once
/**
 * Hedged Fill-Up Strategy (Anti-Fragile)
 *
 * Original Fill-Up problem:
 * - Only opens BUY positions
 * - Stops trading when price drops (survive_down limit)
 * - Misses oscillation profits during drawdowns
 *
 * Solution - Add SELL hedges:
 * - Open SELL positions when price drops below trigger level
 * - SELLs have TP targets (profit from downward oscillations)
 * - Hedge reduces net exposure, allowing more aggressive trading
 * - Can use smaller "effective" survive_down
 * - Keeps trading in BOTH directions during any market condition
 *
 * Philosophy: Turn drawdowns into opportunities by profiting from
 * oscillations in BOTH directions.
 */

#include <vector>
#include <deque>
#include <cmath>
#include <algorithm>

struct HedgedFillUpConfig {
    // Original Fill-Up params
    double survive_down_pct = 2.5;    // Survive down % (can be more aggressive with hedges)
    double spacing = 5.0;             // Grid spacing
    double size_multiplier = 1.0;     // Size multiplier
    int max_positions = 50;           // Max total positions (longs + shorts)

    // Hedge params
    bool enable_hedging = true;
    double hedge_trigger_pct = 1.0;   // Start hedging when price drops X% from highest_buy
    double hedge_ratio = 0.5;         // Hedge size as ratio of long size (0.5 = half size)
    double hedge_spacing = 5.0;       // Spacing between hedge (sell) positions

    // Take profit
    bool use_take_profit = true;
    double tp_buffer = 0.0;           // Extra buffer on TP (0 = spacing + spread)

    // DD Protection - close all when DD exceeds threshold
    bool enable_dd_protection = false;
    double close_all_dd_pct = 70.0;   // Close all positions when DD exceeds this %

    // Velocity Filter - pause trading during crashes
    bool enable_velocity_filter = false;
    double crash_velocity_pct = -1.0; // % drop per velocity_window ticks
    int velocity_window = 500;        // Window size for velocity calculation

    // Risk management
    double max_net_exposure_lots = 5.0;  // Max |long_lots - short_lots|
    double stop_out_level = 50.0;     // Margin stop-out level (Grid uses ~20% but we use 50% for safety buffer)

    // Market
    double leverage = 500.0;
    double contract_size = 100.0;
    double spread = 0.25;
    double min_lot = 0.01;
    double max_lot = 10.0;

    // Swap rates (points per lot per day) -  XAUUSD
    double swap_long = -66.99;        // Cost per lot per day for long positions
    double swap_short = 41.2;         // Credit per lot per day for short positions
    int swap_triple_day = 3;          // Day of week for triple swap (3 = Wednesday, 0 = Sunday)
};

struct HFUPosition {
    double entry_price;
    double lots;
    double take_profit;
    bool is_long;
    int entry_day;      // Day number when position was opened (for swap tracking)
};

struct HedgedFillUpResult {
    double final_equity;
    double max_drawdown_pct;
    double max_equity;
    int long_entries;
    int short_entries;
    int long_tp_hits;
    int short_tp_hits;
    int hedge_activations;
    int dd_protection_triggers;
    int velocity_pauses;
    double max_long_lots;
    double max_short_lots;
    double max_net_exposure;
    bool margin_call_occurred;
    double total_swap_paid;     // Total swap charges paid
    int margin_warnings;        // Times margin level dropped below 100%
};

class HedgedFillUpStrategy {
private:
    HedgedFillUpConfig config_;
    std::vector<HFUPosition> positions_;

    double balance_;
    double initial_balance_;
    double peak_equity_;

    // Grid tracking
    double lowest_buy_;
    double highest_buy_;
    double lowest_sell_;
    double highest_sell_;

    // Hedge state
    bool hedge_active_;
    double hedge_trigger_price_;

    // Velocity filter state
    std::deque<double> price_history_;
    bool velocity_paused_;

    // Stats
    double current_long_lots_;
    double current_short_lots_;

    // Swap tracking
    int current_day_;           // Current day number (0 = first day of backtest)
    int current_day_of_week_;   // 0=Sunday, 1=Monday, ..., 6=Saturday
    bool swap_applied_today_;   // Whether swap was applied for current day

    HedgedFillUpResult result_;

public:
    void configure(const HedgedFillUpConfig& cfg) {
        config_ = cfg;
    }

    void reset(double starting_balance) {
        positions_.clear();
        balance_ = starting_balance;
        initial_balance_ = starting_balance;
        peak_equity_ = starting_balance;

        lowest_buy_ = 1e9;
        highest_buy_ = 0;
        lowest_sell_ = 1e9;
        highest_sell_ = 0;

        hedge_active_ = false;
        hedge_trigger_price_ = 0;

        price_history_.clear();
        velocity_paused_ = false;

        current_long_lots_ = 0;
        current_short_lots_ = 0;

        current_day_ = -1;
        current_day_of_week_ = -1;
        swap_applied_today_ = false;

        result_ = HedgedFillUpResult{};
        result_.max_equity = starting_balance;
        result_.total_swap_paid = 0;
        result_.margin_warnings = 0;
    }

    // New on_tick with day info for swap calculation
    void on_tick(double bid, double ask, int day, int day_of_week) {
        // Apply swap at day rollover (once per day)
        if (day != current_day_ && current_day_ >= 0) {
            apply_daily_swap(day_of_week);
        }
        current_day_ = day;
        current_day_of_week_ = day_of_week;

        // Call the main tick handler
        on_tick_internal(bid, ask);
    }

    // Original on_tick for backwards compatibility (no swap calculation)
    void on_tick(double bid, double ask) {
        on_tick_internal(bid, ask);
    }

private:
    void apply_daily_swap(int day_of_week) {
        if (positions_.empty()) return;

        // Calculate swap multiplier (triple swap on swap_triple_day, typically Wednesday)
        int multiplier = (day_of_week == config_.swap_triple_day) ? 3 : 1;

        double total_swap = 0;
        for (const auto& pos : positions_) {
            // Swap is in points per lot per day
            // For XAUUSD: 1 point = $1 per lot (since contract_size = 100, point_value = 0.01, so 100 * 0.01 = $1)
            double swap_rate = pos.is_long ? config_.swap_long : config_.swap_short;
            double swap_amount = swap_rate * pos.lots * multiplier;
            total_swap += swap_amount;
        }

        balance_ += total_swap;  // swap_long is negative, so this subtracts
        result_.total_swap_paid -= total_swap;  // Track as positive cost
    }

    void on_tick_internal(double bid, double ask) {
        // Check margin first
        if (check_margin_stop_out(bid, ask)) {
            return;
        }

        // Update position tracking
        update_position_tracking();

        // Check TP hits
        check_tp_hits(bid, ask);

        // Update hedge state
        update_hedge_state(bid, ask);

        // Calculate current equity and drawdown
        double equity = get_equity(bid, ask);
        double dd_pct = 0;
        if (peak_equity_ > 0) {
            dd_pct = 100.0 * (peak_equity_ - equity) / peak_equity_;
        }

        // DD Protection: close all if DD exceeds threshold
        if (config_.enable_dd_protection && !positions_.empty() && dd_pct > config_.close_all_dd_pct) {
            close_all_positions(bid, ask);
            result_.dd_protection_triggers++;
            peak_equity_ = balance_;  // Reset peak after DD protection
            return;
        }

        // Update velocity filter
        update_velocity_filter(ask);

        // Open new positions (unless velocity paused)
        if (!velocity_paused_) {
            check_new_long_entries(bid, ask);

            if (config_.enable_hedging && hedge_active_) {
                check_new_short_entries(bid, ask);
            }
        }

        // Update stats
        if (equity > result_.max_equity) {
            result_.max_equity = equity;
        }
        if (equity > peak_equity_) {
            peak_equity_ = equity;
        }
        if (dd_pct > result_.max_drawdown_pct) {
            result_.max_drawdown_pct = dd_pct;
        }

        // Track max exposure
        if (current_long_lots_ > result_.max_long_lots) {
            result_.max_long_lots = current_long_lots_;
        }
        if (current_short_lots_ > result_.max_short_lots) {
            result_.max_short_lots = current_short_lots_;
        }
        double net = std::abs(current_long_lots_ - current_short_lots_);
        if (net > result_.max_net_exposure) {
            result_.max_net_exposure = net;
        }
    }

    double get_equity(double bid, double ask) const {
        double unrealized = 0;
        for (const auto& pos : positions_) {
            if (pos.is_long) {
                unrealized += (bid - pos.entry_price) * pos.lots * config_.contract_size;
            } else {
                unrealized += (pos.entry_price - ask) * pos.lots * config_.contract_size;
            }
        }
        return balance_ + unrealized;
    }

public:
    HedgedFillUpResult get_result(double bid, double ask) {
        result_.final_equity = get_equity(bid, ask);
        return result_;
    }

private:
    void update_position_tracking() {
        lowest_buy_ = 1e9;
        highest_buy_ = 0;
        lowest_sell_ = 1e9;
        highest_sell_ = 0;
        current_long_lots_ = 0;
        current_short_lots_ = 0;

        for (const auto& pos : positions_) {
            if (pos.is_long) {
                lowest_buy_ = std::min(lowest_buy_, pos.entry_price);
                highest_buy_ = std::max(highest_buy_, pos.entry_price);
                current_long_lots_ += pos.lots;
            } else {
                lowest_sell_ = std::min(lowest_sell_, pos.entry_price);
                highest_sell_ = std::max(highest_sell_, pos.entry_price);
                current_short_lots_ += pos.lots;
            }
        }
    }

    void update_hedge_state(double bid, double ask) {
        if (!config_.enable_hedging) return;

        // Calculate hedge trigger price
        if (highest_buy_ > 0) {
            hedge_trigger_price_ = highest_buy_ * (100.0 - config_.hedge_trigger_pct) / 100.0;

            // Activate hedging when price drops below trigger
            if (!hedge_active_ && ask < hedge_trigger_price_) {
                hedge_active_ = true;
                result_.hedge_activations++;
            }

            // Deactivate when price recovers above highest_buy
            // AND close all short positions to avoid losses in uptrend
            if (hedge_active_ && ask >= highest_buy_) {
                hedge_active_ = false;
                close_all_shorts(bid, ask);
            }
        }
    }

    void close_all_shorts(double bid, double ask) {
        // Close all short positions when exiting hedge mode
        for (int i = positions_.size() - 1; i >= 0; i--) {
            if (!positions_[i].is_long) {
                close_position(i, bid, ask);
            }
        }
    }

    void close_all_positions(double bid, double ask) {
        // Close all positions (for DD protection)
        while (!positions_.empty()) {
            close_position(0, bid, ask);
        }
    }

    void update_velocity_filter(double price) {
        if (!config_.enable_velocity_filter) return;

        // Add current price to history
        price_history_.push_back(price);

        // Keep only last N prices
        while (price_history_.size() > static_cast<size_t>(config_.velocity_window)) {
            price_history_.pop_front();
        }

        // Calculate velocity if we have enough history
        if (price_history_.size() >= static_cast<size_t>(config_.velocity_window)) {
            double old_price = price_history_.front();
            double velocity_pct = (price - old_price) / old_price * 100.0;

            // Pause if velocity exceeds threshold (negative = crash)
            if (velocity_pct < config_.crash_velocity_pct) {
                if (!velocity_paused_) {
                    velocity_paused_ = true;
                    result_.velocity_pauses++;
                }
            } else {
                velocity_paused_ = false;
            }
        }
    }

    void check_tp_hits(double bid, double ask) {
        std::vector<int> to_close;
        std::vector<bool> is_long_close;

        for (size_t i = 0; i < positions_.size(); i++) {
            const auto& pos = positions_[i];

            if (pos.is_long && pos.take_profit > 0 && bid >= pos.take_profit) {
                to_close.push_back(i);
                is_long_close.push_back(true);
            }
            else if (!pos.is_long && pos.take_profit > 0 && ask <= pos.take_profit) {
                to_close.push_back(i);
                is_long_close.push_back(false);
            }
        }

        // Close in reverse order
        for (int i = to_close.size() - 1; i >= 0; i--) {
            int idx = to_close[i];
            double close_price = is_long_close[i] ? positions_[idx].take_profit : positions_[idx].take_profit;
            close_position_at_price(idx, close_price, is_long_close[i]);

            if (is_long_close[i]) {
                result_.long_tp_hits++;
            } else {
                result_.short_tp_hits++;
            }
        }
    }

    void check_new_long_entries(double bid, double ask) {
        if (positions_.size() >= static_cast<size_t>(config_.max_positions)) return;

        // Check net exposure limit
        double net_after = current_long_lots_ - current_short_lots_ + config_.min_lot;
        if (std::abs(net_after) > config_.max_net_exposure_lots) return;

        double lot_size = calculate_long_size(ask);
        if (lot_size < config_.min_lot) return;

        bool should_enter = false;

        int long_count = 0;
        for (const auto& pos : positions_) {
            if (pos.is_long) long_count++;
        }

        if (long_count == 0) {
            should_enter = true;
        }
        else if (lowest_buy_ < 1e8 && ask <= lowest_buy_ - config_.spacing) {
            should_enter = true;
        }
        else if (highest_buy_ > 0 && ask >= highest_buy_ + config_.spacing) {
            should_enter = true;
        }

        if (should_enter && can_open_position(ask, lot_size, true)) {
            open_long(ask, lot_size);
        }
    }

    void check_new_short_entries(double bid, double ask) {
        if (positions_.size() >= static_cast<size_t>(config_.max_positions)) return;

        // Check net exposure - shorts help reduce net exposure when we have longs
        double net_after = current_long_lots_ - current_short_lots_ - config_.min_lot;
        // Allow shorts as long as they help hedge (don't go too short)
        if (net_after < -config_.max_net_exposure_lots) return;

        double lot_size = calculate_short_size(bid, ask);
        if (lot_size < config_.min_lot) return;

        bool should_enter = false;

        int short_count = 0;
        for (const auto& pos : positions_) {
            if (!pos.is_long) short_count++;
        }

        if (short_count == 0 && hedge_active_) {
            should_enter = true;
        }
        else if (highest_sell_ > 0 && bid >= highest_sell_ + config_.hedge_spacing) {
            should_enter = true;
        }
        else if (lowest_sell_ < 1e8 && bid <= lowest_sell_ - config_.hedge_spacing) {
            should_enter = true;
        }

        if (should_enter && can_open_position(bid, lot_size, false)) {
            open_short(bid, lot_size);
        }
    }

    double calculate_long_size(double price) {
        // Calculate current equity (mark-to-market)
        double equity = balance_;
        double volume_open = 0;
        for (const auto& pos : positions_) {
            if (pos.is_long) {
                equity += (price - config_.spread - pos.entry_price) * pos.lots * config_.contract_size;
                volume_open += pos.lots;
            } else {
                equity += (pos.entry_price - price) * pos.lots * config_.contract_size;
            }
        }

        // Calculate used margin
        double used_margin = 0;
        for (const auto& pos : positions_) {
            used_margin += pos.lots * config_.contract_size * pos.entry_price / config_.leverage;
        }

        // Use MT5-style margin stop out check
        const double margin_stop_out = 20.0;  // MT5 stop out level

        // Calculate target end price (survive_pct below highest or current)
        double reference_price = (current_long_lots_ > 0 && highest_buy_ > 0) ? highest_buy_ : price;
        double end_price = reference_price * ((100.0 - config_.survive_down_pct) / 100.0);

        // Distance to survive
        double distance = price - end_price;
        if (distance <= 0) return 0;

        // Number of potential trades in this distance
        double num_trades = std::floor(distance / config_.spacing);
        if (num_trades < 1) num_trades = 1;

        // Calculate equity at target (after all positions hit worst case)
        double equity_at_target = equity - volume_open * distance * config_.contract_size;

        // Check if we're already in trouble
        double target_margin_level = (used_margin > 0) ? (equity_at_target / used_margin * 100.0) : 10000.0;

        if (target_margin_level <= margin_stop_out) {
            return 0;  // Can't afford any new positions
        }

        // Calculate grid loss factor: lot * contract * spacing * (n*(n+1)/2)
        double grid_loss_factor = config_.spacing * config_.contract_size * (num_trades * (num_trades + 1) / 2);

        // Available equity for new grid
        double available = equity_at_target - (used_margin * margin_stop_out / 100.0);
        if (available <= 0) return 0;

        // Calculate lot size
        double lot_size = available / grid_loss_factor;
        lot_size = lot_size * config_.size_multiplier;
        lot_size = std::max(config_.min_lot, std::min(lot_size, config_.max_lot));
        lot_size = std::round(lot_size * 100) / 100;

        return lot_size;
    }

    double calculate_short_size(double bid, double ask) {
        // If hedge ratio is 0 or too small, don't hedge
        if (config_.hedge_ratio < 0.01 || current_long_lots_ < config_.min_lot) {
            return 0;
        }

        // Use fixed hedge ratio - don't increase with drop
        // The goal is to provide oscillation profit, not full hedge
        double target_short_lots = current_long_lots_ * config_.hedge_ratio;

        // Cap shorts to prevent over-hedging
        target_short_lots = std::min(target_short_lots, current_long_lots_ * 0.5);

        // Only open shorts if we're below target
        double needed = target_short_lots - current_short_lots_;
        if (needed < config_.min_lot) {
            return 0;
        }

        double base_size = std::min(needed, config_.max_lot);
        return normalize_lots(base_size);
    }

    double normalize_lots(double lots) {
        lots = std::max(config_.min_lot, std::min(lots, config_.max_lot));
        return std::floor(lots * 100) / 100.0;  // Round to 0.01
    }

    double get_used_margin(double bid, double ask) const {
        double margin = 0;
        for (const auto& pos : positions_) {
            double price = pos.is_long ? ask : bid;
            margin += pos.lots * config_.contract_size * price / config_.leverage;
        }
        return margin;
    }

    bool check_margin_stop_out(double bid, double ask) {
        if (positions_.empty()) return false;

        double used = get_used_margin(bid, ask);
        double equity = get_equity(bid, ask);
        double margin_level = (equity / used) * 100.0;

        // Track margin warnings (when margin drops below 100%)
        if (margin_level < 100.0) {
            result_.margin_warnings++;
        }

        if (margin_level < config_.stop_out_level) {
            result_.margin_call_occurred = true;
            while (!positions_.empty()) {
                close_position(0, bid, ask);
            }
            return true;
        }
        return false;
    }

    bool can_open_position(double price, double lots, bool is_long) {
        double equity = get_equity(price - config_.spread, price);
        double margin_needed = lots * config_.contract_size * price / config_.leverage;
        double free_margin = equity - get_used_margin(price - config_.spread, price);
        return margin_needed < free_margin * 0.7;  // 30% buffer
    }

    void open_long(double ask, double lots) {
        HFUPosition pos;
        pos.entry_price = ask;
        pos.lots = lots;
        pos.is_long = true;
        pos.take_profit = config_.use_take_profit ? ask + config_.spacing + config_.spread + config_.tp_buffer : 0;
        pos.entry_day = current_day_;

        positions_.push_back(pos);
        result_.long_entries++;
    }

    void open_short(double bid, double lots) {
        HFUPosition pos;
        pos.entry_price = bid;
        pos.lots = lots;
        pos.is_long = false;
        pos.take_profit = config_.use_take_profit ? bid - config_.hedge_spacing - config_.spread - config_.tp_buffer : 0;
        pos.entry_day = current_day_;

        positions_.push_back(pos);
        result_.short_entries++;
    }

    void close_position(int index, double bid, double ask) {
        if (index < 0 || index >= static_cast<int>(positions_.size())) return;

        auto& pos = positions_[index];
        double pnl;
        if (pos.is_long) {
            pnl = (bid - pos.entry_price) * pos.lots * config_.contract_size;
        } else {
            pnl = (pos.entry_price - ask) * pos.lots * config_.contract_size;
        }

        balance_ += pnl;
        positions_.erase(positions_.begin() + index);
    }

    void close_position_at_price(int index, double price, bool is_long) {
        if (index < 0 || index >= static_cast<int>(positions_.size())) return;

        auto& pos = positions_[index];
        double pnl;
        if (is_long) {
            pnl = (price - pos.entry_price) * pos.lots * config_.contract_size;
        } else {
            pnl = (pos.entry_price - price) * pos.lots * config_.contract_size;
        }

        balance_ += pnl;
        positions_.erase(positions_.begin() + index);
    }
};
