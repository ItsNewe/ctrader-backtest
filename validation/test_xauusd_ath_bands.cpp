#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cfloat>
#include <cmath>
#include <algorithm>
#include <fstream>

using namespace backtest;

int main() {
    std::string tick_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";

    std::cout << "========================================================" << std::endl;
    std::cout << " XAUUSD: Time Spent Within X% of Running ATH" << std::endl;
    std::cout << " (Measuring gold's actual '3% rule' empirically)" << std::endl;
    std::cout << "========================================================" << std::endl;

    std::ifstream file(tick_path);
    if (!file.is_open()) {
        std::cerr << "Cannot open tick file: " << tick_path << std::endl;
        return 1;
    }

    std::string line;
    std::getline(file, line);  // Skip header

    double running_ath = 0.0;
    long total_ticks = 0;

    std::vector<double> bands = {0.5, 1, 1.5, 2, 2.5, 3, 4, 5, 6, 7, 8, 10, 12, 15, 18, 20};
    std::vector<long> ticks_in_band(bands.size(), 0);

    // Monthly analysis
    struct MonthData {
        std::string month;
        double ath_at_end;
        double max_pullback_pct;
        long ticks;
        long ticks_within_2pct;
        long ticks_within_3pct;
        long ticks_within_5pct;
        long ticks_within_8pct;
    };
    std::vector<MonthData> months;
    std::string current_month;
    MonthData current_month_data = {};

    // Pullback depth tracking
    std::vector<double> all_pullback_depths;
    double max_pullback_in_run = 0;
    bool in_pullback = false;

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
        std::string month = timestamp.substr(0, 7);
        if (month != current_month) {
            if (!current_month.empty()) {
                current_month_data.ath_at_end = running_ath;
                months.push_back(current_month_data);
            }
            current_month = month;
            current_month_data = {};
            current_month_data.month = month;
            current_month_data.max_pullback_pct = 0;
            current_month_data.ticks = 0;
            current_month_data.ticks_within_2pct = 0;
            current_month_data.ticks_within_3pct = 0;
            current_month_data.ticks_within_5pct = 0;
            current_month_data.ticks_within_8pct = 0;
        }
        current_month_data.ticks++;

        // Update running ATH
        if (bid > running_ath) {
            running_ath = bid;
            if (in_pullback && max_pullback_in_run > 0.5) {
                all_pullback_depths.push_back(max_pullback_in_run);
            }
            max_pullback_in_run = 0;
            in_pullback = false;
        }

        // Distance from ATH
        double pct_from_ath = (running_ath - bid) / running_ath * 100.0;

        if (pct_from_ath > max_pullback_in_run) {
            max_pullback_in_run = pct_from_ath;
        }
        if (pct_from_ath > 0.5) {
            in_pullback = true;
        }

        // Monthly
        current_month_data.max_pullback_pct = std::max(current_month_data.max_pullback_pct, pct_from_ath);
        if (pct_from_ath <= 2.0) current_month_data.ticks_within_2pct++;
        if (pct_from_ath <= 3.0) current_month_data.ticks_within_3pct++;
        if (pct_from_ath <= 5.0) current_month_data.ticks_within_5pct++;
        if (pct_from_ath <= 8.0) current_month_data.ticks_within_8pct++;

        // Bands
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
    if (in_pullback && max_pullback_in_run > 0.5) {
        all_pullback_depths.push_back(max_pullback_in_run);
    }

    std::cout << "\nTotal ticks: " << total_ticks << std::endl;
    std::cout << "Final ATH: $" << std::fixed << std::setprecision(2) << running_ath << std::endl;

    // Band analysis
    std::cout << "\n--- TIME SPENT WITHIN X% OF RUNNING ATH ---" << std::endl;
    std::cout << std::setw(8) << "Band" << std::setw(12) << "Ticks"
              << std::setw(10) << "% Time" << std::setw(18) << "Interpretation" << std::endl;
    std::cout << std::string(52, '-') << std::endl;

    for (size_t i = 0; i < bands.size(); i++) {
        double pct_time = 100.0 * ticks_in_band[i] / total_ticks;
        std::string interp;
        if (pct_time > 90) interp = "*** SAFE ZONE";
        else if (pct_time > 75) interp = "** GOOD";
        else if (pct_time > 60) interp = "* OKAY";
        else interp = "  TOO TIGHT";

        std::cout << std::setw(5) << std::setprecision(1) << bands[i] << "%"
                  << std::setw(12) << ticks_in_band[i]
                  << std::setw(8) << std::setprecision(1) << pct_time << "%"
                  << "   " << interp << std::endl;
    }

    // Monthly analysis
    std::cout << "\n--- MONTHLY ANALYSIS ---" << std::endl;
    std::cout << std::setw(9) << "Month" << std::setw(8) << "ATH"
              << std::setw(10) << "MaxPull%"
              << std::setw(8) << "In2%"
              << std::setw(8) << "In3%"
              << std::setw(8) << "In5%"
              << std::setw(8) << "In8%" << std::endl;
    std::cout << std::string(61, '-') << std::endl;

    for (const auto& m : months) {
        double pct2 = (m.ticks > 0) ? (100.0 * m.ticks_within_2pct / m.ticks) : 0;
        double pct3 = (m.ticks > 0) ? (100.0 * m.ticks_within_3pct / m.ticks) : 0;
        double pct5 = (m.ticks > 0) ? (100.0 * m.ticks_within_5pct / m.ticks) : 0;
        double pct8 = (m.ticks > 0) ? (100.0 * m.ticks_within_8pct / m.ticks) : 0;

        std::cout << std::setw(9) << m.month
                  << std::setw(7) << std::setprecision(0) << m.ath_at_end
                  << std::setw(9) << std::setprecision(1) << m.max_pullback_pct << "%"
                  << std::setw(7) << std::setprecision(0) << pct2 << "%"
                  << std::setw(7) << std::setprecision(0) << pct3 << "%"
                  << std::setw(7) << std::setprecision(0) << pct5 << "%"
                  << std::setw(7) << std::setprecision(0) << pct8 << "%" << std::endl;
    }

    // Pullback depth distribution
    std::sort(all_pullback_depths.begin(), all_pullback_depths.end());
    std::cout << "\n--- PULLBACK DEPTH DISTRIBUTION ---" << std::endl;
    std::cout << "(Each pullback = drop from ATH before new ATH made)" << std::endl;
    std::cout << "Total pullbacks >0.5%: " << all_pullback_depths.size() << std::endl;

    if (!all_pullback_depths.empty()) {
        int n = all_pullback_depths.size();
        std::cout << "  P25: " << std::setprecision(2) << all_pullback_depths[n/4] << "%" << std::endl;
        std::cout << "  P50: " << all_pullback_depths[n/2] << "%" << std::endl;
        std::cout << "  P75: " << all_pullback_depths[3*n/4] << "%" << std::endl;
        std::cout << "  P90: " << all_pullback_depths[(int)(n*0.9)] << "%" << std::endl;
        std::cout << "  P95: " << all_pullback_depths[(int)(n*0.95)] << "%" << std::endl;
        std::cout << "  Max: " << all_pullback_depths.back() << "%" << std::endl;

        std::cout << "\n  Pullbacks that stayed within:" << std::endl;
        for (double band : {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 8.0, 10.0, 12.0}) {
            int count = 0;
            for (double d : all_pullback_depths) {
                if (d <= band) count++;
            }
            std::cout << "    <=" << std::setprecision(0) << band << "%: "
                      << count << "/" << n << " ("
                      << std::setprecision(0) << (100.0 * count / n) << "%)" << std::endl;
        }
    }

    // Find the 90% threshold
    std::cout << "\n--- FINDING THE '90% TIME' THRESHOLD ---" << std::endl;
    for (size_t i = 0; i < bands.size(); i++) {
        double pct_time = 100.0 * ticks_in_band[i] / total_ticks;
        if (pct_time >= 88 && pct_time <= 95) {
            std::cout << "  " << std::setprecision(1) << bands[i]
                      << "% band -> " << std::setprecision(1) << pct_time << "% of time" << std::endl;
        }
    }

    // Compare with silver
    std::cout << "\n--- GOLD vs SILVER COMPARISON ---" << std::endl;
    std::cout << "Gold 3% band: " << std::setprecision(1)
              << (100.0 * ticks_in_band[5] / total_ticks) << "% of time" << std::endl;
    std::cout << "Gold 5% band: " << std::setprecision(1)
              << (100.0 * ticks_in_band[7] / total_ticks) << "% of time" << std::endl;
    std::cout << "Silver 3% band: 53.2% of time" << std::endl;
    std::cout << "Silver 10% band: 88.4% of time" << std::endl;
    std::cout << "\nRatio (silver/gold for same coverage): ";
    // Find gold's band for ~90%
    for (size_t i = 0; i < bands.size(); i++) {
        double pct_time = 100.0 * ticks_in_band[i] / total_ticks;
        if (pct_time >= 89) {
            std::cout << "Gold needs " << bands[i] << "% for "
                      << std::setprecision(1) << pct_time << "% coverage" << std::endl;
            std::cout << "Silver needs 10% for 88.4% coverage" << std::endl;
            std::cout << "Ratio: " << std::setprecision(1) << (10.0 / bands[i]) << "x" << std::endl;
            break;
        }
    }

    std::cout << "\nDone." << std::endl;
    return 0;
}
