#include "../include/strategy_combined_ju.h"
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

std::mutex g_print_mutex;
std::atomic<int> g_completed{0};

struct TestConfig {
    std::string name;
    int threshold_pos;
    double threshold_mult;
    double sqrt_scale;
    double velocity_threshold;
    bool use_combined;  // true = Combined Ju, false = baseline FillUpOscillation
};

struct TestResult {
    std::string name;
    std::string year;
    double final_balance;
    double max_dd_pct;
    int trades;
    bool stopped_out;
};

TestResult RunSingleTest(const TestConfig& tc, double survive, double spacing,
                        double lookback, const std::vector<Tick>& ticks,
                        const std::string& year) {
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

    if (year == "2024") {
        config.start_date = "2024.01.01";
        config.end_date = "2024.12.30";
    } else {
        config.start_date = "2025.01.01";
        config.end_date = "2025.12.30";
    }
    config.verbose = false;

    TickDataConfig tick_config;
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = true;
    config.tick_data_config = tick_config;

    TickBasedEngine engine(config);

    TestResult r;
    r.name = tc.name;
    r.year = year;

    if (tc.use_combined) {
        StrategyCombinedJu::Config strat_cfg;
        strat_cfg.survive_pct = survive;
        strat_cfg.base_spacing = spacing;
        strat_cfg.volatility_lookback_hours = lookback;
        strat_cfg.typical_vol_pct = 0.55;

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
    } else {
        // Baseline FillUpOscillation
        FillUpOscillation strategy(survive, spacing, 0.01, 10.0, 100.0, 500.0,
            FillUpOscillation::ADAPTIVE_SPACING, 0.0, 0.0, lookback);

        engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });
    }

    auto results = engine.GetResults();
    r.final_balance = results.final_balance;
    r.max_dd_pct = results.max_drawdown_pct;
    r.trades = results.total_trades;
    r.stopped_out = results.stop_out_occurred;

    return r;
}

int main() {
    // Load both years of tick data
    std::cout << "Loading 2024 tick data..." << std::endl;
    std::vector<Tick> ticks_2024;
    {
        TickDataConfig tc;
        tc.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAUUSD_TICKS_2024.csv";
        tc.format = TickDataFormat::MT5_CSV;
        tc.load_all_into_memory = true;
        TickDataManager manager(tc);
        Tick tick;
        while (manager.GetNextTick(tick)) {
            ticks_2024.push_back(tick);
        }
    }
    std::cout << "Loaded " << ticks_2024.size() << " ticks (2024)" << std::endl;

    std::cout << "Loading 2025 tick data..." << std::endl;
    std::vector<Tick> ticks_2025;
    {
        TickDataConfig tc;
        tc.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
        tc.format = TickDataFormat::MT5_CSV;
        tc.load_all_into_memory = true;
        TickDataManager manager(tc);
        Tick tick;
        while (manager.GetNextTick(tick)) {
            ticks_2025.push_back(tick);
        }
    }
    std::cout << "Loaded " << ticks_2025.size() << " ticks (2025)" << std::endl;

    double survive = 13.0;
    double spacing = 1.50;
    double lookback = 4.0;

    // Test configurations
    std::vector<TestConfig> configs = {
        // Baseline FillUpOscillation
        {"BASELINE", 0, 0.0, 0.0, 0.0, false},

        // Combined Ju - various settings
        {"COMBINED_P1_M3", 1, 3.0, 0.5, 0.01, true},
        {"COMBINED_P3_M2.5", 3, 2.5, 0.5, 0.01, true},
        {"COMBINED_P5_M2", 5, 2.0, 0.5, 0.01, true},
        {"COMBINED_P1_M2", 1, 2.0, 0.5, 0.01, true},
        {"COMBINED_P3_M3", 3, 3.0, 0.5, 0.01, true},
    };

    std::cout << "\n=== COMBINED JU STRATEGY REGIME VALIDATION ===" << std::endl;
    std::cout << "Testing on 2024 and 2025 data\n" << std::endl;

    // Run all tests
    std::vector<TestResult> results;

    for (const auto& cfg : configs) {
        std::cout << "Testing " << cfg.name << "..." << std::endl;

        // Run on 2024
        TestResult r2024 = RunSingleTest(cfg, survive, spacing, lookback, ticks_2024, "2024");
        results.push_back(r2024);

        // Run on 2025
        TestResult r2025 = RunSingleTest(cfg, survive, spacing, lookback, ticks_2025, "2025");
        results.push_back(r2025);

        std::cout << "  2024: " << std::fixed << std::setprecision(2)
                  << r2024.final_balance/10000.0 << "x, "
                  << r2024.max_dd_pct << "% DD" << std::endl;
        std::cout << "  2025: " << r2025.final_balance/10000.0 << "x, "
                  << r2025.max_dd_pct << "% DD" << std::endl;
    }

    // Print comparison table
    std::cout << "\n=== REGIME COMPARISON TABLE ===" << std::endl;
    std::cout << std::left << std::setw(18) << "Config"
              << std::right << std::setw(10) << "2024"
              << std::setw(8) << "DD%"
              << std::setw(10) << "2025"
              << std::setw(8) << "DD%"
              << std::setw(10) << "Ratio"
              << std::setw(12) << "Combined" << std::endl;
    std::cout << std::string(76, '-') << std::endl;

    for (size_t i = 0; i < configs.size(); i++) {
        const auto& cfg = configs[i];
        const auto& r2024 = results[i * 2];
        const auto& r2025 = results[i * 2 + 1];

        double ret_2024 = r2024.final_balance / 10000.0;
        double ret_2025 = r2025.final_balance / 10000.0;
        double ratio = ret_2025 / ret_2024;
        double combined = ret_2024 * ret_2025;  // Sequential (2024 → 2025)

        std::string status = "";
        if (r2024.stopped_out || r2025.stopped_out) {
            status = "SO";
        } else if (ratio < 2.5) {
            status = "STABLE";
        } else if (ratio < 4.0) {
            status = "OK";
        } else {
            status = "REGIME-DEP";
        }

        std::cout << std::left << std::setw(18) << cfg.name
                  << std::right << std::fixed
                  << std::setw(9) << std::setprecision(2) << ret_2024 << "x"
                  << std::setw(7) << std::setprecision(1) << r2024.max_dd_pct << "%"
                  << std::setw(9) << std::setprecision(2) << ret_2025 << "x"
                  << std::setw(7) << std::setprecision(1) << r2025.max_dd_pct << "%"
                  << std::setw(9) << std::setprecision(2) << ratio << "x"
                  << std::setw(10) << std::setprecision(1) << combined << "x"
                  << "  " << status << std::endl;
    }

    // Summary statistics
    std::cout << "\n=== SUMMARY ===" << std::endl;

    double baseline_ratio = 0.0;
    double baseline_combined = 0.0;
    for (size_t i = 0; i < configs.size(); i++) {
        if (configs[i].name == "BASELINE") {
            double r2024 = results[i * 2].final_balance / 10000.0;
            double r2025 = results[i * 2 + 1].final_balance / 10000.0;
            baseline_ratio = r2025 / r2024;
            baseline_combined = r2024 * r2025;
            break;
        }
    }

    std::cout << "Baseline 2025/2024 ratio: " << std::fixed << std::setprecision(2)
              << baseline_ratio << "x" << std::endl;
    std::cout << "Baseline combined return: " << baseline_combined << "x\n" << std::endl;

    std::cout << "Interpretation:" << std::endl;
    std::cout << "  Ratio < 2.5x  = Regime stable (good)" << std::endl;
    std::cout << "  Ratio 2.5-4x  = Acceptable" << std::endl;
    std::cout << "  Ratio > 4x    = Regime dependent (caution)" << std::endl;

    std::cout << "\nNote: Lower ratio = more consistent across market regimes" << std::endl;
    std::cout << "2024 was moderate trend (+27%), 2025 was strong bull (+60%)" << std::endl;

    return 0;
}
