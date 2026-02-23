#ifndef STRATEGY_ENTROPY_EXPORT_V2_H
#define STRATEGY_ENTROPY_EXPORT_V2_H

#include "tick_based_engine.h"
#include <vector>
#include <deque>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <unordered_map>

namespace backtest {

/**
 * Entropy Export Strategy V2 (Dissipative Structure)
 *
 * CHAOS CONCEPT #6: Ordered structures maintain themselves by exporting entropy.
 * A hurricane maintains its structure by dissipating heat.
 *
 * For trading: deliberately take small, frequent losses (entropy export) to enable
 * the larger profit structure to persist. This is the OPPOSITE of "never close at
 * a loss" - it's systematically cutting losers to preserve capital for winners.
 *
 * IMPLEMENTATION:
 *   - Run normal FillUpOscillation grid strategy
 *   - For each position, track time open and unrealized P/L
 *   - If position is losing AND open > threshold time: close it (export entropy)
 *   - This cuts losers that aren't reverting, freeing capital for new positions
 *
 * MODES:
 *   - TIME_ONLY: Close after N minutes regardless of loss
 *   - LOSS_ONLY: Close if unrealized loss > threshold
 *   - TIME_AND_LOSS: Close only if BOTH conditions met (conservative)
 *   - TIME_OR_LOSS: Close if EITHER condition met (aggressive)
 */
class StrategyEntropyExportV2 {
public:
    enum ExportMode {
        BASELINE = 0,       // No entropy export (control)
        TIME_ONLY = 1,      // Close losers after time threshold
        LOSS_ONLY = 2,      // Close losers exceeding loss threshold
        TIME_AND_LOSS = 3,  // Close only if BOTH conditions met
        TIME_OR_LOSS = 4    // Close if EITHER condition met
    };

    struct Config {
        // Base FillUp parameters
        double survive_pct;         // Max adverse price move to survive (%)
        double base_spacing;        // Grid spacing ($)

        // Entropy export parameters
        ExportMode mode;
        double time_threshold_minutes;   // Close losers older than this (0=disabled)
        double loss_threshold_dollars;   // Close losers with unrealized loss > this per position (0=disabled)

        // Adaptive spacing (from FillUpOscillation)
        double volatility_lookback_hours;
        double typical_vol_pct;     // e.g., 0.5 for XAUUSD

        Config()
            : survive_pct(13.0),
              base_spacing(1.5),
              mode(BASELINE),
              time_threshold_minutes(30.0),
              loss_threshold_dollars(5.0),
              volatility_lookback_hours(4.0),
              typical_vol_pct(0.5)
        {}
    };

    // Statistics tracking
    struct Stats {
        int total_trades_opened;
        int trades_closed_tp;           // Closed by TP (profit)
        int trades_closed_entropy;      // Closed by entropy export (loss)
        double total_profit_from_tp;
        double total_loss_from_entropy;
        double avg_entropy_hold_minutes;
        double avg_tp_hold_minutes;
        int adaptive_spacing_changes;
        int max_concurrent_positions;
        double total_entropy_hold_time;
        double total_tp_hold_time;

        Stats()
            : total_trades_opened(0), trades_closed_tp(0), trades_closed_entropy(0),
              total_profit_from_tp(0), total_loss_from_entropy(0),
              avg_entropy_hold_minutes(0), avg_tp_hold_minutes(0),
              adaptive_spacing_changes(0), max_concurrent_positions(0),
              total_entropy_hold_time(0), total_tp_hold_time(0) {}
    };

    // Track per-position state
    struct PositionState {
        long open_seconds;      // Timestamp when opened (seconds since reference)
        double entry_price;     // Entry price
    };

    StrategyEntropyExportV2(const Config& cfg)
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
          last_vol_reset_seconds_(0),
          current_seconds_(0)
    {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_spread_ = tick.spread();
        current_timestamp_ = tick.timestamp;
        current_equity_ = engine.GetEquity();
        current_balance_ = engine.GetBalance();
        current_seconds_ = ParseTimestampToSeconds(tick.timestamp);
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

        // Check for entropy export (close old losers)
        if (cfg_.mode != BASELINE) {
            CheckEntropyExport(engine);
        }

        // Process positions to track state
        Iterate(engine);

        // Open new positions
        OpenNew(engine);
    }

    const Stats& GetStats() const { return stats_; }
    double GetCurrentSpacing() const { return current_spacing_; }
    double GetPeakEquity() const { return peak_equity_; }

    // Calculate final averages
    void FinalizeStats() {
        if (stats_.trades_closed_entropy > 0) {
            stats_.avg_entropy_hold_minutes = stats_.total_entropy_hold_time / stats_.trades_closed_entropy / 60.0;
        }
        if (stats_.trades_closed_tp > 0) {
            stats_.avg_tp_hold_minutes = stats_.total_tp_hold_time / stats_.trades_closed_tp / 60.0;
        }
    }

private:
    Config cfg_;
    Stats stats_;

    // Position tracking
    double lowest_buy_;
    double highest_buy_;
    double volume_of_open_trades_;
    std::unordered_map<int, PositionState> position_states_;  // trade_id -> state

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
    long last_vol_reset_seconds_;
    long current_seconds_;

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
        long lookback_seconds = (long)(cfg_.volatility_lookback_hours * 3600.0);

        if (last_vol_reset_seconds_ == 0 || current_seconds_ - last_vol_reset_seconds_ >= lookback_seconds) {
            recent_high_ = current_bid_;
            recent_low_ = current_bid_;
            last_vol_reset_seconds_ = current_seconds_;
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

    void CheckEntropyExport(TickBasedEngine& engine) {
        // Check each position for entropy export conditions
        double time_threshold_seconds = cfg_.time_threshold_minutes * 60.0;
        const auto& ecfg = engine.GetConfig();

        std::vector<Trade*> to_close;

        for (Trade* trade : engine.GetOpenPositions()) {
            // Get position state
            auto it = position_states_.find(trade->id);
            if (it == position_states_.end()) continue;

            const PositionState& state = it->second;

            // Calculate current unrealized P/L (per position)
            double unrealized_pnl = (current_bid_ - trade->entry_price) * trade->lot_size * ecfg.contract_size;

            // Only consider losers (unrealized_pnl < 0)
            if (unrealized_pnl >= 0) continue;

            // Calculate hold time
            double hold_time_seconds = (double)(current_seconds_ - state.open_seconds);

            // Check conditions based on mode
            bool time_exceeded = (cfg_.time_threshold_minutes > 0) && (hold_time_seconds >= time_threshold_seconds);
            bool loss_exceeded = (cfg_.loss_threshold_dollars > 0) && (unrealized_pnl < -cfg_.loss_threshold_dollars);

            bool should_close = false;
            switch (cfg_.mode) {
                case TIME_ONLY:
                    should_close = time_exceeded && (unrealized_pnl < 0);
                    break;
                case LOSS_ONLY:
                    should_close = loss_exceeded;
                    break;
                case TIME_AND_LOSS:
                    should_close = time_exceeded && loss_exceeded;
                    break;
                case TIME_OR_LOSS:
                    should_close = (time_exceeded && unrealized_pnl < 0) || loss_exceeded;
                    break;
                default:
                    break;
            }

            if (should_close) {
                to_close.push_back(trade);
            }
        }

        // Close entropy export positions
        for (Trade* trade : to_close) {
            auto it = position_states_.find(trade->id);
            if (it != position_states_.end()) {
                double hold_time = (double)(current_seconds_ - it->second.open_seconds);
                double loss = (current_bid_ - trade->entry_price) * trade->lot_size * ecfg.contract_size;

                stats_.trades_closed_entropy++;
                stats_.total_loss_from_entropy += loss;  // Loss is negative
                stats_.total_entropy_hold_time += hold_time;

                position_states_.erase(it);
            }
            engine.ClosePosition(trade, "ENTROPY_EXPORT");
        }
    }

    void Iterate(TickBasedEngine& engine) {
        // Use engine aggregates instead of manual calculation
        volume_of_open_trades_ = engine.GetBuyVolume();
        lowest_buy_ = engine.GetLowestBuyEntry();
        highest_buy_ = engine.GetHighestBuyEntry();

        // Track current open position IDs (needed for entropy export TP detection)
        std::unordered_map<int, bool> open_ids;
        for (const Trade* trade : engine.GetOpenPositions()) {
            open_ids[trade->id] = true;
        }

        // Track max concurrent positions
        int current_positions = (int)engine.GetOpenPositions().size();
        if (current_positions > stats_.max_concurrent_positions) {
            stats_.max_concurrent_positions = current_positions;
        }

        // Check for positions that closed via TP (no longer in open list, still in our tracking)
        std::vector<int> closed_by_tp;
        for (const auto& [id, state] : position_states_) {
            if (open_ids.find(id) == open_ids.end()) {
                // Position closed - check if it was a TP (profit) close
                // If we didn't close it via entropy export, it was TP
                closed_by_tp.push_back(id);
            }
        }

        for (int id : closed_by_tp) {
            auto it = position_states_.find(id);
            if (it != position_states_.end()) {
                double hold_time = (double)(current_seconds_ - it->second.open_seconds);
                stats_.trades_closed_tp++;
                stats_.total_tp_hold_time += hold_time;
                // Note: profit is tracked by the engine, not here
                position_states_.erase(it);
            }
        }
    }

    double CalculateLotSize(TickBasedEngine& engine, int positions_total) {
        const auto& ecfg = engine.GetConfig();
        double used_margin = engine.GetUsedMargin();

        double margin_stop_out = 20.0;

        double end_price = (positions_total == 0)
            ? current_ask_ * ((100.0 - cfg_.survive_pct) / 100.0)
            : highest_buy_ * ((100.0 - cfg_.survive_pct) / 100.0);

        double distance = current_ask_ - end_price;
        double number_of_trades = std::floor(distance / current_spacing_);
        if (number_of_trades <= 0) number_of_trades = 1;

        double equity_at_target = current_equity_ - volume_of_open_trades_ * distance * ecfg.contract_size;
        if (used_margin > 0 && (equity_at_target / used_margin * 100.0) < margin_stop_out) {
            return 0.0;
        }

        double trade_size = ecfg.volume_min;
        double d_equity = ecfg.contract_size * trade_size * current_spacing_ * (number_of_trades * (number_of_trades + 1) / 2);
        double d_margin = number_of_trades * trade_size * ecfg.contract_size / ecfg.leverage;

        double max_mult = ecfg.volume_max / ecfg.volume_min;
        for (double mult = max_mult; mult >= 1.0; mult -= 0.1) {
            double test_equity = equity_at_target - mult * d_equity;
            double test_margin = used_margin + mult * d_margin;
            if (test_margin > 0 && (test_equity / test_margin * 100.0) > margin_stop_out) {
                trade_size = mult * ecfg.volume_min;
                break;
            }
        }

        return std::min(trade_size, ecfg.volume_max);
    }

    bool Open(double lots, TickBasedEngine& engine) {
        const auto& ecfg = engine.GetConfig();
        if (lots < ecfg.volume_min) return false;

        double final_lots = engine.NormalizeLots(lots);

        double tp = current_ask_ + current_spread_ + current_spacing_;

        Trade* trade = engine.OpenMarketOrder("BUY", final_lots, 0.0, tp);

        if (trade != nullptr) {
            // Track position state for entropy export
            PositionState state;
            state.open_seconds = current_seconds_;
            state.entry_price = trade->entry_price;
            position_states_[trade->id] = state;

            stats_.total_trades_opened++;
            return true;
        }
        return false;
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

#endif // STRATEGY_ENTROPY_EXPORT_V2_H
