#ifndef STRATEGY_REGIME_SWITCHING_H
#define STRATEGY_REGIME_SWITCHING_H

#include "tick_based_engine.h"
#include "fill_up_oscillation.h"
#include <vector>
#include <deque>
#include <cmath>
#include <algorithm>
#include <cfloat>

namespace backtest {

/**
 * Regime-Switching Strategy (Hidden Markov Model inspired)
 *
 * Markets switch between hidden states (regimes). This strategy detects regimes
 * and adapts parameters accordingly:
 *
 * REGIME DETECTION:
 * - direction_ratio = |sum(returns)| / sum(|returns|)
 *   - 0 = pure oscillation (returns cancel out)
 *   - 1 = pure trend (all returns in same direction)
 * - volatility_ratio = recent_vol / typical_vol
 *   - High = unusual volatility expansion
 *   - Low = quiet/compressed volatility
 * - ma_cross_rate = price crosses over MA / lookback_ticks
 *   - High = oscillating around mean
 *   - Low = trending away from mean
 *
 * REGIMES:
 * 1. OSCILLATING: direction_ratio < oscillating_threshold
 *    - Strategy: Normal trading, tight spacing (profit zone)
 * 2. TRENDING: direction_ratio > trending_threshold
 *    - Strategy: Widen spacing or pause (avoid fighting trend)
 * 3. HIGH_VOL: volatility_ratio > highvol_threshold
 *    - Strategy: Reduce position size (protect capital)
 *
 * Uses FillUpOscillation ADAPTIVE_SPACING as base strategy.
 */
class StrategyRegimeSwitching {
public:
    // Regime types
    enum Regime {
        OSCILLATING = 0,
        TRENDING = 1,
        HIGH_VOLATILITY = 2
    };

    // What to do during trending regime
    enum TrendingAction {
        TREND_WIDEN_2X = 0,     // Double spacing
        TREND_WIDEN_3X = 1,     // Triple spacing
        TREND_PAUSE = 2         // Pause all entries
    };

    // What to do during high volatility regime
    enum HighVolAction {
        HIGHVOL_REDUCE_50 = 0,  // Reduce lot size by 50%
        HIGHVOL_REDUCE_75 = 1,  // Reduce lot size by 75%
        HIGHVOL_NORMAL = 2      // Keep normal size
    };

    // Configuration struct for sweep
    struct Config {
        int lookback_ticks;             // Ticks to track for regime detection
        double oscillating_threshold;   // direction_ratio below this = oscillating
        double trending_threshold;      // direction_ratio above this = trending
        TrendingAction trending_action; // What to do during trends
        double highvol_threshold;       // volatility_ratio above this = high vol
        HighVolAction highvol_action;   // What to do during high vol
        double survive_pct;             // Base survive percentage
        double base_spacing;            // Base grid spacing
        double volatility_lookback_hours; // For adaptive spacing
        double typical_vol_pct;         // Expected volatility %

        Config()
            : lookback_ticks(1000),
              oscillating_threshold(0.3),
              trending_threshold(0.7),
              trending_action(TREND_WIDEN_2X),
              highvol_threshold(2.0),
              highvol_action(HIGHVOL_REDUCE_50),
              survive_pct(13.0),
              base_spacing(1.5),
              volatility_lookback_hours(4.0),
              typical_vol_pct(0.55) {}
    };

    StrategyRegimeSwitching(const Config& cfg)
        : cfg_(cfg),
          lowest_buy_(DBL_MAX),
          highest_buy_(DBL_MIN),
          volume_of_open_trades_(0.0),
          current_ask_(0.0),
          current_bid_(0.0),
          current_spread_(0.0),
          current_equity_(0.0),
          current_balance_(0.0),
          peak_equity_(0.0),
          current_spacing_(cfg.base_spacing),
          last_bid_(0.0),
          ma_sum_(0.0),
          ma_cross_count_(0),
          current_regime_(OSCILLATING),
          regime_changes_(0),
          oscillating_ticks_(0),
          trending_ticks_(0),
          highvol_ticks_(0),
          ticks_processed_(0),
          lot_multiplier_(1.0),
          recent_high_(0.0),
          recent_low_(DBL_MAX),
          last_vol_reset_seconds_(0)
    {
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_spread_ = tick.spread();
        current_equity_ = engine.GetEquity();
        current_balance_ = engine.GetBalance();
        ticks_processed_++;

        // Initialize
        if (peak_equity_ == 0.0) {
            peak_equity_ = current_equity_;
            last_bid_ = current_bid_;
            recent_high_ = current_bid_;
            recent_low_ = current_bid_;
        }

        // Update peak equity
        if (current_equity_ > peak_equity_) {
            peak_equity_ = current_equity_;
        }

        // Track price returns for regime detection
        if (last_bid_ > 0) {
            double ret = current_bid_ - last_bid_;
            returns_.push_back(ret);
            if (returns_.size() > (size_t)cfg_.lookback_ticks) {
                returns_.pop_front();
            }

            // Track MA crossings
            UpdateMACrossings();
        }

        // Track volatility for adaptive spacing
        UpdateVolatility(tick);

        // Detect regime
        DetectRegime();

        // Apply regime-specific parameters
        ApplyRegimeParameters();

        last_bid_ = current_bid_;

        // Check if we should pause during trending regime
        if (current_regime_ == TRENDING && cfg_.trending_action == TREND_PAUSE) {
            // Just process existing positions (TPs will still trigger)
            Iterate(engine);
            return;
        }

        // Process positions
        Iterate(engine);

        // Open new positions
        OpenNew(engine);
    }

    // Statistics getters
    Regime GetCurrentRegime() const { return current_regime_; }
    int GetRegimeChanges() const { return regime_changes_; }
    long GetOscillatingTicks() const { return oscillating_ticks_; }
    long GetTrendingTicks() const { return trending_ticks_; }
    long GetHighVolTicks() const { return highvol_ticks_; }
    double GetDirectionRatio() const { return last_direction_ratio_; }
    double GetVolatilityRatio() const { return last_vol_ratio_; }
    double GetCurrentSpacing() const { return current_spacing_; }
    double GetLotMultiplier() const { return lot_multiplier_; }
    double GetPeakEquity() const { return peak_equity_; }

private:
    Config cfg_;

    // Position tracking
    double lowest_buy_;
    double highest_buy_;
    double volume_of_open_trades_;

    // Market state
    double current_ask_;
    double current_bid_;
    double current_spread_;
    double current_equity_;
    double current_balance_;
    double peak_equity_;
    double current_spacing_;
    double last_bid_;

    // Regime detection data
    std::deque<double> returns_;     // Price returns over lookback
    std::deque<double> prices_;      // Prices for MA calculation
    double ma_sum_;
    int ma_cross_count_;
    double last_direction_ratio_;
    double last_vol_ratio_;

    // Regime state
    Regime current_regime_;
    int regime_changes_;
    long oscillating_ticks_;
    long trending_ticks_;
    long highvol_ticks_;
    long ticks_processed_;
    double lot_multiplier_;

    // Volatility tracking
    double recent_high_;
    double recent_low_;
    long last_vol_reset_seconds_;

    // Note: contract_size_, leverage_, min_volume_, max_volume_ removed
    // — now read from engine.GetConfig() to avoid duplication

    // Parse timestamp to seconds for volatility window reset
    static long ParseTimestampToSeconds(const std::string& ts) {
        if (ts.size() < 19) return 0;
        int year = std::stoi(ts.substr(0, 4));
        int month = std::stoi(ts.substr(5, 2));
        int day = std::stoi(ts.substr(8, 2));
        int hour = std::stoi(ts.substr(11, 2));
        int minute = std::stoi(ts.substr(14, 2));
        int second = std::stoi(ts.substr(17, 2));
        int month_days[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
        long days = (long)(year - 2020) * 365 + (year - 2020) / 4;
        days += month_days[month - 1] + day;
        if (month > 2 && year % 4 == 0) days++;
        return days * 86400L + hour * 3600L + minute * 60L + second;
    }

    void UpdateVolatility(const Tick& tick) {
        long current_seconds = ParseTimestampToSeconds(tick.timestamp);
        long lookback_seconds = (long)(cfg_.volatility_lookback_hours * 3600.0);

        // Reset volatility window based on actual time elapsed
        if (last_vol_reset_seconds_ == 0 || current_seconds - last_vol_reset_seconds_ >= lookback_seconds) {
            recent_high_ = current_bid_;
            recent_low_ = current_bid_;
            last_vol_reset_seconds_ = current_seconds;
        }
        recent_high_ = std::max(recent_high_, current_bid_);
        recent_low_ = std::min(recent_low_, current_bid_);
    }

    void UpdateMACrossings() {
        // Track price crossings over simple moving average
        prices_.push_back(current_bid_);
        if (prices_.size() > (size_t)cfg_.lookback_ticks) {
            ma_sum_ -= prices_.front();
            prices_.pop_front();
        }
        ma_sum_ += current_bid_;

        if (prices_.size() >= 2) {
            double ma = ma_sum_ / prices_.size();
            double prev_bid = prices_[prices_.size() - 2];

            // Check if price crossed MA
            if ((prev_bid < ma && current_bid_ >= ma) ||
                (prev_bid > ma && current_bid_ <= ma)) {
                ma_cross_count_++;
            }
        }
    }

    void DetectRegime() {
        if (returns_.size() < 100) {
            // Not enough data for reliable detection
            current_regime_ = OSCILLATING;
            return;
        }

        // Calculate direction ratio: |sum(returns)| / sum(|returns|)
        double sum_returns = 0.0;
        double sum_abs_returns = 0.0;
        for (double r : returns_) {
            sum_returns += r;
            sum_abs_returns += std::abs(r);
        }

        double direction_ratio = (sum_abs_returns > 0)
            ? std::abs(sum_returns) / sum_abs_returns
            : 0.0;
        last_direction_ratio_ = direction_ratio;

        // Calculate volatility ratio: recent_range / typical_range
        double range = recent_high_ - recent_low_;
        double typical_vol = current_bid_ * (cfg_.typical_vol_pct / 100.0);
        double vol_ratio = (typical_vol > 0) ? range / typical_vol : 1.0;
        last_vol_ratio_ = vol_ratio;

        // Determine regime
        Regime new_regime;

        // First check high volatility (takes precedence if both are true)
        if (vol_ratio > cfg_.highvol_threshold) {
            new_regime = HIGH_VOLATILITY;
            highvol_ticks_++;
        }
        // Then check direction
        else if (direction_ratio < cfg_.oscillating_threshold) {
            new_regime = OSCILLATING;
            oscillating_ticks_++;
        }
        else if (direction_ratio > cfg_.trending_threshold) {
            new_regime = TRENDING;
            trending_ticks_++;
        }
        else {
            // In the middle zone - keep current regime (hysteresis)
            if (current_regime_ == OSCILLATING) {
                oscillating_ticks_++;
            } else if (current_regime_ == TRENDING) {
                trending_ticks_++;
            } else {
                highvol_ticks_++;
            }
            return;  // Don't change regime
        }

        if (new_regime != current_regime_) {
            current_regime_ = new_regime;
            regime_changes_++;
        }
    }

    void ApplyRegimeParameters() {
        // Calculate base adaptive spacing (like FillUpOscillation)
        double range = recent_high_ - recent_low_;
        double typical_vol = current_bid_ * (cfg_.typical_vol_pct / 100.0);
        double vol_ratio = (typical_vol > 0) ? range / typical_vol : 1.0;
        vol_ratio = std::max(0.5, std::min(3.0, vol_ratio));

        double adaptive_spacing = cfg_.base_spacing * vol_ratio;
        adaptive_spacing = std::max(0.5, std::min(5.0, adaptive_spacing));

        // Apply regime-specific modifications
        switch (current_regime_) {
            case OSCILLATING:
                // Normal trading - use adaptive spacing, full lot size
                current_spacing_ = adaptive_spacing;
                lot_multiplier_ = 1.0;
                break;

            case TRENDING:
                // Widen spacing based on action
                switch (cfg_.trending_action) {
                    case TREND_WIDEN_2X:
                        current_spacing_ = adaptive_spacing * 2.0;
                        break;
                    case TREND_WIDEN_3X:
                        current_spacing_ = adaptive_spacing * 3.0;
                        break;
                    case TREND_PAUSE:
                        // Handled in OnTick - return before opening
                        break;
                }
                lot_multiplier_ = 1.0;  // Keep normal size, just wider spacing
                break;

            case HIGH_VOLATILITY:
                // Reduce lot size based on action
                current_spacing_ = adaptive_spacing;  // Keep normal spacing
                switch (cfg_.highvol_action) {
                    case HIGHVOL_REDUCE_50:
                        lot_multiplier_ = 0.5;
                        break;
                    case HIGHVOL_REDUCE_75:
                        lot_multiplier_ = 0.25;
                        break;
                    case HIGHVOL_NORMAL:
                        lot_multiplier_ = 1.0;
                        break;
                }
                break;
        }

        // Clamp spacing
        current_spacing_ = std::max(0.3, std::min(10.0, current_spacing_));
    }

    void Iterate(TickBasedEngine& engine) {
        // Use engine's incrementally-maintained aggregates (O(1) instead of O(N))
        volume_of_open_trades_ = engine.GetBuyVolume();
        lowest_buy_ = engine.GetLowestBuyEntry();
        highest_buy_ = engine.GetHighestBuyEntry();
    }

    double CalculateLotSize(TickBasedEngine& engine, int positions_total) {
        // Use engine's authoritative margin (current market prices, includes margin_rate)
        double used_margin = engine.GetUsedMargin();
        const auto& cfg = engine.GetConfig();

        double margin_stop_out = 20.0;
        double end_price = (positions_total == 0)
            ? current_ask_ * ((100.0 - cfg_.survive_pct) / 100.0)
            : highest_buy_ * ((100.0 - cfg_.survive_pct) / 100.0);

        double distance = current_ask_ - end_price;
        double number_of_trades = std::floor(distance / current_spacing_);
        if (number_of_trades <= 0) number_of_trades = 1;

        double equity_at_target = current_equity_ - volume_of_open_trades_ * distance * cfg.contract_size;
        if (used_margin > 0 && (equity_at_target / used_margin * 100.0) < margin_stop_out) {
            return 0.0;
        }

        double trade_size = cfg.volume_min;
        double d_equity = cfg.contract_size * trade_size * current_spacing_ *
                          (number_of_trades * (number_of_trades + 1) / 2);
        double d_margin = engine.CalculateMarginRequired(trade_size, current_ask_) * number_of_trades;

        double max_mult = cfg.volume_max / cfg.volume_min;
        for (double mult = max_mult; mult >= 1.0; mult -= 0.1) {
            double test_equity = equity_at_target - mult * d_equity;
            double test_margin = used_margin + mult * d_margin;
            if (test_margin > 0 && (test_equity / test_margin * 100.0) > margin_stop_out) {
                trade_size = mult * cfg.volume_min;
                break;
            }
        }

        // Apply regime-based lot multiplier
        trade_size *= lot_multiplier_;

        return std::min(trade_size, cfg.volume_max);
    }

    bool Open(double lots, TickBasedEngine& engine) {
        const auto& cfg = engine.GetConfig();
        if (lots < cfg.volume_min) return false;

        double final_lots = engine.NormalizeLots(std::min(lots, cfg.volume_max));

        double tp = current_ask_ + current_spread_ + current_spacing_;
        Trade* trade = engine.OpenMarketOrder("BUY", final_lots, 0.0, tp);
        return (trade != nullptr);
    }

    void OpenNew(TickBasedEngine& engine) {
        int positions_total = engine.GetOpenPositions().size();

        if (positions_total == 0) {
            double lots = CalculateLotSize(engine, positions_total);
            if (Open(lots, engine)) {
                highest_buy_ = current_ask_;
                lowest_buy_ = current_ask_;
            }
        } else {
            if (lowest_buy_ >= current_ask_ + current_spacing_) {
                double lots = CalculateLotSize(engine, positions_total);
                if (Open(lots, engine)) {
                    lowest_buy_ = current_ask_;
                }
            } else if (highest_buy_ <= current_ask_ - current_spacing_) {
                double lots = CalculateLotSize(engine, positions_total);
                if (Open(lots, engine)) {
                    highest_buy_ = current_ask_;
                }
            }
        }
    }
};

} // namespace backtest

#endif // STRATEGY_REGIME_SWITCHING_H
