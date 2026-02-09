/**
 * Domain 1: Oscillation Characterization
 *
 * Analyzes the fundamental properties of price oscillations:
 * - Amplitude distribution
 * - Duration distribution
 * - Frequency by time period
 * - Stationarity across years
 */

#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <map>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <cfloat>

using namespace backtest;

struct Oscillation {
    double amplitude;      // High - Low
    long duration_ticks;   // Number of ticks
    double start_price;
    double end_price;
    bool direction_up;     // true = trough to peak, false = peak to trough
};

struct HourlyStats {
    int oscillation_count = 0;
    double total_amplitude = 0;
    double avg_amplitude = 0;
    double min_amplitude = DBL_MAX;
    double max_amplitude = 0;
};

class OscillationAnalyzer {
public:
    void Analyze(const std::string& file_path, const std::string& year_label) {
        std::cout << "\n" << std::string(70, '=') << std::endl;
        std::cout << "OSCILLATION CHARACTERIZATION: " << year_label << std::endl;
        std::cout << std::string(70, '=') << std::endl;

        TickDataConfig config;
        config.file_path = file_path;
        config.format = TickDataFormat::MT5_CSV;
        config.load_all_into_memory = false;

        TickDataManager manager(config);
        // Manager initializes automatically in constructor

        // Analysis variables
        std::vector<Oscillation> oscillations;
        std::map<int, HourlyStats> hourly_stats;  // hour -> stats

        double local_high = 0, local_low = DBL_MAX;
        double last_extreme_price = 0;
        long last_extreme_tick = 0;
        bool looking_for_peak = true;
        double threshold = 1.0;  // $1 minimum oscillation

        long tick_count = 0;
        double sum_price = 0;
        double sum_price_sq = 0;
        double min_price = DBL_MAX, max_price = 0;
        int current_hour = -1;
        int hour_oscillations = 0;
        double hour_amplitude_sum = 0;

        Tick tick;
        while (manager.GetNextTick(tick)) {
            tick_count++;
            double mid = (tick.bid + tick.ask) / 2.0;

            // Price statistics
            sum_price += mid;
            sum_price_sq += mid * mid;
            min_price = std::min(min_price, mid);
            max_price = std::max(max_price, mid);

            // Extract hour from timestamp (format: "2025.01.02 03:04:05.678")
            int hour = 0;
            if (tick.timestamp.length() >= 13) {
                hour = std::stoi(tick.timestamp.substr(11, 2));
            }

            // Hour change - save stats
            if (hour != current_hour && current_hour >= 0) {
                if (hourly_stats.find(current_hour) == hourly_stats.end()) {
                    hourly_stats[current_hour] = HourlyStats();
                }
                hourly_stats[current_hour].oscillation_count += hour_oscillations;
                hourly_stats[current_hour].total_amplitude += hour_amplitude_sum;
                hour_oscillations = 0;
                hour_amplitude_sum = 0;
            }
            current_hour = hour;

            // Detect oscillations using swing high/low
            if (looking_for_peak) {
                if (mid > local_high) {
                    local_high = mid;
                }
                if (mid < local_high - threshold) {
                    // Peak confirmed
                    if (last_extreme_price > 0) {
                        Oscillation osc;
                        osc.amplitude = local_high - last_extreme_price;
                        osc.duration_ticks = tick_count - last_extreme_tick;
                        osc.start_price = last_extreme_price;
                        osc.end_price = local_high;
                        osc.direction_up = true;
                        if (osc.amplitude >= threshold) {
                            oscillations.push_back(osc);
                            hour_oscillations++;
                            hour_amplitude_sum += osc.amplitude;
                        }
                    }
                    last_extreme_price = local_high;
                    last_extreme_tick = tick_count;
                    local_low = mid;
                    looking_for_peak = false;
                }
            } else {
                if (mid < local_low) {
                    local_low = mid;
                }
                if (mid > local_low + threshold) {
                    // Trough confirmed
                    if (last_extreme_price > 0) {
                        Oscillation osc;
                        osc.amplitude = last_extreme_price - local_low;
                        osc.duration_ticks = tick_count - last_extreme_tick;
                        osc.start_price = last_extreme_price;
                        osc.end_price = local_low;
                        osc.direction_up = false;
                        if (osc.amplitude >= threshold) {
                            oscillations.push_back(osc);
                            hour_oscillations++;
                            hour_amplitude_sum += osc.amplitude;
                        }
                    }
                    last_extreme_price = local_low;
                    last_extreme_tick = tick_count;
                    local_high = mid;
                    looking_for_peak = true;
                }
            }

            if (tick_count % 10000000 == 0) {
                std::cout << "Processed " << tick_count / 1000000 << "M ticks, "
                          << oscillations.size() << " oscillations..." << std::endl;
            }
        }

        // Calculate statistics
        std::cout << "\n=== BASIC STATISTICS ===" << std::endl;
        std::cout << "Total ticks: " << tick_count << std::endl;
        std::cout << "Total oscillations (>=$" << threshold << "): " << oscillations.size() << std::endl;

        double avg_price = sum_price / tick_count;
        double var_price = (sum_price_sq / tick_count) - (avg_price * avg_price);
        std::cout << "Price range: $" << std::fixed << std::setprecision(2)
                  << min_price << " - $" << max_price << std::endl;
        std::cout << "Average price: $" << avg_price << std::endl;
        std::cout << "Price std dev: $" << std::sqrt(var_price) << std::endl;

        if (oscillations.empty()) {
            std::cout << "No oscillations found!" << std::endl;
            return;
        }

        // Amplitude distribution
        std::cout << "\n=== AMPLITUDE DISTRIBUTION ===" << std::endl;
        std::vector<double> amplitudes;
        for (const auto& osc : oscillations) {
            amplitudes.push_back(osc.amplitude);
        }
        std::sort(amplitudes.begin(), amplitudes.end());

        double sum_amp = std::accumulate(amplitudes.begin(), amplitudes.end(), 0.0);
        double avg_amp = sum_amp / amplitudes.size();
        double sum_sq = 0;
        for (double a : amplitudes) sum_sq += (a - avg_amp) * (a - avg_amp);
        double std_amp = std::sqrt(sum_sq / amplitudes.size());

        std::cout << "Count: " << amplitudes.size() << std::endl;
        std::cout << "Mean amplitude: $" << avg_amp << std::endl;
        std::cout << "Std dev: $" << std_amp << std::endl;
        std::cout << "Min: $" << amplitudes.front() << std::endl;
        std::cout << "25th percentile: $" << amplitudes[amplitudes.size() / 4] << std::endl;
        std::cout << "Median: $" << amplitudes[amplitudes.size() / 2] << std::endl;
        std::cout << "75th percentile: $" << amplitudes[amplitudes.size() * 3 / 4] << std::endl;
        std::cout << "90th percentile: $" << amplitudes[amplitudes.size() * 9 / 10] << std::endl;
        std::cout << "95th percentile: $" << amplitudes[amplitudes.size() * 95 / 100] << std::endl;
        std::cout << "99th percentile: $" << amplitudes[amplitudes.size() * 99 / 100] << std::endl;
        std::cout << "Max: $" << amplitudes.back() << std::endl;

        // Amplitude histogram
        std::cout << "\n=== AMPLITUDE HISTOGRAM ===" << std::endl;
        std::map<int, int> amp_hist;
        for (double a : amplitudes) {
            int bucket = (int)(a / 0.5);  // $0.50 buckets
            amp_hist[bucket]++;
        }
        for (const auto& [bucket, count] : amp_hist) {
            if (bucket <= 20) {  // Up to $10
                double pct = 100.0 * count / amplitudes.size();
                std::cout << "$" << std::setw(4) << (bucket * 0.5) << "-$" << std::setw(4) << ((bucket + 1) * 0.5)
                          << ": " << std::setw(6) << count << " (" << std::setw(5) << std::setprecision(1) << pct << "%) "
                          << std::string((int)(pct / 2), '#') << std::endl;
            }
        }

        // Duration distribution
        std::cout << "\n=== DURATION DISTRIBUTION (ticks) ===" << std::endl;
        std::vector<long> durations;
        for (const auto& osc : oscillations) {
            durations.push_back(osc.duration_ticks);
        }
        std::sort(durations.begin(), durations.end());

        double avg_dur = std::accumulate(durations.begin(), durations.end(), 0L) / (double)durations.size();
        std::cout << "Mean duration: " << (long)avg_dur << " ticks" << std::endl;
        std::cout << "Median duration: " << durations[durations.size() / 2] << " ticks" << std::endl;
        std::cout << "90th percentile: " << durations[durations.size() * 9 / 10] << " ticks" << std::endl;
        std::cout << "99th percentile: " << durations[durations.size() * 99 / 100] << " ticks" << std::endl;

        // Approximate duration in minutes (assuming ~200 ticks/second)
        double ticks_per_min = 200.0 * 60;
        std::cout << "\nApproximate duration in minutes:" << std::endl;
        std::cout << "Mean: " << avg_dur / ticks_per_min << " min" << std::endl;
        std::cout << "Median: " << durations[durations.size() / 2] / ticks_per_min << " min" << std::endl;

        // Hourly pattern
        std::cout << "\n=== OSCILLATIONS BY HOUR (UTC) ===" << std::endl;
        int total_days = tick_count / (200 * 60 * 60 * 24);  // Approximate days
        if (total_days < 1) total_days = 1;

        std::cout << "Hour | Avg Osc/Day | Avg Amplitude" << std::endl;
        std::cout << std::string(40, '-') << std::endl;
        for (int h = 0; h < 24; h++) {
            if (hourly_stats.find(h) != hourly_stats.end()) {
                auto& stats = hourly_stats[h];
                double avg_osc = stats.oscillation_count / (double)total_days;
                double avg_a = stats.oscillation_count > 0 ? stats.total_amplitude / stats.oscillation_count : 0;
                std::cout << std::setw(4) << h << " | " << std::setw(11) << std::setprecision(1) << avg_osc
                          << " | $" << std::setprecision(2) << avg_a << std::endl;
            }
        }

        // Daily frequency
        double total_hours = tick_count / (200.0 * 60 * 60);
        double osc_per_hour = oscillations.size() / total_hours;
        double osc_per_day = osc_per_hour * 24;

        std::cout << "\n=== FREQUENCY SUMMARY ===" << std::endl;
        std::cout << "Total trading hours: " << std::setprecision(1) << total_hours << std::endl;
        std::cout << "Oscillations per hour: " << std::setprecision(2) << osc_per_hour << std::endl;
        std::cout << "Oscillations per day: " << std::setprecision(1) << osc_per_day << std::endl;

        // Store results for comparison
        results_[year_label] = {
            oscillations.size(),
            avg_amp,
            std_amp,
            osc_per_day,
            avg_dur / ticks_per_min
        };
    }

    void CompareYears() {
        if (results_.size() < 2) return;

        std::cout << "\n" << std::string(70, '=') << std::endl;
        std::cout << "YEAR-OVER-YEAR COMPARISON" << std::endl;
        std::cout << std::string(70, '=') << std::endl;
        std::cout << std::setw(10) << "Year"
                  << std::setw(12) << "Osc Count"
                  << std::setw(12) << "Avg Amp"
                  << std::setw(12) << "Std Amp"
                  << std::setw(12) << "Osc/Day"
                  << std::setw(12) << "Avg Min" << std::endl;
        std::cout << std::string(70, '-') << std::endl;

        for (const auto& [year, stats] : results_) {
            std::cout << std::setw(10) << year
                      << std::setw(12) << stats.count
                      << std::setw(11) << "$" << std::setprecision(2) << stats.avg_amplitude
                      << std::setw(11) << "$" << stats.std_amplitude
                      << std::setw(12) << std::setprecision(1) << stats.osc_per_day
                      << std::setw(12) << std::setprecision(2) << stats.avg_duration_min << std::endl;
        }
    }

private:
    struct YearStats {
        size_t count;
        double avg_amplitude;
        double std_amplitude;
        double osc_per_day;
        double avg_duration_min;
    };
    std::map<std::string, YearStats> results_;
};

int main() {
    std::cout << std::fixed;

    OscillationAnalyzer analyzer;

    // Analyze 2025 data
    analyzer.Analyze(
        "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv",
        "2025"
    );

    // Analyze 2024 data (out-of-sample)
    analyzer.Analyze(
        "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAUUSD_TICKS_2024.csv",
        "2024"
    );

    // Compare years
    analyzer.CompareYears();

    std::cout << "\n=== KEY FINDINGS ===" << std::endl;
    std::cout << "1. Oscillation amplitude distribution shape" << std::endl;
    std::cout << "2. Whether oscillation properties are stationary across years" << std::endl;
    std::cout << "3. Time-of-day patterns in oscillation frequency" << std::endl;
    std::cout << "4. Duration characteristics for position holding expectations" << std::endl;

    return 0;
}
