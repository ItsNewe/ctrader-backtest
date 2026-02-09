#include "../include/strategy_wuwei.h"
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
    bool velocity;
    bool below_ema;
    bool spread;
    bool vol;
    int vel_window;
    double vel_threshold;
};

struct TestResult {
    std::string name;
    double final_balance;
    double max_dd_pct;
    int trades;
    double swap;
    long vel_fails;
    long ema_fails;
    long spread_fails;
    long vol_fails;
    long allowed;
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

    StrategyWuWei::Config strat_cfg;
    strat_cfg.survive_pct = survive;
    strat_cfg.base_spacing = spacing;
    strat_cfg.volatility_lookback_hours = lookback;
    strat_cfg.typical_vol_pct = 0.55;

    strat_cfg.require_velocity_zero = tc.velocity;
    strat_cfg.require_below_ema = tc.below_ema;
    strat_cfg.require_spread_normal = tc.spread;
    strat_cfg.require_vol_normal = tc.vol;
    strat_cfg.velocity_window = tc.vel_window;
    strat_cfg.velocity_threshold_pct = tc.vel_threshold;

    StrategyWuWei strategy(strat_cfg);

    engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();
    auto& stats = strategy.GetWuWeiStats();

    TestResult r;
    r.name = tc.name;
    r.final_balance = results.final_balance;
    r.max_dd_pct = results.max_drawdown_pct;
    r.trades = results.total_trades;
    r.swap = results.total_swap_charged;
    r.vel_fails = stats.velocity_fails;
    r.ema_fails = stats.ema_fails;
    r.spread_fails = stats.spread_fails;
    r.vol_fails = stats.vol_fails;
    r.allowed = stats.entries_allowed;
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

    // BASELINE - no Wu Wei filters
    configs.push_back({"BASELINE", false, false, false, false, 10, 0.02});

    // Individual filters
    configs.push_back({"VELOCITY_ONLY", true, false, false, false, 10, 0.02});
    configs.push_back({"EMA_ONLY", false, true, false, false, 10, 0.02});
    configs.push_back({"SPREAD_ONLY", false, false, true, false, 10, 0.02});
    configs.push_back({"VOL_ONLY", false, false, false, true, 10, 0.02});

    // Combinations
    configs.push_back({"VEL+EMA", true, true, false, false, 10, 0.02});
    configs.push_back({"VEL+SPREAD", true, false, true, false, 10, 0.02});
    configs.push_back({"EMA+SPREAD", false, true, true, false, 10, 0.02});
    configs.push_back({"EMA+VOL", false, true, false, true, 10, 0.02});

    // All Wu Wei (full "obvious only")
    configs.push_back({"ALL_WUWEI", true, true, true, true, 10, 0.02});

    // Velocity variations
    configs.push_back({"VEL_W5", true, false, false, false, 5, 0.02});
    configs.push_back({"VEL_W20", true, false, false, false, 20, 0.02});
    configs.push_back({"VEL_T01", true, false, false, false, 10, 0.01});  // Tighter
    configs.push_back({"VEL_T05", true, false, false, false, 10, 0.05});  // Looser

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
    std::cout << "=== WU WEI RESULTS (survive=" << survive << "%, spacing=$" << spacing << ") ===" << std::endl;
    std::cout << "Baseline: $" << std::fixed << std::setprecision(0) << baseline_return
              << " (" << std::setprecision(1) << baseline_return/10000.0 << "x), "
              << baseline_dd << "% DD\n" << std::endl;

    std::cout << std::left << std::setw(14) << "Config"
              << std::right << std::setw(10) << "Balance"
              << std::setw(8) << "Return"
              << std::setw(8) << "DD%"
              << std::setw(8) << "Trades"
              << std::setw(10) << "VelFail"
              << std::setw(10) << "EMAFail"
              << std::setw(10) << "Allowed"
              << std::setw(9) << "vs Base"
              << std::setw(6) << "Stat" << std::endl;
    std::cout << std::string(93, '-') << std::endl;

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

        std::cout << std::left << std::setw(14) << r.name
                  << std::right << std::fixed
                  << std::setw(10) << std::setprecision(0) << r.final_balance
                  << std::setw(7) << std::setprecision(2) << r.final_balance/10000.0 << "x"
                  << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(8) << r.trades
                  << std::setw(10) << r.vel_fails
                  << std::setw(10) << r.ema_fails
                  << std::setw(10) << r.allowed
                  << std::setw(8) << std::setprecision(1) << std::showpos << vs_baseline << "%" << std::noshowpos
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
