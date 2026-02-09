/**
 * XAUUSD Deep Parameter Sweep
 *
 * Thorough exploration of FillUpOscillation parameter space on XAUUSD 2025.
 * Fixes survive_pct at 12/13%, varies all other parameters including
 * unconventional values to build understanding of the parameter landscape.
 *
 * Groups:
 *   1. Core sweep: spacing × lookback × typvol (3,024 configs)
 *   2. Multiplier variations: min/max spacing mult combos (50 configs)
 *   3. BASELINE mode: fixed spacing, no adaptive (24 configs)
 *   4. Extreme/unconventional: edge cases and weird combos (80 configs)
 *
 * Uses parallel sweep pattern: load 52M ticks once, run all configs on 16 threads.
 */

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
// Shared tick data
// ============================================================================
std::vector<Tick> g_shared_ticks;

void LoadTickDataOnce(const std::string& path) {
    std::cout << "Loading tick data into memory..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open tick file: " + path);
    }

    std::string line;
    std::getline(file, line);  // Skip header

    g_shared_ticks.reserve(52000000);

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
              << duration.count() << "s" << std::endl;
    std::cout << "Memory: ~" << (g_shared_ticks.size() * sizeof(Tick) / 1024 / 1024)
              << " MB" << std::endl << std::endl;
}

// ============================================================================
// Task structure
// ============================================================================
struct SweepTask {
    double survive_pct;
    double base_spacing;
    double lookback_hours;
    double typical_vol_pct;
    double min_spacing_mult;
    double max_spacing_mult;
    double min_spacing_abs;
    double max_spacing_abs;
    double spacing_change_thresh;
    FillUpOscillation::Mode mode;
    std::string group;     // Which sweep group this belongs to
    std::string label;
};

struct SweepResult {
    std::string label;
    std::string group;
    double survive_pct;
    double base_spacing;
    double lookback_hours;
    double typical_vol_pct;
    double min_spacing_mult;
    double max_spacing_mult;
    double return_mult;
    double max_dd_pct;
    int total_trades;
    double total_swap;
    double sharpe_proxy;
    double final_balance;
    bool stopped_out;
};

// ============================================================================
// Work Queue
// ============================================================================
class WorkQueue {
    std::queue<SweepTask> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool done_ = false;

public:
    void push(const SweepTask& task) {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push(task);
        cv_.notify_one();
    }

    bool pop(SweepTask& task) {
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
// Globals
// ============================================================================
std::atomic<int> g_completed{0};
std::mutex g_results_mutex;
std::vector<SweepResult> g_results;

// ============================================================================
// Run single config
// ============================================================================
SweepResult run_test(const SweepTask& task, const std::vector<Tick>& ticks) {
    SweepResult r;
    r.label = task.label;
    r.group = task.group;
    r.survive_pct = task.survive_pct;
    r.base_spacing = task.base_spacing;
    r.lookback_hours = task.lookback_hours;
    r.typical_vol_pct = task.typical_vol_pct;
    r.min_spacing_mult = task.min_spacing_mult;
    r.max_spacing_mult = task.max_spacing_mult;
    r.return_mult = 0;
    r.max_dd_pct = 100.0;
    r.total_trades = 0;
    r.total_swap = 0;
    r.sharpe_proxy = 0;
    r.final_balance = 0;
    r.stopped_out = true;

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

        FillUpOscillation::AdaptiveConfig acfg;
        acfg.typical_vol_pct = task.typical_vol_pct;
        acfg.min_spacing_mult = task.min_spacing_mult;
        acfg.max_spacing_mult = task.max_spacing_mult;
        acfg.min_spacing_abs = task.min_spacing_abs;
        acfg.max_spacing_abs = task.max_spacing_abs;
        acfg.spacing_change_threshold = task.spacing_change_thresh;
        acfg.pct_spacing = false;  // Absolute $ spacing for XAUUSD

        FillUpOscillation strategy(
            task.survive_pct,
            task.base_spacing,
            0.01,   // min_volume
            10.0,   // max_volume
            100.0,  // contract_size
            500.0,  // leverage
            task.mode,
            0.1,    // antifragile_scale (unused for ADAPTIVE_SPACING)
            30.0,   // velocity_threshold (unused)
            task.lookback_hours,
            acfg
        );

        engine.RunWithTicks(ticks, [&strategy](const Tick& t, TickBasedEngine& e) {
            strategy.OnTick(t, e);
        });

        auto res = engine.GetResults();
        r.return_mult = res.final_balance / 10000.0;
        r.max_dd_pct = res.max_drawdown_pct;
        r.total_trades = res.total_trades;
        r.total_swap = res.total_swap_charged;
        r.final_balance = res.final_balance;
        r.stopped_out = (res.final_balance < 100.0);  // Effectively stopped out
        r.sharpe_proxy = (r.max_dd_pct > 0) ? (r.return_mult - 1.0) / (r.max_dd_pct / 100.0) : 0;
    } catch (const std::exception& e) {
        // Stopped out or error
        r.stopped_out = true;
        r.max_dd_pct = 100.0;
    }

    return r;
}

// ============================================================================
// Worker thread
// ============================================================================
void worker(WorkQueue& queue, int total, const std::vector<Tick>& ticks) {
    SweepTask task;
    while (queue.pop(task)) {
        SweepResult r = run_test(task, ticks);

        {
            std::lock_guard<std::mutex> lock(g_results_mutex);
            g_results.push_back(r);
        }

        int done = ++g_completed;
        if (done % 50 == 0 || done == total) {
            std::cout << "\r  Progress: " << done << "/" << total
                      << " (" << (done * 100 / total) << "%)" << std::flush;
        }
    }
}

// ============================================================================
// Parameter grids (file-level for use in both generate_tasks and analysis)
// ============================================================================
const std::vector<double> g_survives = {12.0, 13.0};
const std::vector<double> g_spacings = {
    0.05, 0.10, 0.25, 0.50, 0.75, 1.00,
    1.50, 2.00, 3.00, 5.00, 8.00, 12.00, 20.00, 50.00
};
const std::vector<double> g_lookbacks = {
    0.1, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 24.0, 48.0, 96.0, 168.0
};
const std::vector<double> g_typvols = {
    0.01, 0.05, 0.15, 0.30, 0.55, 0.80, 1.50, 3.00, 8.00
};

// ============================================================================
// Task generation
// ============================================================================
std::vector<SweepTask> generate_tasks() {
    std::vector<SweepTask> tasks;

    for (double surv : g_survives) {
        for (double sp : g_spacings) {
            for (double lb : g_lookbacks) {
                for (double tv : g_typvols) {
                    SweepTask t;
                    t.survive_pct = surv;
                    t.base_spacing = sp;
                    t.lookback_hours = lb;
                    t.typical_vol_pct = tv;
                    t.min_spacing_mult = 0.5;
                    t.max_spacing_mult = 3.0;
                    t.min_spacing_abs = 0.05;
                    t.max_spacing_abs = 100.0;
                    t.spacing_change_thresh = 0.1;
                    t.mode = FillUpOscillation::ADAPTIVE_SPACING;
                    t.group = "CORE";

                    std::ostringstream oss;
                    oss << "s" << (int)surv
                        << "_sp" << std::fixed << std::setprecision(2) << sp
                        << "_lb" << std::setprecision(1) << lb
                        << "_tv" << std::setprecision(2) << tv;
                    t.label = oss.str();

                    tasks.push_back(t);
                }
            }
        }
    }

    // ========================================================================
    // GROUP 2: Multiplier variations
    // How much does the adaptive clamp range matter?
    // ========================================================================
    std::vector<double> mult_spacings = {0.50, 1.00, 1.50, 2.00, 3.00};
    struct MultCombo { double min_m; double max_m; std::string label; };
    std::vector<MultCombo> mult_combos = {
        {0.1, 10.0, "wide"},      // Very wide adaptive range
        {0.3,  5.0, "broad"},     // Broad
        {0.5,  3.0, "standard"},  // Standard (baseline)
        {0.8,  1.5, "tight"},     // Tight - minimal adaptation
        {1.0,  1.0, "none"},      // No adaptation (fixed spacing regardless of vol)
    };

    for (double surv : g_survives) {
        for (double sp : mult_spacings) {
            for (const auto& mc : mult_combos) {
                SweepTask t;
                t.survive_pct = surv;
                t.base_spacing = sp;
                t.lookback_hours = 4.0;
                t.typical_vol_pct = 0.55;
                t.min_spacing_mult = mc.min_m;
                t.max_spacing_mult = mc.max_m;
                t.min_spacing_abs = 0.05;
                t.max_spacing_abs = 100.0;
                t.spacing_change_thresh = 0.1;
                t.mode = FillUpOscillation::ADAPTIVE_SPACING;
                t.group = "MULT";

                std::ostringstream oss;
                oss << "s" << (int)surv
                    << "_sp" << std::fixed << std::setprecision(2) << sp
                    << "_mult_" << mc.label;
                t.label = oss.str();

                tasks.push_back(t);
            }
        }
    }

    // ========================================================================
    // GROUP 3: BASELINE mode (no adaptive spacing at all)
    // Pure fixed-spacing grid for comparison
    // ========================================================================
    std::vector<double> baseline_spacings = {
        0.10, 0.25, 0.50, 0.75, 1.00, 1.50,
        2.00, 3.00, 5.00, 8.00, 12.00, 20.00
    };

    for (double surv : g_survives) {
        for (double sp : baseline_spacings) {
            SweepTask t;
            t.survive_pct = surv;
            t.base_spacing = sp;
            t.lookback_hours = 4.0;
            t.typical_vol_pct = 0.55;
            t.min_spacing_mult = 0.5;
            t.max_spacing_mult = 3.0;
            t.min_spacing_abs = 0.05;
            t.max_spacing_abs = 100.0;
            t.spacing_change_thresh = 0.1;
            t.mode = FillUpOscillation::BASELINE;
            t.group = "BASELINE";

            std::ostringstream oss;
            oss << "s" << (int)surv
                << "_sp" << std::fixed << std::setprecision(2) << sp
                << "_BASELINE";
            t.label = oss.str();

            tasks.push_back(t);
        }
    }

    // ========================================================================
    // GROUP 4: Extreme/unconventional combinations
    // Things that "shouldn't work" but might reveal interesting dynamics
    // ========================================================================

    // 4a: Inverted vol assumption - pretend market is 20x calmer than reality
    //     This forces spacing to always be at max_spacing_mult
    for (double surv : g_survives) {
        for (double sp : {0.50, 1.00, 1.50, 2.00, 5.00}) {
            SweepTask t;
            t.survive_pct = surv;
            t.base_spacing = sp;
            t.lookback_hours = 4.0;
            t.typical_vol_pct = 20.0;  // 20% typical vol = always "quiet"
            t.min_spacing_mult = 0.5;
            t.max_spacing_mult = 3.0;
            t.min_spacing_abs = 0.05;
            t.max_spacing_abs = 100.0;
            t.spacing_change_thresh = 0.1;
            t.mode = FillUpOscillation::ADAPTIVE_SPACING;
            t.group = "EXTREME";
            t.label = "s" + std::to_string((int)surv) + "_sp" +
                      std::to_string(sp).substr(0,4) + "_highTV20";
            tasks.push_back(t);
        }
    }

    // 4b: Ultra-reactive lookback (6 minutes) with various spacings
    for (double surv : g_survives) {
        for (double sp : {0.25, 0.50, 1.00, 1.50, 3.00}) {
            SweepTask t;
            t.survive_pct = surv;
            t.base_spacing = sp;
            t.lookback_hours = 0.1;  // 6 minutes
            t.typical_vol_pct = 0.05; // Matched to 6-min range
            t.min_spacing_mult = 0.5;
            t.max_spacing_mult = 3.0;
            t.min_spacing_abs = 0.05;
            t.max_spacing_abs = 100.0;
            t.spacing_change_thresh = 0.05;
            t.mode = FillUpOscillation::ADAPTIVE_SPACING;
            t.group = "EXTREME";
            t.label = "s" + std::to_string((int)surv) + "_sp" +
                      std::to_string(sp).substr(0,4) + "_ultraReactive";
            tasks.push_back(t);
        }
    }

    // 4c: Week-long lookback (168h) - should this be stable or just stale?
    for (double surv : g_survives) {
        for (double sp : {0.50, 1.00, 1.50, 2.00, 5.00}) {
            SweepTask t;
            t.survive_pct = surv;
            t.base_spacing = sp;
            t.lookback_hours = 168.0;  // 1 week
            t.typical_vol_pct = 3.0;   // Weekly range is ~3% for gold
            t.min_spacing_mult = 0.5;
            t.max_spacing_mult = 3.0;
            t.min_spacing_abs = 0.05;
            t.max_spacing_abs = 100.0;
            t.spacing_change_thresh = 0.5;
            t.mode = FillUpOscillation::ADAPTIVE_SPACING;
            t.group = "EXTREME";
            t.label = "s" + std::to_string((int)surv) + "_sp" +
                      std::to_string(sp).substr(0,4) + "_weeklyLB";
            tasks.push_back(t);
        }
    }

    // 4d: Extremely wide adaptive range (0.01x to 100x base spacing)
    for (double surv : g_survives) {
        for (double sp : {0.50, 1.00, 1.50, 3.00}) {
            SweepTask t;
            t.survive_pct = surv;
            t.base_spacing = sp;
            t.lookback_hours = 4.0;
            t.typical_vol_pct = 0.55;
            t.min_spacing_mult = 0.01;
            t.max_spacing_mult = 100.0;
            t.min_spacing_abs = 0.01;
            t.max_spacing_abs = 500.0;
            t.spacing_change_thresh = 0.01;
            t.mode = FillUpOscillation::ADAPTIVE_SPACING;
            t.group = "EXTREME";
            t.label = "s" + std::to_string((int)surv) + "_sp" +
                      std::to_string(sp).substr(0,4) + "_ultraWideMult";
            tasks.push_back(t);
        }
    }

    // 4e: Spacing change threshold = 0 (update every tick) vs very high (never change)
    for (double surv : g_survives) {
        // Zero threshold: spacing changes every tick
        SweepTask t1;
        t1.survive_pct = surv;
        t1.base_spacing = 1.50;
        t1.lookback_hours = 4.0;
        t1.typical_vol_pct = 0.55;
        t1.min_spacing_mult = 0.5;
        t1.max_spacing_mult = 3.0;
        t1.min_spacing_abs = 0.05;
        t1.max_spacing_abs = 100.0;
        t1.spacing_change_thresh = 0.0;  // Every tick
        t1.mode = FillUpOscillation::ADAPTIVE_SPACING;
        t1.group = "EXTREME";
        t1.label = "s" + std::to_string((int)surv) + "_thresh0_everyTick";
        tasks.push_back(t1);

        // Huge threshold: effectively never changes
        SweepTask t2;
        t2.survive_pct = surv;
        t2.base_spacing = 1.50;
        t2.lookback_hours = 4.0;
        t2.typical_vol_pct = 0.55;
        t2.min_spacing_mult = 0.5;
        t2.max_spacing_mult = 3.0;
        t2.min_spacing_abs = 0.05;
        t2.max_spacing_abs = 100.0;
        t2.spacing_change_thresh = 1000.0;  // Never changes
        t2.mode = FillUpOscillation::ADAPTIVE_SPACING;
        t2.group = "EXTREME";
        t2.label = "s" + std::to_string((int)surv) + "_thresh1000_neverChange";
        tasks.push_back(t2);
    }

    // 4f: Very tight min_spacing_abs clamp (force minimum $0.01 spacing)
    for (double surv : g_survives) {
        for (double sp : {0.10, 0.50, 1.00, 1.50}) {
            SweepTask t;
            t.survive_pct = surv;
            t.base_spacing = sp;
            t.lookback_hours = 4.0;
            t.typical_vol_pct = 0.55;
            t.min_spacing_mult = 0.5;
            t.max_spacing_mult = 3.0;
            t.min_spacing_abs = 0.01;  // Almost no floor
            t.max_spacing_abs = 2.0;   // Tight ceiling
            t.spacing_change_thresh = 0.01;
            t.mode = FillUpOscillation::ADAPTIVE_SPACING;
            t.group = "EXTREME";
            t.label = "s" + std::to_string((int)surv) + "_sp" +
                      std::to_string(sp).substr(0,4) + "_tightClamp";
            tasks.push_back(t);
        }
    }

    return tasks;
}

// ============================================================================
// Main
// ============================================================================
int main() {
    std::cout << "=====================================================" << std::endl;
    std::cout << "  XAUUSD Deep Parameter Sweep" << std::endl;
    std::cout << "  survive=[12,13]%, all other params varied widely" << std::endl;
    std::cout << "=====================================================" << std::endl << std::endl;

    // Load tick data
    std::string path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    LoadTickDataOnce(path);

    // Generate tasks
    auto tasks = generate_tasks();
    std::cout << "Total configurations: " << tasks.size() << std::endl;

    // Count by group
    int core = 0, mult = 0, baseline = 0, extreme = 0;
    for (const auto& t : tasks) {
        if (t.group == "CORE") core++;
        else if (t.group == "MULT") mult++;
        else if (t.group == "BASELINE") baseline++;
        else if (t.group == "EXTREME") extreme++;
    }
    std::cout << "  CORE: " << core << " | MULT: " << mult
              << " | BASELINE: " << baseline << " | EXTREME: " << extreme << std::endl;

    unsigned int num_threads = std::thread::hardware_concurrency();
    std::cout << "Threads: " << num_threads << std::endl;
    std::cout << "Estimated time: ~" << (tasks.size() / num_threads) << "s" << std::endl << std::endl;

    // Populate work queue
    WorkQueue queue;
    for (const auto& t : tasks) {
        queue.push(t);
    }
    queue.finish();

    // Launch workers
    auto sweep_start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    int total = (int)tasks.size();
    for (unsigned int i = 0; i < num_threads; i++) {
        threads.emplace_back(worker, std::ref(queue), total, std::cref(g_shared_ticks));
    }

    for (auto& t : threads) {
        t.join();
    }

    auto sweep_end = std::chrono::high_resolution_clock::now();
    auto sweep_duration = std::chrono::duration_cast<std::chrono::seconds>(sweep_end - sweep_start);
    std::cout << std::endl << std::endl;
    std::cout << "Sweep completed in " << sweep_duration.count() << "s ("
              << std::fixed << std::setprecision(2)
              << (double)sweep_duration.count() / tasks.size() << "s/config)" << std::endl << std::endl;

    // ========================================================================
    // Sort and display results
    // ========================================================================

    // Sort by return (descending)
    std::sort(g_results.begin(), g_results.end(),
        [](const SweepResult& a, const SweepResult& b) {
            return a.return_mult > b.return_mult;
        });

    // --- TOP 50 BY RETURN ---
    std::cout << "=====================================================" << std::endl;
    std::cout << "  TOP 50 BY RETURN (all groups)" << std::endl;
    std::cout << "=====================================================" << std::endl;
    std::cout << std::left << std::setw(50) << "Config"
              << std::right << std::setw(8) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(8) << "Sharpe"
              << std::setw(8) << "Trades"
              << std::setw(10) << "Swap"
              << std::setw(8) << "Group" << std::endl;
    std::cout << std::string(100, '-') << std::endl;

    int shown = 0;
    for (const auto& r : g_results) {
        if (shown >= 50) break;
        if (r.stopped_out) continue;
        std::cout << std::left << std::setw(50) << r.label
                  << std::right << std::fixed
                  << std::setw(8) << std::setprecision(2) << r.return_mult << "x"
                  << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(8) << std::setprecision(2) << r.sharpe_proxy
                  << std::setw(8) << r.total_trades
                  << std::setw(10) << std::setprecision(0) << r.total_swap
                  << std::setw(8) << r.group << std::endl;
        shown++;
    }

    // --- TOP 30 BY SHARPE (risk-adjusted) ---
    std::sort(g_results.begin(), g_results.end(),
        [](const SweepResult& a, const SweepResult& b) {
            return a.sharpe_proxy > b.sharpe_proxy;
        });

    std::cout << std::endl;
    std::cout << "=====================================================" << std::endl;
    std::cout << "  TOP 30 BY SHARPE PROXY (risk-adjusted return)" << std::endl;
    std::cout << "=====================================================" << std::endl;
    std::cout << std::left << std::setw(50) << "Config"
              << std::right << std::setw(8) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(8) << "Sharpe"
              << std::setw(8) << "Trades"
              << std::setw(10) << "Swap"
              << std::setw(8) << "Group" << std::endl;
    std::cout << std::string(100, '-') << std::endl;

    shown = 0;
    for (const auto& r : g_results) {
        if (shown >= 30) break;
        if (r.stopped_out || r.total_trades < 10) continue;
        std::cout << std::left << std::setw(50) << r.label
                  << std::right << std::fixed
                  << std::setw(8) << std::setprecision(2) << r.return_mult << "x"
                  << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(8) << std::setprecision(2) << r.sharpe_proxy
                  << std::setw(8) << r.total_trades
                  << std::setw(10) << std::setprecision(0) << r.total_swap
                  << std::setw(8) << r.group << std::endl;
        shown++;
    }

    // --- LOWEST DD (< 40% DD, return > 1.5x) ---
    std::sort(g_results.begin(), g_results.end(),
        [](const SweepResult& a, const SweepResult& b) {
            return a.max_dd_pct < b.max_dd_pct;
        });

    std::cout << std::endl;
    std::cout << "=====================================================" << std::endl;
    std::cout << "  LOWEST DRAWDOWN (DD<40%, Return>1.5x)" << std::endl;
    std::cout << "=====================================================" << std::endl;
    std::cout << std::left << std::setw(50) << "Config"
              << std::right << std::setw(8) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(8) << "Sharpe"
              << std::setw(8) << "Trades"
              << std::setw(10) << "Swap"
              << std::setw(8) << "Group" << std::endl;
    std::cout << std::string(100, '-') << std::endl;

    shown = 0;
    for (const auto& r : g_results) {
        if (shown >= 30) break;
        if (r.stopped_out || r.max_dd_pct >= 40.0 || r.return_mult < 1.5) continue;
        std::cout << std::left << std::setw(50) << r.label
                  << std::right << std::fixed
                  << std::setw(8) << std::setprecision(2) << r.return_mult << "x"
                  << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(8) << std::setprecision(2) << r.sharpe_proxy
                  << std::setw(8) << r.total_trades
                  << std::setw(10) << std::setprecision(0) << r.total_swap
                  << std::setw(8) << r.group << std::endl;
        shown++;
    }

    // --- BASELINE vs ADAPTIVE comparison ---
    std::cout << std::endl;
    std::cout << "=====================================================" << std::endl;
    std::cout << "  BASELINE (fixed) vs ADAPTIVE comparison" << std::endl;
    std::cout << "=====================================================" << std::endl;
    std::cout << std::left << std::setw(35) << "Config"
              << std::right << std::setw(8) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(8) << "Sharpe"
              << std::setw(8) << "Trades"
              << std::setw(8) << "Mode" << std::endl;
    std::cout << std::string(75, '-') << std::endl;

    // Collect baseline results
    std::vector<SweepResult> baselines;
    for (const auto& r : g_results) {
        if (r.group == "BASELINE" && !r.stopped_out) {
            baselines.push_back(r);
        }
    }
    std::sort(baselines.begin(), baselines.end(),
        [](const SweepResult& a, const SweepResult& b) {
            return a.return_mult > b.return_mult;
        });
    for (const auto& r : baselines) {
        std::cout << std::left << std::setw(35) << r.label
                  << std::right << std::fixed
                  << std::setw(8) << std::setprecision(2) << r.return_mult << "x"
                  << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(8) << std::setprecision(2) << r.sharpe_proxy
                  << std::setw(8) << r.total_trades
                  << std::setw(8) << "FIXED" << std::endl;
    }

    // --- MULTIPLIER comparison ---
    std::cout << std::endl;
    std::cout << "=====================================================" << std::endl;
    std::cout << "  MULTIPLIER RANGE comparison (survive=13, lb=4h, tv=0.55)" << std::endl;
    std::cout << "=====================================================" << std::endl;
    std::cout << std::left << std::setw(40) << "Config"
              << std::right << std::setw(8) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(8) << "Sharpe"
              << std::setw(8) << "Trades" << std::endl;
    std::cout << std::string(72, '-') << std::endl;

    std::vector<SweepResult> mults;
    for (const auto& r : g_results) {
        if (r.group == "MULT" && r.survive_pct == 13.0 && !r.stopped_out) {
            mults.push_back(r);
        }
    }
    std::sort(mults.begin(), mults.end(),
        [](const SweepResult& a, const SweepResult& b) {
            if (a.base_spacing != b.base_spacing) return a.base_spacing < b.base_spacing;
            return a.return_mult > b.return_mult;
        });
    for (const auto& r : mults) {
        std::cout << std::left << std::setw(40) << r.label
                  << std::right << std::fixed
                  << std::setw(8) << std::setprecision(2) << r.return_mult << "x"
                  << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(8) << std::setprecision(2) << r.sharpe_proxy
                  << std::setw(8) << r.total_trades << std::endl;
    }

    // --- EXTREME results ---
    std::cout << std::endl;
    std::cout << "=====================================================" << std::endl;
    std::cout << "  EXTREME/UNCONVENTIONAL results" << std::endl;
    std::cout << "=====================================================" << std::endl;
    std::cout << std::left << std::setw(50) << "Config"
              << std::right << std::setw(8) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(8) << "Sharpe"
              << std::setw(8) << "Trades"
              << std::setw(10) << "Swap" << std::endl;
    std::cout << std::string(92, '-') << std::endl;

    std::vector<SweepResult> extremes;
    for (const auto& r : g_results) {
        if (r.group == "EXTREME") {
            extremes.push_back(r);
        }
    }
    std::sort(extremes.begin(), extremes.end(),
        [](const SweepResult& a, const SweepResult& b) {
            return a.return_mult > b.return_mult;
        });
    for (const auto& r : extremes) {
        std::string status = r.stopped_out ? " [SO]" : "";
        std::cout << std::left << std::setw(50) << (r.label + status)
                  << std::right << std::fixed
                  << std::setw(8) << std::setprecision(2) << r.return_mult << "x"
                  << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(8) << std::setprecision(2) << r.sharpe_proxy
                  << std::setw(8) << r.total_trades
                  << std::setw(10) << std::setprecision(0) << r.total_swap << std::endl;
    }

    // --- STOP-OUT summary ---
    int total_stopped = 0;
    std::map<std::string, int> stopped_by_group;
    for (const auto& r : g_results) {
        if (r.stopped_out) {
            total_stopped++;
            stopped_by_group[r.group]++;
        }
    }

    std::cout << std::endl;
    std::cout << "=====================================================" << std::endl;
    std::cout << "  STOP-OUT SUMMARY" << std::endl;
    std::cout << "=====================================================" << std::endl;
    std::cout << "Total stopped out: " << total_stopped << "/" << g_results.size()
              << " (" << (total_stopped * 100 / (int)g_results.size()) << "%)" << std::endl;
    for (const auto& [group, count] : stopped_by_group) {
        std::cout << "  " << group << ": " << count << std::endl;
    }

    // --- Parameter influence analysis ---
    std::cout << std::endl;
    std::cout << "=====================================================" << std::endl;
    std::cout << "  PARAMETER INFLUENCE (CORE group, survive=13, not stopped out)" << std::endl;
    std::cout << "=====================================================" << std::endl;

    // Average return by spacing
    std::cout << std::endl << "By SPACING (avg return | avg DD | count):" << std::endl;
    for (double sp : g_spacings) {
        double sum_ret = 0, sum_dd = 0;
        int count = 0;
        for (const auto& r : g_results) {
            if (r.group == "CORE" && r.survive_pct == 13.0 &&
                std::abs(r.base_spacing - sp) < 0.001 && !r.stopped_out) {
                sum_ret += r.return_mult;
                sum_dd += r.max_dd_pct;
                count++;
            }
        }
        if (count > 0) {
            std::cout << "  sp=" << std::setw(6) << std::fixed << std::setprecision(2) << sp
                      << " → " << std::setprecision(2) << (sum_ret / count) << "x"
                      << " | DD=" << std::setprecision(1) << (sum_dd / count) << "%"
                      << " | n=" << count << std::endl;
        }
    }

    // Average return by lookback
    std::cout << std::endl << "By LOOKBACK (avg return | avg DD | count):" << std::endl;
    for (double lb : g_lookbacks) {
        double sum_ret = 0, sum_dd = 0;
        int count = 0;
        for (const auto& r : g_results) {
            if (r.group == "CORE" && r.survive_pct == 13.0 &&
                std::abs(r.lookback_hours - lb) < 0.001 && !r.stopped_out) {
                sum_ret += r.return_mult;
                sum_dd += r.max_dd_pct;
                count++;
            }
        }
        if (count > 0) {
            std::cout << "  lb=" << std::setw(7) << std::fixed << std::setprecision(2) << lb
                      << "h → " << std::setprecision(2) << (sum_ret / count) << "x"
                      << " | DD=" << std::setprecision(1) << (sum_dd / count) << "%"
                      << " | n=" << count << std::endl;
        }
    }

    // Average return by typvol
    std::cout << std::endl << "By TYPVOL (avg return | avg DD | count):" << std::endl;
    for (double tv : g_typvols) {
        double sum_ret = 0, sum_dd = 0;
        int count = 0;
        for (const auto& r : g_results) {
            if (r.group == "CORE" && r.survive_pct == 13.0 &&
                std::abs(r.typical_vol_pct - tv) < 0.001 && !r.stopped_out) {
                sum_ret += r.return_mult;
                sum_dd += r.max_dd_pct;
                count++;
            }
        }
        if (count > 0) {
            std::cout << "  tv=" << std::setw(6) << std::fixed << std::setprecision(2) << tv
                      << "% → " << std::setprecision(2) << (sum_ret / count) << "x"
                      << " | DD=" << std::setprecision(1) << (sum_dd / count) << "%"
                      << " | n=" << count << std::endl;
        }
    }

    // --- survive=12 vs survive=13 comparison ---
    std::cout << std::endl;
    std::cout << "=====================================================" << std::endl;
    std::cout << "  SURVIVE 12% vs 13% (CORE group, matched configs)" << std::endl;
    std::cout << "=====================================================" << std::endl;

    double sum_12 = 0, sum_13 = 0, sum_dd12 = 0, sum_dd13 = 0;
    int n12 = 0, n13 = 0, so12 = 0, so13 = 0;
    for (const auto& r : g_results) {
        if (r.group != "CORE") continue;
        if (r.survive_pct == 12.0) {
            if (r.stopped_out) so12++;
            else { sum_12 += r.return_mult; sum_dd12 += r.max_dd_pct; n12++; }
        } else if (r.survive_pct == 13.0) {
            if (r.stopped_out) so13++;
            else { sum_13 += r.return_mult; sum_dd13 += r.max_dd_pct; n13++; }
        }
    }
    std::cout << "survive=12%: avg=" << std::fixed << std::setprecision(2) << (n12>0?sum_12/n12:0)
              << "x | avgDD=" << std::setprecision(1) << (n12>0?sum_dd12/n12:0)
              << "% | survived=" << n12 << " | stopped=" << so12 << std::endl;
    std::cout << "survive=13%: avg=" << std::fixed << std::setprecision(2) << (n13>0?sum_13/n13:0)
              << "x | avgDD=" << std::setprecision(1) << (n13>0?sum_dd13/n13:0)
              << "% | survived=" << n13 << " | stopped=" << so13 << std::endl;

    std::cout << std::endl << "Done. Total configs: " << g_results.size() << std::endl;

    return 0;
}
