#ifndef STRATEGY_NOISE_SCALING_H
#define STRATEGY_NOISE_SCALING_H

#include "tick_based_engine.h"
#include <vector>
#include <deque>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <numeric>

namespace backtest {

/**
 * Noise-Scaled Position Sizing Strategy
 *
 * CONCEPT: In some systems, noise doesn't obscure signal - it ENABLES signal
 * transmission (stochastic facilitation). Instead of filtering out noise,
 * use noise LEVEL as a signal.
 *
 * High noise = more oscillation opportunity = larger positions
 * Low noise = less opportunity (trending) = smaller positions
 *
 * Noise metrics:
 * 1. TICK_VOL: Std dev of tick-to-tick returns over N ticks
 * 2. DIRECTION_CHANGES: Count of sign changes in returns (choppiness)
 * 3. COMBINED: Average of both normalized metrics
 */
class StrategyNoiseScaling {
public:
    enum NoiseMetric {
        TICK_VOL = 0,           // Standard deviation of tick returns
        DIRECTION_CHANGES = 1,  // Count of direction reversals (choppiness)
        COMBINED = 2            // Average of both metrics
    };

    struct NoiseConfig {
        NoiseMetric metric;
        int noise_window;           // Ticks for noise measurement (short-term)
        int reference_window;       // Ticks for reference/baseline noise (long-term)
        double min_multiplier;      // Floor for lot scaling
        double max_multiplier;      // Ceiling for lot scaling

        NoiseConfig()
            : metric(TICK_VOL),
              noise_window(100),
              reference_window(5000),
              min_multiplier(0.5),
              max_multiplier(2.0) {}
    };

    StrategyNoiseScaling(double survive_pct, double base_spacing,
                         double min_volume, double max_volume,
                         const NoiseConfig& noise_config = NoiseConfig())
        : survive_pct_(survive_pct),
          base_spacing_(base_spacing),
          min_volume_(min_volume),
          max_volume_(max_volume),
          noise_config_(noise_config),
          lowest_buy_(DBL_MAX),
          highest_buy_(DBL_MIN),
          volume_of_open_trades_(0.0),
          current_ask_(0.0),
          current_bid_(0.0),
          current_spread_(0.0),
          current_equity_(0.0),
          current_balance_(0.0),
          peak_equity_(0.0),
          last_bid_(0.0),
          current_noise_ratio_(1.0),
          noise_scale_factor_(1.0),
          ticks_processed_(0),
          last_direction_(0),
          noise_scale_changes_(0),
          max_noise_ratio_(0.0),
          min_noise_ratio_(DBL_MAX),
          noise_ratio_sum_(0.0),
          noise_ratio_count_(0)
    {
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_spread_ = tick.spread();
        current_equity_ = engine.GetEquity();
        current_balance_ = engine.GetBalance();
        ticks_processed_++;

        // Initialize
        if (peak_equity_ == 0.0) {
            peak_equity_ = current_equity_;
            last_bid_ = current_bid_;
        }

        // Update peak equity
        if (current_equity_ > peak_equity_) {
            peak_equity_ = current_equity_;
        }

        // Update noise measurement
        UpdateNoiseMeasurement(tick);

        // Process positions
        Iterate(engine);

        // Open new positions
        OpenNew(engine);

        last_bid_ = current_bid_;
    }

    // Statistics
    double GetCurrentNoiseRatio() const { return current_noise_ratio_; }
    double GetNoiseScaleFactor() const { return noise_scale_factor_; }
    int GetNoiseScaleChanges() const { return noise_scale_changes_; }
    double GetMaxNoiseRatio() const { return max_noise_ratio_; }
    double GetMinNoiseRatio() const { return min_noise_ratio_ == DBL_MAX ? 0 : min_noise_ratio_; }
    double GetAvgNoiseRatio() const {
        return noise_ratio_count_ > 0 ? noise_ratio_sum_ / noise_ratio_count_ : 1.0;
    }
    double GetPeakEquity() const { return peak_equity_; }

private:
    // Base parameters
    double survive_pct_;
    double base_spacing_;
    double min_volume_;
    double max_volume_;
    // Note: contract_size_/leverage_ removed — now read from engine.GetConfig()

    // Noise configuration
    NoiseConfig noise_config_;

    // Position tracking
    double lowest_buy_;
    double highest_buy_;
    double volume_of_open_trades_;

    // Market state
    double current_ask_;
    double current_bid_;
    double current_spread_;
    double current_equity_;
    double current_balance_;
    double peak_equity_;
    double last_bid_;

    // Noise measurement
    std::deque<double> tick_returns_;           // For TICK_VOL
    std::deque<int> direction_changes_;         // 1=changed, 0=same
    double current_noise_ratio_;
    double noise_scale_factor_;

    // State tracking
    long ticks_processed_;
    int last_direction_;  // 1=up, -1=down, 0=flat

    // Statistics
    int noise_scale_changes_;
    double max_noise_ratio_;
    double min_noise_ratio_;
    double noise_ratio_sum_;
    int noise_ratio_count_;

    void UpdateNoiseMeasurement(const Tick& /*tick*/) {
        if (last_bid_ <= 0) return;

        // Calculate tick-to-tick return
        double tick_return = (current_bid_ - last_bid_) / last_bid_;
        tick_returns_.push_back(tick_return);

        // Track direction changes
        int direction = (tick_return > 0) ? 1 : (tick_return < 0) ? -1 : 0;
        int changed = (direction != 0 && last_direction_ != 0 && direction != last_direction_) ? 1 : 0;
        direction_changes_.push_back(changed);
        if (direction != 0) last_direction_ = direction;

        // Maintain window sizes
        while ((int)tick_returns_.size() > noise_config_.reference_window) {
            tick_returns_.pop_front();
        }
        while ((int)direction_changes_.size() > noise_config_.reference_window) {
            direction_changes_.pop_front();
        }

        // Need enough data
        if ((int)tick_returns_.size() < noise_config_.noise_window) return;

        // Calculate noise ratio based on metric
        double noise_ratio = CalculateNoiseRatio();

        // Track statistics
        if (noise_ratio > max_noise_ratio_) max_noise_ratio_ = noise_ratio;
        if (noise_ratio < min_noise_ratio_) min_noise_ratio_ = noise_ratio;
        noise_ratio_sum_ += noise_ratio;
        noise_ratio_count_++;

        // Significant change detection
        if (std::abs(noise_ratio - current_noise_ratio_) > 0.1) {
            noise_scale_changes_++;
        }

        current_noise_ratio_ = noise_ratio;

        // Apply multiplier bounds
        noise_scale_factor_ = std::max(noise_config_.min_multiplier,
                                       std::min(noise_config_.max_multiplier, noise_ratio));
    }

    double CalculateNoiseRatio() {
        switch (noise_config_.metric) {
            case TICK_VOL:
                return CalculateTickVolRatio();
            case DIRECTION_CHANGES:
                return CalculateDirectionChangeRatio();
            case COMBINED:
                return (CalculateTickVolRatio() + CalculateDirectionChangeRatio()) / 2.0;
            default:
                return 1.0;
        }
    }

    double CalculateTickVolRatio() {
        // Ratio of short-term volatility to long-term volatility
        double short_vol = CalculateStdDev(noise_config_.noise_window);
        double long_vol = CalculateStdDev((int)tick_returns_.size());

        if (long_vol <= 0) return 1.0;
        return short_vol / long_vol;
    }

    double CalculateStdDev(int window_size) {
        if (window_size <= 1 || tick_returns_.empty()) return 0;

        int n = std::min(window_size, (int)tick_returns_.size());
        if (n <= 1) return 0;

        // Use last n elements
        double sum = 0, sum_sq = 0;
        auto it = tick_returns_.end() - n;
        for (int i = 0; i < n; ++i, ++it) {
            sum += *it;
            sum_sq += (*it) * (*it);
        }

        double mean = sum / n;
        double variance = (sum_sq / n) - (mean * mean);
        return (variance > 0) ? std::sqrt(variance) : 0;
    }

    double CalculateDirectionChangeRatio() {
        // Ratio of short-term choppiness to long-term choppiness
        int n_short = std::min(noise_config_.noise_window, (int)direction_changes_.size());
        int n_long = (int)direction_changes_.size();

        if (n_short == 0 || n_long == 0) return 1.0;

        // Count changes in short window
        int short_changes = 0;
        auto it = direction_changes_.end() - n_short;
        for (int i = 0; i < n_short; ++i, ++it) {
            short_changes += *it;
        }
        double short_rate = (double)short_changes / n_short;

        // Count changes in long window
        int long_changes = 0;
        for (int c : direction_changes_) {
            long_changes += c;
        }
        double long_rate = (double)long_changes / n_long;

        if (long_rate <= 0) return 1.0;
        return short_rate / long_rate;
    }

    void Iterate(TickBasedEngine& engine) {
        // Use engine's incrementally-maintained aggregates (O(1) instead of O(N))
        volume_of_open_trades_ = engine.GetBuyVolume();
        lowest_buy_ = engine.GetLowestBuyEntry();
        highest_buy_ = engine.GetHighestBuyEntry();
    }

    double CalculateLotSize(TickBasedEngine& engine, int positions_total) {
        // Use engine's authoritative margin (current market prices, includes margin_rate)
        double used_margin = engine.GetUsedMargin();
        const auto& cfg = engine.GetConfig();

        double margin_stop_out = 20.0;

        double end_price = (positions_total == 0)
            ? current_ask_ * ((100.0 - survive_pct_) / 100.0)
            : highest_buy_ * ((100.0 - survive_pct_) / 100.0);

        double distance = current_ask_ - end_price;
        double number_of_trades = std::floor(distance / base_spacing_);
        if (number_of_trades <= 0) number_of_trades = 1;

        // Calculate base lot size
        double equity_at_target = current_equity_ - volume_of_open_trades_ * distance * cfg.contract_size;
        if (used_margin > 0 && (equity_at_target / used_margin * 100.0) < margin_stop_out) {
            return 0.0;
        }

        double trade_size = min_volume_;
        double d_equity = cfg.contract_size * trade_size * base_spacing_ * (number_of_trades * (number_of_trades + 1) / 2);
        double d_margin = engine.CalculateMarginRequired(trade_size, current_ask_) * number_of_trades;

        // Find multiplier
        double max_mult = max_volume_ / min_volume_;
        for (double mult = max_mult; mult >= 1.0; mult -= 0.1) {
            double test_equity = equity_at_target - mult * d_equity;
            double test_margin = used_margin + mult * d_margin;
            if (test_margin > 0 && (test_equity / test_margin * 100.0) > margin_stop_out) {
                trade_size = mult * min_volume_;
                break;
            }
        }

        // Apply NOISE SCALING - the key mechanism
        trade_size *= noise_scale_factor_;

        return std::min(trade_size, max_volume_);
    }

    bool Open(double lots, TickBasedEngine& engine) {
        if (lots < min_volume_) return false;

        double final_lots = engine.NormalizeLots(std::min(lots, max_volume_));

        double tp = current_ask_ + current_spread_ + base_spacing_;
        Trade* trade = engine.OpenMarketOrder("BUY", final_lots, 0.0, tp);
        return (trade != nullptr);
    }

    void OpenNew(TickBasedEngine& engine) {
        int positions_total = (int)engine.GetOpenPositions().size();

        if (positions_total == 0) {
            double lots = CalculateLotSize(engine, positions_total);
            if (Open(lots, engine)) {
                highest_buy_ = current_ask_;
                lowest_buy_ = current_ask_;
            }
        } else {
            if (lowest_buy_ >= current_ask_ + base_spacing_) {
                double lots = CalculateLotSize(engine, positions_total);
                if (Open(lots, engine)) {
                    lowest_buy_ = current_ask_;
                }
            } else if (highest_buy_ <= current_ask_ - base_spacing_) {
                double lots = CalculateLotSize(engine, positions_total);
                if (Open(lots, engine)) {
                    highest_buy_ = current_ask_;
                }
            }
        }
    }
};

} // namespace backtest

#endif // STRATEGY_NOISE_SCALING_H
