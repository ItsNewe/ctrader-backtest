#ifndef STRATEGY_COMBINED_CHAOS_H
#define STRATEGY_COMBINED_CHAOS_H

#include "tick_based_engine.h"
#include <vector>
#include <deque>
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace backtest {

/**
 * COMBINED CHAOS STRATEGY
 *
 * Integrates three chaos-inspired concepts:
 * 1. FLOATING ATTRACTOR: Grid center tracks EMA (moving average)
 *    - Grid "floats" with price trends
 *    - TP targets the attractor, not fixed above entry
 *    - Expected: better regime independence
 *
 * 2. VELOCITY ZERO-CROSSING: Only enter at local minima
 *    - Track price velocity (linear regression slope)
 *    - Enter only when velocity crosses from negative to positive
 *    - Expected: better entry timing, reduced DD
 *
 * 3. EDGE-OF-CHAOS PARAMETERS: survive=12%, lookback=8h
 *    - Lower survive_pct increases capital efficiency
 *    - Longer lookback smooths volatility estimation
 *
 * Key configuration options:
 * - ema_period: Period for EMA calculation (default 200)
 * - tp_multiplier: TP distance as multiple of spacing toward attractor
 * - velocity_filter: Enable/disable velocity zero-crossing filter
 * - velocity_window: Ticks for velocity calculation (default 100)
 */
class StrategyCombinedChaos {
public:
    struct Config {
        // Survival / risk parameters
        double survive_pct;             // Max adverse move to survive (% of price)
        double base_spacing;            // Base grid spacing ($)
        double min_volume;              // Minimum lot size
        double max_volume;              // Maximum lot size
        double contract_size;           // Contract size (100 for gold)
        double leverage;                // Account leverage

        // Floating attractor parameters
        int ema_period;                 // EMA period in ticks
        double tp_multiplier;           // TP as multiple of spacing toward attractor

        // Volatility adaptation
        bool adaptive_spacing;          // Enable volatility-adaptive spacing
        double typical_vol_pct;         // Typical volatility as % of price
        double volatility_lookback_hours; // Hours for volatility calculation

        // Velocity zero-crossing filter
        bool velocity_filter;           // Enable velocity zero-crossing entry filter
        int velocity_window;            // Ticks for velocity calculation
        double min_velocity_threshold;  // Minimum velocity to detect crossing

        Config()
            : survive_pct(12.0),        // Edge-of-chaos: lower survive
              base_spacing(1.50),
              min_volume(0.01),
              max_volume(10.0),
              contract_size(100.0),
              leverage(500.0),
              ema_period(200),
              tp_multiplier(2.0),
              adaptive_spacing(true),
              typical_vol_pct(0.5),
              volatility_lookback_hours(8.0),  // Edge-of-chaos: longer lookback
              velocity_filter(true),
              velocity_window(100),
              min_velocity_threshold(0.0001) {}
    };

    explicit StrategyCombinedChaos(const Config& config)
        : config_(config),
          ema_attractor_(0.0),
          ema_alpha_(2.0 / (config.ema_period + 1)),
          current_ask_(0.0),
          current_bid_(0.0),
          current_spread_(0.0),
          current_equity_(0.0),
          current_balance_(0.0),
          peak_equity_(0.0),
          current_spacing_(config.base_spacing),
          lowest_buy_(DBL_MAX),
          highest_buy_(DBL_MIN),
          volume_of_open_trades_(0.0),
          ticks_processed_(0),
          attractor_crossings_(0),
          velocity_entries_blocked_(0),
          velocity_entries_allowed_(0),
          recent_high_(0.0),
          recent_low_(DBL_MAX),
          last_vol_reset_seconds_(0),
          prev_velocity_(0.0),
          velocity_just_crossed_up_(false)
    {
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_spread_ = tick.spread();
        current_equity_ = engine.GetEquity();
        current_balance_ = engine.GetBalance();
        ticks_processed_++;

        // Initialize on first tick
        if (ema_attractor_ == 0.0) {
            ema_attractor_ = current_bid_;
            peak_equity_ = current_equity_;
        }

        // Update peak equity
        if (current_equity_ > peak_equity_) {
            peak_equity_ = current_equity_;
        }

        // Track prices for velocity calculation
        prices_.push_back(current_bid_);
        if (prices_.size() > 2000) prices_.pop_front();

        // Update EMA attractor
        UpdateAttractor();

        // Update velocity and detect zero-crossing
        UpdateVelocity();

        // Update volatility for adaptive spacing
        if (config_.adaptive_spacing) {
            UpdateVolatility(tick);
            UpdateAdaptiveSpacing();
        }

        // Process existing positions (update bounds)
        Iterate(engine);

        // Open new positions based on combined logic
        OpenNew(engine);
    }

    // Statistics getters
    double GetAttractor() const { return ema_attractor_; }
    double GetCurrentSpacing() const { return current_spacing_; }
    int GetAttractorCrossings() const { return attractor_crossings_; }
    long GetTicksProcessed() const { return ticks_processed_; }
    int GetVelocityEntriesBlocked() const { return velocity_entries_blocked_; }
    int GetVelocityEntriesAllowed() const { return velocity_entries_allowed_; }
    double GetCurrentVelocity() const { return prev_velocity_; }

private:
    Config config_;

    // Attractor state
    double ema_attractor_;
    double ema_alpha_;
    double prev_attractor_;  // For crossing detection

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
    int attractor_crossings_;
    int velocity_entries_blocked_;
    int velocity_entries_allowed_;

    // Volatility tracking
    double recent_high_;
    double recent_low_;
    long last_vol_reset_seconds_;

    // Velocity tracking
    std::deque<double> prices_;
    double prev_velocity_;
    bool velocity_just_crossed_up_;  // True on tick when velocity crosses from - to +

    // Parse timestamp to seconds for time-based lookback
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

    void UpdateAttractor() {
        prev_attractor_ = ema_attractor_;

        // EMA: alpha * price + (1-alpha) * prev_ema
        ema_attractor_ = ema_alpha_ * current_bid_ + (1.0 - ema_alpha_) * ema_attractor_;

        // Detect attractor crossings (price crossing the EMA)
        if (prev_attractor_ > 0) {
            bool was_above = prev_attractor_ > ema_attractor_;
            bool now_above = current_bid_ > ema_attractor_;
            if (was_above != now_above) {
                attractor_crossings_++;
            }
        }
    }

    void UpdateVelocity() {
        velocity_just_crossed_up_ = false;

        if ((int)prices_.size() < config_.velocity_window + 10) return;

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

        double denom = n * sum_x2 - sum_x * sum_x;
        if (std::abs(denom) < 1e-10) return;

        double velocity = (n * sum_xy - sum_x * sum_y) / denom;  // Price change per tick

        // Detect zero-crossing: velocity crosses from negative to positive
        // This indicates a local minimum (price was falling, now rising)
        if (prev_velocity_ < -config_.min_velocity_threshold && velocity >= 0) {
            velocity_just_crossed_up_ = true;
        }

        prev_velocity_ = velocity;
    }

    void UpdateVolatility(const Tick& tick) {
        long current_seconds = ParseTimestampToSeconds(tick.timestamp);
        long lookback_seconds = (long)(config_.volatility_lookback_hours * 3600.0);

        if (last_vol_reset_seconds_ == 0 || current_seconds - last_vol_reset_seconds_ >= lookback_seconds) {
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
        double d_equity = cfg.contract_size * trade_size * current_spacing_ * (number_of_trades * (number_of_trades + 1) / 2);
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

    bool CanEnterByVelocity() {
        // If velocity filter is disabled, always allow entry
        if (!config_.velocity_filter) return true;

        // Need enough price history for velocity calculation
        if ((int)prices_.size() < config_.velocity_window + 10) return true;

        // Only allow entry when velocity just crossed from negative to positive
        // (local minimum detected)
        return velocity_just_crossed_up_;
    }

    void OpenNew(TickBasedEngine& engine) {
        int positions_total = (int)engine.GetOpenPositions().size();

        // Calculate deviation from attractor
        double deviation = current_bid_ - ema_attractor_;

        // FLOATING ATTRACTOR + VELOCITY FILTER LOGIC:
        // - Grid centered on EMA attractor
        // - Buy when price is BELOW attractor by spacing (dip below mean)
        // - TP targets back toward the attractor
        // - Velocity filter: only enter at local minima (velocity zero-crossing)

        if (positions_total == 0) {
            // First position: buy if price is below attractor by at least half spacing
            if (deviation < -current_spacing_ * 0.5) {
                // Check velocity filter
                if (CanEnterByVelocity()) {
                    double lots = CalculateLotSize(engine, positions_total);
                    // TP is toward the attractor plus spacing * multiplier
                    double tp = ema_attractor_ + current_spread_ + current_spacing_ * config_.tp_multiplier;
                    if (Open(lots, tp, engine)) {
                        highest_buy_ = current_ask_;
                        lowest_buy_ = current_ask_;
                        velocity_entries_allowed_++;
                    }
                } else {
                    velocity_entries_blocked_++;
                }
            }
        } else {
            // Subsequent positions: add when price drops further below lowest buy
            if (lowest_buy_ >= current_ask_ + current_spacing_) {
                // Check velocity filter
                if (CanEnterByVelocity()) {
                    double lots = CalculateLotSize(engine, positions_total);
                    // TP targets the attractor or above
                    double tp_target = std::max(ema_attractor_, current_ask_ + current_spacing_);
                    double tp = tp_target + current_spread_ + current_spacing_ * (config_.tp_multiplier - 1.0);
                    if (Open(lots, tp, engine)) {
                        lowest_buy_ = current_ask_;
                        velocity_entries_allowed_++;
                    }
                } else {
                    velocity_entries_blocked_++;
                }
            } else if (highest_buy_ <= current_ask_ - current_spacing_) {
                // Price moved up: add position only if still below attractor
                if (deviation < 0) {
                    if (CanEnterByVelocity()) {
                        double lots = CalculateLotSize(engine, positions_total);
                        double tp = ema_attractor_ + current_spread_ + current_spacing_ * config_.tp_multiplier;
                        if (Open(lots, tp, engine)) {
                            highest_buy_ = current_ask_;
                            velocity_entries_allowed_++;
                        }
                    } else {
                        velocity_entries_blocked_++;
                    }
                }
            }
        }
    }
};

} // namespace backtest

#endif // STRATEGY_COMBINED_CHAOS_H
