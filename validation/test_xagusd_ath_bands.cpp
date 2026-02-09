#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cfloat>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <sstream>

using namespace backtest;

int main() {
    std::string tick_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\mt5\\fill_up_xagusd\\XAGUSD_TESTER_TICKS.csv";

    std::cout << "========================================================" << std::endl;
    std::cout << " XAGUSD: Time Spent Within X% of Running ATH" << std::endl;
    std::cout << " (Finding silver's equivalent of gold's '3% rule')" << std::endl;
    std::cout << "========================================================" << std::endl;

    // Load ticks
    std::ifstream file(tick_path);
    if (!file.is_open()) {
        std::cerr << "Cannot open tick file" << std::endl;
        return 1;
    }

    std::string line;
    std::getline(file, line);  // Skip header

    double running_ath = 0.0;
    long total_ticks = 0;

    // Count ticks within various % bands of ATH
    // Bands: 1%, 2%, 3%, 4%, 5%, 6%, 7%, 8%, 10%, 12%, 15%, 20%
    std::vector<double> bands = {1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 15, 18, 20};
    std::vector<long> ticks_in_band(bands.size(), 0);

    // Track pullback episodes (drops from ATH before new ATH)
    struct Pullback {
        double pct;
        std::string start_time;
        std::string end_time;
        double ath_price;
        double low_price;
    };
    std::vector<Pullback> pullbacks;

    double current_pullback_low = DBL_MAX;
    double current_pullback_ath = 0;
    std::string pullback_start_time;
    bool in_pullback = false;  // Are we below 1% of ATH?

    // Monthly ATH analysis
    struct MonthData {
        std::string month;
        double ath_at_start;
        double ath_at_end;
        double max_pullback_pct;
        long ticks;
        long ticks_within_3pct;
        long ticks_within_5pct;
        long ticks_within_8pct;
    };
    std::vector<MonthData> months;
    std::string current_month;
    MonthData current_month_data = {};

    // Distribution of pullback depths (how deep each pullback goes before recovery)
    std::vector<double> all_pullback_depths;
    double max_pullback_in_run = 0;  // Current pullback depth

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        size_t pos1 = line.find('\t');
        if (pos1 == std::string::npos) continue;
        std::string timestamp = line.substr(0, pos1);

        size_t pos2 = line.find('\t', pos1 + 1);
        if (pos2 == std::string::npos) continue;
        double bid = std::stod(line.substr(pos1 + 1, pos2 - pos1 - 1));

        total_ticks++;

        // Track month
        std::string month = timestamp.substr(0, 7);  // "2025.01"
        if (month != current_month) {
            if (!current_month.empty()) {
                current_month_data.ath_at_end = running_ath;
                months.push_back(current_month_data);
            }
            current_month = month;
            current_month_data = {};
            current_month_data.month = month;
            current_month_data.ath_at_start = running_ath;
            current_month_data.max_pullback_pct = 0;
            current_month_data.ticks = 0;
            current_month_data.ticks_within_3pct = 0;
            current_month_data.ticks_within_5pct = 0;
            current_month_data.ticks_within_8pct = 0;
        }
        current_month_data.ticks++;

        // Update running ATH
        if (bid > running_ath) {
            running_ath = bid;
            // If we were in a pullback, record it
            if (in_pullback && max_pullback_in_run > 1.0) {
                all_pullback_depths.push_back(max_pullback_in_run);
            }
            max_pullback_in_run = 0;
            in_pullback = false;
        }

        // Calculate distance from ATH
        double pct_from_ath = (running_ath - bid) / running_ath * 100.0;

        // Track pullback
        if (pct_from_ath > max_pullback_in_run) {
            max_pullback_in_run = pct_from_ath;
        }
        if (pct_from_ath > 1.0) {
            in_pullback = true;
        }

        // Monthly tracking
        current_month_data.max_pullback_pct = std::max(current_month_data.max_pullback_pct, pct_from_ath);
        if (pct_from_ath <= 3.0) current_month_data.ticks_within_3pct++;
        if (pct_from_ath <= 5.0) current_month_data.ticks_within_5pct++;
        if (pct_from_ath <= 8.0) current_month_data.ticks_within_8pct++;

        // Count in bands
        for (size_t i = 0; i < bands.size(); i++) {
            if (pct_from_ath <= bands[i]) {
                ticks_in_band[i]++;
            }
        }
    }

    // Save last month
    if (!current_month.empty()) {
        current_month_data.ath_at_end = running_ath;
        months.push_back(current_month_data);
    }
    // Record final pullback if still in one
    if (in_pullback && max_pullback_in_run > 1.0) {
        all_pullback_depths.push_back(max_pullback_in_run);
    }

    std::cout << "\nTotal ticks: " << total_ticks << std::endl;
    std::cout << "Final ATH: $" << std::fixed << std::setprecision(3) << running_ath << std::endl;

    // Print band analysis
    std::cout << "\n--- TIME SPENT WITHIN X% OF RUNNING ATH ---" << std::endl;
    std::cout << std::setw(8) << "Band" << std::setw(12) << "Ticks"
              << std::setw(10) << "% Time" << std::setw(15) << "Interpretation" << std::endl;
    std::cout << std::string(50, '-') << std::endl;

    for (size_t i = 0; i < bands.size(); i++) {
        double pct_time = 100.0 * ticks_in_band[i] / total_ticks;
        std::string interp;
        if (pct_time > 90) interp = "*** SAFE ZONE";
        else if (pct_time > 75) interp = "** GOOD";
        else if (pct_time > 60) interp = "* OKAY";
        else interp = "  TOO TIGHT";

        std::cout << std::setw(6) << std::setprecision(0) << bands[i] << "%"
                  << std::setw(12) << ticks_in_band[i]
                  << std::setw(8) << std::setprecision(1) << pct_time << "%"
                  << "   " << interp << std::endl;
    }

    // Print monthly analysis
    std::cout << "\n--- MONTHLY ANALYSIS ---" << std::endl;
    std::cout << std::setw(9) << "Month" << std::setw(8) << "ATH"
              << std::setw(10) << "MaxPull%"
              << std::setw(10) << "In3%"
              << std::setw(10) << "In5%"
              << std::setw(10) << "In8%" << std::endl;
    std::cout << std::string(57, '-') << std::endl;

    for (const auto& m : months) {
        double pct3 = (m.ticks > 0) ? (100.0 * m.ticks_within_3pct / m.ticks) : 0;
        double pct5 = (m.ticks > 0) ? (100.0 * m.ticks_within_5pct / m.ticks) : 0;
        double pct8 = (m.ticks > 0) ? (100.0 * m.ticks_within_8pct / m.ticks) : 0;

        std::cout << std::setw(9) << m.month
                  << std::setw(7) << std::setprecision(1) << m.ath_at_end
                  << std::setw(9) << std::setprecision(1) << m.max_pullback_pct << "%"
                  << std::setw(8) << std::setprecision(0) << pct3 << "%"
                  << std::setw(8) << std::setprecision(0) << pct5 << "%"
                  << std::setw(8) << std::setprecision(0) << pct8 << "%" << std::endl;
    }

    // Pullback depth analysis
    std::sort(all_pullback_depths.begin(), all_pullback_depths.end());
    std::cout << "\n--- PULLBACK DEPTH DISTRIBUTION ---" << std::endl;
    std::cout << "(Each pullback = drop from ATH before new ATH made)" << std::endl;
    std::cout << "Total pullbacks >1%: " << all_pullback_depths.size() << std::endl;

    if (!all_pullback_depths.empty()) {
        int n = all_pullback_depths.size();
        std::cout << "  P25: " << std::setprecision(1) << all_pullback_depths[n/4] << "%" << std::endl;
        std::cout << "  P50: " << all_pullback_depths[n/2] << "%" << std::endl;
        std::cout << "  P75: " << all_pullback_depths[3*n/4] << "%" << std::endl;
        std::cout << "  P90: " << all_pullback_depths[(int)(n*0.9)] << "%" << std::endl;
        std::cout << "  P95: " << all_pullback_depths[(int)(n*0.95)] << "%" << std::endl;
        std::cout << "  Max: " << all_pullback_depths.back() << "%" << std::endl;

        // Count pullbacks in various bands
        std::cout << "\n  Pullbacks that stayed within:" << std::endl;
        for (double band : {3.0, 5.0, 8.0, 10.0, 12.0, 15.0, 18.0}) {
            int count = 0;
            for (double d : all_pullback_depths) {
                if (d <= band) count++;
            }
            std::cout << "    <=" << std::setprecision(0) << band << "%: "
                      << count << "/" << n << " ("
                      << std::setprecision(0) << (100.0 * count / n) << "%)" << std::endl;
        }
    }

    // Calculate silver-to-gold volatility ratio
    std::cout << "\n--- SILVER vs GOLD VOLATILITY COMPARISON ---" << std::endl;
    std::cout << "Gold's '3% from ATH' captures ~90% of bull market time." << std::endl;
    std::cout << "For silver, find the band that captures ~90% of time:" << std::endl;

    double target_pct = 90.0;
    for (size_t i = 0; i < bands.size(); i++) {
        double pct_time = 100.0 * ticks_in_band[i] / total_ticks;
        if (pct_time >= target_pct) {
            std::cout << "  >>> " << bands[i] << "% band captures "
                      << std::setprecision(1) << pct_time << "% of time <<<" << std::endl;
            std::cout << "  Silver's equivalent of gold's 3% = ~" << bands[i] << "%" << std::endl;
            break;
        }
    }

    // Interpolate more precisely
    std::cout << "\n  Precise interpolation:" << std::endl;
    for (double test = 3.0; test <= 15.0; test += 0.5) {
        long count = 0;
        // Re-read would be needed for precise calc, but approximate from bands
        // Linear interpolation between known band points
        size_t lower_idx = 0, upper_idx = 0;
        for (size_t i = 0; i < bands.size(); i++) {
            if (bands[i] <= test) lower_idx = i;
            if (bands[i] >= test && upper_idx == 0) upper_idx = i;
        }
        double lower_pct = 100.0 * ticks_in_band[lower_idx] / total_ticks;
        double upper_pct = 100.0 * ticks_in_band[upper_idx] / total_ticks;
        double interp_pct;
        if (lower_idx == upper_idx) {
            interp_pct = lower_pct;
        } else {
            double frac = (test - bands[lower_idx]) / (bands[upper_idx] - bands[lower_idx]);
            interp_pct = lower_pct + frac * (upper_pct - lower_pct);
        }
        if (interp_pct >= 85 && interp_pct <= 95) {
            std::cout << "    " << std::setprecision(1) << test << "% -> ~"
                      << std::setprecision(1) << interp_pct << "% of time" << std::endl;
        }
    }

    std::cout << "\nDone." << std::endl;
    return 0;
}
