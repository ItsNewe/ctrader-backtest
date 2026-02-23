#pragma once
/**
 * Bidirectional Grid Strategy
 *
 * Philosophy: Profit from movement in EITHER direction
 * Uses TickBasedEngine for all position/margin/balance management.
 */

#include "tick_based_engine.h"
#include <cmath>
#include <algorithm>

namespace backtest {

struct BiGridConfig {
    double grid_spacing = 50.0;
    double lot_size = 0.1;
    int max_levels_per_side = 10;
    double take_profit = 50.0;
    double stop_out_level = 50.0;
    double max_exposure_ratio = 0.5;
    bool enable_rebalancing = true;
};

struct BiGridResult {
    double final_equity = 0.0;
    double max_drawdown_pct = 0.0;
    double max_equity = 0.0;
    int total_trades = 0;
    int long_trades = 0;
    int short_trades = 0;
    int tp_hits = 0;
    double total_long_lots = 0.0;
    double total_short_lots = 0.0;
    bool margin_call_occurred = false;
};

class BidirectionalGrid {
private:
    BiGridConfig config_;
    double grid_anchor_ = 0.0;
    double highest_long_ = 0.0;
    double lowest_long_ = 1e9;
    double highest_short_ = 0.0;
    double lowest_short_ = 1e9;
    BiGridResult result_;

public:
    BidirectionalGrid() = default;
    explicit BidirectionalGrid(const BiGridConfig& cfg) : config_(cfg) {}
    void configure(const BiGridConfig& cfg) { config_ = cfg; }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        double mid = (tick.bid + tick.ask) / 2.0;
        if (grid_anchor_ == 0.0) grid_anchor_ = mid;

        if (check_margin_stop_out(tick, engine)) return;
        check_new_entries(tick, engine);
        if (config_.enable_rebalancing) check_rebalance(tick, engine);

        double equity = engine.GetEquity();
        if (equity > result_.max_equity) result_.max_equity = equity;
    }

    BiGridResult get_result(const TickBasedEngine& engine) {
        result_.final_equity = engine.GetEquity();
        result_.total_long_lots = engine.GetBuyVolume();
        result_.total_short_lots = engine.GetSellVolume();
        return result_;
    }

private:
    bool check_margin_stop_out(const Tick& tick, TickBasedEngine& engine) {
        if (engine.GetOpenPositions().empty()) return false;
        double margin_level = engine.GetMarginLevel();
        if (margin_level > 0 && margin_level < config_.stop_out_level) {
            result_.margin_call_occurred = true;
            auto positions = engine.GetOpenPositions();
            for (auto* trade : positions) engine.ClosePosition(trade, "STRATEGY_STOP_OUT");
            return true;
        }
        return false;
    }

    void check_new_entries(const Tick& tick, TickBasedEngine& engine) {
        size_t long_count = engine.GetBuyPositionCount();
        size_t short_count = engine.GetSellPositionCount();

        if (long_count < static_cast<size_t>(config_.max_levels_per_side)) {
            double next_long_level = grid_anchor_ - config_.grid_spacing;
            if (lowest_long_ < 1e8) next_long_level = lowest_long_ - config_.grid_spacing;
            if (tick.ask <= next_long_level) {
                double margin_needed = engine.CalculateMarginRequired(config_.lot_size, tick.ask);
                if (margin_needed < engine.GetFreeMargin() * 0.8) {
                    double tp = tick.ask + config_.take_profit;
                    engine.OpenMarketOrder("BUY", config_.lot_size, 0.0, tp);
                    lowest_long_ = std::min(lowest_long_, tick.ask);
                    highest_long_ = std::max(highest_long_, tick.ask);
                    result_.total_trades++; result_.long_trades++;
                }
            }
        }

        if (short_count < static_cast<size_t>(config_.max_levels_per_side)) {
            double next_short_level = grid_anchor_ + config_.grid_spacing;
            if (highest_short_ > 0) next_short_level = highest_short_ + config_.grid_spacing;
            if (tick.bid >= next_short_level) {
                double margin_needed = engine.CalculateMarginRequired(config_.lot_size, tick.bid);
                if (margin_needed < engine.GetFreeMargin() * 0.8) {
                    double tp = tick.bid - config_.take_profit;
                    engine.OpenMarketOrder("SELL", config_.lot_size, 0.0, tp);
                    lowest_short_ = std::min(lowest_short_, tick.bid);
                    highest_short_ = std::max(highest_short_, tick.bid);
                    result_.total_trades++; result_.short_trades++;
                }
            }
        }
    }

    void check_rebalance(const Tick& tick, TickBasedEngine& engine) {
        double long_lots = engine.GetBuyVolume();
        double short_lots = engine.GetSellVolume();
        double total = long_lots + short_lots;
        if (total == 0) return;
        double exposure_ratio = std::abs(long_lots - short_lots) / total;
        if (exposure_ratio > config_.max_exposure_ratio) {
            bool close_longs = long_lots > short_lots;
            for (auto* trade : engine.GetOpenPositions()) {
                if (trade->IsBuy() == close_longs) {
                    engine.ClosePosition(trade, "REBALANCE");
                    break;
                }
            }
        }
    }
};

} // namespace backtest
