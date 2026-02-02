/**
 * Dual TP Grid Strategy
 *
 * At each grid level, open 2 microlots:
 * - One with tight TP (1x spacing) - captures quick oscillations
 * - One with wide TP (e.g., 5x spacing) - captures bigger moves
 *
 * When tight TP closes and price returns to that level, re-open another tight TP lot.
 *
 * Key insight: Price won't return to EXACT entry price, so we use ZONES:
 * - Each grid level is a ZONE of size = spacing
 * - When price enters a zone where we only have wide TP (tight closed), re-enter tight
 *
 * Survive scaling manipulates trade size and spacing.
 */

#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <map>
#include <cmath>
#include <deque>
#include <set>

using namespace backtest;

// Track what we have at each zone
struct ZoneState {
    bool has_tight = false;     // Do we have a tight TP position in this zone?
    bool has_wide = false;      // Do we have a wide TP position in this zone?
    double tight_entry = 0;     // Entry price of tight TP position
    double wide_entry = 0;      // Entry price of wide TP position
    int tight_ticket = -1;      // Ticket ID of tight TP position (-1 = none)
    int wide_ticket = -1;       // Ticket ID of wide TP position
};

// Simple position struct for internal use
struct Position {
    TradeDirection direction;
    double entry_price;
    double lot_size;
    double take_profit;
    int ticket;
};

class DualTPGrid {
public:
    struct Config {
        double survive_pct = 15.0;      // Survive percentage for sizing
        double base_spacing = 1.5;       // Base grid spacing in $
        double min_volume = 0.01;        // Microlot
        double max_volume = 1.0;         // Max per position
        double contract_size = 100.0;
        double leverage = 500.0;
        double tight_tp_mult = 1.0;      // Tight TP = spacing * this
        double wide_tp_mult = 5.0;       // Wide TP = spacing * this
        double zone_tolerance = 0.3;     // Re-entry if within this fraction of spacing from zone center
        bool adaptive_spacing = true;
        double typical_vol_pct = 0.55;
        int vol_lookback_ticks = 36000;  // ~1 hour at 10 ticks/sec
    };

    DualTPGrid(const Config& config) : config_(config), current_spacing_(config.base_spacing) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        double price = tick.ask;

        // Update adaptive spacing
        UpdateSpacing(tick.bid);

        // Get current positions and build zone state map
        const auto& open_trades = engine.GetOpenPositions();

        // Convert Trade* to Position-like for compatibility
        std::vector<Position> positions;
        for (const auto* trade : open_trades) {
            Position pos;
            pos.direction = trade->direction;
            pos.entry_price = trade->entry_price;
            pos.lot_size = trade->lot_size;
            pos.take_profit = trade->take_profit;
            pos.ticket = trade->id;
            positions.push_back(pos);
        }
        UpdateZoneStates(positions);

        // Track price range
        double lowest_entry = 1e9;
        double highest_entry = 0;
        int pos_count = 0;

        for (const auto& pos : positions) {
            if (pos.direction == TradeDirection::BUY && pos.lot_size > 0) {
                lowest_entry = std::min(lowest_entry, pos.entry_price);
                highest_entry = std::max(highest_entry, pos.entry_price);
                pos_count++;
            }
        }

        // Get current zone
        int current_zone = GetZone(price);

        // Calculate position size
        double lot_size = CalculateLotSize(price, engine, pos_count, highest_entry);

        // Entry logic
        if (positions.empty()) {
            // First entry - open 2 positions
            OpenDualEntry(current_zone, price, lot_size, tick.timestamp, engine);
        } else {
            // Check current zone state
            auto& zone = zone_states_[current_zone];

            // CASE 1: New zone on the downside (dip below lowest)
            if (price <= lowest_entry - current_spacing_) {
                int new_zone = GetZone(price);
                if (!zone_states_[new_zone].has_tight && !zone_states_[new_zone].has_wide) {
                    OpenDualEntry(new_zone, price, lot_size, tick.timestamp, engine);
                }
            }
            // CASE 2: New zone on the upside (rally above highest)
            else if (price >= highest_entry + current_spacing_) {
                int new_zone = GetZone(price);
                if (!zone_states_[new_zone].has_tight && !zone_states_[new_zone].has_wide) {
                    OpenDualEntry(new_zone, price, lot_size, tick.timestamp, engine);
                }
            }
            // CASE 3: Re-entry in existing zone where tight TP closed
            else if (!zone.has_tight && zone.has_wide) {
                // We have wide but not tight - price returned to this zone
                // Check if we're close enough to the wide entry to re-enter
                double zone_center = GetZoneCenter(current_zone);
                double distance_from_center = std::abs(price - zone_center);

                if (distance_from_center < current_spacing_ * config_.zone_tolerance) {
                    // Re-open tight TP at current price
                    double tight_tp = price + current_spacing_ * config_.tight_tp_mult;
                    engine.OpenMarketOrder(TradeDirection::BUY, lot_size, 0.0, tight_tp);
                    zone.has_tight = true;
                    zone.tight_entry = price;
                    tight_reentries_++;
                }
            }
        }

        tick_count_++;
    }

    int GetTightReentries() const { return tight_reentries_; }
    double GetCurrentSpacing() const { return current_spacing_; }

private:
    Config config_;
    double current_spacing_;
    std::map<int, ZoneState> zone_states_;  // Zone ID -> state
    std::deque<double> price_history_;
    int tick_count_ = 0;
    int tight_reentries_ = 0;

    int GetZone(double price) const {
        // Zone ID = floor(price / spacing)
        return static_cast<int>(std::floor(price / current_spacing_));
    }

    double GetZoneCenter(int zone) const {
        return (zone + 0.5) * current_spacing_;
    }

    void UpdateZoneStates(const std::vector<Position>& positions) {
        // Reset all zones
        for (auto& [zone_id, state] : zone_states_) {
            state.has_tight = false;
            state.has_wide = false;
            state.tight_ticket = -1;
            state.wide_ticket = -1;
        }

        // Rebuild from current positions
        for (const auto& pos : positions) {
            if (pos.direction != TradeDirection::BUY || pos.lot_size <= 0) continue;

            int zone = GetZone(pos.entry_price);
            auto& state = zone_states_[zone];

            // Classify by TP distance
            double tp_distance = pos.take_profit - pos.entry_price;
            double expected_tight = current_spacing_ * config_.tight_tp_mult;
            double expected_wide = current_spacing_ * config_.wide_tp_mult;

            // Use relative tolerance for classification
            double tight_tolerance = expected_tight * 0.5;
            double wide_tolerance = expected_wide * 0.3;

            if (std::abs(tp_distance - expected_tight) < tight_tolerance) {
                state.has_tight = true;
                state.tight_entry = pos.entry_price;
                state.tight_ticket = pos.ticket;
            } else if (std::abs(tp_distance - expected_wide) < wide_tolerance) {
                state.has_wide = true;
                state.wide_entry = pos.entry_price;
                state.wide_ticket = pos.ticket;
            }
            // If neither matches, it's from a different spacing period - treat as wide
            else if (tp_distance > expected_tight) {
                state.has_wide = true;
                state.wide_entry = pos.entry_price;
            }
        }
    }

    void UpdateSpacing(double bid) {
        price_history_.push_back(bid);
        while (price_history_.size() > static_cast<size_t>(config_.vol_lookback_ticks)) {
            price_history_.pop_front();
        }

        if (config_.adaptive_spacing && price_history_.size() >= 100) {
            double high = *std::max_element(price_history_.begin(), price_history_.end());
            double low = *std::min_element(price_history_.begin(), price_history_.end());
            double mid = (high + low) / 2.0;
            if (mid > 0) {
                double range_pct = (high - low) / mid * 100.0;
                double vol_ratio = range_pct / config_.typical_vol_pct;
                current_spacing_ = config_.base_spacing * vol_ratio;
                current_spacing_ = std::max(current_spacing_, config_.base_spacing * 0.5);
                current_spacing_ = std::min(current_spacing_, config_.base_spacing * 3.0);
            }
        }
    }

    void OpenDualEntry(int zone, double price, double lot_size, const std::string& timestamp, TickBasedEngine& engine) {
        // Open tight TP position
        double tight_tp = price + current_spacing_ * config_.tight_tp_mult;
        engine.OpenMarketOrder(TradeDirection::BUY, lot_size, 0.0, tight_tp);

        // Open wide TP position
        double wide_tp = price + current_spacing_ * config_.wide_tp_mult;
        engine.OpenMarketOrder(TradeDirection::BUY, lot_size, 0.0, wide_tp);

        // Update zone state
        auto& state = zone_states_[zone];
        state.has_tight = true;
        state.has_wide = true;
        state.tight_entry = price;
        state.wide_entry = price;
    }

    double CalculateLotSize(double price, TickBasedEngine& engine, int pos_count, double highest_entry) {
        double equity = engine.GetEquity();

        // Target end price based on survive percentage
        double end_price = (pos_count == 0)
            ? price * ((100.0 - config_.survive_pct) / 100.0)
            : highest_entry * ((100.0 - config_.survive_pct) / 100.0);

        double distance = price - end_price;
        if (distance <= 0) distance = current_spacing_;

        // Estimate number of trades to that level
        double number_of_trades = std::floor(distance / current_spacing_);
        if (number_of_trades <= 0) number_of_trades = 1;

        // Account for 2x positions per level (tight + wide)
        number_of_trades *= 2;

        // Calculate sustainable lot size
        double current_volume = 0;
        for (const auto* trade : engine.GetOpenPositions()) {
            current_volume += trade->lot_size;
        }

        double equity_at_target = equity - current_volume * distance * config_.contract_size;

        // Estimate used margin
        double used_margin = current_volume * config_.contract_size * price / config_.leverage;

        // Check margin constraint
        if (used_margin > 0 && (equity_at_target / used_margin * 100.0) < 30.0) {
            return config_.min_volume;
        }

        // Binary search for optimal size
        double trade_size = config_.min_volume;
        double d_equity = config_.contract_size * trade_size * current_spacing_ *
                         (number_of_trades * (number_of_trades + 1) / 2);
        double d_margin = number_of_trades * trade_size * config_.contract_size / config_.leverage;

        double max_mult = config_.max_volume / config_.min_volume;
        double low = 1.0, high = max_mult;
        double best_mult = 1.0;

        while (high - low > 0.05) {
            double mid = (low + high) / 2.0;
            double test_equity = equity_at_target - mid * d_equity;
            double test_margin = used_margin + mid * d_margin;
            if (test_margin > 0 && (test_equity / test_margin * 100.0) > 30.0) {
                best_mult = mid;
                low = mid;
            } else {
                high = mid;
            }
        }

        return std::min(best_mult * config_.min_volume, config_.max_volume);
    }
};

void RunTest(const std::string& name, DualTPGrid::Config config, bool verbose = false) {
    std::cout << "\n=== " << name << " ===" << std::endl;

    // Setup engine
    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;

    TickBacktestConfig engine_config;
    engine_config.symbol = "XAUUSD";
    engine_config.initial_balance = 10000.0;
    engine_config.contract_size = config.contract_size;
    engine_config.leverage = config.leverage;
    engine_config.pip_size = 0.01;
    engine_config.swap_long = -78.57;
    engine_config.swap_short = 39.14;
    engine_config.swap_mode = 1;
    engine_config.swap_3days = 3;
    engine_config.start_date = "2025.01.01";
    engine_config.end_date = "2025.12.30";
    engine_config.tick_data_config = tick_config;

    TickBasedEngine engine(engine_config);
    DualTPGrid strategy(config);

    int tick_count = 0;
    std::cout << "Starting backtest..." << std::endl;
    engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
        tick_count++;

        if (tick_count == 1) {
            std::cout << "First tick processed" << std::endl;
        }
        if (tick_count == 100) {
            std::cout << "100 ticks processed" << std::endl;
        }
        if (tick_count == 1000) {
            std::cout << "1000 ticks processed" << std::endl;
        }

        if (verbose && tick_count % 5000000 == 0) {
            auto results = eng.GetResults();
            std::cout << "  " << tick_count / 1000000 << "M ticks, Equity: $"
                      << std::fixed << std::setprecision(2) << results.final_balance
                      << ", DD: " << results.max_drawdown_pct << "%"
                      << ", Positions: " << eng.GetOpenPositions().size()
                      << ", Tight re-entries: " << strategy.GetTightReentries()
                      << std::endl;
        }
    });

    auto results = engine.GetResults();
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Final:     $" << results.final_balance
              << " (" << results.final_balance / 10000.0 << "x)" << std::endl;
    std::cout << "Max DD:    " << results.max_drawdown_pct << "%" << std::endl;
    std::cout << "Trades:    " << results.total_trades << std::endl;
    std::cout << "Tight re-entries: " << strategy.GetTightReentries() << std::endl;
}

int main() {
    std::cout << "==================================================" << std::endl;
    std::cout << "   Dual TP Grid Strategy Test" << std::endl;
    std::cout << "   2 lots per level: tight TP + wide TP" << std::endl;
    std::cout << "==================================================" << std::endl;

    // Test 1: Basic dual TP (tight=1x, wide=5x)
    DualTPGrid::Config config1;
    config1.survive_pct = 15.0;
    config1.base_spacing = 1.5;
    config1.tight_tp_mult = 1.0;
    config1.wide_tp_mult = 5.0;
    config1.max_volume = 0.5;
    RunTest("Dual TP (tight=1x, wide=5x)", config1, true);

    // Test 2: Wider spacing
    DualTPGrid::Config config2;
    config2.survive_pct = 15.0;
    config2.base_spacing = 3.0;
    config2.tight_tp_mult = 1.0;
    config2.wide_tp_mult = 5.0;
    config2.max_volume = 0.5;
    RunTest("Dual TP (wider spacing $3)", config2, true);

    // Test 3: More conservative (survive 20%)
    DualTPGrid::Config config3;
    config3.survive_pct = 20.0;
    config3.base_spacing = 2.0;
    config3.tight_tp_mult = 1.0;
    config3.wide_tp_mult = 3.0;
    config3.max_volume = 0.3;
    RunTest("Dual TP (conservative, wide=3x)", config3, true);

    // Test 4: Tighter oscillation capture
    DualTPGrid::Config config4;
    config4.survive_pct = 15.0;
    config4.base_spacing = 1.0;
    config4.tight_tp_mult = 0.5;  // Half spacing for tight
    config4.wide_tp_mult = 3.0;
    config4.max_volume = 0.3;
    RunTest("Dual TP (tight=0.5x, wide=3x)", config4, true);

    return 0;
}
