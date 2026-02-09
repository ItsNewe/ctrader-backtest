/**
 * FillUpKapitza - Advanced Oscillation Control Strategy
 *
 * Inspired by the Kapitza pendulum, this strategy uses multiple control
 * mechanisms to profit from price oscillations while managing risk:
 *
 * 1. FREQUENCY ANALYSIS - Adapt to dominant oscillation period
 * 2. PHASE DETECTION - Enter at local minima (momentum reversal)
 * 3. RESONANCE DETECTION - Detect dangerous synchronization
 * 4. REGIME DETECTION - Oscillation vs Trend awareness
 * 5. PID CONTROL - Adaptive position sizing
 *
 * Key insight from frequency analysis (XAUUSD 2025):
 * - Small swings (~0.05%): 10 min cycle, $3.80 amplitude
 * - Medium swings (~0.15%): 76 min cycle, $10.90 amplitude
 * - Large swings (~0.50%): 816 min cycle, $36.55 amplitude
 * - Peak activity: 15:00-17:00 UTC (US market)
 */

#ifndef FILL_UP_KAPITZA_H
#define FILL_UP_KAPITZA_H

#include "tick_based_engine.h"
#include <deque>
#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <cfloat>

namespace backtest {

class FillUpKapitza {
public:
    // Market regime states
    enum class Regime {
        OSCILLATION,    // Normal mean-reverting behavior (good for grid)
        TRENDING_UP,    // Sustained upward move (reduce new entries)
        TRENDING_DOWN,  // Sustained downward move (dangerous for long grid)
        HIGH_VOLATILITY // Extreme volatility (widen spacing, reduce size)
    };

    // Phase of current oscillation
    enum class Phase {
        UNKNOWN,
        UPSWING,        // Price moving up
        DOWNSWING,      // Price moving down
        REVERSAL_UP,    // Just reversed from down to up (BUY signal)
        REVERSAL_DOWN   // Just reversed from up to down (no action for long-only)
    };

    struct Config {
        // Base parameters
        double survive_pct = 13.0;
        double base_spacing = 1.5;
        double min_volume = 0.01;
        double max_volume = 10.0;
        double contract_size = 100.0;
        double leverage = 500.0;

        // Frequency adaptation
        double lookback_hours = 1.0;       // For volatility measurement
        double typical_vol_pct = 0.5;      // Percentage of price as typical vol

        // Phase detection
        double momentum_period = 20;       // Ticks for momentum calculation
        double reversal_threshold = 0.3;   // Minimum reversal size (% of spacing)

        // Resonance detection
        double resonance_threshold = 0.8;  // Correlation threshold for danger
        int resonance_window = 10;         // Positions to check

        // Regime detection
        double trend_threshold = 0.6;      // Hurst-like threshold for trend
        int regime_lookback = 100;         // Ticks for regime detection

        // PID control
        double pid_kp = 0.5;               // Proportional gain
        double pid_ki = 0.1;               // Integral gain
        double pid_kd = 0.2;               // Derivative gain
        double target_dd_pct = 20.0;       // Target drawdown percentage

        // Regime multipliers for position sizing
        double trending_down_mult = 0.5;   // Size reduction in trending down
        double high_volatility_mult = 0.3; // Size reduction in high volatility
        double resonance_mult = 0.5;       // Size reduction when resonance detected

        // Entry control
        bool allow_trending_down_entry = false;  // Allow entries in trending down
        bool allow_high_vol_entry = false;       // Allow entries in high volatility

        // Safety configuration (forced entry discovery)
        bool force_min_volume_entry = true;      // Force entry at min_volume when lot sizing returns 0
        int max_positions = 0;                   // Maximum concurrent positions (0 = unlimited)
    };

    struct Stats {
        long forced_entries = 0;
        long max_position_blocks = 0;
        int peak_positions = 0;
    };

    FillUpKapitza(const Config& config)
        : config_(config),
          current_regime_(Regime::OSCILLATION),
          current_phase_(Phase::UNKNOWN),
          lowest_buy_(DBL_MAX),
          highest_buy_(DBL_MIN),
          volume_of_open_trades_(0.0),
          current_ask_(0.0),
          current_bid_(0.0),
          current_spread_(0.0),
          current_equity_(0.0),
          current_balance_(0.0),
          peak_equity_(0.0),
          current_spacing_(config.base_spacing),
          recent_high_(0.0),
          recent_low_(DBL_MAX),
          last_momentum_(0.0),
          momentum_direction_(0),
          resonance_level_(0.0),
          pid_integral_(0.0),
          pid_last_error_(0.0),
          pid_multiplier_(1.0),
          phase_entry_wait_(0),
          ticks_processed_(0)
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
        }
        if (peak_equity_ < current_equity_) {
            peak_equity_ = current_equity_;
        }

        // Track price history for analysis
        UpdatePriceHistory(tick);

        // Update control systems
        UpdateRegimeDetection();
        UpdatePhaseDetection();
        UpdateResonanceDetection(engine);
        UpdatePIDControl();

        // Adapt spacing based on frequency analysis
        UpdateAdaptiveSpacing();

        // Process positions
        Iterate(engine);

        // Open new positions (with all control checks)
        OpenNew(engine);
    }

    // Getters for analysis
    Regime GetCurrentRegime() const { return current_regime_; }
    Phase GetCurrentPhase() const { return current_phase_; }
    double GetResonanceLevel() const { return resonance_level_; }
    double GetPIDMultiplier() const { return pid_multiplier_; }
    double GetCurrentSpacing() const { return current_spacing_; }
    const Stats& GetStats() const { return stats_; }

private:
    Config config_;
    Stats stats_;

    // Control states
    Regime current_regime_;
    Phase current_phase_;

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

    // Price history for analysis
    std::deque<double> price_history_;
    std::deque<double> momentum_history_;
    double recent_high_;
    double recent_low_;

    // Phase detection state
    double last_momentum_;
    int momentum_direction_;  // 1=up, -1=down, 0=neutral

    // Resonance detection
    double resonance_level_;
    std::vector<double> position_pnl_history_;

    // PID control state
    double pid_integral_;
    double pid_last_error_;
    double pid_multiplier_;

    // Phase-based entry control
    int phase_entry_wait_;

    // Statistics
    long ticks_processed_;

    //=========================================================================
    // 1. FREQUENCY ANALYSIS - Adaptive Spacing
    //=========================================================================
    void UpdatePriceHistory(const Tick& tick) {
        price_history_.push_back(tick.bid);

        // Keep history for regime/momentum analysis
        size_t max_history = 1000;
        if (price_history_.size() > max_history) {
            price_history_.pop_front();
        }

        // Update recent high/low for volatility
        recent_high_ = std::max(recent_high_, tick.bid);
        recent_low_ = std::min(recent_low_, tick.bid);

        // Reset periodically based on lookback
        long ticks_per_hour = 720000;  // Approximate
        long lookback_ticks = static_cast<long>(config_.lookback_hours * ticks_per_hour);
        if (ticks_processed_ % lookback_ticks == 0) {
            recent_high_ = tick.bid;
            recent_low_ = tick.bid;
        }
    }

    void UpdateAdaptiveSpacing() {
        double range = recent_high_ - recent_low_;
        if (range > 0 && recent_high_ > 0 && current_bid_ > 0) {
            // Percentage-based typical volatility
            double typical_vol = current_bid_ * (config_.typical_vol_pct / 100.0);
            double vol_ratio = range / typical_vol;
            vol_ratio = std::max(0.5, std::min(3.0, vol_ratio));

            double new_spacing = config_.base_spacing * vol_ratio;

            // Regime adjustment
            if (current_regime_ == Regime::TRENDING_DOWN) {
                new_spacing *= 1.5;  // Widen during dangerous trend
            } else if (current_regime_ == Regime::HIGH_VOLATILITY) {
                new_spacing *= 2.0;  // Much wider during extreme vol
            }

            new_spacing = std::max(0.5, std::min(10.0, new_spacing));

            if (std::abs(new_spacing - current_spacing_) > 0.1) {
                current_spacing_ = new_spacing;
            }
        }
    }

    //=========================================================================
    // 2. PHASE DETECTION - Enter at Local Minima
    //=========================================================================
    void UpdatePhaseDetection() {
        if (price_history_.size() < config_.momentum_period) {
            current_phase_ = Phase::UNKNOWN;
            return;
        }

        // Calculate momentum (rate of change)
        size_t period = static_cast<size_t>(config_.momentum_period);
        double old_price = price_history_[price_history_.size() - period];
        double new_price = price_history_.back();
        double momentum = new_price - old_price;

        // Store momentum history
        momentum_history_.push_back(momentum);
        if (momentum_history_.size() > 100) {
            momentum_history_.pop_front();
        }

        // Detect phase
        int new_direction = (momentum > 0) ? 1 : (momentum < 0) ? -1 : 0;

        // Detect reversals
        if (momentum_direction_ == -1 && new_direction == 1) {
            // Down to Up reversal - potential BUY signal
            double reversal_size = std::abs(momentum - last_momentum_);
            if (reversal_size > config_.reversal_threshold * current_spacing_) {
                current_phase_ = Phase::REVERSAL_UP;
                phase_entry_wait_ = 0;  // Ready to enter
            }
        } else if (momentum_direction_ == 1 && new_direction == -1) {
            // Up to Down reversal
            current_phase_ = Phase::REVERSAL_DOWN;
            phase_entry_wait_ = 5;  // Wait before entering
        } else if (new_direction == 1) {
            current_phase_ = Phase::UPSWING;
        } else if (new_direction == -1) {
            current_phase_ = Phase::DOWNSWING;
            phase_entry_wait_++;  // Increment wait counter during downswing
        }

        momentum_direction_ = new_direction;
        last_momentum_ = momentum;
    }

    bool IsGoodPhaseForEntry() const {
        // Best entry: just after reversal from down to up
        if (current_phase_ == Phase::REVERSAL_UP) {
            return true;
        }

        // Okay entry: during upswing
        if (current_phase_ == Phase::UPSWING) {
            return true;
        }

        // During downswing, only enter after waiting
        if (current_phase_ == Phase::DOWNSWING && phase_entry_wait_ > 10) {
            return true;  // Waited long enough, might be near bottom
        }

        // Right after down-reversal, wait
        if (current_phase_ == Phase::REVERSAL_DOWN) {
            return false;
        }

        // Unknown phase - be cautious
        return phase_entry_wait_ > 20;
    }

    //=========================================================================
    // 3. RESONANCE DETECTION - Detect Dangerous Synchronization
    //=========================================================================
    void UpdateResonanceDetection(TickBasedEngine& engine) {
        const auto& positions = engine.GetOpenPositions();

        if (positions.size() < 2) {
            resonance_level_ = 0.0;
            return;
        }

        // Calculate current P/L for each position
        std::vector<double> current_pnls;
        for (const Trade* trade : positions) {
            if (trade->IsBuy()) {
                double pnl = (current_bid_ - trade->entry_price) * trade->lot_size * config_.contract_size;
                current_pnls.push_back(pnl);
            }
        }

        if (current_pnls.size() < 2) {
            resonance_level_ = 0.0;
            return;
        }

        // Check if all positions are moving the same direction
        int positive_count = 0;
        int negative_count = 0;
        for (double pnl : current_pnls) {
            if (pnl > 0) positive_count++;
            else if (pnl < 0) negative_count++;
        }

        int total = current_pnls.size();
        double max_ratio = std::max(positive_count, negative_count) / static_cast<double>(total);

        // High ratio = all positions in sync = resonance danger
        resonance_level_ = max_ratio;

        // If all positions losing together, this is dangerous
        if (negative_count == total) {
            resonance_level_ = 1.0;  // Maximum danger
        }
    }

    bool IsResonanceDanger() const {
        return resonance_level_ > config_.resonance_threshold;
    }

    //=========================================================================
    // 4. REGIME DETECTION - Oscillation vs Trend
    //=========================================================================
    void UpdateRegimeDetection() {
        if (price_history_.size() < config_.regime_lookback) {
            current_regime_ = Regime::OSCILLATION;
            return;
        }

        // Calculate price statistics over lookback period
        size_t start = price_history_.size() - config_.regime_lookback;
        double sum = 0, sum_sq = 0;
        double min_p = DBL_MAX, max_p = DBL_MIN;
        double first_p = price_history_[start];
        double last_p = price_history_.back();

        for (size_t i = start; i < price_history_.size(); i++) {
            double p = price_history_[i];
            sum += p;
            sum_sq += p * p;
            min_p = std::min(min_p, p);
            max_p = std::max(max_p, p);
        }

        double n = config_.regime_lookback;
        double mean = sum / n;
        double variance = (sum_sq / n) - (mean * mean);
        double stddev = std::sqrt(std::max(0.0, variance));

        // Net movement vs total range
        double net_move = last_p - first_p;
        double total_range = max_p - min_p;

        // Trend indicator: high net_move/range = trending
        double trend_ratio = (total_range > 0) ? std::abs(net_move) / total_range : 0;

        // Volatility indicator: stddev relative to price
        double vol_ratio = (mean > 0) ? stddev / mean * 100 : 0;

        // Classify regime
        if (vol_ratio > 2.0) {
            current_regime_ = Regime::HIGH_VOLATILITY;
        } else if (trend_ratio > config_.trend_threshold) {
            if (net_move > 0) {
                current_regime_ = Regime::TRENDING_UP;
            } else {
                current_regime_ = Regime::TRENDING_DOWN;
            }
        } else {
            current_regime_ = Regime::OSCILLATION;
        }
    }

    bool IsGoodRegimeForEntry() const {
        // Best: oscillation regime
        if (current_regime_ == Regime::OSCILLATION) {
            return true;
        }

        // Okay: trending up (we're long)
        if (current_regime_ == Regime::TRENDING_UP) {
            return true;
        }

        // Configurable: trending down entry
        if (current_regime_ == Regime::TRENDING_DOWN && config_.allow_trending_down_entry) {
            return true;
        }

        // Configurable: high volatility entry
        if (current_regime_ == Regime::HIGH_VOLATILITY && config_.allow_high_vol_entry) {
            return true;
        }

        // Default: don't enter in dangerous regimes
        return false;
    }

    //=========================================================================
    // 5. PID CONTROL - Adaptive Position Sizing
    //=========================================================================
    void UpdatePIDControl() {
        if (peak_equity_ <= 0) return;

        // Error = current drawdown - target drawdown
        double current_dd_pct = (peak_equity_ - current_equity_) / peak_equity_ * 100.0;
        double error = current_dd_pct - config_.target_dd_pct;

        // PID calculation
        double p_term = config_.pid_kp * error;
        pid_integral_ += error;
        pid_integral_ = std::max(-100.0, std::min(100.0, pid_integral_));  // Anti-windup
        double i_term = config_.pid_ki * pid_integral_;
        double d_term = config_.pid_kd * (error - pid_last_error_);

        double control = p_term + i_term + d_term;

        // Convert control signal to position size multiplier
        // Negative control (under target) -> larger positions
        // Positive control (over target) -> smaller positions
        pid_multiplier_ = 1.0 - control / 100.0;
        pid_multiplier_ = std::max(0.1, std::min(2.0, pid_multiplier_));

        // In dangerous regimes, reduce multiplier further
        if (current_regime_ == Regime::TRENDING_DOWN) {
            pid_multiplier_ *= config_.trending_down_mult;
        } else if (current_regime_ == Regime::HIGH_VOLATILITY) {
            pid_multiplier_ *= config_.high_volatility_mult;
        }

        // If resonance detected, reduce multiplier
        if (IsResonanceDanger()) {
            pid_multiplier_ *= config_.resonance_mult;
        }

        pid_last_error_ = error;
    }

    //=========================================================================
    // Position Management
    //=========================================================================
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
            ? current_ask_ * ((100.0 - config_.survive_pct) / 100.0)
            : highest_buy_ * ((100.0 - config_.survive_pct) / 100.0);

        double distance = current_ask_ - end_price;
        double number_of_trades = std::floor(distance / current_spacing_);
        if (number_of_trades <= 0) number_of_trades = 1;

        double equity_at_target = current_equity_ - volume_of_open_trades_ * distance * cfg.contract_size;
        if (used_margin > 0 && (equity_at_target / used_margin * 100.0) < margin_stop_out) {
            return 0.0;
        }

        double trade_size = config_.min_volume;
        double d_equity = cfg.contract_size * trade_size * current_spacing_ * (number_of_trades * (number_of_trades + 1) / 2);
        double d_margin = engine.CalculateMarginRequired(trade_size, current_ask_) * number_of_trades;

        double max_mult = config_.max_volume / config_.min_volume;
        for (double mult = max_mult; mult >= 1.0; mult -= 0.1) {
            double test_equity = equity_at_target - mult * d_equity;
            double test_margin = used_margin + mult * d_margin;
            if (test_margin > 0 && (test_equity / test_margin * 100.0) > margin_stop_out) {
                trade_size = mult * config_.min_volume;
                break;
            }
        }

        // Apply PID multiplier
        trade_size *= pid_multiplier_;

        return std::min(trade_size, config_.max_volume);
    }

    bool Open(double lots, TickBasedEngine& engine) {
        // Forced entry: if lot sizing returns 0 but force is enabled, use min_volume
        if (lots < config_.min_volume) {
            if (config_.force_min_volume_entry) {
                lots = config_.min_volume;
                stats_.forced_entries++;
            } else {
                return false;
            }
        }

        double final_lots = engine.NormalizeLots(std::min(lots, config_.max_volume));

        double tp = current_ask_ + current_spread_ + current_spacing_;
        Trade* trade = engine.OpenMarketOrder("BUY", final_lots, 0.0, tp);
        return (trade != nullptr);
    }

    void OpenNew(TickBasedEngine& engine) {
        // Multi-layer control checks
        if (!IsGoodRegimeForEntry()) {
            return;  // Don't enter in bad regime
        }

        if (IsResonanceDanger() && engine.GetOpenPositions().size() > 5) {
            return;  // Too much resonance with many positions
        }

        int positions_total = (int)engine.GetOpenPositions().size();

        // Track peak positions
        if (positions_total > stats_.peak_positions) {
            stats_.peak_positions = positions_total;
        }

        // Safety: max position cap
        if (config_.max_positions > 0 && positions_total >= config_.max_positions) {
            stats_.max_position_blocks++;
            return;
        }

        if (positions_total == 0) {
            // First position - always enter if regime is good
            double lots = CalculateLotSize(engine, positions_total);
            if (Open(lots, engine)) {
                highest_buy_ = current_ask_;
                lowest_buy_ = current_ask_;
            }
        } else {
            // Additional positions - check phase and spacing
            if (lowest_buy_ >= current_ask_ + current_spacing_) {
                // Price dropped enough for new level
                if (IsGoodPhaseForEntry() || positions_total > 20) {
                    // Either good phase or emergency (many positions)
                    double lots = CalculateLotSize(engine, positions_total);
                    if (Open(lots, engine)) {
                        lowest_buy_ = current_ask_;
                    }
                }
            } else if (highest_buy_ <= current_ask_ - current_spacing_) {
                // Price rose enough - extend grid upward
                double lots = CalculateLotSize(engine, positions_total);
                if (Open(lots, engine)) {
                    highest_buy_ = current_ask_;
                }
            }
        }
    }
};

} // namespace backtest

#endif // FILL_UP_KAPITZA_H
