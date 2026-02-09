/**
 * Domain 10: Correlation Analysis
 *
 * Analyze correlations between oscillation frequency and:
 * - Spread (liquidity proxy)
 * - Volatility
 * - Time of day
 * - Price level
 * - Previous oscillation characteristics
 */

#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <map>
#include <cmath>
#include <algorithm>
#include <deque>
#include <cfloat>

using namespace backtest;

struct HourlyData {
    int oscillation_count = 0;
    double total_spread = 0;
    double total_volatility = 0;  // High-Low range
    long tick_count = 0;
    double avg_price = 0;
    double price_sum = 0;
};

class CorrelationAnalyzer {
public:
    void Analyze() {
        std::cout << "\n" << std::string(70, '=') << std::endl;
        std::cout << "CORRELATION ANALYSIS" << std::endl;
        std::cout << std::string(70, '=') << std::endl;

        TickDataConfig config;
        config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
        config.format = TickDataFormat::MT5_CSV;
        config.load_all_into_memory = false;

        TickDataManager manager(config);
        // Manager initializes automatically in constructor

        // Hourly buckets
        std::vector<HourlyData> hourly_data;
        HourlyData current_hour;

        // Oscillation detection
        double threshold = 1.0;
        double local_high = 0, local_low = DBL_MAX;
        double last_extreme = 0;
        bool looking_for_peak = true;

        // Tracking variables
        long tick_count = 0;
        int current_hour_num = -1;
        double hour_high = 0, hour_low = DBL_MAX;

        // Previous oscillation for autocorrelation
        std::vector<double> oscillation_amplitudes;
        std::vector<double> oscillation_durations;
        long last_osc_tick = 0;

        // Spread buckets for correlation
        std::map<int, std::pair<int, double>> spread_to_osc;  // spread_bucket -> (osc_count, total_ticks)

        Tick tick;
        while (manager.GetNextTick(tick)) {
            tick_count++;
            double mid = (tick.bid + tick.ask) / 2.0;
            double spread = tick.ask - tick.bid;

            // Parse hour
            int hour = 0;
            if (tick.timestamp.length() >= 13) {
                hour = std::stoi(tick.timestamp.substr(11, 2));
            }

            // Hour change
            if (hour != current_hour_num && current_hour_num >= 0) {
                current_hour.total_volatility = hour_high - hour_low;
                current_hour.avg_price = current_hour.price_sum / current_hour.tick_count;
                hourly_data.push_back(current_hour);

                current_hour = HourlyData();
                hour_high = mid;
                hour_low = mid;
            }
            current_hour_num = hour;

            // Update current hour
            current_hour.total_spread += spread;
            current_hour.tick_count++;
            current_hour.price_sum += mid;
            hour_high = std::max(hour_high, mid);
            hour_low = std::min(hour_low, mid);

            // Spread bucket (0.1 increments)
            int spread_bucket = (int)(spread * 10);
            if (spread_to_osc.find(spread_bucket) == spread_to_osc.end()) {
                spread_to_osc[spread_bucket] = {0, 0};
            }
            spread_to_osc[spread_bucket].second++;

            // Oscillation detection
            if (looking_for_peak) {
                if (mid > local_high) local_high = mid;
                if (mid < local_high - threshold && last_extreme > 0) {
                    double amp = local_high - last_extreme;
                    if (amp >= threshold) {
                        current_hour.oscillation_count++;
                        oscillation_amplitudes.push_back(amp);
                        oscillation_durations.push_back(tick_count - last_osc_tick);
                        last_osc_tick = tick_count;
                        spread_to_osc[spread_bucket].first++;
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
                        current_hour.oscillation_count++;
                        oscillation_amplitudes.push_back(amp);
                        oscillation_durations.push_back(tick_count - last_osc_tick);
                        last_osc_tick = tick_count;
                        spread_to_osc[spread_bucket].first++;
                    }
                    last_extreme = local_low;
                    local_high = mid;
                    looking_for_peak = true;
                }
            }

            if (tick_count % 10000000 == 0) {
                std::cout << "Processed " << tick_count / 1000000 << "M ticks, "
                          << hourly_data.size() << " hours..." << std::endl;
            }
        }

        // Save final hour
        if (current_hour.tick_count > 0) {
            current_hour.total_volatility = hour_high - hour_low;
            current_hour.avg_price = current_hour.price_sum / current_hour.tick_count;
            hourly_data.push_back(current_hour);
        }

        // Calculate correlations
        std::cout << "\n=== CORRELATION: SPREAD vs OSCILLATION FREQUENCY ===" << std::endl;

        std::vector<double> spreads, osc_rates;
        for (const auto& h : hourly_data) {
            if (h.tick_count > 10000) {  // Only hours with enough data
                spreads.push_back(h.total_spread / h.tick_count);
                osc_rates.push_back(h.oscillation_count);
            }
        }

        double corr_spread_osc = CalculateCorrelation(spreads, osc_rates);
        std::cout << "Correlation coefficient: " << std::fixed << std::setprecision(3)
                  << corr_spread_osc << std::endl;
        std::cout << "Interpretation: " << InterpretCorrelation(corr_spread_osc) << std::endl;

        // Spread bucket analysis
        std::cout << "\n=== SPREAD LEVEL vs OSCILLATION DENSITY ===" << std::endl;
        std::cout << std::setw(15) << "Spread Range"
                  << std::setw(15) << "Osc Count"
                  << std::setw(15) << "Tick Count"
                  << std::setw(15) << "Osc Rate" << std::endl;
        std::cout << std::string(60, '-') << std::endl;

        for (const auto& [bucket, data] : spread_to_osc) {
            if (data.second > 1000000) {  // Only buckets with significant data
                double rate = data.first / (data.second / 720000.0);  // Per hour
                std::cout << std::setw(7) << "$" << std::setprecision(2) << (bucket * 0.1)
                          << "-$" << ((bucket + 1) * 0.1)
                          << std::setw(15) << data.first
                          << std::setw(15) << (long)(data.second / 1000000) << "M"
                          << std::setw(14) << std::setprecision(1) << rate << "/hr" << std::endl;
            }
        }

        // Volatility correlation
        std::cout << "\n=== CORRELATION: VOLATILITY vs OSCILLATION ===" << std::endl;

        std::vector<double> volatilities;
        osc_rates.clear();
        for (const auto& h : hourly_data) {
            if (h.tick_count > 10000 && h.total_volatility > 0) {
                volatilities.push_back(h.total_volatility);
                osc_rates.push_back(h.oscillation_count);
            }
        }

        double corr_vol_osc = CalculateCorrelation(volatilities, osc_rates);
        std::cout << "Correlation coefficient: " << corr_vol_osc << std::endl;
        std::cout << "Interpretation: " << InterpretCorrelation(corr_vol_osc) << std::endl;

        // Price level correlation
        std::cout << "\n=== CORRELATION: PRICE LEVEL vs OSCILLATION ===" << std::endl;

        std::vector<double> prices;
        osc_rates.clear();
        for (const auto& h : hourly_data) {
            if (h.tick_count > 10000) {
                prices.push_back(h.avg_price);
                osc_rates.push_back(h.oscillation_count);
            }
        }

        double corr_price_osc = CalculateCorrelation(prices, osc_rates);
        std::cout << "Correlation coefficient: " << corr_price_osc << std::endl;
        std::cout << "Interpretation: " << InterpretCorrelation(corr_price_osc) << std::endl;

        // Autocorrelation of oscillations
        std::cout << "\n=== AUTOCORRELATION: OSCILLATION AMPLITUDE ===" << std::endl;

        if (oscillation_amplitudes.size() > 100) {
            std::vector<double> current, lagged;
            for (size_t i = 1; i < oscillation_amplitudes.size(); i++) {
                current.push_back(oscillation_amplitudes[i]);
                lagged.push_back(oscillation_amplitudes[i-1]);
            }
            double autocorr = CalculateCorrelation(current, lagged);
            std::cout << "Lag-1 autocorrelation: " << autocorr << std::endl;
            std::cout << "Interpretation: " << (std::abs(autocorr) > 0.3 ? "PREDICTABLE" : "RANDOM") << std::endl;
        }

        // Summary
        std::cout << "\n=== CORRELATION SUMMARY ===" << std::endl;
        std::cout << std::setw(30) << "Relationship"
                  << std::setw(15) << "Correlation"
                  << std::setw(20) << "Trading Implication" << std::endl;
        std::cout << std::string(65, '-') << std::endl;

        auto PrintSummaryRow = [](const char* name, double corr, const char* implication) {
            std::cout << std::setw(30) << name
                      << std::setw(15) << std::setprecision(3) << corr
                      << std::setw(20) << implication << std::endl;
        };

        PrintSummaryRow("Spread <-> Oscillation", corr_spread_osc,
                        corr_spread_osc < -0.2 ? "Avoid wide spread" : "No filter needed");
        PrintSummaryRow("Volatility <-> Oscillation", corr_vol_osc,
                        corr_vol_osc > 0.3 ? "Trade high vol" : "Vol neutral");
        PrintSummaryRow("Price Level <-> Oscillation", corr_price_osc,
                        std::abs(corr_price_osc) > 0.2 ? "Price matters" : "Price neutral");

        // Recommendations
        std::cout << "\n=== TRADING RECOMMENDATIONS FROM CORRELATIONS ===" << std::endl;

        if (corr_spread_osc < -0.3) {
            std::cout << "1. SPREAD FILTER: Avoid trading when spread > 2x average" << std::endl;
        } else {
            std::cout << "1. SPREAD FILTER: Not strongly beneficial" << std::endl;
        }

        if (corr_vol_osc > 0.3) {
            std::cout << "2. VOLATILITY FILTER: Increase activity in high volatility" << std::endl;
        } else {
            std::cout << "2. VOLATILITY FILTER: No clear benefit" << std::endl;
        }
    }

private:
    double CalculateCorrelation(const std::vector<double>& x, const std::vector<double>& y) {
        if (x.size() != y.size() || x.empty()) return 0;

        double mean_x = 0, mean_y = 0;
        for (size_t i = 0; i < x.size(); i++) {
            mean_x += x[i];
            mean_y += y[i];
        }
        mean_x /= x.size();
        mean_y /= y.size();

        double cov = 0, var_x = 0, var_y = 0;
        for (size_t i = 0; i < x.size(); i++) {
            double dx = x[i] - mean_x;
            double dy = y[i] - mean_y;
            cov += dx * dy;
            var_x += dx * dx;
            var_y += dy * dy;
        }

        if (var_x == 0 || var_y == 0) return 0;
        return cov / std::sqrt(var_x * var_y);
    }

    const char* InterpretCorrelation(double r) {
        double abs_r = std::abs(r);
        if (abs_r < 0.1) return "No correlation";
        if (abs_r < 0.3) return "Weak correlation";
        if (abs_r < 0.5) return "Moderate correlation";
        if (abs_r < 0.7) return "Strong correlation";
        return "Very strong correlation";
    }
};

int main() {
    CorrelationAnalyzer analyzer;
    analyzer.Analyze();
    return 0;
}
