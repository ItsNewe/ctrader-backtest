/**
 * ParallelDual Original Strategy - Multi-Parameter Sweep
 *
 * Sweeps: survive_pct, grid_allocation, base_spacing, momentum_spacing, tp_distance
 * Uses broker max volume limits queried from MT5
 * Date range: 2025.01.01 - 2026.01.29
 *
 * RUN mt5/QuerySymbolInfo.mq5 FIRST to get actual broker limits!
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
                    double max_volume, double pip_size,
                    double swap_long, double swap_short,
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
    config.verbose = false;  // Disable logging for sweep performance

    TickBasedEngine engine(config);

    // Configure strategy
    ParallelDualStrategyOriginal::Config strat_config;
    strat_config.survive_pct = params.survive_pct;
    strat_config.grid_allocation = params.grid_allocation;
    strat_config.momentum_allocation = 1.0 - params.grid_allocation;  // Complement
    strat_config.min_volume = 0.01;
    strat_config.max_volume = max_volume;
    strat_config.contract_size = contract_size;
    strat_config.leverage = 500.0;
    strat_config.base_spacing = params.base_spacing;
    strat_config.momentum_spacing = params.momentum_spacing;
    strat_config.margin_stop_out = 20.0;
    strat_config.force_min_volume_entry = false;
    strat_config.use_take_profit = true;
    strat_config.tp_distance = params.tp_distance;

    ParallelDualStrategyOriginal strategy(strat_config);

    // Run backtest using RunWithTicks
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

    // MT5 tick CSV format: Time\tBid\tAsk\tLast\tVolume\tFlags
    // Time is full datetime with space: "2024.12.31 23:00:00.129"

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        Tick tick;
        size_t pos = 0;

        // Timestamp (full datetime in first column)
        pos = line.find('\t');
        if (pos == std::string::npos) continue;
        tick.timestamp = line.substr(0, pos);
        line = line.substr(pos + 1);

        // Extract date for filtering (first 10 chars: "2024.12.31")
        std::string date_str = tick.timestamp.substr(0, 10);
        if (date_str < start_date || date_str > end_date) continue;

        // Bid
        pos = line.find('\t');
        if (pos == std::string::npos) continue;
        tick.bid = std::stod(line.substr(0, pos));
        line = line.substr(pos + 1);

        // Ask
        pos = line.find('\t');
        if (pos == std::string::npos) pos = line.length();
        tick.ask = std::stod(line.substr(0, pos));

        ticks.push_back(tick);
    }

    return ticks;
}

void RunSweep(const std::string& symbol, const std::string& tick_file,
              double contract_size, double max_volume, double pip_size,
              double swap_long, double swap_short) {

    std::cout << "\n========================================" << std::endl;
    std::cout << " ParallelDual Multi-Param Sweep: " << symbol << std::endl;
    std::cout << " Max Volume: " << max_volume << " lots (broker limit)" << std::endl;
    std::cout << " Date: 2025.01.01 - 2026.01.29" << std::endl;
    std::cout << "========================================\n" << std::endl;

    // Load ticks - use full date range for both symbols (2025.01.01 - 2026.01.29)
    std::cout << "Loading tick data..." << std::endl;
    std::string start_date = "2024.12.31";
    std::string end_date = "2026.01.30";
    auto ticks = LoadTicks(tick_file, start_date, end_date);
    std::cout << "Loaded " << ticks.size() << " ticks" << std::endl;

    if (ticks.empty()) {
        std::cerr << "No ticks loaded!" << std::endl;
        return;
    }

    // Define parameter ranges based on symbol
    std::vector<double> survive_values;
    std::vector<double> grid_alloc_values;
    std::vector<double> base_spacing_values;
    std::vector<double> momentum_spacing_values;
    std::vector<double> tp_distance_values;

    if (symbol == "XAUUSD") {
        // XAUUSD ranges (price ~$2600-3000)
        for (double s = 5; s <= 25; s += 2) survive_values.push_back(s);           // 5-25%
        for (double g = 0.05; g <= 0.30; g += 0.05) grid_alloc_values.push_back(g); // 5-30%
        for (double b = 0.50; b <= 3.0; b += 0.50) base_spacing_values.push_back(b); // $0.50-$3.00
        for (double m = 2.0; m <= 10.0; m += 2.0) momentum_spacing_values.push_back(m); // $2-$10
        for (double t = 2.0; t <= 10.0; t += 2.0) tp_distance_values.push_back(t);  // $2-$10
    } else {
        // XAGUSD ranges (price ~$28-35)
        for (double s = 5; s <= 25; s += 2) survive_values.push_back(s);           // 5-25%
        for (double g = 0.05; g <= 0.30; g += 0.05) grid_alloc_values.push_back(g); // 5-30%
        for (double b = 0.05; b <= 0.30; b += 0.05) base_spacing_values.push_back(b); // $0.05-$0.30
        for (double m = 0.20; m <= 1.0; m += 0.20) momentum_spacing_values.push_back(m); // $0.20-$1.00
        for (double t = 0.20; t <= 1.0; t += 0.20) tp_distance_values.push_back(t);  // $0.20-$1.00
    }

    // Build all parameter combinations
    std::vector<TestParams> all_params;
    for (double survive : survive_values) {
        for (double grid_alloc : grid_alloc_values) {
            for (double base_sp : base_spacing_values) {
                for (double mom_sp : momentum_spacing_values) {
                    for (double tp : tp_distance_values) {
                        all_params.push_back({survive, grid_alloc, base_sp, mom_sp, tp});
                    }
                }
            }
        }
    }

    std::cout << "Testing " << all_params.size() << " parameter combinations:" << std::endl;
    std::cout << "  survive_pct: " << survive_values.size() << " values ("
              << survive_values.front() << "-" << survive_values.back() << "%)" << std::endl;
    std::cout << "  grid_alloc: " << grid_alloc_values.size() << " values ("
              << grid_alloc_values.front()*100 << "-" << grid_alloc_values.back()*100 << "%)" << std::endl;
    std::cout << "  base_spacing: " << base_spacing_values.size() << " values" << std::endl;
    std::cout << "  momentum_spacing: " << momentum_spacing_values.size() << " values" << std::endl;
    std::cout << "  tp_distance: " << tp_distance_values.size() << " values" << std::endl;

    // Results storage
    std::vector<SweepResult> results(all_params.size());
    std::atomic<int> completed{0};
    std::mutex output_mutex;

    // Parallel execution
    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;
    std::cout << "\nUsing " << num_threads << " threads" << std::endl;
    std::cout << "Estimated time: ~" << (all_params.size() * 2 / num_threads / 60) << " minutes\n" << std::endl;

    auto worker = [&](size_t w_start_idx, size_t w_end_idx) {
        for (size_t i = w_start_idx; i < w_end_idx; ++i) {
            results[i] = RunTest(ticks, all_params[i], symbol, contract_size,
                                 max_volume, pip_size, swap_long, swap_short,
                                 start_date, end_date);
            int done = ++completed;
            if (done % 100 == 0 || done == (int)all_params.size()) {
                std::lock_guard<std::mutex> lock(output_mutex);
                double pct = 100.0 * done / all_params.size();
                std::cout << "  Progress: " << done << "/" << all_params.size()
                          << " (" << std::fixed << std::setprecision(1) << pct << "%)" << std::endl;
            }
        }
    };

    // Launch threads
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

    // Sort by return multiple
    std::sort(results.begin(), results.end(), [](const SweepResult& a, const SweepResult& b) {
        return a.return_multiple > b.return_multiple;
    });

    // Print top 20 results
    std::cout << "\n=== TOP 20 RESULTS (sorted by return) ===" << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << std::setw(8) << "Surv%"
              << std::setw(8) << "Grid%"
              << std::setw(8) << "BaseSp"
              << std::setw(8) << "MomSp"
              << std::setw(8) << "TP"
              << std::setw(12) << "Final$"
              << std::setw(10) << "Return"
              << std::setw(8) << "DD%"
              << std::setw(8) << "Entries"
              << std::endl;
    std::cout << std::string(86, '-') << std::endl;

    for (size_t i = 0; i < std::min((size_t)20, results.size()); ++i) {
        const auto& r = results[i];
        std::cout << std::setw(8) << r.params.survive_pct
                  << std::setw(8) << (r.params.grid_allocation * 100)
                  << std::setw(8) << r.params.base_spacing
                  << std::setw(8) << r.params.momentum_spacing
                  << std::setw(8) << r.params.tp_distance
                  << std::setw(12) << r.final_balance
                  << std::setw(9) << r.return_multiple << "x"
                  << std::setw(8) << r.max_drawdown_pct
                  << std::setw(8) << r.total_entries
                  << std::endl;
    }

    // Print worst 5 (to understand failures)
    std::cout << "\n=== WORST 5 (for analysis) ===" << std::endl;
    for (size_t i = results.size() - 5; i < results.size(); ++i) {
        const auto& r = results[i];
        std::cout << "Surv=" << r.params.survive_pct << "% Grid=" << (r.params.grid_allocation*100)
                  << "% BaseSp=$" << r.params.base_spacing << " MomSp=$" << r.params.momentum_spacing
                  << " TP=$" << r.params.tp_distance
                  << " -> " << r.return_multiple << "x, DD=" << r.max_drawdown_pct << "%"
                  << std::endl;
    }

    // Best parameters summary
    std::cout << "\n=== BEST PARAMETERS for " << symbol << " ===" << std::endl;
    const auto& best = results[0];
    std::cout << "survive_pct = " << best.params.survive_pct << "%" << std::endl;
    std::cout << "grid_allocation = " << best.params.grid_allocation << " (" << (best.params.grid_allocation*100) << "%)" << std::endl;
    std::cout << "momentum_allocation = " << (1.0 - best.params.grid_allocation) << " (" << ((1.0-best.params.grid_allocation)*100) << "%)" << std::endl;
    std::cout << "base_spacing = $" << best.params.base_spacing << std::endl;
    std::cout << "momentum_spacing = $" << best.params.momentum_spacing << std::endl;
    std::cout << "tp_distance = $" << best.params.tp_distance << std::endl;
    std::cout << "Result: " << best.return_multiple << "x ($" << best.final_balance << "), DD=" << best.max_drawdown_pct << "%" << std::endl;
    std::cout << "Entries: " << best.total_entries << " (Grid:" << best.grid_entries << " Mom:" << best.momentum_entries << ")" << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "==================================================" << std::endl;
    std::cout << "    ParallelDual Original - Multi-Param Sweep" << std::endl;
    std::cout << "==================================================" << std::endl;
    std::cout << "\nNOTE: Run mt5/QuerySymbolInfo.mq5 first to get actual broker limits!" << std::endl;

    // Check command line for which symbol to run
    std::string mode = "both";
    if (argc > 1) {
        mode = argv[1];
    }

    // ============================================================
    // BROKER LIMITS - Queried from  via MT5 Python API
    // Date: 2026-02-01
    // ============================================================
    // XAUUSD: max_volume=100, swap_long=-78.57, swap_short=39.14
    // XAGUSD: max_volume=100, swap_long=-22.34, swap_short=0.13
    double xauusd_max_volume = 100.0;
    double xagusd_max_volume = 100.0;

    if (mode == "gold" || mode == "XAUUSD" || mode == "both") {
        // XAUUSD Sweep ( Live)
        RunSweep(
            "XAUUSD",
            "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv",
            100.0,              // contract_size
            xauusd_max_volume,  // max_volume ( limit)
            0.01,               // pip_size
            -78.57,             // swap_long (from MT5)
            39.14               // swap_short (from MT5)
        );
    }

    if (mode == "silver" || mode == "XAGUSD" || mode == "both") {
        // XAGUSD Sweep ( Live) - Using MT5 exported tick data
        RunSweep(
            "XAGUSD",
            "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\XAGUSD\\XAGUSD_TICKS_MT5_EXPORT.csv",
            5000.0,             // contract_size
            xagusd_max_volume,  // max_volume ( limit)
            0.001,              // pip_size
            -22.34,             // swap_long (from MT5)
            0.13                // swap_short (from MT5)
        );
    }

    return 0;
}
