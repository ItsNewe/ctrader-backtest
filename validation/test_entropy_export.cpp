/**
 * Entropy Export Strategy - Parallel Parameter Sweep
 *
 * CHAOS CONCEPT #6: DISSIPATIVE STRUCTURES (ENTROPY EXPORT)
 *
 * Hypothesis: By deliberately taking small losses on a subset of positions
 * ("entropy export"), the overall system might maintain better order (lower DD)
 * while still capturing profits on the main grid ("core" positions).
 *
 * Tests:
 *   - Baseline: Standard FillUpOscillation (100% core, no entropy)
 *   - Core/Entropy ratios: 70/30, 50/50, 30/70
 *   - Entropy SL multipliers: 0.3x, 0.5x, 0.7x, 1.0x of spacing
 *   - With/without reduced entropy position size
 *
 * Uses PARALLEL pattern: Load tick data ONCE, test all configs in parallel.
 */

#include "../include/strategy_entropy_export.h"
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
struct EntropyTask {
    double core_ratio;          // 1.0 = baseline (all core), 0.7 = 70% core 30% entropy
    double entropy_sl_mult;     // 0.5 = SL at 50% of spacing
    bool entropy_smaller_size;  // Use half size for entropy positions
    std::string label;
    bool is_baseline;           // True for pure FillUpOscillation baseline
};

struct EntropyResult {
    std::string label;
    double core_ratio;
    double entropy_sl_mult;
    bool entropy_smaller_size;
    bool is_baseline;

    // Results
    double return_mult;
    double max_dd_pct;
    int total_trades;
    double total_swap;
    double sharpe_proxy;

    // Entropy-specific stats
    int core_trades_opened;
    int core_trades_tp;
    int entropy_trades_opened;
    int entropy_trades_tp;
    int entropy_trades_sl;
    double entropy_losses;

    bool margin_stopped;
};

// ============================================================================
// Work Queue for thread pool
// ============================================================================
class WorkQueue {
    std::queue<EntropyTask> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool done_ = false;

public:
    void push(const EntropyTask& task) {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push(task);
        cv_.notify_one();
    }

    bool pop(EntropyTask& task) {
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
std::vector<EntropyResult> g_results;

// ============================================================================
// Run single test with shared tick data
// ============================================================================
EntropyResult run_test(const EntropyTask& task, const std::vector<Tick>& ticks) {
    EntropyResult r;
    r.label = task.label;
    r.core_ratio = task.core_ratio;
    r.entropy_sl_mult = task.entropy_sl_mult;
    r.entropy_smaller_size = task.entropy_smaller_size;
    r.is_baseline = task.is_baseline;
    r.return_mult = 0;
    r.max_dd_pct = 0;
    r.total_trades = 0;
    r.total_swap = 0;
    r.sharpe_proxy = 0;
    r.core_trades_opened = 0;
    r.core_trades_tp = 0;
    r.entropy_trades_opened = 0;
    r.entropy_trades_tp = 0;
    r.entropy_trades_sl = 0;
    r.entropy_losses = 0;
    r.margin_stopped = false;

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
            // Run standard FillUpOscillation
            FillUpOscillation strategy(13.0, 1.5, 0.01, 10.0, 100.0, 500.0,
                                       FillUpOscillation::ADAPTIVE_SPACING, 0.1, 30.0, 4.0);

            double peak_equity = 0;
            double max_dd = 0;

            engine.RunWithTicks(ticks, [&strategy, &peak_equity, &max_dd](const Tick& t, TickBasedEngine& e) {
                strategy.OnTick(t, e);
                double eq = e.GetEquity();
                if (eq > peak_equity) peak_equity = eq;
                double dd = (peak_equity > 0) ? (peak_equity - eq) / peak_equity * 100.0 : 0;
                if (dd > max_dd) max_dd = dd;
            });

            auto res = engine.GetResults();
            r.return_mult = res.final_balance / 10000.0;
            r.max_dd_pct = max_dd;
            r.total_trades = res.total_trades;
            r.total_swap = res.total_swap_charged;
            r.sharpe_proxy = (r.max_dd_pct > 0) ? (r.return_mult - 1.0) / (r.max_dd_pct / 100.0) : 0;

            // Baseline has all core trades
            r.core_trades_opened = res.total_trades;
            r.core_trades_tp = res.total_trades;

        } else {
            // Run EntropyExport strategy
            StrategyEntropyExport::Config scfg;
            scfg.survive_pct = 13.0;
            scfg.base_spacing = 1.5;
            scfg.min_volume = 0.01;
            scfg.max_volume = 10.0;
            scfg.contract_size = 100.0;
            scfg.leverage = 500.0;
            scfg.core_ratio = task.core_ratio;
            scfg.entropy_sl_mult = task.entropy_sl_mult;
            scfg.entropy_smaller_size = task.entropy_smaller_size;
            scfg.volatility_lookback_hours = 4.0;
            scfg.typical_vol_pct = 0.5;

            StrategyEntropyExport strategy(scfg);

            double peak_equity = 0;
            double max_dd = 0;

            engine.RunWithTicks(ticks, [&strategy, &peak_equity, &max_dd](const Tick& t, TickBasedEngine& e) {
                strategy.OnTick(t, e);
                double eq = e.GetEquity();
                if (eq > peak_equity) peak_equity = eq;
                double dd = (peak_equity > 0) ? (peak_equity - eq) / peak_equity * 100.0 : 0;
                if (dd > max_dd) max_dd = dd;
            });

            auto res = engine.GetResults();
            auto stats = strategy.GetStats();

            r.return_mult = res.final_balance / 10000.0;
            r.max_dd_pct = max_dd;
            r.total_trades = res.total_trades;
            r.total_swap = res.total_swap_charged;
            r.sharpe_proxy = (r.max_dd_pct > 0) ? (r.return_mult - 1.0) / (r.max_dd_pct / 100.0) : 0;

            r.core_trades_opened = stats.core_trades_opened;
            r.core_trades_tp = stats.core_trades_closed_tp;
            r.entropy_trades_opened = stats.entropy_trades_opened;
            r.entropy_trades_tp = stats.entropy_trades_closed_tp;
            r.entropy_trades_sl = stats.entropy_trades_closed_sl;
            r.entropy_losses = stats.total_entropy_losses;
        }

        r.margin_stopped = (r.return_mult < 0.05);  // Effectively stopped out

    } catch (const std::exception& e) {
        r.margin_stopped = true;
        r.max_dd_pct = 100.0;
    }

    return r;
}

void worker(WorkQueue& queue, int total, const std::vector<Tick>& ticks) {
    EntropyTask task;
    while (queue.pop(task)) {
        EntropyResult r = run_test(task, ticks);

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
// Generate all configurations to test
// ============================================================================
std::vector<EntropyTask> GenerateTasks() {
    std::vector<EntropyTask> tasks;

    // 1. BASELINE (standard FillUpOscillation, no entropy)
    tasks.push_back({1.0, 0.0, false, "BASELINE", true});

    // 2. Core ratio sweep with standard SL mult
    std::vector<double> core_ratios = {0.9, 0.8, 0.7, 0.6, 0.5, 0.4, 0.3, 0.2};
    std::vector<double> sl_mults = {0.3, 0.5, 0.7, 1.0, 1.5, 2.0};

    // 3. Full grid search: core_ratio x sl_mult
    for (double cr : core_ratios) {
        for (double sl : sl_mults) {
            std::string label = "CR" + std::to_string((int)(cr * 100))
                              + "_SL" + std::to_string((int)(sl * 100));
            tasks.push_back({cr, sl, false, label, false});
        }
    }

    // 4. Same grid but with smaller entropy position sizes
    for (double cr : core_ratios) {
        for (double sl : sl_mults) {
            std::string label = "CR" + std::to_string((int)(cr * 100))
                              + "_SL" + std::to_string((int)(sl * 100)) + "_SMALL";
            tasks.push_back({cr, sl, true, label, false});
        }
    }

    // 5. Extreme tests: very low core ratio
    tasks.push_back({0.1, 0.5, false, "CR10_SL50", false});
    tasks.push_back({0.1, 0.5, true, "CR10_SL50_SMALL", false});

    // 6. 100% entropy (all positions have SL)
    tasks.push_back({0.0, 0.3, false, "ALL_ENTROPY_SL30", false});
    tasks.push_back({0.0, 0.5, false, "ALL_ENTROPY_SL50", false});
    tasks.push_back({0.0, 0.7, false, "ALL_ENTROPY_SL70", false});
    tasks.push_back({0.0, 1.0, false, "ALL_ENTROPY_SL100", false});

    return tasks;
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "  ENTROPY EXPORT STRATEGY - PARALLEL SWEEP" << std::endl;
    std::cout << "  Chaos Concept #6: Dissipative Structures" << std::endl;
    std::cout << "  Testing deliberate loss-taking to reduce overall DD" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;
    std::cout << "HYPOTHESIS: By taking small, frequent losses on 'entropy' positions," << std::endl;
    std::cout << "the strategy might maintain better order (lower DD) while still" << std::endl;
    std::cout << "capturing profits on 'core' positions." << std::endl;
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

    // Step 6: Sort by Sharpe proxy
    std::sort(g_results.begin(), g_results.end(), [](const EntropyResult& a, const EntropyResult& b) {
        return a.sharpe_proxy > b.sharpe_proxy;
    });

    // Step 7: Print results
    std::cout << "================================================================" << std::endl;
    std::cout << "  TOP 25 CONFIGURATIONS (sorted by Sharpe proxy = (Return-1)/DD)" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::left << std::setw(3) << "#"
              << std::setw(22) << "Label"
              << std::right << std::setw(8) << "Return"
              << std::setw(7) << "MaxDD"
              << std::setw(8) << "Sharpe"
              << std::setw(8) << "Trades"
              << std::setw(9) << "Swap$"
              << std::setw(8) << "Core"
              << std::setw(8) << "Entrop"
              << std::setw(7) << "SL%"
              << std::endl;
    std::cout << std::string(95, '-') << std::endl;

    for (int i = 0; i < std::min(25, (int)g_results.size()); i++) {
        const auto& r = g_results[i];
        double sl_pct = (r.entropy_trades_opened > 0)
            ? 100.0 * r.entropy_trades_sl / r.entropy_trades_opened : 0;

        std::cout << std::left << std::setw(3) << (i + 1)
                  << std::setw(22) << r.label
                  << std::right << std::fixed
                  << std::setw(6) << std::setprecision(2) << r.return_mult << "x"
                  << std::setw(6) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(8) << std::setprecision(2) << r.sharpe_proxy
                  << std::setw(8) << r.total_trades
                  << std::setw(8) << std::setprecision(0) << r.total_swap
                  << std::setw(8) << r.core_trades_opened
                  << std::setw(8) << r.entropy_trades_opened
                  << std::setw(6) << std::setprecision(0) << sl_pct << "%"
                  << (r.margin_stopped ? " SO" : "")
                  << std::endl;
    }

    // Find baseline
    EntropyResult baseline;
    for (const auto& r : g_results) {
        if (r.is_baseline) { baseline = r; break; }
    }

    std::cout << std::string(95, '-') << std::endl;
    int baseline_rank = 0;
    for (size_t i = 0; i < g_results.size(); i++) {
        if (g_results[i].is_baseline) { baseline_rank = (int)i + 1; break; }
    }
    std::cout << "BASELINE (FillUpOscillation): " << std::fixed << std::setprecision(2)
              << baseline.return_mult << "x return, " << std::setprecision(1)
              << baseline.max_dd_pct << "% DD, Sharpe=" << std::setprecision(2)
              << baseline.sharpe_proxy << " (rank #" << baseline_rank << ")" << std::endl;

    // Analysis: Does entropy export reduce DD?
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  KEY QUESTION: Does entropy export reduce overall DD?" << std::endl;
    std::cout << "================================================================" << std::endl;

    // Find configs that reduce DD vs baseline
    struct DDComparison {
        EntropyResult result;
        double dd_reduction;
        double return_loss_pct;
    };
    std::vector<DDComparison> dd_reducers;

    for (const auto& r : g_results) {
        if (r.is_baseline) continue;
        double dd_red = baseline.max_dd_pct - r.max_dd_pct;
        double ret_loss = (baseline.return_mult - r.return_mult) / baseline.return_mult * 100.0;

        if (dd_red > 0) {
            dd_reducers.push_back({r, dd_red, ret_loss});
        }
    }

    std::sort(dd_reducers.begin(), dd_reducers.end(),
              [](const DDComparison& a, const DDComparison& b) {
                  return a.dd_reduction > b.dd_reduction;
              });

    if (dd_reducers.empty()) {
        std::cout << std::endl;
        std::cout << "*** NO CONFIGURATIONS REDUCED DD BELOW BASELINE ***" << std::endl;
        std::cout << "The entropy export hypothesis is NOT SUPPORTED." << std::endl;
    } else {
        std::cout << std::endl;
        std::cout << "Configs with DD lower than baseline (" << dd_reducers.size() << " found):" << std::endl;
        std::cout << std::left << std::setw(22) << "Label"
                  << std::right << std::setw(9) << "Return"
                  << std::setw(8) << "MaxDD%"
                  << std::setw(9) << "DD Saved"
                  << std::setw(10) << "Ret Loss"
                  << std::setw(8) << "Sharpe"
                  << std::endl;
        std::cout << std::string(70, '-') << std::endl;

        for (size_t i = 0; i < std::min((size_t)15, dd_reducers.size()); i++) {
            const auto& d = dd_reducers[i];
            std::cout << std::left << std::setw(22) << d.result.label
                      << std::right << std::fixed
                      << std::setw(7) << std::setprecision(2) << d.result.return_mult << "x"
                      << std::setw(7) << std::setprecision(1) << d.result.max_dd_pct << "%"
                      << std::setw(7) << std::setprecision(1) << d.dd_reduction << "%"
                      << std::setw(9) << std::setprecision(1) << d.return_loss_pct << "%"
                      << std::setw(8) << std::setprecision(2) << d.result.sharpe_proxy
                      << std::endl;
        }
    }

    // Analysis by core ratio
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  ANALYSIS BY CORE RATIO (best of each ratio)" << std::endl;
    std::cout << "================================================================" << std::endl;

    std::vector<double> ratios = {1.0, 0.9, 0.8, 0.7, 0.6, 0.5, 0.4, 0.3, 0.2, 0.1, 0.0};
    std::cout << std::left << std::setw(10) << "Ratio"
              << std::setw(22) << "Best Config"
              << std::right << std::setw(9) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(8) << "Sharpe"
              << std::setw(8) << "vs Base"
              << std::endl;
    std::cout << std::string(75, '-') << std::endl;

    for (double ratio : ratios) {
        const EntropyResult* best = nullptr;
        for (const auto& r : g_results) {
            if (std::abs(r.core_ratio - ratio) < 0.01) {
                if (!best || r.sharpe_proxy > best->sharpe_proxy) {
                    best = &r;
                }
            }
        }
        if (best) {
            double vs_base = (baseline.sharpe_proxy > 0)
                ? (best->sharpe_proxy - baseline.sharpe_proxy) / baseline.sharpe_proxy * 100.0 : 0;
            std::cout << std::left << std::setw(10) << std::fixed << std::setprecision(0)
                      << (ratio * 100) << "%"
                      << std::setw(22) << best->label
                      << std::right
                      << std::setw(7) << std::setprecision(2) << best->return_mult << "x"
                      << std::setw(7) << std::setprecision(1) << best->max_dd_pct << "%"
                      << std::setw(8) << std::setprecision(2) << best->sharpe_proxy
                      << std::setw(7) << std::setprecision(1) << vs_base << "%"
                      << std::endl;
        }
    }

    // Entropy statistics
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  ENTROPY POSITION STATISTICS" << std::endl;
    std::cout << "================================================================" << std::endl;

    // Group by SL multiplier
    std::vector<double> sl_values = {0.3, 0.5, 0.7, 1.0, 1.5, 2.0};
    std::cout << "Average entropy SL hit rate by SL multiplier:" << std::endl;
    std::cout << std::left << std::setw(12) << "SL Mult"
              << std::right << std::setw(12) << "Avg SL%"
              << std::setw(12) << "Avg Return"
              << std::setw(12) << "Avg DD%"
              << std::endl;
    std::cout << std::string(50, '-') << std::endl;

    for (double sl : sl_values) {
        int count = 0;
        double sum_sl_pct = 0, sum_ret = 0, sum_dd = 0;
        for (const auto& r : g_results) {
            if (r.is_baseline) continue;
            if (std::abs(r.entropy_sl_mult - sl) < 0.01 && !r.entropy_smaller_size) {
                count++;
                double sl_pct = (r.entropy_trades_opened > 0)
                    ? 100.0 * r.entropy_trades_sl / r.entropy_trades_opened : 0;
                sum_sl_pct += sl_pct;
                sum_ret += r.return_mult;
                sum_dd += r.max_dd_pct;
            }
        }
        if (count > 0) {
            std::cout << std::left << std::setw(12) << std::fixed << std::setprecision(1)
                      << sl << "x"
                      << std::right
                      << std::setw(11) << std::setprecision(1) << (sum_sl_pct / count) << "%"
                      << std::setw(11) << std::setprecision(2) << (sum_ret / count) << "x"
                      << std::setw(11) << std::setprecision(1) << (sum_dd / count) << "%"
                      << std::endl;
        }
    }

    // Final conclusion
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  CONCLUSION" << std::endl;
    std::cout << "================================================================" << std::endl;

    // Check if any entropy config beats baseline on Sharpe
    bool any_beats_baseline = false;
    for (const auto& r : g_results) {
        if (!r.is_baseline && r.sharpe_proxy > baseline.sharpe_proxy) {
            any_beats_baseline = true;
            break;
        }
    }

    if (any_beats_baseline) {
        std::cout << "FINDING: Some entropy export configurations achieve better risk-adjusted returns." << std::endl;
    } else {
        std::cout << "FINDING: NO entropy export configuration beats the baseline on risk-adjusted return." << std::endl;
    }

    // Check DD reduction effectiveness
    bool meaningful_dd_reduction = false;
    for (const auto& d : dd_reducers) {
        if (d.dd_reduction > 5.0 && d.return_loss_pct < 30.0) {
            meaningful_dd_reduction = true;
            break;
        }
    }

    if (meaningful_dd_reduction) {
        std::cout << "FINDING: Entropy export CAN reduce DD by >5% with <30% return loss." << std::endl;
    } else {
        std::cout << "FINDING: Entropy export does NOT meaningfully reduce DD at acceptable return cost." << std::endl;
    }

    std::cout << std::endl;
    std::cout << "Total configurations tested: " << g_results.size() << std::endl;
    std::cout << "Execution time: " << duration.count() << "s (" << num_threads << " threads)" << std::endl;

    return 0;
}
