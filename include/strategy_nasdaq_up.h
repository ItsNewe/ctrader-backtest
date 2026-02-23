/**
 * @file strategy_nasdaq_up.h
 * @brief Upward-biased momentum strategy with margin-based position sizing.
 *
 * This strategy is designed for instruments with inherent upward bias (e.g., NASDAQ, S&P500).
 * It opens positions on new highs and sizes them based on "room to fall" before stop-out.
 *
 * Key Concepts:
 * - **Room**: Price distance the market can drop before hitting stop-out margin level
 * - **Power function**: Adjusts room based on accumulated gains: room = base * (gain)^power
 * - **Portfolio stop**: Uses margin level (default 74%) as the exit trigger for all positions
 *
 * @section nup_modes Power Sign Effects
 * | Power | Room Behavior | Position Sizing | Risk Profile |
 * |-------|---------------|-----------------|--------------|
 * | > 0   | Room GROWS    | Smaller at highs| Conservative |
 * | = 0   | Room FIXED    | Constant sizing | Neutral      |
 * | < 0   | Room SHRINKS  | Larger at highs | Aggressive   |
 *
 * @section nup_usage Usage Example
 * @code
 * NasdaqUp::Config config = NasdaqUp::Config::Baseline();
 * config.multiplier = 0.5;
 * config.power = -0.3;
 * NasdaqUp strategy(config);
 *
 * engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
 *     strategy.OnTick(tick, eng);
 * });
 * @endcode
 *
 * @see fill_up_oscillation.h for oscillating market strategies
 */

#ifndef STRATEGY_NASDAQ_UP_H
#define STRATEGY_NASDAQ_UP_H

#include "tick_based_engine.h"
#include <vector>
#include <cmath>
#include <cfloat>
#include <algorithm>

namespace backtest {

/**
 * @brief Momentum strategy for upward-biased instruments.
 *
 * Opens BUY positions on new highs, sizes based on margin survival,
 * exits all positions when margin level drops below threshold.
 */
class NasdaqUp {
public:
    /**
     * Configuration for NasdaqUp strategy.
     * All parameters can be swept for optimization.
     */
    struct Config {
        // === Core Parameters (for experimentation) ===
        double multiplier = 0.1;        ///< Initial room as % of price (0.1 = 0.1%)
        double power = 0.1;             ///< Power for room adjustment (can be negative)
        double stop_out_margin = 74.0;  ///< Margin level % to close all positions

        // === Safety Settings ===
        double min_room_clamp = 0.01;   ///< Minimum room to prevent division issues
        double max_room_clamp = 1e9;    ///< Maximum room to prevent extreme sizing
        bool verbose = false;           ///< Print debug info

        // === Presets ===

        /**
         * Original MQL5 code values
         */
        static Config Baseline() {
            Config c;
            c.multiplier = 0.1;
            c.power = 0.1;
            c.stop_out_margin = 74.0;
            return c;
        }

        /**
         * Aggressive: small room, negative power (larger positions at highs)
         */
        static Config Aggressive() {
            Config c;
            c.multiplier = 0.05;
            c.power = -0.5;
            c.stop_out_margin = 50.0;
            return c;
        }

        /**
         * Conservative: large room, positive power (smaller positions at highs)
         */
        static Config Conservative() {
            Config c;
            c.multiplier = 1.0;
            c.power = 0.5;
            c.stop_out_margin = 100.0;
            return c;
        }

    };

    /**
     * Statistics for analysis and debugging
     */
    struct Stats {
        int total_entries = 0;          ///< Total positions opened
        int stop_outs = 0;              ///< Number of stop-out events
        int cycles = 0;                 ///< Number of start->stop-out cycles
        double peak_volume = 0.0;       ///< Maximum total volume reached
        double max_room_seen = 0.0;     ///< Maximum room value observed
        double min_room_seen = DBL_MAX; ///< Minimum room value observed
        double max_equity = 0.0;        ///< Peak equity
        double final_room = 0.0;        ///< Room at last tick
        double avg_entry_price = 0.0;   ///< Volume-weighted average entry
    };

    /**
     * Config-based constructor
     */
    explicit NasdaqUp(const Config& config)
        : config_(config)
        , starting_x_(0.0)
        , room_(0.0)
        , local_starting_room_(0.0)
        , current_ask_(0.0)
        , current_bid_(0.0)
        , current_spread_(0.0)
    {
    }

    /**
     * Main tick handler - call this from engine.Run()
     */
    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_spread_ = tick.spread();

        double balance = engine.GetBalance();
        double equity = engine.GetEquity();
        double margin_level = engine.GetMarginLevel();
        double buy_volume = engine.GetBuyVolume();

        // Track peak equity
        if (equity > stats_.max_equity) {
            stats_.max_equity = equity;
        }

        // === CLOSE CONDITION: Margin level below threshold ===
        if (margin_level < config_.stop_out_margin && margin_level > 0 && engine.GetBuyPositionCount() > 0) {
            CloseAllPositions(engine, "MARGIN_STOP");
            stats_.stop_outs++;
            stats_.cycles++;

            // Reset state for next cycle
            starting_x_ = 0.0;
            room_ = 0.0;
            return;
        }

        // === OPEN CONDITIONS ===

        // First trade (no positions)
        if (buy_volume == 0.0) {
            OpenFirstTrade(balance, engine);
        }
        // Subsequent trades (price made new high)
        else if (current_ask_ > engine.GetHighestBuyEntry()) {
            OpenSubsequentTrade(equity, engine);
        }

        // Update stats
        stats_.final_room = room_;
        if (buy_volume > stats_.peak_volume) {
            stats_.peak_volume = buy_volume;
        }
    }

    // === Getters ===
    const Stats& GetStats() const { return stats_; }
    const Config& GetConfig() const { return config_; }
    double GetCurrentRoom() const { return room_; }

private:
    Config config_;
    Stats stats_;

    // State variables (matching MQL5)
    double starting_x_;
    double room_;
    double local_starting_room_;

    // Current tick data
    double current_ask_;
    double current_bid_;
    double current_spread_;

    /**
     * Open the first trade of a cycle
     */
    void OpenFirstTrade(double balance, TickBasedEngine& engine) {
        const auto& cfg = engine.GetConfig();

        // Calculate initial room: room = price * multiplier / 100
        local_starting_room_ = current_ask_ * config_.multiplier / 100.0;

        // Clamp room to prevent extreme values
        local_starting_room_ = std::max(config_.min_room_clamp,
                                        std::min(config_.max_room_clamp, local_starting_room_));

        // Calculate spread+commission cost
        double spread_cost = current_spread_ * cfg.contract_size;
        double commission_cost = cfg.commission_per_lot * cfg.contract_size;
        double total_cost = spread_cost + commission_cost;

        // Lot size formula from MQL5:
        // temp = (100 * balance * leverage) /
        //        (100 * room * leverage + 100 * spread_cost * leverage + stopout% * price)
        // temp = temp / contract_size
        double numerator = 100.0 * balance * cfg.leverage;
        double denominator = 100.0 * local_starting_room_ * cfg.leverage
                           + 100.0 * total_cost * cfg.leverage
                           + config_.stop_out_margin * current_ask_;

        if (denominator <= 0) {
            if (config_.verbose) {
                std::cout << "[NasdaqUp] First trade: denominator <= 0, skipping" << std::endl;
            }
            return;
        }

        double lot_size = numerator / denominator / cfg.contract_size;

        // Also check free margin constraint
        double margin_per_min_lot = engine.CalculateMarginRequired(cfg.volume_min, current_ask_);
        double free_margin = engine.GetFreeMargin();
        if (margin_per_min_lot > 0) {
            double lot_size_by_margin = (free_margin / margin_per_min_lot) * cfg.volume_min;
            lot_size = std::min(lot_size, lot_size_by_margin);
        }

        if (lot_size >= cfg.volume_min) {
            lot_size = engine.NormalizeLots(lot_size);

            Trade* trade = engine.OpenMarketOrder(TradeDirection::BUY, lot_size, 0.0, 0.0);
            if (trade) {
                starting_x_ = current_ask_;
                room_ = local_starting_room_;
                stats_.total_entries++;

                if (config_.verbose) {
                    std::cout << "[NasdaqUp] First trade: lots=" << lot_size
                              << ", price=" << current_ask_
                              << ", room=" << room_ << std::endl;
                }
            }
        }
    }

    /**
     * Open subsequent trade when price makes new high
     */
    void OpenSubsequentTrade(double equity, TickBasedEngine& engine) {
        const auto& cfg = engine.GetConfig();
        double used_margin = engine.GetUsedMargin();
        double buy_volume = engine.GetBuyVolume();

        // Calculate new room using power function
        double price_gain = current_ask_ - starting_x_;

        // Handle edge case: price_gain very small or zero
        if (price_gain < 1e-10) {
            // Use local_starting_room unchanged
            room_ = local_starting_room_;
        } else {
            // room = base_room * (price_gain)^power
            // Note: For negative power, this shrinks room as gain increases
            // For positive power, this grows room as gain increases
            double room_temp = local_starting_room_ * std::pow(price_gain, config_.power);

            // Clamp to prevent extreme values
            room_temp = std::max(config_.min_room_clamp,
                                std::min(config_.max_room_clamp, room_temp));
            room_ = room_temp;
        }

        // Track room statistics
        stats_.max_room_seen = std::max(stats_.max_room_seen, room_);
        stats_.min_room_seen = std::min(stats_.min_room_seen, room_);

        // Calculate spread+commission cost
        double spread_cost = current_spread_ * cfg.contract_size;
        double commission_cost = cfg.commission_per_lot * cfg.contract_size;
        double total_cost = spread_cost + commission_cost;

        // Lot size formula from MQL5 for subsequent trades:
        // temp = (100 * equity * leverage - leverage * stopout% * used_margin
        //         - 100 * room * leverage * volume_of_open_trades) /
        //        (100 * room * leverage + 100 * spread_cost * leverage + stopout% * price)
        // temp = temp / contract_size
        double numerator = 100.0 * equity * cfg.leverage
                         - cfg.leverage * config_.stop_out_margin * used_margin
                         - 100.0 * room_ * cfg.leverage * buy_volume;
        double denominator = 100.0 * room_ * cfg.leverage
                           + 100.0 * total_cost * cfg.leverage
                           + config_.stop_out_margin * current_ask_;

        if (denominator <= 0 || numerator <= 0) {
            return;  // Can't add more
        }

        double lot_size = numerator / denominator / cfg.contract_size;

        // Check free margin constraint
        double free_margin = engine.GetFreeMargin();
        double margin_per_min_lot = engine.CalculateMarginRequired(cfg.volume_min, current_ask_);
        if (margin_per_min_lot > 0) {
            double lot_size_by_margin = (free_margin / margin_per_min_lot) * cfg.volume_min;
            lot_size = std::min(lot_size, lot_size_by_margin);
        }

        if (lot_size >= cfg.volume_min) {
            lot_size = engine.NormalizeLots(lot_size);

            Trade* trade = engine.OpenMarketOrder(TradeDirection::BUY, lot_size, 0.0, 0.0);
            if (trade) {
                stats_.total_entries++;

                if (config_.verbose) {
                    std::cout << "[NasdaqUp] Added: lots=" << lot_size
                              << ", price=" << current_ask_
                              << ", room=" << room_
                              << ", total_vol=" << engine.GetBuyVolume() << std::endl;
                }
            }
        }
    }

    /**
     * Close all open positions
     */
    void CloseAllPositions(TickBasedEngine& engine, const std::string& reason) {
        const auto& positions = engine.GetOpenPositions();

        // Collect positions to close (iterate backwards to avoid invalidation)
        std::vector<Trade*> to_close;
        for (Trade* trade : positions) {
            if (trade->IsBuy()) {
                to_close.push_back(trade);
            }
        }

        for (Trade* trade : to_close) {
            engine.ClosePosition(trade, reason);
        }

        if (config_.verbose) {
            std::cout << "[NasdaqUp] Closed " << to_close.size()
                      << " positions, reason=" << reason << std::endl;
        }
    }

};

} // namespace backtest

#endif // STRATEGY_NASDAQ_UP_H
