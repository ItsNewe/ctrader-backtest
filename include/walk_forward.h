#ifndef WALK_FORWARD_H
#define WALK_FORWARD_H

#include "tick_based_engine.h"
#include <vector>
#include <string>
#include <functional>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <tuple>

namespace backtest {

/**
 * Walk-Forward Optimization Framework
 *
 * Prevents overfitting by:
 * 1. Optimizing parameters on in-sample (IS) window
 * 2. Testing on out-of-sample (OOS) window
 * 3. Rolling windows forward through time
 * 4. Combining OOS results to assess true performance
 */

struct WalkForwardConfig {
    int optimization_window_days = 180;  // IS window size
    int test_window_days = 90;           // OOS window size
    int step_days = 90;                  // How much to slide forward
    int max_parallel_workers = 0;        // 0 = auto (hardware_concurrency)

    // Optional anchored mode: IS always starts from beginning
    bool anchored = false;
};

struct WindowResult {
    std::string is_start;
    std::string is_end;
    std::string oos_start;
    std::string oos_end;

    // In-sample performance (best config)
    double is_profit;
    double is_max_dd_pct;
    double is_trades;

    // Out-of-sample performance (using IS-optimal params)
    double oos_profit;
    double oos_max_dd_pct;
    double oos_trades;

    // Best parameters found in IS
    std::vector<double> optimal_params;
    std::string optimal_param_name;

    // Robustness metrics
    double efficiency_ratio;  // OOS_profit / IS_profit
};

struct WalkForwardResult {
    std::vector<WindowResult> windows;

    // Combined OOS metrics
    double total_oos_profit;
    double oos_max_dd_pct;
    double oos_trades;

    // Robustness metrics
    double avg_efficiency_ratio;      // Mean(OOS/IS profit)
    double param_stability;           // How much params changed between windows
    double robustness_score;          // Combined metric (0-100)

    // Comparison vs full optimization
    double full_period_profit;        // If we optimized on entire period
    double walk_forward_ratio;        // OOS_profit / full_period_profit
};

/**
 * Parameter range for optimization
 */
struct ParamRange {
    std::string name;
    double start;
    double end;
    double step;

    std::vector<double> GetValues() const {
        std::vector<double> values;
        for (double v = start; v <= end + 1e-9; v += step) {
            values.push_back(v);
        }
        return values;
    }

    size_t GetCount() const {
        return static_cast<size_t>((end - start) / step + 1);
    }
};

/**
 * Fitness function for optimization
 */
enum class FitnessMetric {
    PROFIT,              // Raw profit
    PROFIT_FACTOR,       // Gross profit / Gross loss
    SHARPE,              // Risk-adjusted return
    CALMAR,              // Return / Max drawdown
    RETURN_DD_RATIO      // Return multiple / DD%
};

/**
 * Walk-Forward Optimizer
 *
 * Template parameter StrategyT must implement:
 * - StrategyT(const std::vector<double>& params)
 * - void OnTick(const Tick& tick, TickBasedEngine& engine)
 */
template<typename StrategyT>
class WalkForwardOptimizer {
public:
    using ParamFactory = std::function<StrategyT(const std::vector<double>& params)>;
    using FitnessFn = std::function<double(const TickBasedEngine::BacktestResults& results)>;

    WalkForwardOptimizer(
        const WalkForwardConfig& config,
        const std::vector<ParamRange>& param_ranges,
        const TickBacktestConfig& engine_config,
        ParamFactory factory
    ) : config_(config),
        param_ranges_(param_ranges),
        engine_config_(engine_config),
        factory_(factory) {

        // Default fitness: return/DD ratio
        fitness_fn_ = [](const TickBasedEngine::BacktestResults& r) {
            double dd = std::max(r.max_drawdown_pct, 1.0);
            double ret = r.final_balance / r.initial_balance;
            return ret / (dd / 100.0);
        };
    }

    void SetFitnessFunction(FitnessFn fn) {
        fitness_fn_ = fn;
    }

    void SetFitnessMetric(FitnessMetric metric) {
        switch (metric) {
            case FitnessMetric::PROFIT:
                fitness_fn_ = [](const TickBasedEngine::BacktestResults& r) {
                    return r.total_profit_loss;
                };
                break;
            case FitnessMetric::CALMAR:
                fitness_fn_ = [](const TickBasedEngine::BacktestResults& r) {
                    double dd = std::max(r.max_drawdown, 1.0);
                    return r.total_profit_loss / dd;
                };
                break;
            case FitnessMetric::RETURN_DD_RATIO:
            default:
                fitness_fn_ = [](const TickBasedEngine::BacktestResults& r) {
                    double dd = std::max(r.max_drawdown_pct, 1.0);
                    double ret = r.final_balance / r.initial_balance;
                    return ret / (dd / 100.0);
                };
                break;
        }
    }

    /**
     * Run walk-forward optimization
     * Ticks must cover the entire date range
     */
    WalkForwardResult Run(const std::vector<Tick>& ticks, bool verbose = true) {
        WalkForwardResult result;

        // Determine date range from ticks
        std::string first_date = ticks.front().timestamp.substr(0, 10);
        std::string last_date = ticks.back().timestamp.substr(0, 10);

        if (verbose) {
            std::cout << "\n=== Walk-Forward Optimization ===" << std::endl;
            std::cout << "Data range: " << first_date << " to " << last_date << std::endl;
            std::cout << "IS window: " << config_.optimization_window_days << " days" << std::endl;
            std::cout << "OOS window: " << config_.test_window_days << " days" << std::endl;
            std::cout << "Step: " << config_.step_days << " days" << std::endl;
        }

        // Generate all parameter combinations
        auto param_combos = GenerateParamCombinations();
        if (verbose) {
            std::cout << "Parameter combinations: " << param_combos.size() << std::endl;
        }

        // Generate windows
        auto windows = GenerateWindows(first_date, last_date);
        if (verbose) {
            std::cout << "Walk-forward windows: " << windows.size() << std::endl;
        }

        if (windows.empty()) {
            std::cerr << "Error: Not enough data for walk-forward" << std::endl;
            return result;
        }

        // Process each window
        for (size_t w = 0; w < windows.size(); w++) {
            std::string is_start = std::get<0>(windows[w]);
            std::string is_end = std::get<1>(windows[w]);
            std::string oos_start = std::get<2>(windows[w]);
            std::string oos_end = std::get<3>(windows[w]);

            if (verbose) {
                std::cout << "\n--- Window " << (w+1) << "/" << windows.size() << " ---" << std::endl;
                std::cout << "IS: " << is_start << " to " << is_end << std::endl;
                std::cout << "OOS: " << oos_start << " to " << oos_end << std::endl;
            }

            // Filter ticks for IS period
            std::vector<Tick> is_ticks;
            std::vector<Tick> oos_ticks;

            for (const auto& tick : ticks) {
                std::string date = tick.timestamp.substr(0, 10);
                if (date >= is_start && date < is_end) {
                    is_ticks.push_back(tick);
                }
                if (date >= oos_start && date < oos_end) {
                    oos_ticks.push_back(tick);
                }
            }

            if (verbose) {
                std::cout << "IS ticks: " << is_ticks.size() << ", OOS ticks: " << oos_ticks.size() << std::endl;
            }

            // Optimize on IS period (parallel)
            std::vector<double> best_params;
            double best_fitness;
            TickBasedEngine::BacktestResults is_results;
            OptimizeOnPeriod(is_ticks, param_combos, verbose, best_params, best_fitness, is_results);

            // Test on OOS period with best params
            TickBasedEngine::BacktestResults oos_results = RunBacktestWithParams(oos_ticks, best_params);

            // Record window result
            WindowResult wr;
            wr.is_start = is_start;
            wr.is_end = is_end;
            wr.oos_start = oos_start;
            wr.oos_end = oos_end;
            wr.is_profit = is_results.total_profit_loss;
            wr.is_max_dd_pct = is_results.max_drawdown_pct;
            wr.is_trades = is_results.total_trades;
            wr.oos_profit = oos_results.total_profit_loss;
            wr.oos_max_dd_pct = oos_results.max_drawdown_pct;
            wr.oos_trades = oos_results.total_trades;
            wr.optimal_params = best_params;
            wr.optimal_param_name = FormatParams(best_params);
            wr.efficiency_ratio = (is_results.total_profit_loss > 0)
                ? oos_results.total_profit_loss / is_results.total_profit_loss
                : 0.0;

            result.windows.push_back(wr);

            if (verbose) {
                std::cout << "Best params: " << wr.optimal_param_name << std::endl;
                std::cout << "IS profit: $" << std::fixed << std::setprecision(2) << wr.is_profit
                          << " (DD: " << wr.is_max_dd_pct << "%)" << std::endl;
                std::cout << "OOS profit: $" << wr.oos_profit
                          << " (DD: " << wr.oos_max_dd_pct << "%)" << std::endl;
                std::cout << "Efficiency: " << std::setprecision(1) << (wr.efficiency_ratio * 100) << "%" << std::endl;
            }
        }

        // Calculate aggregate metrics
        CalculateAggregateMetrics(result, ticks, param_combos, verbose);

        return result;
    }

private:
    WalkForwardConfig config_;
    std::vector<ParamRange> param_ranges_;
    TickBacktestConfig engine_config_;
    ParamFactory factory_;
    FitnessFn fitness_fn_;

    // Thread-safe work queue for parallel optimization
    std::mutex queue_mutex_;
    std::mutex results_mutex_;
    std::queue<std::vector<double>> work_queue_;
    std::vector<std::pair<std::vector<double>, double>> opt_results_;
    std::atomic<int> completed_{0};

    /**
     * Run backtest with specific parameters
     */
    TickBasedEngine::BacktestResults RunBacktestWithParams(const std::vector<Tick>& ticks, const std::vector<double>& params) {
        TickBacktestConfig cfg = engine_config_;
        cfg.verbose = false;

        TickBasedEngine engine(cfg);
        StrategyT strategy = factory_(params);

        engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        return engine.GetResults();
    }

    /**
     * Generate all parameter combinations
     */
    std::vector<std::vector<double>> GenerateParamCombinations() const {
        std::vector<std::vector<double>> result;
        std::vector<std::vector<double>> param_values;

        for (const auto& range : param_ranges_) {
            param_values.push_back(range.GetValues());
        }

        // Cartesian product
        std::vector<double> current(param_values.size());
        GenerateCombosRecursive(param_values, 0, current, result);

        return result;
    }

    void GenerateCombosRecursive(
        const std::vector<std::vector<double>>& values,
        size_t index,
        std::vector<double>& current,
        std::vector<std::vector<double>>& result
    ) const {
        if (index == values.size()) {
            result.push_back(current);
            return;
        }
        for (double v : values[index]) {
            current[index] = v;
            GenerateCombosRecursive(values, index + 1, current, result);
        }
    }

    /**
     * Generate walk-forward windows
     */
    std::vector<std::tuple<std::string, std::string, std::string, std::string>>
    GenerateWindows(const std::string& first_date, const std::string& last_date) const {
        std::vector<std::tuple<std::string, std::string, std::string, std::string>> windows;

        std::string is_start = first_date;

        while (true) {
            std::string is_end = AddDays(is_start, config_.optimization_window_days);
            std::string oos_start = is_end;
            std::string oos_end = AddDays(oos_start, config_.test_window_days);

            // Check if we have enough data
            if (oos_end > last_date) break;

            windows.emplace_back(is_start, is_end, oos_start, oos_end);

            if (config_.anchored) {
                // Anchored: IS start stays at beginning, IS end expands
                is_start = first_date;
            }

            // Slide forward
            is_start = AddDays(is_start, config_.step_days);
        }

        return windows;
    }

    /**
     * Add days to a date string (YYYY.MM.DD format)
     */
    std::string AddDays(const std::string& date, int days) const {
        int year = std::stoi(date.substr(0, 4));
        int month = std::stoi(date.substr(5, 2));
        int day = std::stoi(date.substr(8, 2));

        // Simple day addition (handles month/year rollover)
        static const int days_in_month[] = {31,28,31,30,31,30,31,31,30,31,30,31};

        day += days;
        while (day > days_in_month[month-1]) {
            // Leap year check for February
            int feb_days = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 29 : 28;
            int dim = (month == 2) ? feb_days : days_in_month[month-1];

            if (day > dim) {
                day -= dim;
                month++;
                if (month > 12) {
                    month = 1;
                    year++;
                }
            } else {
                break;
            }
        }

        std::ostringstream oss;
        oss << year << "."
            << std::setfill('0') << std::setw(2) << month << "."
            << std::setfill('0') << std::setw(2) << day;
        return oss.str();
    }

    /**
     * Optimize on a specific period using parallel workers
     * Results returned via output parameters
     */
    void OptimizeOnPeriod(
        const std::vector<Tick>& ticks,
        const std::vector<std::vector<double>>& param_combos,
        bool verbose,
        std::vector<double>& out_best_params,
        double& out_best_fitness,
        TickBasedEngine::BacktestResults& out_best_results
    ) {
        // Clear previous state
        while (!work_queue_.empty()) work_queue_.pop();
        opt_results_.clear();
        completed_ = 0;

        // Fill work queue
        for (const auto& params : param_combos) {
            work_queue_.push(params);
        }

        int total = static_cast<int>(param_combos.size());

        // Spawn workers
        int num_workers = config_.max_parallel_workers > 0
            ? config_.max_parallel_workers
            : static_cast<int>(std::thread::hardware_concurrency());

        std::vector<std::thread> workers;
        for (int i = 0; i < num_workers; i++) {
            workers.emplace_back([this, &ticks, total, verbose]() {
                OptWorker(ticks, total, verbose);
            });
        }

        for (auto& w : workers) {
            w.join();
        }

        if (verbose) {
            std::cout << std::endl;  // Newline after progress
        }

        // Find best
        out_best_fitness = -1e99;

        for (const auto& pr : opt_results_) {
            if (pr.second > out_best_fitness) {
                out_best_fitness = pr.second;
                out_best_params = pr.first;
            }
        }

        // Re-run to get full results (small overhead)
        out_best_results = RunBacktestWithParams(ticks, out_best_params);
    }

    void OptWorker(const std::vector<Tick>& ticks, int total, bool verbose) {
        while (true) {
            std::vector<double> params;
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                if (work_queue_.empty()) return;
                params = work_queue_.front();
                work_queue_.pop();
            }

            TickBasedEngine::BacktestResults results = RunBacktestWithParams(ticks, params);
            double fitness = fitness_fn_(results);

            {
                std::lock_guard<std::mutex> lock(results_mutex_);
                opt_results_.emplace_back(params, fitness);
            }

            int done = ++completed_;
            if (verbose && (done % 50 == 0 || done == total)) {
                std::cout << "\r  Optimizing: " << done << "/" << total << " ("
                          << std::fixed << std::setprecision(1) << (100.0 * done / total) << "%)"
                          << std::string(10, ' ') << std::flush;
            }
        }
    }

    std::string FormatParams(const std::vector<double>& params) const {
        std::ostringstream oss;
        for (size_t i = 0; i < params.size(); i++) {
            if (i > 0) oss << ", ";
            if (i < param_ranges_.size()) {
                oss << param_ranges_[i].name << "=";
            }
            oss << std::fixed << std::setprecision(2) << params[i];
        }
        return oss.str();
    }

    void CalculateAggregateMetrics(
        WalkForwardResult& result,
        const std::vector<Tick>& ticks,
        const std::vector<std::vector<double>>& param_combos,
        bool verbose
    ) {
        // Sum OOS profits
        result.total_oos_profit = 0;
        result.oos_trades = 0;
        result.oos_max_dd_pct = 0;
        double sum_efficiency = 0;

        for (const auto& w : result.windows) {
            result.total_oos_profit += w.oos_profit;
            result.oos_trades += w.oos_trades;
            result.oos_max_dd_pct = std::max(result.oos_max_dd_pct, w.oos_max_dd_pct);
            sum_efficiency += w.efficiency_ratio;
        }

        result.avg_efficiency_ratio = result.windows.empty() ? 0 : sum_efficiency / result.windows.size();

        // Parameter stability: measure variance in optimal params across windows
        if (result.windows.size() > 1 && !result.windows[0].optimal_params.empty()) {
            std::vector<double> param_variances;
            size_t num_params = result.windows[0].optimal_params.size();

            for (size_t p = 0; p < num_params; p++) {
                double sum = 0, sum_sq = 0;
                for (const auto& w : result.windows) {
                    sum += w.optimal_params[p];
                    sum_sq += w.optimal_params[p] * w.optimal_params[p];
                }
                double mean = sum / result.windows.size();
                double variance = sum_sq / result.windows.size() - mean * mean;
                double range = param_ranges_[p].end - param_ranges_[p].start;
                // Normalize variance by parameter range
                param_variances.push_back(std::sqrt(variance) / (range + 1e-9));
            }

            double avg_variance = std::accumulate(param_variances.begin(), param_variances.end(), 0.0) / num_params;
            result.param_stability = 1.0 - std::min(avg_variance, 1.0);  // 1 = stable, 0 = unstable
        } else {
            result.param_stability = 0;
        }

        // Run full-period optimization for comparison
        if (verbose) {
            std::cout << "\nRunning full-period optimization for comparison..." << std::endl;
        }

        std::vector<double> full_params;
        double full_fitness;
        TickBasedEngine::BacktestResults full_results;
        OptimizeOnPeriod(ticks, param_combos, verbose, full_params, full_fitness, full_results);

        result.full_period_profit = full_results.total_profit_loss;
        result.walk_forward_ratio = (result.full_period_profit > 0)
            ? result.total_oos_profit / result.full_period_profit
            : 0;

        // Robustness score (0-100)
        // Combines: efficiency ratio, param stability, walk-forward ratio
        double efficiency_score = std::min(result.avg_efficiency_ratio, 1.0) * 40;  // Max 40 points
        double stability_score = result.param_stability * 30;                        // Max 30 points
        double wf_ratio_score = std::min(result.walk_forward_ratio, 1.0) * 30;      // Max 30 points
        result.robustness_score = efficiency_score + stability_score + wf_ratio_score;

        if (verbose) {
            std::cout << "\n=== Walk-Forward Summary ===" << std::endl;
            std::cout << "Windows completed: " << result.windows.size() << std::endl;
            std::cout << "Combined OOS profit: $" << std::fixed << std::setprecision(2) << result.total_oos_profit << std::endl;
            std::cout << "Combined OOS max DD: " << result.oos_max_dd_pct << "%" << std::endl;
            std::cout << "Average efficiency: " << std::setprecision(1) << (result.avg_efficiency_ratio * 100) << "%" << std::endl;
            std::cout << "Parameter stability: " << (result.param_stability * 100) << "%" << std::endl;
            std::cout << "Full-period profit: $" << std::setprecision(2) << result.full_period_profit << std::endl;
            std::cout << "Walk-forward ratio: " << std::setprecision(1) << (result.walk_forward_ratio * 100) << "%" << std::endl;
            std::cout << "ROBUSTNESS SCORE: " << std::setprecision(0) << result.robustness_score << "/100" << std::endl;

            if (result.robustness_score >= 70) {
                std::cout << "Assessment: ROBUST - Strategy likely to perform in live trading" << std::endl;
            } else if (result.robustness_score >= 50) {
                std::cout << "Assessment: MODERATE - Some overfitting risk, use caution" << std::endl;
            } else {
                std::cout << "Assessment: POOR - High overfitting risk, strategy may fail live" << std::endl;
            }
        }
    }
};

} // namespace backtest

#endif // WALK_FORWARD_H
