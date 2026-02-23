#ifndef STRATEGY_ENTROPY_HARVESTER_H
#define STRATEGY_ENTROPY_HARVESTER_H

#include "tick_based_engine.h"
#include <deque>
#include <cmath>
#include <algorithm>

namespace backtest {

/**
 * Entropy Harvesting (Maxwell's Demon) Strategy
 *
 * Maxwell's Demon selectively opens a door for favorable molecules while blocking
 * unfavorable ones. In trading: the grid acts as a one-way valve that captures
 * oscillations moving in the profitable direction while ignoring those that would hurt.
 *
 * Key difference from FillUp: This strategy is SELECTIVE.
 * - FillUp enters at every spacing level regardless of direction
 * - Maxwell's Demon only "opens the door" when tick momentum confirms reversal
 *
 * The selectivity acts like sorting fast/slow molecules:
 * - "Fast molecule" (favorable): Price dips then immediately reverses up -> ENTER
 * - "Slow molecule" (unfavorable): Price dips and keeps falling -> DON'T ENTER
 *
 * Best on: XAUUSD (281k oscillations/year, tight spread, proven mean-reversion)
 */
class EntropyHarvester {
public:
    struct Config {
        double spacing = 1.50;                // Grid spacing in price units
        double tp_distance = 1.00;            // Take profit distance
        double survive_pct = 13.0;            // Survive percentage for lot sizing
        double min_volume = 0.01;
        double max_volume = 10.0;
        int reversal_window = 30;             // Ticks to detect reversal
        int direction_threshold = 8;          // Net up-ticks needed for "fast molecule"
        int max_positions = 50;               // Max open positions
        bool require_reversal = true;         // Require reversal confirmation
        int cooldown_ticks = 50;              // Min ticks between entries
    };

    EntropyHarvester(const Config& cfg)
        : config_(cfg),
          lowest_buy_(1e18),
          ticks_processed_(0),
          entries_allowed_(0),
          entries_blocked_(0),
          last_entry_tick_(0),
          peak_equity_(0.0),
          max_dd_pct_(0.0) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        ticks_processed_++;

        // Track recent tick directions
        if (!tick_prices_.empty()) {
            if (tick.bid > tick_prices_.back()) {
                direction_buffer_.push_back(1);
            } else if (tick.bid < tick_prices_.back()) {
                direction_buffer_.push_back(-1);
            } else {
                direction_buffer_.push_back(0);
            }
        }
        tick_prices_.push_back(tick.bid);

        if (tick_prices_.size() > 500) tick_prices_.pop_front();
        if (direction_buffer_.size() > (size_t)config_.reversal_window) {
            direction_buffer_.pop_front();
        }

        double equity = engine.GetEquity();
        if (peak_equity_ == 0.0) peak_equity_ = equity;
        if (equity > peak_equity_) peak_equity_ = equity;
        double dd = (peak_equity_ - equity) / peak_equity_ * 100.0;
        if (dd > max_dd_pct_) max_dd_pct_ = dd;

        // Check for entry opportunity
        int open_count = (int)engine.GetOpenPositions().size();
        if (open_count >= config_.max_positions) return;

        // Cooldown check
        if (ticks_processed_ - last_entry_tick_ < config_.cooldown_ticks) return;

        // Calculate entry level (spacing below lowest open or current price)
        double entry_level;
        if (open_count == 0) {
            entry_level = tick.bid;  // First entry at current price
        } else {
            entry_level = lowest_buy_ - config_.spacing;
        }

        // Check if price has reached entry level
        bool spacing_met = (tick.bid <= entry_level);
        if (!spacing_met && open_count > 0) return;

        // THE DEMON'S DOOR: Check if the "molecule" is favorable
        bool door_open = IsMoleculeFavorable();

        if (door_open || open_count == 0) {
            // Calculate lot size (same as FillUp survive_pct logic)
            double lot_size = CalculateLotSize(tick, engine);
            if (lot_size < config_.min_volume) return;
            lot_size = std::min(lot_size, config_.max_volume);

            // Open with TP
            double tp = tick.ask + config_.tp_distance;
            engine.OpenMarketOrder("BUY", lot_size, 0, tp);

            // Update tracking
            if (tick.ask < lowest_buy_) lowest_buy_ = tick.ask;
            last_entry_tick_ = ticks_processed_;
            entries_allowed_++;
        } else {
            entries_blocked_++;
        }
    }

    int GetEntriesAllowed() const { return entries_allowed_; }
    int GetEntriesBlocked() const { return entries_blocked_; }
    double GetSelectivityRatio() const {
        int total = entries_allowed_ + entries_blocked_;
        return total > 0 ? (double)entries_blocked_ / total * 100.0 : 0.0;
    }
    double GetMaxDDPct() const { return max_dd_pct_; }

private:
    Config config_;
    double lowest_buy_;
    int ticks_processed_;
    int entries_allowed_;
    int entries_blocked_;
    int last_entry_tick_;
    double peak_equity_;
    double max_dd_pct_;
    std::deque<double> tick_prices_;
    std::deque<int> direction_buffer_;

    bool IsMoleculeFavorable() {
        if (!config_.require_reversal) return true;
        if (direction_buffer_.size() < (size_t)config_.reversal_window) return true;

        // Count net direction in recent window
        int net_direction = 0;
        int recent_up = 0;
        int recent_down = 0;

        // Split window: first half should be down, second half should be up (reversal)
        size_t half = direction_buffer_.size() / 2;

        for (size_t i = 0; i < half; i++) {
            if (direction_buffer_[i] < 0) recent_down++;
        }
        for (size_t i = half; i < direction_buffer_.size(); i++) {
            if (direction_buffer_[i] > 0) recent_up++;
            net_direction += direction_buffer_[i];
        }

        // Favorable = was falling (first half has downs) AND now rising (second half has ups)
        bool was_falling = recent_down > (int)(half * 0.4);
        bool now_rising = net_direction >= config_.direction_threshold;

        return was_falling && now_rising;
    }

    double CalculateLotSize(const Tick& tick, TickBasedEngine& engine) {
        double equity = engine.GetEquity();
        double survive_amount = equity * config_.survive_pct / 100.0;
        double price_risk = config_.spacing * 5.0;  // Assume 5 levels of risk
        if (price_risk <= 0) return config_.min_volume;

        double max_lots = survive_amount / (price_risk * engine.GetConfig().contract_size);
        int open_count = (int)engine.GetOpenPositions().size();
        double lot_size = max_lots / std::max(1, 10 - open_count);

        lot_size = engine.NormalizeLots(lot_size);
        return std::max(config_.min_volume, std::min(config_.max_volume, lot_size));
    }
};

} // namespace backtest

#endif // STRATEGY_ENTROPY_HARVESTER_H
