#pragma once
/**
 * Hybrid + Fill-Up Combined Strategy
 *
 * - Hybrid (Anti-Fragile) during uptrends: larger positions on dips
 * - Fill-Up (Grid with TP) during consolidation/crash: profit from oscillations
 */

#include <vector>
#include <deque>
#include <cmath>
#include <algorithm>
#include <string>

enum class StrategyMode {
    HYBRID,   // Anti-fragile uptrend mode
    FILLUP    // Grid mode during consolidation
};

struct HybridFillUpConfig {
    // General
    int max_positions = 20;
    double max_equity_risk = 0.3;
    double max_lot_size = 1.0;

    // Hybrid mode (uptrends)
    double hybrid_base_lot = 0.02;
    double hybrid_spacing = 5.0;
    double hybrid_exponent = 1.5;
    double hybrid_tp_pct = 2.0;
    double hybrid_partial_pct = 0.5;

    // Crash detection
    bool enable_crash_detection = true;
    double crash_velocity = -0.4;
    int crash_lookback = 500;
    double crash_exit_pct = 0.5;
    int cooldown_after_crash = 1000;

    // Fill-Up mode (consolidation)
    double fillup_survive_down = 2.5;
    double fillup_spacing = 5.0;
    double fillup_size_mult = 1.0;
    bool fillup_use_tp = true;

    // Mode switching
    int trend_lookback = 2000;
    double uptrend_threshold = 0.5;
    int consolidation_ticks = 5000;

    // Market
    double leverage = 500.0;
    double contract_size = 100.0;  // Gold
    double spread = 0.25;
    double stop_out_level = 50.0;
};

struct HFPosition {
    double entry_price;
    double lots;
    double take_profit;  // 0 = Hybrid position, >0 = FillUp position
    bool is_hybrid;
};

struct HybridFillUpResult {
    double final_equity;
    double max_drawdown_pct;
    double max_equity;
    int hybrid_entries;
    int fillup_entries;
    int crash_exits;
    int profit_takes;
    int tp_hits;
    int mode_changes;
    double time_in_hybrid_pct;
    double time_in_fillup_pct;
    bool margin_call_occurred;
};

class HybridFillUpStrategy {
private:
    HybridFillUpConfig config_;
    std::vector<HFPosition> positions_;
    std::deque<double> price_history_;

    double balance_;
    double initial_balance_;
    double peak_equity_;

    // State
    StrategyMode current_mode_;
    double all_time_high_;
    double last_entry_price_;
    int current_stress_level_;
    int ticks_since_ath_;

    // Crash state
    bool in_crash_mode_;
    int crash_cooldown_;
    double crash_low_;

    // FillUp tracking
    double lowest_buy_;
    double highest_buy_;

    // Stats
    long ticks_in_hybrid_;
    long ticks_in_fillup_;
    long total_ticks_;

    HybridFillUpResult result_;

public:
    void configure(const HybridFillUpConfig& cfg) {
        config_ = cfg;
    }

    void reset(double starting_balance) {
        positions_.clear();
        price_history_.clear();
        balance_ = starting_balance;
        initial_balance_ = starting_balance;
        peak_equity_ = starting_balance;

        current_mode_ = StrategyMode::HYBRID;
        all_time_high_ = 0;
        last_entry_price_ = 0;
        current_stress_level_ = 0;
        ticks_since_ath_ = 0;

        in_crash_mode_ = false;
        crash_cooldown_ = 0;
        crash_low_ = 0;

        lowest_buy_ = 1e9;
        highest_buy_ = 0;

        ticks_in_hybrid_ = 0;
        ticks_in_fillup_ = 0;
        total_ticks_ = 0;

        result_ = HybridFillUpResult{};
        result_.max_equity = starting_balance;
    }

    void on_tick(double bid, double ask) {
        double mid = (bid + ask) / 2.0;
        total_ticks_++;

        // Update price history
        price_history_.push_back(mid);
        int max_lookback = std::max(config_.crash_lookback, config_.trend_lookback) + 100;
        if (price_history_.size() > static_cast<size_t>(max_lookback)) {
            price_history_.pop_front();
        }

        // Track ATH and stress
        if (mid > all_time_high_ || all_time_high_ == 0) {
            all_time_high_ = mid;
            current_stress_level_ = 0;
            ticks_since_ath_ = 0;
        } else {
            double drop_pct = (all_time_high_ - mid) / all_time_high_ * 100.0;
            current_stress_level_ = static_cast<int>(drop_pct / 5.0);
            ticks_since_ath_++;
        }

        // Check margin
        if (check_margin_stop_out(bid, ask)) {
            return;
        }

        // Check TP hits for FillUp positions
        check_tp_hits(bid);

        // Crash detection
        if (config_.enable_crash_detection) {
            update_crash_state(mid);
            if (!in_crash_mode_) {
                check_crash_exit(bid, ask);
            }
            if (crash_cooldown_ > 0) {
                crash_cooldown_--;
            }
        }

        // Determine mode
        determine_mode(mid);

        // Track time in each mode
        if (current_mode_ == StrategyMode::HYBRID) {
            ticks_in_hybrid_++;
        } else {
            ticks_in_fillup_++;
        }

        // Execute based on mode
        if (current_mode_ == StrategyMode::HYBRID) {
            execute_hybrid_mode(bid, ask);
        } else {
            execute_fillup_mode(bid, ask);
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
        double unrealized = 0;
        for (const auto& pos : positions_) {
            unrealized += (bid - pos.entry_price) * pos.lots * config_.contract_size;
        }
        return balance_ + unrealized;
    }

    HybridFillUpResult get_result(double bid, double ask) {
        result_.final_equity = get_equity(bid, ask);
        if (total_ticks_ > 0) {
            result_.time_in_hybrid_pct = 100.0 * ticks_in_hybrid_ / total_ticks_;
            result_.time_in_fillup_pct = 100.0 * ticks_in_fillup_ / total_ticks_;
        }
        return result_;
    }

private:
    double calculate_velocity(int lookback) {
        if (price_history_.size() < static_cast<size_t>(lookback)) {
            return 0.0;
        }
        double start_price = price_history_[price_history_.size() - lookback];
        double end_price = price_history_.back();
        if (start_price == 0) return 0.0;
        return (end_price - start_price) / start_price * 100.0;
    }

    void update_crash_state(double mid) {
        if (in_crash_mode_) {
            if (mid < crash_low_) crash_low_ = mid;
            double bounce = (mid - crash_low_) / crash_low_ * 100.0;
            if (bounce >= 0.5) {
                in_crash_mode_ = false;
            }
        }
    }

    void check_crash_exit(double bid, double ask) {
        double velocity = calculate_velocity(config_.crash_lookback);

        if (velocity <= config_.crash_velocity) {
            in_crash_mode_ = true;
            crash_cooldown_ = config_.cooldown_after_crash;
            crash_low_ = bid;

            int pos_count = positions_.size();
            int to_close = static_cast<int>(pos_count * config_.crash_exit_pct);
            if (to_close < 1 && pos_count > 0) to_close = 1;

            for (int i = 0; i < to_close && !positions_.empty(); i++) {
                close_worst_position(bid, ask);
                result_.crash_exits++;
            }
        }
    }

    void determine_mode(double mid) {
        StrategyMode new_mode = current_mode_;

        if (in_crash_mode_ || crash_cooldown_ > 0) {
            new_mode = StrategyMode::FILLUP;
        }
        else if (ticks_since_ath_ > config_.consolidation_ticks) {
            new_mode = StrategyMode::FILLUP;
        }
        else {
            double trend = calculate_velocity(config_.trend_lookback);
            if (trend >= config_.uptrend_threshold) {
                new_mode = StrategyMode::HYBRID;
            } else if (trend < -config_.uptrend_threshold) {
                new_mode = StrategyMode::FILLUP;
            }
        }

        if (new_mode != current_mode_) {
            current_mode_ = new_mode;
            result_.mode_changes++;
        }
    }

    void execute_hybrid_mode(double bid, double ask) {
        // Check profit taking
        check_hybrid_profit_taking(bid, ask);

        // Check entries
        if (positions_.size() >= static_cast<size_t>(config_.max_positions)) return;

        double entry_ref = (last_entry_price_ > 0) ? last_entry_price_ : all_time_high_;
        if (entry_ref == 0) {
            entry_ref = ask;
            all_time_high_ = ask;
        }

        bool should_enter = false;

        // Enter at ATH with no positions
        if (ask >= all_time_high_ && positions_.empty()) {
            should_enter = true;
        }
        // Enter on dip
        else if (ask <= entry_ref - config_.hybrid_spacing) {
            should_enter = true;
        }

        if (should_enter) {
            double stress_mult = std::pow(1.0 + current_stress_level_, config_.hybrid_exponent);
            double lot_size = config_.hybrid_base_lot * stress_mult;
            lot_size = std::min(lot_size, config_.max_lot_size);

            if (can_open_position(ask, lot_size)) {
                open_position(ask, lot_size, 0, true);  // TP=0 for hybrid
                last_entry_price_ = ask;
                result_.hybrid_entries++;
            }
        }
    }

    void execute_fillup_mode(double bid, double ask) {
        if (positions_.size() >= static_cast<size_t>(config_.max_positions)) return;

        double lot_size = calculate_fillup_size(ask);
        if (lot_size <= 0) return;

        bool should_enter = false;

        if (positions_.empty()) {
            should_enter = true;
        }
        else if (lowest_buy_ < 1e8 && ask <= lowest_buy_ - config_.fillup_spacing) {
            should_enter = true;
        }
        else if (highest_buy_ > 0 && ask >= highest_buy_ + config_.fillup_spacing) {
            should_enter = true;
        }

        if (should_enter) {
            double tp = 0;
            if (config_.fillup_use_tp) {
                tp = ask + config_.fillup_spacing + config_.spread;
            }

            if (can_open_position(ask, lot_size)) {
                open_position(ask, lot_size, tp, false);
                lowest_buy_ = std::min(lowest_buy_, ask);
                highest_buy_ = std::max(highest_buy_, ask);
                result_.fillup_entries++;
            }
        }
    }

    double calculate_fillup_size(double current_price) {
        double equity = balance_;
        for (const auto& pos : positions_) {
            equity += (current_price - config_.spread - pos.entry_price) * pos.lots * config_.contract_size;
        }

        double ref_price = (highest_buy_ > 0) ? highest_buy_ : current_price;
        double end_price = ref_price * (100 - config_.fillup_survive_down) / 100;
        double distance = current_price - end_price;
        double num_trades = std::floor(distance / config_.fillup_spacing);
        if (num_trades < 1) num_trades = 1;

        double available = equity * (1 - config_.max_equity_risk);
        double per_trade = available / (num_trades * 2);

        double lot_size = (per_trade * config_.leverage) / (config_.contract_size * current_price);
        lot_size = lot_size * config_.fillup_size_mult;
        lot_size = std::max(0.01, std::min(lot_size, config_.max_lot_size));

        return lot_size;
    }

    void check_hybrid_profit_taking(double bid, double ask) {
        double total_cost = 0, total_lots = 0;

        for (const auto& pos : positions_) {
            if (pos.is_hybrid) {
                total_cost += pos.entry_price * pos.lots;
                total_lots += pos.lots;
            }
        }

        if (total_lots == 0) return;

        double avg_entry = total_cost / total_lots;
        double profit_pct = (bid - avg_entry) / avg_entry * 100.0;

        if (profit_pct >= config_.hybrid_tp_pct) {
            int hybrid_count = 0;
            for (const auto& pos : positions_) {
                if (pos.is_hybrid) hybrid_count++;
            }

            int to_close = static_cast<int>(hybrid_count * config_.hybrid_partial_pct);
            if (to_close < 1) to_close = 1;

            for (int i = 0; i < to_close; i++) {
                close_best_hybrid_position(bid, ask);
                result_.profit_takes++;
            }
        }
    }

    void check_tp_hits(double bid) {
        std::vector<int> to_close;

        for (size_t i = 0; i < positions_.size(); i++) {
            if (!positions_[i].is_hybrid && positions_[i].take_profit > 0) {
                if (bid >= positions_[i].take_profit) {
                    to_close.push_back(i);
                }
            }
        }

        // Close in reverse order
        for (int i = to_close.size() - 1; i >= 0; i--) {
            close_position_at_tp(to_close[i], positions_[to_close[i]].take_profit);
            result_.tp_hits++;
        }
    }

    double get_used_margin(double ask) const {
        double margin = 0;
        for (const auto& pos : positions_) {
            margin += pos.lots * config_.contract_size * ask / config_.leverage;
        }
        return margin;
    }

    bool check_margin_stop_out(double bid, double ask) {
        if (positions_.empty()) return false;

        double used = get_used_margin(ask);
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

    bool can_open_position(double price, double lots) {
        double equity = get_equity(price - config_.spread, price);
        double margin_needed = lots * config_.contract_size * price / config_.leverage;
        double free_margin = equity - get_used_margin(price);
        return margin_needed < free_margin * (1.0 - config_.max_equity_risk);
    }

    void open_position(double price, double lots, double tp, bool is_hybrid) {
        HFPosition pos;
        pos.entry_price = price;
        pos.lots = lots;
        pos.take_profit = tp;
        pos.is_hybrid = is_hybrid;
        positions_.push_back(pos);
    }

    void close_position(int index, double bid) {
        if (index < 0 || index >= static_cast<int>(positions_.size())) return;

        auto& pos = positions_[index];
        double pnl = (bid - pos.entry_price) * pos.lots * config_.contract_size;
        balance_ += pnl;

        // Update tracking
        if (pos.entry_price == lowest_buy_) {
            lowest_buy_ = 1e9;
            for (const auto& p : positions_) {
                if (&p != &pos) lowest_buy_ = std::min(lowest_buy_, p.entry_price);
            }
        }
        if (pos.entry_price == highest_buy_) {
            highest_buy_ = 0;
            for (const auto& p : positions_) {
                if (&p != &pos) highest_buy_ = std::max(highest_buy_, p.entry_price);
            }
        }

        positions_.erase(positions_.begin() + index);
    }

    void close_position_at_tp(int index, double tp_price) {
        if (index < 0 || index >= static_cast<int>(positions_.size())) return;

        auto& pos = positions_[index];
        double pnl = (tp_price - pos.entry_price) * pos.lots * config_.contract_size;
        balance_ += pnl;

        positions_.erase(positions_.begin() + index);
    }

    void close_worst_position(double bid, double ask) {
        int worst_idx = -1;
        double worst_pnl = 1e9;

        for (size_t i = 0; i < positions_.size(); i++) {
            double pnl = bid - positions_[i].entry_price;
            if (pnl < worst_pnl) {
                worst_pnl = pnl;
                worst_idx = i;
            }
        }

        if (worst_idx >= 0) {
            close_position(worst_idx, bid);
        }
    }

    void close_best_hybrid_position(double bid, double ask) {
        int best_idx = -1;
        double best_pnl = -1e9;

        for (size_t i = 0; i < positions_.size(); i++) {
            if (positions_[i].is_hybrid) {
                double pnl = bid - positions_[i].entry_price;
                if (pnl > best_pnl) {
                    best_pnl = pnl;
                    best_idx = i;
                }
            }
        }

        if (best_idx >= 0) {
            close_position(best_idx, bid);
        }
    }
};
