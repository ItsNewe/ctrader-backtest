#ifndef FILL_UP_STRATEGY_H
#define FILL_UP_STRATEGY_H

#include "tick_based_engine.h"
#include <vector>
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace backtest {

/**
 * Fill-Up Grid Trading Strategy
 *
 * This strategy places BUY orders at regular spacing intervals,
 * with dynamic position sizing based on surviving a target drawdown percentage.
 *
 * Key Features:
 * - Grid trading: Opens positions at fixed spacing intervals
 * - Dynamic lot sizing: Calculates position size to survive drawdown
 * - Take profit at spacing + spread for each position
 * - Martingale-style recovery with position sizing
 */
class FillUpStrategy {
public:
    FillUpStrategy(double survive_pct, double size_multiplier, double spacing_dollars,
                   double min_volume, double max_volume, double contract_size,
                   double leverage, int symbol_digits = 2, double margin_rate = 1.0,
                   bool enable_dd_protection = false, double dd_threshold_pct = 50.0)
        : survive_pct_(survive_pct),
          size_multiplier_(size_multiplier),
          debug_counter_(0),
          spacing_(spacing_dollars),
          min_volume_(min_volume),
          max_volume_(max_volume),
          symbol_digits_(symbol_digits),
          lowest_buy_(DBL_MAX),
          highest_buy_(DBL_MIN),
          volume_of_open_trades_(0.0),
          trade_size_buy_(0.0),
          spacing_buy_(spacing_dollars),
          max_balance_(0.0),
          max_number_of_open_(0),
          max_used_funds_(0.0),
          max_trade_size_(0.0),
          enable_dd_protection_(enable_dd_protection),
          dd_threshold_pct_(dd_threshold_pct),
          peak_equity_(0.0),
          dd_triggers_(0)
    {
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_spread_ = tick.spread();

        // Update account state
        current_equity_ = engine.GetEquity();
        current_balance_ = engine.GetBalance();

        // Initialize peak equity on first tick
        if (peak_equity_ == 0.0) {
            peak_equity_ = current_equity_;
        }

        // DD Protection: check BEFORE updating peak (to trigger on threshold breach)
        if (enable_dd_protection_ && !engine.GetOpenPositions().empty() && peak_equity_ > 0) {
            double dd_pct = (peak_equity_ - current_equity_) / peak_equity_ * 100.0;
            if (dd_pct > dd_threshold_pct_) {
                // Close all positions
                std::vector<Trade*> positions_copy;
                for (Trade* t : engine.GetOpenPositions()) {
                    positions_copy.push_back(t);
                }
                for (Trade* t : positions_copy) {
                    engine.ClosePosition(t, "DD_PROTECTION");
                }
                dd_triggers_++;
                // Reset peak to current equity after closing
                peak_equity_ = engine.GetBalance();
                // Reset grid tracking
                lowest_buy_ = DBL_MAX;
                highest_buy_ = DBL_MIN;
                return;
            }
        }

        // Update peak equity AFTER DD check
        if (current_equity_ > peak_equity_) {
            peak_equity_ = current_equity_;
        }

        // Update statistics
        max_balance_ = std::max(max_balance_, current_balance_);
        max_number_of_open_ = std::max(max_number_of_open_, (int)engine.GetOpenPositions().size());

        // Iterate through positions
        Iterate(engine);

        // Check for new trade opportunities
        OpenNew(engine);
    }

    // Get strategy statistics
    double GetMaxBalance() const { return max_balance_; }
    int GetMaxNumberOfOpen() const { return max_number_of_open_; }
    double GetMaxUsedFunds() const { return max_used_funds_; }
    double GetMaxTradeSize() const { return max_trade_size_; }
    int GetDDTriggers() const { return dd_triggers_; }
    double GetPeakEquity() const { return peak_equity_; }

private:
    // Strategy parameters
    double survive_pct_;           // Drawdown survival percentage
    double size_multiplier_;       // Lot size multiplier
    double spacing_;               // Spacing between trades (in currency units)
    double min_volume_;            // Minimum lot size
    double max_volume_;            // Maximum lot size
    // Note: contract_size_/leverage_/margin_rate_ removed — now read from engine.GetConfig()
    int symbol_digits_;            // Price decimal places

    // Position tracking
    double lowest_buy_;
    double highest_buy_;
    double closest_above_;
    double closest_below_;
    double volume_of_open_trades_;

    // Current market state
    double current_ask_;
    double current_bid_;
    double current_spread_;
    double current_equity_;
    double current_balance_;

    // Trade sizing
    double trade_size_buy_;
    double spacing_buy_;

    // Statistics
    double max_balance_;
    int max_number_of_open_;
    double max_used_funds_;
    double max_trade_size_;

    // DD Protection
    bool enable_dd_protection_;
    double dd_threshold_pct_;
    double peak_equity_;
    int dd_triggers_;

    // Debug
    int debug_counter_;

    void Iterate(TickBasedEngine& engine) {
        // Use engine's incrementally-maintained aggregates for volume/highest/lowest
        volume_of_open_trades_ = engine.GetBuyVolume();
        lowest_buy_ = engine.GetLowestBuyEntry();
        highest_buy_ = engine.GetHighestBuyEntry();

        // Strategy-specific: closest_above/below requires per-tick iteration
        closest_above_ = DBL_MAX;
        closest_below_ = DBL_MAX;  // Must be DBL_MAX (like MT5) for gap-fill condition to work

        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->IsBuy()) {
                double open_price = trade->entry_price;
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
        const auto& cfg = engine.GetConfig();

        // Calculate margin parameters
        double used_margin = engine.GetUsedMargin();
        double margin_stop_out_level = 20.0;  // MT5  stop out level

        // Calculate current margin level (equity / used_margin * 100)
        double current_margin_level = (used_margin > 0) ? (current_equity_ / used_margin * 100.0) : 10000.0;

        // Calculate equity at target margin level
        double equity_at_target = current_equity_ * margin_stop_out_level / current_margin_level;
        double equity_difference = current_equity_ - equity_at_target;

        // Calculate price difference we can survive
        double price_difference = 0.0;
        if (volume_of_open_trades_ > 0) {
            price_difference = equity_difference / (volume_of_open_trades_ * cfg.contract_size);
        }

        // Determine end price (target drawdown level)
        double end_price = 0.0;
        if (positions_total == 0) {
            end_price = current_ask_ * ((100.0 - survive_pct_) / 100.0);
        } else {
            end_price = highest_buy_ * ((100.0 - survive_pct_) / 100.0);
        }

        double distance = current_ask_ - end_price;
        double number_of_trades = std::floor(distance / spacing_buy_);

        // Calculate if we can afford new positions
        if ((positions_total == 0) || ((current_ask_ - price_difference) < end_price)) {
            equity_at_target = current_equity_ - volume_of_open_trades_ * std::abs(distance) * cfg.contract_size;
            double margin_level = (used_margin > 0) ? (equity_at_target / used_margin * 100.0) : 10000.0;

            double trade_size = min_volume_;

            if (margin_level > margin_stop_out_level) {
                // Calculate required equity for all trades in grid
                double d_equity = cfg.contract_size * trade_size * (number_of_trades * (number_of_trades + 1) / 2);
                double d_spread = number_of_trades * trade_size * current_spread_ * cfg.contract_size;
                d_equity += d_spread;

                // Calculate required margin for forward-looking grid sizing
                double local_used_margin = engine.CalculateMarginRequired(trade_size, current_ask_) * number_of_trades;

                // Debug output for first 10 sizing calls
                if (debug_counter_ < 10) {
                    std::cout << "=== SIZING DEBUG #" << debug_counter_ << " ===" << std::endl;
                    std::cout << "Equity: $" << current_equity_ << std::endl;
                    std::cout << "Used Margin: $" << used_margin << std::endl;
                    std::cout << "Margin Level: " << current_margin_level << "%" << std::endl;
                    std::cout << "Positions Total: " << positions_total << std::endl;
                    std::cout << "Number of Trades: " << number_of_trades << std::endl;
                    std::cout << "Equity at Target (AFTER adjustment): $" << equity_at_target << std::endl;
                    std::cout << "Margin Level after adjustment: " << margin_level << "%" << std::endl;
                    std::cout << "d_equity (grid loss cost): $" << d_equity << std::endl;
                    std::cout << "local_used_margin (grid margin): $" << local_used_margin << std::endl;
                    std::cout << "Total Required: $" << (d_equity + local_used_margin) << std::endl;
                }

                // Find maximum multiplier that keeps us above stop out
                double multiplier = 0.0;
                double equity_backup = equity_at_target;
                double used_margin_backup = used_margin;

                double max = max_volume_ / min_volume_;

                // Try maximum first
                equity_at_target -= max * d_equity;
                used_margin += max * local_used_margin;

                if (margin_stop_out_level < (equity_at_target / used_margin * 100.0)) {
                    multiplier = max;
                } else {
                    // Binary search for optimal multiplier
                    used_margin = used_margin_backup;
                    equity_at_target = equity_backup;

                    for (double increment = max; increment >= 1; increment = increment / 10) {
                        if (debug_counter_ == 8) {
                            std::cout << "Binary search increment: " << increment << ", starting multiplier: " << multiplier << std::endl;
                        }
                        while (margin_stop_out_level < (equity_at_target / used_margin * 100.0)) {
                            equity_backup = equity_at_target;
                            used_margin_backup = used_margin;
                            multiplier += increment;
                            equity_at_target -= increment * d_equity;
                            used_margin += increment * local_used_margin;
                            if (debug_counter_ == 8) {
                                std::cout << "  Added " << increment << ", multiplier=" << multiplier
                                          << ", margin_level=" << (equity_at_target / used_margin * 100.0) << "%" << std::endl;
                            }
                        }
                        multiplier -= increment;
                        used_margin = used_margin_backup;
                        equity_at_target = equity_backup;
                        if (debug_counter_ == 8) {
                            std::cout << "  After decrement: multiplier=" << multiplier << std::endl;
                        }
                    }
                }

                multiplier = std::max(1.0, multiplier);
                trade_size_buy_ = multiplier * min_volume_;
                trade_size_buy_ = std::min(trade_size_buy_, max_volume_);
                max_trade_size_ = std::max(max_trade_size_, trade_size_buy_);

                // Debug output continued
                if (debug_counter_ < 10) {
                    std::cout << "Multiplier Found: " << multiplier << std::endl;
                    std::cout << "FINAL TRADE SIZE: " << trade_size_buy_ << " lots" << std::endl;
                    std::cout << "=====================================" << std::endl << std::endl;
                    debug_counter_++;
                }
            }
        }
    }

    bool Open(double local_unit, TickBasedEngine& engine) {
        if (local_unit < min_volume_) {
            return false;
        }

        double final_unit = engine.NormalizeLots(std::min(local_unit, max_volume_));

        // Calculate TP
        double tp = current_ask_ + current_spread_ + spacing_buy_;

        // Open BUY position
        Trade* trade = engine.OpenMarketOrder("BUY", final_unit, 0.0, tp);

        return (trade != nullptr);
    }

    void OpenNew(TickBasedEngine& engine) {
        int positions_total = engine.GetOpenPositions().size();

        if (positions_total == 0) {
            // First position
            SizingBuy(engine, positions_total);
            if (Open(trade_size_buy_, engine)) {
                highest_buy_ = current_ask_;
                lowest_buy_ = current_ask_;
            }
        } else {
            // Check if price moved below lowest position (add to grid below)
            if (lowest_buy_ >= current_ask_ + spacing_buy_) {
                SizingBuy(engine, positions_total);
                if (Open(trade_size_buy_, engine)) {
                    lowest_buy_ = current_ask_;
                }
            }
            // Check if price moved above highest position (add to grid above)
            else if (highest_buy_ <= current_ask_ - spacing_buy_) {
                SizingBuy(engine, positions_total);
                if (Open(trade_size_buy_, engine)) {
                    highest_buy_ = current_ask_;
                }
            }
            // Check if there's a gap in the grid
            else if ((closest_above_ >= spacing_buy_) && (closest_below_ >= spacing_buy_)) {
                SizingBuy(engine, positions_total);
                Open(trade_size_buy_, engine);
            }
        }
    }
};

} // namespace backtest

#endif // FILL_UP_STRATEGY_H
