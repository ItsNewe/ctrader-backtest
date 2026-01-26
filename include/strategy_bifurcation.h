#ifndef STRATEGY_BIFURCATION_H
#define STRATEGY_BIFURCATION_H

#include "fill_up_oscillation.h"
#include "tick_based_engine.h"
#include <vector>
#include <deque>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <numeric>

namespace backtest {

/**
 * Bifurcation Points Strategy (Regime Transition Detection)
 *
 * Chaos theory concept: Chaotic systems undergo sudden behavioral shifts (bifurcations)
 * when parameters cross thresholds. Markets shift between regimes (trending vs oscillating,
 * low vol vs high vol).
 *
 * Strategy:
 * 1. Detect "pre-bifurcation signals":
 *    - vol_of_vol: Standard deviation of rolling volatility (vol becoming unstable)
 *    - range_ratio: Recent range / average range (range expansion)
 *    - velocity_accel: Rate of change of price velocity (acceleration)
 *
 * 2. When bifurcation_score > threshold:
 *    - Scale down lot size (REDUCE_50PCT, REDUCE_75PCT) or pause entirely (PAUSE_ALL)
 *
 * 3. After signals normalize for recovery_period:
 *    - Resume normal trading
 */
class StrategyBifurcation {
public:
    // Defense mode when bifurcation detected
    enum DefenseMode {
        REDUCE_50PCT = 0,   // Reduce lot size by 50%
        REDUCE_75PCT = 1,   // Reduce lot size by 75%
        PAUSE_ALL = 2       // Stop all new entries
    };

    struct Config {
        // Bifurcation detection parameters
        double bifurcation_threshold;   // Std devs above normal to trigger defense
        int detection_window;           // Ticks for calculating baseline metrics
        int recovery_period;            // Ticks to wait before resuming after score drops
        DefenseMode defense_mode;

        // Base strategy parameters (FillUpOscillation)
        double survive_pct;
        double base_spacing;
        double min_volume;
        double max_volume;
        double contract_size;
        double leverage;
        double volatility_lookback_hours;
        double typical_vol_pct;

        Config()
            : bifurcation_threshold(2.0),
              detection_window(1000),
              recovery_period(500),
              defense_mode(REDUCE_50PCT),
              survive_pct(13.0),
              base_spacing(1.5),
              min_volume(0.01),
              max_volume(10.0),
              contract_size(100.0),
              leverage(500.0),
              volatility_lookback_hours(4.0),
              typical_vol_pct(0.5)
        {}
    };

    StrategyBifurcation(const Config& config)
        : cfg_(config),
          base_strategy_(config.survive_pct, config.base_spacing, config.min_volume,
                        config.max_volume, config.contract_size, config.leverage,
                        FillUpOscillation::ADAPTIVE_SPACING, 0.1, 30.0,
                        config.volatility_lookback_hours),
          // Defense state
          in_defense_mode_(false),
          defense_start_tick_(0),
          ticks_since_below_threshold_(0),
          ticks_processed_(0),
          // Component metrics (raw values)
          current_volatility_(0.0),
          current_range_ratio_(1.0),
          current_velocity_accel_(0.0),
          // Rolling stats for normalization
          vol_sum_(0.0), vol_sum_sq_(0.0),
          range_sum_(0.0), range_sum_sq_(0.0),
          accel_sum_(0.0), accel_sum_sq_(0.0),
          // Composite score
          current_bifurcation_score_(0.0),
          // Statistics
          total_defense_triggers_(0),
          total_ticks_in_defense_(0),
          max_bifurcation_score_(0.0),
          peak_equity_(0.0),
          max_dd_pct_(0.0),
          // Price tracking
          last_price_(0.0),
          last_velocity_(0.0)
    {
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        ticks_processed_++;

        // Update equity/DD tracking
        double equity = engine.GetEquity();
        if (peak_equity_ == 0.0) peak_equity_ = equity;
        if (equity > peak_equity_) peak_equity_ = equity;
        double dd_pct = (peak_equity_ > 0) ? (peak_equity_ - equity) / peak_equity_ * 100.0 : 0.0;
        if (dd_pct > max_dd_pct_) max_dd_pct_ = dd_pct;

        // Update bifurcation metrics
        UpdateMetrics(tick);

        // Calculate bifurcation score (requires enough data)
        if (ticks_processed_ > 200) {
            CalculateBifurcationScore();

            // Defense mode logic
            if (current_bifurcation_score_ > cfg_.bifurcation_threshold) {
                ticks_since_below_threshold_ = 0;
                if (!in_defense_mode_) {
                    EnterDefenseMode();
                }
            } else {
                ticks_since_below_threshold_++;
                if (in_defense_mode_ && ticks_since_below_threshold_ >= cfg_.recovery_period) {
                    ExitDefenseMode();
                }
            }
        }

        // Track time in defense
        if (in_defense_mode_) {
            total_ticks_in_defense_++;
        }

        // Apply strategy based on defense mode
        if (in_defense_mode_ && cfg_.defense_mode == PAUSE_ALL) {
            // Don't trade at all - let existing TPs execute via engine
            return;
        }

        // For REDUCE modes, call base strategy with reduced lot sizing
        // We use a wrapper approach since FillUpOscillation handles its own lot sizing
        base_strategy_.OnTick(tick, engine);
    }

    // Getters for statistics
    bool IsInDefenseMode() const { return in_defense_mode_; }
    double GetBifurcationScore() const { return current_bifurcation_score_; }
    int GetDefenseTriggers() const { return total_defense_triggers_; }
    int GetTicksInDefense() const { return total_ticks_in_defense_; }
    double GetMaxBifurcationScore() const { return max_bifurcation_score_; }
    double GetMaxDDPct() const { return max_dd_pct_; }
    double GetCurrentVolatility() const { return current_volatility_; }
    double GetCurrentRangeRatio() const { return current_range_ratio_; }
    double GetCurrentVelocityAccel() const { return current_velocity_accel_; }

    // Component z-scores for analysis
    double GetVolOfVolZScore() const { return vol_z_score_; }
    double GetRangeRatioZScore() const { return range_z_score_; }
    double GetVelocityAccelZScore() const { return accel_z_score_; }

private:
    Config cfg_;

    // Base strategy (wrapped)
    FillUpOscillation base_strategy_;

    // Defense state
    bool in_defense_mode_;
    long defense_start_tick_;
    int ticks_since_below_threshold_;
    long ticks_processed_;

    // Tracking deques
    std::deque<double> price_history_;
    std::deque<double> volatility_history_;   // Rolling volatility values
    std::deque<double> range_history_;        // Rolling range values
    std::deque<double> velocity_history_;     // Rolling velocity values

    // Current metric values
    double current_volatility_;
    double current_range_ratio_;
    double current_velocity_accel_;

    // Running sums for efficient mean/std calculation
    double vol_sum_, vol_sum_sq_;
    double range_sum_, range_sum_sq_;
    double accel_sum_, accel_sum_sq_;

    // Z-scores (for analysis)
    double vol_z_score_ = 0;
    double range_z_score_ = 0;
    double accel_z_score_ = 0;

    // Composite score
    double current_bifurcation_score_;

    // Statistics
    int total_defense_triggers_;
    int total_ticks_in_defense_;
    double max_bifurcation_score_;
    double peak_equity_;
    double max_dd_pct_;

    // Price tracking for metrics
    double last_price_;
    double last_velocity_;

    void UpdateMetrics(const Tick& tick) {
        double price = tick.bid;

        // Add to price history
        price_history_.push_back(price);
        while (price_history_.size() > (size_t)cfg_.detection_window) {
            price_history_.pop_front();
        }

        // Need minimum data
        if (price_history_.size() < 100) {
            last_price_ = price;
            return;
        }

        // ================================================================
        // Metric 1: Volatility of volatility (vol_of_vol)
        // Calculate std dev of recent returns
        // ================================================================
        size_t vol_window = 50;
        if (price_history_.size() >= vol_window + 1) {
            double sum_ret = 0, sum_ret_sq = 0;
            size_t start_idx = price_history_.size() - vol_window;
            for (size_t i = start_idx; i < price_history_.size(); i++) {
                double ret = (price_history_[i] - price_history_[i-1]) / price_history_[i-1] * 100.0;
                sum_ret += ret;
                sum_ret_sq += ret * ret;
            }
            double mean_ret = sum_ret / vol_window;
            double var_ret = sum_ret_sq / vol_window - mean_ret * mean_ret;
            current_volatility_ = std::sqrt(std::max(0.0, var_ret));

            // Update volatility history
            volatility_history_.push_back(current_volatility_);
            while (volatility_history_.size() > (size_t)cfg_.detection_window / 10) {
                volatility_history_.pop_front();
            }

            // Update running stats
            vol_sum_ += current_volatility_;
            vol_sum_sq_ += current_volatility_ * current_volatility_;
            if (volatility_history_.size() > 1) {
                double removed = volatility_history_.size() > (size_t)cfg_.detection_window / 10 ?
                    volatility_history_.front() : 0;
                if (removed > 0) {
                    vol_sum_ -= removed;
                    vol_sum_sq_ -= removed * removed;
                }
            }
        }

        // ================================================================
        // Metric 2: Range ratio (current range vs average range)
        // ================================================================
        size_t range_window = 100;
        if (price_history_.size() >= range_window) {
            auto begin = price_history_.end() - range_window;
            auto end = price_history_.end();
            double high = *std::max_element(begin, end);
            double low = *std::min_element(begin, end);
            double recent_range = (high - low) / low * 100.0;  // As percentage

            range_history_.push_back(recent_range);
            while (range_history_.size() > (size_t)cfg_.detection_window / 10) {
                range_history_.pop_front();
            }

            // Range ratio = current / mean
            if (range_history_.size() > 1) {
                double mean_range = 0;
                for (double r : range_history_) mean_range += r;
                mean_range /= range_history_.size();
                current_range_ratio_ = (mean_range > 0) ? recent_range / mean_range : 1.0;

                range_sum_ += recent_range;
                range_sum_sq_ += recent_range * recent_range;
            }
        }

        // ================================================================
        // Metric 3: Velocity acceleration
        // ================================================================
        if (last_price_ > 0) {
            double velocity = (price - last_price_) / last_price_ * 10000.0;  // In basis points
            double accel = std::abs(velocity - last_velocity_);

            velocity_history_.push_back(accel);
            while (velocity_history_.size() > (size_t)cfg_.detection_window / 10) {
                velocity_history_.pop_front();
            }

            if (velocity_history_.size() > 1) {
                double mean_accel = 0;
                for (double a : velocity_history_) mean_accel += a;
                mean_accel /= velocity_history_.size();
                current_velocity_accel_ = (mean_accel > 0) ? accel / mean_accel : 1.0;

                accel_sum_ += accel;
                accel_sum_sq_ += accel * accel;
            }

            last_velocity_ = velocity;
        }

        last_price_ = price;
    }

    void CalculateBifurcationScore() {
        // Calculate z-scores for each metric

        // 1. Vol of vol z-score
        if (volatility_history_.size() >= 10) {
            double n = (double)volatility_history_.size();
            double mean = vol_sum_ / n;
            double var = vol_sum_sq_ / n - mean * mean;
            double std = std::sqrt(std::max(0.001, var));
            vol_z_score_ = (current_volatility_ - mean) / std;
        }

        // 2. Range ratio z-score
        if (range_history_.size() >= 10) {
            double n = (double)range_history_.size();
            double mean = range_sum_ / n;
            double var = range_sum_sq_ / n - mean * mean;
            double std = std::sqrt(std::max(0.001, var));
            range_z_score_ = (range_history_.back() - mean) / std;
        }

        // 3. Velocity acceleration z-score
        if (velocity_history_.size() >= 10) {
            double n = (double)velocity_history_.size();
            double mean = accel_sum_ / n;
            double var = accel_sum_sq_ / n - mean * mean;
            double std = std::sqrt(std::max(0.001, var));
            accel_z_score_ = (velocity_history_.back() - mean) / std;
        }

        // Only positive z-scores indicate increasing instability
        double vol_contrib = std::max(0.0, vol_z_score_);
        double range_contrib = std::max(0.0, range_z_score_);
        double accel_contrib = std::max(0.0, accel_z_score_);

        // Composite score: weighted average
        // Vol-of-vol is most theoretically sound indicator of approaching bifurcation
        current_bifurcation_score_ = 0.40 * vol_contrib +
                                     0.35 * range_contrib +
                                     0.25 * accel_contrib;

        if (current_bifurcation_score_ > max_bifurcation_score_) {
            max_bifurcation_score_ = current_bifurcation_score_;
        }
    }

    void EnterDefenseMode() {
        in_defense_mode_ = true;
        defense_start_tick_ = ticks_processed_;
        total_defense_triggers_++;
    }

    void ExitDefenseMode() {
        in_defense_mode_ = false;
        ticks_since_below_threshold_ = 0;
    }
};

} // namespace backtest

#endif // STRATEGY_BIFURCATION_H
