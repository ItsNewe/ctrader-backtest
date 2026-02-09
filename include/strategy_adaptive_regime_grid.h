#ifndef STRATEGY_ADAPTIVE_REGIME_GRID_H
#define STRATEGY_ADAPTIVE_REGIME_GRID_H

#include "tick_based_engine.h"
#include <cfloat>
#include <cmath>
#include <algorithm>
#include <deque>
#include <vector>

namespace backtest {

/**
 * ADAPTIVE REGIME GRID STRATEGY
 *
 * Combines proven mechanics from FillUpOscillation and CombinedJu with:
 * 1. Regime Detection (Efficiency Ratio) - trend vs range awareness
 * 2. Drawdown Circuit Breaker - caps max DD with graduated response
 * 3. Smart TP Management - LINEAR TP in ranges, trailing stops in trends
 * 4. Multi-Window Velocity Filter - consensus calm across 3 timeframes
 * 5. Asymmetric Spacing - tighter with trend, wider against trend
 *
 * Target: >10x return with <55% max drawdown (better risk-adjusted than
 * FillUpOscillation's 6.57x/67%DD or CombinedJu's 22.11x/83%DD).
 */
class AdaptiveRegimeGrid {
public:
    enum Regime {
        RANGING = 0,
        TRENDING_UP = 1,
        TRENDING_DOWN = 2
    };

    struct Config {
        // Core grid parameters
        double survive_pct = 13.0;
        double base_spacing = 1.50;
        double min_volume = 0.01;
        double max_volume = 10.0;
        double contract_size = 100.0;
        double leverage = 500.0;
        double volatility_lookback_hours = 4.0;
        double typical_vol_pct = 0.55;

        // Regime detection
        int regime_lookback_ticks = 500;
        double er_trending_threshold = 0.25;    // Lowered: gold trends are subtle at tick level
        double er_ranging_threshold = 0.10;     // Lowered: hysteresis band below trending

        // Drawdown circuit breaker
        double dd_reduce_threshold = 40.0;      // Raised: 30% triggered too often
        double dd_halt_threshold = 55.0;        // Raised: allow more room before halting
        double dd_liquidate_threshold = 65.0;   // Emergency liquidation

        // TP management
        double tp_linear_scale = 0.3;
        double tp_min = 1.50;
        double trailing_distance = 2.0;
        double trailing_activation = 1.5;       // Raised: need solid profit before trailing
        bool use_trailing_in_trends = true;

        // Multi-window velocity filter
        bool enable_velocity_filter = true;
        int velocity_fast_window = 5;
        int velocity_mid_window = 20;
        int velocity_slow_window = 100;
        double velocity_threshold_pct = 0.02;   // Relaxed: 0.015 was blocking too many entries

        // Asymmetric spacing
        bool enable_asymmetric = true;
        double trend_spacing_tighten = 0.7;
        double trend_spacing_widen = 1.5;

        // Percentage spacing mode (for XAGUSD)
        bool pct_spacing = false;

        // Safety
        bool force_min_volume_entry = false;

        static Config XAUUSD_Default() {
            Config c;
            // All defaults are tuned for XAUUSD
            return c;
        }

        static Config XAUUSD_Conservative() {
            Config c;
            c.survive_pct = 16.0;
            c.base_spacing = 2.0;
            c.dd_reduce_threshold = 35.0;
            c.dd_halt_threshold = 45.0;
            c.dd_liquidate_threshold = 55.0;
            return c;
        }

        static Config XAGUSD_Default() {
            Config c;
            c.survive_pct = 19.0;
            c.base_spacing = 2.0;
            c.contract_size = 5000.0;
            c.volatility_lookback_hours = 1.0;
            c.typical_vol_pct = 0.45;
            c.pct_spacing = true;
            c.tp_min = 2.0;
            return c;
        }
    };

    struct Stats {
        long velocity_blocks = 0;
        long entries_allowed = 0;
        long dd_reduce_active_ticks = 0;
        long dd_halt_ticks = 0;
        long dd_liquidations = 0;
        long positions_liquidated = 0;
        long regime_changes = 0;
        long trending_entries = 0;
        long ranging_entries = 0;
        long trailing_tp_entries = 0;
        long fixed_tp_entries = 0;
        int max_position_count = 0;
        long lot_size_zero_blocks = 0;
        double peak_dd_pct = 0.0;
    };

    explicit AdaptiveRegimeGrid(const Config& config)
        : config_(config),
          current_ask_(0.0),
          current_bid_(0.0),
          current_spread_(0.0),
          current_equity_(0.0),
          current_balance_(0.0),
          peak_equity_(0.0),
          current_spacing_(config.base_spacing),
          lowest_buy_(DBL_MAX),
          highest_buy_(DBL_MIN),
          volume_of_open_trades_(0.0),
          position_count_(0),
          first_entry_price_(0.0),
          // Volatility
          recent_high_(0.0),
          recent_low_(DBL_MAX),
          last_vol_reset_seconds_(0),
          // Regime
          current_regime_(RANGING),
          current_er_(0.0),
          // Circuit breaker
          liquidation_triggered_(false),
          // Velocity
          velocity_fast_(0.0),
          velocity_mid_(0.0),
          velocity_slow_(0.0)
    {
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        // 1. Update market state
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_spread_ = tick.spread();
        current_equity_ = engine.GetEquity();
        current_balance_ = engine.GetBalance();

        // Initialize peak equity
        if (peak_equity_ == 0.0) {
            peak_equity_ = current_equity_;
            if (config_.pct_spacing) {
                current_spacing_ = current_bid_ * (config_.base_spacing / 100.0);
            }
        }

        // 2. Update peak equity and calculate DD%
        if (current_equity_ > peak_equity_) {
            peak_equity_ = current_equity_;
        }
        double dd_pct = 0.0;
        if (peak_equity_ > 0.0) {
            dd_pct = (peak_equity_ - current_equity_) / peak_equity_ * 100.0;
        }
        if (dd_pct > stats_.peak_dd_pct) {
            stats_.peak_dd_pct = dd_pct;
        }

        // 3. Circuit breaker: liquidate worst positions at extreme DD
        if (dd_pct >= config_.dd_liquidate_threshold && !liquidation_triggered_) {
            LiquidateWorstPositions(engine, 0.25);
            liquidation_triggered_ = true;
        }
        // Reset liquidation flag when DD recovers
        if (dd_pct < config_.dd_halt_threshold) {
            liquidation_triggered_ = false;
        }

        // 4. Circuit breaker: halt new entries at high DD
        if (dd_pct >= config_.dd_halt_threshold) {
            stats_.dd_halt_ticks++;
            // Still need to iterate positions for tracking
            Iterate(engine);
            return;
        }

        // Track reduced sizing ticks
        if (dd_pct >= config_.dd_reduce_threshold) {
            stats_.dd_reduce_active_ticks++;
        }

        // 5. Update volatility
        UpdateVolatility(tick);

        // 6. Update adaptive spacing
        UpdateAdaptiveSpacing();

        // 7. Update regime detection
        UpdateRegime();

        // 8. Apply asymmetric spacing multiplier
        double effective_spacing = current_spacing_;
        if (config_.enable_asymmetric) {
            if (current_regime_ == TRENDING_UP) {
                effective_spacing *= config_.trend_spacing_tighten;
            } else if (current_regime_ == TRENDING_DOWN) {
                effective_spacing *= config_.trend_spacing_widen;
            }
        }

        // 9. Update velocity windows
        UpdateVelocity();

        // 10. Iterate positions
        Iterate(engine);

        // 11-12. Open new positions
        OpenNew(engine, effective_spacing, dd_pct);
    }

    // Accessors
    const Stats& GetStats() const { return stats_; }
    double GetCurrentSpacing() const { return current_spacing_; }
    double GetEfficiencyRatio() const { return current_er_; }
    Regime GetCurrentRegime() const { return current_regime_; }
    double GetPeakEquity() const { return peak_equity_; }
    double GetFirstEntryPrice() const { return first_entry_price_; }

    const char* GetRegimeString() const {
        switch (current_regime_) {
            case RANGING: return "RANGING";
            case TRENDING_UP: return "TRENDING_UP";
            case TRENDING_DOWN: return "TRENDING_DOWN";
        }
        return "UNKNOWN";
    }

private:
    Config config_;

    // Market state
    double current_ask_;
    double current_bid_;
    double current_spread_;
    double current_equity_;
    double current_balance_;
    double peak_equity_;
    double current_spacing_;
    double lowest_buy_;
    double highest_buy_;
    double volume_of_open_trades_;
    int position_count_;
    double first_entry_price_;

    // Volatility tracking
    double recent_high_;
    double recent_low_;
    long last_vol_reset_seconds_;

    // Regime detection
    Regime current_regime_;
    double current_er_;
    std::deque<double> regime_prices_;

    // Circuit breaker
    bool liquidation_triggered_;

    // Multi-window velocity
    std::deque<double> vel_fast_window_;
    std::deque<double> vel_mid_window_;
    std::deque<double> vel_slow_window_;
    double velocity_fast_;
    double velocity_mid_;
    double velocity_slow_;

    Stats stats_;

    // --- Reused from FillUpOscillation (optimized zero-alloc parser) ---
    static long ParseTimestampToSeconds(const std::string& ts) {
        if (ts.size() < 19) return 0;
        int year = (ts[0] - '0') * 1000 + (ts[1] - '0') * 100 + (ts[2] - '0') * 10 + (ts[3] - '0');
        int month = (ts[5] - '0') * 10 + (ts[6] - '0');
        int day = (ts[8] - '0') * 10 + (ts[9] - '0');
        int hour = (ts[11] - '0') * 10 + (ts[12] - '0');
        int minute = (ts[14] - '0') * 10 + (ts[15] - '0');
        int second = (ts[17] - '0') * 10 + (ts[18] - '0');
        static const int month_days[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
        long days = (long)(year - 2020) * 365 + (year - 2020) / 4;
        days += month_days[month - 1] + day;
        if (month > 2 && year % 4 == 0) days++;
        return days * 86400L + hour * 3600L + minute * 60L + second;
    }

    // --- Regime Detection via Efficiency Ratio ---
    void UpdateRegime() {
        regime_prices_.push_back(current_bid_);
        while ((int)regime_prices_.size() > config_.regime_lookback_ticks) {
            regime_prices_.pop_front();
        }

        if ((int)regime_prices_.size() < config_.regime_lookback_ticks) {
            return; // Not enough data yet
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
        if (current_er_ > config_.er_trending_threshold) {
            new_regime = (net_change > 0) ? TRENDING_UP : TRENDING_DOWN;
        } else if (current_er_ < config_.er_ranging_threshold) {
            new_regime = RANGING;
        }
        // Between thresholds: keep current regime (hysteresis)

        if (new_regime != current_regime_) {
            current_regime_ = new_regime;
            stats_.regime_changes++;
        }
    }

    // --- Multi-Window Velocity Filter ---
    void UpdateVelocity() {
        vel_fast_window_.push_back(current_bid_);
        vel_mid_window_.push_back(current_bid_);
        vel_slow_window_.push_back(current_bid_);

        while ((int)vel_fast_window_.size() > config_.velocity_fast_window)
            vel_fast_window_.pop_front();
        while ((int)vel_mid_window_.size() > config_.velocity_mid_window)
            vel_mid_window_.pop_front();
        while ((int)vel_slow_window_.size() > config_.velocity_slow_window)
            vel_slow_window_.pop_front();

        auto calc_vel = [](const std::deque<double>& window) -> double {
            if (window.size() < 2) return 0.0;
            return (window.back() - window.front()) / window.front() * 100.0;
        };

        velocity_fast_ = calc_vel(vel_fast_window_);
        velocity_mid_ = calc_vel(vel_mid_window_);
        velocity_slow_ = calc_vel(vel_slow_window_);
    }

    bool CheckVelocityFilter() const {
        if (!config_.enable_velocity_filter) return true;
        // Block if ANY window shows excessive momentum
        return std::abs(velocity_fast_) < config_.velocity_threshold_pct &&
               std::abs(velocity_mid_) < config_.velocity_threshold_pct &&
               std::abs(velocity_slow_) < config_.velocity_threshold_pct;
    }

    // --- Volatility Tracking ---
    void UpdateVolatility(const Tick& tick) {
        long current_seconds = ParseTimestampToSeconds(tick.timestamp);
        long lookback_seconds = (long)(config_.volatility_lookback_hours * 3600.0);

        if (last_vol_reset_seconds_ == 0 ||
            current_seconds - last_vol_reset_seconds_ >= lookback_seconds) {
            recent_high_ = current_bid_;
            recent_low_ = current_bid_;
            last_vol_reset_seconds_ = current_seconds;
        }
        recent_high_ = std::max(recent_high_, current_bid_);
        recent_low_ = std::min(recent_low_, current_bid_);
    }

    // --- Adaptive Spacing (reused from existing strategies) ---
    void UpdateAdaptiveSpacing() {
        double range = recent_high_ - recent_low_;
        if (range > 0 && recent_high_ > 0 && current_bid_ > 0) {
            double typical_vol = current_bid_ * (config_.typical_vol_pct / 100.0);
            double vol_ratio = range / typical_vol;
            vol_ratio = std::max(0.5, std::min(3.0, vol_ratio));

            double base_spacing_abs;
            if (config_.pct_spacing) {
                base_spacing_abs = current_bid_ * (config_.base_spacing / 100.0);
            } else {
                base_spacing_abs = config_.base_spacing;
            }

            double new_spacing = base_spacing_abs * vol_ratio;

            double min_spacing = config_.pct_spacing ? current_bid_ * 0.001 : 0.5;
            double max_spacing = config_.pct_spacing ? current_bid_ * 0.10 : 5.0;
            new_spacing = std::max(min_spacing, std::min(max_spacing, new_spacing));

            double threshold = config_.pct_spacing ? current_bid_ * 0.001 : 0.1;
            if (std::abs(new_spacing - current_spacing_) > threshold) {
                current_spacing_ = new_spacing;
            }
        }
    }

    // --- Circuit Breaker: Close worst 25% of positions ---
    void LiquidateWorstPositions(TickBasedEngine& engine, double fraction) {
        const auto& positions = engine.GetOpenPositions();
        if (positions.empty()) return;

        // Collect positions with unrealized P/L
        struct PosInfo {
            Trade* trade;
            double unrealized_pl;
        };
        std::vector<PosInfo> pos_list;
        pos_list.reserve(positions.size());

        for (Trade* trade : positions) {
            double exit_price = trade->IsBuy() ? current_bid_ : current_ask_;
            double pl = (exit_price - trade->entry_price) * trade->lot_size *
                        config_.contract_size * (trade->IsBuy() ? 1.0 : -1.0);
            pos_list.push_back({trade, pl});
        }

        // Sort by unrealized P/L ascending (worst first)
        std::sort(pos_list.begin(), pos_list.end(),
                  [](const PosInfo& a, const PosInfo& b) {
                      return a.unrealized_pl < b.unrealized_pl;
                  });

        // Close worst fraction
        int to_close = std::max(1, (int)(pos_list.size() * fraction));
        for (int i = 0; i < to_close && i < (int)pos_list.size(); ++i) {
            engine.ClosePosition(pos_list[i].trade, "CircuitBreaker");
            stats_.positions_liquidated++;
        }
        stats_.dd_liquidations++;
    }

    // --- LINEAR TP Calculation (from CombinedJu) ---
    double CalculateTP() const {
        if (first_entry_price_ <= 0) {
            return current_ask_ + current_spread_ + current_spacing_;
        }

        double deviation = std::abs(first_entry_price_ - current_ask_);

        double tp_min_abs = config_.pct_spacing
            ? current_ask_ * (config_.tp_min / 100.0)
            : config_.tp_min;

        double tp_addition = config_.tp_linear_scale * deviation;
        return current_ask_ + current_spread_ + std::max(tp_min_abs, tp_addition);
    }

    // --- Position Iteration ---
    void Iterate(TickBasedEngine& engine) {
        lowest_buy_ = DBL_MAX;
        highest_buy_ = DBL_MIN;
        volume_of_open_trades_ = 0.0;
        position_count_ = 0;

        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->IsBuy()) {
                volume_of_open_trades_ += trade->lot_size;
                lowest_buy_ = std::min(lowest_buy_, trade->entry_price);
                highest_buy_ = std::max(highest_buy_, trade->entry_price);
                position_count_++;
            }
        }

        if (position_count_ == 0) {
            first_entry_price_ = 0.0;
        }

        if (position_count_ > stats_.max_position_count) {
            stats_.max_position_count = position_count_;
        }
    }

    // --- Binary Search Lot Sizing (from FillUpOscillation) ---
    double CalculateLotSize(TickBasedEngine& engine, int positions_total, double dd_pct) {
        double used_margin = 0.0;
        for (const Trade* trade : engine.GetOpenPositions()) {
            used_margin += trade->lot_size * config_.contract_size *
                          trade->entry_price / config_.leverage;
        }

        double margin_stop_out = 20.0;

        double end_price = (positions_total == 0)
            ? current_ask_ * ((100.0 - config_.survive_pct) / 100.0)
            : highest_buy_ * ((100.0 - config_.survive_pct) / 100.0);

        double distance = current_ask_ - end_price;
        double number_of_trades = std::floor(distance / current_spacing_);
        if (number_of_trades <= 0) number_of_trades = 1;

        double equity_at_target = current_equity_ -
                                 volume_of_open_trades_ * distance * config_.contract_size;
        if (used_margin > 0 && (equity_at_target / used_margin * 100.0) < margin_stop_out) {
            return 0.0;
        }

        double trade_size = config_.min_volume;
        double d_equity = config_.contract_size * trade_size * current_spacing_ *
                         (number_of_trades * (number_of_trades + 1) / 2);
        double d_margin = number_of_trades * trade_size * config_.contract_size / config_.leverage;

        // Binary search for largest valid multiplier
        double max_mult = config_.max_volume / config_.min_volume;
        double low = 1.0, high = max_mult;
        double best_mult = 1.0;

        while (high - low > 0.05) {
            double mid = (low + high) / 2.0;
            double test_equity = equity_at_target - mid * d_equity;
            double test_margin = used_margin + mid * d_margin;
            if (test_margin > 0 && (test_equity / test_margin * 100.0) > margin_stop_out) {
                best_mult = mid;
                low = mid;
            } else {
                high = mid;
            }
        }
        trade_size = best_mult * config_.min_volume;

        // Apply DD reduction: at dd_reduce_threshold, cut sizing by 50%
        if (dd_pct >= config_.dd_reduce_threshold) {
            double dd_factor = 0.5;
            trade_size *= dd_factor;
        }

        return std::min(trade_size, config_.max_volume);
    }

    // --- Open a position ---
    bool Open(double lots, double tp, TickBasedEngine& engine, bool use_trailing) {
        if (lots < config_.min_volume) {
            if (config_.force_min_volume_entry) {
                lots = config_.min_volume;
            } else {
                stats_.lot_size_zero_blocks++;
                return false;
            }
        }

        double final_lots = std::min(lots, config_.max_volume);
        final_lots = std::round(final_lots * 100.0) / 100.0;
        if (final_lots < config_.min_volume) return false;

        Trade* trade = nullptr;
        if (use_trailing && config_.use_trailing_in_trends) {
            // Trailing stop mode for trending markets
            trade = engine.OpenMarketOrderWithTrailing(
                TradeDirection::BUY, final_lots,
                config_.trailing_distance,
                config_.trailing_activation,
                0.0  // No fixed TP
            );
            if (trade) stats_.trailing_tp_entries++;
        } else {
            // Fixed TP mode for ranging markets
            trade = engine.OpenMarketOrder(TradeDirection::BUY, final_lots, 0.0, tp);
            if (trade) stats_.fixed_tp_entries++;
        }

        if (trade != nullptr) {
            stats_.entries_allowed++;
            return true;
        }
        return false;
    }

    // --- Main entry logic ---
    void OpenNew(TickBasedEngine& engine, double effective_spacing, double dd_pct) {
        int positions_total = (int)engine.GetOpenPositions().size();

        if (positions_total == 0) {
            // First position: set equilibrium, no velocity filter
            double lots = CalculateLotSize(engine, positions_total, dd_pct);
            double tp = current_ask_ + current_spread_ + current_spacing_;
            if (Open(lots, tp, engine, false)) {
                first_entry_price_ = current_ask_;
                highest_buy_ = current_ask_;
                lowest_buy_ = current_ask_;
                stats_.ranging_entries++;
            }
        } else {
            // Additional positions: check spacing condition
            if (lowest_buy_ >= current_ask_ + effective_spacing) {
                // Check multi-window velocity filter
                if (!CheckVelocityFilter()) {
                    stats_.velocity_blocks++;
                    return;
                }

                // Calculate lot size with DD reduction
                double lots = CalculateLotSize(engine, positions_total, dd_pct);

                // Determine if we use trailing or fixed TP
                bool use_trailing = (current_regime_ == TRENDING_UP);

                // Calculate fixed TP (used if not trailing)
                double tp = CalculateTP();

                if (Open(lots, tp, engine, use_trailing)) {
                    lowest_buy_ = current_ask_;
                    if (current_regime_ == RANGING) {
                        stats_.ranging_entries++;
                    } else {
                        stats_.trending_entries++;
                    }
                }
            } else if (highest_buy_ <= current_ask_ - effective_spacing) {
                // Price moved up past highest grid level
                if (!CheckVelocityFilter()) {
                    stats_.velocity_blocks++;
                    return;
                }

                double lots = CalculateLotSize(engine, positions_total, dd_pct);
                bool use_trailing = (current_regime_ == TRENDING_UP);
                double tp = CalculateTP();

                if (Open(lots, tp, engine, use_trailing)) {
                    highest_buy_ = current_ask_;
                    if (current_regime_ == RANGING) {
                        stats_.ranging_entries++;
                    } else {
                        stats_.trending_entries++;
                    }
                }
            }
        }
    }
};

} // namespace backtest

#endif // STRATEGY_ADAPTIVE_REGIME_GRID_H
