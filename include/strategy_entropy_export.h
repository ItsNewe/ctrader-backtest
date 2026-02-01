#ifndef STRATEGY_ENTROPY_EXPORT_H
#define STRATEGY_ENTROPY_EXPORT_H

#include "tick_based_engine.h"
#include <vector>
#include <deque>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <unordered_set>

namespace backtest {

/**
 * Entropy Export Strategy (Dissipative Structure)
 *
 * CHAOS CONCEPT #6: Systems maintain order by exporting entropy (like a hurricane
 * dissipating heat). This strategy deliberately takes small, frequent losses on
 * a subset of positions ("entropy" positions) to potentially enable the main grid
 * ("core" positions) to capture larger oscillations with reduced overall drawdown.
 *
 * DUAL-MODE POSITIONS:
 *   - "Core" positions: Normal FillUp behavior (no stops, wait for TP)
 *   - "Entropy" positions: Tight stop-loss (e.g., 0.5x spacing), same TP
 *
 * The hypothesis: By exporting small losses quickly (entropy export), the overall
 * system might maintain better "order" (lower DD) while still capturing profits.
 */
class StrategyEntropyExport {
public:
    struct Config {
        // Base FillUp parameters
        double survive_pct;         // Max adverse price move to survive (%)
        double base_spacing;        // Grid spacing ($)
        double min_volume;          // Min lot size per trade
        double max_volume;          // Max lot size per trade
        double contract_size;       // e.g., 100 for XAUUSD
        double leverage;            // e.g., 500

        // Entropy export parameters
        double core_ratio;          // Fraction of positions that are "core" (0.0-1.0)
        double entropy_sl_mult;     // Stop-loss for entropy positions as mult of spacing (e.g., 0.5)
        bool entropy_smaller_size;  // If true, entropy positions use half the lot size

        // Adaptive spacing (from FillUpOscillation)
        double volatility_lookback_hours;
        double typical_vol_pct;     // e.g., 0.5 for XAUUSD

        Config()
            : survive_pct(13.0),
              base_spacing(1.5),
              min_volume(0.01),
              max_volume(10.0),
              contract_size(100.0),
              leverage(500.0),
              core_ratio(0.7),       // 70% core, 30% entropy by default
              entropy_sl_mult(0.5),  // Entropy SL at 50% of spacing
              entropy_smaller_size(false),
              volatility_lookback_hours(4.0),
              typical_vol_pct(0.5)
        {}
    };

    // Statistics tracking
    struct Stats {
        int core_trades_opened;
        int core_trades_closed_tp;
        int entropy_trades_opened;
        int entropy_trades_closed_tp;
        int entropy_trades_closed_sl;
        double total_entropy_losses;
        double total_core_profits;
        double total_entropy_profits;
        int adaptive_spacing_changes;

        Stats()
            : core_trades_opened(0), core_trades_closed_tp(0),
              entropy_trades_opened(0), entropy_trades_closed_tp(0),
              entropy_trades_closed_sl(0), total_entropy_losses(0),
              total_core_profits(0), total_entropy_profits(0),
              adaptive_spacing_changes(0) {}
    };

    StrategyEntropyExport(const Config& cfg)
        : cfg_(cfg),
          lowest_buy_(DBL_MAX),
          highest_buy_(DBL_MIN),
          volume_of_open_trades_(0.0),
          current_ask_(0.0),
          current_bid_(0.0),
          current_spread_(0.0),
          current_equity_(0.0),
          current_balance_(0.0),
          peak_equity_(0.0),
          current_spacing_(cfg.base_spacing),
          recent_high_(0.0),
          recent_low_(DBL_MAX),
          ticks_processed_(0),
          position_counter_(0),
          last_vol_reset_seconds_(0)
    {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_spread_ = tick.spread();
        current_timestamp_ = tick.timestamp;
        current_equity_ = engine.GetEquity();
        current_balance_ = engine.GetBalance();
        ticks_processed_++;

        // Initialize
        if (peak_equity_ == 0.0) {
            peak_equity_ = current_equity_;
        }

        // Update peak
        if (current_equity_ > peak_equity_) {
            peak_equity_ = current_equity_;
        }

        // Track volatility for adaptive spacing
        UpdateVolatility(tick);

        // Update adaptive spacing
        UpdateAdaptiveSpacing();

        // Check entropy position stop-losses manually
        CheckEntropyStopLosses(engine);

        // Process positions to track state
        Iterate(engine);

        // Open new positions
        OpenNew(engine);
    }

    const Stats& GetStats() const { return stats_; }
    double GetCurrentSpacing() const { return current_spacing_; }
    double GetPeakEquity() const { return peak_equity_; }

private:
    Config cfg_;
    Stats stats_;

    // Position tracking
    double lowest_buy_;
    double highest_buy_;
    double volume_of_open_trades_;
    std::unordered_set<long> entropy_position_ids_;  // Track which positions are entropy
    std::unordered_set<long> core_position_ids_;     // Track which positions are core

    // Market state
    double current_ask_;
    double current_bid_;
    double current_spread_;
    double current_equity_;
    double current_balance_;
    double peak_equity_;
    std::string current_timestamp_;

    // Adaptive spacing
    double current_spacing_;
    double recent_high_;
    double recent_low_;
    long ticks_processed_;
    long position_counter_;  // For alternating core/entropy
    long last_vol_reset_seconds_;

    // Parse timestamp to seconds
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
        long lookback_seconds = (long)(cfg_.volatility_lookback_hours * 3600.0);

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
            double typical_vol = current_bid_ * (cfg_.typical_vol_pct / 100.0);
            double vol_ratio = range / typical_vol;
            vol_ratio = std::max(0.5, std::min(3.0, vol_ratio));

            double new_spacing = cfg_.base_spacing * vol_ratio;
            new_spacing = std::max(0.5, std::min(5.0, new_spacing));

            if (std::abs(new_spacing - current_spacing_) > 0.1) {
                current_spacing_ = new_spacing;
                stats_.adaptive_spacing_changes++;
            }
        }
    }

    void CheckEntropyStopLosses(TickBasedEngine& engine) {
        // Check each entropy position for stop-loss hit
        double sl_distance = current_spacing_ * cfg_.entropy_sl_mult;

        std::vector<Trade*> to_close;
        for (Trade* trade : engine.GetOpenPositions()) {
            if (entropy_position_ids_.count(trade->id) > 0) {
                // This is an entropy position - check SL
                double current_loss = (trade->entry_price - current_bid_) * trade->lot_size * cfg_.contract_size;
                double sl_level = trade->entry_price - sl_distance;

                if (current_bid_ <= sl_level) {
                    to_close.push_back(trade);
                }
            }
        }

        for (Trade* trade : to_close) {
            double loss = (trade->entry_price - current_bid_) * trade->lot_size * cfg_.contract_size;
            stats_.total_entropy_losses += loss;
            stats_.entropy_trades_closed_sl++;
            entropy_position_ids_.erase(trade->id);
            engine.ClosePosition(trade, "ENTROPY_SL");
        }
    }

    void Iterate(TickBasedEngine& engine) {
        lowest_buy_ = DBL_MAX;
        highest_buy_ = DBL_MIN;
        volume_of_open_trades_ = 0.0;

        // Also clean up closed positions from our tracking sets
        std::unordered_set<long> open_ids;
        for (const Trade* trade : engine.GetOpenPositions()) {
            open_ids.insert(trade->id);
            if (trade->IsBuy()) {
                volume_of_open_trades_ += trade->lot_size;
                lowest_buy_ = std::min(lowest_buy_, trade->entry_price);
                highest_buy_ = std::max(highest_buy_, trade->entry_price);
            }
        }

        // Track profits from closed positions
        // (This would require additional tracking of trade IDs we've already counted)
        // For now, we track on open/close events

        // Clean up tracking sets
        std::vector<long> to_remove_core, to_remove_entropy;
        for (long id : core_position_ids_) {
            if (open_ids.count(id) == 0) {
                to_remove_core.push_back(id);
                stats_.core_trades_closed_tp++;
            }
        }
        for (long id : entropy_position_ids_) {
            if (open_ids.count(id) == 0) {
                // Already counted SL closes, so this must be TP
                to_remove_entropy.push_back(id);
                stats_.entropy_trades_closed_tp++;
            }
        }
        for (long id : to_remove_core) core_position_ids_.erase(id);
        for (long id : to_remove_entropy) entropy_position_ids_.erase(id);
    }

    double CalculateLotSize(TickBasedEngine& engine, int positions_total, bool is_entropy) {
        double used_margin = 0.0;
        for (const Trade* trade : engine.GetOpenPositions()) {
            used_margin += trade->lot_size * cfg_.contract_size * trade->entry_price / cfg_.leverage;
        }

        double margin_stop_out = 20.0;

        double end_price = (positions_total == 0)
            ? current_ask_ * ((100.0 - cfg_.survive_pct) / 100.0)
            : highest_buy_ * ((100.0 - cfg_.survive_pct) / 100.0);

        double distance = current_ask_ - end_price;
        double number_of_trades = std::floor(distance / current_spacing_);
        if (number_of_trades <= 0) number_of_trades = 1;

        double equity_at_target = current_equity_ - volume_of_open_trades_ * distance * cfg_.contract_size;
        if (used_margin > 0 && (equity_at_target / used_margin * 100.0) < margin_stop_out) {
            return 0.0;
        }

        double trade_size = cfg_.min_volume;
        double d_equity = cfg_.contract_size * trade_size * current_spacing_ * (number_of_trades * (number_of_trades + 1) / 2);
        double d_margin = number_of_trades * trade_size * cfg_.contract_size / cfg_.leverage;

        double max_mult = cfg_.max_volume / cfg_.min_volume;
        for (double mult = max_mult; mult >= 1.0; mult -= 0.1) {
            double test_equity = equity_at_target - mult * d_equity;
            double test_margin = used_margin + mult * d_margin;
            if (test_margin > 0 && (test_equity / test_margin * 100.0) > margin_stop_out) {
                trade_size = mult * cfg_.min_volume;
                break;
            }
        }

        // Apply entropy size reduction if configured
        if (is_entropy && cfg_.entropy_smaller_size) {
            trade_size *= 0.5;
        }

        return std::min(trade_size, cfg_.max_volume);
    }

    bool IsEntropyPosition() {
        // Determine if next position should be entropy or core based on ratio
        // Use a simple counter-based approach for deterministic distribution
        position_counter_++;

        // For 70/30 core/entropy: every 10 positions, 7 are core, 3 are entropy
        // Check if this position falls in the entropy portion
        int cycle_pos = (int)((position_counter_ - 1) % 100);
        double entropy_threshold = (1.0 - cfg_.core_ratio) * 100.0;

        // First (entropy_threshold)% of each cycle are entropy
        return cycle_pos < (int)entropy_threshold;
    }

    bool Open(double lots, TickBasedEngine& engine, bool is_entropy) {
        if (lots < cfg_.min_volume) return false;

        double final_lots = std::min(lots, cfg_.max_volume);
        final_lots = std::round(final_lots * 100.0) / 100.0;

        double tp = current_ask_ + current_spread_ + current_spacing_;

        // Note: We don't set SL via engine because we manually manage entropy SLs
        // This allows different SL behavior for core vs entropy
        Trade* trade = engine.OpenMarketOrder("BUY", final_lots, 0.0, tp);

        if (trade != nullptr) {
            if (is_entropy) {
                entropy_position_ids_.insert(trade->id);
                stats_.entropy_trades_opened++;
            } else {
                core_position_ids_.insert(trade->id);
                stats_.core_trades_opened++;
            }
            return true;
        }
        return false;
    }

    void OpenNew(TickBasedEngine& engine) {
        int positions_total = engine.GetOpenPositions().size();

        if (positions_total == 0) {
            bool is_entropy = IsEntropyPosition();
            double lots = CalculateLotSize(engine, positions_total, is_entropy);
            if (Open(lots, engine, is_entropy)) {
                highest_buy_ = current_ask_;
                lowest_buy_ = current_ask_;
            }
        } else {
            if (lowest_buy_ >= current_ask_ + current_spacing_) {
                bool is_entropy = IsEntropyPosition();
                double lots = CalculateLotSize(engine, positions_total, is_entropy);
                if (Open(lots, engine, is_entropy)) {
                    lowest_buy_ = current_ask_;
                }
            } else if (highest_buy_ <= current_ask_ - current_spacing_) {
                bool is_entropy = IsEntropyPosition();
                double lots = CalculateLotSize(engine, positions_total, is_entropy);
                if (Open(lots, engine, is_entropy)) {
                    highest_buy_ = current_ask_;
                }
            }
        }
    }
};

} // namespace backtest

#endif // STRATEGY_ENTROPY_EXPORT_H
