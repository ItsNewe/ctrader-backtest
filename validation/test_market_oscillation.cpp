/**
 * Market Oscillation Analysis
 * Measures frequency and amplitude of price oscillations
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <iomanip>
#include <deque>

struct SwingPoint {
    double price;
    long tick_index;
    bool is_high;
};

// Simpler approach: track price reversals using ZigZag-style logic
class OscillationAnalyzer {
private:
    double min_swing_;
    std::vector<SwingPoint> swings_;
    double last_extreme_ = 0;
    long last_extreme_idx_ = 0;
    bool last_was_high_ = false;
    bool initialized_ = false;

public:
    OscillationAnalyzer(int /*lookback*/ = 100, double min_swing = 1.0)
        : min_swing_(min_swing) {}

    void AddPrice(double price, long tick_index) {
        if (!initialized_) {
            last_extreme_ = price;
            last_extreme_idx_ = tick_index;
            initialized_ = true;
            return;
        }

        double move = price - last_extreme_;

        // Looking for reversal
        if (!last_was_high_ && move >= min_swing_) {
            // Price moved up by min_swing - mark previous as low, current direction is up
            swings_.push_back({last_extreme_, last_extreme_idx_, false});
            last_extreme_ = price;
            last_extreme_idx_ = tick_index;
            last_was_high_ = true;
        } else if (last_was_high_ && move <= -min_swing_) {
            // Price moved down by min_swing - mark previous as high
            swings_.push_back({last_extreme_, last_extreme_idx_, true});
            last_extreme_ = price;
            last_extreme_idx_ = tick_index;
            last_was_high_ = false;
        } else {
            // Update extreme if continuing in same direction
            if (last_was_high_ && price > last_extreme_) {
                last_extreme_ = price;
                last_extreme_idx_ = tick_index;
            } else if (!last_was_high_ && price < last_extreme_) {
                last_extreme_ = price;
                last_extreme_idx_ = tick_index;
            }
        }
    }

    void GetStats(double ticks_per_minute, int& count, double& avg_amp, double& med_amp,
                  double& max_amp, double& avg_period_min) {
        count = 0; avg_amp = 0; med_amp = 0; max_amp = 0; avg_period_min = 0;
        if (swings_.size() < 2) return;

        std::vector<double> amplitudes;
        std::vector<long> periods;

        for (size_t i = 1; i < swings_.size(); i++) {
            amplitudes.push_back(std::abs(swings_[i].price - swings_[i-1].price));
            periods.push_back(swings_[i].tick_index - swings_[i-1].tick_index);
        }

        count = amplitudes.size();
        double sum_amp = 0, sum_period = 0;
        for (size_t i = 0; i < amplitudes.size(); i++) {
            sum_amp += amplitudes[i];
            sum_period += periods[i];
            max_amp = std::max(max_amp, amplitudes[i]);
        }
        avg_amp = sum_amp / count;
        avg_period_min = (sum_period / count) / ticks_per_minute;

        std::sort(amplitudes.begin(), amplitudes.end());
        med_amp = amplitudes[amplitudes.size() / 2];
    }
};

int main() {
    std::cout << "=== Market Oscillation Analysis ===" << std::endl;
    std::cout << std::fixed << std::setprecision(2);

    std::string filename = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    std::ifstream file(filename);
    if (!file.is_open()) { std::cerr << "Cannot open file\n"; return 1; }

    std::string line;
    std::getline(file, line);

    std::vector<double> min_swings = {0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 50.0};
    std::vector<OscillationAnalyzer> analyzers;
    for (double ms : min_swings) analyzers.emplace_back(50, ms);

    long tick_count = 0;
    double first_price = 0, last_price = 0, min_price = 1e9, max_price = 0;
    std::vector<double> hourly_ranges;
    double hour_high = 0, hour_low = 1e9;
    std::string current_hour = "";

    while (std::getline(file, line)) {
        try {
            std::stringstream ss(line);
            std::string token;
            std::getline(ss, token, '\t');
            std::string hour = token.substr(0, 13);
            std::getline(ss, token, '\t');
            if (token.empty()) continue;
            double bid = std::stod(token);
            if (bid <= 0) continue;

            if (first_price == 0) first_price = bid;
            last_price = bid;
            min_price = std::min(min_price, bid);
            max_price = std::max(max_price, bid);

            if (hour != current_hour && !current_hour.empty() && hour_high > hour_low) {
                hourly_ranges.push_back(hour_high - hour_low);
                hour_high = hour_low = bid;
            }
            current_hour = hour;
            hour_high = std::max(hour_high, bid);
            hour_low = std::min(hour_low, bid);

            for (auto& a : analyzers) a.AddPrice(bid, tick_count);
            tick_count++;
            if (tick_count % 10000000 == 0) std::cout << "Processed " << tick_count/1000000 << "M ticks...\n";
        } catch (...) { continue; }
    }

    double ticks_per_minute = tick_count / (250.0 * 23.0 * 60.0);

    std::cout << "\nTotal Ticks: " << tick_count << std::endl;
    std::cout << "Price Range: $" << min_price << " - $" << max_price << std::endl;
    std::cout << "Max Drop: $" << (max_price - min_price) << " ("
              << ((max_price - min_price) / max_price * 100) << "%)\n";

    if (!hourly_ranges.empty()) {
        std::sort(hourly_ranges.begin(), hourly_ranges.end());
        double sum = 0; for (double r : hourly_ranges) sum += r;
        std::cout << "\nHourly Range - Avg: $" << sum/hourly_ranges.size()
                  << ", Median: $" << hourly_ranges[hourly_ranges.size()/2]
                  << ", 90th: $" << hourly_ranges[hourly_ranges.size()*9/10] << std::endl;
    }

    std::cout << "\n" << std::setw(10) << "MinSwing" << std::setw(10) << "Count"
              << std::setw(10) << "AvgAmp" << std::setw(10) << "MedAmp"
              << std::setw(10) << "MaxAmp" << std::setw(12) << "Period(min)"
              << std::setw(10) << "Freq/Day" << std::endl;

    for (size_t i = 0; i < min_swings.size(); i++) {
        int count; double avg, med, max_a, period;
        analyzers[i].GetStats(ticks_per_minute, count, avg, med, max_a, period);
        double freq = period > 0 ? 24*60/period : 0;
        std::cout << std::setw(9) << "$" << min_swings[i] << std::setw(10) << count
                  << std::setw(9) << "$" << avg << std::setw(9) << "$" << med
                  << std::setw(9) << "$" << max_a << std::setw(12) << period
                  << std::setw(10) << freq << std::endl;
    }

    std::cout << "\n=== Recommendations ===" << std::endl;
    std::cout << "Survive Down: " << ((max_price-min_price)/max_price*100 + 2) << "% (covers max drop + buffer)\n";
    std::cout << "Grid Spacing: Use row where Freq/Day = 10-20 for optimal trade frequency\n";

    return 0;
}
