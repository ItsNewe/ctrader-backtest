#ifndef POSITION_VALIDATOR_H
#define POSITION_VALIDATOR_H

#include <string>
#include <cmath>

/**
 * PositionValidator - Validate position parameters against broker rules
 *
 * Validates:
 * - Lot size within min/max/step limits
 * - Stop loss / Take profit minimum distance
 * - Margin requirements
 */
class PositionValidator {
public:
    /**
     * Validate lot size against symbol specifications
     *
     * @param lot_size Requested lot size
     * @param volume_min Minimum lot size (e.g., 0.01)
     * @param volume_max Maximum lot size (e.g., 100.0)
     * @param volume_step Lot size step (e.g., 0.01)
     * @param error_message Output error message if validation fails
     * @return true if valid, false otherwise
     */
    static bool ValidateLotSize(
        double lot_size,
        double volume_min,
        double volume_max,
        double volume_step,
        std::string* error_message = nullptr
    ) {
        // Check minimum
        if (lot_size < volume_min) {
            if (error_message) {
                *error_message = "Lot size " + std::to_string(lot_size) +
                               " is below minimum " + std::to_string(volume_min);
            }
            return false;
        }

        // Check maximum
        if (lot_size > volume_max) {
            if (error_message) {
                *error_message = "Lot size " + std::to_string(lot_size) +
                               " exceeds maximum " + std::to_string(volume_max);
            }
            return false;
        }

        // Check step (lot size must be multiple of step)
        if (volume_step > 0) {
            double remainder = fmod(lot_size - volume_min, volume_step);
            // Use relative tolerance based on step size
            double tolerance = volume_step * 0.01;  // 1% of step
            if (remainder > tolerance && (volume_step - remainder) > tolerance) {
                if (error_message) {
                    *error_message = "Lot size " + std::to_string(lot_size) +
                                   " is not a valid multiple of step " +
                                   std::to_string(volume_step);
                }
                return false;
            }
        }

        return true;
    }

    /**
     * Normalize lot size to nearest valid value
     *
     * @param lot_size Requested lot size
     * @param volume_min Minimum lot size
     * @param volume_max Maximum lot size
     * @param volume_step Lot size step
     * @return Normalized lot size within valid range
     */
    static double NormalizeLotSize(
        double lot_size,
        double volume_min,
        double volume_max,
        double volume_step
    ) {
        // Clamp to min/max first
        if (lot_size < volume_min) {
            return volume_min;
        }
        if (lot_size > volume_max) {
            return volume_max;
        }

        // Round to nearest step
        if (volume_step > 0) {
            // Calculate number of steps from minimum
            double steps_from_min = (lot_size - volume_min) / volume_step;
            double rounded_steps = round(steps_from_min);
            double normalized = volume_min + (rounded_steps * volume_step);

            // Clamp again after rounding (in case rounding pushed us over max)
            if (normalized > volume_max) {
                normalized = volume_max;
            }
            if (normalized < volume_min) {
                normalized = volume_min;
            }

            return normalized;
        }

        return lot_size;
    }

    /**
     * Validate stop loss / take profit distance
     *
     * @param entry_price Position entry price
     * @param sl_tp_price Stop loss or take profit price
     * @param is_buy True for long position, false for short
     * @param stops_level Minimum distance in points
     * @param point_value Point value (e.g., 0.00001)
     * @param error_message Output error message if validation fails
     * @return true if valid, false otherwise
     */
    static bool ValidateStopDistance(
        double entry_price,
        double sl_tp_price,
        bool is_buy,
        int stops_level,
        double point_value,
        std::string* error_message = nullptr
    ) {
        if (sl_tp_price == 0) {
            return true;  // No SL/TP set
        }

        // Calculate distance in points
        double distance_price = fabs(entry_price - sl_tp_price);
        int distance_points = static_cast<int>(round(distance_price / point_value));

        if (distance_points < stops_level) {
            if (error_message) {
                *error_message = "Stop distance " + std::to_string(distance_points) +
                               " points is below minimum " + std::to_string(stops_level);
            }
            return false;
        }

        // Check direction
        if (is_buy) {
            // For buy: SL must be below entry, TP must be above
            // We need to know if sl_tp_price is SL or TP to validate direction correctly here
            // However, the current API doesn't distinguish. 
            // For now, let's just ensure the price is not exactly the entry price
            if (sl_tp_price == entry_price) {
                if (error_message) *error_message = "Stop price cannot be equal to entry price";
                return false;
            }
        }

        return true;
    }

    /**
     * Validate position can be opened with available margin
     *
     * @param required_margin Margin needed for position
     * @param available_margin Free margin in account
     * @param error_message Output error message if validation fails
     * @return true if sufficient margin, false otherwise
     */
    static bool ValidateMargin(
        double required_margin,
        double available_margin,
        std::string* error_message = nullptr
    ) {
        if (required_margin > available_margin) {
            if (error_message) {
                *error_message = "Insufficient margin: need " +
                               std::to_string(required_margin) + ", have " +
                               std::to_string(available_margin);
            }
            return false;
        }
        return true;
    }

    /**
     * Comprehensive position validation
     *
     * @param lot_size Requested lot size
     * @param entry_price Entry price
     * @param stop_loss Stop loss price (0 if none)
     * @param take_profit Take profit price (0 if none)
     * @param is_buy True for long, false for short
     * @param required_margin Margin needed
     * @param available_margin Free margin available
     * @param volume_min Minimum lot size
     * @param volume_max Maximum lot size
     * @param volume_step Lot size step
     * @param stops_level Minimum SL/TP distance in points
     * @param point_value Point value
     * @param error_message Output error message if validation fails
     * @return true if all validations pass, false otherwise
     */
    static bool ValidatePosition(
        double lot_size,
        double entry_price,
        double stop_loss,
        double take_profit,
        bool is_buy,
        double required_margin,
        double available_margin,
        double volume_min,
        double volume_max,
        double volume_step,
        int stops_level,
        double point_value,
        std::string* error_message = nullptr
    ) {
        // Validate lot size
        if (!ValidateLotSize(lot_size, volume_min, volume_max, volume_step, error_message)) {
            return false;
        }

        // Validate stop loss distance and direction
        if (stop_loss != 0) {
            if (!ValidateStopDistance(entry_price, stop_loss, is_buy, stops_level,
                                     point_value, error_message)) {
                return false;
            }
            // Check SL direction
            if (is_buy && stop_loss >= entry_price) {
                if (error_message) *error_message = "Buy SL must be below entry price";
                return false;
            }
            if (!is_buy && stop_loss <= entry_price) {
                if (error_message) *error_message = "Sell SL must be above entry price";
                return false;
            }
        }

        // Validate take profit distance and direction
        if (take_profit != 0) {
            if (!ValidateStopDistance(entry_price, take_profit, !is_buy, stops_level,
                                     point_value, error_message)) {
                return false;
            }
            // Check TP direction
            if (is_buy && take_profit <= entry_price) {
                if (error_message) *error_message = "Buy TP must be above entry price";
                return false;
            }
            if (!is_buy && take_profit >= entry_price) {
                if (error_message) *error_message = "Sell TP must be below entry price";
                return false;
            }
        }

        // Validate margin
        if (!ValidateMargin(required_margin, available_margin, error_message)) {
            return false;
        }

        return true;
    }
};

#endif // POSITION_VALIDATOR_H
