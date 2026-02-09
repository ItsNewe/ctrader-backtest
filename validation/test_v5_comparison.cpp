#include "../include/fill_up_strategy_v5.h"
#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <thread>
#include <mutex>

using namespace backtest;

std::mutex g_print_mutex;
std::mutex g_results_mutex;

struct TestConfig {
    std::string name;
    double survive_pct;
    double spacing;
    int ma_period;
    std::string start_date;
};

struct Result {
    std::string name;
    std::string start;
    double ret;
    double max_dd;
    int trades;
    double swap;
    bool stopped_out;
};

std::vector<Result> g_results;

void RunTest(const TestConfig& tc, const std::vector<Tick>& ticks) {
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
    config.start_date = tc.start_date;
    config.end_date = "2025.12.29";

    FillUpStrategyV5::Config sc;
    sc.survive_pct = tc.survive_pct;
    sc.spacing = tc.spacing;
    sc.min_volume = 0.01;
    sc.max_volume = 10.0;
    sc.contract_size = 100.0;
    sc.leverage = 500.0;
    sc.ma_period = tc.ma_period;
    sc.tp_multiplier = 1.0;  // Standard TP
    sc.stop_new_at_dd = 5.0;
    sc.partial_close_at_dd = 8.0;
    sc.close_all_at_dd = 25.0;
    sc.max_positions = 50;
    sc.reduce_size_at_dd = 3.0;

    FillUpStrategyV5 strategy(sc);
    TickBasedEngine engine(config);

    engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();

    Result r;
    r.name = tc.name;
    r.start = tc.start_date;
    r.ret = results.final_balance / 10000.0;
    r.max_dd = results.max_drawdown_pct;
    r.trades = results.total_trades;
    r.swap = results.total_swap_charged;
    r.stopped_out = results.final_balance < 1000.0;

    {
        std::lock_guard<std::mutex> lock(g_results_mutex);
        g_results.push_back(r);
    }

    {
        std::lock_guard<std::mutex> lock(g_print_mutex);
        std::cout << std::fixed << std::setprecision(2);
        std::cout << tc.name << " (" << tc.start_date << ") | "
                  << r.ret << "x | " << r.max_dd << "% DD | "
                  << r.trades << " trades | "
                  << (r.stopped_out ? "STOP-OUT" : "ok") << std::endl;
    }
}

int main() {
    std::cout << "=== V5 (SMA Filter) vs CombinedJu Comparison ===" << std::endl;
    std::cout << "Testing both from Jan and Oct start dates" << std::endl;
    std::cout << std::endl;

    // Load tick data
    std::cout << "Loading tick data..." << std::endl;
    TickDataConfig tick_config;
    tick_config.file_path = "C:/Users/user/Documents/ctrader-backtest/validation/Grid/XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = true;

    TickDataManager manager(tick_config);
    std::vector<Tick> all_ticks;
    Tick tick;
    while (manager.GetNextTick(tick)) {
        all_ticks.push_back(tick);
    }
    std::cout << "Loaded " << all_ticks.size() << " ticks" << std::endl;
    std::cout << std::endl;

    // Test configs - V5 with different parameters
    std::vector<TestConfig> configs;

    // January start
    configs.push_back({"V5_s13_sp1.5_ma11000", 13.0, 1.5, 11000, "2025.01.01"});
    configs.push_back({"V5_s13_sp1.5_ma5000", 13.0, 1.5, 5000, "2025.01.01"});
    configs.push_back({"V5_s13_sp1.5_ma20000", 13.0, 1.5, 20000, "2025.01.01"});
    configs.push_back({"V5_s15_sp2.0_ma11000", 15.0, 2.0, 11000, "2025.01.01"});
    configs.push_back({"V5_s18_sp2.0_ma11000", 18.0, 2.0, 11000, "2025.01.01"});
    configs.push_back({"V5_s20_sp2.5_ma11000", 20.0, 2.5, 11000, "2025.01.01"});
    configs.push_back({"V5_s25_sp3.0_ma11000", 25.0, 3.0, 11000, "2025.01.01"});

    // October start
    configs.push_back({"V5_s13_sp1.5_ma11000", 13.0, 1.5, 11000, "2025.10.17"});
    configs.push_back({"V5_s13_sp1.5_ma5000", 13.0, 1.5, 5000, "2025.10.17"});
    configs.push_back({"V5_s13_sp1.5_ma20000", 13.0, 1.5, 20000, "2025.10.17"});
    configs.push_back({"V5_s15_sp2.0_ma11000", 15.0, 2.0, 11000, "2025.10.17"});
    configs.push_back({"V5_s18_sp2.0_ma11000", 18.0, 2.0, 11000, "2025.10.17"});
    configs.push_back({"V5_s20_sp2.5_ma11000", 20.0, 2.5, 11000, "2025.10.17"});
    configs.push_back({"V5_s25_sp3.0_ma11000", 25.0, 3.0, 11000, "2025.10.17"});

    std::cout << "Config | Return | MaxDD | Trades | Status" << std::endl;
    std::cout << "-------|--------|-------|--------|--------" << std::endl;

    // Run tests in parallel
    std::vector<std::thread> threads;
    for (const auto& tc : configs) {
        threads.emplace_back(RunTest, tc, std::cref(all_ticks));
    }

    for (auto& t : threads) {
        t.join();
    }

    // Summary
    std::cout << std::endl;
    std::cout << "=== SUMMARY ===" << std::endl;
    std::cout << std::endl;

    // Sort by start date then by return
    std::sort(g_results.begin(), g_results.end(),
              [](const Result& a, const Result& b) {
                  if (a.start != b.start) return a.start < b.start;
                  return a.ret > b.ret;
              });

    std::string current_start = "";
    for (const auto& r : g_results) {
        if (r.start != current_start) {
            current_start = r.start;
            std::cout << std::endl;
            std::cout << "--- Start: " << current_start << " ---" << std::endl;
            std::cout << std::fixed << std::setprecision(2);
        }
        double ret_dd = (r.max_dd > 0) ? (r.ret / (r.max_dd / 100.0)) : 0;
        std::cout << r.name << " | " << r.ret << "x | " << r.max_dd << "% DD | Ret/DD: " << ret_dd;
        if (r.stopped_out) std::cout << " | STOP-OUT";
        std::cout << std::endl;
    }

    return 0;
}
