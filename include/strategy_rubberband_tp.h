#ifndef STRATEGY_RUBBERBAND_TP_H
#define STRATEGY_RUBBERBAND_TP_H

#include "tick_based_engine.h"
#include <vector>
#include <deque>
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace backtest {

/**
 * Rubber Band TP Strategy
 *
 * Like a rubber band - the more stretched, the more it snaps back.
 * Scale TP target with deviation from mean:
 * - Small deviation from mean -> small TP (quick exit, small profit)
 * - Large deviation from mean -> large TP (expect big reversion, larger profit)
 *
 * Current behavior (baseline): Fixed TP = spread + spacing (same for all positions)
 * New behavior: TP scales with how far we are from the "equilibrium" (mean price)
 *
 * TP Modes:
 * - FIXED: TP = spread + spacing (baseline, standard FillUp)
 * - LINEAR: TP = spread + spacing * (1 + deviation/spacing * scale)
 * - SQRT: TP = spread + spacing * (1 + sqrt(deviation/spacing) * scale)
 * - PROPORTIONAL: TP = spread + deviation * reversion_pct
 *
 * Equilibrium Types:
 * - FIRST_ENTRY: Use the first position's entry price as equilibrium
 * - EMA_200: 200-period exponential moving average of price
 * - EMA_500: 500-period exponential moving average of price
 */
class StrategyRubberBandTP {
public:
    // TP scaling modes
    enum TPMode {
        FIXED = 0,        // Baseline: TP = spread + spacing
        LINEAR = 1,       // TP scales linearly with deviation
        SQRT = 2,         // TP scales with sqrt (diminishing returns)
        PROPORTIONAL = 3  // TP = fraction of deviation back toward mean
    };

    // Equilibrium calculation methods
    enum EquilibriumType {
        FIRST_ENTRY = 0,  // First position entry price
        EMA_200 = 1,      // 200-tick EMA
        EMA_500 = 2       // 500-tick EMA
    };

    struct Config {
        // Base strategy parameters
        double survive_pct;
        double base_spacing;
        double min_volume;
        double max_volume;
        double contract_size;
        double leverage;

        // Rubber band TP parameters
        TPMode tp_mode;
        EquilibriumType equilibrium_type;
        double linear_scale;    // For LINEAR mode: multiplier per unit deviation
        double sqrt_scale;      // For SQRT mode: multiplier for sqrt term
        double reversion_pct;   // For PROPORTIONAL: what fraction of deviation to target

        // Volatility-based spacing (from FillUpOscillation ADAPTIVE_SPACING)
        double volatility_lookback_hours;
        double typical_vol_pct;

        Config()
            : survive_pct(13.0),
              base_spacing(1.5),
              min_volume(0.01),
              max_volume(10.0),
              contract_size(100.0),
              leverage(500.0),
              tp_mode(FIXED),
              equilibrium_type(FIRST_ENTRY),
              linear_scale(1.0),
              sqrt_scale(1.0),
              reversion_pct(0.5),
              volatility_lookback_hours(4.0),
              typical_vol_pct(0.5) {}
    };

    explicit StrategyRubberBandTP(const Config& config)
        : config_(config),
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
          ticks_processed_(0),
          adaptive_spacing_changes_(0),
          ema_value_(0.0),
          ema_initialized_(false),
          first_entry_price_(0.0),
          equilibrium_(0.0),
          last_vol_reset_seconds_(0),
          total_deviation_sum_(0.0),
          total_deviation_count_(0),
          total_tp_sum_(0.0)
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

        // Update EMA for equilibrium calculation
        UpdateEMA(tick);

        // Track volatility for adaptive spacing
        UpdateVolatility(tick);

        // Update adaptive spacing
        UpdateAdaptiveSpacing();

        // Process positions - update equilibrium tracking
        Iterate(engine);

        // Open new positions
        OpenNew(engine);
    }

    // Statistics
    double GetCurrentSpacing() const { return current_spacing_; }
    int GetAdaptiveSpacingChanges() const { return adaptive_spacing_changes_; }
    double GetEquilibrium() const { return equilibrium_; }
    double GetAverageDeviation() const {
        return total_deviation_count_ > 0 ? total_deviation_sum_ / total_deviation_count_ : 0.0;
    }
    double GetAverageTP() const {
        return total_deviation_count_ > 0 ? total_tp_sum_ / total_deviation_count_ : 0.0;
    }
    long GetTicksProcessed() const { return ticks_processed_; }

private:
    Config config_;

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
    double recent_high_;
    double recent_low_;

    // Statistics
    long ticks_processed_;
    int adaptive_spacing_changes_;

    // Equilibrium tracking
    double ema_value_;
    bool ema_initialized_;
    double first_entry_price_;
    double equilibrium_;

    // Timestamp-based volatility tracking
    long last_vol_reset_seconds_;

    // TP analysis stats
    double total_deviation_sum_;
    int total_deviation_count_;
    double total_tp_sum_;

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

    void UpdateEMA(const Tick& tick) {
        double price = tick.bid;

        if (!ema_initialized_) {
            ema_value_ = price;
            ema_initialized_ = true;
            return;
        }

        // EMA period based on equilibrium type
        double period = (config_.equilibrium_type == EMA_200) ? 200.0 : 500.0;
        double alpha = 2.0 / (period + 1.0);

        ema_value_ = alpha * price + (1.0 - alpha) * ema_value_;
    }

    double GetEquilibriumPrice() const {
        switch (config_.equilibrium_type) {
            case FIRST_ENTRY:
                return first_entry_price_ > 0 ? first_entry_price_ : current_bid_;
            case EMA_200:
            case EMA_500:
                return ema_value_ > 0 ? ema_value_ : current_bid_;
            default:
                return current_bid_;
        }
    }

    void UpdateVolatility(const Tick& tick) {
        long current_seconds = ParseTimestampToSeconds(tick.timestamp);
        long lookback_seconds = (long)(config_.volatility_lookback_hours * 3600.0);

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
            // Percentage-based typical volatility
            double typical_vol = current_bid_ * (config_.typical_vol_pct / 100.0);
            double vol_ratio = range / typical_vol;
            vol_ratio = std::max(0.5, std::min(3.0, vol_ratio));

            double new_spacing = config_.base_spacing * vol_ratio;
            new_spacing = std::max(0.5, std::min(5.0, new_spacing));

            if (std::abs(new_spacing - current_spacing_) > 0.1) {
                current_spacing_ = new_spacing;
                adaptive_spacing_changes_++;
            }
        }
    }

    double CalculateTP(double entry_price) {
        double base_tp = current_spread_ + current_spacing_;

        if (config_.tp_mode == FIXED) {
            return entry_price + base_tp;
        }

        // Calculate deviation from equilibrium
        equilibrium_ = GetEquilibriumPrice();
        double deviation = std::abs(equilibrium_ - entry_price);

        // Track for statistics
        total_deviation_sum_ += deviation;
        total_deviation_count_++;

        double tp_distance = base_tp;

        switch (config_.tp_mode) {
            case LINEAR: {
                // TP scales linearly with deviation
                // deviation/spacing gives how many spacing units away we are
                double deviation_ratio = deviation / current_spacing_;
                tp_distance = base_tp * (1.0 + deviation_ratio * config_.linear_scale);
                break;
            }

            case SQRT: {
                // TP scales with sqrt (diminishing returns for larger deviations)
                double deviation_ratio = deviation / current_spacing_;
                tp_distance = base_tp * (1.0 + std::sqrt(deviation_ratio) * config_.sqrt_scale);
                break;
            }

            case PROPORTIONAL: {
                // TP targets a fraction of the deviation back toward mean
                // If we're far from mean, we expect larger reversion
                if (deviation > base_tp) {
                    tp_distance = deviation * config_.reversion_pct;
                    // But at minimum, use base_tp
                    tp_distance = std::max(tp_distance, base_tp);
                }
                break;
            }

            default:
                break;
        }

        // Cap TP at reasonable level (10x base to prevent unrealistic targets)
        tp_distance = std::min(tp_distance, base_tp * 10.0);

        total_tp_sum_ += tp_distance;

        return entry_price + tp_distance;
    }

    void Iterate(TickBasedEngine& engine) {
        lowest_buy_ = DBL_MAX;
        highest_buy_ = DBL_MIN;
        volume_of_open_trades_ = 0.0;

        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->direction == "BUY") {
                volume_of_open_trades_ += trade->lot_size;
                lowest_buy_ = std::min(lowest_buy_, trade->entry_price);
                highest_buy_ = std::max(highest_buy_, trade->entry_price);
            }
        }
    }

    double CalculateLotSize(TickBasedEngine& engine, int positions_total) {
        double used_margin = 0.0;
        for (const Trade* trade : engine.GetOpenPositions()) {
            used_margin += trade->lot_size * config_.contract_size * trade->entry_price / config_.leverage;
        }

        double margin_stop_out = 20.0;
        double margin_level = (used_margin > 0) ? (current_equity_ / used_margin * 100.0) : 10000.0;

        double end_price = (positions_total == 0)
            ? current_ask_ * ((100.0 - config_.survive_pct) / 100.0)
            : highest_buy_ * ((100.0 - config_.survive_pct) / 100.0);

        double distance = current_ask_ - end_price;
        double number_of_trades = std::floor(distance / current_spacing_);
        if (number_of_trades <= 0) number_of_trades = 1;

        double equity_at_target = current_equity_ - volume_of_open_trades_ * distance * config_.contract_size;
        if (used_margin > 0 && (equity_at_target / used_margin * 100.0) < margin_stop_out) {
            return 0.0;
        }

        double trade_size = config_.min_volume;
        double d_equity = config_.contract_size * trade_size * current_spacing_ * (number_of_trades * (number_of_trades + 1) / 2);
        double d_margin = number_of_trades * trade_size * config_.contract_size / config_.leverage;

        double max_mult = config_.max_volume / config_.min_volume;
        for (double mult = max_mult; mult >= 1.0; mult -= 0.1) {
            double test_equity = equity_at_target - mult * d_equity;
            double test_margin = used_margin + mult * d_margin;
            if (test_margin > 0 && (test_equity / test_margin * 100.0) > margin_stop_out) {
                trade_size = mult * config_.min_volume;
                break;
            }
        }

        return std::min(trade_size, config_.max_volume);
    }

    bool Open(double lots, TickBasedEngine& engine) {
        if (lots < config_.min_volume) return false;

        double final_lots = std::min(lots, config_.max_volume);
        final_lots = std::round(final_lots * 100.0) / 100.0;

        // Calculate TP based on rubber band logic
        double tp = CalculateTP(current_ask_);

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
                // Set first entry price for FIRST_ENTRY equilibrium type
                first_entry_price_ = current_ask_;
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

#endif // STRATEGY_RUBBERBAND_TP_H
