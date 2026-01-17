/**
 * Typical Volatility Analysis
 *
 * Measures the actual 4-hour price range distribution from tick data
 * to establish the correct "typical volatility" parameter.
 *
 * Uses actual timestamps, not tick count approximations.
 */

#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace backtest;

void AnalyzeFile(const std::string& name, const std::string& path) {
    std::cout << "\n=== " << name << " ===" << std::endl;

    TickDataConfig config;
    config.file_path = path;
    config.format = TickDataFormat::MT5_CSV;
    config.load_all_into_memory = false;

    TickDataManager manager(config);

    std::vector<double> hourly_ranges;
    std::vector<double> four_hour_ranges;

    double hour_high = 0, hour_low = 1e9;
    double four_hour_high = 0, four_hour_low = 1e9;

    long tick_count = 0;
    long last_hour = -1;
    long last_four_hour = -1;

    Tick tick;
    while (manager.GetNextTick(tick)) {
        tick_count++;

        // Parse timestamp: "2024.01.02 19:00:00.123" -> extract day and hour
        // Format: YYYY.MM.DD HH:MM:SS.mmm
        int year, month, day, hour, minute;
        if (sscanf(tick.timestamp.c_str(), "%d.%d.%d %d:%d",
                   &year, &month, &day, &hour, &minute) >= 4) {
            // Create a unique hour identifier: day * 24 + hour
            long day_num = year * 10000 + month * 100 + day;
            long current_hour = day_num * 24 + hour;
            long current_four_hour = day_num * 6 + (hour / 4);  // 6 four-hour blocks per day

            // Track hourly
            hour_high = std::max(hour_high, tick.bid);
            hour_low = std::min(hour_low, tick.bid);

            // Track 4-hour
            four_hour_high = std::max(four_hour_high, tick.bid);
            four_hour_low = std::min(four_hour_low, tick.bid);

            // End of hour (new hour started)
            if (last_hour != -1 && current_hour != last_hour) {
                if (hour_high > 0 && hour_low < 1e9 && hour_low > 0) {
                    hourly_ranges.push_back(hour_high - hour_low);
                }
                hour_high = tick.bid;
                hour_low = tick.bid;
            }
            last_hour = current_hour;

            // End of 4-hour period
            if (last_four_hour != -1 && current_four_hour != last_four_hour) {
                if (four_hour_high > 0 && four_hour_low < 1e9 && four_hour_low > 0) {
                    four_hour_ranges.push_back(four_hour_high - four_hour_low);
                }
                four_hour_high = tick.bid;
                four_hour_low = tick.bid;
            }
            last_four_hour = current_four_hour;
        }  // end if sscanf

        // Progress
        if (tick_count % 10000000 == 0) {
            std::cout << "Processed " << (tick_count / 1000000) << "M ticks..." << std::endl;
        }
    }

    // Calculate statistics for hourly ranges
    if (!hourly_ranges.empty()) {
        std::sort(hourly_ranges.begin(), hourly_ranges.end());

        double sum = 0;
        for (double r : hourly_ranges) sum += r;
        double mean = sum / hourly_ranges.size();

        double median = hourly_ranges[hourly_ranges.size() / 2];
        double p10 = hourly_ranges[hourly_ranges.size() / 10];
        double p90 = hourly_ranges[hourly_ranges.size() * 9 / 10];
        double min_r = hourly_ranges.front();
        double max_r = hourly_ranges.back();

        std::cout << "\n1-Hour Range Statistics:" << std::endl;
        std::cout << "  Count:   " << hourly_ranges.size() << " periods" << std::endl;
        std::cout << "  Mean:    $" << std::fixed << std::setprecision(2) << mean << std::endl;
        std::cout << "  Median:  $" << median << std::endl;
        std::cout << "  P10:     $" << p10 << std::endl;
        std::cout << "  P90:     $" << p90 << std::endl;
        std::cout << "  Min:     $" << min_r << std::endl;
        std::cout << "  Max:     $" << max_r << std::endl;
    }

    // Calculate statistics for 4-hour ranges
    if (!four_hour_ranges.empty()) {
        std::sort(four_hour_ranges.begin(), four_hour_ranges.end());

        double sum = 0;
        for (double r : four_hour_ranges) sum += r;
        double mean = sum / four_hour_ranges.size();

        double median = four_hour_ranges[four_hour_ranges.size() / 2];
        double p10 = four_hour_ranges[four_hour_ranges.size() / 10];
        double p90 = four_hour_ranges[four_hour_ranges.size() * 9 / 10];
        double min_r = four_hour_ranges.front();
        double max_r = four_hour_ranges.back();

        std::cout << "\n4-Hour Range Statistics (used for adaptive spacing):" << std::endl;
        std::cout << "  Count:   " << four_hour_ranges.size() << " periods" << std::endl;
        std::cout << "  Mean:    $" << std::fixed << std::setprecision(2) << mean << std::endl;
        std::cout << "  Median:  $" << median << " <-- RECOMMENDED TYPICAL_VOLATILITY" << std::endl;
        std::cout << "  P10:     $" << p10 << std::endl;
        std::cout << "  P90:     $" << p90 << std::endl;
        std::cout << "  Min:     $" << min_r << std::endl;
        std::cout << "  Max:     $" << max_r << std::endl;
    }
}

int main() {
    std::cout << std::string(70, '=') << std::endl;
    std::cout << "TYPICAL VOLATILITY ANALYSIS" << std::endl;
    std::cout << "Establishing correct value for adaptive spacing normalization" << std::endl;
    std::cout << std::string(70, '=') << std::endl;

    // Analyze 2024 data
    AnalyzeFile("XAUUSD 2024",
        "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAUUSD_TICKS_2024.csv");

    // Analyze 2025 data
    AnalyzeFile("XAUUSD 2025",
        "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv");

    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "RECOMMENDATION" << std::endl;
    std::cout << std::string(70, '=') << std::endl;
    std::cout << "Use the MEDIAN 4-hour range as TypicalVolatility." << std::endl;
    std::cout << "This normalizes spacing so that:" << std::endl;
    std::cout << "  - In normal conditions: spacing = base_spacing" << std::endl;
    std::cout << "  - In low volatility: spacing = base_spacing * 0.5" << std::endl;
    std::cout << "  - In high volatility: spacing = base_spacing * 2-3x" << std::endl;
    std::cout << "\nNote: 2024 and 2025 may have different typical values due to" << std::endl;
    std::cout << "different price levels ($2300 vs $3500) and market conditions." << std::endl;

    return 0;
}
