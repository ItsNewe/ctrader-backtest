/**
 * OGY Control Strategy - Parallel Parameter Sweep
 *
 * Tests the hypothesis: Can tiny, well-timed position adjustments at "unstable
 * equilibria" (local min/max) outperform random timing?
 *
 * The OGY method from chaos theory suggests that chaotic systems can be controlled
 * with small perturbations at the right moment. This test applies that concept
 * to trading: tiny lot sizes (0.01-0.05), timed to local minima/maxima.
 *
 * KEY QUESTIONS:
 * 1. Does timing entries at "unstable equilibria" outperform random timing?
 * 2. What velocity window scale works best for detection?
 * 3. Can tiny well-timed positions match larger untimed positions?
 *
 * Uses parallel sweep pattern: load ticks once, test many configs simultaneously.
 */

#include "../include/strategy_ogy_control.h"
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
struct OGYTask {
    int velocity_window;
    double lot_size;
    double tp_distance;
    StrategyOGYControl::EquilibriumType equilibrium_type;
    int cooldown_ticks;
    bool random_mode;
    double random_entry_prob;
    std::string label;
};

struct OGYResult {
    std::string label;
    int velocity_window;
    double lot_size;
    double tp_distance;
    std::string equilibrium_type_str;
    bool random_mode;
    double return_mult;
    double max_dd_pct;
    int total_trades;
    int equilibria_detected;
    int entries;
    double total_swap;
    double profit_per_trade;
    double sharpe_proxy;    // (return - 1) / (DD/100)
    bool stop_out;
};

// ============================================================================
// Work Queue for thread pool
// ============================================================================
class WorkQueue {
    std::queue<OGYTask> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool done_ = false;

public:
    void push(const OGYTask& task) {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push(task);
        cv_.notify_one();
    }

    bool pop(OGYTask& task) {
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
std::vector<OGYResult> g_results;

// ============================================================================
// Run single test with shared tick data
// ============================================================================
OGYResult run_test(const OGYTask& task, const std::vector<Tick>& ticks) {
    OGYResult r;
    r.label = task.label;
    r.velocity_window = task.velocity_window;
    r.lot_size = task.lot_size;
    r.tp_distance = task.tp_distance;
    r.random_mode = task.random_mode;
    r.return_mult = 0;
    r.max_dd_pct = 0;
    r.total_trades = 0;
    r.equilibria_detected = 0;
    r.entries = 0;
    r.total_swap = 0;
    r.profit_per_trade = 0;
    r.sharpe_proxy = 0;
    r.stop_out = false;

    switch (task.equilibrium_type) {
        case StrategyOGYControl::VELOCITY_ZERO: r.equilibrium_type_str = "VEL_ZERO"; break;
        case StrategyOGYControl::LOCAL_MINMAX: r.equilibrium_type_str = "LOCAL_MM"; break;
        case StrategyOGYControl::BOTH: r.equilibrium_type_str = "BOTH"; break;
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
    cfg.start_date = "2025.01.01";
    cfg.end_date = "2025.12.30";
    cfg.verbose = false;

    TickDataConfig tc;
    tc.file_path = "";
    tc.load_all_into_memory = false;
    cfg.tick_data_config = tc;

    try {
        TickBasedEngine engine(cfg);

        StrategyOGYControl::Config strat_cfg;
        strat_cfg.velocity_window = task.velocity_window;
        strat_cfg.lot_size = task.lot_size;
        strat_cfg.tp_distance = task.tp_distance;
        strat_cfg.equilibrium_type = task.equilibrium_type;
        strat_cfg.local_minmax_window = task.velocity_window;  // Use same window
        strat_cfg.cooldown_ticks = task.cooldown_ticks;
        strat_cfg.max_positions = 50;
        // contract_size/leverage now read from engine.GetConfig()
        strat_cfg.warmup_ticks = 500;
        strat_cfg.min_velocity_threshold = 0.001;
        strat_cfg.random_mode = task.random_mode;
        strat_cfg.random_entry_prob = task.random_entry_prob;
        strat_cfg.random_seed = 42;

        StrategyOGYControl strategy(strat_cfg);

        engine.RunWithTicks(ticks, [&strategy](const Tick& t, TickBasedEngine& e) {
            strategy.OnTick(t, e);
        });

        auto res = engine.GetResults();
        r.return_mult = res.final_balance / 10000.0;
        r.max_dd_pct = strategy.GetMaxDDPct();
        r.total_trades = res.total_trades;
        r.equilibria_detected = strategy.GetEquilibriaDetected();
        r.entries = strategy.GetEntries();
        r.total_swap = res.total_swap_charged;
        r.stop_out = res.stop_out_occurred;

        if (r.total_trades > 0) {
            r.profit_per_trade = (res.final_balance - 10000.0 - r.total_swap) / r.total_trades;
        }
        r.sharpe_proxy = (r.max_dd_pct > 0) ? (r.return_mult - 1.0) / (r.max_dd_pct / 100.0) : 0;

    } catch (const std::exception& e) {
        r.stop_out = true;
        r.max_dd_pct = 100.0;
    }

    return r;
}

void worker(WorkQueue& queue, int total, const std::vector<Tick>& ticks) {
    OGYTask task;
    while (queue.pop(task)) {
        OGYResult r = run_test(task, ticks);

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
// Generate all OGY configurations to test
// ============================================================================
std::vector<OGYTask> GenerateTasks() {
    std::vector<OGYTask> tasks;

    // Velocity windows to test
    std::vector<int> velocity_windows = {10, 50, 100, 500, 1000};

    // Lot sizes to test (tiny = OGY principle)
    std::vector<double> lot_sizes = {0.01, 0.02, 0.05};

    // TP distances to test
    std::vector<double> tp_distances = {0.5, 1.0, 2.0, 5.0};

    // Equilibrium detection types
    std::vector<StrategyOGYControl::EquilibriumType> eq_types = {
        StrategyOGYControl::VELOCITY_ZERO,
        StrategyOGYControl::LOCAL_MINMAX,
        StrategyOGYControl::BOTH
    };

    // Cooldown values
    std::vector<int> cooldowns = {50, 100};

    int task_id = 0;

    // 1. OGY Timed entries - sweep all parameters
    for (int vw : velocity_windows) {
        for (double lot : lot_sizes) {
            for (double tp : tp_distances) {
                for (auto eq : eq_types) {
                    for (int cd : cooldowns) {
                        std::string eq_str;
                        switch (eq) {
                            case StrategyOGYControl::VELOCITY_ZERO: eq_str = "VZ"; break;
                            case StrategyOGYControl::LOCAL_MINMAX: eq_str = "MM"; break;
                            case StrategyOGYControl::BOTH: eq_str = "BOTH"; break;
                        }

                        OGYTask t;
                        t.velocity_window = vw;
                        t.lot_size = lot;
                        t.tp_distance = tp;
                        t.equilibrium_type = eq;
                        t.cooldown_ticks = cd;
                        t.random_mode = false;
                        t.random_entry_prob = 0.0;
                        t.label = "OGY_vw" + std::to_string(vw) + "_" + eq_str +
                                  "_lot" + std::to_string((int)(lot*100)) +
                                  "_tp" + std::to_string((int)(tp*10)) +
                                  "_cd" + std::to_string(cd);
                        tasks.push_back(t);
                        task_id++;
                    }
                }
            }
        }
    }

    // 2. Random baseline - same lot sizes and TPs but random timing
    // Match entry count approximately to OGY by tuning random_entry_prob
    for (double lot : lot_sizes) {
        for (double tp : tp_distances) {
            // Different random probabilities to get various trade counts
            for (double prob : {0.0001, 0.0005, 0.001, 0.002}) {
                OGYTask t;
                t.velocity_window = 100;  // Doesn't matter for random
                t.lot_size = lot;
                t.tp_distance = tp;
                t.equilibrium_type = StrategyOGYControl::VELOCITY_ZERO;
                t.cooldown_ticks = 50;
                t.random_mode = true;
                t.random_entry_prob = prob;
                t.label = "RANDOM_lot" + std::to_string((int)(lot*100)) +
                          "_tp" + std::to_string((int)(tp*10)) +
                          "_prob" + std::to_string((int)(prob*10000));
                tasks.push_back(t);
                task_id++;
            }
        }
    }

    return tasks;
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "  OGY CONTROL STRATEGY - PARALLEL PARAMETER SWEEP" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;
    std::cout << "Testing: Can tiny well-timed entries beat random timing?" << std::endl;
    std::cout << std::endl;
    std::cout << "Parameters:" << std::endl;
    std::cout << "  - velocity_window: [10, 50, 100, 500, 1000] ticks" << std::endl;
    std::cout << "  - lot_size: [0.01, 0.02, 0.05]" << std::endl;
    std::cout << "  - tp_distance: [$0.5, $1.0, $2.0, $5.0]" << std::endl;
    std::cout << "  - equilibrium_type: [VELOCITY_ZERO, LOCAL_MINMAX, BOTH]" << std::endl;
    std::cout << "  - cooldown: [50, 100] ticks" << std::endl;
    std::cout << "  + Random baseline with matching parameters" << std::endl;
    std::cout << std::endl;

    // Step 1: Load tick data ONCE
    std::string tick_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    LoadTickDataOnce(tick_path);

    // Step 2: Generate tasks
    auto tasks = GenerateTasks();
    int total_tasks = tasks.size();
    std::cout << "Generated " << total_tasks << " test configurations" << std::endl;
    std::cout << std::endl;

    // Step 3: Create work queue and start workers
    WorkQueue queue;
    for (const auto& task : tasks) {
        queue.push(task);
    }
    queue.finish();

    auto start_time = std::chrono::high_resolution_clock::now();

    unsigned int num_threads = std::thread::hardware_concurrency();
    std::cout << "Starting " << num_threads << " worker threads..." << std::endl;

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < num_threads; i++) {
        threads.emplace_back(worker, std::ref(queue), total_tasks, std::cref(g_shared_ticks));
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);

    std::cout << "\n\nCompleted in " << duration.count() << " seconds" << std::endl;
    std::cout << "Average: " << std::fixed << std::setprecision(2)
              << (double)duration.count() / total_tasks << " sec/config" << std::endl;
    std::cout << std::endl;

    // Step 4: Sort and display results
    // Sort by return (descending)
    std::sort(g_results.begin(), g_results.end(),
              [](const OGYResult& a, const OGYResult& b) {
                  return a.return_mult > b.return_mult;
              });

    // Separate OGY and Random results
    std::vector<OGYResult> ogy_results;
    std::vector<OGYResult> random_results;
    for (const auto& r : g_results) {
        if (r.random_mode) {
            random_results.push_back(r);
        } else {
            ogy_results.push_back(r);
        }
    }

    // Sort each by return
    std::sort(ogy_results.begin(), ogy_results.end(),
              [](const OGYResult& a, const OGYResult& b) { return a.return_mult > b.return_mult; });
    std::sort(random_results.begin(), random_results.end(),
              [](const OGYResult& a, const OGYResult& b) { return a.return_mult > b.return_mult; });

    std::cout << "================================================================" << std::endl;
    std::cout << "  TOP 20 OGY (TIMED) RESULTS" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::left << std::setw(45) << "Label"
              << std::right << std::setw(8) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(10) << "Trades"
              << std::setw(10) << "Equilib"
              << std::setw(10) << "$/Trade"
              << std::setw(10) << "Sharpe"
              << std::endl;
    std::cout << std::string(101, '-') << std::endl;

    int count = 0;
    for (const auto& r : ogy_results) {
        if (count++ >= 20) break;
        std::cout << std::left << std::setw(45) << r.label
                  << std::right << std::fixed << std::setprecision(2)
                  << std::setw(8) << r.return_mult << "x"
                  << std::setw(7) << r.max_dd_pct << "%"
                  << std::setw(10) << r.total_trades
                  << std::setw(10) << r.equilibria_detected
                  << std::setw(10) << r.profit_per_trade
                  << std::setw(10) << r.sharpe_proxy
                  << (r.stop_out ? " SO" : "")
                  << std::endl;
    }

    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  TOP 10 RANDOM (BASELINE) RESULTS" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::left << std::setw(35) << "Label"
              << std::right << std::setw(8) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(10) << "Trades"
              << std::setw(10) << "$/Trade"
              << std::setw(10) << "Sharpe"
              << std::endl;
    std::cout << std::string(81, '-') << std::endl;

    count = 0;
    for (const auto& r : random_results) {
        if (count++ >= 10) break;
        std::cout << std::left << std::setw(35) << r.label
                  << std::right << std::fixed << std::setprecision(2)
                  << std::setw(8) << r.return_mult << "x"
                  << std::setw(7) << r.max_dd_pct << "%"
                  << std::setw(10) << r.total_trades
                  << std::setw(10) << r.profit_per_trade
                  << std::setw(10) << r.sharpe_proxy
                  << (r.stop_out ? " SO" : "")
                  << std::endl;
    }

    // Analysis: Compare OGY vs Random at similar trade counts
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  KEY COMPARISON: OGY TIMED vs RANDOM (similar trade counts)" << std::endl;
    std::cout << "================================================================" << std::endl;

    // Group by lot size and TP, compare best OGY vs best Random
    std::map<std::string, std::pair<OGYResult*, OGYResult*>> comparisons;

    for (auto& r : ogy_results) {
        std::string key = "lot" + std::to_string((int)(r.lot_size*100)) +
                          "_tp" + std::to_string((int)(r.tp_distance*10));
        if (comparisons.find(key) == comparisons.end()) {
            comparisons[key] = {nullptr, nullptr};
        }
        if (comparisons[key].first == nullptr ||
            r.return_mult > comparisons[key].first->return_mult) {
            comparisons[key].first = const_cast<OGYResult*>(&r);
        }
    }

    for (auto& r : random_results) {
        std::string key = "lot" + std::to_string((int)(r.lot_size*100)) +
                          "_tp" + std::to_string((int)(r.tp_distance*10));
        if (comparisons.find(key) != comparisons.end()) {
            if (comparisons[key].second == nullptr ||
                r.return_mult > comparisons[key].second->return_mult) {
                comparisons[key].second = const_cast<OGYResult*>(&r);
            }
        }
    }

    std::cout << std::left << std::setw(15) << "Config"
              << std::setw(12) << "OGY Return"
              << std::setw(10) << "OGY DD%"
              << std::setw(10) << "OGY Trd"
              << std::setw(12) << "Rnd Return"
              << std::setw(10) << "Rnd DD%"
              << std::setw(10) << "Rnd Trd"
              << std::setw(12) << "OGY Beats?"
              << std::endl;
    std::cout << std::string(101, '-') << std::endl;

    int ogy_wins = 0, random_wins = 0;
    for (const auto& kv : comparisons) {
        if (kv.second.first && kv.second.second) {
            bool ogy_better = kv.second.first->return_mult > kv.second.second->return_mult;
            if (ogy_better) ogy_wins++; else random_wins++;

            std::cout << std::left << std::setw(15) << kv.first
                      << std::right << std::fixed << std::setprecision(2)
                      << std::setw(10) << kv.second.first->return_mult << "x"
                      << std::setw(9) << kv.second.first->max_dd_pct << "%"
                      << std::setw(10) << kv.second.first->total_trades
                      << std::setw(10) << kv.second.second->return_mult << "x"
                      << std::setw(9) << kv.second.second->max_dd_pct << "%"
                      << std::setw(10) << kv.second.second->total_trades
                      << std::setw(12) << (ogy_better ? "YES" : "NO")
                      << std::endl;
        }
    }

    std::cout << std::endl;
    std::cout << "OGY Timed beats Random: " << ogy_wins << " / " << (ogy_wins + random_wins) << std::endl;

    // Summary statistics
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  ANALYSIS BY VELOCITY WINDOW (best per window)" << std::endl;
    std::cout << "================================================================" << std::endl;

    std::map<int, OGYResult*> best_by_window;
    for (auto& r : ogy_results) {
        if (best_by_window.find(r.velocity_window) == best_by_window.end() ||
            r.return_mult > best_by_window[r.velocity_window]->return_mult) {
            best_by_window[r.velocity_window] = const_cast<OGYResult*>(&r);
        }
    }

    std::cout << std::left << std::setw(10) << "Window"
              << std::setw(8) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(10) << "Trades"
              << std::setw(10) << "Equilib"
              << std::setw(35) << "Best Config"
              << std::endl;
    std::cout << std::string(81, '-') << std::endl;

    for (const auto& kv : best_by_window) {
        std::cout << std::left << std::setw(10) << kv.first
                  << std::right << std::fixed << std::setprecision(2)
                  << std::setw(6) << kv.second->return_mult << "x"
                  << std::setw(7) << kv.second->max_dd_pct << "%"
                  << std::setw(10) << kv.second->total_trades
                  << std::setw(10) << kv.second->equilibria_detected
                  << std::left << std::setw(35) << kv.second->label
                  << std::endl;
    }

    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  ANALYSIS BY EQUILIBRIUM TYPE" << std::endl;
    std::cout << "================================================================" << std::endl;

    std::map<std::string, std::pair<double, double>> by_eq_type;  // sum return, count
    for (const auto& r : ogy_results) {
        if (by_eq_type.find(r.equilibrium_type_str) == by_eq_type.end()) {
            by_eq_type[r.equilibrium_type_str] = {0.0, 0.0};
        }
        by_eq_type[r.equilibrium_type_str].first += r.return_mult;
        by_eq_type[r.equilibrium_type_str].second += 1.0;
    }

    std::cout << std::left << std::setw(15) << "Eq Type"
              << std::setw(15) << "Avg Return"
              << std::setw(10) << "Count"
              << std::endl;
    std::cout << std::string(40, '-') << std::endl;

    for (const auto& kv : by_eq_type) {
        double avg = kv.second.first / kv.second.second;
        std::cout << std::left << std::setw(15) << kv.first
                  << std::fixed << std::setprecision(3)
                  << std::setw(13) << avg << "x"
                  << std::setw(10) << (int)kv.second.second
                  << std::endl;
    }

    // Final summary
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  CONCLUSIONS" << std::endl;
    std::cout << "================================================================" << std::endl;

    // Find overall best OGY and best Random
    OGYResult* best_ogy = &ogy_results[0];
    OGYResult* best_random = random_results.empty() ? nullptr : &random_results[0];

    std::cout << std::endl;
    std::cout << "Best OGY (timed) result:" << std::endl;
    std::cout << "  Config: " << best_ogy->label << std::endl;
    std::cout << "  Return: " << std::fixed << std::setprecision(2) << best_ogy->return_mult << "x" << std::endl;
    std::cout << "  Max DD: " << best_ogy->max_dd_pct << "%" << std::endl;
    std::cout << "  Trades: " << best_ogy->total_trades << std::endl;
    std::cout << "  Equilibria detected: " << best_ogy->equilibria_detected << std::endl;

    if (best_random) {
        std::cout << std::endl;
        std::cout << "Best Random (baseline) result:" << std::endl;
        std::cout << "  Config: " << best_random->label << std::endl;
        std::cout << "  Return: " << std::fixed << std::setprecision(2) << best_random->return_mult << "x" << std::endl;
        std::cout << "  Max DD: " << best_random->max_dd_pct << "%" << std::endl;
        std::cout << "  Trades: " << best_random->total_trades << std::endl;
    }

    std::cout << std::endl;
    std::cout << "KEY QUESTIONS ANSWERED:" << std::endl;
    std::cout << "1. Does timing at equilibria outperform random? " << std::endl;
    std::cout << "   -> OGY wins " << ogy_wins << "/" << (ogy_wins + random_wins)
              << " comparisons (" << std::fixed << std::setprecision(1)
              << (100.0 * ogy_wins / (ogy_wins + random_wins)) << "%)" << std::endl;

    // Find best velocity window
    int best_window = 0;
    double best_window_return = 0;
    for (const auto& kv : best_by_window) {
        if (kv.second->return_mult > best_window_return) {
            best_window = kv.first;
            best_window_return = kv.second->return_mult;
        }
    }
    std::cout << "2. Best velocity window scale: " << best_window << " ticks" << std::endl;

    std::cout << "3. Can tiny positions work? Max return with 0.01 lots: ";
    double max_01 = 0;
    for (const auto& r : ogy_results) {
        if (r.lot_size == 0.01 && r.return_mult > max_01) {
            max_01 = r.return_mult;
        }
    }
    std::cout << std::fixed << std::setprecision(2) << max_01 << "x" << std::endl;

    return 0;
}
