#ifndef FILL_UP_STRATEGY_V5_H
#define FILL_UP_STRATEGY_V5_H

#include "tick_based_engine.h"
#include <vector>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <deque>

namespace backtest {

/**
 * Fill-Up Grid Trading Strategy V5/V6
 *
 * V5 Key improvement: SMA Trend Filter
 * - Based on V3 protection logic (5/8/25 DD thresholds)
 * - Adds SMA 11000 trend filter: only open positions when price > SMA
 * - This prevents opening new positions during sustained downtrends
 * - Empirically shown to significantly reduce drawdown during crashes
 *
 * V6 Improvement: Wider Take Profit (Optional)
 * - Adds tp_multiplier config parameter (default 2.0)
 * - TP calculation: ask + spread + (spacing * tp_multiplier)
 * - Testing shows 2.0x wider TP improves total returns
 * - Reduces premature exits during favorable moves
 *
 * Why SMA 11000?
 * - Empirical testing showed SMA 11000 provides optimal trend identification
 * - Filters out the entire December crash period
 * - Allows strategy to "sit out" dangerous market conditions
 */
class FillUpStrategyV5 {
public:
    struct Config {
        // Original parameters (from FillUpStrategy)
        double survive_pct = 13.0;
        double size_multiplier = 1.0;
        double spacing = 1.0;
        double min_volume = 0.01;
        double max_volume = 100.0;
        double contract_size = 100.0;
        double leverage = 500.0;
        int symbol_digits = 2;
        double margin_rate = 1.0;

        // V3 Protection (unchanged from V3)
        double stop_new_at_dd = 5.0;
        double partial_close_at_dd = 8.0;
        double close_all_at_dd = 25.0;
        int max_positions = 20;
        double reduce_size_at_dd = 3.0;

        // V5 Trend Filter
        int ma_period = 11000;  // SMA period for trend filter

        // V6 Improvement: Wider TP
        double tp_multiplier = 2.0;  // Multiplier for take profit (2.0 = 2x wider TP)
    };

    explicit FillUpStrategyV5(const Config& config)
        : config_(config),
          lowest_buy_(DBL_MAX),
          highest_buy_(DBL_MIN),
          volume_of_open_trades_(0.0),
          trade_size_buy_(0.0),
          peak_equity_(0.0),
          max_number_of_open_(0),
          max_used_funds_(0.0),
          max_trade_size_(0.0),
          partial_close_done_(false),
          all_closed_(false),
          trades_closed_by_protection_(0),
          buffer_index_(0),
          ticks_seen_(0),
          running_sum_(0.0),
          sma_value_(0.0) {
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_spread_ = tick.spread();
        current_equity_ = engine.GetEquity();
        current_balance_ = engine.GetBalance();

        // V5: Update SMA
        UpdateSMA(tick);

        // FIX: Reset peak equity when no positions are open
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

        // V3 Protection: Close ALL positions if DD exceeds threshold
        if (current_drawdown_pct > config_.close_all_at_dd && !all_closed_ && !engine.GetOpenPositions().empty()) {
            CloseAllPositions(engine);
            all_closed_ = true;
            peak_equity_ = engine.GetBalance();
            return;
        }

        // V3 Protection: Partial close at DD threshold
        if (current_drawdown_pct > config_.partial_close_at_dd && !partial_close_done_ && engine.GetOpenPositions().size() > 1) {
            CloseHalfPositions(engine);
            partial_close_done_ = true;
        }

        // Iterate through positions
        Iterate(engine);

        // V5: Check trend filter BEFORE opening new positions
        // Only open if:
        // 1. Below DD threshold (V3 protection)
        // 2. Price is above SMA (V5 trend filter)
        bool trend_ok = IsTrendOk();
        if (current_drawdown_pct < config_.stop_new_at_dd && trend_ok) {
            OpenNew(engine, current_drawdown_pct);
        }
    }

    // Statistics getters
    int GetMaxNumberOfOpen() const { return max_number_of_open_; }
    double GetMaxUsedFunds() const { return max_used_funds_; }
    double GetMaxTradeSize() const { return max_trade_size_; }
    int GetTradesClosedByProtection() const { return trades_closed_by_protection_; }
    double GetSMA() const { return sma_value_; }

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

    // Trade sizing
    double trade_size_buy_;

    // Statistics
    double peak_equity_;
    int max_number_of_open_;
    double max_used_funds_;
    double max_trade_size_;

    // Protection state
    bool partial_close_done_;
    bool all_closed_;
    int trades_closed_by_protection_;

    // V5: Trend filter - optimized with running sum
    std::vector<double> price_buffer_;
    size_t buffer_index_;
    size_t ticks_seen_;
    double running_sum_;
    double sma_value_;

    void UpdateSMA(const Tick& tick) {
        double price = tick.bid;

        // Initialize buffer on first call
        if (price_buffer_.empty()) {
            price_buffer_.resize(config_.ma_period, 0.0);
            buffer_index_ = 0;
            ticks_seen_ = 0;
            running_sum_ = 0.0;
        }

        // Update running sum
        if (ticks_seen_ >= (size_t)config_.ma_period) {
            running_sum_ -= price_buffer_[buffer_index_];  // Remove old value
        }
        running_sum_ += price;  // Add new value
        price_buffer_[buffer_index_] = price;
        ticks_seen_++;

        // Move to next position (circular buffer)
        buffer_index_ = (buffer_index_ + 1) % config_.ma_period;

        // Calculate SMA (valid once we've filled the buffer once)
        if (ticks_seen_ >= (size_t)config_.ma_period) {
            sma_value_ = running_sum_ / config_.ma_period;
        } else {
            sma_value_ = 0.0;
        }
    }

    bool IsTrendOk() const {
        // Need valid SMA (non-zero means we have enough data)
        if (sma_value_ == 0.0) {
            return false;  // Don't trade until we have full SMA
        }

        // Only open positions when price is above SMA
        return current_bid_ > sma_value_;
    }

    void CloseAllPositions(TickBasedEngine& engine) {
        auto& positions = engine.GetOpenPositions();
        while (!positions.empty()) {
            engine.ClosePosition(positions[0]);
            trades_closed_by_protection_++;
        }
    }

    void CloseHalfPositions(TickBasedEngine& engine) {
        auto& positions = engine.GetOpenPositions();
        if (positions.size() <= 1) return;

        // Sort by P/L (close worst performers first)
        std::vector<std::pair<double, Trade*>> pl_and_trade;
        for (Trade* t : positions) {
            double pl = (current_bid_ - t->entry_price) * t->lot_size * config_.contract_size;
            pl_and_trade.push_back({pl, t});
        }
        std::sort(pl_and_trade.begin(), pl_and_trade.end());

        // Close half
        int to_close = pl_and_trade.size() / 2;
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

    void SizingBuy(TickBasedEngine& engine, int positions_total, double current_dd_pct) {
        trade_size_buy_ = 0.0;

        if (positions_total >= config_.max_positions) {
            return;
        }

        // V3: Reduce size based on drawdown
        double size_reduction = 1.0;
        if (current_dd_pct > config_.reduce_size_at_dd) {
            double dd_range = config_.stop_new_at_dd - config_.reduce_size_at_dd;
            double dd_progress = (current_dd_pct - config_.reduce_size_at_dd) / dd_range;
            size_reduction = 1.0 - (dd_progress * 0.75);
            size_reduction = std::max(0.25, size_reduction);
        }

        // Original sizing logic
        double used_margin = CalculateUsedMargin(engine);
        double margin_stop_out_level = 20.0;
        double current_margin_level = (used_margin > 0) ? (current_equity_ / used_margin * 100.0) : 10000.0;

        double equity_at_target = current_equity_ * margin_stop_out_level / current_margin_level;
        double equity_difference = current_equity_ - equity_at_target;

        double price_difference = 0.0;
        if (volume_of_open_trades_ > 0) {
            price_difference = equity_difference / (volume_of_open_trades_ * config_.contract_size);
        }

        double end_price = 0.0;
        if (positions_total == 0) {
            end_price = current_ask_ * ((100.0 - config_.survive_pct) / 100.0);
        } else {
            end_price = highest_buy_ * ((100.0 - config_.survive_pct) / 100.0);
        }

        double distance = current_ask_ - end_price;
        double number_of_trades = std::floor(distance / config_.spacing);

        number_of_trades = std::min(number_of_trades, (double)(config_.max_positions - positions_total));

        if ((positions_total == 0) || ((current_ask_ - price_difference) < end_price)) {
            equity_at_target = current_equity_ - volume_of_open_trades_ * std::abs(distance) * config_.contract_size;
            double margin_level = (used_margin > 0) ? (equity_at_target / used_margin * 100.0) : 10000.0;

            double trade_size = config_.min_volume;

            if (margin_level > margin_stop_out_level) {
                double d_equity = config_.contract_size * trade_size * (number_of_trades * (number_of_trades + 1) / 2);
                double d_spread = number_of_trades * trade_size * current_spread_ * config_.contract_size;
                d_equity += d_spread;

                double local_used_margin = CalculateMarginForSizing(trade_size);
                local_used_margin = number_of_trades * local_used_margin;

                double multiplier = 0.0;
                double equity_backup = equity_at_target;
                double used_margin_backup = used_margin;
                double max = config_.max_volume / config_.min_volume;

                equity_at_target -= max * d_equity;
                used_margin += max * local_used_margin;

                if (margin_stop_out_level < (equity_at_target / used_margin * 100.0)) {
                    multiplier = max;
                } else {
                    used_margin = used_margin_backup;
                    equity_at_target = equity_backup;

                    for (double increment = max; increment >= 1; increment = increment / 10) {
                        while (margin_stop_out_level < (equity_at_target / used_margin * 100.0)) {
                            equity_backup = equity_at_target;
                            used_margin_backup = used_margin;
                            multiplier += increment;
                            equity_at_target -= increment * d_equity;
                            used_margin += increment * local_used_margin;
                        }
                        multiplier -= increment;
                        used_margin = used_margin_backup;
                        equity_at_target = equity_backup;
                    }
                }

                multiplier = std::max(1.0, multiplier);
                trade_size_buy_ = multiplier * config_.min_volume;

                // V3: Apply size reduction
                trade_size_buy_ *= size_reduction;

                trade_size_buy_ = std::min(trade_size_buy_, config_.max_volume);
                trade_size_buy_ = std::max(trade_size_buy_, config_.min_volume);
                max_trade_size_ = std::max(max_trade_size_, trade_size_buy_);
            }
        }
    }

    double CalculateUsedMargin(TickBasedEngine& engine) {
        double used_margin = 0.0;
        const auto& positions = engine.GetOpenPositions();
        for (const Trade* trade : positions) {
            used_margin += CalculateMarginForTrade(trade->lot_size, trade->entry_price);
        }
        return used_margin;
    }

    double CalculateMarginForTrade(double lots, double price) {
        return lots * config_.contract_size * price / config_.leverage * config_.margin_rate;
    }

    double CalculateMarginForSizing(double lots) {
        return lots * config_.contract_size / config_.leverage * config_.margin_rate;
    }

    bool Open(double local_unit, TickBasedEngine& engine) {
        if (local_unit < config_.min_volume) {
            return false;
        }

        double final_unit = std::min(local_unit, config_.max_volume);
        final_unit = NormalizeVolume(final_unit);

        // V6: Apply TP multiplier for wider take profit
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

    void OpenNew(TickBasedEngine& engine, double current_dd_pct) {
        int positions_total = engine.GetOpenPositions().size();

        if (positions_total >= config_.max_positions) {
            return;
        }

        if (positions_total == 0) {
            SizingBuy(engine, positions_total, current_dd_pct);
            if (Open(trade_size_buy_, engine)) {
                highest_buy_ = current_ask_;
                lowest_buy_ = current_ask_;
            }
        } else {
            if (lowest_buy_ >= current_ask_ + config_.spacing) {
                SizingBuy(engine, positions_total, current_dd_pct);
                if (Open(trade_size_buy_, engine)) {
                    lowest_buy_ = current_ask_;
                }
            }
            else if (highest_buy_ <= current_ask_ - config_.spacing) {
                SizingBuy(engine, positions_total, current_dd_pct);
                if (Open(trade_size_buy_, engine)) {
                    highest_buy_ = current_ask_;
                }
            }
            else if ((closest_above_ >= config_.spacing) && (closest_below_ >= config_.spacing)) {
                SizingBuy(engine, positions_total, current_dd_pct);
                Open(trade_size_buy_, engine);
            }
        }
    }
};

} // namespace backtest

#endif // FILL_UP_STRATEGY_V5_H
