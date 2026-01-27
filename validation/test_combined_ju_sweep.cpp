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

using namespace backtest;

std::vector<Tick> g_shared_ticks;
std::mutex g_print_mutex;
std::atomic<int> g_completed{0};

struct TestConfig {
    std::string name;
    int threshold_pos;
    double threshold_mult;
    double sqrt_scale;
    double velocity_threshold;
};

struct TestResult {
    std::string name;
    double final_balance;
    double max_dd_pct;
    int trades;
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

    // Full Ju config: Rubber Band + Velocity + Threshold Barbell
    strat_cfg.tp_mode = StrategyCombinedJu::SQRT;
    strat_cfg.tp_sqrt_scale = tc.sqrt_scale;
    strat_cfg.tp_min = spacing;

    strat_cfg.enable_velocity_filter = true;
    strat_cfg.velocity_threshold_pct = tc.velocity_threshold;

    strat_cfg.sizing_mode = StrategyCombinedJu::THRESHOLD_SIZING;
    strat_cfg.sizing_threshold_pos = tc.threshold_pos;
    strat_cfg.sizing_threshold_mult = tc.threshold_mult;

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

    double survive = 13.0;
    double spacing = 1.50;
    double lookback = 4.0;

    std::vector<TestConfig> configs;

    // Sweep threshold position, multiplier, sqrt_scale, velocity threshold
    for (int pos : {1, 2, 3, 4, 5, 7, 10, 15}) {
        for (double mult : {1.5, 2.0, 2.5, 3.0}) {
            for (double sqrt_sc : {0.3, 0.5, 0.7}) {
                std::ostringstream ss;
                ss << "P" << pos << "_M" << mult << "_S" << sqrt_sc;
                configs.push_back({ss.str(), pos, mult, sqrt_sc, 0.01});
            }
        }
    }

    // Also test velocity threshold variations on best configs
    for (double vel : {0.005, 0.01, 0.02, 0.03}) {
        std::ostringstream ss;
        ss << "P3_M2_S0.5_V" << vel;
        configs.push_back({ss.str(), 3, 2.0, 0.5, vel});
    }

    int total = (int)configs.size();
    std::cout << "\nRunning " << total << " configurations...\n" << std::endl;

    std::queue<TestConfig> tasks;
    for (const auto& c : configs) {
        tasks.push(c);
    }

    std::vector<TestResult> results;
    std::mutex task_mutex, result_mutex;

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

    // Sort by return (surviving only)
    std::sort(results.begin(), results.end(), [](const TestResult& a, const TestResult& b) {
        if (a.stopped_out != b.stopped_out) return b.stopped_out;
        return a.final_balance > b.final_balance;
    });

    // Print top 20
    std::cout << "=== TOP 20 COMBINED JU CONFIGURATIONS ===" << std::endl;
    std::cout << std::left << std::setw(22) << "Config"
              << std::right << std::setw(10) << "Balance"
              << std::setw(8) << "Return"
              << std::setw(8) << "DD%"
              << std::setw(8) << "Trades"
              << std::setw(8) << "MaxPos"
              << std::setw(6) << "Stat" << std::endl;
    std::cout << std::string(70, '-') << std::endl;

    int count = 0;
    for (const auto& r : results) {
        if (count >= 20) break;

        std::string status = r.stopped_out ? "SO" : "ok";
        if (!r.stopped_out && r.max_dd_pct < 68.8) {
            status = "DD-";
        }
        if (!r.stopped_out && r.final_balance > 145612) {
            status = r.max_dd_pct < 68.8 ? "BEST" : "RET+";
        }

        std::cout << std::left << std::setw(22) << r.name
                  << std::right << std::fixed
                  << std::setw(10) << std::setprecision(0) << r.final_balance
                  << std::setw(7) << std::setprecision(2) << r.final_balance/10000.0 << "x"
                  << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(8) << r.trades
                  << std::setw(8) << r.max_positions
                  << std::setw(6) << status << std::endl;
        count++;
    }

    // Summary by threshold position
    std::cout << "\n=== SUMMARY BY THRESHOLD POSITION ===" << std::endl;
    std::cout << "(Average return for each position, excluding stop-outs)\n" << std::endl;

    for (int pos : {1, 2, 3, 4, 5, 7, 10, 15}) {
        double sum_return = 0;
        double min_dd = 100;
        int count_surviving = 0;

        for (const auto& r : results) {
            if (r.stopped_out) continue;
            std::string prefix = "P" + std::to_string(pos) + "_";
            if (r.name.substr(0, prefix.length()) == prefix) {
                sum_return += r.final_balance;
                min_dd = std::min(min_dd, r.max_dd_pct);
                count_surviving++;
            }
        }

        if (count_surviving > 0) {
            double avg_return = sum_return / count_surviving;
            std::cout << "Position " << std::setw(2) << pos << ": "
                      << std::setw(6) << std::setprecision(2) << avg_return/10000.0 << "x avg, "
                      << std::setw(5) << std::setprecision(1) << min_dd << "% min DD, "
                      << count_surviving << "/" << 12 << " surviving" << std::endl;
        } else {
            std::cout << "Position " << std::setw(2) << pos << ": ALL STOPPED OUT" << std::endl;
        }
    }

    return 0;
}
