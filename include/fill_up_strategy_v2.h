#ifndef FILL_UP_STRATEGY_V2_H
#define FILL_UP_STRATEGY_V2_H

#include "tick_based_engine.h"
#include <vector>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <deque>

namespace backtest {

/**
 * Fill-Up Grid Trading Strategy V2
 *
 * Improvements over V1:
 * 1. Max drawdown circuit breaker - stops trading if drawdown exceeds threshold
 * 2. Position count limit - caps maximum open positions
 * 3. Adaptive spacing - widens spacing in volatile conditions
 * 4. Cool-down after losses - pauses trading after consecutive losing closes
 * 5. Trend detection - reduces position size in strong downtrends
 * 6. Recovery mode - more conservative after drawdown recovery begins
 */
class FillUpStrategyV2 {
public:
    struct Config {
        // Original parameters
        double survive_pct = 13.0;
        double size_multiplier = 1.0;
        double spacing = 1.0;
        double min_volume = 0.01;
        double max_volume = 100.0;
        double contract_size = 100.0;
        double leverage = 500.0;
        int symbol_digits = 2;
        double margin_rate = 1.0;

        // V2 improvements
        double max_drawdown_pct = 50.0;       // Stop trading if DD exceeds this
        int max_positions = 50;                // Maximum open positions
        bool adaptive_spacing = true;          // Enable volatility-based spacing
        int cooldown_after_losses = 3;         // Pause after N consecutive losses
        bool trend_filter = true;              // Reduce size in downtrends
        double recovery_threshold = 0.8;       // % of peak to exit recovery mode

        // Volatility parameters
        int volatility_window = 100;           // Ticks to measure volatility
        double volatility_spacing_mult = 2.0;  // Spacing multiplier when volatile
        double volatility_threshold = 0.5;     // Threshold to trigger adaptive spacing
    };

    explicit FillUpStrategyV2(const Config& config)
        : config_(config),
          lowest_buy_(DBL_MAX),
          highest_buy_(DBL_MIN),
          volume_of_open_trades_(0.0),
          trade_size_buy_(0.0),
          effective_spacing_(config.spacing),
          max_balance_(0.0),
          peak_equity_(0.0),
          max_number_of_open_(0),
          max_used_funds_(0.0),
          max_trade_size_(0.0),
          consecutive_losses_(0),
          is_paused_(false),
          in_recovery_mode_(false),
          total_trades_opened_(0),
          total_trades_closed_(0),
          debug_counter_(0) {
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_spread_ = tick.spread();
        current_equity_ = engine.GetEquity();
        current_balance_ = engine.GetBalance();

        // Track price history for volatility calculation
        price_history_.push_back(tick.bid);
        if (price_history_.size() > (size_t)config_.volatility_window) {
            price_history_.pop_front();
        }

        // Update peak equity tracking
        if (current_equity_ > peak_equity_) {
            peak_equity_ = current_equity_;
            in_recovery_mode_ = false;  // Exit recovery mode when new high
        }

        // Calculate current drawdown
        double current_drawdown_pct = 0.0;
        if (peak_equity_ > 0) {
            current_drawdown_pct = (peak_equity_ - current_equity_) / peak_equity_ * 100.0;
        }

        // Check circuit breaker
        if (current_drawdown_pct > config_.max_drawdown_pct) {
            is_paused_ = true;
            if (debug_counter_ < 3) {
                std::cout << "[V2] CIRCUIT BREAKER TRIGGERED! DD=" << current_drawdown_pct << "%" << std::endl;
            }
        }

        // Check if entering recovery mode
        if (current_drawdown_pct > config_.max_drawdown_pct * 0.5 && !in_recovery_mode_) {
            in_recovery_mode_ = true;
            if (debug_counter_ < 5) {
                std::cout << "[V2] Entering RECOVERY MODE at DD=" << current_drawdown_pct << "%" << std::endl;
            }
        }

        // Update statistics
        max_balance_ = std::max(max_balance_, current_balance_);
        max_number_of_open_ = std::max(max_number_of_open_, (int)engine.GetOpenPositions().size());

        // Calculate effective spacing (adaptive)
        if (config_.adaptive_spacing) {
            double volatility = CalculateVolatility();
            if (volatility > config_.volatility_threshold) {
                effective_spacing_ = config_.spacing * config_.volatility_spacing_mult;
            } else {
                effective_spacing_ = config_.spacing;
            }
        }

        // Iterate through positions
        Iterate(engine);

        // Check for new trade opportunities (if not paused)
        if (!is_paused_ && consecutive_losses_ < config_.cooldown_after_losses) {
            OpenNew(engine);
        }
    }

    // Statistics getters
    double GetMaxBalance() const { return max_balance_; }
    int GetMaxNumberOfOpen() const { return max_number_of_open_; }
    double GetMaxUsedFunds() const { return max_used_funds_; }
    double GetMaxTradeSize() const { return max_trade_size_; }
    bool IsPaused() const { return is_paused_; }
    bool InRecoveryMode() const { return in_recovery_mode_; }
    int GetConsecutiveLosses() const { return consecutive_losses_; }
    double GetEffectiveSpacing() const { return effective_spacing_; }

    // Called when a trade closes (for tracking consecutive losses)
    void OnTradeClose(double profit_loss) {
        total_trades_closed_++;
        if (profit_loss < 0) {
            consecutive_losses_++;
        } else {
            consecutive_losses_ = 0;  // Reset on profitable trade
            // Can unpause if was paused due to cooldown
            if (is_paused_ && !CircuitBreakerActive()) {
                is_paused_ = false;
            }
        }
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

    // Trade sizing
    double trade_size_buy_;
    double effective_spacing_;

    // Statistics
    double max_balance_;
    double peak_equity_;
    int max_number_of_open_;
    double max_used_funds_;
    double max_trade_size_;

    // V2 state tracking
    int consecutive_losses_;
    bool is_paused_;
    bool in_recovery_mode_;
    int total_trades_opened_;
    int total_trades_closed_;
    std::deque<double> price_history_;

    int debug_counter_;

    bool CircuitBreakerActive() const {
        if (peak_equity_ <= 0) return false;
        double dd = (peak_equity_ - current_equity_) / peak_equity_ * 100.0;
        return dd > config_.max_drawdown_pct;
    }

    double CalculateVolatility() const {
        if (price_history_.size() < 10) return 0.0;

        // Calculate standard deviation of price changes
        double sum = 0.0;
        double sum_sq = 0.0;
        int n = price_history_.size() - 1;

        for (size_t i = 1; i < price_history_.size(); i++) {
            double change = price_history_[i] - price_history_[i-1];
            sum += change;
            sum_sq += change * change;
        }

        double mean = sum / n;
        double variance = (sum_sq / n) - (mean * mean);
        return std::sqrt(std::max(0.0, variance));
    }

    double CalculateTrendStrength() const {
        if (price_history_.size() < 20) return 0.0;

        // Simple linear regression slope
        int n = price_history_.size();
        double sum_x = 0, sum_y = 0, sum_xy = 0, sum_xx = 0;

        for (int i = 0; i < n; i++) {
            sum_x += i;
            sum_y += price_history_[i];
            sum_xy += i * price_history_[i];
            sum_xx += i * i;
        }

        double slope = (n * sum_xy - sum_x * sum_y) / (n * sum_xx - sum_x * sum_x);
        return slope;  // Negative = downtrend
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

    void SizingBuy(TickBasedEngine& engine, int positions_total) {
        trade_size_buy_ = 0.0;

        // V2: Check position limit
        if (positions_total >= config_.max_positions) {
            return;
        }

        // V2: Reduce size in recovery mode
        double size_reduction = 1.0;
        if (in_recovery_mode_) {
            size_reduction = 0.5;  // Half size in recovery
        }

        // V2: Reduce size in strong downtrends
        if (config_.trend_filter) {
            double trend = CalculateTrendStrength();
            if (trend < -0.1) {  // Strong downtrend
                size_reduction *= 0.5;
            } else if (trend < -0.05) {  // Moderate downtrend
                size_reduction *= 0.75;
            }
        }

        // Original sizing logic with modifications
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
        double number_of_trades = std::floor(distance / effective_spacing_);

        // V2: Limit number of trades based on max_positions
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

                // V2: Apply size reduction
                trade_size_buy_ *= size_reduction;

                trade_size_buy_ = std::min(trade_size_buy_, config_.max_volume);
                trade_size_buy_ = std::max(trade_size_buy_, config_.min_volume);
                max_trade_size_ = std::max(max_trade_size_, trade_size_buy_);

                if (debug_counter_ < 5) {
                    std::cout << "[V2] Sizing: mult=" << multiplier
                              << " reduction=" << size_reduction
                              << " final=" << trade_size_buy_
                              << " positions=" << positions_total
                              << " spacing=" << effective_spacing_
                              << std::endl;
                    debug_counter_++;
                }
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

        double tp = current_ask_ + current_spread_ + effective_spacing_;

        Trade* trade = engine.OpenMarketOrder("BUY", final_unit, 0.0, tp);

        if (trade != nullptr) {
            total_trades_opened_++;
            return true;
        }
        return false;
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

        // V2: Check position limit
        if (positions_total >= config_.max_positions) {
            return;
        }

        if (positions_total == 0) {
            SizingBuy(engine, positions_total);
            if (Open(trade_size_buy_, engine)) {
                highest_buy_ = current_ask_;
                lowest_buy_ = current_ask_;
            }
        } else {
            if (lowest_buy_ >= current_ask_ + effective_spacing_) {
                SizingBuy(engine, positions_total);
                if (Open(trade_size_buy_, engine)) {
                    lowest_buy_ = current_ask_;
                }
            }
            else if (highest_buy_ <= current_ask_ - effective_spacing_) {
                SizingBuy(engine, positions_total);
                if (Open(trade_size_buy_, engine)) {
                    highest_buy_ = current_ask_;
                }
            }
            else if ((closest_above_ >= effective_spacing_) && (closest_below_ >= effective_spacing_)) {
                SizingBuy(engine, positions_total);
                Open(trade_size_buy_, engine);
            }
        }
    }
};

} // namespace backtest

#endif // FILL_UP_STRATEGY_V2_H
