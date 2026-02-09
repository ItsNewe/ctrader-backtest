/**
 * ParallelDual Original Strategy - Conservative Sweep
 *
 * After finding that all previous params fail the Dec 29 crash,
 * this sweep focuses on survival-first parameters:
 * - Higher survive_pct (15-40%)
 * - Lower max_volume (5-50 lots instead of 100)
 * - Lower allocations
 */

#include "../include/strategy_parallel_dual_original.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>
#include <fstream>
#include <sstream>

using namespace backtest;

struct TestParams {
    double survive_pct;
    double grid_allocation;
    double base_spacing;
    double momentum_spacing;
    double tp_distance;
    double max_volume;
};

struct SweepResult {
    TestParams params;
    double final_balance;
    double max_drawdown_pct;
    double return_multiple;
    int total_entries;
    int grid_entries;
    int momentum_entries;
    int skipped_by_margin;
};

SweepResult RunTest(const std::vector<Tick>& ticks, const TestParams& params,
                    const std::string& symbol, double contract_size,
                    double pip_size, double swap_long, double swap_short,
                    const std::string& start_date, const std::string& end_date) {

    TickBacktestConfig config;
    config.symbol = symbol;
    config.initial_balance = 10000.0;
    config.contract_size = contract_size;
    config.leverage = 500.0;
    config.pip_size = pip_size;
    config.swap_long = swap_long;
    config.swap_short = swap_short;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = start_date;
    config.end_date = end_date;
    config.verbose = false;

    TickBasedEngine engine(config);

    ParallelDualStrategyOriginal::Config strat_config;
    strat_config.survive_pct = params.survive_pct;
    strat_config.grid_allocation = params.grid_allocation;
    strat_config.momentum_allocation = 1.0 - params.grid_allocation;
    strat_config.min_volume = 0.01;
    strat_config.max_volume = params.max_volume;
    strat_config.contract_size = contract_size;
    strat_config.leverage = 500.0;
    strat_config.base_spacing = params.base_spacing;
    strat_config.momentum_spacing = params.momentum_spacing;
    strat_config.margin_stop_out = 20.0;
    strat_config.force_min_volume_entry = false;
    strat_config.use_take_profit = true;
    strat_config.tp_distance = params.tp_distance;

    ParallelDualStrategyOriginal strategy(strat_config);

    engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();
    auto stats = strategy.GetStats();

    SweepResult result;
    result.params = params;
    result.final_balance = results.final_balance;
    result.max_drawdown_pct = results.max_drawdown_pct;
    result.return_multiple = results.final_balance / 10000.0;
    result.total_entries = stats.total_entries;
    result.grid_entries = stats.grid_entries;
    result.momentum_entries = stats.momentum_entries;
    result.skipped_by_margin = stats.skipped_by_margin;

    return result;
}

std::vector<Tick> LoadTicks(const std::string& file_path, const std::string& start_date, const std::string& end_date) {
    std::vector<Tick> ticks;

    std::ifstream file(file_path);
    if (!file.is_open()) {
        std::cerr << "Failed to open: " << file_path << std::endl;
        return ticks;
    }

    std::string line;
    std::getline(file, line); // Skip header

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        Tick tick;
        size_t pos = 0;

        pos = line.find('\t');
        if (pos == std::string::npos) continue;
        tick.timestamp = line.substr(0, pos);
        line = line.substr(pos + 1);

        std::string date_str = tick.timestamp.substr(0, 10);
        if (date_str < start_date || date_str > end_date) continue;

        pos = line.find('\t');
        if (pos == std::string::npos) continue;
        tick.bid = std::stod(line.substr(0, pos));
        line = line.substr(pos + 1);

        pos = line.find('\t');
        if (pos == std::string::npos) pos = line.length();
        tick.ask = std::stod(line.substr(0, pos));

        ticks.push_back(tick);
    }

    return ticks;
}

int main() {
    std::cout << "==================================================" << std::endl;
    std::cout << "   ParallelDual XAGUSD - Conservative Sweep" << std::endl;
    std::cout << "   Focus: Survive the Dec 29 crash" << std::endl;
    std::cout << "==================================================" << std::endl;

    std::cout << "\nLoading MT5 tick data..." << std::endl;
    std::string start_date = "2024.12.31";
    std::string end_date = "2026.01.30";
    auto ticks = LoadTicks(
        "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\XAGUSD\\XAGUSD_TICKS_MT5_EXPORT.csv",
        start_date, end_date);
    std::cout << "Loaded " << ticks.size() << " ticks" << std::endl;

    if (ticks.empty()) {
        std::cerr << "No ticks loaded!" << std::endl;
        return 1;
    }

    // Conservative parameter ranges
    std::vector<double> survive_values = {15, 20, 25, 30, 35, 40};     // Higher survive
    std::vector<double> grid_alloc_values = {0.05, 0.10, 0.15, 0.20};   // Lower allocations
    std::vector<double> base_spacing_values = {0.10, 0.20, 0.30, 0.40, 0.50};  // Wider spacing
    std::vector<double> momentum_spacing_values = {0.50, 1.0, 1.5, 2.0};  // Wider momentum spacing
    std::vector<double> tp_distance_values = {0.30, 0.50, 0.80, 1.0};   // Various TPs
    std::vector<double> max_volume_values = {5, 10, 20, 50};            // Lower max volumes

    // Build all parameter combinations
    std::vector<TestParams> all_params;
    for (double survive : survive_values) {
        for (double grid_alloc : grid_alloc_values) {
            for (double base_sp : base_spacing_values) {
                for (double mom_sp : momentum_spacing_values) {
                    for (double tp : tp_distance_values) {
                        for (double max_vol : max_volume_values) {
                            all_params.push_back({survive, grid_alloc, base_sp, mom_sp, tp, max_vol});
                        }
                    }
                }
            }
        }
    }

    std::cout << "Testing " << all_params.size() << " parameter combinations" << std::endl;
    std::cout << "  survive_pct: " << survive_values.size() << " values (15-40%)" << std::endl;
    std::cout << "  grid_alloc: " << grid_alloc_values.size() << " values (5-20%)" << std::endl;
    std::cout << "  max_volume: " << max_volume_values.size() << " values (5-50 lots)" << std::endl;

    std::vector<SweepResult> results(all_params.size());
    std::atomic<int> completed{0};
    std::mutex output_mutex;

    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;
    std::cout << "\nUsing " << num_threads << " threads\n" << std::endl;

    auto worker = [&](size_t w_start_idx, size_t w_end_idx) {
        for (size_t i = w_start_idx; i < w_end_idx; ++i) {
            results[i] = RunTest(ticks, all_params[i], "XAGUSD", 5000.0,
                                 0.001, -22.34, 0.13, start_date, end_date);
            int done = ++completed;
            if (done % 500 == 0 || done == (int)all_params.size()) {
                std::lock_guard<std::mutex> lock(output_mutex);
                double pct = 100.0 * done / all_params.size();
                std::cout << "  Progress: " << done << "/" << all_params.size()
                          << " (" << std::fixed << std::setprecision(1) << pct << "%)" << std::endl;
            }
        }
    };

    std::vector<std::thread> threads;
    size_t chunk_size = (all_params.size() + num_threads - 1) / num_threads;

    for (unsigned int t = 0; t < num_threads; ++t) {
        size_t start_idx = t * chunk_size;
        size_t end_idx = std::min(start_idx + chunk_size, all_params.size());
        if (start_idx < all_params.size()) {
            threads.emplace_back(worker, start_idx, end_idx);
        }
    }

    for (auto& t : threads) {
        t.join();
    }

    // Filter for DD < 90% first (survival focus)
    std::vector<SweepResult> survivors;
    for (const auto& r : results) {
        if (r.max_drawdown_pct < 90.0 && r.final_balance > 10000.0) {
            survivors.push_back(r);
        }
    }

    std::cout << "\n=== SURVIVORS (DD < 90%, profit > 0) ===" << std::endl;
    std::cout << "Found " << survivors.size() << " surviving parameter sets\n" << std::endl;

    if (survivors.empty()) {
        std::cout << "No survivors found! All parameter combinations failed the crash.\n" << std::endl;

        // Show the least-bad results
        std::sort(results.begin(), results.end(), [](const SweepResult& a, const SweepResult& b) {
            return a.max_drawdown_pct < b.max_drawdown_pct;
        });

        std::cout << "=== LEAST-BAD RESULTS (by drawdown) ===" << std::endl;
        std::cout << std::fixed << std::setprecision(2);
        std::cout << std::setw(8) << "Surv%"
                  << std::setw(8) << "Grid%"
                  << std::setw(8) << "BaseSp"
                  << std::setw(8) << "MomSp"
                  << std::setw(8) << "TP"
                  << std::setw(8) << "MaxVol"
                  << std::setw(12) << "Final$"
                  << std::setw(10) << "Return"
                  << std::setw(8) << "DD%"
                  << std::endl;
        std::cout << std::string(90, '-') << std::endl;

        for (size_t i = 0; i < std::min((size_t)20, results.size()); ++i) {
            const auto& r = results[i];
            std::cout << std::setw(8) << r.params.survive_pct
                      << std::setw(8) << (r.params.grid_allocation * 100)
                      << std::setw(8) << r.params.base_spacing
                      << std::setw(8) << r.params.momentum_spacing
                      << std::setw(8) << r.params.tp_distance
                      << std::setw(8) << r.params.max_volume
                      << std::setw(12) << r.final_balance
                      << std::setw(9) << r.return_multiple << "x"
                      << std::setw(8) << r.max_drawdown_pct
                      << std::endl;
        }
    } else {
        // Sort survivors by return
        std::sort(survivors.begin(), survivors.end(), [](const SweepResult& a, const SweepResult& b) {
            return a.return_multiple > b.return_multiple;
        });

        std::cout << std::fixed << std::setprecision(2);
        std::cout << std::setw(8) << "Surv%"
                  << std::setw(8) << "Grid%"
                  << std::setw(8) << "BaseSp"
                  << std::setw(8) << "MomSp"
                  << std::setw(8) << "TP"
                  << std::setw(8) << "MaxVol"
                  << std::setw(12) << "Final$"
                  << std::setw(10) << "Return"
                  << std::setw(8) << "DD%"
                  << std::endl;
        std::cout << std::string(90, '-') << std::endl;

        for (size_t i = 0; i < std::min((size_t)20, survivors.size()); ++i) {
            const auto& r = survivors[i];
            std::cout << std::setw(8) << r.params.survive_pct
                      << std::setw(8) << (r.params.grid_allocation * 100)
                      << std::setw(8) << r.params.base_spacing
                      << std::setw(8) << r.params.momentum_spacing
                      << std::setw(8) << r.params.tp_distance
                      << std::setw(8) << r.params.max_volume
                      << std::setw(12) << r.final_balance
                      << std::setw(9) << r.return_multiple << "x"
                      << std::setw(8) << r.max_drawdown_pct
                      << std::endl;
        }

        std::cout << "\n=== BEST SURVIVOR ===" << std::endl;
        const auto& best = survivors[0];
        std::cout << "survive_pct = " << best.params.survive_pct << "%" << std::endl;
        std::cout << "grid_allocation = " << best.params.grid_allocation << std::endl;
        std::cout << "momentum_allocation = " << (1.0 - best.params.grid_allocation) << std::endl;
        std::cout << "base_spacing = $" << best.params.base_spacing << std::endl;
        std::cout << "momentum_spacing = $" << best.params.momentum_spacing << std::endl;
        std::cout << "tp_distance = $" << best.params.tp_distance << std::endl;
        std::cout << "max_volume = " << best.params.max_volume << " lots" << std::endl;
        std::cout << "Result: " << best.return_multiple << "x, DD=" << best.max_drawdown_pct << "%" << std::endl;
    }

    return 0;
}
