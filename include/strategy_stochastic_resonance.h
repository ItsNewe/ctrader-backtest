#ifndef STRATEGY_STOCHASTIC_RESONANCE_H
#define STRATEGY_STOCHASTIC_RESONANCE_H

#include "tick_based_engine.h"
#include <deque>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace backtest {

/**
 * Stochastic Resonance Strategy
 *
 * In physics, stochastic resonance occurs when adding noise to a weak signal
 * makes it MORE detectable. In markets: a weak mean-reversion signal becomes
 * strongly tradeable when market noise (volatility) is at the "resonant" level.
 *
 * The strategy:
 * 1. Identifies support/resistance barriers (dynamic, based on recent extremes)
 * 2. Measures current noise level (volatility)
 * 3. Only trades when noise is in the "resonant band" (not too low, not too high)
 * 4. Enters at barriers when noise is optimal, exits at mean
 *
 * Best on: USDJPY (positive swap, clear barriers, moderate volatility)
 * Also works on: XAUUSD, any mean-reverting instrument
 */
class StochasticResonance {
public:
    struct Config {
        double barrier_lookback_hours = 24.0;   // Hours to look back for barriers
        double noise_lookback_hours = 4.0;      // Hours to measure noise
        double resonance_low = 0.6;             // Noise too low below this (fraction of typical)
        double resonance_high = 1.8;            // Noise too high above this
        double entry_distance_pct = 0.3;        // Enter when price within 0.3% of barrier
        double tp_distance_pct = 0.15;          // TP at 0.15% from entry (toward mean)
        double sl_distance_pct = 0.5;           // SL at 0.5% beyond barrier
        double lot_size = 0.02;                 // Position size
        double contract_size = 100.0;
        double leverage = 500.0;
        int max_positions = 3;                  // Max concurrent positions
        int warmup_ticks = 5000;                // Warmup for barrier detection
        int ticks_per_hour = 8000;              // Approximate ticks per hour (for lookback calc)
    };

    StochasticResonance(const Config& cfg)
        : config_(cfg),
          ticks_processed_(0),
          barrier_high_(0.0),
          barrier_low_(0.0),
          typical_noise_(0.0),
          current_noise_(0.0),
          resonance_entries_(0),
          noise_rejections_(0),
          peak_equity_(0.0),
          max_dd_pct_(0.0),
          recent_high_(0.0),
          recent_low_(1e18) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        ticks_processed_++;

        // Track prices for barrier and noise detection
        prices_.push_back(tick.bid);
        size_t max_lookback = (size_t)(config_.barrier_lookback_hours * config_.ticks_per_hour);
        if (prices_.size() > max_lookback) {
            prices_.pop_front();
        }

        double equity = engine.GetEquity();
        if (peak_equity_ == 0.0) peak_equity_ = equity;
        if (equity > peak_equity_) peak_equity_ = equity;
        double dd = (peak_equity_ - equity) / peak_equity_ * 100.0;
        if (dd > max_dd_pct_) max_dd_pct_ = dd;

        if (ticks_processed_ < config_.warmup_ticks) return;

        // Track running high/low for noise measurement (O(1) per tick)
        if (tick.bid > recent_high_) recent_high_ = tick.bid;
        if (tick.bid < recent_low_) recent_low_ = tick.bid;

        // Update barriers and noise periodically (every ~5000 ticks)
        if (ticks_processed_ % 5000 == 0) {
            UpdateBarriers();
            UpdateTypicalNoise();
            // Update current noise from running high/low
            size_t noise_ticks = (size_t)(config_.noise_lookback_hours * config_.ticks_per_hour);
            double hours = std::max(1.0, (double)std::min(noise_ticks, prices_.size()) / config_.ticks_per_hour);
            current_noise_ = (recent_high_ - recent_low_) / hours;
            // Reset running trackers
            recent_high_ = tick.bid;
            recent_low_ = tick.bid;
        }

        if (barrier_high_ <= barrier_low_ || typical_noise_ <= 0) return;

        double noise_ratio = current_noise_ / typical_noise_;

        // Check if we're in the resonance band
        bool in_resonance = (noise_ratio >= config_.resonance_low &&
                            noise_ratio <= config_.resonance_high);

        if (!in_resonance) {
            noise_rejections_++;
            return;
        }

        int open_count = (int)engine.GetOpenPositions().size();
        if (open_count >= config_.max_positions) return;

        // Check if price is near a barrier
        double price = tick.bid;
        double range = barrier_high_ - barrier_low_;
        double mean_price = (barrier_high_ + barrier_low_) / 2.0;

        double dist_to_low = (price - barrier_low_) / price * 100.0;
        double dist_to_high = (barrier_high_ - price) / price * 100.0;

        // Near lower barrier -> BUY (expect bounce to mean)
        if (dist_to_low < config_.entry_distance_pct && dist_to_low >= 0) {
            double tp = tick.ask + price * config_.tp_distance_pct / 100.0;
            double sl = tick.ask - price * config_.sl_distance_pct / 100.0;
            engine.OpenMarketOrder("BUY", config_.lot_size, sl, tp);
            resonance_entries_++;
        }
        // Near upper barrier -> SELL (expect bounce to mean)
        else if (dist_to_high < config_.entry_distance_pct && dist_to_high >= 0) {
            double tp = tick.bid - price * config_.tp_distance_pct / 100.0;
            double sl = tick.bid + price * config_.sl_distance_pct / 100.0;
            engine.OpenMarketOrder("SELL", config_.lot_size, sl, tp);
            resonance_entries_++;
        }
    }

    int GetResonanceEntries() const { return resonance_entries_; }
    int GetNoiseRejections() const { return noise_rejections_; }
    double GetBarrierHigh() const { return barrier_high_; }
    double GetBarrierLow() const { return barrier_low_; }
    double GetMaxDDPct() const { return max_dd_pct_; }

private:
    Config config_;
    int ticks_processed_;
    double barrier_high_;
    double barrier_low_;
    double typical_noise_;
    double current_noise_;
    int resonance_entries_;
    int noise_rejections_;
    double peak_equity_;
    double max_dd_pct_;
    double recent_high_;
    double recent_low_;
    std::deque<double> prices_;

    void UpdateBarriers() {
        if (prices_.size() < 1000) return;

        // Efficient: sample every Nth price for percentile estimation
        size_t n = prices_.size();
        size_t sample_step = std::max((size_t)1, n / 500);  // Sample ~500 points

        std::vector<double> sample;
        sample.reserve(500);
        for (size_t i = 0; i < n; i += sample_step) {
            sample.push_back(prices_[i]);
        }
        std::sort(sample.begin(), sample.end());

        size_t s = sample.size();
        barrier_low_ = sample[s / 10];      // P10
        barrier_high_ = sample[s * 9 / 10]; // P90
    }

    void UpdateTypicalNoise() {
        if (prices_.size() < (size_t)config_.ticks_per_hour) return;

        // Sample a few hourly ranges instead of computing all
        size_t hour_ticks = (size_t)config_.ticks_per_hour;
        size_t n = prices_.size();
        int num_samples = std::min(10, (int)(n / hour_ticks));
        if (num_samples < 2) return;

        std::vector<double> ranges;
        for (int i = 0; i < num_samples; i++) {
            size_t start = n - (i + 1) * hour_ticks;
            if (start >= n) break;
            size_t end = start + hour_ticks;
            if (end > n) end = n;
            double hi = *std::max_element(prices_.begin() + start, prices_.begin() + end);
            double lo = *std::min_element(prices_.begin() + start, prices_.begin() + end);
            ranges.push_back(hi - lo);
        }

        if (ranges.empty()) return;
        std::sort(ranges.begin(), ranges.end());
        typical_noise_ = ranges[ranges.size() / 2];
    }

    // MeasureCurrentNoise is now done incrementally in OnTick using recent_high_/recent_low_
};

} // namespace backtest

#endif // STRATEGY_STOCHASTIC_RESONANCE_H
