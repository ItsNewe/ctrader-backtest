/**
 * test_combinedju_sweep.cpp
 *
 * PARALLEL sweep of CombinedJu strategy parameters on 2025 XAUUSD data.
 * Tests variations of:
 * - survive_pct: 12%, 13%
 * - tp_mode: FIXED, SQRT, LINEAR
 * - sizing_mode: UNIFORM, LINEAR_SIZING, THRESHOLD_SIZING
 * - velocity_filter: ON/OFF
 * - base_spacing: 1.0, 1.5, 2.0
 * - tp parameters: sqrt_scale, linear_scale
 * - velocity parameters: window, threshold
 * - sizing parameters: threshold_pos, threshold_mult
 */

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
#include <algorithm>

using namespace backtest;

// Shared tick data
std::vector<Tick> g_ticks;

struct TestConfig {
    std::string name;
    double survive_pct;
    double base_spacing;
    StrategyCombinedJu::TPMode tp_mode;
    double tp_sqrt_scale;
    double tp_linear_scale;
    double tp_min;
    bool enable_velocity_filter;
    int velocity_window;
    double velocity_threshold_pct;
    StrategyCombinedJu::SizingMode sizing_mode;
    double sizing_linear_scale;
    int sizing_threshold_pos;
    double sizing_threshold_mult;
    double volatility_lookback_hours;
    double typical_vol_pct;
};

struct TestResult {
    std::string name;
    double final_balance;
    double return_multiple;
    double max_dd_pct;
    int total_trades;
    int peak_positions;
    long velocity_blocks;
    long lot_zero_blocks;
};

std::mutex g_queue_mutex;
std::mutex g_results_mutex;
std::queue<TestConfig> g_work_queue;
std::vector<TestResult> g_results;
std::atomic<int> g_completed(0);
int g_total_tasks = 0;

void LoadTickData() {
    auto start = std::chrono::high_resolution_clock::now();
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
            g_ticks.push_back(tick);
        }
    }
    std::sort(g_ticks.begin(), g_ticks.end(),
              [](const Tick& a, const Tick& b) { return a.timestamp < b.timestamp; });

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();
    std::cout << "Loaded " << g_ticks.size() << " ticks in " << duration << "s" << std::endl;
}

TestResult RunTest(const TestConfig& cfg) {
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
    config.start_date = "2025.01.01";
    config.end_date = "2026.01.27";
    config.verbose = false;

    TickDataConfig tick_cfg;
    tick_cfg.format = TickDataFormat::MT5_CSV;
    config.tick_data_config = tick_cfg;

    TickBasedEngine engine(config);

    StrategyCombinedJu::Config jucfg;
    jucfg.survive_pct = cfg.survive_pct;
    jucfg.base_spacing = cfg.base_spacing;
    jucfg.min_volume = 0.01;
    jucfg.max_volume = 10.0;
    jucfg.contract_size = 100.0;
    jucfg.leverage = 500.0;
    jucfg.volatility_lookback_hours = cfg.volatility_lookback_hours;
    jucfg.typical_vol_pct = cfg.typical_vol_pct;
    jucfg.tp_mode = cfg.tp_mode;
    jucfg.tp_sqrt_scale = cfg.tp_sqrt_scale;
    jucfg.tp_linear_scale = cfg.tp_linear_scale;
    jucfg.tp_min = cfg.tp_min;
    jucfg.enable_velocity_filter = cfg.enable_velocity_filter;
    jucfg.velocity_window = cfg.velocity_window;
    jucfg.velocity_threshold_pct = cfg.velocity_threshold_pct;
    jucfg.sizing_mode = cfg.sizing_mode;
    jucfg.sizing_linear_scale = cfg.sizing_linear_scale;
    jucfg.sizing_threshold_pos = cfg.sizing_threshold_pos;
    jucfg.sizing_threshold_mult = cfg.sizing_threshold_mult;

    StrategyCombinedJu strategy(jucfg);

    engine.RunWithTicks(g_ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();
    auto& stats = strategy.GetStats();

    TestResult result;
    result.name = cfg.name;
    result.final_balance = results.final_balance;
    result.return_multiple = results.final_balance / 10000.0;
    result.max_dd_pct = results.max_drawdown_pct;
    result.total_trades = results.total_trades;
    result.peak_positions = stats.max_position_count;
    result.velocity_blocks = stats.velocity_blocks;
    result.lot_zero_blocks = stats.lot_size_zero_blocks;

    return result;
}

void Worker() {
    while (true) {
        TestConfig task;
        {
            std::lock_guard<std::mutex> lock(g_queue_mutex);
            if (g_work_queue.empty()) return;
            task = g_work_queue.front();
            g_work_queue.pop();
        }

        TestResult result = RunTest(task);

        {
            std::lock_guard<std::mutex> lock(g_results_mutex);
            g_results.push_back(result);
        }

        int done = ++g_completed;
        if (done % 10 == 0 || done == g_total_tasks) {
            std::cout << "\r  [" << done << "/" << g_total_tasks << "] "
                      << std::fixed << std::setprecision(1)
                      << (100.0 * done / g_total_tasks) << "%"
                      << std::string(20, ' ') << std::flush;
        }
    }
}

std::string TPModeStr(StrategyCombinedJu::TPMode m) {
    switch(m) {
        case StrategyCombinedJu::FIXED: return "FIX";
        case StrategyCombinedJu::SQRT: return "SQR";
        case StrategyCombinedJu::LINEAR: return "LIN";
    }
    return "?";
}

std::string SizingModeStr(StrategyCombinedJu::SizingMode m) {
    switch(m) {
        case StrategyCombinedJu::UNIFORM: return "UNI";
        case StrategyCombinedJu::LINEAR_SIZING: return "LIN";
        case StrategyCombinedJu::THRESHOLD_SIZING: return "THR";
    }
    return "?";
}

int main() {
    std::cout << std::string(100, '=') << std::endl;
    std::cout << "COMBINED_JU PARAMETER SWEEP" << std::endl;
    std::cout << "Testing parameter variations on 2025 XAUUSD data" << std::endl;
    std::cout << std::string(100, '=') << std::endl << std::endl;

    LoadTickData();

    // Build sweep configurations
    std::vector<double> survive_vals = {12.0, 13.0};
    std::vector<double> spacing_vals = {1.0, 1.5, 2.0};
    std::vector<StrategyCombinedJu::TPMode> tp_modes = {
        StrategyCombinedJu::FIXED,
        StrategyCombinedJu::SQRT,
        StrategyCombinedJu::LINEAR
    };
    std::vector<StrategyCombinedJu::SizingMode> sizing_modes = {
        StrategyCombinedJu::UNIFORM,
        StrategyCombinedJu::LINEAR_SIZING,
        StrategyCombinedJu::THRESHOLD_SIZING
    };
    std::vector<bool> velocity_vals = {false, true};
    std::vector<double> tp_sqrt_scales = {0.3, 0.5, 0.7};
    std::vector<double> tp_linear_scales = {0.2, 0.3, 0.5};
    std::vector<int> velocity_windows = {5, 10, 20};
    std::vector<double> velocity_thresholds = {0.005, 0.01, 0.02};
    std::vector<int> sizing_threshold_pos = {3, 5, 7};
    std::vector<double> sizing_threshold_mult = {1.5, 2.0, 3.0};
    std::vector<double> lookback_vals = {2.0, 4.0};
    std::vector<double> typvol_vals = {0.45, 0.55};

    int config_id = 0;

    // Core sweep: survive × spacing × tp_mode × sizing_mode × velocity
    for (double survive : survive_vals) {
        for (double spacing : spacing_vals) {
            for (auto tp_mode : tp_modes) {
                for (auto sizing_mode : sizing_modes) {
                    for (bool vel : velocity_vals) {
                        TestConfig cfg;
                        cfg.survive_pct = survive;
                        cfg.base_spacing = spacing;
                        cfg.tp_mode = tp_mode;
                        cfg.tp_sqrt_scale = 0.5;
                        cfg.tp_linear_scale = 0.3;
                        cfg.tp_min = spacing;
                        cfg.enable_velocity_filter = vel;
                        cfg.velocity_window = 10;
                        cfg.velocity_threshold_pct = 0.01;
                        cfg.sizing_mode = sizing_mode;
                        cfg.sizing_linear_scale = 0.5;
                        cfg.sizing_threshold_pos = 5;
                        cfg.sizing_threshold_mult = 2.0;
                        cfg.volatility_lookback_hours = 4.0;
                        cfg.typical_vol_pct = 0.55;

                        std::ostringstream name;
                        name << "s" << (int)survive
                             << "_sp" << std::fixed << std::setprecision(1) << spacing
                             << "_tp" << TPModeStr(tp_mode)
                             << "_sz" << SizingModeStr(sizing_mode)
                             << "_v" << (vel ? "1" : "0");
                        cfg.name = name.str();

                        g_work_queue.push(cfg);
                        config_id++;
                    }
                }
            }
        }
    }

    // Extended sweep: TP parameters (for SQRT and LINEAR modes)
    for (double survive : {12.0, 13.0}) {
        for (double sqrt_scale : tp_sqrt_scales) {
            TestConfig cfg;
            cfg.survive_pct = survive;
            cfg.base_spacing = 1.5;
            cfg.tp_mode = StrategyCombinedJu::SQRT;
            cfg.tp_sqrt_scale = sqrt_scale;
            cfg.tp_linear_scale = 0.3;
            cfg.tp_min = 1.5;
            cfg.enable_velocity_filter = true;
            cfg.velocity_window = 10;
            cfg.velocity_threshold_pct = 0.01;
            cfg.sizing_mode = StrategyCombinedJu::THRESHOLD_SIZING;
            cfg.sizing_linear_scale = 0.5;
            cfg.sizing_threshold_pos = 5;
            cfg.sizing_threshold_mult = 2.0;
            cfg.volatility_lookback_hours = 4.0;
            cfg.typical_vol_pct = 0.55;

            std::ostringstream name;
            name << "s" << (int)survive << "_sqrtsc" << std::fixed << std::setprecision(1) << sqrt_scale;
            cfg.name = name.str();
            g_work_queue.push(cfg);
            config_id++;
        }
        for (double lin_scale : tp_linear_scales) {
            TestConfig cfg;
            cfg.survive_pct = survive;
            cfg.base_spacing = 1.5;
            cfg.tp_mode = StrategyCombinedJu::LINEAR;
            cfg.tp_sqrt_scale = 0.5;
            cfg.tp_linear_scale = lin_scale;
            cfg.tp_min = 1.5;
            cfg.enable_velocity_filter = true;
            cfg.velocity_window = 10;
            cfg.velocity_threshold_pct = 0.01;
            cfg.sizing_mode = StrategyCombinedJu::THRESHOLD_SIZING;
            cfg.sizing_linear_scale = 0.5;
            cfg.sizing_threshold_pos = 5;
            cfg.sizing_threshold_mult = 2.0;
            cfg.volatility_lookback_hours = 4.0;
            cfg.typical_vol_pct = 0.55;

            std::ostringstream name;
            name << "s" << (int)survive << "_linsc" << std::fixed << std::setprecision(1) << lin_scale;
            cfg.name = name.str();
            g_work_queue.push(cfg);
            config_id++;
        }
    }

    // Extended sweep: Velocity parameters
    for (double survive : {12.0, 13.0}) {
        for (int vwin : velocity_windows) {
            for (double vthresh : velocity_thresholds) {
                TestConfig cfg;
                cfg.survive_pct = survive;
                cfg.base_spacing = 1.5;
                cfg.tp_mode = StrategyCombinedJu::SQRT;
                cfg.tp_sqrt_scale = 0.5;
                cfg.tp_linear_scale = 0.3;
                cfg.tp_min = 1.5;
                cfg.enable_velocity_filter = true;
                cfg.velocity_window = vwin;
                cfg.velocity_threshold_pct = vthresh;
                cfg.sizing_mode = StrategyCombinedJu::THRESHOLD_SIZING;
                cfg.sizing_linear_scale = 0.5;
                cfg.sizing_threshold_pos = 5;
                cfg.sizing_threshold_mult = 2.0;
                cfg.volatility_lookback_hours = 4.0;
                cfg.typical_vol_pct = 0.55;

                std::ostringstream name;
                name << "s" << (int)survive << "_vw" << vwin << "_vt" << std::fixed << std::setprecision(3) << vthresh;
                cfg.name = name.str();
                g_work_queue.push(cfg);
                config_id++;
            }
        }
    }

    // Extended sweep: Sizing parameters (for THRESHOLD mode)
    for (double survive : {12.0, 13.0}) {
        for (int tpos : sizing_threshold_pos) {
            for (double tmult : sizing_threshold_mult) {
                TestConfig cfg;
                cfg.survive_pct = survive;
                cfg.base_spacing = 1.5;
                cfg.tp_mode = StrategyCombinedJu::SQRT;
                cfg.tp_sqrt_scale = 0.5;
                cfg.tp_linear_scale = 0.3;
                cfg.tp_min = 1.5;
                cfg.enable_velocity_filter = true;
                cfg.velocity_window = 10;
                cfg.velocity_threshold_pct = 0.01;
                cfg.sizing_mode = StrategyCombinedJu::THRESHOLD_SIZING;
                cfg.sizing_linear_scale = 0.5;
                cfg.sizing_threshold_pos = tpos;
                cfg.sizing_threshold_mult = tmult;
                cfg.volatility_lookback_hours = 4.0;
                cfg.typical_vol_pct = 0.55;

                std::ostringstream name;
                name << "s" << (int)survive << "_tpos" << tpos << "_tmult" << std::fixed << std::setprecision(1) << tmult;
                cfg.name = name.str();
                g_work_queue.push(cfg);
                config_id++;
            }
        }
    }

    // Extended sweep: Volatility lookback and typical vol
    for (double survive : {12.0, 13.0}) {
        for (double lb : lookback_vals) {
            for (double tv : typvol_vals) {
                TestConfig cfg;
                cfg.survive_pct = survive;
                cfg.base_spacing = 1.5;
                cfg.tp_mode = StrategyCombinedJu::SQRT;
                cfg.tp_sqrt_scale = 0.5;
                cfg.tp_linear_scale = 0.3;
                cfg.tp_min = 1.5;
                cfg.enable_velocity_filter = true;
                cfg.velocity_window = 10;
                cfg.velocity_threshold_pct = 0.01;
                cfg.sizing_mode = StrategyCombinedJu::THRESHOLD_SIZING;
                cfg.sizing_linear_scale = 0.5;
                cfg.sizing_threshold_pos = 5;
                cfg.sizing_threshold_mult = 2.0;
                cfg.volatility_lookback_hours = lb;
                cfg.typical_vol_pct = tv;

                std::ostringstream name;
                name << "s" << (int)survive << "_lb" << std::fixed << std::setprecision(0) << lb
                     << "_tv" << std::setprecision(2) << tv;
                cfg.name = name.str();
                g_work_queue.push(cfg);
                config_id++;
            }
        }
    }

    g_total_tasks = g_work_queue.size();
    std::cout << "\nRunning " << g_total_tasks << " configurations..." << std::endl;

    auto start = std::chrono::high_resolution_clock::now();

    unsigned int num_threads = std::thread::hardware_concurrency();
    std::cout << "Using " << num_threads << " threads" << std::endl;

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < num_threads; i++) {
        threads.emplace_back(Worker);
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();

    std::cout << "\n\nCompleted in " << duration << "s" << std::endl;

    // Sort by return (descending)
    std::sort(g_results.begin(), g_results.end(), [](const TestResult& a, const TestResult& b) {
        return a.return_multiple > b.return_multiple;
    });

    // Print top 30 results
    std::cout << "\n" << std::string(130, '=') << std::endl;
    std::cout << "TOP 30 CONFIGURATIONS BY RETURN" << std::endl;
    std::cout << std::string(130, '=') << std::endl;

    std::cout << std::left << std::setw(40) << "Config"
              << std::right << std::setw(12) << "Return"
              << std::setw(12) << "MaxDD"
              << std::setw(12) << "Trades"
              << std::setw(12) << "PeakPos"
              << std::setw(15) << "VelBlocks"
              << std::setw(15) << "LotZeroBlk"
              << std::endl;
    std::cout << std::string(130, '-') << std::endl;

    for (size_t i = 0; i < std::min((size_t)30, g_results.size()); i++) {
        const auto& r = g_results[i];
        std::cout << std::left << std::setw(40) << r.name
                  << std::right << std::setw(11) << std::fixed << std::setprecision(2) << r.return_multiple << "x"
                  << std::setw(11) << r.max_dd_pct << "%"
                  << std::setw(12) << r.total_trades
                  << std::setw(12) << r.peak_positions
                  << std::setw(15) << r.velocity_blocks
                  << std::setw(15) << r.lot_zero_blocks
                  << std::endl;
    }

    // Print bottom 10 (worst)
    std::cout << "\n" << std::string(130, '=') << std::endl;
    std::cout << "BOTTOM 10 CONFIGURATIONS (WORST)" << std::endl;
    std::cout << std::string(130, '=') << std::endl;

    for (size_t i = g_results.size() - 10; i < g_results.size(); i++) {
        const auto& r = g_results[i];
        std::cout << std::left << std::setw(40) << r.name
                  << std::right << std::setw(11) << std::fixed << std::setprecision(2) << r.return_multiple << "x"
                  << std::setw(11) << r.max_dd_pct << "%"
                  << std::setw(12) << r.total_trades
                  << std::setw(12) << r.peak_positions
                  << std::endl;
    }

    // Analysis by parameter
    std::cout << "\n" << std::string(130, '=') << std::endl;
    std::cout << "PARAMETER IMPACT ANALYSIS" << std::endl;
    std::cout << std::string(130, '=') << std::endl;

    // Survive 12 vs 13
    double sum12 = 0, sum13 = 0;
    int cnt12 = 0, cnt13 = 0;
    for (const auto& r : g_results) {
        if (r.name.find("s12") != std::string::npos) { sum12 += r.return_multiple; cnt12++; }
        if (r.name.find("s13") != std::string::npos) { sum13 += r.return_multiple; cnt13++; }
    }
    std::cout << "Survive 12%: avg " << std::fixed << std::setprecision(2) << (sum12/cnt12) << "x (" << cnt12 << " configs)" << std::endl;
    std::cout << "Survive 13%: avg " << (sum13/cnt13) << "x (" << cnt13 << " configs)" << std::endl;

    // Velocity filter ON vs OFF
    double sumVel0 = 0, sumVel1 = 0;
    int cntVel0 = 0, cntVel1 = 0;
    for (const auto& r : g_results) {
        if (r.name.find("_v0") != std::string::npos) { sumVel0 += r.return_multiple; cntVel0++; }
        if (r.name.find("_v1") != std::string::npos) { sumVel1 += r.return_multiple; cntVel1++; }
    }
    if (cntVel0 > 0) std::cout << "Velocity OFF: avg " << (sumVel0/cntVel0) << "x (" << cntVel0 << " configs)" << std::endl;
    if (cntVel1 > 0) std::cout << "Velocity ON:  avg " << (sumVel1/cntVel1) << "x (" << cntVel1 << " configs)" << std::endl;

    // TP Mode
    double sumFix = 0, sumSqr = 0, sumLin = 0;
    int cntFix = 0, cntSqr = 0, cntLin = 0;
    for (const auto& r : g_results) {
        if (r.name.find("_tpFIX") != std::string::npos) { sumFix += r.return_multiple; cntFix++; }
        if (r.name.find("_tpSQR") != std::string::npos) { sumSqr += r.return_multiple; cntSqr++; }
        if (r.name.find("_tpLIN") != std::string::npos) { sumLin += r.return_multiple; cntLin++; }
    }
    if (cntFix > 0) std::cout << "TP FIXED:  avg " << (sumFix/cntFix) << "x (" << cntFix << " configs)" << std::endl;
    if (cntSqr > 0) std::cout << "TP SQRT:   avg " << (sumSqr/cntSqr) << "x (" << cntSqr << " configs)" << std::endl;
    if (cntLin > 0) std::cout << "TP LINEAR: avg " << (sumLin/cntLin) << "x (" << cntLin << " configs)" << std::endl;

    // Sizing Mode
    double sumUni = 0, sumLinSz = 0, sumThr = 0;
    int cntUni = 0, cntLinSz = 0, cntThr = 0;
    for (const auto& r : g_results) {
        if (r.name.find("_szUNI") != std::string::npos) { sumUni += r.return_multiple; cntUni++; }
        if (r.name.find("_szLIN") != std::string::npos) { sumLinSz += r.return_multiple; cntLinSz++; }
        if (r.name.find("_szTHR") != std::string::npos) { sumThr += r.return_multiple; cntThr++; }
    }
    if (cntUni > 0) std::cout << "Sizing UNIFORM:   avg " << (sumUni/cntUni) << "x (" << cntUni << " configs)" << std::endl;
    if (cntLinSz > 0) std::cout << "Sizing LINEAR:    avg " << (sumLinSz/cntLinSz) << "x (" << cntLinSz << " configs)" << std::endl;
    if (cntThr > 0) std::cout << "Sizing THRESHOLD: avg " << (sumThr/cntThr) << "x (" << cntThr << " configs)" << std::endl;

    // Best by risk-adjusted (Return / DD)
    std::cout << "\n" << std::string(130, '=') << std::endl;
    std::cout << "TOP 10 BY RISK-ADJUSTED (Return/DD)" << std::endl;
    std::cout << std::string(130, '=') << std::endl;

    std::vector<std::pair<double, size_t>> risk_adj;
    for (size_t i = 0; i < g_results.size(); i++) {
        double ra = g_results[i].return_multiple / (g_results[i].max_dd_pct / 100.0 + 0.01);
        risk_adj.push_back({ra, i});
    }
    std::sort(risk_adj.begin(), risk_adj.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });

    for (size_t i = 0; i < std::min((size_t)10, risk_adj.size()); i++) {
        const auto& r = g_results[risk_adj[i].second];
        std::cout << std::left << std::setw(40) << r.name
                  << std::right << std::setw(11) << std::fixed << std::setprecision(2) << r.return_multiple << "x"
                  << std::setw(11) << r.max_dd_pct << "%"
                  << "  RA=" << std::setw(8) << risk_adj[i].first
                  << std::endl;
    }

    return 0;
}
