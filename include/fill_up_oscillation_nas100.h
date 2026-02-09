#ifndef FILL_UP_OSCILLATION_NAS100_H
#define FILL_UP_OSCILLATION_NAS100_H

#include "tick_based_engine.h"
#include <vector>
#include <deque>
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace backtest {

/**
 * NAS100-Adapted Oscillation Fill-Up Strategy
 *
 * Key differences from XAUUSD version:
 * - NAS100 price ~20,000 vs Gold ~2,600 (7.7x ratio)
 * - NAS100 contract size = 1.0 vs Gold = 100.0
 * - NAS100 typical 4hr volatility ~$100 vs Gold ~$10
 * - NAS100 pip size = 0.01 vs Gold = 0.01
 * - Spacing scaled proportionally to price level
 * - Velocity threshold scaled to NAS100 moves
 */
class FillUpOscillationNAS100 {
public:
    // Oscillation enhancement modes
    enum Mode {
        BASELINE = 0,           // No enhancements (control)
        ADAPTIVE_SPACING = 1,   // Adjust spacing to volatility
        ANTIFRAGILE = 2,        // Increase size in drawdown
        VELOCITY_FILTER = 3,    // Pause on crash velocity
        ALL_COMBINED = 4        // All enhancements
    };

    FillUpOscillationNAS100(double survive_pct, double base_spacing,
                            double min_volume, double max_volume,
                            double contract_size, double leverage,
                            Mode mode = BASELINE,
                            double antifragile_scale = 0.1,        // 10% more per 5% DD
                            double velocity_threshold = 200.0,      // $200/hour = crash (scaled from gold's $30)
                            double volatility_lookback_hours = 4.0,
                            double volatility_baseline = 100.0)     // NAS100 typical 4hr range ~$100
        : survive_pct_(survive_pct),
          base_spacing_(base_spacing),
          min_volume_(min_volume),
          max_volume_(max_volume),
          contract_size_(contract_size),
          leverage_(leverage),
          mode_(mode),
          antifragile_scale_(antifragile_scale),
          velocity_threshold_(velocity_threshold),
          volatility_lookback_hours_(volatility_lookback_hours),
          volatility_baseline_(volatility_baseline),
          lowest_buy_(DBL_MAX),
          highest_buy_(DBL_MIN),
          volume_of_open_trades_(0.0),
          current_ask_(0.0),
          current_bid_(0.0),
          current_spread_(0.0),
          current_equity_(0.0),
          current_balance_(0.0),
          peak_equity_(0.0),
          current_spacing_(base_spacing),
          recent_high_(0.0),
          recent_low_(DBL_MAX),
          hour_start_price_(0.0),
          velocity_paused_(false),
          pause_until_price_(0.0),
          ticks_processed_(0),
          velocity_pause_count_(0),
          max_velocity_seen_(0.0),
          adaptive_spacing_changes_(0)
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
            hour_start_price_ = current_bid_;
        }

        // Update peak
        if (current_equity_ > peak_equity_) {
            peak_equity_ = current_equity_;
        }

        // Track volatility (price range over lookback period)
        UpdateVolatility(tick);

        // Check velocity filter
        if (mode_ == VELOCITY_FILTER || mode_ == ALL_COMBINED) {
            CheckVelocity(tick);
            if (velocity_paused_) {
                // Still paused - check if price recovered
                if (current_bid_ > pause_until_price_) {
                    velocity_paused_ = false;
                }
                return;
            }
        }

        // Update adaptive spacing
        if (mode_ == ADAPTIVE_SPACING || mode_ == ALL_COMBINED) {
            UpdateAdaptiveSpacing();
        }

        // Process positions
        Iterate(engine);

        // Open new positions
        OpenNew(engine);
    }

    // Statistics
    double GetCurrentSpacing() const { return current_spacing_; }
    int GetVelocityPauseCount() const { return velocity_pause_count_; }
    double GetMaxVelocity() const { return max_velocity_seen_; }
    int GetAdaptiveSpacingChanges() const { return adaptive_spacing_changes_; }
    double GetPeakEquity() const { return peak_equity_; }

private:
    // Base parameters
    double survive_pct_;
    double base_spacing_;
    double min_volume_;
    double max_volume_;
    double contract_size_;
    double leverage_;
    Mode mode_;

    // Oscillation parameters
    double antifragile_scale_;
    double velocity_threshold_;
    double volatility_lookback_hours_;
    double volatility_baseline_;  // Typical 4-hour range for NAS100

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

    // Adaptive spacing
    double current_spacing_;
    std::deque<double> price_history_;
    double recent_high_;
    double recent_low_;
    double hour_start_price_;

    // Velocity filter
    bool velocity_paused_;
    double pause_until_price_;
    std::deque<std::pair<long, double>> hourly_prices_;  // tick_count, price

    // Statistics
    long ticks_processed_;
    int velocity_pause_count_;
    double max_velocity_seen_;
    int adaptive_spacing_changes_;

    void UpdateVolatility(const Tick& /*tick*/) {
        // Track hourly prices for velocity and volatility
        // NAS100 has fewer ticks than gold typically - estimate ~100 ticks/second
        long ticks_per_hour = 360000;

        // Check if new hour
        if (ticks_processed_ % ticks_per_hour == 0) {
            hourly_prices_.push_back(std::make_pair(ticks_processed_, current_bid_));
            if (hourly_prices_.size() > 24) {
                hourly_prices_.pop_front();
            }
            hour_start_price_ = current_bid_;
        }

        // Track recent high/low for volatility (reset every ~4 hours)
        long volatility_ticks = (long)(volatility_lookback_hours_ * ticks_per_hour);
        if (ticks_processed_ % volatility_ticks == 0) {
            recent_high_ = current_bid_;
            recent_low_ = current_bid_;
        }
        recent_high_ = std::max(recent_high_, current_bid_);
        recent_low_ = std::min(recent_low_, current_bid_);
    }

    void CheckVelocity(const Tick& /*tick*/) {
        // Calculate price velocity ($/hour)
        if (hourly_prices_.size() >= 1) {
            double velocity = hour_start_price_ - current_bid_;  // Positive = dropping
            max_velocity_seen_ = std::max(max_velocity_seen_, velocity);

            if (velocity > velocity_threshold_ && !velocity_paused_) {
                // Crash detected - pause
                velocity_paused_ = true;
                pause_until_price_ = current_bid_ + velocity_threshold_ * 0.5;  // Resume when recovers 50%
                velocity_pause_count_++;
            }
        }
    }

    void UpdateAdaptiveSpacing() {
        // Adjust spacing based on recent volatility
        double range = recent_high_ - recent_low_;
        if (range > 0 && recent_high_ > 0) {
            // Volatility ratio: current range vs typical NAS100 4hr range (~$100)
            double vol_ratio = range / volatility_baseline_;
            vol_ratio = std::max(0.5, std::min(3.0, vol_ratio));  // Clamp 0.5x to 3x

            double new_spacing = base_spacing_ * vol_ratio;
            // NAS100: clamp spacing to reasonable range (e.g., $5 to $50)
            double min_spacing = base_spacing_ * 0.5;
            double max_spacing = base_spacing_ * 5.0;
            new_spacing = std::max(min_spacing, std::min(max_spacing, new_spacing));

            if (std::abs(new_spacing - current_spacing_) > base_spacing_ * 0.1) {
                current_spacing_ = new_spacing;
                adaptive_spacing_changes_++;
            }
        }
    }

    double GetAntifragileMultiplier() {
        if (mode_ != ANTIFRAGILE && mode_ != ALL_COMBINED) {
            return 1.0;
        }

        // Calculate current drawdown
        double dd_pct = 0.0;
        if (peak_equity_ > 0) {
            dd_pct = (peak_equity_ - current_equity_) / peak_equity_ * 100.0;
        }

        // Increase position size during drawdown
        // At 5% DD: 1.1x, at 10% DD: 1.2x, etc.
        double multiplier = 1.0 + (dd_pct / 5.0) * antifragile_scale_;
        return std::min(2.0, multiplier);  // Cap at 2x
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
        // Similar to original but with anti-fragile scaling
        double used_margin = 0.0;
        for (const Trade* trade : engine.GetOpenPositions()) {
            used_margin += trade->lot_size * contract_size_ * trade->entry_price / leverage_;
        }

        double margin_stop_out = 20.0;
        double margin_level = (used_margin > 0) ? (current_equity_ / used_margin * 100.0) : 10000.0;

        double end_price = (positions_total == 0)
            ? current_ask_ * ((100.0 - survive_pct_) / 100.0)
            : highest_buy_ * ((100.0 - survive_pct_) / 100.0);

        double distance = current_ask_ - end_price;
        double number_of_trades = std::floor(distance / current_spacing_);
        if (number_of_trades <= 0) number_of_trades = 1;

        // Calculate base lot size
        double equity_at_target = current_equity_ - volume_of_open_trades_ * distance * contract_size_;
        if (used_margin > 0 && (equity_at_target / used_margin * 100.0) < margin_stop_out) {
            return 0.0;
        }

        double trade_size = min_volume_;
        double d_equity = contract_size_ * trade_size * current_spacing_ * (number_of_trades * (number_of_trades + 1) / 2);
        double d_margin = number_of_trades * trade_size * contract_size_ / leverage_;

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

        // Apply anti-fragile scaling
        trade_size *= GetAntifragileMultiplier();

        return std::min(trade_size, max_volume_);
    }

    bool Open(double lots, TickBasedEngine& engine) {
        if (lots < min_volume_) return false;

        double final_lots = std::min(lots, max_volume_);
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

#endif // FILL_UP_OSCILLATION_NAS100_H
