#pragma once
/**
 * Regime-Adaptive Strategy
 *
 * Philosophy: Different market conditions need different approaches
 *
 * Detects and adapts to:
 * 1. TRENDING_UP: Strong upward momentum - be aggressive
 * 2. TRENDING_DOWN: Strong downward momentum - be defensive
 * 3. RANGING: Sideways movement - profit from oscillation
 * 4. HIGH_VOLATILITY: Large swings - reduce size, widen spacing
 *
 * Key insight: No single strategy works in all conditions.
 * Adapt or die.
 *
 * Uses TickBasedEngine for all position/margin/balance management.
 */

#include "tick_based_engine.h"
#include <deque>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace backtest {

enum class MarketRegime {
    TRENDING_UP,
    TRENDING_DOWN,
    RANGING,
    HIGH_VOLATILITY
};

struct RegimeConfig {
    // Base parameters
    double base_lot_size = 0.01;
    double base_spacing = 50.0;
    int max_positions = 20;

    // Regime detection
    int trend_lookback = 1000;
    double trend_threshold = 1.0;
    int volatility_lookback = 500;
    double high_vol_threshold = 2.0;

    // Regime-specific multipliers
    double trending_up_size_mult = 1.5;
    double trending_down_size_mult = 0.5;
    double ranging_size_mult = 1.0;
    double high_vol_size_mult = 0.3;

    double trending_up_spacing_mult = 0.8;
    double trending_down_spacing_mult = 1.5;
    double ranging_spacing_mult = 1.0;
    double high_vol_spacing_mult = 2.0;

    // Profit taking
    double take_profit_pct = 2.0;
    double partial_take_pct = 0.5;

    // Risk management
    double stop_out_level = 50.0;
    double max_equity_risk = 0.3;
};

struct RegimeResult {
    double final_equity = 0.0;
    double max_drawdown_pct = 0.0;
    double max_equity = 0.0;
    int total_entries = 0;
    int total_exits = 0;
    int profit_takes = 0;

    // Regime stats
    int trending_up_entries = 0;
    int trending_down_entries = 0;
    int ranging_entries = 0;
    int high_vol_entries = 0;

    double time_in_trending_up = 0.0;
    double time_in_trending_down = 0.0;
    double time_in_ranging = 0.0;
    double time_in_high_vol = 0.0;

    bool margin_call_occurred = false;
};

class RegimeAdaptiveStrategy {
private:
    RegimeConfig config_;
    std::deque<double> price_history_;
    std::deque<double> returns_history_;

    double last_entry_price_ = 0.0;
    double baseline_volatility_ = 0.0;
    long tick_count_ = 0;

    MarketRegime current_regime_ = MarketRegime::RANGING;
    RegimeResult result_;

public:
    RegimeAdaptiveStrategy() = default;
    explicit RegimeAdaptiveStrategy(const RegimeConfig& cfg) : config_(cfg) {}

    void configure(const RegimeConfig& cfg) { config_ = cfg; }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        double mid = (tick.bid + tick.ask) / 2.0;
        tick_count_++;

        // Update price history and returns
        if (!price_history_.empty()) {
            double ret = (mid - price_history_.back()) / price_history_.back() * 100.0;
            returns_history_.push_back(ret);
            if (returns_history_.size() > static_cast<size_t>(config_.volatility_lookback)) {
                returns_history_.pop_front();
            }
        }

        price_history_.push_back(mid);
        if (price_history_.size() > static_cast<size_t>(config_.trend_lookback + 100)) {
            price_history_.pop_front();
        }

        // Detect regime
        detect_regime();
        update_regime_time();

        // Check margin stop-out
        if (check_margin_stop_out(tick, engine)) return;

        // Check profit taking
        check_profit_taking(tick, engine);

        // Check new entries (regime-adjusted)
        check_new_entries(tick, engine);

        // Update stats
        double equity = engine.GetEquity();
        if (equity > result_.max_equity) result_.max_equity = equity;
    }

    RegimeResult get_result(const TickBasedEngine& engine) {
        result_.final_equity = engine.GetEquity();

        // Normalize regime time to percentages
        double total = result_.time_in_trending_up + result_.time_in_trending_down +
                      result_.time_in_ranging + result_.time_in_high_vol;
        if (total > 0) {
            result_.time_in_trending_up = 100.0 * result_.time_in_trending_up / total;
            result_.time_in_trending_down = 100.0 * result_.time_in_trending_down / total;
            result_.time_in_ranging = 100.0 * result_.time_in_ranging / total;
            result_.time_in_high_vol = 100.0 * result_.time_in_high_vol / total;
        }

        return result_;
    }

    MarketRegime get_current_regime() const { return current_regime_; }

private:
    void detect_regime() {
        if (price_history_.size() < static_cast<size_t>(config_.trend_lookback)) {
            current_regime_ = MarketRegime::RANGING;
            return;
        }

        size_t start_idx = price_history_.size() - config_.trend_lookback;
        double start_price = price_history_[start_idx];
        double end_price = price_history_.back();
        double trend_pct = (end_price - start_price) / start_price * 100.0;

        double current_vol = calculate_volatility();

        if (baseline_volatility_ == 0.0) {
            baseline_volatility_ = current_vol;
        } else {
            baseline_volatility_ = baseline_volatility_ * 0.999 + current_vol * 0.001;
        }

        bool is_high_vol = (baseline_volatility_ > 0 &&
                          current_vol > baseline_volatility_ * config_.high_vol_threshold);

        if (is_high_vol) {
            current_regime_ = MarketRegime::HIGH_VOLATILITY;
        } else if (trend_pct > config_.trend_threshold) {
            current_regime_ = MarketRegime::TRENDING_UP;
        } else if (trend_pct < -config_.trend_threshold) {
            current_regime_ = MarketRegime::TRENDING_DOWN;
        } else {
            current_regime_ = MarketRegime::RANGING;
        }
    }

    double calculate_volatility() {
        if (returns_history_.size() < 10) return 0.0;

        double sum = 0.0, sum_sq = 0.0;
        for (double r : returns_history_) {
            sum += r;
            sum_sq += r * r;
        }

        double n = static_cast<double>(returns_history_.size());
        double mean = sum / n;
        double variance = (sum_sq / n) - (mean * mean);
        return std::sqrt(std::max(0.0, variance));
    }

    void update_regime_time() {
        switch (current_regime_) {
            case MarketRegime::TRENDING_UP:    result_.time_in_trending_up++; break;
            case MarketRegime::TRENDING_DOWN:  result_.time_in_trending_down++; break;
            case MarketRegime::RANGING:        result_.time_in_ranging++; break;
            case MarketRegime::HIGH_VOLATILITY: result_.time_in_high_vol++; break;
        }
    }

    double get_size_multiplier() const {
        switch (current_regime_) {
            case MarketRegime::TRENDING_UP:    return config_.trending_up_size_mult;
            case MarketRegime::TRENDING_DOWN:  return config_.trending_down_size_mult;
            case MarketRegime::RANGING:        return config_.ranging_size_mult;
            case MarketRegime::HIGH_VOLATILITY: return config_.high_vol_size_mult;
        }
        return 1.0;
    }

    double get_spacing_multiplier() const {
        switch (current_regime_) {
            case MarketRegime::TRENDING_UP:    return config_.trending_up_spacing_mult;
            case MarketRegime::TRENDING_DOWN:  return config_.trending_down_spacing_mult;
            case MarketRegime::RANGING:        return config_.ranging_spacing_mult;
            case MarketRegime::HIGH_VOLATILITY: return config_.high_vol_spacing_mult;
        }
        return 1.0;
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

        // Skip entries in downtrend (defensive mode)
        if (current_regime_ == MarketRegime::TRENDING_DOWN && engine.GetBuyPositionCount() > 0) {
            return;
        }

        double effective_spacing = config_.base_spacing * get_spacing_multiplier();

        bool should_enter = false;
        if (last_entry_price_ == 0.0) {
            should_enter = true;
        } else if (tick.ask <= last_entry_price_ - effective_spacing) {
            should_enter = true;
        }

        if (should_enter) {
            double effective_size = config_.base_lot_size * get_size_multiplier();
            effective_size = engine.NormalizeLots(effective_size);

            double margin_needed = engine.CalculateMarginRequired(effective_size, tick.ask);
            if (margin_needed < engine.GetFreeMargin() * (1.0 - config_.max_equity_risk)) {
                engine.OpenMarketOrder("BUY", effective_size);
                last_entry_price_ = tick.ask;
                result_.total_entries++;

                // Track regime-specific entries
                switch (current_regime_) {
                    case MarketRegime::TRENDING_UP:    result_.trending_up_entries++; break;
                    case MarketRegime::TRENDING_DOWN:  result_.trending_down_entries++; break;
                    case MarketRegime::RANGING:        result_.ranging_entries++; break;
                    case MarketRegime::HIGH_VOLATILITY: result_.high_vol_entries++; break;
                }
            }
        }
    }
};

} // namespace backtest
