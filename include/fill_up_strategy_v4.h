#ifndef FILL_UP_STRATEGY_V4_H
#define FILL_UP_STRATEGY_V4_H

#include "tick_based_engine.h"
#include <vector>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <deque>

namespace backtest {

/**
 * Fill-Up Grid Trading Strategy V4
 *
 * Building on V3 Optimized, V4 adds:
 * 1. Adaptive position limit - reduces max positions when approaching DD thresholds
 * 2. Volatility-aware spacing - widens grid in high volatility
 * 3. Recovery mode - more conservative after emergency close
 *
 * V3 Parameters (proven optimal on real data):
 * - stop_new_at_dd = 5.0%
 * - partial_close_at_dd = 8.0%
 * - close_all_at_dd = 25.0%
 * - max_positions = 20
 * - partial_close_pct = 0.50 (50%)
 *
 * V4 Additions:
 * - Adaptive max_positions based on DD (20 -> 15 -> 10)
 * - Wider spacing when volatility is high
 * - Cooldown period after close-all
 */
class FillUpStrategyV4 {
public:
    struct Config {
        // Core parameters (from V3 Optimized)
        double spacing = 1.0;
        double min_volume = 0.01;
        double max_volume = 100.0;
        double contract_size = 100.0;
        double leverage = 500.0;
        int symbol_digits = 2;
        double margin_rate = 1.0;

        // V3 Protection (proven optimal)
        double stop_new_at_dd = 5.0;
        double partial_close_at_dd = 8.0;
        double close_all_at_dd = 25.0;
        int max_positions = 20;
        double partial_close_pct = 0.50;

        // V4 Enhancements
        bool adaptive_positions = true;      // Reduce max positions as DD increases
        int max_pos_at_3pct_dd = 15;         // Max positions when DD > 3%
        int max_pos_at_5pct_dd = 10;         // Max positions when DD > 5%

        bool volatility_spacing = true;      // Widen spacing in high volatility
        int volatility_window = 50;          // Ticks to measure volatility
        double volatility_mult_max = 2.0;    // Max spacing multiplier

        int recovery_cooldown = 0;           // Ticks to wait after close-all (0 = disabled)
    };

    explicit FillUpStrategyV4(const Config& config)
        : config_(config),
          lowest_buy_(DBL_MAX),
          highest_buy_(DBL_MIN),
          volume_of_open_trades_(0.0),
          peak_equity_(0.0),
          max_number_of_open_(0),
          partial_close_done_(false),
          all_closed_(false),
          cooldown_remaining_(0),
          trades_closed_by_protection_(0),
          current_spacing_(config.spacing),
          effective_max_positions_(config.max_positions) {
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_spread_ = tick.spread();
        current_equity_ = engine.GetEquity();
        current_balance_ = engine.GetBalance();

        // Track price history for volatility
        price_history_.push_back(tick.bid);
        if (price_history_.size() > (size_t)config_.volatility_window) {
            price_history_.pop_front();
        }

        // FIX: Reset peak equity when no positions are open
        // This prevents the strategy from getting stuck when DD > stop_new_at_dd
        // but < close_all_at_dd after all positions close naturally by TP
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

        // V4: Adaptive position limit
        if (config_.adaptive_positions) {
            if (current_drawdown_pct > config_.stop_new_at_dd) {
                effective_max_positions_ = config_.max_pos_at_5pct_dd;
            } else if (current_drawdown_pct > 3.0) {
                effective_max_positions_ = config_.max_pos_at_3pct_dd;
            } else {
                effective_max_positions_ = config_.max_positions;
            }
        }

        // V4: Volatility-based spacing
        if (config_.volatility_spacing) {
            double vol = CalculateVolatility();
            double vol_mult = 1.0 + std::min(vol * 2.0, config_.volatility_mult_max - 1.0);
            current_spacing_ = config_.spacing * vol_mult;
        }

        // Cooldown check
        if (cooldown_remaining_ > 0) {
            cooldown_remaining_--;
            return;
        }

        // V3 Protection: Close ALL at threshold
        if (current_drawdown_pct > config_.close_all_at_dd && !all_closed_ && !engine.GetOpenPositions().empty()) {
            CloseAllPositions(engine);
            all_closed_ = true;
            cooldown_remaining_ = config_.recovery_cooldown;
            peak_equity_ = engine.GetBalance();
            return;
        }

        // V3 Protection: Partial close at threshold
        if (current_drawdown_pct > config_.partial_close_at_dd && !partial_close_done_ && engine.GetOpenPositions().size() > 1) {
            CloseWorstPositions(engine, config_.partial_close_pct);
            partial_close_done_ = true;
        }

        // Iterate through positions
        Iterate(engine);

        // Open new positions only if below DD threshold
        if (current_drawdown_pct < config_.stop_new_at_dd) {
            OpenNew(engine);
        }
    }

    // Statistics getters
    int GetMaxNumberOfOpen() const { return max_number_of_open_; }
    int GetTradesClosedByProtection() const { return trades_closed_by_protection_; }
    double GetCurrentSpacing() const { return current_spacing_; }
    int GetEffectiveMaxPositions() const { return effective_max_positions_; }

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
    int cooldown_remaining_;
    int trades_closed_by_protection_;

    // V4 state
    std::deque<double> price_history_;
    double current_spacing_;
    int effective_max_positions_;

    double CalculateVolatility() {
        if (price_history_.size() < 10) return 0.0;

        double sum = 0.0, sum_sq = 0.0;
        int n = price_history_.size() - 1;

        for (size_t i = 1; i < price_history_.size(); i++) {
            double change = std::abs(price_history_[i] - price_history_[i-1]);
            sum += change;
            sum_sq += change * change;
        }

        double mean = sum / n;
        double variance = (sum_sq / n) - (mean * mean);
        return std::sqrt(std::max(0.0, variance));
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

        // Sort by P/L (worst first)
        std::vector<std::pair<double, Trade*>> pl_and_trade;
        for (Trade* t : positions) {
            double pl = (current_bid_ - t->entry_price) * t->lot_size * config_.contract_size;
            pl_and_trade.push_back({pl, t});
        }
        std::sort(pl_and_trade.begin(), pl_and_trade.end());

        // Close specified percentage
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

        double tp = current_ask_ + current_spread_ + current_spacing_;

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

        // Check effective position limit
        if (positions_total >= effective_max_positions_) {
            return;
        }

        double lot_size = config_.min_volume;

        if (positions_total == 0) {
            if (Open(lot_size, engine)) {
                highest_buy_ = current_ask_;
                lowest_buy_ = current_ask_;
            }
        } else {
            if (lowest_buy_ >= current_ask_ + current_spacing_) {
                if (Open(lot_size, engine)) {
                    lowest_buy_ = current_ask_;
                }
            }
            else if (highest_buy_ <= current_ask_ - current_spacing_) {
                if (Open(lot_size, engine)) {
                    highest_buy_ = current_ask_;
                }
            }
            else if ((closest_above_ >= current_spacing_) && (closest_below_ >= current_spacing_)) {
                Open(lot_size, engine);
            }
        }
    }
};

} // namespace backtest

#endif // FILL_UP_STRATEGY_V4_H
