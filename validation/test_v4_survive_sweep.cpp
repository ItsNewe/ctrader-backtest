/**
 * V4 Survive Percentage Sweep
 *
 * Find optimal survive_pct for FillUpOscillation V4 (ADAPTIVE_SPACING)
 * Testing: What survive% gives best risk-adjusted returns across regimes?
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
#include <algorithm>

using namespace backtest;

std::vector<Tick> g_ticks_2024;
std::vector<Tick> g_ticks_2025;

struct TestConfig {
    double survive_pct;
};

struct Result {
    double survive_pct;
    double ret_2024;
    double dd_2024;
    int trades_2024;
    double ret_2025;
    double dd_2025;
    int trades_2025;
    std::string status_2024;
    std::string status_2025;
};

std::mutex g_mutex;
std::vector<Result> g_results;
std::atomic<int> g_completed{0};

TickBacktestConfig GetConfig() {
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
    return config;
}

Result RunTest(const TestConfig& tc) {
    Result r;
    r.survive_pct = tc.survive_pct;

    auto config = GetConfig();

    // V4 = ADAPTIVE_SPACING with standard params
    FillUpOscillation::AdaptiveConfig adaptive_cfg;
    adaptive_cfg.pct_spacing = false;
    adaptive_cfg.typical_vol_pct = 0.55;
    adaptive_cfg.min_spacing_mult = 0.5;
    adaptive_cfg.max_spacing_mult = 3.0;

    // 2024
    {
        TickBasedEngine engine(config);
        FillUpOscillation strategy(tc.survive_pct, 1.5, 0.01, 10.0, 100.0, 500.0,
            FillUpOscillation::ADAPTIVE_SPACING, 0.0, 0.0, 4.0, adaptive_cfg);

        engine.RunWithTicks(g_ticks_2024, [&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto res = engine.GetResults();
        r.ret_2024 = res.final_balance / 10000.0;
        r.dd_2024 = res.max_drawdown_pct;
        r.trades_2024 = res.total_trades;
        r.status_2024 = res.stop_out_occurred ? "SO" : "OK";
    }

    // 2025
    {
        TickBasedEngine engine(config);
        FillUpOscillation strategy(tc.survive_pct, 1.5, 0.01, 10.0, 100.0, 500.0,
            FillUpOscillation::ADAPTIVE_SPACING, 0.0, 0.0, 4.0, adaptive_cfg);

        engine.RunWithTicks(g_ticks_2025, [&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto res = engine.GetResults();
        r.ret_2025 = res.final_balance / 10000.0;
        r.dd_2025 = res.max_drawdown_pct;
        r.trades_2025 = res.total_trades;
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

            double seq = r.ret_2024 * r.ret_2025;
            double avg_dd = (r.dd_2024 + r.dd_2025) / 2.0;
            std::cout << "[" << g_completed << "/" << total << "] "
                      << "survive=" << std::fixed << std::setprecision(0) << r.survive_pct << "%"
                      << " | 2024=" << std::setprecision(2) << r.ret_2024 << "x"
                      << " | 2025=" << r.ret_2025 << "x"
                      << " | seq=" << std::setprecision(1) << seq << "x"
                      << " | avgDD=" << std::setprecision(1) << avg_dd << "%"
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

        std::getline(ss, tick.timestamp, '\t');
        std::getline(ss, token, '\t'); tick.bid = std::stod(token);
        std::getline(ss, token, '\t'); tick.ask = std::stod(token);

        ticks.push_back(tick);
    }

    std::cout << ticks.size() << " ticks loaded\n";
}

int main() {
    std::cout << std::fixed;
    std::cout << "====================================================================\n";
    std::cout << "V4 SURVIVE PERCENTAGE SWEEP\n";
    std::cout << "Strategy: FillUpOscillation ADAPTIVE_SPACING\n";
    std::cout << "====================================================================\n\n";

    std::string path_2024 = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAUUSD_TICKS_2024.csv";
    std::string path_2025 = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";

    try {
        LoadTickData(path_2024, g_ticks_2024);
        LoadTickData(path_2025, g_ticks_2025);
    } catch (const std::exception& e) {
        std::cerr << "Error loading data: " << e.what() << std::endl;
        return 1;
    }

    // Sweep survive_pct from 3% to 25%
    std::vector<double> survive_values = {3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 18, 20, 22, 25};

    std::queue<TestConfig> work_queue;
    for (double s : survive_values) {
        work_queue.push({s});
    }
    int total = (int)survive_values.size();

    std::cout << "\nRunning " << total << " configurations...\n\n";

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

    // Sort by survive_pct
    std::sort(g_results.begin(), g_results.end(), [](const Result& a, const Result& b) {
        return a.survive_pct < b.survive_pct;
    });

    // Summary table
    std::cout << "\n====================================================================\n";
    std::cout << "RESULTS BY SURVIVE PERCENTAGE\n";
    std::cout << "====================================================================\n\n";

    std::cout << std::left
              << std::setw(10) << "Survive"
              << std::setw(10) << "2024"
              << std::setw(10) << "2025"
              << std::setw(12) << "2-Year Seq"
              << std::setw(10) << "DD 2024"
              << std::setw(10) << "DD 2025"
              << std::setw(10) << "Avg DD"
              << std::setw(12) << "Return/DD"
              << "Status\n";
    std::cout << std::string(94, '-') << "\n";

    double best_seq = 0;
    double best_survive_seq = 0;
    double best_ratio = 0;
    double best_survive_ratio = 0;

    for (const auto& r : g_results) {
        double seq = r.ret_2024 * r.ret_2025;
        double avg_dd = (r.dd_2024 + r.dd_2025) / 2.0;
        double return_per_dd = (avg_dd > 0) ? seq / avg_dd : 0;
        std::string status = (r.status_2024 == "OK" && r.status_2025 == "OK") ? "BOTH_OK" :
                             (r.status_2024 == "SO" && r.status_2025 == "SO") ? "BOTH_SO" : "MIXED";

        std::cout << std::setw(10) << (std::to_string((int)r.survive_pct) + "%")
                  << std::setw(10) << std::setprecision(2) << (std::to_string(r.ret_2024).substr(0,5) + "x")
                  << std::setw(10) << (std::to_string(r.ret_2025).substr(0,5) + "x")
                  << std::setw(12) << std::setprecision(1) << (std::to_string(seq).substr(0,6) + "x")
                  << std::setw(10) << (std::to_string(r.dd_2024).substr(0,5) + "%")
                  << std::setw(10) << (std::to_string(r.dd_2025).substr(0,5) + "%")
                  << std::setw(10) << (std::to_string(avg_dd).substr(0,5) + "%")
                  << std::setw(12) << std::setprecision(3) << return_per_dd
                  << status << "\n";

        if (status == "BOTH_OK") {
            if (seq > best_seq) {
                best_seq = seq;
                best_survive_seq = r.survive_pct;
            }
            if (return_per_dd > best_ratio) {
                best_ratio = return_per_dd;
                best_survive_ratio = r.survive_pct;
            }
        }
    }

    // Analysis
    std::cout << "\n====================================================================\n";
    std::cout << "ANALYSIS\n";
    std::cout << "====================================================================\n";

    std::cout << "\nBest 2-year sequential return: survive=" << best_survive_seq << "% (" << best_seq << "x)\n";
    std::cout << "Best return/DD ratio: survive=" << best_survive_ratio << "% (ratio=" << best_ratio << ")\n";

    // Find survival threshold
    double min_survive_both = 100;
    for (const auto& r : g_results) {
        if (r.status_2024 == "OK" && r.status_2025 == "OK") {
            if (r.survive_pct < min_survive_both) {
                min_survive_both = r.survive_pct;
            }
        }
    }
    std::cout << "\nMinimum survive% that survives both years: " << min_survive_both << "%\n";

    // Recommendations
    std::cout << "\n====================================================================\n";
    std::cout << "RECOMMENDATIONS\n";
    std::cout << "====================================================================\n";

    std::cout << "\nFor MAXIMUM RETURN (accept high DD):\n";
    std::cout << "  survive=" << best_survive_seq << "%\n";

    std::cout << "\nFor BEST RISK-ADJUSTED (return per unit DD):\n";
    std::cout << "  survive=" << best_survive_ratio << "%\n";

    std::cout << "\nFor SAFETY MARGIN (handles today's 9% drop):\n";
    std::cout << "  survive >= 10% (9% drop + buffer)\n";

    std::cout << "\nCompleted in " << std::setprecision(1) << elapsed << " seconds\n";

    return 0;
}
