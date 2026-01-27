#ifndef STRATEGY_COMBINED_JU_H
#define STRATEGY_COMBINED_JU_H

#include "tick_based_engine.h"
#include <cfloat>
#include <cmath>
#include <algorithm>
#include <deque>

namespace backtest {

/**
 * COMBINED JU STRATEGY
 *
 * Combines three promising Ju-inspired concepts:
 * 1. Rubber Band TP (SQRT mode, FIRST_ENTRY equilibrium)
 * 2. Velocity Zero Filter (Wu Wei - enter at local minima)
 * 3. Barbell Sizing (larger lots at deeper deviations)
 *
 * The synergy hypothesis:
 * - Fewer trades (velocity filter) means each trade matters more
 * - Barbell sizing amplifies the best entries (deep deviations)
 * - Rubber band TP captures larger profits from those entries
 */
class StrategyCombinedJu {
public:
    enum TPMode {
        FIXED,      // Standard: TP = spacing + spread
        SQRT,       // TP = base + sqrt_scale * sqrt(deviation)
        LINEAR      // TP = base + linear_scale * deviation
    };

    enum SizingMode {
        UNIFORM,    // All positions same size
        LINEAR_SIZING,    // lots = base * (1 + pos_num * scale)
        THRESHOLD_SIZING  // lots = base if pos<N, else base * mult
    };

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

        // Rubber Band TP
        TPMode tp_mode;
        double tp_sqrt_scale;      // For SQRT mode
        double tp_linear_scale;    // For LINEAR mode
        double tp_min;             // Minimum TP (spacing)

        // Velocity Zero Filter (Wu Wei)
        bool enable_velocity_filter;
        int velocity_window;
        double velocity_threshold_pct;

        // Barbell Sizing
        SizingMode sizing_mode;
        double sizing_linear_scale;    // For LINEAR_SIZING
        int sizing_threshold_pos;      // For THRESHOLD_SIZING
        double sizing_threshold_mult;  // Multiplier after threshold

        Config()
            : survive_pct(13.0),
              base_spacing(1.50),
              min_volume(0.01),
              max_volume(10.0),
              contract_size(100.0),
              leverage(500.0),
              volatility_lookback_hours(4.0),
              typical_vol_pct(0.55),
              tp_mode(SQRT),
              tp_sqrt_scale(0.5),
              tp_linear_scale(0.3),
              tp_min(1.50),
              enable_velocity_filter(true),
              velocity_window(10),
              velocity_threshold_pct(0.01),
              sizing_mode(UNIFORM),
              sizing_linear_scale(0.5),
              sizing_threshold_pos(5),
              sizing_threshold_mult(2.0) {}
    };

    struct Stats {
        long velocity_blocks = 0;
        long entries_allowed = 0;
        double total_tp_set = 0.0;
        double total_lots_opened = 0.0;
        int max_position_count = 0;
    };

    explicit StrategyCombinedJu(const Config& config)
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
          first_entry_price_(0.0),
          position_count_(0)
    {
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_spread_ = tick.spread();
        current_equity_ = engine.GetEquity();
        current_balance_ = engine.GetBalance();

        // Update velocity tracking
        UpdateVelocity();

        // Update volatility
        UpdateVolatility(tick);
        UpdateAdaptiveSpacing();

        // Process existing positions
        Iterate(engine);

        // Open new positions
        OpenNew(engine);
    }

    const Stats& GetStats() const { return stats_; }
    double GetCurrentSpacing() const { return current_spacing_; }
    double GetFirstEntryPrice() const { return first_entry_price_; }
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

    double first_entry_price_;
    int position_count_;

    // Velocity tracking
    std::deque<double> price_window_;
    double current_velocity_pct_ = 0.0;

    Stats stats_;

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
        if (!config_.enable_velocity_filter) return true;
        return std::abs(current_velocity_pct_) < config_.velocity_threshold_pct;
    }

    double CalculateTP() const {
        if (first_entry_price_ <= 0) {
            return current_ask_ + current_spread_ + current_spacing_;
        }

        double deviation = std::abs(first_entry_price_ - current_ask_);

        switch (config_.tp_mode) {
            case FIXED:
                return current_ask_ + current_spread_ + current_spacing_;

            case SQRT: {
                double tp_addition = config_.tp_sqrt_scale * std::sqrt(deviation);
                double tp = current_ask_ + current_spread_ + std::max(config_.tp_min, tp_addition);
                return tp;
            }

            case LINEAR: {
                double tp_addition = config_.tp_linear_scale * deviation;
                double tp = current_ask_ + current_spread_ + std::max(config_.tp_min, tp_addition);
                return tp;
            }
        }

        return current_ask_ + current_spread_ + current_spacing_;
    }

    double CalculateLotMultiplier() const {
        switch (config_.sizing_mode) {
            case UNIFORM:
                return 1.0;

            case LINEAR_SIZING:
                return 1.0 + position_count_ * config_.sizing_linear_scale;

            case THRESHOLD_SIZING:
                if (position_count_ >= config_.sizing_threshold_pos) {
                    return config_.sizing_threshold_mult;
                }
                return 1.0;
        }
        return 1.0;
    }

    void Iterate(TickBasedEngine& engine) {
        lowest_buy_ = DBL_MAX;
        highest_buy_ = DBL_MIN;
        volume_of_open_trades_ = 0.0;
        position_count_ = 0;

        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->direction == "BUY") {
                volume_of_open_trades_ += trade->lot_size;
                lowest_buy_ = std::min(lowest_buy_, trade->entry_price);
                highest_buy_ = std::max(highest_buy_, trade->entry_price);
                position_count_++;
            }
        }

        // Reset first entry price when all positions closed
        if (position_count_ == 0) {
            first_entry_price_ = 0.0;
        }

        if (position_count_ > stats_.max_position_count) {
            stats_.max_position_count = position_count_;
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
        if (trade != nullptr) {
            stats_.entries_allowed++;
            stats_.total_tp_set += tp - current_ask_;
            stats_.total_lots_opened += final_lots;
            return true;
        }
        return false;
    }

    void OpenNew(TickBasedEngine& engine) {
        int positions_total = (int)engine.GetOpenPositions().size();

        if (positions_total == 0) {
            // First position - set equilibrium, no velocity filter
            double lots = CalculateLotSize(engine, positions_total);
            double tp = current_ask_ + current_spread_ + current_spacing_;
            if (Open(lots, tp, engine)) {
                first_entry_price_ = current_ask_;
                highest_buy_ = current_ask_;
                lowest_buy_ = current_ask_;
            }
        } else {
            // Additional positions
            if (lowest_buy_ >= current_ask_ + current_spacing_) {
                // Check velocity filter (Wu Wei)
                if (!CheckVelocityZero()) {
                    stats_.velocity_blocks++;
                    return;
                }

                // Calculate base lot size (already respects margin)
                double lots = CalculateLotSize(engine, positions_total);

                // Apply barbell sizing multiplier, but cap to preserve margin safety
                double lot_mult = CalculateLotMultiplier();
                if (lot_mult > 1.0) {
                    // Only increase lots if we have margin headroom
                    // Use conservative approach: scale down the multiplier based on position count
                    double safety_factor = 1.0 / (1.0 + position_count_ * 0.05);
                    lot_mult = 1.0 + (lot_mult - 1.0) * safety_factor;
                }
                lots *= lot_mult;

                // Ensure within bounds
                lots = std::max(lots, config_.min_volume);
                lots = std::min(lots, config_.max_volume);

                // Calculate rubber band TP
                double tp = CalculateTP();

                if (Open(lots, tp, engine)) {
                    lowest_buy_ = current_ask_;
                }
            }
        }
    }
};

} // namespace backtest

#endif // STRATEGY_COMBINED_JU_H
