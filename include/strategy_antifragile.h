#pragma once
/**
 * Anti-Fragile Strategy
 *
 * Philosophy: Get STRONGER from stress, not weaker
 *
 * Mechanism:
 * - Small positions in calm markets
 * - Progressively larger positions as price drops (better prices)
 * - Drawdowns = loading the spring for future profit
 * - Quick profit-taking on bounces to lock in loaded gains
 *
 * Key insight: Crashes are OPPORTUNITIES, not disasters
 * The strategy is designed to profit MORE from volatile markets
 */

#include <vector>
#include <cmath>
#include <algorithm>
#include <deque>

struct AntifragileConfig {
    // Core parameters
    double base_lot_size = 0.01;     // Starting position size
    double max_lot_size = 1.0;       // Maximum position size
    double entry_spacing = 50.0;     // Distance between entries

    // Anti-fragile sizing
    double sizing_exponent = 1.5;    // How much to increase size on dips
    double max_position_count = 20;  // Max concurrent positions

    // Profit taking
    double take_profit_pct = 2.0;    // Take profit on average position
    double partial_take_pct = 0.5;   // Take 50% at first target

    // Risk management
    double max_equity_risk = 0.3;    // Max 30% equity at risk
    double stop_out_level = 50.0;    // Margin level stop out

    // Market parameters
    double leverage = 500.0;
    double contract_size = 1.0;
    double spread = 1.0;
};

struct AntifragilePosition {
    double entry_price;
    double lots;
    int stress_level;  // How many dips since ATH when entered (higher = more stressed)
};

struct AntifragileResult {
    double final_equity;
    double max_drawdown_pct;
    double max_equity;
    int total_entries;
    int total_exits;
    int profit_takes;
    double max_stress_level;
    bool margin_call_occurred;
};

class AntifragileStrategy {
private:
    AntifragileConfig config_;
    std::vector<AntifragilePosition> positions_;

    double balance_;
    double initial_balance_;
    double peak_equity_;
    double all_time_high_;

    double last_entry_price_;
    int current_stress_level_;

    AntifragileResult result_;

public:
    void configure(const AntifragileConfig& cfg) {
        config_ = cfg;
    }

    void reset(double starting_balance) {
        positions_.clear();
        balance_ = starting_balance;
        initial_balance_ = starting_balance;
        peak_equity_ = starting_balance;
        all_time_high_ = 0.0;
        last_entry_price_ = 0.0;
        current_stress_level_ = 0;
        result_ = AntifragileResult{};
        result_.max_equity = starting_balance;
    }

    void on_tick(double bid, double ask) {
        double mid = (bid + ask) / 2.0;

        // Track all-time high
        if (mid > all_time_high_) {
            all_time_high_ = mid;
            current_stress_level_ = 0;  // Reset stress at new ATH
        } else {
            // Calculate stress level based on drop from ATH
            double drop_pct = (all_time_high_ - mid) / all_time_high_ * 100.0;
            current_stress_level_ = static_cast<int>(drop_pct / 5.0);  // Every 5% drop = 1 stress level
        }

        if (current_stress_level_ > result_.max_stress_level) {
            result_.max_stress_level = current_stress_level_;
        }

        // Check margin
        if (check_margin_stop_out(bid, ask)) {
            return;
        }

        // Check profit taking
        check_profit_taking(bid, ask);

        // Check for new entries (anti-fragile: more at lower prices)
        check_new_entries(bid, ask);

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

    AntifragileResult get_result(double bid, double ask) {
        result_.final_equity = get_equity(bid, ask);
        return result_;
    }

private:
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

        // Calculate average entry and total lots
        double total_cost = 0.0;
        double total_lots = 0.0;
        for (const auto& pos : positions_) {
            total_cost += pos.entry_price * pos.lots;
            total_lots += pos.lots;
        }

        if (total_lots == 0) return;

        double avg_entry = total_cost / total_lots;
        double profit_pct = (bid - avg_entry) / avg_entry * 100.0;

        // Take profit when average position is profitable
        if (profit_pct >= config_.take_profit_pct) {
            // Close portion of positions (most profitable ones first)
            int to_close = static_cast<int>(positions_.size() * config_.partial_take_pct);
            if (to_close < 1) to_close = 1;

            // Sort by profit (descending) and close best ones
            std::vector<std::pair<double, int>> profits;
            for (size_t i = 0; i < positions_.size(); i++) {
                double pos_profit = bid - positions_[i].entry_price;
                profits.push_back({pos_profit, i});
            }
            std::sort(profits.begin(), profits.end(), std::greater<>());

            for (int i = 0; i < to_close && !positions_.empty(); i++) {
                // Find and close the most profitable position
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
        if (positions_.size() >= config_.max_position_count) return;

        // Entry condition: price dropped enough from last entry or ATH
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
            // Calculate anti-fragile position size
            // Higher stress = larger position (up to max)
            double stress_multiplier = std::pow(1.0 + current_stress_level_, config_.sizing_exponent);
            double lot_size = config_.base_lot_size * stress_multiplier;
            lot_size = std::min(lot_size, config_.max_lot_size);

            // Check if we have margin
            double equity = get_equity(bid, ask);
            double margin_needed = lot_size * config_.contract_size * ask / config_.leverage;
            double free_margin = equity - get_used_margin(bid, ask);

            if (margin_needed < free_margin * (1.0 - config_.max_equity_risk)) {
                open_position(ask, lot_size);
            }
        }
    }

    void open_position(double price, double lots) {
        AntifragilePosition pos;
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
