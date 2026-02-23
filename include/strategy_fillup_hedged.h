#pragma once
/**
 * Hedged Fill-Up Strategy (Anti-Fragile)
 *
 * Solution - Add SELL hedges to Fill-Up grid:
 * - Open SELL positions when price drops below trigger level
 * - SELLs have TP targets (profit from downward oscillations)
 * - Hedge reduces net exposure, allowing more aggressive trading
 *
 * Uses TickBasedEngine for all position/margin/balance management.
 * Engine handles swaps automatically - no manual swap logic needed.
 */

#include "tick_based_engine.h"
#include <deque>
#include <cmath>
#include <algorithm>

namespace backtest {

struct HedgedFillUpConfig {
    double survive_down_pct = 2.5;
    double spacing = 5.0;
    double size_multiplier = 1.0;
    int max_positions = 50;

    bool enable_hedging = true;
    double hedge_trigger_pct = 1.0;
    double hedge_ratio = 0.5;
    double hedge_spacing = 5.0;

    bool use_take_profit = true;
    double tp_buffer = 0.0;

    bool enable_dd_protection = false;
    double close_all_dd_pct = 70.0;

    bool enable_velocity_filter = false;
    double crash_velocity_pct = -1.0;
    int velocity_window = 500;

    double max_net_exposure_lots = 5.0;
    double stop_out_level = 50.0;
};

struct HedgedFillUpResult {
    double final_equity = 0.0;
    double max_drawdown_pct = 0.0;
    double max_equity = 0.0;
    int long_entries = 0;
    int short_entries = 0;
    int long_tp_hits = 0;
    int short_tp_hits = 0;
    int hedge_activations = 0;
    int dd_protection_triggers = 0;
    int velocity_pauses = 0;
    double max_long_lots = 0.0;
    double max_short_lots = 0.0;
    double max_net_exposure = 0.0;
    bool margin_call_occurred = false;
    int margin_warnings = 0;
};

class HedgedFillUpStrategy {
private:
    HedgedFillUpConfig config_;
    bool hedge_active_ = false;
    double hedge_trigger_price_ = 0.0;
    std::deque<double> price_history_;
    bool velocity_paused_ = false;
    double peak_equity_ = 0.0;
    HedgedFillUpResult result_;

public:
    HedgedFillUpStrategy() = default;
    explicit HedgedFillUpStrategy(const HedgedFillUpConfig& cfg) : config_(cfg) {}
    void configure(const HedgedFillUpConfig& cfg) { config_ = cfg; }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        if (check_margin_stop_out(tick, engine)) return;
        update_hedge_state(tick, engine);

        double equity = engine.GetEquity();
        double dd_pct = 0;
        if (peak_equity_ > 0) dd_pct = 100.0 * (peak_equity_ - equity) / peak_equity_;

        if (config_.enable_dd_protection && !engine.GetOpenPositions().empty() &&
            dd_pct > config_.close_all_dd_pct) {
            auto positions = engine.GetOpenPositions();
            for (auto* trade : positions) engine.ClosePosition(trade, "DD_PROTECTION");
            result_.dd_protection_triggers++;
            peak_equity_ = engine.GetBalance();
            return;
        }

        update_velocity_filter(tick);

        if (!velocity_paused_) {
            check_new_long_entries(tick, engine);
            if (config_.enable_hedging && hedge_active_)
                check_new_short_entries(tick, engine);
        }

        if (equity > result_.max_equity) result_.max_equity = equity;
        if (equity > peak_equity_) peak_equity_ = equity;
        if (dd_pct > result_.max_drawdown_pct) result_.max_drawdown_pct = dd_pct;

        double buy_lots = engine.GetBuyVolume();
        double sell_lots = engine.GetSellVolume();
        if (buy_lots > result_.max_long_lots) result_.max_long_lots = buy_lots;
        if (sell_lots > result_.max_short_lots) result_.max_short_lots = sell_lots;
        double net = std::abs(buy_lots - sell_lots);
        if (net > result_.max_net_exposure) result_.max_net_exposure = net;
    }

    HedgedFillUpResult get_result(const TickBasedEngine& engine) {
        result_.final_equity = engine.GetEquity();
        return result_;
    }

private:
    bool check_margin_stop_out(const Tick& tick, TickBasedEngine& engine) {
        if (engine.GetOpenPositions().empty()) return false;
        double margin_level = engine.GetMarginLevel();
        if (margin_level > 0 && margin_level < 100.0) result_.margin_warnings++;
        if (margin_level > 0 && margin_level < config_.stop_out_level) {
            result_.margin_call_occurred = true;
            auto positions = engine.GetOpenPositions();
            for (auto* trade : positions) engine.ClosePosition(trade, "STRATEGY_STOP_OUT");
            return true;
        }
        return false;
    }

    void update_hedge_state(const Tick& tick, TickBasedEngine& engine) {
        if (!config_.enable_hedging) return;
        double highest_buy = engine.GetHighestBuyEntry();
        if (engine.GetBuyPositionCount() > 0 && highest_buy > -1e307) {
            hedge_trigger_price_ = highest_buy * (100.0 - config_.hedge_trigger_pct) / 100.0;
            if (!hedge_active_ && tick.ask < hedge_trigger_price_) {
                hedge_active_ = true;
                result_.hedge_activations++;
            }
            if (hedge_active_ && tick.ask >= highest_buy) {
                hedge_active_ = false;
                auto positions = engine.GetOpenPositions();
                for (auto* trade : positions) {
                    if (trade->IsSell()) engine.ClosePosition(trade, "HEDGE_EXIT");
                }
            }
        }
    }

    void update_velocity_filter(const Tick& tick) {
        if (!config_.enable_velocity_filter) return;
        price_history_.push_back(tick.ask);
        while (price_history_.size() > static_cast<size_t>(config_.velocity_window))
            price_history_.pop_front();
        if (price_history_.size() >= static_cast<size_t>(config_.velocity_window)) {
            double old_price = price_history_.front();
            double velocity_pct = (tick.ask - old_price) / old_price * 100.0;
            if (velocity_pct < config_.crash_velocity_pct) {
                if (!velocity_paused_) { velocity_paused_ = true; result_.velocity_pauses++; }
            } else { velocity_paused_ = false; }
        }
    }

    void check_new_long_entries(const Tick& tick, TickBasedEngine& engine) {
        if (engine.GetOpenPositions().size() >= static_cast<size_t>(config_.max_positions)) return;
        double net_after = engine.GetBuyVolume() - engine.GetSellVolume() + engine.GetConfig().volume_min;
        if (std::abs(net_after) > config_.max_net_exposure_lots) return;

        double lot_size = calculate_long_size(tick, engine);
        if (lot_size < engine.GetConfig().volume_min) return;

        bool should_enter = false;
        size_t long_count = engine.GetBuyPositionCount();
        if (long_count == 0) {
            should_enter = true;
        } else {
            double lowest_buy = engine.GetLowestBuyEntry();
            double highest_buy = engine.GetHighestBuyEntry();
            if (lowest_buy < 1e307 && tick.ask <= lowest_buy - config_.spacing) should_enter = true;
            else if (highest_buy > -1e307 && tick.ask >= highest_buy + config_.spacing) should_enter = true;
        }

        if (should_enter) {
            double margin_needed = engine.CalculateMarginRequired(lot_size, tick.ask);
            if (margin_needed < engine.GetFreeMargin() * 0.7) {
                double tp = 0;
                if (config_.use_take_profit) {
                    double spread = tick.ask - tick.bid;
                    tp = tick.ask + config_.spacing + spread + config_.tp_buffer;
                }
                engine.OpenMarketOrder("BUY", lot_size, 0.0, tp);
                result_.long_entries++;
            }
        }
    }

    void check_new_short_entries(const Tick& tick, TickBasedEngine& engine) {
        if (engine.GetOpenPositions().size() >= static_cast<size_t>(config_.max_positions)) return;
        double net_after = engine.GetBuyVolume() - engine.GetSellVolume() - engine.GetConfig().volume_min;
        if (net_after < -config_.max_net_exposure_lots) return;

        double lot_size = calculate_short_size(tick, engine);
        if (lot_size < engine.GetConfig().volume_min) return;

        bool should_enter = false;
        size_t short_count = engine.GetSellPositionCount();
        if (short_count == 0 && hedge_active_) {
            should_enter = true;
        } else if (short_count > 0) {
            double highest_sell = engine.GetHighestSellEntry();
            double lowest_sell = engine.GetLowestSellEntry();
            if (highest_sell > -1e307 && tick.bid >= highest_sell + config_.hedge_spacing) should_enter = true;
            else if (lowest_sell < 1e307 && tick.bid <= lowest_sell - config_.hedge_spacing) should_enter = true;
        }

        if (should_enter) {
            double margin_needed = engine.CalculateMarginRequired(lot_size, tick.bid);
            if (margin_needed < engine.GetFreeMargin() * 0.7) {
                double tp = 0;
                if (config_.use_take_profit) {
                    double spread = tick.ask - tick.bid;
                    tp = tick.bid - config_.hedge_spacing - spread - config_.tp_buffer;
                }
                engine.OpenMarketOrder("SELL", lot_size, 0.0, tp);
                result_.short_entries++;
            }
        }
    }

    double calculate_long_size(const Tick& tick, TickBasedEngine& engine) {
        const auto& cfg = engine.GetConfig();
        double equity = engine.GetEquity();
        double used_margin = engine.GetUsedMargin();
        double volume_open = engine.GetBuyVolume();

        double reference_price = (engine.GetBuyPositionCount() > 0 && engine.GetHighestBuyEntry() > -1e307)
                                 ? engine.GetHighestBuyEntry() : tick.ask;
        double end_price = reference_price * ((100.0 - config_.survive_down_pct) / 100.0);
        double distance = tick.ask - end_price;
        if (distance <= 0) return 0;

        double num_trades = std::floor(distance / config_.spacing);
        if (num_trades < 1) num_trades = 1;

        double equity_at_target = equity - volume_open * distance * cfg.contract_size;
        const double margin_stop_out = 20.0;
        double target_margin_level = (used_margin > 0) ? (equity_at_target / used_margin * 100.0) : 10000.0;
        if (target_margin_level <= margin_stop_out) return 0;

        double grid_loss_factor = config_.spacing * cfg.contract_size * (num_trades * (num_trades + 1) / 2);
        double available = equity_at_target - (used_margin * margin_stop_out / 100.0);
        if (available <= 0) return 0;

        double lot_size = available / grid_loss_factor;
        lot_size = lot_size * config_.size_multiplier;
        return engine.NormalizeLots(lot_size);
    }

    double calculate_short_size(const Tick& tick, TickBasedEngine& engine) {
        double buy_volume = engine.GetBuyVolume();
        double sell_volume = engine.GetSellVolume();
        if (config_.hedge_ratio < 0.01 || buy_volume < engine.GetConfig().volume_min) return 0;
        double target_short_lots = std::min(buy_volume * config_.hedge_ratio, buy_volume * 0.5);
        double needed = target_short_lots - sell_volume;
        if (needed < engine.GetConfig().volume_min) return 0;
        return engine.NormalizeLots(std::min(needed, engine.GetConfig().volume_max));
    }
};

} // namespace backtest
