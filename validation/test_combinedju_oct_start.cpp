#include "../include/strategy_combined_ju.h"
#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <thread>
#include <mutex>
#include <queue>

using namespace backtest;

std::mutex g_print_mutex;
std::mutex g_results_mutex;

struct TestConfig {
    std::string name;
    double survive_pct;
    double base_spacing;
    StrategyCombinedJu::TPMode tp_mode;
    StrategyCombinedJu::SizingMode sizing_mode;
    int threshold_pos;
    double threshold_mult;
    bool velocity_filter;
};

struct TestResult {
    std::string name;
    double final_balance;
    double max_dd_pct;
    int trades;
    bool stopped_out;
    std::string notes;
};

std::vector<TestResult> g_results;

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
    config.start_date = "2025.10.17";
    config.end_date = "2025.12.29";

    StrategyCombinedJu::Config sc;
    sc.survive_pct = tc.survive_pct;
    sc.base_spacing = tc.base_spacing;
    sc.min_volume = 0.01;
    sc.max_volume = 10.0;
    sc.contract_size = 100.0;
    sc.leverage = 500.0;
    sc.volatility_lookback_hours = 4.0;
    sc.typical_vol_pct = 0.55;
    sc.tp_mode = tc.tp_mode;
    sc.tp_sqrt_scale = 0.5;
    sc.tp_linear_scale = 0.3;
    sc.tp_min = 1.0;
    sc.enable_velocity_filter = tc.velocity_filter;
    sc.velocity_window = 10;
    sc.velocity_threshold_pct = 0.01;
    sc.sizing_mode = tc.sizing_mode;
    sc.sizing_linear_scale = 0.5;
    sc.sizing_threshold_pos = tc.threshold_pos;
    sc.sizing_threshold_mult = tc.threshold_mult;
    sc.force_min_volume_entry = false;  // Always OFF for crash safety

    StrategyCombinedJu strategy(sc);
    TickBasedEngine engine(config);

    engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();

    TestResult tr;
    tr.name = tc.name;
    tr.final_balance = results.final_balance;
    tr.max_dd_pct = results.max_drawdown_pct;
    tr.trades = results.total_trades;
    tr.stopped_out = results.final_balance < 1000.0;

    double ret = results.final_balance / 10000.0;

    {
        std::lock_guard<std::mutex> lock(g_print_mutex);
        std::cout << std::fixed << std::setprecision(2);
        std::cout << tc.name << " | "
                  << ret << "x | "
                  << tr.max_dd_pct << "% DD | "
                  << tr.trades << " trades | "
                  << (tr.stopped_out ? "STOP-OUT" : "ok") << std::endl;
    }

    {
        std::lock_guard<std::mutex> lock(g_results_mutex);
        g_results.push_back(tr);
    }
}

int main() {
    std::cout << "=== CombinedJu October 2025 Start Test ===" << std::endl;
    std::cout << "Testing if presets survive starting from 2025.10.17" << std::endl;
    std::cout << std::endl;

    // Load tick data
    std::cout << "Loading tick data..." << std::endl;
    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = true;

    TickDataManager manager(tick_config);
    std::vector<Tick> all_ticks;
    Tick tick;
    while (manager.GetNextTick(tick)) {
        // Filter to October 17 onwards
        if (tick.timestamp >= "2025.10.17") {
            all_ticks.push_back(tick);
        }
    }
    std::cout << "Loaded " << all_ticks.size() << " ticks from 2025.10.17 onwards" << std::endl;
    std::cout << std::endl;

    // Define test configurations - current presets
    std::vector<TestConfig> tests = {
        // Current presets
        {"Best_s12_sp1.0_p1_m2.0", 12.0, 1.0, StrategyCombinedJu::LINEAR, StrategyCombinedJu::THRESHOLD_SIZING, 1, 2.0, true},
        {"VelFilter_s12_sp1.0_p2_m2.0", 12.0, 1.0, StrategyCombinedJu::LINEAR, StrategyCombinedJu::THRESHOLD_SIZING, 2, 2.0, true},
        {"THR3_s12_sp1.0_p3_m2.0", 12.0, 1.0, StrategyCombinedJu::LINEAR, StrategyCombinedJu::THRESHOLD_SIZING, 3, 2.0, true},
        {"LinearTP_s12_sp1.0_UNI", 12.0, 1.0, StrategyCombinedJu::LINEAR, StrategyCombinedJu::UNIFORM, 5, 2.0, true},
        {"THR5_s12_sp1.0_p5_m2.0", 12.0, 1.0, StrategyCombinedJu::LINEAR, StrategyCombinedJu::THRESHOLD_SIZING, 5, 2.0, true},
        {"Conservative_s13_sp1.5", 13.0, 1.5, StrategyCombinedJu::LINEAR, StrategyCombinedJu::UNIFORM, 5, 2.0, true},

        // More conservative variants to test
        {"s15_sp1.5_UNI", 15.0, 1.5, StrategyCombinedJu::LINEAR, StrategyCombinedJu::UNIFORM, 5, 2.0, true},
        {"s18_sp2.0_UNI", 18.0, 2.0, StrategyCombinedJu::LINEAR, StrategyCombinedJu::UNIFORM, 5, 2.0, true},
        {"s20_sp2.5_UNI", 20.0, 2.5, StrategyCombinedJu::LINEAR, StrategyCombinedJu::UNIFORM, 5, 2.0, true},
        {"s25_sp3.0_UNI", 25.0, 3.0, StrategyCombinedJu::LINEAR, StrategyCombinedJu::UNIFORM, 5, 2.0, true},

        // Higher survive with tight spacing
        {"s15_sp1.0_UNI", 15.0, 1.0, StrategyCombinedJu::LINEAR, StrategyCombinedJu::UNIFORM, 5, 2.0, true},
        {"s18_sp1.0_UNI", 18.0, 1.0, StrategyCombinedJu::LINEAR, StrategyCombinedJu::UNIFORM, 5, 2.0, true},
        {"s20_sp1.0_UNI", 20.0, 1.0, StrategyCombinedJu::LINEAR, StrategyCombinedJu::UNIFORM, 5, 2.0, true},
        {"s25_sp1.0_UNI", 25.0, 1.0, StrategyCombinedJu::LINEAR, StrategyCombinedJu::UNIFORM, 5, 2.0, true},

        // FillUpOscillation ADAPTIVE equivalent
        {"s13_sp1.5_UNI_noVel", 13.0, 1.5, StrategyCombinedJu::FIXED, StrategyCombinedJu::UNIFORM, 5, 2.0, false},
    };

    std::cout << "Config | Return | MaxDD | Trades | Status" << std::endl;
    std::cout << "-------|--------|-------|--------|--------" << std::endl;

    // Run tests in parallel
    std::vector<std::thread> threads;
    for (const auto& tc : tests) {
        threads.emplace_back(RunTest, tc, std::cref(all_ticks));
    }

    for (auto& t : threads) {
        t.join();
    }

    // Summary
    std::cout << std::endl;
    std::cout << "=== SUMMARY ===" << std::endl;

    int stopped_out = 0;
    int survived = 0;
    TestResult best_survivor = {"", 0.0, 100.0, 0, true, ""};

    for (const auto& r : g_results) {
        if (r.stopped_out) {
            stopped_out++;
        } else {
            survived++;
            if (r.final_balance > best_survivor.final_balance) {
                best_survivor = r;
            }
        }
    }

    std::cout << "Stopped out: " << stopped_out << "/" << g_results.size() << std::endl;
    std::cout << "Survived: " << survived << "/" << g_results.size() << std::endl;

    if (survived > 0) {
        std::cout << std::endl;
        std::cout << "Best survivor: " << best_survivor.name << std::endl;
        std::cout << "  Return: " << std::fixed << std::setprecision(2)
                  << best_survivor.final_balance / 10000.0 << "x" << std::endl;
        std::cout << "  MaxDD: " << best_survivor.max_dd_pct << "%" << std::endl;
    }

    return 0;
}
