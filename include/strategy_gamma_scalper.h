#ifndef STRATEGY_GAMMA_SCALPER_H
#define STRATEGY_GAMMA_SCALPER_H

#include "tick_based_engine.h"
#include <deque>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace backtest {

/**
 * Gamma Scalping (Synthetic Long Gamma)
 *
 * Replicates options gamma exposure without buying options.
 * Maintains a base position and "scalps" around it by:
 * - Buying more when price drops (delta decreases)
 * - Selling some when price rises (delta increases)
 *
 * Profit comes from realized volatility: P/L ∝ ½ × Γ × (ΔS)²
 * The quadratic relationship means large moves are disproportionately profitable.
 *
 * The market's oscillations ARE the fuel - more volatility = more profit.
 * "Theta" cost is the swap fees paid for holding positions.
 */
class GammaScalper {
public:
    struct Config {
        double base_lots = 0.05;            // Base position size (always held)
        double scalp_lots = 0.01;           // Size of each gamma scalp
        double delta_band = 0.5;            // % price move to trigger rehedge
        double harvest_profit = 0.50;       // $ profit per 0.01 lot to harvest
        double max_position_lots = 0.50;    // Maximum total lots
        double min_position_lots = 0.01;    // Minimum total lots
        int lookback_ticks = 200;           // Ticks for volatility measurement
        int warmup_ticks = 500;             // Initial warmup
    };

    GammaScalper(const Config& cfg)
        : config_(cfg),
          base_established_(false),
          ticks_processed_(0),
          scalp_count_(0),
          harvest_count_(0),
          last_scalp_price_(0.0),
          peak_equity_(0.0),
          max_dd_pct_(0.0) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        ticks_processed_++;
        prices_.push_back(tick.bid);
        if (prices_.size() > (size_t)config_.lookback_ticks) {
            prices_.pop_front();
        }

        double equity = engine.GetEquity();
        if (peak_equity_ == 0.0) peak_equity_ = equity;
        if (equity > peak_equity_) peak_equity_ = equity;
        double dd = (peak_equity_ - equity) / peak_equity_ * 100.0;
        if (dd > max_dd_pct_) max_dd_pct_ = dd;

        if (ticks_processed_ < config_.warmup_ticks) return;

        // Establish base position
        if (!base_established_) {
            engine.OpenMarketOrder("BUY", config_.base_lots, 0, 0);
            base_established_ = true;
            last_scalp_price_ = tick.bid;
            return;
        }

        // Calculate current realized vol (for information)
        double realized_vol = CalculateRealizedVol();

        // Calculate total long position
        double total_longs = engine.GetBuyVolume();

        // Gamma scalping logic
        double price_move = tick.bid - last_scalp_price_;
        double move_pct = std::abs(price_move) / last_scalp_price_ * 100.0;

        if (move_pct >= config_.delta_band) {
            if (price_move < 0 && total_longs < config_.max_position_lots) {
                // Price dropped -> buy more (increase delta)
                engine.OpenMarketOrder("BUY", config_.scalp_lots, 0, 0);
                last_scalp_price_ = tick.bid;
                scalp_count_++;
            } else if (price_move > 0 && total_longs > config_.min_position_lots) {
                // Price rose -> sell some (decrease delta, harvest gamma profit)
                // Find the most profitable position to close
                Trade* best = nullptr;
                double best_pnl = -1e9;
                for (Trade* t : engine.GetOpenPositions()) {
                    if (t->IsBuy() && t != engine.GetOpenPositions().front()) {
                        double pnl = (tick.bid - t->entry_price) * t->lot_size * engine.GetConfig().contract_size;
                        if (pnl > best_pnl) {
                            best_pnl = pnl;
                            best = t;
                        }
                    }
                }
                if (best && best_pnl > 0) {
                    engine.ClosePosition(best, "GAMMA_HARVEST");
                    harvest_count_++;
                }
                last_scalp_price_ = tick.bid;
                scalp_count_++;
            }
        }

        // Also harvest individual positions that hit profit target
        HarvestProfitable(tick, engine);
    }

    int GetScalpCount() const { return scalp_count_; }
    int GetHarvestCount() const { return harvest_count_; }
    double GetMaxDDPct() const { return max_dd_pct_; }

private:
    Config config_;
    bool base_established_;
    int ticks_processed_;
    int scalp_count_;
    int harvest_count_;
    double last_scalp_price_;
    double peak_equity_;
    double max_dd_pct_;
    std::deque<double> prices_;

    double CalculateRealizedVol() {
        if (prices_.size() < 50) return 0.0;

        // Calculate returns
        std::vector<double> returns;
        for (size_t i = 1; i < prices_.size(); i++) {
            if (prices_[i-1] > 0) {
                returns.push_back(std::log(prices_[i] / prices_[i-1]));
            }
        }

        if (returns.empty()) return 0.0;

        double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
        double var = 0.0;
        for (double r : returns) {
            var += (r - mean) * (r - mean);
        }
        var /= returns.size();

        return std::sqrt(var) * std::sqrt(252.0 * 6.5 * 3600.0);  // Annualized
    }

    void HarvestProfitable(const Tick& tick, TickBasedEngine& engine) {
        auto positions = engine.GetOpenPositions();
        for (size_t i = 1; i < positions.size(); i++) {  // Skip base position (index 0)
            Trade* t = positions[i];
            if (t->IsBuy()) {
                double pnl = (tick.bid - t->entry_price) * t->lot_size * engine.GetConfig().contract_size;
                double threshold = config_.harvest_profit * (t->lot_size / 0.01);
                if (pnl >= threshold) {
                    engine.ClosePosition(t, "PROFIT_HARVEST");
                    harvest_count_++;
                    break;  // Only close one per tick to avoid iterator issues
                }
            }
        }
    }
};

} // namespace backtest

#endif // STRATEGY_GAMMA_SCALPER_H
