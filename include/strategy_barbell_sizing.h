#ifndef STRATEGY_BARBELL_SIZING_H
#define STRATEGY_BARBELL_SIZING_H

#include "tick_based_engine.h"
#include <vector>
#include <deque>
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace backtest {

/**
 * Barbell Sizing Strategy (Taleb's Asymmetry)
 *
 * Instead of uniform lot sizing, uses asymmetric sizing like Taleb's barbell:
 * - Small positions early (near the mean) - SAFE end
 * - Large positions only at deep deviation from mean - SPECULATIVE end
 * - Creates convex payoff: limited early losses, amplified gains from deep reversions
 *
 * Modes:
 *   UNIFORM: Standard sizing (control/baseline)
 *   LINEAR: lot = base_lot * (1 + position_number * scale_factor)
 *   EXPONENTIAL: lot = base_lot * (deviation_ratio ^ exponent)
 *   THRESHOLD: base_lot for first N positions, then base_lot * multiplier
 */
class StrategyBarbellSizing {
public:
    enum SizingMode {
        UNIFORM = 0,      // Control: same lot size for all positions
        LINEAR = 1,       // Lot increases linearly with position count
        EXPONENTIAL = 2,  // Lot increases exponentially with deviation
        THRESHOLD = 3     // Small lots until threshold, then jump
    };

    struct BarbellConfig {
        SizingMode mode;
        double scale_factor;          // LINEAR: multiplier per position (e.g., 0.2 = +20% per position)
        double exponent;              // EXPONENTIAL: power for deviation ratio
        int threshold_position;       // THRESHOLD: after N positions, increase size
        double threshold_multiplier;  // THRESHOLD: how much to multiply after threshold

        BarbellConfig()
            : mode(UNIFORM), scale_factor(0.2), exponent(2.0),
              threshold_position(5), threshold_multiplier(3.0) {}
    };

    // Adaptive spacing configuration (from FillUpOscillation)
    struct AdaptiveConfig {
        double typical_vol_pct;
        double min_spacing_mult;
        double max_spacing_mult;
        double min_spacing_abs;
        double max_spacing_abs;
        double spacing_change_threshold;

        AdaptiveConfig()
            : typical_vol_pct(0.5), min_spacing_mult(0.5), max_spacing_mult(3.0),
              min_spacing_abs(0.5), max_spacing_abs(5.0), spacing_change_threshold(0.1) {}
    };

    StrategyBarbellSizing(double survive_pct, double base_spacing,
                          double min_volume, double max_volume,
                          double contract_size, double leverage,
                          double volatility_lookback_hours = 4.0,
                          BarbellConfig barbell_config = BarbellConfig(),
                          AdaptiveConfig adaptive_config = AdaptiveConfig())
        : survive_pct_(survive_pct),
          base_spacing_(base_spacing),
          min_volume_(min_volume),
          max_volume_(max_volume),
          contract_size_(contract_size),
          leverage_(leverage),
          volatility_lookback_hours_(volatility_lookback_hours),
          barbell_config_(barbell_config),
          adaptive_config_(adaptive_config),
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
          first_entry_price_(0.0),
          position_count_(0),
          max_position_count_(0),
          total_lot_opened_(0.0),
          total_trades_opened_(0),
          ticks_processed_(0),
          adaptive_spacing_changes_(0),
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
        }

        // Update peak
        if (current_equity_ > peak_equity_) {
            peak_equity_ = current_equity_;
        }

        // Track volatility
        UpdateVolatility(tick);

        // Update adaptive spacing
        UpdateAdaptiveSpacing();

        // Process positions
        Iterate(engine);

        // Open new positions
        OpenNew(engine);
    }

    // Statistics
    double GetCurrentSpacing() const { return current_spacing_; }
    int GetAdaptiveSpacingChanges() const { return adaptive_spacing_changes_; }
    double GetPeakEquity() const { return peak_equity_; }
    int GetMaxPositionCount() const { return max_position_count_; }
    double GetTotalLotOpened() const { return total_lot_opened_; }
    double GetAverageLotSize() const {
        return (total_trades_opened_ > 0) ? total_lot_opened_ / total_trades_opened_ : 0;
    }
    int GetTotalTradesOpened() const { return total_trades_opened_; }

    // Barbell-specific stats
    double GetDeviationPct() const {
        if (first_entry_price_ <= 0 || current_bid_ <= 0) return 0;
        return (first_entry_price_ - current_bid_) / first_entry_price_ * 100.0;
    }

private:
    // Base parameters
    double survive_pct_;
    double base_spacing_;
    double min_volume_;
    double max_volume_;
    double contract_size_;
    double leverage_;
    double volatility_lookback_hours_;

    // Barbell configuration
    BarbellConfig barbell_config_;

    // Adaptive spacing parameters
    AdaptiveConfig adaptive_config_;

    // Position tracking
    double lowest_buy_;
    double highest_buy_;
    double volume_of_open_trades_;
    double first_entry_price_;  // Reference for deviation calculation
    int position_count_;        // Current open positions
    int max_position_count_;    // Max positions reached
    double total_lot_opened_;   // Sum of all lots opened (for avg calculation)
    int total_trades_opened_;   // Total trades opened (for avg calculation)

    // Market state
    double current_ask_;
    double current_bid_;
    double current_spread_;
    double current_equity_;
    double current_balance_;
    double peak_equity_;

    // Adaptive spacing
    double current_spacing_;
    double recent_high_;
    double recent_low_;

    // Statistics
    long ticks_processed_;
    int adaptive_spacing_changes_;
    long last_vol_reset_seconds_;

    // Parse timestamp to seconds
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
        long lookback_seconds = (long)(volatility_lookback_hours_ * 3600.0);

        if (last_vol_reset_seconds_ == 0 || current_seconds - last_vol_reset_seconds_ >= lookback_seconds) {
            recent_high_ = current_bid_;
            recent_low_ = current_bid_;
            last_vol_reset_seconds_ = current_seconds;
        }
        recent_high_ = std::max(recent_high_, current_bid_);
        recent_low_ = std::min(recent_low_, current_bid_);
    }

    void UpdateAdaptiveSpacing() {
        double range = recent_high_ - recent_low_;
        if (range > 0 && recent_high_ > 0 && current_bid_ > 0) {
            double typical_vol = current_bid_ * (adaptive_config_.typical_vol_pct / 100.0);
            double vol_ratio = range / typical_vol;
            vol_ratio = std::max(adaptive_config_.min_spacing_mult,
                                 std::min(adaptive_config_.max_spacing_mult, vol_ratio));

            double new_spacing = base_spacing_ * vol_ratio;
            new_spacing = std::max(adaptive_config_.min_spacing_abs,
                                   std::min(adaptive_config_.max_spacing_abs, new_spacing));

            if (std::abs(new_spacing - current_spacing_) > adaptive_config_.spacing_change_threshold) {
                current_spacing_ = new_spacing;
                adaptive_spacing_changes_++;
            }
        }
    }

    void Iterate(TickBasedEngine& engine) {
        lowest_buy_ = DBL_MAX;
        highest_buy_ = DBL_MIN;
        volume_of_open_trades_ = 0.0;
        position_count_ = 0;

        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->direction == "BUY") {
                volume_of_open_trades_ += trade->lot_size;
                lowest_buy_ = std::min(lowest_buy_, trade->entry_price);
                highest_buy_ = std::max(highest_buy_, trade->entry_price);
                position_count_++;
            }
        }

        if (position_count_ > max_position_count_) {
            max_position_count_ = position_count_;
        }

        // Reset reference when all positions close
        if (position_count_ == 0) {
            first_entry_price_ = 0.0;
        }
    }

    /**
     * Calculate lot size based on barbell mode
     *
     * The key insight: commit lightly early, commit heavily when extended
     */
    double CalculateBarbellLotSize(int positions_total) {
        // Calculate base lot size using standard margin calculation
        double base_lot = CalculateBaseLotSize(positions_total);
        if (base_lot < min_volume_) return 0.0;

        // Apply barbell sizing based on mode
        double final_lot = base_lot;

        switch (barbell_config_.mode) {
            case UNIFORM:
                // Control: no modification
                break;

            case LINEAR: {
                // Lot increases linearly with position count
                // lot = base * (1 + position_number * scale_factor)
                // Position 0: 1.0x, Position 1: 1.2x, Position 2: 1.4x, etc. (at scale=0.2)
                double multiplier = 1.0 + positions_total * barbell_config_.scale_factor;
                final_lot = base_lot * multiplier;
                break;
            }

            case EXPONENTIAL: {
                // Lot increases exponentially with deviation from first entry
                // lot = base * (deviation_ratio ^ exponent)
                // deviation_ratio = (survive_pct - current_deviation) / survive_pct
                // When at 0% deviation: ratio = 1.0, lot = base
                // When at 50% of survive: ratio = 0.5, lot = base * 0.5^exp (smaller)
                // Wait, we want LARGER when deeper...
                // So: ratio = 1 + (current_deviation / survive_pct)
                // At 0% deviation: ratio = 1.0
                // At survive/2: ratio = 1.5
                // At survive: ratio = 2.0
                if (first_entry_price_ > 0) {
                    double deviation = (first_entry_price_ - current_ask_) / first_entry_price_ * 100.0;
                    if (deviation > 0) {
                        double deviation_ratio = 1.0 + (deviation / survive_pct_);
                        double multiplier = std::pow(deviation_ratio, barbell_config_.exponent);
                        multiplier = std::min(multiplier, 10.0);  // Cap at 10x
                        final_lot = base_lot * multiplier;
                    }
                }
                break;
            }

            case THRESHOLD: {
                // Small lots for first N positions, then larger lots
                // This is the purest "barbell" - safe at one end, speculative at the other
                if (positions_total >= barbell_config_.threshold_position) {
                    final_lot = base_lot * barbell_config_.threshold_multiplier;
                }
                break;
            }
        }

        // Clamp to min/max
        final_lot = std::max(min_volume_, std::min(max_volume_, final_lot));
        return final_lot;
    }

    double CalculateBaseLotSize(int positions_total) {
        // Standard margin-aware lot sizing (from FillUpOscillation)
        double used_margin = 0.0;
        for (const Trade* trade : engine_ptr_->GetOpenPositions()) {
            used_margin += trade->lot_size * contract_size_ * trade->entry_price / leverage_;
        }

        double margin_stop_out = 20.0;

        double end_price = (positions_total == 0)
            ? current_ask_ * ((100.0 - survive_pct_) / 100.0)
            : highest_buy_ * ((100.0 - survive_pct_) / 100.0);

        double distance = current_ask_ - end_price;
        double number_of_trades = std::floor(distance / current_spacing_);
        if (number_of_trades <= 0) number_of_trades = 1;

        double equity_at_target = current_equity_ - volume_of_open_trades_ * distance * contract_size_;
        if (used_margin > 0 && (equity_at_target / used_margin * 100.0) < margin_stop_out) {
            return 0.0;
        }

        double trade_size = min_volume_;
        double d_equity = contract_size_ * trade_size * current_spacing_ * (number_of_trades * (number_of_trades + 1) / 2);
        double d_margin = number_of_trades * trade_size * contract_size_ / leverage_;

        double max_mult = max_volume_ / min_volume_;
        for (double mult = max_mult; mult >= 1.0; mult -= 0.1) {
            double test_equity = equity_at_target - mult * d_equity;
            double test_margin = used_margin + mult * d_margin;
            if (test_margin > 0 && (test_equity / test_margin * 100.0) > margin_stop_out) {
                trade_size = mult * min_volume_;
                break;
            }
        }

        return std::min(trade_size, max_volume_);
    }

    // Store engine pointer for margin calculations
    TickBasedEngine* engine_ptr_;

    bool Open(double lots, TickBasedEngine& engine) {
        if (lots < min_volume_) return false;

        double final_lots = std::min(lots, max_volume_);
        final_lots = std::round(final_lots * 100.0) / 100.0;

        double tp = current_ask_ + current_spread_ + current_spacing_;
        Trade* trade = engine.OpenMarketOrder("BUY", final_lots, 0.0, tp);

        if (trade != nullptr) {
            total_lot_opened_ += final_lots;
            total_trades_opened_++;
            return true;
        }
        return false;
    }

    void OpenNew(TickBasedEngine& engine) {
        engine_ptr_ = &engine;  // Store for margin calculations
        int positions_total = engine.GetOpenPositions().size();

        if (positions_total == 0) {
            double lots = CalculateBarbellLotSize(positions_total);
            if (Open(lots, engine)) {
                highest_buy_ = current_ask_;
                lowest_buy_ = current_ask_;
                first_entry_price_ = current_ask_;  // Set reference for deviation
            }
        } else {
            if (lowest_buy_ >= current_ask_ + current_spacing_) {
                double lots = CalculateBarbellLotSize(positions_total);
                if (Open(lots, engine)) {
                    lowest_buy_ = current_ask_;
                }
            } else if (highest_buy_ <= current_ask_ - current_spacing_) {
                double lots = CalculateBarbellLotSize(positions_total);
                if (Open(lots, engine)) {
                    highest_buy_ = current_ask_;
                }
            }
        }
    }
};

} // namespace backtest

#endif // STRATEGY_BARBELL_SIZING_H
