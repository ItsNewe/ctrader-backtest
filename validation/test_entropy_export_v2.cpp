/**
 * Entropy Export V2 (Dissipative Structure) Parallel Parameter Sweep
 *
 * CONCEPT: Ordered structures maintain themselves by exporting entropy.
 * For trading: deliberately take small, frequent losses (entropy export)
 * to enable the larger profit structure to persist.
 *
 * This tests whether cutting losers early (based on TIME and LOSS thresholds)
 * improves overall returns and/or reduces drawdown compared to the baseline
 * "hold until TP" approach.
 *
 * Different from V1 (strategy_entropy_export.h) which splits positions into
 * "core" and "entropy" categories with predefined stop-losses.
 *
 * V2 APPROACH:
 *   - Track each position's age and unrealized P/L
 *   - Close positions that are LOSING AND exceed time/loss thresholds
 *   - Idea: positions that don't revert quickly are "stuck" - export them
 *
 * Uses PARALLEL pattern: loads tick data ONCE into shared memory, then
 * tests multiple configurations in parallel using std::thread + WorkQueue.
 */

#include "../include/strategy_entropy_export_v2.h"
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
    StrategyEntropyExportV2::ExportMode mode;
    double time_threshold_minutes;
    double loss_threshold_dollars;
    std::string label;
};

struct EntropyResult {
    std::string label;
    std::string mode_name;
    double time_threshold;
    double loss_threshold;
    double return_mult;
    double max_dd_pct;
    int total_trades;
    int trades_tp;
    int trades_entropy;
    double loss_from_entropy;
    double avg_entropy_hold_min;
    double avg_tp_hold_min;
    double total_swap;
    double sharpe_proxy;
    bool stop_out;
    double entropy_efficiency;  // How much DD saved per $ lost to entropy
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
// Mode name helper
// ============================================================================
std::string GetModeName(StrategyEntropyExportV2::ExportMode mode) {
    switch (mode) {
        case StrategyEntropyExportV2::BASELINE: return "BASELINE";
        case StrategyEntropyExportV2::TIME_ONLY: return "TIME_ONLY";
        case StrategyEntropyExportV2::LOSS_ONLY: return "LOSS_ONLY";
        case StrategyEntropyExportV2::TIME_AND_LOSS: return "TIME_AND_LOSS";
        case StrategyEntropyExportV2::TIME_OR_LOSS: return "TIME_OR_LOSS";
        default: return "UNKNOWN";
    }
}

// ============================================================================
// Run single test with shared tick data
// ============================================================================
EntropyResult run_test(const EntropyTask& task, const std::vector<Tick>& ticks) {
    EntropyResult r;
    r.label = task.label;
    r.mode_name = GetModeName(task.mode);
    r.time_threshold = task.time_threshold_minutes;
    r.loss_threshold = task.loss_threshold_dollars;
    r.return_mult = 0;
    r.max_dd_pct = 0;
    r.total_trades = 0;
    r.trades_tp = 0;
    r.trades_entropy = 0;
    r.loss_from_entropy = 0;
    r.avg_entropy_hold_min = 0;
    r.avg_tp_hold_min = 0;
    r.total_swap = 0;
    r.sharpe_proxy = 0;
    r.stop_out = false;
    r.entropy_efficiency = 0;

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

        StrategyEntropyExportV2::Config strategy_cfg;
        strategy_cfg.survive_pct = 13.0;
        strategy_cfg.base_spacing = 1.5;
        strategy_cfg.min_volume = 0.01;
        strategy_cfg.max_volume = 10.0;
        strategy_cfg.contract_size = 100.0;
        strategy_cfg.leverage = 500.0;
        strategy_cfg.mode = task.mode;
        strategy_cfg.time_threshold_minutes = task.time_threshold_minutes;
        strategy_cfg.loss_threshold_dollars = task.loss_threshold_dollars;
        strategy_cfg.volatility_lookback_hours = 4.0;
        strategy_cfg.typical_vol_pct = 0.5;

        StrategyEntropyExportV2 strategy(strategy_cfg);

        engine.RunWithTicks(ticks, [&strategy](const Tick& t, TickBasedEngine& e) {
            strategy.OnTick(t, e);
        });

        strategy.FinalizeStats();
        auto res = engine.GetResults();
        const auto& stats = strategy.GetStats();

        r.return_mult = res.final_balance / 10000.0;
        r.max_dd_pct = res.max_drawdown_pct;
        r.total_trades = stats.total_trades_opened;
        r.trades_tp = stats.trades_closed_tp;
        r.trades_entropy = stats.trades_closed_entropy;
        r.loss_from_entropy = stats.total_loss_from_entropy;
        r.avg_entropy_hold_min = stats.avg_entropy_hold_minutes;
        r.avg_tp_hold_min = stats.avg_tp_hold_minutes;
        r.total_swap = res.total_swap_charged;
        r.sharpe_proxy = (r.max_dd_pct > 0) ? (r.return_mult - 1.0) / (r.max_dd_pct / 100.0) : 0;
        r.stop_out = res.stop_out_occurred;

    } catch (const std::exception& e) {
        r.stop_out = true;
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
        if (done % 10 == 0 || done == total) {
            std::lock_guard<std::mutex> lock(g_output_mutex);
            std::cout << "\rProgress: " << done << "/" << total
                      << " (" << std::fixed << std::setprecision(1)
                      << (100.0 * done / total) << "%)" << std::flush;
        }
    }
}

// ============================================================================
// Generate all entropy export configurations to test
// ============================================================================
std::vector<EntropyTask> GenerateTasks() {
    std::vector<EntropyTask> tasks;

    // 1. BASELINE (no entropy export)
    tasks.push_back({StrategyEntropyExportV2::BASELINE, 0, 0, "BASELINE"});

    // Time thresholds to test (in minutes)
    std::vector<double> time_thresholds = {5, 15, 30, 60, 120, 240};

    // Loss thresholds to test (in dollars per position)
    std::vector<double> loss_thresholds = {1, 2, 5, 10, 20};

    // 2. TIME_ONLY mode - close losers after time threshold
    for (double t : time_thresholds) {
        std::string label = "TIME_" + std::to_string((int)t) + "m";
        tasks.push_back({StrategyEntropyExportV2::TIME_ONLY, t, 0, label});
    }

    // 3. LOSS_ONLY mode - close losers exceeding loss threshold
    for (double l : loss_thresholds) {
        std::string label = "LOSS_$" + std::to_string((int)l);
        tasks.push_back({StrategyEntropyExportV2::LOSS_ONLY, 0, l, label});
    }

    // 4. TIME_AND_LOSS mode (conservative) - close if BOTH conditions met
    for (double t : time_thresholds) {
        for (double l : loss_thresholds) {
            std::string label = "AND_" + std::to_string((int)t) + "m_$" + std::to_string((int)l);
            tasks.push_back({StrategyEntropyExportV2::TIME_AND_LOSS, t, l, label});
        }
    }

    // 5. TIME_OR_LOSS mode (aggressive) - close if EITHER condition met
    for (double t : time_thresholds) {
        for (double l : loss_thresholds) {
            std::string label = "OR_" + std::to_string((int)t) + "m_$" + std::to_string((int)l);
            tasks.push_back({StrategyEntropyExportV2::TIME_OR_LOSS, t, l, label});
        }
    }

    return tasks;
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "  ENTROPY EXPORT V2 (DISSIPATIVE STRUCTURE) PARALLEL SWEEP" << std::endl;
    std::cout << "  Concept: Cut losers early to preserve capital for winners" << std::endl;
    std::cout << "  Base: FillUpOscillation ADAPTIVE_SPACING (survive=13%, spacing=$1.50)" << std::endl;
    std::cout << "  Data: XAUUSD 2025 (full year)" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;
    std::cout << "MODES:" << std::endl;
    std::cout << "  BASELINE:      Hold all positions until TP (control)" << std::endl;
    std::cout << "  TIME_ONLY:     Close losers after N minutes" << std::endl;
    std::cout << "  LOSS_ONLY:     Close losers exceeding $X unrealized loss" << std::endl;
    std::cout << "  TIME_AND_LOSS: Close only if BOTH time AND loss thresholds met" << std::endl;
    std::cout << "  TIME_OR_LOSS:  Close if EITHER time OR loss threshold met" << std::endl;
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

    // Step 6: Find baseline for comparison
    EntropyResult baseline;
    for (const auto& r : g_results) {
        if (r.label == "BASELINE") {
            baseline = r;
            break;
        }
    }

    // Step 7: Calculate efficiency metrics
    for (auto& r : g_results) {
        if (r.label != "BASELINE" && r.loss_from_entropy < 0) {
            // DD saved vs baseline
            double dd_saved = baseline.max_dd_pct - r.max_dd_pct;
            // Loss given up to entropy export
            double loss_given = -r.loss_from_entropy;  // Make positive
            // Efficiency: DD% saved per $1000 lost
            if (loss_given > 0) {
                r.entropy_efficiency = dd_saved / (loss_given / 1000.0);
            }
        }
    }

    // Step 8: Sort by sharpe_proxy (best risk-adjusted return first)
    std::sort(g_results.begin(), g_results.end(), [](const EntropyResult& a, const EntropyResult& b) {
        return a.sharpe_proxy > b.sharpe_proxy;
    });

    // Step 9: Print results
    std::cout << "================================================================" << std::endl;
    std::cout << "  ALL RESULTS (sorted by Sharpe proxy = (Return-1)/DD)" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::left << std::setw(4) << "#"
              << std::setw(22) << "Label"
              << std::right << std::setw(8) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(8) << "Trades"
              << std::setw(7) << "TP"
              << std::setw(7) << "Entr"
              << std::setw(10) << "EntrLoss"
              << std::setw(8) << "Sharpe"
              << std::setw(6) << "SO"
              << std::endl;
    std::cout << std::string(95, '-') << std::endl;

    for (int i = 0; i < (int)g_results.size(); i++) {
        const auto& r = g_results[i];
        std::cout << std::left << std::setw(4) << (i + 1)
                  << std::setw(22) << r.label
                  << std::right << std::fixed
                  << std::setw(6) << std::setprecision(2) << r.return_mult << "x"
                  << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(8) << r.total_trades
                  << std::setw(7) << r.trades_tp
                  << std::setw(7) << r.trades_entropy
                  << std::setw(9) << std::setprecision(0) << r.loss_from_entropy
                  << std::setw(8) << std::setprecision(2) << r.sharpe_proxy
                  << std::setw(6) << (r.stop_out ? "YES" : "no")
                  << std::endl;
    }

    // Step 10: Print baseline comparison
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  BASELINE vs BEST ENTROPY EXPORT" << std::endl;
    std::cout << "================================================================" << std::endl;

    // Find baseline rank
    int baseline_rank = 0;
    for (int i = 0; i < (int)g_results.size(); i++) {
        if (g_results[i].label == "BASELINE") {
            baseline_rank = i + 1;
            break;
        }
    }

    std::cout << "BASELINE:" << std::endl;
    std::cout << "  Return: " << std::fixed << std::setprecision(2) << baseline.return_mult << "x" << std::endl;
    std::cout << "  Max DD: " << std::setprecision(1) << baseline.max_dd_pct << "%" << std::endl;
    std::cout << "  Trades: " << baseline.total_trades << " (all to TP)" << std::endl;
    std::cout << "  Sharpe: " << std::setprecision(2) << baseline.sharpe_proxy << std::endl;
    std::cout << "  Rank:   #" << baseline_rank << " of " << g_results.size() << std::endl;
    std::cout << std::endl;

    // Find best non-baseline
    const EntropyResult* best = nullptr;
    for (const auto& r : g_results) {
        if (r.label != "BASELINE") {
            best = &r;
            break;  // Already sorted by sharpe
        }
    }

    if (best) {
        std::cout << "BEST ENTROPY EXPORT (" << best->label << "):" << std::endl;
        std::cout << "  Return: " << std::setprecision(2) << best->return_mult << "x"
                  << " (" << ((best->return_mult / baseline.return_mult - 1) * 100) << "% vs baseline)" << std::endl;
        std::cout << "  Max DD: " << std::setprecision(1) << best->max_dd_pct << "%"
                  << " (" << (baseline.max_dd_pct - best->max_dd_pct) << "% saved)" << std::endl;
        std::cout << "  TP Trades: " << best->trades_tp << std::endl;
        std::cout << "  Entropy Closes: " << best->trades_entropy << std::endl;
        std::cout << "  Loss from Entropy: $" << std::setprecision(0) << best->loss_from_entropy << std::endl;
        std::cout << "  Sharpe: " << std::setprecision(2) << best->sharpe_proxy << std::endl;
    }

    // Step 11: Analysis by mode
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  ANALYSIS BY MODE" << std::endl;
    std::cout << "================================================================" << std::endl;

    std::vector<std::string> modes = {"BASELINE", "TIME_ONLY", "LOSS_ONLY", "TIME_AND_LOSS", "TIME_OR_LOSS"};

    for (const auto& mode : modes) {
        // Find best config for this mode
        const EntropyResult* best_in_mode = nullptr;
        double best_sharpe = -1e9;
        int count = 0;
        double avg_return = 0, avg_dd = 0;

        for (const auto& r : g_results) {
            if (r.mode_name == mode || (mode == "BASELINE" && r.label == "BASELINE")) {
                count++;
                avg_return += r.return_mult;
                avg_dd += r.max_dd_pct;
                if (r.sharpe_proxy > best_sharpe) {
                    best_sharpe = r.sharpe_proxy;
                    best_in_mode = &r;
                }
            }
        }

        if (count > 0) {
            avg_return /= count;
            avg_dd /= count;

            std::cout << mode << " (" << count << " configs):" << std::endl;
            std::cout << "  Avg Return: " << std::setprecision(2) << avg_return << "x" << std::endl;
            std::cout << "  Avg Max DD: " << std::setprecision(1) << avg_dd << "%" << std::endl;
            if (best_in_mode) {
                std::cout << "  Best:       " << best_in_mode->label
                          << " (" << std::setprecision(2) << best_in_mode->return_mult << "x, "
                          << std::setprecision(1) << best_in_mode->max_dd_pct << "% DD, "
                          << "Sharpe=" << std::setprecision(2) << best_in_mode->sharpe_proxy << ")" << std::endl;
            }
            std::cout << std::endl;
        }
    }

    // Step 12: Key insights
    std::cout << "================================================================" << std::endl;
    std::cout << "  KEY INSIGHTS" << std::endl;
    std::cout << "================================================================" << std::endl;

    // Count configs that beat baseline on return
    int beat_return = 0;
    int beat_dd = 0;
    int beat_sharpe = 0;

    for (const auto& r : g_results) {
        if (r.label == "BASELINE") continue;
        if (r.return_mult > baseline.return_mult) beat_return++;
        if (r.max_dd_pct < baseline.max_dd_pct) beat_dd++;
        if (r.sharpe_proxy > baseline.sharpe_proxy) beat_sharpe++;
    }

    int non_baseline = (int)g_results.size() - 1;
    std::cout << "Configs beating baseline on:" << std::endl;
    std::cout << "  Return:        " << beat_return << "/" << non_baseline
              << " (" << std::setprecision(1) << (100.0 * beat_return / non_baseline) << "%)" << std::endl;
    std::cout << "  Drawdown:      " << beat_dd << "/" << non_baseline
              << " (" << (100.0 * beat_dd / non_baseline) << "%)" << std::endl;
    std::cout << "  Sharpe (both): " << beat_sharpe << "/" << non_baseline
              << " (" << (100.0 * beat_sharpe / non_baseline) << "%)" << std::endl;

    // Time threshold analysis
    std::cout << std::endl;
    std::cout << "Time threshold effect (TIME_ONLY mode):" << std::endl;
    for (const auto& r : g_results) {
        if (r.mode_name == "TIME_ONLY") {
            double return_diff = (r.return_mult / baseline.return_mult - 1) * 100;
            double dd_diff = baseline.max_dd_pct - r.max_dd_pct;
            std::cout << "  " << std::setw(6) << (int)r.time_threshold << " min: "
                      << std::setprecision(2) << r.return_mult << "x ("
                      << (return_diff >= 0 ? "+" : "") << return_diff << "%), "
                      << "DD " << std::setprecision(1) << r.max_dd_pct << "% ("
                      << (dd_diff >= 0 ? "-" : "+") << std::abs(dd_diff) << "%)"
                      << ", Entr=" << r.trades_entropy << std::endl;
        }
    }

    // Loss threshold analysis
    std::cout << std::endl;
    std::cout << "Loss threshold effect (LOSS_ONLY mode):" << std::endl;
    for (const auto& r : g_results) {
        if (r.mode_name == "LOSS_ONLY") {
            double return_diff = (r.return_mult / baseline.return_mult - 1) * 100;
            double dd_diff = baseline.max_dd_pct - r.max_dd_pct;
            std::cout << "  $" << std::setw(4) << (int)r.loss_threshold << " loss: "
                      << std::setprecision(2) << r.return_mult << "x ("
                      << (return_diff >= 0 ? "+" : "") << return_diff << "%), "
                      << "DD " << std::setprecision(1) << r.max_dd_pct << "% ("
                      << (dd_diff >= 0 ? "-" : "+") << std::abs(dd_diff) << "%)"
                      << ", Entr=" << r.trades_entropy << std::endl;
        }
    }

    // Hold time analysis
    std::cout << std::endl;
    std::cout << "Average hold times (minutes):" << std::endl;
    std::cout << "  BASELINE avg TP hold: " << std::setprecision(1) << baseline.avg_tp_hold_min << " min" << std::endl;
    for (const auto& r : g_results) {
        if (r.mode_name == "TIME_ONLY" && r.trades_entropy > 0) {
            std::cout << "  " << r.label << ": TP=" << std::setprecision(1) << r.avg_tp_hold_min
                      << " min, Entropy=" << r.avg_entropy_hold_min << " min" << std::endl;
        }
    }

    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  CONCLUSION" << std::endl;
    std::cout << "================================================================" << std::endl;

    if (beat_sharpe > 0) {
        std::cout << "FINDING: " << beat_sharpe << " entropy export configs achieve better risk-adjusted returns." << std::endl;
        std::cout << "The dissipative structure hypothesis is SUPPORTED for some parameter ranges." << std::endl;
    } else {
        std::cout << "FINDING: NO entropy export configuration beats the baseline on risk-adjusted return." << std::endl;
        std::cout << "The dissipative structure hypothesis is NOT SUPPORTED." << std::endl;
        std::cout << "Cutting losers early does not improve the FillUpOscillation strategy." << std::endl;
    }

    if (beat_dd > non_baseline * 0.5) {
        std::cout << "FINDING: Entropy export CAN reduce DD but at significant return cost." << std::endl;
    }

    std::cout << std::endl;
    std::cout << "Total configurations tested: " << g_results.size() << std::endl;
    std::cout << "Execution time: " << duration.count() << "s (" << num_threads << " threads)" << std::endl;

    return 0;
}
