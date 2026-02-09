#include "../include/strategy_reflexivity.h"
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
    StrategyReflexivity::FeedbackMode mode;
    int lookback;
    double neg_threshold;
    double pos_threshold;
    double neg_mult;
    double pos_mult;
};

struct TestResult {
    std::string name;
    double final_balance;
    double max_dd_pct;
    int trades;
    double swap;
    int positive_count;
    int negative_count;
    int blocked;
    double feedback_score;
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

    StrategyReflexivity::Config strat_cfg;
    strat_cfg.survive_pct = survive;
    strat_cfg.base_spacing = spacing;
    strat_cfg.volatility_lookback_hours = lookback;
    strat_cfg.typical_vol_pct = 0.55;

    strat_cfg.feedback_mode = tc.mode;
    strat_cfg.feedback_lookback_trades = tc.lookback;
    strat_cfg.negative_threshold = tc.neg_threshold;
    strat_cfg.positive_threshold = tc.pos_threshold;
    strat_cfg.negative_lot_mult = tc.neg_mult;
    strat_cfg.positive_lot_mult = tc.pos_mult;

    StrategyReflexivity strategy(strat_cfg);

    engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();
    auto& fstats = strategy.GetFeedbackStats();

    TestResult r;
    r.name = tc.name;
    r.final_balance = results.final_balance;
    r.max_dd_pct = results.max_drawdown_pct;
    r.trades = results.total_trades;
    r.swap = results.total_swap_charged;
    r.positive_count = fstats.positive_feedback_count;
    r.negative_count = fstats.negative_feedback_count;
    r.blocked = fstats.entries_blocked;
    r.feedback_score = fstats.current_feedback_score;
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

    // BASELINE - always trade
    configs.push_back({"BASELINE", StrategyReflexivity::ALWAYS_TRADE, 5, -0.1, 0.1, 0.5, 1.5});

    // === PAUSE ON NEGATIVE MODE ===
    // Vary lookback window
    configs.push_back({"PAUSE_LB3", StrategyReflexivity::PAUSE_ON_NEGATIVE, 3, -0.1, 0.1, 0.5, 1.5});
    configs.push_back({"PAUSE_LB5", StrategyReflexivity::PAUSE_ON_NEGATIVE, 5, -0.1, 0.1, 0.5, 1.5});
    configs.push_back({"PAUSE_LB10", StrategyReflexivity::PAUSE_ON_NEGATIVE, 10, -0.1, 0.1, 0.5, 1.5});

    // Vary negative threshold
    configs.push_back({"PAUSE_NEG05", StrategyReflexivity::PAUSE_ON_NEGATIVE, 5, -0.05, 0.1, 0.5, 1.5});
    configs.push_back({"PAUSE_NEG15", StrategyReflexivity::PAUSE_ON_NEGATIVE, 5, -0.15, 0.1, 0.5, 1.5});
    configs.push_back({"PAUSE_NEG20", StrategyReflexivity::PAUSE_ON_NEGATIVE, 5, -0.20, 0.1, 0.5, 1.5});

    // === SCALE WITH FEEDBACK MODE ===
    // Vary scaling factors
    configs.push_back({"SCALE_DEFAULT", StrategyReflexivity::SCALE_WITH_FEEDBACK, 5, -0.1, 0.1, 0.5, 1.5});
    configs.push_back({"SCALE_AGGR", StrategyReflexivity::SCALE_WITH_FEEDBACK, 5, -0.1, 0.1, 0.3, 2.0});
    configs.push_back({"SCALE_CONS", StrategyReflexivity::SCALE_WITH_FEEDBACK, 5, -0.1, 0.1, 0.7, 1.2});

    // Vary lookback + thresholds
    configs.push_back({"SCALE_LB10", StrategyReflexivity::SCALE_WITH_FEEDBACK, 10, -0.1, 0.1, 0.5, 1.5});
    configs.push_back({"SCALE_TIGHT", StrategyReflexivity::SCALE_WITH_FEEDBACK, 5, -0.05, 0.05, 0.5, 1.5});
    configs.push_back({"SCALE_WIDE", StrategyReflexivity::SCALE_WITH_FEEDBACK, 5, -0.2, 0.2, 0.5, 1.5});

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
    std::cout << "=== REFLEXIVITY RESULTS (survive=" << survive << "%, spacing=$" << spacing << ") ===" << std::endl;
    std::cout << "Baseline: $" << std::fixed << std::setprecision(0) << baseline_return
              << " (" << std::setprecision(1) << baseline_return/10000.0 << "x), "
              << baseline_dd << "% DD\n" << std::endl;

    std::cout << std::left << std::setw(16) << "Config"
              << std::right << std::setw(10) << "Balance"
              << std::setw(8) << "Return"
              << std::setw(8) << "DD%"
              << std::setw(8) << "Trades"
              << std::setw(8) << "+FB"
              << std::setw(8) << "-FB"
              << std::setw(8) << "Block"
              << std::setw(10) << "vs Base"
              << std::setw(6) << "Stat" << std::endl;
    std::cout << std::string(90, '-') << std::endl;

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
                  << std::setw(8) << r.positive_count
                  << std::setw(8) << r.negative_count
                  << std::setw(8) << r.blocked
                  << std::setw(9) << std::setprecision(1) << std::showpos << vs_baseline << "%" << std::noshowpos
                  << std::setw(6) << status << std::endl;
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
