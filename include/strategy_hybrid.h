#pragma once
/**
 * Hybrid Strategy: Anti-Fragile + Crash Detection
 *
 * Philosophy: Load the spring during dips, but escape before crashes
 *
 * Combines:
 * 1. Anti-fragile sizing: Larger positions at lower prices (better entries)
 * 2. Crash detection: Exit when velocity indicates impending crash
 * 3. Re-entry logic: Re-enter after crash stabilizes
 *
 * Key insight: Not all dips are crashes. We want to buy dips but avoid crashes.
 * Velocity detection distinguishes between "healthy pullback" and "panic selling"
 */

#include <vector>
#include <deque>
#include <cmath>
#include <algorithm>

struct HybridConfig {
    // Anti-fragile sizing
    double base_lot_size = 0.01;
    double max_lot_size = 1.0;
    double entry_spacing = 50.0;
    double sizing_exponent = 1.5;
    int max_positions = 20;

    // Profit taking
    double take_profit_pct = 2.0;
    double partial_take_pct = 0.5;

    // Crash detection
    bool enable_crash_detection = true;
    double crash_velocity_threshold = -0.3;  // % drop per lookback
    int crash_lookback = 500;
    double crash_exit_pct = 0.5;             // Exit 50% on crash signal

    // Re-entry after crash
    int cooldown_after_crash = 1000;         // Ticks before re-entering
    double reentry_bounce_pct = 0.5;         // Need X% bounce to re-enter

    // Risk management
    double max_equity_risk = 0.3;
    double stop_out_level = 50.0;

    // Market parameters
    double leverage = 500.0;
    double contract_size = 1.0;
    double spread = 1.0;
};

struct HybridPosition {
    double entry_price;
    double lots;
    int stress_level;
};

struct HybridResult {
    double final_equity;
    double max_drawdown_pct;
    double max_equity;
    int total_entries;
    int total_exits;
    int profit_takes;
    int crash_exits;
    int reentries;
    double max_stress_level;
    bool margin_call_occurred;
};

class HybridStrategy {
private:
    HybridConfig config_;
    std::vector<HybridPosition> positions_;
    std::deque<double> price_history_;

    double balance_;
    double initial_balance_;
    double peak_equity_;
    double all_time_high_;

    double last_entry_price_;
    int current_stress_level_;

    // Crash state
    bool in_crash_mode_;
    int crash_cooldown_;
    double crash_low_;

    HybridResult result_;

public:
    void configure(const HybridConfig& cfg) {
        config_ = cfg;
    }

    void reset(double starting_balance) {
        positions_.clear();
        price_history_.clear();
        balance_ = starting_balance;
        initial_balance_ = starting_balance;
        peak_equity_ = starting_balance;
        all_time_high_ = 0.0;
        last_entry_price_ = 0.0;
        current_stress_level_ = 0;
        in_crash_mode_ = false;
        crash_cooldown_ = 0;
        crash_low_ = 0.0;
        result_ = HybridResult{};
        result_.max_equity = starting_balance;
    }

    void on_tick(double bid, double ask) {
        double mid = (bid + ask) / 2.0;

        // Update price history for velocity calculation
        price_history_.push_back(mid);
        if (price_history_.size() > static_cast<size_t>(config_.crash_lookback + 100)) {
            price_history_.pop_front();
        }

        // Track all-time high and stress level
        if (mid > all_time_high_) {
            all_time_high_ = mid;
            current_stress_level_ = 0;
        } else {
            double drop_pct = (all_time_high_ - mid) / all_time_high_ * 100.0;
            current_stress_level_ = static_cast<int>(drop_pct / 5.0);
        }

        if (current_stress_level_ > result_.max_stress_level) {
            result_.max_stress_level = current_stress_level_;
        }

        // Update crash state
        update_crash_state(mid);

        // Check margin
        if (check_margin_stop_out(bid, ask)) {
            return;
        }

        // Check crash exit
        if (config_.enable_crash_detection && !in_crash_mode_) {
            check_crash_exit(bid, ask);
        }

        // Check profit taking
        check_profit_taking(bid, ask);

        // Check for new entries (only if not in crash mode)
        if (!in_crash_mode_ || crash_cooldown_ <= 0) {
            check_new_entries(bid, ask);
        }

        // Decrement cooldown
        if (crash_cooldown_ > 0) {
            crash_cooldown_--;
        }

        // Update stats
        double equity = get_equity(bid, ask);
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

    double get_equity(double bid, double ask) const {
        double unrealized = 0.0;
        for (const auto& pos : positions_) {
            unrealized += (bid - pos.entry_price) * pos.lots * config_.contract_size;
        }
        return balance_ + unrealized;
    }

    HybridResult get_result(double bid, double ask) {
        result_.final_equity = get_equity(bid, ask);
        return result_;
    }

private:
    double calculate_velocity() {
        if (price_history_.size() < static_cast<size_t>(config_.crash_lookback)) {
            return 0.0;
        }

        size_t start_idx = price_history_.size() - config_.crash_lookback;
        double start_price = price_history_[start_idx];
        double end_price = price_history_.back();

        return (end_price - start_price) / start_price * 100.0;
    }

    void update_crash_state(double mid) {
        if (in_crash_mode_) {
            // Track crash low
            if (mid < crash_low_) {
                crash_low_ = mid;
            }

            // Check for bounce to exit crash mode
            double bounce_pct = (mid - crash_low_) / crash_low_ * 100.0;
            if (bounce_pct >= config_.reentry_bounce_pct) {
                in_crash_mode_ = false;
                result_.reentries++;
            }
        }
    }

    void check_crash_exit(double bid, double ask) {
        double velocity = calculate_velocity();

        if (velocity <= config_.crash_velocity_threshold) {
            // Crash detected! Exit portion of positions
            in_crash_mode_ = true;
            crash_cooldown_ = config_.cooldown_after_crash;
            crash_low_ = (bid + ask) / 2.0;

            int to_close = static_cast<int>(positions_.size() * config_.crash_exit_pct);
            if (to_close < 1 && !positions_.empty()) to_close = 1;

            // Close worst performing positions first
            for (int i = 0; i < to_close && !positions_.empty(); i++) {
                int worst_idx = -1;
                double worst_pnl = 1e9;

                for (size_t j = 0; j < positions_.size(); j++) {
                    double pnl = bid - positions_[j].entry_price;
                    if (pnl < worst_pnl) {
                        worst_pnl = pnl;
                        worst_idx = j;
                    }
                }

                if (worst_idx >= 0) {
                    close_position(worst_idx, bid);
                    result_.crash_exits++;
                }
            }
        }
    }

    double get_used_margin(double bid, double ask) const {
        double margin = 0.0;
        for (const auto& pos : positions_) {
            margin += pos.lots * config_.contract_size * ask / config_.leverage;
        }
        return margin;
    }

    bool check_margin_stop_out(double bid, double ask) {
        if (positions_.empty()) return false;

        double used = get_used_margin(bid, ask);
        double equity = get_equity(bid, ask);
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

    void check_profit_taking(double bid, double ask) {
        if (positions_.empty()) return;

        double total_cost = 0.0;
        double total_lots = 0.0;
        for (const auto& pos : positions_) {
            total_cost += pos.entry_price * pos.lots;
            total_lots += pos.lots;
        }

        if (total_lots == 0) return;

        double avg_entry = total_cost / total_lots;
        double profit_pct = (bid - avg_entry) / avg_entry * 100.0;

        if (profit_pct >= config_.take_profit_pct) {
            int to_close = static_cast<int>(positions_.size() * config_.partial_take_pct);
            if (to_close < 1) to_close = 1;

            for (int i = 0; i < to_close && !positions_.empty(); i++) {
                int best_idx = -1;
                double best_profit = -1e9;

                for (size_t j = 0; j < positions_.size(); j++) {
                    double p = bid - positions_[j].entry_price;
                    if (p > best_profit) {
                        best_profit = p;
                        best_idx = j;
                    }
                }

                if (best_idx >= 0) {
                    close_position(best_idx, bid);
                    result_.profit_takes++;
                }
            }
        }
    }

    void check_new_entries(double bid, double ask) {
        if (positions_.size() >= static_cast<size_t>(config_.max_positions)) return;

        double entry_ref = last_entry_price_ > 0 ? last_entry_price_ : all_time_high_;
        if (entry_ref == 0) {
            entry_ref = ask;
            all_time_high_ = ask;
        }

        bool should_enter = false;

        // Enter at new ATH (small position)
        if (ask >= all_time_high_ && positions_.empty()) {
            should_enter = true;
        }
        // Enter when price drops by spacing (anti-fragile: larger positions)
        else if (ask <= entry_ref - config_.entry_spacing) {
            should_enter = true;
        }

        if (should_enter) {
            // Anti-fragile sizing: higher stress = larger position
            double stress_multiplier = std::pow(1.0 + current_stress_level_, config_.sizing_exponent);
            double lot_size = config_.base_lot_size * stress_multiplier;
            lot_size = std::min(lot_size, config_.max_lot_size);

            double equity = get_equity(bid, ask);
            double margin_needed = lot_size * config_.contract_size * ask / config_.leverage;
            double free_margin = equity - get_used_margin(bid, ask);

            if (margin_needed < free_margin * (1.0 - config_.max_equity_risk)) {
                open_position(ask, lot_size);
            }
        }
    }

    void open_position(double price, double lots) {
        HybridPosition pos;
        pos.entry_price = price;
        pos.lots = lots;
        pos.stress_level = current_stress_level_;

        positions_.push_back(pos);
        last_entry_price_ = price;
        result_.total_entries++;
    }

    void close_position(int index, double bid) {
        if (index < 0 || index >= (int)positions_.size()) return;

        auto& pos = positions_[index];
        double pnl = (bid - pos.entry_price) * pos.lots * config_.contract_size;

        balance_ += pnl;
        positions_.erase(positions_.begin() + index);
        result_.total_exits++;
    }
};
