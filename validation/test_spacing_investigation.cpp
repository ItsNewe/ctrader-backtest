/**
 * Spacing Investigation: Why does $0.30 outperform $1.00+?
 *
 * Questions to answer:
 * 1. Is $0.30 optimal across all periods, or just 2025 average?
 * 2. How does trade count vary with spacing?
 * 3. What is gold's typical oscillation size?
 * 4. Is there a relationship between volatility and optimal spacing?
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <cmath>
#include <map>

using namespace backtest;

struct PeriodResult {
    std::string period_name;
    double spacing;
    double return_multiple;
    double max_dd_pct;
    int total_trades;
    bool stopped_out;
};

struct GoldStats {
    double avg_daily_range;      // Average (high - low) per day
    double avg_hourly_range;     // Average hourly range
    double avg_tick_move;        // Average absolute tick-to-tick move
    double median_oscillation;   // Median distance between local highs/lows
    int oscillation_count;       // Number of reversals detected
    double price_start;
    double price_end;
    double price_change_pct;
};

PeriodResult RunSpacingTest(double spacing, const std::string& data_path,
                            const std::string& start_date, const std::string& end_date,
                            const std::string& period_name, double initial_balance) {
    PeriodResult result;
    result.period_name = period_name;
    result.spacing = spacing;

    TickDataConfig tick_config;
    tick_config.file_path = data_path;
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = initial_balance;
    config.account_currency = "USD";
    config.contract_size = 100.0;
    config.leverage = 500.0;
    config.margin_rate = 1.0;
    config.pip_size = 0.01;
    config.swap_long = -66.99;
    config.swap_short = 41.2;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = start_date;
    config.end_date = end_date;
    config.tick_data_config = tick_config;
    config.verbose = false;

    try {
        TickBasedEngine engine(config);

        FillUpOscillation strategy(
            13.0,   // survive_pct
            spacing,
            0.01,
            10.0,
            100.0,
            500.0,
            FillUpOscillation::Mode::BASELINE,
            0.1,
            30.0,
            1.0
        );

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto res = engine.GetResults();
        result.return_multiple = res.final_balance / initial_balance;
        double peak_equity = res.final_balance + res.max_drawdown;
        result.max_dd_pct = (peak_equity > 0) ? (res.max_drawdown / peak_equity * 100.0) : 0;
        result.total_trades = res.total_trades;
        result.stopped_out = engine.IsStopOutOccurred();

    } catch (...) {
        result.return_multiple = 0;
        result.max_dd_pct = 100;
        result.stopped_out = true;
    }

    return result;
}

// Helper to compare dates in YYYY.MM.DD format
bool DateInRange(const std::string& timestamp, const std::string& start, const std::string& end) {
    // Extract date part (first 10 chars: YYYY.MM.DD)
    if (timestamp.length() < 10) return false;
    std::string date = timestamp.substr(0, 10);
    return date >= start && date < end;
}

GoldStats AnalyzeGoldCharacteristics(const std::string& data_path,
                                     const std::string& start_date,
                                     const std::string& end_date) {
    GoldStats stats = {};

    TickDataConfig tick_config;
    tick_config.file_path = data_path;
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickDataManager manager(tick_config);

    double prev_price = 0;
    double day_high = 0, day_low = 999999;
    double hour_high = 0, hour_low = 999999;
    int prev_day = -1, prev_hour = -1;

    std::vector<double> daily_ranges;
    std::vector<double> hourly_ranges;
    std::vector<double> tick_moves;
    std::vector<double> oscillation_sizes;

    // For oscillation detection
    double local_high = 0, local_low = 999999;
    bool looking_for_high = true;
    double reversal_threshold = 0.5;  // $0.50 reversal to confirm

    Tick tick;
    bool first_tick = true;

    while (manager.GetNextTick(tick)) {
        // Filter by date range
        if (!DateInRange(tick.timestamp, start_date, end_date)) {
            continue;
        }

        double price = (tick.bid + tick.ask) / 2.0;

        if (first_tick) {
            stats.price_start = price;
            prev_price = price;
            first_tick = false;
            local_high = price;
            local_low = price;
        }

        // Track tick-to-tick moves
        double tick_move = std::abs(price - prev_price);
        if (tick_move > 0.001) {  // Ignore tiny moves
            tick_moves.push_back(tick_move);
        }

        // Parse date/time from tick
        int year, month, day, hour, minute, second;
        if (sscanf(tick.timestamp.c_str(), "%d.%d.%d %d:%d:%d",
                   &year, &month, &day, &hour, &minute, &second) >= 4) {

            int current_day = year * 10000 + month * 100 + day;
            int current_hour = current_day * 100 + hour;

            // New day
            if (current_day != prev_day && prev_day != -1) {
                if (day_high > day_low) {
                    daily_ranges.push_back(day_high - day_low);
                }
                day_high = price;
                day_low = price;
            }

            // New hour
            if (current_hour != prev_hour && prev_hour != -1) {
                if (hour_high > hour_low) {
                    hourly_ranges.push_back(hour_high - hour_low);
                }
                hour_high = price;
                hour_low = price;
            }

            day_high = std::max(day_high, price);
            day_low = std::min(day_low, price);
            hour_high = std::max(hour_high, price);
            hour_low = std::min(hour_low, price);

            prev_day = current_day;
            prev_hour = current_hour;
        }

        // Oscillation detection
        if (looking_for_high) {
            if (price > local_high) {
                local_high = price;
            } else if (local_high - price > reversal_threshold) {
                // Found a high, now looking for low
                oscillation_sizes.push_back(local_high - local_low);
                local_low = price;
                looking_for_high = false;
            }
        } else {
            if (price < local_low) {
                local_low = price;
            } else if (price - local_low > reversal_threshold) {
                // Found a low, now looking for high
                oscillation_sizes.push_back(local_high - local_low);
                local_high = price;
                looking_for_high = true;
            }
        }

        stats.price_end = price;
        prev_price = price;
    }

    // Calculate averages
    if (!daily_ranges.empty()) {
        double sum = 0;
        for (double r : daily_ranges) sum += r;
        stats.avg_daily_range = sum / daily_ranges.size();
    }

    if (!hourly_ranges.empty()) {
        double sum = 0;
        for (double r : hourly_ranges) sum += r;
        stats.avg_hourly_range = sum / hourly_ranges.size();
    }

    if (!tick_moves.empty()) {
        double sum = 0;
        for (double m : tick_moves) sum += m;
        stats.avg_tick_move = sum / tick_moves.size();
    }

    if (!oscillation_sizes.empty()) {
        std::sort(oscillation_sizes.begin(), oscillation_sizes.end());
        stats.median_oscillation = oscillation_sizes[oscillation_sizes.size() / 2];
        stats.oscillation_count = oscillation_sizes.size();
    }

    stats.price_change_pct = (stats.price_end - stats.price_start) / stats.price_start * 100.0;

    return stats;
}

int main() {
    std::cout << "=== Spacing Investigation ===" << std::endl;
    std::cout << "Understanding why $0.30 outperforms larger spacing" << std::endl;
    std::cout << std::endl;

    std::string data_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    double initial_balance = 10000.0;

    // Define test periods
    struct Period {
        std::string name;
        std::string start;
        std::string end;
    };

    std::vector<Period> periods = {
        {"Full Year", "2025.01.01", "2025.12.30"},
        {"Q1 2025",   "2025.01.01", "2025.04.01"},
        {"Q2 2025",   "2025.04.01", "2025.07.01"},
        {"Q3 2025",   "2025.07.01", "2025.10.01"},
        {"Q4 2025",   "2025.10.01", "2025.12.30"},
        {"Jan 2025",  "2025.01.01", "2025.02.01"},
        {"Feb 2025",  "2025.02.01", "2025.03.01"},
        {"Mar 2025",  "2025.03.01", "2025.04.01"},
    };

    // Define spacing values to test
    std::vector<double> spacings = {0.20, 0.30, 0.40, 0.50, 1.00, 1.50, 2.00, 3.00, 4.00, 5.00};

    // PART 1: Analyze gold characteristics for each period
    std::cout << "=== Part 1: Gold Price Characteristics ===" << std::endl;
    std::cout << std::setw(12) << "Period"
              << std::setw(10) << "DailyRng"
              << std::setw(10) << "HourlyRng"
              << std::setw(10) << "MedOsc"
              << std::setw(10) << "OscCount"
              << std::setw(10) << "Change%"
              << std::endl;
    std::cout << std::string(62, '-') << std::endl;

    std::map<std::string, GoldStats> period_stats;

    for (const auto& period : periods) {
        auto stats = AnalyzeGoldCharacteristics(data_path, period.start, period.end);
        period_stats[period.name] = stats;

        std::cout << std::setw(12) << period.name
                  << std::setw(10) << std::fixed << std::setprecision(2) << stats.avg_daily_range
                  << std::setw(10) << stats.avg_hourly_range
                  << std::setw(10) << stats.median_oscillation
                  << std::setw(10) << stats.oscillation_count
                  << std::setw(10) << std::setprecision(1) << stats.price_change_pct << "%"
                  << std::endl;
    }

    // PART 2: Test which spacing is optimal for each period
    std::cout << std::endl;
    std::cout << "=== Part 2: Optimal Spacing by Period ===" << std::endl;
    std::cout << "Testing spacing: ";
    for (double s : spacings) std::cout << "$" << s << " ";
    std::cout << std::endl << std::endl;

    // Store results
    std::map<std::string, std::vector<PeriodResult>> all_results;

    for (const auto& period : periods) {
        std::cout << "Testing " << period.name << "..." << std::endl;

        std::vector<PeriodResult> period_results;
        for (double spacing : spacings) {
            auto result = RunSpacingTest(spacing, data_path, period.start, period.end,
                                        period.name, initial_balance);
            period_results.push_back(result);
        }
        all_results[period.name] = period_results;
    }

    // Print results table
    std::cout << std::endl;
    std::cout << "=== Results: Return Multiple by Period and Spacing ===" << std::endl;

    // Header
    std::cout << std::setw(12) << "Period";
    for (double s : spacings) {
        std::cout << std::setw(8) << ("$" + std::to_string(s).substr(0,4));
    }
    std::cout << std::setw(10) << "Best$" << std::setw(10) << "BestRet" << std::endl;
    std::cout << std::string(12 + spacings.size() * 8 + 20, '-') << std::endl;

    for (const auto& period : periods) {
        std::cout << std::setw(12) << period.name;

        double best_return = 0;
        double best_spacing = 0;

        for (const auto& result : all_results[period.name]) {
            if (result.stopped_out) {
                std::cout << std::setw(8) << "STOP";
            } else {
                std::cout << std::setw(7) << std::fixed << std::setprecision(2) << result.return_multiple << "x";
                if (result.return_multiple > best_return) {
                    best_return = result.return_multiple;
                    best_spacing = result.spacing;
                }
            }
        }

        std::cout << std::setw(10) << ("$" + std::to_string(best_spacing).substr(0,4))
                  << std::setw(9) << std::setprecision(2) << best_return << "x"
                  << std::endl;
    }

    // PART 3: Trade count analysis
    std::cout << std::endl;
    std::cout << "=== Part 3: Trade Count by Spacing (Full Year) ===" << std::endl;
    std::cout << std::setw(10) << "Spacing"
              << std::setw(12) << "Trades"
              << std::setw(12) << "Return"
              << std::setw(15) << "Profit/Trade"
              << std::endl;
    std::cout << std::string(49, '-') << std::endl;

    for (const auto& result : all_results["Full Year"]) {
        double profit = (result.return_multiple - 1.0) * initial_balance;
        double profit_per_trade = result.total_trades > 0 ? profit / result.total_trades : 0;

        std::cout << std::setw(10) << ("$" + std::to_string(result.spacing).substr(0,4))
                  << std::setw(12) << result.total_trades
                  << std::setw(11) << std::fixed << std::setprecision(2) << result.return_multiple << "x"
                  << std::setw(15) << std::setprecision(2) << profit_per_trade
                  << std::endl;
    }

    // PART 4: Correlation analysis
    std::cout << std::endl;
    std::cout << "=== Part 4: Optimal Spacing vs Gold Characteristics ===" << std::endl;
    std::cout << std::setw(12) << "Period"
              << std::setw(10) << "BestSpac"
              << std::setw(10) << "DailyRng"
              << std::setw(10) << "Ratio"
              << std::setw(12) << "Direction"
              << std::endl;
    std::cout << std::string(54, '-') << std::endl;

    for (const auto& period : periods) {
        // Find best spacing for this period
        double best_spacing = 0;
        double best_return = 0;
        for (const auto& result : all_results[period.name]) {
            if (!result.stopped_out && result.return_multiple > best_return) {
                best_return = result.return_multiple;
                best_spacing = result.spacing;
            }
        }

        auto& stats = period_stats[period.name];
        double ratio = stats.avg_daily_range > 0 ? best_spacing / stats.avg_daily_range : 0;
        std::string direction = stats.price_change_pct > 5 ? "BULL" :
                               (stats.price_change_pct < -5 ? "BEAR" : "FLAT");

        std::cout << std::setw(12) << period.name
                  << std::setw(10) << ("$" + std::to_string(best_spacing).substr(0,4))
                  << std::setw(10) << std::fixed << std::setprecision(2) << stats.avg_daily_range
                  << std::setw(10) << std::setprecision(3) << ratio
                  << std::setw(12) << direction
                  << std::endl;
    }

    std::cout << std::endl;
    std::cout << "=== Key Insights ===" << std::endl;
    std::cout << "1. If optimal spacing is consistent across periods: $0.30 may be fundamentally optimal" << std::endl;
    std::cout << "2. If optimal spacing varies: Need dynamic spacing based on volatility" << std::endl;
    std::cout << "3. Ratio of best_spacing/daily_range reveals the relationship" << std::endl;

    return 0;
}
