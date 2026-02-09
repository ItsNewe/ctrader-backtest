#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <cmath>

using namespace backtest;

void analyzeOscillations(const std::string& file_path, const std::string& year) {
    TickDataConfig config;
    config.file_path = file_path;
    config.format = TickDataFormat::MT5_CSV;
    config.load_all_into_memory = false;

    TickDataManager manager(config);

    std::vector<double> hourly_ranges;
    std::vector<double> spreads;
    
    double hour_high = 0, hour_low = 999999;
    double prev_mid = 0;
    int last_hour = -1;
    long tick_count = 0;
    
    Tick tick;
    while (manager.GetNextTick(tick)) {
        double mid = (tick.bid + tick.ask) / 2.0;
        double spread = tick.ask - tick.bid;
        spreads.push_back(spread);
        
        // Parse hour from timestamp
        int hour = 0;
        int day = 0;
        sscanf(tick.timestamp.c_str(), "%*d.%*d.%d %d", &day, &hour);
        int hour_id = day * 24 + hour;
        
        if (hour_id != last_hour && last_hour != -1) {
            if (hour_high > hour_low && hour_low < 999999) {
                hourly_ranges.push_back(hour_high - hour_low);
            }
            hour_high = mid;
            hour_low = mid;
        }
        
        hour_high = std::max(hour_high, mid);
        hour_low = std::min(hour_low, mid);
        last_hour = hour_id;
        prev_mid = mid;
        tick_count++;
    }

    // Calculate statistics
    std::sort(hourly_ranges.begin(), hourly_ranges.end());
    std::sort(spreads.begin(), spreads.end());
    
    double sum_range = 0;
    for (double r : hourly_ranges) sum_range += r;
    double avg_range = sum_range / hourly_ranges.size();
    
    double sum_spread = 0;
    for (double s : spreads) sum_spread += s;
    double avg_spread = sum_spread / spreads.size();
    
    double median_range = hourly_ranges[hourly_ranges.size() / 2];
    double p25_range = hourly_ranges[hourly_ranges.size() * 25 / 100];
    double p75_range = hourly_ranges[hourly_ranges.size() * 75 / 100];
    
    std::cout << "=== " << year << " Oscillation Analysis ===" << std::endl;
    std::cout << "Ticks: " << tick_count << std::endl;
    std::cout << "Hours analyzed: " << hourly_ranges.size() << std::endl;
    std::cout << std::endl;
    std::cout << "Hourly Range (high-low):" << std::endl;
    std::cout << "  Average: $" << std::fixed << std::setprecision(2) << avg_range << std::endl;
    std::cout << "  Median:  $" << median_range << std::endl;
    std::cout << "  P25:     $" << p25_range << std::endl;
    std::cout << "  P75:     $" << p75_range << std::endl;
    std::cout << std::endl;
    std::cout << "Spread:" << std::endl;
    std::cout << "  Average: $" << avg_spread << std::endl;
    std::cout << std::endl;
    std::cout << "Ratio (median_range / spread): " << std::setprecision(1) << (median_range / avg_spread) << "x" << std::endl;
    std::cout << std::endl;
}

int main() {
    analyzeOscillations("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAUUSD_TICKS_2024.csv", "2024");
    analyzeOscillations("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv", "2025");
    
    std::cout << "=== Summary ===" << std::endl;
    std::cout << "If optimal_spacing ~ median_oscillation - spread_cost" << std::endl;
    std::cout << "Then years with larger oscillations should prefer wider spacing" << std::endl;
    
    return 0;
}
