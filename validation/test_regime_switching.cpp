/**
 * Regime-Switching Strategy Parallel Parameter Sweep
 *
 * Tests Hidden Markov Model inspired regime detection:
 * - OSCILLATING: Low direction ratio -> normal trading
 * - TRENDING: High direction ratio -> widen spacing or pause
 * - HIGH_VOLATILITY: High vol ratio -> reduce position size
 *
 * Loads tick data ONCE into shared memory, then tests configurations
 * in parallel using std::thread + WorkQueue pattern.
 *
 * Tests on BOTH 2024 and 2025 data to verify regime independence.
 */

#include "../include/strategy_regime_switching.h"
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
// Shared tick data - loaded ONCE per year, used by ALL threads
// ============================================================================
std::vector<Tick> g_ticks_2025;
std::vector<Tick> g_ticks_2024;

void LoadTickData(const std::string& path, std::vector<Tick>& ticks, const std::string& label) {
    std::cout << "Loading " << label << " tick data..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open tick file: " + path);
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

    std::cout << "  Loaded " << ticks.size() << " ticks in " << duration.count() << "s" << std::endl;
}

// ============================================================================
// Task and Result structures
// ============================================================================
struct RegimeTask {
    int lookback_ticks;
    double oscillating_threshold;
    double trending_threshold;
    StrategyRegimeSwitching::TrendingAction trending_action;
    double highvol_threshold;
    StrategyRegimeSwitching::HighVolAction highvol_action;
    double survive_pct;
    std::string label;
    bool is_baseline;
};

struct RegimeResult {
    std::string label;
    bool is_baseline;

    // 2025 results
    double return_2025;
    double max_dd_2025;
    int trades_2025;
    double swap_2025;
    long osc_ticks_2025;
    long trend_ticks_2025;
    long highvol_ticks_2025;
    int regime_changes_2025;
    bool stopped_out_2025;

    // 2024 results
    double return_2024;
    double max_dd_2024;
    int trades_2024;
    double swap_2024;
    long osc_ticks_2024;
    long trend_ticks_2024;
    long highvol_ticks_2024;
    int regime_changes_2024;
    bool stopped_out_2024;

    // Derived metrics
    double avg_return;
    double ratio_2025_2024;
    double sharpe_2025;
    double sharpe_2024;
};

// ============================================================================
// Work Queue for thread pool
// ============================================================================
class WorkQueue {
    std::queue<RegimeTask> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool done_ = false;

public:
    void push(const RegimeTask& task) {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push(task);
        cv_.notify_one();
    }

    bool pop(RegimeTask& task) {
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
std::vector<RegimeResult> g_results;

// ============================================================================
// Run single test for one year
// ============================================================================
struct YearResult {
    double return_mult;
    double max_dd_pct;
    int total_trades;
    double total_swap;
    long osc_ticks;
    long trend_ticks;
    long highvol_ticks;
    int regime_changes;
    bool stopped_out;
};

YearResult run_single_year(const RegimeTask& task, const std::vector<Tick>& ticks,
                           const std::string& start_date, const std::string& end_date) {
    YearResult yr;
    yr.return_mult = 0;
    yr.max_dd_pct = 0;
    yr.total_trades = 0;
    yr.total_swap = 0;
    yr.osc_ticks = 0;
    yr.trend_ticks = 0;
    yr.highvol_ticks = 0;
    yr.regime_changes = 0;
    yr.stopped_out = false;

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
    cfg.start_date = start_date;
    cfg.end_date = end_date;
    cfg.verbose = false;

    TickDataConfig tc;
    tc.file_path = "";
    tc.load_all_into_memory = false;
    cfg.tick_data_config = tc;

    try {
        TickBasedEngine engine(cfg);

        if (task.is_baseline) {
            // Run baseline FillUpOscillation ADAPTIVE_SPACING
            FillUpOscillation baseline(task.survive_pct, 1.5, 0.01, 10.0, 100.0, 500.0,
                                       FillUpOscillation::ADAPTIVE_SPACING, 0.1, 30.0, 4.0);
            engine.RunWithTicks(ticks, [&baseline](const Tick& t, TickBasedEngine& e) {
                baseline.OnTick(t, e);
            });
            yr.osc_ticks = ticks.size();  // All ticks are "oscillating" for baseline
            yr.trend_ticks = 0;
            yr.highvol_ticks = 0;
            yr.regime_changes = 0;
        } else {
            // Run regime-switching strategy
            StrategyRegimeSwitching::Config scfg;
            scfg.lookback_ticks = task.lookback_ticks;
            scfg.oscillating_threshold = task.oscillating_threshold;
            scfg.trending_threshold = task.trending_threshold;
            scfg.trending_action = task.trending_action;
            scfg.highvol_threshold = task.highvol_threshold;
            scfg.highvol_action = task.highvol_action;
            scfg.survive_pct = task.survive_pct;
            scfg.base_spacing = 1.5;
            scfg.volatility_lookback_hours = 4.0;
            scfg.typical_vol_pct = 0.55;

            StrategyRegimeSwitching strategy(scfg);
            engine.RunWithTicks(ticks, [&strategy](const Tick& t, TickBasedEngine& e) {
                strategy.OnTick(t, e);
            });

            yr.osc_ticks = strategy.GetOscillatingTicks();
            yr.trend_ticks = strategy.GetTrendingTicks();
            yr.highvol_ticks = strategy.GetHighVolTicks();
            yr.regime_changes = strategy.GetRegimeChanges();
        }

        auto res = engine.GetResults();
        yr.return_mult = res.final_balance / 10000.0;
        yr.max_dd_pct = res.max_drawdown_pct;
        yr.total_trades = res.total_trades;
        yr.total_swap = res.total_swap_charged;
        yr.stopped_out = res.stop_out_occurred;

    } catch (const std::exception& e) {
        yr.stopped_out = true;
        yr.max_dd_pct = 100.0;
    }

    return yr;
}

RegimeResult run_test(const RegimeTask& task) {
    RegimeResult r;
    r.label = task.label;
    r.is_baseline = task.is_baseline;

    // Run 2025
    auto yr2025 = run_single_year(task, g_ticks_2025, "2025.01.01", "2025.12.30");
    r.return_2025 = yr2025.return_mult;
    r.max_dd_2025 = yr2025.max_dd_pct;
    r.trades_2025 = yr2025.total_trades;
    r.swap_2025 = yr2025.total_swap;
    r.osc_ticks_2025 = yr2025.osc_ticks;
    r.trend_ticks_2025 = yr2025.trend_ticks;
    r.highvol_ticks_2025 = yr2025.highvol_ticks;
    r.regime_changes_2025 = yr2025.regime_changes;
    r.stopped_out_2025 = yr2025.stopped_out;

    // Run 2024
    auto yr2024 = run_single_year(task, g_ticks_2024, "2024.01.01", "2024.12.30");
    r.return_2024 = yr2024.return_mult;
    r.max_dd_2024 = yr2024.max_dd_pct;
    r.trades_2024 = yr2024.total_trades;
    r.swap_2024 = yr2024.total_swap;
    r.osc_ticks_2024 = yr2024.osc_ticks;
    r.trend_ticks_2024 = yr2024.trend_ticks;
    r.highvol_ticks_2024 = yr2024.highvol_ticks;
    r.regime_changes_2024 = yr2024.regime_changes;
    r.stopped_out_2024 = yr2024.stopped_out;

    // Derived metrics
    r.avg_return = (r.return_2025 + r.return_2024) / 2.0;
    r.ratio_2025_2024 = (r.return_2024 > 0.1) ? r.return_2025 / r.return_2024 : 999.0;
    r.sharpe_2025 = (r.max_dd_2025 > 0) ? (r.return_2025 - 1.0) / (r.max_dd_2025 / 100.0) : 0;
    r.sharpe_2024 = (r.max_dd_2024 > 0) ? (r.return_2024 - 1.0) / (r.max_dd_2024 / 100.0) : 0;

    return r;
}

void worker(WorkQueue& queue, int total) {
    RegimeTask task;
    while (queue.pop(task)) {
        RegimeResult r = run_test(task);

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
std::vector<RegimeTask> GenerateTasks() {
    std::vector<RegimeTask> tasks;

    // Survive percentages to test
    std::vector<double> survive_vals = {12.0, 13.0};

    // Baseline configs (no regime detection - pure FillUpOscillation ADAPTIVE_SPACING)
    for (double surv : survive_vals) {
        RegimeTask t;
        t.lookback_ticks = 0;
        t.oscillating_threshold = 0;
        t.trending_threshold = 1.0;  // Never detect trending
        t.trending_action = StrategyRegimeSwitching::TREND_WIDEN_2X;
        t.highvol_threshold = 100.0;  // Never detect high vol
        t.highvol_action = StrategyRegimeSwitching::HIGHVOL_NORMAL;
        t.survive_pct = surv;
        t.label = "BASELINE_s" + std::to_string((int)surv);
        t.is_baseline = true;
        tasks.push_back(t);
    }

    // Lookback sweep
    std::vector<int> lookbacks = {500, 1000, 5000};

    // Oscillating threshold sweep
    std::vector<double> osc_thresholds = {0.2, 0.3, 0.4};

    // Trending threshold sweep
    std::vector<double> trend_thresholds = {0.6, 0.7, 0.8};

    // Trending actions
    std::vector<StrategyRegimeSwitching::TrendingAction> trend_actions = {
        StrategyRegimeSwitching::TREND_WIDEN_2X,
        StrategyRegimeSwitching::TREND_WIDEN_3X,
        StrategyRegimeSwitching::TREND_PAUSE
    };
    std::vector<std::string> trend_action_names = {"W2X", "W3X", "PAUSE"};

    // High vol threshold sweep
    std::vector<double> highvol_thresholds = {1.5, 2.0, 2.5};

    // High vol actions
    std::vector<StrategyRegimeSwitching::HighVolAction> highvol_actions = {
        StrategyRegimeSwitching::HIGHVOL_REDUCE_50,
        StrategyRegimeSwitching::HIGHVOL_REDUCE_75,
        StrategyRegimeSwitching::HIGHVOL_NORMAL
    };
    std::vector<std::string> highvol_action_names = {"R50", "R75", "NORM"};

    // Generate full sweep
    for (double surv : survive_vals) {
        for (int lb : lookbacks) {
            for (double osc : osc_thresholds) {
                for (double trnd : trend_thresholds) {
                    for (int ta = 0; ta < 3; ta++) {
                        for (double hv : highvol_thresholds) {
                            for (int ha = 0; ha < 3; ha++) {
                                RegimeTask t;
                                t.lookback_ticks = lb;
                                t.oscillating_threshold = osc;
                                t.trending_threshold = trnd;
                                t.trending_action = trend_actions[ta];
                                t.highvol_threshold = hv;
                                t.highvol_action = highvol_actions[ha];
                                t.survive_pct = surv;
                                t.is_baseline = false;

                                // Compact label
                                t.label = "s" + std::to_string((int)surv) +
                                         "_lb" + std::to_string(lb) +
                                         "_o" + std::to_string((int)(osc * 10)) +
                                         "_t" + std::to_string((int)(trnd * 10)) +
                                         "_" + trend_action_names[ta] +
                                         "_hv" + std::to_string((int)(hv * 10)) +
                                         "_" + highvol_action_names[ha];

                                tasks.push_back(t);
                            }
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
    std::cout << "  REGIME-SWITCHING STRATEGY PARALLEL SWEEP" << std::endl;
    std::cout << "  Testing HMM-inspired regime detection on XAUUSD" << std::endl;
    std::cout << "  Comparing 2024 vs 2025 for regime independence" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;

    // Step 1: Load tick data for both years
    try {
        LoadTickData("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv",
                     g_ticks_2025, "2025");
        LoadTickData("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAUUSD_TICKS_2024.csv",
                     g_ticks_2024, "2024");
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
    std::cout << "Testing " << total << " configurations (including "
              << 2 << " baselines)..." << std::endl;
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
              << std::fixed << std::setprecision(1) << (double)duration.count() / total
              << "s per config)" << std::endl;
    std::cout << std::endl;

    // ========================================================================
    // RESULTS ANALYSIS
    // ========================================================================

    // Find baselines
    RegimeResult baseline_s12, baseline_s13;
    for (const auto& r : g_results) {
        if (r.label == "BASELINE_s12") baseline_s12 = r;
        if (r.label == "BASELINE_s13") baseline_s13 = r;
    }

    // Print baseline results
    std::cout << "================================================================" << std::endl;
    std::cout << "  BASELINE RESULTS (FillUpOscillation ADAPTIVE_SPACING)" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::left << std::setw(16) << "Config"
              << std::right << std::setw(10) << "2025 Ret"
              << std::setw(10) << "2025 DD%"
              << std::setw(10) << "2024 Ret"
              << std::setw(10) << "2024 DD%"
              << std::setw(10) << "Ratio"
              << std::setw(10) << "Sharpe25"
              << std::endl;
    std::cout << std::string(76, '-') << std::endl;

    for (const auto& r : {baseline_s12, baseline_s13}) {
        std::cout << std::left << std::setw(16) << r.label
                  << std::right << std::fixed
                  << std::setw(8) << std::setprecision(2) << r.return_2025 << "x"
                  << std::setw(9) << std::setprecision(1) << r.max_dd_2025 << "%"
                  << std::setw(8) << std::setprecision(2) << r.return_2024 << "x"
                  << std::setw(9) << std::setprecision(1) << r.max_dd_2024 << "%"
                  << std::setw(10) << std::setprecision(2) << r.ratio_2025_2024
                  << std::setw(10) << std::setprecision(2) << r.sharpe_2025
                  << std::endl;
    }

    // Sort by average return (both years)
    std::vector<RegimeResult> sorted_avg = g_results;
    std::sort(sorted_avg.begin(), sorted_avg.end(), [](const RegimeResult& a, const RegimeResult& b) {
        return a.avg_return > b.avg_return;
    });

    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  TOP 20 BY AVERAGE RETURN (2024+2025)/2" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::left << std::setw(4) << "#"
              << std::setw(38) << "Config"
              << std::right << std::setw(8) << "2025"
              << std::setw(8) << "2024"
              << std::setw(8) << "Avg"
              << std::setw(8) << "Ratio"
              << std::setw(8) << "DD25%"
              << std::setw(8) << "DD24%"
              << std::endl;
    std::cout << std::string(90, '-') << std::endl;

    for (int i = 0; i < std::min(20, (int)sorted_avg.size()); i++) {
        const auto& r = sorted_avg[i];
        std::string status = "";
        if (r.stopped_out_2025 || r.stopped_out_2024) status = " [SO]";
        std::cout << std::left << std::setw(4) << (i + 1)
                  << std::setw(38) << (r.label + status)
                  << std::right << std::fixed
                  << std::setw(6) << std::setprecision(2) << r.return_2025 << "x"
                  << std::setw(6) << std::setprecision(2) << r.return_2024 << "x"
                  << std::setw(6) << std::setprecision(2) << r.avg_return << "x"
                  << std::setw(8) << std::setprecision(2) << r.ratio_2025_2024
                  << std::setw(7) << std::setprecision(1) << r.max_dd_2025 << "%"
                  << std::setw(7) << std::setprecision(1) << r.max_dd_2024 << "%"
                  << std::endl;
    }

    // Sort by regime independence (lowest ratio = most stable across years)
    std::vector<RegimeResult> sorted_stable = g_results;
    // Filter out stopped-out and baselines for this analysis
    sorted_stable.erase(std::remove_if(sorted_stable.begin(), sorted_stable.end(),
        [](const RegimeResult& r) {
            return r.stopped_out_2025 || r.stopped_out_2024 ||
                   r.return_2025 < 1.0 || r.return_2024 < 1.0 || r.is_baseline;
        }), sorted_stable.end());

    std::sort(sorted_stable.begin(), sorted_stable.end(), [](const RegimeResult& a, const RegimeResult& b) {
        return a.ratio_2025_2024 < b.ratio_2025_2024;
    });

    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  TOP 20 MOST REGIME-STABLE (lowest 2025/2024 ratio)" << std::endl;
    std::cout << "  (Excluding stopped-out and losing configs)" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::left << std::setw(4) << "#"
              << std::setw(38) << "Config"
              << std::right << std::setw(8) << "2025"
              << std::setw(8) << "2024"
              << std::setw(8) << "Ratio"
              << std::setw(8) << "DD25%"
              << std::setw(8) << "DD24%"
              << std::endl;
    std::cout << std::string(86, '-') << std::endl;

    for (int i = 0; i < std::min(20, (int)sorted_stable.size()); i++) {
        const auto& r = sorted_stable[i];
        std::cout << std::left << std::setw(4) << (i + 1)
                  << std::setw(38) << r.label
                  << std::right << std::fixed
                  << std::setw(6) << std::setprecision(2) << r.return_2025 << "x"
                  << std::setw(6) << std::setprecision(2) << r.return_2024 << "x"
                  << std::setw(8) << std::setprecision(2) << r.ratio_2025_2024
                  << std::setw(7) << std::setprecision(1) << r.max_dd_2025 << "%"
                  << std::setw(7) << std::setprecision(1) << r.max_dd_2024 << "%"
                  << std::endl;
    }

    // Sort by Sharpe 2025 (risk-adjusted)
    std::vector<RegimeResult> sorted_sharpe = g_results;
    std::sort(sorted_sharpe.begin(), sorted_sharpe.end(), [](const RegimeResult& a, const RegimeResult& b) {
        return a.sharpe_2025 > b.sharpe_2025;
    });

    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  TOP 20 BY SHARPE RATIO (2025)" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::left << std::setw(4) << "#"
              << std::setw(38) << "Config"
              << std::right << std::setw(8) << "2025"
              << std::setw(8) << "DD25%"
              << std::setw(10) << "Sharpe25"
              << std::setw(10) << "Sharpe24"
              << std::endl;
    std::cout << std::string(80, '-') << std::endl;

    for (int i = 0; i < std::min(20, (int)sorted_sharpe.size()); i++) {
        const auto& r = sorted_sharpe[i];
        std::string status = "";
        if (r.stopped_out_2025) status = " [SO]";
        std::cout << std::left << std::setw(4) << (i + 1)
                  << std::setw(38) << (r.label + status)
                  << std::right << std::fixed
                  << std::setw(6) << std::setprecision(2) << r.return_2025 << "x"
                  << std::setw(7) << std::setprecision(1) << r.max_dd_2025 << "%"
                  << std::setw(10) << std::setprecision(2) << r.sharpe_2025
                  << std::setw(10) << std::setprecision(2) << r.sharpe_2024
                  << std::endl;
    }

    // Configs that beat baseline
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  CONFIGS BEATING BASELINE (survive=13)" << std::endl;
    std::cout << "  Criteria: Higher return on BOTH years, no stop-out" << std::endl;
    std::cout << "================================================================" << std::endl;

    std::vector<RegimeResult> beats_baseline;
    for (const auto& r : g_results) {
        if (r.is_baseline) continue;
        if (r.stopped_out_2025 || r.stopped_out_2024) continue;
        if (r.return_2025 > baseline_s13.return_2025 && r.return_2024 > baseline_s13.return_2024) {
            beats_baseline.push_back(r);
        }
    }

    std::sort(beats_baseline.begin(), beats_baseline.end(), [](const RegimeResult& a, const RegimeResult& b) {
        return a.avg_return > b.avg_return;
    });

    if (beats_baseline.empty()) {
        std::cout << "  No configs beat baseline on both years." << std::endl;
    } else {
        std::cout << "Found " << beats_baseline.size() << " configs that beat baseline on both years:" << std::endl;
        std::cout << std::endl;
        std::cout << std::left << std::setw(4) << "#"
                  << std::setw(38) << "Config"
                  << std::right << std::setw(8) << "2025"
                  << std::setw(8) << "2024"
                  << std::setw(8) << "DD25%"
                  << std::setw(8) << "DD24%"
                  << std::endl;
        std::cout << std::string(74, '-') << std::endl;

        for (int i = 0; i < std::min(20, (int)beats_baseline.size()); i++) {
            const auto& r = beats_baseline[i];
            std::cout << std::left << std::setw(4) << (i + 1)
                      << std::setw(38) << r.label
                      << std::right << std::fixed
                      << std::setw(6) << std::setprecision(2) << r.return_2025 << "x"
                      << std::setw(6) << std::setprecision(2) << r.return_2024 << "x"
                      << std::setw(7) << std::setprecision(1) << r.max_dd_2025 << "%"
                      << std::setw(7) << std::setprecision(1) << r.max_dd_2024 << "%"
                      << std::endl;
        }
    }

    // Regime distribution analysis - pick a representative config
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  REGIME DISTRIBUTION ANALYSIS" << std::endl;
    std::cout << "================================================================" << std::endl;

    // Find a middle-ground config
    RegimeResult* sample = nullptr;
    for (auto& r : g_results) {
        if (r.label.find("s13_lb1000_o3_t7_W2X_hv20_R50") != std::string::npos) {
            sample = &r;
            break;
        }
    }

    if (sample) {
        std::cout << "Sample config: " << sample->label << std::endl;
        std::cout << std::endl;
        std::cout << "2025:" << std::endl;
        long total_2025 = sample->osc_ticks_2025 + sample->trend_ticks_2025 + sample->highvol_ticks_2025;
        if (total_2025 > 0) {
            std::cout << "  OSCILLATING:   " << std::setw(12) << sample->osc_ticks_2025
                      << " (" << std::fixed << std::setprecision(1)
                      << (100.0 * sample->osc_ticks_2025 / total_2025) << "%)" << std::endl;
            std::cout << "  TRENDING:      " << std::setw(12) << sample->trend_ticks_2025
                      << " (" << std::fixed << std::setprecision(1)
                      << (100.0 * sample->trend_ticks_2025 / total_2025) << "%)" << std::endl;
            std::cout << "  HIGH_VOLATILITY:" << std::setw(11) << sample->highvol_ticks_2025
                      << " (" << std::fixed << std::setprecision(1)
                      << (100.0 * sample->highvol_ticks_2025 / total_2025) << "%)" << std::endl;
            std::cout << "  Regime changes: " << sample->regime_changes_2025 << std::endl;
        }
        std::cout << std::endl;
        std::cout << "2024:" << std::endl;
        long total_2024 = sample->osc_ticks_2024 + sample->trend_ticks_2024 + sample->highvol_ticks_2024;
        if (total_2024 > 0) {
            std::cout << "  OSCILLATING:   " << std::setw(12) << sample->osc_ticks_2024
                      << " (" << std::fixed << std::setprecision(1)
                      << (100.0 * sample->osc_ticks_2024 / total_2024) << "%)" << std::endl;
            std::cout << "  TRENDING:      " << std::setw(12) << sample->trend_ticks_2024
                      << " (" << std::fixed << std::setprecision(1)
                      << (100.0 * sample->trend_ticks_2024 / total_2024) << "%)" << std::endl;
            std::cout << "  HIGH_VOLATILITY:" << std::setw(11) << sample->highvol_ticks_2024
                      << " (" << std::fixed << std::setprecision(1)
                      << (100.0 * sample->highvol_ticks_2024 / total_2024) << "%)" << std::endl;
            std::cout << "  Regime changes: " << sample->regime_changes_2024 << std::endl;
        }
    }

    // DD reduction analysis
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  DD REDUCTION ANALYSIS (vs baseline survive=13)" << std::endl;
    std::cout << "  Configs that reduce DD with <15% return loss" << std::endl;
    std::cout << "================================================================" << std::endl;

    struct DDImprovement {
        RegimeResult result;
        double dd_reduction_2025;
        double dd_reduction_2024;
        double return_loss_2025;
        double return_loss_2024;
    };
    std::vector<DDImprovement> dd_improved;

    for (const auto& r : g_results) {
        if (r.is_baseline) continue;
        if (r.stopped_out_2025 || r.stopped_out_2024) continue;

        double dd_red_2025 = baseline_s13.max_dd_2025 - r.max_dd_2025;
        double dd_red_2024 = baseline_s13.max_dd_2024 - r.max_dd_2024;
        double ret_loss_2025 = (baseline_s13.return_2025 - r.return_2025) / baseline_s13.return_2025 * 100.0;
        double ret_loss_2024 = (baseline_s13.return_2024 - r.return_2024) / baseline_s13.return_2024 * 100.0;

        // Must reduce DD in at least one year with <15% return loss in both
        if ((dd_red_2025 > 5 || dd_red_2024 > 5) &&
            ret_loss_2025 < 15 && ret_loss_2024 < 15) {
            dd_improved.push_back({r, dd_red_2025, dd_red_2024, ret_loss_2025, ret_loss_2024});
        }
    }

    std::sort(dd_improved.begin(), dd_improved.end(),
        [](const DDImprovement& a, const DDImprovement& b) {
            return (a.dd_reduction_2025 + a.dd_reduction_2024) >
                   (b.dd_reduction_2025 + b.dd_reduction_2024);
        });

    if (dd_improved.empty()) {
        std::cout << "  No configs found meeting DD reduction criteria." << std::endl;
    } else {
        std::cout << "Found " << dd_improved.size() << " configs with DD improvement:" << std::endl;
        std::cout << std::endl;
        std::cout << std::left << std::setw(38) << "Config"
                  << std::right << std::setw(10) << "DD Red25"
                  << std::setw(10) << "DD Red24"
                  << std::setw(10) << "Ret L25"
                  << std::setw(10) << "Ret L24"
                  << std::endl;
        std::cout << std::string(78, '-') << std::endl;

        for (int i = 0; i < std::min(15, (int)dd_improved.size()); i++) {
            const auto& d = dd_improved[i];
            std::cout << std::left << std::setw(38) << d.result.label
                      << std::right << std::fixed
                      << std::setw(8) << std::setprecision(1) << d.dd_reduction_2025 << "%"
                      << std::setw(8) << std::setprecision(1) << d.dd_reduction_2024 << "%"
                      << std::setw(8) << std::setprecision(1) << d.return_loss_2025 << "%"
                      << std::setw(8) << std::setprecision(1) << d.return_loss_2024 << "%"
                      << std::endl;
        }
    }

    // Summary statistics
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  SUMMARY" << std::endl;
    std::cout << "================================================================" << std::endl;

    int stopped_out_2025 = 0, stopped_out_2024 = 0;
    int profitable_both = 0;
    for (const auto& r : g_results) {
        if (r.stopped_out_2025) stopped_out_2025++;
        if (r.stopped_out_2024) stopped_out_2024++;
        if (r.return_2025 > 1.0 && r.return_2024 > 1.0 &&
            !r.stopped_out_2025 && !r.stopped_out_2024) profitable_both++;
    }

    std::cout << "Total configs tested: " << g_results.size() << std::endl;
    std::cout << "Stopped out in 2025:  " << stopped_out_2025 << std::endl;
    std::cout << "Stopped out in 2024:  " << stopped_out_2024 << std::endl;
    std::cout << "Profitable both years: " << profitable_both << std::endl;
    std::cout << "Configs beating baseline: " << beats_baseline.size() << std::endl;
    std::cout << "Configs with DD improvement: " << dd_improved.size() << std::endl;
    std::cout << std::endl;
    std::cout << "Baseline s13: 2025=" << std::fixed << std::setprecision(2) << baseline_s13.return_2025
              << "x, 2024=" << baseline_s13.return_2024 << "x, ratio=" << baseline_s13.ratio_2025_2024 << std::endl;
    std::cout << std::endl;
    std::cout << "Execution time: " << duration.count() << "s (" << num_threads << " threads)" << std::endl;

    return 0;
}
