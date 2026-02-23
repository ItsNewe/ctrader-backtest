#pragma once
/**
 * Dynamic Hedging Strategy
 *
 * Philosophy: Always have skin in the game BOTH ways
 * Uses TickBasedEngine for all position/margin/balance management.
 */

#include "tick_based_engine.h"
#include <deque>
#include <cmath>
#include <algorithm>

namespace backtest {

struct DynamicHedgeConfig {
    double base_lot_size = 0.1;
    double max_total_lots = 2.0;
    double hedge_spacing = 50.0;
    double target_spread = 100.0;
    int ma_period = 500;
    double tp_spread = 50.0;
    double max_net_exposure = 0.5;
    double stop_out_level = 50.0;
};

struct DynamicHedgeResult {
    double final_equity = 0.0;
    double max_drawdown_pct = 0.0;
    double max_equity = 0.0;
    int total_trades = 0;
    int long_trades = 0;
    int short_trades = 0;
    int pairs_closed = 0;
    double max_net_exposure = 0.0;
    bool margin_call_occurred = false;
};

class DynamicHedgeStrategy {
private:
    DynamicHedgeConfig config_;
    std::deque<double> price_history_;
    double moving_average_ = 0.0;
    double last_long_price_ = 0.0;
    double last_short_price_ = 0.0;
    DynamicHedgeResult result_;

public:
    DynamicHedgeStrategy() = default;
    explicit DynamicHedgeStrategy(const DynamicHedgeConfig& cfg) : config_(cfg) {}
    void configure(const DynamicHedgeConfig& cfg) { config_ = cfg; }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        double mid = (tick.bid + tick.ask) / 2.0;
        price_history_.push_back(mid);
        if (price_history_.size() > static_cast<size_t>(config_.ma_period))
            price_history_.pop_front();
        if (price_history_.size() >= 10) {
            double sum = 0;
            for (double p : price_history_) sum += p;
            moving_average_ = sum / price_history_.size();
        } else {
            moving_average_ = mid;
        }

        if (check_margin_stop_out(tick, engine)) return;
        check_pair_profit(tick, engine);
        check_new_hedges(tick, engine);
        track_exposure(engine);

        double equity = engine.GetEquity();
        if (equity > result_.max_equity) result_.max_equity = equity;
    }

    DynamicHedgeResult get_result(const TickBasedEngine& engine) {
        result_.final_equity = engine.GetEquity();
        return result_;
    }

private:
    bool check_margin_stop_out(const Tick& tick, TickBasedEngine& engine) {
        if (engine.GetOpenPositions().empty()) return false;
        double margin_level = engine.GetMarginLevel();
        if (margin_level > 0 && margin_level < config_.stop_out_level) {
            result_.margin_call_occurred = true;
            auto positions = engine.GetOpenPositions();
            for (auto* trade : positions) engine.ClosePosition(trade, "STRATEGY_STOP_OUT");
            return true;
        }
        return false;
    }

    void check_pair_profit(const Tick& tick, TickBasedEngine& engine) {
        const auto& positions = engine.GetOpenPositions();
        for (size_t i = 0; i < positions.size(); i++) {
            if (!positions[i]->IsBuy()) continue;
            const auto* long_pos = positions[i];
            Trade* best_short = nullptr;
            double best_spread = 0;
            for (size_t j = 0; j < positions.size(); j++) {
                if (positions[j]->IsBuy()) continue;
                double spread = positions[j]->entry_price - long_pos->entry_price;
                if (spread > best_spread) { best_spread = spread; best_short = positions[j]; }
            }
            if (best_short && best_spread >= config_.tp_spread) {
                double cs = engine.GetConfig().contract_size;
                double long_pnl = (tick.bid - long_pos->entry_price) * long_pos->lot_size * cs;
                double short_pnl = (best_short->entry_price - tick.ask) * best_short->lot_size * cs;
                if (long_pnl + short_pnl > 0) {
                    Trade* long_trade = positions[i];
                    engine.ClosePosition(best_short, "PAIR_TP");
                    engine.ClosePosition(long_trade, "PAIR_TP");
                    result_.pairs_closed++;
                    return;
                }
            }
        }
    }

    void check_new_hedges(const Tick& tick, TickBasedEngine& engine) {
        double total_lots = engine.GetBuyVolume() + engine.GetSellVolume();
        if (total_lots >= config_.max_total_lots) return;
        double bias = 0.5;
        if (moving_average_ > 0) {
            double dev = (tick.ask - moving_average_) / moving_average_ * 100.0;
            bias = std::max(0.2, std::min(0.8, 0.5 - (dev / 20.0)));
        }
        bool need_long = (last_long_price_ == 0) || (tick.ask <= last_long_price_ - config_.hedge_spacing);
        bool need_short = (last_short_price_ == 0) || (tick.bid >= last_short_price_ + config_.hedge_spacing);
        if (need_long && need_short) {
            if (bias >= 0.5) {
                if (can_open(tick, engine)) open_long(tick, engine);
                if (can_open(tick, engine)) open_short(tick, engine);
            } else {
                if (can_open(tick, engine)) open_short(tick, engine);
                if (can_open(tick, engine)) open_long(tick, engine);
            }
        } else if (need_long) {
            if (can_open(tick, engine)) open_long(tick, engine);
        } else if (need_short) {
            if (can_open(tick, engine)) open_short(tick, engine);
        }
    }

    bool can_open(const Tick& tick, TickBasedEngine& engine) {
        return engine.CalculateMarginRequired(config_.base_lot_size, tick.ask) < engine.GetFreeMargin() * 0.8;
    }

    void track_exposure(TickBasedEngine& engine) {
        double total = engine.GetBuyVolume() + engine.GetSellVolume();
        if (total > 0) {
            double exposure = std::abs(engine.GetBuyVolume() - engine.GetSellVolume()) / total;
            if (exposure > result_.max_net_exposure) result_.max_net_exposure = exposure;
        }
    }

    void open_long(const Tick& tick, TickBasedEngine& engine) {
        engine.OpenMarketOrder("BUY", config_.base_lot_size);
        last_long_price_ = tick.ask;
        result_.total_trades++; result_.long_trades++;
    }

    void open_short(const Tick& tick, TickBasedEngine& engine) {
        engine.OpenMarketOrder("SELL", config_.base_lot_size);
        last_short_price_ = tick.bid;
        result_.total_trades++; result_.short_trades++;
    }
};

} // namespace backtest
