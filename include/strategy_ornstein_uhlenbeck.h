#ifndef STRATEGY_ORNSTEIN_UHLENBECK_H
#define STRATEGY_ORNSTEIN_UHLENBECK_H

#include "tick_based_engine.h"
#include <vector>
#include <deque>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <numeric>

namespace backtest {

/**
 * Ornstein-Uhlenbeck Mean-Reversion Strategy
 *
 * The OU process: dX = theta(mu - X)dt + sigma*dW
 *
 * Key insight: theta (mean-reversion speed) tells us how fast price reverts.
 * - High theta = fast reversion = tighter spacing profitable
 * - Low theta = slow reversion = need wider spacing
 *
 * Implementation:
 * 1. Estimate OU parameters from rolling window of prices
 * 2. Use theta to dynamically adjust grid spacing
 * 3. Spacing = base_spacing / theta_ratio where theta_ratio = current_theta / typical_theta
 */
class StrategyOrnsteinUhlenbeck {
public:
    struct OUConfig {
        int estimation_window;      // Number of ticks for OU estimation
        double theta_scaling;       // How aggressively to adjust spacing (1.0 = normal)
        double min_theta_mult;      // Floor for spacing multiplier (e.g., 0.3)
        double max_theta_mult;      // Ceiling for spacing multiplier (e.g., 3.0)
        double base_spacing;        // Base grid spacing in dollars
        double survive_pct;         // Survive percentage for grid
        double typical_theta;       // Reference theta value for normalization

        OUConfig()
            : estimation_window(1000),
              theta_scaling(1.0),
              min_theta_mult(0.3),
              max_theta_mult(3.0),
              base_spacing(1.5),
              survive_pct(13.0),
              typical_theta(0.001) {}  // Will be calibrated from data
    };

    // Statistics for analysis
    struct OUStats {
        double current_theta;
        double current_mu;
        double current_sigma;
        double current_spacing;
        int theta_updates;
        double min_theta_seen;
        double max_theta_seen;
        double avg_theta;
        double theta_sum;
        int theta_count;
        int spacing_changes;
    };

    StrategyOrnsteinUhlenbeck(const OUConfig& config = OUConfig())
        : config_(config),
          lowest_buy_(DBL_MAX),
          highest_buy_(DBL_MIN),
          volume_of_open_trades_(0.0),
          current_ask_(0.0),
          current_bid_(0.0),
          current_spread_(0.0),
          current_equity_(0.0),
          current_spacing_(config.base_spacing),
          current_theta_(config.typical_theta),
          current_mu_(0.0),
          current_sigma_(0.0),
          warmup_complete_(false),
          stats_({0.0, 0.0, 0.0, config.base_spacing, 0, DBL_MAX, DBL_MIN, 0.0, 0.0, 0, 0}),
          ticks_processed_(0),
          last_estimation_tick_(0)
    {
        stats_.current_spacing = config.base_spacing;
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_spread_ = tick.spread();
        current_equity_ = engine.GetEquity();
        ticks_processed_++;

        // Collect price data
        price_history_.push_back(current_bid_);
        if ((int)price_history_.size() > config_.estimation_window * 2) {
            price_history_.pop_front();
        }

        // Initialize spacing from first price if needed
        if (current_spacing_ == 0.0) {
            current_spacing_ = config_.base_spacing;
            stats_.current_spacing = current_spacing_;
        }

        // Update OU parameters periodically (every estimation_window/10 ticks after warmup)
        int update_interval = std::max(1, config_.estimation_window / 10);
        if ((int)price_history_.size() >= config_.estimation_window &&
            (ticks_processed_ - last_estimation_tick_) >= update_interval) {

            EstimateOUParameters();
            UpdateSpacing();
            last_estimation_tick_ = ticks_processed_;
            warmup_complete_ = true;
        }

        // Process existing positions
        Iterate(engine);

        // Open new positions (only after warmup if using OU adjustment)
        OpenNew(engine);
    }

    // Accessors
    double GetCurrentSpacing() const { return current_spacing_; }
    double GetCurrentTheta() const { return current_theta_; }
    double GetCurrentMu() const { return current_mu_; }
    double GetCurrentSigma() const { return current_sigma_; }
    bool IsWarmupComplete() const { return warmup_complete_; }
    const OUStats& GetStats() const { return stats_; }

private:
    OUConfig config_;

    // Position tracking
    double lowest_buy_;
    double highest_buy_;
    double volume_of_open_trades_;

    // Market state
    double current_ask_;
    double current_bid_;
    double current_spread_;
    double current_equity_;

    // OU model state
    double current_spacing_;
    double current_theta_;
    double current_mu_;
    double current_sigma_;
    bool warmup_complete_;

    // Data collection
    std::deque<double> price_history_;

    // Statistics
    OUStats stats_;
    long ticks_processed_;
    long last_estimation_tick_;

    /**
     * Estimate OU parameters using the autocorrelation method
     *
     * For discrete samples: X_{t+dt} - X_t = theta*(mu - X_t)*dt + sigma*sqrt(dt)*Z
     *
     * Autocorrelation method:
     * - rho_1 = autocorrelation at lag 1
     * - theta = -ln(rho_1) / dt
     * - mu = mean of X
     * - sigma = std of residuals
     */
    void EstimateOUParameters() {
        if ((int)price_history_.size() < config_.estimation_window) return;

        // Get the most recent window of prices
        int n = config_.estimation_window;
        std::vector<double> prices(price_history_.end() - n, price_history_.end());

        // Calculate mean
        double sum = 0.0;
        for (double p : prices) sum += p;
        double mean = sum / n;
        current_mu_ = mean;

        // Calculate variance
        double var_sum = 0.0;
        for (double p : prices) {
            double d = p - mean;
            var_sum += d * d;
        }
        double variance = var_sum / (n - 1);

        // Calculate lag-1 autocorrelation
        // rho_1 = Cov(X_t, X_{t+1}) / Var(X)
        double cov_sum = 0.0;
        for (int i = 0; i < n - 1; i++) {
            cov_sum += (prices[i] - mean) * (prices[i+1] - mean);
        }
        double covariance = cov_sum / (n - 2);

        double rho1 = (variance > 0) ? covariance / variance : 0.0;

        // Clamp rho1 to valid range (0, 1) for log calculation
        // rho1 should be positive for mean-reverting process
        rho1 = std::max(0.001, std::min(0.999, rho1));

        // Estimate theta: theta = -ln(rho_1) / dt
        // dt = 1 tick (our time unit)
        // For tick data, we want theta per tick, but we'll convert to a normalized scale
        current_theta_ = -std::log(rho1);

        // Estimate sigma from residuals
        // In the OU process, sigma^2 = 2*theta*variance (at stationarity)
        double sigma_sq = 2.0 * current_theta_ * variance;
        current_sigma_ = std::sqrt(std::max(0.0, sigma_sq));

        // Update statistics
        stats_.current_theta = current_theta_;
        stats_.current_mu = current_mu_;
        stats_.current_sigma = current_sigma_;
        stats_.theta_updates++;

        if (current_theta_ < stats_.min_theta_seen) stats_.min_theta_seen = current_theta_;
        if (current_theta_ > stats_.max_theta_seen) stats_.max_theta_seen = current_theta_;

        stats_.theta_sum += current_theta_;
        stats_.theta_count++;
        stats_.avg_theta = stats_.theta_sum / stats_.theta_count;
    }

    void UpdateSpacing() {
        if (!warmup_complete_ && stats_.theta_count < 10) {
            // During initial warmup, use base spacing
            return;
        }

        // Calculate theta ratio: current_theta / typical_theta
        // Use running average as typical_theta if not specified
        double typical = (config_.typical_theta > 0) ? config_.typical_theta : stats_.avg_theta;
        if (typical <= 0) typical = 0.001;

        double theta_ratio = current_theta_ / typical;

        // Apply scaling factor
        // Higher theta_scaling = more aggressive adjustment
        theta_ratio = 1.0 + (theta_ratio - 1.0) * config_.theta_scaling;

        // Clamp the ratio
        theta_ratio = std::max(config_.min_theta_mult, std::min(config_.max_theta_mult, theta_ratio));

        // Spacing = base_spacing / theta_ratio
        // High theta (fast reversion) -> lower spacing (tighter grid)
        // Low theta (slow reversion) -> higher spacing (wider grid)
        double new_spacing = config_.base_spacing / theta_ratio;

        // Apply absolute clamps
        new_spacing = std::max(0.20, std::min(10.0, new_spacing));

        // Only update if changed significantly (>5%)
        if (std::abs(new_spacing - current_spacing_) / current_spacing_ > 0.05) {
            current_spacing_ = new_spacing;
            stats_.current_spacing = current_spacing_;
            stats_.spacing_changes++;
        }
    }

    void Iterate(TickBasedEngine& engine) {
        lowest_buy_ = DBL_MAX;
        highest_buy_ = DBL_MIN;
        volume_of_open_trades_ = 0.0;

        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->direction == "BUY") {
                volume_of_open_trades_ += trade->lot_size;
                lowest_buy_ = std::min(lowest_buy_, trade->entry_price);
                highest_buy_ = std::max(highest_buy_, trade->entry_price);
            }
        }
    }

    double CalculateLotSize(TickBasedEngine& engine, int positions_total) {
        double used_margin = 0.0;
        double contract_size = 100.0;  // XAUUSD
        double leverage = 500.0;
        double min_volume = 0.01;
        double max_volume = 10.0;

        for (const Trade* trade : engine.GetOpenPositions()) {
            used_margin += trade->lot_size * contract_size * trade->entry_price / leverage;
        }

        double margin_stop_out = 20.0;
        double margin_level = (used_margin > 0) ? (current_equity_ / used_margin * 100.0) : 10000.0;

        double end_price = (positions_total == 0)
            ? current_ask_ * ((100.0 - config_.survive_pct) / 100.0)
            : highest_buy_ * ((100.0 - config_.survive_pct) / 100.0);

        double distance = current_ask_ - end_price;
        double number_of_trades = std::floor(distance / current_spacing_);
        if (number_of_trades <= 0) number_of_trades = 1;

        double equity_at_target = current_equity_ - volume_of_open_trades_ * distance * contract_size;
        if (used_margin > 0 && (equity_at_target / used_margin * 100.0) < margin_stop_out) {
            return 0.0;
        }

        double trade_size = min_volume;
        double d_equity = contract_size * trade_size * current_spacing_ * (number_of_trades * (number_of_trades + 1) / 2);
        double d_margin = number_of_trades * trade_size * contract_size / leverage;

        double max_mult = max_volume / min_volume;
        for (double mult = max_mult; mult >= 1.0; mult -= 0.1) {
            double test_equity = equity_at_target - mult * d_equity;
            double test_margin = used_margin + mult * d_margin;
            if (test_margin > 0 && (test_equity / test_margin * 100.0) > margin_stop_out) {
                trade_size = mult * min_volume;
                break;
            }
        }

        return std::min(trade_size, max_volume);
    }

    bool Open(double lots, TickBasedEngine& engine) {
        if (lots < 0.01) return false;

        double final_lots = std::min(lots, 10.0);
        final_lots = std::round(final_lots * 100.0) / 100.0;

        double tp = current_ask_ + current_spread_ + current_spacing_;
        Trade* trade = engine.OpenMarketOrder("BUY", final_lots, 0.0, tp);
        return (trade != nullptr);
    }

    void OpenNew(TickBasedEngine& engine) {
        int positions_total = engine.GetOpenPositions().size();

        if (positions_total == 0) {
            double lots = CalculateLotSize(engine, positions_total);
            if (Open(lots, engine)) {
                highest_buy_ = current_ask_;
                lowest_buy_ = current_ask_;
            }
        } else {
            if (lowest_buy_ >= current_ask_ + current_spacing_) {
                double lots = CalculateLotSize(engine, positions_total);
                if (Open(lots, engine)) {
                    lowest_buy_ = current_ask_;
                }
            } else if (highest_buy_ <= current_ask_ - current_spacing_) {
                double lots = CalculateLotSize(engine, positions_total);
                if (Open(lots, engine)) {
                    highest_buy_ = current_ask_;
                }
            }
        }
    }
};

} // namespace backtest

#endif // STRATEGY_ORNSTEIN_UHLENBECK_H
