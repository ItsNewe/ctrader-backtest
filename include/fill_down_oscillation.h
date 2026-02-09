/**
 * @file fill_down_oscillation.h
 * @brief Short-only oscillation strategy - mirror of FillUp for short positions.
 *
 * This strategy opens SHORT positions on price rallies and closes them on dips.
 * Designed to be paired with FillUp (long gold) for a hedged gold/silver strategy.
 *
 * Key differences from FillUp:
 * - Opens SELL orders instead of BUY
 * - Entries on rallies above highest_sell + spacing
 * - Take profit below entry (bid - spread - spacing)
 * - Survive calculation: price rises X% from lowest_sell
 */

#ifndef FILL_DOWN_OSCILLATION_H
#define FILL_DOWN_OSCILLATION_H

#include "tick_based_engine.h"
#include <vector>
#include <deque>
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace backtest {

class FillDownOscillation {
public:
    enum Mode {
        BASELINE = 0,
        ADAPTIVE_SPACING = 1
    };

    struct AdaptiveConfig {
        double typical_vol_pct;
        double min_spacing_mult;
        double max_spacing_mult;
        double min_spacing_abs;
        double max_spacing_abs;
        bool pct_spacing;

        AdaptiveConfig()
            : typical_vol_pct(0.5), min_spacing_mult(0.5), max_spacing_mult(3.0),
              min_spacing_abs(0.05), max_spacing_abs(1.0), pct_spacing(true) {}
    };

    struct Stats {
        int total_entries;
        int forced_entries;
        int peak_positions;
        double peak_equity;
        double max_drawdown_pct;

        Stats() : total_entries(0), forced_entries(0), peak_positions(0),
                  peak_equity(0.0), max_drawdown_pct(0.0) {}
    };

    struct Config {
        double survive_pct = 20.0;
        double base_spacing = 0.5;       // In pct_spacing mode, this is %
        double min_volume = 0.01;
        double max_volume = 5.0;
        double contract_size = 5000.0;   // XAGUSD default
        double leverage = 500.0;
        Mode mode = ADAPTIVE_SPACING;
        double volatility_lookback_hours = 4.0;
        AdaptiveConfig adaptive;
        bool force_min_volume_entry = true;

        // Safety controls (matching FillUp's SafetyConfig)
        int max_positions = 0;           // 0 = unlimited
        double equity_stop_pct = 0;      // Close all if DD exceeds this % (0 = disabled)

        static Config XAGUSD_Default() {
            Config c;
            c.survive_pct = 20.0;
            c.base_spacing = 0.5;        // 0.5% of price
            c.contract_size = 5000.0;
            c.leverage = 500.0;
            c.mode = ADAPTIVE_SPACING;
            c.adaptive.pct_spacing = true;
            c.adaptive.typical_vol_pct = 0.45;
            c.adaptive.min_spacing_abs = 0.1;   // 0.1%
            c.adaptive.max_spacing_abs = 2.0;   // 2.0%
            return c;
        }
    };

    explicit FillDownOscillation(const Config& config)
        : config_(config),
          lowest_sell_(DBL_MAX),
          highest_sell_(DBL_MIN),
          volume_of_open_trades_(0.0),
          current_ask_(0.0),
          current_bid_(0.0),
          current_spread_(0.0),
          current_equity_(0.0),
          current_balance_(0.0),
          peak_equity_(0.0),
          current_spacing_(config.base_spacing),
          recent_high_(0.0),
          recent_low_(DBL_MAX),
          last_vol_reset_seconds_(0)
    {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_spread_ = tick.spread();
        current_equity_ = engine.GetEquity();
        current_balance_ = engine.GetBalance();

        // Initialize
        if (peak_equity_ == 0.0) {
            peak_equity_ = current_equity_;
            stats_.peak_equity = current_equity_;
            // Set initial spacing from first price when using percentage mode
            if (config_.adaptive.pct_spacing) {
                current_spacing_ = current_bid_ * (config_.base_spacing / 100.0);
            } else {
                current_spacing_ = config_.base_spacing;
            }
        }

        // Update peak and drawdown
        if (current_equity_ > peak_equity_) {
            peak_equity_ = current_equity_;
            stats_.peak_equity = current_equity_;
        }
        double dd = (peak_equity_ - current_equity_) / peak_equity_ * 100.0;
        if (dd > stats_.max_drawdown_pct) {
            stats_.max_drawdown_pct = dd;
        }

        // Track volatility
        UpdateVolatility(tick);

        // Update adaptive spacing
        if (config_.mode == ADAPTIVE_SPACING) {
            UpdateAdaptiveSpacing();
        }

        // Process positions
        Iterate(engine);

        // Open new positions
        OpenNew(engine);
    }

    double GetCurrentSpacing() const { return current_spacing_; }
    const Stats& GetStats() const { return stats_; }
    double GetLowestSell() const { return lowest_sell_; }
    double GetHighestSell() const { return highest_sell_; }
    void SetMaxVolume(double v) { config_.max_volume = v; }

private:
    Config config_;
    Stats stats_;

    // Position tracking (for shorts)
    double lowest_sell_;
    double highest_sell_;
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
    int64_t last_vol_reset_seconds_;

    // Optimized: zero-allocation manual parsing (matching FillUpOscillation)
    static int64_t ParseTimestamp(const std::string& ts) {
        if (ts.size() < 19) return 0;
        int year = (ts[0] - '0') * 1000 + (ts[1] - '0') * 100 + (ts[2] - '0') * 10 + (ts[3] - '0');
        int month = (ts[5] - '0') * 10 + (ts[6] - '0');
        int day = (ts[8] - '0') * 10 + (ts[9] - '0');
        int hour = (ts[11] - '0') * 10 + (ts[12] - '0');
        int minute = (ts[14] - '0') * 10 + (ts[15] - '0');
        int second = (ts[17] - '0') * 10 + (ts[18] - '0');
        static const int month_days[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
        long days = (long)(year - 2020) * 365 + (year - 2020) / 4;
        days += month_days[month - 1] + day;
        if (month > 2 && year % 4 == 0) days++;
        return days * 86400L + hour * 3600L + minute * 60L + second;
    }

    void UpdateVolatility(const Tick& tick) {
        price_history_.push_back(tick.bid);

        // Calculate lookback samples (rough: ~10 ticks per second)
        size_t lookback_samples = static_cast<size_t>(config_.volatility_lookback_hours * 3600 * 10);
        lookback_samples = std::max(lookback_samples, size_t(100));

        while (price_history_.size() > lookback_samples) {
            price_history_.pop_front();
        }

        // Update high/low
        if (tick.bid > recent_high_) recent_high_ = tick.bid;
        if (tick.bid < recent_low_) recent_low_ = tick.bid;

        // Reset high/low periodically
        int64_t current_seconds = ParseTimestamp(tick.timestamp);
        int64_t lookback_seconds = static_cast<int64_t>(config_.volatility_lookback_hours * 3600);
        if (current_seconds - last_vol_reset_seconds_ > lookback_seconds) {
            recent_high_ = tick.bid;
            recent_low_ = tick.bid;
            last_vol_reset_seconds_ = current_seconds;
        }
    }

    void UpdateAdaptiveSpacing() {
        // Use incrementally tracked recent_high_/recent_low_ (from UpdateVolatility)
        // instead of O(N) scan of full deque — critical for performance
        double high = recent_high_;
        double low = recent_low_;
        if (high <= 0 || low <= 0 || high <= low) return;

        double mid = (high + low) / 2.0;
        if (mid <= 0) return;

        double range_pct = (high - low) / mid * 100.0;
        double vol_ratio = range_pct / config_.adaptive.typical_vol_pct;

        double new_spacing;
        if (config_.adaptive.pct_spacing) {
            // Spacing as percentage of price
            double base_pct = config_.base_spacing;
            double adjusted_pct = base_pct * vol_ratio;
            adjusted_pct = std::max(adjusted_pct, config_.adaptive.min_spacing_abs);
            adjusted_pct = std::min(adjusted_pct, config_.adaptive.max_spacing_abs);
            new_spacing = current_bid_ * (adjusted_pct / 100.0);
        } else {
            new_spacing = config_.base_spacing * vol_ratio;
            new_spacing = std::max(new_spacing, config_.adaptive.min_spacing_abs);
            new_spacing = std::min(new_spacing, config_.adaptive.max_spacing_abs);
        }

        current_spacing_ = new_spacing;
    }

    void Iterate(TickBasedEngine& engine) {
        lowest_sell_ = DBL_MAX;
        highest_sell_ = DBL_MIN;
        volume_of_open_trades_ = 0.0;

        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->direction == TradeDirection::SELL) {
                volume_of_open_trades_ += trade->lot_size;
                lowest_sell_ = std::min(lowest_sell_, trade->entry_price);
                highest_sell_ = std::max(highest_sell_, trade->entry_price);
            }
        }

        int positions = static_cast<int>(engine.GetOpenPositions().size());
        if (positions > stats_.peak_positions) {
            stats_.peak_positions = positions;
        }
    }

    double CalculateLotSize(TickBasedEngine& engine, int positions_total) {
        // Calculate used margin for shorts
        double used_margin = 0.0;
        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->direction == TradeDirection::SELL) {
                used_margin += trade->lot_size * config_.contract_size * trade->entry_price / config_.leverage;
            }
        }

        double margin_stop_out = 20.0;

        // For shorts: survive a price RISE of survive_pct% from lowest_sell
        // end_price is where price could rise to
        double end_price = (positions_total == 0)
            ? current_bid_ * ((100.0 + config_.survive_pct) / 100.0)
            : lowest_sell_ * ((100.0 + config_.survive_pct) / 100.0);

        double distance = end_price - current_bid_;  // How much price could rise against us
        if (distance <= 0) distance = current_spacing_;

        double number_of_trades = std::floor(distance / current_spacing_);
        if (number_of_trades <= 0) number_of_trades = 1;

        // Calculate equity at target (if price rises to end_price)
        // For shorts: loss = volume * contract_size * price_rise
        double equity_at_target = current_equity_ - volume_of_open_trades_ * distance * config_.contract_size;

        if (used_margin > 0 && (equity_at_target / used_margin * 100.0) < margin_stop_out) {
            return 0.0;
        }

        double trade_size = config_.min_volume;
        double d_equity = config_.contract_size * trade_size * current_spacing_ * (number_of_trades * (number_of_trades + 1) / 2);
        double d_margin = number_of_trades * trade_size * config_.contract_size / config_.leverage;

        // Binary search for largest valid multiplier
        double max_mult = config_.max_volume / config_.min_volume;
        double low = 1.0, high = max_mult;
        double best_mult = 1.0;

        while (high - low > 0.05) {
            double mid = (low + high) / 2.0;
            double test_equity = equity_at_target - mid * d_equity;
            double test_margin = used_margin + mid * d_margin;
            if (test_margin > 0 && (test_equity / test_margin * 100.0) > margin_stop_out) {
                best_mult = mid;
                low = mid;
            } else {
                high = mid;
            }
        }

        return std::min(best_mult * config_.min_volume, config_.max_volume);
    }

    bool Open(double lots, TickBasedEngine& engine) {
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

        // Short: TP is below entry price
        double tp = current_bid_ - current_spread_ - current_spacing_;
        if (tp <= 0) tp = current_bid_ * 0.9;  // Safety: at least 10% below

        Trade* trade = engine.OpenMarketOrder(TradeDirection::SELL, final_lots, 0.0, tp);
        if (trade) {
            stats_.total_entries++;
            return true;
        }
        return false;
    }

    void OpenNew(TickBasedEngine& engine) {
        int positions_total = 0;
        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->direction == TradeDirection::SELL) {
                positions_total++;
            }
        }

        // Safety: max position cap
        if (config_.max_positions > 0 && positions_total >= config_.max_positions) {
            return;
        }

        // Safety: equity stop - halt entries if DD exceeds threshold
        if (config_.equity_stop_pct > 0 && peak_equity_ > 0) {
            double dd = (peak_equity_ - current_equity_) / peak_equity_ * 100.0;
            if (dd >= config_.equity_stop_pct) {
                return;
            }
        }

        if (positions_total == 0) {
            // First entry
            double lots = CalculateLotSize(engine, 0);
            if (Open(lots, engine)) {
                highest_sell_ = current_bid_;
                lowest_sell_ = current_bid_;
            }
        } else {
            // Entry on rally above highest_sell + spacing
            if (current_bid_ >= highest_sell_ + current_spacing_) {
                double lots = CalculateLotSize(engine, positions_total);
                if (Open(lots, engine)) {
                    highest_sell_ = current_bid_;
                }
            }
            // Entry on dip below lowest_sell - spacing (fill down on dips too)
            else if (current_bid_ <= lowest_sell_ - current_spacing_) {
                double lots = CalculateLotSize(engine, positions_total);
                if (Open(lots, engine)) {
                    lowest_sell_ = current_bid_;
                }
            }
        }
    }
};

} // namespace backtest

#endif // FILL_DOWN_OSCILLATION_H
