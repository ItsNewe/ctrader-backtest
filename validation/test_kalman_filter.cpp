/**
 * KALMAN FILTER STRATEGY PARALLEL SWEEP
 *
 * Tests the Kalman Filter as a dynamic attractor for grid trading.
 * Compares against EMA-based Floating Attractor baseline.
 *
 * The Kalman Filter provides:
 *   1. Adaptive smoothing (gain adjusts to noise ratio)
 *   2. Uncertainty estimates (variance of estimate)
 *   3. Optimal linear estimator for Gaussian systems
 *
 * Key questions:
 *   1. Does Kalman-filtered attractor beat EMA attractor?
 *   2. Does uncertainty-based spacing help?
 *   3. What Q/R ratio works best?
 *   4. Is Kalman's adaptive smoothing beneficial?
 *
 * Pattern: Load tick data ONCE into shared memory, run parallel tests.
 */

#include "../include/strategy_kalman_filter.h"
#include "../include/strategy_floating_attractor.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <map>

using namespace backtest;

// ============================================================================
// Shared tick data - loaded ONCE, used by ALL threads
// ============================================================================
std::vector<Tick> g_shared_ticks_2025;
std::vector<Tick> g_shared_ticks_2024;

void LoadTickData(const std::string& path, std::vector<Tick>& ticks, const std::string& label) {
    std::cout << "Loading " << label << " tick data..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cout << "WARNING: Cannot open " << label << " tick file: " << path << std::endl;
        return;
    }

    std::string line;
    std::getline(file, line);  // Skip header

    ticks.reserve(52000000);

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        Tick tick;
        std::stringstream ss(line);
        std::string datetime_str, bid_str, ask_str;

        std::getline(ss, datetime_str, '\t');
        std::getline(ss, bid_str, '\t');
        std::getline(ss, ask_str, '\t');

        tick.timestamp = datetime_str;
        tick.bid = std::stod(bid_str);
        tick.ask = std::stod(ask_str);
        tick.volume = 0;

        ticks.push_back(tick);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

    std::cout << "  Loaded " << ticks.size() << " ticks in "
              << duration.count() << "s (~"
              << (ticks.size() * sizeof(Tick) / 1024 / 1024) << " MB)" << std::endl;
}

// ============================================================================
// Task and Result structures
// ============================================================================
enum StrategyType {
    KALMAN_FILTER,
    EMA_BASELINE
};

struct KalmanTask {
    StrategyType type;
    double process_noise_q;
    double measurement_noise_r;
    bool use_uncertainty_scaling;
    double tp_multiplier;
    double survive_pct;
    int year;  // 2024 or 2025
    std::string label;
};

struct KalmanResult {
    std::string label;
    StrategyType type;
    double q;
    double r;
    bool uncertainty_scaling;
    double tp_mult;
    double survive_pct;
    int year;
    double return_mult;
    double max_dd_pct;
    int total_trades;
    double total_swap;
    double sharpe_proxy;
    int crossings;           // Kalman or EMA crossings
    double avg_uncertainty;  // Only for Kalman
    double kalman_gain;      // Only for Kalman
    bool stopped_out;
};

// ============================================================================
// Work Queue for thread pool
// ============================================================================
class WorkQueue {
    std::queue<KalmanTask> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool done_ = false;

public:
    void push(const KalmanTask& task) {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push(task);
        cv_.notify_one();
    }

    bool pop(KalmanTask& task) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !tasks_.empty() || done_; });
        if (tasks_.empty()) return false;
        task = tasks_.front();
        tasks_.pop();
        return true;
    }

    void finish() {
        std::lock_guard<std::mutex> lock(mutex_);
        done_ = true;
        cv_.notify_all();
    }
};

// ============================================================================
// Global state
// ============================================================================
std::atomic<int> g_completed{0};
std::mutex g_results_mutex;
std::mutex g_output_mutex;
std::vector<KalmanResult> g_results;

// ============================================================================
// Run single Kalman test
// ============================================================================
KalmanResult run_kalman_test(const KalmanTask& task, const std::vector<Tick>& ticks) {
    KalmanResult r;
    r.label = task.label;
    r.type = task.type;
    r.q = task.process_noise_q;
    r.r = task.measurement_noise_r;
    r.uncertainty_scaling = task.use_uncertainty_scaling;
    r.tp_mult = task.tp_multiplier;
    r.survive_pct = task.survive_pct;
    r.year = task.year;
    r.return_mult = 0;
    r.max_dd_pct = 0;
    r.total_trades = 0;
    r.total_swap = 0;
    r.sharpe_proxy = 0;
    r.crossings = 0;
    r.avg_uncertainty = 0;
    r.kalman_gain = 0;
    r.stopped_out = false;

    if (ticks.empty()) {
        r.stopped_out = true;
        return r;
    }

    TickBacktestConfig cfg;
    cfg.symbol = "XAUUSD";
    cfg.initial_balance = 10000.0;
    cfg.account_currency = "USD";
    cfg.contract_size = 100.0;
    cfg.leverage = 500.0;
    cfg.margin_rate = 1.0;
    cfg.pip_size = 0.01;
    cfg.swap_long = -66.99;
    cfg.swap_short = 41.2;
    cfg.swap_mode = 1;
    cfg.swap_3days = 3;

    if (task.year == 2024) {
        cfg.start_date = "2024.01.01";
        cfg.end_date = "2024.12.30";
    } else {
        cfg.start_date = "2025.01.01";
        cfg.end_date = "2025.12.30";
    }
    cfg.verbose = false;

    TickDataConfig tc;
    tc.file_path = "";
    tc.load_all_into_memory = false;
    cfg.tick_data_config = tc;

    try {
        TickBasedEngine engine(cfg);

        if (task.type == KALMAN_FILTER) {
            StrategyKalmanFilter::Config kc;
            kc.process_noise_q = task.process_noise_q;
            kc.measurement_noise_r = task.measurement_noise_r;
            kc.use_uncertainty_scaling = task.use_uncertainty_scaling;
            kc.uncertainty_scale_factor = 0.5;
            kc.survive_pct = task.survive_pct;
            kc.base_spacing = 1.50;
            kc.min_volume = 0.01;
            kc.max_volume = 10.0;
            kc.contract_size = 100.0;
            kc.leverage = 500.0;
            kc.tp_multiplier = task.tp_multiplier;
            kc.adaptive_spacing = true;
            kc.typical_vol_pct = 0.5;
            kc.volatility_lookback_hours = 4.0;

            StrategyKalmanFilter strategy(kc);

            engine.RunWithTicks(ticks, [&strategy](const Tick& t, TickBasedEngine& e) {
                strategy.OnTick(t, e);
            });

            r.crossings = strategy.GetKalmanCrossings();
            r.avg_uncertainty = (strategy.GetMaxUncertainty() + strategy.GetMinUncertainty()) / 2.0;
            r.kalman_gain = strategy.GetKalmanGain();
        } else {
            // EMA Baseline (Floating Attractor)
            StrategyFloatingAttractor::Config fc;
            fc.survive_pct = task.survive_pct;
            fc.base_spacing = 1.50;
            fc.min_volume = 0.01;
            fc.max_volume = 10.0;
            fc.contract_size = 100.0;
            fc.leverage = 500.0;
            fc.attractor_period = 200;
            fc.attractor_type = StrategyFloatingAttractor::EMA;
            fc.tp_multiplier = task.tp_multiplier;
            fc.adaptive_spacing = true;
            fc.typical_vol_pct = 0.5;
            fc.volatility_lookback_hours = 4.0;

            StrategyFloatingAttractor strategy(fc);

            engine.RunWithTicks(ticks, [&strategy](const Tick& t, TickBasedEngine& e) {
                strategy.OnTick(t, e);
            });

            r.crossings = strategy.GetAttractorCrossings();
        }

        auto res = engine.GetResults();
        r.return_mult = res.final_balance / 10000.0;
        r.max_dd_pct = res.max_drawdown_pct;
        r.total_trades = res.total_trades;
        r.total_swap = res.total_swap_charged;
        r.sharpe_proxy = (r.max_dd_pct > 0) ? (r.return_mult - 1.0) / (r.max_dd_pct / 100.0) : 0;
        r.stopped_out = res.stop_out_occurred;

    } catch (const std::exception& e) {
        r.stopped_out = true;
        r.max_dd_pct = 100.0;
    }

    return r;
}

void worker(WorkQueue& queue, int total) {
    KalmanTask task;
    while (queue.pop(task)) {
        const std::vector<Tick>& ticks = (task.year == 2024) ? g_shared_ticks_2024 : g_shared_ticks_2025;
        KalmanResult r = run_kalman_test(task, ticks);

        {
            std::lock_guard<std::mutex> lock(g_results_mutex);
            g_results.push_back(r);
        }

        int done = ++g_completed;
        if (done % 10 == 0 || done == total) {
            std::lock_guard<std::mutex> lock(g_output_mutex);
            std::cout << "\rProgress: " << done << "/" << total
                      << " (" << std::fixed << std::setprecision(1)
                      << (100.0 * done / total) << "%)" << std::flush;
        }
    }
}

// ============================================================================
// Generate all configurations to test
// ============================================================================
std::vector<KalmanTask> GenerateTasks() {
    std::vector<KalmanTask> tasks;

    // Process noise Q values
    std::vector<double> q_values = {0.0001, 0.001, 0.01, 0.1};
    // Measurement noise R values
    std::vector<double> r_values = {0.01, 0.1, 1.0, 10.0};
    // Uncertainty scaling
    std::vector<bool> uncertainty_scaling = {false, true};
    // TP multipliers
    std::vector<double> tp_mults = {1.5, 2.0};
    // Survive percentages
    std::vector<double> survives = {12.0, 13.0};
    // Years
    std::vector<int> years = {2024, 2025};

    // 1. EMA Baseline for each year and survive
    for (int year : years) {
        for (double surv : survives) {
            for (double tp : tp_mults) {
                std::string lbl = "EMA_s" + std::to_string((int)surv) +
                                  "_tp" + std::to_string((int)(tp * 10)) +
                                  "_" + std::to_string(year);
                tasks.push_back({EMA_BASELINE, 0, 0, false, tp, surv, year, lbl});
            }
        }
    }

    // 2. Kalman Filter sweep
    for (int year : years) {
        for (double surv : survives) {
            for (double q : q_values) {
                for (double r_val : r_values) {
                    for (bool unc_scale : uncertainty_scaling) {
                        for (double tp : tp_mults) {
                            std::string lbl = "KF_q" + std::to_string(q).substr(0, 6) +
                                              "_r" + std::to_string(r_val).substr(0, 5) +
                                              (unc_scale ? "_US" : "") +
                                              "_tp" + std::to_string((int)(tp * 10)) +
                                              "_s" + std::to_string((int)surv) +
                                              "_" + std::to_string(year);
                            tasks.push_back({KALMAN_FILTER, q, r_val, unc_scale, tp, surv, year, lbl});
                        }
                    }
                }
            }
        }
    }

    return tasks;
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "  KALMAN FILTER STRATEGY PARALLEL SWEEP" << std::endl;
    std::cout << "  Comparing Kalman Filter vs EMA as grid attractor" << std::endl;
    std::cout << "  Data: XAUUSD 2024 + 2025" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;

    // Step 1: Load tick data for both years
    std::string path_2025 = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    std::string path_2024 = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAUUSD_TICKS_2024.csv";

    LoadTickData(path_2025, g_shared_ticks_2025, "2025");
    LoadTickData(path_2024, g_shared_ticks_2024, "2024");

    if (g_shared_ticks_2025.empty() && g_shared_ticks_2024.empty()) {
        std::cerr << "ERROR: No tick data loaded!" << std::endl;
        return 1;
    }

    std::cout << std::endl;

    // Step 2: Generate tasks
    auto tasks = GenerateTasks();
    int total = (int)tasks.size();

    // Step 3: Setup thread pool
    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;
    std::cout << "Using " << num_threads << " worker threads" << std::endl;
    std::cout << "Testing " << total << " configurations..." << std::endl;
    std::cout << std::endl;

    // Step 4: Fill work queue
    WorkQueue queue;
    for (const auto& task : tasks) {
        queue.push(task);
    }

    // Step 5: Launch workers
    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < num_threads; i++) {
        threads.emplace_back(worker, std::ref(queue), total);
    }

    queue.finish();
    for (auto& t : threads) {
        t.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

    std::cout << std::endl << std::endl;
    std::cout << "Completed in " << duration.count() << " seconds ("
              << std::fixed << std::setprecision(2) << (double)duration.count() / total
              << "s per config)" << std::endl;
    std::cout << std::endl;

    // ========================================================================
    // ANALYSIS BY YEAR
    // ========================================================================
    for (int year : {2025, 2024}) {
        std::vector<KalmanResult> year_results;
        for (const auto& r : g_results) {
            if (r.year == year) {
                year_results.push_back(r);
            }
        }

        if (year_results.empty()) continue;

        // Sort by return
        std::sort(year_results.begin(), year_results.end(), [](const KalmanResult& a, const KalmanResult& b) {
            if (a.stopped_out != b.stopped_out) return !a.stopped_out;
            return a.return_mult > b.return_mult;
        });

        std::cout << "================================================================" << std::endl;
        std::cout << "  YEAR " << year << " - TOP 20 BY RETURN" << std::endl;
        std::cout << "================================================================" << std::endl;
        std::cout << std::left << std::setw(4) << "#"
                  << std::setw(38) << "Label"
                  << std::right << std::setw(8) << "Return"
                  << std::setw(8) << "MaxDD%"
                  << std::setw(7) << "Trades"
                  << std::setw(8) << "Swap$"
                  << std::setw(8) << "Sharpe"
                  << std::setw(8) << "Cross"
                  << std::setw(6) << "SO"
                  << std::endl;
        std::cout << std::string(95, '-') << std::endl;

        for (int i = 0; i < std::min(20, (int)year_results.size()); i++) {
            const auto& r = year_results[i];
            std::cout << std::left << std::setw(4) << (i + 1)
                      << std::setw(38) << r.label.substr(0, 37)
                      << std::right << std::fixed
                      << std::setw(6) << std::setprecision(2) << r.return_mult << "x"
                      << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                      << std::setw(7) << r.total_trades
                      << std::setw(8) << std::setprecision(0) << r.total_swap
                      << std::setw(8) << std::setprecision(2) << r.sharpe_proxy
                      << std::setw(8) << r.crossings
                      << std::setw(6) << (r.stopped_out ? "YES" : "no")
                      << std::endl;
        }

        // Find EMA baselines for this year
        std::cout << std::endl;
        std::cout << "  EMA BASELINES (" << year << "):" << std::endl;
        for (const auto& r : year_results) {
            if (r.type == EMA_BASELINE) {
                std::cout << "    " << r.label << ": "
                          << std::fixed << std::setprecision(2) << r.return_mult << "x, "
                          << std::setprecision(1) << r.max_dd_pct << "% DD, "
                          << r.total_trades << " trades" << std::endl;
            }
        }

        // Best Kalman vs Best EMA comparison
        KalmanResult best_kalman, best_ema;
        bool found_kalman = false, found_ema = false;

        for (const auto& r : year_results) {
            if (!r.stopped_out) {
                if (r.type == KALMAN_FILTER && !found_kalman) {
                    best_kalman = r;
                    found_kalman = true;
                } else if (r.type == EMA_BASELINE && !found_ema) {
                    best_ema = r;
                    found_ema = true;
                }
            }
            if (found_kalman && found_ema) break;
        }

        if (found_kalman && found_ema) {
            std::cout << std::endl;
            std::cout << "  BEST KALMAN vs BEST EMA (" << year << "):" << std::endl;
            std::cout << "    Kalman: " << best_kalman.label << std::endl;
            std::cout << "            " << std::fixed << std::setprecision(2) << best_kalman.return_mult << "x return, "
                      << std::setprecision(1) << best_kalman.max_dd_pct << "% DD, "
                      << "Sharpe=" << std::setprecision(2) << best_kalman.sharpe_proxy << std::endl;
            std::cout << "    EMA:    " << best_ema.label << std::endl;
            std::cout << "            " << std::fixed << std::setprecision(2) << best_ema.return_mult << "x return, "
                      << std::setprecision(1) << best_ema.max_dd_pct << "% DD, "
                      << "Sharpe=" << std::setprecision(2) << best_ema.sharpe_proxy << std::endl;
            double diff_pct = (best_kalman.return_mult - best_ema.return_mult) / best_ema.return_mult * 100.0;
            std::cout << "    Difference: " << (diff_pct > 0 ? "+" : "") << std::fixed << std::setprecision(1) << diff_pct << "%" << std::endl;
        }
        std::cout << std::endl;
    }

    // ========================================================================
    // Q/R RATIO ANALYSIS
    // ========================================================================
    std::cout << "================================================================" << std::endl;
    std::cout << "  Q/R RATIO ANALYSIS (Kalman Filter only, 2025)" << std::endl;
    std::cout << "  Low Q/R = more smoothing (trust observation less)" << std::endl;
    std::cout << "  High Q/R = less smoothing (track observation more)" << std::endl;
    std::cout << "================================================================" << std::endl;

    // Group by Q/R ratio
    std::map<std::string, std::vector<KalmanResult>> qr_groups;
    for (const auto& r : g_results) {
        if (r.type == KALMAN_FILTER && r.year == 2025 && !r.stopped_out) {
            std::stringstream ss;
            ss << "Q=" << std::fixed << std::setprecision(4) << r.q
               << ", R=" << std::setprecision(2) << r.r;
            qr_groups[ss.str()].push_back(r);
        }
    }

    std::cout << std::left << std::setw(22) << "Q/R Combination"
              << std::right << std::setw(10) << "Q/R Ratio"
              << std::setw(10) << "Avg Ret"
              << std::setw(10) << "Avg DD%"
              << std::setw(10) << "Configs"
              << std::endl;
    std::cout << std::string(62, '-') << std::endl;

    std::vector<std::pair<double, std::string>> ratio_order;
    for (const auto& [key, results] : qr_groups) {
        double q = results[0].q;
        double r_val = results[0].r;
        double ratio = q / r_val;
        ratio_order.push_back({ratio, key});
    }
    std::sort(ratio_order.begin(), ratio_order.end());

    for (const auto& [ratio, key] : ratio_order) {
        const auto& results = qr_groups[key];
        double avg_ret = 0, avg_dd = 0;
        for (const auto& r : results) {
            avg_ret += r.return_mult;
            avg_dd += r.max_dd_pct;
        }
        avg_ret /= results.size();
        avg_dd /= results.size();

        std::cout << std::left << std::setw(22) << key
                  << std::right << std::fixed
                  << std::setw(10) << std::setprecision(4) << ratio
                  << std::setw(8) << std::setprecision(2) << avg_ret << "x"
                  << std::setw(9) << std::setprecision(1) << avg_dd << "%"
                  << std::setw(10) << results.size()
                  << std::endl;
    }

    // ========================================================================
    // UNCERTAINTY SCALING ANALYSIS
    // ========================================================================
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  UNCERTAINTY SCALING IMPACT (2025)" << std::endl;
    std::cout << "================================================================" << std::endl;

    double sum_ret_us = 0, sum_ret_no_us = 0;
    double sum_dd_us = 0, sum_dd_no_us = 0;
    int count_us = 0, count_no_us = 0;

    for (const auto& r : g_results) {
        if (r.type == KALMAN_FILTER && r.year == 2025 && !r.stopped_out) {
            if (r.uncertainty_scaling) {
                sum_ret_us += r.return_mult;
                sum_dd_us += r.max_dd_pct;
                count_us++;
            } else {
                sum_ret_no_us += r.return_mult;
                sum_dd_no_us += r.max_dd_pct;
                count_no_us++;
            }
        }
    }

    if (count_us > 0 && count_no_us > 0) {
        double avg_ret_us = sum_ret_us / count_us;
        double avg_ret_no_us = sum_ret_no_us / count_no_us;
        double avg_dd_us = sum_dd_us / count_us;
        double avg_dd_no_us = sum_dd_no_us / count_no_us;

        std::cout << "  Without Uncertainty Scaling: " << count_no_us << " configs" << std::endl;
        std::cout << "    Avg Return: " << std::fixed << std::setprecision(2) << avg_ret_no_us << "x" << std::endl;
        std::cout << "    Avg DD:     " << std::setprecision(1) << avg_dd_no_us << "%" << std::endl;
        std::cout << std::endl;
        std::cout << "  With Uncertainty Scaling: " << count_us << " configs" << std::endl;
        std::cout << "    Avg Return: " << std::fixed << std::setprecision(2) << avg_ret_us << "x" << std::endl;
        std::cout << "    Avg DD:     " << std::setprecision(1) << avg_dd_us << "%" << std::endl;
        std::cout << std::endl;
        double diff = (avg_ret_us - avg_ret_no_us) / avg_ret_no_us * 100.0;
        std::cout << "  Uncertainty scaling impact: " << (diff > 0 ? "+" : "") << std::fixed << std::setprecision(1) << diff << "% return" << std::endl;
    }

    // ========================================================================
    // CROSS-YEAR CONSISTENCY
    // ========================================================================
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  CROSS-YEAR CONSISTENCY (2024 vs 2025)" << std::endl;
    std::cout << "================================================================" << std::endl;

    // Find configs that exist in both years
    std::map<std::string, std::pair<KalmanResult, KalmanResult>> paired;
    for (const auto& r : g_results) {
        // Extract base label without year
        std::string base = r.label;
        size_t pos = base.rfind('_');
        if (pos != std::string::npos) {
            base = base.substr(0, pos);
        }

        if (r.year == 2025 && !r.stopped_out) {
            // Find matching 2024 result
            for (const auto& r2 : g_results) {
                std::string base2 = r2.label;
                size_t pos2 = base2.rfind('_');
                if (pos2 != std::string::npos) {
                    base2 = base2.substr(0, pos2);
                }
                if (base == base2 && r2.year == 2024 && !r2.stopped_out) {
                    paired[base] = {r, r2};
                    break;
                }
            }
        }
    }

    // Calculate year ratio for each config
    std::vector<std::tuple<std::string, double, double, double>> consistency;
    for (const auto& [base, pair] : paired) {
        double ratio = pair.first.return_mult / pair.second.return_mult;
        consistency.push_back({base, pair.first.return_mult, pair.second.return_mult, ratio});
    }

    // Sort by ratio (closer to 1 = more consistent)
    std::sort(consistency.begin(), consistency.end(),
              [](const auto& a, const auto& b) {
                  double diff_a = std::abs(std::get<3>(a) - 1.0);
                  double diff_b = std::abs(std::get<3>(b) - 1.0);
                  return diff_a < diff_b;
              });

    std::cout << "  MOST CONSISTENT (ratio closest to 1.0 means similar performance both years):" << std::endl;
    std::cout << std::left << std::setw(38) << "Config"
              << std::right << std::setw(10) << "2025"
              << std::setw(10) << "2024"
              << std::setw(10) << "Ratio"
              << std::endl;
    std::cout << std::string(68, '-') << std::endl;

    for (int i = 0; i < std::min(10, (int)consistency.size()); i++) {
        const auto& [base, ret25, ret24, ratio] = consistency[i];
        std::cout << std::left << std::setw(38) << base.substr(0, 37)
                  << std::right << std::fixed
                  << std::setw(8) << std::setprecision(2) << ret25 << "x"
                  << std::setw(8) << std::setprecision(2) << ret24 << "x"
                  << std::setw(10) << std::setprecision(2) << ratio
                  << std::endl;
    }

    // ========================================================================
    // SUMMARY
    // ========================================================================
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  SUMMARY & CONCLUSIONS" << std::endl;
    std::cout << "================================================================" << std::endl;

    // Find overall best Kalman and EMA
    KalmanResult overall_best_kalman_2025, overall_best_ema_2025;
    bool found_k = false, found_e = false;
    for (const auto& r : g_results) {
        if (r.year == 2025 && !r.stopped_out) {
            if (r.type == KALMAN_FILTER && (!found_k || r.return_mult > overall_best_kalman_2025.return_mult)) {
                overall_best_kalman_2025 = r;
                found_k = true;
            } else if (r.type == EMA_BASELINE && (!found_e || r.return_mult > overall_best_ema_2025.return_mult)) {
                overall_best_ema_2025 = r;
                found_e = true;
            }
        }
    }

    std::cout << std::endl;
    std::cout << "  KEY FINDINGS:" << std::endl;
    if (found_k && found_e) {
        double diff = (overall_best_kalman_2025.return_mult - overall_best_ema_2025.return_mult) /
                      overall_best_ema_2025.return_mult * 100.0;
        std::cout << "  1. Best Kalman (2025): " << std::fixed << std::setprecision(2)
                  << overall_best_kalman_2025.return_mult << "x return, "
                  << std::setprecision(1) << overall_best_kalman_2025.max_dd_pct << "% DD" << std::endl;
        std::cout << "     Config: " << overall_best_kalman_2025.label << std::endl;
        std::cout << std::endl;
        std::cout << "  2. Best EMA (2025): " << std::fixed << std::setprecision(2)
                  << overall_best_ema_2025.return_mult << "x return, "
                  << std::setprecision(1) << overall_best_ema_2025.max_dd_pct << "% DD" << std::endl;
        std::cout << "     Config: " << overall_best_ema_2025.label << std::endl;
        std::cout << std::endl;
        std::cout << "  3. Kalman vs EMA: " << (diff > 0 ? "+" : "") << std::fixed << std::setprecision(1)
                  << diff << "% " << (diff > 0 ? "better" : "worse") << std::endl;
    }

    std::cout << std::endl;
    std::cout << "  Total configs tested: " << g_results.size() << std::endl;
    std::cout << "  Execution time: " << duration.count() << "s (" << num_threads << " threads)" << std::endl;

    return 0;
}
