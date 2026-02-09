#include "../include/strategy_via_negativa.h"
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
    bool velocity_veto;
    bool concentration_veto;
    bool losing_streak_veto;
    bool extreme_vol_veto;
    bool against_trend_veto;
    double velocity_threshold;
    int max_positions;
    int losing_streak;
    double vol_extreme_mult;
    double trend_threshold;
};

struct TestResult {
    std::string name;
    double final_balance;
    double max_dd_pct;
    int trades;
    double swap;
    long velocity_vetoes;
    long concentration_vetoes;
    long losing_vetoes;
    long vol_vetoes;
    long trend_vetoes;
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
    config.verbose = false;  // Disable trade logging

    TickDataConfig tick_config;
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = true;
    config.tick_data_config = tick_config;

    TickBasedEngine engine(config);

    StrategyViaNegativa::Config strat_cfg;
    strat_cfg.survive_pct = survive;
    strat_cfg.base_spacing = spacing;
    strat_cfg.volatility_lookback_hours = lookback;
    strat_cfg.typical_vol_pct = 0.55;

    // Veto settings from config
    strat_cfg.velocity_veto = tc.velocity_veto;
    strat_cfg.concentration_veto = tc.concentration_veto;
    strat_cfg.losing_streak_veto = tc.losing_streak_veto;
    strat_cfg.extreme_vol_veto = tc.extreme_vol_veto;
    strat_cfg.against_trend_veto = tc.against_trend_veto;
    strat_cfg.velocity_threshold_pct = tc.velocity_threshold;
    strat_cfg.max_positions = tc.max_positions;
    strat_cfg.losing_streak_count = tc.losing_streak;
    strat_cfg.vol_extreme_mult = tc.vol_extreme_mult;
    strat_cfg.trend_threshold_pct = tc.trend_threshold;
    strat_cfg.trend_lookback_ticks = 1000;  // Reduced from default 10000 for performance

    StrategyViaNegativa strategy(strat_cfg);

    engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();
    auto& vstats = strategy.GetVetoStats();

    TestResult r;
    r.name = tc.name;
    r.final_balance = results.final_balance;
    r.max_dd_pct = results.max_drawdown_pct;
    r.trades = results.total_trades;
    r.swap = results.total_swap_charged;
    r.velocity_vetoes = vstats.velocity_vetoes;
    r.concentration_vetoes = vstats.concentration_vetoes;
    r.losing_vetoes = vstats.losing_streak_vetoes;
    r.vol_vetoes = vstats.extreme_vol_vetoes;
    r.trend_vetoes = vstats.against_trend_vetoes;
    r.allowed = vstats.entries_allowed;
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

    // Create test configurations - simplified for faster testing
    std::vector<TestConfig> configs;

    // BASELINE - no vetoes (trend_lookback reduced to 1000 for performance)
    // Note: All configs use trend_lookback_ticks=1000 (set via strat_cfg.trend_lookback_ticks)
    configs.push_back({"BASELINE", false, false, false, false, false, 0.5, 50, 5, 2.0, 0.3});

    // === KEY INDIVIDUAL VETOES ===
    configs.push_back({"VEL_0.3", true, false, false, false, false, 0.3, 50, 5, 2.0, 0.3});
    configs.push_back({"VEL_0.5", true, false, false, false, false, 0.5, 50, 5, 2.0, 0.3});
    configs.push_back({"CONC_30", false, true, false, false, false, 0.5, 30, 5, 2.0, 0.3});
    configs.push_back({"CONC_50", false, true, false, false, false, 0.5, 50, 5, 2.0, 0.3});
    configs.push_back({"VOL_1.5x", false, false, false, true, false, 0.5, 50, 5, 1.5, 0.3});
    configs.push_back({"VOL_2.0x", false, false, false, true, false, 0.5, 50, 5, 2.0, 0.3});
    configs.push_back({"TREND_0.2", false, false, false, false, true, 0.5, 50, 5, 2.0, 0.2});
    configs.push_back({"TREND_0.3", false, false, false, false, true, 0.5, 50, 5, 2.0, 0.3});

    // === KEY COMBINATIONS ===
    configs.push_back({"VEL+CONC", true, true, false, false, false, 0.5, 50, 5, 2.0, 0.3});
    configs.push_back({"VEL+TREND", true, false, false, false, true, 0.5, 50, 5, 2.0, 0.3});
    configs.push_back({"ALL_VETOES", true, true, true, true, true, 0.5, 50, 5, 2.0, 0.3});
    configs.push_back({"ALL_STRICT", true, true, true, true, true, 0.3, 30, 3, 1.5, 0.2});

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
    std::cout << "=== VIA NEGATIVA RESULTS (survive=" << survive << "%, spacing=$" << spacing << ") ===" << std::endl;
    std::cout << "Baseline: $" << std::fixed << std::setprecision(0) << baseline_return
              << " (" << std::setprecision(1) << baseline_return/10000.0 << "x), "
              << baseline_dd << "% DD\n" << std::endl;

    std::cout << std::left << std::setw(18) << "Config"
              << std::right << std::setw(10) << "Balance"
              << std::setw(8) << "Return"
              << std::setw(8) << "DD%"
              << std::setw(8) << "Trades"
              << std::setw(10) << "Vetoes"
              << std::setw(10) << "vs Base"
              << std::setw(6) << "Stat" << std::endl;
    std::cout << std::string(78, '-') << std::endl;

    for (const auto& r : results) {
        long total_vetoes = r.velocity_vetoes + r.concentration_vetoes +
                           r.losing_vetoes + r.vol_vetoes + r.trend_vetoes;

        double vs_baseline = (r.final_balance / baseline_return - 1.0) * 100.0;

        std::string status = r.stopped_out ? "SO" : "ok";
        if (!r.stopped_out && r.final_balance > baseline_return && r.max_dd_pct < baseline_dd) {
            status = "BOTH+";
        } else if (!r.stopped_out && r.final_balance > baseline_return) {
            status = "RET+";
        } else if (!r.stopped_out && r.max_dd_pct < baseline_dd) {
            status = "DD-";
        }

        std::cout << std::left << std::setw(18) << r.name
                  << std::right << std::fixed
                  << std::setw(10) << std::setprecision(0) << r.final_balance
                  << std::setw(7) << std::setprecision(2) << r.final_balance/10000.0 << "x"
                  << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(8) << r.trades
                  << std::setw(10) << total_vetoes
                  << std::setw(9) << std::setprecision(1) << std::showpos << vs_baseline << "%" << std::noshowpos
                  << std::setw(6) << status << std::endl;
    }

    // Detailed veto breakdown for interesting configs
    std::cout << "\n=== VETO BREAKDOWN ===" << std::endl;
    std::cout << std::left << std::setw(18) << "Config"
              << std::right << std::setw(10) << "VelVeto"
              << std::setw(10) << "ConcVeto"
              << std::setw(10) << "LossVeto"
              << std::setw(10) << "VolVeto"
              << std::setw(10) << "TrendVeto"
              << std::setw(10) << "Allowed" << std::endl;
    std::cout << std::string(78, '-') << std::endl;

    for (const auto& r : results) {
        if (r.name == "BASELINE" || r.name.find("+") != std::string::npos ||
            r.name.find("ALL") != std::string::npos) {
            std::cout << std::left << std::setw(18) << r.name
                      << std::right
                      << std::setw(10) << r.velocity_vetoes
                      << std::setw(10) << r.concentration_vetoes
                      << std::setw(10) << r.losing_vetoes
                      << std::setw(10) << r.vol_vetoes
                      << std::setw(10) << r.trend_vetoes
                      << std::setw(10) << r.allowed << std::endl;
        }
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
