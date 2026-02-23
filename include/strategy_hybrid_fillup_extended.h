#pragma once

#include "tick_based_engine.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include <set>
#include <cfloat>

namespace backtest {

/**
 * HybridFillUpExtended Strategy
 *
 * Combines FillUp's tight-spacing oscillation capture with an extended mode
 * that uses wider spacing when normal margin-based sizing is exhausted.
 *
 * Problem solved:
 * - FillUp stops opening trades when lot_sizing() returns 0 (margin exhausted)
 * - Price may still oscillate within survive range, missing profit opportunities
 *
 * Solution:
 * - When normal lot sizing returns 0, switch to "extended mode"
 * - Extended mode uses wider spacing based on remaining distance to survive limit
 * - Uses minimum lot size (0.01) to minimize margin impact
 * - Captures oscillations that would otherwise be missed
 *
 * Modes:
 * - NORMAL: FillUp-style tight spacing, calculated lot sizes
 * - EXTENDED: Wider spacing, min_volume only, when margin exhausted
 */
class HybridFillUpExtended {
public:
    enum Mode {
        NORMAL,     // Standard FillUp behavior
        EXTENDED    // Wider spacing when margin exhausted
    };

    struct Config {
        double survive_pct = 13.0;
        double base_spacing = 1.50;              // Normal mode spacing
        double min_volume = 0.01;
        double max_volume = 10.0;

        // Extended mode settings
        int extended_grid_levels = 3;            // How many levels in extended mode
        double extended_spacing_pct = 0.5;       // Spacing as % of remaining survive distance
        bool close_extended_on_reversal = true;  // Close extended positions on direction change

        // Volatility adaptation
        double volatility_lookback_hours = 4.0;
        double typical_vol_pct = 0.55;
        double min_spacing_mult = 0.5;
        double max_spacing_mult = 3.0;

        // Ticks per hour estimate (XAUUSD: ~100k ticks/hour during active trading)
        int ticks_per_hour = 100000;
    };

    struct Stats {
        int normal_entries = 0;
        int extended_entries = 0;
        int normal_exits = 0;
        int extended_exits = 0;
        int mode_switches_to_extended = 0;
        int mode_switches_to_normal = 0;
        double max_extended_positions = 0;
        double total_extended_profit = 0.0;
        double total_normal_profit = 0.0;
        long total_ticks = 0;
    };

private:
    Config config_;
    Stats stats_;
    Mode current_mode_ = NORMAL;

    // Position tracking
    double lowest_buy_ = DBL_MAX;
    double highest_buy_ = 0.0;
    double lowest_extended_buy_ = DBL_MAX;
    int normal_position_count_ = 0;
    int extended_position_count_ = 0;

    // Track which trades are extended by their ID
    std::set<int> extended_trade_ids_;

    // Price at which we switched to extended mode
    double extended_mode_entry_price_ = 0.0;

    // Volatility tracking (tick-based)
    double period_high_ = 0.0;
    double period_low_ = DBL_MAX;
    long period_start_tick_ = 0;
    long current_tick_ = 0;
    double current_spacing_ = 0.0;
    double extended_spacing_ = 0.0;

    // Direction tracking
    int direction_ = 0;  // 1 = up, -1 = down
    double bid_at_turn_up_ = DBL_MAX;
    double bid_at_turn_down_ = 0.0;
    double ask_at_turn_up_ = DBL_MAX;
    double ask_at_turn_down_ = 0.0;

public:
    HybridFillUpExtended(const Config& config) : config_(config) {
        current_spacing_ = config_.base_spacing;
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_tick_++;
        stats_.total_ticks++;
        double spread = tick.ask - tick.bid;

        // Update volatility tracking
        UpdateVolatility(tick);

        // Update direction tracking
        UpdateDirection(tick, spread);

        // Count and track positions
        UpdatePositionTracking(engine);

        // Calculate normal lot size
        double normal_lot_size = CalculateNormalLotSize(tick, engine);

        // Determine mode
        Mode new_mode = DetermineMode(tick, normal_lot_size);

        if (new_mode != current_mode_) {
            HandleModeSwitch(new_mode, tick);
        }

        // Execute based on current mode
        if (current_mode_ == NORMAL) {
            ExecuteNormalMode(tick, engine, normal_lot_size, spread);
        } else {
            ExecuteExtendedMode(tick, engine);
        }

        // Handle direction-based closing for extended positions
        if (config_.close_extended_on_reversal && direction_ == 1 && extended_position_count_ > 0) {
            CloseExtendedPositions(tick, engine);
        }
    }

    const Stats& GetStats() const { return stats_; }
    Mode GetCurrentMode() const { return current_mode_; }
    double GetCurrentSpacing() const { return current_spacing_; }
    double GetExtendedSpacing() const { return extended_spacing_; }

private:
    void UpdateVolatility(const Tick& tick) {
        // Calculate lookback in ticks
        long lookback_ticks = (long)(config_.volatility_lookback_hours * config_.ticks_per_hour);

        if (current_tick_ - period_start_tick_ > lookback_ticks || period_start_tick_ == 0) {
            // New period
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

    void UpdatePositionTracking(TickBasedEngine& engine) {
        // Reset counters
        normal_position_count_ = 0;
        extended_position_count_ = 0;
        lowest_buy_ = DBL_MAX;
        highest_buy_ = 0.0;
        lowest_extended_buy_ = DBL_MAX;

        // Clean up extended_trade_ids for closed trades
        std::set<int> current_trade_ids;
        for (const Trade* trade : engine.GetOpenPositions()) {
            current_trade_ids.insert(trade->id);
        }

        // Remove IDs of closed trades from our tracking
        std::set<int> closed_ids;
        for (int id : extended_trade_ids_) {
            if (current_trade_ids.find(id) == current_trade_ids.end()) {
                closed_ids.insert(id);
            }
        }
        for (int id : closed_ids) {
            extended_trade_ids_.erase(id);
        }

        // Count positions by type
        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->IsBuy()) {
                bool is_extended = (extended_trade_ids_.find(trade->id) != extended_trade_ids_.end());

                if (is_extended) {
                    extended_position_count_++;
                    lowest_extended_buy_ = std::min(lowest_extended_buy_, trade->entry_price);
                } else {
                    normal_position_count_++;
                    lowest_buy_ = std::min(lowest_buy_, trade->entry_price);
                    highest_buy_ = std::max(highest_buy_, trade->entry_price);
                }
            }
        }

        stats_.max_extended_positions = std::max(stats_.max_extended_positions,
                                                  (double)extended_position_count_);
    }

    double CalculateNormalLotSize(const Tick& tick, TickBasedEngine& engine) {
        double equity = engine.GetEquity();
        double survive_distance = tick.ask * (config_.survive_pct / 100.0);

        // Calculate how much equity we'd lose if price drops by survive_distance
        double total_volume = engine.GetBuyVolume();

        double potential_loss = total_volume * survive_distance * engine.GetConfig().contract_size;
        double available_equity = equity - potential_loss;

        if (available_equity <= 0) return 0.0;

        // Calculate lot size based on available equity
        double margin_per_lot = engine.CalculateMarginRequired(1.0, tick.ask);
        double max_lots_by_margin = available_equity / margin_per_lot;

        // Limit by distance to survive
        double remaining_distance = (lowest_buy_ > tick.ask || lowest_buy_ >= DBL_MAX) ? 0 : tick.ask - lowest_buy_;
        double expected_loss_per_lot = remaining_distance * engine.GetConfig().contract_size;
        double max_lots_by_loss = (available_equity * 0.5) / std::max(1.0, expected_loss_per_lot);

        double lot_size = std::min(max_lots_by_margin, max_lots_by_loss);
        lot_size = std::min(lot_size, config_.max_volume);
        lot_size = engine.NormalizeLots(lot_size);

        return lot_size;
    }

    Mode DetermineMode(const Tick& tick, double normal_lot_size) {
        // If we can afford normal trades, use normal mode
        if (normal_lot_size >= config_.min_volume) {
            return NORMAL;
        }

        // Check if we have room for extended mode
        double survive_limit = highest_buy_ * (1.0 - config_.survive_pct / 100.0);
        double remaining_distance = tick.ask - survive_limit;

        // Only use extended mode if there's meaningful room and we have positions
        if (highest_buy_ > 0 && remaining_distance > current_spacing_ * 0.5) {
            return EXTENDED;
        }

        // Too close to survive limit or no positions yet, stay in normal (wait)
        return NORMAL;
    }

    void HandleModeSwitch(Mode new_mode, const Tick& tick) {
        if (new_mode == EXTENDED && current_mode_ == NORMAL) {
            // Switching to extended mode
            stats_.mode_switches_to_extended++;
            extended_mode_entry_price_ = tick.ask;
            lowest_extended_buy_ = DBL_MAX;

            // Calculate extended spacing based on remaining survive distance
            double survive_limit = highest_buy_ * (1.0 - config_.survive_pct / 100.0);
            double remaining_distance = tick.ask - survive_limit;
            extended_spacing_ = remaining_distance / config_.extended_grid_levels;
            extended_spacing_ = std::max(extended_spacing_, current_spacing_ * 0.5);  // Min 50% of normal
        } else if (new_mode == NORMAL && current_mode_ == EXTENDED) {
            // Switching back to normal mode
            stats_.mode_switches_to_normal++;
        }

        current_mode_ = new_mode;
    }

    void ExecuteNormalMode(const Tick& tick, TickBasedEngine& engine,
                           double lot_size, double spread) {
        // First position
        if (normal_position_count_ == 0 && extended_position_count_ == 0) {
            if (lot_size >= config_.min_volume) {
                double tp = tick.ask + current_spacing_ + spread;
                Trade* trade = engine.OpenMarketOrder("BUY", lot_size, 0.0, tp);
                if (trade) {
                    lowest_buy_ = tick.ask;
                    highest_buy_ = tick.ask;
                    normal_position_count_++;
                    stats_.normal_entries++;
                }
            }
            return;
        }

        // Add positions as price drops
        if (lowest_buy_ < DBL_MAX && tick.ask <= lowest_buy_ - current_spacing_) {
            if (lot_size >= config_.min_volume) {
                double tp = tick.ask + current_spacing_ + spread;
                Trade* trade = engine.OpenMarketOrder("BUY", lot_size, 0.0, tp);
                if (trade) {
                    lowest_buy_ = tick.ask;
                    normal_position_count_++;
                    stats_.normal_entries++;
                }
            }
        }

        // Note: TP is handled automatically by the engine
    }

    void ExecuteExtendedMode(const Tick& tick, TickBasedEngine& engine) {
        // Limit extended positions
        if (extended_position_count_ >= config_.extended_grid_levels) {
            return;
        }

        // Calculate survive limit from highest normal position
        double survive_limit = highest_buy_ * (1.0 - config_.survive_pct / 100.0);

        // Don't open if too close to survive limit
        if (tick.ask - survive_limit < extended_spacing_) {
            return;
        }

        // Open extended position if spacing condition met
        bool should_open = false;
        if (extended_position_count_ == 0) {
            // First extended position - open at current price (we just switched modes)
            should_open = true;
        } else if (lowest_extended_buy_ < DBL_MAX && tick.ask <= lowest_extended_buy_ - extended_spacing_) {
            // Add more extended positions as price drops
            should_open = true;
        }

        if (should_open && direction_ == -1) {  // Only open when going down
            // Extended positions use wider TP (50% of extended spacing)
            double extended_tp = tick.ask + extended_spacing_ * 0.5;
            Trade* trade = engine.OpenMarketOrder("BUY", config_.min_volume, 0.0, extended_tp);
            if (trade) {
                extended_trade_ids_.insert(trade->id);
                lowest_extended_buy_ = tick.ask;
                extended_position_count_++;
                stats_.extended_entries++;
            }
        }

        // Note: TP is handled automatically by the engine
    }

    void CloseExtendedPositions(const Tick& tick, TickBasedEngine& engine) {
        // Close extended positions that are in profit when direction reverses
        std::vector<Trade*> to_close;
        double cs = engine.GetConfig().contract_size;

        for (Trade* trade : engine.GetOpenPositions()) {
            if (trade->IsBuy() &&
                extended_trade_ids_.find(trade->id) != extended_trade_ids_.end()) {
                double profit = (tick.bid - trade->entry_price) * trade->lot_size * cs;
                if (profit > 0) {
                    to_close.push_back(trade);
                }
            }
        }

        for (Trade* trade : to_close) {
            double profit = (tick.bid - trade->entry_price) * trade->lot_size * cs;
            engine.ClosePosition(trade, "Extended reversal");
            extended_trade_ids_.erase(trade->id);
            stats_.extended_exits++;
            stats_.total_extended_profit += profit;
        }

        extended_position_count_ = 0;
    }
};

} // namespace backtest
