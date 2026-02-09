/**
 * Frequency Domain Analysis for FillUpOscillation Strategy
 *
 * Purpose: Analyze dominant oscillation frequencies in XAUUSD price data
 * to inform adaptive grid spacing based on oscillation period.
 *
 * Measures:
 * 1. Swing durations (time from local min to local max and vice versa)
 * 2. Swing amplitudes (price change during each swing)
 * 3. Dominant periods at different time scales (hourly, 4-hourly, daily)
 * 4. Relationship between amplitude and period
 */

#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <algorithm>
#include <map>
#include <numeric>

using namespace backtest;

// Structure to track a single swing (up or down movement)
struct Swing {
    std::string start_time;
    std::string end_time;
    double start_price;
    double end_price;
    double amplitude;      // Absolute price change
    double duration_secs;  // Duration in seconds
    bool is_upswing;       // true = up, false = down

    // Amplitude per unit time
    double velocity() const {
        return duration_secs > 0 ? amplitude / duration_secs : 0.0;
    }
};

// Parse timestamp to seconds since epoch (simplified)
// Format: "2025.01.02 19:00:00.123"
double timestamp_to_seconds(const std::string& ts) {
    if (ts.length() < 19) return 0.0;

    int year = std::stoi(ts.substr(0, 4));
    int month = std::stoi(ts.substr(5, 2));
    int day = std::stoi(ts.substr(8, 2));
    int hour = std::stoi(ts.substr(11, 2));
    int minute = std::stoi(ts.substr(14, 2));
    int second = std::stoi(ts.substr(17, 2));
    int ms = 0;
    if (ts.length() > 20) {
        ms = std::stoi(ts.substr(20, 3));
    }

    // Simplified: days since 2024.01.01
    int total_days = (year - 2024) * 365 + (month - 1) * 30 + day;
    double total_secs = total_days * 86400.0 + hour * 3600.0 + minute * 60.0 + second + ms / 1000.0;
    return total_secs;
}

// Extract hour of day from timestamp
int get_hour(const std::string& ts) {
    if (ts.length() < 13) return 0;
    return std::stoi(ts.substr(11, 2));
}

// Extract day of week (0=Sunday) from timestamp - simplified
int get_day_of_week(const std::string& ts) {
    if (ts.length() < 10) return 0;
    int year = std::stoi(ts.substr(0, 4));
    int month = std::stoi(ts.substr(5, 2));
    int day = std::stoi(ts.substr(8, 2));

    // Zeller's formula (simplified)
    if (month < 3) {
        month += 12;
        year--;
    }
    int k = year % 100;
    int j = year / 100;
    int h = (day + (13 * (month + 1)) / 5 + k + k / 4 + j / 4 - 2 * j) % 7;
    return ((h + 6) % 7);  // Convert to 0=Sunday
}

// Detect swing reversals using a zigzag-like algorithm
class SwingDetector {
public:
    SwingDetector(double min_swing_pct)
        : min_swing_pct_(min_swing_pct),
          in_upswing_(true),
          swing_start_price_(0.0),
          swing_extreme_price_(0.0),
          swing_extreme_time_("") {}

    // Process a tick and return true if a swing completed
    bool process(const Tick& tick, Swing& completed_swing) {
        double price = tick.mid();

        // Initialize on first tick
        if (swing_start_price_ == 0.0) {
            swing_start_price_ = price;
            swing_extreme_price_ = price;
            swing_start_time_ = tick.timestamp;
            swing_extreme_time_ = tick.timestamp;
            return false;
        }

        double threshold = swing_extreme_price_ * min_swing_pct_ / 100.0;

        if (in_upswing_) {
            if (price > swing_extreme_price_) {
                // Continue upswing
                swing_extreme_price_ = price;
                swing_extreme_time_ = tick.timestamp;
            } else if (price < swing_extreme_price_ - threshold) {
                // Reversal detected - upswing completed
                completed_swing.start_time = swing_start_time_;
                completed_swing.end_time = swing_extreme_time_;
                completed_swing.start_price = swing_start_price_;
                completed_swing.end_price = swing_extreme_price_;
                completed_swing.amplitude = swing_extreme_price_ - swing_start_price_;
                completed_swing.duration_secs = timestamp_to_seconds(swing_extreme_time_)
                                               - timestamp_to_seconds(swing_start_time_);
                completed_swing.is_upswing = true;

                // Start new downswing
                in_upswing_ = false;
                swing_start_price_ = swing_extreme_price_;
                swing_start_time_ = swing_extreme_time_;
                swing_extreme_price_ = price;
                swing_extreme_time_ = tick.timestamp;

                return true;
            }
        } else {
            if (price < swing_extreme_price_) {
                // Continue downswing
                swing_extreme_price_ = price;
                swing_extreme_time_ = tick.timestamp;
            } else if (price > swing_extreme_price_ + threshold) {
                // Reversal detected - downswing completed
                completed_swing.start_time = swing_start_time_;
                completed_swing.end_time = swing_extreme_time_;
                completed_swing.start_price = swing_start_price_;
                completed_swing.end_price = swing_extreme_price_;
                completed_swing.amplitude = swing_start_price_ - swing_extreme_price_;
                completed_swing.duration_secs = timestamp_to_seconds(swing_extreme_time_)
                                               - timestamp_to_seconds(swing_start_time_);
                completed_swing.is_upswing = false;

                // Start new upswing
                in_upswing_ = true;
                swing_start_price_ = swing_extreme_price_;
                swing_start_time_ = swing_extreme_time_;
                swing_extreme_price_ = price;
                swing_extreme_time_ = tick.timestamp;

                return true;
            }
        }

        return false;
    }

private:
    double min_swing_pct_;
    bool in_upswing_;
    double swing_start_price_;
    double swing_extreme_price_;
    std::string swing_start_time_;
    std::string swing_extreme_time_;
};

// Aggregate statistics for a set of swings
struct SwingStats {
    int count = 0;
    double avg_amplitude = 0.0;
    double median_amplitude = 0.0;
    double avg_duration_mins = 0.0;
    double median_duration_mins = 0.0;
    double avg_velocity = 0.0;

    // Percentiles
    double p25_duration = 0.0;
    double p75_duration = 0.0;
    double p25_amplitude = 0.0;
    double p75_amplitude = 0.0;
};

SwingStats calculate_stats(std::vector<Swing>& swings) {
    SwingStats stats;
    if (swings.empty()) return stats;

    stats.count = swings.size();

    // Calculate averages
    double sum_amp = 0.0, sum_dur = 0.0, sum_vel = 0.0;
    for (const auto& s : swings) {
        sum_amp += s.amplitude;
        sum_dur += s.duration_secs;
        sum_vel += s.velocity();
    }
    stats.avg_amplitude = sum_amp / swings.size();
    stats.avg_duration_mins = (sum_dur / swings.size()) / 60.0;
    stats.avg_velocity = sum_vel / swings.size();

    // Sort for percentiles
    std::vector<double> amplitudes, durations;
    for (const auto& s : swings) {
        amplitudes.push_back(s.amplitude);
        durations.push_back(s.duration_secs / 60.0);
    }
    std::sort(amplitudes.begin(), amplitudes.end());
    std::sort(durations.begin(), durations.end());

    int n = swings.size();
    stats.median_amplitude = amplitudes[n / 2];
    stats.median_duration_mins = durations[n / 2];
    stats.p25_amplitude = amplitudes[n / 4];
    stats.p75_amplitude = amplitudes[3 * n / 4];
    stats.p25_duration = durations[n / 4];
    stats.p75_duration = durations[3 * n / 4];

    return stats;
}

// Analyze hourly patterns
struct HourlyStats {
    int hour;
    int swing_count;
    double avg_amplitude;
    double avg_duration_mins;
};

int main() {
    std::cout << "=== XAUUSD Oscillation Frequency Analysis ===" << std::endl;
    std::cout << "Analyzing tick data to determine optimal grid spacing parameters\n" << std::endl;

    // Configure tick data
    TickDataConfig config;
#ifdef _WIN32
    config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
#else
    config.file_path = "/path/to/XAUUSD_TICKS_2025.csv";
#endif
    config.load_all_into_memory = false;  // Streaming mode for 51M ticks

    try {
        TickDataManager manager(config);

        // Multiple swing detectors for different thresholds
        // Threshold determines the minimum swing size to detect
        SwingDetector detector_small(0.05);   // 0.05% = ~$1.30 at $2600, ~$2.15 at $4300
        SwingDetector detector_medium(0.15);  // 0.15% = ~$4.00 at $2600, ~$6.50 at $4300
        SwingDetector detector_large(0.50);   // 0.50% = ~$13 at $2600, ~$21.50 at $4300

        std::vector<Swing> swings_small, swings_medium, swings_large;

        // Track hourly swing completions
        std::map<int, std::vector<double>> hourly_amplitudes;
        std::map<int, std::vector<double>> hourly_durations;

        // Track price for range calculations
        double day_open = 0.0;
        double day_high = 0.0;
        double day_low = 1e9;
        std::string current_day = "";
        std::vector<double> daily_ranges;

        // 4-hour range tracking
        double four_hour_high = 0.0;
        double four_hour_low = 1e9;
        int last_four_hour_block = -1;
        std::vector<double> four_hour_ranges;

        // Hourly range tracking
        double hour_high = 0.0;
        double hour_low = 1e9;
        int last_hour = -1;
        std::vector<double> hourly_ranges;

        Tick tick;
        long tick_count = 0;
        long report_interval = 5000000;  // Report every 5M ticks

        std::cout << "Processing ticks..." << std::endl;

        while (manager.GetNextTick(tick)) {
            tick_count++;

            if (tick_count % report_interval == 0) {
                std::cout << "  Processed " << (tick_count / 1000000) << "M ticks..." << std::endl;
            }

            double price = tick.mid();
            int hour = get_hour(tick.timestamp);
            std::string day = tick.timestamp.substr(0, 10);
            int four_hour_block = hour / 4;

            // Daily range tracking
            if (day != current_day) {
                if (!current_day.empty() && day_high > 0) {
                    daily_ranges.push_back(day_high - day_low);
                }
                current_day = day;
                day_open = price;
                day_high = price;
                day_low = price;
            }
            day_high = std::max(day_high, price);
            day_low = std::min(day_low, price);

            // 4-hour range tracking
            if (four_hour_block != last_four_hour_block || day != current_day) {
                if (last_four_hour_block >= 0 && four_hour_high > 0) {
                    four_hour_ranges.push_back(four_hour_high - four_hour_low);
                }
                last_four_hour_block = four_hour_block;
                four_hour_high = price;
                four_hour_low = price;
            }
            four_hour_high = std::max(four_hour_high, price);
            four_hour_low = std::min(four_hour_low, price);

            // Hourly range tracking
            if (hour != last_hour) {
                if (last_hour >= 0 && hour_high > 0) {
                    hourly_ranges.push_back(hour_high - hour_low);
                }
                last_hour = hour;
                hour_high = price;
                hour_low = price;
            }
            hour_high = std::max(hour_high, price);
            hour_low = std::min(hour_low, price);

            // Swing detection
            Swing swing;

            if (detector_small.process(tick, swing)) {
                swings_small.push_back(swing);
                int swing_hour = get_hour(swing.start_time);
                hourly_amplitudes[swing_hour].push_back(swing.amplitude);
                hourly_durations[swing_hour].push_back(swing.duration_secs / 60.0);
            }

            if (detector_medium.process(tick, swing)) {
                swings_medium.push_back(swing);
            }

            if (detector_large.process(tick, swing)) {
                swings_large.push_back(swing);
            }
        }

        std::cout << "\n=== RESULTS ===" << std::endl;
        std::cout << "Total ticks processed: " << tick_count << std::endl;

        // Range statistics
        std::cout << "\n--- Range Statistics ---" << std::endl;

        auto print_range_stats = [](const std::vector<double>& ranges, const std::string& name) {
            if (ranges.empty()) return;
            double sum = std::accumulate(ranges.begin(), ranges.end(), 0.0);
            double avg = sum / ranges.size();
            std::vector<double> sorted = ranges;
            std::sort(sorted.begin(), sorted.end());
            double median = sorted[sorted.size() / 2];
            double p25 = sorted[sorted.size() / 4];
            double p75 = sorted[3 * sorted.size() / 4];
            double min_val = sorted.front();
            double max_val = sorted.back();

            std::cout << name << " (" << ranges.size() << " samples):" << std::endl;
            std::cout << "  Average: $" << std::fixed << std::setprecision(2) << avg << std::endl;
            std::cout << "  Median:  $" << median << std::endl;
            std::cout << "  P25-P75: $" << p25 << " - $" << p75 << std::endl;
            std::cout << "  Min-Max: $" << min_val << " - $" << max_val << std::endl;
        };

        print_range_stats(hourly_ranges, "Hourly Range");
        std::cout << std::endl;
        print_range_stats(four_hour_ranges, "4-Hour Range");
        std::cout << std::endl;
        print_range_stats(daily_ranges, "Daily Range");

        // Swing statistics by threshold
        std::cout << "\n--- Swing Detection Results ---" << std::endl;

        auto print_swing_stats = [](std::vector<Swing>& swings, const std::string& name, double threshold) {
            if (swings.empty()) {
                std::cout << name << ": No swings detected" << std::endl;
                return;
            }

            // Separate upswings and downswings
            std::vector<Swing> upswings, downswings;
            for (const auto& s : swings) {
                if (s.is_upswing) upswings.push_back(s);
                else downswings.push_back(s);
            }

            SwingStats all_stats = calculate_stats(swings);
            SwingStats up_stats = calculate_stats(upswings);
            SwingStats down_stats = calculate_stats(downswings);

            std::cout << "\n" << name << " (threshold " << threshold << "%):" << std::endl;
            std::cout << "  Total swings: " << swings.size() << std::endl;
            std::cout << "    Upswings:   " << upswings.size() << std::endl;
            std::cout << "    Downswings: " << downswings.size() << std::endl;

            std::cout << "\n  ALL SWINGS:" << std::endl;
            std::cout << "    Avg amplitude:  $" << std::fixed << std::setprecision(2) << all_stats.avg_amplitude << std::endl;
            std::cout << "    Median amplitude: $" << all_stats.median_amplitude << std::endl;
            std::cout << "    P25-P75 amplitude: $" << all_stats.p25_amplitude << " - $" << all_stats.p75_amplitude << std::endl;
            std::cout << "    Avg duration:  " << std::fixed << std::setprecision(1) << all_stats.avg_duration_mins << " min" << std::endl;
            std::cout << "    Median duration: " << all_stats.median_duration_mins << " min" << std::endl;
            std::cout << "    P25-P75 duration: " << all_stats.p25_duration << " - " << all_stats.p75_duration << " min" << std::endl;

            if (!upswings.empty()) {
                std::cout << "\n  UPSWINGS:" << std::endl;
                std::cout << "    Avg amplitude: $" << up_stats.avg_amplitude << std::endl;
                std::cout << "    Avg duration:  " << up_stats.avg_duration_mins << " min" << std::endl;
            }

            if (!downswings.empty()) {
                std::cout << "\n  DOWNSWINGS:" << std::endl;
                std::cout << "    Avg amplitude: $" << down_stats.avg_amplitude << std::endl;
                std::cout << "    Avg duration:  " << down_stats.avg_duration_mins << " min" << std::endl;
            }

            // Calculate implied oscillation period (full cycle = up + down)
            double avg_cycle_duration = (up_stats.avg_duration_mins + down_stats.avg_duration_mins);
            double avg_cycle_amplitude = (up_stats.avg_amplitude + down_stats.avg_amplitude) / 2.0;
            std::cout << "\n  CYCLE CHARACTERISTICS:" << std::endl;
            std::cout << "    Avg full cycle: " << avg_cycle_duration << " min" << std::endl;
            std::cout << "    Avg half-cycle amplitude: $" << avg_cycle_amplitude << std::endl;
            std::cout << "    Implied optimal spacing: $" << (avg_cycle_amplitude * 0.5) << " (50% of amplitude)" << std::endl;
        };

        print_swing_stats(swings_small, "Small Swings", 0.05);
        print_swing_stats(swings_medium, "Medium Swings", 0.15);
        print_swing_stats(swings_large, "Large Swings", 0.50);

        // Hourly pattern analysis
        std::cout << "\n--- Hourly Pattern Analysis (Small Swings) ---" << std::endl;
        std::cout << "Hour | Swings | Avg Amp | Avg Dur (min)" << std::endl;
        std::cout << "-----|--------|---------|---------------" << std::endl;

        for (int h = 0; h < 24; h++) {
            if (hourly_amplitudes.count(h) == 0) continue;

            auto& amps = hourly_amplitudes[h];
            auto& durs = hourly_durations[h];

            double avg_amp = std::accumulate(amps.begin(), amps.end(), 0.0) / amps.size();
            double avg_dur = std::accumulate(durs.begin(), durs.end(), 0.0) / durs.size();

            std::cout << std::setw(4) << h << " | "
                      << std::setw(6) << amps.size() << " | "
                      << std::setw(7) << std::fixed << std::setprecision(2) << avg_amp << " | "
                      << std::setw(14) << std::setprecision(1) << avg_dur << std::endl;
        }

        // Recommendations
        std::cout << "\n=== RECOMMENDATIONS FOR ADAPTIVE SPACING ===" << std::endl;

        if (!swings_medium.empty()) {
            SwingStats stats = calculate_stats(swings_medium);

            std::cout << "\nBased on medium swings (0.15% threshold):" << std::endl;
            std::cout << "  1. Base spacing should be ~$" << (stats.median_amplitude * 0.4) << " - $" << (stats.median_amplitude * 0.6) << std::endl;
            std::cout << "     (40-60% of median amplitude $" << stats.median_amplitude << ")" << std::endl;

            std::cout << "\n  2. Expected cycle duration: ~" << (stats.median_duration_mins * 2) << " minutes" << std::endl;
            std::cout << "     Lookback period should be at least this for volatility calculation" << std::endl;

            std::cout << "\n  3. Spacing formula suggestion:" << std::endl;
            std::cout << "     spacing = (4h_range * 0.3) to (4h_range * 0.5)" << std::endl;

            if (!four_hour_ranges.empty()) {
                std::vector<double> sorted = four_hour_ranges;
                std::sort(sorted.begin(), sorted.end());
                double median_4h = sorted[sorted.size() / 2];
                std::cout << "     With median 4h range of $" << median_4h << std::endl;
                std::cout << "     Suggested spacing: $" << (median_4h * 0.3) << " - $" << (median_4h * 0.5) << std::endl;
            }
        }

        // Period-adaptive spacing formula
        std::cout << "\n  4. Period-adaptive spacing formula:" << std::endl;
        std::cout << "     spacing = amplitude / (period_mins / min_trade_duration_mins)" << std::endl;
        std::cout << "     Where:" << std::endl;
        std::cout << "       amplitude = recent range (1h or 4h)" << std::endl;
        std::cout << "       period_mins = typical oscillation period" << std::endl;
        std::cout << "       min_trade_duration_mins = minimum time to hold a position (~5-10 min for gold)" << std::endl;

        if (!swings_medium.empty() && !four_hour_ranges.empty()) {
            SwingStats stats = calculate_stats(swings_medium);
            std::vector<double> sorted = four_hour_ranges;
            std::sort(sorted.begin(), sorted.end());
            double median_4h = sorted[sorted.size() / 2];
            double period = stats.median_duration_mins * 2;
            double min_hold = 10.0;  // 10 minutes

            double calculated_spacing = median_4h / (period / min_hold);
            std::cout << "\n     Example: $" << median_4h << " / (" << period << " / " << min_hold << ") = $" << calculated_spacing << std::endl;
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
