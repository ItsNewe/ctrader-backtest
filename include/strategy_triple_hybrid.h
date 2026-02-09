#pragma once

#include "tick_based_engine.h"
#include <cmath>
#include <algorithm>
#include <set>
#include <cfloat>

namespace backtest {

/**
 * TripleHybrid Strategy
 *
 * Combines three trading modes:
 * 1. GRID (FillUp): Buy dips with TP - captures oscillations
 * 2. TREND_UP: Buy during uptrends, no TP - rides trends
 * 3. REVERSAL: Buy during downtrends, no TP - rides recoveries
 *
 * Position tracking:
 * - GRID positions: Have TP, close automatically
 * - TREND_UP positions: No TP, held indefinitely (optional close on turn down)
 * - REVERSAL positions: No TP, held through recovery
 */
class TripleHybrid {
public:
    struct Config {
        // Core parameters
        double survive_pct = 13.0;
        double base_spacing = 1.50;
        double min_volume = 0.01;
        double max_volume = 10.0;
        double contract_size = 100.0;
        double leverage = 500.0;

        // Enable/disable each mode
        bool enable_grid = true;
        bool enable_trend_up = true;
        bool enable_reversal = true;

        // TREND_UP settings
        double trend_up_spacing_mult = 2.0;  // Wider spacing for trend positions
        double trend_up_tp_pct = 5.0;        // TP as % above entry (5% default)
        bool close_trend_on_reversal = true; // Close trend_up positions when direction turns down

        // REVERSAL settings
        double reversal_spacing_mult = 2.0;  // Wider spacing for reversal positions
        double reversal_tp_pct = 5.0;        // TP as % above entry (5% default)

        // Volatility adaptation
        double volatility_lookback_hours = 4.0;
        double typical_vol_pct = 0.55;
        double min_spacing_mult = 0.5;
        double max_spacing_mult = 3.0;
        int ticks_per_hour = 100000;
    };

    struct Stats {
        int grid_entries = 0;
        int grid_exits = 0;
        int trend_up_entries = 0;
        int trend_up_exits = 0;
        int reversal_entries = 0;
        int reversal_exits = 0;
        double grid_profit = 0.0;
        double trend_up_profit = 0.0;
        double reversal_profit = 0.0;
        int direction_changes = 0;
        long total_ticks = 0;
    };

    enum PositionType {
        GRID,
        TREND_UP,
        REVERSAL
    };

private:
    Config config_;
    Stats stats_;

    // Direction tracking
    int direction_ = 0;  // 1 = up, -1 = down
    double bid_at_turn_up_ = DBL_MAX;
    double bid_at_turn_down_ = 0.0;
    double ask_at_turn_up_ = DBL_MAX;
    double ask_at_turn_down_ = 0.0;

    // Position tracking
    double lowest_grid_buy_ = DBL_MAX;
    double highest_grid_buy_ = 0.0;
    double last_trend_up_entry_ = 0.0;
    double last_reversal_entry_ = DBL_MAX;

    // Track position types by trade ID
    std::set<int> trend_up_ids_;
    std::set<int> reversal_ids_;

    // Volatility tracking
    double period_high_ = 0.0;
    double period_low_ = DBL_MAX;
    long period_start_tick_ = 0;
    long current_tick_ = 0;
    double current_spacing_ = 0.0;

    // Total volume tracking for lot sizing
    double total_volume_ = 0.0;

public:
    TripleHybrid(const Config& config) : config_(config) {
        current_spacing_ = config_.base_spacing;
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_tick_++;
        stats_.total_ticks++;
        double spread = tick.ask - tick.bid;

        // Update volatility and spacing
        UpdateVolatility(tick);

        // Update direction
        int old_direction = direction_;
        UpdateDirection(tick, spread);
        if (direction_ != old_direction && old_direction != 0) {
            stats_.direction_changes++;
        }

        // Update position tracking
        UpdatePositionTracking(engine, tick);

        // Calculate lot size
        double lots = CalculateLotSize(tick, engine);

        // Execute each mode
        if (config_.enable_grid) {
            ExecuteGrid(tick, engine, lots, spread);
        }

        if (config_.enable_trend_up && direction_ == 1) {
            ExecuteTrendUp(tick, engine, lots);
        }

        if (config_.enable_reversal && direction_ == -1) {
            ExecuteReversal(tick, engine, lots);
        }

        // Handle optional closes
        if (config_.close_trend_on_reversal && direction_ == -1) {
            CloseTrendUpPositions(tick, engine);
        }
    }

    const Stats& GetStats() const { return stats_; }
    int GetDirection() const { return direction_; }
    double GetCurrentSpacing() const { return current_spacing_; }

private:
    void UpdateVolatility(const Tick& tick) {
        long lookback_ticks = (long)(config_.volatility_lookback_hours * config_.ticks_per_hour);

        if (current_tick_ - period_start_tick_ > lookback_ticks || period_start_tick_ == 0) {
            double range = period_high_ - period_low_;
            if (range > 0 && period_low_ > 0 && period_low_ < DBL_MAX) {
                double typical_vol = tick.bid * (config_.typical_vol_pct / 100.0);
                double vol_ratio = range / typical_vol;
                vol_ratio = std::max(config_.min_spacing_mult,
                           std::min(config_.max_spacing_mult, vol_ratio));
                current_spacing_ = config_.base_spacing * vol_ratio;
            }
            period_high_ = tick.bid;
            period_low_ = tick.bid;
            period_start_tick_ = current_tick_;
        } else {
            period_high_ = std::max(period_high_, tick.bid);
            period_low_ = std::min(period_low_, tick.bid);
        }
    }

    void UpdateDirection(const Tick& tick, double spread) {
        bid_at_turn_down_ = std::max(tick.bid, bid_at_turn_down_);
        bid_at_turn_up_ = std::min(tick.bid, bid_at_turn_up_);
        ask_at_turn_down_ = std::max(tick.ask, ask_at_turn_down_);
        ask_at_turn_up_ = std::min(tick.ask, ask_at_turn_up_);

        double threshold = spread * 2;

        // Detect turn down
        if (direction_ != -1 &&
            ask_at_turn_down_ >= tick.ask + threshold &&
            bid_at_turn_down_ >= tick.bid + threshold) {
            direction_ = -1;
            ClearTurnMarks();
        }

        // Detect turn up
        if (direction_ != 1 &&
            bid_at_turn_up_ <= tick.bid - threshold &&
            ask_at_turn_up_ <= tick.ask - threshold) {
            direction_ = 1;
            ClearTurnMarks();
        }
    }

    void ClearTurnMarks() {
        bid_at_turn_up_ = DBL_MAX;
        ask_at_turn_up_ = DBL_MAX;
        bid_at_turn_down_ = 0.0;
        ask_at_turn_down_ = 0.0;
    }

    void UpdatePositionTracking(TickBasedEngine& engine, const Tick& tick) {
        // Get current open trade IDs
        std::set<int> current_ids;
        for (const Trade* trade : engine.GetOpenPositions()) {
            current_ids.insert(trade->id);
        }

        // Track closed positions and their profit
        std::set<int> closed_trend_up;
        for (int id : trend_up_ids_) {
            if (current_ids.find(id) == current_ids.end()) {
                closed_trend_up.insert(id);
            }
        }
        for (int id : closed_trend_up) {
            trend_up_ids_.erase(id);
            stats_.trend_up_exits++;
        }

        std::set<int> closed_reversal;
        for (int id : reversal_ids_) {
            if (current_ids.find(id) == current_ids.end()) {
                closed_reversal.insert(id);
            }
        }
        for (int id : closed_reversal) {
            reversal_ids_.erase(id);
            stats_.reversal_exits++;
        }

        // Reset tracking
        lowest_grid_buy_ = DBL_MAX;
        highest_grid_buy_ = 0.0;
        total_volume_ = 0.0;

        // Iterate positions
        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->direction == "BUY") {
                total_volume_ += trade->lot_size;

                bool is_trend_up = (trend_up_ids_.find(trade->id) != trend_up_ids_.end());
                bool is_reversal = (reversal_ids_.find(trade->id) != reversal_ids_.end());

                if (!is_trend_up && !is_reversal) {
                    // It's a GRID position
                    lowest_grid_buy_ = std::min(lowest_grid_buy_, trade->entry_price);
                    highest_grid_buy_ = std::max(highest_grid_buy_, trade->entry_price);
                }
            }
        }
    }

    double CalculateLotSize(const Tick& tick, TickBasedEngine& engine) {
        double equity = engine.GetEquity();
        double survive_distance = tick.ask * (config_.survive_pct / 100.0);

        // Calculate potential loss if price drops by survive_distance
        double potential_loss = total_volume_ * survive_distance * config_.contract_size;
        double available_equity = equity - potential_loss;

        if (available_equity <= 0) return 0.0;

        // Margin per lot
        double margin_per_lot = (tick.ask * config_.contract_size) / config_.leverage;
        double max_lots_by_margin = available_equity / margin_per_lot;

        // Conservative: use 50% of available for new positions
        double lots = max_lots_by_margin * 0.5;
        lots = std::min(lots, config_.max_volume);
        lots = std::floor(lots * 100) / 100;

        return std::max(lots, 0.0);
    }

    void ExecuteGrid(const Tick& tick, TickBasedEngine& engine, double lots, double spread) {
        if (lots < config_.min_volume) {
            lots = config_.min_volume;  // Force min_volume for grid continuity
        }

        // First position
        if (lowest_grid_buy_ >= DBL_MAX) {
            double tp = tick.ask + current_spacing_ + spread;
            Trade* trade = engine.OpenMarketOrder("BUY", lots, 0.0, tp);
            if (trade) {
                lowest_grid_buy_ = tick.ask;
                highest_grid_buy_ = tick.ask;
                stats_.grid_entries++;
            }
            return;
        }

        // Grid entry on dip
        if (tick.ask <= lowest_grid_buy_ - current_spacing_) {
            double tp = tick.ask + current_spacing_ + spread;
            Trade* trade = engine.OpenMarketOrder("BUY", lots, 0.0, tp);
            if (trade) {
                lowest_grid_buy_ = tick.ask;
                stats_.grid_entries++;
            }
        }

        // Grid entry on rise (up while up component for grid)
        if (tick.ask >= highest_grid_buy_ + current_spacing_) {
            double tp = tick.ask + current_spacing_ + spread;
            Trade* trade = engine.OpenMarketOrder("BUY", lots, 0.0, tp);
            if (trade) {
                highest_grid_buy_ = tick.ask;
                stats_.grid_entries++;
            }
        }
    }

    void ExecuteTrendUp(const Tick& tick, TickBasedEngine& engine, double lots) {
        if (lots < config_.min_volume) {
            lots = config_.min_volume;
        }

        double trend_spacing = current_spacing_ * config_.trend_up_spacing_mult;

        // First trend_up position or spacing condition met
        bool should_open = false;
        if (last_trend_up_entry_ == 0.0) {
            should_open = true;
        } else if (tick.ask >= last_trend_up_entry_ + trend_spacing) {
            should_open = true;
        }

        if (should_open) {
            // Calculate TP based on config
            double tp = 0.0;
            if (config_.trend_up_tp_pct > 0) {
                tp = tick.ask * (1.0 + config_.trend_up_tp_pct / 100.0);
            }

            Trade* trade = engine.OpenMarketOrder("BUY", lots, 0.0, tp);
            if (trade) {
                trend_up_ids_.insert(trade->id);
                last_trend_up_entry_ = tick.ask;
                stats_.trend_up_entries++;
            }
        }
    }

    void ExecuteReversal(const Tick& tick, TickBasedEngine& engine, double lots) {
        if (lots < config_.min_volume) {
            lots = config_.min_volume;
        }

        double reversal_spacing = current_spacing_ * config_.reversal_spacing_mult;

        // First reversal position or spacing condition met
        bool should_open = false;
        if (last_reversal_entry_ >= DBL_MAX) {
            should_open = true;
        } else if (tick.ask <= last_reversal_entry_ - reversal_spacing) {
            should_open = true;
        }

        if (should_open) {
            // Calculate TP if configured
            double tp = 0.0;
            if (config_.reversal_tp_pct > 0) {
                tp = tick.ask * (1.0 + config_.reversal_tp_pct / 100.0);
            }

            Trade* trade = engine.OpenMarketOrder("BUY", lots, 0.0, tp);
            if (trade) {
                reversal_ids_.insert(trade->id);
                last_reversal_entry_ = tick.ask;
                stats_.reversal_entries++;
            }
        }
    }

    void CloseTrendUpPositions(const Tick& tick, TickBasedEngine& engine) {
        std::vector<Trade*> to_close;

        for (Trade* trade : engine.GetOpenPositions()) {
            if (trend_up_ids_.find(trade->id) != trend_up_ids_.end()) {
                // Only close if in profit
                double profit = (tick.bid - trade->entry_price) * trade->lot_size * config_.contract_size;
                if (profit > 0) {
                    to_close.push_back(trade);
                }
            }
        }

        for (Trade* trade : to_close) {
            double profit = (tick.bid - trade->entry_price) * trade->lot_size * config_.contract_size;
            engine.ClosePosition(trade, "TrendUp reversal close");
            trend_up_ids_.erase(trade->id);
            stats_.trend_up_exits++;
            stats_.trend_up_profit += profit;
        }
    }
};

} // namespace backtest
