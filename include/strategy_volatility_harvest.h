#pragma once
/**
 * Volatility Harvesting Strategy
 *
 * Philosophy: Profit from price MOVEMENT, not direction
 *
 * Mechanism:
 * - Buy on dips (short-term price drops)
 * - Quick take-profit targets
 * - Small positions, high frequency
 * - Collect "oscillation premium"
 *
 * Key insight: Markets oscillate constantly. We harvest the micro-movements.
 * Like a boat bobbing on waves - we profit from the motion itself.
 */

#include <vector>
#include <deque>
#include <cmath>
#include <algorithm>

struct VolatilityHarvestConfig {
    // Position parameters
    double lot_size = 0.1;
    int max_positions = 10;

    // Entry parameters
    int lookback = 100;           // Ticks to measure movement
    double entry_drop_pct = 0.1;  // Enter when price drops this % in lookback
    double min_spacing = 20.0;    // Min distance between entries

    // Exit parameters
    double take_profit = 20.0;    // Quick TP
    double stop_loss = 50.0;      // Wider SL to survive volatility

    // Risk management
    double stop_out_level = 50.0;

    // Market parameters
    double leverage = 500.0;
    double contract_size = 1.0;
    double spread = 1.0;
};

struct VolatilityPosition {
    double entry_price;
    double lots;
    double take_profit;
    double stop_loss;
};

struct VolatilityHarvestResult {
    double final_equity;
    double max_drawdown_pct;
    double max_equity;
    int total_trades;
    int tp_hits;
    int sl_hits;
    double avg_trade_duration;
    bool margin_call_occurred;
};

class VolatilityHarvestStrategy {
private:
    VolatilityHarvestConfig config_;
    std::vector<VolatilityPosition> positions_;
    std::deque<double> price_history_;

    double balance_;
    double initial_balance_;
    double peak_equity_;

    double last_entry_price_;
    long total_duration_;
    int closed_trades_;

    VolatilityHarvestResult result_;

public:
    void configure(const VolatilityHarvestConfig& cfg) {
        config_ = cfg;
    }

    void reset(double starting_balance) {
        positions_.clear();
        price_history_.clear();
        balance_ = starting_balance;
        initial_balance_ = starting_balance;
        peak_equity_ = starting_balance;
        last_entry_price_ = 0.0;
        total_duration_ = 0;
        closed_trades_ = 0;
        result_ = VolatilityHarvestResult{};
        result_.max_equity = starting_balance;
    }

    void on_tick(double bid, double ask) {
        double mid = (bid + ask) / 2.0;

        // Update price history
        price_history_.push_back(mid);
        if (price_history_.size() > static_cast<size_t>(config_.lookback)) {
            price_history_.pop_front();
        }

        // Check margin
        if (check_margin_stop_out(bid)) {
            return;
        }

        // Check exits (TP and SL)
        check_exits(bid);

        // Check for new entries
        check_new_entries(bid, ask);

        // Update stats
        double equity = get_equity(bid);
        if (equity > result_.max_equity) {
            result_.max_equity = equity;
        }
        if (equity > peak_equity_) {
            peak_equity_ = equity;
        }

        if (peak_equity_ > 0) {
            double dd = 100.0 * (peak_equity_ - equity) / peak_equity_;
            if (dd > result_.max_drawdown_pct) {
                result_.max_drawdown_pct = dd;
            }
        }
    }

    double get_equity(double bid) const {
        double unrealized = 0.0;
        for (const auto& pos : positions_) {
            unrealized += (bid - pos.entry_price) * pos.lots * config_.contract_size;
        }
        return balance_ + unrealized;
    }

    VolatilityHarvestResult get_result(double bid) {
        result_.final_equity = get_equity(bid);
        if (closed_trades_ > 0) {
            result_.avg_trade_duration = static_cast<double>(total_duration_) / closed_trades_;
        }
        return result_;
    }

private:
    double get_used_margin(double ask) const {
        double margin = 0.0;
        for (const auto& pos : positions_) {
            margin += pos.lots * config_.contract_size * ask / config_.leverage;
        }
        return margin;
    }

    bool check_margin_stop_out(double bid) {
        if (positions_.empty()) return false;

        double used = get_used_margin(bid + config_.spread);
        double equity = get_equity(bid);
        double margin_level = (equity / used) * 100.0;

        if (margin_level < config_.stop_out_level) {
            result_.margin_call_occurred = true;
            while (!positions_.empty()) {
                close_position(0, bid);
            }
            return true;
        }
        return false;
    }

    void check_exits(double bid) {
        std::vector<int> to_close;
        std::vector<bool> is_tp;

        for (size_t i = 0; i < positions_.size(); i++) {
            const auto& pos = positions_[i];

            if (bid >= pos.take_profit) {
                to_close.push_back(i);
                is_tp.push_back(true);
            } else if (bid <= pos.stop_loss) {
                to_close.push_back(i);
                is_tp.push_back(false);
            }
        }

        // Close in reverse order
        for (int i = to_close.size() - 1; i >= 0; i--) {
            close_position(to_close[i], bid);
            if (is_tp[i]) {
                result_.tp_hits++;
            } else {
                result_.sl_hits++;
            }
        }
    }

    void check_new_entries(double bid, double ask) {
        if (positions_.size() >= static_cast<size_t>(config_.max_positions)) return;
        if (price_history_.size() < static_cast<size_t>(config_.lookback)) return;

        // Check if we're spaced enough from last entry
        if (last_entry_price_ > 0) {
            double dist = std::abs(ask - last_entry_price_);
            if (dist < config_.min_spacing) return;
        }

        // Calculate short-term price movement
        double prev_price = price_history_.front();
        double curr_price = price_history_.back();
        double pct_change = (curr_price - prev_price) / prev_price * 100.0;

        // Enter on dip (negative movement)
        if (pct_change <= -config_.entry_drop_pct) {
            if (can_open_position(ask)) {
                open_position(ask);
            }
        }
    }

    bool can_open_position(double ask) {
        double equity = get_equity(ask - config_.spread);
        double margin_needed = config_.lot_size * config_.contract_size * ask / config_.leverage;
        double free_margin = equity - get_used_margin(ask);
        return margin_needed < free_margin * 0.8;
    }

    void open_position(double ask) {
        VolatilityPosition pos;
        pos.entry_price = ask;
        pos.lots = config_.lot_size;
        pos.take_profit = ask + config_.take_profit;
        pos.stop_loss = ask - config_.stop_loss;

        positions_.push_back(pos);
        last_entry_price_ = ask;
        result_.total_trades++;
    }

    void close_position(int index, double bid) {
        if (index < 0 || index >= (int)positions_.size()) return;

        auto& pos = positions_[index];
        double pnl = (bid - pos.entry_price) * pos.lots * config_.contract_size;

        balance_ += pnl;
        positions_.erase(positions_.begin() + index);
        closed_trades_++;
    }
};
