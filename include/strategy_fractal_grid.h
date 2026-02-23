#ifndef STRATEGY_FRACTAL_GRID_H
#define STRATEGY_FRACTAL_GRID_H

/**
 * Fractal Self-Similarity Strategy (Multi-Scale Grid)
 *
 * CONCEPT: Market patterns are self-similar at different scales.
 * Run the SAME percentage-based strategy at multiple timeframes
 * simultaneously, each capturing oscillations at its frequency.
 *
 * Three independent grids:
 * - Micro-grid: 0.02% spacing (captures small/fast oscillations)
 * - Meso-grid:  0.10% spacing (captures medium oscillations)
 * - Macro-grid: 0.50% spacing (captures large/slow oscillations)
 *
 * Each grid operates independently with its own positions.
 * Capital is allocated across the scales.
 */

#include "tick_based_engine.h"
#include <vector>
#include <deque>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>

namespace backtest {

class StrategyFractalGrid {
public:
    // Configuration for a single grid scale
    struct GridConfig {
        std::string name;              // e.g., "MICRO", "MESO", "MACRO"
        double spacing_pct;            // Grid spacing as % of price
        double capital_fraction;       // Fraction of capital allocated (0.0-1.0)
        double survive_pct;            // Max adverse move this grid survives
        double min_lot;                // Minimum lot size
        double max_lot;                // Maximum lot size

        GridConfig(const std::string& n, double sp, double cap, double surv,
                   double minl = 0.01, double maxl = 10.0)
            : name(n), spacing_pct(sp), capital_fraction(cap), survive_pct(surv),
              min_lot(minl), max_lot(maxl) {}
    };

    // State for a single grid
    struct GridState {
        double lowest_buy;
        double highest_buy;
        double volume_open;
        double peak_equity;
        int trade_count;
        double realized_pnl;
        std::vector<int> position_ids;  // Track positions belonging to this grid

        GridState() : lowest_buy(DBL_MAX), highest_buy(DBL_MIN),
                      volume_open(0.0), peak_equity(0.0),
                      trade_count(0), realized_pnl(0.0) {}
    };

    /**
     * Constructor
     * @param grids Vector of grid configurations
     */
    StrategyFractalGrid(const std::vector<GridConfig>& grids)
        : grids_(grids),
          current_ask_(0.0),
          current_bid_(0.0),
          current_spread_(0.0),
          current_equity_(0.0),
          current_balance_(0.0),
          total_peak_equity_(0.0),
          ticks_processed_(0),
          total_trades_(0),
          initialized_(false)
    {
        // Initialize grid states
        grid_states_.resize(grids.size());

        // Normalize capital fractions
        double total_fraction = 0.0;
        for (const auto& g : grids_) {
            total_fraction += g.capital_fraction;
        }
        if (total_fraction > 0 && std::abs(total_fraction - 1.0) > 0.01) {
            for (auto& g : grids_) {
                g.capital_fraction /= total_fraction;
            }
        }
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_spread_ = tick.spread();
        current_equity_ = engine.GetEquity();
        current_balance_ = engine.GetBalance();
        ticks_processed_++;

        // Initialize on first tick
        if (!initialized_) {
            total_peak_equity_ = current_equity_;
            for (auto& state : grid_states_) {
                state.peak_equity = current_equity_;
            }
            initialized_ = true;
        }

        // Update peak equity
        if (current_equity_ > total_peak_equity_) {
            total_peak_equity_ = current_equity_;
        }

        // Track which positions belong to which grid
        UpdatePositionTracking(engine);

        // Process each grid independently
        for (size_t i = 0; i < grids_.size(); i++) {
            ProcessGrid(i, tick, engine);
        }
    }

    // Statistics accessors
    int GetTotalTrades() const { return total_trades_; }
    long GetTicksProcessed() const { return ticks_processed_; }
    double GetPeakEquity() const { return total_peak_equity_; }

    int GetGridTrades(size_t grid_idx) const {
        if (grid_idx < grid_states_.size()) {
            return grid_states_[grid_idx].trade_count;
        }
        return 0;
    }

    double GetGridRealizedPnL(size_t grid_idx) const {
        if (grid_idx < grid_states_.size()) {
            return grid_states_[grid_idx].realized_pnl;
        }
        return 0.0;
    }

    int GetGridOpenPositions(size_t grid_idx) const {
        if (grid_idx < grid_states_.size()) {
            return (int)grid_states_[grid_idx].position_ids.size();
        }
        return 0;
    }

    const std::vector<GridConfig>& GetGridConfigs() const { return grids_; }
    const std::vector<GridState>& GetGridStates() const { return grid_states_; }

private:
    std::vector<GridConfig> grids_;
    std::vector<GridState> grid_states_;

    double current_ask_;
    double current_bid_;
    double current_spread_;
    double current_equity_;
    double current_balance_;
    double total_peak_equity_;

    long ticks_processed_;
    int total_trades_;
    bool initialized_;

    // Map trade ID to grid index
    std::unordered_map<int, size_t> trade_to_grid_;

    void UpdatePositionTracking(TickBasedEngine& engine) {
        // Clear position counts
        for (auto& state : grid_states_) {
            state.position_ids.clear();
            state.volume_open = 0.0;
            state.lowest_buy = DBL_MAX;
            state.highest_buy = DBL_MIN;
        }

        // Rebuild from open positions
        for (const Trade* trade : engine.GetOpenPositions()) {
            auto it = trade_to_grid_.find(trade->id);
            if (it != trade_to_grid_.end()) {
                size_t grid_idx = it->second;
                if (grid_idx < grid_states_.size()) {
                    auto& state = grid_states_[grid_idx];
                    state.position_ids.push_back(trade->id);
                    if (trade->IsBuy()) {
                        state.volume_open += trade->lot_size;
                        state.lowest_buy = std::min(state.lowest_buy, trade->entry_price);
                        state.highest_buy = std::max(state.highest_buy, trade->entry_price);
                    }
                }
            }
        }
    }

    void ProcessGrid(size_t grid_idx, const Tick& tick, TickBasedEngine& engine) {
        const auto& config = grids_[grid_idx];
        auto& state = grid_states_[grid_idx];

        // Calculate current spacing in absolute terms
        double spacing = current_bid_ * (config.spacing_pct / 100.0);

        // Calculate TP (spread + spacing)
        double tp_distance = current_spread_ + spacing;

        // Check if we should open a new position
        int positions_for_grid = (int)state.position_ids.size();

        if (positions_for_grid == 0) {
            // Open first position for this grid
            double lots = CalculateLotSize(grid_idx, engine, 0);
            if (lots >= config.min_lot) {
                double tp = current_ask_ + tp_distance;
                Trade* trade = engine.OpenMarketOrder("BUY", lots, 0.0, tp);
                if (trade) {
                    trade_to_grid_[trade->id] = grid_idx;
                    state.trade_count++;
                    total_trades_++;
                    state.highest_buy = current_ask_;
                    state.lowest_buy = current_ask_;
                }
            }
        } else {
            // Check for grid entry conditions
            if (state.lowest_buy >= current_ask_ + spacing) {
                // Price dropped by spacing - add position
                double lots = CalculateLotSize(grid_idx, engine, positions_for_grid);
                if (lots >= config.min_lot) {
                    double tp = current_ask_ + tp_distance;
                    Trade* trade = engine.OpenMarketOrder("BUY", lots, 0.0, tp);
                    if (trade) {
                        trade_to_grid_[trade->id] = grid_idx;
                        state.trade_count++;
                        total_trades_++;
                        state.lowest_buy = current_ask_;
                    }
                }
            } else if (state.highest_buy <= current_ask_ - spacing) {
                // Price rose by spacing - add position at higher level
                double lots = CalculateLotSize(grid_idx, engine, positions_for_grid);
                if (lots >= config.min_lot) {
                    double tp = current_ask_ + tp_distance;
                    Trade* trade = engine.OpenMarketOrder("BUY", lots, 0.0, tp);
                    if (trade) {
                        trade_to_grid_[trade->id] = grid_idx;
                        state.trade_count++;
                        total_trades_++;
                        state.highest_buy = current_ask_;
                    }
                }
            }
        }
    }

    double CalculateLotSize(size_t grid_idx, TickBasedEngine& engine, int positions_for_grid) {
        const auto& config = grids_[grid_idx];
        auto& state = grid_states_[grid_idx];

        // Calculate allocated capital for this grid
        double allocated_capital = current_equity_ * config.capital_fraction;

        // Calculate used margin for this grid's positions
        double grid_used_margin = 0.0;
        for (int trade_id : state.position_ids) {
            for (const Trade* trade : engine.GetOpenPositions()) {
                if (trade->id == trade_id) {
                    grid_used_margin += engine.CalculateMarginRequired(trade->lot_size, trade->entry_price);
                    break;
                }
            }
        }

        // Calculate spacing and survive distance
        double spacing = current_bid_ * (config.spacing_pct / 100.0);

        // End price if price drops by survive_pct from entry
        double reference_price = (positions_for_grid == 0) ? current_ask_ : state.highest_buy;
        double end_price = reference_price * ((100.0 - config.survive_pct) / 100.0);
        double distance = current_ask_ - end_price;

        // Number of potential grid levels
        double number_of_trades = std::floor(distance / spacing);
        if (number_of_trades <= 0) number_of_trades = 1;

        // Calculate lot size that keeps margin level above 20% at worst point
        double margin_stop_out = 20.0;

        // Equity at target (worst case) for this grid's capital
        double contract_size = engine.GetConfig().contract_size;
        double grid_equity_at_target = allocated_capital - state.volume_open * distance * contract_size;

        // Check if already at risk
        double margin_level_now = (grid_used_margin > 0)
            ? (allocated_capital / grid_used_margin * 100.0)
            : 10000.0;

        if (margin_level_now < margin_stop_out * 2) {
            return 0.0;  // Already too close to stop-out
        }

        // Calculate safe lot size
        double trade_size = config.min_lot;
        double d_equity = contract_size * trade_size * spacing *
                         (number_of_trades * (number_of_trades + 1) / 2);
        double d_margin = number_of_trades * engine.CalculateMarginRequired(trade_size, current_ask_);

        // Find maximum multiplier
        double max_mult = config.max_lot / config.min_lot;
        for (double mult = max_mult; mult >= 1.0; mult -= 0.1) {
            double test_equity = grid_equity_at_target - mult * d_equity;
            double test_margin = grid_used_margin + mult * d_margin;

            if (test_margin > 0 && (test_equity / test_margin * 100.0) > margin_stop_out) {
                trade_size = mult * config.min_lot;
                break;
            }
        }

        // Apply lot constraints
        trade_size = std::max(config.min_lot, std::min(config.max_lot, trade_size));
        trade_size = engine.NormalizeLots(trade_size);

        return trade_size;
    }
};

/**
 * Factory function for common fractal configurations
 */
inline std::vector<StrategyFractalGrid::GridConfig> CreateFractalConfig(
    const std::string& preset,
    double survive_pct = 13.0)
{
    std::vector<StrategyFractalGrid::GridConfig> configs;

    if (preset == "EQUAL" || preset == "33_33_33") {
        // Equal capital allocation
        configs.emplace_back("MICRO", 0.02, 0.333, survive_pct, 0.01, 3.0);
        configs.emplace_back("MESO",  0.10, 0.333, survive_pct, 0.01, 5.0);
        configs.emplace_back("MACRO", 0.50, 0.334, survive_pct, 0.01, 10.0);
    }
    else if (preset == "20_30_50") {
        // More capital to larger oscillations
        configs.emplace_back("MICRO", 0.02, 0.20, survive_pct, 0.01, 2.0);
        configs.emplace_back("MESO",  0.10, 0.30, survive_pct, 0.01, 5.0);
        configs.emplace_back("MACRO", 0.50, 0.50, survive_pct, 0.01, 10.0);
    }
    else if (preset == "10_30_60") {
        // Heavy weighting to large oscillations
        configs.emplace_back("MICRO", 0.02, 0.10, survive_pct, 0.01, 1.0);
        configs.emplace_back("MESO",  0.10, 0.30, survive_pct, 0.01, 5.0);
        configs.emplace_back("MACRO", 0.50, 0.60, survive_pct, 0.01, 10.0);
    }
    else if (preset == "50_30_20") {
        // Heavy weighting to small oscillations (higher frequency)
        configs.emplace_back("MICRO", 0.02, 0.50, survive_pct, 0.01, 5.0);
        configs.emplace_back("MESO",  0.10, 0.30, survive_pct, 0.01, 5.0);
        configs.emplace_back("MACRO", 0.50, 0.20, survive_pct, 0.01, 10.0);
    }
    else if (preset == "MICRO_ONLY") {
        // Single scale: micro only (for comparison)
        configs.emplace_back("MICRO", 0.02, 1.0, survive_pct, 0.01, 10.0);
    }
    else if (preset == "MESO_ONLY") {
        // Single scale: meso only (for comparison)
        configs.emplace_back("MESO", 0.10, 1.0, survive_pct, 0.01, 10.0);
    }
    else if (preset == "MACRO_ONLY") {
        // Single scale: macro only (for comparison)
        configs.emplace_back("MACRO", 0.50, 1.0, survive_pct, 0.01, 10.0);
    }
    else if (preset == "BASELINE") {
        // Baseline: single grid at 0.043% (~$1.50 at $3500)
        configs.emplace_back("BASELINE", 0.043, 1.0, survive_pct, 0.01, 10.0);
    }
    else if (preset == "FIVE_SCALE") {
        // Five scales for finer fractal coverage
        configs.emplace_back("NANO",   0.01, 0.15, survive_pct, 0.01, 2.0);
        configs.emplace_back("MICRO",  0.02, 0.15, survive_pct, 0.01, 3.0);
        configs.emplace_back("MINI",   0.05, 0.20, survive_pct, 0.01, 4.0);
        configs.emplace_back("MESO",   0.10, 0.25, survive_pct, 0.01, 5.0);
        configs.emplace_back("MACRO",  0.50, 0.25, survive_pct, 0.01, 10.0);
    }
    else if (preset == "ADAPTIVE_MICRO") {
        // Micro spacing with adaptive vol (for volatility comparison)
        configs.emplace_back("MICRO_AD", 0.03, 0.40, survive_pct, 0.01, 5.0);
        configs.emplace_back("MESO_AD",  0.08, 0.30, survive_pct, 0.01, 5.0);
        configs.emplace_back("MACRO_AD", 0.25, 0.30, survive_pct, 0.01, 10.0);
    }

    return configs;
}

} // namespace backtest

#endif // STRATEGY_FRACTAL_GRID_H
