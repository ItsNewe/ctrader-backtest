#include "../include/tick_data.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>

using namespace backtest;

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "     FINDING CRASH PERIODS IN REAL XAUUSD DATA" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;

    std::string filename = "../validation/Grid/XAUUSD_TICKS_2025.csv";
    std::ifstream file(filename);

    if (!file.is_open()) {
        std::cerr << "Failed to open: " << filename << std::endl;
        return 1;
    }

    std::string line;
    std::getline(file, line);  // Skip header

    // Track price highs and drops
    double peak_price = 0;
    double current_price = 0;
    std::string peak_timestamp;
    std::string current_timestamp;

    struct CrashEvent {
        std::string peak_time;
        std::string bottom_time;
        double peak_price;
        double bottom_price;
        double drop_pct;
        size_t line_start;
        size_t duration_ticks;
    };

    std::vector<CrashEvent> crashes;
    size_t line_num = 0;
    size_t peak_line = 0;
    double bottom_price = 0;
    std::string bottom_timestamp;
    bool in_crash = false;
    CrashEvent current_crash;

    std::cout << "Scanning for price drops > 2%..." << std::endl;

    while (std::getline(file, line)) {
        line_num++;

        std::stringstream ss(line);
        std::string timestamp, bid_str, ask_str;
        std::getline(ss, timestamp, '\t');
        std::getline(ss, bid_str, '\t');

        try {
            current_price = std::stod(bid_str);
            current_timestamp = timestamp;
        } catch (...) {
            continue;
        }

        // Update peak
        if (current_price > peak_price) {
            // If we were in a crash, record it
            if (in_crash && current_crash.drop_pct > 2.0) {
                current_crash.bottom_time = bottom_timestamp;
                current_crash.bottom_price = bottom_price;
                current_crash.duration_ticks = line_num - current_crash.line_start;
                crashes.push_back(current_crash);
            }

            peak_price = current_price;
            peak_timestamp = current_timestamp;
            peak_line = line_num;
            bottom_price = current_price;
            in_crash = false;
        }

        // Check for crash
        double drop_pct = (peak_price - current_price) / peak_price * 100.0;

        if (drop_pct > 2.0 && !in_crash) {
            // Start new crash
            in_crash = true;
            current_crash.peak_time = peak_timestamp;
            current_crash.peak_price = peak_price;
            current_crash.line_start = peak_line;
            current_crash.drop_pct = drop_pct;
            bottom_price = current_price;
            bottom_timestamp = current_timestamp;
        } else if (in_crash) {
            current_crash.drop_pct = drop_pct;
            if (current_price < bottom_price) {
                bottom_price = current_price;
                bottom_timestamp = current_timestamp;
            }
        }

        // Progress
        if (line_num % 5000000 == 0) {
            std::cout << "  Processed " << (line_num / 1000000) << "M ticks..." << std::endl;
        }
    }

    // Final crash if still in one
    if (in_crash && current_crash.drop_pct > 2.0) {
        current_crash.bottom_time = bottom_timestamp;
        current_crash.bottom_price = bottom_price;
        current_crash.duration_ticks = line_num - current_crash.line_start;
        crashes.push_back(current_crash);
    }

    std::cout << std::endl;
    std::cout << "Found " << crashes.size() << " crash events (>2% drop from peak)" << std::endl;
    std::cout << std::endl;

    // Sort by drop percentage
    std::sort(crashes.begin(), crashes.end(),
              [](const CrashEvent& a, const CrashEvent& b) { return a.drop_pct > b.drop_pct; });

    // Show top 20 crashes
    std::cout << "TOP 20 CRASH EVENTS BY SEVERITY:" << std::endl;
    std::cout << std::string(95, '-') << std::endl;
    std::cout << std::left << std::setw(22) << "Peak Time"
              << std::setw(22) << "Bottom Time"
              << std::right << std::setw(10) << "Peak$"
              << std::setw(10) << "Bottom$"
              << std::setw(8) << "Drop%"
              << std::setw(12) << "Duration"
              << std::setw(12) << "StartLine" << std::endl;
    std::cout << std::string(95, '-') << std::endl;

    for (int i = 0; i < std::min(20, (int)crashes.size()); i++) {
        const auto& c = crashes[i];
        std::cout << std::fixed << std::setprecision(2);
        std::cout << std::left << std::setw(22) << c.peak_time
                  << std::setw(22) << c.bottom_time
                  << std::right << std::setw(10) << c.peak_price
                  << std::setw(10) << c.bottom_price
                  << std::setw(7) << c.drop_pct << "%"
                  << std::setw(12) << c.duration_ticks
                  << std::setw(12) << c.line_start << std::endl;
    }

    std::cout << std::string(95, '-') << std::endl;
    std::cout << std::endl;

    // Find crashes > 5%
    int severe_count = 0;
    for (const auto& c : crashes) {
        if (c.drop_pct > 5.0) severe_count++;
    }
    std::cout << "Crashes > 5%: " << severe_count << std::endl;

    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;

    return 0;
}
