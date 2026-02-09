#include "../include/fill_up_strategy.h"
#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <fstream>
#include <sstream>
#include <chrono>

using namespace backtest;

// ============================================================================
// Shared tick data - loaded ONCE, used by ALL threads
// ============================================================================
std::vector<Tick> g_shared_ticks;

void LoadTickDataOnce(const std::string& path) {
    std::cout << "Loading XAGUSD tick data into memory (one-time)..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open tick file: " + path);
    }

    std::string line;
    std::getline(file, line);  // Skip header

    g_shared_ticks.reserve(30000000);  // ~29M ticks for XAGUSD

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
struct SweepTask {
    double survive_pct;
    double spacing;
    std::string label;
};

struct SweepResult {
    std::string label;
    double survive_pct;
    double spacing;
    double final_balance;
    double return_mult;
    double max_dd_pct;
    int total_trades;
    double total_swap;
    bool stopped_out;
    int max_positions;
    double max_trade_size;
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

// Global state
std::atomic<int> g_completed{0};
std::mutex g_results_mutex;
std::mutex g_output_mutex;
std::vector<SweepResult> g_results;

// ============================================================================
// Run single backtest with shared tick data
// ============================================================================
SweepResult run_test(const SweepTask& task, const std::vector<Tick>& ticks) {
    SweepResult r;
    r.label = task.label;
    r.survive_pct = task.survive_pct;
    r.spacing = task.spacing;
    r.final_balance = 0;
    r.return_mult = 0;
    r.max_dd_pct = 0;
    r.total_trades = 0;
    r.total_swap = 0;
    r.stopped_out = false;
    r.max_positions = 0;
    r.max_trade_size = 0;

    // XAGUSD engine config
    TickBacktestConfig cfg;
    cfg.symbol = "XAGUSD";
    cfg.initial_balance = 10000.0;
    cfg.account_currency = "USD";
    cfg.contract_size = 5000.0;     // Silver contract size
    cfg.leverage = 500.0;
    cfg.margin_rate = 1.0;
    cfg.pip_size = 0.001;           // Silver SYMBOL_POINT (3 decimal places)
    cfg.swap_long = -25.44;         // Points per lot per day
    cfg.swap_short = 13.72;
    cfg.swap_mode = 1;              // SYMBOL_SWAP_MODE_POINTS
    cfg.swap_3days = 3;             // Triple swap Thursday
    cfg.start_date = "2025.01.01";
    cfg.end_date = "2025.12.30";
    cfg.verbose = false;

    // Empty tick config - we feed ticks manually
    TickDataConfig tc;
    tc.file_path = "";
    tc.load_all_into_memory = false;
    cfg.tick_data_config = tc;

    try {
        TickBasedEngine engine(cfg);

        FillUpStrategy strategy(
            task.survive_pct,   // survive_pct
            1.0,                // size_multiplier
            task.spacing,       // spacing_dollars
            0.01,               // min_volume
            10.0,               // max_volume
            5000.0,             // contract_size (XAGUSD)
            500.0,              // leverage
            3,                  // symbol_digits
            1.0,                // margin_rate
            false,              // enable_dd_protection
            50.0                // dd_threshold_pct (unused)
        );

        engine.RunWithTicks(ticks, [&strategy](const Tick& t, TickBasedEngine& e) {
            strategy.OnTick(t, e);
        });

        auto res = engine.GetResults();
        r.final_balance = res.final_balance;
        r.return_mult = res.final_balance / 10000.0;
        r.total_trades = res.total_trades;
        r.total_swap = res.total_swap_charged;
        r.max_positions = strategy.GetMaxNumberOfOpen();
        r.max_trade_size = strategy.GetMaxTradeSize();

        // Calculate max DD%
        double peak = res.final_balance + res.max_drawdown;
        r.max_dd_pct = (peak > 0) ? (res.max_drawdown / peak * 100.0) : 0;

        if (r.max_dd_pct > 95.0 || r.return_mult < 0.05) {
            r.stopped_out = true;
        }
    } catch (const std::exception& e) {
        r.stopped_out = true;
    } catch (...) {
        r.stopped_out = true;
    }

    return r;
}

void worker(WorkQueue& queue, int total, const std::vector<Tick>& ticks) {
    SweepTask task;
    while (queue.pop(task)) {
        SweepResult r = run_test(task, ticks);

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

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "  XAGUSD FillUpStrategy SPACING SWEEP" << std::endl;
    std::cout << "  Strategy: Base FillUpStrategy (fixed spacing, no adaptive)" << std::endl;
    std::cout << "  survive_pct: 18%, 19%, 20%" << std::endl;
    std::cout << "  Contract: 5000, Leverage: 500" << std::endl;
    std::cout << "  Swap Long: -25.44 pts, Swap Short: +13.72 pts" << std::endl;
    std::cout << "  Data: XAGUSD 2025 (full year)" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;

    // Load tick data once
    std::string tick_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\XAGUSD\\XAGUSD_TICKS_2025.csv";
    LoadTickDataOnce(tick_path);

    // Generate sweep configurations
    // Fine granularity in MT5 optimal range ($0.90-$2.00)
    std::vector<double> spacings = {
        0.05, 0.10, 0.15, 0.20, 0.30, 0.50, 0.75,
        0.90, 1.00, 1.10, 1.20, 1.30, 1.40, 1.50,
        1.60, 1.70, 1.80, 1.90, 2.00, 2.50, 3.00
    };
    std::vector<double> survive_values = {18.0, 19.0, 20.0};

    std::vector<SweepTask> tasks;
    for (double survive : survive_values) {
        for (double spacing : spacings) {
            SweepTask task;
            task.survive_pct = survive;
            task.spacing = spacing;
            std::ostringstream oss;
            oss << "S" << (int)survive << "_SP" << std::fixed << std::setprecision(2) << spacing;
            task.label = oss.str();
            tasks.push_back(task);
        }
    }

    int total = tasks.size();
    std::cout << "Testing " << total << " configurations..." << std::endl;

    // Set up work queue and threads
    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;
    std::cout << "Using " << num_threads << " worker threads" << std::endl << std::endl;

    WorkQueue queue;
    for (const auto& task : tasks) {
        queue.push(task);
    }
    queue.finish();

    auto sweep_start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < num_threads; i++) {
        threads.emplace_back(worker, std::ref(queue), total, std::cref(g_shared_ticks));
    }
    for (auto& t : threads) {
        t.join();
    }

    auto sweep_end = std::chrono::high_resolution_clock::now();
    auto sweep_dur = std::chrono::duration_cast<std::chrono::seconds>(sweep_end - sweep_start);

    std::cout << std::endl << std::endl;
    std::cout << "Completed in " << sweep_dur.count() << " seconds ("
              << std::fixed << std::setprecision(1) << (double)sweep_dur.count() / total
              << "s per config, " << num_threads << " threads)" << std::endl;

    // Sort by return (highest first)
    std::sort(g_results.begin(), g_results.end(), [](const SweepResult& a, const SweepResult& b) {
        return a.return_mult > b.return_mult;
    });

    // Print results
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  RESULTS (sorted by return)" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::left << std::setw(18) << "Label"
              << std::right << std::setw(8) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(8) << "Trades"
              << std::setw(10) << "Swap$"
              << std::setw(8) << "MaxPos"
              << std::setw(10) << "MaxLot"
              << std::setw(10) << "Status"
              << std::endl;
    std::cout << std::string(80, '-') << std::endl;

    for (const auto& r : g_results) {
        std::cout << std::left << std::setw(18) << r.label
                  << std::right << std::fixed
                  << std::setw(7) << std::setprecision(2) << r.return_mult << "x"
                  << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(8) << r.total_trades
                  << std::setw(10) << std::setprecision(0) << r.total_swap
                  << std::setw(8) << r.max_positions
                  << std::setw(10) << std::setprecision(2) << r.max_trade_size
                  << std::setw(10) << (r.stopped_out ? "STOPPED" : "OK")
                  << std::endl;
    }

    // Summary by survive_pct
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  SURVIVE=18% CONFIGS" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::left << std::setw(10) << "Spacing"
              << std::right << std::setw(8) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(8) << "Trades"
              << std::setw(10) << "Swap$"
              << std::setw(10) << "Status"
              << std::endl;
    std::cout << std::string(54, '-') << std::endl;

    std::vector<SweepResult> s18, s19, s20;
    for (const auto& r : g_results) {
        if (r.survive_pct == 18.0) s18.push_back(r);
        else if (r.survive_pct == 19.0) s19.push_back(r);
        else s20.push_back(r);
    }
    std::sort(s18.begin(), s18.end(), [](const SweepResult& a, const SweepResult& b) {
        return a.spacing < b.spacing;
    });
    std::sort(s19.begin(), s19.end(), [](const SweepResult& a, const SweepResult& b) {
        return a.spacing < b.spacing;
    });
    std::sort(s20.begin(), s20.end(), [](const SweepResult& a, const SweepResult& b) {
        return a.spacing < b.spacing;
    });

    for (const auto& r : s18) {
        std::cout << "$" << std::left << std::setw(9) << std::fixed << std::setprecision(2) << r.spacing
                  << std::right
                  << std::setw(7) << std::setprecision(2) << r.return_mult << "x"
                  << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(8) << r.total_trades
                  << std::setw(10) << std::setprecision(0) << r.total_swap
                  << std::setw(10) << (r.stopped_out ? "STOPPED" : "OK")
                  << std::endl;
    }

    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  SURVIVE=19% CONFIGS" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::left << std::setw(10) << "Spacing"
              << std::right << std::setw(8) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(8) << "Trades"
              << std::setw(10) << "Swap$"
              << std::setw(10) << "Status"
              << std::endl;
    std::cout << std::string(54, '-') << std::endl;

    for (const auto& r : s19) {
        std::cout << "$" << std::left << std::setw(9) << std::fixed << std::setprecision(2) << r.spacing
                  << std::right
                  << std::setw(7) << std::setprecision(2) << r.return_mult << "x"
                  << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(8) << r.total_trades
                  << std::setw(10) << std::setprecision(0) << r.total_swap
                  << std::setw(10) << (r.stopped_out ? "STOPPED" : "OK")
                  << std::endl;
    }

    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  SURVIVE=20% CONFIGS" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::left << std::setw(10) << "Spacing"
              << std::right << std::setw(8) << "Return"
              << std::setw(8) << "MaxDD%"
              << std::setw(8) << "Trades"
              << std::setw(10) << "Swap$"
              << std::setw(10) << "Status"
              << std::endl;
    std::cout << std::string(54, '-') << std::endl;

    for (const auto& r : s20) {
        std::cout << "$" << std::left << std::setw(9) << std::fixed << std::setprecision(2) << r.spacing
                  << std::right
                  << std::setw(7) << std::setprecision(2) << r.return_mult << "x"
                  << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(8) << r.total_trades
                  << std::setw(10) << std::setprecision(0) << r.total_swap
                  << std::setw(10) << (r.stopped_out ? "STOPPED" : "OK")
                  << std::endl;
    }

    // Best configs summary
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  BEST CONFIGS (survived, sorted by return)" << std::endl;
    std::cout << "================================================================" << std::endl;

    int count = 0;
    for (const auto& r : g_results) {
        if (!r.stopped_out && count < 10) {
            std::cout << r.label << ": " << std::fixed << std::setprecision(2) << r.return_mult
                      << "x return, " << std::setprecision(1) << r.max_dd_pct << "% DD, "
                      << r.total_trades << " trades, $" << std::setprecision(0) << r.total_swap << " swap"
                      << std::endl;
            count++;
        }
    }
    if (count == 0) {
        std::cout << "  NO CONFIGURATIONS SURVIVED!" << std::endl;
    }

    return 0;
}
