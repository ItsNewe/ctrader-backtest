#include "../include/strategy_combined_ju.h"
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
#include <sstream>

using namespace backtest;

// Global shared tick data
std::vector<Tick> g_shared_ticks;
std::mutex g_print_mutex;
std::atomic<int> g_completed{0};

struct TestConfig {
    std::string name;
    StrategyCombinedJu::TPMode tp_mode;
    double tp_sqrt_scale;
    double tp_linear_scale;
    bool velocity_filter;
    double velocity_threshold;
    StrategyCombinedJu::SizingMode sizing_mode;
    double sizing_scale;
    int sizing_threshold_pos;
    double sizing_threshold_mult;
};

struct TestResult {
    std::string name;
    double final_balance;
    double max_dd_pct;
    int trades;
    double swap;
    long velocity_blocks;
    long entries;
    double avg_tp;
    double avg_lots;
    int max_positions;
    bool stopped_out;
};

void LoadTickDataOnce(const std::string& path) {
    std::cout << "Loading tick data into memory..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    TickDataManager manager(path);
    Tick tick;
    while (manager.GetNextTick(tick)) {
        g_shared_ticks.push_back(tick);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();
    std::cout << "Loaded " << g_shared_ticks.size() << " ticks in " << duration << "s" << std::endl;
}

TestResult RunSingleTest(const TestConfig& tc, double survive, double spacing,
                        double lookback, const std::vector<Tick>& ticks) {
    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = 10000.0;
    config.account_currency = "USD";
    config.contract_size = 100.0;
    config.leverage = 500.0;
    config.margin_rate = 1.0;
    config.pip_size = 0.01;
    config.swap_long = -66.99;
    config.swap_short = 41.2;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = "2025.01.01";
    config.end_date = "2025.12.30";
    config.verbose = false;

    TickDataConfig tick_config;
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = true;
    config.tick_data_config = tick_config;

    TickBasedEngine engine(config);

    StrategyCombinedJu::Config strat_cfg;
    strat_cfg.survive_pct = survive;
    strat_cfg.base_spacing = spacing;
    strat_cfg.volatility_lookback_hours = lookback;
    strat_cfg.typical_vol_pct = 0.55;

    strat_cfg.tp_mode = tc.tp_mode;
    strat_cfg.tp_sqrt_scale = tc.tp_sqrt_scale;
    strat_cfg.tp_linear_scale = tc.tp_linear_scale;
    strat_cfg.tp_min = spacing;

    strat_cfg.enable_velocity_filter = tc.velocity_filter;
    strat_cfg.velocity_threshold_pct = tc.velocity_threshold;

    strat_cfg.sizing_mode = tc.sizing_mode;
    strat_cfg.sizing_linear_scale = tc.sizing_scale;
    strat_cfg.sizing_threshold_pos = tc.sizing_threshold_pos;
    strat_cfg.sizing_threshold_mult = tc.sizing_threshold_mult;

    StrategyCombinedJu strategy(strat_cfg);

    engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();
    auto& stats = strategy.GetStats();

    TestResult r;
    r.name = tc.name;
    r.final_balance = results.final_balance;
    r.max_dd_pct = results.max_drawdown_pct;
    r.trades = results.total_trades;
    r.swap = results.total_swap_charged;
    r.velocity_blocks = stats.velocity_blocks;
    r.entries = stats.entries_allowed;
    r.avg_tp = stats.entries_allowed > 0 ? stats.total_tp_set / stats.entries_allowed : 0;
    r.avg_lots = stats.entries_allowed > 0 ? stats.total_lots_opened / stats.entries_allowed : 0;
    r.max_positions = stats.max_position_count;
    r.stopped_out = results.stop_out_occurred;

    return r;
}

void Worker(std::queue<TestConfig>& tasks, std::mutex& task_mutex,
           std::vector<TestResult>& results, std::mutex& result_mutex,
           int total, double survive, double spacing, double lookback) {
    while (true) {
        TestConfig tc;
        {
            std::lock_guard<std::mutex> lock(task_mutex);
            if (tasks.empty()) break;
            tc = tasks.front();
            tasks.pop();
        }

        TestResult result = RunSingleTest(tc, survive, spacing, lookback, g_shared_ticks);

        {
            std::lock_guard<std::mutex> lock(result_mutex);
            results.push_back(result);
        }

        int done = ++g_completed;
        {
            std::lock_guard<std::mutex> lock(g_print_mutex);
            std::cout << "\rProgress: " << done << "/" << total << std::flush;
        }
    }
}

int main() {
    std::string tick_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    LoadTickDataOnce(tick_path);

    // Base parameters
    double survive = 13.0;
    double spacing = 1.50;
    double lookback = 4.0;

    // Create test configurations
    std::vector<TestConfig> configs;

    // === BASELINES ===
    // Pure baseline (no enhancements)
    configs.push_back({"BASELINE",
        StrategyCombinedJu::FIXED, 0.5, 0.3, false, 0.01,
        StrategyCombinedJu::UNIFORM, 0.5, 5, 2.0});

    // Individual components
    configs.push_back({"RUBBER_ONLY",
        StrategyCombinedJu::SQRT, 0.5, 0.3, false, 0.01,
        StrategyCombinedJu::UNIFORM, 0.5, 5, 2.0});

    configs.push_back({"VELOCITY_ONLY",
        StrategyCombinedJu::FIXED, 0.5, 0.3, true, 0.01,
        StrategyCombinedJu::UNIFORM, 0.5, 5, 2.0});

    configs.push_back({"BARBELL_LIN_ONLY",
        StrategyCombinedJu::FIXED, 0.5, 0.3, false, 0.01,
        StrategyCombinedJu::LINEAR_SIZING, 0.5, 5, 2.0});

    configs.push_back({"BARBELL_THR_ONLY",
        StrategyCombinedJu::FIXED, 0.5, 0.3, false, 0.01,
        StrategyCombinedJu::THRESHOLD_SIZING, 0.5, 5, 2.0});

    // === TWO-WAY COMBINATIONS ===
    // Rubber Band + Velocity
    configs.push_back({"RB+VEL",
        StrategyCombinedJu::SQRT, 0.5, 0.3, true, 0.01,
        StrategyCombinedJu::UNIFORM, 0.5, 5, 2.0});

    // Rubber Band + Barbell Linear
    configs.push_back({"RB+BB_LIN",
        StrategyCombinedJu::SQRT, 0.5, 0.3, false, 0.01,
        StrategyCombinedJu::LINEAR_SIZING, 0.5, 5, 2.0});

    // Rubber Band + Barbell Threshold
    configs.push_back({"RB+BB_THR",
        StrategyCombinedJu::SQRT, 0.5, 0.3, false, 0.01,
        StrategyCombinedJu::THRESHOLD_SIZING, 0.5, 5, 2.0});

    // Velocity + Barbell Linear
    configs.push_back({"VEL+BB_LIN",
        StrategyCombinedJu::FIXED, 0.5, 0.3, true, 0.01,
        StrategyCombinedJu::LINEAR_SIZING, 0.5, 5, 2.0});

    // Velocity + Barbell Threshold
    configs.push_back({"VEL+BB_THR",
        StrategyCombinedJu::FIXED, 0.5, 0.3, true, 0.01,
        StrategyCombinedJu::THRESHOLD_SIZING, 0.5, 5, 2.0});

    // === THREE-WAY COMBINATIONS (THE FULL JU) ===
    // All three: Rubber Band + Velocity + Barbell Linear
    configs.push_back({"FULL_JU_LIN",
        StrategyCombinedJu::SQRT, 0.5, 0.3, true, 0.01,
        StrategyCombinedJu::LINEAR_SIZING, 0.5, 5, 2.0});

    // All three: Rubber Band + Velocity + Barbell Threshold
    configs.push_back({"FULL_JU_THR",
        StrategyCombinedJu::SQRT, 0.5, 0.3, true, 0.01,
        StrategyCombinedJu::THRESHOLD_SIZING, 0.5, 5, 2.0});

    // === VARIATIONS OF FULL JU ===
    // Aggressive barbell
    configs.push_back({"FULL_JU_AGGR",
        StrategyCombinedJu::SQRT, 0.5, 0.3, true, 0.01,
        StrategyCombinedJu::LINEAR_SIZING, 1.0, 5, 2.0});  // Higher scale

    // Conservative barbell
    configs.push_back({"FULL_JU_CONS",
        StrategyCombinedJu::SQRT, 0.5, 0.3, true, 0.01,
        StrategyCombinedJu::LINEAR_SIZING, 0.25, 5, 2.0});  // Lower scale

    // Threshold at position 3
    configs.push_back({"FULL_JU_THR3",
        StrategyCombinedJu::SQRT, 0.5, 0.3, true, 0.01,
        StrategyCombinedJu::THRESHOLD_SIZING, 0.5, 3, 2.0});

    // Threshold at position 10
    configs.push_back({"FULL_JU_THR10",
        StrategyCombinedJu::SQRT, 0.5, 0.3, true, 0.01,
        StrategyCombinedJu::THRESHOLD_SIZING, 0.5, 10, 2.0});

    // Higher threshold multiplier
    configs.push_back({"FULL_JU_THR_3X",
        StrategyCombinedJu::SQRT, 0.5, 0.3, true, 0.01,
        StrategyCombinedJu::THRESHOLD_SIZING, 0.5, 5, 3.0});

    // Looser velocity threshold
    configs.push_back({"FULL_JU_VEL02",
        StrategyCombinedJu::SQRT, 0.5, 0.3, true, 0.02,
        StrategyCombinedJu::LINEAR_SIZING, 0.5, 5, 2.0});

    // Tighter velocity threshold (more selective)
    configs.push_back({"FULL_JU_VEL005",
        StrategyCombinedJu::SQRT, 0.5, 0.3, true, 0.005,
        StrategyCombinedJu::LINEAR_SIZING, 0.5, 5, 2.0});

    // Higher sqrt scale for TP
    configs.push_back({"FULL_JU_TP07",
        StrategyCombinedJu::SQRT, 0.7, 0.3, true, 0.01,
        StrategyCombinedJu::LINEAR_SIZING, 0.5, 5, 2.0});

    // Linear TP mode instead of SQRT
    configs.push_back({"FULL_JU_LINTP",
        StrategyCombinedJu::LINEAR, 0.5, 0.3, true, 0.01,
        StrategyCombinedJu::LINEAR_SIZING, 0.5, 5, 2.0});

    int total = (int)configs.size();
    std::cout << "\nRunning " << total << " configurations...\n" << std::endl;

    // Create work queue
    std::queue<TestConfig> tasks;
    for (const auto& c : configs) {
        tasks.push(c);
    }

    // Results storage
    std::vector<TestResult> results;
    std::mutex task_mutex, result_mutex;

    // Launch workers
    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;
    std::vector<std::thread> threads;

    auto start_time = std::chrono::high_resolution_clock::now();

    for (unsigned int i = 0; i < num_threads; i++) {
        threads.emplace_back(Worker, std::ref(tasks), std::ref(task_mutex),
                           std::ref(results), std::ref(result_mutex),
                           total, survive, spacing, lookback);
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count();

    std::cout << "\n\nCompleted in " << duration << "s\n" << std::endl;

    // Sort by return
    std::sort(results.begin(), results.end(), [](const TestResult& a, const TestResult& b) {
        if (a.stopped_out != b.stopped_out) return b.stopped_out;
        return a.final_balance > b.final_balance;
    });

    // Find baseline
    double baseline_return = 0.0;
    double baseline_dd = 0.0;
    for (const auto& r : results) {
        if (r.name == "BASELINE") {
            baseline_return = r.final_balance;
            baseline_dd = r.max_dd_pct;
            break;
        }
    }

    // Print results
    std::cout << "=== COMBINED JU RESULTS (survive=" << survive << "%, spacing=$" << spacing << ") ===" << std::endl;
    std::cout << "Baseline: $" << std::fixed << std::setprecision(0) << baseline_return
              << " (" << std::setprecision(1) << baseline_return/10000.0 << "x), "
              << baseline_dd << "% DD\n" << std::endl;

    std::cout << std::left << std::setw(16) << "Config"
              << std::right << std::setw(10) << "Balance"
              << std::setw(8) << "Return"
              << std::setw(8) << "DD%"
              << std::setw(8) << "Trades"
              << std::setw(8) << "MaxPos"
              << std::setw(8) << "AvgTP"
              << std::setw(9) << "vs Base"
              << std::setw(6) << "Stat" << std::endl;
    std::cout << std::string(89, '-') << std::endl;

    for (const auto& r : results) {
        double vs_baseline = (r.final_balance / baseline_return - 1.0) * 100.0;

        std::string status = r.stopped_out ? "SO" : "ok";
        if (!r.stopped_out && r.final_balance > baseline_return && r.max_dd_pct < baseline_dd) {
            status = "BOTH+";
        } else if (!r.stopped_out && r.final_balance > baseline_return) {
            status = "RET+";
        } else if (!r.stopped_out && r.max_dd_pct < baseline_dd) {
            status = "DD-";
        }

        std::cout << std::left << std::setw(16) << r.name
                  << std::right << std::fixed
                  << std::setw(10) << std::setprecision(0) << r.final_balance
                  << std::setw(7) << std::setprecision(2) << r.final_balance/10000.0 << "x"
                  << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(8) << r.trades
                  << std::setw(8) << r.max_positions
                  << std::setw(7) << std::setprecision(2) << r.avg_tp << "$"
                  << std::setw(8) << std::setprecision(1) << std::showpos << vs_baseline << "%" << std::noshowpos
                  << std::setw(6) << status << std::endl;
    }

    // Summary by category
    std::cout << "\n=== COMPARISON BY CATEGORY ===" << std::endl;

    // Find specific results
    TestResult* baseline = nullptr;
    TestResult* rubber_only = nullptr;
    TestResult* velocity_only = nullptr;
    TestResult* rb_vel = nullptr;
    TestResult* full_ju = nullptr;

    for (auto& r : results) {
        if (r.name == "BASELINE") baseline = &r;
        else if (r.name == "RUBBER_ONLY") rubber_only = &r;
        else if (r.name == "VELOCITY_ONLY") velocity_only = &r;
        else if (r.name == "RB+VEL") rb_vel = &r;
        else if (r.name == "FULL_JU_LIN") full_ju = &r;
    }

    if (baseline && rubber_only && velocity_only && rb_vel && full_ju) {
        std::cout << "\nSynergy Analysis:" << std::endl;
        std::cout << "- BASELINE:      " << baseline->final_balance/10000.0 << "x, " << baseline->max_dd_pct << "% DD" << std::endl;
        std::cout << "- RUBBER_ONLY:   " << rubber_only->final_balance/10000.0 << "x, " << rubber_only->max_dd_pct << "% DD" << std::endl;
        std::cout << "- VELOCITY_ONLY: " << velocity_only->final_balance/10000.0 << "x, " << velocity_only->max_dd_pct << "% DD" << std::endl;
        std::cout << "- RB+VEL:        " << rb_vel->final_balance/10000.0 << "x, " << rb_vel->max_dd_pct << "% DD" << std::endl;
        std::cout << "- FULL_JU_LIN:   " << full_ju->final_balance/10000.0 << "x, " << full_ju->max_dd_pct << "% DD" << std::endl;

        double rubber_add = (rubber_only->final_balance - baseline->final_balance) / baseline->final_balance * 100;
        double velocity_add = (velocity_only->final_balance - baseline->final_balance) / baseline->final_balance * 100;
        double expected_combined = rubber_add + velocity_add;
        double actual_combined = (rb_vel->final_balance - baseline->final_balance) / baseline->final_balance * 100;

        std::cout << "\nExpected RB+VEL (additive): +" << expected_combined << "%" << std::endl;
        std::cout << "Actual RB+VEL:              +" << actual_combined << "%" << std::endl;
        std::cout << "Synergy:                    " << (actual_combined > expected_combined ? "POSITIVE" : "NEGATIVE") << std::endl;
    }

    // Summary
    std::cout << "\n=== SUMMARY ===" << std::endl;

    int improved_both = 0, improved_return = 0, improved_dd = 0, hurt = 0;
    for (const auto& r : results) {
        if (r.name == "BASELINE" || r.stopped_out) continue;
        if (r.final_balance > baseline_return && r.max_dd_pct < baseline_dd) {
            improved_both++;
        } else if (r.final_balance > baseline_return) {
            improved_return++;
        } else if (r.max_dd_pct < baseline_dd) {
            improved_dd++;
        } else {
            hurt++;
        }
    }

    std::cout << "Improved BOTH (return + DD): " << improved_both << std::endl;
    std::cout << "Improved return only: " << improved_return << std::endl;
    std::cout << "Improved DD only: " << improved_dd << std::endl;
    std::cout << "Hurt performance: " << hurt << std::endl;

    return 0;
}
