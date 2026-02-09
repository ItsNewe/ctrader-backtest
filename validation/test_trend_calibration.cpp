/**
 * Trend Calibration Test
 *
 * Questions:
 * 1. What are typical 3-day trend values?
 * 2. Are our thresholds (3%, 6%, 10%) appropriate for 3-day lookback?
 * 3. What spacing values are actually being used?
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <map>
#include <cmath>

using namespace backtest;

int main() {
    std::cout << "=== Trend Calibration Analysis ===" << std::endl;
    std::cout << std::endl;

    std::string data_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";

    // Analyze what 3-day trends actually look like
    std::cout << "Step 1: Measuring actual 3-day trend distribution..." << std::endl;

    TickDataConfig tick_config;
    tick_config.file_path = data_path;
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickDataManager manager(tick_config);

    // Track prices using timestamps, not tick counts
    // Sample every 3 days based on date parsing

    std::vector<double> trend_values;
    std::map<std::string, int> trend_buckets;

    double period_start_price = 0;
    double current_price = 0;
    int last_period_day = -1;
    int period_count = 0;

    Tick tick;
    while (manager.GetNextTick(tick)) {
        current_price = (tick.bid + tick.ask) / 2.0;

        // Parse date from timestamp (format: YYYY.MM.DD HH:MM:SS)
        int month, day;
        if (sscanf(tick.timestamp.c_str(), "%*d.%d.%d", &month, &day) >= 2) {
            // Calculate day of year (approximate)
            int day_of_year = (month - 1) * 30 + day;

            // Every 3 days, calculate trend
            int period_id = day_of_year / 3;

            if (period_id != last_period_day) {
                if (period_start_price > 0) {
                    double trend_pct = (current_price - period_start_price) / period_start_price * 100.0;
                    trend_values.push_back(trend_pct);

                    // Bucket the trend
                    double abs_trend = std::abs(trend_pct);
                    std::string bucket;
                    if (abs_trend < 0.5) bucket = "<0.5%";
                    else if (abs_trend < 1.0) bucket = "0.5-1%";
                    else if (abs_trend < 2.0) bucket = "1-2%";
                    else if (abs_trend < 3.0) bucket = "2-3%";
                    else if (abs_trend < 5.0) bucket = "3-5%";
                    else if (abs_trend < 10.0) bucket = "5-10%";
                    else bucket = ">10%";

                    trend_buckets[bucket]++;
                    period_count++;
                }

                period_start_price = current_price;
                last_period_day = period_id;
            }
        }
    }

    std::cout << std::endl;
    std::cout << "3-Day Trend Distribution (over 2025):" << std::endl;
    std::cout << std::string(40, '-') << std::endl;

    std::vector<std::string> bucket_order = {"<0.5%", "0.5-1%", "1-2%", "2-3%", "3-5%", "5-10%", ">10%"};
    int total_periods = trend_values.size();

    for (const auto& bucket : bucket_order) {
        int count = trend_buckets[bucket];
        double pct = (double)count / total_periods * 100.0;
        std::cout << std::setw(10) << bucket << ": "
                  << std::setw(5) << count << " periods ("
                  << std::fixed << std::setprecision(1) << pct << "%)" << std::endl;
    }

    // Calculate statistics
    double sum = 0, sum_sq = 0;
    double min_trend = 999, max_trend = -999;
    for (double t : trend_values) {
        sum += std::abs(t);
        sum_sq += t * t;
        min_trend = std::min(min_trend, t);
        max_trend = std::max(max_trend, t);
    }
    double avg_abs_trend = sum / trend_values.size();
    double std_dev = std::sqrt(sum_sq / trend_values.size());

    std::cout << std::endl;
    std::cout << "Statistics:" << std::endl;
    std::cout << "  Total 3-day periods: " << total_periods << std::endl;
    std::cout << "  Average |trend|: " << std::fixed << std::setprecision(2) << avg_abs_trend << "%" << std::endl;
    std::cout << "  Std dev: " << std_dev << "%" << std::endl;
    std::cout << "  Range: " << min_trend << "% to " << max_trend << "%" << std::endl;

    // Current thresholds analysis
    std::cout << std::endl;
    std::cout << "=== Current Threshold Analysis ===" << std::endl;
    std::cout << std::endl;
    std::cout << "Current thresholds (designed for quarterly trends):" << std::endl;
    std::cout << "  >10%: tight spacing ($0.20-$0.50)" << std::endl;
    std::cout << "  6-10%: moderate ($0.50-$1.50)" << std::endl;
    std::cout << "  3-6%: wide ($1.50-$5.00)" << std::endl;
    std::cout << "  <3%: widest ($5.00)" << std::endl;

    // Count what spacing would be used
    int tight = 0, moderate = 0, wide = 0, widest = 0;
    for (double t : trend_values) {
        double abs_t = std::abs(t);
        if (abs_t >= 10.0) tight++;
        else if (abs_t >= 6.0) moderate++;
        else if (abs_t >= 3.0) wide++;
        else widest++;
    }

    std::cout << std::endl;
    std::cout << "With current thresholds, 3-day periods would get:" << std::endl;
    std::cout << "  Tight ($0.20-$0.50): " << tight << " periods ("
              << std::fixed << std::setprecision(1) << (double)tight/total_periods*100 << "%)" << std::endl;
    std::cout << "  Moderate ($0.50-$1.50): " << moderate << " periods ("
              << (double)moderate/total_periods*100 << "%)" << std::endl;
    std::cout << "  Wide ($1.50-$5.00): " << wide << " periods ("
              << (double)wide/total_periods*100 << "%)" << std::endl;
    std::cout << "  Widest ($5.00): " << widest << " periods ("
              << (double)widest/total_periods*100 << "%)" << std::endl;

    // Suggested thresholds for 3-day lookback
    std::cout << std::endl;
    std::cout << "=== Suggested Thresholds for 3-Day Lookback ===" << std::endl;
    std::cout << std::endl;

    // Use percentiles to set thresholds
    std::vector<double> abs_trends;
    for (double t : trend_values) abs_trends.push_back(std::abs(t));
    std::sort(abs_trends.begin(), abs_trends.end());

    double p25 = abs_trends[abs_trends.size() * 25 / 100];
    double p50 = abs_trends[abs_trends.size() * 50 / 100];
    double p75 = abs_trends[abs_trends.size() * 75 / 100];
    double p90 = abs_trends[abs_trends.size() * 90 / 100];

    std::cout << "Trend percentiles:" << std::endl;
    std::cout << "  25th percentile: " << std::fixed << std::setprecision(2) << p25 << "%" << std::endl;
    std::cout << "  50th percentile: " << p50 << "%" << std::endl;
    std::cout << "  75th percentile: " << p75 << "%" << std::endl;
    std::cout << "  90th percentile: " << p90 << "%" << std::endl;

    std::cout << std::endl;
    std::cout << "Suggested thresholds (based on percentiles):" << std::endl;
    std::cout << "  >" << p75 << "%: STRONG trend -> tight spacing" << std::endl;
    std::cout << "  " << p50 << "-" << p75 << "%: MODERATE trend -> medium spacing" << std::endl;
    std::cout << "  " << p25 << "-" << p50 << "%: WEAK trend -> wide spacing" << std::endl;
    std::cout << "  <" << p25 << "%: FLAT -> widest spacing" << std::endl;

    return 0;
}
