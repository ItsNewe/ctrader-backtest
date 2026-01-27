/**
 * test_forced_entry_parallel.cpp
 *
 * PARALLEL version of forced entry validation.
 * Loads tick data ONCE into shared memory, then runs multiple configs in parallel.
 *
 * Tests forced entry ON vs OFF with various safety mechanisms
 * across both 2024 and 2025 to validate no overfitting.
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <chrono>

using namespace backtest;

// Shared tick data - loaded ONCE
std::vector<Tick> g_ticks_2024;
std::vector<Tick> g_ticks_2025;

struct TestConfig {
    std::string name;
    bool force_min_volume_entry;
    int max_positions;
    double margin_level_floor;
    std::string year;  // "2024" or "2025"
};

struct TestResult {
    std::string name;
    std::string year;
    double final_balance;
    double return_multiple;
    double max_dd_pct;
    int total_trades;
    long forced_entries;
    long max_pos_blocks;
    int peak_positions;
    bool completed;
};

// Thread-safe work queue
std::mutex g_queue_mutex;
std::mutex g_results_mutex;
std::queue<TestConfig> g_work_queue;
std::vector<TestResult> g_results;
std::atomic<int> g_completed(0);
int g_total_tasks = 0;

void LoadTickData() {
    auto start = std::chrono::high_resolution_clock::now();

    // Load 2024
    std::cout << "Loading 2024 tick data..." << std::endl;
    {
        TickDataConfig cfg;
        cfg.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAUUSD_TICKS_2024.csv";
        cfg.format = TickDataFormat::MT5_CSV;
        TickDataManager mgr(cfg);
        Tick tick;
        while (mgr.GetNextTick(tick)) {
            g_ticks_2024.push_back(tick);
        }
    }
    std::cout << "  Loaded " << g_ticks_2024.size() << " ticks (2024)" << std::endl;

    // Load 2025+Jan2026
    std::cout << "Loading 2025 tick data..." << std::endl;
    {
        std::vector<std::string> files = {
            "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv",
            "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_JAN2026.csv"
        };
        for (const auto& file : files) {
            TickDataConfig cfg;
            cfg.file_path = file;
            cfg.format = TickDataFormat::MT5_CSV;
            TickDataManager mgr(cfg);
            Tick tick;
            while (mgr.GetNextTick(tick)) {
                g_ticks_2025.push_back(tick);
            }
        }
        std::sort(g_ticks_2025.begin(), g_ticks_2025.end(),
                  [](const Tick& a, const Tick& b) { return a.timestamp < b.timestamp; });
    }
    std::cout << "  Loaded " << g_ticks_2025.size() << " ticks (2025+Jan2026)" << std::endl;

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();
    std::cout << "Total load time: " << duration << "s" << std::endl;
}

TestResult RunTest(const TestConfig& cfg) {
    const auto& ticks = (cfg.year == "2024") ? g_ticks_2024 : g_ticks_2025;
    std::string start_date = (cfg.year == "2024") ? "2024.01.01" : "2025.01.01";
    std::string end_date = (cfg.year == "2024") ? "2024.12.31" : "2026.01.27";

    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = 10000.0;
    config.contract_size = 100.0;
    config.leverage = 500.0;
    config.pip_size = 0.01;
    config.swap_long = -68.25;
    config.swap_short = 35.06;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = start_date;
    config.end_date = end_date;

    TickDataConfig tick_cfg;
    tick_cfg.format = TickDataFormat::MT5_CSV;
    config.tick_data_config = tick_cfg;

    TickBasedEngine engine(config);

    // Configure safety
    FillUpOscillation::SafetyConfig safety;
    safety.force_min_volume_entry = cfg.force_min_volume_entry;
    safety.max_positions = cfg.max_positions;
    safety.margin_level_floor = cfg.margin_level_floor;
    safety.equity_stop_pct = 0;  // Disabled for this test

    FillUpOscillation::AdaptiveConfig adaptive;
    adaptive.typical_vol_pct = 0.55;

    FillUpOscillation strategy(
        13.0,   // survive_pct
        1.5,    // base_spacing
        0.01,   // min_volume
        10.0,   // max_volume
        100.0,  // contract_size
        500.0,  // leverage
        FillUpOscillation::ADAPTIVE_SPACING,
        0.1,    // antifragile_scale
        30.0,   // velocity_threshold
        4.0,    // volatility_lookback_hours
        adaptive,
        safety
    );

    engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();
    auto& stats = strategy.GetStats();

    TestResult result;
    result.name = cfg.name;
    result.year = cfg.year;
    result.final_balance = results.final_balance;
    result.return_multiple = results.final_balance / 10000.0;
    result.max_dd_pct = results.max_drawdown_pct;
    result.total_trades = results.total_trades;
    result.forced_entries = stats.forced_entries;
    result.max_pos_blocks = stats.max_position_blocks;
    result.peak_positions = stats.peak_positions;
    result.completed = true;

    return result;
}

void Worker() {
    while (true) {
        TestConfig task;
        {
            std::lock_guard<std::mutex> lock(g_queue_mutex);
            if (g_work_queue.empty()) {
                return;
            }
            task = g_work_queue.front();
            g_work_queue.pop();
        }

        TestResult result = RunTest(task);

        {
            std::lock_guard<std::mutex> lock(g_results_mutex);
            g_results.push_back(result);
        }

        int done = ++g_completed;
        std::cout << "\r  [" << done << "/" << g_total_tasks << "] "
                  << task.name << " " << task.year << ": "
                  << std::fixed << std::setprecision(2) << result.return_multiple << "x"
                  << std::string(20, ' ') << std::flush;
    }
}

int main() {
    std::cout << std::string(80, '=') << std::endl;
    std::cout << "FORCED ENTRY PARALLEL VALIDATION TEST" << std::endl;
    std::cout << "Testing forced entry ON vs OFF across 2024 and 2025" << std::endl;
    std::cout << std::string(80, '=') << std::endl << std::endl;

    // Load tick data once
    LoadTickData();

    // Create test configurations
    std::vector<std::pair<std::string, bool>> force_configs = {
        {"FORCE_OFF", false},
        {"FORCE_ON", true}
    };

    std::vector<std::pair<std::string, int>> max_pos_configs = {
        {"", 0},           // unlimited
        {"_MAX200", 200},
        {"_MAX150", 150},
        {"_MAX100", 100},
        {"_MAX50", 50}
    };

    std::vector<std::pair<std::string, double>> margin_configs = {
        {"", 0},           // disabled
        {"_MARGIN100", 100.0},
        {"_MARGIN150", 150.0}
    };

    // Build work queue
    for (const auto& force : force_configs) {
        for (const auto& maxpos : max_pos_configs) {
            for (const auto& margin : margin_configs) {
                // Skip redundant configs (FORCE_OFF with max_pos or margin has no effect)
                if (!force.second && (maxpos.second > 0 || margin.second > 0)) {
                    continue;
                }
                // Skip combined configs with FORCE_OFF
                std::string name = force.first + maxpos.first + margin.first;

                for (const std::string& year : {"2024", "2025"}) {
                    TestConfig cfg;
                    cfg.name = name;
                    cfg.force_min_volume_entry = force.second;
                    cfg.max_positions = maxpos.second;
                    cfg.margin_level_floor = margin.second;
                    cfg.year = year;
                    g_work_queue.push(cfg);
                }
            }
        }
    }

    g_total_tasks = g_work_queue.size();
    std::cout << "\nRunning " << g_total_tasks << " tests in parallel..." << std::endl;

    auto start = std::chrono::high_resolution_clock::now();

    // Launch worker threads
    unsigned int num_threads = std::thread::hardware_concurrency();
    std::cout << "Using " << num_threads << " threads" << std::endl;

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < num_threads; i++) {
        threads.emplace_back(Worker);
    }

    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();

    std::cout << "\n\nCompleted in " << duration << "s ("
              << std::fixed << std::setprecision(2)
              << (double)duration / g_total_tasks << "s/config)" << std::endl;

    // Sort results by name then year
    std::sort(g_results.begin(), g_results.end(), [](const TestResult& a, const TestResult& b) {
        if (a.name != b.name) return a.name < b.name;
        return a.year < b.year;
    });

    // Print results table
    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "RESULTS SUMMARY" << std::endl;
    std::cout << std::string(120, '=') << std::endl;

    std::cout << std::left << std::setw(30) << "Config"
              << std::right << std::setw(8) << "Year"
              << std::setw(10) << "Return"
              << std::setw(10) << "MaxDD"
              << std::setw(10) << "Trades"
              << std::setw(12) << "Forced"
              << std::setw(12) << "MaxPosBlk"
              << std::setw(10) << "PeakPos"
              << std::endl;
    std::cout << std::string(120, '-') << std::endl;

    for (const auto& r : g_results) {
        std::cout << std::left << std::setw(30) << r.name
                  << std::right << std::setw(8) << r.year
                  << std::setw(9) << std::fixed << std::setprecision(2) << r.return_multiple << "x"
                  << std::setw(9) << r.max_dd_pct << "%"
                  << std::setw(10) << r.total_trades
                  << std::setw(12) << r.forced_entries
                  << std::setw(12) << r.max_pos_blocks
                  << std::setw(10) << r.peak_positions
                  << std::endl;
    }

    // Regime independence analysis - find unique config names
    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "REGIME INDEPENDENCE ANALYSIS (2025/2024 Ratio)" << std::endl;
    std::cout << std::string(120, '=') << std::endl;
    std::cout << "Lower ratio = more regime-independent (ideal ~1.0-2.0)" << std::endl;
    std::cout << std::string(120, '-') << std::endl;

    std::cout << std::left << std::setw(30) << "Config"
              << std::right << std::setw(12) << "2024 Return"
              << std::setw(12) << "2025 Return"
              << std::setw(12) << "Ratio"
              << std::setw(12) << "2024 DD"
              << std::setw(12) << "2025 DD"
              << std::setw(15) << "2-Year Seq"
              << std::endl;
    std::cout << std::string(120, '-') << std::endl;

    // Group by config name
    for (size_t i = 0; i < g_results.size(); i += 2) {
        if (i + 1 >= g_results.size()) break;
        const auto& r2024 = g_results[i];
        const auto& r2025 = g_results[i + 1];

        if (r2024.year != "2024" || r2025.year != "2025" || r2024.name != r2025.name) {
            continue;  // Skip mismatched pairs
        }

        double ratio = r2025.return_multiple / r2024.return_multiple;
        double two_year = r2024.return_multiple * r2025.return_multiple;

        std::cout << std::left << std::setw(30) << r2024.name
                  << std::right << std::setw(11) << std::fixed << std::setprecision(2) << r2024.return_multiple << "x"
                  << std::setw(11) << r2025.return_multiple << "x"
                  << std::setw(11) << ratio << "x"
                  << std::setw(11) << r2024.max_dd_pct << "%"
                  << std::setw(11) << r2025.max_dd_pct << "%"
                  << std::setw(14) << two_year << "x"
                  << std::endl;
    }

    // Find best configs
    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "BEST CONFIGURATIONS" << std::endl;
    std::cout << std::string(120, '=') << std::endl;

    // Best by 2-year sequential
    double best_2yr = 0;
    std::string best_2yr_name;
    for (size_t i = 0; i < g_results.size(); i += 2) {
        if (i + 1 >= g_results.size()) break;
        const auto& r2024 = g_results[i];
        const auto& r2025 = g_results[i + 1];
        if (r2024.year != "2024" || r2025.year != "2025") continue;
        double two_year = r2024.return_multiple * r2025.return_multiple;
        if (two_year > best_2yr) {
            best_2yr = two_year;
            best_2yr_name = r2024.name;
        }
    }
    std::cout << "Best 2-year sequential: " << best_2yr_name << " = " << best_2yr << "x" << std::endl;

    // Best by lowest regime ratio (most stable)
    double best_ratio = 999;
    std::string best_ratio_name;
    double best_ratio_2yr = 0;
    for (size_t i = 0; i < g_results.size(); i += 2) {
        if (i + 1 >= g_results.size()) break;
        const auto& r2024 = g_results[i];
        const auto& r2025 = g_results[i + 1];
        if (r2024.year != "2024" || r2025.year != "2025") continue;
        double ratio = r2025.return_multiple / r2024.return_multiple;
        double two_year = r2024.return_multiple * r2025.return_multiple;
        if (ratio < best_ratio && two_year > 20) {  // Must have reasonable return
            best_ratio = ratio;
            best_ratio_name = r2024.name;
            best_ratio_2yr = two_year;
        }
    }
    std::cout << "Most regime-stable (>20x 2yr): " << best_ratio_name
              << " = " << best_ratio << "x ratio, " << best_ratio_2yr << "x 2yr" << std::endl;

    // Best by lowest DD (2025)
    double best_dd = 999;
    std::string best_dd_name;
    double best_dd_return = 0;
    for (const auto& r : g_results) {
        if (r.year == "2025" && r.max_dd_pct < best_dd && r.return_multiple > 5) {
            best_dd = r.max_dd_pct;
            best_dd_name = r.name;
            best_dd_return = r.return_multiple;
        }
    }
    std::cout << "Lowest DD (2025, >5x): " << best_dd_name
              << " = " << best_dd << "% DD, " << best_dd_return << "x return" << std::endl;

    return 0;
}
