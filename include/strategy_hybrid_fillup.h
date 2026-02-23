#pragma once
/**
 * Hybrid + Fill-Up Combined Strategy
 *
 * - Hybrid (Anti-Fragile) during uptrends: larger positions on dips
 * - Fill-Up (Grid with TP) during consolidation/crash: profit from oscillations
 *
 * Uses TickBasedEngine for all position/margin/balance management.
 */

#include "tick_based_engine.h"
#include <deque>
#include <cmath>
#include <algorithm>
#include <unordered_set>

namespace backtest {

enum class StrategyMode {
    HYBRID,   // Anti-fragile uptrend mode
    FILLUP    // Grid mode during consolidation
};

struct HybridFillUpConfig {
    // General
    int max_positions = 20;
    double max_equity_risk = 0.3;
    double max_lot_size = 1.0;

    // Hybrid mode (uptrends)
    double hybrid_base_lot = 0.02;
    double hybrid_spacing = 5.0;
    double hybrid_exponent = 1.5;
    double hybrid_tp_pct = 2.0;
    double hybrid_partial_pct = 0.5;

    // Crash detection
    bool enable_crash_detection = true;
    double crash_velocity = -0.4;
    int crash_lookback = 500;
    double crash_exit_pct = 0.5;
    int cooldown_after_crash = 1000;

    // Fill-Up mode (consolidation)
    double fillup_survive_down = 2.5;
    double fillup_spacing = 5.0;
    double fillup_size_mult = 1.0;
    bool fillup_use_tp = true;

    // Mode switching
    int trend_lookback = 2000;
    double uptrend_threshold = 0.5;
    int consolidation_ticks = 5000;

    // Strategy-level stop-out
    double stop_out_level = 50.0;
};

struct HybridFillUpResult {
    double final_equity = 0.0;
    double max_drawdown_pct = 0.0;
    double max_equity = 0.0;
    int hybrid_entries = 0;
    int fillup_entries = 0;
    int crash_exits = 0;
    int profit_takes = 0;
    int tp_hits = 0;
    int mode_changes = 0;
    double time_in_hybrid_pct = 0.0;
    double time_in_fillup_pct = 0.0;
    bool margin_call_occurred = false;
};

class HybridFillUpStrategy {
private:
    HybridFillUpConfig config_;
    std::deque<double> price_history_;

    // Track which engine trade IDs are hybrid vs fillup
    std::unordered_set<int> hybrid_trade_ids_;

    // State
    StrategyMode current_mode_ = StrategyMode::HYBRID;
    double all_time_high_ = 0.0;
    double last_entry_price_ = 0.0;
    int current_stress_level_ = 0;
    int ticks_since_ath_ = 0;

    // Crash state
    bool in_crash_mode_ = false;
    int crash_cooldown_ = 0;
    double crash_low_ = 0.0;

    // FillUp tracking (lowest/highest buy entry for spacing)
    double lowest_buy_ = 1e9;
    double highest_buy_ = 0.0;

    // Stats
    long ticks_in_hybrid_ = 0;
    long ticks_in_fillup_ = 0;
    long total_ticks_ = 0;

    HybridFillUpResult result_;

public:
    HybridFillUpStrategy() = default;
    explicit HybridFillUpStrategy(const HybridFillUpConfig& cfg) : config_(cfg) {}

    void configure(const HybridFillUpConfig& cfg) { config_ = cfg; }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        double mid = (tick.bid + tick.ask) / 2.0;
        total_ticks_++;

        // Update price history
        price_history_.push_back(mid);
        int max_lookback = std::max(config_.crash_lookback, config_.trend_lookback) + 100;
        if (price_history_.size() > static_cast<size_t>(max_lookback)) {
            price_history_.pop_front();
        }

        // Track ATH and stress
        if (mid > all_time_high_ || all_time_high_ == 0.0) {
            all_time_high_ = mid;
            current_stress_level_ = 0;
            ticks_since_ath_ = 0;
        } else {
            double drop_pct = (all_time_high_ - mid) / all_time_high_ * 100.0;
            current_stress_level_ = static_cast<int>(drop_pct / 5.0);
            ticks_since_ath_++;
        }

        // Check margin stop-out
        if (check_margin_stop_out(tick, engine)) return;

        // TP hits for FillUp positions are handled by engine automatically
        // (we pass TP to OpenMarketOrder for fillup entries)
        // But we need to clean up tracking when engine closes them
        clean_stale_ids(engine);

        // Crash detection
        if (config_.enable_crash_detection) {
            update_crash_state(mid);
            if (!in_crash_mode_) {
                check_crash_exit(tick, engine);
            }
            if (crash_cooldown_ > 0) crash_cooldown_--;
        }

        // Determine mode
        determine_mode(mid);

        // Track time in each mode
        if (current_mode_ == StrategyMode::HYBRID) {
            ticks_in_hybrid_++;
        } else {
            ticks_in_fillup_++;
        }

        // Execute based on mode
        if (current_mode_ == StrategyMode::HYBRID) {
            execute_hybrid_mode(tick, engine);
        } else {
            execute_fillup_mode(tick, engine);
        }

        // Update stats
        double equity = engine.GetEquity();
        if (equity > result_.max_equity) result_.max_equity = equity;
    }

    HybridFillUpResult get_result(const TickBasedEngine& engine) {
        result_.final_equity = engine.GetEquity();
        if (total_ticks_ > 0) {
            result_.time_in_hybrid_pct = 100.0 * ticks_in_hybrid_ / total_ticks_;
            result_.time_in_fillup_pct = 100.0 * ticks_in_fillup_ / total_ticks_;
        }
        return result_;
    }

private:
    // Remove trade IDs that no longer exist in engine (closed by TP or other)
    void clean_stale_ids(const TickBasedEngine& engine) {
        const auto& positions = engine.GetOpenPositions();
        std::unordered_set<int> active_ids;
        for (const auto* trade : positions) {
            active_ids.insert(trade->id);
        }
        // Count TP hits: fillup trades that disappeared
        for (auto it = hybrid_trade_ids_.begin(); it != hybrid_trade_ids_.end(); ) {
            if (active_ids.find(*it) == active_ids.end()) {
                it = hybrid_trade_ids_.erase(it);
            } else {
                ++it;
            }
        }
        // Note: fillup TP hits are tracked by engine auto-closing TP
    }

    double calculate_velocity(int lookback) {
        if (price_history_.size() < static_cast<size_t>(lookback)) return 0.0;
        double start_price = price_history_[price_history_.size() - lookback];
        double end_price = price_history_.back();
        if (start_price == 0) return 0.0;
        return (end_price - start_price) / start_price * 100.0;
    }

    void update_crash_state(double mid) {
        if (in_crash_mode_) {
            if (mid < crash_low_) crash_low_ = mid;
            double bounce = (mid - crash_low_) / crash_low_ * 100.0;
            if (bounce >= 0.5) in_crash_mode_ = false;
        }
    }

    void check_crash_exit(const Tick& tick, TickBasedEngine& engine) {
        double velocity = calculate_velocity(config_.crash_lookback);
        if (velocity <= config_.crash_velocity) {
            in_crash_mode_ = true;
            crash_cooldown_ = config_.cooldown_after_crash;
            crash_low_ = tick.bid;

            size_t pos_count = engine.GetBuyPositionCount();
            int to_close = static_cast<int>(pos_count * config_.crash_exit_pct);
            if (to_close < 1 && pos_count > 0) to_close = 1;

            for (int i = 0; i < to_close; i++) {
                // Close worst (most loss) position
                Trade* worst = nullptr;
                double worst_pnl = 1e9;
                for (auto* trade : engine.GetOpenPositions()) {
                    if (!trade->IsBuy()) continue;
                    double pnl = tick.bid - trade->entry_price;
                    if (pnl < worst_pnl) { worst_pnl = pnl; worst = trade; }
                }
                if (worst) {
                    hybrid_trade_ids_.erase(worst->id);
                    engine.ClosePosition(worst, "CRASH_EXIT");
                    result_.crash_exits++;
                }
            }
        }
    }

    void determine_mode(double mid) {
        StrategyMode new_mode = current_mode_;

        if (in_crash_mode_ || crash_cooldown_ > 0) {
            new_mode = StrategyMode::FILLUP;
        } else if (ticks_since_ath_ > config_.consolidation_ticks) {
            new_mode = StrategyMode::FILLUP;
        } else {
            double trend = calculate_velocity(config_.trend_lookback);
            if (trend >= config_.uptrend_threshold) {
                new_mode = StrategyMode::HYBRID;
            } else if (trend < -config_.uptrend_threshold) {
                new_mode = StrategyMode::FILLUP;
            }
        }

        if (new_mode != current_mode_) {
            current_mode_ = new_mode;
            result_.mode_changes++;
        }
    }

    bool check_margin_stop_out(const Tick& tick, TickBasedEngine& engine) {
        if (engine.GetOpenPositions().empty()) return false;
        double margin_level = engine.GetMarginLevel();
        if (margin_level > 0 && margin_level < config_.stop_out_level) {
            result_.margin_call_occurred = true;
            auto positions = engine.GetOpenPositions();
            for (auto* trade : positions) {
                hybrid_trade_ids_.erase(trade->id);
                engine.ClosePosition(trade, "STRATEGY_STOP_OUT");
            }
            return true;
        }
        return false;
    }

    void execute_hybrid_mode(const Tick& tick, TickBasedEngine& engine) {
        // Check profit taking on hybrid positions
        check_hybrid_profit_taking(tick, engine);

        // Check entries
        if (engine.GetBuyPositionCount() >= static_cast<size_t>(config_.max_positions)) return;

        double entry_ref = (last_entry_price_ > 0) ? last_entry_price_ : all_time_high_;
        if (entry_ref == 0.0) { entry_ref = tick.ask; all_time_high_ = tick.ask; }

        bool should_enter = false;
        if (tick.ask >= all_time_high_ && engine.GetBuyPositionCount() == 0)
            should_enter = true;
        else if (tick.ask <= entry_ref - config_.hybrid_spacing)
            should_enter = true;

        if (should_enter) {
            double stress_mult = std::pow(1.0 + current_stress_level_, config_.hybrid_exponent);
            double lot_size = std::min(config_.hybrid_base_lot * stress_mult, config_.max_lot_size);
            lot_size = engine.NormalizeLots(lot_size);

            double margin_needed = engine.CalculateMarginRequired(lot_size, tick.ask);
            if (margin_needed < engine.GetFreeMargin() * (1.0 - config_.max_equity_risk)) {
                Trade* trade = engine.OpenMarketOrder("BUY", lot_size);  // No TP for hybrid
                if (trade) {
                    hybrid_trade_ids_.insert(trade->id);
                    last_entry_price_ = tick.ask;
                    result_.hybrid_entries++;
                }
            }
        }
    }

    void execute_fillup_mode(const Tick& tick, TickBasedEngine& engine) {
        if (engine.GetBuyPositionCount() >= static_cast<size_t>(config_.max_positions)) return;

        double lot_size = calculate_fillup_size(tick, engine);
        if (lot_size <= 0) return;

        // Use engine aggregates for spacing
        double low = engine.GetLowestBuyEntry();
        double high = engine.GetHighestBuyEntry();

        bool should_enter = false;
        if (engine.GetBuyPositionCount() == 0) {
            should_enter = true;
        } else if (low > 0 && tick.ask <= low - config_.fillup_spacing) {
            should_enter = true;
        } else if (high > 0 && tick.ask >= high + config_.fillup_spacing) {
            should_enter = true;
        }

        if (should_enter) {
            double tp = 0.0;
            if (config_.fillup_use_tp) {
                tp = tick.ask + config_.fillup_spacing + (tick.ask - tick.bid);  // spacing + spread
            }

            double margin_needed = engine.CalculateMarginRequired(lot_size, tick.ask);
            if (margin_needed < engine.GetFreeMargin() * (1.0 - config_.max_equity_risk)) {
                Trade* trade = engine.OpenMarketOrder("BUY", lot_size, 0.0, tp);
                if (trade) {
                    // NOT in hybrid_trade_ids_ -> it's a fillup trade
                    result_.fillup_entries++;
                }
            }
        }
    }

    double calculate_fillup_size(const Tick& tick, TickBasedEngine& engine) {
        double equity = engine.GetEquity();
        double high = engine.GetHighestBuyEntry();
        double ref_price = (high > 0) ? high : tick.ask;
        double end_price = ref_price * (100.0 - config_.fillup_survive_down) / 100.0;
        double distance = tick.ask - end_price;
        double num_trades = std::floor(distance / config_.fillup_spacing);
        if (num_trades < 1) num_trades = 1;

        double available = equity * (1.0 - config_.max_equity_risk);
        double per_trade = available / (num_trades * 2.0);

        double cs = engine.GetConfig().contract_size;
        double lev = engine.GetConfig().leverage;
        double lot_size = (per_trade * lev) / (cs * tick.ask);
        lot_size *= config_.fillup_size_mult;
        lot_size = engine.NormalizeLots(lot_size);
        return lot_size;
    }

    void check_hybrid_profit_taking(const Tick& tick, TickBasedEngine& engine) {
        // Calculate avg entry of hybrid positions only
        double total_cost = 0.0, total_lots = 0.0;
        int hybrid_count = 0;
        for (const auto* trade : engine.GetOpenPositions()) {
            if (!trade->IsBuy()) continue;
            if (hybrid_trade_ids_.count(trade->id)) {
                total_cost += trade->entry_price * trade->lot_size;
                total_lots += trade->lot_size;
                hybrid_count++;
            }
        }
        if (total_lots == 0) return;

        double avg_entry = total_cost / total_lots;
        double profit_pct = (tick.bid - avg_entry) / avg_entry * 100.0;

        if (profit_pct >= config_.hybrid_tp_pct) {
            int to_close = static_cast<int>(hybrid_count * config_.hybrid_partial_pct);
            if (to_close < 1) to_close = 1;

            for (int i = 0; i < to_close; i++) {
                Trade* best = nullptr;
                double best_profit = -1e9;
                for (auto* trade : engine.GetOpenPositions()) {
                    if (!trade->IsBuy()) continue;
                    if (!hybrid_trade_ids_.count(trade->id)) continue;
                    double p = tick.bid - trade->entry_price;
                    if (p > best_profit) { best_profit = p; best = trade; }
                }
                if (best) {
                    hybrid_trade_ids_.erase(best->id);
                    engine.ClosePosition(best, "HYBRID_PROFIT_TAKE");
                    result_.profit_takes++;
                }
            }
        }
    }
};

} // namespace backtest
