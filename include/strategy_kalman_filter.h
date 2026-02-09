#ifndef STRATEGY_KALMAN_FILTER_H
#define STRATEGY_KALMAN_FILTER_H

#include "tick_based_engine.h"
#include <vector>
#include <deque>
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace backtest {

/**
 * KALMAN FILTER GRID STRATEGY
 *
 * Uses a Kalman Filter to optimally estimate the "true" price level from noisy
 * tick observations. Unlike fixed EMA smoothing, the Kalman Filter:
 *   1. Adapts smoothing automatically based on noise levels
 *   2. Provides uncertainty estimates (confidence bands)
 *   3. Is mathematically optimal for linear Gaussian systems
 *
 * The filter tracks a 2D state: [price, velocity] where velocity is the rate
 * of price change. The Kalman gain adapts based on the ratio of process noise
 * (how much we expect price to move) vs measurement noise (observation error).
 *
 * Trading logic:
 *   - Buy when price is below the Kalman estimate by spacing
 *   - Set TP back toward the Kalman estimate
 *   - Optionally scale spacing by the filter's uncertainty (variance)
 */
class StrategyKalmanFilter {
public:
    struct Config {
        // Kalman filter parameters
        double process_noise_q;         // Q: how much price is expected to move
        double measurement_noise_r;     // R: observation noise variance
        bool use_uncertainty_scaling;   // Scale spacing by Kalman uncertainty
        double uncertainty_scale_factor;// Multiplier for uncertainty-based spacing

        // Grid parameters
        double survive_pct;             // Max adverse move to survive (% of price)
        double base_spacing;            // Base grid spacing ($)
        double min_volume;              // Minimum lot size
        double max_volume;              // Maximum lot size
        double contract_size;           // Contract size (100 for gold)
        double leverage;                // Account leverage
        double tp_multiplier;           // TP as multiple of spacing toward Kalman estimate

        // Volatility adaptation (optional, for comparison with EMA baseline)
        bool adaptive_spacing;          // Enable volatility-adaptive spacing
        double typical_vol_pct;         // Typical volatility as % of price
        double volatility_lookback_hours; // Hours for volatility calculation

        Config()
            : process_noise_q(0.01),
              measurement_noise_r(1.0),
              use_uncertainty_scaling(false),
              uncertainty_scale_factor(0.5),
              survive_pct(13.0),
              base_spacing(1.50),
              min_volume(0.01),
              max_volume(10.0),
              contract_size(100.0),
              leverage(500.0),
              tp_multiplier(1.5),
              adaptive_spacing(true),
              typical_vol_pct(0.5),
              volatility_lookback_hours(4.0) {}
    };

    explicit StrategyKalmanFilter(const Config& config)
        : config_(config),
          // Kalman state
          x_price_(0.0),
          x_velocity_(0.0),
          p_price_(1.0),
          p_velocity_(1.0),
          p_price_velocity_(0.0),
          kalman_initialized_(false),
          // Market state
          current_ask_(0.0),
          current_bid_(0.0),
          current_spread_(0.0),
          current_equity_(0.0),
          current_balance_(0.0),
          peak_equity_(0.0),
          // Grid state
          current_spacing_(config.base_spacing),
          lowest_buy_(DBL_MAX),
          highest_buy_(DBL_MIN),
          volume_of_open_trades_(0.0),
          // Statistics
          ticks_processed_(0),
          kalman_crossings_(0),
          max_uncertainty_(0.0),
          min_uncertainty_(DBL_MAX),
          // Volatility tracking
          recent_high_(0.0),
          recent_low_(DBL_MAX),
          last_vol_reset_seconds_(0),
          // Time tracking for Kalman (dt estimation)
          last_tick_seconds_(0),
          dt_(1.0)  // Default dt = 1 second
    {
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_spread_ = tick.spread();
        current_equity_ = engine.GetEquity();
        current_balance_ = engine.GetBalance();
        ticks_processed_++;

        // Parse timestamp for time tracking
        long current_seconds = ParseTimestampToSeconds(tick.timestamp);

        // Initialize on first tick
        if (!kalman_initialized_) {
            x_price_ = current_bid_;
            x_velocity_ = 0.0;
            p_price_ = config_.measurement_noise_r;
            p_velocity_ = config_.process_noise_q;
            p_price_velocity_ = 0.0;
            peak_equity_ = current_equity_;
            last_tick_seconds_ = current_seconds;
            kalman_initialized_ = true;
            return;
        }

        // Update peak equity
        if (current_equity_ > peak_equity_) {
            peak_equity_ = current_equity_;
        }

        // Estimate dt (time since last tick, clamped)
        if (current_seconds > last_tick_seconds_) {
            dt_ = std::min(60.0, (double)(current_seconds - last_tick_seconds_));
        } else {
            dt_ = 0.001; // Same second, use small dt
        }
        last_tick_seconds_ = current_seconds;

        // Kalman Filter Update
        double prev_estimate = x_price_;
        KalmanUpdate(current_bid_);

        // Track crossings (price crossing Kalman estimate)
        TrackCrossings(prev_estimate);

        // Update volatility for adaptive spacing
        if (config_.adaptive_spacing) {
            UpdateVolatility(tick);
            UpdateAdaptiveSpacing();
        }

        // Apply uncertainty scaling if enabled
        if (config_.use_uncertainty_scaling) {
            ApplyUncertaintyScaling();
        }

        // Process existing positions
        Iterate(engine);

        // Open new positions
        OpenNew(engine);
    }

    // Getters for statistics
    double GetKalmanEstimate() const { return x_price_; }
    double GetKalmanVelocity() const { return x_velocity_; }
    double GetKalmanUncertainty() const { return std::sqrt(p_price_); }
    double GetCurrentSpacing() const { return current_spacing_; }
    int GetKalmanCrossings() const { return kalman_crossings_; }
    double GetMaxUncertainty() const { return max_uncertainty_; }
    double GetMinUncertainty() const { return min_uncertainty_; }
    long GetTicksProcessed() const { return ticks_processed_; }
    double GetKalmanGain() const {
        // Current effective Kalman gain
        return p_price_ / (p_price_ + config_.measurement_noise_r);
    }

private:
    Config config_;

    // Kalman filter state (2D: price + velocity)
    double x_price_;           // Estimated "true" price
    double x_velocity_;        // Estimated price velocity (drift)
    double p_price_;           // Price variance (uncertainty)
    double p_velocity_;        // Velocity variance
    double p_price_velocity_;  // Cross-covariance
    bool kalman_initialized_;

    // Market state
    double current_ask_;
    double current_bid_;
    double current_spread_;
    double current_equity_;
    double current_balance_;
    double peak_equity_;

    // Grid state
    double current_spacing_;
    double lowest_buy_;
    double highest_buy_;
    double volume_of_open_trades_;

    // Statistics
    long ticks_processed_;
    int kalman_crossings_;
    double max_uncertainty_;
    double min_uncertainty_;

    // Volatility tracking
    double recent_high_;
    double recent_low_;
    long last_vol_reset_seconds_;

    // Time tracking
    long last_tick_seconds_;
    double dt_;

    // Parse "YYYY.MM.DD HH:MM:SS.mmm" to seconds since reference
    static long ParseTimestampToSeconds(const std::string& ts) {
        if (ts.size() < 19) return 0;
        int year = std::stoi(ts.substr(0, 4));
        int month = std::stoi(ts.substr(5, 2));
        int day = std::stoi(ts.substr(8, 2));
        int hour = std::stoi(ts.substr(11, 2));
        int minute = std::stoi(ts.substr(14, 2));
        int second = std::stoi(ts.substr(17, 2));
        int month_days[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
        long days = (long)(year - 2020) * 365 + (year - 2020) / 4;
        days += month_days[month - 1] + day;
        if (month > 2 && year % 4 == 0) days++;
        return days * 86400L + hour * 3600L + minute * 60L + second;
    }

    /**
     * Kalman Filter Update (simplified 2D model: price + velocity)
     *
     * State: x = [price, velocity]
     * State transition: price_new = price + velocity * dt
     *                   velocity_new = velocity (constant velocity model)
     *
     * Process noise: Q scales with dt
     * Measurement: z = observed_price (noisy)
     */
    void KalmanUpdate(double measurement) {
        // === PREDICTION STEP ===
        // State transition matrix A = [[1, dt], [0, 1]]
        double x_pred_price = x_price_ + x_velocity_ * dt_;
        double x_pred_velocity = x_velocity_;

        // Covariance prediction: P_pred = A * P * A^T + Q
        // Simplified: process noise adds to both price and velocity variance
        double q_scale = dt_ * dt_;  // Scale Q with dt^2
        double q = config_.process_noise_q * q_scale;
        double q_vel = config_.process_noise_q * dt_;

        double p_pred_price = p_price_ + 2.0 * dt_ * p_price_velocity_ + dt_ * dt_ * p_velocity_ + q;
        double p_pred_velocity = p_velocity_ + q_vel;
        double p_pred_cross = p_price_velocity_ + dt_ * p_velocity_;

        // === UPDATE STEP ===
        // Measurement matrix H = [1, 0] (we observe only price)
        // Innovation: y = z - H * x_pred
        double innovation = measurement - x_pred_price;

        // Innovation covariance: S = H * P_pred * H^T + R = p_pred_price + R
        double S = p_pred_price + config_.measurement_noise_r;

        // Kalman gain: K = P_pred * H^T * S^-1
        double K_price = p_pred_price / S;
        double K_velocity = p_pred_cross / S;

        // State update: x = x_pred + K * y
        x_price_ = x_pred_price + K_price * innovation;
        x_velocity_ = x_pred_velocity + K_velocity * innovation;

        // Covariance update: P = (I - K * H) * P_pred
        p_price_ = (1.0 - K_price) * p_pred_price;
        p_velocity_ = p_pred_velocity - K_velocity * p_pred_cross;
        p_price_velocity_ = (1.0 - K_price) * p_pred_cross;

        // Ensure covariance stays positive (numerical stability)
        p_price_ = std::max(0.0001, p_price_);
        p_velocity_ = std::max(0.0001, p_velocity_);

        // Track uncertainty statistics
        double uncertainty = std::sqrt(p_price_);
        max_uncertainty_ = std::max(max_uncertainty_, uncertainty);
        if (uncertainty > 0) {
            min_uncertainty_ = std::min(min_uncertainty_, uncertainty);
        }
    }

    void TrackCrossings(double prev_estimate) {
        // Detect when price crosses the Kalman estimate
        bool was_above = current_bid_ > prev_estimate;
        bool now_above = current_bid_ > x_price_;
        if (was_above != now_above) {
            kalman_crossings_++;
        }
    }

    void UpdateVolatility(const Tick& tick) {
        long current_seconds = ParseTimestampToSeconds(tick.timestamp);
        long lookback_seconds = (long)(config_.volatility_lookback_hours * 3600.0);

        if (last_vol_reset_seconds_ == 0 ||
            current_seconds - last_vol_reset_seconds_ >= lookback_seconds) {
            recent_high_ = current_bid_;
            recent_low_ = current_bid_;
            last_vol_reset_seconds_ = current_seconds;
        }
        recent_high_ = std::max(recent_high_, current_bid_);
        recent_low_ = std::min(recent_low_, current_bid_);
    }

    void UpdateAdaptiveSpacing() {
        double range = recent_high_ - recent_low_;
        if (range > 0 && recent_high_ > 0 && current_bid_ > 0) {
            double typical_vol = current_bid_ * (config_.typical_vol_pct / 100.0);
            double vol_ratio = range / typical_vol;
            vol_ratio = std::max(0.5, std::min(3.0, vol_ratio));

            double new_spacing = config_.base_spacing * vol_ratio;
            new_spacing = std::max(0.5, std::min(5.0, new_spacing));

            if (std::abs(new_spacing - current_spacing_) > 0.1) {
                current_spacing_ = new_spacing;
            }
        }
    }

    void ApplyUncertaintyScaling() {
        // Scale spacing based on Kalman uncertainty
        // High uncertainty = wider spacing (less confident in estimate)
        // Low uncertainty = tighter spacing (more confident)
        double uncertainty = std::sqrt(p_price_);

        // Normalize uncertainty relative to typical (start with measurement noise as reference)
        double typical_uncertainty = std::sqrt(config_.measurement_noise_r);
        double uncertainty_ratio = uncertainty / typical_uncertainty;

        // Apply scaling: ratio > 1 = wider, ratio < 1 = tighter
        double scale = 1.0 + (uncertainty_ratio - 1.0) * config_.uncertainty_scale_factor;
        scale = std::max(0.5, std::min(2.0, scale));

        current_spacing_ = current_spacing_ * scale;
        current_spacing_ = std::max(0.5, std::min(5.0, current_spacing_));
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
            ? current_ask_ * ((100.0 - config_.survive_pct) / 100.0)
            : highest_buy_ * ((100.0 - config_.survive_pct) / 100.0);

        double distance = current_ask_ - end_price;
        double number_of_trades = std::floor(distance / current_spacing_);
        if (number_of_trades <= 0) number_of_trades = 1;

        double equity_at_target = current_equity_ - volume_of_open_trades_ * distance * cfg.contract_size;
        if (used_margin > 0 && (equity_at_target / used_margin * 100.0) < margin_stop_out) {
            return 0.0;
        }

        double trade_size = config_.min_volume;
        double d_equity = cfg.contract_size * trade_size * current_spacing_ *
                          (number_of_trades * (number_of_trades + 1) / 2);
        double d_margin = engine.CalculateMarginRequired(trade_size, current_ask_) * number_of_trades;

        double max_mult = config_.max_volume / config_.min_volume;
        for (double mult = max_mult; mult >= 1.0; mult -= 0.1) {
            double test_equity = equity_at_target - mult * d_equity;
            double test_margin = used_margin + mult * d_margin;
            if (test_margin > 0 && (test_equity / test_margin * 100.0) > margin_stop_out) {
                trade_size = mult * config_.min_volume;
                break;
            }
        }

        return std::min(trade_size, config_.max_volume);
    }

    bool Open(double lots, double tp, TickBasedEngine& engine) {
        if (lots < config_.min_volume) return false;

        double final_lots = engine.NormalizeLots(std::min(lots, config_.max_volume));

        Trade* trade = engine.OpenMarketOrder("BUY", final_lots, 0.0, tp);
        return (trade != nullptr);
    }

    void OpenNew(TickBasedEngine& engine) {
        int positions_total = (int)engine.GetOpenPositions().size();

        // Calculate deviation from Kalman estimate
        double deviation = current_bid_ - x_price_;

        // KALMAN ATTRACTOR LOGIC:
        // - Buy when price is BELOW Kalman estimate by spacing (dip below "true" level)
        // - Set TP back toward (or above) the Kalman estimate

        if (positions_total == 0) {
            // First position: buy if price is below Kalman estimate by at least half spacing
            if (deviation < -current_spacing_ * 0.5) {
                double lots = CalculateLotSize(engine, positions_total);
                // TP targets the Kalman estimate plus spacing * multiplier
                double tp = x_price_ + current_spread_ + current_spacing_ * config_.tp_multiplier;
                if (Open(lots, tp, engine)) {
                    highest_buy_ = current_ask_;
                    lowest_buy_ = current_ask_;
                }
            }
        } else {
            // Subsequent positions: add when price drops further below lowest buy
            if (lowest_buy_ >= current_ask_ + current_spacing_) {
                // Standard grid: add position when price drops by spacing
                double lots = CalculateLotSize(engine, positions_total);
                // TP targets the Kalman estimate or above
                double tp_target = std::max(x_price_, current_ask_ + current_spacing_);
                double tp = tp_target + current_spread_ + current_spacing_ * (config_.tp_multiplier - 1.0);
                if (Open(lots, tp, engine)) {
                    lowest_buy_ = current_ask_;
                }
            } else if (highest_buy_ <= current_ask_ - current_spacing_) {
                // Price moved up: add position only if still below Kalman estimate
                if (deviation < 0) {
                    double lots = CalculateLotSize(engine, positions_total);
                    double tp = x_price_ + current_spread_ + current_spacing_ * config_.tp_multiplier;
                    if (Open(lots, tp, engine)) {
                        highest_buy_ = current_ask_;
                    }
                }
            }
        }
    }
};

} // namespace backtest

#endif // STRATEGY_KALMAN_FILTER_H
