#pragma once

#include "tick_based_engine.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include <set>
#include <cfloat>

namespace backtest {

/**
 * DualAllocationStrategy
 *
 * Splits capital into two independent allocations:
 * 1. PRIMARY (FillUp-style): Tight spacing, margin-based sizing
 * 2. EXTENDED: Wider spacing, activates when primary is "full"
 *
 * Key differences from failed HybridFillUpExtended:
 * - Each allocation has its OWN capital budget and survive calculation
 * - Extended doesn't eat into Primary's safety margin
 * - Both allocations scale with equity growth
 * - Percentage-based spacing handles price changes
 *
 * Capital Allocation Example ($10,000 account):
 *   Primary: 80% = $8,000 (survive calculated on this)
 *   Extended: 20% = $2,000 (independent survive calculation)
 *
 * When equity grows to $20,000:
 *   Primary: 80% = $16,000
 *   Extended: 20% = $4,000
 */
class DualAllocationStrategy {
public:
    struct Config {
        // Capital allocation (must sum to 1.0)
        double primary_allocation_pct = 0.80;    // 80% for primary FillUp
        double extended_allocation_pct = 0.20;   // 20% for extended

        // Primary (FillUp) settings
        double primary_survive_pct = 13.0;
        double primary_base_spacing_pct = 0.055; // % of price (~$1.50 at $2700)
        double primary_min_volume = 0.01;
        double primary_max_volume = 10.0;

        // Extended settings
        double extended_survive_pct = 8.0;       // Smaller survive = more aggressive
        double extended_base_spacing_pct = 0.30; // Wider spacing (% of price)
        double extended_min_volume = 0.01;
        double extended_max_volume = 1.0;        // Cap extended position size
        int extended_max_positions = 5;          // Limit concurrent extended positions

        // Shared settings
        double contract_size = 100.0;
        double leverage = 500.0;

        // Volatility adaptation
        double volatility_lookback_hours = 4.0;
        double typical_vol_pct = 0.55;
        double min_spacing_mult = 0.5;
        double max_spacing_mult = 3.0;
        int ticks_per_hour = 100000;

        // Mode switching
        bool enable_extended = true;             // Can disable extended entirely
        bool close_extended_on_reversal = true;  // Close extended when direction reverses
    };

    struct Stats {
        // Primary stats
        int primary_entries = 0;
        int primary_exits = 0;
        double primary_profit = 0.0;
        int primary_peak_positions = 0;

        // Extended stats
        int extended_entries = 0;
        int extended_exits = 0;
        double extended_profit = 0.0;
        int extended_peak_positions = 0;
        int extended_activations = 0;           // Times extended mode activated

        // Allocation tracking
        double primary_allocation_used = 0.0;   // Current margin used by primary
        double extended_allocation_used = 0.0;  // Current margin used by extended

        long total_ticks = 0;
    };

private:
    Config config_;
    Stats stats_;

    // Position tracking - separate for each allocation
    std::set<int> primary_trade_ids_;
    std::set<int> extended_trade_ids_;

    // Primary state
    double primary_lowest_buy_ = DBL_MAX;
    double primary_highest_buy_ = 0.0;
    int primary_position_count_ = 0;
    double primary_current_spacing_ = 0.0;

    // Extended state
    double extended_lowest_buy_ = DBL_MAX;
    int extended_position_count_ = 0;
    double extended_current_spacing_ = 0.0;
    bool extended_active_ = false;

    // Volatility tracking
    double period_high_ = 0.0;
    double period_low_ = DBL_MAX;
    long period_start_tick_ = 0;
    long current_tick_ = 0;
    double vol_ratio_ = 1.0;

    // Direction tracking
    int direction_ = 0;
    double bid_at_turn_up_ = DBL_MAX;
    double bid_at_turn_down_ = 0.0;
    double ask_at_turn_up_ = DBL_MAX;
    double ask_at_turn_down_ = 0.0;

    // Initial equity for reference
    double initial_equity_ = 0.0;

public:
    DualAllocationStrategy(const Config& config) : config_(config) {
        // Validate allocations sum to 1.0
        double total = config_.primary_allocation_pct + config_.extended_allocation_pct;
        if (std::abs(total - 1.0) > 0.001) {
            // Normalize
            config_.primary_allocation_pct /= total;
            config_.extended_allocation_pct /= total;
        }
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_tick_++;
        stats_.total_ticks++;

        // Initialize on first tick
        if (initial_equity_ == 0.0) {
            initial_equity_ = engine.GetEquity();
            primary_current_spacing_ = tick.bid * (config_.primary_base_spacing_pct / 100.0);
            extended_current_spacing_ = tick.bid * (config_.extended_base_spacing_pct / 100.0);
        }

        double spread = tick.ask - tick.bid;
        double current_equity = engine.GetEquity();

        // Update volatility
        UpdateVolatility(tick);

        // Update direction
        UpdateDirection(tick, spread);

        // Update position tracking
        UpdatePositionTracking(engine, tick);

        // Calculate allocations based on current equity
        double primary_budget = current_equity * config_.primary_allocation_pct;
        double extended_budget = current_equity * config_.extended_allocation_pct;

        // Update spacing based on price and volatility
        primary_current_spacing_ = tick.bid * (config_.primary_base_spacing_pct / 100.0) * vol_ratio_;
        extended_current_spacing_ = tick.bid * (config_.extended_base_spacing_pct / 100.0) * vol_ratio_;

        // Execute primary allocation
        ExecutePrimary(tick, engine, primary_budget, spread);

        // Execute extended allocation (if enabled)
        if (config_.enable_extended) {
            ExecuteExtended(tick, engine, extended_budget, spread);
        }

        // Handle direction-based closing for extended
        if (config_.close_extended_on_reversal && direction_ == 1 && extended_position_count_ > 0) {
            CloseExtendedOnReversal(tick, engine);
        }
    }

    const Stats& GetStats() const { return stats_; }
    const Config& GetConfig() const { return config_; }
    bool IsExtendedActive() const { return extended_active_; }

private:
    void UpdateVolatility(const Tick& tick) {
        long lookback_ticks = (long)(config_.volatility_lookback_hours * config_.ticks_per_hour);

        if (current_tick_ - period_start_tick_ > lookback_ticks || period_start_tick_ == 0) {
            double range = period_high_ - period_low_;
            if (range > 0 && period_low_ > 0 && period_low_ < DBL_MAX) {
                double typical_vol = tick.bid * (config_.typical_vol_pct / 100.0);
                vol_ratio_ = range / typical_vol;
                vol_ratio_ = std::max(config_.min_spacing_mult,
                           std::min(config_.max_spacing_mult, vol_ratio_));
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

        if (direction_ != -1 &&
            ask_at_turn_down_ >= tick.ask + threshold &&
            bid_at_turn_down_ >= tick.bid + threshold) {
            direction_ = -1;
            bid_at_turn_up_ = DBL_MAX;
            ask_at_turn_up_ = DBL_MAX;
            bid_at_turn_down_ = 0.0;
            ask_at_turn_down_ = 0.0;
        }

        if (direction_ != 1 &&
            bid_at_turn_up_ <= tick.bid - threshold &&
            ask_at_turn_up_ <= tick.ask - threshold) {
            direction_ = 1;
            bid_at_turn_up_ = DBL_MAX;
            ask_at_turn_up_ = DBL_MAX;
            bid_at_turn_down_ = 0.0;
            ask_at_turn_down_ = 0.0;
        }
    }

    void UpdatePositionTracking(TickBasedEngine& engine, const Tick& tick) {
        // Get current open trade IDs
        std::set<int> current_ids;
        for (const Trade* trade : engine.GetOpenPositions()) {
            current_ids.insert(trade->id);
        }

        // Clean up closed trades from tracking
        std::set<int> primary_closed, extended_closed;
        for (int id : primary_trade_ids_) {
            if (current_ids.find(id) == current_ids.end()) {
                primary_closed.insert(id);
            }
        }
        for (int id : extended_trade_ids_) {
            if (current_ids.find(id) == current_ids.end()) {
                extended_closed.insert(id);
            }
        }
        for (int id : primary_closed) {
            primary_trade_ids_.erase(id);
            stats_.primary_exits++;
        }
        for (int id : extended_closed) {
            extended_trade_ids_.erase(id);
            stats_.extended_exits++;
        }

        // Reset counters and recalculate
        primary_position_count_ = 0;
        extended_position_count_ = 0;
        primary_lowest_buy_ = DBL_MAX;
        primary_highest_buy_ = 0.0;
        extended_lowest_buy_ = DBL_MAX;
        stats_.primary_allocation_used = 0.0;
        stats_.extended_allocation_used = 0.0;

        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->direction != "BUY") continue;

            double margin_used = (trade->entry_price * config_.contract_size * trade->lot_size) / config_.leverage;

            if (primary_trade_ids_.find(trade->id) != primary_trade_ids_.end()) {
                primary_position_count_++;
                primary_lowest_buy_ = std::min(primary_lowest_buy_, trade->entry_price);
                primary_highest_buy_ = std::max(primary_highest_buy_, trade->entry_price);
                stats_.primary_allocation_used += margin_used;
            } else if (extended_trade_ids_.find(trade->id) != extended_trade_ids_.end()) {
                extended_position_count_++;
                extended_lowest_buy_ = std::min(extended_lowest_buy_, trade->entry_price);
                stats_.extended_allocation_used += margin_used;
            }
        }

        // Track peak positions
        if (primary_position_count_ > stats_.primary_peak_positions) {
            stats_.primary_peak_positions = primary_position_count_;
        }
        if (extended_position_count_ > stats_.extended_peak_positions) {
            stats_.extended_peak_positions = extended_position_count_;
        }
    }

    double CalculatePrimaryLotSize(const Tick& tick, double budget) {
        // Calculate survive distance in dollars based on current price
        double survive_distance = tick.ask * (config_.primary_survive_pct / 100.0);

        // Calculate total volume of primary positions
        double total_primary_volume = 0.0;
        // Use margin as proxy: margin = (price * contract_size * lots) / leverage
        // So lots = margin * leverage / (price * contract_size)
        if (tick.ask > 0) {
            total_primary_volume = stats_.primary_allocation_used * config_.leverage /
                                   (tick.ask * config_.contract_size);
        }

        // Calculate potential loss if price drops by survive_distance
        // Loss = volume * price_drop * contract_size
        double potential_loss = total_primary_volume * survive_distance * config_.contract_size;

        // Available equity for new positions (budget minus potential loss)
        double available = budget - potential_loss;
        if (available <= 0) return 0.0;

        // Calculate margin needed for one lot
        double margin_per_lot = (tick.ask * config_.contract_size) / config_.leverage;

        // Calculate loss per lot if price drops by survive_distance
        double loss_per_lot = survive_distance * config_.contract_size;

        // Maximum lots we can afford considering both margin and potential loss
        // New position will add to both margin requirement and potential loss exposure
        double max_lots_by_equity = available / (margin_per_lot + loss_per_lot);

        double lots = max_lots_by_equity;

        // Apply limits
        lots = std::min(lots, config_.primary_max_volume);
        lots = std::max(lots, 0.0);
        lots = std::floor(lots * 100) / 100;  // Round to 0.01

        if (lots < config_.primary_min_volume) return 0.0;
        return lots;
    }

    double CalculateExtendedLotSize(const Tick& tick, double budget) {
        // Extended uses simpler sizing - just check if we have room in budget
        double margin_per_lot = (tick.ask * config_.contract_size) / config_.leverage;
        double available = budget - stats_.extended_allocation_used;

        if (available < margin_per_lot * config_.extended_min_volume) return 0.0;

        // Use minimum volume for extended trades (safer)
        return config_.extended_min_volume;
    }

    void ExecutePrimary(const Tick& tick, TickBasedEngine& engine, double budget, double spread) {
        double lot_size = CalculatePrimaryLotSize(tick, budget);

        // First position
        if (primary_position_count_ == 0) {
            if (lot_size >= config_.primary_min_volume) {
                double tp = tick.ask + primary_current_spacing_ + spread;
                Trade* trade = engine.OpenMarketOrder("BUY", lot_size, 0.0, tp);
                if (trade) {
                    primary_trade_ids_.insert(trade->id);
                    primary_lowest_buy_ = tick.ask;
                    primary_highest_buy_ = tick.ask;
                    primary_position_count_++;
                    stats_.primary_entries++;
                }
            }
            extended_active_ = false;
            return;
        }

        // Add positions as price drops
        if (primary_lowest_buy_ < DBL_MAX && tick.ask <= primary_lowest_buy_ - primary_current_spacing_) {
            if (lot_size >= config_.primary_min_volume) {
                double tp = tick.ask + primary_current_spacing_ + spread;
                Trade* trade = engine.OpenMarketOrder("BUY", lot_size, 0.0, tp);
                if (trade) {
                    primary_trade_ids_.insert(trade->id);
                    primary_lowest_buy_ = tick.ask;
                    primary_position_count_++;
                    stats_.primary_entries++;
                }
                extended_active_ = false;
            } else {
                // Primary can't open - extended can activate
                if (!extended_active_) {
                    extended_active_ = true;
                    stats_.extended_activations++;
                }
            }
        }
    }

    void ExecuteExtended(const Tick& tick, TickBasedEngine& engine, double budget, double spread) {
        // Only execute if primary is "full" (can't open trades)
        if (!extended_active_) return;

        // Check position limit
        if (extended_position_count_ >= config_.extended_max_positions) return;

        // Check if we have budget
        double lot_size = CalculateExtendedLotSize(tick, budget);
        if (lot_size < config_.extended_min_volume) return;

        // Only trade when going down
        if (direction_ != -1) return;

        // First extended position or spacing condition met
        bool should_open = false;
        if (extended_position_count_ == 0) {
            should_open = true;
        } else if (extended_lowest_buy_ < DBL_MAX && tick.ask <= extended_lowest_buy_ - extended_current_spacing_) {
            should_open = true;
        }

        if (should_open) {
            // Extended positions use their own (wider) spacing for TP
            double tp = tick.ask + extended_current_spacing_ * 0.6 + spread;
            Trade* trade = engine.OpenMarketOrder("BUY", lot_size, 0.0, tp);
            if (trade) {
                extended_trade_ids_.insert(trade->id);
                extended_lowest_buy_ = tick.ask;
                extended_position_count_++;
                stats_.extended_entries++;
            }
        }
    }

    void CloseExtendedOnReversal(const Tick& tick, TickBasedEngine& engine) {
        std::vector<Trade*> to_close;

        for (Trade* trade : engine.GetOpenPositions()) {
            if (trade->direction == "BUY" &&
                extended_trade_ids_.find(trade->id) != extended_trade_ids_.end()) {
                double profit = (tick.bid - trade->entry_price) * trade->lot_size * config_.contract_size;
                if (profit > 0) {
                    to_close.push_back(trade);
                }
            }
        }

        for (Trade* trade : to_close) {
            double profit = (tick.bid - trade->entry_price) * trade->lot_size * config_.contract_size;
            engine.ClosePosition(trade, "Extended reversal close");
            extended_trade_ids_.erase(trade->id);
            stats_.extended_profit += profit;
        }

        // Reset extended state when direction reverses
        if (direction_ == 1) {
            extended_active_ = false;
        }
    }
};

} // namespace backtest
