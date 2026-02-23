#pragma once
/**
 * Anti-Fragile Strategy
 *
 * Philosophy: Get STRONGER from stress, not weaker
 *
 * Mechanism:
 * - Small positions in calm markets
 * - Progressively larger positions as price drops (better prices)
 * - Drawdowns = loading the spring for future profit
 * - Quick profit-taking on bounces to lock in loaded gains
 *
 * Key insight: Crashes are OPPORTUNITIES, not disasters
 * The strategy is designed to profit MORE from volatile markets
 *
 * Uses TickBasedEngine for all position/margin/balance management.
 */

#include "tick_based_engine.h"
#include <cmath>
#include <algorithm>

namespace backtest {

struct AntifragileConfig {
    // Core parameters
    double base_lot_size = 0.01;     // Starting position size
    double max_lot_size = 1.0;       // Maximum position size
    double entry_spacing = 50.0;     // Distance between entries

    // Anti-fragile sizing
    double sizing_exponent = 1.5;    // How much to increase size on dips
    int max_position_count = 20;     // Max concurrent positions

    // Profit taking
    double take_profit_pct = 2.0;    // Take profit on average position
    double partial_take_pct = 0.5;   // Take 50% at first target

    // Risk management
    double max_equity_risk = 0.3;    // Max 30% equity at risk
    double stop_out_level = 50.0;    // Margin level stop out (strategy-level)
};

struct AntifragileResult {
    double final_equity = 0.0;
    double max_drawdown_pct = 0.0;
    double max_equity = 0.0;
    int total_entries = 0;
    int total_exits = 0;
    int profit_takes = 0;
    double max_stress_level = 0.0;
    bool margin_call_occurred = false;
};

class AntifragileStrategy {
private:
    AntifragileConfig config_;
    double all_time_high_ = 0.0;
    double last_entry_price_ = 0.0;
    int current_stress_level_ = 0;
    AntifragileResult result_;

public:
    AntifragileStrategy() = default;
    explicit AntifragileStrategy(const AntifragileConfig& cfg) : config_(cfg) {}

    void configure(const AntifragileConfig& cfg) { config_ = cfg; }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        double mid = (tick.bid + tick.ask) / 2.0;

        // Track all-time high
        if (mid > all_time_high_) {
            all_time_high_ = mid;
            current_stress_level_ = 0;
        } else {
            double drop_pct = (all_time_high_ - mid) / all_time_high_ * 100.0;
            current_stress_level_ = static_cast<int>(drop_pct / 5.0);
        }

        if (current_stress_level_ > result_.max_stress_level) {
            result_.max_stress_level = current_stress_level_;
        }

        if (check_margin_stop_out(tick, engine)) return;
        check_profit_taking(tick, engine);
        check_new_entries(tick, engine);

        double equity = engine.GetEquity();
        if (equity > result_.max_equity) {
            result_.max_equity = equity;
        }
    }

    AntifragileResult get_result(const TickBasedEngine& engine) {
        result_.final_equity = engine.GetEquity();
        return result_;
    }

private:
    bool check_margin_stop_out(const Tick& tick, TickBasedEngine& engine) {
        if (engine.GetOpenPositions().empty()) return false;

        double margin_level = engine.GetMarginLevel();
        if (margin_level > 0 && margin_level < config_.stop_out_level) {
            result_.margin_call_occurred = true;
            auto positions = engine.GetOpenPositions();  // copy since we mutate
            for (auto* trade : positions) {
                engine.ClosePosition(trade, "STRATEGY_STOP_OUT");
                result_.total_exits++;
            }
            return true;
        }
        return false;
    }

    void check_profit_taking(const Tick& tick, TickBasedEngine& engine) {
        if (engine.GetBuyPositionCount() == 0) return;

        double total_cost = 0.0, total_lots = 0.0;
        for (const auto* trade : engine.GetOpenPositions()) {
            if (trade->IsBuy()) {
                total_cost += trade->entry_price * trade->lot_size;
                total_lots += trade->lot_size;
            }
        }

        if (total_lots == 0) return;

        double avg_entry = total_cost / total_lots;
        double profit_pct = (tick.bid - avg_entry) / avg_entry * 100.0;

        if (profit_pct >= config_.take_profit_pct) {
            int to_close = static_cast<int>(engine.GetBuyPositionCount() * config_.partial_take_pct);
            if (to_close < 1) to_close = 1;

            for (int i = 0; i < to_close; i++) {
                Trade* best = nullptr;
                double best_profit = -1e9;
                for (auto* trade : engine.GetOpenPositions()) {
                    if (!trade->IsBuy()) continue;
                    double p = tick.bid - trade->entry_price;
                    if (p > best_profit) { best_profit = p; best = trade; }
                }
                if (best) {
                    engine.ClosePosition(best, "PROFIT_TAKE");
                    result_.profit_takes++;
                    result_.total_exits++;
                }
            }
        }
    }

    void check_new_entries(const Tick& tick, TickBasedEngine& engine) {
        if (engine.GetBuyPositionCount() >= static_cast<size_t>(config_.max_position_count)) return;

        double entry_ref = last_entry_price_ > 0 ? last_entry_price_ : all_time_high_;
        if (entry_ref == 0) { entry_ref = tick.ask; all_time_high_ = tick.ask; }

        bool should_enter = false;
        if (tick.ask >= all_time_high_ && engine.GetBuyPositionCount() == 0)
            should_enter = true;
        else if (tick.ask <= entry_ref - config_.entry_spacing)
            should_enter = true;

        if (should_enter) {
            double stress_multiplier = std::pow(1.0 + current_stress_level_, config_.sizing_exponent);
            double lot_size = std::min(config_.base_lot_size * stress_multiplier, config_.max_lot_size);
            lot_size = engine.NormalizeLots(lot_size);

            double margin_needed = engine.CalculateMarginRequired(lot_size, tick.ask);
            if (margin_needed < engine.GetFreeMargin() * (1.0 - config_.max_equity_risk)) {
                engine.OpenMarketOrder("BUY", lot_size);
                last_entry_price_ = tick.ask;
                result_.total_entries++;
            }
        }
    }
};

} // namespace backtest
