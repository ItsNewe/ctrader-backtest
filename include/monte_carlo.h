#ifndef MONTE_CARLO_H
#define MONTE_CARLO_H

#include "tick_based_engine.h"
#include <vector>
#include <random>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <thread>
#include <mutex>
#include <atomic>
#include <iostream>
#include <iomanip>

namespace backtest {

/**
 * Monte Carlo Simulation Framework
 *
 * A single backtest shows ONE path through market history.
 * Monte Carlo shows the DISTRIBUTION of possible outcomes by:
 * - Shuffling trade order (tests sequence dependency)
 * - Skipping random trades (tests robustness to missed entries)
 * - Adding random slippage (tests sensitivity to execution)
 * - Bootstrap sampling (tests statistical significance)
 */

enum class MonteCarloMode {
    SHUFFLE_TRADES,     // Randomize trade order
    SKIP_TRADES,        // Randomly skip X% of trades
    VARY_SLIPPAGE,      // Add random slippage to entries/exits
    BOOTSTRAP,          // Sample trades with replacement
    COMBINED            // Multiple effects combined
};

struct MonteCarloConfig {
    int num_simulations = 1000;
    MonteCarloMode mode = MonteCarloMode::SHUFFLE_TRADES;

    // For SKIP_TRADES mode
    double skip_probability = 0.10;  // Skip 10% of trades

    // For VARY_SLIPPAGE mode
    double slippage_stddev_points = 0.5;  // Stddev of slippage in price points

    // For BOOTSTRAP mode
    double sample_ratio = 1.0;  // 1.0 = same number of trades as original

    // For COMBINED mode
    bool enable_shuffle = true;
    bool enable_skip = false;
    bool enable_slippage = true;

    // Threading
    int max_parallel_workers = 0;  // 0 = auto
};

struct MonteCarloResult {
    // Distribution statistics
    double profit_mean;
    double profit_median;
    double profit_stddev;
    double profit_5th_percentile;   // Worst realistic case
    double profit_25th_percentile;
    double profit_75th_percentile;
    double profit_95th_percentile;  // Best realistic case

    double max_dd_mean;
    double max_dd_median;
    double max_dd_95th_percentile;  // Worst realistic DD

    double probability_of_loss;     // % of simulations with negative profit
    double probability_below_half;  // % below 50% of original profit

    // Original backtest for comparison
    double original_profit;
    double original_max_dd;

    // Full distributions (for plotting)
    std::vector<double> profit_distribution;
    std::vector<double> drawdown_distribution;

    // Confidence assessment
    std::string confidence_level;   // "HIGH", "MEDIUM", "LOW"
    double confidence_score;        // 0-100
};

/**
 * Simulated trade for Monte Carlo analysis
 */
struct SimTrade {
    double profit_loss;
    double entry_price;
    double exit_price;
    double lot_size;
    bool is_buy;

    // For slippage simulation
    double original_profit;
};

/**
 * Monte Carlo Simulator
 *
 * Takes a completed backtest's trade list and runs simulations
 */
class MonteCarloSimulator {
public:
    explicit MonteCarloSimulator(const MonteCarloConfig& config)
        : config_(config), rng_(std::random_device{}()) {}

    /**
     * Run Monte Carlo simulation on completed trades
     */
    MonteCarloResult Run(const std::vector<Trade>& trades, double initial_balance, bool verbose = true) {
        MonteCarloResult result;

        if (trades.empty()) {
            std::cerr << "Error: No trades for Monte Carlo simulation" << std::endl;
            return result;
        }

        // Convert to SimTrades
        std::vector<SimTrade> sim_trades;
        for (const auto& t : trades) {
            SimTrade st;
            st.profit_loss = t.profit_loss;
            st.entry_price = t.entry_price;
            st.exit_price = t.exit_price;
            st.lot_size = t.lot_size;
            st.is_buy = (t.direction == "BUY");
            st.original_profit = t.profit_loss;
            sim_trades.push_back(st);
        }

        // Calculate original metrics
        result.original_profit = std::accumulate(sim_trades.begin(), sim_trades.end(), 0.0,
            [](double sum, const SimTrade& t) { return sum + t.profit_loss; });

        auto [orig_dd, orig_dd_pct] = CalculateMaxDrawdown(sim_trades, initial_balance);
        result.original_max_dd = orig_dd_pct;

        if (verbose) {
            std::cout << "\n=== Monte Carlo Simulation ===" << std::endl;
            std::cout << "Mode: " << GetModeName(config_.mode) << std::endl;
            std::cout << "Simulations: " << config_.num_simulations << std::endl;
            std::cout << "Original trades: " << trades.size() << std::endl;
            std::cout << "Original profit: $" << std::fixed << std::setprecision(2) << result.original_profit << std::endl;
            std::cout << "Original max DD: " << result.original_max_dd << "%" << std::endl;
        }

        // Run simulations in parallel
        result.profit_distribution.resize(config_.num_simulations);
        result.drawdown_distribution.resize(config_.num_simulations);

        int num_workers = config_.max_parallel_workers > 0
            ? config_.max_parallel_workers
            : std::thread::hardware_concurrency();

        std::atomic<int> completed{0};
        std::vector<std::thread> workers;

        int sims_per_worker = config_.num_simulations / num_workers;
        int remainder = config_.num_simulations % num_workers;

        int start_idx = 0;
        for (int w = 0; w < num_workers; w++) {
            int count = sims_per_worker + (w < remainder ? 1 : 0);
            workers.emplace_back([this, &sim_trades, &result, initial_balance, start_idx, count, &completed, verbose]() {
                SimWorker(sim_trades, result, initial_balance, start_idx, count, completed, verbose);
            });
            start_idx += count;
        }

        for (auto& w : workers) {
            w.join();
        }

        // Calculate statistics
        CalculateStatistics(result, verbose);

        return result;
    }

private:
    MonteCarloConfig config_;
    std::mt19937 rng_;
    std::mutex result_mutex_;

    void SimWorker(
        const std::vector<SimTrade>& original_trades,
        MonteCarloResult& result,
        double initial_balance,
        int start_idx,
        int count,
        std::atomic<int>& completed,
        bool verbose
    ) {
        // Thread-local RNG
        std::mt19937 local_rng(std::random_device{}());

        for (int i = 0; i < count; i++) {
            int sim_idx = start_idx + i;

            // Generate simulated trades based on mode
            std::vector<SimTrade> sim_trades = GenerateSimulation(original_trades, local_rng);

            // Calculate metrics
            double profit = std::accumulate(sim_trades.begin(), sim_trades.end(), 0.0,
                [](double sum, const SimTrade& t) { return sum + t.profit_loss; });

            auto [dd, dd_pct] = CalculateMaxDrawdown(sim_trades, initial_balance);

            // Store results (thread-safe)
            {
                std::lock_guard<std::mutex> lock(result_mutex_);
                result.profit_distribution[sim_idx] = profit;
                result.drawdown_distribution[sim_idx] = dd_pct;
            }

            int done = ++completed;
            if (verbose && (done % 100 == 0 || done == config_.num_simulations)) {
                std::cout << "\r  Simulating: " << done << "/" << config_.num_simulations << " ("
                          << std::fixed << std::setprecision(1) << (100.0 * done / config_.num_simulations) << "%)"
                          << std::string(10, ' ') << std::flush;
            }
        }
    }

    std::vector<SimTrade> GenerateSimulation(const std::vector<SimTrade>& original, std::mt19937& rng) {
        std::vector<SimTrade> result;

        switch (config_.mode) {
            case MonteCarloMode::SHUFFLE_TRADES:
                result = original;
                std::shuffle(result.begin(), result.end(), rng);
                break;

            case MonteCarloMode::SKIP_TRADES: {
                std::uniform_real_distribution<> dist(0.0, 1.0);
                for (const auto& t : original) {
                    if (dist(rng) >= config_.skip_probability) {
                        result.push_back(t);
                    }
                }
                break;
            }

            case MonteCarloMode::VARY_SLIPPAGE: {
                std::normal_distribution<> slip_dist(0.0, config_.slippage_stddev_points);
                for (auto t : original) {
                    double slip = slip_dist(rng);
                    // Adverse slippage: reduces profit
                    t.profit_loss -= std::abs(slip) * t.lot_size * 100;  // Assuming contract_size=100
                    result.push_back(t);
                }
                break;
            }

            case MonteCarloMode::BOOTSTRAP: {
                std::uniform_int_distribution<> idx_dist(0, original.size() - 1);
                int sample_size = static_cast<int>(original.size() * config_.sample_ratio);
                for (int i = 0; i < sample_size; i++) {
                    result.push_back(original[idx_dist(rng)]);
                }
                break;
            }

            case MonteCarloMode::COMBINED: {
                result = original;

                // Shuffle
                if (config_.enable_shuffle) {
                    std::shuffle(result.begin(), result.end(), rng);
                }

                // Skip
                if (config_.enable_skip) {
                    std::uniform_real_distribution<> dist(0.0, 1.0);
                    std::vector<SimTrade> filtered;
                    for (const auto& t : result) {
                        if (dist(rng) >= config_.skip_probability) {
                            filtered.push_back(t);
                        }
                    }
                    result = filtered;
                }

                // Slippage
                if (config_.enable_slippage) {
                    std::normal_distribution<> slip_dist(0.0, config_.slippage_stddev_points);
                    for (auto& t : result) {
                        double slip = slip_dist(rng);
                        t.profit_loss -= std::abs(slip) * t.lot_size * 100;
                    }
                }
                break;
            }
        }

        return result;
    }

    std::pair<double, double> CalculateMaxDrawdown(const std::vector<SimTrade>& trades, double initial_balance) {
        double balance = initial_balance;
        double peak = initial_balance;
        double max_dd = 0;
        double max_dd_pct = 0;

        for (const auto& t : trades) {
            balance += t.profit_loss;
            if (balance > peak) {
                peak = balance;
            }
            double dd = peak - balance;
            if (dd > max_dd) {
                max_dd = dd;
                max_dd_pct = (peak > 0) ? (dd / peak) * 100.0 : 0;
            }
        }

        return {max_dd, max_dd_pct};
    }

    void CalculateStatistics(MonteCarloResult& result, bool verbose) {
        auto& profits = result.profit_distribution;
        auto& dds = result.drawdown_distribution;

        // Sort for percentiles
        std::vector<double> sorted_profits = profits;
        std::vector<double> sorted_dds = dds;
        std::sort(sorted_profits.begin(), sorted_profits.end());
        std::sort(sorted_dds.begin(), sorted_dds.end());

        int n = profits.size();

        // Profit statistics
        result.profit_mean = std::accumulate(profits.begin(), profits.end(), 0.0) / n;

        double sum_sq = 0;
        for (double p : profits) {
            sum_sq += (p - result.profit_mean) * (p - result.profit_mean);
        }
        result.profit_stddev = std::sqrt(sum_sq / n);

        result.profit_median = sorted_profits[n / 2];
        result.profit_5th_percentile = sorted_profits[static_cast<int>(n * 0.05)];
        result.profit_25th_percentile = sorted_profits[static_cast<int>(n * 0.25)];
        result.profit_75th_percentile = sorted_profits[static_cast<int>(n * 0.75)];
        result.profit_95th_percentile = sorted_profits[static_cast<int>(n * 0.95)];

        // Drawdown statistics
        result.max_dd_mean = std::accumulate(dds.begin(), dds.end(), 0.0) / n;
        result.max_dd_median = sorted_dds[n / 2];
        result.max_dd_95th_percentile = sorted_dds[static_cast<int>(n * 0.95)];

        // Probability metrics
        int loss_count = std::count_if(profits.begin(), profits.end(),
            [](double p) { return p < 0; });
        result.probability_of_loss = 100.0 * loss_count / n;

        double half_original = result.original_profit / 2;
        int below_half_count = std::count_if(profits.begin(), profits.end(),
            [half_original](double p) { return p < half_original; });
        result.probability_below_half = 100.0 * below_half_count / n;

        // Confidence assessment
        // Based on: probability of loss, profit variance, and comparison to original
        double loss_score = std::max(0.0, 100.0 - result.probability_of_loss * 5);  // Max 100 if <1% loss prob
        double variance_score = std::max(0.0, 100.0 - (result.profit_stddev / std::abs(result.original_profit + 1)) * 50);
        double median_score = (result.profit_median >= result.original_profit * 0.8) ? 100 : (result.profit_median / result.original_profit * 100);

        result.confidence_score = (loss_score + variance_score + median_score) / 3;

        if (result.confidence_score >= 80) {
            result.confidence_level = "HIGH";
        } else if (result.confidence_score >= 60) {
            result.confidence_level = "MEDIUM";
        } else {
            result.confidence_level = "LOW";
        }

        if (verbose) {
            std::cout << "\n\n=== Monte Carlo Results ===" << std::endl;
            std::cout << "Original profit: $" << std::fixed << std::setprecision(2) << result.original_profit << std::endl;
            std::cout << std::endl;

            std::cout << "Profit Distribution:" << std::endl;
            std::cout << "  5th percentile:  $" << result.profit_5th_percentile << " (worst realistic)" << std::endl;
            std::cout << "  25th percentile: $" << result.profit_25th_percentile << std::endl;
            std::cout << "  Median:          $" << result.profit_median << std::endl;
            std::cout << "  Mean:            $" << result.profit_mean << std::endl;
            std::cout << "  75th percentile: $" << result.profit_75th_percentile << std::endl;
            std::cout << "  95th percentile: $" << result.profit_95th_percentile << " (best realistic)" << std::endl;
            std::cout << "  Std Dev:         $" << result.profit_stddev << std::endl;
            std::cout << std::endl;

            std::cout << "Max Drawdown Distribution:" << std::endl;
            std::cout << "  Median:          " << result.max_dd_median << "%" << std::endl;
            std::cout << "  Mean:            " << result.max_dd_mean << "%" << std::endl;
            std::cout << "  95th percentile: " << result.max_dd_95th_percentile << "% (worst realistic)" << std::endl;
            std::cout << std::endl;

            std::cout << "Risk Metrics:" << std::endl;
            std::cout << "  Probability of loss:       " << std::setprecision(1) << result.probability_of_loss << "%" << std::endl;
            std::cout << "  Probability below 50%:     " << result.probability_below_half << "%" << std::endl;
            std::cout << std::endl;

            std::cout << "CONFIDENCE: " << result.confidence_level
                      << " (" << std::setprecision(0) << result.confidence_score << "/100)" << std::endl;

            if (result.confidence_level == "HIGH") {
                std::cout << "Assessment: Strategy is ROBUST to " << GetModeName(config_.mode) << std::endl;
            } else if (result.confidence_level == "MEDIUM") {
                std::cout << "Assessment: Strategy has MODERATE sensitivity to " << GetModeName(config_.mode) << std::endl;
            } else {
                std::cout << "Assessment: Strategy is SENSITIVE to " << GetModeName(config_.mode) << " - use caution" << std::endl;
            }
        }
    }

    std::string GetModeName(MonteCarloMode mode) const {
        switch (mode) {
            case MonteCarloMode::SHUFFLE_TRADES: return "trade sequence shuffling";
            case MonteCarloMode::SKIP_TRADES: return "random trade skipping";
            case MonteCarloMode::VARY_SLIPPAGE: return "slippage variation";
            case MonteCarloMode::BOOTSTRAP: return "bootstrap sampling";
            case MonteCarloMode::COMBINED: return "combined effects";
        }
        return "unknown";
    }
};

/**
 * Convenience function to run Monte Carlo on a completed backtest
 */
inline MonteCarloResult RunMonteCarlo(
    const std::vector<Trade>& trades,
    double initial_balance,
    int num_simulations = 1000,
    MonteCarloMode mode = MonteCarloMode::SHUFFLE_TRADES,
    bool verbose = true
) {
    MonteCarloConfig config;
    config.num_simulations = num_simulations;
    config.mode = mode;

    MonteCarloSimulator sim(config);
    return sim.Run(trades, initial_balance, verbose);
}

} // namespace backtest

#endif // MONTE_CARLO_H
