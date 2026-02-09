/**
 * @file strategy_carry_grid.h
 * @brief Fully adaptive bidirectional grid — no fixed swap bias assumption.
 *
 * CarryGrid v2: regime-driven directional allocation.
 *
 * Key design changes from v1:
 * - NO fixed long_bias — direction is purely regime-driven
 * - RANGING = 50/50 allocation (swap is a bonus, not a dependency)
 * - TRENDING = shift capital toward trend direction (max_bias/min_bias)
 * - NO strategy rebuild on regime change — SetMaxVolume() preserves state
 * - Counter-trend position closing — actively cuts losing side on regime change
 * - Per-side position cap prevents position explosion
 *
 * Architecture: Internally composes FillUpOscillation (long grid) and
 * FillDownOscillation (short grid). Both coexist on the same TickBasedEngine
 * with zero position conflict (FillUp only manages BUY, FillDown only SELL).
 */

#ifndef STRATEGY_CARRY_GRID_H
#define STRATEGY_CARRY_GRID_H

#include "fill_up_oscillation.h"
#include "fill_down_oscillation.h"
#include "tick_based_engine.h"
#include <deque>
#include <cmath>
#include <algorithm>
#include <vector>

namespace backtest {

class CarryGrid {
public:
    enum Regime {
        RANGING = 0,
        TRENDING_UP = 1,
        TRENDING_DOWN = 2
    };

    struct Config {
        // Grid parameters (shared between long and short)
        double survive_pct = 40.0;
        double base_spacing = 3.0;        // In pct_spacing mode: % of price
        double min_volume = 0.01;
        double max_volume = 1.0;
        double contract_size = 1000.0;
        double leverage = 500.0;
        double volatility_lookback_hours = 4.0;
        double typical_vol_pct = 0.6;

        // Regime detection (Efficiency Ratio)
        int regime_lookback = 500;        // Ticks for ER calculation
        double er_trending = 0.25;        // ER above this = trending
        double er_ranging = 0.10;         // ER below this = ranging

        // Adaptive directional allocation (REPLACES fixed long_bias)
        // RANGING: 50/50 split — swap is a bonus, not a dependency
        // TRENDING_UP: max_bias% long / min_bias% short
        // TRENDING_DOWN: max_bias% short / min_bias% long
        double max_bias = 0.80;           // Max capital to trending direction
        double min_bias = 0.20;           // Min capital to counter-trend direction

        // Counter-trend position management
        double trend_close_fraction = 0.25;  // Close 25% of counter-trend on regime change
        bool close_counter_trend = true;     // Enable counter-trend closing

        // Safety
        double dd_halt_pct = 55.0;        // Halt new entries at this DD
        double equity_stop_pct = 60.0;    // Strategy equity stop
        bool force_min_volume = false;    // Force min lot entries
        int max_positions_per_side = 40;  // Cap per direction

        // Pct spacing mode (true for all oil)
        bool pct_spacing = true;

        // Adaptive spacing bounds (in pct_spacing mode: % of price)
        double min_spacing_abs = 0.5;     // Minimum spacing %
        double max_spacing_abs = 10.0;    // Maximum spacing %

        // Presets — ALL use neutral 50/50 ranging allocation
        static Config NG_C() {
            Config c;
            c.survive_pct = 50.0;
            c.base_spacing = 5.0;
            c.min_volume = 0.10;
            c.max_volume = 0.10;
            c.contract_size = 10000.0;
            c.leverage = 500.0;
            c.volatility_lookback_hours = 2.0;
            c.typical_vol_pct = 2.0;
            c.dd_halt_pct = 50.0;
            c.equity_stop_pct = 60.0;
            c.min_spacing_abs = 1.0;
            c.max_spacing_abs = 15.0;
            c.max_positions_per_side = 20;
            return c;
        }

        static Config CL_OIL() {
            Config c;
            c.survive_pct = 40.0;
            c.base_spacing = 3.0;
            c.min_volume = 0.01;
            c.max_volume = 0.5;
            c.contract_size = 1000.0;
            c.leverage = 500.0;
            c.volatility_lookback_hours = 4.0;
            c.typical_vol_pct = 0.6;
            return c;
        }

        static Config UKOUSD() {
            Config c;
            c.survive_pct = 40.0;
            c.base_spacing = 3.0;
            c.min_volume = 0.01;
            c.max_volume = 0.5;
            c.contract_size = 1000.0;
            c.leverage = 500.0;
            c.volatility_lookback_hours = 4.0;
            c.typical_vol_pct = 0.6;
            return c;
        }

        static Config GASOIL_C() {
            Config c;
            c.survive_pct = 30.0;
            c.base_spacing = 2.0;
            c.min_volume = 0.10;
            c.max_volume = 1.0;
            c.contract_size = 100.0;
            c.leverage = 500.0;
            c.volatility_lookback_hours = 4.0;
            c.typical_vol_pct = 0.6;
            return c;
        }

        static Config GAS_C() {
            Config c;
            c.survive_pct = 40.0;
            c.base_spacing = 3.0;
            c.min_volume = 0.10;
            c.max_volume = 0.10;
            c.contract_size = 42000.0;
            c.leverage = 500.0;
            c.volatility_lookback_hours = 4.0;
            c.typical_vol_pct = 0.5;
            c.dd_halt_pct = 50.0;
            c.equity_stop_pct = 60.0;
            c.max_positions_per_side = 20;
            return c;
        }
    };

    struct Stats {
        // Regime tracking
        long regime_changes = 0;
        long ticks_ranging = 0;
        long ticks_trending_up = 0;
        long ticks_trending_down = 0;

        // Entry tracking
        long long_entries = 0;
        long short_entries = 0;
        long dd_halt_ticks = 0;

        // Position management
        int peak_long_positions = 0;
        int peak_short_positions = 0;
        double peak_dd_pct = 0.0;
        long counter_trend_closes = 0;

        // Effective bias tracking
        double avg_effective_bias = 0.0;
        long bias_samples = 0;
    };

    explicit CarryGrid(const Config& config)
        : config_(config),
          long_strategy_(nullptr),
          short_strategy_(nullptr),
          current_regime_(RANGING),
          prev_regime_(RANGING),
          current_er_(0.0),
          effective_long_bias_(0.5),
          peak_equity_(0.0),
          initialized_(false),
          last_regime_change_engine_(nullptr)
    {
        // Create FillUp (long) config
        FillUpOscillation::Config long_cfg;
        long_cfg.survive_pct = config.survive_pct;
        long_cfg.base_spacing = config.base_spacing;
        long_cfg.min_volume = config.min_volume;
        long_cfg.max_volume = config.max_volume * 0.5;  // Start at 50/50
        long_cfg.contract_size = config.contract_size;
        long_cfg.leverage = config.leverage;
        long_cfg.mode = FillUpOscillation::ADAPTIVE_SPACING;
        long_cfg.volatility_lookback_hours = config.volatility_lookback_hours;
        long_cfg.adaptive.pct_spacing = config.pct_spacing;
        long_cfg.adaptive.typical_vol_pct = config.typical_vol_pct;
        long_cfg.adaptive.min_spacing_abs = config.min_spacing_abs;
        long_cfg.adaptive.max_spacing_abs = config.max_spacing_abs;
        long_cfg.safety.force_min_volume_entry = config.force_min_volume;
        long_cfg.safety.equity_stop_pct = config.equity_stop_pct;
        long_cfg.safety.max_positions = config.max_positions_per_side;

        // Create FillDown (short) config
        FillDownOscillation::Config short_cfg;
        short_cfg.survive_pct = config.survive_pct;
        short_cfg.base_spacing = config.base_spacing;
        short_cfg.min_volume = config.min_volume;
        short_cfg.max_volume = config.max_volume * 0.5;  // Start at 50/50
        short_cfg.contract_size = config.contract_size;
        short_cfg.leverage = config.leverage;
        short_cfg.mode = FillDownOscillation::ADAPTIVE_SPACING;
        short_cfg.volatility_lookback_hours = config.volatility_lookback_hours;
        short_cfg.adaptive.pct_spacing = config.pct_spacing;
        short_cfg.adaptive.typical_vol_pct = config.typical_vol_pct;
        short_cfg.adaptive.min_spacing_abs = config.min_spacing_abs;
        short_cfg.adaptive.max_spacing_abs = config.max_spacing_abs;
        short_cfg.force_min_volume_entry = config.force_min_volume;
        short_cfg.equity_stop_pct = config.equity_stop_pct;
        short_cfg.max_positions = config.max_positions_per_side;

        // Create strategy instances
        long_strategy_ = std::make_unique<FillUpOscillation>(long_cfg);
        short_strategy_ = std::make_unique<FillDownOscillation>(short_cfg);
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        double equity = engine.GetEquity();

        // Initialize
        if (!initialized_) {
            peak_equity_ = equity;
            initialized_ = true;
        }

        // Update peak equity and DD
        if (equity > peak_equity_) {
            peak_equity_ = equity;
        }
        double dd_pct = 0.0;
        if (peak_equity_ > 0.0) {
            dd_pct = (peak_equity_ - equity) / peak_equity_ * 100.0;
        }
        if (dd_pct > stats_.peak_dd_pct) {
            stats_.peak_dd_pct = dd_pct;
        }

        // Update regime detection
        prev_regime_ = current_regime_;
        UpdateRegime(tick);

        // Handle regime transition — close counter-trend positions
        if (current_regime_ != prev_regime_ && config_.close_counter_trend) {
            HandleRegimeTransition(prev_regime_, current_regime_, engine);
        }

        // Update effective bias based on regime (purely price-driven)
        UpdateEffectiveBias();

        // Adjust strategy max volumes based on bias (no rebuild!)
        double long_max = std::max(config_.max_volume * effective_long_bias_, config_.min_volume);
        double short_max = std::max(config_.max_volume * (1.0 - effective_long_bias_), config_.min_volume);
        long_strategy_->SetMaxVolume(long_max);
        short_strategy_->SetMaxVolume(short_max);

        // Track bias
        stats_.avg_effective_bias = (stats_.avg_effective_bias * stats_.bias_samples + effective_long_bias_)
                                     / (stats_.bias_samples + 1);
        stats_.bias_samples++;

        // Track regime ticks
        switch (current_regime_) {
            case RANGING: stats_.ticks_ranging++; break;
            case TRENDING_UP: stats_.ticks_trending_up++; break;
            case TRENDING_DOWN: stats_.ticks_trending_down++; break;
        }

        // DD circuit breaker: halt all new entries
        if (dd_pct >= config_.dd_halt_pct) {
            stats_.dd_halt_ticks++;
            return;
        }

        // Count positions before strategy ticks
        int long_before = 0, short_before = 0;
        for (const Trade* t : engine.GetOpenPositions()) {
            if (t->IsBuy()) long_before++;
            else short_before++;
        }

        // Run both strategies — they only manage their own direction
        long_strategy_->OnTick(tick, engine);
        short_strategy_->OnTick(tick, engine);

        // Count positions after strategy ticks
        int long_after = 0, short_after = 0;
        for (const Trade* t : engine.GetOpenPositions()) {
            if (t->IsBuy()) long_after++;
            else short_after++;
        }

        // Track entries
        if (long_after > long_before) {
            stats_.long_entries += (long_after - long_before);
        }
        if (short_after > short_before) {
            stats_.short_entries += (short_after - short_before);
        }

        // Track peak positions
        if (long_after > stats_.peak_long_positions) {
            stats_.peak_long_positions = long_after;
        }
        if (short_after > stats_.peak_short_positions) {
            stats_.peak_short_positions = short_after;
        }
    }

    // Accessors
    const Stats& GetStats() const { return stats_; }
    Regime GetCurrentRegime() const { return current_regime_; }
    double GetEfficiencyRatio() const { return current_er_; }
    double GetEffectiveBias() const { return effective_long_bias_; }
    double GetPeakDD() const { return stats_.peak_dd_pct; }

    const char* GetRegimeString() const {
        switch (current_regime_) {
            case RANGING: return "RANGING";
            case TRENDING_UP: return "TRENDING_UP";
            case TRENDING_DOWN: return "TRENDING_DOWN";
        }
        return "UNKNOWN";
    }

    // For stress testing: allow creating with fixed bias mode (simulates old behavior)
    void SetFixedBiasMode(double fixed_long_bias) {
        fixed_bias_mode_ = true;
        fixed_bias_value_ = fixed_long_bias;
    }

private:
    Config config_;
    Stats stats_;

    // Internal strategies (created once, never rebuilt)
    std::unique_ptr<FillUpOscillation> long_strategy_;
    std::unique_ptr<FillDownOscillation> short_strategy_;

    // Regime detection
    Regime current_regime_;
    Regime prev_regime_;
    double current_er_;
    std::deque<double> regime_prices_;

    // Capital allocation
    double effective_long_bias_;  // Current effective long allocation (0-1)

    // State
    double peak_equity_;
    bool initialized_;
    TickBasedEngine* last_regime_change_engine_;

    // Fixed bias mode (for stress test comparison)
    bool fixed_bias_mode_ = false;
    double fixed_bias_value_ = 0.5;

    void UpdateRegime(const Tick& tick) {
        regime_prices_.push_back(tick.bid);
        while ((int)regime_prices_.size() > config_.regime_lookback) {
            regime_prices_.pop_front();
        }

        if ((int)regime_prices_.size() < config_.regime_lookback) {
            return;  // Not enough data yet
        }

        // ER = |net change| / sum(|individual changes|)
        double net_change = regime_prices_.back() - regime_prices_.front();
        double sum_abs_changes = 0.0;
        for (size_t i = 1; i < regime_prices_.size(); ++i) {
            sum_abs_changes += std::abs(regime_prices_[i] - regime_prices_[i - 1]);
        }

        if (sum_abs_changes < 1e-10) {
            current_er_ = 0.0;
            return;
        }

        current_er_ = std::abs(net_change) / sum_abs_changes;

        // Determine regime with hysteresis
        Regime new_regime = current_regime_;
        if (current_er_ > config_.er_trending) {
            new_regime = (net_change > 0) ? TRENDING_UP : TRENDING_DOWN;
        } else if (current_er_ < config_.er_ranging) {
            new_regime = RANGING;
        }
        // Between thresholds: keep current regime (hysteresis)

        if (new_regime != current_regime_) {
            current_regime_ = new_regime;
            stats_.regime_changes++;
        }
    }

    void UpdateEffectiveBias() {
        if (fixed_bias_mode_) {
            effective_long_bias_ = fixed_bias_value_;
            return;
        }

        switch (current_regime_) {
            case RANGING:
                effective_long_bias_ = 0.5;  // Neutral — swap is a bonus, not driver
                break;
            case TRENDING_UP:
                effective_long_bias_ = config_.max_bias;  // Heavy long
                break;
            case TRENDING_DOWN:
                effective_long_bias_ = config_.min_bias;  // Heavy short
                break;
        }
    }

    void HandleRegimeTransition(Regime old_regime, Regime new_regime, TickBasedEngine& engine) {
        if (old_regime == new_regime) return;

        if (new_regime == TRENDING_DOWN) {
            // Close fraction of BUY positions (they'll keep losing in downtrend)
            CloseWorstByDirection(engine, true, config_.trend_close_fraction);
        } else if (new_regime == TRENDING_UP) {
            // Close fraction of SELL positions (they'll keep losing in uptrend)
            CloseWorstByDirection(engine, false, config_.trend_close_fraction);
        }
        // Transition to RANGING: don't close anything — let TPs work
    }

    void CloseWorstByDirection(TickBasedEngine& engine, bool close_buys, double fraction) {
        // Collect positions of the target direction with their unrealized P/L
        struct PosInfo {
            Trade* trade;
            double unrealized_pnl;
        };
        std::vector<PosInfo> targets;

        for (Trade* t : engine.GetOpenPositions()) {
            bool is_buy = t->IsBuy();
            if (close_buys && !is_buy) continue;
            if (!close_buys && is_buy) continue;

            // Estimate unrealized P/L (simplified — uses entry price vs current)
            // For BUY: profit if bid > entry, loss if bid < entry
            // For SELL: profit if ask < entry, loss if ask > entry
            // We don't have direct bid/ask here but can use entry_price as reference
            // The engine tracks this internally; we just need relative ordering
            // Use entry_price as a proxy: worst positions are furthest from current
            targets.push_back({t, 0.0});  // P/L doesn't matter for ordering
        }

        if (targets.empty()) return;

        // Close the worst fraction (oldest entries, likely furthest from current price)
        int to_close = std::max(1, (int)(targets.size() * fraction));

        // Close from the beginning (oldest = likely worst in a trend reversal)
        for (int i = 0; i < to_close && i < (int)targets.size(); i++) {
            engine.ClosePosition(targets[i].trade, "CounterTrend");
            stats_.counter_trend_closes++;
        }
    }
};

} // namespace backtest

#endif // STRATEGY_CARRY_GRID_H
