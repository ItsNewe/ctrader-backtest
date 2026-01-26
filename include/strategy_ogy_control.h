#ifndef STRATEGY_OGY_CONTROL_H
#define STRATEGY_OGY_CONTROL_H

#include "tick_based_engine.h"
#include <deque>
#include <cmath>
#include <random>

namespace backtest {

/**
 * OGY (Ott-Grebogi-Yorke) Control Strategy
 *
 * Concept: The OGY method shows that chaotic systems can be controlled with
 * tiny, well-timed perturbations near unstable equilibria. Instead of large
 * position changes, make minimal adjustments at key moments. The market's
 * own dynamics amplify the effect.
 *
 * Unstable equilibria in markets:
 *   1. VELOCITY_ZERO: Price velocity crosses zero (local min/max)
 *   2. LOCAL_MINMAX: Price at N-period high/low
 *   3. BOTH: Either condition triggers entry
 *
 * The strategy:
 *   1. Track price velocity using a rolling window
 *   2. Detect "unstable equilibria" when velocity crosses zero
 *   3. Make TINY position adjustments (0.01 lots) at these moments
 *   4. Let chaos amplify small correct moves via TP
 *
 * Key test: Compare well-timed tiny entries vs random-timed tiny entries
 */
class StrategyOGYControl {
public:
    enum EquilibriumType {
        VELOCITY_ZERO,    // Entry when velocity crosses zero
        LOCAL_MINMAX,     // Entry at N-period high/low
        BOTH              // Either condition
    };

    struct Config {
        int velocity_window = 100;         // Ticks for velocity calculation
        double lot_size = 0.01;            // TINY lot size (OGY = small perturbation)
        double tp_distance = 1.0;          // Take profit distance in $
        EquilibriumType equilibrium_type = VELOCITY_ZERO;
        int local_minmax_window = 100;     // Window for local min/max detection
        int cooldown_ticks = 50;           // Min ticks between entries
        int max_positions = 50;            // Max concurrent positions
        double contract_size = 100.0;
        double leverage = 500.0;
        int warmup_ticks = 500;            // Warmup before trading
        double min_velocity_threshold = 0.001;  // Minimum velocity to consider ($/tick)

        // Random baseline mode
        bool random_mode = false;          // If true, ignore equilibria and enter randomly
        double random_entry_prob = 0.001;  // Probability of random entry per tick
        unsigned int random_seed = 42;     // Random seed for reproducibility
    };

    StrategyOGYControl(const Config& cfg)
        : config_(cfg),
          ticks_processed_(0),
          equilibria_detected_(0),
          entries_(0),
          last_entry_tick_(0),
          prev_velocity_(0.0),
          peak_equity_(0.0),
          max_dd_pct_(0.0),
          rng_(cfg.random_seed),
          random_dist_(0.0, 1.0) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        ticks_processed_++;

        // Track prices for velocity calculation
        prices_.push_back(tick.bid);
        if (prices_.size() > 2000) prices_.pop_front();

        // Track equity and drawdown
        double equity = engine.GetEquity();
        if (peak_equity_ == 0.0) peak_equity_ = equity;
        if (equity > peak_equity_) peak_equity_ = equity;
        double dd = (peak_equity_ - equity) / peak_equity_ * 100.0;
        if (dd > max_dd_pct_) max_dd_pct_ = dd;

        // Warmup period
        if (ticks_processed_ < config_.warmup_ticks) return;
        if ((int)prices_.size() < config_.velocity_window + 10) return;

        int open_count = (int)engine.GetOpenPositions().size();
        bool can_enter = (open_count < config_.max_positions &&
                         ticks_processed_ - last_entry_tick_ >= config_.cooldown_ticks);

        if (!can_enter) return;

        // Random baseline mode
        if (config_.random_mode) {
            if (random_dist_(rng_) < config_.random_entry_prob) {
                // Random entry direction based on velocity
                double velocity = CalculateVelocity();
                if (velocity >= 0) {
                    // Random buy at local minimum guess
                    double tp = tick.ask + config_.tp_distance;
                    engine.OpenMarketOrder("BUY", config_.lot_size, 0, tp);
                } else {
                    // Random sell at local maximum guess
                    double tp = tick.bid - config_.tp_distance;
                    engine.OpenMarketOrder("SELL", config_.lot_size, 0, tp);
                }
                entries_++;
                last_entry_tick_ = ticks_processed_;
            }
            prev_velocity_ = CalculateVelocity();
            return;
        }

        // Calculate velocity (rate of change)
        double velocity = CalculateVelocity();

        // Detect equilibrium conditions
        bool at_equilibrium = false;
        bool is_local_min = false;
        bool is_local_max = false;

        if (config_.equilibrium_type == VELOCITY_ZERO ||
            config_.equilibrium_type == BOTH) {
            // Velocity zero-crossing detection
            // Down-to-up crossing = local minimum = buy signal
            // Up-to-down crossing = local maximum = sell signal
            if (prev_velocity_ < -config_.min_velocity_threshold && velocity >= 0) {
                at_equilibrium = true;
                is_local_min = true;
            }
            if (prev_velocity_ > config_.min_velocity_threshold && velocity <= 0) {
                at_equilibrium = true;
                is_local_max = true;
            }
        }

        if (config_.equilibrium_type == LOCAL_MINMAX ||
            config_.equilibrium_type == BOTH) {
            // Local min/max detection using price window
            if (IsLocalMinimum(tick.bid)) {
                at_equilibrium = true;
                is_local_min = true;
            }
            if (IsLocalMaximum(tick.bid)) {
                at_equilibrium = true;
                is_local_max = true;
            }
        }

        if (at_equilibrium) {
            equilibria_detected_++;

            // OGY: Small perturbation at the right moment
            if (is_local_min) {
                // Buy at local minimum - expect bounce up
                double tp = tick.ask + config_.tp_distance;
                engine.OpenMarketOrder("BUY", config_.lot_size, 0, tp);
                entries_++;
                last_entry_tick_ = ticks_processed_;
            } else if (is_local_max) {
                // Sell at local maximum - expect drop down
                double tp = tick.bid - config_.tp_distance;
                engine.OpenMarketOrder("SELL", config_.lot_size, 0, tp);
                entries_++;
                last_entry_tick_ = ticks_processed_;
            }
        }

        prev_velocity_ = velocity;
    }

    // Getters for analysis
    int GetTicksProcessed() const { return ticks_processed_; }
    int GetEquilibriaDetected() const { return equilibria_detected_; }
    int GetEntries() const { return entries_; }
    double GetMaxDDPct() const { return max_dd_pct_; }

private:
    Config config_;
    int ticks_processed_;
    int equilibria_detected_;
    int entries_;
    int last_entry_tick_;
    double prev_velocity_;
    double peak_equity_;
    double max_dd_pct_;
    std::deque<double> prices_;

    // Random number generator for baseline
    std::mt19937 rng_;
    std::uniform_real_distribution<double> random_dist_;

    double CalculateVelocity() {
        // Linear regression slope over velocity_window ticks
        int n = config_.velocity_window;
        if ((int)prices_.size() < n) return 0.0;

        size_t start = prices_.size() - n;

        double sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0;
        for (int i = 0; i < n; i++) {
            double x = i;
            double y = prices_[start + i];
            sum_x += x;
            sum_y += y;
            sum_xy += x * y;
            sum_x2 += x * x;
        }

        double denom = n * sum_x2 - sum_x * sum_x;
        if (std::abs(denom) < 1e-10) return 0.0;

        double slope = (n * sum_xy - sum_x * sum_y) / denom;
        return slope;  // Price change per tick
    }

    bool IsLocalMinimum(double current_price) {
        int window = config_.local_minmax_window;
        if ((int)prices_.size() < window) return false;

        // Check if current price is lowest in window
        for (int i = 1; i < window; i++) {
            if (prices_[prices_.size() - 1 - i] < current_price) {
                return false;
            }
        }
        return true;
    }

    bool IsLocalMaximum(double current_price) {
        int window = config_.local_minmax_window;
        if ((int)prices_.size() < window) return false;

        // Check if current price is highest in window
        for (int i = 1; i < window; i++) {
            if (prices_[prices_.size() - 1 - i] > current_price) {
                return false;
            }
        }
        return true;
    }
};

} // namespace backtest

#endif // STRATEGY_OGY_CONTROL_H
