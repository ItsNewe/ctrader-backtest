/**
 * @file fill_up_oscillation.h
 * @brief Oscillation-optimized grid trading strategy for precious metals.
 *
 * This strategy implements an enhanced grid trading approach optimized for
 * oscillating markets like XAUUSD and XAGUSD. Key features include:
 *
 * - **Adaptive spacing**: Automatically adjusts grid spacing based on volatility
 * - **Anti-fragile scaling**: Increases position size during drawdowns
 * - **Velocity filter**: Pauses trading during market crashes
 * - **Trend adaptation**: Tightens spacing during strong trends
 *
 * @section fuo_usage Basic Usage
 * @code
 * // Using config struct (recommended)
 * auto config = FillUpOscillation::Config::XAUUSD_Default();
 * config.survive_pct = 15.0;  // Customize if needed
 * FillUpOscillation strategy(config);
 *
 * // Run with engine
 * engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
 *     strategy.OnTick(tick, eng);
 * });
 * @endcode
 *
 * @section fuo_modes Enhancement Modes
 * | Mode | Description |
 * |------|-------------|
 * | BASELINE | No enhancements (control) |
 * | ADAPTIVE_SPACING | Adjust spacing to volatility (recommended) |
 * | ANTIFRAGILE | Increase size in drawdown |
 * | VELOCITY_FILTER | Pause on crash velocity |
 * | ALL_COMBINED | All enhancements active |
 *
 * @see TickBasedEngine, StrategyCombinedJu
 */

#ifndef FILL_UP_OSCILLATION_H
#define FILL_UP_OSCILLATION_H

#include "tick_based_engine.h"
#include <vector>
#include <deque>
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace backtest {

/**
 * @brief Oscillation-optimized grid trading strategy.
 *
 * Enhances basic grid trading with:
 * 1. **Adaptive spacing** based on recent volatility
 * 2. **Anti-fragile scaling** - increases position size during drawdowns
 * 3. **Velocity filter** - pauses on crashes, not slow drawdowns
 * 4. **Trend adaptation** - tighter spacing in strong trends
 *
 * @section fuo_presets Recommended Settings
 * | Symbol | survive_pct | base_spacing | lookback | Mode |
 * |--------|-------------|--------------|----------|------|
 * | XAUUSD | 13% | $1.50 | 4h | ADAPTIVE_SPACING |
 * | XAGUSD | 19% | 2.0% | 1h | ADAPTIVE_SPACING + pct_spacing |
 */
class FillUpOscillation {
public:
    // Oscillation enhancement modes
    enum Mode {
        BASELINE = 0,           // No enhancements (control)
        ADAPTIVE_SPACING = 1,   // Adjust spacing to volatility
        ANTIFRAGILE = 2,        // Increase size in drawdown
        VELOCITY_FILTER = 3,    // Pause on crash velocity
        ALL_COMBINED = 4,       // All enhancements
        ADAPTIVE_LOOKBACK = 5,  // Adapt lookback period based on volatility regime
        DOUBLE_ADAPTIVE = 6,    // Adaptive spacing + adaptive lookback
        TREND_ADAPTIVE = 7      // Adjust spacing based on trend strength (tight in trends, wide in chop)
    };

    // Adaptive spacing configuration (matching EA input parameters)
    struct AdaptiveConfig {
        double typical_vol_pct;
        double min_spacing_mult;
        double max_spacing_mult;
        double min_spacing_abs;      // In pct_spacing mode: percentage of price
        double max_spacing_abs;      // In pct_spacing mode: percentage of price
        double spacing_change_threshold;
        bool pct_spacing;            // When true, base_spacing and abs clamps are % of price

        AdaptiveConfig()
            : typical_vol_pct(0.5), min_spacing_mult(0.5), max_spacing_mult(3.0),
              min_spacing_abs(0.5), max_spacing_abs(5.0), spacing_change_threshold(0.1),
              pct_spacing(false) {}
    };

    // Safety configuration - alternative to blocking entries
    struct SafetyConfig {
        bool force_min_volume_entry;   // Force entry at min_volume when lot sizing returns 0
        int max_positions;             // Maximum concurrent positions (0 = unlimited)
        double equity_stop_pct;        // Close all if DD exceeds this % (0 = disabled)
        double margin_level_floor;     // Skip entry if margin level below this % (0 = disabled)

        SafetyConfig()
            : force_min_volume_entry(true),  // Default ON - key discovery
              max_positions(0),               // Unlimited by default
              equity_stop_pct(0),             // Disabled
              margin_level_floor(0) {}        // Disabled
    };

    /**
     * Unified configuration struct for FillUpOscillation strategy.
     * Replaces 11+ constructor parameters with a single config object.
     *
     * Example usage:
     *   FillUpOscillation::Config config;
     *   config.survive_pct = 13.0;
     *   config.base_spacing = 1.5;
     *   config.mode = FillUpOscillation::ADAPTIVE_SPACING;
     *   FillUpOscillation strategy(config);
     */
    struct Config {
        // Core parameters (required)
        double survive_pct = 13.0;          ///< Survive percentage for lot sizing
        double base_spacing = 1.5;          ///< Base grid spacing in price units
        double min_volume = 0.01;           ///< Minimum lot size
        double max_volume = 10.0;           ///< Maximum lot size
        double contract_size = 100.0;       ///< Contract size (XAUUSD=100, XAGUSD=5000)
        double leverage = 500.0;            ///< Account leverage

        // Enhancement mode
        Mode mode = ADAPTIVE_SPACING;       ///< Strategy enhancement mode

        // Mode-specific parameters
        double antifragile_scale = 0.1;     ///< Antifragile scaling (10% more per 5% DD)
        double velocity_threshold = 30.0;   ///< Crash velocity threshold ($/hour)
        double volatility_lookback_hours = 4.0;  ///< Volatility measurement window

        // Nested configurations
        AdaptiveConfig adaptive;            ///< Adaptive spacing configuration
        SafetyConfig safety;                ///< Safety feature configuration

        // Preset configurations
        static Config XAUUSD_Default() {
            Config c;
            c.survive_pct = 13.0;
            c.base_spacing = 1.5;
            c.contract_size = 100.0;
            c.leverage = 500.0;
            c.mode = ADAPTIVE_SPACING;
            c.volatility_lookback_hours = 4.0;
            c.adaptive.typical_vol_pct = 0.55;
            return c;
        }

        static Config XAGUSD_Default() {
            Config c;
            c.survive_pct = 19.0;
            c.base_spacing = 2.0;  // Percentage mode
            c.contract_size = 5000.0;
            c.leverage = 500.0;
            c.mode = ADAPTIVE_SPACING;
            c.volatility_lookback_hours = 1.0;
            c.adaptive.pct_spacing = true;  // Enable percentage spacing
            c.adaptive.typical_vol_pct = 0.45;
            return c;
        }

        static Config Aggressive() {
            Config c = XAUUSD_Default();
            c.survive_pct = 8.0;
            c.base_spacing = 1.0;
            c.mode = ALL_COMBINED;
            c.antifragile_scale = 0.2;
            return c;
        }

        static Config Conservative() {
            Config c = XAUUSD_Default();
            c.survive_pct = 20.0;
            c.base_spacing = 2.5;
            c.mode = ADAPTIVE_SPACING;
            c.safety.max_positions = 50;
            c.safety.margin_level_floor = 150.0;
            return c;
        }
    };

    // Statistics tracking
    struct Stats {
        long forced_entries = 0;           // Entries forced at min_volume
        long max_position_blocks = 0;      // Entries blocked by max_positions
        long margin_level_blocks = 0;      // Entries blocked by margin_level_floor
        int peak_positions = 0;            // Maximum positions reached
    };

    /**
     * Config-based constructor (recommended).
     * Use presets like Config::XAUUSD_Default() or customize the config.
     */
    explicit FillUpOscillation(const Config& config)
        : FillUpOscillation(
              config.survive_pct, config.base_spacing,
              config.min_volume, config.max_volume,
              config.contract_size, config.leverage,
              config.mode, config.antifragile_scale,
              config.velocity_threshold, config.volatility_lookback_hours,
              config.adaptive, config.safety)
    {}

    /**
     * Legacy constructor with positional parameters.
     * @deprecated Use Config-based constructor instead.
     */
    FillUpOscillation(double survive_pct, double base_spacing,
                      double min_volume, double max_volume,
                      double contract_size, double leverage,
                      Mode mode = BASELINE,
                      double antifragile_scale = 0.1,      // 10% more per 5% DD
                      double velocity_threshold = 30.0,     // $30/hour = crash
                      double volatility_lookback_hours = 4.0,
                      AdaptiveConfig adaptive_config = AdaptiveConfig(),
                      SafetyConfig safety_config = SafetyConfig())
        : survive_pct_(survive_pct),
          base_spacing_(base_spacing),
          min_volume_(min_volume),
          max_volume_(max_volume),
          mode_(mode),
          antifragile_scale_(antifragile_scale),
          velocity_threshold_(velocity_threshold),
          volatility_lookback_hours_(volatility_lookback_hours),
          adaptive_config_(adaptive_config),
          safety_config_(safety_config),
          lowest_buy_(DBL_MAX),
          highest_buy_(DBL_MIN),
          volume_of_open_trades_(0.0),
          current_ask_(0.0),
          current_bid_(0.0),
          current_spread_(0.0),
          current_equity_(0.0),
          current_balance_(0.0),
          peak_equity_(0.0),
          current_spacing_(base_spacing),
          recent_high_(0.0),
          recent_low_(DBL_MAX),
          hour_start_price_(0.0),
          velocity_paused_(false),
          pause_until_price_(0.0),
          ticks_processed_(0),
          velocity_pause_count_(0),
          max_velocity_seen_(0.0),
          adaptive_spacing_changes_(0),
          current_lookback_hours_(volatility_lookback_hours),
          meta_high_(0.0),
          meta_low_(DBL_MAX),
          lookback_changes_(0),
          trend_start_price_(0.0),
          trend_lookback_hours_(volatility_lookback_hours),
          trend_spacing_changes_(0),
          current_timestamp_(""),
          last_trend_period_id_(-1),
          calibration_complete_(false),
          warmup_periods_(30),
          thresh_strong_(2.0),
          thresh_moderate_(1.0),
          thresh_weak_(0.4),
          last_vol_reset_seconds_(0),
          last_hour_seconds_(0),
          last_meta_reset_seconds_(0)
    {
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_spread_ = tick.spread();
        current_timestamp_ = tick.timestamp;
        current_equity_ = engine.GetEquity();
        current_balance_ = engine.GetBalance();
        ticks_processed_++;

        // Initialize
        if (peak_equity_ == 0.0) {
            peak_equity_ = current_equity_;
            hour_start_price_ = current_bid_;
            // Set initial spacing from first price when using percentage mode
            if (adaptive_config_.pct_spacing) {
                current_spacing_ = current_bid_ * (base_spacing_ / 100.0);
            }
        }

        // Update peak
        if (current_equity_ > peak_equity_) {
            peak_equity_ = current_equity_;
        }

        // Track volatility (price range over lookback period)
        UpdateVolatility(tick);

        // Check velocity filter
        if (mode_ == VELOCITY_FILTER || mode_ == ALL_COMBINED) {
            CheckVelocity(tick);
            if (velocity_paused_) {
                // Still paused - check if price recovered
                if (current_bid_ > pause_until_price_) {
                    velocity_paused_ = false;
                }
                return;
            }
        }

        // Update adaptive lookback (must come before adaptive spacing)
        if (mode_ == ADAPTIVE_LOOKBACK || mode_ == DOUBLE_ADAPTIVE) {
            UpdateAdaptiveLookback();
        }

        // Update adaptive spacing
        if (mode_ == ADAPTIVE_SPACING || mode_ == ALL_COMBINED || mode_ == DOUBLE_ADAPTIVE) {
            UpdateAdaptiveSpacing();
        }

        // Update trend-adaptive spacing
        if (mode_ == TREND_ADAPTIVE) {
            UpdateTrendAdaptiveSpacing();
        }

        // Process positions
        Iterate(engine);

        // Open new positions
        OpenNew(engine);
    }

    // Statistics
    double GetCurrentSpacing() const { return current_spacing_; }
    int GetVelocityPauseCount() const { return velocity_pause_count_; }
    double GetMaxVelocity() const { return max_velocity_seen_; }
    int GetAdaptiveSpacingChanges() const { return adaptive_spacing_changes_; }
    double GetPeakEquity() const { return peak_equity_; }
    double GetCurrentLookback() const { return current_lookback_hours_; }
    int GetLookbackChanges() const { return lookback_changes_; }
    int GetTrendSpacingChanges() const { return trend_spacing_changes_; }
    double GetTrendStrength() const {
        if (trend_start_price_ <= 0) return 0;
        return (current_bid_ - trend_start_price_) / trend_start_price_ * 100.0;
    }
    bool IsCalibrationComplete() const { return calibration_complete_; }
    double GetThreshStrong() const { return thresh_strong_; }
    double GetThreshModerate() const { return thresh_moderate_; }
    double GetThreshWeak() const { return thresh_weak_; }
    int GetWarmupSamples() const { return (int)trend_history_.size(); }

    // Safety statistics
    const Stats& GetStats() const { return stats_; }
    void SetSafetyConfig(const SafetyConfig& config) { safety_config_ = config; }
    void SetMaxVolume(double v) { max_volume_ = v; }

private:
    // Base parameters
    double survive_pct_;
    double base_spacing_;
    double min_volume_;
    double max_volume_;
    // Note: contract_size_/leverage_ removed — now read from engine.GetConfig()
    Mode mode_;

    // Oscillation parameters
    double antifragile_scale_;
    double velocity_threshold_;
    double volatility_lookback_hours_;

    // Configurable adaptive spacing parameters
    AdaptiveConfig adaptive_config_;

    // Safety configuration and stats
    SafetyConfig safety_config_;
    Stats stats_;

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

    // Adaptive spacing
    double current_spacing_;
    std::deque<double> price_history_;
    double recent_high_;
    double recent_low_;
    double hour_start_price_;

    // Velocity filter
    bool velocity_paused_;
    double pause_until_price_;
    std::deque<std::pair<long, double>> hourly_prices_;  // tick_count, price

    // Statistics
    long ticks_processed_;
    int velocity_pause_count_;
    double max_velocity_seen_;
    int adaptive_spacing_changes_;

    // Adaptive lookback
    double current_lookback_hours_;
    double meta_high_;           // High over longer period (for regime detection)
    double meta_low_;            // Low over longer period
    int lookback_changes_;

    // Trend tracking (for TREND_ADAPTIVE mode)
    double trend_start_price_;       // Price at start of trend window
    double trend_lookback_hours_;    // How far back to measure trend (default: 24h)
    int trend_spacing_changes_;      // Count of trend-based spacing changes
    std::string current_timestamp_;  // Current tick timestamp for period detection
    int last_trend_period_id_;       // Last trend period ID (for timestamp-based detection)

    // Self-calibration for TREND_ADAPTIVE
    std::vector<double> trend_history_;     // Collected trend values during warmup
    bool calibration_complete_;             // Whether warmup calibration is done
    int warmup_periods_;                    // Number of periods to collect before calibrating
    double thresh_strong_;                  // Calibrated threshold for strong trend (p75)
    double thresh_moderate_;                // Calibrated threshold for moderate trend (p50)
    double thresh_weak_;                    // Calibrated threshold for weak trend (p25)

    // Timestamp-based volatility tracking
    long last_vol_reset_seconds_;           // Last volatility window reset (seconds)
    long last_hour_seconds_;                // Last hourly tracking reset (seconds)
    long last_meta_reset_seconds_;          // Last meta-volatility reset (seconds)

    // Parse "YYYY.MM.DD HH:MM:SS.mmm" to seconds since reference
    // Optimized: zero-allocation manual parsing (no substr/stoi)
    static long ParseTimestampToSeconds(const std::string& ts) {
        if (ts.size() < 19) return 0;
        // Fast manual digit extraction
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

    void UpdateVolatility(const Tick& tick) {
        // Parse timestamp to seconds for time-based lookback
        long current_seconds = ParseTimestampToSeconds(tick.timestamp);

        // Track hourly prices for velocity
        if (last_hour_seconds_ == 0 || current_seconds - last_hour_seconds_ >= 3600) {
            hourly_prices_.push_back(std::make_pair(ticks_processed_, current_bid_));
            if (hourly_prices_.size() > 24) {
                hourly_prices_.pop_front();
            }
            hour_start_price_ = current_bid_;
            last_hour_seconds_ = current_seconds;
        }

        // Reset volatility window based on actual time elapsed
        double effective_lookback = (mode_ == ADAPTIVE_LOOKBACK || mode_ == DOUBLE_ADAPTIVE)
            ? current_lookback_hours_
            : volatility_lookback_hours_;
        long lookback_seconds = (long)(effective_lookback * 3600.0);

        if (last_vol_reset_seconds_ == 0 || current_seconds - last_vol_reset_seconds_ >= lookback_seconds) {
            recent_high_ = current_bid_;
            recent_low_ = current_bid_;
            last_vol_reset_seconds_ = current_seconds;
        }
        recent_high_ = std::max(recent_high_, current_bid_);
        recent_low_ = std::min(recent_low_, current_bid_);

        // Meta-volatility (4 hours fixed) for lookback adaptation
        if (last_meta_reset_seconds_ == 0 || current_seconds - last_meta_reset_seconds_ >= 14400) { // 4h
            meta_high_ = current_bid_;
            meta_low_ = current_bid_;
            last_meta_reset_seconds_ = current_seconds;
        }
        meta_high_ = std::max(meta_high_, current_bid_);
        meta_low_ = std::min(meta_low_, current_bid_);
    }

    void CheckVelocity(const Tick& /*tick*/) {
        // Calculate price velocity ($/hour)
        if (hourly_prices_.size() >= 1) {
            double velocity = hour_start_price_ - current_bid_;  // Positive = dropping
            max_velocity_seen_ = std::max(max_velocity_seen_, velocity);

            if (velocity > velocity_threshold_ && !velocity_paused_) {
                // Crash detected - pause
                velocity_paused_ = true;
                pause_until_price_ = current_bid_ + velocity_threshold_ * 0.5;  // Resume when recovers 50%
                velocity_pause_count_++;
            }
        }
    }

    void UpdateAdaptiveSpacing() {
        double range = recent_high_ - recent_low_;
        if (range > 0 && recent_high_ > 0 && current_bid_ > 0) {
            // Percentage-based typical volatility (auto-adapts to any instrument/price)
            double typical_vol = current_bid_ * (adaptive_config_.typical_vol_pct / 100.0);
            double vol_ratio = range / typical_vol;
            vol_ratio = std::max(adaptive_config_.min_spacing_mult,
                                 std::min(adaptive_config_.max_spacing_mult, vol_ratio));

            // Compute effective base spacing and clamps
            double effective_base, min_abs, max_abs, change_thresh;
            if (adaptive_config_.pct_spacing) {
                // Percentage mode: all spacing values scale with current price
                effective_base = current_bid_ * (base_spacing_ / 100.0);
                min_abs = current_bid_ * (adaptive_config_.min_spacing_abs / 100.0);
                max_abs = current_bid_ * (adaptive_config_.max_spacing_abs / 100.0);
                change_thresh = current_bid_ * (adaptive_config_.spacing_change_threshold / 100.0);
            } else {
                // Absolute mode (original behavior)
                effective_base = base_spacing_;
                min_abs = adaptive_config_.min_spacing_abs;
                max_abs = adaptive_config_.max_spacing_abs;
                change_thresh = adaptive_config_.spacing_change_threshold;
            }

            double new_spacing = effective_base * vol_ratio;
            new_spacing = std::max(min_abs, std::min(max_abs, new_spacing));

            if (std::abs(new_spacing - current_spacing_) > change_thresh) {
                current_spacing_ = new_spacing;
                adaptive_spacing_changes_++;
            }
        }
    }

    void UpdateAdaptiveLookback() {
        // Adjust lookback period based on meta-volatility (4-hour range)
        // High meta-volatility = shorter lookback (react quickly to regime)
        // Low meta-volatility = longer lookback (smooth out noise)
        double meta_range = meta_high_ - meta_low_;
        if (meta_range > 0 && meta_high_ > 0) {
            // Meta volatility ratio: current 4h range vs typical ($20 = normal for gold over 4h)
            double meta_vol_ratio = meta_range / 20.0;
            meta_vol_ratio = std::max(0.5, std::min(3.0, meta_vol_ratio));

            // Inverse relationship: high volatility = short lookback
            // At 0.5x meta_vol: lookback = base * 2.0 (longer, smoother)
            // At 1.0x meta_vol: lookback = base * 1.0 (normal)
            // At 3.0x meta_vol: lookback = base * 0.33 (shorter, reactive)
            double lookback_multiplier = 1.0 / meta_vol_ratio;
            double new_lookback = volatility_lookback_hours_ * lookback_multiplier;
            new_lookback = std::max(0.25, std::min(4.0, new_lookback));  // Clamp 15min to 4h

            if (std::abs(new_lookback - current_lookback_hours_) > 0.1) {
                current_lookback_hours_ = new_lookback;
                lookback_changes_++;
            }
        }
    }

    void UpdateTrendAdaptiveSpacing() {
        // Self-calibrating trend-adaptive spacing
        // Collects trend samples during warmup, then uses percentiles as thresholds

        // Calculate lookback period in days (hours / 24)
        int lookback_days = std::max(1, (int)(trend_lookback_hours_ / 24.0));

        // Parse timestamp to get period ID (timestamp format: YYYY.MM.DD HH:MM:SS)
        int month = 0, day = 0;
        if (current_timestamp_.length() >= 10) {
            sscanf(current_timestamp_.c_str(), "%*d.%d.%d", &month, &day);
        }
        int day_of_year = (month - 1) * 30 + day;  // Approximate day of year
        int period_id = day_of_year / lookback_days;

        // Initialize on first tick
        if (trend_start_price_ <= 0) {
            trend_start_price_ = current_bid_;
            last_trend_period_id_ = period_id;
            return;
        }

        // At each lookback interval, record the trend and (optionally) reset
        if (period_id != last_trend_period_id_) {
            double trend_pct = (current_bid_ - trend_start_price_) / trend_start_price_ * 100.0;
            double abs_trend = std::abs(trend_pct);

            // Collect trend samples (for analysis/research)
            trend_history_.push_back(abs_trend);

            // Keep only last 120 samples (~1 year with 3-day periods)
            while (trend_history_.size() > 120) {
                trend_history_.erase(trend_history_.begin());
            }

            // NOTE: NOT resetting trend_start_price_ means we measure cumulative
            // year-to-date trend instead of rolling period trend. This performs
            // better in sustained trends (like 2025's bull market) because it
            // keeps tight spacing throughout.
            // To use rolling period trend instead, uncomment:
            // trend_start_price_ = current_bid_;

            last_trend_period_id_ = period_id;
        }

        // Calculate current trend
        double trend_pct = (current_bid_ - trend_start_price_) / trend_start_price_ * 100.0;
        double abs_trend = std::abs(trend_pct);

        // Use calibrated thresholds (or defaults during warmup)
        // thresh_strong_ = p75, thresh_moderate_ = p50, thresh_weak_ = p25
        double new_spacing;
        if (abs_trend >= thresh_strong_) {
            // Strong trend (top 25%): tight spacing
            double trend_factor = std::min(1.0, (abs_trend - thresh_strong_) / thresh_strong_);
            new_spacing = 0.50 - trend_factor * 0.30;
            new_spacing = std::max(0.20, new_spacing);
        } else if (abs_trend >= thresh_moderate_) {
            // Moderate trend (50-75%): medium spacing
            double range = thresh_strong_ - thresh_moderate_;
            double trend_factor = (range > 0) ? (abs_trend - thresh_moderate_) / range : 0;
            new_spacing = 1.50 - trend_factor * 1.0;
        } else if (abs_trend >= thresh_weak_) {
            // Weak trend (25-50%): wide spacing
            double range = thresh_moderate_ - thresh_weak_;
            double trend_factor = (range > 0) ? (abs_trend - thresh_weak_) / range : 0;
            new_spacing = 3.00 - trend_factor * 1.5;
        } else {
            // Flat market (bottom 25%): widest spacing
            new_spacing = 5.0;
        }

        // Update spacing if changed significantly
        if (std::abs(new_spacing - current_spacing_) > 0.05) {
            current_spacing_ = new_spacing;
            trend_spacing_changes_++;
        }
    }

    void CalibrateThresholds() {
        // Calculate percentiles from collected trend data
        std::vector<double> sorted = trend_history_;
        std::sort(sorted.begin(), sorted.end());

        size_t n = sorted.size();
        if (n < 4) return;  // Need at least 4 samples

        // Calculate percentiles: p25, p50, p75
        thresh_weak_ = sorted[n * 25 / 100];      // 25th percentile
        thresh_moderate_ = sorted[n * 50 / 100];  // 50th percentile (median)
        thresh_strong_ = sorted[n * 75 / 100];    // 75th percentile

        // Ensure minimum separation
        if (thresh_moderate_ <= thresh_weak_) thresh_moderate_ = thresh_weak_ + 0.1;
        if (thresh_strong_ <= thresh_moderate_) thresh_strong_ = thresh_moderate_ + 0.1;

        calibration_complete_ = true;
    }

    double GetAntifragileMultiplier() {
        if (mode_ != ANTIFRAGILE && mode_ != ALL_COMBINED) {
            return 1.0;
        }

        // Calculate current drawdown
        double dd_pct = 0.0;
        if (peak_equity_ > 0) {
            dd_pct = (peak_equity_ - current_equity_) / peak_equity_ * 100.0;
        }

        // Increase position size during drawdown
        // At 5% DD: 1.1x, at 10% DD: 1.2x, etc.
        double multiplier = 1.0 + (dd_pct / 5.0) * antifragile_scale_;
        return std::min(2.0, multiplier);  // Cap at 2x
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
            ? current_ask_ * ((100.0 - survive_pct_) / 100.0)
            : highest_buy_ * ((100.0 - survive_pct_) / 100.0);

        double distance = current_ask_ - end_price;
        double number_of_trades = std::floor(distance / current_spacing_);
        if (number_of_trades <= 0) number_of_trades = 1;

        // Calculate base lot size
        double equity_at_target = current_equity_ - volume_of_open_trades_ * distance * cfg.contract_size;
        if (used_margin > 0 && (equity_at_target / used_margin * 100.0) < margin_stop_out) {
            return 0.0;
        }

        double trade_size = min_volume_;
        double d_equity = cfg.contract_size * trade_size * current_spacing_ * (number_of_trades * (number_of_trades + 1) / 2);
        double d_margin = engine.CalculateMarginRequired(trade_size, current_ask_) * number_of_trades;

        // Find multiplier using binary search (O(log N) instead of O(N))
        double max_mult = max_volume_ / min_volume_;
        double low = 1.0, high = max_mult;
        double best_mult = 1.0;  // Default to minimum if nothing works

        // Binary search for largest valid multiplier
        while (high - low > 0.05) {  // Precision to 0.05
            double mid = (low + high) / 2.0;
            double test_equity = equity_at_target - mid * d_equity;
            double test_margin = used_margin + mid * d_margin;
            if (test_margin > 0 && (test_equity / test_margin * 100.0) > margin_stop_out) {
                best_mult = mid;
                low = mid;  // Try larger multipliers
            } else {
                high = mid;  // Need smaller multipliers
            }
        }
        trade_size = best_mult * min_volume_;

        // Apply anti-fragile scaling
        trade_size *= GetAntifragileMultiplier();

        return std::min(trade_size, max_volume_);
    }

    bool Open(double lots, TickBasedEngine& engine) {
        // Forced entry: if lot sizing returns 0 but force is enabled, use min_volume
        // This "keeps the grid alive" and captures rebounds during margin stress
        if (lots < min_volume_) {
            if (safety_config_.force_min_volume_entry) {
                lots = min_volume_;
                stats_.forced_entries++;
            } else {
                return false;
            }
        }

        double final_lots = engine.NormalizeLots(std::min(lots, max_volume_));

        double tp = current_ask_ + current_spread_ + current_spacing_;
        Trade* trade = engine.OpenMarketOrder(TradeDirection::BUY, final_lots, 0.0, tp);
        return (trade != nullptr);
    }

    void OpenNew(TickBasedEngine& engine) {
        int positions_total = (int)engine.GetOpenPositions().size();

        // Track peak positions
        if (positions_total > stats_.peak_positions) {
            stats_.peak_positions = positions_total;
        }

        // Safety: max position cap (alternative to blocking based on lot sizing)
        if (safety_config_.max_positions > 0 && positions_total >= safety_config_.max_positions) {
            stats_.max_position_blocks++;
            return;
        }

        // Safety: margin level floor
        if (safety_config_.margin_level_floor > 0) {
            double margin_level = engine.GetMarginLevel();
            if (margin_level > 0 && margin_level < safety_config_.margin_level_floor) {
                stats_.margin_level_blocks++;
                return;
            }
        }

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

#endif // FILL_UP_OSCILLATION_H
