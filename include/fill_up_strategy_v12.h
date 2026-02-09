#ifndef FILL_UP_STRATEGY_V12_H
#define FILL_UP_STRATEGY_V12_H

#include "tick_based_engine.h"
#include <vector>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <deque>

namespace backtest {

/**
 * Fill-Up Grid Trading Strategy V12 - OPTIMIZED BASED ON DEEP ANALYSIS
 *
 * This version incorporates findings from comprehensive trade analysis:
 *
 * KEY DISCOVERIES FROM ANALYSIS:
 *
 * 1. ATR FILTER MAY HURT PERFORMANCE (correlation analysis):
 *    - ATR divergence showed 45.9% accuracy (worse than random 50%)
 *    - When short ATR > long ATR, market often RANGES (good for grid!)
 *    - V12 makes ATR filter optional/loosened
 *
 * 2. TIME-BASED EXIT FOR STUCK POSITIONS (trade analysis):
 *    - Losers held 5.3x longer than winners (160K vs 868 ticks)
 *    - Winners hit TP quickly; losers drag on
 *    - V12 adds max hold time exit
 *
 * 3. ENHANCED SESSION FILTER (trade analysis):
 *    - Worst hours: 04:00 (97.9%), 09:00 (98.1%), 17:00 (98.2%)
 *    - Best hours: 06:00-08:00, 10:00-16:00, 18:00+ (100%)
 *    - V12 adds specific bad hour avoidance
 *
 * 4. VOLATILITY SPIKES UNPREDICTABLE (regime detection):
 *    - Best predictor only 16% vs 10% random
 *    - Focus on RESPONSE not prediction
 *    - V12 keeps tight protection for rapid response
 *
 * All proven features retained:
 * - Mean reversion filter (2.7x improvement)
 * - Tight protection levels (3%/5%/15%)
 * - DD-based lot scaling
 * - Fixed optimal spacing (0.75)
 */
class FillUpStrategyV12 {
public:
    struct Config {
        // Core grid parameters
        double spacing = 0.75;              // Optimal from sweep
        double min_volume = 0.01;
        double max_volume = 100.0;
        double contract_size = 100.0;
        double leverage = 500.0;
        int symbol_digits = 2;
        double margin_rate = 1.0;
        int max_positions = 15;

        // ATR Volatility Filter - NOW OPTIONAL (may hurt performance!)
        bool enable_volatility_filter = false;  // DISABLED by default in V12
        int atr_short_period = 50;
        int atr_long_period = 1000;
        double volatility_threshold = 0.8;      // Loosened from 0.6 if enabled

        // Mean reversion filter (KEEP - proven 2.7x improvement)
        bool enable_mean_reversion_filter = true;
        int mean_reversion_sma_period = 500;
        double mean_reversion_threshold = -0.04;

        // TIME-BASED EXIT (NEW in V12 - from trade analysis)
        bool enable_time_exit = true;
        int max_hold_ticks = 50000;             // Exit if held > 50K ticks
        double time_exit_loss_threshold = -0.5; // Only exit if in loss (% of equity)

        // ENHANCED SESSION FILTER (V12 - specific bad hours)
        bool enable_session_filter = true;
        bool avoid_hour_4 = true;               // 04:00 UTC - 97.9% win rate
        bool avoid_hour_9 = true;               // 09:00 UTC - 98.1% win rate
        bool avoid_hour_17 = true;              // 17:00 UTC - 98.2% win rate
        int session_avoid_start = 14;           // US session peak
        int session_avoid_end = 18;

        // Protection levels (KEEP - for rapid response)
        double stop_new_at_dd = 3.0;
        double partial_close_at_dd = 5.0;
        double close_all_at_dd = 15.0;

        // DD-based lot scaling
        bool enable_dd_lot_scaling = true;
        double lot_scale_start_dd = 1.0;
        double lot_scale_min_factor = 0.25;

        // TP configuration
        double tp_multiplier = 2.0;
    };

    explicit FillUpStrategyV12(const Config& config)
        : config_(config),
          lowest_buy_(DBL_MAX),
          highest_buy_(DBL_MIN),
          volume_of_open_trades_(0.0),
          peak_equity_(0.0),
          max_number_of_open_(0),
          partial_close_done_(false),
          all_closed_(false),
          trades_closed_by_protection_(0),
          trades_closed_by_time_exit_(0),
          last_price_(0.0),
          atr_short_sum_(0.0),
          atr_long_sum_(0.0),
          current_hour_(0),
          dd_lot_reductions_applied_(0),
          sma_sum_(0.0),
          current_tick_count_(0) {
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_spread_ = tick.spread();
        current_equity_ = engine.GetEquity();
        current_balance_ = engine.GetBalance();
        current_tick_count_++;

        // Update indicators
        UpdateATR(tick.bid);
        UpdateSMA(tick.bid);
        UpdateSessionTime(tick);

        // Peak equity reset when no positions
        if (engine.GetOpenPositions().empty()) {
            if (peak_equity_ != current_balance_) {
                peak_equity_ = current_balance_;
                partial_close_done_ = false;
                all_closed_ = false;
            }
        }

        // Track peak equity
        if (current_equity_ > peak_equity_) {
            peak_equity_ = current_equity_;
            partial_close_done_ = false;
            all_closed_ = false;
        }

        // Calculate drawdown
        double current_drawdown_pct = 0.0;
        if (peak_equity_ > 0) {
            current_drawdown_pct = (peak_equity_ - current_equity_) / peak_equity_ * 100.0;
        }

        // Update statistics
        max_number_of_open_ = std::max(max_number_of_open_, (int)engine.GetOpenPositions().size());

        // Protection: Close ALL at threshold
        if (current_drawdown_pct > config_.close_all_at_dd && !all_closed_ && !engine.GetOpenPositions().empty()) {
            CloseAllPositions(engine);
            all_closed_ = true;
            peak_equity_ = engine.GetBalance();
            return;
        }

        // Protection: Partial close at threshold
        if (current_drawdown_pct > config_.partial_close_at_dd && !partial_close_done_ && engine.GetOpenPositions().size() > 1) {
            CloseWorstPositions(engine, 0.50);
            partial_close_done_ = true;
        }

        // V12: Time-based exit for stuck positions
        if (config_.enable_time_exit) {
            CheckTimeBasedExits(engine);
        }

        // Iterate through positions
        Iterate(engine);

        // Check filters
        bool volatility_ok = !config_.enable_volatility_filter || IsVolatilityLow();
        bool session_ok = IsSessionAllowed();
        bool mean_reversion_ok = IsMeanReversionEntry();

        // Open new positions if all conditions pass
        if (current_drawdown_pct < config_.stop_new_at_dd && volatility_ok && session_ok && mean_reversion_ok) {
            OpenNew(engine, current_drawdown_pct);
        }
    }

    // Statistics getters
    int GetMaxNumberOfOpen() const { return max_number_of_open_; }
    int GetTradesClosedByProtection() const { return trades_closed_by_protection_; }
    int GetTradesClosedByTimeExit() const { return trades_closed_by_time_exit_; }
    double GetCurrentATRShort() const { return GetATRShort(); }
    double GetCurrentATRLong() const { return GetATRLong(); }
    bool IsVolatilityFilterActive() const { return config_.enable_volatility_filter && IsVolatilityLow(); }
    bool IsSessionFilterActive() const { return IsSessionAllowed(); }

private:
    Config config_;

    // Position tracking
    double lowest_buy_;
    double highest_buy_;
    double closest_above_;
    double closest_below_;
    double volume_of_open_trades_;

    // Market state
    double current_ask_;
    double current_bid_;
    double current_spread_;
    double current_equity_;
    double current_balance_;

    // Statistics
    double peak_equity_;
    int max_number_of_open_;

    // Protection state
    bool partial_close_done_;
    bool all_closed_;
    int trades_closed_by_protection_;
    int trades_closed_by_time_exit_;

    // ATR calculation state
    double last_price_;
    std::deque<double> atr_short_values_;
    std::deque<double> atr_long_values_;
    double atr_short_sum_;
    double atr_long_sum_;

    // Session tracking
    int current_hour_;

    // DD lot scaling
    int dd_lot_reductions_applied_;

    // SMA state
    std::deque<double> sma_prices_;
    double sma_sum_;

    // Tick counter for time-based exits
    size_t current_tick_count_;

    // Track entry tick for each position (position id -> entry tick)
    std::map<size_t, size_t> position_entry_ticks_;

    void UpdateATR(double price) {
        if (last_price_ > 0) {
            double range = std::abs(price - last_price_);

            atr_short_values_.push_back(range);
            atr_short_sum_ += range;
            if ((int)atr_short_values_.size() > config_.atr_short_period) {
                atr_short_sum_ -= atr_short_values_.front();
                atr_short_values_.pop_front();
            }

            atr_long_values_.push_back(range);
            atr_long_sum_ += range;
            if ((int)atr_long_values_.size() > config_.atr_long_period) {
                atr_long_sum_ -= atr_long_values_.front();
                atr_long_values_.pop_front();
            }
        }
        last_price_ = price;
    }

    void UpdateSMA(double price) {
        sma_prices_.push_back(price);
        sma_sum_ += price;

        if ((int)sma_prices_.size() > config_.mean_reversion_sma_period) {
            sma_sum_ -= sma_prices_.front();
            sma_prices_.pop_front();
        }
    }

    double GetSMA() const {
        if (sma_prices_.empty()) return 0.0;
        return sma_sum_ / sma_prices_.size();
    }

    double CalculatePriceDeviation() const {
        double sma = GetSMA();
        if (sma <= 0) return 0.0;
        return (current_bid_ - sma) / sma * 100.0;
    }

    bool IsMeanReversionEntry() const {
        if (!config_.enable_mean_reversion_filter) return true;
        if ((int)sma_prices_.size() < config_.mean_reversion_sma_period) return true;

        double deviation = CalculatePriceDeviation();
        return deviation < config_.mean_reversion_threshold;
    }

    void UpdateSessionTime(const Tick& tick) {
        if (tick.timestamp.length() >= 13) {
            std::string hour_str;
            size_t space_pos = tick.timestamp.find(' ');
            if (space_pos != std::string::npos && tick.timestamp.length() > space_pos + 2) {
                hour_str = tick.timestamp.substr(space_pos + 1, 2);
                try {
                    current_hour_ = std::stoi(hour_str);
                } catch (...) {
                    current_hour_ = 12;
                }
            }
        }
    }

    double GetATRShort() const {
        if (atr_short_values_.empty()) return 0.0;
        return atr_short_sum_ / atr_short_values_.size();
    }

    double GetATRLong() const {
        if (atr_long_values_.empty()) return 0.0;
        return atr_long_sum_ / atr_long_values_.size();
    }

    bool IsVolatilityLow() const {
        if ((int)atr_short_values_.size() < config_.atr_short_period) return true;
        if ((int)atr_long_values_.size() < config_.atr_long_period) return true;

        double atr_short = GetATRShort();
        double atr_long = GetATRLong();
        if (atr_long <= 0) return true;

        return atr_short < atr_long * config_.volatility_threshold;
    }

    /**
     * V12 Enhanced Session Filter
     * - Avoids specific bad hours identified from trade analysis
     * - Also avoids US session peak (14-18 UTC)
     */
    bool IsSessionAllowed() const {
        if (!config_.enable_session_filter) return true;

        // V12: Avoid specific bad hours
        if (config_.avoid_hour_4 && current_hour_ == 4) return false;
        if (config_.avoid_hour_9 && current_hour_ == 9) return false;
        if (config_.avoid_hour_17 && current_hour_ == 17) return false;

        // US session peak
        if (current_hour_ >= config_.session_avoid_start &&
            current_hour_ < config_.session_avoid_end) {
            return false;
        }

        return true;
    }

    /**
     * V12: Time-based exit for stuck positions
     *
     * From trade analysis:
     * - Winners hit TP in median 868 ticks
     * - Losers drag on for median 160,312 ticks (5.3x longer)
     *
     * If a position is held too long and in loss, exit early
     * to free up capital and reduce risk.
     */
    void CheckTimeBasedExits(TickBasedEngine& engine) {
        auto& positions = engine.GetOpenPositions();

        std::vector<Trade*> to_close;

        for (Trade* t : positions) {
            // Check if we have entry tick recorded
            auto it = position_entry_ticks_.find(t->id);
            if (it == position_entry_ticks_.end()) {
                // First time seeing this position, record entry tick
                position_entry_ticks_[t->id] = current_tick_count_;
                continue;
            }

            size_t ticks_held = current_tick_count_ - it->second;

            if (ticks_held > (size_t)config_.max_hold_ticks) {
                // Position held too long - check if in loss
                double pl_pct = 0;
                if (current_equity_ > 0) {
                    double pl = (current_bid_ - t->entry_price) * t->lot_size * config_.contract_size;
                    pl_pct = pl / current_equity_ * 100.0;
                }

                if (pl_pct < config_.time_exit_loss_threshold) {
                    to_close.push_back(t);
                }
            }
        }

        // Close stuck positions
        for (Trade* t : to_close) {
            position_entry_ticks_.erase(t->id);
            engine.ClosePosition(t);
            trades_closed_by_time_exit_++;
        }
    }

    double CalculateLotScale(double dd_pct) const {
        if (!config_.enable_dd_lot_scaling) return 1.0;
        if (dd_pct <= config_.lot_scale_start_dd) return 1.0;
        if (dd_pct >= config_.stop_new_at_dd) return config_.lot_scale_min_factor;

        double dd_range = config_.stop_new_at_dd - config_.lot_scale_start_dd;
        double scale_range = 1.0 - config_.lot_scale_min_factor;
        double dd_progress = (dd_pct - config_.lot_scale_start_dd) / dd_range;

        return 1.0 - (dd_progress * scale_range);
    }

    void CloseAllPositions(TickBasedEngine& engine) {
        auto& positions = engine.GetOpenPositions();
        while (!positions.empty()) {
            position_entry_ticks_.erase(positions[0]->id);
            engine.ClosePosition(positions[0]);
            trades_closed_by_protection_++;
        }
    }

    void CloseWorstPositions(TickBasedEngine& engine, double pct) {
        auto& positions = engine.GetOpenPositions();
        if (positions.size() <= 1) return;

        std::vector<std::pair<double, Trade*>> pl_and_trade;
        for (Trade* t : positions) {
            double pl = (current_bid_ - t->entry_price) * t->lot_size * config_.contract_size;
            pl_and_trade.push_back({pl, t});
        }
        std::sort(pl_and_trade.begin(), pl_and_trade.end());

        int to_close = (int)(pl_and_trade.size() * pct);
        to_close = std::max(1, to_close);

        for (int i = 0; i < to_close; i++) {
            position_entry_ticks_.erase(pl_and_trade[i].second->id);
            engine.ClosePosition(pl_and_trade[i].second);
            trades_closed_by_protection_++;
        }
    }

    void Iterate(TickBasedEngine& engine) {
        lowest_buy_ = DBL_MAX;
        highest_buy_ = DBL_MIN;
        closest_above_ = DBL_MAX;
        closest_below_ = DBL_MIN;
        volume_of_open_trades_ = 0.0;

        const auto& positions = engine.GetOpenPositions();

        for (const Trade* trade : positions) {
            if (trade->direction == "BUY") {
                double open_price = trade->entry_price;
                double lots = trade->lot_size;

                volume_of_open_trades_ += lots;
                lowest_buy_ = std::min(lowest_buy_, open_price);
                highest_buy_ = std::max(highest_buy_, open_price);

                if (open_price >= current_ask_) {
                    closest_above_ = std::min(closest_above_, open_price - current_ask_);
                }
                if (open_price <= current_ask_) {
                    closest_below_ = std::min(closest_below_, current_ask_ - open_price);
                }
            }
        }
    }

    bool Open(double local_unit, TickBasedEngine& engine) {
        if (local_unit < config_.min_volume) return false;

        double final_unit = std::min(local_unit, config_.max_volume);
        final_unit = NormalizeVolume(final_unit);

        double tp = current_ask_ + current_spread_ + (config_.spacing * config_.tp_multiplier);

        Trade* trade = engine.OpenMarketOrder("BUY", final_unit, 0.0, tp);
        if (trade) {
            position_entry_ticks_[trade->id] = current_tick_count_;
            return true;
        }
        return false;
    }

    double NormalizeVolume(double volume) const {
        int digits = 2;
        if (config_.min_volume == 0.01) digits = 2;
        else if (config_.min_volume == 0.1) digits = 1;
        else digits = 0;

        double factor = std::pow(10.0, digits);
        double normalized = std::round(volume * factor) / factor;
        return std::max(normalized, config_.min_volume);
    }

    void OpenNew(TickBasedEngine& engine, double current_dd_pct) {
        int positions_total = engine.GetOpenPositions().size();

        if (positions_total >= config_.max_positions) return;

        double dd_scale = CalculateLotScale(current_dd_pct);
        double lot_size = NormalizeVolume(config_.min_volume * dd_scale);

        if (dd_scale < 1.0) dd_lot_reductions_applied_++;

        if (positions_total == 0) {
            if (Open(lot_size, engine)) {
                highest_buy_ = current_ask_;
                lowest_buy_ = current_ask_;
            }
        } else {
            if (lowest_buy_ >= current_ask_ + config_.spacing) {
                if (Open(lot_size, engine)) {
                    lowest_buy_ = current_ask_;
                }
            }
            else if (highest_buy_ <= current_ask_ - config_.spacing) {
                if (Open(lot_size, engine)) {
                    highest_buy_ = current_ask_;
                }
            }
            else if ((closest_above_ >= config_.spacing) && (closest_below_ >= config_.spacing)) {
                Open(lot_size, engine);
            }
        }
    }
};

} // namespace backtest

#endif // FILL_UP_STRATEGY_V12_H
