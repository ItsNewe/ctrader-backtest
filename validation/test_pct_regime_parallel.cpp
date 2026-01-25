/**
 * Percentage Spacing Regime Test - PARALLEL
 * Full range of pct spacings to find regime dependence pattern
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

using namespace backtest;

// Shared tick data
std::vector<Tick> g_ticks_2025;
std::vector<Tick> g_ticks_2024;

struct Task {
    double spacing_pct;
};

struct Result {
    double spacing_pct;
    double ret_2025, ret_2024;
    double dd_2025, dd_2024;
    int trades_2025, trades_2024;
};

std::mutex g_queue_mutex;
std::mutex g_results_mutex;
std::queue<Task> g_work_queue;
std::vector<Result> g_results;
std::atomic<int> g_completed{0};

double run_single(const std::vector<Tick>& ticks, const std::string& start, const std::string& end,
                  double spacing_pct) {
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

    TickBasedEngine engine(cfg);

    FillUpOscillation::AdaptiveConfig acfg;
    acfg.typical_vol_pct = 0.55;
    acfg.min_spacing_mult = 0.5;
    acfg.max_spacing_mult = 3.0;
    acfg.pct_spacing = true;
    acfg.min_spacing_abs = 0.01;
    acfg.max_spacing_abs = 10.0;
    acfg.spacing_change_threshold = 0.05;

    FillUpOscillation strategy(
        13.0, spacing_pct, 0.01, 10.0, 100.0, 500.0,
        FillUpOscillation::ADAPTIVE_SPACING,
        0.1, 30.0, 4.0, acfg
    );

    engine.RunWithTicks(ticks, [&strategy](const Tick& t, TickBasedEngine& e) {
        strategy.OnTick(t, e);
    });

    return engine.GetResults().final_balance / 10000.0;
}

struct SingleResult {
    double ret;
    double dd;
    int trades;
};

SingleResult run_full(const std::vector<Tick>& ticks, const std::string& start, const std::string& end,
                      double spacing_pct) {
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

    TickBasedEngine engine(cfg);

    FillUpOscillation::AdaptiveConfig acfg;
    acfg.typical_vol_pct = 0.55;
    acfg.min_spacing_mult = 0.5;
    acfg.max_spacing_mult = 3.0;
    acfg.pct_spacing = true;
    acfg.min_spacing_abs = 0.01;
    acfg.max_spacing_abs = 10.0;
    acfg.spacing_change_threshold = 0.05;

    FillUpOscillation strategy(
        13.0, spacing_pct, 0.01, 10.0, 100.0, 500.0,
        FillUpOscillation::ADAPTIVE_SPACING,
        0.1, 30.0, 4.0, acfg
    );

    engine.RunWithTicks(ticks, [&strategy](const Tick& t, TickBasedEngine& e) {
        strategy.OnTick(t, e);
    });

    auto res = engine.GetResults();
    return {res.final_balance / 10000.0, res.max_drawdown_pct, (int)res.total_trades};
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

        auto r2025 = run_full(g_ticks_2025, "2025.01.01", "2025.12.30", task.spacing_pct);
        auto r2024 = run_full(g_ticks_2024, "2024.01.01", "2024.12.30", task.spacing_pct);

        Result result;
        result.spacing_pct = task.spacing_pct;
        result.ret_2025 = r2025.ret;
        result.ret_2024 = r2024.ret;
        result.dd_2025 = r2025.dd;
        result.dd_2024 = r2024.dd;
        result.trades_2025 = r2025.trades;
        result.trades_2024 = r2024.trades;

        {
            std::lock_guard<std::mutex> lock(g_results_mutex);
            g_results.push_back(result);
        }

        int done = ++g_completed;
        std::cout << "  Progress: " << done << "/" << 20 << "\r" << std::flush;
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
    std::cout << "Loading tick data into shared memory..." << std::endl;

    std::cout << "  2025: " << std::flush;
    g_ticks_2025 = load_ticks("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv");
    std::cout << g_ticks_2025.size() << " ticks" << std::endl;

    std::cout << "  2024: " << std::flush;
    g_ticks_2024 = load_ticks("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAUUSD_TICKS_2024.csv");
    std::cout << g_ticks_2024.size() << " ticks" << std::endl;

    // Full range of percentage spacings
    std::vector<double> spacings = {
        0.01, 0.02, 0.03, 0.04, 0.05, 0.06, 0.08, 0.10,
        0.15, 0.20, 0.30, 0.50, 0.75, 1.00, 1.50, 2.00,
        3.00, 4.00, 5.00, 6.00
    };

    // Create work queue
    for (double sp : spacings) {
        g_work_queue.push({sp});
    }

    std::cout << "\nRunning " << spacings.size() << " configs on "
              << std::thread::hardware_concurrency() << " threads..." << std::endl;

    // Launch workers
    std::vector<std::thread> threads;
    unsigned int num_threads = std::thread::hardware_concurrency();
    for (unsigned int i = 0; i < num_threads; i++) {
        threads.emplace_back(worker);
    }

    for (auto& t : threads) t.join();

    // Sort results by spacing
    std::sort(g_results.begin(), g_results.end(),
              [](const Result& a, const Result& b) { return a.spacing_pct < b.spacing_pct; });

    std::cout << "\n\n=====================================================\n";
    std::cout << "  Percentage Spacing: Full Regime Dependence Analysis\n";
    std::cout << "=====================================================\n\n";

    std::cout << std::left << std::setw(10) << "Spacing"
              << std::right
              << std::setw(10) << "2025"
              << std::setw(10) << "2024"
              << std::setw(10) << "Ratio"
              << std::setw(10) << "2025 DD"
              << std::setw(10) << "2024 DD"
              << std::setw(12) << "2025 Trd"
              << std::setw(12) << "2024 Trd"
              << std::setw(10) << "Trd Rat"
              << std::endl;
    std::cout << std::string(94, '-') << std::endl;

    for (const auto& r : g_results) {
        double ratio = (r.ret_2024 > 0) ? r.ret_2025 / r.ret_2024 : 0;
        double trd_ratio = (r.trades_2024 > 0) ? (double)r.trades_2025 / r.trades_2024 : 0;

        std::ostringstream label;
        label << std::fixed << std::setprecision(2) << r.spacing_pct << "%";

        std::cout << std::left << std::setw(10) << label.str()
                  << std::right << std::fixed
                  << std::setw(9) << std::setprecision(2) << r.ret_2025 << "x"
                  << std::setw(9) << std::setprecision(2) << r.ret_2024 << "x"
                  << std::setw(9) << std::setprecision(2) << ratio << "x"
                  << std::setw(9) << std::setprecision(1) << r.dd_2025 << "%"
                  << std::setw(9) << std::setprecision(1) << r.dd_2024 << "%"
                  << std::setw(12) << r.trades_2025
                  << std::setw(12) << r.trades_2024
                  << std::setw(9) << std::setprecision(2) << trd_ratio << "x"
                  << std::endl;
    }

    // Find best regime-independent config
    double best_ratio = 999;
    Result best;
    for (const auto& r : g_results) {
        double ratio = (r.ret_2024 > 0) ? r.ret_2025 / r.ret_2024 : 999;
        if (ratio < best_ratio && r.ret_2024 > 1.5) {  // Must have decent 2024 return
            best_ratio = ratio;
            best = r;
        }
    }

    std::cout << "\n=====================================================\n";
    std::cout << "  Most Regime-Independent: " << best.spacing_pct << "% (ratio="
              << std::fixed << std::setprecision(2) << best_ratio << "x)\n";
    std::cout << "=====================================================\n";

    return 0;
}
