/**
 * test_forced_entry_features.cpp
 *
 * Comprehensive test: Forced entry across different:
 * 1. Survive percentages (12%, 13%)
 * 2. Strategy modes/features (BASELINE, ADAPTIVE_SPACING, etc.)
 * 3. Years (2024, 2025)
 *
 * Goal: Determine if forced entry benefits all configurations equally
 * or is mode-specific.
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
#include <atomic>
#include <chrono>
#include <map>

using namespace backtest;

// Shared tick data - loaded ONCE
std::vector<Tick> g_ticks_2024;
std::vector<Tick> g_ticks_2025;

struct TestConfig {
    std::string name;
    double survive_pct;
    FillUpOscillation::Mode mode;
    bool force_entry;
    std::string year;
};

struct TestResult {
    std::string name;
    double survive_pct;
    std::string mode_name;
    bool force_entry;
    std::string year;
    double final_balance;
    double return_multiple;
    double max_dd_pct;
    int total_trades;
    long forced_entries;
    int peak_positions;
};

// Thread-safe work queue
std::mutex g_queue_mutex;
std::mutex g_results_mutex;
std::mutex g_print_mutex;
std::queue<TestConfig> g_work_queue;
std::vector<TestResult> g_results;
std::atomic<int> g_completed(0);
int g_total_tasks = 0;

std::string ModeToString(FillUpOscillation::Mode mode) {
    switch (mode) {
        case FillUpOscillation::BASELINE: return "BASELINE";
        case FillUpOscillation::ADAPTIVE_SPACING: return "ADAPTIVE";
        case FillUpOscillation::ANTIFRAGILE: return "ANTIFRAG";
        case FillUpOscillation::VELOCITY_FILTER: return "VELOCITY";
        case FillUpOscillation::ALL_COMBINED: return "ALL_COMB";
        case FillUpOscillation::ADAPTIVE_LOOKBACK: return "ADAPT_LB";
        case FillUpOscillation::DOUBLE_ADAPTIVE: return "DBL_ADPT";
        case FillUpOscillation::TREND_ADAPTIVE: return "TREND";
        default: return "UNKNOWN";
    }
}

void LoadTickData() {
    auto start = std::chrono::high_resolution_clock::now();

    // Load 2024
    std::cout << "Loading 2024 tick data..." << std::endl;
    {
        TickDataConfig cfg;
        cfg.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAUUSD_TICKS_2024.csv";
        cfg.format = TickDataFormat::MT5_CSV;
        TickDataManager mgr(cfg);
        Tick tick;
        while (mgr.GetNextTick(tick)) {
            g_ticks_2024.push_back(tick);
        }
    }
    std::cout << "  Loaded " << g_ticks_2024.size() << " ticks (2024)" << std::endl;

    // Load 2025+Jan2026
    std::cout << "Loading 2025 tick data..." << std::endl;
    {
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
    }
    std::cout << "  Loaded " << g_ticks_2025.size() << " ticks (2025+Jan2026)" << std::endl;

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();
    std::cout << "Total load time: " << duration << "s" << std::endl;
}

TestResult RunTest(const TestConfig& cfg) {
    const auto& ticks = (cfg.year == "2024") ? g_ticks_2024 : g_ticks_2025;
    std::string start_date = (cfg.year == "2024") ? "2024.01.01" : "2025.01.01";
    std::string end_date = (cfg.year == "2024") ? "2024.12.31" : "2026.01.27";

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
    config.verbose = false;  // Disable trade logging for speed

    TickDataConfig tick_cfg;
    tick_cfg.format = TickDataFormat::MT5_CSV;
    config.tick_data_config = tick_cfg;

    TickBasedEngine engine(config);

    // Configure safety
    FillUpOscillation::SafetyConfig safety;
    safety.force_min_volume_entry = cfg.force_entry;
    safety.max_positions = 0;  // Unlimited for this comparison
    safety.margin_level_floor = 0;
    safety.equity_stop_pct = 0;

    FillUpOscillation::AdaptiveConfig adaptive;
    adaptive.typical_vol_pct = 0.55;

    FillUpOscillation strategy(
        cfg.survive_pct,
        1.5,    // base_spacing
        0.01,   // min_volume
        10.0,   // max_volume
        100.0,  // contract_size
        500.0,  // leverage
        cfg.mode,
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
    result.survive_pct = cfg.survive_pct;
    result.mode_name = ModeToString(cfg.mode);
    result.force_entry = cfg.force_entry;
    result.year = cfg.year;
    result.final_balance = results.final_balance;
    result.return_multiple = results.final_balance / 10000.0;
    result.max_dd_pct = results.max_drawdown_pct;
    result.total_trades = results.total_trades;
    result.forced_entries = stats.forced_entries;
    result.peak_positions = stats.peak_positions;

    return result;
}

void Worker() {
    while (true) {
        TestConfig task;
        {
            std::lock_guard<std::mutex> lock(g_queue_mutex);
            if (g_work_queue.empty()) {
                return;
            }
            task = g_work_queue.front();
            g_work_queue.pop();
        }

        TestResult result = RunTest(task);

        {
            std::lock_guard<std::mutex> lock(g_results_mutex);
            g_results.push_back(result);
        }

        int done = ++g_completed;
        {
            std::lock_guard<std::mutex> lock(g_print_mutex);
            std::cout << "\r  [" << done << "/" << g_total_tasks << "] "
                      << task.name << " " << task.year
                      << std::string(30, ' ') << std::flush;
        }
    }
}

int main() {
    std::cout << std::string(100, '=') << std::endl;
    std::cout << "FORCED ENTRY FEATURE COMPARISON TEST" << std::endl;
    std::cout << "Testing across survive%, modes, and years" << std::endl;
    std::cout << std::string(100, '=') << std::endl << std::endl;

    // Load tick data once
    LoadTickData();

    // Define test parameters
    std::vector<double> survive_pcts = {12.0, 13.0};
    std::vector<FillUpOscillation::Mode> modes = {
        FillUpOscillation::BASELINE,
        FillUpOscillation::ADAPTIVE_SPACING,
        FillUpOscillation::ANTIFRAGILE,
        FillUpOscillation::VELOCITY_FILTER,
        FillUpOscillation::ALL_COMBINED,
        FillUpOscillation::ADAPTIVE_LOOKBACK,
        FillUpOscillation::DOUBLE_ADAPTIVE,
        FillUpOscillation::TREND_ADAPTIVE
    };
    std::vector<bool> force_options = {false, true};
    std::vector<std::string> years = {"2024", "2025"};

    // Build work queue
    for (double survive : survive_pcts) {
        for (auto mode : modes) {
            for (bool force : force_options) {
                for (const auto& year : years) {
                    TestConfig cfg;
                    cfg.survive_pct = survive;
                    cfg.mode = mode;
                    cfg.force_entry = force;
                    cfg.year = year;
                    cfg.name = "s" + std::to_string((int)survive) + "_" +
                               ModeToString(mode) + "_" +
                               (force ? "FORCE" : "NOFORCE");
                    g_work_queue.push(cfg);
                }
            }
        }
    }

    g_total_tasks = g_work_queue.size();
    std::cout << "\nRunning " << g_total_tasks << " tests in parallel..." << std::endl;
    std::cout << "  " << survive_pcts.size() << " survive% × "
              << modes.size() << " modes × "
              << force_options.size() << " force × "
              << years.size() << " years" << std::endl;

    auto start = std::chrono::high_resolution_clock::now();

    // Launch worker threads
    unsigned int num_threads = std::thread::hardware_concurrency();
    std::cout << "Using " << num_threads << " threads" << std::endl;

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < num_threads; i++) {
        threads.emplace_back(Worker);
    }

    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();

    std::cout << "\n\nCompleted in " << duration << "s ("
              << std::fixed << std::setprecision(2)
              << (double)duration / g_total_tasks << "s/config)" << std::endl;

    // ============================================================
    // RESULTS ANALYSIS
    // ============================================================

    std::cout << "\n" << std::string(140, '=') << std::endl;
    std::cout << "FULL RESULTS" << std::endl;
    std::cout << std::string(140, '=') << std::endl;

    std::cout << std::left << std::setw(12) << "Survive"
              << std::setw(12) << "Mode"
              << std::setw(10) << "Force"
              << std::setw(8) << "Year"
              << std::right << std::setw(10) << "Return"
              << std::setw(10) << "MaxDD"
              << std::setw(10) << "Trades"
              << std::setw(12) << "ForcedEnt"
              << std::setw(10) << "PeakPos"
              << std::endl;
    std::cout << std::string(140, '-') << std::endl;

    // Sort results for organized output
    std::sort(g_results.begin(), g_results.end(), [](const TestResult& a, const TestResult& b) {
        if (a.survive_pct != b.survive_pct) return a.survive_pct < b.survive_pct;
        if (a.mode_name != b.mode_name) return a.mode_name < b.mode_name;
        if (a.force_entry != b.force_entry) return !a.force_entry;  // NOFORCE first
        return a.year < b.year;
    });

    for (const auto& r : g_results) {
        std::cout << std::left << std::setw(12) << (std::to_string((int)r.survive_pct) + "%")
                  << std::setw(12) << r.mode_name
                  << std::setw(10) << (r.force_entry ? "ON" : "OFF")
                  << std::setw(8) << r.year
                  << std::right << std::setw(9) << std::fixed << std::setprecision(2) << r.return_multiple << "x"
                  << std::setw(9) << r.max_dd_pct << "%"
                  << std::setw(10) << r.total_trades
                  << std::setw(12) << r.forced_entries
                  << std::setw(10) << r.peak_positions
                  << std::endl;
    }

    // ============================================================
    // FORCED ENTRY IMPACT BY MODE
    // ============================================================

    std::cout << "\n" << std::string(140, '=') << std::endl;
    std::cout << "FORCED ENTRY IMPACT BY MODE (2-year performance)" << std::endl;
    std::cout << std::string(140, '=') << std::endl;

    std::cout << std::left << std::setw(12) << "Survive"
              << std::setw(12) << "Mode"
              << std::right << std::setw(15) << "NOFORCE 2yr"
              << std::setw(15) << "FORCE 2yr"
              << std::setw(12) << "Improvement"
              << std::setw(15) << "NOFORCE MaxDD"
              << std::setw(15) << "FORCE MaxDD"
              << std::setw(12) << "DD Change"
              << std::endl;
    std::cout << std::string(140, '-') << std::endl;

    // Group results for comparison
    std::map<std::string, std::vector<TestResult>> grouped;
    for (const auto& r : g_results) {
        std::string key = std::to_string((int)r.survive_pct) + "_" + r.mode_name;
        grouped[key].push_back(r);
    }

    for (const auto& kv : grouped) {
        // Find the 4 results: NOFORCE 2024, NOFORCE 2025, FORCE 2024, FORCE 2025
        double noforce_2024 = 0, noforce_2025 = 0, force_2024 = 0, force_2025 = 0;
        double noforce_dd_2024 = 0, noforce_dd_2025 = 0, force_dd_2024 = 0, force_dd_2025 = 0;
        double survive = 0;
        std::string mode_name;

        for (const auto& r : kv.second) {
            survive = r.survive_pct;
            mode_name = r.mode_name;
            if (!r.force_entry && r.year == "2024") { noforce_2024 = r.return_multiple; noforce_dd_2024 = r.max_dd_pct; }
            if (!r.force_entry && r.year == "2025") { noforce_2025 = r.return_multiple; noforce_dd_2025 = r.max_dd_pct; }
            if (r.force_entry && r.year == "2024") { force_2024 = r.return_multiple; force_dd_2024 = r.max_dd_pct; }
            if (r.force_entry && r.year == "2025") { force_2025 = r.return_multiple; force_dd_2025 = r.max_dd_pct; }
        }

        double noforce_2yr = noforce_2024 * noforce_2025;
        double force_2yr = force_2024 * force_2025;
        double improvement = (noforce_2yr > 0) ? ((force_2yr / noforce_2yr - 1) * 100) : 0;
        double noforce_max_dd = std::max(noforce_dd_2024, noforce_dd_2025);
        double force_max_dd = std::max(force_dd_2024, force_dd_2025);
        double dd_change = force_max_dd - noforce_max_dd;

        std::cout << std::left << std::setw(12) << (std::to_string((int)survive) + "%")
                  << std::setw(12) << mode_name
                  << std::right << std::setw(14) << std::fixed << std::setprecision(2) << noforce_2yr << "x"
                  << std::setw(14) << force_2yr << "x"
                  << std::setw(11) << std::showpos << improvement << "%" << std::noshowpos
                  << std::setw(14) << noforce_max_dd << "%"
                  << std::setw(14) << force_max_dd << "%"
                  << std::setw(11) << std::showpos << dd_change << "%" << std::noshowpos
                  << std::endl;
    }

    // ============================================================
    // BEST CONFIGURATIONS
    // ============================================================

    std::cout << "\n" << std::string(140, '=') << std::endl;
    std::cout << "BEST CONFIGURATIONS" << std::endl;
    std::cout << std::string(140, '=') << std::endl;

    // Find best by 2-year return
    double best_2yr = 0;
    std::string best_2yr_config;
    for (const auto& kv : grouped) {
        double force_2024 = 0, force_2025 = 0;
        for (const auto& r : kv.second) {
            if (r.force_entry && r.year == "2024") force_2024 = r.return_multiple;
            if (r.force_entry && r.year == "2025") force_2025 = r.return_multiple;
        }
        double two_yr = force_2024 * force_2025;
        if (two_yr > best_2yr) {
            best_2yr = two_yr;
            best_2yr_config = kv.first;
        }
    }
    std::cout << "Best 2-year (FORCE ON): " << best_2yr_config << " = " << best_2yr << "x" << std::endl;

    // Find best improvement from forced entry
    double best_improvement = 0;
    std::string best_improvement_config;
    for (const auto& kv : grouped) {
        double noforce_2024 = 0, noforce_2025 = 0, force_2024 = 0, force_2025 = 0;
        for (const auto& r : kv.second) {
            if (!r.force_entry && r.year == "2024") noforce_2024 = r.return_multiple;
            if (!r.force_entry && r.year == "2025") noforce_2025 = r.return_multiple;
            if (r.force_entry && r.year == "2024") force_2024 = r.return_multiple;
            if (r.force_entry && r.year == "2025") force_2025 = r.return_multiple;
        }
        double noforce_2yr = noforce_2024 * noforce_2025;
        double force_2yr = force_2024 * force_2025;
        double improvement = (noforce_2yr > 0) ? ((force_2yr / noforce_2yr - 1) * 100) : 0;
        if (improvement > best_improvement) {
            best_improvement = improvement;
            best_improvement_config = kv.first;
        }
    }
    std::cout << "Most improved by forced entry: " << best_improvement_config
              << " = +" << best_improvement << "%" << std::endl;

    // ============================================================
    // SURVIVE 12% vs 13% COMPARISON
    // ============================================================

    std::cout << "\n" << std::string(140, '=') << std::endl;
    std::cout << "SURVIVE 12% vs 13% COMPARISON (with FORCE ON)" << std::endl;
    std::cout << std::string(140, '=') << std::endl;

    std::cout << std::left << std::setw(12) << "Mode"
              << std::right << std::setw(12) << "12% 2024"
              << std::setw(12) << "12% 2025"
              << std::setw(12) << "12% 2yr"
              << std::setw(12) << "12% MaxDD"
              << std::setw(12) << "13% 2024"
              << std::setw(12) << "13% 2025"
              << std::setw(12) << "13% 2yr"
              << std::setw(12) << "13% MaxDD"
              << std::endl;
    std::cout << std::string(140, '-') << std::endl;

    for (auto mode : modes) {
        std::string mode_name = ModeToString(mode);
        double s12_2024 = 0, s12_2025 = 0, s12_dd = 0;
        double s13_2024 = 0, s13_2025 = 0, s13_dd = 0;

        for (const auto& r : g_results) {
            if (r.mode_name == mode_name && r.force_entry) {
                if (r.survive_pct == 12.0 && r.year == "2024") { s12_2024 = r.return_multiple; s12_dd = std::max(s12_dd, r.max_dd_pct); }
                if (r.survive_pct == 12.0 && r.year == "2025") { s12_2025 = r.return_multiple; s12_dd = std::max(s12_dd, r.max_dd_pct); }
                if (r.survive_pct == 13.0 && r.year == "2024") { s13_2024 = r.return_multiple; s13_dd = std::max(s13_dd, r.max_dd_pct); }
                if (r.survive_pct == 13.0 && r.year == "2025") { s13_2025 = r.return_multiple; s13_dd = std::max(s13_dd, r.max_dd_pct); }
            }
        }

        std::cout << std::left << std::setw(12) << mode_name
                  << std::right << std::setw(11) << std::fixed << std::setprecision(2) << s12_2024 << "x"
                  << std::setw(11) << s12_2025 << "x"
                  << std::setw(11) << (s12_2024 * s12_2025) << "x"
                  << std::setw(11) << s12_dd << "%"
                  << std::setw(11) << s13_2024 << "x"
                  << std::setw(11) << s13_2025 << "x"
                  << std::setw(11) << (s13_2024 * s13_2025) << "x"
                  << std::setw(11) << s13_dd << "%"
                  << std::endl;
    }

    // ============================================================
    // KEY INSIGHTS
    // ============================================================

    std::cout << "\n" << std::string(140, '=') << std::endl;
    std::cout << "KEY INSIGHTS" << std::endl;
    std::cout << std::string(140, '=') << std::endl;

    // Check if forced entry helps all modes
    int modes_helped = 0;
    int modes_hurt = 0;
    for (const auto& kv : grouped) {
        double noforce_2024 = 0, noforce_2025 = 0, force_2024 = 0, force_2025 = 0;
        for (const auto& r : kv.second) {
            if (!r.force_entry && r.year == "2024") noforce_2024 = r.return_multiple;
            if (!r.force_entry && r.year == "2025") noforce_2025 = r.return_multiple;
            if (r.force_entry && r.year == "2024") force_2024 = r.return_multiple;
            if (r.force_entry && r.year == "2025") force_2025 = r.return_multiple;
        }
        double noforce_2yr = noforce_2024 * noforce_2025;
        double force_2yr = force_2024 * force_2025;
        if (force_2yr > noforce_2yr) modes_helped++;
        else modes_hurt++;
    }

    std::cout << "Forced entry improved " << modes_helped << " of " << (modes_helped + modes_hurt)
              << " configurations" << std::endl;

    if (modes_hurt > 0) {
        std::cout << "WARNING: " << modes_hurt << " configurations performed WORSE with forced entry" << std::endl;
    }

    return 0;
}
