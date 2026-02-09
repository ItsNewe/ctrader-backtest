#ifndef SWAP_MANAGER_H
#define SWAP_MANAGER_H

#include <ctime>

/**
 * SwapManager - MT5-Validated Swap Application
 *
 * Validated against MT5 Test E results (2026-01-07)
 * - Swap applied at 00:00-00:02 server time
 * - Applied once per day
 * - Detected on Tuesday and Wednesday in test
 *
 * Test Results:
 * - 2025.12.02 (Tuesday) 00:02: -$0.09 swap
 * - 2025.12.03 (Wednesday) 00:02: -$0.09 swap
 *
 * Now supports broker-specific triple-swap day configuration
 */

class SwapManager {
private:
    int swap_hour_;               // Hour when swap is applied (typically 0 = midnight)
    time_t last_swap_day_;        // Track which day swap was last applied
    int triple_swap_day_;         // Day for triple swap (0=Sun, 3=Wed, 5=Fri)

public:
    /**
     * Constructor
     * @param swap_hour Hour of day for swap application (0-23, default: 0 for midnight)
     * @param triple_swap_day Day of week for triple swap (0-6, default: 3=Wednesday)
     */
    explicit SwapManager(int swap_hour = 0, int triple_swap_day = 3)
        : swap_hour_(swap_hour), last_swap_day_(0), triple_swap_day_(triple_swap_day) {}

    /**
     * Check if swap should be applied at current time
     * Call this on every bar/tick in your main loop
     *
     * @param current_time Current server time (unix timestamp)
     * @return true if swap should be applied now, false otherwise
     */
    bool ShouldApplySwap(time_t current_time) {
        struct tm* timeinfo = gmtime(&current_time);

        // Check if we've crossed the swap hour
        if (timeinfo->tm_hour >= swap_hour_) {
            // Calculate day number (days since epoch)
            time_t current_day = current_time / 86400;

            // Check if it's a different day than last swap
            if (current_day != last_swap_day_) {
                last_swap_day_ = current_day;
                return true;
            }
        }

        return false;
    }

    /**
     * Calculate swap charge/credit for a position
     *
     * @param lot_size Position size in lots
     * @param is_buy True for long position, false for short
     * @param swap_long Swap rate for long positions (from symbol specification)
     * @param swap_short Swap rate for short positions (from symbol specification)
     * @param point_value Value of one point for the symbol
     * @param contract_size Standard lot size (typically 100,000)
     * @param day_of_week Day of week (0=Sunday, 3=Wednesday)
     * @param triple_swap_day Day for triple swap (default: 3=Wednesday, can be 5=Friday)
     * @return Swap amount in account currency (negative = charge, positive = credit)
     */
    static double CalculateSwap(
        double lot_size,
        bool is_buy,
        double swap_long,
        double swap_short,
        double point_value,
        double contract_size = 100000.0,
        int day_of_week = -1,
        int triple_swap_day = 3
    ) {
        // Select appropriate swap rate
        double swap_points = is_buy ? swap_long : swap_short;

        // Triple swap on specified day (to account for weekend)
        // Most brokers: Wednesday (3), Some CFDs: Friday (5)
        if (day_of_week == triple_swap_day) {
            swap_points *= 3.0;
        }

        // Calculate swap amount
        // Formula: lot_size × contract_size × swap_points × point_value
        return lot_size * contract_size * swap_points * point_value;
    }

    /**
     * Instance method to calculate swap using manager's triple_swap_day setting
     */
    double CalculateSwapForPosition(
        double lot_size,
        bool is_buy,
        double swap_long,
        double swap_short,
        double point_value,
        double contract_size,
        int day_of_week
    ) const {
        return CalculateSwap(lot_size, is_buy, swap_long, swap_short,
                           point_value, contract_size, day_of_week, triple_swap_day_);
    }

    /**
     * Alternative swap calculation using currency conversion
     * For cross-currency pairs where direct calculation is needed
     *
     * @param lot_size Position size in lots
     * @param is_buy True for long, false for short
     * @param swap_long_pips Swap in pips for long positions
     * @param swap_short_pips Swap in pips for short positions
     * @param pip_value Value of 1 pip in account currency
     * @param day_of_week Day of week (0=Sunday, 3=Wednesday)
     * @return Swap amount in account currency
     */
    static double CalculateSwapSimple(
        double lot_size,
        bool is_buy,
        double swap_long_pips,
        double swap_short_pips,
        double pip_value,
        int day_of_week = -1
    ) {
        // Select swap rate in pips
        double swap_pips = is_buy ? swap_long_pips : swap_short_pips;

        // Triple swap on Wednesday
        if (day_of_week == 3) {
            swap_pips *= 3.0;
        }

        // Convert to account currency
        return lot_size * swap_pips * pip_value;
    }

    /**
     * Get day of week from unix timestamp
     *
     * @param timestamp Unix timestamp
     * @return Day of week (0=Sunday, 1=Monday, ..., 6=Saturday)
     */
    static int GetDayOfWeek(time_t timestamp) {
        struct tm* timeinfo = gmtime(&timestamp);
        return timeinfo->tm_wday;
    }

    /**
     * Reset swap manager state (useful for backtesting)
     */
    void Reset() {
        last_swap_day_ = 0;
    }

    /**
     * Set the hour when swap is applied
     * @param hour Hour of day (0-23)
     */
    void SetSwapHour(int hour) {
        if (hour >= 0 && hour < 24) {
            swap_hour_ = hour;
        }
    }

    /**
     * Get current swap hour setting
     * @return Swap hour (0-23)
     */
    int GetSwapHour() const {
        return swap_hour_;
    }
};

#endif // SWAP_MANAGER_H
