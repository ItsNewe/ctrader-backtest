/**
 * Profit Extraction Model Test
 *
 * For each survive%, run the full year and track:
 * - Peak equity before any stop-out
 * - Number of stop-outs
 * - What could have been extracted at peak
 *
 * Uses V4 Aggressive preset: pct_spacing=true, BaseSpacingPct=0.05
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

const double BASE_CAPITAL = 10000.0;

struct TestResult {
    double survive_pct;
    // 2024
    double final_2024;
    double peak_2024;
    double dd_2024;
    bool stopped_2024;
    // 2025
    double final_2025;
    double peak_2025;
    double dd_2025;
    bool stopped_2025;
};

std::mutex g_mutex;
std::vector<TestResult> g_results;
std::atomic<int> g_completed{0};

TickBacktestConfig GetConfig() {
    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = BASE_CAPITAL;
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

TestResult RunTest(double survive_pct) {
    TestResult r;
    r.survive_pct = survive_pct;

    auto config = GetConfig();

    FillUpOscillation::AdaptiveConfig adaptive_cfg;
    adaptive_cfg.pct_spacing = true;
    adaptive_cfg.typical_vol_pct = 0.55;
    adaptive_cfg.min_spacing_mult = 0.5;
    adaptive_cfg.max_spacing_mult = 3.0;
    adaptive_cfg.min_spacing_abs = 0.005;
    adaptive_cfg.max_spacing_abs = 1.0;
    adaptive_cfg.spacing_change_threshold = 0.01;

    // 2024
    {
        TickBasedEngine engine(config);
        FillUpOscillation strategy(survive_pct, 0.05, 0.01, 10.0, 100.0, 500.0,
            FillUpOscillation::ADAPTIVE_SPACING, 0.0, 0.0, 4.0, adaptive_cfg);

        double peak = BASE_CAPITAL;

        engine.RunWithTicks(g_ticks_2024, [&](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
            double eq = eng.GetEquity();
            if (eq > peak) peak = eq;
        });

        auto res = engine.GetResults();
        r.final_2024 = res.final_balance;
        r.peak_2024 = peak;
        r.dd_2024 = res.max_drawdown_pct;
        r.stopped_2024 = res.stop_out_occurred;
    }

    // 2025
    {
        TickBasedEngine engine(config);
        FillUpOscillation strategy(survive_pct, 0.05, 0.01, 10.0, 100.0, 500.0,
            FillUpOscillation::ADAPTIVE_SPACING, 0.0, 0.0, 4.0, adaptive_cfg);

        double peak = BASE_CAPITAL;

        engine.RunWithTicks(g_ticks_2025, [&](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
            double eq = eng.GetEquity();
            if (eq > peak) peak = eq;
        });

        auto res = engine.GetResults();
        r.final_2025 = res.final_balance;
        r.peak_2025 = peak;
        r.dd_2025 = res.max_drawdown_pct;
        r.stopped_2025 = res.stop_out_occurred;
    }

    return r;
}

void Worker(std::queue<double>& work_queue, int total) {
    while (true) {
        double survive_pct;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (work_queue.empty()) return;
            survive_pct = work_queue.front();
            work_queue.pop();
        }

        TestResult r = RunTest(survive_pct);

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_results.push_back(r);
            g_completed++;

            std::cout << "[" << g_completed << "/" << total << "] "
                      << "survive=" << std::fixed << std::setprecision(0) << r.survive_pct << "%"
                      << " | 2024: " << (r.stopped_2024 ? "SO" : "OK")
                      << " peak=$" << (int)r.peak_2024
                      << " | 2025: " << (r.stopped_2025 ? "SO" : "OK")
                      << " peak=$" << (int)r.peak_2025 << "\n";
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
    std::getline(file, line);

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

    std::cout << ticks.size() << " ticks\n";
}

int main() {
    std::cout << "====================================================================\n";
    std::cout << "EXTRACTION MODEL TEST (V4 Aggressive: BaseSpacingPct=0.05)\n";
    std::cout << "====================================================================\n\n";

    try {
        LoadTickData("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAUUSD_TICKS_2024.csv", g_ticks_2024);
        LoadTickData("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv", g_ticks_2025);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    std::vector<double> survive_values = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};

    std::queue<double> work_queue;
    for (double s : survive_values) {
        work_queue.push(s);
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

    std::sort(g_results.begin(), g_results.end(), [](const TestResult& a, const TestResult& b) {
        return a.survive_pct < b.survive_pct;
    });

    // Results table
    std::cout << "\n====================================================================\n";
    std::cout << "RESULTS: EXTRACTION POTENTIAL BY SURVIVE PERCENTAGE\n";
    std::cout << "====================================================================\n\n";

    std::cout << "Logic: If stopped out, you could have extracted (peak - $10k) before the crash.\n";
    std::cout << "       If survived, your final balance is available.\n\n";

    std::cout << std::left
              << std::setw(10) << "Survive"
              << std::setw(10) << "2024"
              << std::setw(14) << "Peak 2024"
              << std::setw(14) << "Extract 2024"
              << std::setw(10) << "2025"
              << std::setw(14) << "Peak 2025"
              << std::setw(14) << "Extract 2025"
              << std::setw(14) << "2-Year Total"
              << "\n";
    std::cout << std::string(100, '-') << "\n";

    double best_total = 0;
    int best_survive = 0;

    for (const auto& r : g_results) {
        // Extraction logic:
        // If stopped out: could have extracted (peak - base) before crash
        // If survived: can extract (final - base)
        double extract_2024 = r.stopped_2024 ?
            std::max(0.0, r.peak_2024 - BASE_CAPITAL) :
            std::max(0.0, r.final_2024 - BASE_CAPITAL);

        double extract_2025 = r.stopped_2025 ?
            std::max(0.0, r.peak_2025 - BASE_CAPITAL) :
            std::max(0.0, r.final_2025 - BASE_CAPITAL);

        double total = extract_2024 + extract_2025;

        std::cout << std::setw(10) << (std::to_string((int)r.survive_pct) + "%")
                  << std::setw(10) << (r.stopped_2024 ? "SO" : "OK")
                  << std::setw(14) << ("$" + std::to_string((int)r.peak_2024))
                  << std::setw(14) << ("$" + std::to_string((int)extract_2024))
                  << std::setw(10) << (r.stopped_2025 ? "SO" : "OK")
                  << std::setw(14) << ("$" + std::to_string((int)r.peak_2025))
                  << std::setw(14) << ("$" + std::to_string((int)extract_2025))
                  << std::setw(14) << ("$" + std::to_string((int)total))
                  << "\n";

        if (total > best_total) {
            best_total = total;
            best_survive = (int)r.survive_pct;
        }
    }

    std::cout << "\n====================================================================\n";
    std::cout << "ANALYSIS\n";
    std::cout << "====================================================================\n";

    std::cout << "\nBest survive% for extraction: " << best_survive << "% -> $" << (int)best_total << "\n";

    // Find survival thresholds
    int min_survive_2024 = 13, min_survive_2025 = 13;
    for (const auto& r : g_results) {
        if (!r.stopped_2024 && r.survive_pct < min_survive_2024) min_survive_2024 = (int)r.survive_pct;
        if (!r.stopped_2025 && r.survive_pct < min_survive_2025) min_survive_2025 = (int)r.survive_pct;
    }
    std::cout << "Min survive% that survives 2024: " << min_survive_2024 << "%\n";
    std::cout << "Min survive% that survives 2025: " << min_survive_2025 << "%\n";

    std::cout << "\nCompleted in " << std::setprecision(1) << elapsed << " seconds\n";

    return 0;
}
