/**
 * Van der Pol Oscillator Phase-Based Trading - Parallel Parameter Sweep
 *
 * Tests the hypothesis that modeling price as an oscillator with phase tracking
 * can improve entry/exit timing compared to simple grid strategies.
 *
 * Key questions:
 *   1. Can phase-based entry timing improve over grid entry?
 *   2. Does phase-based exit beat fixed TP?
 *   3. What phase angles actually correspond to price troughs/peaks?
 *   4. Is the oscillator model a good fit for gold price dynamics?
 *
 * Loads tick data ONCE into shared memory, then tests multiple configurations
 * in parallel using std::thread + WorkQueue.
 */

#include "../include/strategy_vanderpol.h"
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
std::vector<Tick> g_shared_ticks_2025;
std::vector<Tick> g_shared_ticks_2024;

void LoadTickData(const std::string& path, std::vector<Tick>& ticks, const std::string& label) {
    std::cout << "Loading " << label << " tick data..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cout << "WARNING: Cannot open tick file: " << path << std::endl;
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
              << duration.count() << " seconds" << std::endl;
}

// ============================================================================
// Task and Result structures
// ============================================================================
struct VDPTask {
    // Van der Pol parameters
    int ma_period;
    int velocity_smoothing;
    double entry_phase_center;
    double entry_phase_width;
    double exit_phase_center;
    int amplitude_lookback;
    double survive_pct;
    bool use_phase_exit;

    // Which year to test
    int year;

    // Task label
    std::string label;

    // Is this a baseline test?
    bool is_baseline;
};

struct VDPResult {
    std::string label;
    int year;
    double return_mult;
    double max_dd_pct;
    int total_trades;
    double total_swap;
    int entries;
    int phase_exits;
    double sharpe_proxy;
    bool is_baseline;

    // Phase analysis
    double typical_amplitude;
    double typical_velocity;
};

// ============================================================================
// Work Queue for thread pool
// ============================================================================
class WorkQueue {
    std::queue<VDPTask> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool done_ = false;

public:
    void push(const VDPTask& task) {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push(task);
        cv_.notify_one();
    }

    bool pop(VDPTask& task) {
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
std::vector<VDPResult> g_results;

// ============================================================================
// Run single Van der Pol test with shared tick data
// ============================================================================
VDPResult run_vdp_test(const VDPTask& task, const std::vector<Tick>& ticks) {
    VDPResult r;
    r.label = task.label;
    r.year = task.year;
    r.return_mult = 0;
    r.max_dd_pct = 0;
    r.total_trades = 0;
    r.total_swap = 0;
    r.entries = 0;
    r.phase_exits = 0;
    r.sharpe_proxy = 0;
    r.is_baseline = task.is_baseline;
    r.typical_amplitude = 0;
    r.typical_velocity = 0;

    if (ticks.empty()) {
        r.max_dd_pct = 100.0;
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
    cfg.start_date = (task.year == 2025) ? "2025.01.01" : "2024.01.01";
    cfg.end_date = (task.year == 2025) ? "2025.12.30" : "2024.12.31";
    cfg.verbose = false;

    TickDataConfig tc;
    tc.file_path = "";
    tc.load_all_into_memory = false;
    cfg.tick_data_config = tc;

    try {
        TickBasedEngine engine(cfg);

        if (task.is_baseline) {
            // Run baseline FillUpOscillation
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
            r.sharpe_proxy = (r.max_dd_pct > 0) ? (r.return_mult - 1.0) / (r.max_dd_pct / 100.0) : 0;
        } else {
            // Run Van der Pol strategy
            StrategyVanDerPol::Config vcfg;
            vcfg.ma_period = task.ma_period;
            vcfg.velocity_smoothing = task.velocity_smoothing;
            vcfg.entry_phase_center = task.entry_phase_center;
            vcfg.entry_phase_width = task.entry_phase_width;
            vcfg.exit_phase_center = task.exit_phase_center;
            vcfg.exit_phase_width = 60.0;
            vcfg.amplitude_lookback = task.amplitude_lookback;
            vcfg.survive_pct = task.survive_pct;
            vcfg.use_phase_exit = task.use_phase_exit;
            vcfg.lot_size = 0.02;
            vcfg.max_lots = 0.50;
            // contract_size/leverage now read from engine.GetConfig()
            vcfg.cooldown_ticks = 200;
            vcfg.max_positions = 20;
            vcfg.warmup_ticks = 2000;
            vcfg.fallback_tp = 1.50;

            StrategyVanDerPol strategy(vcfg);

            engine.RunWithTicks(ticks, [&strategy](const Tick& t, TickBasedEngine& e) {
                strategy.OnTick(t, e);
            });

            auto res = engine.GetResults();
            r.return_mult = res.final_balance / 10000.0;
            r.max_dd_pct = strategy.GetMaxDDPct();
            r.total_trades = res.total_trades;
            r.total_swap = res.total_swap_charged;
            r.entries = strategy.GetEntries();
            r.phase_exits = strategy.GetPhaseExits();
            r.typical_amplitude = strategy.GetTypicalAmplitude();
            r.typical_velocity = strategy.GetTypicalVelocity();
            r.sharpe_proxy = (r.max_dd_pct > 0) ? (r.return_mult - 1.0) / (r.max_dd_pct / 100.0) : 0;
        }
    } catch (const std::exception& e) {
        r.max_dd_pct = 100.0;
    }

    return r;
}

void worker(WorkQueue& queue, int total) {
    VDPTask task;
    while (queue.pop(task)) {
        const std::vector<Tick>& ticks = (task.year == 2025) ? g_shared_ticks_2025 : g_shared_ticks_2024;
        VDPResult r = run_vdp_test(task, ticks);

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
// Generate all parameter combinations to test
// ============================================================================
std::vector<VDPTask> GenerateTasks() {
    std::vector<VDPTask> tasks;

    // Focused parameter ranges for faster initial testing
    std::vector<int> ma_periods = {200, 500, 1000};
    std::vector<int> velocity_smoothings = {20, 50, 100};
    std::vector<double> entry_phases = {240.0, 270.0, 300.0};
    std::vector<double> entry_widths = {45.0, 90.0};
    std::vector<double> exit_phases = {60.0, 90.0, 120.0};
    std::vector<int> amplitude_lookbacks = {500};
    std::vector<double> survive_pcts = {13.0};
    std::vector<int> years = {2024, 2025};

    // 1. BASELINE tests (FillUpOscillation ADAPTIVE_SPACING)
    for (int year : years) {
        VDPTask t;
        t.is_baseline = true;
        t.year = year;
        t.label = "BASELINE_" + std::to_string(year);
        tasks.push_back(t);
    }

    // 2. Van der Pol with phase exit - core sweep
    for (int year : years) {
        for (int ma : ma_periods) {
            for (int vs : velocity_smoothings) {
                for (double ep : entry_phases) {
                    for (double ew : entry_widths) {
                        for (double xp : exit_phases) {
                            for (int al : amplitude_lookbacks) {
                                for (double sp : survive_pcts) {
                                    VDPTask t;
                                    t.is_baseline = false;
                                    t.year = year;
                                    t.ma_period = ma;
                                    t.velocity_smoothing = vs;
                                    t.entry_phase_center = ep;
                                    t.entry_phase_width = ew;
                                    t.exit_phase_center = xp;
                                    t.amplitude_lookback = al;
                                    t.survive_pct = sp;
                                    t.use_phase_exit = true;

                                    std::ostringstream oss;
                                    oss << "VDP_ma" << ma << "_vs" << vs
                                        << "_ep" << (int)ep << "_ew" << (int)ew
                                        << "_xp" << (int)xp << "_al" << al
                                        << "_s" << (int)sp << "_phx_" << year;
                                    t.label = oss.str();
                                    tasks.push_back(t);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // 3. Van der Pol with fixed TP (no phase exit) - for comparison
    for (int year : years) {
        for (int ma : ma_periods) {
            for (int vs : {50}) {
                for (double ep : entry_phases) {
                    for (double ew : {45.0}) {
                        for (int al : {500}) {
                            for (double sp : {13.0}) {
                                VDPTask t;
                                t.is_baseline = false;
                                t.year = year;
                                t.ma_period = ma;
                                t.velocity_smoothing = vs;
                                t.entry_phase_center = ep;
                                t.entry_phase_width = ew;
                                t.exit_phase_center = 90.0;
                                t.amplitude_lookback = al;
                                t.survive_pct = sp;
                                t.use_phase_exit = false;  // Fixed TP only

                                std::ostringstream oss;
                                oss << "VDP_ma" << ma << "_vs" << vs
                                    << "_ep" << (int)ep << "_ew" << (int)ew
                                    << "_fixedTP_al" << al
                                    << "_s" << (int)sp << "_" << year;
                                t.label = oss.str();
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
    std::cout << "  VAN DER POL OSCILLATOR PHASE-BASED TRADING" << std::endl;
    std::cout << "  Parallel Parameter Sweep" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;

    // Step 1: Load tick data
    std::string path_2025 = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    std::string path_2024 = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAUUSD_TICKS_2024.csv";

    LoadTickData(path_2025, g_shared_ticks_2025, "2025");
    LoadTickData(path_2024, g_shared_ticks_2024, "2024");

    std::cout << std::endl;

    // Step 2: Generate tasks
    auto tasks = GenerateTasks();
    int total = (int)tasks.size();
    std::cout << "Generated " << total << " test configurations" << std::endl << std::endl;

    // Step 3: Create work queue and push tasks
    WorkQueue queue;
    for (const auto& t : tasks) {
        queue.push(t);
    }

    // Step 4: Launch worker threads
    auto start = std::chrono::high_resolution_clock::now();
    unsigned int num_threads = std::thread::hardware_concurrency();
    std::cout << "Running on " << num_threads << " threads..." << std::endl;

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < num_threads; i++) {
        threads.emplace_back(worker, std::ref(queue), total);
    }

    // Signal completion and wait
    queue.finish();
    for (auto& t : threads) {
        t.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

    std::cout << std::endl << std::endl;
    std::cout << "Completed " << total << " tests in " << duration.count() << " seconds" << std::endl;
    std::cout << "Average: " << std::fixed << std::setprecision(2)
              << (duration.count() / (double)total) << " seconds per test" << std::endl;
    std::cout << std::endl;

    // Step 5: Analyze results
    std::cout << "================================================================" << std::endl;
    std::cout << "  RESULTS ANALYSIS" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;

    // Separate baseline and VDP results
    VDPResult baseline_2025, baseline_2024;
    std::vector<VDPResult> vdp_2025, vdp_2024;

    for (const auto& r : g_results) {
        if (r.is_baseline) {
            if (r.year == 2025) baseline_2025 = r;
            else baseline_2024 = r;
        } else {
            if (r.year == 2025) vdp_2025.push_back(r);
            else vdp_2024.push_back(r);
        }
    }

    // Print baselines
    std::cout << "BASELINE (FillUpOscillation ADAPTIVE_SPACING, survive=13%, spacing=$1.50):" << std::endl;
    std::cout << "----------------------------------------------------------------" << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  2025: Return=" << baseline_2025.return_mult << "x, MaxDD="
              << baseline_2025.max_dd_pct << "%, Trades=" << baseline_2025.total_trades
              << ", Sharpe=" << baseline_2025.sharpe_proxy << std::endl;
    std::cout << "  2024: Return=" << baseline_2024.return_mult << "x, MaxDD="
              << baseline_2024.max_dd_pct << "%, Trades=" << baseline_2024.total_trades
              << ", Sharpe=" << baseline_2024.sharpe_proxy << std::endl;
    std::cout << std::endl;

    // Sort VDP results by return
    std::sort(vdp_2025.begin(), vdp_2025.end(), [](const VDPResult& a, const VDPResult& b) {
        return a.return_mult > b.return_mult;
    });
    std::sort(vdp_2024.begin(), vdp_2024.end(), [](const VDPResult& a, const VDPResult& b) {
        return a.return_mult > b.return_mult;
    });

    // Print top 15 for each year
    std::cout << "TOP 15 VAN DER POL CONFIGURATIONS (2025):" << std::endl;
    std::cout << "----------------------------------------------------------------" << std::endl;
    std::cout << std::setw(60) << std::left << "Config"
              << std::setw(10) << "Return"
              << std::setw(10) << "MaxDD%"
              << std::setw(10) << "Entries"
              << std::setw(10) << "PhaseExit"
              << std::setw(10) << "Sharpe" << std::endl;

    int count = 0;
    for (const auto& r : vdp_2025) {
        if (count++ >= 15) break;
        std::cout << std::setw(60) << std::left << r.label
                  << std::setw(10) << std::fixed << std::setprecision(2) << r.return_mult
                  << std::setw(10) << r.max_dd_pct
                  << std::setw(10) << r.entries
                  << std::setw(10) << r.phase_exits
                  << std::setw(10) << r.sharpe_proxy << std::endl;
    }
    std::cout << std::endl;

    std::cout << "TOP 15 VAN DER POL CONFIGURATIONS (2024):" << std::endl;
    std::cout << "----------------------------------------------------------------" << std::endl;
    std::cout << std::setw(60) << std::left << "Config"
              << std::setw(10) << "Return"
              << std::setw(10) << "MaxDD%"
              << std::setw(10) << "Entries"
              << std::setw(10) << "PhaseExit"
              << std::setw(10) << "Sharpe" << std::endl;

    count = 0;
    for (const auto& r : vdp_2024) {
        if (count++ >= 15) break;
        std::cout << std::setw(60) << std::left << r.label
                  << std::setw(10) << std::fixed << std::setprecision(2) << r.return_mult
                  << std::setw(10) << r.max_dd_pct
                  << std::setw(10) << r.entries
                  << std::setw(10) << r.phase_exits
                  << std::setw(10) << r.sharpe_proxy << std::endl;
    }
    std::cout << std::endl;

    // Summary statistics
    std::cout << "SUMMARY STATISTICS:" << std::endl;
    std::cout << "----------------------------------------------------------------" << std::endl;

    // Count how many VDP configs beat baseline
    int beat_baseline_2025 = 0, beat_baseline_2024 = 0;
    double max_return_2025 = 0, max_return_2024 = 0;
    double min_dd_2025 = 100, min_dd_2024 = 100;

    for (const auto& r : vdp_2025) {
        if (r.return_mult > baseline_2025.return_mult) beat_baseline_2025++;
        if (r.return_mult > max_return_2025) max_return_2025 = r.return_mult;
        if (r.max_dd_pct < min_dd_2025 && r.return_mult > 1.0) min_dd_2025 = r.max_dd_pct;
    }
    for (const auto& r : vdp_2024) {
        if (r.return_mult > baseline_2024.return_mult) beat_baseline_2024++;
        if (r.return_mult > max_return_2024) max_return_2024 = r.return_mult;
        if (r.max_dd_pct < min_dd_2024 && r.return_mult > 1.0) min_dd_2024 = r.max_dd_pct;
    }

    std::cout << "2025:" << std::endl;
    std::cout << "  Baseline: " << baseline_2025.return_mult << "x return, "
              << baseline_2025.max_dd_pct << "% DD" << std::endl;
    std::cout << "  VDP configs tested: " << vdp_2025.size() << std::endl;
    std::cout << "  VDP beat baseline: " << beat_baseline_2025
              << " (" << std::fixed << std::setprecision(1)
              << (100.0 * beat_baseline_2025 / vdp_2025.size()) << "%)" << std::endl;
    std::cout << "  VDP max return: " << max_return_2025 << "x" << std::endl;
    std::cout << "  VDP min DD (profitable): " << min_dd_2025 << "%" << std::endl;
    std::cout << std::endl;

    std::cout << "2024:" << std::endl;
    std::cout << "  Baseline: " << baseline_2024.return_mult << "x return, "
              << baseline_2024.max_dd_pct << "% DD" << std::endl;
    std::cout << "  VDP configs tested: " << vdp_2024.size() << std::endl;
    std::cout << "  VDP beat baseline: " << beat_baseline_2024
              << " (" << std::fixed << std::setprecision(1)
              << (100.0 * beat_baseline_2024 / vdp_2024.size()) << "%)" << std::endl;
    std::cout << "  VDP max return: " << max_return_2024 << "x" << std::endl;
    std::cout << "  VDP min DD (profitable): " << min_dd_2024 << "%" << std::endl;
    std::cout << std::endl;

    // Analyze phase exit effectiveness
    std::cout << "PHASE EXIT ANALYSIS:" << std::endl;
    std::cout << "----------------------------------------------------------------" << std::endl;

    // Find configs with phase exit vs fixed TP
    double avg_return_phase = 0, avg_return_fixed = 0;
    int count_phase = 0, count_fixed = 0;

    for (const auto& r : vdp_2025) {
        if (r.label.find("fixedTP") != std::string::npos) {
            avg_return_fixed += r.return_mult;
            count_fixed++;
        } else if (r.label.find("phx") != std::string::npos) {
            avg_return_phase += r.return_mult;
            count_phase++;
        }
    }

    if (count_phase > 0) avg_return_phase /= count_phase;
    if (count_fixed > 0) avg_return_fixed /= count_fixed;

    std::cout << "2025:" << std::endl;
    std::cout << "  Phase Exit configs: " << count_phase
              << ", avg return: " << std::fixed << std::setprecision(2)
              << avg_return_phase << "x" << std::endl;
    std::cout << "  Fixed TP configs: " << count_fixed
              << ", avg return: " << avg_return_fixed << "x" << std::endl;
    std::cout << std::endl;

    // Analyze entry phase effectiveness
    std::cout << "ENTRY PHASE ANALYSIS (2025):" << std::endl;
    std::cout << "----------------------------------------------------------------" << std::endl;

    std::map<int, std::pair<double, int>> entry_phase_returns;
    for (const auto& r : vdp_2025) {
        if (r.label.find("_ep") != std::string::npos) {
            size_t pos = r.label.find("_ep");
            int ep = std::stoi(r.label.substr(pos + 3, 3));
            entry_phase_returns[ep].first += r.return_mult;
            entry_phase_returns[ep].second++;
        }
    }

    for (const auto& [ep, data] : entry_phase_returns) {
        double avg = data.first / data.second;
        std::cout << "  Entry phase " << ep << " deg: " << data.second
                  << " configs, avg return: " << std::fixed << std::setprecision(2)
                  << avg << "x" << std::endl;
    }
    std::cout << std::endl;

    // Analyze MA period effectiveness
    std::cout << "MA PERIOD ANALYSIS (2025):" << std::endl;
    std::cout << "----------------------------------------------------------------" << std::endl;

    std::map<int, std::pair<double, int>> ma_returns;
    for (const auto& r : vdp_2025) {
        if (r.label.find("_ma") != std::string::npos) {
            size_t pos = r.label.find("_ma");
            size_t end_pos = r.label.find("_vs");
            int ma = std::stoi(r.label.substr(pos + 3, end_pos - pos - 3));
            ma_returns[ma].first += r.return_mult;
            ma_returns[ma].second++;
        }
    }

    for (const auto& [ma, data] : ma_returns) {
        double avg = data.first / data.second;
        std::cout << "  MA period " << ma << ": " << data.second
                  << " configs, avg return: " << std::fixed << std::setprecision(2)
                  << avg << "x" << std::endl;
    }
    std::cout << std::endl;

    // Print typical amplitude/velocity for best configs
    std::cout << "PHASE PORTRAIT ANALYSIS (best 2025 configs):" << std::endl;
    std::cout << "----------------------------------------------------------------" << std::endl;

    count = 0;
    for (const auto& r : vdp_2025) {
        if (count++ >= 5) break;
        std::cout << "  " << r.label << ":" << std::endl;
        std::cout << "    Typical amplitude: $" << std::fixed << std::setprecision(2)
                  << r.typical_amplitude << std::endl;
        std::cout << "    Typical velocity: " << std::setprecision(4)
                  << r.typical_velocity << " $/tick" << std::endl;
    }
    std::cout << std::endl;

    // Final conclusion
    std::cout << "================================================================" << std::endl;
    std::cout << "  CONCLUSIONS" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;

    bool vdp_beats_baseline = (beat_baseline_2025 > 0 && beat_baseline_2024 > 0);
    bool consistent_winner = false;

    // Check if any config beats baseline in both years
    for (const auto& r25 : vdp_2025) {
        for (const auto& r24 : vdp_2024) {
            // Same config, different year
            if (r25.label.substr(0, r25.label.length() - 5) ==
                r24.label.substr(0, r24.label.length() - 5)) {
                if (r25.return_mult > baseline_2025.return_mult &&
                    r24.return_mult > baseline_2024.return_mult) {
                    consistent_winner = true;
                    break;
                }
            }
        }
        if (consistent_winner) break;
    }

    std::cout << "1. Phase-based entry timing: ";
    if (beat_baseline_2025 > vdp_2025.size() / 2) {
        std::cout << "YES - majority of configs beat baseline" << std::endl;
    } else if (beat_baseline_2025 > 0) {
        std::cout << "PARTIAL - some configs beat baseline" << std::endl;
    } else {
        std::cout << "NO - no configs beat baseline" << std::endl;
    }

    std::cout << "2. Phase-based exit vs fixed TP: ";
    if (avg_return_phase > avg_return_fixed * 1.1) {
        std::cout << "YES - phase exit outperforms by " << std::fixed << std::setprecision(0)
                  << ((avg_return_phase / avg_return_fixed - 1) * 100) << "%" << std::endl;
    } else if (avg_return_fixed > avg_return_phase * 1.1) {
        std::cout << "NO - fixed TP outperforms phase exit" << std::endl;
    } else {
        std::cout << "NEUTRAL - similar performance" << std::endl;
    }

    std::cout << "3. Consistent across years: ";
    if (consistent_winner) {
        std::cout << "YES - found configs that beat baseline in both years" << std::endl;
    } else {
        std::cout << "NO - no config consistently beats baseline" << std::endl;
    }

    std::cout << "4. Oscillator model fit: ";
    if (max_return_2025 > baseline_2025.return_mult * 1.5 ||
        max_return_2024 > baseline_2024.return_mult * 1.5) {
        std::cout << "GOOD - significant outperformance possible" << std::endl;
    } else if (max_return_2025 > baseline_2025.return_mult ||
               max_return_2024 > baseline_2024.return_mult) {
        std::cout << "MODERATE - some improvement possible" << std::endl;
    } else {
        std::cout << "POOR - no improvement over baseline" << std::endl;
    }

    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;

    return 0;
}
