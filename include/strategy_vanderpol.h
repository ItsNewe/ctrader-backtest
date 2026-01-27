#ifndef STRATEGY_VANDERPOL_H
#define STRATEGY_VANDERPOL_H

#include "tick_based_engine.h"
#include <deque>
#include <cmath>
#include <algorithm>
#include <numeric>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace backtest {

/**
 * Van der Pol Oscillator Phase-Based Trading Strategy
 *
 * Models price as a nonlinear oscillator with stable limit cycles.
 * The key insight is that we can estimate the PHASE of oscillation:
 *
 *   Phase 0deg (equilibrium, rising): Price at mean, moving up
 *   Phase 90deg (peak): Price at local maximum
 *   Phase 180deg (equilibrium, falling): Price at mean, moving down
 *   Phase 270deg (trough): Price at local minimum
 *
 * Optimal trading:
 *   - BUY when phase is near 270deg (trough)
 *   - SELL/TP when phase is near 90deg (peak)
 *
 * This is more sophisticated than simple velocity zero-crossing because
 * it tracks the full oscillation cycle via phase portrait analysis.
 *
 * Implementation:
 *   1. Track deviation from moving average: x = price - MA
 *   2. Track velocity: v = dx/dt (rate of change)
 *   3. Normalize both by typical amplitude and velocity
 *   4. Calculate phase: phase = atan2(v_norm, x_norm)
 *   5. Enter at trough phase, exit at peak phase
 */
class StrategyVanDerPol {
public:
    struct Config {
        // MA and phase calculation
        int ma_period = 500;                 // Ticks for moving average
        int velocity_smoothing = 50;         // Ticks for velocity smoothing
        int amplitude_lookback = 1000;       // Ticks for amplitude normalization

        // Entry zone (phase in degrees)
        double entry_phase_center = 270.0;   // Target phase for entry (270 = trough)
        double entry_phase_width = 60.0;     // +/- degrees from center

        // Exit zone (phase in degrees)
        double exit_phase_center = 90.0;     // Target phase for exit (90 = peak)
        double exit_phase_width = 60.0;      // +/- degrees from center

        // Position sizing
        double lot_size = 0.02;
        double max_lots = 0.50;
        double contract_size = 100.0;
        double leverage = 500.0;

        // Risk management
        double survive_pct = 13.0;           // Max allowed adverse move
        int cooldown_ticks = 200;            // Min ticks between entries
        int max_positions = 20;              // Max concurrent positions
        int warmup_ticks = 2000;             // Warmup for MA and amplitude

        // Fallback TP (if phase exit doesn't trigger)
        double fallback_tp = 1.50;           // Fixed TP as fallback
        bool use_phase_exit = true;          // Whether to use phase-based exit

        Config() = default;
    };

    StrategyVanDerPol(const Config& cfg)
        : config_(cfg),
          ticks_processed_(0),
          entries_(0),
          phase_exits_(0),
          tp_exits_(0),
          last_entry_tick_(0),
          prev_phase_(0.0),
          peak_equity_(0.0),
          max_dd_pct_(0.0),
          typical_amplitude_(1.0),
          typical_velocity_(0.001),
          current_phase_(0.0),
          current_x_(0.0),
          current_v_(0.0) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        ticks_processed_++;

        // Track price history
        prices_.push_back(tick.bid);
        if (prices_.size() > (size_t)(config_.amplitude_lookback + 100)) {
            prices_.pop_front();
        }

        // Track equity and drawdown
        double equity = engine.GetEquity();
        if (peak_equity_ == 0.0) peak_equity_ = equity;
        if (equity > peak_equity_) peak_equity_ = equity;
        double dd = (peak_equity_ - equity) / peak_equity_ * 100.0;
        if (dd > max_dd_pct_) max_dd_pct_ = dd;

        // Wait for warmup
        if (ticks_processed_ < config_.warmup_ticks) return;
        if ((int)prices_.size() < config_.ma_period + config_.velocity_smoothing) return;

        // Calculate moving average (equilibrium)
        double ma = CalculateMA();

        // Calculate deviation from equilibrium
        current_x_ = tick.bid - ma;

        // Calculate velocity (smoothed rate of change)
        current_v_ = CalculateVelocity();

        // Update amplitude and velocity normalization
        UpdateNormalization();

        // Calculate phase angle (normalized)
        current_phase_ = CalculatePhase();

        // Phase-based exit check (before entry)
        if (config_.use_phase_exit) {
            CheckPhaseExit(tick, engine);
        }

        // Entry check
        CheckEntry(tick, engine);

        prev_phase_ = current_phase_;
    }

    // Accessors
    int GetEntries() const { return entries_; }
    int GetPhaseExits() const { return phase_exits_; }
    int GetTPExits() const { return tp_exits_; }
    double GetMaxDDPct() const { return max_dd_pct_; }
    double GetCurrentPhase() const { return current_phase_; }
    double GetTypicalAmplitude() const { return typical_amplitude_; }
    double GetTypicalVelocity() const { return typical_velocity_; }

private:
    Config config_;
    int ticks_processed_;
    int entries_;
    int phase_exits_;
    int tp_exits_;
    int last_entry_tick_;
    double prev_phase_;
    double peak_equity_;
    double max_dd_pct_;
    double typical_amplitude_;
    double typical_velocity_;
    double current_phase_;
    double current_x_;
    double current_v_;
    std::deque<double> prices_;
    std::deque<double> amplitudes_;  // For tracking typical amplitude
    std::deque<double> velocities_;  // For tracking typical velocity

    double CalculateMA() {
        if (prices_.size() < (size_t)config_.ma_period) return prices_.back();

        double sum = 0.0;
        size_t start = prices_.size() - config_.ma_period;
        for (size_t i = start; i < prices_.size(); i++) {
            sum += prices_[i];
        }
        return sum / config_.ma_period;
    }

    double CalculateVelocity() {
        // Use linear regression slope for smoothed velocity
        int n = config_.velocity_smoothing;
        if (prices_.size() < (size_t)n) return 0.0;

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

        return (n * sum_xy - sum_x * sum_y) / denom;
    }

    void UpdateNormalization() {
        // Track amplitude (max deviation from MA over lookback)
        amplitudes_.push_back(std::abs(current_x_));
        if (amplitudes_.size() > (size_t)config_.amplitude_lookback) {
            amplitudes_.pop_front();
        }

        // Track velocity
        velocities_.push_back(std::abs(current_v_));
        if (velocities_.size() > (size_t)config_.amplitude_lookback) {
            velocities_.pop_front();
        }

        // Only recalculate percentiles every 1000 ticks (expensive operation)
        if (ticks_processed_ % 1000 != 0) return;

        // Update typical values (use 75th percentile for robustness)
        if (amplitudes_.size() >= 100) {
            std::vector<double> sorted_amp(amplitudes_.begin(), amplitudes_.end());
            std::sort(sorted_amp.begin(), sorted_amp.end());
            typical_amplitude_ = sorted_amp[sorted_amp.size() * 75 / 100];
            typical_amplitude_ = std::max(0.1, typical_amplitude_);  // Floor
        }

        if (velocities_.size() >= 100) {
            std::vector<double> sorted_vel(velocities_.begin(), velocities_.end());
            std::sort(sorted_vel.begin(), sorted_vel.end());
            typical_velocity_ = sorted_vel[sorted_vel.size() * 75 / 100];
            typical_velocity_ = std::max(0.0001, typical_velocity_);  // Floor
        }
    }

    double CalculatePhase() {
        // Normalize x and v to put them on similar scales
        double x_norm = current_x_ / typical_amplitude_;
        double v_norm = current_v_ / typical_velocity_;

        // Calculate phase using atan2
        // atan2(v, x) gives angle where:
        //   x>0, v=0 -> 0deg (at positive displacement, no velocity)
        //   x=0, v>0 -> 90deg (at equilibrium, moving up)
        //   x<0, v=0 -> 180deg (at negative displacement, no velocity)
        //   x=0, v<0 -> 270deg (at equilibrium, moving down)
        //
        // But for trading, we want:
        //   Phase 0deg = at equilibrium, rising
        //   Phase 90deg = at peak (x positive, v=0)
        //   Phase 180deg = at equilibrium, falling
        //   Phase 270deg = at trough (x negative, v=0)
        //
        // So we swap x and v: atan2(x, v)
        double phase_rad = std::atan2(x_norm, v_norm);

        // Convert to degrees (0-360)
        double phase_deg = phase_rad * 180.0 / M_PI;
        if (phase_deg < 0) phase_deg += 360.0;

        return phase_deg;
    }

    bool IsInPhaseZone(double phase, double center, double width) {
        // Check if phase is within center +/- width (handling wrap-around)
        double low = center - width;
        double high = center + width;

        if (low < 0) {
            // Zone wraps around 0
            return (phase >= low + 360.0) || (phase <= high);
        } else if (high > 360.0) {
            // Zone wraps around 360
            return (phase >= low) || (phase <= high - 360.0);
        } else {
            return (phase >= low) && (phase <= high);
        }
    }

    void CheckEntry(const Tick& tick, TickBasedEngine& engine) {
        int open_count = (int)engine.GetOpenPositions().size();

        // Check limits
        if (open_count >= config_.max_positions) return;
        if (ticks_processed_ - last_entry_tick_ < config_.cooldown_ticks) return;

        // Check if we're in the entry phase zone (trough)
        if (!IsInPhaseZone(current_phase_, config_.entry_phase_center, config_.entry_phase_width)) {
            return;
        }

        // Additional check: velocity should be near zero or turning positive
        // (we're at or near the trough)
        if (current_v_ > typical_velocity_ * 0.5) {
            // Already moving up significantly - might have missed the optimal entry
            return;
        }

        // Calculate TP
        double tp = tick.ask + tick.spread() + config_.fallback_tp;

        // Open position
        Trade* trade = engine.OpenMarketOrder("BUY", config_.lot_size, 0, tp);
        if (trade) {
            entries_++;
            last_entry_tick_ = ticks_processed_;
        }
    }

    void CheckPhaseExit(const Tick& tick, TickBasedEngine& engine) {
        // Check if we're in the exit phase zone (peak)
        if (!IsInPhaseZone(current_phase_, config_.exit_phase_center, config_.exit_phase_width)) {
            return;
        }

        // Close profitable positions at peak phase
        auto positions = engine.GetOpenPositions();
        for (int i = (int)positions.size() - 1; i >= 0; i--) {
            Trade* t = positions[i];
            if (t->direction == "BUY") {
                double pnl = (tick.bid - t->entry_price) * t->lot_size * config_.contract_size;
                // Only close if profitable
                if (pnl > 0) {
                    engine.ClosePosition(t, "PHASE_EXIT_PEAK");
                    phase_exits_++;
                }
            }
        }
    }
};

} // namespace backtest

#endif // STRATEGY_VANDERPOL_H
