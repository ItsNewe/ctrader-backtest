#ifndef STRATEGY_LIQUIDITY_PREMIUM_H
#define STRATEGY_LIQUIDITY_PREMIUM_H

#include "tick_based_engine.h"
#include <deque>
#include <cmath>
#include <algorithm>
#include <vector>

namespace backtest {

/**
 * Liquidity Premium Capture Strategy
 *
 * Market makers earn the bid-ask spread by providing liquidity.
 * This strategy captures a similar premium by placing a grid of buy orders
 * below the current price, acting as a passive liquidity provider.
 *
 * When price oscillates down to your levels and back up, you've effectively
 * been "paid" the oscillation premium for providing liquidity at lower levels.
 *
 * Key insight: Grid levels act as limit orders. Price touching them = your
 * "limit order filled" by aggressive sellers needing immediate execution.
 *
 * Difference from FillUp: Focus on SHORT hold times and TIGHT TP.
 * Goal is high turnover, small profit per trade, relying on volume.
 *
 * Best on: XAGUSD (highest OMR, moderate spread, high volatility)
 * Also works on: XAUUSD (proven oscillation data)
 */
class LiquidityPremium {
public:
    struct Config {
        int grid_levels = 10;               // Number of buy levels below price
        double level_spacing = 0.50;        // Spacing between levels (price units)
        double tp_distance = 0.30;          // Tight TP for quick turnover
        double lot_size = 0.02;             // Fixed lot per level
        double max_total_lots = 0.50;       // Maximum total exposure
        double contract_size = 100.0;
        double leverage = 500.0;
        int refresh_interval = 5000;        // Ticks between grid rebuilds
        double grid_shift_threshold = 2.0;  // % move before grid rebuild
        int warmup_ticks = 1000;
    };

    LiquidityPremium(const Config& cfg)
        : config_(cfg),
          grid_anchor_(0.0),
          ticks_processed_(0),
          fills_(0),
          grid_rebuilds_(0),
          last_rebuild_tick_(0),
          peak_equity_(0.0),
          max_dd_pct_(0.0) {
        grid_filled_.resize(cfg.grid_levels, false);
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        ticks_processed_++;

        double equity = engine.GetEquity();
        if (peak_equity_ == 0.0) peak_equity_ = equity;
        if (equity > peak_equity_) peak_equity_ = equity;
        double dd = (peak_equity_ - equity) / peak_equity_ * 100.0;
        if (dd > max_dd_pct_) max_dd_pct_ = dd;

        if (ticks_processed_ < config_.warmup_ticks) return;

        // Initialize grid anchor
        if (grid_anchor_ == 0.0) {
            grid_anchor_ = tick.bid;
            last_rebuild_tick_ = ticks_processed_;
        }

        // Check if grid needs rebuilding (price moved significantly)
        double anchor_drift = std::abs(tick.bid - grid_anchor_) / grid_anchor_ * 100.0;
        bool time_for_rebuild = (ticks_processed_ - last_rebuild_tick_) > config_.refresh_interval;

        if (anchor_drift > config_.grid_shift_threshold && time_for_rebuild) {
            RebuildGrid(tick, engine);
        }

        // Check for fills (price touching grid levels)
        CheckFills(tick, engine);
    }

    int GetFills() const { return fills_; }
    int GetGridRebuilds() const { return grid_rebuilds_; }
    double GetMaxDDPct() const { return max_dd_pct_; }

private:
    Config config_;
    double grid_anchor_;
    int ticks_processed_;
    int fills_;
    int grid_rebuilds_;
    int last_rebuild_tick_;
    double peak_equity_;
    double max_dd_pct_;
    std::vector<bool> grid_filled_;

    void RebuildGrid(const Tick& tick, TickBasedEngine& engine) {
        // Close all open positions (reset grid)
        auto positions = engine.GetOpenPositions();
        for (int i = (int)positions.size() - 1; i >= 0; i--) {
            engine.ClosePosition(positions[i], "GRID_REBUILD");
        }

        // Reset grid state
        grid_anchor_ = tick.bid;
        std::fill(grid_filled_.begin(), grid_filled_.end(), false);
        last_rebuild_tick_ = ticks_processed_;
        grid_rebuilds_++;
    }

    void CheckFills(const Tick& tick, TickBasedEngine& engine) {
        // Calculate total current lots
        double total_lots = 0.0;
        for (const Trade* t : engine.GetOpenPositions()) {
            total_lots += t->lot_size;
        }

        // Check each grid level
        for (int i = 0; i < config_.grid_levels; i++) {
            if (grid_filled_[i]) continue;

            double level_price = grid_anchor_ - (i + 1) * config_.level_spacing;

            // Check if price has touched this level
            if (tick.bid <= level_price && total_lots < config_.max_total_lots) {
                // "Limit order filled" - enter buy with tight TP
                double tp = tick.ask + config_.tp_distance;
                engine.OpenMarketOrder("BUY", config_.lot_size, 0, tp);
                grid_filled_[i] = true;
                fills_++;
                total_lots += config_.lot_size;
            }
        }

        // Check if levels should be reset (price back above anchor)
        if (tick.bid > grid_anchor_ + config_.level_spacing) {
            // Price above grid - reset filled levels for reuse
            grid_anchor_ = tick.bid;
            std::fill(grid_filled_.begin(), grid_filled_.end(), false);
        }
    }
};

} // namespace backtest

#endif // STRATEGY_LIQUIDITY_PREMIUM_H
