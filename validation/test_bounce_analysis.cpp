#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <cmath>

using namespace backtest;

void analyzeBounces(const std::string& file_path, const std::string& year) {
    TickDataConfig config;
    config.file_path = file_path;
    config.format = TickDataFormat::MT5_CSV;
    config.load_all_into_memory = false;

    TickDataManager manager(config);

    std::vector<double> bounces;  // Up-moves after down-moves
    
    double prev_price = 0;
    double local_low = 999999;
    double local_high = 0;
    bool going_down = true;
    double threshold = 0.50;  // Minimum move to count as direction change
    
    Tick tick;
    while (manager.GetNextTick(tick)) {
        double price = (tick.bid + tick.ask) / 2.0;
        
        if (prev_price == 0) {
            prev_price = price;
            local_low = price;
            local_high = price;
            continue;
        }
        
        if (going_down) {
            if (price < local_low) {
                local_low = price;
            } else if (price > local_low + threshold) {
                // Direction changed: was going down, now going up
                // Record the bounce from the low
                going_down = false;
                local_high = price;
            }
        } else {
            if (price > local_high) {
                local_high = price;
            } else if (price < local_high - threshold) {
                // Direction changed: was going up, now going down
                // Record the bounce size (how much it went up from last low)
                double bounce = local_high - local_low;
                if (bounce > 0.1) bounces.push_back(bounce);
                going_down = true;
                local_low = price;
            }
        }
        prev_price = price;
    }

    std::sort(bounces.begin(), bounces.end());
    
    double sum = 0;
    for (double b : bounces) sum += b;
    double avg = sum / bounces.size();
    double median = bounces[bounces.size() / 2];
    double p25 = bounces[bounces.size() * 25 / 100];
    double p75 = bounces[bounces.size() * 75 / 100];
    double p10 = bounces[bounces.size() * 10 / 100];
    
    std::cout << "=== " << year << " Bounce Analysis ===" << std::endl;
    std::cout << "Bounces counted: " << bounces.size() << std::endl;
    std::cout << std::endl;
    std::cout << "Bounce size (low to next high):" << std::endl;
    std::cout << "  Average: $" << std::fixed << std::setprecision(2) << avg << std::endl;
    std::cout << "  Median:  $" << median << std::endl;
    std::cout << "  P10:     $" << p10 << std::endl;
    std::cout << "  P25:     $" << p25 << std::endl;
    std::cout << "  P75:     $" << p75 << std::endl;
    std::cout << std::endl;
}

int main() {
    analyzeBounces("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAUUSD_TICKS_2024.csv", "2024");
    analyzeBounces("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv", "2025");
    return 0;
}
