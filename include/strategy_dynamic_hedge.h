#pragma once
/**
 * Dynamic Hedging Strategy
 *
 * Philosophy: Always have skin in the game BOTH ways
 *
 * Mechanism:
 * - Always maintain both long AND short positions
 * - Adjust ratio based on price vs moving average
 * - Above MA: slightly more short, Below MA: slightly more long
 * - Net exposure near zero, profit from spread between longs/shorts
 *
 * Key insight: We profit from the DIFFERENCE in movement, not the direction
 * If we're long at 100 and short at 110, we make $10 regardless of direction
 */

#include <vector>
#include <deque>
#include <cmath>
#include <algorithm>

struct DynamicHedgeConfig {
    // Position parameters
    double base_lot_size = 0.1;
    double max_total_lots = 2.0;

    // Hedging parameters
    double hedge_spacing = 50.0;      // Open hedge when price moves this much
    double target_spread = 100.0;     // Target spread between longs/shorts
    int ma_period = 500;              // Moving average period

    // Take profit
    double tp_spread = 50.0;          // Close pair when spread profit is this much

    // Risk management
    double max_net_exposure = 0.5;    // Max |longs - shorts| / total
    double stop_out_level = 50.0;

    // Market parameters
    double leverage = 500.0;
    double contract_size = 1.0;
    double spread = 1.0;
};

struct HedgePosition {
    double entry_price;
    double lots;
    bool is_long;
    int pair_id;  // ID to match long/short pairs
};

struct DynamicHedgeResult {
    double final_equity;
    double max_drawdown_pct;
    double max_equity;
    int total_trades;
    int long_trades;
    int short_trades;
    int pairs_closed;
    double max_net_exposure;
    bool margin_call_occurred;
};

class DynamicHedgeStrategy {
private:
    DynamicHedgeConfig config_;
    std::vector<HedgePosition> positions_;
    std::deque<double> price_history_;

    double balance_;
    double initial_balance_;
    double peak_equity_;
    double moving_average_;

    double last_long_price_;
    double last_short_price_;
    int next_pair_id_;

    DynamicHedgeResult result_;

public:
    void configure(const DynamicHedgeConfig& cfg) {
        config_ = cfg;
    }

    void reset(double starting_balance) {
        positions_.clear();
        price_history_.clear();
        balance_ = starting_balance;
        initial_balance_ = starting_balance;
        peak_equity_ = starting_balance;
        moving_average_ = 0.0;
        last_long_price_ = 0.0;
        last_short_price_ = 0.0;
        next_pair_id_ = 1;
        result_ = DynamicHedgeResult{};
        result_.max_equity = starting_balance;
    }

    void on_tick(double bid, double ask) {
        double mid = (bid + ask) / 2.0;

        // Update moving average
        price_history_.push_back(mid);
        if (price_history_.size() > static_cast<size_t>(config_.ma_period)) {
            price_history_.pop_front();
        }

        if (price_history_.size() >= 10) {
            double sum = 0;
            for (double p : price_history_) sum += p;
            moving_average_ = sum / price_history_.size();
        } else {
            moving_average_ = mid;
        }

        // Check margin
        if (check_margin_stop_out(bid, ask)) {
            return;
        }

        // Check for profitable pair closes
        check_pair_profit(bid, ask);

        // Check for new hedges
        check_new_hedges(bid, ask);

        // Track exposure
        track_exposure();

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

    DynamicHedgeResult get_result(double bid, double ask) {
        result_.final_equity = get_equity(bid, ask);
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

    void check_pair_profit(double bid, double ask) {
        // Find long/short pairs and close if profitable
        std::vector<int> longs_to_close, shorts_to_close;

        for (size_t i = 0; i < positions_.size(); i++) {
            if (!positions_[i].is_long) continue;  // Process longs

            const auto& long_pos = positions_[i];

            // Find matching short (lowest entry price first)
            int best_short = -1;
            double best_spread = 0;

            for (size_t j = 0; j < positions_.size(); j++) {
                if (positions_[j].is_long) continue;

                const auto& short_pos = positions_[j];
                double spread = short_pos.entry_price - long_pos.entry_price;

                if (spread > best_spread) {
                    best_spread = spread;
                    best_short = j;
                }
            }

            // Check if we can close this pair profitably
            if (best_short >= 0 && best_spread >= config_.tp_spread) {
                // Calculate pair PnL
                double long_pnl = (bid - long_pos.entry_price) * long_pos.lots * config_.contract_size;
                double short_pnl = (positions_[best_short].entry_price - ask) * positions_[best_short].lots * config_.contract_size;

                if (long_pnl + short_pnl > 0) {
                    longs_to_close.push_back(i);
                    shorts_to_close.push_back(best_short);
                }
            }
        }

        // Close pairs in reverse order
        for (int i = longs_to_close.size() - 1; i >= 0; i--) {
            int short_idx = shorts_to_close[i];
            int long_idx = longs_to_close[i];

            // Adjust indices after each removal
            if (short_idx > long_idx) {
                close_position(short_idx, bid, ask);
                close_position(long_idx, bid, ask);
            } else {
                close_position(long_idx, bid, ask);
                close_position(short_idx, bid, ask);
            }
            result_.pairs_closed++;
        }
    }

    void check_new_hedges(double bid, double ask) {
        double total_lots = 0;
        double long_lots = 0, short_lots = 0;

        for (const auto& pos : positions_) {
            total_lots += pos.lots;
            if (pos.is_long) long_lots += pos.lots;
            else short_lots += pos.lots;
        }

        if (total_lots >= config_.max_total_lots) return;

        // Determine bias based on price vs MA
        double bias = 0.5;  // 0.5 = neutral, >0.5 = prefer longs, <0.5 = prefer shorts
        if (moving_average_ > 0) {
            double dev = (ask - moving_average_) / moving_average_ * 100.0;
            // Below MA: prefer longs, Above MA: prefer shorts
            bias = 0.5 - (dev / 20.0);  // 10% below MA = 100% long bias
            bias = std::max(0.2, std::min(0.8, bias));
        }

        // Check if we need new long
        bool need_long = false;
        if (last_long_price_ == 0) {
            need_long = true;
        } else if (ask <= last_long_price_ - config_.hedge_spacing) {
            need_long = true;
        }

        // Check if we need new short
        bool need_short = false;
        if (last_short_price_ == 0) {
            need_short = true;
        } else if (bid >= last_short_price_ + config_.hedge_spacing) {
            need_short = true;
        }

        // Apply bias to decide which to open
        if (need_long && need_short) {
            // Open the one with higher bias first
            if (bias >= 0.5) {
                if (can_open_position(ask)) open_long(ask);
                if (can_open_position(bid)) open_short(bid);
            } else {
                if (can_open_position(bid)) open_short(bid);
                if (can_open_position(ask)) open_long(ask);
            }
        } else if (need_long) {
            if (can_open_position(ask)) open_long(ask);
        } else if (need_short) {
            if (can_open_position(bid)) open_short(bid);
        }
    }

    bool can_open_position(double price) {
        double equity = get_equity(price - config_.spread, price);
        double margin_needed = config_.base_lot_size * config_.contract_size * price / config_.leverage;
        double free_margin = equity - get_used_margin(price - config_.spread, price);
        return margin_needed < free_margin * 0.8;
    }

    void track_exposure() {
        double long_lots = 0, short_lots = 0;
        for (const auto& pos : positions_) {
            if (pos.is_long) long_lots += pos.lots;
            else short_lots += pos.lots;
        }

        double total = long_lots + short_lots;
        if (total > 0) {
            double exposure = std::abs(long_lots - short_lots) / total;
            if (exposure > result_.max_net_exposure) {
                result_.max_net_exposure = exposure;
            }
        }
    }

    void open_long(double ask) {
        HedgePosition pos;
        pos.entry_price = ask;
        pos.lots = config_.base_lot_size;
        pos.is_long = true;
        pos.pair_id = next_pair_id_++;

        positions_.push_back(pos);
        last_long_price_ = ask;

        result_.total_trades++;
        result_.long_trades++;
    }

    void open_short(double bid) {
        HedgePosition pos;
        pos.entry_price = bid;
        pos.lots = config_.base_lot_size;
        pos.is_long = false;
        pos.pair_id = next_pair_id_++;

        positions_.push_back(pos);
        last_short_price_ = bid;

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
};
