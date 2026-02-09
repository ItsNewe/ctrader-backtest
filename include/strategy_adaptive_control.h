/**
 * Adaptive Control Strategy - Self-Tuning Parameters
 *
 * An adaptive control system that automatically adjusts its own parameters
 * to achieve target behavior. Instead of manually tuning spacing/survive,
 * the system observes its own performance and adapts.
 *
 * Key idea:
 * - Define TARGET: e.g., "keep rolling DD < 50%" or "maintain Sharpe > 5"
 * - Observe: Recent performance metrics over rolling window
 * - Adjust: Modify parameters smoothly to move toward target
 *
 * Control rules:
 * - If rolling_dd > target_dd: INCREASE survive_pct (more conservative)
 * - If rolling_dd < target_dd * 0.5: DECREASE survive_pct (more aggressive)
 * - If rolling_sharpe < target_sharpe: WIDEN spacing (fewer trades, better quality)
 * - If rolling_sharpe > target_sharpe * 2: TIGHTEN spacing (capture more)
 */

#ifndef STRATEGY_ADAPTIVE_CONTROL_H
#define STRATEGY_ADAPTIVE_CONTROL_H

#include "tick_based_engine.h"
#include <vector>
#include <deque>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <numeric>

namespace backtest {

class StrategyAdaptiveControl {
public:
    struct Config {
        // Target behavior
        double target_max_dd;          // Target max drawdown % (e.g., 50)
        double target_min_sharpe;      // Target minimum rolling Sharpe (e.g., 5)

        // Adaptation parameters
        double adaptation_speed;       // Learning rate (0.01-0.1, higher = faster adaptation)
        int lookback_trades;           // Number of trades for rolling metrics

        // Initial strategy parameters (starting points)
        double initial_survive;        // Initial survive_pct
        double initial_spacing;        // Initial base spacing

        // Bounds for adapted parameters
        double min_survive;            // Minimum survive_pct (e.g., 10)
        double max_survive;            // Maximum survive_pct (e.g., 25)
        double min_spacing;            // Minimum spacing (e.g., 0.5)
        double max_spacing;            // Maximum spacing (e.g., 5.0)

        // Fixed parameters
        double min_volume;
        double max_volume;
        double contract_size;
        double leverage;

        Config()
            : target_max_dd(50.0),
              target_min_sharpe(5.0),
              adaptation_speed(0.05),
              lookback_trades(100),
              initial_survive(13.0),
              initial_spacing(1.5),
              min_survive(10.0),
              max_survive(25.0),
              min_spacing(0.5),
              max_spacing(5.0),
              min_volume(0.01),
              max_volume(10.0),
              contract_size(100.0),
              leverage(500.0) {}
    };

    struct TradeRecord {
        double entry_price;
        double exit_price;
        double profit;
        double duration_seconds;
        double dd_at_entry;        // Drawdown when trade was opened
    };

    struct Metrics {
        double rolling_dd;         // Max DD over lookback period
        double rolling_sharpe;     // Sharpe ratio over lookback
        double rolling_win_rate;   // Win rate over lookback
        double rolling_profit;     // Total profit over lookback
        int trades_in_window;      // Number of completed trades in window
    };

    StrategyAdaptiveControl(const Config& config)
        : config_(config),
          current_survive_(config.initial_survive),
          current_spacing_(config.initial_spacing),
          lowest_buy_(DBL_MAX),
          highest_buy_(DBL_MIN),
          volume_of_open_trades_(0.0),
          current_ask_(0.0),
          current_bid_(0.0),
          current_spread_(0.0),
          current_equity_(0.0),
          current_balance_(0.0),
          peak_equity_(0.0),
          peak_equity_in_window_(0.0),
          ticks_processed_(0),
          total_adaptations_(0),
          survive_adaptations_(0),
          spacing_adaptations_(0),
          max_dd_seen_(0.0) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_spread_ = tick.spread();
        current_equity_ = engine.GetEquity();
        current_balance_ = engine.GetBalance();
        ticks_processed_++;

        // Initialize peaks
        if (peak_equity_ == 0.0) {
            peak_equity_ = current_equity_;
            peak_equity_in_window_ = current_equity_;
        }

        // Update peaks
        if (current_equity_ > peak_equity_) {
            peak_equity_ = current_equity_;
        }
        if (current_equity_ > peak_equity_in_window_) {
            peak_equity_in_window_ = current_equity_;
        }

        // Track current DD
        double current_dd = 0.0;
        if (peak_equity_ > 0) {
            current_dd = (peak_equity_ - current_equity_) / peak_equity_ * 100.0;
        }
        if (current_dd > max_dd_seen_) {
            max_dd_seen_ = current_dd;
        }

        // Process closed trades - check if any positions were closed
        UpdateTradeRecords(engine);

        // Periodic adaptation (every 1000 ticks to avoid overhead)
        if (ticks_processed_ % 1000 == 0 && trade_history_.size() >= 10) {
            AdaptParameters();
        }

        // Process positions
        Iterate(engine);

        // Open new positions
        OpenNew(engine);
    }

    // Getters for analysis
    double GetCurrentSurvive() const { return current_survive_; }
    double GetCurrentSpacing() const { return current_spacing_; }
    double GetMaxDD() const { return max_dd_seen_; }
    int GetTotalAdaptations() const { return total_adaptations_; }
    int GetSurviveAdaptations() const { return survive_adaptations_; }
    int GetSpacingAdaptations() const { return spacing_adaptations_; }
    int GetTradeCount() const { return (int)all_trades_.size(); }
    Metrics GetCurrentMetrics() const { return CalculateMetrics(); }

private:
    Config config_;

    // Adaptive parameters
    double current_survive_;
    double current_spacing_;

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
    double peak_equity_in_window_;

    // Trade history for rolling metrics
    std::deque<TradeRecord> trade_history_;   // Rolling window
    std::vector<TradeRecord> all_trades_;     // All trades for final stats
    std::vector<double> equity_history_;      // For DD calculation

    // Statistics
    long ticks_processed_;
    int total_adaptations_;
    int survive_adaptations_;
    int spacing_adaptations_;
    double max_dd_seen_;

    // Track open positions to detect closures
    std::vector<int> last_open_position_ids_;

    void UpdateTradeRecords(TickBasedEngine& engine) {
        // Get current open positions
        std::vector<int> current_ids;
        for (const Trade* trade : engine.GetOpenPositions()) {
            current_ids.push_back(trade->id);
        }

        // Check for closed positions by comparing with last known open
        for (int old_id : last_open_position_ids_) {
            bool still_open = std::find(current_ids.begin(), current_ids.end(), old_id) != current_ids.end();
            if (!still_open) {
                // Position was closed - find it in closed positions
                for (const Trade& trade : engine.GetClosedTrades()) {
                    if (trade.id == old_id) {
                        TradeRecord record;
                        record.entry_price = trade.entry_price;
                        record.exit_price = trade.exit_price;
                        record.profit = trade.profit_loss;
                        record.duration_seconds = 0;  // Could parse timestamps
                        record.dd_at_entry = (peak_equity_ > 0)
                            ? (peak_equity_ - current_equity_) / peak_equity_ * 100.0
                            : 0.0;

                        trade_history_.push_back(record);
                        all_trades_.push_back(record);

                        // Keep rolling window size
                        while ((int)trade_history_.size() > config_.lookback_trades) {
                            trade_history_.pop_front();
                        }
                        break;
                    }
                }
            }
        }

        last_open_position_ids_ = current_ids;

        // Track equity for DD calculation
        equity_history_.push_back(current_equity_);
        if (equity_history_.size() > 10000) {
            equity_history_.erase(equity_history_.begin(), equity_history_.begin() + 5000);
        }
    }

    Metrics CalculateMetrics() const {
        Metrics m;
        m.rolling_dd = 0;
        m.rolling_sharpe = 0;
        m.rolling_win_rate = 0;
        m.rolling_profit = 0;
        m.trades_in_window = (int)trade_history_.size();

        if (trade_history_.empty()) {
            return m;
        }

        // Calculate rolling profit and win rate
        int wins = 0;
        double sum_profit = 0;
        std::vector<double> profits;

        for (const auto& trade : trade_history_) {
            profits.push_back(trade.profit);
            sum_profit += trade.profit;
            if (trade.profit > 0) wins++;
        }

        m.rolling_profit = sum_profit;
        m.rolling_win_rate = (trade_history_.size() > 0)
            ? (double)wins / trade_history_.size() * 100.0
            : 0;

        // Calculate rolling Sharpe ratio
        if (profits.size() >= 2) {
            double mean = sum_profit / profits.size();
            double sum_sq = 0;
            for (double p : profits) {
                sum_sq += (p - mean) * (p - mean);
            }
            double stddev = std::sqrt(sum_sq / profits.size());
            if (stddev > 0.001) {
                // Annualized Sharpe (assume ~250 trading days, scale by sqrt)
                m.rolling_sharpe = (mean / stddev) * std::sqrt(250.0 / profits.size());
            } else {
                m.rolling_sharpe = (mean > 0) ? 100.0 : 0.0;
            }
        }

        // Calculate rolling DD from equity history window
        if (equity_history_.size() > 100) {
            size_t start = equity_history_.size() > 5000 ? equity_history_.size() - 5000 : 0;
            double window_peak = equity_history_[start];
            double window_max_dd = 0;

            for (size_t i = start; i < equity_history_.size(); i++) {
                if (equity_history_[i] > window_peak) {
                    window_peak = equity_history_[i];
                }
                double dd = (window_peak - equity_history_[i]) / window_peak * 100.0;
                if (dd > window_max_dd) {
                    window_max_dd = dd;
                }
            }
            m.rolling_dd = window_max_dd;
        }

        return m;
    }

    void AdaptParameters() {
        Metrics m = CalculateMetrics();
        bool adapted = false;

        // ========================================
        // Rule 1: DD-based survive adjustment
        // ========================================
        if (m.rolling_dd > config_.target_max_dd) {
            // DD too high - need to be more conservative (increase survive)
            double target = current_survive_ + (m.rolling_dd - config_.target_max_dd) * 0.1;
            target = std::min(target, config_.max_survive);

            double delta = (target - current_survive_) * config_.adaptation_speed;
            if (std::abs(delta) > 0.1) {
                current_survive_ += delta;
                current_survive_ = std::max(config_.min_survive,
                                           std::min(config_.max_survive, current_survive_));
                survive_adaptations_++;
                adapted = true;
            }
        } else if (m.rolling_dd < config_.target_max_dd * 0.5) {
            // DD well below target - can be more aggressive (decrease survive)
            double target = current_survive_ - (config_.target_max_dd * 0.5 - m.rolling_dd) * 0.05;
            target = std::max(target, config_.min_survive);

            double delta = (target - current_survive_) * config_.adaptation_speed;
            if (std::abs(delta) > 0.1) {
                current_survive_ += delta;
                current_survive_ = std::max(config_.min_survive,
                                           std::min(config_.max_survive, current_survive_));
                survive_adaptations_++;
                adapted = true;
            }
        }

        // ========================================
        // Rule 2: Sharpe-based spacing adjustment
        // ========================================
        if (m.rolling_sharpe < config_.target_min_sharpe && m.trades_in_window >= 20) {
            // Sharpe too low - widen spacing (fewer but better trades)
            double sharpe_gap = config_.target_min_sharpe - m.rolling_sharpe;
            double target = current_spacing_ + sharpe_gap * 0.1;
            target = std::min(target, config_.max_spacing);

            double delta = (target - current_spacing_) * config_.adaptation_speed;
            if (std::abs(delta) > 0.05) {
                current_spacing_ += delta;
                current_spacing_ = std::max(config_.min_spacing,
                                           std::min(config_.max_spacing, current_spacing_));
                spacing_adaptations_++;
                adapted = true;
            }
        } else if (m.rolling_sharpe > config_.target_min_sharpe * 2.0 && m.trades_in_window >= 20) {
            // Sharpe well above target - tighten spacing (capture more)
            double sharpe_excess = m.rolling_sharpe - config_.target_min_sharpe * 2.0;
            double target = current_spacing_ - sharpe_excess * 0.05;
            target = std::max(target, config_.min_spacing);

            double delta = (target - current_spacing_) * config_.adaptation_speed;
            if (std::abs(delta) > 0.05) {
                current_spacing_ += delta;
                current_spacing_ = std::max(config_.min_spacing,
                                           std::min(config_.max_spacing, current_spacing_));
                spacing_adaptations_++;
                adapted = true;
            }
        }

        if (adapted) {
            total_adaptations_++;
        }
    }

    void Iterate(TickBasedEngine& engine) {
        // Use engine's incrementally-maintained aggregates (O(1) instead of O(N))
        volume_of_open_trades_ = engine.GetBuyVolume();
        lowest_buy_ = engine.GetLowestBuyEntry();
        highest_buy_ = engine.GetHighestBuyEntry();
    }

    double CalculateLotSize(TickBasedEngine& engine, int positions_total) {
        // Use engine's authoritative margin (current market prices, includes margin_rate)
        double used_margin = engine.GetUsedMargin();

        double margin_stop_out = 20.0;

        double end_price = (positions_total == 0)
            ? current_ask_ * ((100.0 - current_survive_) / 100.0)
            : highest_buy_ * ((100.0 - current_survive_) / 100.0);

        double distance = current_ask_ - end_price;
        double number_of_trades = std::floor(distance / current_spacing_);
        if (number_of_trades <= 0) number_of_trades = 1;

        double equity_at_target = current_equity_ - volume_of_open_trades_ * distance * config_.contract_size;
        if (used_margin > 0 && (equity_at_target / used_margin * 100.0) < margin_stop_out) {
            return 0.0;
        }

        double trade_size = config_.min_volume;
        double d_equity = config_.contract_size * trade_size * current_spacing_ * (number_of_trades * (number_of_trades + 1) / 2);
        double d_margin = engine.CalculateMarginRequired(trade_size, current_ask_) * number_of_trades;

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

        double final_lots = engine.NormalizeLots(std::min(lots, config_.max_volume));

        double tp = current_ask_ + current_spread_ + current_spacing_;
        Trade* trade = engine.OpenMarketOrder("BUY", final_lots, 0.0, tp);
        return (trade != nullptr);
    }

    void OpenNew(TickBasedEngine& engine) {
        int positions_total = (int)engine.GetOpenPositions().size();

        if (positions_total == 0) {
            double lots = CalculateLotSize(engine, positions_total);
            if (Open(lots, engine)) {
                highest_buy_ = current_ask_;
                lowest_buy_ = current_ask_;
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

#endif // STRATEGY_ADAPTIVE_CONTROL_H
