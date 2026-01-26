/**
 * Noise Scaling Parallel Parameter Sweep
 *
 * CONCEPT: Noise as signal carrier - use noise LEVEL to scale position size.
 * High noise = more oscillation opportunity = larger positions.
 * Low noise = trending/quiet = smaller positions.
 *
 * Noise metrics:
 * 1. TICK_VOL: Ratio of short-term to long-term tick return std dev
 * 2. DIRECTION_CHANGES: Ratio of short-term to long-term direction change rate
 * 3. COMBINED: Average of both metrics
 *
 * Loads tick data ONCE into shared memory, then tests multiple configurations
 * in parallel using std::thread + WorkQueue.
 */

#include "../include/strategy_noise_scaling.h"
#include "../include/fill_up_oscillation.h"
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
std::vector<Tick> g_shared_ticks;

void LoadTickDataOnce(const std::string& path) {
    std::cout << "Loading tick data into memory (one-time)..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open tick file: " + path);
    }

    std::string line;
    std::getline(file, line);  // Skip header

    g_shared_ticks.reserve(52000000);  // Pre-allocate for ~52M ticks

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

        g_shared_ticks.push_back(tick);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

    std::cout << "Loaded " << g_shared_ticks.size() << " ticks in "
              << duration.count() << " seconds" << std::endl;
    std::cout << "Memory usage: ~" << (g_shared_ticks.size() * sizeof(Tick) / 1024 / 1024)
              << " MB" << std::endl << std::endl;
}

// ============================================================================
// Task and Result structures
// ============================================================================
struct NoiseTask {
    int noise_window;           // Short-term noise window (ticks)
    int reference_window;       // Long-term reference window (ticks)
    double min_multiplier;      // Floor for lot scaling
    double max_multiplier;      // Ceiling for lot scaling
    int metric;                 // 0=TICK_VOL, 1=DIRECTION_CHANGES, 2=COMBINED
    bool is_baseline;           // True = use FillUpOscillation baseline
    std::string label;
};

struct NoiseResult {
    std::string label;
    int noise_window;
    int reference_window;
    double min_multiplier;
    double max_multiplier;
    std::string metric_name;
    double return_mult;
    double max_dd_pct;
    int total_trades;
    double total_swap;
    double sharpe_proxy;        // (return - 1) / (DD/100)
    double avg_noise_ratio;
    double min_noise_ratio;
    double max_noise_ratio;
    int noise_scale_changes;
    bool is_baseline;
    bool stop_out;
};

// ============================================================================
// Work Queue for thread pool
// ============================================================================
class WorkQueue {
    std::queue<NoiseTask> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool done_ = false;

public:
    void push(const NoiseTask& task) {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push(task);
        cv_.notify_one();
    }

    bool pop(NoiseTask& task) {
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
std::vector<NoiseResult> g_results;

// ============================================================================
// Run single test with shared tick data
// ============================================================================
NoiseResult run_test(const NoiseTask& task, const std::vector<Tick>& ticks) {
    NoiseResult r;
    r.label = task.label;
    r.noise_window = task.noise_window;
    r.reference_window = task.reference_window;
    r.min_multiplier = task.min_multiplier;
    r.max_multiplier = task.max_multiplier;
    r.is_baseline = task.is_baseline;
    r.stop_out = false;

    const char* metric_names[] = {"TICK_VOL", "DIR_CHG", "COMBINED"};
    r.metric_name = task.is_baseline ? "BASELINE" : metric_names[task.metric];

    r.return_mult = 0;
    r.max_dd_pct = 0;
    r.total_trades = 0;
    r.total_swap = 0;
    r.sharpe_proxy = 0;
    r.avg_noise_ratio = 1.0;
    r.min_noise_ratio = 1.0;
    r.max_noise_ratio = 1.0;
    r.noise_scale_changes = 0;

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
    cfg.start_date = "2025.01.01";
    cfg.end_date = "2025.12.30";
    cfg.verbose = false;

    TickDataConfig tc;
    tc.file_path = "";
    tc.load_all_into_memory = false;
    cfg.tick_data_config = tc;

    try {
        TickBasedEngine engine(cfg);

        if (task.is_baseline) {
            // Use standard FillUpOscillation as baseline
            FillUpOscillation strategy(13.0, 1.5, 0.01, 10.0, 100.0, 500.0,
                                       FillUpOscillation::ADAPTIVE_SPACING, 0.1, 30.0, 4.0);

            engine.RunWithTicks(ticks, [&strategy](const Tick& t, TickBasedEngine& e) {
                strategy.OnTick(t, e);
            });

            auto res = engine.GetResults();
            r.return_mult = res.final_balance / 10000.0;
            r.max_dd_pct = res.max_drawdown_pct;
            r.total_trades = res.total_trades;
            r.total_swap = res.total_swap_charged;
            r.stop_out = res.stop_out_occurred;
        } else {
            // Use noise scaling strategy
            StrategyNoiseScaling::NoiseConfig nc;
            nc.noise_window = task.noise_window;
            nc.reference_window = task.reference_window;
            nc.min_multiplier = task.min_multiplier;
            nc.max_multiplier = task.max_multiplier;
            nc.metric = static_cast<StrategyNoiseScaling::NoiseMetric>(task.metric);

            StrategyNoiseScaling strategy(13.0, 1.5, 0.01, 10.0, 100.0, 500.0, nc);

            engine.RunWithTicks(ticks, [&strategy](const Tick& t, TickBasedEngine& e) {
                strategy.OnTick(t, e);
            });

            auto res = engine.GetResults();
            r.return_mult = res.final_balance / 10000.0;
            r.max_dd_pct = res.max_drawdown_pct;
            r.total_trades = res.total_trades;
            r.total_swap = res.total_swap_charged;
            r.stop_out = res.stop_out_occurred;

            // Noise statistics
            r.avg_noise_ratio = strategy.GetAvgNoiseRatio();
            r.min_noise_ratio = strategy.GetMinNoiseRatio();
            r.max_noise_ratio = strategy.GetMaxNoiseRatio();
            r.noise_scale_changes = strategy.GetNoiseScaleChanges();
        }

        r.sharpe_proxy = (r.max_dd_pct > 0) ? (r.return_mult - 1.0) / (r.max_dd_pct / 100.0) : 0;

    } catch (const std::exception& e) {
        r.max_dd_pct = 100.0;
        r.return_mult = 0.0;
        r.stop_out = true;
    }

    return r;
}

void worker(WorkQueue& queue, int total, const std::vector<Tick>& ticks) {
    NoiseTask task;
    while (queue.pop(task)) {
        NoiseResult r = run_test(task, ticks);

        {
            std::lock_guard<std::mutex> lock(g_results_mutex);
            g_results.push_back(r);
        }

        int done = ++g_completed;
        if (done % 5 == 0 || done == total) {
            std::lock_guard<std::mutex> lock(g_output_mutex);
            std::cout << "\rProgress: " << done << "/" << total
                      << " (" << std::fixed << std::setprecision(1)
                      << (100.0 * done / total) << "%)" << std::flush;
        }
    }
}

// ============================================================================
// Generate all noise scaling configurations to test
// ============================================================================
std::vector<NoiseTask> GenerateTasks() {
    std::vector<NoiseTask> tasks;

    // 1. BASELINE (FillUpOscillation ADAPTIVE_SPACING, no noise scaling)
    tasks.push_back({0, 0, 1.0, 1.0, 0, true, "BASELINE"});

    // Sweep parameters (as specified in task)
    std::vector<int> noise_windows = {50, 100, 500, 1000, 5000};
    std::vector<int> reference_windows = {1000, 5000, 10000};
    std::vector<double> min_mults = {0.2, 0.5};
    std::vector<double> max_mults = {1.5, 2.0, 3.0};
    std::vector<int> metrics = {0, 1, 2};  // TICK_VOL, DIRECTION_CHANGES, COMBINED

    // Full parameter sweep
    for (int nw : noise_windows) {
        for (int rw : reference_windows) {
            // Skip if noise window >= reference window
            if (nw >= rw) continue;

            for (double min_m : min_mults) {
                for (double max_m : max_mults) {
                    for (int metric : metrics) {
                        std::string metric_str = (metric == 0) ? "TV" : (metric == 1) ? "DC" : "CB";
                        std::string label = "nw" + std::to_string(nw) +
                                            "_rw" + std::to_string(rw) +
                                            "_" + std::to_string((int)(min_m * 10)) +
                                            "-" + std::to_string((int)(max_m * 10)) +
                                            "_" + metric_str;
                        tasks.push_back({nw, rw, min_m, max_m, metric, false, label});
                    }
                }
            }
        }
    }

    return tasks;
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "  NOISE SCALING PARALLEL SWEEP" << std::endl;
    std::cout << "  Concept: Noise as signal carrier - scale lot with noise level" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;
    std::cout << "HYPOTHESIS:" << std::endl;
    std::cout << "  High noise = more oscillation opportunity = larger positions" << std::endl;
    std::cout << "  Low noise = trending/quiet = smaller positions" << std::endl;
    std::cout << std::endl;
    std::cout << "Noise metrics:" << std::endl;
    std::cout << "  TICK_VOL (TV): Ratio of short-term to long-term tick volatility" << std::endl;
    std::cout << "  DIR_CHG (DC):  Ratio of short-term to long-term direction changes" << std::endl;
    std::cout << "  COMBINED (CB): Average of both metrics" << std::endl;
    std::cout << std::endl;
    std::cout << "Base strategy: FillUp survive=13%, spacing=$1.50" << std::endl;
    std::cout << "Data: XAUUSD 2025 (full year)" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;

    // Step 1: Load tick data ONCE
    std::string tick_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    LoadTickDataOnce(tick_path);

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
        threads.emplace_back(worker, std::ref(queue), total, std::cref(g_shared_ticks));
    }

    queue.finish();
    for (auto& t : threads) {
        t.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

    std::cout << std::endl << std::endl;
    std::cout << "Completed in " << duration.count() << " seconds ("
              << std::fixed << std::setprecision(1) << (double)duration.count() / total
              << "s per config, " << num_threads << " threads)" << std::endl;
    std::cout << std::endl;

    // Step 6: Sort by sharpe_proxy
    std::sort(g_results.begin(), g_results.end(), [](const NoiseResult& a, const NoiseResult& b) {
        return a.sharpe_proxy > b.sharpe_proxy;
    });

    // Step 7: Print results - TOP 30
    std::cout << "================================================================" << std::endl;
    std::cout << "  TOP 30 CONFIGURATIONS (sorted by Sharpe proxy = (Return-1)/DD)" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::left << std::setw(4) << "#"
              << std::setw(32) << "Label"
              << std::right << std::setw(8) << "Return"
              << std::setw(7) << "MaxDD"
              << std::setw(7) << "Trades"
              << std::setw(8) << "Swap$"
              << std::setw(7) << "Sharpe"
              << std::setw(7) << "AvgNR"
              << std::setw(8) << "Changes"
              << std::endl;
    std::cout << std::string(100, '-') << std::endl;

    for (int i = 0; i < std::min(30, (int)g_results.size()); i++) {
        const auto& r = g_results[i];
        std::cout << std::left << std::setw(4) << (i + 1)
                  << std::setw(32) << r.label.substr(0, 31)
                  << std::right << std::fixed
                  << std::setw(6) << std::setprecision(2) << r.return_mult << "x"
                  << std::setw(6) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(7) << r.total_trades
                  << std::setw(8) << std::setprecision(0) << r.total_swap
                  << std::setw(7) << std::setprecision(2) << r.sharpe_proxy
                  << std::setw(7) << std::setprecision(2) << r.avg_noise_ratio
                  << std::setw(8) << r.noise_scale_changes
                  << std::endl;
    }

    // Find and print baseline
    std::cout << std::string(100, '-') << std::endl;
    const NoiseResult* baseline = nullptr;
    for (const auto& r : g_results) {
        if (r.is_baseline) {
            baseline = &r;
            int rank = 0;
            for (size_t i = 0; i < g_results.size(); i++) {
                if (g_results[i].is_baseline) { rank = (int)i + 1; break; }
            }
            std::cout << std::left << std::setw(4) << "BASE"
                      << std::setw(32) << "BASELINE (no noise scaling)"
                      << std::right << std::fixed
                      << std::setw(6) << std::setprecision(2) << r.return_mult << "x"
                      << std::setw(6) << std::setprecision(1) << r.max_dd_pct << "%"
                      << std::setw(7) << r.total_trades
                      << std::setw(8) << std::setprecision(0) << r.total_swap
                      << std::setw(7) << std::setprecision(2) << r.sharpe_proxy
                      << std::setw(7) << "-"
                      << std::setw(8) << "-"
                      << std::endl;
            std::cout << "  (Baseline rank: #" << rank << " of " << g_results.size() << ")" << std::endl;
            break;
        }
    }

    // Analysis: Group by metric type
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  BY NOISE METRIC TYPE (best of each)" << std::endl;
    std::cout << "================================================================" << std::endl;

    std::map<std::string, const NoiseResult*> best_by_metric;
    best_by_metric["BASELINE"] = nullptr;
    best_by_metric["TICK_VOL"] = nullptr;
    best_by_metric["DIR_CHG"] = nullptr;
    best_by_metric["COMBINED"] = nullptr;

    for (const auto& r : g_results) {
        if (r.stop_out) continue;
        const std::string& m = r.metric_name;
        if (best_by_metric.find(m) != best_by_metric.end()) {
            if (!best_by_metric[m] || r.sharpe_proxy > best_by_metric[m]->sharpe_proxy) {
                best_by_metric[m] = &r;
            }
        }
    }

    std::cout << std::left << std::setw(12) << "Metric"
              << std::setw(34) << "Best Config"
              << std::right << std::setw(8) << "Return"
              << std::setw(7) << "MaxDD"
              << std::setw(7) << "Sharpe"
              << std::setw(7) << "AvgNR"
              << std::endl;
    std::cout << std::string(80, '-') << std::endl;

    for (const auto& [name, ptr] : best_by_metric) {
        if (ptr) {
            std::cout << std::left << std::setw(12) << name
                      << std::setw(34) << ptr->label.substr(0, 33)
                      << std::right << std::fixed
                      << std::setw(6) << std::setprecision(2) << ptr->return_mult << "x"
                      << std::setw(6) << std::setprecision(1) << ptr->max_dd_pct << "%"
                      << std::setw(7) << std::setprecision(2) << ptr->sharpe_proxy
                      << std::setw(7) << std::setprecision(2) << ptr->avg_noise_ratio
                      << std::endl;
        }
    }

    // Analysis: By multiplier range
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  BY MULTIPLIER RANGE (best of each)" << std::endl;
    std::cout << "================================================================" << std::endl;

    struct MultRange {
        double min_m, max_m;
        const NoiseResult* best;
    };
    std::vector<MultRange> mult_ranges = {
        {0.2, 1.5, nullptr},
        {0.2, 2.0, nullptr},
        {0.2, 3.0, nullptr},
        {0.5, 1.5, nullptr},
        {0.5, 2.0, nullptr},
        {0.5, 3.0, nullptr}
    };

    for (const auto& r : g_results) {
        if (r.is_baseline || r.stop_out) continue;
        for (auto& mr : mult_ranges) {
            if (std::abs(r.min_multiplier - mr.min_m) < 0.01 &&
                std::abs(r.max_multiplier - mr.max_m) < 0.01) {
                if (!mr.best || r.sharpe_proxy > mr.best->sharpe_proxy) {
                    mr.best = &r;
                }
            }
        }
    }

    std::cout << std::left << std::setw(12) << "Range"
              << std::setw(34) << "Best Config"
              << std::right << std::setw(8) << "Return"
              << std::setw(7) << "MaxDD"
              << std::setw(7) << "Sharpe"
              << std::endl;
    std::cout << std::string(75, '-') << std::endl;

    for (const auto& mr : mult_ranges) {
        if (mr.best) {
            std::ostringstream range_str;
            range_str << std::setprecision(1) << mr.min_m << "-" << mr.max_m << "x";
            std::cout << std::left << std::setw(12) << range_str.str()
                      << std::setw(34) << mr.best->label.substr(0, 33)
                      << std::right << std::fixed
                      << std::setw(6) << std::setprecision(2) << mr.best->return_mult << "x"
                      << std::setw(6) << std::setprecision(1) << mr.best->max_dd_pct << "%"
                      << std::setw(7) << std::setprecision(2) << mr.best->sharpe_proxy
                      << std::endl;
        }
    }

    // Analysis: By noise window
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  BY NOISE WINDOW (best of each)" << std::endl;
    std::cout << "================================================================" << std::endl;

    std::map<int, const NoiseResult*> best_by_window;
    for (int nw : {50, 100, 500, 1000, 5000}) {
        best_by_window[nw] = nullptr;
    }

    for (const auto& r : g_results) {
        if (r.is_baseline || r.stop_out) continue;
        if (best_by_window.find(r.noise_window) != best_by_window.end()) {
            if (!best_by_window[r.noise_window] || r.sharpe_proxy > best_by_window[r.noise_window]->sharpe_proxy) {
                best_by_window[r.noise_window] = &r;
            }
        }
    }

    std::cout << std::left << std::setw(12) << "Window"
              << std::setw(34) << "Best Config"
              << std::right << std::setw(8) << "Return"
              << std::setw(7) << "MaxDD"
              << std::setw(7) << "Sharpe"
              << std::endl;
    std::cout << std::string(75, '-') << std::endl;

    for (const auto& [window, ptr] : best_by_window) {
        if (ptr) {
            std::cout << std::left << std::setw(12) << std::to_string(window) + " ticks"
                      << std::setw(34) << ptr->label.substr(0, 33)
                      << std::right << std::fixed
                      << std::setw(6) << std::setprecision(2) << ptr->return_mult << "x"
                      << std::setw(6) << std::setprecision(1) << ptr->max_dd_pct << "%"
                      << std::setw(7) << std::setprecision(2) << ptr->sharpe_proxy
                      << std::endl;
        }
    }

    // Analysis: Improvement over baseline
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  IMPROVEMENT VS BASELINE" << std::endl;
    std::cout << "================================================================" << std::endl;

    if (baseline) {
        int better_return = 0, better_dd = 0, better_sharpe = 0;
        const NoiseResult* best_return = nullptr;
        const NoiseResult* best_dd = nullptr;
        const NoiseResult* best_sharpe = nullptr;

        for (const auto& r : g_results) {
            if (r.is_baseline || r.stop_out) continue;
            if (r.return_mult > baseline->return_mult) {
                better_return++;
                if (!best_return || r.return_mult > best_return->return_mult) best_return = &r;
            }
            if (r.max_dd_pct < baseline->max_dd_pct) {
                better_dd++;
                if (!best_dd || r.max_dd_pct < best_dd->max_dd_pct) best_dd = &r;
            }
            if (r.sharpe_proxy > baseline->sharpe_proxy) {
                better_sharpe++;
                if (!best_sharpe || r.sharpe_proxy > best_sharpe->sharpe_proxy) best_sharpe = &r;
            }
        }

        int total_configs = (int)g_results.size() - 1;
        std::cout << "Configs with better RETURN: " << better_return << "/" << total_configs
                  << " (" << std::fixed << std::setprecision(1) << (100.0 * better_return / total_configs) << "%)" << std::endl;
        std::cout << "Configs with lower DD:      " << better_dd << "/" << total_configs
                  << " (" << std::fixed << std::setprecision(1) << (100.0 * better_dd / total_configs) << "%)" << std::endl;
        std::cout << "Configs with better SHARPE: " << better_sharpe << "/" << total_configs
                  << " (" << std::fixed << std::setprecision(1) << (100.0 * better_sharpe / total_configs) << "%)" << std::endl;

        std::cout << std::endl;
        std::cout << "BASELINE:     " << std::fixed << std::setprecision(2) << baseline->return_mult << "x return, "
                  << std::setprecision(1) << baseline->max_dd_pct << "% DD, "
                  << std::setprecision(2) << baseline->sharpe_proxy << " Sharpe" << std::endl;

        if (best_return) {
            double ret_improve = (best_return->return_mult - baseline->return_mult) / baseline->return_mult * 100.0;
            std::cout << "BEST RETURN:  " << std::fixed << std::setprecision(2) << best_return->return_mult << "x ("
                      << std::showpos << std::setprecision(1) << ret_improve << std::noshowpos << "%) - "
                      << best_return->label << std::endl;
        }

        if (best_dd) {
            double dd_improve = baseline->max_dd_pct - best_dd->max_dd_pct;
            std::cout << "LOWEST DD:    " << std::fixed << std::setprecision(1) << best_dd->max_dd_pct << "% ("
                      << -dd_improve << "% saved) - "
                      << best_dd->label << std::endl;
        }

        if (best_sharpe) {
            double sharpe_improve = (best_sharpe->sharpe_proxy - baseline->sharpe_proxy) / baseline->sharpe_proxy * 100.0;
            std::cout << "BEST SHARPE:  " << std::fixed << std::setprecision(2) << best_sharpe->sharpe_proxy << " ("
                      << std::showpos << std::setprecision(1) << sharpe_improve << std::noshowpos << "%) - "
                      << best_sharpe->label << std::endl;
        }
    }

    // Summary and answers to key questions
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  ANSWERS TO KEY QUESTIONS" << std::endl;
    std::cout << "================================================================" << std::endl;

    if (baseline) {
        // Count stats
        int better_ret = 0, better_dd = 0, better_both = 0;
        for (const auto& r : g_results) {
            if (r.is_baseline || r.stop_out) continue;
            if (r.return_mult > baseline->return_mult) better_ret++;
            if (r.max_dd_pct < baseline->max_dd_pct) better_dd++;
            if (r.return_mult > baseline->return_mult && r.max_dd_pct < baseline->max_dd_pct) better_both++;
        }

        std::cout << std::endl;
        std::cout << "Q1: Does noise-scaled sizing improve returns?" << std::endl;
        if (better_ret > 0) {
            std::cout << "    YES - " << better_ret << " configs beat baseline return." << std::endl;
        } else {
            std::cout << "    NO - No configs beat baseline return." << std::endl;
        }

        std::cout << std::endl;
        std::cout << "Q2: Does it reduce DD by sizing down in quiet/trending periods?" << std::endl;
        if (better_dd > 0) {
            std::cout << "    YES - " << better_dd << " configs have lower DD than baseline." << std::endl;
        } else {
            std::cout << "    NO - No configs reduce DD vs baseline." << std::endl;
        }

        std::cout << std::endl;
        std::cout << "Q3: What noise measurement works best?" << std::endl;
        const NoiseResult* overall_best = nullptr;
        for (const auto& r : g_results) {
            if (r.is_baseline || r.stop_out) continue;
            if (!overall_best || r.sharpe_proxy > overall_best->sharpe_proxy) {
                overall_best = &r;
            }
        }
        if (overall_best) {
            std::cout << "    " << overall_best->metric_name << " with config: " << overall_best->label << std::endl;
        }

        std::cout << std::endl;
        std::cout << "Q4: What's the optimal scaling range?" << std::endl;
        if (overall_best) {
            std::cout << "    min=" << overall_best->min_multiplier << "x, max=" << overall_best->max_multiplier << "x" << std::endl;
            std::cout << "    noise_window=" << overall_best->noise_window << ", ref_window=" << overall_best->reference_window << std::endl;
        }
    }

    // Final summary
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  SUMMARY" << std::endl;
    std::cout << "================================================================" << std::endl;

    int stop_outs = 0;
    for (const auto& r : g_results) {
        if (r.stop_out) stop_outs++;
    }

    std::cout << "Total configurations tested: " << g_results.size() << std::endl;
    std::cout << "Stop-outs: " << stop_outs << std::endl;
    std::cout << "Execution time: " << duration.count() << "s (" << num_threads << " threads)" << std::endl;

    return 0;
}
