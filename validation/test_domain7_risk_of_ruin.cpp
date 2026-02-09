/**
 * Domain 7: Risk-of-Ruin Quantification
 *
 * Monte Carlo simulation to quantify:
 * - Probability of margin call within 1 year
 * - Expected drawdown distribution
 * - Confidence intervals for returns
 */

#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <random>
#include <algorithm>
#include <cmath>
#include <cfloat>

using namespace backtest;

struct SimulationResult {
    double final_equity;
    double max_drawdown_pct;
    bool margin_call;
    int days_to_margin_call;
};

class MonteCarloSimulator {
public:
    void RunSimulation(int num_simulations = 1000) {
        std::cout << "\n" << std::string(70, '=') << std::endl;
        std::cout << "RISK-OF-RUIN MONTE CARLO SIMULATION" << std::endl;
        std::cout << std::string(70, '=') << std::endl;

        // First, extract oscillation statistics from real data
        std::cout << "\nExtracting oscillation statistics from 2025 data..." << std::endl;
        ExtractOscillationStats();

        // Run simulations
        std::cout << "\nRunning " << num_simulations << " Monte Carlo simulations..." << std::endl;
        std::vector<SimulationResult> results;

        std::random_device rd;
        std::mt19937 gen(rd());

        for (int i = 0; i < num_simulations; i++) {
            results.push_back(SimulateOneYear(gen));
            if ((i + 1) % 100 == 0) {
                std::cout << "  Completed " << (i + 1) << " simulations..." << std::endl;
            }
        }

        // Analyze results
        AnalyzeResults(results);
    }

private:
    // Oscillation statistics from real data
    double mean_osc_amplitude_ = 1.5;
    double std_osc_amplitude_ = 0.8;
    double osc_per_day_ = 1200.0;
    double crash_probability_per_day_ = 0.002;  // ~0.5 crashes/year
    double mean_crash_magnitude_ = 8.0;  // 8% crash
    double std_crash_magnitude_ = 3.0;

    void ExtractOscillationStats() {
        TickDataConfig config;
        config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
        config.format = TickDataFormat::MT5_CSV;
        config.load_all_into_memory = false;

        TickDataManager manager(config);
        // Manager initializes automatically in constructor

        std::vector<double> amplitudes;
        double local_high = 0, local_low = DBL_MAX;
        double last_extreme = 0;
        bool looking_for_peak = true;
        double threshold = 1.0;
        long tick_count = 0;
        int daily_osc = 0;
        std::vector<int> daily_osc_counts;

        Tick tick;
        while (manager.GetNextTick(tick)) {
            tick_count++;
            double mid = (tick.bid + tick.ask) / 2.0;

            // Daily boundary (approx every 720000 * 24 = 17.28M ticks)
            if (tick_count % 17280000 == 0) {
                daily_osc_counts.push_back(daily_osc);
                daily_osc = 0;
            }

            if (looking_for_peak) {
                if (mid > local_high) local_high = mid;
                if (mid < local_high - threshold && last_extreme > 0) {
                    double amp = local_high - last_extreme;
                    if (amp >= threshold) {
                        amplitudes.push_back(amp);
                        daily_osc++;
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
                        daily_osc++;
                    }
                    last_extreme = local_low;
                    local_high = mid;
                    looking_for_peak = true;
                }
            }

            if (tick_count > 50000000) break;  // Sample ~1/3 of data
        }

        // Calculate statistics
        if (!amplitudes.empty()) {
            double sum = 0;
            for (double a : amplitudes) sum += a;
            mean_osc_amplitude_ = sum / amplitudes.size();

            double var = 0;
            for (double a : amplitudes) var += (a - mean_osc_amplitude_) * (a - mean_osc_amplitude_);
            std_osc_amplitude_ = std::sqrt(var / amplitudes.size());
        }

        if (!daily_osc_counts.empty()) {
            double sum = 0;
            for (int c : daily_osc_counts) sum += c;
            osc_per_day_ = sum / daily_osc_counts.size();
        }

        std::cout << "Extracted statistics:" << std::endl;
        std::cout << "  Mean oscillation amplitude: $" << std::fixed << std::setprecision(2)
                  << mean_osc_amplitude_ << std::endl;
        std::cout << "  Std oscillation amplitude: $" << std_osc_amplitude_ << std::endl;
        std::cout << "  Oscillations per day: " << std::setprecision(0) << osc_per_day_ << std::endl;
    }

    SimulationResult SimulateOneYear(std::mt19937& gen) {
        // Distributions
        std::normal_distribution<> osc_dist(mean_osc_amplitude_, std_osc_amplitude_);
        std::uniform_real_distribution<> uniform(0.0, 1.0);
        std::normal_distribution<> crash_dist(mean_crash_magnitude_, std_crash_magnitude_);

        SimulationResult result;
        result.margin_call = false;
        result.days_to_margin_call = -1;

        double equity = 10000.0;
        double peak_equity = equity;
        double max_dd_pct = 0;
        double price = 2800.0;  // Starting gold price

        // Strategy parameters
        double survive_pct = 13.0;
        double spacing = 1.0;
        double lot_size = 0.01;
        double contract_size = 100.0;
        double leverage = 500.0;

        // Simulate 252 trading days
        for (int day = 0; day < 252; day++) {
            // Check for crash (rare event)
            if (uniform(gen) < crash_probability_per_day_) {
                double crash_pct = std::max(3.0, crash_dist(gen));
                price *= (1.0 - crash_pct / 100.0);

                // Estimate positions that would be underwater
                int estimated_positions = (int)(crash_pct / 1.0 * 5);  // Rough estimate
                double loss_per_position = crash_pct / 100.0 * price * contract_size * lot_size;
                equity -= estimated_positions * loss_per_position;
            }

            // Normal daily oscillations
            int daily_osc = (int)(osc_per_day_ * (0.8 + uniform(gen) * 0.4));  // Vary ±20%

            for (int osc = 0; osc < daily_osc; osc++) {
                double amplitude = std::max(0.5, osc_dist(gen));

                // Profit from this oscillation (simplified)
                // Assumes we capture some fraction of oscillations
                double capture_rate = 0.3;  // Capture ~30% of oscillations
                if (uniform(gen) < capture_rate) {
                    double profit = (amplitude - spacing * 0.5) * contract_size * lot_size;
                    equity += std::max(0.0, profit);
                }

                // Random walk for price
                price += (uniform(gen) - 0.5) * amplitude;
            }

            // Swap cost per day (approximate)
            double swap_cost = 0.67 * 5;  // ~$0.67 per 0.01 lot, assume 5 avg positions
            equity -= swap_cost;

            // Track drawdown
            if (equity > peak_equity) peak_equity = equity;
            double dd_pct = (peak_equity - equity) / peak_equity * 100.0;
            if (dd_pct > max_dd_pct) max_dd_pct = dd_pct;

            // Check for margin call (simplified: equity < 20% of peak)
            if (equity < peak_equity * 0.1) {
                result.margin_call = true;
                result.days_to_margin_call = day;
                break;
            }
        }

        result.final_equity = equity;
        result.max_drawdown_pct = max_dd_pct;
        return result;
    }

    void AnalyzeResults(const std::vector<SimulationResult>& results) {
        // Count margin calls
        int margin_calls = 0;
        std::vector<int> days_to_ruin;
        std::vector<double> final_equities;
        std::vector<double> max_drawdowns;

        for (const auto& r : results) {
            if (r.margin_call) {
                margin_calls++;
                days_to_ruin.push_back(r.days_to_margin_call);
            }
            final_equities.push_back(r.final_equity);
            max_drawdowns.push_back(r.max_drawdown_pct);
        }

        std::sort(final_equities.begin(), final_equities.end());
        std::sort(max_drawdowns.begin(), max_drawdowns.end());

        std::cout << "\n" << std::string(70, '=') << std::endl;
        std::cout << "MONTE CARLO RESULTS (" << results.size() << " simulations)" << std::endl;
        std::cout << std::string(70, '=') << std::endl;

        std::cout << std::fixed << std::setprecision(2);

        // Risk of ruin
        double ruin_probability = 100.0 * margin_calls / results.size();
        std::cout << "\n=== RISK OF RUIN ===" << std::endl;
        std::cout << "Margin calls: " << margin_calls << " / " << results.size() << std::endl;
        std::cout << "Probability of ruin (1 year): " << ruin_probability << "%" << std::endl;

        if (!days_to_ruin.empty()) {
            std::sort(days_to_ruin.begin(), days_to_ruin.end());
            double avg_days = 0;
            for (int d : days_to_ruin) avg_days += d;
            avg_days /= days_to_ruin.size();
            std::cout << "Average days to margin call: " << std::setprecision(0) << avg_days << std::endl;
            std::cout << "Earliest margin call: day " << days_to_ruin.front() << std::endl;
        }

        // Return distribution
        std::cout << "\n=== RETURN DISTRIBUTION ===" << std::endl;
        double mean_equity = 0;
        for (double e : final_equities) mean_equity += e;
        mean_equity /= final_equities.size();
        double mean_return = (mean_equity - 10000) / 10000 * 100;

        std::cout << "Mean final equity: $" << std::setprecision(2) << mean_equity << std::endl;
        std::cout << "Mean return: " << mean_return << "%" << std::endl;
        std::cout << "5th percentile: $" << final_equities[final_equities.size() * 5 / 100] << std::endl;
        std::cout << "25th percentile: $" << final_equities[final_equities.size() / 4] << std::endl;
        std::cout << "Median: $" << final_equities[final_equities.size() / 2] << std::endl;
        std::cout << "75th percentile: $" << final_equities[final_equities.size() * 3 / 4] << std::endl;
        std::cout << "95th percentile: $" << final_equities[final_equities.size() * 95 / 100] << std::endl;

        // Drawdown distribution
        std::cout << "\n=== MAX DRAWDOWN DISTRIBUTION ===" << std::endl;
        double mean_dd = 0;
        for (double d : max_drawdowns) mean_dd += d;
        mean_dd /= max_drawdowns.size();

        std::cout << "Mean max drawdown: " << mean_dd << "%" << std::endl;
        std::cout << "Median max drawdown: " << max_drawdowns[max_drawdowns.size() / 2] << "%" << std::endl;
        std::cout << "90th percentile: " << max_drawdowns[max_drawdowns.size() * 9 / 10] << "%" << std::endl;
        std::cout << "95th percentile: " << max_drawdowns[max_drawdowns.size() * 95 / 100] << "%" << std::endl;
        std::cout << "99th percentile: " << max_drawdowns[max_drawdowns.size() * 99 / 100] << "%" << std::endl;

        // Confidence intervals
        std::cout << "\n=== CONFIDENCE INTERVALS ===" << std::endl;
        std::cout << "95% CI for final equity: [$" << final_equities[final_equities.size() * 25 / 1000]
                  << " - $" << final_equities[final_equities.size() * 975 / 1000] << "]" << std::endl;
        std::cout << "95% CI for max drawdown: [" << max_drawdowns[max_drawdowns.size() * 25 / 1000]
                  << "% - " << max_drawdowns[max_drawdowns.size() * 975 / 1000] << "%]" << std::endl;

        // Risk assessment
        std::cout << "\n=== RISK ASSESSMENT ===" << std::endl;
        if (ruin_probability < 1.0) {
            std::cout << "LOW RISK: <1% probability of ruin" << std::endl;
        } else if (ruin_probability < 5.0) {
            std::cout << "MODERATE RISK: 1-5% probability of ruin" << std::endl;
        } else if (ruin_probability < 20.0) {
            std::cout << "HIGH RISK: 5-20% probability of ruin" << std::endl;
        } else {
            std::cout << "EXTREME RISK: >20% probability of ruin" << std::endl;
        }
    }
};

int main() {
    MonteCarloSimulator sim;
    sim.RunSimulation(1000);
    return 0;
}
