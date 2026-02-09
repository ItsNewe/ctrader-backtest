/**
 * Domain 3: Time-Based Patterns
 *
 * Analyze oscillation patterns by:
 * - Hour of day
 * - Day of week
 * - Trading session (Asian/London/NY)
 */

#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <map>
#include <cmath>
#include <cfloat>

using namespace backtest;

struct TimeSlotStats {
    long tick_count = 0;
    int oscillation_count = 0;
    double total_amplitude = 0;
    double total_spread = 0;
    double high = 0;
    double low = DBL_MAX;
};

class TimePatternAnalyzer {
public:
    void Analyze(const std::string& file_path) {
        std::cout << "\n" << std::string(70, '=') << std::endl;
        std::cout << "TIME-BASED PATTERN ANALYSIS" << std::endl;
        std::cout << std::string(70, '=') << std::endl;

        TickDataConfig config;
        config.file_path = file_path;
        config.format = TickDataFormat::MT5_CSV;
        config.load_all_into_memory = false;

        TickDataManager manager(config);
        // Manager initializes automatically in constructor

        // Stats by hour (0-23 UTC)
        std::map<int, TimeSlotStats> hourly_stats;

        // Stats by day of week (0=Sunday, 6=Saturday)
        std::map<int, TimeSlotStats> daily_stats;

        // Stats by session
        TimeSlotStats asian_stats, london_stats, ny_stats, overlap_stats;

        // Oscillation detection
        double threshold = 1.0;
        double local_high = 0, local_low = DBL_MAX;
        double last_extreme = 0;
        bool looking_for_peak = true;
        int current_hour = -1;
        int current_dow = -1;  // day of week

        long tick_count = 0;
        Tick tick;

        while (manager.GetNextTick(tick)) {
            tick_count++;
            double mid = (tick.bid + tick.ask) / 2.0;
            double spread = tick.ask - tick.bid;

            // Parse timestamp "2025.01.02 03:04:05.678"
            int hour = 0, day = 0, month = 0, year = 0;
            if (tick.timestamp.length() >= 13) {
                year = std::stoi(tick.timestamp.substr(0, 4));
                month = std::stoi(tick.timestamp.substr(5, 2));
                day = std::stoi(tick.timestamp.substr(8, 2));
                hour = std::stoi(tick.timestamp.substr(11, 2));
            }

            // Calculate day of week (Zeller's formula simplified)
            // This is approximate - good enough for pattern analysis
            int dow = CalculateDayOfWeek(year, month, day);

            // Update hourly stats
            hourly_stats[hour].tick_count++;
            hourly_stats[hour].total_spread += spread;
            hourly_stats[hour].high = std::max(hourly_stats[hour].high, mid);
            hourly_stats[hour].low = std::min(hourly_stats[hour].low, mid);

            // Update daily stats
            daily_stats[dow].tick_count++;
            daily_stats[dow].total_spread += spread;

            // Session classification (UTC)
            // Asian: 00:00 - 08:00, London: 08:00 - 16:00, NY: 13:00 - 21:00
            // Overlap: London/NY 13:00 - 16:00
            TimeSlotStats* session_ptr = nullptr;
            if (hour >= 13 && hour < 16) {
                session_ptr = &overlap_stats;
            } else if (hour >= 0 && hour < 8) {
                session_ptr = &asian_stats;
            } else if (hour >= 8 && hour < 16) {
                session_ptr = &london_stats;
            } else {
                session_ptr = &ny_stats;
            }

            session_ptr->tick_count++;
            session_ptr->total_spread += spread;
            session_ptr->high = std::max(session_ptr->high, mid);
            session_ptr->low = std::min(session_ptr->low, mid);

            // Oscillation detection
            if (looking_for_peak) {
                if (mid > local_high) local_high = mid;
                if (mid < local_high - threshold) {
                    if (last_extreme > 0) {
                        double amp = local_high - last_extreme;
                        if (amp >= threshold) {
                            hourly_stats[hour].oscillation_count++;
                            hourly_stats[hour].total_amplitude += amp;
                            daily_stats[dow].oscillation_count++;
                            daily_stats[dow].total_amplitude += amp;
                            session_ptr->oscillation_count++;
                            session_ptr->total_amplitude += amp;
                        }
                    }
                    last_extreme = local_high;
                    local_low = mid;
                    looking_for_peak = false;
                }
            } else {
                if (mid < local_low) local_low = mid;
                if (mid > local_low + threshold) {
                    if (last_extreme > 0) {
                        double amp = last_extreme - local_low;
                        if (amp >= threshold) {
                            hourly_stats[hour].oscillation_count++;
                            hourly_stats[hour].total_amplitude += amp;
                            daily_stats[dow].oscillation_count++;
                            daily_stats[dow].total_amplitude += amp;
                            session_ptr->oscillation_count++;
                            session_ptr->total_amplitude += amp;
                        }
                    }
                    last_extreme = local_low;
                    local_high = mid;
                    looking_for_peak = true;
                }
            }

            current_hour = hour;
            current_dow = dow;

            if (tick_count % 10000000 == 0) {
                std::cout << "Processed " << tick_count / 1000000 << "M ticks..." << std::endl;
            }
        }

        // Calculate approximate days for averaging
        double total_hours = tick_count / 720000.0;
        double total_days = total_hours / 24.0;

        // Output hourly patterns
        std::cout << "\n=== HOURLY PATTERN (UTC) ===" << std::endl;
        std::cout << std::setw(6) << "Hour"
                  << std::setw(12) << "Osc/Day"
                  << std::setw(12) << "Avg Amp"
                  << std::setw(12) << "Avg Spread"
                  << std::setw(15) << "Session" << std::endl;
        std::cout << std::string(57, '-') << std::endl;

        double max_osc_per_day = 0;
        int best_hour = 0;
        for (int h = 0; h < 24; h++) {
            auto& s = hourly_stats[h];
            double osc_per_day = s.oscillation_count / total_days;
            double avg_amp = s.oscillation_count > 0 ? s.total_amplitude / s.oscillation_count : 0;
            double avg_spread = s.tick_count > 0 ? s.total_spread / s.tick_count : 0;

            if (osc_per_day > max_osc_per_day) {
                max_osc_per_day = osc_per_day;
                best_hour = h;
            }

            const char* session = "";
            if (h >= 13 && h < 16) session = "LON/NY";
            else if (h >= 0 && h < 8) session = "ASIAN";
            else if (h >= 8 && h < 16) session = "LONDON";
            else session = "NY";

            std::cout << std::setw(6) << h
                      << std::setw(12) << std::fixed << std::setprecision(1) << osc_per_day
                      << std::setw(11) << "$" << std::setprecision(2) << avg_amp
                      << std::setw(11) << "$" << std::setprecision(3) << avg_spread
                      << std::setw(15) << session << std::endl;
        }

        // Daily patterns
        std::cout << "\n=== DAY OF WEEK PATTERN ===" << std::endl;
        std::cout << std::setw(12) << "Day"
                  << std::setw(12) << "Osc/Day"
                  << std::setw(12) << "Avg Amp" << std::endl;
        std::cout << std::string(36, '-') << std::endl;

        const char* day_names[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
        for (int d = 0; d < 7; d++) {
            auto& s = daily_stats[d];
            int day_count = (int)(total_days / 7.0);  // Approximate days of this type
            if (day_count < 1) day_count = 1;
            double osc_per_day = s.oscillation_count / (double)day_count;
            double avg_amp = s.oscillation_count > 0 ? s.total_amplitude / s.oscillation_count : 0;

            std::cout << std::setw(12) << day_names[d]
                      << std::setw(12) << std::setprecision(1) << osc_per_day
                      << std::setw(11) << "$" << std::setprecision(2) << avg_amp << std::endl;
        }

        // Session comparison
        std::cout << "\n=== SESSION COMPARISON ===" << std::endl;
        std::cout << std::setw(15) << "Session"
                  << std::setw(12) << "Hours"
                  << std::setw(12) << "Osc Total"
                  << std::setw(12) << "Osc/Hour"
                  << std::setw(12) << "Avg Amp" << std::endl;
        std::cout << std::string(63, '-') << std::endl;

        auto PrintSession = [](const char* name, TimeSlotStats& s, int hours_per_day, double total_days) {
            double total_hours = hours_per_day * total_days;
            double osc_per_hour = s.oscillation_count / total_hours;
            double avg_amp = s.oscillation_count > 0 ? s.total_amplitude / s.oscillation_count : 0;
            std::cout << std::setw(15) << name
                      << std::setw(12) << hours_per_day
                      << std::setw(12) << s.oscillation_count
                      << std::setw(12) << std::setprecision(2) << osc_per_hour
                      << std::setw(11) << "$" << std::setprecision(2) << avg_amp << std::endl;
        };

        PrintSession("Asian (0-8)", asian_stats, 8, total_days);
        PrintSession("London (8-16)", london_stats, 8, total_days);
        PrintSession("NY (16-21)", ny_stats, 5, total_days);
        PrintSession("Overlap (13-16)", overlap_stats, 3, total_days);

        // Recommendations
        std::cout << "\n=== TRADING RECOMMENDATIONS ===" << std::endl;
        std::cout << "1. Best hour for oscillation capture: " << best_hour << ":00 UTC ("
                  << std::setprecision(1) << max_osc_per_day << " osc/day)" << std::endl;
        std::cout << "2. Session ranking by oscillation density:" << std::endl;

        double asian_density = asian_stats.oscillation_count / (8.0 * total_days);
        double london_density = london_stats.oscillation_count / (8.0 * total_days);
        double ny_density = ny_stats.oscillation_count / (5.0 * total_days);
        double overlap_density = overlap_stats.oscillation_count / (3.0 * total_days);

        std::vector<std::pair<std::string, double>> sessions = {
            {"Asian", asian_density}, {"London", london_density},
            {"NY", ny_density}, {"Overlap", overlap_density}
        };
        std::sort(sessions.begin(), sessions.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });

        for (size_t i = 0; i < sessions.size(); i++) {
            std::cout << "   " << (i+1) << ". " << sessions[i].first
                      << " (" << std::setprecision(2) << sessions[i].second << " osc/hour)" << std::endl;
        }
    }

private:
    int CalculateDayOfWeek(int year, int month, int day) {
        // Simplified day of week calculation
        if (month < 3) { month += 12; year--; }
        int k = year % 100;
        int j = year / 100;
        int dow = (day + (13 * (month + 1)) / 5 + k + k / 4 + j / 4 - 2 * j) % 7;
        return (dow + 6) % 7;  // Adjust to 0=Sunday
    }
};

int main() {
    TimePatternAnalyzer analyzer;
    analyzer.Analyze(
        "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv"
    );
    return 0;
}
