#ifndef STRATEGY_PARALLEL_DUAL_ORIGINAL_H
#define STRATEGY_PARALLEL_DUAL_ORIGINAL_H

#include "tick_based_engine.h"
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace backtest {

/**
 * Parallel Dual-Mode Strategy (ORIGINAL)
 *
 * Combines two complementary systems that run simultaneously:
 *
 * 1. GRID SYSTEM ("up while down"): Opens positions on dips below the grid ceiling.
 *    - Entries happen when price drops below (lowest_entry - spacing)
 *    - Naturally becomes inactive when price rises enough (geometric impossibility)
 *    - Reactivates if price crashes back into grid territory
 *
 * 2. MOMENTUM SYSTEM ("up while up"): Opens positions on new highs.
 *    - Entries happen when price exceeds last_momentum_entry
 *    - Continues accumulating as price trends upward
 *
 * Both systems share the margin budget via configurable allocation percentages.
 * NO TRADES ARE EVER CLOSED - pure accumulation strategy.
 */
class ParallelDualStrategyOriginal {
public:
    struct Config {
        // Shared parameters
        double survive_pct;           // % drop to survive for grid sizing
        double grid_allocation;       // Fraction of equity for grid (0.0-1.0)
        double momentum_allocation;   // Fraction of equity for momentum
        double min_volume;
        double max_volume;
        // Grid-specific
        double base_spacing;          // Base spacing for grid entries ($)

        // Momentum-specific
        double momentum_spacing;      // Min distance between momentum entries ($)

        // Safety
        double margin_stop_out;       // Stop-out margin level %
        bool force_min_volume_entry;  // Force entry at min_volume when sizing returns 0

        // Take profit
        bool use_take_profit;         // Enable TP for profit extraction
        double tp_distance;           // Take profit distance from entry ($)

        Config()
            : survive_pct(15.0),
              grid_allocation(0.5),
              momentum_allocation(0.5),
              min_volume(0.01),
              max_volume(10.0),
              base_spacing(1.50),
              momentum_spacing(5.0),
              margin_stop_out(20.0),
              force_min_volume_entry(false),
              use_take_profit(true),
              tp_distance(3.0) {}
    };

    struct Stats {
        // Grid stats
        int grid_entries;
        double grid_volume;
        double grid_lowest_entry;
        double grid_highest_entry;

        // Momentum stats
        int momentum_entries;
        double momentum_volume;
        double momentum_lowest_entry;
        double momentum_highest_entry;

        // Combined
        int total_entries;
        double total_volume;
        int forced_entries;
        int skipped_by_margin;

        // State tracking
        bool grid_active;  // Whether grid can currently fire

        Stats()
            : grid_entries(0),
              grid_volume(0.0),
              grid_lowest_entry(DBL_MAX),
              grid_highest_entry(0.0),
              momentum_entries(0),
              momentum_volume(0.0),
              momentum_lowest_entry(DBL_MAX),
              momentum_highest_entry(0.0),
              total_entries(0),
              total_volume(0.0),
              forced_entries(0),
              skipped_by_margin(0),
              grid_active(true) {}
    };

    explicit ParallelDualStrategyOriginal(const Config& config = Config())
        : config_(config),
          stats_(),
          initialized_(false),
          start_price_(0.0),
          grid_ceiling_(0.0),
          grid_floor_(DBL_MAX),
          current_spacing_(config.base_spacing),
          last_momentum_entry_(0.0) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        if (!initialized_) {
            Initialize(tick, engine);
            return;
        }

        // Update grid ceiling on new highs (expands grid territory)
        if (tick.ask > grid_ceiling_) {
            grid_ceiling_ = tick.ask;
        }

        // Check if grid is geometrically possible
        // Grid entries happen below grid_floor_. If current price is so high that
        // even a survive_pct drop wouldn't reach grid_floor_, grid is inactive.
        double threshold_price = grid_ceiling_ * (1.0 + config_.survive_pct / 100.0);
        stats_.grid_active = (tick.ask < threshold_price);

        // GRID SYSTEM: Entry on dips
        TryGridEntry(tick, engine);

        // MOMENTUM SYSTEM: Entry on new highs
        TryMomentumEntry(tick, engine);
    }

    const Stats& GetStats() const { return stats_; }
    const Config& GetConfig() const { return config_; }
    double GetCurrentSpacing() const { return current_spacing_; }

private:
    Config config_;
    Stats stats_;

    // State
    bool initialized_;
    double start_price_;

    // Grid state
    double grid_ceiling_;     // Highest price seen - defines top of grid
    double grid_floor_;       // Lowest grid entry price - triggers next entry
    double current_spacing_;

    // Momentum state
    double last_momentum_entry_;  // Price of last momentum entry

    void Initialize(const Tick& tick, TickBasedEngine& engine) {
        start_price_ = tick.ask;
        grid_ceiling_ = tick.ask;
        grid_floor_ = tick.ask;
        last_momentum_entry_ = tick.ask;

        // Open initial position (counts as both grid and momentum start)
        double lot = CalculateGridLotSize(tick, engine);
        if (lot >= config_.min_volume) {
            double tp = config_.use_take_profit ? (tick.ask + config_.tp_distance) : 0.0;
            Trade* trade = engine.OpenMarketOrder("BUY", lot, 0.0, tp);
            if (trade) {
                stats_.grid_entries++;
                stats_.momentum_entries++;
                stats_.grid_volume += lot;
                stats_.momentum_volume += lot;
                stats_.total_entries++;
                stats_.total_volume += lot;
                stats_.grid_lowest_entry = tick.ask;
                stats_.grid_highest_entry = tick.ask;
                stats_.momentum_lowest_entry = tick.ask;
                stats_.momentum_highest_entry = tick.ask;
            }
        }

        initialized_ = true;
    }

    void TryGridEntry(const Tick& tick, TickBasedEngine& engine) {
        // Grid entry triggers when price drops below (grid_floor_ - spacing)
        if (tick.ask >= grid_floor_ - current_spacing_) {
            return;  // Price not low enough
        }

        double lot = CalculateGridLotSize(tick, engine);

        if (lot < config_.min_volume) {
            if (config_.force_min_volume_entry) {
                lot = config_.min_volume;
                stats_.forced_entries++;
            } else {
                stats_.skipped_by_margin++;
                return;
            }
        }

        // Check margin before opening
        if (!HasSufficientMargin(tick, engine, lot)) {
            stats_.skipped_by_margin++;
            return;
        }

        // Calculate take profit
        double tp = config_.use_take_profit ? (tick.ask + config_.tp_distance) : 0.0;

        Trade* trade = engine.OpenMarketOrder("BUY", lot, 0.0, tp);
        if (trade) {
            grid_floor_ = tick.ask;  // Update floor to current entry
            stats_.grid_entries++;
            stats_.grid_volume += lot;
            stats_.total_entries++;
            stats_.total_volume += lot;
            stats_.grid_lowest_entry = std::min(stats_.grid_lowest_entry, tick.ask);
            if (stats_.grid_highest_entry == 0) stats_.grid_highest_entry = tick.ask;
            stats_.grid_highest_entry = std::max(stats_.grid_highest_entry, tick.ask);
        }
    }

    void TryMomentumEntry(const Tick& tick, TickBasedEngine& engine) {
        // Momentum entry triggers when price exceeds last entry by momentum_spacing
        if (tick.ask <= last_momentum_entry_ + config_.momentum_spacing) {
            return;  // Price not high enough
        }

        double lot = CalculateMomentumLotSize(tick, engine);

        if (lot < config_.min_volume) {
            if (config_.force_min_volume_entry) {
                lot = config_.min_volume;
                stats_.forced_entries++;
            } else {
                stats_.skipped_by_margin++;
                return;
            }
        }

        // Check margin before opening
        if (!HasSufficientMargin(tick, engine, lot)) {
            stats_.skipped_by_margin++;
            return;
        }

        // Calculate take profit
        double tp = config_.use_take_profit ? (tick.ask + config_.tp_distance) : 0.0;

        Trade* trade = engine.OpenMarketOrder("BUY", lot, 0.0, tp);
        if (trade) {
            last_momentum_entry_ = tick.ask;  // Update to current entry
            stats_.momentum_entries++;
            stats_.momentum_volume += lot;
            stats_.total_entries++;
            stats_.total_volume += lot;
            stats_.momentum_lowest_entry = std::min(stats_.momentum_lowest_entry, tick.ask);
            stats_.momentum_highest_entry = std::max(stats_.momentum_highest_entry, tick.ask);
        }
    }

    double CalculateGridLotSize(const Tick& tick, TickBasedEngine& engine) {
        // AGGRESSIVE COMPOUNDING: Use fraction of equity for position sizing
        double equity = engine.GetEquity();

        // Calculate margin per lot using engine's authoritative calculation
        double margin_per_lot = engine.CalculateMarginRequired(1.0, tick.ask);

        // Use a fraction of equity for each new position
        double risk_factor = 0.05 * config_.grid_allocation;  // 5% of equity * allocation

        // Calculate lot size
        double lot = (equity * risk_factor) / margin_per_lot;

        // Apply free margin check - don't use more than 50% of free margin
        double free_margin = engine.GetFreeMargin();
        double max_lot_by_margin = (free_margin * 0.5) / margin_per_lot;
        lot = std::min(lot, max_lot_by_margin);

        // Clamp and normalize
        lot = std::max(lot, config_.min_volume);
        lot = std::min(lot, config_.max_volume);
        lot = engine.NormalizeLots(lot);

        return lot;
    }

    double CalculateMomentumLotSize(const Tick& tick, TickBasedEngine& engine) {
        // AGGRESSIVE COMPOUNDING: Use fraction of equity for position sizing
        double equity = engine.GetEquity();

        double margin_per_lot = engine.CalculateMarginRequired(1.0, tick.ask);
        double risk_factor = 0.05 * config_.momentum_allocation;  // 5% of equity * allocation

        double lot = (equity * risk_factor) / margin_per_lot;

        double free_margin = engine.GetFreeMargin();
        double max_lot_by_margin = (free_margin * 0.5) / margin_per_lot;
        lot = std::min(lot, max_lot_by_margin);

        lot = std::max(lot, config_.min_volume);
        lot = std::min(lot, config_.max_volume);
        lot = engine.NormalizeLots(lot);

        return lot;
    }

    bool HasSufficientMargin(const Tick& tick, TickBasedEngine& engine, double lot) {
        // Check if opening this position would cause immediate margin issues
        double equity = engine.GetEquity();

        // Use engine's authoritative margin
        double used_margin = engine.GetUsedMargin();

        // Calculate margin for new position
        double new_margin = engine.CalculateMarginRequired(lot, tick.ask);
        double total_margin = used_margin + new_margin;

        if (total_margin <= 0) return true;

        // Check margin level
        double margin_level = (equity / total_margin) * 100.0;

        // Require at least 2x the stop-out level for safety
        return margin_level > config_.margin_stop_out * 2.0;
    }
};

} // namespace backtest

#endif // STRATEGY_PARALLEL_DUAL_ORIGINAL_H
