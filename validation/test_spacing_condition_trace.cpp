/**
 * test_spacing_condition_trace.cpp
 *
 * Trace exactly when the spacing condition is true to compare with MT5
 * MT5 shows 2.7M entry attempts, C++ shows 190K - need to understand why
 */

#include "../include/strategy_combined_ju.h"
#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <deque>
#include <cmath>

using namespace backtest;

std::vector<Tick> g_ticks;

void LoadTicks() {
    std::cout << "Loading tick data..." << std::endl;
    std::vector<std::string> files = {
        "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv",
        "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_JAN2026.csv"
    };
    for (const auto& file : files) {
        TickDataManager mgr(file);
        Tick tick;
        while (mgr.GetNextTick(tick)) {
            g_ticks.push_back(tick);
        }
    }
    std::sort(g_ticks.begin(), g_ticks.end(),
              [](const Tick& a, const Tick& b) { return a.timestamp < b.timestamp; });
    std::cout << "Loaded " << g_ticks.size() << " ticks" << std::endl;
}

int main() {
    std::cout << "=" << std::string(80, '=') << std::endl;
    std::cout << "SPACING CONDITION TRACE" << std::endl;
    std::cout << "Understanding why MT5 has 14x more entry attempts than C++" << std::endl;
    std::cout << "=" << std::string(80, '=') << std::endl << std::endl;

    LoadTicks();

    // Config matching CombinedJu P1_M3
    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = 10000.0;
    config.contract_size = 100.0;
    config.leverage = 500.0;
    config.pip_size = 0.01;
    config.swap_long = -68.25;
    config.swap_short = 35.06;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = "2025.01.01";
    config.end_date = "2026.01.27";

    TickDataConfig tick_cfg;
    tick_cfg.format = TickDataFormat::MT5_CSV;
    config.tick_data_config = tick_cfg;

    TickBasedEngine engine(config);

    // Manual tracking (replicating strategy internals)
    double base_spacing = 1.5;
    double typical_vol_pct = 0.55;
    double volatility_lookback_hours = 4.0;
    int velocity_window = 10;
    double velocity_threshold_pct = 0.01;

    double current_spacing = base_spacing;
    double recent_high = 0;
    double recent_low = DBL_MAX;
    long last_vol_reset = 0;

    std::deque<double> price_window;
    double current_velocity_pct = 0;

    double lowest_buy = DBL_MAX;
    double highest_buy = 0;
    int position_count = 0;
    double first_entry_price = 0;

    // Statistics
    long total_ticks = 0;
    long spacing_condition_true = 0;
    long velocity_blocks = 0;
    long entries_allowed = 0;
    int max_positions = 0;
    double min_spacing = DBL_MAX;
    double max_spacing = 0;
    double sum_spacing = 0;
    long spacing_samples = 0;

    // Extra diagnostic
    long ticks_with_positions = 0;
    long spacing_changes = 0;

    auto ParseTimestamp = [](const std::string& ts) -> long {
        int year = std::stoi(ts.substr(0, 4));
        int month = std::stoi(ts.substr(5, 2));
        int day = std::stoi(ts.substr(8, 2));
        int hour = std::stoi(ts.substr(11, 2));
        int minute = std::stoi(ts.substr(14, 2));
        int second = std::stoi(ts.substr(17, 2));
        int month_days[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
        long days = (long)(year - 2020) * 365 + (year - 2020) / 4;
        days += month_days[month - 1] + day;
        if (month > 2 && year % 4 == 0) days++;
        return days * 86400L + hour * 3600L + minute * 60L + second;
    };

    std::cout << "Processing ticks..." << std::endl;

    for (const auto& tick : g_ticks) {
        total_ticks++;

        double ask = tick.ask;
        double bid = tick.bid;
        double spread = ask - bid;

        // Update velocity
        price_window.push_back(bid);
        while ((int)price_window.size() > velocity_window) {
            price_window.pop_front();
        }
        if ((int)price_window.size() >= velocity_window) {
            double old_price = price_window.front();
            current_velocity_pct = (bid - old_price) / old_price * 100.0;
        }

        // Update volatility
        long current_seconds = ParseTimestamp(tick.timestamp);
        long lookback_seconds = (long)(volatility_lookback_hours * 3600.0);
        if (last_vol_reset == 0 || current_seconds - last_vol_reset >= lookback_seconds) {
            recent_high = bid;
            recent_low = bid;
            last_vol_reset = current_seconds;
        }
        recent_high = std::max(recent_high, bid);
        recent_low = std::min(recent_low, bid);

        // Update adaptive spacing
        double range = recent_high - recent_low;
        if (range > 0 && recent_high > 0 && bid > 0) {
            double typical_vol = bid * (typical_vol_pct / 100.0);
            double vol_ratio = range / typical_vol;
            vol_ratio = std::max(0.5, std::min(3.0, vol_ratio));
            double new_spacing = base_spacing * vol_ratio;
            new_spacing = std::max(0.5, std::min(5.0, new_spacing));
            if (std::abs(new_spacing - current_spacing) > 0.1) {
                current_spacing = new_spacing;
                spacing_changes++;
            }
        }

        // Track spacing stats
        if (current_spacing > 0) {
            min_spacing = std::min(min_spacing, current_spacing);
            max_spacing = std::max(max_spacing, current_spacing);
            sum_spacing += current_spacing;
            spacing_samples++;
        }

        // Process through engine to track positions
        engine.ProcessTick(tick);

        // Count open positions and find lowest/highest buy
        lowest_buy = DBL_MAX;
        highest_buy = 0;
        position_count = 0;
        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->direction == "BUY") {
                lowest_buy = std::min(lowest_buy, trade->entry_price);
                highest_buy = std::max(highest_buy, trade->entry_price);
                position_count++;
            }
        }

        if (position_count > max_positions) {
            max_positions = position_count;
        }

        // Entry logic (matching CombinedJu exactly)
        if (position_count == 0) {
            // First position - open immediately, no velocity filter
            double equity = engine.GetEquity();
            if (equity > 0) {
                double end_price = ask * ((100.0 - 13.0) / 100.0);
                double distance = ask - end_price;
                double num_trades = std::floor(distance / current_spacing);
                if (num_trades <= 0) num_trades = 1;

                double lots = 0.01;  // Simplified lot calculation
                double tp = ask + spread + current_spacing;

                engine.OpenPosition("BUY", lots, tp, 0.0);
                first_entry_price = ask;
                entries_allowed++;
            }
        } else {
            ticks_with_positions++;

            // Check spacing condition - THIS IS THE KEY
            if (lowest_buy >= ask + current_spacing) {
                spacing_condition_true++;

                // Check velocity filter
                if (std::abs(current_velocity_pct) >= velocity_threshold_pct) {
                    velocity_blocks++;
                } else {
                    // Would open position
                    double lots = 0.01;  // Simplified
                    double deviation = std::abs(first_entry_price - ask);
                    double tp_addition = 0.5 * std::sqrt(deviation);
                    double tp_distance = std::max(1.5, tp_addition);
                    double tp = ask + spread + tp_distance;

                    engine.OpenPosition("BUY", lots, tp, 0.0);
                    entries_allowed++;
                }
            }
        }

        // Progress
        if (total_ticks % 10000000 == 0) {
            std::cout << "  " << total_ticks / 1000000 << "M ticks processed..." << std::endl;
        }
    }

    // Print results
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "RESULTS (C++ Manual Trace)" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Total ticks:              " << total_ticks << std::endl;
    std::cout << "Ticks with positions:     " << ticks_with_positions << std::endl;
    std::cout << "Spacing condition true:   " << spacing_condition_true << std::endl;
    std::cout << "Velocity blocks:          " << velocity_blocks << std::endl;
    std::cout << "Entries allowed:          " << entries_allowed << std::endl;
    std::cout << "Block rate:               " << std::setprecision(2)
              << (spacing_condition_true > 0 ? 100.0 * velocity_blocks / spacing_condition_true : 0)
              << "%" << std::endl;
    std::cout << "Max positions:            " << max_positions << std::endl;
    std::cout << "Spacing changes:          " << spacing_changes << std::endl;
    std::cout << "Min spacing:              $" << min_spacing << std::endl;
    std::cout << "Max spacing:              $" << max_spacing << std::endl;
    std::cout << "Avg spacing:              $" << (spacing_samples > 0 ? sum_spacing / spacing_samples : 0) << std::endl;

    std::cout << "\n=== COMPARISON WITH MT5 ===" << std::endl;
    std::cout << "                          C++            MT5" << std::endl;
    std::cout << "Entry attempts:     " << std::setw(12) << (velocity_blocks + entries_allowed)
              << std::setw(15) << "2,695,102" << std::endl;
    std::cout << "Velocity blocks:    " << std::setw(12) << velocity_blocks
              << std::setw(15) << "2,663,922" << std::endl;
    std::cout << "Entries allowed:    " << std::setw(12) << entries_allowed
              << std::setw(15) << "31,180" << std::endl;
    std::cout << "Block rate:         " << std::setw(11)
              << (spacing_condition_true > 0 ? 100.0 * velocity_blocks / spacing_condition_true : 0) << "%"
              << std::setw(14) << "98.84%" << std::endl;

    double ratio = (velocity_blocks + entries_allowed) > 0
                   ? 2695102.0 / (velocity_blocks + entries_allowed)
                   : 0;
    std::cout << "\nMT5/C++ entry attempt ratio: " << std::setprecision(1) << ratio << "x" << std::endl;

    return 0;
}
