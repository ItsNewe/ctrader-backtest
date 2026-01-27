#ifndef STRATEGY_WUWEI_H
#define STRATEGY_WUWEI_H

#include "tick_based_engine.h"
#include <cfloat>
#include <cmath>
#include <algorithm>
#include <deque>

namespace backtest {

/**
 * WU WEI (无为) GRID STRATEGY
 *
 * Jū principle: Effortless action - only act when the path is clear.
 * "Be like water" - flow where there is no resistance.
 *
 * Concept: Instead of entering on every grid level, wait for "obvious"
 * setups where multiple conditions align:
 *
 * 1. VELOCITY_ZERO: Price velocity near zero (local extremum)
 * 2. BELOW_EMA: Price below the moving average
 * 3. SPREAD_NORMAL: Spread not unusually wide
 * 4. VOLATILITY_NORMAL: Not in extreme vol period
 *
 * Wu Wei = only enter when ALL conditions are met (effortless, obvious)
 *
 * Key insight: "Forcing" entries (trading against resistance) leads to
 * worse fills and more DD. "Flowing" entries (going with the natural
 * rhythm) should improve quality.
 */
class StrategyWuWei {
public:
    struct Config {
        // Base grid parameters
        double survive_pct;
        double base_spacing;
        double min_volume;
        double max_volume;
        double contract_size;
        double leverage;
        double volatility_lookback_hours;
        double typical_vol_pct;

        // Wu Wei parameters
        bool require_velocity_zero;      // Wait for velocity near zero
        bool require_below_ema;          // Wait for price below EMA
        bool require_spread_normal;      // Require normal spread
        bool require_vol_normal;         // Require normal volatility

        int velocity_window;             // Ticks to measure velocity
        double velocity_threshold_pct;   // Max velocity to consider "zero"
        int ema_period;                  // EMA period for direction
        double spread_max_mult;          // Max spread as multiple of typical
        double vol_max_mult;             // Max vol as multiple of typical

        Config()
            : survive_pct(13.0),
              base_spacing(1.50),
              min_volume(0.01),
              max_volume(10.0),
              contract_size(100.0),
              leverage(500.0),
              volatility_lookback_hours(4.0),
              typical_vol_pct(0.55),
              require_velocity_zero(true),
              require_below_ema(true),
              require_spread_normal(true),
              require_vol_normal(true),
              velocity_window(10),
              velocity_threshold_pct(0.02),  // 0.02% = nearly flat
              ema_period(200),
              spread_max_mult(2.0),
              vol_max_mult(2.0) {}
    };

    struct WuWeiStats {
        long velocity_fails = 0;
        long ema_fails = 0;
        long spread_fails = 0;
        long vol_fails = 0;
        long entries_allowed = 0;
        long total_checks = 0;
    };

    explicit StrategyWuWei(const Config& config)
        : config_(config),
          current_ask_(0.0),
          current_bid_(0.0),
          current_spread_(0.0),
          current_equity_(0.0),
          current_balance_(0.0),
          current_spacing_(config.base_spacing),
          lowest_buy_(DBL_MAX),
          highest_buy_(DBL_MIN),
          volume_of_open_trades_(0.0),
          recent_high_(0.0),
          recent_low_(DBL_MAX),
          last_vol_reset_seconds_(0),
          ema_(0.0),
          ema_alpha_(2.0 / (config.ema_period + 1)),
          typical_spread_(0.0),
          spread_count_(0)
    {
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_spread_ = tick.spread();
        current_equity_ = engine.GetEquity();
        current_balance_ = engine.GetBalance();

        // Initialize EMA
        if (ema_ == 0.0) {
            ema_ = current_bid_;
        }

        // Update EMA
        ema_ = ema_alpha_ * current_bid_ + (1.0 - ema_alpha_) * ema_;

        // Track typical spread
        UpdateTypicalSpread();

        // Track velocity
        UpdateVelocity();

        // Update volatility
        UpdateVolatility(tick);
        UpdateAdaptiveSpacing();

        // Process existing positions
        Iterate(engine);

        // Open new positions (Wu Wei checks)
        OpenNew(engine);
    }

    const WuWeiStats& GetWuWeiStats() const { return stats_; }
    double GetCurrentSpacing() const { return current_spacing_; }
    double GetEMA() const { return ema_; }
    double GetVelocity() const { return current_velocity_pct_; }

private:
    Config config_;

    double current_ask_;
    double current_bid_;
    double current_spread_;
    double current_equity_;
    double current_balance_;
    double current_spacing_;
    double lowest_buy_;
    double highest_buy_;
    double volume_of_open_trades_;

    double recent_high_;
    double recent_low_;
    long last_vol_reset_seconds_;

    double ema_;
    double ema_alpha_;

    // Velocity tracking
    std::deque<double> price_window_;
    double current_velocity_pct_ = 0.0;

    // Spread tracking
    double typical_spread_;
    int spread_count_;
    double spread_sum_ = 0.0;

    WuWeiStats stats_;

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

    void UpdateTypicalSpread() {
        spread_sum_ += current_spread_;
        spread_count_++;
        typical_spread_ = spread_sum_ / spread_count_;
    }

    void UpdateVelocity() {
        price_window_.push_back(current_bid_);
        while ((int)price_window_.size() > config_.velocity_window) {
            price_window_.pop_front();
        }

        if ((int)price_window_.size() >= config_.velocity_window) {
            double old_price = price_window_.front();
            current_velocity_pct_ = (current_bid_ - old_price) / old_price * 100.0;
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

    bool CheckVelocityZero() const {
        if (!config_.require_velocity_zero) return true;
        return std::abs(current_velocity_pct_) < config_.velocity_threshold_pct;
    }

    bool CheckBelowEMA() const {
        if (!config_.require_below_ema) return true;
        return current_bid_ < ema_;
    }

    bool CheckSpreadNormal() const {
        if (!config_.require_spread_normal) return true;
        if (typical_spread_ <= 0) return true;  // Not enough data
        return current_spread_ < typical_spread_ * config_.spread_max_mult;
    }

    bool CheckVolNormal() const {
        if (!config_.require_vol_normal) return true;
        double range = recent_high_ - recent_low_;
        double typical_vol = current_bid_ * (config_.typical_vol_pct / 100.0);
        if (typical_vol <= 0) return true;
        return range < typical_vol * config_.vol_max_mult;
    }

    bool IsObviousEntry() {
        stats_.total_checks++;

        if (!CheckVelocityZero()) {
            stats_.velocity_fails++;
            return false;
        }

        if (!CheckBelowEMA()) {
            stats_.ema_fails++;
            return false;
        }

        if (!CheckSpreadNormal()) {
            stats_.spread_fails++;
            return false;
        }

        if (!CheckVolNormal()) {
            stats_.vol_fails++;
            return false;
        }

        stats_.entries_allowed++;
        return true;
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
        for (const Trade* trade : engine.GetOpenPositions()) {
            used_margin += trade->lot_size * config_.contract_size *
                          trade->entry_price / config_.leverage;
        }

        double margin_stop_out = 20.0;

        double end_price = (positions_total == 0)
            ? current_ask_ * ((100.0 - config_.survive_pct) / 100.0)
            : highest_buy_ * ((100.0 - config_.survive_pct) / 100.0);

        double distance = current_ask_ - end_price;
        double number_of_trades = std::floor(distance / current_spacing_);
        if (number_of_trades <= 0) number_of_trades = 1;

        double equity_at_target = current_equity_ -
                                 volume_of_open_trades_ * distance * config_.contract_size;
        if (used_margin > 0 && (equity_at_target / used_margin * 100.0) < margin_stop_out) {
            return 0.0;
        }

        double trade_size = config_.min_volume;
        double d_equity = config_.contract_size * trade_size * current_spacing_ *
                         (number_of_trades * (number_of_trades + 1) / 2);
        double d_margin = number_of_trades * trade_size * config_.contract_size / config_.leverage;

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

        double final_lots = std::min(lots, config_.max_volume);
        final_lots = std::round(final_lots * 100.0) / 100.0;

        Trade* trade = engine.OpenMarketOrder("BUY", final_lots, 0.0, tp);
        return (trade != nullptr);
    }

    void OpenNew(TickBasedEngine& engine) {
        int positions_total = (int)engine.GetOpenPositions().size();

        if (positions_total == 0) {
            // First position - always open (no Wu Wei filter on first)
            double lots = CalculateLotSize(engine, positions_total);
            double tp = current_ask_ + current_spread_ + current_spacing_;
            if (Open(lots, tp, engine)) {
                highest_buy_ = current_ask_;
                lowest_buy_ = current_ask_;
            }
        } else {
            // Additional positions - apply Wu Wei checks
            if (lowest_buy_ >= current_ask_ + current_spacing_) {
                if (!IsObviousEntry()) {
                    return;
                }

                double lots = CalculateLotSize(engine, positions_total);
                double tp = current_ask_ + current_spread_ + current_spacing_;
                if (Open(lots, tp, engine)) {
                    lowest_buy_ = current_ask_;
                }
            }
        }
    }
};

} // namespace backtest

#endif // STRATEGY_WUWEI_H
