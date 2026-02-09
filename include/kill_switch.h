#ifndef KILL_SWITCH_H
#define KILL_SWITCH_H

#include <deque>
#include <string>
#include <cmath>
#include <algorithm>

namespace backtest {

/**
 * KillSwitch - Multi-level protection for grid trading strategies
 *
 * Implements graduated response to adverse market conditions:
 * - Level 0: NORMAL - Full trading
 * - Level 1: CAUTION - Reduce position size by 50%
 * - Level 2: PAUSE - No new entries, keep existing positions
 * - Level 3: EMERGENCY - Close all positions immediately
 *
 * Detection methods:
 * 1. Consecutive down moves (hourly price changes)
 * 2. Drawdown from peak equity
 * 3. Daily loss percentage
 * 4. Margin level monitoring
 * 5. Price velocity (crash detection)
 */
class KillSwitch {
public:
    enum Level {
        NORMAL = 0,      // Full trading
        CAUTION = 1,     // Reduce size 50%
        PAUSE = 2,       // No new entries
        EMERGENCY = 3    // Close all positions
    };

    struct Config {
        // Consecutive down move detection
        double hourly_down_threshold_pct = 0.5;   // What counts as a "down" hour
        int pause_consecutive_down = 3;           // Pause after N consecutive down hours
        int emergency_consecutive_down = 5;       // Emergency after N consecutive down hours

        // Drawdown thresholds
        double caution_dd_pct = 15.0;             // Enter caution at 15% DD
        double pause_dd_pct = 25.0;               // Pause at 25% DD
        double emergency_dd_pct = 35.0;           // Emergency at 35% DD

        // Daily loss thresholds
        double pause_daily_loss_pct = 10.0;       // Pause after 10% daily loss
        double emergency_daily_loss_pct = 15.0;   // Emergency after 15% daily loss

        // Margin level thresholds
        double caution_margin_level = 150.0;      // Caution below 150% margin
        double pause_margin_level = 100.0;        // Pause below 100% margin
        double emergency_margin_level = 50.0;     // Emergency below 50% margin

        // Price velocity (crash detection)
        double crash_velocity_pct_hour = 2.0;     // 2% drop in 1 hour = crash
        double emergency_velocity_pct_hour = 3.0; // 3% drop in 1 hour = emergency

        // Recovery requirements
        double recovery_pct = 30.0;               // Must recover 30% of losses to downgrade level

        // Confirmation/delay requirements (NEW)
        int dd_confirmation_hours = 2;            // DD must persist N hours before triggering
        bool use_smoothed_dd = false;             // Use exponential moving average of DD
        double dd_smoothing_alpha = 0.1;          // Smoothing factor (0.1 = slow, 0.9 = fast)
    };

    struct State {
        Level current_level = NORMAL;
        int consecutive_down_hours = 0;
        int consecutive_up_hours = 0;
        double peak_equity = 0.0;
        double trough_equity = 0.0;
        double day_start_equity = 0.0;
        double current_equity = 0.0;
        double current_dd_pct = 0.0;
        double current_daily_loss_pct = 0.0;
        double current_margin_level = 10000.0;
        double hourly_velocity_pct = 0.0;
        double last_hour_price = 0.0;
        long ticks_since_hour = 0;
        int day_count = 0;
        std::string trigger_reason;

        // Recovery tracking
        double level_entry_equity = 0.0;         // Equity when level was entered
        double level_entry_dd = 0.0;             // DD when level was entered

        // Smoothed DD tracking (NEW)
        double smoothed_dd_pct = 0.0;            // Exponential moving average of DD
        int hours_above_pause_threshold = 0;     // Hours DD has exceeded pause threshold
        int hours_above_emergency_threshold = 0; // Hours DD has exceeded emergency threshold
    };

    KillSwitch() : config_(), state_() {}

    explicit KillSwitch(const Config& config) : config_(config), state_() {}

    /**
     * Update kill switch state with current market data
     * Returns the current protection level
     */
    Level Update(double current_price, double current_equity,
                 double used_margin, long tick_count) {

        // Initialize on first call
        if (state_.peak_equity == 0.0) {
            state_.peak_equity = current_equity;
            state_.trough_equity = current_equity;
            state_.day_start_equity = current_equity;
            state_.current_equity = current_equity;
            state_.last_hour_price = current_price;
            state_.level_entry_equity = current_equity;
            return state_.current_level;
        }

        state_.current_equity = current_equity;

        // Update peak/trough
        if (current_equity > state_.peak_equity) {
            state_.peak_equity = current_equity;
        }
        if (current_equity < state_.trough_equity) {
            state_.trough_equity = current_equity;
        }

        // Calculate drawdown
        state_.current_dd_pct = 0.0;
        if (state_.peak_equity > 0) {
            state_.current_dd_pct = (state_.peak_equity - current_equity) / state_.peak_equity * 100.0;
        }

        // Update smoothed DD (exponential moving average)
        if (config_.use_smoothed_dd) {
            state_.smoothed_dd_pct = config_.dd_smoothing_alpha * state_.current_dd_pct +
                                     (1.0 - config_.dd_smoothing_alpha) * state_.smoothed_dd_pct;
        } else {
            state_.smoothed_dd_pct = state_.current_dd_pct;
        }

        // Calculate daily loss
        state_.current_daily_loss_pct = 0.0;
        if (state_.day_start_equity > 0) {
            state_.current_daily_loss_pct = (state_.day_start_equity - current_equity) / state_.day_start_equity * 100.0;
        }

        // Calculate margin level
        state_.current_margin_level = (used_margin > 0) ? (current_equity / used_margin * 100.0) : 10000.0;

        // Hourly price velocity check (approximate: 720000 ticks/hour)
        const long TICKS_PER_HOUR = 720000;
        state_.ticks_since_hour++;

        if (state_.ticks_since_hour >= TICKS_PER_HOUR) {
            // New hour - calculate velocity and update consecutive counts
            if (state_.last_hour_price > 0) {
                double price_change_pct = (current_price - state_.last_hour_price) / state_.last_hour_price * 100.0;
                state_.hourly_velocity_pct = -price_change_pct;  // Positive = down move

                // Track consecutive moves
                if (price_change_pct < -config_.hourly_down_threshold_pct) {
                    state_.consecutive_down_hours++;
                    state_.consecutive_up_hours = 0;
                } else if (price_change_pct > config_.hourly_down_threshold_pct) {
                    state_.consecutive_up_hours++;
                    state_.consecutive_down_hours = 0;
                } else {
                    // Sideways - reset both
                    state_.consecutive_down_hours = 0;
                    state_.consecutive_up_hours = 0;
                }
            }

            // Track DD confirmation hours (NEW)
            double dd_to_check = config_.use_smoothed_dd ? state_.smoothed_dd_pct : state_.current_dd_pct;
            if (dd_to_check >= config_.pause_dd_pct) {
                state_.hours_above_pause_threshold++;
            } else {
                state_.hours_above_pause_threshold = 0;
            }
            if (dd_to_check >= config_.emergency_dd_pct) {
                state_.hours_above_emergency_threshold++;
            } else {
                state_.hours_above_emergency_threshold = 0;
            }

            state_.last_hour_price = current_price;
            state_.ticks_since_hour = 0;
        }

        // Daily reset check (approximate: every 24 hours)
        const long TICKS_PER_DAY = TICKS_PER_HOUR * 24;
        if (tick_count > 0 && tick_count % TICKS_PER_DAY == 0) {
            state_.day_start_equity = current_equity;
            state_.day_count++;
        }

        // Evaluate protection level
        Level new_level = EvaluateLevel();

        // Check for level changes
        if (new_level != state_.current_level) {
            // Going up in protection (worse conditions)
            if (new_level > state_.current_level) {
                state_.level_entry_equity = current_equity;
                state_.level_entry_dd = state_.current_dd_pct;
                state_.current_level = new_level;
            }
            // Going down in protection (recovery) - requires recovery threshold
            else if (CanDowngrade(new_level)) {
                state_.current_level = new_level;
                state_.level_entry_equity = current_equity;
                state_.level_entry_dd = state_.current_dd_pct;
            }
        }

        return state_.current_level;
    }

    /**
     * Reset for new day (call at market open)
     */
    void ResetDaily(double current_equity) {
        state_.day_start_equity = current_equity;
        state_.day_count++;
    }

    /**
     * Full reset (call when starting new backtest)
     */
    void Reset() {
        state_ = State();
    }

    /**
     * Check if trading is allowed at current level
     */
    bool CanTrade() const {
        return state_.current_level < PAUSE;
    }

    /**
     * Check if should close all positions
     */
    bool ShouldCloseAll() const {
        return state_.current_level >= EMERGENCY;
    }

    /**
     * Get position size multiplier based on current level
     */
    double GetSizeMultiplier() const {
        switch (state_.current_level) {
            case NORMAL: return 1.0;
            case CAUTION: return 0.5;
            case PAUSE: return 0.0;
            case EMERGENCY: return 0.0;
            default: return 1.0;
        }
    }

    /**
     * Get human-readable level name
     */
    static std::string LevelToString(Level level) {
        switch (level) {
            case NORMAL: return "NORMAL";
            case CAUTION: return "CAUTION";
            case PAUSE: return "PAUSE";
            case EMERGENCY: return "EMERGENCY";
            default: return "UNKNOWN";
        }
    }

    // Accessors
    const State& GetState() const { return state_; }
    const Config& GetConfig() const { return config_; }
    void SetConfig(const Config& config) { config_ = config; }

private:
    Config config_;
    State state_;

    Level EvaluateLevel() {
        Level max_level = NORMAL;
        state_.trigger_reason = "";

        // Use smoothed DD if configured, otherwise raw DD
        double dd_to_check = config_.use_smoothed_dd ? state_.smoothed_dd_pct : state_.current_dd_pct;
        bool require_dd_confirmation = config_.dd_confirmation_hours > 0;

        // Check emergency conditions first (most severe)

        // Emergency: 5+ consecutive down hours
        if (state_.consecutive_down_hours >= config_.emergency_consecutive_down) {
            max_level = std::max(max_level, EMERGENCY);
            state_.trigger_reason = "5+ consecutive down hours";
        }

        // Emergency: Daily loss > threshold
        if (state_.current_daily_loss_pct >= config_.emergency_daily_loss_pct) {
            max_level = std::max(max_level, EMERGENCY);
            state_.trigger_reason = "Daily loss > " + std::to_string((int)config_.emergency_daily_loss_pct) + "%";
        }

        // Emergency: DD > threshold (with optional confirmation)
        if (require_dd_confirmation) {
            // Only trigger if DD has been above threshold for N hours
            if (state_.hours_above_emergency_threshold >= config_.dd_confirmation_hours) {
                max_level = std::max(max_level, EMERGENCY);
                state_.trigger_reason = "DD > " + std::to_string((int)config_.emergency_dd_pct) + "% for " +
                                        std::to_string(config_.dd_confirmation_hours) + "h";
            }
        } else {
            // Immediate trigger
            if (dd_to_check >= config_.emergency_dd_pct) {
                max_level = std::max(max_level, EMERGENCY);
                state_.trigger_reason = "DD > " + std::to_string((int)config_.emergency_dd_pct) + "%";
            }
        }

        // Emergency: Margin level < threshold
        if (state_.current_margin_level < config_.emergency_margin_level) {
            max_level = std::max(max_level, EMERGENCY);
            state_.trigger_reason = "Margin < " + std::to_string((int)config_.emergency_margin_level) + "%";
        }

        // Emergency: Crash velocity > threshold
        if (state_.hourly_velocity_pct >= config_.emergency_velocity_pct_hour) {
            max_level = std::max(max_level, EMERGENCY);
            state_.trigger_reason = "Crash velocity > " + std::to_string((int)config_.emergency_velocity_pct_hour) + "%/h";
        }

        // Check pause conditions

        // Pause: 3+ consecutive down hours
        if (state_.consecutive_down_hours >= config_.pause_consecutive_down) {
            max_level = std::max(max_level, PAUSE);
            if (state_.trigger_reason.empty()) state_.trigger_reason = "3+ consecutive down hours";
        }

        // Pause: Daily loss > 10%
        if (state_.current_daily_loss_pct >= config_.pause_daily_loss_pct) {
            max_level = std::max(max_level, PAUSE);
            if (state_.trigger_reason.empty()) state_.trigger_reason = "Daily loss > " + std::to_string((int)config_.pause_daily_loss_pct) + "%";
        }

        // Pause: DD > threshold (with optional confirmation)
        if (require_dd_confirmation) {
            // Only trigger if DD has been above threshold for N hours
            if (state_.hours_above_pause_threshold >= config_.dd_confirmation_hours) {
                max_level = std::max(max_level, PAUSE);
                if (state_.trigger_reason.empty()) {
                    state_.trigger_reason = "DD > " + std::to_string((int)config_.pause_dd_pct) + "% for " +
                                            std::to_string(config_.dd_confirmation_hours) + "h";
                }
            }
        } else {
            // Immediate trigger
            if (dd_to_check >= config_.pause_dd_pct) {
                max_level = std::max(max_level, PAUSE);
                if (state_.trigger_reason.empty()) state_.trigger_reason = "DD > " + std::to_string((int)config_.pause_dd_pct) + "%";
            }
        }

        // Pause: Margin level < 100%
        if (state_.current_margin_level < config_.pause_margin_level) {
            max_level = std::max(max_level, PAUSE);
            if (state_.trigger_reason.empty()) state_.trigger_reason = "Margin < " + std::to_string((int)config_.pause_margin_level) + "%";
        }

        // Pause: Crash velocity > 2%/hour
        if (state_.hourly_velocity_pct >= config_.crash_velocity_pct_hour) {
            max_level = std::max(max_level, PAUSE);
            if (state_.trigger_reason.empty()) state_.trigger_reason = "Crash velocity > " + std::to_string((int)config_.crash_velocity_pct_hour) + "%/h";
        }

        // Check caution conditions

        // Caution: DD > threshold (immediate, uses smoothed DD if configured)
        if (dd_to_check >= config_.caution_dd_pct) {
            max_level = std::max(max_level, CAUTION);
            if (state_.trigger_reason.empty()) state_.trigger_reason = "DD > " + std::to_string((int)config_.caution_dd_pct) + "%";
        }

        // Caution: Margin level < 150%
        if (state_.current_margin_level < config_.caution_margin_level) {
            max_level = std::max(max_level, CAUTION);
            if (state_.trigger_reason.empty()) state_.trigger_reason = "Margin < " + std::to_string((int)config_.caution_margin_level) + "%";
        }

        return max_level;
    }

    bool CanDowngrade(Level target_level) {
        // Require recovery before downgrading protection level
        // Recovery = equity increased by X% from when level was entered

        if (state_.level_entry_equity <= 0) return true;

        double recovery_pct = (state_.current_equity - state_.level_entry_equity) / state_.level_entry_equity * 100.0;

        // Need positive recovery to downgrade
        // More recovery needed for bigger downgrades
        double required_recovery = config_.recovery_pct * (state_.current_level - target_level);

        // Also require consecutive up hours for full downgrade
        if (target_level == NORMAL && state_.consecutive_up_hours < 2) {
            return false;
        }

        return recovery_pct >= required_recovery;
    }
};

} // namespace backtest

#endif // KILL_SWITCH_H
