/**
 * Domain 8: Multi-Instrument Comparison
 *
 * Compare oscillation characteristics across instruments:
 * - XAUUSD (Gold)
 * - NAS100
 *
 * Goal: Find which instruments have best oscillation:spread ratio
 */

#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <cfloat>
#include <algorithm>

using namespace backtest;

struct InstrumentStats {
    std::string symbol;
    long tick_count;
    int oscillation_count;
    double avg_amplitude;
    double avg_spread;
    double oscillation_per_day;
    double amplitude_to_spread_ratio;
    double price_range_pct;
};

class MultiInstrumentAnalyzer {
public:
    InstrumentStats AnalyzeInstrument(const std::string& symbol,
                                       const std::string& file_path,
                                       double threshold) {
        InstrumentStats stats;
        stats.symbol = symbol;
        stats.tick_count = 0;
        stats.oscillation_count = 0;
        stats.avg_amplitude = 0;
        stats.avg_spread = 0;

        TickDataConfig config;
        config.file_path = file_path;
        config.format = TickDataFormat::MT5_CSV;
        config.load_all_into_memory = false;

        TickDataManager manager(config);
        // Manager initializes automatically in constructor - may throw on error

        std::vector<double> amplitudes;
        double total_spread = 0;
        double local_high = 0, local_low = DBL_MAX;
        double last_extreme = 0;
        bool looking_for_peak = true;
        double min_price = DBL_MAX, max_price = 0;

        Tick tick;
        while (manager.GetNextTick(tick)) {
            stats.tick_count++;
            double mid = (tick.bid + tick.ask) / 2.0;
            double spread = tick.ask - tick.bid;

            total_spread += spread;
            min_price = std::min(min_price, mid);
            max_price = std::max(max_price, mid);

            // Oscillation detection
            if (looking_for_peak) {
                if (mid > local_high) local_high = mid;
                if (mid < local_high - threshold && last_extreme > 0) {
                    double amp = local_high - last_extreme;
                    if (amp >= threshold) {
                        amplitudes.push_back(amp);
                    }
                    last_extreme = local_high;
                    local_low = mid;
                    looking_for_peak = false;
                }
            } else {
                if (mid < local_low) local_low = mid;
                if (mid > local_low + threshold && last_extreme > 0) {
                    double amp = last_extreme - local_low;
                    if (amp >= threshold) {
                        amplitudes.push_back(amp);
                    }
                    last_extreme = local_low;
                    local_high = mid;
                    looking_for_peak = true;
                }
            }

            if (stats.tick_count % 10000000 == 0) {
                std::cout << "  " << symbol << ": " << stats.tick_count / 1000000 << "M ticks..." << std::endl;
            }
        }

        // Calculate statistics
        stats.oscillation_count = amplitudes.size();

        if (!amplitudes.empty()) {
            double sum = 0;
            for (double a : amplitudes) sum += a;
            stats.avg_amplitude = sum / amplitudes.size();
        }

        stats.avg_spread = total_spread / stats.tick_count;

        double hours = stats.tick_count / 720000.0;
        stats.oscillation_per_day = stats.oscillation_count / (hours / 24.0);

        stats.amplitude_to_spread_ratio = stats.avg_amplitude / stats.avg_spread;

        stats.price_range_pct = (max_price - min_price) / min_price * 100.0;

        return stats;
    }

    void CompareInstruments() {
        std::cout << "\n" << std::string(70, '=') << std::endl;
        std::cout << "MULTI-INSTRUMENT OSCILLATION COMPARISON" << std::endl;
        std::cout << std::string(70, '=') << std::endl;

        std::vector<InstrumentStats> all_stats;

        // Analyze XAUUSD
        std::cout << "\nAnalyzing XAUUSD..." << std::endl;
        all_stats.push_back(AnalyzeInstrument(
            "XAUUSD",
            "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv",
            1.0  // $1 threshold for gold
        ));

        // Analyze NAS100 (if available)
        std::cout << "\nAnalyzing NAS100..." << std::endl;
        all_stats.push_back(AnalyzeInstrument(
            "NAS100",
            "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\NAS100\\NAS100_TICKS_2025.csv",
            10.0  // $10 threshold for NAS100 (~$20k price)
        ));

        // Print comparison table
        std::cout << "\n" << std::string(90, '=') << std::endl;
        std::cout << "INSTRUMENT COMPARISON" << std::endl;
        std::cout << std::string(90, '=') << std::endl;
        std::cout << std::setw(10) << "Symbol"
                  << std::setw(12) << "Osc/Day"
                  << std::setw(12) << "Avg Amp"
                  << std::setw(12) << "Avg Spread"
                  << std::setw(12) << "Amp/Spread"
                  << std::setw(12) << "Range %"
                  << std::setw(15) << "Quality" << std::endl;
        std::cout << std::string(90, '-') << std::endl;

        for (const auto& s : all_stats) {
            if (s.tick_count == 0) continue;

            std::string quality;
            if (s.amplitude_to_spread_ratio > 5) quality = "EXCELLENT";
            else if (s.amplitude_to_spread_ratio > 3) quality = "GOOD";
            else if (s.amplitude_to_spread_ratio > 2) quality = "FAIR";
            else quality = "POOR";

            std::cout << std::setw(10) << s.symbol
                      << std::setw(12) << std::fixed << std::setprecision(0) << s.oscillation_per_day
                      << std::setw(11) << "$" << std::setprecision(2) << s.avg_amplitude
                      << std::setw(11) << "$" << std::setprecision(3) << s.avg_spread
                      << std::setw(12) << std::setprecision(1) << s.amplitude_to_spread_ratio << "x"
                      << std::setw(11) << std::setprecision(1) << s.price_range_pct << "%"
                      << std::setw(15) << quality << std::endl;
        }

        // Ranking
        std::cout << "\n=== OSCILLATION QUALITY RANKING ===" << std::endl;
        std::cout << "Metric: Amplitude-to-Spread ratio (higher = better for grid trading)" << std::endl;
        std::cout << std::endl;

        std::sort(all_stats.begin(), all_stats.end(),
                  [](const InstrumentStats& a, const InstrumentStats& b) {
                      return a.amplitude_to_spread_ratio > b.amplitude_to_spread_ratio;
                  });

        int rank = 1;
        for (const auto& s : all_stats) {
            if (s.tick_count == 0) continue;
            std::cout << rank++ << ". " << s.symbol << " (" << std::setprecision(1)
                      << s.amplitude_to_spread_ratio << "x amplitude/spread)" << std::endl;
        }

        // Recommendations
        std::cout << "\n=== RECOMMENDATIONS ===" << std::endl;
        if (!all_stats.empty() && all_stats[0].tick_count > 0) {
            std::cout << "1. Best instrument for oscillation strategy: " << all_stats[0].symbol << std::endl;
            std::cout << "2. Expected oscillations per day: " << std::setprecision(0)
                      << all_stats[0].oscillation_per_day << std::endl;
            std::cout << "3. Optimal spacing suggestion: $" << std::setprecision(2)
                      << all_stats[0].avg_amplitude * 0.5 << " (half of avg amplitude)" << std::endl;
        }
    }
};

int main() {
    MultiInstrumentAnalyzer analyzer;
    analyzer.CompareInstruments();
    return 0;
}
