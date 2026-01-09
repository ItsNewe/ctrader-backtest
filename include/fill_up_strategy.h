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
                   double leverage, int symbol_digits = 2, double margin_rate = 1.0)
        : survive_pct_(survive_pct),
          size_multiplier_(size_multiplier),
          debug_counter_(0),
          spacing_(spacing_dollars),
          min_volume_(min_volume),
          max_volume_(max_volume),
          contract_size_(contract_size),
          leverage_(leverage),
          symbol_digits_(symbol_digits),
          margin_rate_(margin_rate),
          lowest_buy_(DBL_MAX),
          highest_buy_(DBL_MIN),
          volume_of_open_trades_(0.0),
          trade_size_buy_(0.0),
          spacing_buy_(spacing_dollars),
          max_balance_(0.0),
          max_number_of_open_(0),
          max_used_funds_(0.0),
          max_trade_size_(0.0)
    {
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_spread_ = tick.spread();

        // Update account state
        current_equity_ = engine.GetEquity();
        current_balance_ = engine.GetBalance();

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

private:
    // Strategy parameters
    double survive_pct_;           // Drawdown survival percentage
    double size_multiplier_;       // Lot size multiplier
    double spacing_;               // Spacing between trades (in currency units)
    double min_volume_;            // Minimum lot size
    double max_volume_;            // Maximum lot size
    double contract_size_;         // Contract size (e.g., 100 for gold)
    double leverage_;              // Account leverage
    int symbol_digits_;            // Price decimal places
    double margin_rate_;           // Margin rate (initial_margin_rate from MT5)

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

    // Debug
    int debug_counter_;

    void Iterate(TickBasedEngine& engine) {
        // Reset tracking variables
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

        // Calculate margin parameters
        double used_margin = CalculateUsedMargin(engine);
        double margin_stop_out_level = 20.0;  // MT5  stop out level

        // Calculate current margin level (equity / used_margin * 100)
        double current_margin_level = (used_margin > 0) ? (current_equity_ / used_margin * 100.0) : 10000.0;

        // Calculate equity at target margin level
        double equity_at_target = current_equity_ * margin_stop_out_level / current_margin_level;
        double equity_difference = current_equity_ - equity_at_target;

        // Calculate price difference we can survive
        double price_difference = 0.0;
        if (volume_of_open_trades_ > 0) {
            price_difference = equity_difference / (volume_of_open_trades_ * contract_size_);
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
            equity_at_target = current_equity_ - volume_of_open_trades_ * std::abs(distance) * contract_size_;
            double margin_level = (used_margin > 0) ? (equity_at_target / used_margin * 100.0) : 10000.0;

            double trade_size = min_volume_;
            double starting_price = current_ask_;

            if (margin_level > margin_stop_out_level) {
                // Calculate required equity for all trades in grid
                double d_equity = contract_size_ * trade_size * (number_of_trades * (number_of_trades + 1) / 2);
                double d_spread = number_of_trades * trade_size * current_spread_ * contract_size_;
                d_equity += d_spread;

                // Calculate required margin using FOREX mode (MT5 quirk!)
                // MT5 uses FOREX mode here even though actual positions use CFD_LEVERAGE
                double local_used_margin = CalculateMarginForSizing(trade_size);
                local_used_margin += CalculateMarginForSizing(trade_size);
                local_used_margin = local_used_margin / 2;
                local_used_margin = number_of_trades * local_used_margin;

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

    double CalculateUsedMargin(TickBasedEngine& engine) {
        // Calculate used margin for all open positions
        double used_margin = 0.0;
        const auto& positions = engine.GetOpenPositions();

        for (const Trade* trade : positions) {
            used_margin += CalculateMarginForTrade(trade->lot_size, trade->entry_price);
        }

        return used_margin;
    }

    double CalculateMarginForTrade(double lots, double price) {
        // XAUUSD (Gold) uses price-based margin calculation for ACTUAL positions
        // Margin = Lots * Contract_Size * Price / Leverage * MarginRate
        // This is CFD_LEVERAGE mode calculation with margin rate
        return lots * contract_size_ * price / leverage_ * margin_rate_;
    }

    double CalculateMarginForSizing(double lots) {
        // CRITICAL: MT5 uses FOREX mode (no price) in the sizing algorithm
        // even though actual positions use CFD_LEVERAGE mode!
        // This underestimates margin, making the algorithm more conservative
        // Margin = Lots * Contract_Size / Leverage * MarginRate
        return lots * contract_size_ / leverage_ * margin_rate_;
    }

    bool Open(double local_unit, TickBasedEngine& engine) {
        if (local_unit < min_volume_) {
            return false;
        }

        double final_unit = std::min(local_unit, max_volume_);
        final_unit = NormalizeVolume(final_unit);

        // Calculate TP
        double tp = current_ask_ + current_spread_ + spacing_buy_;

        // Open BUY position
        Trade* trade = engine.OpenMarketOrder("BUY", final_unit, 0.0, tp);

        return (trade != nullptr);
    }

    double NormalizeVolume(double volume) {
        int digits = 2;  // Default for most brokers
        if (min_volume_ == 0.01) digits = 2;
        else if (min_volume_ == 0.1) digits = 1;
        else digits = 0;

        double factor = std::pow(10.0, digits);
        return std::round(volume * factor) / factor;
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
