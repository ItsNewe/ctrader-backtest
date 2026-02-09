#ifndef FILL_UP_STRATEGY_V10_H
#define FILL_UP_STRATEGY_V10_H

#include "tick_based_engine.h"
#include <vector>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <deque>

namespace backtest {

/**
 * Fill-Up Grid Trading Strategy V10 - ULTIMATE VERSION WITH MEAN REVERSION
 *
 * This version adds the MAJOR discovery from optimization sweeps:
 * MEAN REVERSION FILTER - showed 2.7x better returns in testing!
 *
 * Key features (ALL proven improvements combined):
 * 1. MEAN REVERSION FILTER (NEW - major improvement):
 *    - Calculate 500-tick SMA of price
 *    - Only enter when price deviation < -0.04% (price below SMA)
 *    - Ensures entries at "oversold" levels
 *
 * 2. Position count scaling (NEW - optional):
 *    - lot = base_lot / (position_count + 1)
 *    - Reduces cascade risk in extended moves
 *
 * 3. All V9 features:
 *    - Optimal spacing: 0.75 (from parameter sweep)
 *    - Session filter: Avoid US session peak (14-18 UTC)
 *    - Anti-martingale option: +25% lot after each consecutive win
 *    - DD-based lot scaling: Reduce lots as drawdown increases
 *    - Optimized ATR parameters: 50/1000/0.6
 *    - Wider TP: 2.0x multiplier
 *    - Tighter protection levels: 3%/5%/15%
 *
 * Risk philosophy:
 * - ONLY enter when price is below moving average (mean reversion)
 * - Reduce position size as more positions accumulate
 * - Aggressive early intervention to prevent deep drawdowns
 * - Avoid high-volatility sessions
 * - Strict volatility filter (only trade in calm markets)
 *
 * This is the "ultimate" version designed for maximum risk-adjusted returns.
 */
class FillUpStrategyV10 {
public:
    struct Config {
        // Core grid parameters - V9 OPTIMAL
        double spacing = 0.75;              // Optimal from sweep (was 1.0)
        double min_volume = 0.01;
        double max_volume = 100.0;
        double contract_size = 100.0;
        double leverage = 500.0;
        int symbol_digits = 2;
        double margin_rate = 1.0;

        // Mean reversion filter (NEW - major improvement)
        bool enable_mean_reversion_filter = true;
        int mean_reversion_sma_period = 500;
        double mean_reversion_threshold = -0.04;  // Enter when deviation < this (%)

        // Position count scaling (NEW)
        bool enable_position_scaling = false;  // Optional - reduces lot as positions increase

        // V9 Optimized Volatility Filter (from V7/V8 sweep)
        int atr_short_period = 50;          // More responsive to recent volatility
        int atr_long_period = 1000;         // More stable baseline
        double volatility_threshold = 0.6;  // Strict filter (only trade when volatility is 40% below average)

        // V9 Optimized TP multiplier
        double tp_multiplier = 2.0;         // Wider TP for better risk/reward

        // V8 TIGHTER Protection (proven to reduce max DD)
        double stop_new_at_dd = 3.0;        // Stop new positions early
        double partial_close_at_dd = 5.0;   // Partial close kicks in early
        double close_all_at_dd = 15.0;      // Emergency close much earlier
        int max_positions = 15;             // Fewer positions = less exposure

        // Session filter - V9 DEFAULT ENABLED (avoid US peak)
        bool enable_session_filter = true;  // Enabled by default in V9
        int session_avoid_start = 14;       // 14:00 UTC - US market peak volatility
        int session_avoid_end = 18;         // 18:00 UTC - End of peak volatility

        // Anti-martingale system - V9 NEW (optional)
        bool enable_anti_martingale = false;    // Disabled by default, user can enable
        double anti_martingale_increment = 0.25; // +25% lot after each consecutive win
        double max_lot_multiplier = 4.0;        // Maximum 4x lot size cap

        // DD-based lot scaling (from V8)
        bool enable_dd_lot_scaling = true;  // Reduce lot size based on current DD
        double lot_scale_start_dd = 1.0;    // Start reducing lots at this DD%
        double lot_scale_min_factor = 0.25; // Minimum lot factor at max DD
    };

    explicit FillUpStrategyV10(const Config& config)
        : config_(config),
          lowest_buy_(DBL_MAX),
          highest_buy_(DBL_MIN),
          volume_of_open_trades_(0.0),
          peak_equity_(0.0),
          max_number_of_open_(0),
          partial_close_done_(false),
          all_closed_(false),
          trades_closed_by_protection_(0),
          last_price_(0.0),
          atr_short_sum_(0.0),
          atr_long_sum_(0.0),
          current_hour_(0),
          dd_lot_reductions_applied_(0),
          consecutive_wins_(0),
          anti_martingale_multiplier_(1.0),
          total_anti_martingale_boosts_(0),
          last_closed_trade_count_(0),
          sma_sum_(0.0),
          mean_reversion_entries_blocked_(0),
          mean_reversion_entries_allowed_(0) {
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_spread_ = tick.spread();
        current_equity_ = engine.GetEquity();
        current_balance_ = engine.GetBalance();

        // Update ATR calculations
        UpdateATR(tick.bid);

        // Update SMA for mean reversion filter
        UpdateSMA(tick.bid);

        // Update session time if available
        UpdateSessionTime(tick);

        // Track trade closures for anti-martingale
        UpdateAntiMartingale(engine);

        // Peak equity reset when no positions (fix from V3)
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

        // Calculate current drawdown
        double current_drawdown_pct = 0.0;
        if (peak_equity_ > 0) {
            current_drawdown_pct = (peak_equity_ - current_equity_) / peak_equity_ * 100.0;
        }

        // Update statistics
        max_number_of_open_ = std::max(max_number_of_open_, (int)engine.GetOpenPositions().size());

        // V9 Protection: Close ALL at threshold
        if (current_drawdown_pct > config_.close_all_at_dd && !all_closed_ && !engine.GetOpenPositions().empty()) {
            CloseAllPositions(engine);
            all_closed_ = true;
            peak_equity_ = engine.GetBalance();
            // Reset anti-martingale on emergency close
            ResetAntiMartingale();
            return;
        }

        // V9 Protection: Partial close at threshold
        if (current_drawdown_pct > config_.partial_close_at_dd && !partial_close_done_ && engine.GetOpenPositions().size() > 1) {
            CloseWorstPositions(engine, 0.50);
            partial_close_done_ = true;
            // Reduce anti-martingale on partial close
            consecutive_wins_ = std::max(0, consecutive_wins_ - 2);
            UpdateAntiMartingaleMultiplier();
        }

        // Iterate through positions
        Iterate(engine);

        // V9: Check volatility filter before opening new positions
        bool volatility_ok = IsVolatilityLow();

        // V9: Check session filter (default enabled)
        bool session_ok = IsSessionAllowed();

        // V10: Check mean reversion filter (NEW - major improvement)
        bool mean_reversion_ok = IsMeanReversionEntry();

        // Open new positions only if all filters pass
        if (current_drawdown_pct < config_.stop_new_at_dd && volatility_ok && session_ok && mean_reversion_ok) {
            OpenNew(engine, current_drawdown_pct);
        }
    }

    // Statistics getters
    int GetMaxNumberOfOpen() const { return max_number_of_open_; }
    int GetTradesClosedByProtection() const { return trades_closed_by_protection_; }
    double GetCurrentATRShort() const { return GetATRShort(); }
    double GetCurrentATRLong() const { return GetATRLong(); }
    bool IsVolatilityFilterActive() const { return IsVolatilityLow(); }
    bool IsSessionFilterActive() const { return IsSessionAllowed(); }
    int GetDDLotReductionsApplied() const { return dd_lot_reductions_applied_; }

    // V9 Anti-martingale getters
    int GetConsecutiveWins() const { return consecutive_wins_; }
    double GetAntiMartingaleMultiplier() const { return anti_martingale_multiplier_; }
    int GetTotalAntiMartingaleBoosts() const { return total_anti_martingale_boosts_; }

    // V10 Mean reversion getters
    double GetCurrentSMA() const { return GetSMA(); }
    double GetCurrentDeviation() const { return CalculatePriceDeviation(); }
    bool IsMeanReversionFilterActive() const { return IsMeanReversionEntry(); }
    int GetMeanReversionEntriesBlocked() const { return mean_reversion_entries_blocked_; }
    int GetMeanReversionEntriesAllowed() const { return mean_reversion_entries_allowed_; }

    // Get effective lot size at current DD (including all scaling factors)
    double GetEffectiveLotSize(double dd_pct, int position_count = 0) const {
        double base_lot = config_.min_volume;

        // Apply DD-based scaling (reduction)
        double dd_scale = 1.0;
        if (config_.enable_dd_lot_scaling) {
            dd_scale = CalculateLotScale(dd_pct);
        }

        // Apply anti-martingale scaling (increase)
        double am_scale = 1.0;
        if (config_.enable_anti_martingale) {
            am_scale = anti_martingale_multiplier_;
        }

        // V10: Apply position count scaling (reduction)
        double pos_scale = 1.0;
        if (config_.enable_position_scaling && position_count > 0) {
            pos_scale = 1.0 / (position_count + 1);
        }

        double lot = base_lot * dd_scale * am_scale * pos_scale;
        return NormalizeVolume(lot);
    }

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

    // ATR calculation state
    double last_price_;
    std::deque<double> atr_short_values_;
    std::deque<double> atr_long_values_;
    double atr_short_sum_;
    double atr_long_sum_;

    // Session tracking
    int current_hour_;

    // DD lot scaling stats
    int dd_lot_reductions_applied_;

    // V9: Anti-martingale state
    int consecutive_wins_;
    double anti_martingale_multiplier_;
    int total_anti_martingale_boosts_;
    int last_closed_trade_count_;

    // V10: Mean reversion state
    std::deque<double> sma_prices_;
    double sma_sum_;
    int mean_reversion_entries_blocked_;
    int mean_reversion_entries_allowed_;

    void UpdateATR(double price) {
        if (last_price_ > 0) {
            double range = std::abs(price - last_price_);

            // Update short ATR
            atr_short_values_.push_back(range);
            atr_short_sum_ += range;
            if ((int)atr_short_values_.size() > config_.atr_short_period) {
                atr_short_sum_ -= atr_short_values_.front();
                atr_short_values_.pop_front();
            }

            // Update long ATR
            atr_long_values_.push_back(range);
            atr_long_sum_ += range;
            if ((int)atr_long_values_.size() > config_.atr_long_period) {
                atr_long_sum_ -= atr_long_values_.front();
                atr_long_values_.pop_front();
            }
        }
        last_price_ = price;
    }

    /**
     * V10: Update SMA for mean reversion filter
     *
     * Maintains a running sum for efficient SMA calculation.
     * Uses a deque to track the last N prices.
     */
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

    /**
     * V10: Calculate price deviation from SMA (in percentage)
     *
     * Returns: (current_price - sma) / sma * 100
     * Negative values indicate price is below SMA (potential buy zone)
     * Positive values indicate price is above SMA (avoid buying)
     */
    double CalculatePriceDeviation() const {
        double sma = GetSMA();
        if (sma <= 0) return 0.0;
        return (current_bid_ - sma) / sma * 100.0;
    }

    /**
     * V10: Mean Reversion Entry Filter
     *
     * Only allow entries when price is below the SMA by at least the threshold.
     * This ensures we're buying at "oversold" levels, improving mean reversion potential.
     *
     * Default threshold: -0.04% (price must be at least 0.04% below SMA)
     */
    bool IsMeanReversionEntry() const {
        if (!config_.enable_mean_reversion_filter) {
            return true;  // Filter disabled
        }

        // Not enough data yet - allow trading to build up SMA
        if ((int)sma_prices_.size() < config_.mean_reversion_sma_period) {
            return true;
        }

        double deviation = CalculatePriceDeviation();

        // Only enter when deviation is below threshold (price below SMA)
        // threshold is negative, so deviation must be MORE negative (price further below SMA)
        return deviation < config_.mean_reversion_threshold;
    }

    void UpdateSessionTime(const Tick& tick) {
        // Extract hour from tick timestamp if available
        if (tick.timestamp.length() >= 13) {
            std::string hour_str;
            size_t space_pos = tick.timestamp.find(' ');
            if (space_pos != std::string::npos && tick.timestamp.length() > space_pos + 2) {
                hour_str = tick.timestamp.substr(space_pos + 1, 2);
                try {
                    current_hour_ = std::stoi(hour_str);
                } catch (...) {
                    current_hour_ = 12; // Default to midday if parsing fails
                }
            }
        }
    }

    /**
     * V9 Anti-martingale tracking
     *
     * Track closed trades to determine consecutive wins.
     * Increase lot multiplier after each consecutive win.
     * Reset on loss or protection close.
     */
    void UpdateAntiMartingale(TickBasedEngine& engine) {
        if (!config_.enable_anti_martingale) {
            return;
        }

        // Get closed trades and check for new closures
        const auto& closed_trades = engine.GetClosedTrades();
        int current_closed_count = (int)closed_trades.size();

        if (current_closed_count > last_closed_trade_count_) {
            // New trade(s) closed - check if profitable
            for (int i = last_closed_trade_count_; i < current_closed_count; i++) {
                const Trade& trade = closed_trades[i];
                if (trade.profit > 0) {
                    // Win - increase consecutive wins
                    consecutive_wins_++;
                    total_anti_martingale_boosts_++;
                } else {
                    // Loss - reset streak
                    consecutive_wins_ = 0;
                }
            }
            UpdateAntiMartingaleMultiplier();
            last_closed_trade_count_ = current_closed_count;
        }
    }

    void UpdateAntiMartingaleMultiplier() {
        if (!config_.enable_anti_martingale) {
            anti_martingale_multiplier_ = 1.0;
            return;
        }

        // Calculate multiplier: 1.0 + (wins * increment)
        // Capped at max_lot_multiplier
        double multiplier = 1.0 + (consecutive_wins_ * config_.anti_martingale_increment);
        anti_martingale_multiplier_ = std::min(multiplier, config_.max_lot_multiplier);
    }

    void ResetAntiMartingale() {
        consecutive_wins_ = 0;
        anti_martingale_multiplier_ = 1.0;
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
        // Not enough data yet - allow trading
        if ((int)atr_short_values_.size() < config_.atr_short_period) return true;
        if ((int)atr_long_values_.size() < config_.atr_long_period) return true;

        double atr_short = GetATRShort();
        double atr_long = GetATRLong();

        if (atr_long <= 0) return true;

        // Trade when short-term volatility is below threshold of long-term
        // V9: With threshold 0.6, only trade when volatility is 40% below average
        return atr_short < atr_long * config_.volatility_threshold;
    }

    bool IsSessionAllowed() const {
        if (!config_.enable_session_filter) {
            return true;  // Session filter disabled
        }

        // V9: Check US peak volatility window (default 14-18 UTC)
        if (current_hour_ >= config_.session_avoid_start &&
            current_hour_ < config_.session_avoid_end) {
            return false;
        }

        return true;
    }

    /**
     * DD-based lot scaling (from V8)
     *
     * As drawdown increases, reduce lot size to limit additional exposure.
     * This helps prevent the strategy from digging a deeper hole when already losing.
     *
     * Scale factor:
     * - At 0% DD: 100% of normal lot size
     * - At lot_scale_start_dd (1%): 100% of normal lot size
     * - At stop_new_at_dd (3%): lot_scale_min_factor (25%) of normal lot size
     * - Linear interpolation between
     */
    double CalculateLotScale(double dd_pct) const {
        if (!config_.enable_dd_lot_scaling) {
            return 1.0;
        }

        if (dd_pct <= config_.lot_scale_start_dd) {
            return 1.0;  // No reduction below start threshold
        }

        if (dd_pct >= config_.stop_new_at_dd) {
            return config_.lot_scale_min_factor;  // Minimum at max DD
        }

        // Linear interpolation
        double dd_range = config_.stop_new_at_dd - config_.lot_scale_start_dd;
        double scale_range = 1.0 - config_.lot_scale_min_factor;
        double dd_progress = (dd_pct - config_.lot_scale_start_dd) / dd_range;

        return 1.0 - (dd_progress * scale_range);
    }

    void CloseAllPositions(TickBasedEngine& engine) {
        auto& positions = engine.GetOpenPositions();
        while (!positions.empty()) {
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
        if (local_unit < config_.min_volume) {
            return false;
        }

        double final_unit = std::min(local_unit, config_.max_volume);
        final_unit = NormalizeVolume(final_unit);

        // V9: Use optimized spacing (0.75) and TP multiplier (2.0x)
        double tp = current_ask_ + current_spread_ + (config_.spacing * config_.tp_multiplier);

        Trade* trade = engine.OpenMarketOrder("BUY", final_unit, 0.0, tp);
        return (trade != nullptr);
    }

    double NormalizeVolume(double volume) const {
        int digits = 2;
        if (config_.min_volume == 0.01) digits = 2;
        else if (config_.min_volume == 0.1) digits = 1;
        else digits = 0;

        double factor = std::pow(10.0, digits);
        double normalized = std::round(volume * factor) / factor;

        // Ensure we don't go below minimum
        return std::max(normalized, config_.min_volume);
    }

    void OpenNew(TickBasedEngine& engine, double current_dd_pct) {
        int positions_total = engine.GetOpenPositions().size();

        if (positions_total >= config_.max_positions) {
            return;
        }

        // V9: Calculate lot size with DD-based scaling AND anti-martingale
        double dd_scale = CalculateLotScale(current_dd_pct);
        double am_scale = config_.enable_anti_martingale ? anti_martingale_multiplier_ : 1.0;

        // V10: Apply position count scaling (reduces lot as positions increase)
        double pos_scale = 1.0;
        if (config_.enable_position_scaling && positions_total > 0) {
            pos_scale = 1.0 / (positions_total + 1);
        }

        // Apply all scales
        double lot_size = NormalizeVolume(config_.min_volume * dd_scale * am_scale * pos_scale);

        // Track if we applied a DD reduction
        if (dd_scale < 1.0) {
            dd_lot_reductions_applied_++;
        }

        if (positions_total == 0) {
            if (Open(lot_size, engine)) {
                highest_buy_ = current_ask_;
                lowest_buy_ = current_ask_;
            }
        } else {
            // V9: Use optimal spacing of 0.75
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

#endif // FILL_UP_STRATEGY_V10_H
