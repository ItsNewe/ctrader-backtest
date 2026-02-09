#ifndef FILL_UP_STRATEGY_V7_H
#define FILL_UP_STRATEGY_V7_H

#include "tick_based_engine.h"
#include <vector>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <deque>

namespace backtest {

/**
 * Fill-Up Grid Trading Strategy V7
 *
 * Key improvement: Low Volatility Filter (replaces SMA trend filter)
 *
 * Philosophy change from V5:
 * - V5 filtered by TREND (only trade uptrends) = dependent on gold going UP
 * - V7 filters by VOLATILITY (only trade calm markets) = market-neutral
 *
 * Why low volatility works better for grid strategies:
 * - Grid strategies exploit MEAN REVERSION (price oscillates around a level)
 * - Mean reversion works in RANGING markets (low volatility)
 * - Mean reversion FAILS in TRENDING markets (high volatility)
 * - By trading only in low volatility, we trade when the strategy works best
 *
 * The filter:
 * - Calculate short-term ATR (recent volatility)
 * - Calculate long-term ATR (average volatility)
 * - Only trade when short ATR < long ATR * threshold
 * - This means: only trade when current volatility is below average
 *
 * Benefits:
 * - Works in both UP and DOWN calm markets
 * - Avoids trending periods in both directions
 * - Not dependent on gold's overall direction
 * - Exploits what grid strategies are designed for
 */
class FillUpStrategyV7 {
public:
    struct Config {
        // Core grid parameters
        double spacing = 1.0;
        double min_volume = 0.01;
        double max_volume = 100.0;
        double contract_size = 100.0;
        double leverage = 500.0;
        int symbol_digits = 2;
        double margin_rate = 1.0;

        // V3 Protection (unchanged - proven effective)
        double stop_new_at_dd = 5.0;
        double partial_close_at_dd = 8.0;
        double close_all_at_dd = 25.0;
        int max_positions = 20;

        // V7 Volatility Filter (NEW)
        int atr_short_period = 100;      // Short-term ATR period
        int atr_long_period = 500;       // Long-term ATR for comparison
        double volatility_threshold = 0.8; // Trade when short ATR < long ATR * this
        // 0.8 = trade when volatility is 20% below average
        // 1.0 = trade when volatility is at or below average
        // 0.5 = trade when volatility is 50% below average (very strict)

        // V6 TP multiplier (optional enhancement)
        double tp_multiplier = 1.0;  // Set to 2.0 for wider TP
    };

    explicit FillUpStrategyV7(const Config& config)
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
          atr_long_sum_(0.0) {
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_spread_ = tick.spread();
        current_equity_ = engine.GetEquity();
        current_balance_ = engine.GetBalance();

        // Update ATR calculations
        UpdateATR(tick.bid);

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

        // V3 Protection: Close ALL at threshold
        if (current_drawdown_pct > config_.close_all_at_dd && !all_closed_ && !engine.GetOpenPositions().empty()) {
            CloseAllPositions(engine);
            all_closed_ = true;
            peak_equity_ = engine.GetBalance();
            return;
        }

        // V3 Protection: Partial close at threshold
        if (current_drawdown_pct > config_.partial_close_at_dd && !partial_close_done_ && engine.GetOpenPositions().size() > 1) {
            CloseWorstPositions(engine, 0.50);
            partial_close_done_ = true;
        }

        // Iterate through positions
        Iterate(engine);

        // V7: Check volatility filter before opening new positions
        bool volatility_ok = IsVolatilityLow();

        // Open new positions only if DD < threshold AND volatility is low
        if (current_drawdown_pct < config_.stop_new_at_dd && volatility_ok) {
            OpenNew(engine);
        }
    }

    // Statistics getters
    int GetMaxNumberOfOpen() const { return max_number_of_open_; }
    int GetTradesClosedByProtection() const { return trades_closed_by_protection_; }
    double GetCurrentATRShort() const { return GetATRShort(); }
    double GetCurrentATRLong() const { return GetATRLong(); }
    bool IsVolatilityFilterActive() const { return IsVolatilityLow(); }

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

    // V7: ATR calculation state
    double last_price_;
    std::deque<double> atr_short_values_;
    std::deque<double> atr_long_values_;
    double atr_short_sum_;
    double atr_long_sum_;

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
        return atr_short < atr_long * config_.volatility_threshold;
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

        // V6/V7: Use TP multiplier
        double tp = current_ask_ + current_spread_ + (config_.spacing * config_.tp_multiplier);

        Trade* trade = engine.OpenMarketOrder("BUY", final_unit, 0.0, tp);
        return (trade != nullptr);
    }

    double NormalizeVolume(double volume) {
        int digits = 2;
        if (config_.min_volume == 0.01) digits = 2;
        else if (config_.min_volume == 0.1) digits = 1;
        else digits = 0;

        double factor = std::pow(10.0, digits);
        return std::round(volume * factor) / factor;
    }

    void OpenNew(TickBasedEngine& engine) {
        int positions_total = engine.GetOpenPositions().size();

        if (positions_total >= config_.max_positions) {
            return;
        }

        double lot_size = config_.min_volume;

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

#endif // FILL_UP_STRATEGY_V7_H
