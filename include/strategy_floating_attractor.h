#ifndef STRATEGY_FLOATING_ATTRACTOR_H
#define STRATEGY_FLOATING_ATTRACTOR_H

#include "tick_based_engine.h"
#include <vector>
#include <deque>
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace backtest {

/**
 * FLOATING ATTRACTOR GRID STRATEGY
 *
 * Chaos concept: Instead of fixed grid levels, center the grid on a moving
 * attractor (EMA of recent prices). The grid "floats" with the attractor
 * while capturing oscillations around it.
 *
 * Key differences from FillUpOscillation:
 * 1. Grid center follows a moving average (attractor)
 * 2. Positions open when price deviates from attractor by spacing
 * 3. TP is set back toward the attractor, not fixed above entry
 *
 * Expected benefits:
 * - Better regime independence (grid moves with trends)
 * - Reduced DD during trends (not fighting trend direction)
 * - Same oscillation capture in ranging markets
 */
class StrategyFloatingAttractor {
public:
    enum AttractorType {
        EMA,            // Exponential moving average
        SMA,            // Simple moving average
        VWAP            // Volume-weighted (approximated with equal weight if no volume)
    };

    struct Config {
        double survive_pct;             // Max adverse move to survive (% of price)
        double base_spacing;            // Base grid spacing ($)
        double min_volume;              // Minimum lot size
        double max_volume;              // Maximum lot size
        double contract_size;           // Contract size (100 for gold)
        double leverage;                // Account leverage
        int attractor_period;           // EMA/SMA period in ticks
        AttractorType attractor_type;   // Type of moving average
        double tp_multiplier;           // TP as multiple of spacing toward attractor
        bool adaptive_spacing;          // Enable volatility-adaptive spacing
        double typical_vol_pct;         // Typical volatility as % of price (for adaptive)
        double volatility_lookback_hours; // Hours for volatility calculation

        // Safety configuration
        bool force_min_volume_entry;    // Force entry at min_volume when lot sizing returns 0
        int max_positions;              // Maximum concurrent positions (0 = unlimited)

        Config()
            : survive_pct(13.0),
              base_spacing(1.50),
              min_volume(0.01),
              max_volume(10.0),
              contract_size(100.0),
              leverage(500.0),
              attractor_period(200),
              attractor_type(EMA),
              tp_multiplier(1.0),
              adaptive_spacing(true),
              typical_vol_pct(0.5),
              volatility_lookback_hours(4.0),
              force_min_volume_entry(true),  // Default ON - forced entry discovery
              max_positions(0) {}            // Unlimited by default
    };

    struct Stats {
        long forced_entries = 0;
        long max_position_blocks = 0;
        int peak_positions = 0;
    };

    explicit StrategyFloatingAttractor(const Config& config)
        : config_(config),
          attractor_(0.0),
          ema_alpha_(2.0 / (config.attractor_period + 1)),
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
          recent_high_(0.0),
          recent_low_(DBL_MAX),
          last_vol_reset_seconds_(0)
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
        if (attractor_ == 0.0) {
            attractor_ = current_bid_;
            peak_equity_ = current_equity_;
        }

        // Update peak equity
        if (current_equity_ > peak_equity_) {
            peak_equity_ = current_equity_;
        }

        // Update attractor (moving average)
        UpdateAttractor();

        // Update volatility for adaptive spacing
        if (config_.adaptive_spacing) {
            UpdateVolatility(tick);
            UpdateAdaptiveSpacing();
        }

        // Process existing positions (update bounds)
        Iterate(engine);

        // Open new positions based on deviation from attractor
        OpenNew(engine);
    }

    // Statistics getters
    double GetAttractor() const { return attractor_; }
    double GetCurrentSpacing() const { return current_spacing_; }
    int GetAttractorCrossings() const { return attractor_crossings_; }
    long GetTicksProcessed() const { return ticks_processed_; }
    const Stats& GetStats() const { return stats_; }

private:
    Config config_;
    Stats stats_;

    // Attractor state
    double attractor_;
    double ema_alpha_;
    std::deque<double> price_history_;  // For SMA/VWAP
    double prev_bid_;  // For crossing detection

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

    // Volatility tracking
    double recent_high_;
    double recent_low_;
    long last_vol_reset_seconds_;

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
        prev_bid_ = attractor_;

        switch (config_.attractor_type) {
            case EMA:
                // EMA: alpha * price + (1-alpha) * prev_ema
                attractor_ = ema_alpha_ * current_bid_ + (1.0 - ema_alpha_) * attractor_;
                break;

            case SMA:
                // Simple moving average
                price_history_.push_back(current_bid_);
                if ((int)price_history_.size() > config_.attractor_period) {
                    price_history_.pop_front();
                }
                if (!price_history_.empty()) {
                    double sum = 0.0;
                    for (double p : price_history_) sum += p;
                    attractor_ = sum / price_history_.size();
                }
                break;

            case VWAP:
                // Approximate VWAP (equal weight without volume data)
                // Same as SMA in this implementation
                price_history_.push_back(current_bid_);
                if ((int)price_history_.size() > config_.attractor_period) {
                    price_history_.pop_front();
                }
                if (!price_history_.empty()) {
                    double sum = 0.0;
                    for (double p : price_history_) sum += p;
                    attractor_ = sum / price_history_.size();
                }
                break;
        }

        // Detect attractor crossings (price crossing the moving average)
        if (prev_bid_ > 0) {
            bool was_above = prev_bid_ > attractor_;
            bool now_above = current_bid_ > attractor_;
            if (was_above != now_above) {
                attractor_crossings_++;
            }
        }
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
        lowest_buy_ = DBL_MAX;
        highest_buy_ = DBL_MIN;
        volume_of_open_trades_ = 0.0;

        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->IsBuy()) {
                volume_of_open_trades_ += trade->lot_size;
                lowest_buy_ = std::min(lowest_buy_, trade->entry_price);
                highest_buy_ = std::max(highest_buy_, trade->entry_price);
            }
        }
    }

    double CalculateLotSize(TickBasedEngine& engine, int positions_total) {
        double used_margin = 0.0;
        for (const Trade* trade : engine.GetOpenPositions()) {
            used_margin += trade->lot_size * config_.contract_size * trade->entry_price / config_.leverage;
        }

        double margin_stop_out = 20.0;

        double end_price = (positions_total == 0)
            ? current_ask_ * ((100.0 - config_.survive_pct) / 100.0)
            : highest_buy_ * ((100.0 - config_.survive_pct) / 100.0);

        double distance = current_ask_ - end_price;
        double number_of_trades = std::floor(distance / current_spacing_);
        if (number_of_trades <= 0) number_of_trades = 1;

        double equity_at_target = current_equity_ - volume_of_open_trades_ * distance * config_.contract_size;
        if (used_margin > 0 && (equity_at_target / used_margin * 100.0) < margin_stop_out) {
            return 0.0;
        }

        double trade_size = config_.min_volume;
        double d_equity = config_.contract_size * trade_size * current_spacing_ * (number_of_trades * (number_of_trades + 1) / 2);
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
        // Forced entry: if lot sizing returns 0 but force is enabled, use min_volume
        if (lots < config_.min_volume) {
            if (config_.force_min_volume_entry) {
                lots = config_.min_volume;
                stats_.forced_entries++;
            } else {
                return false;
            }
        }

        double final_lots = std::min(lots, config_.max_volume);
        final_lots = std::round(final_lots * 100.0) / 100.0;

        Trade* trade = engine.OpenMarketOrder("BUY", final_lots, 0.0, tp);
        return (trade != nullptr);
    }

    void OpenNew(TickBasedEngine& engine) {
        int positions_total = (int)engine.GetOpenPositions().size();

        // Track peak positions
        if (positions_total > stats_.peak_positions) {
            stats_.peak_positions = positions_total;
        }

        // Safety: max position cap
        if (config_.max_positions > 0 && positions_total >= config_.max_positions) {
            stats_.max_position_blocks++;
            return;
        }

        // Calculate deviation from attractor
        double deviation = current_bid_ - attractor_;

        // FLOATING ATTRACTOR LOGIC:
        // - Buy when price is BELOW attractor by spacing (dip below mean)
        // - Set TP back toward (or above) the attractor

        if (positions_total == 0) {
            // First position: buy if price is below attractor by at least spacing
            if (deviation < -current_spacing_ * 0.5) {
                double lots = CalculateLotSize(engine, positions_total);
                // TP is toward the attractor plus a bit
                double tp = attractor_ + current_spread_ + current_spacing_ * config_.tp_multiplier;
                if (Open(lots, tp, engine)) {
                    highest_buy_ = current_ask_;
                    lowest_buy_ = current_ask_;
                }
            }
        } else {
            // Subsequent positions: add when price drops further below lowest buy
            // But also respect the attractor - buy when sufficiently below
            if (lowest_buy_ >= current_ask_ + current_spacing_) {
                // Standard grid: add position when price drops by spacing
                double lots = CalculateLotSize(engine, positions_total);
                // TP targets the attractor or above
                double tp_target = std::max(attractor_, current_ask_ + current_spacing_);
                double tp = tp_target + current_spread_ + current_spacing_ * (config_.tp_multiplier - 1.0);
                if (Open(lots, tp, engine)) {
                    lowest_buy_ = current_ask_;
                }
            } else if (highest_buy_ <= current_ask_ - current_spacing_) {
                // Price moved up: add position only if still below attractor
                if (deviation < 0) {
                    double lots = CalculateLotSize(engine, positions_total);
                    double tp = attractor_ + current_spread_ + current_spacing_ * config_.tp_multiplier;
                    if (Open(lots, tp, engine)) {
                        highest_buy_ = current_ask_;
                    }
                }
            }
        }
    }
};

} // namespace backtest

#endif // STRATEGY_FLOATING_ATTRACTOR_H
