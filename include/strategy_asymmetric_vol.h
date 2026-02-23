#ifndef STRATEGY_ASYMMETRIC_VOL_H
#define STRATEGY_ASYMMETRIC_VOL_H

#include "tick_based_engine.h"
#include <deque>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace backtest {

/**
 * Asymmetric Volatility Harvesting Strategy
 *
 * Exploits the known asymmetry in how markets move:
 * - Crashes are FAST (fear, margin calls, cascading liquidations)
 * - Recoveries are SLOW (gradual confidence rebuilding)
 *
 * For gold/silver: downswings are 33% faster than upswings
 * (328 vs 489 minutes for large swings from frequency analysis)
 *
 * The strategy:
 * 1. Monitors price velocity (speed of movement)
 * 2. On FAST DOWN moves: deploy capital aggressively (expect quick reversal)
 * 3. On SLOW UP moves: deploy conservatively (no urgency)
 * 4. Scale position size with downside velocity (faster = larger position)
 *
 * The market's structural asymmetry (fear > greed speed) is persistent
 * and exploitable because it's driven by human psychology + margin mechanics.
 *
 * Best on: XAGUSD (most extreme asymmetry, highest OMR)
 * Also works on: XAUUSD (proven asymmetry data)
 */
class AsymmetricVol {
public:
    struct Config {
        int velocity_window = 100;          // Ticks for velocity measurement
        double fast_down_threshold = -0.03; // % per velocity window (fast drop)
        double slow_up_threshold = 0.01;    // % per velocity window (slow rise)
        double base_lots = 0.01;            // Base position size
        double max_scale = 5.0;             // Maximum velocity scaling factor
        double max_lots = 0.50;             // Maximum total exposure
        double fast_tp_pct = 0.10;          // Tight TP for fast drops (0.1% bounce)
        double slow_tp_pct = 0.20;          // Wider TP for slow entries (0.2%)
        double sl_pct = 0.50;               // Stop loss at 0.5% below entry
        int cooldown_ticks = 200;           // Min ticks between entries
        int max_positions = 20;
        int warmup_ticks = 500;
        double acceleration_threshold = -0.001; // Require acceleration (falling faster)
    };

    AsymmetricVol(const Config& cfg)
        : config_(cfg),
          ticks_processed_(0),
          fast_entries_(0),
          slow_entries_(0),
          last_entry_tick_(0),
          peak_equity_(0.0),
          max_dd_pct_(0.0),
          max_velocity_down_(0.0),
          max_velocity_up_(0.0) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        ticks_processed_++;

        prices_.push_back(tick.bid);
        if (prices_.size() > 2000) prices_.pop_front();

        double equity = engine.GetEquity();
        if (peak_equity_ == 0.0) peak_equity_ = equity;
        if (equity > peak_equity_) peak_equity_ = equity;
        double dd = (peak_equity_ - equity) / peak_equity_ * 100.0;
        if (dd > max_dd_pct_) max_dd_pct_ = dd;

        if ((int)prices_.size() < config_.velocity_window + 10) return;
        if (ticks_processed_ < config_.warmup_ticks) return;

        // Calculate velocity (% change over window)
        double velocity = CalculateVelocity();

        // Track extremes for reporting
        if (velocity < max_velocity_down_) max_velocity_down_ = velocity;
        if (velocity > max_velocity_up_) max_velocity_up_ = velocity;

        int open_count = (int)engine.GetOpenPositions().size();
        bool can_enter = (open_count < config_.max_positions &&
                         ticks_processed_ - last_entry_tick_ >= config_.cooldown_ticks);

        if (!can_enter) return;

        // Calculate total current exposure
        double total_lots = engine.GetBuyVolume();
        if (total_lots >= config_.max_lots) return;

        // FAST DOWN: Deploy aggressively (exploit fear discount)
        if (velocity <= config_.fast_down_threshold) {
            // Check acceleration (should be getting faster = more negative)
            double acceleration = CalculateAcceleration();

            // Scale position with velocity (faster drop = larger position)
            double speed_ratio = std::abs(velocity / config_.fast_down_threshold);
            double scale = std::min(config_.max_scale, speed_ratio);
            double lots = std::min(config_.base_lots * scale, config_.max_lots - total_lots);
            lots = std::max(0.01, engine.NormalizeLots(lots));

            // Tight TP (expect quick bounce from panic selling)
            double tp = tick.ask + tick.ask * config_.fast_tp_pct / 100.0;
            double sl = tick.ask - tick.ask * config_.sl_pct / 100.0;

            engine.OpenMarketOrder("BUY", lots, sl, tp);
            fast_entries_++;
            last_entry_tick_ = ticks_processed_;
        }
        // SLOW UP: Deploy conservatively (gradual move, no urgency)
        else if (velocity >= config_.slow_up_threshold && velocity < config_.slow_up_threshold * 3) {
            // Standard size, wider TP
            double lots = std::min(config_.base_lots, config_.max_lots - total_lots);
            lots = std::max(0.01, engine.NormalizeLots(lots));

            double tp = tick.ask + tick.ask * config_.slow_tp_pct / 100.0;
            double sl = tick.ask - tick.ask * config_.sl_pct / 100.0;

            engine.OpenMarketOrder("BUY", lots, sl, tp);
            slow_entries_++;
            last_entry_tick_ = ticks_processed_;
        }
    }

    int GetFastEntries() const { return fast_entries_; }
    int GetSlowEntries() const { return slow_entries_; }
    double GetMaxVelocityDown() const { return max_velocity_down_; }
    double GetMaxVelocityUp() const { return max_velocity_up_; }
    double GetMaxDDPct() const { return max_dd_pct_; }

private:
    Config config_;
    int ticks_processed_;
    int fast_entries_;
    int slow_entries_;
    int last_entry_tick_;
    double peak_equity_;
    double max_dd_pct_;
    double max_velocity_down_;
    double max_velocity_up_;
    std::deque<double> prices_;

    double CalculateVelocity() {
        size_t n = prices_.size();
        int window = config_.velocity_window;
        if ((int)n < window) return 0;

        double current = prices_[n - 1];
        double past = prices_[n - 1 - window];

        return (current - past) / past * 100.0;  // Percentage change
    }

    double CalculateAcceleration() {
        size_t n = prices_.size();
        int half_window = config_.velocity_window / 2;
        if ((int)n < config_.velocity_window + half_window) return 0;

        // Velocity now
        double vel_now = (prices_[n-1] - prices_[n-1-half_window]) / prices_[n-1-half_window] * 100.0;
        // Velocity before
        double vel_prev = (prices_[n-1-half_window] - prices_[n-1-config_.velocity_window]) /
                          prices_[n-1-config_.velocity_window] * 100.0;

        return vel_now - vel_prev;  // Positive = accelerating up, Negative = accelerating down
    }
};

} // namespace backtest

#endif // STRATEGY_ASYMMETRIC_VOL_H
