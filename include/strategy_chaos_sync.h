#ifndef STRATEGY_CHAOS_SYNC_H
#define STRATEGY_CHAOS_SYNC_H

#include "tick_based_engine.h"
#include <deque>
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>
#include <unordered_map>

namespace backtest {

/**
 * Chaos Synchronization Strategy
 *
 * Concept: Normally-correlated instruments (XAUUSD/XAGUSD) can desynchronize
 * then re-synchronize. Trade the convergence when they diverge.
 *
 * The gold/silver ratio has historically ranged from 50-90, with a long-term
 * mean around 60-70. When the ratio deviates significantly from its rolling
 * mean, we expect mean reversion.
 *
 * Trading logic:
 * - Track the gold/silver ratio over time
 * - Calculate rolling mean and standard deviation
 * - When ratio > mean + X*stddev: ratio is high, expect gold to underperform
 *   -> SELL gold (or BUY silver)
 * - When ratio < mean - X*stddev: ratio is low, expect gold to outperform
 *   -> BUY gold (or SELL silver)
 * - Exit when ratio returns to mean
 *
 * This strategy requires synchronized tick data from both instruments.
 * We use time-bucket matching (1-second buckets) to align prices.
 */
class StrategyChaosSync {
public:
    struct Config {
        // Lookback period for rolling statistics (in buckets/seconds)
        int lookback_seconds = 3600;  // 1 hour

        // Entry threshold in standard deviations
        double entry_std_devs = 2.0;

        // Exit threshold (return to mean)
        double exit_std_devs = 0.5;

        // Position sizing
        double lot_size = 0.01;
        double max_lots = 0.10;

        // Note: contract_size/leverage removed — now read from engine.GetConfig()

        // Risk management
        double max_positions = 5;
        double sl_pct = 1.0;  // 1% stop loss
        double tp_pct = 2.0;  // 2% take profit (ratio reversion)

        // Warmup period
        int warmup_buckets = 300;  // 5 minutes of data

        // Time bucket size in seconds
        int bucket_size_seconds = 1;

        // Mode: GOLD_ONLY (trade gold based on ratio) or BOTH (also need silver engine)
        bool gold_only = true;

        Config() = default;
    };

    explicit StrategyChaosSync(const Config& config)
        : config_(config),
          gold_price_(0.0),
          silver_price_(0.0),
          current_ratio_(0.0),
          rolling_mean_(0.0),
          rolling_std_(0.0),
          peak_equity_(0.0),
          max_dd_(0.0),
          warmup_complete_(false),
          position_direction_(0),
          entry_ratio_(0.0),
          entry_price_(0.0),
          ticks_processed_(0),
          entries_(0),
          exits_(0),
          ratio_signals_high_(0),
          ratio_signals_low_(0),
          last_bucket_seconds_(0)
    {
    }

    // Update with external silver price (for synchronized backtesting)
    void UpdateSilverPrice(double silver_bid) {
        silver_price_ = silver_bid;
    }

    // Get current silver price from time-aligned data
    double GetSilverPriceForTime(const std::string& timestamp) {
        long seconds = ParseTimestampToSeconds(timestamp);
        long bucket = seconds / config_.bucket_size_seconds;

        auto it = silver_price_buckets_.find(bucket);
        if (it != silver_price_buckets_.end()) {
            return it->second;
        }
        return 0.0;  // No matching data
    }

    // Pre-load silver tick data into time buckets
    void LoadSilverData(const std::vector<Tick>& silver_ticks) {
        silver_price_buckets_.clear();

        for (const auto& tick : silver_ticks) {
            long seconds = ParseTimestampToSeconds(tick.timestamp);
            long bucket = seconds / config_.bucket_size_seconds;

            // Store the latest price for each bucket
            silver_price_buckets_[bucket] = tick.bid;
        }

        silver_data_loaded_ = true;
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        gold_price_ = tick.bid;
        ticks_processed_++;

        // Track equity for DD
        double equity = engine.GetEquity();
        if (peak_equity_ == 0.0) peak_equity_ = equity;
        if (equity > peak_equity_) peak_equity_ = equity;
        double dd = (peak_equity_ - equity) / peak_equity_ * 100.0;
        if (dd > max_dd_) max_dd_ = dd;

        // Get silver price for this timestamp
        if (silver_data_loaded_) {
            silver_price_ = GetSilverPriceForTime(tick.timestamp);
        }

        if (silver_price_ <= 0.0 || gold_price_ <= 0.0) {
            return;  // Need both prices
        }

        // Calculate gold/silver ratio
        current_ratio_ = gold_price_ / silver_price_;

        // Store in time-bucketed history
        long seconds = ParseTimestampToSeconds(tick.timestamp);
        long bucket = seconds / config_.bucket_size_seconds;

        if (bucket != last_bucket_seconds_) {
            // New time bucket - store ratio
            ratio_history_.push_back(current_ratio_);

            // Keep only lookback period
            int max_buckets = config_.lookback_seconds / config_.bucket_size_seconds;
            while (ratio_history_.size() > static_cast<size_t>(max_buckets)) {
                ratio_history_.pop_front();
            }

            last_bucket_seconds_ = bucket;
        }

        // Need enough history for statistics
        int min_buckets = std::max(config_.warmup_buckets, 30);
        if (ratio_history_.size() < static_cast<size_t>(min_buckets)) {
            return;
        }

        warmup_complete_ = true;

        // Calculate rolling mean and standard deviation
        CalculateRollingStats();

        // Calculate z-score
        double z_score = 0.0;
        if (rolling_std_ > 0.0) {
            z_score = (current_ratio_ - rolling_mean_) / rolling_std_;
        }

        // Trading logic
        int positions = engine.GetOpenPositions().size();

        // Check existing positions for exit
        if (position_direction_ != 0) {
            CheckExit(tick, engine, z_score);
        }

        // Check for new entries
        if (position_direction_ == 0 && positions < config_.max_positions) {
            CheckEntry(tick, engine, z_score);
        }
    }

    // Statistics
    double GetMaxDDPct() const { return max_dd_; }
    double GetCurrentRatio() const { return current_ratio_; }
    double GetRollingMean() const { return rolling_mean_; }
    double GetRollingStd() const { return rolling_std_; }
    int GetEntries() const { return entries_; }
    int GetExits() const { return exits_; }
    int GetRatioSignalsHigh() const { return ratio_signals_high_; }
    int GetRatioSignalsLow() const { return ratio_signals_low_; }
    bool IsWarmupComplete() const { return warmup_complete_; }
    size_t GetRatioHistorySize() const { return ratio_history_.size(); }
    int GetPositionDirection() const { return position_direction_; }

    // Get ratio statistics for analysis
    struct RatioStats {
        double min_ratio;
        double max_ratio;
        double avg_ratio;
        double std_ratio;
        size_t samples;
    };

    RatioStats GetRatioStats() const {
        RatioStats stats = {0, 0, 0, 0, 0};
        if (ratio_history_.empty()) return stats;

        double sum = 0.0;
        double min_r = 1e9;
        double max_r = 0.0;

        for (double r : ratio_history_) {
            sum += r;
            min_r = std::min(min_r, r);
            max_r = std::max(max_r, r);
        }

        stats.samples = ratio_history_.size();
        stats.avg_ratio = sum / stats.samples;
        stats.min_ratio = min_r;
        stats.max_ratio = max_r;

        // Calculate std
        double sum_sq = 0.0;
        for (double r : ratio_history_) {
            sum_sq += (r - stats.avg_ratio) * (r - stats.avg_ratio);
        }
        stats.std_ratio = std::sqrt(sum_sq / stats.samples);

        return stats;
    }

private:
    Config config_;

    // Price tracking
    double gold_price_;
    double silver_price_;
    double current_ratio_;

    // Rolling statistics
    double rolling_mean_;
    double rolling_std_;
    std::deque<double> ratio_history_;

    // Time-bucketed silver prices
    std::unordered_map<long, double> silver_price_buckets_;
    bool silver_data_loaded_ = false;

    // Equity tracking
    double peak_equity_;
    double max_dd_;

    // State
    bool warmup_complete_;
    int position_direction_;  // 0 = none, 1 = long gold (ratio low), -1 = short gold (ratio high)
    double entry_ratio_;
    double entry_price_;

    // Statistics
    long ticks_processed_;
    int entries_;
    int exits_;
    int ratio_signals_high_;
    int ratio_signals_low_;
    long last_bucket_seconds_;

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

    void CalculateRollingStats() {
        if (ratio_history_.empty()) return;

        // Calculate mean
        double sum = 0.0;
        for (double r : ratio_history_) {
            sum += r;
        }
        rolling_mean_ = sum / ratio_history_.size();

        // Calculate standard deviation
        double sum_sq = 0.0;
        for (double r : ratio_history_) {
            sum_sq += (r - rolling_mean_) * (r - rolling_mean_);
        }
        rolling_std_ = std::sqrt(sum_sq / ratio_history_.size());
    }

    void CheckEntry(const Tick& tick, TickBasedEngine& engine, double z_score) {
        // Ratio too high (gold expensive relative to silver) -> SHORT gold
        if (z_score > config_.entry_std_devs) {
            ratio_signals_high_++;

            // Enter short gold position
            double sl = tick.ask * (1.0 + config_.sl_pct / 100.0);
            double tp = tick.ask * (1.0 - config_.tp_pct / 100.0);

            Trade* trade = engine.OpenMarketOrder("SELL", config_.lot_size, sl, tp);
            if (trade) {
                position_direction_ = -1;
                entry_ratio_ = current_ratio_;
                entry_price_ = tick.bid;
                entries_++;
            }
        }
        // Ratio too low (gold cheap relative to silver) -> LONG gold
        else if (z_score < -config_.entry_std_devs) {
            ratio_signals_low_++;

            // Enter long gold position
            double sl = tick.bid * (1.0 - config_.sl_pct / 100.0);
            double tp = tick.bid * (1.0 + config_.tp_pct / 100.0);

            Trade* trade = engine.OpenMarketOrder("BUY", config_.lot_size, sl, tp);
            if (trade) {
                position_direction_ = 1;
                entry_ratio_ = current_ratio_;
                entry_price_ = tick.ask;
                entries_++;
            }
        }
    }

    void CheckExit(const Tick& tick, TickBasedEngine& engine, double z_score) {
        bool should_exit = false;
        std::string reason = "";

        // Exit when ratio returns to mean
        if (position_direction_ == -1 && z_score < config_.exit_std_devs) {
            // Was short (ratio was high), now ratio has normalized
            should_exit = true;
            reason = "Ratio normalized (was high)";
        }
        else if (position_direction_ == 1 && z_score > -config_.exit_std_devs) {
            // Was long (ratio was low), now ratio has normalized
            should_exit = true;
            reason = "Ratio normalized (was low)";
        }

        if (should_exit) {
            // Close all positions
            auto positions = engine.GetOpenPositions();
            for (Trade* trade : positions) {
                engine.ClosePosition(trade, reason);
            }
            position_direction_ = 0;
            entry_ratio_ = 0.0;
            exits_++;
        }
    }
};

} // namespace backtest

#endif // STRATEGY_CHAOS_SYNC_H
