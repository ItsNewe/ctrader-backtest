#ifndef STRATEGY_PARALLEL_DUAL_H
#define STRATEGY_PARALLEL_DUAL_H

#include "tick_based_engine.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>

namespace backtest {

/**
 * Unified Dynamic Survival Strategy
 *
 * A single unified system where:
 * 1. Survive floor tracks current price: floor = tick.ask * (1 - survive_pct/100)
 * 2. Before ANY entry, verify margin survives a drop to floor
 * 3. Spacing adapts based on remaining distance and usable margin
 *
 * Entry conditions:
 * - UPWARD: price > highest_entry + min_spacing
 * - DOWNWARD: price < lowest_entry - adaptive_spacing
 *
 * NO TAKE PROFIT - pure accumulation strategy.
 * Profits come from unrealized gains as price rises.
 */
class ParallelDualStrategy {
public:
    struct Config {
        // Core survival parameter
        double survive_pct;           // % drop to survive (floor = price * (1 - survive_pct/100))

        // Volume limits
        double min_volume;
        double max_volume;

        // Broker settings
        double contract_size;
        double leverage;

        // Spacing parameters
        double min_spacing;           // Minimum spacing for upward entries ($)
        double max_spacing;           // Maximum adaptive spacing for downward entries ($)
        double base_spacing;          // Base spacing for downward entries ($)

        // Target trades for spacing calculation
        int target_trades_in_range;   // How many trades to fit between price and floor

        // Safety
        double margin_stop_out;       // Stop-out margin level %
        double safety_buffer;         // Extra margin safety multiplier (e.g., 1.2 = 20% buffer)

        Config()
            : survive_pct(15.0),
              min_volume(0.01),
              max_volume(10.0),
              contract_size(100.0),
              leverage(500.0),
              min_spacing(1.0),
              max_spacing(10.0),
              base_spacing(1.50),
              target_trades_in_range(20),
              margin_stop_out(20.0),
              safety_buffer(1.5) {}
    };

    struct Stats {
        int upward_entries;
        int downward_entries;
        int total_entries;
        double total_volume;
        int skipped_by_margin;

        double highest_entry;
        double lowest_entry;

        Stats()
            : upward_entries(0),
              downward_entries(0),
              total_entries(0),
              total_volume(0.0),
              skipped_by_margin(0),
              highest_entry(0.0),
              lowest_entry(DBL_MAX) {}
    };

    explicit ParallelDualStrategy(const Config& config = Config())
        : config_(config),
          stats_(),
          initialized_(false),
          highest_entry_(0.0),
          lowest_entry_(DBL_MAX),
          current_adaptive_spacing_(config.base_spacing) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        if (!initialized_) {
            Initialize(tick, engine);
            return;
        }

        // Calculate current floor (tracks current price)
        double floor = tick.ask * (1.0 - config_.survive_pct / 100.0);

        // UPWARD: price rose above highest entry + min_spacing
        if (tick.ask > highest_entry_ + config_.min_spacing) {
            TryUpwardEntry(tick, engine, floor);
        }
        // DOWNWARD: price dropped below lowest entry - adaptive spacing
        else if (tick.ask < lowest_entry_ - current_adaptive_spacing_) {
            TryDownwardEntry(tick, engine, floor);
        }
    }

    const Stats& GetStats() const { return stats_; }
    const Config& GetConfig() const { return config_; }
    double GetCurrentSpacing() const { return current_adaptive_spacing_; }
    double GetHighestEntry() const { return highest_entry_; }
    double GetLowestEntry() const { return lowest_entry_; }

private:
    Config config_;
    Stats stats_;

    bool initialized_;
    double highest_entry_;
    double lowest_entry_;
    double current_adaptive_spacing_;

    void Initialize(const Tick& tick, TickBasedEngine& engine) {
        double floor = tick.ask * (1.0 - config_.survive_pct / 100.0);

        // Calculate initial lot sized to survive drop to floor
        double lot = CalculateInitialLot(tick.ask, floor, engine);

        if (lot >= config_.min_volume) {
            Trade* trade = engine.OpenMarketOrder("BUY", lot, 0.0, 0.0);  // No TP
            if (trade) {
                highest_entry_ = tick.ask;
                lowest_entry_ = tick.ask;
                stats_.highest_entry = tick.ask;
                stats_.lowest_entry = tick.ask;
                stats_.total_entries++;
                stats_.total_volume += lot;
                // First entry counts as both upward and downward start
                stats_.upward_entries++;
            }
        }

        // Calculate initial adaptive spacing
        UpdateAdaptiveSpacing(tick.ask, floor, engine);

        initialized_ = true;
    }

    void TryUpwardEntry(const Tick& tick, TickBasedEngine& engine, double floor) {
        // Calculate lot size for upward entry
        double lot = CalculateLotForEntry(tick.ask, floor, engine);

        if (lot < config_.min_volume) {
            stats_.skipped_by_margin++;
            return;
        }

        // Verify we can survive a drop to floor with this new trade
        if (!CanSurvive(tick, engine, lot, floor)) {
            stats_.skipped_by_margin++;
            return;
        }

        Trade* trade = engine.OpenMarketOrder("BUY", lot, 0.0, 0.0);  // No TP
        if (trade) {
            highest_entry_ = tick.ask;
            stats_.highest_entry = std::max(stats_.highest_entry, tick.ask);
            stats_.upward_entries++;
            stats_.total_entries++;
            stats_.total_volume += lot;

            // Recalculate adaptive spacing after entry
            UpdateAdaptiveSpacing(tick.ask, floor, engine);
        }
    }

    void TryDownwardEntry(const Tick& tick, TickBasedEngine& engine, double floor) {
        // Recalculate adaptive spacing for current conditions
        UpdateAdaptiveSpacing(tick.ask, floor, engine);

        // Calculate lot size for downward entry
        double lot = CalculateLotForEntry(tick.ask, floor, engine);

        if (lot < config_.min_volume) {
            stats_.skipped_by_margin++;
            return;
        }

        // Verify we can survive a drop to floor with this new trade
        if (!CanSurvive(tick, engine, lot, floor)) {
            stats_.skipped_by_margin++;
            return;
        }

        Trade* trade = engine.OpenMarketOrder("BUY", lot, 0.0, 0.0);  // No TP
        if (trade) {
            lowest_entry_ = tick.ask;
            stats_.lowest_entry = std::min(stats_.lowest_entry, tick.ask);
            stats_.downward_entries++;
            stats_.total_entries++;
            stats_.total_volume += lot;

            // Recalculate adaptive spacing after entry
            UpdateAdaptiveSpacing(tick.ask, floor, engine);
        }
    }

    double CalculateInitialLot(double price, double floor, TickBasedEngine& engine) {
        double equity = engine.GetEquity();
        double distance = price - floor;

        if (distance <= 0) {
            distance = price * (config_.survive_pct / 100.0);
        }

        double loss_per_lot = distance * config_.contract_size;
        double margin_per_lot = engine.CalculateMarginRequired(1.0, floor);

        double target_margin_level = config_.margin_stop_out * config_.safety_buffer;
        double cost_per_lot = loss_per_lot + margin_per_lot * target_margin_level / 100.0;

        if (cost_per_lot <= 0) return config_.min_volume;

        double max_lots = equity / cost_per_lot;
        double lot = max_lots * 0.05;

        lot = std::floor(lot / config_.min_volume) * config_.min_volume;
        return std::clamp(lot, config_.min_volume, config_.max_volume);
    }

    double CalculateLotForEntry(double price, double floor, TickBasedEngine& engine) {
        double equity = engine.GetEquity();

        // Single-pass: compute P/L at floor, P/L at current, and margin at floor
        double current_pnl_at_floor = 0.0;
        double current_margin_at_floor = 0.0;
        double current_pnl = 0.0;

        for (const Trade* trade : engine.GetOpenPositions()) {
            double lots_x_contract = trade->lot_size * config_.contract_size;
            current_pnl_at_floor += (floor - trade->entry_price) * lots_x_contract;
            current_pnl += (price - trade->entry_price) * lots_x_contract;
            current_margin_at_floor += engine.CalculateMarginRequired(trade->lot_size, floor);
        }

        double equity_at_floor = equity + (current_pnl_at_floor - current_pnl);
        if (equity_at_floor <= 0) return 0.0;

        double target_margin_level = config_.margin_stop_out * config_.safety_buffer;

        if (current_margin_at_floor > 0) {
            double current_margin_level = (equity_at_floor / current_margin_at_floor) * 100.0;
            if (current_margin_level < target_margin_level * 2.0) {
                return 0.0;
            }
        }

        double distance = price - floor;
        if (distance <= 0) distance = price * (config_.survive_pct / 100.0);

        double loss_per_lot = distance * config_.contract_size;
        double margin_per_lot = engine.CalculateMarginRequired(1.0, floor);

        double required_reserve = current_margin_at_floor * target_margin_level / 100.0;
        double available = equity_at_floor - required_reserve;
        if (available <= 0) return 0.0;

        double cost_per_lot = loss_per_lot + margin_per_lot * target_margin_level / 100.0;
        if (cost_per_lot <= 0) return config_.min_volume;

        double max_lots = available / cost_per_lot * 0.20;
        double lot = std::floor(max_lots / config_.min_volume) * config_.min_volume;

        return std::clamp(lot, 0.0, config_.max_volume);
    }

    bool CanSurvive(const Tick& tick, TickBasedEngine& engine, double new_lot, double floor) {
        double equity = engine.GetEquity();

        // Single-pass: compute loss and margin at floor for all existing positions
        double total_loss = 0.0;
        double total_margin = 0.0;

        for (const Trade* trade : engine.GetOpenPositions()) {
            total_loss += (trade->entry_price - floor) * trade->lot_size * config_.contract_size;
            total_margin += engine.CalculateMarginRequired(trade->lot_size, floor);
        }

        // Add hypothetical new trade
        total_loss += (tick.ask - floor) * new_lot * config_.contract_size;
        total_margin += engine.CalculateMarginRequired(new_lot, floor);

        double equity_at_floor = equity - total_loss;
        if (equity_at_floor <= 0) return false;
        if (total_margin <= 0) return true;

        double margin_level = (equity_at_floor / total_margin) * 100.0;
        return margin_level > config_.margin_stop_out * config_.safety_buffer;
    }

    void UpdateAdaptiveSpacing(double current_price, double floor, TickBasedEngine& engine) {
        double remaining_distance = current_price - floor;

        if (remaining_distance <= 0) {
            current_adaptive_spacing_ = config_.min_spacing;
            return;
        }

        double equity = engine.GetEquity();
        double current_loss_at_floor = 0.0;
        double current_margin_at_floor = 0.0;

        // Single-pass: compute loss and margin at floor
        for (const Trade* trade : engine.GetOpenPositions()) {
            current_loss_at_floor += (trade->entry_price - floor) * trade->lot_size * config_.contract_size;
            current_margin_at_floor += engine.CalculateMarginRequired(trade->lot_size, floor);
        }

        double equity_at_floor = equity - current_loss_at_floor;
        double target_margin_level = config_.margin_stop_out * config_.safety_buffer;

        double usable_equity = equity_at_floor - (current_margin_at_floor * target_margin_level / 100.0);

        if (usable_equity <= 0) {
            current_adaptive_spacing_ = config_.max_spacing;
            return;
        }

        double loss_per_trade = (remaining_distance / config_.target_trades_in_range) * config_.min_volume * config_.contract_size;
        double margin_per_trade = engine.CalculateMarginRequired(config_.min_volume, floor);
        double cost_per_trade = loss_per_trade + margin_per_trade * target_margin_level / 100.0;

        if (cost_per_trade <= 0) {
            current_adaptive_spacing_ = config_.base_spacing;
            return;
        }

        int max_trades = static_cast<int>(usable_equity / cost_per_trade);
        max_trades = std::max(1, max_trades);

        double spacing = remaining_distance / max_trades;
        current_adaptive_spacing_ = std::clamp(spacing, config_.min_spacing, config_.max_spacing);
    }
};

} // namespace backtest

#endif // STRATEGY_PARALLEL_DUAL_H
