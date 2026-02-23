#pragma once
/**
 * Hybrid Strategy: Anti-Fragile + Crash Detection
 *
 * Philosophy: Load the spring during dips, but escape before crashes
 * Uses TickBasedEngine for all position/margin/balance management.
 */

#include "tick_based_engine.h"
#include <deque>
#include <cmath>
#include <algorithm>

namespace backtest {

struct HybridConfig {
    double base_lot_size = 0.01;
    double max_lot_size = 1.0;
    double entry_spacing = 50.0;
    double sizing_exponent = 1.5;
    int max_positions = 20;
    double take_profit_pct = 2.0;
    double partial_take_pct = 0.5;

    bool enable_crash_detection = true;
    double crash_velocity_threshold = -0.3;
    int crash_lookback = 500;
    double crash_exit_pct = 0.5;
    int cooldown_after_crash = 1000;
    double reentry_bounce_pct = 0.5;

    double max_equity_risk = 0.3;
    double stop_out_level = 50.0;
};

struct HybridResult {
    double final_equity = 0.0;
    double max_drawdown_pct = 0.0;
    double max_equity = 0.0;
    int total_entries = 0;
    int total_exits = 0;
    int profit_takes = 0;
    int crash_exits = 0;
    int reentries = 0;
    double max_stress_level = 0.0;
    bool margin_call_occurred = false;
};

class HybridStrategy {
private:
    HybridConfig config_;
    std::deque<double> price_history_;
    double all_time_high_ = 0.0;
    double last_entry_price_ = 0.0;
    int current_stress_level_ = 0;
    bool in_crash_mode_ = false;
    int crash_cooldown_ = 0;
    double crash_low_ = 0.0;
    HybridResult result_;

public:
    HybridStrategy() = default;
    explicit HybridStrategy(const HybridConfig& cfg) : config_(cfg) {}
    void configure(const HybridConfig& cfg) { config_ = cfg; }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        double mid = (tick.bid + tick.ask) / 2.0;
        price_history_.push_back(mid);
        if (price_history_.size() > static_cast<size_t>(config_.crash_lookback + 100))
            price_history_.pop_front();

        if (mid > all_time_high_) {
            all_time_high_ = mid;
            current_stress_level_ = 0;
        } else {
            double drop_pct = (all_time_high_ - mid) / all_time_high_ * 100.0;
            current_stress_level_ = static_cast<int>(drop_pct / 5.0);
        }
        if (current_stress_level_ > result_.max_stress_level)
            result_.max_stress_level = current_stress_level_;

        update_crash_state(mid);
        if (check_margin_stop_out(tick, engine)) return;
        if (config_.enable_crash_detection && !in_crash_mode_) check_crash_exit(tick, engine);
        check_profit_taking(tick, engine);
        if (!in_crash_mode_ || crash_cooldown_ <= 0) check_new_entries(tick, engine);
        if (crash_cooldown_ > 0) crash_cooldown_--;

        double equity = engine.GetEquity();
        if (equity > result_.max_equity) result_.max_equity = equity;
    }

    HybridResult get_result(const TickBasedEngine& engine) {
        result_.final_equity = engine.GetEquity();
        return result_;
    }

private:
    double calculate_velocity() {
        if (price_history_.size() < static_cast<size_t>(config_.crash_lookback)) return 0.0;
        size_t start_idx = price_history_.size() - config_.crash_lookback;
        double start_price = price_history_[start_idx];
        double end_price = price_history_.back();
        return (end_price - start_price) / start_price * 100.0;
    }

    void update_crash_state(double mid) {
        if (in_crash_mode_) {
            if (mid < crash_low_) crash_low_ = mid;
            double bounce_pct = (mid - crash_low_) / crash_low_ * 100.0;
            if (bounce_pct >= config_.reentry_bounce_pct) {
                in_crash_mode_ = false;
                result_.reentries++;
            }
        }
    }

    bool check_margin_stop_out(const Tick& tick, TickBasedEngine& engine) {
        if (engine.GetOpenPositions().empty()) return false;
        double margin_level = engine.GetMarginLevel();
        if (margin_level > 0 && margin_level < config_.stop_out_level) {
            result_.margin_call_occurred = true;
            auto positions = engine.GetOpenPositions();
            for (auto* trade : positions) {
                engine.ClosePosition(trade, "STRATEGY_STOP_OUT");
                result_.total_exits++;
            }
            return true;
        }
        return false;
    }

    void check_crash_exit(const Tick& tick, TickBasedEngine& engine) {
        double velocity = calculate_velocity();
        if (velocity <= config_.crash_velocity_threshold) {
            in_crash_mode_ = true;
            crash_cooldown_ = config_.cooldown_after_crash;
            crash_low_ = (tick.bid + tick.ask) / 2.0;

            int pos_count = engine.GetBuyPositionCount();
            int to_close = static_cast<int>(pos_count * config_.crash_exit_pct);
            if (to_close < 1 && pos_count > 0) to_close = 1;

            for (int i = 0; i < to_close; i++) {
                Trade* worst = nullptr;
                double worst_pnl = 1e9;
                for (auto* trade : engine.GetOpenPositions()) {
                    if (!trade->IsBuy()) continue;
                    double pnl = tick.bid - trade->entry_price;
                    if (pnl < worst_pnl) { worst_pnl = pnl; worst = trade; }
                }
                if (worst) {
                    engine.ClosePosition(worst, "CRASH_EXIT");
                    result_.crash_exits++;
                    result_.total_exits++;
                }
            }
        }
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
        if (engine.GetBuyPositionCount() >= static_cast<size_t>(config_.max_positions)) return;
        double entry_ref = last_entry_price_ > 0 ? last_entry_price_ : all_time_high_;
        if (entry_ref == 0) { entry_ref = tick.ask; all_time_high_ = tick.ask; }

        bool should_enter = false;
        if (tick.ask >= all_time_high_ && engine.GetBuyPositionCount() == 0) should_enter = true;
        else if (tick.ask <= entry_ref - config_.entry_spacing) should_enter = true;

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
