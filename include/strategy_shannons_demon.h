#ifndef STRATEGY_SHANNONS_DEMON_H
#define STRATEGY_SHANNONS_DEMON_H

#include "tick_based_engine.h"
#include <deque>
#include <cmath>
#include <algorithm>

namespace backtest {

/**
 * Shannon's Demon (Volatility Pumping) - Leveraged Adaptation
 *
 * In its original form, Shannon's Demon maintains a 50/50 stock/cash ratio.
 * For leveraged CFDs, we adapt: maintain a TARGET NOTIONAL EXPOSURE and
 * rebalance when price moves cause the exposure to drift.
 *
 * When price RISES: notional grows -> SELL some (take profit)
 * When price FALLS: notional shrinks -> BUY some (average in)
 *
 * This is the leveraged equivalent of buying low / selling high mechanically.
 * Expected excess return ≈ σ²/2 from the rebalancing premium, minus swap costs.
 *
 * The market's natural oscillations do the work - no prediction needed.
 */
class ShannonsDemon {
public:
    struct Config {
        double target_exposure_pct = 200.0;     // Target notional as % of equity (200% = 2x)
        double rebalance_threshold_pct = 15.0;  // Rebalance when exposure drifts ±15%
        double min_trade_size = 0.01;           // Minimum lot size
        double max_position_lots = 0.50;        // Maximum total lots
        double contract_size = 100.0;
        double leverage = 500.0;
        int warmup_ticks = 100;
        double rebalance_fraction = 0.5;        // Rebalance 50% of the way back to target
    };

    ShannonsDemon(const Config& cfg)
        : config_(cfg),
          ticks_processed_(0),
          rebalance_count_(0),
          buys_(0),
          sells_(0),
          total_lots_open_(0.0),
          peak_equity_(0.0),
          max_dd_pct_(0.0) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        ticks_processed_++;

        double equity = engine.GetEquity();
        if (equity <= 0) return;

        if (peak_equity_ == 0.0) peak_equity_ = equity;
        if (equity > peak_equity_) peak_equity_ = equity;
        double dd_pct = (peak_equity_ - equity) / peak_equity_ * 100.0;
        if (dd_pct > max_dd_pct_) max_dd_pct_ = dd_pct;

        if (ticks_processed_ < config_.warmup_ticks) return;

        // Calculate current notional exposure
        total_lots_open_ = 0.0;
        for (const Trade* t : engine.GetOpenPositions()) {
            if (t->direction == "BUY") total_lots_open_ += t->lot_size;
            else total_lots_open_ -= t->lot_size;
        }

        double notional = total_lots_open_ * tick.bid * config_.contract_size;
        double current_exposure_pct = (notional / equity) * 100.0;

        // Calculate target exposure in lots
        double target_notional = equity * config_.target_exposure_pct / 100.0;
        double target_lots = target_notional / (tick.bid * config_.contract_size);
        target_lots = std::min(target_lots, config_.max_position_lots);

        // Check if rebalancing needed
        double drift_pct = current_exposure_pct - config_.target_exposure_pct;

        if (std::abs(drift_pct) > config_.rebalance_threshold_pct) {
            // Calculate how many lots to adjust
            double target_after_rebal = config_.target_exposure_pct +
                                        drift_pct * (1.0 - config_.rebalance_fraction);
            double target_notional_rebal = equity * target_after_rebal / 100.0;
            double target_lots_rebal = target_notional_rebal / (tick.bid * config_.contract_size);
            target_lots_rebal = std::min(target_lots_rebal, config_.max_position_lots);

            double lots_diff = target_lots_rebal - total_lots_open_;
            double lots_to_trade = std::abs(lots_diff);
            lots_to_trade = std::floor(lots_to_trade / config_.min_trade_size) * config_.min_trade_size;

            if (lots_to_trade >= config_.min_trade_size) {
                if (lots_diff > 0) {
                    // Under-exposed: BUY more (price fell)
                    double buy_lots = std::min(lots_to_trade,
                                               config_.max_position_lots - std::max(0.0, total_lots_open_));
                    if (buy_lots >= config_.min_trade_size) {
                        buy_lots = std::floor(buy_lots / config_.min_trade_size) * config_.min_trade_size;
                        engine.OpenMarketOrder("BUY", buy_lots, 0, 0);
                        rebalance_count_++;
                        buys_++;
                    }
                } else {
                    // Over-exposed: SELL some (price rose)
                    double sell_lots = lots_to_trade;
                    auto positions = engine.GetOpenPositions();
                    for (int i = (int)positions.size() - 1; i >= 0 && sell_lots >= config_.min_trade_size; i--) {
                        Trade* pos = positions[i];
                        if (pos->direction == "BUY" && pos->lot_size <= sell_lots) {
                            sell_lots -= pos->lot_size;
                            engine.ClosePosition(pos, "REBALANCE_SELL");
                        }
                    }
                    rebalance_count_++;
                    sells_++;
                }
            }
        }

        // Initial position if none exists
        if (total_lots_open_ <= 0 && engine.GetOpenPositions().empty()) {
            double init_lots = target_lots;
            init_lots = std::floor(init_lots / config_.min_trade_size) * config_.min_trade_size;
            init_lots = std::min(init_lots, config_.max_position_lots);
            if (init_lots >= config_.min_trade_size) {
                engine.OpenMarketOrder("BUY", init_lots, 0, 0);
                rebalance_count_++;
                buys_++;
            }
        }
    }

    int GetRebalanceCount() const { return rebalance_count_; }
    int GetBuys() const { return buys_; }
    int GetSells() const { return sells_; }
    double GetMaxDDPct() const { return max_dd_pct_; }
    double GetTotalLotsOpen() const { return total_lots_open_; }

private:
    Config config_;
    int ticks_processed_;
    int rebalance_count_;
    int buys_;
    int sells_;
    double total_lots_open_;
    double peak_equity_;
    double max_dd_pct_;
};

} // namespace backtest

#endif // STRATEGY_SHANNONS_DEMON_H
