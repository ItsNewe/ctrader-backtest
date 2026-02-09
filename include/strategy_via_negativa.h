#ifndef STRATEGY_VIA_NEGATIVA_H
#define STRATEGY_VIA_NEGATIVA_H

#include "tick_based_engine.h"
#include <vector>
#include <deque>
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace backtest {

/**
 * VIA NEGATIVA GRID STRATEGY
 *
 * Jū principle: Improvement through removal, not addition.
 * Instead of finding good entries, focus on avoiding bad ones.
 *
 * Veto conditions (any one blocks entry):
 * 1. VELOCITY_VETO: Don't enter during fast directional moves
 * 2. CONCENTRATION_VETO: Don't add when already heavily exposed
 * 3. LOSING_STREAK_VETO: Don't enter when recent trades are losing
 * 4. EXTREME_VOL_VETO: Don't enter when volatility is abnormal
 * 5. AGAINST_TREND_VETO: Don't buy during strong downtrends
 *
 * Each veto can be enabled/disabled independently.
 */
class StrategyViaNegativa {
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

        // Veto enables
        bool velocity_veto;
        bool concentration_veto;
        bool losing_streak_veto;
        bool extreme_vol_veto;
        bool against_trend_veto;

        // Veto thresholds
        double velocity_threshold_pct;    // Max % move per hour to allow entry
        int max_positions;                // Max positions before concentration veto
        int losing_streak_count;          // Consecutive losses to trigger veto
        double vol_extreme_mult;          // Vol > typical * mult triggers veto
        double trend_threshold_pct;       // Trend slope to trigger against-trend veto
        int trend_lookback_ticks;         // Ticks to measure trend

        Config()
            : survive_pct(13.0),
              base_spacing(1.50),
              min_volume(0.01),
              max_volume(10.0),
              contract_size(100.0),
              leverage(500.0),
              volatility_lookback_hours(4.0),
              typical_vol_pct(0.55),
              velocity_veto(false),
              concentration_veto(false),
              losing_streak_veto(false),
              extreme_vol_veto(false),
              against_trend_veto(false),
              velocity_threshold_pct(0.5),
              max_positions(50),
              losing_streak_count(5),
              vol_extreme_mult(2.0),
              trend_threshold_pct(0.3),
              trend_lookback_ticks(10000) {}
    };

    struct VetoStats {
        long velocity_vetoes = 0;
        long concentration_vetoes = 0;
        long losing_streak_vetoes = 0;
        long extreme_vol_vetoes = 0;
        long against_trend_vetoes = 0;
        long entries_allowed = 0;
        long total_entry_attempts = 0;
    };

    explicit StrategyViaNegativa(const Config& config)
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
          consecutive_losses_(0),
          last_trade_profit_(0.0),
          ticks_processed_(0)
    {
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_spread_ = tick.spread();
        current_equity_ = engine.GetEquity();
        current_balance_ = engine.GetBalance();
        ticks_processed_++;

        // Track price history for trend/velocity
        price_history_.push_back(current_bid_);
        while ((int)price_history_.size() > config_.trend_lookback_ticks + 10) {
            price_history_.pop_front();
        }

        // Update volatility
        UpdateVolatility(tick);
        UpdateAdaptiveSpacing();

        // Track closed trades for losing streak
        TrackClosedTrades(engine);

        // Process existing positions
        Iterate(engine);

        // Open new positions (with veto checks)
        OpenNew(engine);
    }

    const VetoStats& GetVetoStats() const { return veto_stats_; }
    double GetCurrentSpacing() const { return current_spacing_; }
    long GetTicksProcessed() const { return ticks_processed_; }

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

    std::deque<double> price_history_;
    int consecutive_losses_;
    double last_trade_profit_;
    std::vector<int> closed_trade_ids_;

    long ticks_processed_;
    VetoStats veto_stats_;

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

    void TrackClosedTrades(TickBasedEngine& engine) {
        // Check for newly closed trades
        for (const Trade& trade : engine.GetClosedTrades()) {
            bool already_tracked = false;
            for (int id : closed_trade_ids_) {
                if (id == trade.id) {
                    already_tracked = true;
                    break;
                }
            }

            if (!already_tracked) {
                closed_trade_ids_.push_back(trade.id);

                // Calculate profit
                double profit = trade.profit_loss;

                if (profit < 0) {
                    consecutive_losses_++;
                } else {
                    consecutive_losses_ = 0;
                }

                last_trade_profit_ = profit;
            }
        }

        // Limit memory
        if (closed_trade_ids_.size() > 10000) {
            closed_trade_ids_.erase(closed_trade_ids_.begin(),
                                   closed_trade_ids_.begin() + 5000);
        }
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

    // === VETO CHECKS ===

    bool CheckVelocityVeto() {
        if (!config_.velocity_veto) return false;
        if (price_history_.size() < 1000) return false;

        // Measure velocity over last 1000 ticks (~1 hour equivalent)
        double old_price = price_history_[price_history_.size() - 1000];
        double velocity_pct = (current_bid_ - old_price) / old_price * 100.0;

        // Veto if moving down too fast
        if (velocity_pct < -config_.velocity_threshold_pct) {
            return true;
        }
        return false;
    }

    bool CheckConcentrationVeto(TickBasedEngine& engine) {
        if (!config_.concentration_veto) return false;
        return (int)engine.GetOpenPositions().size() >= config_.max_positions;
    }

    bool CheckLosingStreakVeto() {
        if (!config_.losing_streak_veto) return false;
        return consecutive_losses_ >= config_.losing_streak_count;
    }

    bool CheckExtremeVolVeto() {
        if (!config_.extreme_vol_veto) return false;

        double range = recent_high_ - recent_low_;
        double typical_vol = current_bid_ * (config_.typical_vol_pct / 100.0);

        if (range > typical_vol * config_.vol_extreme_mult) {
            return true;
        }
        return false;
    }

    bool CheckAgainstTrendVeto() {
        if (!config_.against_trend_veto) return false;
        if ((int)price_history_.size() < config_.trend_lookback_ticks) return false;

        double old_price = price_history_[price_history_.size() - config_.trend_lookback_ticks];
        double trend_pct = (current_bid_ - old_price) / old_price * 100.0;

        // Veto if trend is strongly negative (buying against downtrend)
        if (trend_pct < -config_.trend_threshold_pct) {
            return true;
        }
        return false;
    }

    bool ShouldVetoEntry(TickBasedEngine& engine) {
        veto_stats_.total_entry_attempts++;

        if (CheckVelocityVeto()) {
            veto_stats_.velocity_vetoes++;
            return true;
        }

        if (CheckConcentrationVeto(engine)) {
            veto_stats_.concentration_vetoes++;
            return true;
        }

        if (CheckLosingStreakVeto()) {
            veto_stats_.losing_streak_vetoes++;
            return true;
        }

        if (CheckExtremeVolVeto()) {
            veto_stats_.extreme_vol_vetoes++;
            return true;
        }

        if (CheckAgainstTrendVeto()) {
            veto_stats_.against_trend_vetoes++;
            return true;
        }

        veto_stats_.entries_allowed++;
        return false;
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
            // First position - no veto on first entry
            double lots = CalculateLotSize(engine, positions_total);
            double tp = current_ask_ + current_spread_ + current_spacing_;
            if (Open(lots, tp, engine)) {
                highest_buy_ = current_ask_;
                lowest_buy_ = current_ask_;
            }
        } else {
            // Additional positions - check vetoes
            if (lowest_buy_ >= current_ask_ + current_spacing_) {
                // Check all veto conditions
                if (ShouldVetoEntry(engine)) {
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

#endif // STRATEGY_VIA_NEGATIVA_H
