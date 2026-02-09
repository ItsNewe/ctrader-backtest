#pragma once
/**
 * Regime-Adaptive Strategy
 *
 * Philosophy: Different market conditions need different approaches
 *
 * Detects and adapts to:
 * 1. TRENDING_UP: Strong upward momentum - be aggressive
 * 2. TRENDING_DOWN: Strong downward momentum - be defensive
 * 3. RANGING: Sideways movement - profit from oscillation
 * 4. HIGH_VOLATILITY: Large swings - reduce size, widen spacing
 *
 * Key insight: No single strategy works in all conditions.
 * Adapt or die.
 */

#include <vector>
#include <deque>
#include <cmath>
#include <algorithm>
#include <numeric>

enum class MarketRegime {
    TRENDING_UP,
    TRENDING_DOWN,
    RANGING,
    HIGH_VOLATILITY
};

struct RegimeConfig {
    // Base parameters
    double base_lot_size = 0.01;
    double base_spacing = 50.0;
    int max_positions = 20;

    // Regime detection
    int trend_lookback = 1000;       // Ticks to detect trend
    double trend_threshold = 1.0;    // % move to consider trending
    int volatility_lookback = 500;   // Ticks for volatility calc
    double high_vol_threshold = 2.0; // Multiplier for "high" volatility

    // Regime-specific multipliers
    double trending_up_size_mult = 1.5;    // More aggressive in uptrend
    double trending_down_size_mult = 0.5;  // Defensive in downtrend
    double ranging_size_mult = 1.0;        // Normal in ranging
    double high_vol_size_mult = 0.3;       // Small in high vol

    double trending_up_spacing_mult = 0.8;   // Tighter spacing in uptrend
    double trending_down_spacing_mult = 1.5; // Wider spacing in downtrend
    double ranging_spacing_mult = 1.0;       // Normal spacing
    double high_vol_spacing_mult = 2.0;      // Much wider in high vol

    // Profit taking
    double take_profit_pct = 2.0;
    double partial_take_pct = 0.5;

    // Risk management
    double stop_out_level = 50.0;
    double max_equity_risk = 0.3;

    // Market parameters
    double leverage = 500.0;
    double contract_size = 1.0;
    double spread = 1.0;
};

struct RegimePosition {
    double entry_price;
    double lots;
    MarketRegime entry_regime;
};

struct RegimeResult {
    double final_equity;
    double max_drawdown_pct;
    double max_equity;
    int total_entries;
    int total_exits;
    int profit_takes;

    // Regime stats
    int trending_up_entries;
    int trending_down_entries;
    int ranging_entries;
    int high_vol_entries;

    double time_in_trending_up;
    double time_in_trending_down;
    double time_in_ranging;
    double time_in_high_vol;

    bool margin_call_occurred;
};

class RegimeAdaptiveStrategy {
private:
    RegimeConfig config_;
    std::vector<RegimePosition> positions_;
    std::deque<double> price_history_;
    std::deque<double> returns_history_;

    double balance_;
    double initial_balance_;
    double peak_equity_;

    double last_entry_price_;
    double baseline_volatility_;
    long tick_count_;

    MarketRegime current_regime_;
    RegimeResult result_;

public:
    void configure(const RegimeConfig& cfg) {
        config_ = cfg;
    }

    void reset(double starting_balance) {
        positions_.clear();
        price_history_.clear();
        returns_history_.clear();
        balance_ = starting_balance;
        initial_balance_ = starting_balance;
        peak_equity_ = starting_balance;
        last_entry_price_ = 0.0;
        baseline_volatility_ = 0.0;
        tick_count_ = 0;
        current_regime_ = MarketRegime::RANGING;
        result_ = RegimeResult{};
        result_.max_equity = starting_balance;
    }

    void on_tick(double bid, double ask) {
        double mid = (bid + ask) / 2.0;
        tick_count_++;

        // Update price history
        if (!price_history_.empty()) {
            double ret = (mid - price_history_.back()) / price_history_.back() * 100.0;
            returns_history_.push_back(ret);
            if (returns_history_.size() > static_cast<size_t>(config_.volatility_lookback)) {
                returns_history_.pop_front();
            }
        }

        price_history_.push_back(mid);
        if (price_history_.size() > static_cast<size_t>(config_.trend_lookback + 100)) {
            price_history_.pop_front();
        }

        // Detect regime
        detect_regime();
        update_regime_time();

        // Check margin
        if (check_margin_stop_out(bid, ask)) {
            return;
        }

        // Check profit taking
        check_profit_taking(bid, ask);

        // Check for new entries (regime-adjusted)
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

    RegimeResult get_result(double bid, double ask) {
        result_.final_equity = get_equity(bid, ask);

        // Normalize regime time to percentages
        double total = result_.time_in_trending_up + result_.time_in_trending_down +
                      result_.time_in_ranging + result_.time_in_high_vol;
        if (total > 0) {
            result_.time_in_trending_up = 100.0 * result_.time_in_trending_up / total;
            result_.time_in_trending_down = 100.0 * result_.time_in_trending_down / total;
            result_.time_in_ranging = 100.0 * result_.time_in_ranging / total;
            result_.time_in_high_vol = 100.0 * result_.time_in_high_vol / total;
        }

        return result_;
    }

    MarketRegime get_current_regime() const { return current_regime_; }

private:
    void detect_regime() {
        if (price_history_.size() < static_cast<size_t>(config_.trend_lookback)) {
            current_regime_ = MarketRegime::RANGING;
            return;
        }

        // Calculate trend (% change over lookback)
        size_t start_idx = price_history_.size() - config_.trend_lookback;
        double start_price = price_history_[start_idx];
        double end_price = price_history_.back();
        double trend_pct = (end_price - start_price) / start_price * 100.0;

        // Calculate current volatility
        double current_vol = calculate_volatility();

        // Update baseline volatility (rolling average)
        if (baseline_volatility_ == 0.0) {
            baseline_volatility_ = current_vol;
        } else {
            baseline_volatility_ = baseline_volatility_ * 0.999 + current_vol * 0.001;
        }

        // Determine regime
        bool is_high_vol = (baseline_volatility_ > 0 &&
                          current_vol > baseline_volatility_ * config_.high_vol_threshold);

        if (is_high_vol) {
            current_regime_ = MarketRegime::HIGH_VOLATILITY;
        } else if (trend_pct > config_.trend_threshold) {
            current_regime_ = MarketRegime::TRENDING_UP;
        } else if (trend_pct < -config_.trend_threshold) {
            current_regime_ = MarketRegime::TRENDING_DOWN;
        } else {
            current_regime_ = MarketRegime::RANGING;
        }
    }

    double calculate_volatility() {
        if (returns_history_.size() < 10) return 0.0;

        double sum = 0.0;
        double sum_sq = 0.0;
        for (double r : returns_history_) {
            sum += r;
            sum_sq += r * r;
        }

        double n = returns_history_.size();
        double mean = sum / n;
        double variance = (sum_sq / n) - (mean * mean);

        return std::sqrt(variance);
    }

    void update_regime_time() {
        switch (current_regime_) {
            case MarketRegime::TRENDING_UP:
                result_.time_in_trending_up++;
                break;
            case MarketRegime::TRENDING_DOWN:
                result_.time_in_trending_down++;
                break;
            case MarketRegime::RANGING:
                result_.time_in_ranging++;
                break;
            case MarketRegime::HIGH_VOLATILITY:
                result_.time_in_high_vol++;
                break;
        }
    }

    double get_size_multiplier() {
        switch (current_regime_) {
            case MarketRegime::TRENDING_UP:
                return config_.trending_up_size_mult;
            case MarketRegime::TRENDING_DOWN:
                return config_.trending_down_size_mult;
            case MarketRegime::RANGING:
                return config_.ranging_size_mult;
            case MarketRegime::HIGH_VOLATILITY:
                return config_.high_vol_size_mult;
        }
        return 1.0;
    }

    double get_spacing_multiplier() {
        switch (current_regime_) {
            case MarketRegime::TRENDING_UP:
                return config_.trending_up_spacing_mult;
            case MarketRegime::TRENDING_DOWN:
                return config_.trending_down_spacing_mult;
            case MarketRegime::RANGING:
                return config_.ranging_spacing_mult;
            case MarketRegime::HIGH_VOLATILITY:
                return config_.high_vol_spacing_mult;
        }
        return 1.0;
    }

    double get_used_margin(double ask) const {
        double margin = 0.0;
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

        // Skip entries in downtrend (defensive mode)
        if (current_regime_ == MarketRegime::TRENDING_DOWN && !positions_.empty()) {
            return;  // Don't add to positions in downtrend
        }

        double effective_spacing = config_.base_spacing * get_spacing_multiplier();

        // Entry logic
        bool should_enter = false;

        if (last_entry_price_ == 0) {
            should_enter = true;
        } else if (ask <= last_entry_price_ - effective_spacing) {
            should_enter = true;
        }

        if (should_enter) {
            double effective_size = config_.base_lot_size * get_size_multiplier();

            double equity = get_equity(bid, ask);
            double margin_needed = effective_size * config_.contract_size * ask / config_.leverage;
            double free_margin = equity - get_used_margin(ask);

            if (margin_needed < free_margin * (1.0 - config_.max_equity_risk)) {
                open_position(ask, effective_size);
            }
        }
    }

    void open_position(double price, double lots) {
        RegimePosition pos;
        pos.entry_price = price;
        pos.lots = lots;
        pos.entry_regime = current_regime_;

        positions_.push_back(pos);
        last_entry_price_ = price;
        result_.total_entries++;

        // Track regime-specific entries
        switch (current_regime_) {
            case MarketRegime::TRENDING_UP:
                result_.trending_up_entries++;
                break;
            case MarketRegime::TRENDING_DOWN:
                result_.trending_down_entries++;
                break;
            case MarketRegime::RANGING:
                result_.ranging_entries++;
                break;
            case MarketRegime::HIGH_VOLATILITY:
                result_.high_vol_entries++;
                break;
        }
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
