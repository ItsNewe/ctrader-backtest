#ifndef STRATEGY_BIDIRECTIONAL_MOMENTUM_H
#define STRATEGY_BIDIRECTIONAL_MOMENTUM_H

/**
 * Bidirectional Momentum Strategy
 *
 * Combines two approaches:
 * 1. "Up while going down" - Grid-style entries when price falls (mean reversion)
 * 2. "Up while going up" - Momentum entries when price rises (trend following)
 *
 * Both use margin-based position sizing to survive a specified drawdown.
 */

#include "tick_based_engine.h"
#include <cmath>
#include <cfloat>
#include <algorithm>

namespace backtest {

class BidirectionalMomentumStrategy {
public:
    // Sizing mode: how to scale position sizes as grid fills
    enum SizingMode {
        CONSTANT = 0,      // Fixed lot size per level
        INCREMENTAL = 1,   // Increasing lots: 0.01, 0.02, 0.03...
        DENSITY = 2        // Variable spacing based on density_distortion
    };

    // Closing mode for "up while down" direction changes
    enum ClosingMode {
        CLOSE_PROFITABLE = 0,  // Close only profitable positions on direction change
        CLOSE_ALL_IF_PROFIT = 1 // Close all if no unprofitable positions
    };

    struct Config {
        // Survival parameters
        double survive_down_pct;   // % drop to survive for "up while down"
        double survive_up_pct;     // % drop to survive for "up while up"

        // Position sizing (contract_size, leverage, min/max volume come from engine.GetConfig())
        double margin_stopout;    // Margin call level %
        double commission_pips;    // Commission in pips

        // Grid settings
        SizingMode sizing_mode;
        ClosingMode closing_mode;
        double density_distortion; // For DENSITY mode

        // Enable/disable components
        bool enable_up_while_down;
        bool enable_up_while_up;

        // Direction detection spread multiplier
        double direction_threshold; // Multiplied by spread+commission

        Config() :
            survive_down_pct(5.0),
            survive_up_pct(4.0),
            margin_stopout(20.0),
            commission_pips(0.0),
            sizing_mode(CONSTANT),
            closing_mode(CLOSE_PROFITABLE),
            density_distortion(1.0),
            enable_up_while_down(true),
            enable_up_while_up(true),
            direction_threshold(1.0) {}
    };

    struct Stats {
        int down_grid_entries = 0;
        int up_momentum_entries = 0;
        double down_grid_volume = 0.0;
        double up_momentum_volume = 0.0;
        double max_volume = 0.0;
        double highest_price = 0.0;
        double lowest_price = DBL_MAX;
        int direction_changes = 0;
        int profitable_closes = 0;
    };

private:
    Config config_;
    Stats stats_;

    // Grid state ("up while down")
    double grid_spacing_ = 0.0;
    double grid_lot_size_ = 0.0;
    double highest_entry_ = DBL_MIN;
    double lowest_entry_ = DBL_MAX;
    double grid_volume_ = 0.0;
    int grid_count_ = 0;
    std::vector<double> density_spacing_;

    // Momentum state ("up while up")
    double last_momentum_entry_ = DBL_MIN;
    double momentum_volume_ = 0.0;

    // Direction tracking
    int current_direction_ = 0;  // 1 = up, -1 = down, 0 = unknown
    double bid_at_turn_up_ = DBL_MAX;
    double ask_at_turn_up_ = DBL_MAX;
    double bid_at_turn_down_ = DBL_MIN;
    double ask_at_turn_down_ = DBL_MIN;

    // Spread tracking
    double current_spread_ = 0.0;
    double spread_and_commission_ = 0.0;

    // Flags
    bool close_profitable_flag_ = false;
    bool close_all_flag_ = false;
    bool initialized_ = false;

public:
    BidirectionalMomentumStrategy(const Config& config = Config())
        : config_(config) {
        density_spacing_.resize(500, 0.0);
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        // Initialize on first tick
        if (!initialized_) {
            stats_.highest_price = tick.ask;
            stats_.lowest_price = tick.ask;
            initialized_ = true;
        }

        // Update stats
        stats_.highest_price = std::max(stats_.highest_price, tick.ask);
        stats_.lowest_price = std::min(stats_.lowest_price, tick.ask);

        // Calculate spread
        current_spread_ = tick.ask - tick.bid;
        double commission_value = config_.commission_pips * 0.01; // Convert pips to price
        spread_and_commission_ = current_spread_ + commission_value;

        // Update direction tracking
        UpdateDirection(tick, engine);

        // Track total volume
        double total_volume = engine.GetBuyVolume();
        stats_.max_volume = std::max(stats_.max_volume, total_volume);

        // Execute strategies
        if (config_.enable_up_while_down) {
            ExecuteUpWhileDown(tick, engine);
        }

        if (config_.enable_up_while_up) {
            ExecuteUpWhileUp(tick, engine);
        }
    }

    const Stats& GetStats() const { return stats_; }
    const Config& GetConfig() const { return config_; }

private:
    void UpdateDirection(const Tick& tick, TickBasedEngine& engine) {
        // Track extreme prices for direction detection
        bid_at_turn_down_ = std::max(tick.bid, bid_at_turn_down_);
        bid_at_turn_up_ = std::min(tick.bid, bid_at_turn_up_);
        ask_at_turn_down_ = std::max(tick.ask, ask_at_turn_down_);
        ask_at_turn_up_ = std::min(tick.ask, ask_at_turn_up_);

        double threshold = spread_and_commission_ * config_.direction_threshold;

        // Detect turn to DOWN
        if (current_direction_ != -1 &&
            ask_at_turn_down_ >= tick.ask + threshold &&
            bid_at_turn_down_ >= tick.bid + threshold) {

            current_direction_ = -1;
            stats_.direction_changes++;

            // Handle closing on direction change
            HandleDownwardTurn(tick, engine);

            // Reset turn tracking
            ClearTurnMarks();
        }

        // Detect turn to UP
        if (current_direction_ != 1 &&
            bid_at_turn_up_ <= tick.bid - threshold &&
            ask_at_turn_up_ <= tick.ask - threshold) {

            current_direction_ = 1;
            ClearTurnMarks();
        }
    }

    void ClearTurnMarks() {
        bid_at_turn_up_ = DBL_MAX;
        ask_at_turn_up_ = DBL_MAX;
        ask_at_turn_down_ = DBL_MIN;
        bid_at_turn_down_ = DBL_MIN;
    }

    void HandleDownwardTurn(const Tick& tick, TickBasedEngine& engine) {
        // Get current positions and their profit status
        auto& positions = engine.GetOpenPositions();
        if (positions.empty()) return;

        const auto& cfg = engine.GetConfig();
        double total_profit = 0.0;
        bool has_unprofitable = false;

        for (auto* pos : positions) {
            double price_diff = tick.bid - pos->entry_price;
            double unrealized = price_diff * pos->lot_size * cfg.contract_size;
            total_profit += unrealized;
            if (unrealized < 0) has_unprofitable = true;
        }

        // Decide what to close based on mode
        std::vector<Trade*> to_close;

        switch (config_.closing_mode) {
        case CLOSE_PROFITABLE:
            if (total_profit > 0) {
                for (auto* pos : positions) {
                    double price_diff = tick.bid - pos->entry_price;
                    double unrealized = price_diff * pos->lot_size * cfg.contract_size;
                    if (unrealized > 0) {
                        to_close.push_back(pos);
                    }
                }
            }
            break;

        case CLOSE_ALL_IF_PROFIT:
            if (!has_unprofitable) {
                for (auto* pos : positions) {
                    to_close.push_back(pos);
                }
            }
            break;
        }

        // Close selected positions
        for (auto* pos : to_close) {
            engine.ClosePosition(pos, "DirectionChange");
            stats_.profitable_closes++;
        }

        // Reset grid tracking after closes
        RefreshGridState(engine);
    }

    void RefreshGridState(TickBasedEngine& engine) {
        const auto& cfg = engine.GetConfig();
        grid_volume_ = engine.GetBuyVolume();
        grid_count_ = (int)engine.GetBuyPositionCount();
        highest_entry_ = (grid_count_ > 0) ? engine.GetHighestBuyEntry() : DBL_MIN;
        lowest_entry_ = (grid_count_ > 0) ? engine.GetLowestBuyEntry() : DBL_MAX;

        if (config_.sizing_mode == INCREMENTAL) {
            // Find max lot size for incremental mode (needs per-position scan)
            grid_lot_size_ = 0.0;
            for (auto* pos : engine.GetOpenPositions()) {
                grid_lot_size_ = std::max(grid_lot_size_, pos->lot_size);
            }
            grid_lot_size_ += cfg.volume_min;
        }
    }

    void ExecuteUpWhileDown(const Tick& tick, TickBasedEngine& engine) {
        const auto& cfg = engine.GetConfig();

        // Calculate sizing if no positions yet
        if (grid_count_ == 0) {
            CalculateGridSizing(tick, engine);
        }

        // First entry
        if (grid_count_ == 0) {
            if (OpenBuy(grid_lot_size_, tick, engine)) {
                highest_entry_ = tick.ask;
                lowest_entry_ = tick.ask;
                grid_count_++;
                stats_.down_grid_entries++;
                stats_.down_grid_volume += grid_lot_size_;

                if (config_.sizing_mode == INCREMENTAL) {
                    grid_lot_size_ += cfg.volume_min;
                }
            }
            return;
        }

        // Grid entries when price drops below lowest entry minus spacing
        if (lowest_entry_ > tick.ask) {
            double volume_to_add = 0.0;

            switch (config_.sizing_mode) {
            case CONSTANT: {
                double distance = highest_entry_ - tick.ask;
                double levels = std::floor(distance / grid_spacing_);
                double lot_per_level = grid_lot_size_;
                double spacing_fraction = grid_spacing_ / (lot_per_level / cfg.volume_min);
                double sub_levels = std::floor((distance - levels * grid_spacing_) / spacing_fraction);
                double expected_volume = levels * lot_per_level + sub_levels * cfg.volume_min;
                volume_to_add = expected_volume - (grid_volume_ - grid_lot_size_);
                break;
            }

            case INCREMENTAL: {
                double distance = highest_entry_ - tick.ask;
                double levels = std::floor(distance / grid_spacing_);
                double triangular_sum = 0.0;
                for (int i = 1; i <= (int)levels; i++) triangular_sum += i;
                double spacing_fraction = grid_spacing_ / (levels + 1);
                double sub_levels = std::floor((distance - levels * grid_spacing_) / spacing_fraction);
                double expected_volume = (triangular_sum + sub_levels) * cfg.volume_min;
                volume_to_add = expected_volume - (grid_volume_ - cfg.volume_min);
                break;
            }

            case DENSITY: {
                int idx = std::min(grid_count_, (int)density_spacing_.size() - 1);
                if (density_spacing_[idx] > 0 && lowest_entry_ > tick.ask + density_spacing_[idx]) {
                    volume_to_add = grid_lot_size_;
                }
                break;
            }
            }

            if (volume_to_add > 0) {
                if (OpenBuy(volume_to_add, tick, engine)) {
                    lowest_entry_ = tick.ask;
                    grid_count_++;
                    grid_volume_ += volume_to_add;
                    stats_.down_grid_entries++;
                    stats_.down_grid_volume += volume_to_add;
                }
            }
        }
    }

    void ExecuteUpWhileUp(const Tick& tick, TickBasedEngine& engine) {
        const auto& cfg = engine.GetConfig();

        // "Up while up": buy when price makes new high
        if (momentum_volume_ == 0.0 || last_momentum_entry_ < tick.ask) {
            // Calculate lot size based on margin capacity
            double lot_size = CalculateMomentumLotSize(tick, engine);

            if (lot_size >= cfg.volume_min) {
                if (OpenBuy(lot_size, tick, engine)) {
                    last_momentum_entry_ = tick.ask;
                    momentum_volume_ += lot_size;
                    stats_.up_momentum_entries++;
                    stats_.up_momentum_volume += lot_size;
                }
            }
        }
    }

    void CalculateGridSizing(const Tick& tick, TickBasedEngine& engine) {
        const auto& cfg = engine.GetConfig();
        double equity = engine.GetEquity();
        double used_margin = engine.GetUsedMargin();

        // Target price for survival
        double end_price = tick.ask * ((100.0 - config_.survive_down_pct) / 100.0);
        double price_range = tick.ask - end_price;

        // Calculate equity at margin stopout
        double margin_level = (equity / used_margin) * 100.0;
        if (used_margin == 0) margin_level = DBL_MAX;

        double equity_at_target = equity * config_.margin_stopout / margin_level;
        if (margin_level == DBL_MAX) equity_at_target = equity;

        // Estimate number of trades we can afford
        double trade_size = cfg.volume_min;
        double margin_per_trade = (trade_size * cfg.contract_size * tick.ask) / cfg.leverage;
        double loss_per_trade = trade_size * cfg.contract_size * price_range +
                               trade_size * spread_and_commission_ * cfg.contract_size;
        double cost_per_trade = (config_.margin_stopout / 100.0) * margin_per_trade + loss_per_trade;

        double num_trades;
        if (used_margin == 0) {
            num_trades = std::floor(equity / cost_per_trade);
        } else {
            num_trades = std::floor((equity - (config_.margin_stopout / 100.0) * used_margin) / cost_per_trade);
        }

        num_trades = std::max(1.0, num_trades);

        // Calculate spacing and lot size based on mode
        switch (config_.sizing_mode) {
        case CONSTANT: {
            double proportion = num_trades / price_range;
            if (proportion >= 1.0) {
                grid_spacing_ = 1.0;
                grid_lot_size_ = std::floor(proportion) * cfg.volume_min;
            } else {
                grid_lot_size_ = cfg.volume_min;
                grid_spacing_ = std::round((price_range / num_trades) * 100.0) / 100.0;
            }
            break;
        }

        case INCREMENTAL: {
            double temp = (-1.0 + std::sqrt(1.0 + 8.0 * num_trades)) / 2.0;
            temp = std::floor(temp);
            grid_spacing_ = price_range / (temp - 1.0);
            grid_lot_size_ = cfg.volume_min;
            break;
        }

        case DENSITY: {
            double proportion = num_trades / price_range;
            if (proportion >= 1.0) {
                grid_spacing_ = 1.0;
                grid_lot_size_ = std::floor(proportion) * cfg.volume_min;
            } else {
                grid_lot_size_ = cfg.volume_min;
                grid_spacing_ = std::round((price_range / num_trades) * 100.0) / 100.0;
            }

            // Calculate density-based spacing
            double sum = ((num_trades - 1) * num_trades / 2.0) * grid_spacing_;
            double unit = 0.0;
            double sum_2 = 0.0;

            while (sum_2 < sum) {
                sum_2 = 0.0;
                unit += 0.01;
                double count = 0.0;
                for (int i = (int)num_trades - 1; i > 0; i--) {
                    sum_2 += i * unit * std::pow(config_.density_distortion, count);
                    count++;
                }
            }
            unit -= 0.01;

            double count = 0.0;
            for (int i = (int)num_trades; i > 0 && i < (int)density_spacing_.size(); i--) {
                density_spacing_[i] = unit * std::pow(config_.density_distortion, count);
                count++;
            }
            break;
        }
        }

        // Ensure minimum values
        grid_lot_size_ = std::max(cfg.volume_min, grid_lot_size_);
        grid_spacing_ = std::max(0.01, grid_spacing_);
    }

    double CalculateMomentumLotSize(const Tick& tick, TickBasedEngine& engine) {
        const auto& cfg = engine.GetConfig();
        double equity = engine.GetEquity();
        double used_margin = engine.GetUsedMargin();

        // Target drop distance
        double end_price = tick.ask * ((100.0 - config_.survive_up_pct) / 100.0);
        double distance = tick.ask - end_price;

        // Get current total volume
        double total_volume = engine.GetBuyVolume();

        // Check if we can add more
        double equity_at_target = equity * config_.margin_stopout / 100.0;
        double equity_difference = equity - equity_at_target;
        double price_difference = equity_difference / (total_volume * cfg.contract_size + 0.0001);

        if (total_volume > 0 && (tick.ask - price_difference) >= end_price) {
            return 0.0; // Already at capacity
        }

        // Calculate available lot size using CFD leverage formula
        double loss_from_existing = total_volume * distance * cfg.contract_size;
        double available_equity = equity - loss_from_existing - (config_.margin_stopout / 100.0) * used_margin;

        // Lot size that doesn't exceed margin limits
        double margin_cost = (tick.ask * cfg.contract_size) / cfg.leverage;
        double loss_cost = distance * cfg.contract_size + spread_and_commission_ * cfg.contract_size;
        double cost_per_lot = (config_.margin_stopout / 100.0) * margin_cost + loss_cost;

        double lot_size = available_equity / cost_per_lot;

        // Normalize using engine's lot constraints
        lot_size = engine.NormalizeLots(lot_size);
        lot_size = std::max(0.0, lot_size);

        return lot_size;
    }

    bool OpenBuy(double lot_size, const Tick& tick, TickBasedEngine& engine) {
        const auto& cfg = engine.GetConfig();
        if (lot_size < cfg.volume_min) return false;

        lot_size = engine.NormalizeLots(lot_size);
        if (lot_size < cfg.volume_min) return false;

        auto* trade = engine.OpenMarketOrder("BUY", lot_size);
        return trade != nullptr;
    }
};

} // namespace backtest

#endif // STRATEGY_BIDIRECTIONAL_MOMENTUM_H
