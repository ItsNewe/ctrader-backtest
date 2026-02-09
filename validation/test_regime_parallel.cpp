/**
 * PARALLEL Regime Robustness: Can FIXED spacing work across years?
 *
 * Uses shared tick data + thread pool for 16x speedup.
 * Tests same configs as test_fixed_regime_robustness.cpp but much faster.
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>

using namespace backtest;

// Global shared tick data
std::vector<Tick> g_ticks_2024;
std::vector<Tick> g_ticks_2025;

struct TestConfig {
    std::string name;
    std::string type;  // "FIXED_ABS", "FIXED_PCT", "ADAPTIVE"
    double spacing;    // $ for ABS, % for PCT
};

struct Result {
    std::string name;
    double ret_2024;
    double dd_2024;
    double ret_2025;
    double dd_2025;
    std::string status_2024;
    std::string status_2025;
};

std::mutex g_mutex;
std::vector<Result> g_results;
std::atomic<int> g_completed{0};

TickBacktestConfig GetConfig(const std::string& symbol = "XAUUSD") {
    TickBacktestConfig config;
    config.symbol = symbol;
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
    return config;
}

Result RunTest(const TestConfig& tc) {
    Result r;
    r.name = tc.name;

    auto config = GetConfig();

    // Run 2024
    {
        TickBasedEngine engine(config);

        if (tc.type == "ADAPTIVE") {
            FillUpOscillation::AdaptiveConfig adaptive_cfg;
            adaptive_cfg.pct_spacing = false;
            adaptive_cfg.typical_vol_pct = 0.55;
            adaptive_cfg.min_spacing_mult = 0.5;
            adaptive_cfg.max_spacing_mult = 3.0;

            FillUpOscillation strategy(13.0, 1.5, 0.01, 10.0, 100.0, 500.0,
                FillUpOscillation::ADAPTIVE_SPACING, 0.0, 0.0, 4.0, adaptive_cfg);

            engine.RunWithTicks(g_ticks_2024, [&strategy](const Tick& tick, TickBasedEngine& eng) {
                strategy.OnTick(tick, eng);
            });
        }
        else if (tc.type == "FIXED_ABS") {
            FillUpOscillation strategy(13.0, tc.spacing, 0.01, 10.0, 100.0, 500.0,
                FillUpOscillation::BASELINE, 0.0, 0.0, 4.0);

            engine.RunWithTicks(g_ticks_2024, [&strategy](const Tick& tick, TickBasedEngine& eng) {
                strategy.OnTick(tick, eng);
            });
        }
        else if (tc.type == "FIXED_PCT") {
            FillUpOscillation::AdaptiveConfig adaptive_cfg;
            adaptive_cfg.pct_spacing = true;
            adaptive_cfg.min_spacing_mult = 1.0;  // No adaptation
            adaptive_cfg.max_spacing_mult = 1.0;
            adaptive_cfg.typical_vol_pct = 0.55;

            FillUpOscillation strategy(13.0, tc.spacing, 0.01, 10.0, 100.0, 500.0,
                FillUpOscillation::BASELINE, 0.0, 0.0, 4.0, adaptive_cfg);

            engine.RunWithTicks(g_ticks_2024, [&strategy](const Tick& tick, TickBasedEngine& eng) {
                strategy.OnTick(tick, eng);
            });
        }

        auto res = engine.GetResults();
        r.ret_2024 = res.final_balance / 10000.0;
        r.dd_2024 = res.max_drawdown_pct;
        r.status_2024 = res.stop_out_occurred ? "SO" : "OK";
    }

    // Run 2025
    {
        TickBasedEngine engine(config);

        if (tc.type == "ADAPTIVE") {
            FillUpOscillation::AdaptiveConfig adaptive_cfg;
            adaptive_cfg.pct_spacing = false;
            adaptive_cfg.typical_vol_pct = 0.55;
            adaptive_cfg.min_spacing_mult = 0.5;
            adaptive_cfg.max_spacing_mult = 3.0;

            FillUpOscillation strategy(13.0, 1.5, 0.01, 10.0, 100.0, 500.0,
                FillUpOscillation::ADAPTIVE_SPACING, 0.0, 0.0, 4.0, adaptive_cfg);

            engine.RunWithTicks(g_ticks_2025, [&strategy](const Tick& tick, TickBasedEngine& eng) {
                strategy.OnTick(tick, eng);
            });
        }
        else if (tc.type == "FIXED_ABS") {
            FillUpOscillation strategy(13.0, tc.spacing, 0.01, 10.0, 100.0, 500.0,
                FillUpOscillation::BASELINE, 0.0, 0.0, 4.0);

            engine.RunWithTicks(g_ticks_2025, [&strategy](const Tick& tick, TickBasedEngine& eng) {
                strategy.OnTick(tick, eng);
            });
        }
        else if (tc.type == "FIXED_PCT") {
            FillUpOscillation::AdaptiveConfig adaptive_cfg;
            adaptive_cfg.pct_spacing = true;
            adaptive_cfg.min_spacing_mult = 1.0;
            adaptive_cfg.max_spacing_mult = 1.0;
            adaptive_cfg.typical_vol_pct = 0.55;

            FillUpOscillation strategy(13.0, tc.spacing, 0.01, 10.0, 100.0, 500.0,
                FillUpOscillation::BASELINE, 0.0, 0.0, 4.0, adaptive_cfg);

            engine.RunWithTicks(g_ticks_2025, [&strategy](const Tick& tick, TickBasedEngine& eng) {
                strategy.OnTick(tick, eng);
            });
        }

        auto res = engine.GetResults();
        r.ret_2025 = res.final_balance / 10000.0;
        r.dd_2025 = res.max_drawdown_pct;
        r.status_2025 = res.stop_out_occurred ? "SO" : "OK";
    }

    return r;
}

void Worker(std::queue<TestConfig>& work_queue, int total) {
    while (true) {
        TestConfig task;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (work_queue.empty()) return;
            task = work_queue.front();
            work_queue.pop();
        }

        Result r = RunTest(task);

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_results.push_back(r);
            g_completed++;

            double ratio = (r.ret_2024 > 0.1) ? r.ret_2025 / r.ret_2024 : 999.0;
            double seq = r.ret_2024 * r.ret_2025;
            std::cout << "[" << g_completed << "/" << total << "] "
                      << std::setw(12) << r.name
                      << " 2024=" << std::fixed << std::setprecision(2) << r.ret_2024 << "x"
                      << " 2025=" << r.ret_2025 << "x"
                      << " ratio=" << std::setprecision(1) << ratio << "x"
                      << " seq=" << seq << "x"
                      << " (" << r.status_2024 << "/" << r.status_2025 << ")\n";
        }
    }
}

void LoadTickData(const std::string& path, std::vector<Tick>& ticks) {
    std::cout << "Loading: " << path << "... " << std::flush;

    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open: " + path);
    }

    std::string line;
    std::getline(file, line);  // Skip header

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        Tick tick;
        std::stringstream ss(line);
        std::string token;

        // Parse: timestamp, bid, ask, bid_vol, ask_vol
        std::getline(ss, tick.timestamp, '\t');
        std::getline(ss, token, '\t'); tick.bid = std::stod(token);
        std::getline(ss, token, '\t'); tick.ask = std::stod(token);

        ticks.push_back(tick);
    }

    std::cout << ticks.size() << " ticks loaded\n";
}

int main() {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "====================================================================\n";
    std::cout << "PARALLEL REGIME ROBUSTNESS: FIXED vs ADAPTIVE across years\n";
    std::cout << "====================================================================\n\n";

    // Load tick data
    std::string path_2024 = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAUUSD_TICKS_2024.csv";
    std::string path_2025 = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";

    try {
        LoadTickData(path_2024, g_ticks_2024);
        LoadTickData(path_2025, g_ticks_2025);
    } catch (const std::exception& e) {
        std::cerr << "Error loading data: " << e.what() << std::endl;
        return 1;
    }

    // Create test configurations
    std::vector<TestConfig> configs;

    // FIXED absolute spacings
    for (double sp : {1.5, 2.0, 2.5, 3.0}) {
        std::ostringstream name;
        name << "FIX_$" << std::fixed << std::setprecision(1) << sp;
        configs.push_back({name.str(), "FIXED_ABS", sp});
    }

    // FIXED percentage spacings
    for (double pct : {0.05, 0.06, 0.08, 0.10}) {
        std::ostringstream name;
        name << "FIX_" << std::fixed << std::setprecision(2) << pct << "%";
        configs.push_back({name.str(), "FIXED_PCT", pct});
    }

    // ADAPTIVE baseline
    configs.push_back({"ADAPTIVE", "ADAPTIVE", 1.5});

    // Create work queue
    std::queue<TestConfig> work_queue;
    for (const auto& cfg : configs) {
        work_queue.push(cfg);
    }
    int total = (int)configs.size();

    std::cout << "\nRunning " << total << " configurations on "
              << std::thread::hardware_concurrency() << " threads...\n\n";

    // Launch workers
    auto start = std::chrono::high_resolution_clock::now();

    unsigned int num_threads = std::min((unsigned int)total, std::thread::hardware_concurrency());
    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < num_threads; i++) {
        threads.emplace_back(Worker, std::ref(work_queue), total);
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();

    // Summary
    std::cout << "\n====================================================================\n";
    std::cout << "SUMMARY: Regime Robustness Comparison\n";
    std::cout << "====================================================================\n\n";

    std::cout << std::left << std::setw(14) << "Strategy"
              << std::setw(10) << "2024"
              << std::setw(10) << "2025"
              << std::setw(10) << "Ratio"
              << std::setw(12) << "2-Year Seq"
              << std::setw(10) << "Avg DD"
              << "Status\n";
    std::cout << std::string(76, '-') << "\n";

    // Sort by 2-year sequential return
    std::sort(g_results.begin(), g_results.end(), [](const Result& a, const Result& b) {
        return (a.ret_2024 * a.ret_2025) > (b.ret_2024 * b.ret_2025);
    });

    for (const auto& r : g_results) {
        double ratio = (r.ret_2024 > 0.1) ? r.ret_2025 / r.ret_2024 : 999.0;
        double seq = r.ret_2024 * r.ret_2025;
        double avg_dd = (r.dd_2024 + r.dd_2025) / 2.0;
        std::string status = (r.status_2024 == "OK" && r.status_2025 == "OK") ? "BOTH_OK" : "FAIL";

        std::cout << std::setw(14) << r.name
                  << std::setw(10) << (std::to_string(r.ret_2024).substr(0,5) + "x")
                  << std::setw(10) << (std::to_string(r.ret_2025).substr(0,5) + "x")
                  << std::setw(10) << (ratio < 100 ? std::to_string(ratio).substr(0,4) + "x" : "INF")
                  << std::setw(12) << (std::to_string(seq).substr(0,6) + "x")
                  << std::setw(10) << (std::to_string(avg_dd).substr(0,5) + "%")
                  << status << "\n";
    }

    // Analysis
    std::cout << "\n====================================================================\n";
    std::cout << "ANALYSIS\n";
    std::cout << "====================================================================\n";

    // Find best and compare to ADAPTIVE
    double adaptive_seq = 0;
    double best_fixed_seq = 0;
    std::string best_fixed_name;

    for (const auto& r : g_results) {
        if (r.status_2024 == "OK" && r.status_2025 == "OK") {
            double seq = r.ret_2024 * r.ret_2025;
            if (r.name == "ADAPTIVE") {
                adaptive_seq = seq;
            } else if (seq > best_fixed_seq) {
                best_fixed_seq = seq;
                best_fixed_name = r.name;
            }
        }
    }

    std::cout << "\nBest FIXED config: " << best_fixed_name << " (" << best_fixed_seq << "x 2-year)\n";
    std::cout << "ADAPTIVE baseline: " << adaptive_seq << "x 2-year\n";

    double pct_of_adaptive = (best_fixed_seq / adaptive_seq) * 100.0;
    std::cout << "Best FIXED achieves " << pct_of_adaptive << "% of ADAPTIVE's return\n";

    if (best_fixed_seq >= adaptive_seq * 0.9) {
        std::cout << "\n=> SIMPLE CAN MATCH COMPLEX with the right parameter!\n";
        std::cout << "   " << best_fixed_name << " achieves >90% of ADAPTIVE with fewer parameters.\n";
    } else {
        std::cout << "\n=> ADAPTIVE's complexity is JUSTIFIED.\n";
        std::cout << "   Best FIXED achieves only " << pct_of_adaptive << "% of ADAPTIVE.\n";
    }

    std::cout << "\nCompleted in " << elapsed << " seconds\n";

    return 0;
}
