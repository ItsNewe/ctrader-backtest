#ifndef STRATEGY_REFLEXIVITY_H
#define STRATEGY_REFLEXIVITY_H

#include "tick_based_engine.h"
#include <cfloat>
#include <cmath>
#include <algorithm>

namespace backtest {

/**
 * REFLEXIVITY-AWARE GRID STRATEGY
 *
 * Jū principle: Go with the flow - ride positive feedback, pause negative.
 *
 * Soros's Reflexivity: Market participants' beliefs affect market reality,
 * which in turn affects their beliefs - creating feedback loops.
 *
 * Concepts:
 * 1. POSITIVE FEEDBACK: Our buying pushes price up -> we profit -> can buy more
 *    - Ride this wave: continue accumulating
 *
 * 2. NEGATIVE FEEDBACK: Our buying absorbed by selling -> price stays flat
 *    - Pause and wait: market has absorbed our liquidity
 *
 * Detection methods:
 * - Track entry price vs price movement after entry
 * - If price rises after our buys: positive feedback (continue)
 * - If price falls despite our buys: negative feedback (pause)
 *
 * Key insight: We're always a liquidity provider. When our liquidity is
 * absorbed without price impact, something is "taking the other side" -
 * likely a larger, more informed player.
 */
class StrategyReflexivity {
public:
    enum FeedbackMode {
        ALWAYS_TRADE,       // Baseline: ignore feedback
        PAUSE_ON_NEGATIVE,  // Pause entries during negative feedback
        SCALE_WITH_FEEDBACK // Scale lot size with feedback strength
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

        // Reflexivity parameters
        FeedbackMode feedback_mode;
        int feedback_lookback_trades;    // Number of recent trades to analyze
        double negative_threshold;       // % price drop after entry to trigger negative
        double positive_threshold;       // % price rise after entry to trigger positive
        int min_trades_for_feedback;     // Min trades before feedback detection active
        double negative_lot_mult;        // Lot multiplier during negative feedback
        double positive_lot_mult;        // Lot multiplier during positive feedback

        Config()
            : survive_pct(13.0),
              base_spacing(1.50),
              min_volume(0.01),
              max_volume(10.0),
              contract_size(100.0),
              leverage(500.0),
              volatility_lookback_hours(4.0),
              typical_vol_pct(0.55),
              feedback_mode(PAUSE_ON_NEGATIVE),
              feedback_lookback_trades(5),
              negative_threshold(-0.1),   // -0.1% price drop = negative
              positive_threshold(0.1),    // +0.1% price rise = positive
              min_trades_for_feedback(5),
              negative_lot_mult(0.5),
              positive_lot_mult(1.5) {}
    };

    struct FeedbackStats {
        int positive_feedback_count = 0;
        int negative_feedback_count = 0;
        int neutral_count = 0;
        int entries_blocked = 0;
        double current_feedback_score = 0.0;  // Running average
    };

    struct TradeRecord {
        double entry_price;
        double price_at_5_ticks_later;
        double price_change_pct;
    };

    explicit StrategyReflexivity(const Config& config)
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
          ticks_since_last_entry_(0),
          last_entry_price_(0.0),
          pending_feedback_check_(false),
          total_entries_(0)
    {
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_spread_ = tick.spread();
        current_equity_ = engine.GetEquity();
        current_balance_ = engine.GetBalance();

        // Update volatility
        UpdateVolatility(tick);
        UpdateAdaptiveSpacing();

        // Process existing positions
        Iterate(engine);

        // Track feedback from recent entry
        if (pending_feedback_check_) {
            ticks_since_last_entry_++;
            if (ticks_since_last_entry_ >= 5) {
                RecordFeedback();
            }
        }

        // Open new positions
        OpenNew(engine);
    }

    const FeedbackStats& GetFeedbackStats() const { return feedback_stats_; }
    double GetCurrentSpacing() const { return current_spacing_; }
    int GetTotalEntries() const { return total_entries_; }

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

    // Feedback tracking
    int ticks_since_last_entry_;
    double last_entry_price_;
    bool pending_feedback_check_;
    std::vector<TradeRecord> recent_feedback_;
    int total_entries_;

    FeedbackStats feedback_stats_;

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

    void RecordFeedback() {
        double price_change_pct = (current_bid_ - last_entry_price_) / last_entry_price_ * 100.0;

        TradeRecord record;
        record.entry_price = last_entry_price_;
        record.price_at_5_ticks_later = current_bid_;
        record.price_change_pct = price_change_pct;

        recent_feedback_.push_back(record);

        // Keep only the lookback window
        while ((int)recent_feedback_.size() > config_.feedback_lookback_trades) {
            recent_feedback_.erase(recent_feedback_.begin());
        }

        // Update stats
        if (price_change_pct >= config_.positive_threshold) {
            feedback_stats_.positive_feedback_count++;
        } else if (price_change_pct <= config_.negative_threshold) {
            feedback_stats_.negative_feedback_count++;
        } else {
            feedback_stats_.neutral_count++;
        }

        // Calculate running feedback score
        if (!recent_feedback_.empty()) {
            double sum = 0.0;
            for (const auto& r : recent_feedback_) {
                sum += r.price_change_pct;
            }
            feedback_stats_.current_feedback_score = sum / recent_feedback_.size();
        }

        pending_feedback_check_ = false;
    }

    bool IsNegativeFeedback() const {
        if ((int)recent_feedback_.size() < config_.min_trades_for_feedback) {
            return false;  // Not enough data
        }

        return feedback_stats_.current_feedback_score < config_.negative_threshold;
    }

    bool IsPositiveFeedback() const {
        if ((int)recent_feedback_.size() < config_.min_trades_for_feedback) {
            return false;  // Not enough data
        }

        return feedback_stats_.current_feedback_score > config_.positive_threshold;
    }

    double GetFeedbackMultiplier() const {
        if (config_.feedback_mode == ALWAYS_TRADE) {
            return 1.0;
        }

        if ((int)recent_feedback_.size() < config_.min_trades_for_feedback) {
            return 1.0;  // Default until we have data
        }

        if (config_.feedback_mode == PAUSE_ON_NEGATIVE) {
            if (IsNegativeFeedback()) {
                return 0.0;  // Block entry
            }
            return 1.0;
        }

        if (config_.feedback_mode == SCALE_WITH_FEEDBACK) {
            if (IsNegativeFeedback()) {
                return config_.negative_lot_mult;
            }
            if (IsPositiveFeedback()) {
                return config_.positive_lot_mult;
            }
            return 1.0;
        }

        return 1.0;
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
        if (trade != nullptr) {
            // Start tracking for feedback
            last_entry_price_ = current_ask_;
            ticks_since_last_entry_ = 0;
            pending_feedback_check_ = true;
            total_entries_++;
            return true;
        }
        return false;
    }

    void OpenNew(TickBasedEngine& engine) {
        int positions_total = (int)engine.GetOpenPositions().size();

        if (positions_total == 0) {
            // First position - always open
            double lots = CalculateLotSize(engine, positions_total);
            double tp = current_ask_ + current_spread_ + current_spacing_;
            if (Open(lots, tp, engine)) {
                highest_buy_ = current_ask_;
                lowest_buy_ = current_ask_;
            }
        } else {
            // Additional positions - check feedback
            if (lowest_buy_ >= current_ask_ + current_spacing_) {
                double feedback_mult = GetFeedbackMultiplier();

                if (feedback_mult <= 0.0) {
                    feedback_stats_.entries_blocked++;
                    return;
                }

                double lots = CalculateLotSize(engine, positions_total);
                lots *= feedback_mult;

                if (lots < config_.min_volume) {
                    lots = config_.min_volume;
                }

                double tp = current_ask_ + current_spread_ + current_spacing_;
                if (Open(lots, tp, engine)) {
                    lowest_buy_ = current_ask_;
                }
            }
        }
    }
};

} // namespace backtest

#endif // STRATEGY_REFLEXIVITY_H
