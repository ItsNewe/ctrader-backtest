/**
 * Barbell Sizing Parallel Parameter Sweep
 *
 * Tests Taleb's barbell concept applied to grid trading:
 * - Small positions early (near mean) = SAFE end
 * - Large positions at deep deviations = SPECULATIVE end
 * - Creates convex payoff: limited early losses, amplified deep reversion gains
 *
 * Loads tick data ONCE into shared memory, then tests all configurations in parallel.
 *
 * KEY QUESTIONS:
 * 1. Does barbell sizing create convex payoffs (better risk-adjusted returns)?
 * 2. Does it reduce DD while maintaining returns?
 * 3. Which sizing mode works best?
 * 4. Does committing more at deep deviations pay off?
 */

#include "../include/strategy_barbell_sizing.h"
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

void LoadTickData(const std::string& path, std::vector<Tick>& dest, const std::string& label) {
    std::cout << "Loading " << label << " tick data..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open tick file: " + path);
    }

    std::string line;
    std::getline(file, line);  // Skip header

    dest.reserve(55000000);

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
struct BarbellTask {
    int mode;                   // 0=UNIFORM, 1=LINEAR, 2=EXPONENTIAL, 3=THRESHOLD
    double scale_factor;        // LINEAR: multiplier per position
    double exponent;            // EXPONENTIAL: power
    int threshold_position;     // THRESHOLD: after N positions
    double threshold_mult;      // THRESHOLD: multiplier
    double survive_pct;
    double base_spacing;
    int year;                   // 2024 or 2025
    std::string label;
};

struct BarbellResult {
    std::string label;
    int mode;
    double scale_factor;
    double exponent;
    int threshold_position;
    double threshold_mult;
    double survive_pct;
    int year;
    double return_mult;
    double max_dd_pct;
    int total_trades;
    double total_swap;
    double sharpe_proxy;
    int max_positions;
    double avg_lot_size;
    double total_lots_opened;
    bool stopped_out;
};

// ============================================================================
// Work Queue for thread pool
// ============================================================================
class WorkQueue {
    std::queue<BarbellTask> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool done_ = false;

public:
    void push(const BarbellTask& task) {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push(task);
        cv_.notify_one();
    }

    bool pop(BarbellTask& task) {
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
std::vector<BarbellResult> g_results;

// ============================================================================
// Run single test with shared tick data
// ============================================================================
BarbellResult run_test(const BarbellTask& task, const std::vector<Tick>& ticks) {
    BarbellResult r;
    r.label = task.label;
    r.mode = task.mode;
    r.scale_factor = task.scale_factor;
    r.exponent = task.exponent;
    r.threshold_position = task.threshold_position;
    r.threshold_mult = task.threshold_mult;
    r.survive_pct = task.survive_pct;
    r.year = task.year;
    r.return_mult = 0;
    r.max_dd_pct = 0;
    r.total_trades = 0;
    r.total_swap = 0;
    r.sharpe_proxy = 0;
    r.max_positions = 0;
    r.avg_lot_size = 0;
    r.total_lots_opened = 0;
    r.stopped_out = false;

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

    if (task.year == 2025) {
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

        // Create barbell config
        StrategyBarbellSizing::BarbellConfig bb_cfg;
        bb_cfg.mode = static_cast<StrategyBarbellSizing::SizingMode>(task.mode);
        bb_cfg.scale_factor = task.scale_factor;
        bb_cfg.exponent = task.exponent;
        bb_cfg.threshold_position = task.threshold_position;
        bb_cfg.threshold_multiplier = task.threshold_mult;

        StrategyBarbellSizing strategy(
            task.survive_pct,
            task.base_spacing,
            0.01,   // min_volume
            10.0,   // max_volume
            100.0,  // contract_size
            500.0,  // leverage
            4.0,    // volatility_lookback_hours
            bb_cfg
        );

        engine.RunWithTicks(ticks, [&strategy](const Tick& t, TickBasedEngine& e) {
            strategy.OnTick(t, e);
        });

        auto res = engine.GetResults();
        r.return_mult = res.final_balance / 10000.0;
        r.max_dd_pct = res.max_drawdown_pct;
        r.total_trades = res.total_trades;
        r.total_swap = res.total_swap_charged;
        r.sharpe_proxy = (r.max_dd_pct > 0) ? (r.return_mult - 1.0) / (r.max_dd_pct / 100.0) : 0;
        r.max_positions = strategy.GetMaxPositionCount();
        r.avg_lot_size = strategy.GetAverageLotSize();
        r.total_lots_opened = strategy.GetTotalLotOpened();
        r.stopped_out = (res.final_balance < 100);  // margin stop-out
    } catch (const std::exception& e) {
        r.stopped_out = true;
        r.max_dd_pct = 100.0;
    }

    return r;
}

void worker(WorkQueue& queue, int total) {
    BarbellTask task;
    while (queue.pop(task)) {
        const std::vector<Tick>& ticks = (task.year == 2025) ? g_shared_ticks_2025 : g_shared_ticks_2024;
        BarbellResult r = run_test(task, ticks);

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
std::vector<BarbellTask> GenerateTasks() {
    std::vector<BarbellTask> tasks;

    std::vector<double> survive_pcts = {12.0, 13.0};
    std::vector<int> years = {2024, 2025};
    double base_spacing = 1.5;

    for (int year : years) {
        for (double survive : survive_pcts) {
            std::string suffix = "_s" + std::to_string((int)survive) + "_" + std::to_string(year);

            // UNIFORM (baseline)
            tasks.push_back({0, 0, 0, 0, 0, survive, base_spacing, year, "UNIFORM" + suffix});

            // LINEAR: sweep scale_factor
            for (double sf : {0.1, 0.2, 0.5, 1.0}) {
                tasks.push_back({1, sf, 0, 0, 0, survive, base_spacing, year,
                    "LINEAR_sf" + std::to_string(sf).substr(0,3) + suffix});
            }

            // EXPONENTIAL: sweep exponent
            for (double exp : {1.5, 2.0, 2.5}) {
                tasks.push_back({2, 0, exp, 0, 0, survive, base_spacing, year,
                    "EXP_e" + std::to_string(exp).substr(0,3) + suffix});
            }

            // THRESHOLD: sweep position and multiplier
            for (int thresh_pos : {3, 5, 7}) {
                for (double thresh_mult : {2.0, 3.0, 5.0}) {
                    tasks.push_back({3, 0, 0, thresh_pos, thresh_mult, survive, base_spacing, year,
                        "THRESH_p" + std::to_string(thresh_pos) + "_m" +
                        std::to_string((int)thresh_mult) + suffix});
                }
            }
        }
    }

    return tasks;
}

std::string ModeName(int mode) {
    switch (mode) {
        case 0: return "UNIFORM";
        case 1: return "LINEAR";
        case 2: return "EXPONENTIAL";
        case 3: return "THRESHOLD";
        default: return "UNKNOWN";
    }
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "  BARBELL SIZING PARALLEL SWEEP" << std::endl;
    std::cout << "  Testing Taleb's asymmetric sizing concept" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;

    // Step 1: Load tick data for both years
    try {
        LoadTickData("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv",
                     g_shared_ticks_2025, "2025");
        LoadTickData("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAUUSD_TICKS_2024.csv",
                     g_shared_ticks_2024, "2024");
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
    std::vector<BarbellResult> results_2025, results_2024;
    for (const auto& r : g_results) {
        if (r.year == 2025) results_2025.push_back(r);
        else results_2024.push_back(r);
    }

    // Sort each by return
    std::sort(results_2025.begin(), results_2025.end(),
        [](const auto& a, const auto& b) { return a.return_mult > b.return_mult; });
    std::sort(results_2024.begin(), results_2024.end(),
        [](const auto& a, const auto& b) { return a.return_mult > b.return_mult; });

    // Print 2025 results
    std::cout << "================================================================" << std::endl;
    std::cout << "  2025 RESULTS (sorted by return)" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::left << std::setw(4) << "#"
              << std::setw(32) << "Config"
              << std::right << std::setw(9) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(8) << "Trades"
              << std::setw(9) << "Swap$"
              << std::setw(8) << "Sharpe"
              << std::setw(9) << "MaxPos"
              << std::setw(9) << "AvgLot"
              << std::endl;
    std::cout << std::string(100, '-') << std::endl;

    for (int i = 0; i < std::min(30, (int)results_2025.size()); i++) {
        const auto& r = results_2025[i];
        std::cout << std::left << std::setw(4) << (i + 1)
                  << std::setw(32) << r.label
                  << std::right << std::fixed
                  << std::setw(7) << std::setprecision(2) << r.return_mult << "x"
                  << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(8) << r.total_trades
                  << std::setw(8) << std::setprecision(0) << r.total_swap
                  << std::setw(8) << std::setprecision(2) << r.sharpe_proxy
                  << std::setw(9) << r.max_positions
                  << std::setw(9) << std::setprecision(3) << r.avg_lot_size
                  << (r.stopped_out ? " SO" : "")
                  << std::endl;
    }

    // Print 2024 results
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  2024 RESULTS (sorted by return)" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::left << std::setw(4) << "#"
              << std::setw(32) << "Config"
              << std::right << std::setw(9) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(8) << "Trades"
              << std::setw(9) << "Swap$"
              << std::setw(8) << "Sharpe"
              << std::setw(9) << "MaxPos"
              << std::setw(9) << "AvgLot"
              << std::endl;
    std::cout << std::string(100, '-') << std::endl;

    for (int i = 0; i < std::min(30, (int)results_2024.size()); i++) {
        const auto& r = results_2024[i];
        std::cout << std::left << std::setw(4) << (i + 1)
                  << std::setw(32) << r.label
                  << std::right << std::fixed
                  << std::setw(7) << std::setprecision(2) << r.return_mult << "x"
                  << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(8) << r.total_trades
                  << std::setw(8) << std::setprecision(0) << r.total_swap
                  << std::setw(8) << std::setprecision(2) << r.sharpe_proxy
                  << std::setw(9) << r.max_positions
                  << std::setw(9) << std::setprecision(3) << r.avg_lot_size
                  << (r.stopped_out ? " SO" : "")
                  << std::endl;
    }

    // ========================================================================
    // Comparison by mode
    // ========================================================================
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  COMPARISON BY MODE (best of each)" << std::endl;
    std::cout << "================================================================" << std::endl;

    // Find baselines
    auto find_baseline = [](const std::vector<BarbellResult>& results, double survive) -> const BarbellResult* {
        for (const auto& r : results) {
            if (r.mode == 0 && std::abs(r.survive_pct - survive) < 0.1) return &r;
        }
        return nullptr;
    };

    // Find best by mode for each survive_pct and year
    auto find_best_by_mode = [](const std::vector<BarbellResult>& results, int mode, double survive) {
        const BarbellResult* best = nullptr;
        for (const auto& r : results) {
            if (r.mode == mode && std::abs(r.survive_pct - survive) < 0.1 && !r.stopped_out) {
                if (!best || r.sharpe_proxy > best->sharpe_proxy) {
                    best = &r;
                }
            }
        }
        return best;
    };

    std::cout << std::left << std::setw(12) << "Year"
              << std::setw(10) << "Survive"
              << std::setw(12) << "Mode"
              << std::setw(28) << "Best Config"
              << std::right << std::setw(9) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(8) << "Sharpe"
              << std::setw(10) << "vs Base"
              << std::endl;
    std::cout << std::string(100, '-') << std::endl;

    for (int year : {2025, 2024}) {
        const auto& results = (year == 2025) ? results_2025 : results_2024;

        for (double survive : {12.0, 13.0}) {
            const BarbellResult* baseline = find_baseline(results, survive);
            double base_return = baseline ? baseline->return_mult : 1.0;
            double base_sharpe = baseline ? baseline->sharpe_proxy : 0.0;

            for (int mode = 0; mode <= 3; mode++) {
                const BarbellResult* best = find_best_by_mode(results, mode, survive);
                if (best) {
                    double vs_base_return = (base_return > 0) ? (best->return_mult / base_return - 1.0) * 100.0 : 0;
                    std::cout << std::left << std::setw(12) << year
                              << std::setw(10) << survive
                              << std::setw(12) << ModeName(mode)
                              << std::setw(28) << best->label
                              << std::right << std::fixed
                              << std::setw(7) << std::setprecision(2) << best->return_mult << "x"
                              << std::setw(7) << std::setprecision(1) << best->max_dd_pct << "%"
                              << std::setw(8) << std::setprecision(2) << best->sharpe_proxy
                              << std::setw(7) << std::setprecision(1) << std::showpos << vs_base_return << "%" << std::noshowpos
                              << std::endl;
                }
            }
            std::cout << std::endl;
        }
    }

    // ========================================================================
    // Convexity Analysis: Does larger sizing at deep deviations pay off?
    // ========================================================================
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  CONVEXITY ANALYSIS" << std::endl;
    std::cout << "  Key question: Do larger positions at deep deviations help?" << std::endl;
    std::cout << "================================================================" << std::endl;

    // Calculate average stats by mode
    struct ModeStats {
        double avg_return = 0;
        double avg_dd = 0;
        double avg_sharpe = 0;
        double avg_lots = 0;
        int count = 0;
        int stopped_out = 0;
    };

    std::map<std::string, ModeStats> mode_stats;

    for (const auto& r : g_results) {
        std::string key = ModeName(r.mode) + "_" + std::to_string(r.year);
        auto& stats = mode_stats[key];
        if (!r.stopped_out) {
            stats.avg_return += r.return_mult;
            stats.avg_dd += r.max_dd_pct;
            stats.avg_sharpe += r.sharpe_proxy;
            stats.avg_lots += r.total_lots_opened;
            stats.count++;
        } else {
            stats.stopped_out++;
        }
    }

    std::cout << std::left << std::setw(20) << "Mode_Year"
              << std::right << std::setw(10) << "AvgReturn"
              << std::setw(10) << "AvgDD%"
              << std::setw(10) << "AvgSharpe"
              << std::setw(12) << "AvgTotLots"
              << std::setw(8) << "Count"
              << std::setw(8) << "StopOut"
              << std::endl;
    std::cout << std::string(80, '-') << std::endl;

    for (auto& [key, stats] : mode_stats) {
        if (stats.count > 0) {
            stats.avg_return /= stats.count;
            stats.avg_dd /= stats.count;
            stats.avg_sharpe /= stats.count;
            stats.avg_lots /= stats.count;
        }
        std::cout << std::left << std::setw(20) << key
                  << std::right << std::fixed
                  << std::setw(9) << std::setprecision(2) << stats.avg_return << "x"
                  << std::setw(9) << std::setprecision(1) << stats.avg_dd << "%"
                  << std::setw(10) << std::setprecision(2) << stats.avg_sharpe
                  << std::setw(12) << std::setprecision(1) << stats.avg_lots
                  << std::setw(8) << stats.count
                  << std::setw(8) << stats.stopped_out
                  << std::endl;
    }

    // ========================================================================
    // Summary and Conclusions
    // ========================================================================
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  SUMMARY" << std::endl;
    std::cout << "================================================================" << std::endl;

    // Find overall best
    const BarbellResult* best_2025 = nullptr;
    const BarbellResult* best_2024 = nullptr;

    for (const auto& r : results_2025) {
        if (!r.stopped_out && (!best_2025 || r.sharpe_proxy > best_2025->sharpe_proxy)) {
            best_2025 = &r;
        }
    }
    for (const auto& r : results_2024) {
        if (!r.stopped_out && (!best_2024 || r.sharpe_proxy > best_2024->sharpe_proxy)) {
            best_2024 = &r;
        }
    }

    if (best_2025) {
        std::cout << "Best 2025 (by Sharpe): " << best_2025->label << std::endl;
        std::cout << "  Return: " << std::fixed << std::setprecision(2) << best_2025->return_mult << "x"
                  << "  MaxDD: " << std::setprecision(1) << best_2025->max_dd_pct << "%"
                  << "  Sharpe: " << std::setprecision(2) << best_2025->sharpe_proxy << std::endl;
    }

    if (best_2024) {
        std::cout << "Best 2024 (by Sharpe): " << best_2024->label << std::endl;
        std::cout << "  Return: " << std::fixed << std::setprecision(2) << best_2024->return_mult << "x"
                  << "  MaxDD: " << std::setprecision(1) << best_2024->max_dd_pct << "%"
                  << "  Sharpe: " << std::setprecision(2) << best_2024->sharpe_proxy << std::endl;
    }

    std::cout << std::endl;
    std::cout << "Total configurations tested: " << g_results.size() << std::endl;
    std::cout << "Execution time: " << duration.count() << "s (" << num_threads << " threads)" << std::endl;

    return 0;
}
