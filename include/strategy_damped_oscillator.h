#ifndef STRATEGY_DAMPED_OSCILLATOR_H
#define STRATEGY_DAMPED_OSCILLATOR_H

#include "tick_based_engine.h"
#include <deque>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace backtest {

/**
 * Damped Oscillator Energy Extraction Strategy
 *
 * Models the market as a damped harmonic oscillator where:
 * - Positions store energy (unrealized P/L = potential energy)
 * - Price oscillations transfer energy between kinetic and potential forms
 * - The strategy extracts energy at velocity zero-crossings (reversal points)
 *
 * Key physics insight: Energy extraction is maximized at the velocity
 * zero-crossings (price reversal points), not at displacement extremes.
 *
 * The strategy:
 * 1. Calculates price velocity (rate of change)
 * 2. Detects velocity zero-crossings (down→up = buy signal)
 * 3. Enters at the point of maximum potential energy (local minimum)
 * 4. Exits when kinetic energy peaks (velocity maximum = momentum exhaustion)
 *
 * Best on: XAUUSD (highest oscillation frequency, 281k/year, tight spread)
 */
class DampedOscillator {
public:
    struct Config {
        int velocity_window = 20;           // Ticks for velocity calculation
        int acceleration_window = 10;       // Ticks for acceleration (2nd derivative)
        double min_velocity = 0.02;         // Min velocity magnitude for signal ($0.02/tick)
        double tp_amplitude = 1.50;         // Expected oscillation amplitude for TP
        double lot_size = 0.02;             // Position size
        double max_lots = 0.30;             // Maximum total exposure
        int cooldown_ticks = 100;           // Min ticks between entries
        int max_positions = 15;             // Max concurrent positions
        int warmup_ticks = 500;
        double energy_harvest_ratio = 0.7;  // Close when 70% of amplitude captured
    };

    DampedOscillator(const Config& cfg)
        : config_(cfg),
          ticks_processed_(0),
          zero_crossings_(0),
          entries_(0),
          energy_harvests_(0),
          last_entry_tick_(0),
          prev_velocity_(0.0),
          peak_equity_(0.0),
          max_dd_pct_(0.0) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        ticks_processed_++;

        prices_.push_back(tick.bid);
        if (prices_.size() > 1000) prices_.pop_front();

        double equity = engine.GetEquity();
        if (peak_equity_ == 0.0) peak_equity_ = equity;
        if (equity > peak_equity_) peak_equity_ = equity;
        double dd = (peak_equity_ - equity) / peak_equity_ * 100.0;
        if (dd > max_dd_pct_) max_dd_pct_ = dd;

        if ((int)prices_.size() < config_.velocity_window + config_.acceleration_window) return;
        if (ticks_processed_ < config_.warmup_ticks) return;

        // Calculate velocity (first derivative of price)
        double velocity = CalculateVelocity();

        // Calculate acceleration (second derivative)
        double acceleration = CalculateAcceleration();

        // Detect velocity zero-crossing (down→up = reversal from local minimum)
        bool zero_crossing_up = (prev_velocity_ < -config_.min_velocity && velocity >= 0);
        bool zero_crossing_down = (prev_velocity_ > config_.min_velocity && velocity <= 0);

        if (zero_crossing_up) zero_crossings_++;

        // Entry: velocity zero-crossing from negative to positive (local minimum)
        int open_count = (int)engine.GetOpenPositions().size();
        bool can_enter = (open_count < config_.max_positions &&
                         ticks_processed_ - last_entry_tick_ >= config_.cooldown_ticks);

        if (zero_crossing_up && can_enter && acceleration > 0) {
            // Entering at local minimum (maximum potential energy point)
            double tp = tick.ask + config_.tp_amplitude;
            engine.OpenMarketOrder("BUY", config_.lot_size, 0, tp);
            entries_++;
            last_entry_tick_ = ticks_processed_;
        }

        // Energy harvest: close positions at velocity zero-crossing from positive to negative
        if (zero_crossing_down && !engine.GetOpenPositions().empty()) {
            HarvestEnergy(tick, engine);
        }

        prev_velocity_ = velocity;
    }

    int GetZeroCrossings() const { return zero_crossings_; }
    int GetEntries() const { return entries_; }
    int GetEnergyHarvests() const { return energy_harvests_; }
    double GetMaxDDPct() const { return max_dd_pct_; }

private:
    Config config_;
    int ticks_processed_;
    int zero_crossings_;
    int entries_;
    int energy_harvests_;
    int last_entry_tick_;
    double prev_velocity_;
    double peak_equity_;
    double max_dd_pct_;
    std::deque<double> prices_;

    double CalculateVelocity() {
        // Linear regression slope over velocity_window ticks
        int n = config_.velocity_window;
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

        double slope = (n * sum_xy - sum_x * sum_y) / (n * sum_x2 - sum_x * sum_x);
        return slope;  // Price change per tick
    }

    double CalculateAcceleration() {
        // Second derivative: change in velocity
        int n = config_.acceleration_window;
        if (prices_.size() < (size_t)(config_.velocity_window + n + 5)) return 0;

        // Velocity now vs velocity n ticks ago
        double vel_now = CalculateVelocityAt(prices_.size() - 1);
        double vel_prev = CalculateVelocityAt(prices_.size() - 1 - n);

        return (vel_now - vel_prev) / n;
    }

    double CalculateVelocityAt(size_t end_idx) {
        int n = std::min(config_.velocity_window, (int)(end_idx + 1));
        if (n < 5) return 0;
        size_t start = end_idx - n + 1;

        double sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0;
        for (int i = 0; i < n; i++) {
            double x = i;
            double y = prices_[start + i];
            sum_x += x;
            sum_y += y;
            sum_xy += x * y;
            sum_x2 += x * x;
        }

        return (n * sum_xy - sum_x * sum_y) / (n * sum_x2 - sum_x * sum_x);
    }

    void HarvestEnergy(const Tick& tick, TickBasedEngine& engine) {
        // At velocity zero-crossing (up→down), close profitable positions
        // This is the point where kinetic energy is at maximum
        auto positions = engine.GetOpenPositions();
        for (int i = (int)positions.size() - 1; i >= 0; i--) {
            Trade* t = positions[i];
            if (t->IsBuy()) {
                double cs = engine.GetConfig().contract_size;
                double pnl = (tick.bid - t->entry_price) * t->lot_size * cs;
                double expected_max = config_.tp_amplitude * t->lot_size * cs;
                // Harvest if captured significant portion of expected amplitude
                if (pnl >= expected_max * config_.energy_harvest_ratio) {
                    engine.ClosePosition(t, "ENERGY_HARVEST");
                    energy_harvests_++;
                }
            }
        }
    }
};

} // namespace backtest

#endif // STRATEGY_DAMPED_OSCILLATOR_H
