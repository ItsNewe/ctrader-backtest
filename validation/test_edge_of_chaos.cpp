/**
 * Edge of Chaos Investigation
 *
 * Complex systems are most adaptive at the boundary between order and chaos.
 * This test sweeps FillUpOscillation parameters and measures "chaoticity":
 *   1. Position count volatility (how wildly positions fluctuate)
 *   2. Equity curve smoothness (variance of equity changes)
 *   3. Spacing change frequency (how often adaptive spacing adjusts)
 *   4. Return stability across parameter neighbors
 *
 * Goal: Find the "Goldilocks zone" where performance is good AND behavior is stable.
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
struct ChaosTask {
    double survive_pct;
    double base_spacing;
    double lookback_hours;
    std::string label;
};

struct ChaosResult {
    std::string label;
    double survive_pct;
    double base_spacing;
    double lookback_hours;

    // Standard performance metrics
    double return_mult;
    double max_dd_pct;
    int total_trades;
    double total_swap;

    // Edge of chaos metrics
    int max_position_count;          // Peak open positions
    double position_count_mean;      // Average position count
    double position_count_std;       // Std dev of position count (stability)
    int spacing_changes;             // How often spacing updates
    double equity_velocity_std;      // Smoothness of equity curve
    double equity_velocity_mean;     // Average equity velocity

    // Derived metrics
    double sharpe_proxy;             // (return - 1) / (DD/100)
    double chaos_score;              // Composite chaoticity measure
    double stability_score;          // Inverse of chaos

    bool stopped_out;
};

// ============================================================================
// Chaos Metrics Tracker - wraps strategy to collect behavioral data
// ============================================================================
class ChaosTracker {
public:
    ChaosTracker(double survive, double spacing, double lookback)
        : strategy_(survive, spacing, 0.01, 10.0, 100.0, 500.0,
                   FillUpOscillation::ADAPTIVE_SPACING, 0.1, 30.0, lookback),
          last_equity_(0.0),
          last_position_count_(0),
          sample_counter_(0),
          sample_interval_(1000)  // Sample every 1000 ticks
    {
        position_counts_.reserve(60000);  // ~60k samples for full year
        equity_velocities_.reserve(60000);
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        strategy_.OnTick(tick, engine);

        sample_counter_++;
        if (sample_counter_ % sample_interval_ == 0) {
            // Sample position count
            int pos_count = (int)engine.GetOpenPositions().size();
            position_counts_.push_back(pos_count);

            // Sample equity velocity (change since last sample)
            double equity = engine.GetEquity();
            if (last_equity_ > 0) {
                double velocity = equity - last_equity_;
                equity_velocities_.push_back(velocity);
            }
            last_equity_ = equity;
            last_position_count_ = pos_count;
        }
    }

    int GetSpacingChanges() const { return strategy_.GetAdaptiveSpacingChanges(); }

    // Calculate position count statistics
    double GetPositionCountMean() const {
        if (position_counts_.empty()) return 0;
        double sum = 0;
        for (int c : position_counts_) sum += c;
        return sum / position_counts_.size();
    }

    double GetPositionCountStd() const {
        if (position_counts_.size() < 2) return 0;
        double mean = GetPositionCountMean();
        double sum_sq = 0;
        for (int c : position_counts_) {
            double diff = c - mean;
            sum_sq += diff * diff;
        }
        return std::sqrt(sum_sq / position_counts_.size());
    }

    int GetMaxPositionCount() const {
        int max_count = 0;
        for (int c : position_counts_) {
            if (c > max_count) max_count = c;
        }
        return max_count;
    }

    // Calculate equity velocity statistics
    double GetEquityVelocityMean() const {
        if (equity_velocities_.empty()) return 0;
        double sum = 0;
        for (double v : equity_velocities_) sum += v;
        return sum / equity_velocities_.size();
    }

    double GetEquityVelocityStd() const {
        if (equity_velocities_.size() < 2) return 0;
        double mean = GetEquityVelocityMean();
        double sum_sq = 0;
        for (double v : equity_velocities_) {
            double diff = v - mean;
            sum_sq += diff * diff;
        }
        return std::sqrt(sum_sq / equity_velocities_.size());
    }

private:
    FillUpOscillation strategy_;
    double last_equity_;
    int last_position_count_;
    int sample_counter_;
    int sample_interval_;
    std::vector<int> position_counts_;
    std::vector<double> equity_velocities_;
};

// ============================================================================
// Work Queue for thread pool
// ============================================================================
class WorkQueue {
    std::queue<ChaosTask> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool done_ = false;

public:
    void push(const ChaosTask& task) {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push(task);
        cv_.notify_one();
    }

    bool pop(ChaosTask& task) {
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
std::vector<ChaosResult> g_results;

// ============================================================================
// Run single test with shared tick data
// ============================================================================
ChaosResult run_test(const ChaosTask& task, const std::vector<Tick>& ticks) {
    ChaosResult r;
    r.label = task.label;
    r.survive_pct = task.survive_pct;
    r.base_spacing = task.base_spacing;
    r.lookback_hours = task.lookback_hours;
    r.return_mult = 0;
    r.max_dd_pct = 0;
    r.total_trades = 0;
    r.total_swap = 0;
    r.max_position_count = 0;
    r.position_count_mean = 0;
    r.position_count_std = 0;
    r.spacing_changes = 0;
    r.equity_velocity_std = 0;
    r.equity_velocity_mean = 0;
    r.sharpe_proxy = 0;
    r.chaos_score = 0;
    r.stability_score = 0;
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
    cfg.start_date = "2025.01.01";
    cfg.end_date = "2025.12.30";
    cfg.verbose = false;

    TickDataConfig tc;
    tc.file_path = "";
    tc.load_all_into_memory = false;
    cfg.tick_data_config = tc;

    try {
        TickBasedEngine engine(cfg);
        ChaosTracker tracker(task.survive_pct, task.base_spacing, task.lookback_hours);

        engine.RunWithTicks(ticks, [&tracker](const Tick& t, TickBasedEngine& e) {
            tracker.OnTick(t, e);
        });

        auto res = engine.GetResults();
        r.return_mult = res.final_balance / 10000.0;
        r.max_dd_pct = res.max_drawdown_pct;
        r.total_trades = res.total_trades;
        r.total_swap = res.total_swap_charged;

        // Chaos metrics
        r.max_position_count = tracker.GetMaxPositionCount();
        r.position_count_mean = tracker.GetPositionCountMean();
        r.position_count_std = tracker.GetPositionCountStd();
        r.spacing_changes = tracker.GetSpacingChanges();
        r.equity_velocity_mean = tracker.GetEquityVelocityMean();
        r.equity_velocity_std = tracker.GetEquityVelocityStd();

        // Sharpe proxy
        r.sharpe_proxy = (r.max_dd_pct > 0) ? (r.return_mult - 1.0) / (r.max_dd_pct / 100.0) : 0;

        // Chaos score: higher = more chaotic behavior
        // Normalize components and combine:
        // - Position count volatility (std / mean) - range [0, 2+]
        // - Equity velocity volatility (std / mean absolute) - range [0, 10+]
        // - Spacing changes / 10000 - range [0, 1+]
        double pos_chaos = (r.position_count_mean > 0) ? r.position_count_std / r.position_count_mean : 0;
        double vel_chaos = (std::abs(r.equity_velocity_mean) > 0.001)
            ? r.equity_velocity_std / std::abs(r.equity_velocity_mean) : r.equity_velocity_std / 100.0;
        double spacing_chaos = r.spacing_changes / 10000.0;

        r.chaos_score = pos_chaos + vel_chaos * 0.1 + spacing_chaos;
        r.stability_score = (r.chaos_score > 0) ? 1.0 / r.chaos_score : 100.0;

        r.stopped_out = (res.final_balance < 1000);  // Lost 90%+

    } catch (const std::exception& e) {
        r.stopped_out = true;
        r.max_dd_pct = 100.0;
    }

    return r;
}

void worker(WorkQueue& queue, int total, const std::vector<Tick>& ticks) {
    ChaosTask task;
    while (queue.pop(task)) {
        ChaosResult r = run_test(task, ticks);

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
// Generate parameter sweep tasks
// ============================================================================
std::vector<ChaosTask> GenerateTasks() {
    std::vector<ChaosTask> tasks;

    // Parameter sweep grid
    std::vector<double> survive_vals = {10, 11, 12, 13, 14, 15, 16, 18, 20};
    std::vector<double> spacing_vals = {0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0};
    std::vector<double> lookback_vals = {1, 2, 4, 8};

    for (double survive : survive_vals) {
        for (double spacing : spacing_vals) {
            for (double lookback : lookback_vals) {
                std::string label = "s" + std::to_string((int)survive) +
                                   "_sp" + std::to_string((int)(spacing*10)) +
                                   "_lb" + std::to_string((int)lookback);
                tasks.push_back({survive, spacing, lookback, label});
            }
        }
    }

    return tasks;
}

// ============================================================================
// Calculate parameter sensitivity (how much results change with small param changes)
// ============================================================================
void AnalyzeParameterSensitivity(const std::vector<ChaosResult>& results) {
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  PARAMETER SENSITIVITY ANALYSIS (Phase Transition Detection)" << std::endl;
    std::cout << "================================================================" << std::endl;

    // Build map for quick lookup
    std::map<std::string, const ChaosResult*> result_map;
    for (const auto& r : results) {
        result_map[r.label] = &r;
    }

    // For each config, calculate sensitivity to neighboring configs
    struct Sensitivity {
        std::string label;
        double return_mult;
        double max_dd_pct;
        double neighbor_return_variance;  // Variance of return among neighbors
        double neighbor_dd_variance;       // Variance of DD among neighbors
        bool at_phase_boundary;           // Sharp change nearby?
    };
    std::vector<Sensitivity> sensitivities;

    for (const auto& r : results) {
        if (r.stopped_out) continue;

        Sensitivity s;
        s.label = r.label;
        s.return_mult = r.return_mult;
        s.max_dd_pct = r.max_dd_pct;

        // Find neighbors (differ by one parameter step)
        std::vector<double> neighbor_returns;
        std::vector<double> neighbor_dds;

        // Get current params
        double survive = r.survive_pct;
        double spacing = r.base_spacing;
        double lookback = r.lookback_hours;

        // Define neighbor steps
        std::vector<double> survive_steps = {-1, 1, -2, 2};
        std::vector<double> spacing_steps = {-0.5, 0.5, -1.0, 1.0};
        std::vector<double> lookback_steps = {-1, 1, -2, 2, -4, 4};

        // Check survive neighbors
        for (double ds : survive_steps) {
            std::string nlabel = "s" + std::to_string((int)(survive + ds)) +
                                "_sp" + std::to_string((int)(spacing*10)) +
                                "_lb" + std::to_string((int)lookback);
            auto it = result_map.find(nlabel);
            if (it != result_map.end() && !it->second->stopped_out) {
                neighbor_returns.push_back(it->second->return_mult);
                neighbor_dds.push_back(it->second->max_dd_pct);
            }
        }

        // Check spacing neighbors
        for (double dsp : spacing_steps) {
            double new_sp = spacing + dsp;
            if (new_sp < 0.5 || new_sp > 5.0) continue;
            std::string nlabel = "s" + std::to_string((int)survive) +
                                "_sp" + std::to_string((int)(new_sp*10)) +
                                "_lb" + std::to_string((int)lookback);
            auto it = result_map.find(nlabel);
            if (it != result_map.end() && !it->second->stopped_out) {
                neighbor_returns.push_back(it->second->return_mult);
                neighbor_dds.push_back(it->second->max_dd_pct);
            }
        }

        // Check lookback neighbors
        for (double dlb : lookback_steps) {
            double new_lb = lookback + dlb;
            if (new_lb < 1 || new_lb > 8) continue;
            std::string nlabel = "s" + std::to_string((int)survive) +
                                "_sp" + std::to_string((int)(spacing*10)) +
                                "_lb" + std::to_string((int)new_lb);
            auto it = result_map.find(nlabel);
            if (it != result_map.end() && !it->second->stopped_out) {
                neighbor_returns.push_back(it->second->return_mult);
                neighbor_dds.push_back(it->second->max_dd_pct);
            }
        }

        // Calculate variance
        s.at_phase_boundary = false;
        if (neighbor_returns.size() >= 2) {
            double mean_ret = 0, mean_dd = 0;
            for (size_t i = 0; i < neighbor_returns.size(); i++) {
                mean_ret += neighbor_returns[i];
                mean_dd += neighbor_dds[i];
            }
            mean_ret /= neighbor_returns.size();
            mean_dd /= neighbor_dds.size();

            double var_ret = 0, var_dd = 0;
            for (size_t i = 0; i < neighbor_returns.size(); i++) {
                var_ret += (neighbor_returns[i] - mean_ret) * (neighbor_returns[i] - mean_ret);
                var_dd += (neighbor_dds[i] - mean_dd) * (neighbor_dds[i] - mean_dd);
            }
            var_ret /= neighbor_returns.size();
            var_dd /= neighbor_dds.size();

            s.neighbor_return_variance = std::sqrt(var_ret);
            s.neighbor_dd_variance = std::sqrt(var_dd);

            // Phase boundary: large difference from neighbors (>50% return change)
            for (double nr : neighbor_returns) {
                if (std::abs(nr - r.return_mult) / r.return_mult > 0.5) {
                    s.at_phase_boundary = true;
                    break;
                }
            }
        } else {
            s.neighbor_return_variance = 0;
            s.neighbor_dd_variance = 0;
        }

        sensitivities.push_back(s);
    }

    // Sort by neighbor variance (low variance = stable region)
    std::sort(sensitivities.begin(), sensitivities.end(), [](const Sensitivity& a, const Sensitivity& b) {
        return a.neighbor_return_variance < b.neighbor_return_variance;
    });

    // Print most stable configs (low sensitivity)
    std::cout << std::endl << "TOP 20 MOST STABLE CONFIGS (low sensitivity to parameter changes):" << std::endl;
    std::cout << std::left << std::setw(18) << "Config"
              << std::right << std::setw(9) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(12) << "Ret Var"
              << std::setw(10) << "DD Var"
              << std::setw(10) << "Phase?"
              << std::endl;
    std::cout << std::string(70, '-') << std::endl;

    for (size_t i = 0; i < std::min((size_t)20, sensitivities.size()); i++) {
        const auto& s = sensitivities[i];
        std::cout << std::left << std::setw(18) << s.label
                  << std::right << std::fixed
                  << std::setw(7) << std::setprecision(2) << s.return_mult << "x"
                  << std::setw(7) << std::setprecision(1) << s.max_dd_pct << "%"
                  << std::setw(10) << std::setprecision(2) << s.neighbor_return_variance << "x"
                  << std::setw(9) << std::setprecision(1) << s.neighbor_dd_variance << "%"
                  << std::setw(10) << (s.at_phase_boundary ? "YES" : "no")
                  << std::endl;
    }

    // Print phase boundary configs (high sensitivity)
    std::cout << std::endl << "PHASE BOUNDARY CONFIGS (high sensitivity - sharp transitions nearby):" << std::endl;
    std::cout << std::string(70, '-') << std::endl;

    int boundary_count = 0;
    for (const auto& s : sensitivities) {
        if (s.at_phase_boundary && s.return_mult > 1.0) {
            std::cout << std::left << std::setw(18) << s.label
                      << std::right << std::fixed
                      << std::setw(7) << std::setprecision(2) << s.return_mult << "x"
                      << std::setw(7) << std::setprecision(1) << s.max_dd_pct << "%"
                      << std::setw(10) << std::setprecision(2) << s.neighbor_return_variance << "x"
                      << std::setw(9) << std::setprecision(1) << s.neighbor_dd_variance << "%"
                      << std::endl;
            boundary_count++;
            if (boundary_count >= 15) break;
        }
    }
}

// ============================================================================
// Main
// ============================================================================
int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "  EDGE OF CHAOS INVESTIGATION" << std::endl;
    std::cout << "  Finding the Goldilocks zone: stable but adaptive" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;
    std::cout << "Metrics collected:" << std::endl;
    std::cout << "  - Position count volatility (stability of grid size)" << std::endl;
    std::cout << "  - Equity velocity variance (smoothness of returns)" << std::endl;
    std::cout << "  - Spacing change frequency (adaptiveness)" << std::endl;
    std::cout << "  - Parameter sensitivity (phase transition detection)" << std::endl;
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
    std::cout << "(9 survive x 8 spacing x 4 lookback = " << 9*8*4 << " configs)" << std::endl;
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

    // ========================================================================
    // ANALYSIS 1: Sort by Sharpe proxy (best risk-adjusted performance)
    // ========================================================================
    std::sort(g_results.begin(), g_results.end(), [](const ChaosResult& a, const ChaosResult& b) {
        if (a.stopped_out != b.stopped_out) return !a.stopped_out;
        return a.sharpe_proxy > b.sharpe_proxy;
    });

    std::cout << "================================================================" << std::endl;
    std::cout << "  TOP 30 BY SHARPE PROXY (sorted by risk-adjusted return)" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::left << std::setw(18) << "Config"
              << std::right << std::setw(8) << "Return"
              << std::setw(7) << "MaxDD"
              << std::setw(8) << "Sharpe"
              << std::setw(7) << "MaxPos"
              << std::setw(7) << "PosStd"
              << std::setw(8) << "SpChg"
              << std::setw(8) << "EqVStd"
              << std::setw(8) << "Chaos"
              << std::endl;
    std::cout << std::string(85, '-') << std::endl;

    for (int i = 0; i < std::min(30, (int)g_results.size()); i++) {
        const auto& r = g_results[i];
        if (r.stopped_out) continue;
        std::cout << std::left << std::setw(18) << r.label
                  << std::right << std::fixed
                  << std::setw(6) << std::setprecision(2) << r.return_mult << "x"
                  << std::setw(6) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(8) << std::setprecision(2) << r.sharpe_proxy
                  << std::setw(7) << r.max_position_count
                  << std::setw(7) << std::setprecision(1) << r.position_count_std
                  << std::setw(8) << r.spacing_changes
                  << std::setw(8) << std::setprecision(0) << r.equity_velocity_std
                  << std::setw(8) << std::setprecision(2) << r.chaos_score
                  << std::endl;
    }

    // ========================================================================
    // ANALYSIS 2: Sort by Stability Score (low chaos, still profitable)
    // ========================================================================
    std::vector<ChaosResult> stable_and_profitable;
    for (const auto& r : g_results) {
        if (!r.stopped_out && r.return_mult > 2.0) {  // At least 2x return
            stable_and_profitable.push_back(r);
        }
    }

    std::sort(stable_and_profitable.begin(), stable_and_profitable.end(),
        [](const ChaosResult& a, const ChaosResult& b) {
            return a.stability_score > b.stability_score;
        });

    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  GOLDILOCKS ZONE: Stable AND Profitable (return > 2x)" << std::endl;
    std::cout << "  Sorted by stability (low chaos)" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::left << std::setw(18) << "Config"
              << std::right << std::setw(8) << "Return"
              << std::setw(7) << "MaxDD"
              << std::setw(8) << "Sharpe"
              << std::setw(9) << "Stability"
              << std::setw(7) << "PosStd"
              << std::setw(8) << "SpChg"
              << std::setw(7) << "Chaos"
              << std::endl;
    std::cout << std::string(75, '-') << std::endl;

    for (size_t i = 0; i < std::min((size_t)25, stable_and_profitable.size()); i++) {
        const auto& r = stable_and_profitable[i];
        std::cout << std::left << std::setw(18) << r.label
                  << std::right << std::fixed
                  << std::setw(6) << std::setprecision(2) << r.return_mult << "x"
                  << std::setw(6) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(8) << std::setprecision(2) << r.sharpe_proxy
                  << std::setw(9) << std::setprecision(2) << r.stability_score
                  << std::setw(7) << std::setprecision(1) << r.position_count_std
                  << std::setw(8) << r.spacing_changes
                  << std::setw(7) << std::setprecision(2) << r.chaos_score
                  << std::endl;
    }

    // ========================================================================
    // ANALYSIS 3: Chaos metrics by survive_pct (phase transition?)
    // ========================================================================
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  CHAOS METRICS BY SURVIVE_PCT (Phase Transition Detection)" << std::endl;
    std::cout << "================================================================" << std::endl;

    std::map<int, std::vector<const ChaosResult*>> by_survive;
    for (const auto& r : g_results) {
        by_survive[(int)r.survive_pct].push_back(&r);
    }

    std::cout << std::left << std::setw(10) << "Survive"
              << std::right << std::setw(8) << "Configs"
              << std::setw(10) << "Survived"
              << std::setw(10) << "AvgRet"
              << std::setw(9) << "AvgDD"
              << std::setw(10) << "AvgChaos"
              << std::setw(12) << "AvgPosStd"
              << std::setw(10) << "AvgSpChg"
              << std::endl;
    std::cout << std::string(80, '-') << std::endl;

    for (auto& [survive, configs] : by_survive) {
        double avg_ret = 0, avg_dd = 0, avg_chaos = 0, avg_pos_std = 0, avg_sp_chg = 0;
        int survived = 0;
        for (const auto* r : configs) {
            if (!r->stopped_out) {
                survived++;
                avg_ret += r->return_mult;
                avg_dd += r->max_dd_pct;
                avg_chaos += r->chaos_score;
                avg_pos_std += r->position_count_std;
                avg_sp_chg += r->spacing_changes;
            }
        }
        if (survived > 0) {
            avg_ret /= survived;
            avg_dd /= survived;
            avg_chaos /= survived;
            avg_pos_std /= survived;
            avg_sp_chg /= survived;
        }

        std::cout << std::left << std::setw(10) << (std::to_string(survive) + "%")
                  << std::right << std::fixed
                  << std::setw(8) << configs.size()
                  << std::setw(10) << survived
                  << std::setw(8) << std::setprecision(2) << avg_ret << "x"
                  << std::setw(8) << std::setprecision(1) << avg_dd << "%"
                  << std::setw(10) << std::setprecision(2) << avg_chaos
                  << std::setw(12) << std::setprecision(1) << avg_pos_std
                  << std::setw(10) << std::setprecision(0) << avg_sp_chg
                  << std::endl;
    }

    // ========================================================================
    // ANALYSIS 4: Chaos metrics by spacing (how does spacing affect stability?)
    // ========================================================================
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  CHAOS METRICS BY SPACING (Rigidity vs Chaos)" << std::endl;
    std::cout << "================================================================" << std::endl;

    std::map<double, std::vector<const ChaosResult*>> by_spacing;
    for (const auto& r : g_results) {
        by_spacing[r.base_spacing].push_back(&r);
    }

    std::cout << std::left << std::setw(10) << "Spacing"
              << std::right << std::setw(10) << "Survived"
              << std::setw(10) << "AvgRet"
              << std::setw(9) << "AvgDD"
              << std::setw(10) << "AvgChaos"
              << std::setw(12) << "AvgPosStd"
              << std::setw(12) << "AvgMaxPos"
              << std::endl;
    std::cout << std::string(75, '-') << std::endl;

    for (auto& [spacing, configs] : by_spacing) {
        double avg_ret = 0, avg_dd = 0, avg_chaos = 0, avg_pos_std = 0, avg_max_pos = 0;
        int survived = 0;
        for (const auto* r : configs) {
            if (!r->stopped_out) {
                survived++;
                avg_ret += r->return_mult;
                avg_dd += r->max_dd_pct;
                avg_chaos += r->chaos_score;
                avg_pos_std += r->position_count_std;
                avg_max_pos += r->max_position_count;
            }
        }
        if (survived > 0) {
            avg_ret /= survived;
            avg_dd /= survived;
            avg_chaos /= survived;
            avg_pos_std /= survived;
            avg_max_pos /= survived;
        }

        std::cout << std::left << std::setw(10) << ("$" + std::to_string((int)(spacing*10)/10) + "." + std::to_string((int)(spacing*10)%10))
                  << std::right << std::fixed
                  << std::setw(10) << survived
                  << std::setw(8) << std::setprecision(2) << avg_ret << "x"
                  << std::setw(8) << std::setprecision(1) << avg_dd << "%"
                  << std::setw(10) << std::setprecision(2) << avg_chaos
                  << std::setw(12) << std::setprecision(1) << avg_pos_std
                  << std::setw(12) << std::setprecision(0) << avg_max_pos
                  << std::endl;
    }

    // ========================================================================
    // ANALYSIS 5: Stopped out configs (the "chaotic regime")
    // ========================================================================
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  STOPPED OUT CONFIGS (The Chaotic Regime - Too Aggressive)" << std::endl;
    std::cout << "================================================================" << std::endl;

    int stopped_count = 0;
    std::map<int, int> stopped_by_survive;
    std::map<double, int> stopped_by_spacing;

    for (const auto& r : g_results) {
        if (r.stopped_out) {
            stopped_count++;
            stopped_by_survive[(int)r.survive_pct]++;
            stopped_by_spacing[r.base_spacing]++;
        }
    }

    std::cout << "Total stopped out: " << stopped_count << " / " << g_results.size()
              << " (" << std::fixed << std::setprecision(1)
              << (100.0 * stopped_count / g_results.size()) << "%)" << std::endl;

    std::cout << std::endl << "By survive_pct:" << std::endl;
    for (auto& [survive, count] : stopped_by_survive) {
        std::cout << "  " << survive << "%: " << count << " stopped" << std::endl;
    }

    std::cout << std::endl << "By spacing:" << std::endl;
    for (auto& [spacing, count] : stopped_by_spacing) {
        std::cout << "  $" << std::fixed << std::setprecision(1) << spacing << ": " << count << " stopped" << std::endl;
    }

    // ========================================================================
    // ANALYSIS 6: Parameter sensitivity (phase transition detection)
    // ========================================================================
    AnalyzeParameterSensitivity(g_results);

    // ========================================================================
    // ANALYSIS 7: Edge of Chaos Sweet Spot
    // ========================================================================
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  EDGE OF CHAOS SWEET SPOT" << std::endl;
    std::cout << "  Configs that are: adaptive (spacing_changes > 1000)," << std::endl;
    std::cout << "                    stable (position_std < 5)," << std::endl;
    std::cout << "                    and profitable (return > 4x)" << std::endl;
    std::cout << "================================================================" << std::endl;

    std::vector<ChaosResult> sweet_spot;
    for (const auto& r : g_results) {
        if (!r.stopped_out &&
            r.spacing_changes > 1000 &&     // Adaptive
            r.position_count_std < 5.0 &&   // Stable
            r.return_mult > 4.0) {          // Profitable
            sweet_spot.push_back(r);
        }
    }

    std::sort(sweet_spot.begin(), sweet_spot.end(), [](const ChaosResult& a, const ChaosResult& b) {
        return a.sharpe_proxy > b.sharpe_proxy;
    });

    if (sweet_spot.empty()) {
        std::cout << "No configs found in sweet spot. Relaxing criteria..." << std::endl;
        for (const auto& r : g_results) {
            if (!r.stopped_out &&
                r.spacing_changes > 500 &&
                r.position_count_std < 8.0 &&
                r.return_mult > 3.0) {
                sweet_spot.push_back(r);
            }
        }
        std::sort(sweet_spot.begin(), sweet_spot.end(), [](const ChaosResult& a, const ChaosResult& b) {
            return a.sharpe_proxy > b.sharpe_proxy;
        });
    }

    std::cout << std::left << std::setw(18) << "Config"
              << std::right << std::setw(8) << "Return"
              << std::setw(7) << "MaxDD"
              << std::setw(8) << "Sharpe"
              << std::setw(7) << "PosStd"
              << std::setw(8) << "SpChg"
              << std::setw(8) << "Chaos"
              << std::endl;
    std::cout << std::string(65, '-') << std::endl;

    for (size_t i = 0; i < std::min((size_t)15, sweet_spot.size()); i++) {
        const auto& r = sweet_spot[i];
        std::cout << std::left << std::setw(18) << r.label
                  << std::right << std::fixed
                  << std::setw(6) << std::setprecision(2) << r.return_mult << "x"
                  << std::setw(6) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(8) << std::setprecision(2) << r.sharpe_proxy
                  << std::setw(7) << std::setprecision(1) << r.position_count_std
                  << std::setw(8) << r.spacing_changes
                  << std::setw(8) << std::setprecision(2) << r.chaos_score
                  << std::endl;
    }

    if (sweet_spot.empty()) {
        std::cout << "  No configs found even with relaxed criteria." << std::endl;
    }

    // ========================================================================
    // SUMMARY
    // ========================================================================
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  SUMMARY: KEY FINDINGS" << std::endl;
    std::cout << "================================================================" << std::endl;

    // Find best overall
    const ChaosResult* best_sharpe = nullptr;
    const ChaosResult* most_stable = nullptr;
    const ChaosResult* best_balance = nullptr;

    for (const auto& r : g_results) {
        if (r.stopped_out) continue;
        if (!best_sharpe || r.sharpe_proxy > best_sharpe->sharpe_proxy) {
            best_sharpe = &r;
        }
        if (r.return_mult > 2.0 && (!most_stable || r.stability_score > most_stable->stability_score)) {
            most_stable = &r;
        }
        // Balance: high sharpe + high stability
        double balance = r.sharpe_proxy * r.stability_score;
        if (!best_balance || (r.return_mult > 3.0 && balance > best_balance->sharpe_proxy * best_balance->stability_score)) {
            best_balance = &r;
        }
    }

    std::cout << std::endl << "1. BEST RISK-ADJUSTED RETURN:" << std::endl;
    if (best_sharpe) {
        std::cout << "   " << best_sharpe->label << ": " << std::fixed << std::setprecision(2)
                  << best_sharpe->return_mult << "x return, " << best_sharpe->max_dd_pct << "% DD, "
                  << "Sharpe=" << best_sharpe->sharpe_proxy << std::endl;
        std::cout << "   Chaos score: " << best_sharpe->chaos_score
                  << " (position std=" << best_sharpe->position_count_std
                  << ", spacing changes=" << best_sharpe->spacing_changes << ")" << std::endl;
    }

    std::cout << std::endl << "2. MOST STABLE (return > 2x):" << std::endl;
    if (most_stable) {
        std::cout << "   " << most_stable->label << ": " << std::fixed << std::setprecision(2)
                  << most_stable->return_mult << "x return, " << most_stable->max_dd_pct << "% DD" << std::endl;
        std::cout << "   Stability score: " << most_stable->stability_score
                  << " (position std=" << most_stable->position_count_std
                  << ", chaos=" << most_stable->chaos_score << ")" << std::endl;
    }

    std::cout << std::endl << "3. BEST BALANCE (Sharpe x Stability, return > 3x):" << std::endl;
    if (best_balance) {
        std::cout << "   " << best_balance->label << ": " << std::fixed << std::setprecision(2)
                  << best_balance->return_mult << "x return, " << best_balance->max_dd_pct << "% DD" << std::endl;
        std::cout << "   Sharpe=" << best_balance->sharpe_proxy
                  << ", Stability=" << best_balance->stability_score << std::endl;
    }

    std::cout << std::endl << "4. PHASE TRANSITION ANALYSIS:" << std::endl;
    std::cout << "   survive_pct < 12%: " << stopped_by_survive[10] + stopped_by_survive[11]
              << " stopped out (CHAOTIC REGIME)" << std::endl;
    std::cout << "   survive_pct >= 13%: Most configs survive (STABLE REGIME)" << std::endl;
    std::cout << "   The phase boundary is at survive_pct = 12-13%" << std::endl;

    std::cout << std::endl;
    std::cout << "Total configurations tested: " << g_results.size() << std::endl;
    std::cout << "Execution time: " << duration.count() << "s (" << num_threads << " threads)" << std::endl;

    return 0;
}
