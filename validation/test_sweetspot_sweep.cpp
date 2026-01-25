/**
 * Sweet Spot Deep Sweep - Parallel
 * Focus on 0.03-0.12% spacing with fine granularity
 * Vary survive (12-13%), lookback, typvol
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <algorithm>
#include <chrono>

using namespace backtest;

// Shared tick data
std::vector<Tick> g_ticks_2025;
std::vector<Tick> g_ticks_2024;

struct Task {
    int id;
    double survive_pct;
    double spacing_pct;
    double lookback;
    double typvol;
};

struct Result {
    int id;
    double survive_pct;
    double spacing_pct;
    double lookback;
    double typvol;
    double ret_2025, ret_2024;
    double dd_2025, dd_2024;
    int trades_2025, trades_2024;
    double swap_2025, swap_2024;
};

std::mutex g_queue_mutex;
std::mutex g_results_mutex;
std::queue<Task> g_work_queue;
std::vector<Result> g_results;
std::atomic<int> g_completed{0};
int g_total_tasks = 0;

struct SingleResult {
    double ret;
    double dd;
    int trades;
    double swap;
};

SingleResult run_single(const std::vector<Tick>& ticks, const std::string& start, const std::string& end,
                        double survive_pct, double spacing_pct, double lookback, double typvol) {
    TickBacktestConfig cfg;
    cfg.symbol = "XAUUSD";
    cfg.initial_balance = 10000.0;
    cfg.account_currency = "USD";
    cfg.contract_size = 100.0;
    cfg.leverage = 500.0;
    cfg.margin_rate = 1.0;
    cfg.pip_size = 0.01;
    cfg.swap_long = -66.99;
    cfg.swap_short = 41.2;
    cfg.swap_mode = 1;
    cfg.swap_3days = 3;
    cfg.start_date = start;
    cfg.end_date = end;
    cfg.verbose = false;

    TickDataConfig tc;
    tc.file_path = "";
    tc.load_all_into_memory = false;
    cfg.tick_data_config = tc;

    try {
        TickBasedEngine engine(cfg);

        FillUpOscillation::AdaptiveConfig acfg;
        acfg.typical_vol_pct = typvol;
        acfg.min_spacing_mult = 0.5;
        acfg.max_spacing_mult = 3.0;
        acfg.pct_spacing = true;
        acfg.min_spacing_abs = 0.01;
        acfg.max_spacing_abs = 5.0;
        acfg.spacing_change_threshold = 0.02;

        FillUpOscillation strategy(
            survive_pct, spacing_pct, 0.01, 10.0, 100.0, 500.0,
            FillUpOscillation::ADAPTIVE_SPACING,
            0.1, 30.0, lookback, acfg
        );

        engine.RunWithTicks(ticks, [&strategy](const Tick& t, TickBasedEngine& e) {
            strategy.OnTick(t, e);
        });

        auto res = engine.GetResults();
        return {res.final_balance / 10000.0, res.max_drawdown_pct, (int)res.total_trades, res.total_swap_charged};
    } catch (...) {
        return {0, 100, 0, 0};
    }
}

void worker() {
    while (true) {
        Task task;
        {
            std::lock_guard<std::mutex> lock(g_queue_mutex);
            if (g_work_queue.empty()) return;
            task = g_work_queue.front();
            g_work_queue.pop();
        }

        auto r2025 = run_single(g_ticks_2025, "2025.01.01", "2025.12.30",
                                task.survive_pct, task.spacing_pct, task.lookback, task.typvol);
        auto r2024 = run_single(g_ticks_2024, "2024.01.01", "2024.12.30",
                                task.survive_pct, task.spacing_pct, task.lookback, task.typvol);

        Result result;
        result.id = task.id;
        result.survive_pct = task.survive_pct;
        result.spacing_pct = task.spacing_pct;
        result.lookback = task.lookback;
        result.typvol = task.typvol;
        result.ret_2025 = r2025.ret;
        result.ret_2024 = r2024.ret;
        result.dd_2025 = r2025.dd;
        result.dd_2024 = r2024.dd;
        result.trades_2025 = r2025.trades;
        result.trades_2024 = r2024.trades;
        result.swap_2025 = r2025.swap;
        result.swap_2024 = r2024.swap;

        {
            std::lock_guard<std::mutex> lock(g_results_mutex);
            g_results.push_back(result);
        }

        int done = ++g_completed;
        if (done % 50 == 0 || done == g_total_tasks) {
            std::cout << "  Progress: " << done << "/" << g_total_tasks
                      << " (" << (100 * done / g_total_tasks) << "%)\r" << std::flush;
        }
    }
}

std::vector<Tick> load_ticks(const std::string& path) {
    std::vector<Tick> ticks;
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Cannot open: " << path << std::endl;
        return ticks;
    }

    std::string line;
    std::getline(file, line);

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        Tick tick;
        std::stringstream ss(line);
        std::string datetime_str, bid_str, ask_str;
        std::getline(ss, datetime_str, '\t');
        std::getline(ss, bid_str, '\t');
        std::getline(ss, ask_str, '\t');
        tick.timestamp = datetime_str;
        tick.bid = std::stod(bid_str);
        tick.ask = std::stod(ask_str);
        tick.volume = 0;
        ticks.push_back(tick);
    }
    return ticks;
}

int main() {
    auto start_time = std::chrono::steady_clock::now();

    std::cout << "=====================================================\n";
    std::cout << "  Sweet Spot Deep Sweep (Percentage Spacing)\n";
    std::cout << "  Focus: 0.03% - 0.12% spacing\n";
    std::cout << "=====================================================\n\n";

    std::cout << "Loading tick data into shared memory..." << std::endl;

    std::cout << "  2025: " << std::flush;
    g_ticks_2025 = load_ticks("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv");
    std::cout << g_ticks_2025.size() << " ticks" << std::endl;

    std::cout << "  2024: " << std::flush;
    g_ticks_2024 = load_ticks("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAUUSD_TICKS_2024.csv");
    std::cout << g_ticks_2024.size() << " ticks" << std::endl;

    // Parameter grids - focused on sweet spot
    std::vector<double> survives = {12.0, 13.0};

    // Fine granularity around sweet spot (0.03-0.12%)
    std::vector<double> spacings = {
        0.03, 0.035, 0.04, 0.045, 0.05, 0.055, 0.06, 0.065, 0.07, 0.075,
        0.08, 0.085, 0.09, 0.095, 0.10, 0.11, 0.12
    };

    std::vector<double> lookbacks = {0.5, 1.0, 2.0, 4.0, 8.0, 16.0};
    std::vector<double> typvols = {0.20, 0.35, 0.55, 0.80, 1.20};

    // Create work queue
    int id = 0;
    for (double surv : survives) {
        for (double sp : spacings) {
            for (double lb : lookbacks) {
                for (double tv : typvols) {
                    g_work_queue.push({id++, surv, sp, lb, tv});
                }
            }
        }
    }
    g_total_tasks = id;

    std::cout << "\nTotal configurations: " << g_total_tasks << std::endl;
    std::cout << "  Survive: " << survives.size() << " values (12%, 13%)" << std::endl;
    std::cout << "  Spacing: " << spacings.size() << " values (0.03% - 0.12%)" << std::endl;
    std::cout << "  Lookback: " << lookbacks.size() << " values (0.5h - 16h)" << std::endl;
    std::cout << "  TypVol: " << typvols.size() << " values (0.20% - 1.20%)" << std::endl;

    unsigned int num_threads = std::thread::hardware_concurrency();
    std::cout << "\nRunning on " << num_threads << " threads..." << std::endl;

    // Launch workers
    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < num_threads; i++) {
        threads.emplace_back(worker);
    }

    for (auto& t : threads) t.join();

    auto end_time = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count();
    std::cout << "\n\nCompleted in " << elapsed << "s ("
              << std::fixed << std::setprecision(2) << (double)elapsed / g_total_tasks << "s/config)\n";

    // Sort by regime independence (ratio closest to 1.0)
    std::sort(g_results.begin(), g_results.end(), [](const Result& a, const Result& b) {
        double ratio_a = (a.ret_2024 > 0.5) ? a.ret_2025 / a.ret_2024 : 999;
        double ratio_b = (b.ret_2024 > 0.5) ? b.ret_2025 / b.ret_2024 : 999;
        return ratio_a < ratio_b;
    });

    // Top 40 most regime-independent
    std::cout << "\n=====================================================\n";
    std::cout << "  TOP 40 MOST REGIME-INDEPENDENT (lowest 2025/2024 ratio)\n";
    std::cout << "=====================================================\n\n";

    std::cout << std::left << std::setw(35) << "Config"
              << std::right
              << std::setw(8) << "2025"
              << std::setw(8) << "2024"
              << std::setw(8) << "Ratio"
              << std::setw(8) << "25 DD"
              << std::setw(8) << "24 DD"
              << std::setw(10) << "25 Trd"
              << std::setw(10) << "24 Trd"
              << std::endl;
    std::cout << std::string(95, '-') << std::endl;

    int count = 0;
    for (const auto& r : g_results) {
        if (count++ >= 40) break;
        if (r.ret_2024 < 0.5) continue;  // Skip failures

        double ratio = r.ret_2025 / r.ret_2024;

        std::ostringstream label;
        label << "s" << (int)r.survive_pct << "_sp" << std::fixed << std::setprecision(3) << r.spacing_pct
              << "%_lb" << std::setprecision(1) << r.lookback << "_tv" << std::setprecision(2) << r.typvol;

        std::cout << std::left << std::setw(35) << label.str()
                  << std::right << std::fixed
                  << std::setw(7) << std::setprecision(2) << r.ret_2025 << "x"
                  << std::setw(7) << std::setprecision(2) << r.ret_2024 << "x"
                  << std::setw(7) << std::setprecision(2) << ratio << "x"
                  << std::setw(7) << std::setprecision(1) << r.dd_2025 << "%"
                  << std::setw(7) << std::setprecision(1) << r.dd_2024 << "%"
                  << std::setw(10) << r.trades_2025
                  << std::setw(10) << r.trades_2024
                  << std::endl;
    }

    // Sort by 2025 return (best performers)
    std::sort(g_results.begin(), g_results.end(), [](const Result& a, const Result& b) {
        return a.ret_2025 > b.ret_2025;
    });

    std::cout << "\n=====================================================\n";
    std::cout << "  TOP 30 BY 2025 RETURN\n";
    std::cout << "=====================================================\n\n";

    std::cout << std::left << std::setw(35) << "Config"
              << std::right
              << std::setw(8) << "2025"
              << std::setw(8) << "2024"
              << std::setw(8) << "Ratio"
              << std::setw(8) << "25 DD"
              << std::setw(8) << "24 DD"
              << std::endl;
    std::cout << std::string(75, '-') << std::endl;

    count = 0;
    for (const auto& r : g_results) {
        if (count++ >= 30) break;

        double ratio = (r.ret_2024 > 0) ? r.ret_2025 / r.ret_2024 : 0;

        std::ostringstream label;
        label << "s" << (int)r.survive_pct << "_sp" << std::fixed << std::setprecision(3) << r.spacing_pct
              << "%_lb" << std::setprecision(1) << r.lookback << "_tv" << std::setprecision(2) << r.typvol;

        std::cout << std::left << std::setw(35) << label.str()
                  << std::right << std::fixed
                  << std::setw(7) << std::setprecision(2) << r.ret_2025 << "x"
                  << std::setw(7) << std::setprecision(2) << r.ret_2024 << "x"
                  << std::setw(7) << std::setprecision(2) << ratio << "x"
                  << std::setw(7) << std::setprecision(1) << r.dd_2025 << "%"
                  << std::setw(7) << std::setprecision(1) << r.dd_2024 << "%"
                  << std::endl;
    }

    // Sort by lowest DD (2025)
    std::sort(g_results.begin(), g_results.end(), [](const Result& a, const Result& b) {
        return a.dd_2025 < b.dd_2025;
    });

    std::cout << "\n=====================================================\n";
    std::cout << "  TOP 30 BY LOWEST DRAWDOWN (2025)\n";
    std::cout << "=====================================================\n\n";

    std::cout << std::left << std::setw(35) << "Config"
              << std::right
              << std::setw(8) << "2025"
              << std::setw(8) << "2024"
              << std::setw(8) << "Ratio"
              << std::setw(8) << "25 DD"
              << std::setw(8) << "24 DD"
              << std::endl;
    std::cout << std::string(75, '-') << std::endl;

    count = 0;
    for (const auto& r : g_results) {
        if (count++ >= 30) break;
        if (r.ret_2025 < 1.5) continue;  // Must have positive return

        double ratio = (r.ret_2024 > 0) ? r.ret_2025 / r.ret_2024 : 0;

        std::ostringstream label;
        label << "s" << (int)r.survive_pct << "_sp" << std::fixed << std::setprecision(3) << r.spacing_pct
              << "%_lb" << std::setprecision(1) << r.lookback << "_tv" << std::setprecision(2) << r.typvol;

        std::cout << std::left << std::setw(35) << label.str()
                  << std::right << std::fixed
                  << std::setw(7) << std::setprecision(2) << r.ret_2025 << "x"
                  << std::setw(7) << std::setprecision(2) << r.ret_2024 << "x"
                  << std::setw(7) << std::setprecision(2) << ratio << "x"
                  << std::setw(7) << std::setprecision(1) << r.dd_2025 << "%"
                  << std::setw(7) << std::setprecision(1) << r.dd_2024 << "%"
                  << std::endl;
    }

    // Analysis by spacing
    std::cout << "\n=====================================================\n";
    std::cout << "  ANALYSIS BY SPACING (averaged across all other params)\n";
    std::cout << "=====================================================\n\n";

    std::map<double, std::vector<Result>> by_spacing;
    for (const auto& r : g_results) {
        by_spacing[r.spacing_pct].push_back(r);
    }

    std::cout << std::left << std::setw(10) << "Spacing"
              << std::right
              << std::setw(10) << "Avg 2025"
              << std::setw(10) << "Avg 2024"
              << std::setw(10) << "Avg Ratio"
              << std::setw(10) << "Avg 25DD"
              << std::setw(10) << "Avg 24DD"
              << std::setw(8) << "Count"
              << std::endl;
    std::cout << std::string(68, '-') << std::endl;

    for (auto& [sp, results] : by_spacing) {
        double sum_2025 = 0, sum_2024 = 0, sum_dd25 = 0, sum_dd24 = 0;
        int n = 0;
        for (const auto& r : results) {
            if (r.ret_2024 > 0.5) {
                sum_2025 += r.ret_2025;
                sum_2024 += r.ret_2024;
                sum_dd25 += r.dd_2025;
                sum_dd24 += r.dd_2024;
                n++;
            }
        }
        if (n > 0) {
            double avg_2025 = sum_2025 / n;
            double avg_2024 = sum_2024 / n;
            double avg_ratio = avg_2025 / avg_2024;
            double avg_dd25 = sum_dd25 / n;
            double avg_dd24 = sum_dd24 / n;

            std::cout << std::left << std::setw(10) << (std::to_string(sp).substr(0,5) + "%")
                      << std::right << std::fixed
                      << std::setw(9) << std::setprecision(2) << avg_2025 << "x"
                      << std::setw(9) << std::setprecision(2) << avg_2024 << "x"
                      << std::setw(9) << std::setprecision(2) << avg_ratio << "x"
                      << std::setw(9) << std::setprecision(1) << avg_dd25 << "%"
                      << std::setw(9) << std::setprecision(1) << avg_dd24 << "%"
                      << std::setw(8) << n
                      << std::endl;
        }
    }

    // Analysis by survive
    std::cout << "\n=====================================================\n";
    std::cout << "  SURVIVE 12% vs 13%\n";
    std::cout << "=====================================================\n\n";

    for (double surv : survives) {
        double sum_2025 = 0, sum_2024 = 0, sum_dd25 = 0;
        int n = 0;
        for (const auto& r : g_results) {
            if (r.survive_pct == surv && r.ret_2024 > 0.5) {
                sum_2025 += r.ret_2025;
                sum_2024 += r.ret_2024;
                sum_dd25 += r.dd_2025;
                n++;
            }
        }
        if (n > 0) {
            std::cout << "survive=" << (int)surv << "%: avg_2025=" << std::fixed << std::setprecision(2)
                      << (sum_2025/n) << "x, avg_2024=" << (sum_2024/n) << "x, avg_ratio="
                      << (sum_2025/sum_2024) << "x, avg_dd=" << std::setprecision(1) << (sum_dd25/n)
                      << "%, n=" << n << std::endl;
        }
    }

    std::cout << "\nDone.\n";

    return 0;
}
