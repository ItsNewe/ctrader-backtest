/**
 * test_forced_entry_validation.cpp
 *
 * Validate the forced entry discovery:
 * 1. Compare forced entry ON vs OFF
 * 2. Test on both 2024 and 2025 to check for overfitting
 * 3. Measure regime independence (2024/2025 ratio)
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

using namespace backtest;

std::vector<Tick> g_ticks_2024;
std::vector<Tick> g_ticks_2025;
std::mutex g_mutex;

struct TestConfig {
    std::string name;
    bool force_min_volume_entry;
    int max_positions;
    double margin_level_floor;
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
};

void LoadTicks2024() {
    std::cout << "Loading 2024 tick data..." << std::endl;
    TickDataConfig cfg;
    cfg.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAUUSD_TICKS_2024.csv";
    cfg.format = TickDataFormat::MT5_CSV;
    TickDataManager mgr(cfg);
    Tick tick;
    while (mgr.GetNextTick(tick)) {
        g_ticks_2024.push_back(tick);
    }
    std::cout << "Loaded " << g_ticks_2024.size() << " ticks (2024)" << std::endl;
}

void LoadTicks2025() {
    std::cout << "Loading 2025 tick data..." << std::endl;
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
    std::cout << "Loaded " << g_ticks_2025.size() << " ticks (2025+Jan2026)" << std::endl;
}

TestResult RunTest(const TestConfig& cfg, const std::vector<Tick>& ticks,
                   const std::string& year, const std::string& start_date, const std::string& end_date) {
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
    result.year = year;
    result.final_balance = results.final_balance;
    result.return_multiple = results.final_balance / 10000.0;
    result.max_dd_pct = results.max_drawdown_pct;
    result.total_trades = results.total_trades;
    result.forced_entries = stats.forced_entries;
    result.max_pos_blocks = stats.max_position_blocks;
    result.peak_positions = stats.peak_positions;

    return result;
}

int main() {
    std::cout << "=" << std::string(80, '=') << std::endl;
    std::cout << "FORCED ENTRY VALIDATION TEST" << std::endl;
    std::cout << "Testing forced entry ON vs OFF across 2024 and 2025" << std::endl;
    std::cout << "=" << std::string(80, '=') << std::endl << std::endl;

    // Load tick data
    LoadTicks2024();
    LoadTicks2025();

    // Test configurations
    std::vector<TestConfig> configs = {
        // Baseline: forced entry OFF (original behavior)
        {"FORCE_OFF", false, 0, 0},

        // Forced entry ON (the discovery)
        {"FORCE_ON", true, 0, 0},

        // Forced entry ON + max positions cap
        {"FORCE_ON_MAX200", true, 200, 0},
        {"FORCE_ON_MAX150", true, 150, 0},
        {"FORCE_ON_MAX100", true, 100, 0},

        // Forced entry ON + margin floor
        {"FORCE_ON_MARGIN100", true, 0, 100.0},
        {"FORCE_ON_MARGIN150", true, 0, 150.0},

        // Combined
        {"FORCE_ON_MAX200_MARGIN100", true, 200, 100.0},
    };

    std::vector<TestResult> all_results;

    std::cout << "\nRunning tests..." << std::endl;
    std::cout << std::string(80, '-') << std::endl;

    int total = configs.size() * 2;  // 2 years
    int completed = 0;

    for (const auto& cfg : configs) {
        // Test 2024
        auto result_2024 = RunTest(cfg, g_ticks_2024, "2024", "2024.01.01", "2024.12.31");
        all_results.push_back(result_2024);
        completed++;
        std::cout << "  [" << completed << "/" << total << "] " << cfg.name << " 2024: "
                  << std::fixed << std::setprecision(2) << result_2024.return_multiple << "x" << std::endl;

        // Test 2025
        auto result_2025 = RunTest(cfg, g_ticks_2025, "2025", "2025.01.01", "2026.01.27");
        all_results.push_back(result_2025);
        completed++;
        std::cout << "  [" << completed << "/" << total << "] " << cfg.name << " 2025: "
                  << std::fixed << std::setprecision(2) << result_2025.return_multiple << "x" << std::endl;
    }

    // Print results table
    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "RESULTS SUMMARY" << std::endl;
    std::cout << std::string(120, '=') << std::endl;

    std::cout << std::left << std::setw(25) << "Config"
              << std::right << std::setw(8) << "Year"
              << std::setw(10) << "Return"
              << std::setw(10) << "MaxDD"
              << std::setw(10) << "Trades"
              << std::setw(12) << "Forced"
              << std::setw(12) << "MaxPosBlk"
              << std::setw(10) << "PeakPos"
              << std::endl;
    std::cout << std::string(120, '-') << std::endl;

    for (const auto& r : all_results) {
        std::cout << std::left << std::setw(25) << r.name
                  << std::right << std::setw(8) << r.year
                  << std::setw(9) << std::fixed << std::setprecision(2) << r.return_multiple << "x"
                  << std::setw(9) << r.max_dd_pct << "%"
                  << std::setw(10) << r.total_trades
                  << std::setw(12) << r.forced_entries
                  << std::setw(12) << r.max_pos_blocks
                  << std::setw(10) << r.peak_positions
                  << std::endl;
    }

    // Regime independence analysis
    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "REGIME INDEPENDENCE ANALYSIS (2025/2024 Ratio)" << std::endl;
    std::cout << std::string(120, '=') << std::endl;
    std::cout << "Lower ratio = more regime-independent (ideal ~1.0-2.0)" << std::endl;
    std::cout << std::string(120, '-') << std::endl;

    std::cout << std::left << std::setw(25) << "Config"
              << std::right << std::setw(12) << "2024 Return"
              << std::setw(12) << "2025 Return"
              << std::setw(12) << "Ratio"
              << std::setw(12) << "2024 DD"
              << std::setw(12) << "2025 DD"
              << std::setw(15) << "2-Year Seq"
              << std::endl;
    std::cout << std::string(120, '-') << std::endl;

    for (size_t i = 0; i < configs.size(); i++) {
        const auto& r2024 = all_results[i * 2];
        const auto& r2025 = all_results[i * 2 + 1];
        double ratio = r2025.return_multiple / r2024.return_multiple;
        double two_year = r2024.return_multiple * r2025.return_multiple;

        std::cout << std::left << std::setw(25) << configs[i].name
                  << std::right << std::setw(11) << std::fixed << std::setprecision(2) << r2024.return_multiple << "x"
                  << std::setw(11) << r2025.return_multiple << "x"
                  << std::setw(11) << ratio << "x"
                  << std::setw(11) << r2024.max_dd_pct << "%"
                  << std::setw(11) << r2025.max_dd_pct << "%"
                  << std::setw(14) << two_year << "x"
                  << std::endl;
    }

    // Key comparisons
    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "KEY COMPARISONS" << std::endl;
    std::cout << std::string(120, '=') << std::endl;

    // Find FORCE_OFF and FORCE_ON results
    TestResult* force_off_2024 = nullptr;
    TestResult* force_off_2025 = nullptr;
    TestResult* force_on_2024 = nullptr;
    TestResult* force_on_2025 = nullptr;

    for (auto& r : all_results) {
        if (r.name == "FORCE_OFF" && r.year == "2024") force_off_2024 = &r;
        if (r.name == "FORCE_OFF" && r.year == "2025") force_off_2025 = &r;
        if (r.name == "FORCE_ON" && r.year == "2024") force_on_2024 = &r;
        if (r.name == "FORCE_ON" && r.year == "2025") force_on_2025 = &r;
    }

    if (force_off_2024 && force_on_2024 && force_off_2025 && force_on_2025) {
        std::cout << "\nForced Entry Impact:" << std::endl;
        std::cout << "  2024: " << force_off_2024->return_multiple << "x -> " << force_on_2024->return_multiple << "x"
                  << " (" << std::showpos << ((force_on_2024->return_multiple / force_off_2024->return_multiple - 1) * 100)
                  << std::noshowpos << "%)" << std::endl;
        std::cout << "  2025: " << force_off_2025->return_multiple << "x -> " << force_on_2025->return_multiple << "x"
                  << " (" << std::showpos << ((force_on_2025->return_multiple / force_off_2025->return_multiple - 1) * 100)
                  << std::noshowpos << "%)" << std::endl;

        double ratio_off = force_off_2025->return_multiple / force_off_2024->return_multiple;
        double ratio_on = force_on_2025->return_multiple / force_on_2024->return_multiple;
        std::cout << "\nRegime Ratio Change:" << std::endl;
        std::cout << "  FORCE_OFF: " << ratio_off << "x (2025/2024)" << std::endl;
        std::cout << "  FORCE_ON:  " << ratio_on << "x (2025/2024)" << std::endl;

        if (std::abs(ratio_on - ratio_off) < 0.5) {
            std::cout << "\n>>> GOOD: Forced entry does NOT increase regime dependence" << std::endl;
        } else if (ratio_on > ratio_off) {
            std::cout << "\n>>> WARNING: Forced entry may increase regime dependence" << std::endl;
        }

        std::cout << "\nForced Entry Stats (2025):" << std::endl;
        std::cout << "  Entries forced at MinVolume: " << force_on_2025->forced_entries << std::endl;
        std::cout << "  Peak positions: " << force_on_2025->peak_positions << std::endl;
    }

    return 0;
}
