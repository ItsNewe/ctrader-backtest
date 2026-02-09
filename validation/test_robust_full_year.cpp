#include "../include/strategy_combined_ju.h"
#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <thread>
#include <mutex>

using namespace backtest;

std::mutex g_print_mutex;

struct Config {
    std::string name;
    double survive_pct;
    double base_spacing;
};

struct Result {
    std::string name;
    double ret;
    double max_dd;
    int trades;
    double swap;
    bool stopped_out;
};

std::vector<Result> g_results;
std::mutex g_results_mutex;

void RunTest(const Config& cfg, const std::vector<Tick>& ticks) {
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
    config.end_date = "2025.12.29";

    StrategyCombinedJu::Config sc;
    sc.survive_pct = cfg.survive_pct;
    sc.base_spacing = cfg.base_spacing;
    sc.min_volume = 0.01;
    sc.max_volume = 10.0;
    sc.contract_size = 100.0;
    sc.leverage = 500.0;
    sc.volatility_lookback_hours = 4.0;
    sc.typical_vol_pct = 0.55;
    sc.tp_mode = StrategyCombinedJu::LINEAR;
    sc.tp_sqrt_scale = 0.5;
    sc.tp_linear_scale = 0.3;
    sc.tp_min = 1.0;
    sc.enable_velocity_filter = true;
    sc.velocity_window = 10;
    sc.velocity_threshold_pct = 0.01;
    sc.sizing_mode = StrategyCombinedJu::UNIFORM;
    sc.force_min_volume_entry = false;

    StrategyCombinedJu strategy(sc);
    TickBasedEngine engine(config);

    engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();

    Result r;
    r.name = cfg.name;
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
        std::cout << cfg.name << " | " << r.ret << "x | " << r.max_dd << "% DD | "
                  << r.trades << " trades | $" << r.swap << " swap | "
                  << (r.stopped_out ? "STOP-OUT" : "ok") << std::endl;
    }
}

int main() {
    std::cout << "=== Robust Presets Full Year Test (Jan 1 - Dec 29, 2025) ===" << std::endl;
    std::cout << "All configs: UNIFORM sizing, LINEAR TP, velocity filter ON" << std::endl;
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

    // Define robust configs
    std::vector<Config> configs = {
        {"s12_sp1.0_UNI", 12.0, 1.0},
        {"s12_sp1.5_UNI", 12.0, 1.5},
        {"s13_sp1.5_UNI", 13.0, 1.5},
        {"s15_sp1.5_UNI", 15.0, 1.5},
        {"s15_sp2.0_UNI", 15.0, 2.0},
        {"s18_sp2.0_UNI", 18.0, 2.0},
        {"s18_sp2.5_UNI", 18.0, 2.5},
        {"s20_sp2.5_UNI", 20.0, 2.5},
        {"s20_sp3.0_UNI", 20.0, 3.0},
        {"s25_sp3.0_UNI", 25.0, 3.0},
        {"s25_sp4.0_UNI", 25.0, 4.0},
    };

    std::cout << "Config | Return | MaxDD | Trades | Swap | Status" << std::endl;
    std::cout << "-------|--------|-------|--------|------|--------" << std::endl;

    // Run tests in parallel
    std::vector<std::thread> threads;
    for (const auto& cfg : configs) {
        threads.emplace_back(RunTest, cfg, std::cref(all_ticks));
    }

    for (auto& t : threads) {
        t.join();
    }

    // Summary sorted by return
    std::cout << std::endl;
    std::cout << "=== SUMMARY (sorted by return) ===" << std::endl;
    std::cout << std::endl;

    std::sort(g_results.begin(), g_results.end(),
              [](const Result& a, const Result& b) { return a.ret > b.ret; });

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Config              | Return |  MaxDD | Return/DD | Trades |    Swap" << std::endl;
    std::cout << "--------------------|--------|--------|-----------|--------|--------" << std::endl;

    for (const auto& r : g_results) {
        double return_dd = (r.max_dd > 0) ? (r.ret / (r.max_dd / 100.0)) : 0;
        std::cout << std::left << std::setw(19) << r.name << " | "
                  << std::right << std::setw(5) << r.ret << "x | "
                  << std::setw(5) << r.max_dd << "% | "
                  << std::setw(9) << return_dd << " | "
                  << std::setw(6) << r.trades << " | $"
                  << std::setw(6) << (int)r.swap << std::endl;
    }

    return 0;
}
