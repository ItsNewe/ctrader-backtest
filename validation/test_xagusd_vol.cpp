#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <cmath>

using namespace backtest;

int main() {
    TickDataConfig config;
    config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\mt5\\fill_up_xagusd\\XAGUSD_TESTER_TICKS.csv";
    config.format = TickDataFormat::MT5_CSV;
    config.load_all_into_memory = true;

    std::cout << "Loading XAGUSD ticks..." << std::flush;
    TickDataManager loader(config);
    const auto& ticks = loader.GetAllTicks();
    std::cout << " " << ticks.size() << " ticks loaded" << std::endl;

    // Track hourly and 4-hourly ranges as % of price
    std::vector<double> hourly_pct;
    std::vector<double> four_hour_pct;

    double h_high = ticks[0].bid, h_low = ticks[0].bid;
    double f_high = ticks[0].bid, f_low = ticks[0].bid;
    std::string h_start = ticks[0].timestamp.substr(0, 13); // YYYY.MM.DD HH
    std::string f_start = ticks[0].timestamp.substr(0, 13);
    int f_hours = 0;
    std::string prev_hour = h_start;

    for (size_t i = 0; i < ticks.size(); i++) {
        double bid = ticks[i].bid;
        std::string cur_hour = ticks[i].timestamp.substr(0, 13);

        h_high = std::max(h_high, bid);
        h_low = std::min(h_low, bid);
        f_high = std::max(f_high, bid);
        f_low = std::min(f_low, bid);

        if (cur_hour != prev_hour) {
            // Hourly period ended
            double mid = (h_high + h_low) / 2.0;
            if (mid > 0) {
                double range_pct = (h_high - h_low) / mid * 100.0;
                hourly_pct.push_back(range_pct);
            }
            f_hours++;

            if (f_hours >= 4) {
                double fmid = (f_high + f_low) / 2.0;
                if (fmid > 0) {
                    double frange_pct = (f_high - f_low) / fmid * 100.0;
                    four_hour_pct.push_back(frange_pct);
                }
                f_high = bid;
                f_low = bid;
                f_hours = 0;
            }

            h_high = bid;
            h_low = bid;
            prev_hour = cur_hour;
        }
    }

    // Sort for percentiles
    std::sort(hourly_pct.begin(), hourly_pct.end());
    std::sort(four_hour_pct.begin(), four_hour_pct.end());

    auto percentile = [](const std::vector<double>& v, double p) -> double {
        size_t idx = (size_t)(v.size() * p / 100.0);
        if (idx >= v.size()) idx = v.size() - 1;
        return v[idx];
    };

    double h_sum = 0;
    for (double v : hourly_pct) h_sum += v;
    double h_avg = h_sum / hourly_pct.size();

    double f_sum = 0;
    for (double v : four_hour_pct) f_sum += v;
    double f_avg = f_sum / four_hour_pct.size();

    std::cout << "\n=== XAGUSD Volatility Analysis ===" << std::endl;
    std::cout << "Price range: $" << std::fixed << std::setprecision(2)
              << ticks.front().bid << " to $" << ticks.back().bid << std::endl;

    std::cout << "\n--- Hourly Range (% of price) ---" << std::endl;
    std::cout << "Samples: " << hourly_pct.size() << std::endl;
    std::cout << "Average: " << std::setprecision(3) << h_avg << "%" << std::endl;
    std::cout << "Median:  " << percentile(hourly_pct, 50) << "%" << std::endl;
    std::cout << "P25:     " << percentile(hourly_pct, 25) << "%" << std::endl;
    std::cout << "P75:     " << percentile(hourly_pct, 75) << "%" << std::endl;
    std::cout << "P10:     " << percentile(hourly_pct, 10) << "%" << std::endl;
    std::cout << "P90:     " << percentile(hourly_pct, 90) << "%" << std::endl;

    std::cout << "\n--- 4-Hour Range (% of price) ---" << std::endl;
    std::cout << "Samples: " << four_hour_pct.size() << std::endl;
    std::cout << "Average: " << std::setprecision(3) << f_avg << "%" << std::endl;
    std::cout << "Median:  " << percentile(four_hour_pct, 50) << "%" << std::endl;
    std::cout << "P25:     " << percentile(four_hour_pct, 25) << "%" << std::endl;
    std::cout << "P75:     " << percentile(four_hour_pct, 75) << "%" << std::endl;
    std::cout << "P10:     " << percentile(four_hour_pct, 10) << "%" << std::endl;
    std::cout << "P90:     " << percentile(four_hour_pct, 90) << "%" << std::endl;

    std::cout << "\n--- Recommended TypicalVolPct ---" << std::endl;
    std::cout << "For 1h lookback: " << std::setprecision(2) << percentile(hourly_pct, 50) << "%" << std::endl;
    std::cout << "For 4h lookback: " << std::setprecision(2) << percentile(four_hour_pct, 50) << "%" << std::endl;

    // Compare with XAUUSD values from CLAUDE.md
    std::cout << "\n--- Comparison with XAUUSD ---" << std::endl;
    std::cout << "XAUUSD 1h median: ~0.27% (measured from 2025 data)" << std::endl;
    std::cout << "XAUUSD 4h median: ~0.55% (measured from 2025 data)" << std::endl;
    std::cout << "XAGUSD 1h median: " << percentile(hourly_pct, 50) << "%" << std::endl;
    std::cout << "XAGUSD 4h median: " << percentile(four_hour_pct, 50) << "%" << std::endl;
    double ratio_1h = percentile(hourly_pct, 50) / 0.27;
    double ratio_4h = percentile(four_hour_pct, 50) / 0.55;
    std::cout << "Silver/Gold volatility ratio (1h): " << std::setprecision(1) << ratio_1h << "x" << std::endl;
    std::cout << "Silver/Gold volatility ratio (4h): " << std::setprecision(1) << ratio_4h << "x" << std::endl;

    return 0;
}
