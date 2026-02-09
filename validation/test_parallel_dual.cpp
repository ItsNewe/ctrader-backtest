#include "../include/strategy_parallel_dual.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <chrono>

using namespace backtest;

// Global tick data (loaded once, reused for all tests)
std::vector<Tick> g_ticks;

void LoadTickData() {
    std::cout << "Loading tick data..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;

    TickDataManager manager(tick_config);
    manager.Reset();

    Tick tick;
    while (manager.GetNextTick(tick)) {
        g_ticks.push_back(tick);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::cout << "Loaded " << g_ticks.size() << " ticks in " << duration << "ms" << std::endl;
}

struct TestResult {
    double survive_pct;
    double min_spacing;
    double safety_buffer;
    int target_trades;
    double final_balance;
    double final_equity;
    double return_multiple;
    double max_dd_pct;
    int upward_entries;
    int downward_entries;
    int total_entries;
    double total_volume;
    int skipped_by_margin;
    double swap_charged;
    bool stop_out;
};

TestResult RunTest(double survive_pct, double min_spacing, double safety_buffer, int target_trades,
                   double initial_balance = 10000.0, bool verbose = false) {
    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = initial_balance;
    config.contract_size = 100.0;
    config.leverage = 500.0;
    config.pip_size = 0.01;
    config.swap_long = -66.99;
    config.swap_short = 41.2;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = "2025.01.01";
    config.end_date = "2025.12.30";
    config.verbose = false;

    TickBasedEngine engine(config);

    ParallelDualStrategy::Config strat_config;
    strat_config.survive_pct = survive_pct;
    strat_config.min_volume = 0.01;
    strat_config.max_volume = 10.0;
    strat_config.contract_size = 100.0;
    strat_config.leverage = 500.0;
    strat_config.min_spacing = min_spacing;
    strat_config.max_spacing = 15.0;
    strat_config.base_spacing = 1.50;
    strat_config.target_trades_in_range = target_trades;
    strat_config.margin_stop_out = 20.0;
    strat_config.safety_buffer = safety_buffer;

    ParallelDualStrategy strategy(strat_config);

    engine.RunWithTicks(g_ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();
    auto stats = strategy.GetStats();

    // Calculate final equity (balance + unrealized P/L)
    double total_unrealized = 0;
    double contract_size = strat_config.contract_size;
    for (const Trade* t : engine.GetOpenPositions()) {
        double unrealized = (g_ticks.back().bid - t->entry_price) * t->lot_size * contract_size;
        total_unrealized += unrealized;
    }
    double final_equity = results.final_balance + total_unrealized;

    if (verbose) {
        std::cout << "\n=== Diagnostic ===" << std::endl;
        std::cout << "Parameters: survive=" << survive_pct << "%, min_spacing=$" << min_spacing
                  << ", safety=" << safety_buffer << ", target_trades=" << target_trades << std::endl;
        std::cout << "Open positions: " << engine.GetOpenPositions().size() << std::endl;
        std::cout << "Closed trades: " << engine.GetClosedTrades().size() << std::endl;
        std::cout << "Balance: $" << results.final_balance << std::endl;
        std::cout << "Total swap charged: $" << results.total_swap_charged << std::endl;

        // Show price range
        std::cout << "\nPrice Range:" << std::endl;
        std::cout << "  Highest entry: $" << stats.highest_entry << std::endl;
        std::cout << "  Lowest entry:  $" << stats.lowest_entry << std::endl;
        std::cout << "  Entry spread:  $" << (stats.highest_entry - stats.lowest_entry) << std::endl;

        // Show entry breakdown
        std::cout << "\nEntry Breakdown:" << std::endl;
        std::cout << "  Upward entries:   " << stats.upward_entries << std::endl;
        std::cout << "  Downward entries: " << stats.downward_entries << std::endl;
        std::cout << "  Total entries:    " << stats.total_entries << std::endl;
        std::cout << "  Skipped (margin): " << stats.skipped_by_margin << std::endl;

        // Show some open positions
        std::cout << "\nSample Open Positions (first 5):" << std::endl;
        int count = 0;
        for (const Trade* t : engine.GetOpenPositions()) {
            if (count++ >= 5) break;
            double unrealized = (g_ticks.back().bid - t->entry_price) * t->lot_size * contract_size;
            std::cout << "  " << t->direction << " " << std::fixed << std::setprecision(2)
                      << t->lot_size << " @ $" << t->entry_price
                      << " (P/L: $" << unrealized << ")" << std::endl;
        }

        std::cout << "\nTotal unrealized P/L: $" << total_unrealized << std::endl;
        std::cout << "Final Equity: $" << final_equity << std::endl;
        std::cout << "Return: " << std::fixed << std::setprecision(2)
                  << (final_equity / initial_balance) << "x" << std::endl;
    }

    TestResult r;
    r.survive_pct = survive_pct;
    r.min_spacing = min_spacing;
    r.safety_buffer = safety_buffer;
    r.target_trades = target_trades;
    r.final_balance = results.final_balance;
    r.final_equity = final_equity;
    r.return_multiple = final_equity / initial_balance;
    r.max_dd_pct = results.max_drawdown_pct;
    r.upward_entries = stats.upward_entries;
    r.downward_entries = stats.downward_entries;
    r.total_entries = stats.total_entries;
    r.total_volume = stats.total_volume;
    r.skipped_by_margin = stats.skipped_by_margin;
    r.swap_charged = results.total_swap_charged;
    r.stop_out = results.stop_out_occurred;

    return r;
}

void PrintResult(const TestResult& r) {
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "  survive=" << std::setw(4) << r.survive_pct << "% "
              << "spacing=$" << std::setw(4) << r.min_spacing << " "
              << "safety=" << std::setw(3) << r.safety_buffer << " "
              << "trades=" << std::setw(2) << r.target_trades << " | ";

    if (r.stop_out) {
        std::cout << "STOP-OUT" << std::endl;
        return;
    }

    std::cout << std::setprecision(2);
    std::cout << "Eq: $" << std::setw(10) << r.final_equity
              << " (" << std::setw(5) << r.return_multiple << "x) "
              << "DD: " << std::setw(5) << r.max_dd_pct << "% | "
              << "Up: " << std::setw(3) << r.upward_entries
              << " Dn: " << std::setw(3) << r.downward_entries
              << " Skip: " << std::setw(4) << r.skipped_by_margin
              << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  Unified Dynamic Survival Strategy" << std::endl;
    std::cout << "  XAUUSD 2025 - No Take Profit" << std::endl;
    std::cout << "========================================" << std::endl;

    LoadTickData();
    if (g_ticks.empty()) {
        std::cerr << "ERROR: No ticks loaded!" << std::endl;
        return 1;
    }

    std::cout << "\nFirst tick: " << g_ticks.front().timestamp << " @ $" << g_ticks.front().bid << std::endl;
    std::cout << "Last tick:  " << g_ticks.back().timestamp << " @ $" << g_ticks.back().bid << std::endl;

    // Calculate price change over period
    double start_price = g_ticks.front().bid;
    double end_price = g_ticks.back().bid;
    double price_change_pct = ((end_price - start_price) / start_price) * 100.0;
    std::cout << "Price change: " << std::fixed << std::setprecision(2)
              << price_change_pct << "% ($" << start_price << " -> $" << end_price << ")" << std::endl;

    // Parameter sweep
    std::vector<double> survive_values = {10.0, 12.0, 15.0, 18.0, 20.0};
    std::vector<double> spacing_values = {0.5, 1.0, 2.0, 3.0};
    std::vector<double> safety_values = {1.2, 1.5, 2.0};
    std::vector<int> target_trade_values = {10, 20, 30};

    std::vector<TestResult> all_results;

    std::cout << "\n=== Parameter Sweep ===" << std::endl;

    // Quick sweep with key combinations
    for (double survive : survive_values) {
        std::cout << "\n--- survive_pct = " << survive << "% ---" << std::endl;
        for (double spacing : spacing_values) {
            // Use middle values for safety and target_trades in quick sweep
            TestResult r = RunTest(survive, spacing, 1.5, 20);
            all_results.push_back(r);
            PrintResult(r);
        }
    }

    // Find best result by return (excluding stop-outs)
    std::cout << "\n=== Top 10 Results (by return multiple) ===" << std::endl;

    std::vector<TestResult> valid_results;
    for (const auto& r : all_results) {
        if (!r.stop_out) {
            valid_results.push_back(r);
        }
    }

    std::sort(valid_results.begin(), valid_results.end(),
              [](const TestResult& a, const TestResult& b) {
                  return a.return_multiple > b.return_multiple;
              });

    for (size_t i = 0; i < std::min(size_t(10), valid_results.size()); i++) {
        std::cout << (i + 1) << ". ";
        PrintResult(valid_results[i]);
    }

    // Find best risk-adjusted (return / drawdown)
    std::cout << "\n=== Top 5 Results (by return/drawdown ratio) ===" << std::endl;

    std::sort(valid_results.begin(), valid_results.end(),
              [](const TestResult& a, const TestResult& b) {
                  double ratio_a = (a.max_dd_pct > 0) ? a.return_multiple / a.max_dd_pct : 0;
                  double ratio_b = (b.max_dd_pct > 0) ? b.return_multiple / b.max_dd_pct : 0;
                  return ratio_a > ratio_b;
              });

    for (size_t i = 0; i < std::min(size_t(5), valid_results.size()); i++) {
        double ratio = (valid_results[i].max_dd_pct > 0)
            ? valid_results[i].return_multiple / valid_results[i].max_dd_pct * 100.0
            : 0;
        std::cout << (i + 1) << ". [Ratio: " << std::fixed << std::setprecision(3) << ratio << "] ";
        PrintResult(valid_results[i]);
    }

    // Summary stats
    int stop_outs = 0;
    for (const auto& r : all_results) {
        if (r.stop_out) stop_outs++;
    }

    std::cout << "\n=== Summary ===" << std::endl;
    std::cout << "Total tests: " << all_results.size() << std::endl;
    std::cout << "Stop-outs: " << stop_outs << std::endl;
    std::cout << "Successful: " << (all_results.size() - stop_outs) << std::endl;

    // Deep sweep on best parameters
    if (!valid_results.empty()) {
        std::cout << "\n=== Deep Sweep on Best Parameters ===" << std::endl;

        // Find best by return
        std::sort(valid_results.begin(), valid_results.end(),
                  [](const TestResult& a, const TestResult& b) {
                      return a.return_multiple > b.return_multiple;
                  });

        double best_survive = valid_results.front().survive_pct;
        double best_spacing = valid_results.front().min_spacing;

        std::cout << "Testing around survive=" << best_survive << "%, spacing=$" << best_spacing << std::endl;

        // Sweep safety_buffer and target_trades
        std::vector<TestResult> deep_results;
        for (double safety : safety_values) {
            for (int targets : target_trade_values) {
                TestResult r = RunTest(best_survive, best_spacing, safety, targets);
                deep_results.push_back(r);
                PrintResult(r);
            }
        }

        // Find best from deep sweep
        std::vector<TestResult> deep_valid;
        for (const auto& r : deep_results) {
            if (!r.stop_out) {
                deep_valid.push_back(r);
            }
        }

        if (!deep_valid.empty()) {
            std::sort(deep_valid.begin(), deep_valid.end(),
                      [](const TestResult& a, const TestResult& b) {
                          return a.return_multiple > b.return_multiple;
                      });

            std::cout << "\n=== Best Configuration ===" << std::endl;
            PrintResult(deep_valid.front());

            // Run diagnostic on best
            std::cout << "\n=== Detailed Diagnostic ===" << std::endl;
            RunTest(deep_valid.front().survive_pct, deep_valid.front().min_spacing,
                    deep_valid.front().safety_buffer, deep_valid.front().target_trades,
                    10000.0, true);
        }
    }

    // October crash test (critical survival check)
    std::cout << "\n=== October 2025 Crash Test ===" << std::endl;
    std::cout << "Testing best parameters on Oct 1 - Oct 31 period..." << std::endl;

    // Find October ticks
    std::vector<Tick> oct_ticks;
    for (const auto& tick : g_ticks) {
        if (tick.timestamp.find("2025.10.") != std::string::npos) {
            oct_ticks.push_back(tick);
        }
    }

    if (!oct_ticks.empty()) {
        std::cout << "October ticks: " << oct_ticks.size() << std::endl;
        std::cout << "Oct start: $" << oct_ticks.front().bid << std::endl;
        std::cout << "Oct end: $" << oct_ticks.back().bid << std::endl;

        // Find min/max in October
        double oct_min = oct_ticks.front().bid;
        double oct_max = oct_ticks.front().bid;
        for (const auto& tick : oct_ticks) {
            oct_min = std::min(oct_min, tick.bid);
            oct_max = std::max(oct_max, tick.bid);
        }
        std::cout << "Oct range: $" << oct_min << " - $" << oct_max << std::endl;
        std::cout << "Oct max drop: " << ((oct_max - oct_min) / oct_max * 100.0) << "%" << std::endl;
    }

    return 0;
}
