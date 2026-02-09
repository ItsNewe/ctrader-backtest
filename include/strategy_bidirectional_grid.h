#pragma once
/**
 * Bidirectional Grid Strategy
 *
 * Philosophy: Profit from movement in EITHER direction
 *
 * Mechanism:
 * - Maintain grid of BUY orders below current price
 * - Maintain grid of SELL orders above current price
 * - When price oscillates, both sides generate profit
 * - Net exposure stays relatively balanced
 *
 * Key insight: We don't care if price goes up or down,
 * we care that it MOVES and then RETURNS (oscillates)
 */

#include <vector>
#include <cmath>
#include <algorithm>

struct BiGridConfig {
    double grid_spacing = 50.0;      // Distance between grid levels
    double lot_size = 0.1;           // Fixed lot size per level
    int max_levels_per_side = 10;    // Max buy levels + max sell levels
    double take_profit = 50.0;       // TP distance (usually = spacing)
    double leverage = 500.0;
    double contract_size = 1.0;
    double spread = 1.0;
    double stop_out_level = 50.0;

    // Risk management
    double max_exposure_ratio = 0.5; // Max (longs-shorts)/total as ratio
    bool enable_rebalancing = true;  // Auto-close to rebalance
};

struct BiGridPosition {
    double entry_price;
    double lots;
    bool is_long;  // true = BUY, false = SELL
    double take_profit;
};

struct BiGridResult {
    double final_equity;
    double max_drawdown_pct;
    double max_equity;
    int total_trades;
    int long_trades;
    int short_trades;
    int tp_hits;
    double total_long_lots;
    double total_short_lots;
    bool margin_call_occurred;
};

class BidirectionalGrid {
private:
    BiGridConfig config_;
    std::vector<BiGridPosition> positions_;

    double balance_;
    double initial_balance_;
    double peak_equity_;

    // Grid tracking
    double grid_anchor_;      // Reference price for grid
    double highest_long_;     // Highest long entry
    double lowest_long_;      // Lowest long entry
    double highest_short_;    // Highest short entry
    double lowest_short_;     // Lowest short entry

    BiGridResult result_;

public:
    void configure(const BiGridConfig& cfg) {
        config_ = cfg;
    }

    void reset(double starting_balance) {
        positions_.clear();
        balance_ = starting_balance;
        initial_balance_ = starting_balance;
        peak_equity_ = starting_balance;
        grid_anchor_ = 0.0;
        highest_long_ = 0.0;
        lowest_long_ = 1e9;
        highest_short_ = 0.0;
        lowest_short_ = 1e9;
        result_ = BiGridResult{};
        result_.max_equity = starting_balance;
    }

    void on_tick(double bid, double ask) {
        double mid = (bid + ask) / 2.0;

        // Initialize grid anchor on first tick
        if (grid_anchor_ == 0.0) {
            grid_anchor_ = mid;
        }

        // Check margin
        if (check_margin_stop_out(bid, ask)) {
            return;
        }

        // Check take profits
        check_take_profits(bid, ask);

        // Check for new grid entries
        check_new_entries(bid, ask);

        // Optional: rebalance if too skewed
        if (config_.enable_rebalancing) {
            check_rebalance(bid, ask);
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
            if (pos.is_long) {
                unrealized += (bid - pos.entry_price) * pos.lots * config_.contract_size;
            } else {
                unrealized += (pos.entry_price - ask) * pos.lots * config_.contract_size;
            }
        }
        return balance_ + unrealized;
    }

    BiGridResult get_result(double bid, double ask) {
        result_.final_equity = get_equity(bid, ask);

        // Count current exposure
        for (const auto& pos : positions_) {
            if (pos.is_long) {
                result_.total_long_lots += pos.lots;
            } else {
                result_.total_short_lots += pos.lots;
            }
        }

        return result_;
    }

private:
    double get_used_margin(double bid, double ask) const {
        double margin = 0.0;
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

        if (margin_level < config_.stop_out_level) {
            result_.margin_call_occurred = true;
            while (!positions_.empty()) {
                close_position(0, bid, ask);
            }
            return true;
        }
        return false;
    }

    void check_take_profits(double bid, double ask) {
        std::vector<int> to_close;

        for (size_t i = 0; i < positions_.size(); i++) {
            const auto& pos = positions_[i];

            if (pos.is_long && bid >= pos.take_profit) {
                to_close.push_back(i);
            } else if (!pos.is_long && ask <= pos.take_profit) {
                to_close.push_back(i);
            }
        }

        // Close in reverse order
        for (int i = to_close.size() - 1; i >= 0; i--) {
            close_position(to_close[i], bid, ask);
            result_.tp_hits++;
        }
    }

    void check_new_entries(double bid, double ask) {
        int long_count = 0, short_count = 0;
        for (const auto& pos : positions_) {
            if (pos.is_long) long_count++;
            else short_count++;
        }

        // Check for new LONG entry (price dropped to new grid level below)
        if (long_count < config_.max_levels_per_side) {
            double next_long_level = grid_anchor_ - config_.grid_spacing;
            if (lowest_long_ < 1e8) {
                next_long_level = lowest_long_ - config_.grid_spacing;
            }

            if (ask <= next_long_level) {
                if (can_open_position(ask)) {
                    open_long(ask);
                }
            }
        }

        // Check for new SHORT entry (price rose to new grid level above)
        if (short_count < config_.max_levels_per_side) {
            double next_short_level = grid_anchor_ + config_.grid_spacing;
            if (highest_short_ > 0) {
                next_short_level = highest_short_ + config_.grid_spacing;
            }

            if (bid >= next_short_level) {
                if (can_open_position(bid)) {
                    open_short(bid);
                }
            }
        }
    }

    bool can_open_position(double price) {
        double equity = get_equity(price - config_.spread, price);
        double margin_needed = config_.lot_size * config_.contract_size * price / config_.leverage;
        double free_margin = equity - get_used_margin(price - config_.spread, price);
        return margin_needed < free_margin * 0.8;
    }

    void open_long(double ask) {
        BiGridPosition pos;
        pos.entry_price = ask;
        pos.lots = config_.lot_size;
        pos.is_long = true;
        pos.take_profit = ask + config_.take_profit;

        positions_.push_back(pos);
        lowest_long_ = std::min(lowest_long_, ask);
        highest_long_ = std::max(highest_long_, ask);

        result_.total_trades++;
        result_.long_trades++;
    }

    void open_short(double bid) {
        BiGridPosition pos;
        pos.entry_price = bid;
        pos.lots = config_.lot_size;
        pos.is_long = false;
        pos.take_profit = bid - config_.take_profit;

        positions_.push_back(pos);
        lowest_short_ = std::min(lowest_short_, bid);
        highest_short_ = std::max(highest_short_, bid);

        result_.total_trades++;
        result_.short_trades++;
    }

    void close_position(int index, double bid, double ask) {
        if (index < 0 || index >= (int)positions_.size()) return;

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

    void check_rebalance(double bid, double ask) {
        double long_lots = 0, short_lots = 0;
        for (const auto& pos : positions_) {
            if (pos.is_long) long_lots += pos.lots;
            else short_lots += pos.lots;
        }

        double total = long_lots + short_lots;
        if (total == 0) return;

        double exposure_ratio = std::abs(long_lots - short_lots) / total;

        // If too skewed, close some of the larger side
        if (exposure_ratio > config_.max_exposure_ratio) {
            bool close_longs = long_lots > short_lots;

            for (size_t i = 0; i < positions_.size(); i++) {
                if (positions_[i].is_long == close_longs) {
                    close_position(i, bid, ask);
                    break;  // Close one at a time
                }
            }
        }
    }
};
