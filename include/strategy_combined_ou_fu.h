/**
 * @file strategy_combined_ou_fu.h
 * @brief Combined OU+FU strategy: Open Upwards (grid down + continuous up) + Fill Up.
 *
 * Ported from Combined_Strategies_Optimized_Fixed.mq5.
 * Runs three sub-strategies simultaneously on a single account:
 *
 * 1. **OU Down** — Grid buying on downward moves. Opens positions as price drops,
 *    with sizing that accounts for survive percentage. Closes on direction reversal.
 *    Identified by: comment contains ";"
 *
 * 2. **OU Up** — Continuous buying with margin-aware sizing. Opens one trade per tick
 *    as long as margin allows. Closes smallest profitable when max trades exceeded.
 *    Identified by: empty comment, TP=0
 *
 * 3. **FU (Fill Up)** — Grid trading with take-profit. Opens at grid spacing intervals,
 *    positions close individually via TP. Similar to FillUpOscillation but simpler.
 *    Identified by: TP > 0
 *
 * @see FillUpOscillation, StrategyCombinedJu
 */

#ifndef STRATEGY_COMBINED_OU_FU_H
#define STRATEGY_COMBINED_OU_FU_H

#include "tick_based_engine.h"
#include <vector>
#include <unordered_set>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>

namespace backtest {

class StrategyCombinedOUFU {
public:
    // Position identification tags (stored in Trade comment field isn't available,
    // so we track via internal bookkeeping with trade IDs)
    enum SubStrategy {
        SUB_NONE = 0,
        SUB_FU,       // Fill Up (has TP > 0)
        SUB_OU_DOWN,  // OU grid down
        SUB_OU_UP     // OU continuous up
    };

    struct Config {
        // Shared settings
        double base_survive = 5.0;          // Base survive percentage
        int max_number_of_trades = 200;     // Max OU_UP positions before closing smallest

        // Survive multipliers per sub-strategy
        double mult_ou_down = 1.0;          // Multiplier for OU Down grid
        double mult_ou_up = 2.0;            // Multiplier for OU Up continuous
        double mult_fu = 0.5;              // Multiplier for Fill Up

        // OU settings
        int ou_sizing = 0;                  // 0=constant, 1=incremental
        int ou_closing_mode = 0;            // 0=close profitable on reversal, 1=close all if none unprofitable

        // FU settings
        double fu_size = 1.0;              // FU size multiplier (unused in lot calc, kept for compat)
        double fu_spacing = 1.0;           // FU grid spacing in price units

        // Engine-level (set from CLI config)
        double contract_size = 100.0;
        double leverage = 500.0;
        double min_volume = 0.01;
        double max_volume = 10.0;

        // Computed survive percentages (set in constructor)
        double survive_ou_down() const { return base_survive * mult_ou_down; }
        double survive_ou_up() const { return base_survive * mult_ou_up; }
        double survive_fu() const { return base_survive * mult_fu; }
    };

    struct Stats {
        // OU Down stats
        int ou_down_entries = 0;
        int ou_down_closures = 0;

        // OU Up stats
        int ou_up_entries = 0;
        int ou_up_max_trade_closures = 0;   // Closed due to max_number_of_trades

        // FU stats
        int fu_entries = 0;
        int fu_max_positions = 0;

        // Overall
        int direction_changes = 0;
    };

    explicit StrategyCombinedOUFU(const Config& config)
        : config_(config),
          // Market state
          ask_(0.0), bid_(0.0), spread_(0.0),
          equity_(0.0), balance_(0.0),
          // OU Down aggregates
          ou_value_of_buy_trades_(0.0),
          ou_lowest_buy_(DBL_MAX), ou_highest_buy_(DBL_MIN),
          ou_volume_of_open_trades_(0.0),
          ou_trade_size_buy_(0.0), ou_spacing_buy_(0.0),
          ou_count_buy_(0),
          ou_is_there_unprofitable_buy_(false),
          // OU Up aggregates
          ou_volume_of_open_trades_up_(0.0),
          ou_cost_of_open_trades_up_(0.0),
          ou_smallest_profitable_up_id_(-1),
          ou_smallest_profitable_up_vol_(DBL_MAX),
          ou_up_count_(0),
          // OU direction tracking
          ou_direction_(0), ou_direction_change_(0),
          ou_bid_at_turn_up_(DBL_MAX), ou_ask_at_turn_up_(DBL_MAX),
          ou_bid_at_turn_down_(DBL_MIN), ou_ask_at_turn_down_(DBL_MIN),
          ou_close_all_buy_flag_(false),
          ou_close_all_profitable_buy_flag_(false),
          // FU aggregates
          fu_lowest_buy_(DBL_MAX), fu_highest_buy_(DBL_MIN),
          fu_closest_above_(DBL_MAX), fu_closest_below_(DBL_MAX),
          fu_volume_open_(0.0), fu_pos_count_(0),
          fu_trade_size_buy_(0.0)
    {
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        // 1. Update market data
        ask_ = tick.ask;
        bid_ = tick.bid;
        spread_ = tick.spread();
        equity_ = engine.GetEquity();
        balance_ = engine.GetBalance();

        // 2. Scan all positions (single pass)
        ScanPositions(engine);

        // 3. OU Logic
        OU_DirectionCheck();
        OU_ProcessClosures(engine);

        if (config_.mult_ou_up > 0) {
            OU_OpenNewUpWUp(engine);
        }
        if (config_.mult_ou_down > 0) {
            OU_OpenNewUpWDown(engine);
        }

        // 4. FU Logic
        if (config_.mult_fu > 0) {
            FU_OnTickLogic(engine);
        }
    }

    const Stats& GetStats() const { return stats_; }
    const Config& GetConfig() const { return config_; }

private:
    Config config_;

    // Market state
    double ask_, bid_, spread_;
    double equity_, balance_;

    // ── OU Down Aggregates ──
    double ou_value_of_buy_trades_;       // Sum of unrealized P&L for OU Down
    double ou_lowest_buy_, ou_highest_buy_;
    double ou_volume_of_open_trades_;
    double ou_trade_size_buy_, ou_spacing_buy_;
    int ou_count_buy_;
    bool ou_is_there_unprofitable_buy_;

    // ── OU Up Aggregates ──
    double ou_volume_of_open_trades_up_;
    double ou_cost_of_open_trades_up_;    // sum(vol_i * open_price_i)
    int ou_smallest_profitable_up_id_;    // Trade ID of smallest profitable UP position
    double ou_smallest_profitable_up_vol_;
    int ou_up_count_;

    // ── OU Direction Tracking ──
    int ou_direction_;                    // -1=down, 0=unknown, 1=up
    int ou_direction_change_;
    double ou_bid_at_turn_up_, ou_ask_at_turn_up_;
    double ou_bid_at_turn_down_, ou_ask_at_turn_down_;
    bool ou_close_all_buy_flag_;
    bool ou_close_all_profitable_buy_flag_;

    // ── FU Aggregates ──
    double fu_lowest_buy_, fu_highest_buy_;
    double fu_closest_above_, fu_closest_below_;
    double fu_volume_open_;
    int fu_pos_count_;
    double fu_trade_size_buy_;

    // ── Position tracking ──
    // We identify positions by their properties:
    //   FU: has TP > 0
    //   OU_DOWN: no TP, opened during down grid (tracked by ID set)
    //   OU_UP: no TP, opened during up logic (tracked by ID set)
    // Using unordered_set for O(1) lookup/insert/erase
    std::unordered_set<int> ou_down_trade_ids_;
    std::unordered_set<int> ou_up_trade_ids_;

    Stats stats_;

    // ────────────────── Position Scanning ──────────────────

    void ScanPositions(TickBasedEngine& engine) {
        // Reset all aggregates
        ou_value_of_buy_trades_ = 0.0;
        ou_count_buy_ = 0;
        ou_volume_of_open_trades_ = 0.0;
        ou_lowest_buy_ = DBL_MAX;
        ou_highest_buy_ = DBL_MIN;
        ou_is_there_unprofitable_buy_ = false;
        if (config_.ou_sizing == 1) ou_trade_size_buy_ = 0.0;

        ou_volume_of_open_trades_up_ = 0.0;
        ou_cost_of_open_trades_up_ = 0.0;
        ou_smallest_profitable_up_id_ = -1;
        ou_smallest_profitable_up_vol_ = DBL_MAX;
        ou_up_count_ = 0;

        fu_lowest_buy_ = DBL_MAX;
        fu_highest_buy_ = DBL_MIN;
        fu_closest_above_ = DBL_MAX;
        fu_closest_below_ = DBL_MAX;
        fu_volume_open_ = 0.0;
        fu_pos_count_ = 0;

        const auto& positions = engine.GetOpenPositions();
        for (const Trade* trade : positions) {
            if (!trade->IsBuy()) continue;

            double open_price = trade->entry_price;
            double lots = trade->lot_size;
            double tp = trade->take_profit;
            int id = trade->id;

            // Classify by sub-strategy
            SubStrategy type = ClassifyTrade(id, tp);

            if (type == SUB_FU) {
                fu_pos_count_++;
                fu_volume_open_ += lots;
                fu_lowest_buy_ = std::min(fu_lowest_buy_, open_price);
                fu_highest_buy_ = std::max(fu_highest_buy_, open_price);

                // Distance tracking for gap-filling
                if (open_price >= ask_) {
                    fu_closest_above_ = std::min(fu_closest_above_, open_price - ask_);
                }
                if (open_price <= ask_) {
                    fu_closest_below_ = std::min(fu_closest_below_, ask_ - open_price);
                }
            } else if (type == SUB_OU_DOWN) {
                // Calculate unrealized P&L for this position
                double pnl = (bid_ - open_price) * lots * config_.contract_size;
                ou_value_of_buy_trades_ += pnl;
                ou_count_buy_++;
                ou_volume_of_open_trades_ += lots;
                ou_lowest_buy_ = std::min(ou_lowest_buy_, open_price);
                ou_highest_buy_ = std::max(ou_highest_buy_, open_price);
                if (pnl < 0) ou_is_there_unprofitable_buy_ = true;
                if (config_.ou_sizing == 1) {
                    ou_trade_size_buy_ = std::max(ou_trade_size_buy_, lots);
                }
            } else if (type == SUB_OU_UP) {
                ou_volume_of_open_trades_up_ += lots;
                ou_cost_of_open_trades_up_ += lots * open_price;
                ou_up_count_++;

                double pnl = (bid_ - open_price) * lots * config_.contract_size;
                if (pnl > 0 && lots < ou_smallest_profitable_up_vol_) {
                    ou_smallest_profitable_up_vol_ = lots;
                    ou_smallest_profitable_up_id_ = id;
                }
            }
        }

        // Post-scan adjustments
        if (config_.ou_sizing == 1) {
            ou_trade_size_buy_ += config_.min_volume;
        }

        // Track FU stats
        if (fu_pos_count_ > stats_.fu_max_positions) {
            stats_.fu_max_positions = fu_pos_count_;
        }

        // Prune stale trade IDs (from trades closed by TP/SL/engine)
        {
            std::unordered_set<int> live_ids;
            for (const Trade* t : positions) live_ids.insert(t->id);
            for (auto it = ou_down_trade_ids_.begin(); it != ou_down_trade_ids_.end(); ) {
                if (live_ids.count(*it) == 0) it = ou_down_trade_ids_.erase(it);
                else ++it;
            }
            for (auto it = ou_up_trade_ids_.begin(); it != ou_up_trade_ids_.end(); ) {
                if (live_ids.count(*it) == 0) it = ou_up_trade_ids_.erase(it);
                else ++it;
            }
        }

        // OU Up: close smallest profitable if over max trades
        if (ou_up_count_ >= config_.max_number_of_trades && ou_smallest_profitable_up_id_ >= 0) {
            // Find and close the trade
            const auto& pos = engine.GetOpenPositions();
            for (Trade* t : pos) {
                if (t->id == ou_smallest_profitable_up_id_) {
                    engine.ClosePosition(t, "OU_UP_MaxTrades");
                    stats_.ou_up_max_trade_closures++;
                    // Remove from tracking
                    ou_up_trade_ids_.erase(t->id);
                    break;
                }
            }
        }
    }

    SubStrategy ClassifyTrade(int id, double tp) const {
        // FU trades have TP > 0
        if (tp > 0.0001) return SUB_FU;

        // Check our tracking sets (O(1) lookup)
        if (ou_down_trade_ids_.count(id)) return SUB_OU_DOWN;
        if (ou_up_trade_ids_.count(id)) return SUB_OU_UP;

        // Unknown trade (shouldn't happen in normal operation)
        return SUB_NONE;
    }

    // ────────────────── OU Direction Logic ──────────────────

    void OU_DirectionCheck() {
        ou_direction_change_ = 0;

        // Track turn points
        ou_bid_at_turn_down_ = std::max(bid_, ou_bid_at_turn_down_);
        ou_bid_at_turn_up_ = std::min(bid_, ou_bid_at_turn_up_);
        ou_ask_at_turn_down_ = std::max(ask_, ou_ask_at_turn_down_);
        ou_ask_at_turn_up_ = std::min(ask_, ou_ask_at_turn_up_);

        // Check for downward turn
        if (ou_direction_ != -1 &&
            ou_ask_at_turn_down_ >= ask_ + spread_ &&
            ou_bid_at_turn_down_ >= bid_ + spread_) {
            ou_direction_ = -1;
            ou_direction_change_ = -1;
            stats_.direction_changes++;

            switch (config_.ou_closing_mode) {
                case 0:
                    if (ou_value_of_buy_trades_ > 0) {
                        ou_close_all_profitable_buy_flag_ = true;
                    }
                    break;
                case 1:
                    if (!ou_is_there_unprofitable_buy_) {
                        ou_close_all_buy_flag_ = true;
                    }
                    break;
            }

            // Clear turn marks
            ou_bid_at_turn_up_ = DBL_MAX;
            ou_ask_at_turn_up_ = DBL_MAX;
            ou_ask_at_turn_down_ = DBL_MIN;
            ou_bid_at_turn_down_ = DBL_MIN;
        }

        // Check for upward turn
        if (ou_direction_ != 1 &&
            ou_bid_at_turn_up_ <= bid_ - spread_ &&
            ou_ask_at_turn_up_ <= ask_ - spread_) {
            ou_direction_ = 1;
            ou_direction_change_ = 1;
            stats_.direction_changes++;

            // Clear turn marks
            ou_bid_at_turn_up_ = DBL_MAX;
            ou_ask_at_turn_up_ = DBL_MAX;
            ou_ask_at_turn_down_ = DBL_MIN;
            ou_bid_at_turn_down_ = DBL_MIN;
        }
    }

    // ────────────────── OU Closures ──────────────────

    void OU_ProcessClosures(TickBasedEngine& engine) {
        if (!ou_close_all_buy_flag_ && !ou_close_all_profitable_buy_flag_) return;

        // Close OU Down positions
        std::vector<Trade*> to_close;
        const auto& positions = engine.GetOpenPositions();

        for (Trade* trade : positions) {
            if (!trade->IsBuy()) continue;
            if (trade->take_profit > 0.0001) continue;  // Skip FU

            // Check if it's OU_DOWN
            if (std::find(ou_down_trade_ids_.begin(), ou_down_trade_ids_.end(), trade->id) == ou_down_trade_ids_.end()) {
                continue;  // Not OU_DOWN
            }

            double pnl = (bid_ - trade->entry_price) * trade->lot_size * config_.contract_size;
            bool close_it = false;

            if (ou_close_all_buy_flag_) {
                close_it = true;
            } else if (ou_close_all_profitable_buy_flag_ && pnl > 0) {
                close_it = true;
            }

            if (close_it) {
                to_close.push_back(trade);
            }
        }

        // Close collected positions
        for (auto it = to_close.rbegin(); it != to_close.rend(); ++it) {
            Trade* t = *it;
            ou_down_trade_ids_.erase(t->id);
            engine.ClosePosition(t, "OU_DOWN_Reversal");
            stats_.ou_down_closures++;
        }

        // Reset flags
        ou_close_all_buy_flag_ = false;
        ou_close_all_profitable_buy_flag_ = false;

        // Re-scan after closures for accurate state
        if (!to_close.empty()) {
            ScanPositions(engine);
        }
    }

    // ────────────────── OU Sizing (Down Grid) ──────────────────

    void OU_SizingBuy(TickBasedEngine& engine) {
        ou_trade_size_buy_ = 0;
        ou_spacing_buy_ = 0;

        double survive = config_.survive_ou_down();
        double end_price = ask_ * ((100.0 - survive) / 100.0);
        double distance = end_price - ask_;  // Negative (end < ask for long)
        double abs_distance = std::fabs(distance);

        double used_margin = engine.GetUsedMargin();
        double margin_so = 20.0;  // Stop-out level %

        // Equity at target considering OU Down positions only
        double equity_at_target = equity_ - ou_volume_of_open_trades_ * abs_distance * config_.contract_size;

        bool need_new_grid = (ou_count_buy_ == 0) ||
            (used_margin <= 0 || equity_at_target > margin_so / 100.0 * used_margin);

        if (!need_new_grid) return;

        double margin_level_local = (used_margin > 0)
            ? equity_at_target / used_margin * 100.0 : 99999.0;
        if (margin_level_local <= margin_so) return;

        double trade_size = config_.min_volume;
        double len = abs_distance;
        if (len <= 0) return;

        double C = config_.contract_size * trade_size;
        double loss_per_unit;
        if (config_.ou_sizing == 1) {
            loss_per_unit = C * len / 3.0;  // Incremental: triangular averaging
        } else {
            loss_per_unit = C * len / 2.0;  // Constant: uniform averaging
        }

        double d_equity = loss_per_unit + trade_size * spread_ * config_.contract_size;
        double local_used_margin = CalculateMarginAvg(trade_size, ask_, end_price);

        double number_of_trades;
        if (used_margin == 0) {
            number_of_trades = std::floor(equity_at_target /
                (margin_so / 100.0 * local_used_margin + d_equity));
        } else {
            number_of_trades = std::floor(
                (equity_at_target - margin_so / 100.0 * used_margin) /
                (margin_so / 100.0 * local_used_margin + d_equity));
        }

        number_of_trades = std::min(number_of_trades, (double)config_.max_number_of_trades);
        if (number_of_trades < 1) return;

        double proportion = (len != 0) ? number_of_trades / len : 0;

        switch (config_.ou_sizing) {
            case 0: {  // Constant sizing
                if (proportion >= 1) {
                    ou_spacing_buy_ = 1.0;
                    ou_trade_size_buy_ = std::floor(proportion) * config_.min_volume;
                } else {
                    ou_trade_size_buy_ = config_.min_volume;
                    if (number_of_trades > 0) {
                        ou_spacing_buy_ = std::round((len / number_of_trades) * 100.0) / 100.0;
                    } else {
                        ou_spacing_buy_ = len;
                    }
                }
                break;
            }
            case 1: {  // Incremental sizing
                double temp = (-1.0 + std::sqrt(1.0 + 8.0 * number_of_trades)) / 2.0;
                temp = std::floor(temp);
                if (temp > 1) {
                    ou_spacing_buy_ = len / (temp - 1.0);
                } else {
                    ou_spacing_buy_ = len;
                }
                ou_trade_size_buy_ = config_.min_volume;
                break;
            }
        }
    }

    // ────────────────── OU Down Entry ──────────────────

    void OU_OpenNewUpWDown(TickBasedEngine& engine) {
        if (ou_count_buy_ == 0) {
            OU_SizingBuy(engine);
        }

        if (ou_count_buy_ == 0) {
            // First entry
            double lots = std::min(ou_trade_size_buy_, config_.max_volume);
            if (lots >= config_.min_volume) {
                OU_SendDownOrder(lots, engine);
            }
        } else {
            if (ou_spacing_buy_ <= 0) return;

            double temp, temp1, temp2, temp3, temp4, temp5, temp6;
            switch (config_.ou_sizing) {
                case 0: {  // Constant
                    if (ou_lowest_buy_ > ask_) {
                        temp = (ou_highest_buy_ - ask_) / ou_spacing_buy_;
                        temp1 = std::floor(temp);
                        temp2 = temp - temp1;
                        temp3 = temp1;
                        double factor = (ou_trade_size_buy_ / config_.min_volume);
                        if (factor == 0) factor = 1;
                        temp4 = ou_spacing_buy_ / factor;
                        temp5 = std::floor(temp2 / temp4);
                        temp6 = temp3 * ou_trade_size_buy_ + temp5 * config_.min_volume - ou_volume_of_open_trades_;
                        if (temp6 > 0) {
                            OU_SendDownOrder(temp6, engine);
                        }
                    }
                    break;
                }
                case 1: {  // Incremental
                    if (ou_lowest_buy_ > ask_) {
                        temp = (ou_highest_buy_ - ask_) / ou_spacing_buy_;
                        temp1 = std::floor(temp);
                        temp2 = temp - temp1;
                        temp3 = 0;
                        if (temp >= 1) {
                            for (int i = 1; i <= (int)temp1 + 1; i++) temp3 += i;
                        }
                        temp4 = ou_spacing_buy_ / (temp1 + 1);
                        temp5 = std::floor(temp2 / temp4);
                        temp6 = (temp3 + temp5) * config_.min_volume - ou_volume_of_open_trades_;
                        if (temp6 > 0) {
                            OU_SendDownOrder(temp6, engine);
                        }
                    }
                    break;
                }
            }
        }
    }

    void OU_SendDownOrder(double lots, TickBasedEngine& engine) {
        if (lots < config_.min_volume) return;
        double final_lots = engine.NormalizeLots(std::min(lots, config_.max_volume));

        // OU Down: no TP, no SL
        Trade* trade = engine.OpenMarketOrder(TradeDirection::BUY, final_lots, 0.0, 0.0);
        if (trade) {
            ou_down_trade_ids_.insert(trade->id);
            stats_.ou_down_entries++;
        }
    }

    // ────────────────── OU Up Entry ──────────────────

    void OU_OpenNewUpWUp(TickBasedEngine& engine) {
        double survive = config_.survive_ou_up();
        double end_price = ask_ * ((100.0 - survive) / 100.0);
        double distance = ask_ - end_price;
        double abs_distance = std::fabs(distance);

        double used_margin = engine.GetUsedMargin();
        double margin_so = 20.0;

        // Equity after existing OU_UP positions drop to end_price
        double equity_after_existing = equity_ - ou_volume_of_open_trades_up_ * abs_distance * config_.contract_size;

        bool need_new_trade = (ou_volume_of_open_trades_up_ == 0) ||
            (used_margin <= 0 || equity_after_existing > margin_so / 100.0 * used_margin);

        if (!need_new_trade) return;

        // CFD_LEVERAGE margin calculation (our engine's default)
        double c_size = config_.contract_size;
        double num = (100.0 * equity_ * config_.leverage
                     - 100.0 * c_size * abs_distance * ou_volume_of_open_trades_up_ * config_.leverage
                     - config_.leverage * margin_so * used_margin);
        double den = (c_size * (100.0 * abs_distance * config_.leverage
                     + 100.0 * spread_ * config_.leverage
                     + ask_ * 1.0 * margin_so));  // margin_rate = 1.0

        double trade_size = 0;
        if (std::fabs(den) > 1e-10) {
            trade_size = num / den;
            trade_size = std::floor(trade_size / config_.min_volume) * config_.min_volume;
        }

        if (trade_size >= config_.min_volume) {
            double final_lots = engine.NormalizeLots(std::min(trade_size, config_.max_volume));
            // OU Up: no TP, no SL
            Trade* trade = engine.OpenMarketOrder(TradeDirection::BUY, final_lots, 0.0, 0.0);
            if (trade) {
                ou_up_trade_ids_.insert(trade->id);
                ou_cost_of_open_trades_up_ += final_lots * ask_;
                stats_.ou_up_entries++;
            }
        }
    }

    // ────────────────── FU Logic ──────────────────

    void FU_OnTickLogic(TickBasedEngine& engine) {
        double spacing = config_.fu_spacing;
        if (spacing <= 0) return;

        if (fu_pos_count_ == 0) {
            FU_CalcLotSize(engine);
            if (FU_OpenBuy(engine, spacing)) {
                fu_highest_buy_ = fu_lowest_buy_ = ask_;
            }
        } else if (fu_lowest_buy_ >= ask_ + spacing) {
            FU_CalcLotSize(engine);
            if (FU_OpenBuy(engine, spacing)) {
                fu_lowest_buy_ = ask_;
            }
        } else if (fu_highest_buy_ <= ask_ - spacing) {
            FU_CalcLotSize(engine);
            if (FU_OpenBuy(engine, spacing)) {
                fu_highest_buy_ = ask_;
            }
        } else if (fu_closest_above_ >= spacing && fu_closest_below_ >= spacing) {
            FU_CalcLotSize(engine);
            FU_OpenBuy(engine, spacing);
        }
    }

    bool FU_OpenBuy(TickBasedEngine& engine, double spacing) {
        if (fu_trade_size_buy_ < config_.min_volume) return false;

        double lots = engine.NormalizeLots(std::min(fu_trade_size_buy_, config_.max_volume));
        double tp = ask_ + spread_ + spacing;

        Trade* trade = engine.OpenMarketOrder(TradeDirection::BUY, lots, 0.0, tp);
        if (trade) {
            // FU trades are identified by their TP > 0, no need to track IDs
            stats_.fu_entries++;
            return true;
        }
        return false;
    }

    void FU_CalcLotSize(TickBasedEngine& engine) {
        fu_trade_size_buy_ = 0;
        double spacing = config_.fu_spacing;
        double survive = config_.survive_fu();

        double ref_price = (fu_pos_count_ == 0) ? ask_ : fu_highest_buy_;
        double end_price = ref_price * (100.0 - survive) / 100.0;
        double distance = ask_ - end_price;
        if (distance <= 0) return;

        double num_trades = std::floor(distance / spacing);
        if (num_trades < 1) num_trades = 1;

        double eq_at_target = equity_ - fu_volume_open_ * distance * config_.contract_size;
        double used_margin = engine.GetUsedMargin();
        double margin_so = 20.0;

        if (used_margin > 0 && (eq_at_target / used_margin * 100.0) <= margin_so) return;

        double unit = config_.min_volume;
        double d_equity = config_.contract_size * unit * spacing * (num_trades * (num_trades + 1) / 2.0);
        d_equity += num_trades * unit * spread_ * config_.contract_size;

        double unit_margin = CalculateMarginAvg(unit, ask_, end_price);
        unit_margin *= num_trades;

        double S = margin_so / 100.0;
        double denominator = d_equity + S * unit_margin;
        if (denominator <= 0) return;

        double m = (eq_at_target - S * used_margin) / denominator;
        m = std::max(1.0, m);
        double max_mult = config_.max_volume / config_.min_volume;
        if (m > max_mult) m = max_mult;

        fu_trade_size_buy_ = std::floor(m) * config_.min_volume;
    }

    // ────────────────── Margin Helpers ──────────────────

    double CalculateMarginAvg(double volume, double start_p, double end_p) const {
        // CFD_LEVERAGE mode (our engine's default):
        // Average margin between start and end price
        return ((volume * config_.contract_size * start_p) / config_.leverage
              + (volume * config_.contract_size * end_p) / config_.leverage) / 2.0;
    }
};

} // namespace backtest

#endif // STRATEGY_COMBINED_OU_FU_H
