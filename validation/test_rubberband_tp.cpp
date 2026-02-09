/**
 * Rubber Band TP Parallel Parameter Sweep
 *
 * Tests the "Rubber Band" TP concept: Scale TP target with deviation from mean.
 * - Small deviation from mean -> small TP (quick exit)
 * - Large deviation from mean -> large TP (expect big reversion)
 *
 * Loads tick data ONCE into shared memory, then tests multiple configurations
 * in parallel using std::thread + WorkQueue.
 *
 * Tests on BOTH 2024 and 2025 data to assess regime independence.
 */

#include "../include/strategy_rubberband_tp.h"
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

using namespace backtest;

// ============================================================================
// Shared tick data - loaded ONCE per year, used by ALL threads
// ============================================================================
std::vector<Tick> g_ticks_2025;
std::vector<Tick> g_ticks_2024;

void LoadTickData(const std::string& path, std::vector<Tick>& dest, const std::string& label) {
    std::cout << "Loading " << label << " tick data..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open tick file: " + path);
    }

    std::string line;
    std::getline(file, line);  // Skip header

    dest.reserve(52000000);

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

        dest.push_back(tick);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

    std::cout << "  Loaded " << dest.size() << " ticks in " << duration.count() << "s" << std::endl;
}

// ============================================================================
// Task and Result structures
// ============================================================================
struct RBTask {
    int survive_pct;
    StrategyRubberBandTP::TPMode tp_mode;
    StrategyRubberBandTP::EquilibriumType equilibrium_type;
    double linear_scale;
    double sqrt_scale;
    double reversion_pct;
    bool use_2025;  // true = 2025, false = 2024
    std::string label;
};

struct RBResult {
    std::string label;
    int survive_pct;
    std::string tp_mode_str;
    std::string equilibrium_str;
    double param;  // scale or reversion_pct
    int year;
    double return_mult;
    double max_dd_pct;
    int total_trades;
    double total_swap;
    double avg_deviation;
    double avg_tp;
    double sharpe_proxy;
    bool stopped_out;
};

// ============================================================================
// Work Queue for thread pool
// ============================================================================
class WorkQueue {
    std::queue<RBTask> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool done_ = false;

public:
    void push(const RBTask& task) {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push(task);
        cv_.notify_one();
    }

    bool pop(RBTask& task) {
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
std::vector<RBResult> g_results;

// ============================================================================
// Convert enum to string for reporting
// ============================================================================
std::string TPModeToString(StrategyRubberBandTP::TPMode mode) {
    switch (mode) {
        case StrategyRubberBandTP::FIXED: return "FIXED";
        case StrategyRubberBandTP::LINEAR: return "LINEAR";
        case StrategyRubberBandTP::SQRT: return "SQRT";
        case StrategyRubberBandTP::PROPORTIONAL: return "PROPORTIONAL";
        default: return "UNKNOWN";
    }
}

std::string EquilibriumToString(StrategyRubberBandTP::EquilibriumType eq) {
    switch (eq) {
        case StrategyRubberBandTP::FIRST_ENTRY: return "FIRST_ENTRY";
        case StrategyRubberBandTP::EMA_200: return "EMA_200";
        case StrategyRubberBandTP::EMA_500: return "EMA_500";
        default: return "UNKNOWN";
    }
}

// ============================================================================
// Run single test with shared tick data
// ============================================================================
RBResult run_test(const RBTask& task, const std::vector<Tick>& ticks, int year) {
    RBResult r;
    r.label = task.label;
    r.survive_pct = task.survive_pct;
    r.tp_mode_str = TPModeToString(task.tp_mode);
    r.equilibrium_str = EquilibriumToString(task.equilibrium_type);
    r.year = year;
    r.return_mult = 0;
    r.max_dd_pct = 0;
    r.total_trades = 0;
    r.total_swap = 0;
    r.avg_deviation = 0;
    r.avg_tp = 0;
    r.sharpe_proxy = 0;
    r.stopped_out = false;

    // Set the relevant parameter for display
    if (task.tp_mode == StrategyRubberBandTP::LINEAR) {
        r.param = task.linear_scale;
    } else if (task.tp_mode == StrategyRubberBandTP::SQRT) {
        r.param = task.sqrt_scale;
    } else if (task.tp_mode == StrategyRubberBandTP::PROPORTIONAL) {
        r.param = task.reversion_pct;
    } else {
        r.param = 0;
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
    if (year == 2025) {
        cfg.start_date = "2025.01.01";
        cfg.end_date = "2025.12.30";
    } else {
        cfg.start_date = "2024.01.01";
        cfg.end_date = "2024.12.30";
    }
    cfg.verbose = false;

    TickDataConfig tc;
    tc.file_path = "";
    tc.load_all_into_memory = false;
    cfg.tick_data_config = tc;

    try {
        TickBasedEngine engine(cfg);

        StrategyRubberBandTP::Config strat_cfg;
        strat_cfg.survive_pct = (double)task.survive_pct;
        strat_cfg.base_spacing = 1.5;
        strat_cfg.min_volume = 0.01;
        strat_cfg.max_volume = 10.0;
        strat_cfg.contract_size = 100.0;
        strat_cfg.leverage = 500.0;
        strat_cfg.tp_mode = task.tp_mode;
        strat_cfg.equilibrium_type = task.equilibrium_type;
        strat_cfg.linear_scale = task.linear_scale;
        strat_cfg.sqrt_scale = task.sqrt_scale;
        strat_cfg.reversion_pct = task.reversion_pct;
        strat_cfg.volatility_lookback_hours = 4.0;
        strat_cfg.typical_vol_pct = 0.5;

        StrategyRubberBandTP strategy(strat_cfg);

        engine.RunWithTicks(ticks, [&strategy](const Tick& t, TickBasedEngine& e) {
            strategy.OnTick(t, e);
        });

        auto res = engine.GetResults();
        r.return_mult = res.final_balance / 10000.0;
        r.max_dd_pct = res.max_drawdown_pct;
        r.total_trades = res.total_trades;
        r.total_swap = res.total_swap_charged;
        r.avg_deviation = strategy.GetAverageDeviation();
        r.avg_tp = strategy.GetAverageTP();
        r.sharpe_proxy = (r.max_dd_pct > 0) ? (r.return_mult - 1.0) / (r.max_dd_pct / 100.0) : 0;
        r.stopped_out = res.stop_out_occurred;
    } catch (const std::exception& e) {
        r.stopped_out = true;
        r.max_dd_pct = 100.0;
    }

    return r;
}

void worker(WorkQueue& queue, int total) {
    RBTask task;
    while (queue.pop(task)) {
        const std::vector<Tick>& ticks = task.use_2025 ? g_ticks_2025 : g_ticks_2024;
        int year = task.use_2025 ? 2025 : 2024;

        RBResult r = run_test(task, ticks, year);

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
std::vector<RBTask> GenerateTasks() {
    std::vector<RBTask> tasks;

    // Parameter ranges
    std::vector<int> survive_pcts = {12, 13};
    std::vector<StrategyRubberBandTP::EquilibriumType> eq_types = {
        StrategyRubberBandTP::FIRST_ENTRY,
        StrategyRubberBandTP::EMA_200,
        StrategyRubberBandTP::EMA_500
    };
    std::vector<double> linear_scales = {0.5, 1.0, 1.5, 2.0};
    std::vector<double> sqrt_scales = {0.5, 1.0, 2.0};
    std::vector<double> reversion_pcts = {0.3, 0.5, 0.7, 1.0};

    // For each year (2024 and 2025)
    for (bool use_2025 : {true, false}) {
        int year = use_2025 ? 2025 : 2024;
        std::string yr = std::to_string(year);

        // For each survive_pct
        for (int survive : survive_pcts) {
            std::string sp = "s" + std::to_string(survive);

            // 1. BASELINE (FIXED mode) - only need one per survive/year combo
            RBTask baseline;
            baseline.survive_pct = survive;
            baseline.tp_mode = StrategyRubberBandTP::FIXED;
            baseline.equilibrium_type = StrategyRubberBandTP::FIRST_ENTRY;
            baseline.linear_scale = 0;
            baseline.sqrt_scale = 0;
            baseline.reversion_pct = 0;
            baseline.use_2025 = use_2025;
            baseline.label = yr + "_" + sp + "_FIXED";
            tasks.push_back(baseline);

            // 2. LINEAR mode
            for (auto eq : eq_types) {
                for (double scale : linear_scales) {
                    RBTask t;
                    t.survive_pct = survive;
                    t.tp_mode = StrategyRubberBandTP::LINEAR;
                    t.equilibrium_type = eq;
                    t.linear_scale = scale;
                    t.sqrt_scale = 0;
                    t.reversion_pct = 0;
                    t.use_2025 = use_2025;
                    t.label = yr + "_" + sp + "_LIN_" + EquilibriumToString(eq).substr(0, 4) +
                              "_" + std::to_string((int)(scale * 10));
                    tasks.push_back(t);
                }
            }

            // 3. SQRT mode
            for (auto eq : eq_types) {
                for (double scale : sqrt_scales) {
                    RBTask t;
                    t.survive_pct = survive;
                    t.tp_mode = StrategyRubberBandTP::SQRT;
                    t.equilibrium_type = eq;
                    t.linear_scale = 0;
                    t.sqrt_scale = scale;
                    t.reversion_pct = 0;
                    t.use_2025 = use_2025;
                    t.label = yr + "_" + sp + "_SQRT_" + EquilibriumToString(eq).substr(0, 4) +
                              "_" + std::to_string((int)(scale * 10));
                    tasks.push_back(t);
                }
            }

            // 4. PROPORTIONAL mode
            for (auto eq : eq_types) {
                for (double rev : reversion_pcts) {
                    RBTask t;
                    t.survive_pct = survive;
                    t.tp_mode = StrategyRubberBandTP::PROPORTIONAL;
                    t.equilibrium_type = eq;
                    t.linear_scale = 0;
                    t.sqrt_scale = 0;
                    t.reversion_pct = rev;
                    t.use_2025 = use_2025;
                    t.label = yr + "_" + sp + "_PROP_" + EquilibriumToString(eq).substr(0, 4) +
                              "_" + std::to_string((int)(rev * 100));
                    tasks.push_back(t);
                }
            }
        }
    }

    return tasks;
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "  RUBBER BAND TP PARALLEL SWEEP" << std::endl;
    std::cout << "  Concept: Scale TP with deviation from equilibrium" << std::endl;
    std::cout << "  Data: XAUUSD 2024 + 2025" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;

    // Step 1: Load tick data for both years
    std::string path_2025 = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    std::string path_2024 = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAUUSD_TICKS_2024.csv";

    try {
        LoadTickData(path_2025, g_ticks_2025, "2025");
        LoadTickData(path_2024, g_ticks_2024, "2024");
    } catch (const std::exception& e) {
        std::cerr << "Error loading tick data: " << e.what() << std::endl;
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
    std::cout << "Completed in " << duration.count() << " seconds" << std::endl;
    std::cout << std::endl;

    // ========================================================================
    // ANALYSIS
    // ========================================================================

    // Separate results by year
    std::vector<RBResult> results_2025, results_2024;
    for (const auto& r : g_results) {
        if (r.year == 2025) results_2025.push_back(r);
        else results_2024.push_back(r);
    }

    // Sort each by return
    std::sort(results_2025.begin(), results_2025.end(), [](const RBResult& a, const RBResult& b) {
        return a.return_mult > b.return_mult;
    });
    std::sort(results_2024.begin(), results_2024.end(), [](const RBResult& a, const RBResult& b) {
        return a.return_mult > b.return_mult;
    });

    // Helper to find baseline
    auto find_baseline = [](const std::vector<RBResult>& results, int survive) -> const RBResult* {
        for (const auto& r : results) {
            if (r.tp_mode_str == "FIXED" && r.survive_pct == survive) {
                return &r;
            }
        }
        return nullptr;
    };

    // ========================================================================
    // PRINT RESULTS FOR 2025
    // ========================================================================
    std::cout << "================================================================" << std::endl;
    std::cout << "  2025 RESULTS - TOP 25 BY RETURN" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::left << std::setw(4) << "#"
              << std::setw(32) << "Label"
              << std::right << std::setw(8) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(8) << "Trades"
              << std::setw(9) << "Swap$"
              << std::setw(8) << "AvgDev"
              << std::setw(8) << "AvgTP"
              << std::setw(8) << "Sharpe"
              << std::setw(5) << "SO"
              << std::endl;
    std::cout << std::string(98, '-') << std::endl;

    for (int i = 0; i < std::min(25, (int)results_2025.size()); i++) {
        const auto& r = results_2025[i];
        std::cout << std::left << std::setw(4) << (i + 1)
                  << std::setw(32) << r.label
                  << std::right << std::fixed
                  << std::setw(6) << std::setprecision(2) << r.return_mult << "x"
                  << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(8) << r.total_trades
                  << std::setw(8) << std::setprecision(0) << r.total_swap
                  << std::setw(8) << std::setprecision(2) << r.avg_deviation
                  << std::setw(8) << std::setprecision(2) << r.avg_tp
                  << std::setw(8) << std::setprecision(2) << r.sharpe_proxy
                  << std::setw(5) << (r.stopped_out ? "YES" : "")
                  << std::endl;
    }

    // Print baselines
    std::cout << std::string(98, '-') << std::endl;
    for (int survive : {12, 13}) {
        const RBResult* baseline = find_baseline(results_2025, survive);
        if (baseline) {
            std::cout << std::left << std::setw(4) << "BAS"
                      << std::setw(32) << baseline->label
                      << std::right << std::fixed
                      << std::setw(6) << std::setprecision(2) << baseline->return_mult << "x"
                      << std::setw(7) << std::setprecision(1) << baseline->max_dd_pct << "%"
                      << std::setw(8) << baseline->total_trades
                      << std::setw(8) << std::setprecision(0) << baseline->total_swap
                      << std::setw(8) << std::setprecision(2) << baseline->avg_deviation
                      << std::setw(8) << std::setprecision(2) << baseline->avg_tp
                      << std::setw(8) << std::setprecision(2) << baseline->sharpe_proxy
                      << std::setw(5) << (baseline->stopped_out ? "YES" : "")
                      << std::endl;
        }
    }

    // ========================================================================
    // PRINT RESULTS FOR 2024
    // ========================================================================
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  2024 RESULTS - TOP 25 BY RETURN" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::left << std::setw(4) << "#"
              << std::setw(32) << "Label"
              << std::right << std::setw(8) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(8) << "Trades"
              << std::setw(9) << "Swap$"
              << std::setw(8) << "AvgDev"
              << std::setw(8) << "AvgTP"
              << std::setw(8) << "Sharpe"
              << std::setw(5) << "SO"
              << std::endl;
    std::cout << std::string(98, '-') << std::endl;

    for (int i = 0; i < std::min(25, (int)results_2024.size()); i++) {
        const auto& r = results_2024[i];
        std::cout << std::left << std::setw(4) << (i + 1)
                  << std::setw(32) << r.label
                  << std::right << std::fixed
                  << std::setw(6) << std::setprecision(2) << r.return_mult << "x"
                  << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(8) << r.total_trades
                  << std::setw(8) << std::setprecision(0) << r.total_swap
                  << std::setw(8) << std::setprecision(2) << r.avg_deviation
                  << std::setw(8) << std::setprecision(2) << r.avg_tp
                  << std::setw(8) << std::setprecision(2) << r.sharpe_proxy
                  << std::setw(5) << (r.stopped_out ? "YES" : "")
                  << std::endl;
    }

    // Print baselines
    std::cout << std::string(98, '-') << std::endl;
    for (int survive : {12, 13}) {
        const RBResult* baseline = find_baseline(results_2024, survive);
        if (baseline) {
            std::cout << std::left << std::setw(4) << "BAS"
                      << std::setw(32) << baseline->label
                      << std::right << std::fixed
                      << std::setw(6) << std::setprecision(2) << baseline->return_mult << "x"
                      << std::setw(7) << std::setprecision(1) << baseline->max_dd_pct << "%"
                      << std::setw(8) << baseline->total_trades
                      << std::setw(8) << std::setprecision(0) << baseline->total_swap
                      << std::setw(8) << std::setprecision(2) << baseline->avg_deviation
                      << std::setw(8) << std::setprecision(2) << baseline->avg_tp
                      << std::setw(8) << std::setprecision(2) << baseline->sharpe_proxy
                      << std::setw(5) << (baseline->stopped_out ? "YES" : "")
                      << std::endl;
        }
    }

    // ========================================================================
    // COMPARISON BY TP MODE
    // ========================================================================
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  COMPARISON BY TP MODE (survive=13 only, best of each mode)" << std::endl;
    std::cout << "================================================================" << std::endl;

    auto find_best_by_mode = [](const std::vector<RBResult>& results, const std::string& mode, int survive) -> const RBResult* {
        const RBResult* best = nullptr;
        for (const auto& r : results) {
            if (r.tp_mode_str == mode && r.survive_pct == survive && !r.stopped_out) {
                if (!best || r.return_mult > best->return_mult) {
                    best = &r;
                }
            }
        }
        return best;
    };

    std::cout << std::left << std::setw(15) << "Mode"
              << std::setw(15) << "2025 Best"
              << std::right << std::setw(8) << "2025 Ret"
              << std::setw(8) << "2025 DD"
              << std::setw(15) << "  2024 Best"
              << std::setw(8) << "2024 Ret"
              << std::setw(8) << "2024 DD"
              << std::endl;
    std::cout << std::string(77, '-') << std::endl;

    for (const std::string& mode : {"FIXED", "LINEAR", "SQRT", "PROPORTIONAL"}) {
        const RBResult* best_2025 = find_best_by_mode(results_2025, mode, 13);
        const RBResult* best_2024 = find_best_by_mode(results_2024, mode, 13);

        std::cout << std::left << std::setw(15) << mode;

        if (best_2025) {
            std::string label_short = best_2025->label.substr(best_2025->label.find("_s13_") + 5);
            std::cout << std::setw(15) << label_short
                      << std::right << std::fixed
                      << std::setw(6) << std::setprecision(2) << best_2025->return_mult << "x"
                      << std::setw(7) << std::setprecision(1) << best_2025->max_dd_pct << "%";
        } else {
            std::cout << std::setw(15) << "N/A" << std::setw(8) << "" << std::setw(8) << "";
        }

        if (best_2024) {
            std::string label_short = best_2024->label.substr(best_2024->label.find("_s13_") + 5);
            std::cout << std::setw(15) << label_short
                      << std::right << std::fixed
                      << std::setw(6) << std::setprecision(2) << best_2024->return_mult << "x"
                      << std::setw(7) << std::setprecision(1) << best_2024->max_dd_pct << "%";
        } else {
            std::cout << std::setw(15) << "N/A" << std::setw(8) << "" << std::setw(8) << "";
        }

        std::cout << std::endl;
    }

    // ========================================================================
    // COMPARISON BY EQUILIBRIUM TYPE
    // ========================================================================
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  COMPARISON BY EQUILIBRIUM TYPE (survive=13, LINEAR mode)" << std::endl;
    std::cout << "================================================================" << std::endl;

    auto find_best_by_eq = [](const std::vector<RBResult>& results, const std::string& eq, int survive) -> const RBResult* {
        const RBResult* best = nullptr;
        for (const auto& r : results) {
            if (r.equilibrium_str == eq && r.survive_pct == survive && r.tp_mode_str == "LINEAR" && !r.stopped_out) {
                if (!best || r.return_mult > best->return_mult) {
                    best = &r;
                }
            }
        }
        return best;
    };

    std::cout << std::left << std::setw(15) << "Equilibrium"
              << std::right << std::setw(10) << "2025 Ret"
              << std::setw(8) << "2025 DD"
              << std::setw(10) << "2024 Ret"
              << std::setw(8) << "2024 DD"
              << std::setw(10) << "AvgDev25"
              << std::setw(10) << "AvgDev24"
              << std::endl;
    std::cout << std::string(71, '-') << std::endl;

    for (const std::string& eq : {"FIRST_ENTRY", "EMA_200", "EMA_500"}) {
        const RBResult* best_2025 = find_best_by_eq(results_2025, eq, 13);
        const RBResult* best_2024 = find_best_by_eq(results_2024, eq, 13);

        std::cout << std::left << std::setw(15) << eq;

        if (best_2025) {
            std::cout << std::right << std::fixed
                      << std::setw(8) << std::setprecision(2) << best_2025->return_mult << "x"
                      << std::setw(7) << std::setprecision(1) << best_2025->max_dd_pct << "%";
        } else {
            std::cout << std::setw(10) << "N/A" << std::setw(8) << "";
        }

        if (best_2024) {
            std::cout << std::right << std::fixed
                      << std::setw(8) << std::setprecision(2) << best_2024->return_mult << "x"
                      << std::setw(7) << std::setprecision(1) << best_2024->max_dd_pct << "%";
        } else {
            std::cout << std::setw(10) << "N/A" << std::setw(8) << "";
        }

        if (best_2025) {
            std::cout << std::setw(10) << std::setprecision(2) << best_2025->avg_deviation;
        } else {
            std::cout << std::setw(10) << "";
        }

        if (best_2024) {
            std::cout << std::setw(10) << std::setprecision(2) << best_2024->avg_deviation;
        } else {
            std::cout << std::setw(10) << "";
        }

        std::cout << std::endl;
    }

    // ========================================================================
    // KEY QUESTIONS ANALYSIS
    // ========================================================================
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  KEY FINDINGS" << std::endl;
    std::cout << "================================================================" << std::endl;

    // Q1: Does scaling TP with deviation improve returns?
    const RBResult* baseline_2025_13 = find_baseline(results_2025, 13);
    const RBResult* best_scaled_2025 = nullptr;
    for (const auto& r : results_2025) {
        if (r.tp_mode_str != "FIXED" && r.survive_pct == 13 && !r.stopped_out) {
            if (!best_scaled_2025 || r.return_mult > best_scaled_2025->return_mult) {
                best_scaled_2025 = &r;
            }
        }
    }

    std::cout << std::endl;
    std::cout << "Q1: Does scaling TP with deviation improve returns?" << std::endl;
    if (baseline_2025_13 && best_scaled_2025) {
        double improvement = (best_scaled_2025->return_mult - baseline_2025_13->return_mult) /
                             baseline_2025_13->return_mult * 100.0;
        std::cout << "    Baseline (FIXED):      " << std::fixed << std::setprecision(2)
                  << baseline_2025_13->return_mult << "x return, "
                  << std::setprecision(1) << baseline_2025_13->max_dd_pct << "% DD" << std::endl;
        std::cout << "    Best scaled (" << best_scaled_2025->tp_mode_str << "): "
                  << std::setprecision(2) << best_scaled_2025->return_mult << "x return, "
                  << std::setprecision(1) << best_scaled_2025->max_dd_pct << "% DD" << std::endl;
        std::cout << "    Improvement: " << std::setprecision(1) << improvement << "%" << std::endl;
        std::cout << "    Answer: " << (improvement > 5 ? "YES - meaningful improvement" :
                                        (improvement > 0 ? "MARGINAL - small improvement" : "NO - no improvement"))
                  << std::endl;
    }

    // Q2: Which TP mode works best?
    std::cout << std::endl;
    std::cout << "Q2: Which TP mode works best?" << std::endl;

    struct ModeStats {
        std::string mode;
        double avg_return_2025;
        double avg_return_2024;
        int count;
    };

    std::vector<ModeStats> mode_stats;
    for (const std::string& mode : {"FIXED", "LINEAR", "SQRT", "PROPORTIONAL"}) {
        ModeStats ms;
        ms.mode = mode;
        ms.avg_return_2025 = 0;
        ms.avg_return_2024 = 0;
        ms.count = 0;

        double sum_2025 = 0, sum_2024 = 0;
        int count_2025 = 0, count_2024 = 0;

        for (const auto& r : results_2025) {
            if (r.tp_mode_str == mode && r.survive_pct == 13 && !r.stopped_out) {
                sum_2025 += r.return_mult;
                count_2025++;
            }
        }
        for (const auto& r : results_2024) {
            if (r.tp_mode_str == mode && r.survive_pct == 13 && !r.stopped_out) {
                sum_2024 += r.return_mult;
                count_2024++;
            }
        }

        if (count_2025 > 0) ms.avg_return_2025 = sum_2025 / count_2025;
        if (count_2024 > 0) ms.avg_return_2024 = sum_2024 / count_2024;
        ms.count = count_2025;
        mode_stats.push_back(ms);
    }

    std::cout << std::left << std::setw(15) << "    Mode"
              << std::right << std::setw(12) << "Avg 2025"
              << std::setw(12) << "Avg 2024"
              << std::setw(10) << "Configs" << std::endl;
    for (const auto& ms : mode_stats) {
        std::cout << std::left << std::setw(15) << ("    " + ms.mode)
                  << std::right << std::fixed
                  << std::setw(10) << std::setprecision(2) << ms.avg_return_2025 << "x"
                  << std::setw(10) << std::setprecision(2) << ms.avg_return_2024 << "x"
                  << std::setw(10) << ms.count << std::endl;
    }

    // Q3: Average TP comparison
    std::cout << std::endl;
    std::cout << "Q3: Does targeting larger reversions from deep positions pay off?" << std::endl;
    if (baseline_2025_13 && best_scaled_2025) {
        std::cout << "    Baseline avg TP:    $" << std::fixed << std::setprecision(2)
                  << baseline_2025_13->avg_tp << std::endl;
        std::cout << "    Best scaled avg TP: $" << best_scaled_2025->avg_tp << std::endl;
        std::cout << "    Avg deviation:      $" << best_scaled_2025->avg_deviation << std::endl;
        double tp_increase = (best_scaled_2025->avg_tp - baseline_2025_13->avg_tp) /
                             baseline_2025_13->avg_tp * 100.0;
        std::cout << "    TP increase:        " << std::setprecision(1) << tp_increase << "%" << std::endl;
    }

    std::cout << std::endl;
    std::cout << "Total configurations tested: " << g_results.size() << std::endl;
    std::cout << "Execution time: " << duration.count() << "s (" << num_threads << " threads)" << std::endl;

    return 0;
}
