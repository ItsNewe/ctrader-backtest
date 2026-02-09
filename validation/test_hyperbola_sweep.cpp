/**
 * Thorough sweep of the original hyperbola room decay function
 * Testing power, multiplier, and stop_out combinations
 */

#include "../include/strategy_nasdaq_up.h"
#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>

using namespace backtest;

struct SweepResult {
    double multiplier;
    double power;
    double stop_out;
    double final_balance;
    double return_pct;
    double max_drawdown_pct;
    int total_trades;
    double profit_factor;
};

int main() {
    std::cout << "=== Thorough Hyperbola Parameter Sweep ===" << std::endl;
    std::cout << "Formula: room = starting_room × (price_gain)^power" << std::endl;
    std::cout << "Starting room = price × multiplier / 100\n" << std::endl;

    // Load tick data once
    std::cout << "Loading NAS100 tick data..." << std::endl;

    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\.claude-worktrees\\ctrader-backtest\\beautiful-margulis\\validation\\Grid\\NAS100_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = true;

    TickDataManager manager(tick_config);
    manager.LoadAllTicks();
    const std::vector<Tick>& ticks = manager.GetAllTicks();

    std::cout << "Loaded " << ticks.size() << " ticks\n" << std::endl;

    // Parameter ranges to sweep - reduced for faster testing
    std::vector<double> multipliers = {5.0, 10.0, 20.0, 50.0};
    std::vector<double> powers = {-0.3, -0.5, -0.7, -1.0};
    std::vector<double> stop_outs = {200.0, 500.0, 1000.0};

    size_t total_configs = multipliers.size() * powers.size() * stop_outs.size();
    std::cout << "Testing " << total_configs << " configurations..." << std::endl;
    std::cout << "  Multipliers: " << multipliers.size() << " values (1-50)" << std::endl;
    std::cout << "  Powers: " << powers.size() << " values (-0.1 to -1.0)" << std::endl;
    std::cout << "  Stop-outs: " << stop_outs.size() << " values (100-1000%)\n" << std::endl;

    // Build all configs
    std::vector<std::tuple<double, double, double>> configs;
    for (double mult : multipliers) {
        for (double power : powers) {
            for (double stop : stop_outs) {
                configs.push_back({mult, power, stop});
            }
        }
    }

    // Results storage
    std::vector<SweepResult> results(configs.size());
    std::atomic<int> completed(0);
    std::mutex print_mutex;

    // Thread worker
    auto run_test = [&](size_t idx) {
        auto [mult, power, stop] = configs[idx];

        TickBacktestConfig engine_config;
        engine_config.symbol = "NAS100";
        engine_config.initial_balance = 10000.0;
        engine_config.contract_size = 1.0;
        engine_config.leverage = 100.0;
        engine_config.pip_size = 0.01;
        engine_config.swap_long = -17.14;
        engine_config.swap_short = 5.76;
        engine_config.swap_mode = 5;
        engine_config.swap_3days = 5;
        engine_config.start_date = "2025.04.07";
        engine_config.end_date = "2025.10.30";
        engine_config.tick_data_config = tick_config;
        engine_config.verbose = false;

        NasdaqUp::Config strat_config;
        strat_config.multiplier = mult;
        strat_config.power = power;
        strat_config.stop_out_margin = stop;
        strat_config.contract_size = 1.0;
        strat_config.leverage = 100.0;

        TickBasedEngine engine(engine_config);
        NasdaqUp strategy(strat_config);

        engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto res = engine.GetResults();

        SweepResult sr;
        sr.multiplier = mult;
        sr.power = power;
        sr.stop_out = stop;
        sr.final_balance = res.final_balance;
        sr.return_pct = (res.final_balance - 10000.0) / 10000.0 * 100.0;
        sr.max_drawdown_pct = res.max_drawdown_pct;
        sr.total_trades = res.total_trades;
        sr.profit_factor = res.profit_factor;

        results[idx] = sr;

        int done = ++completed;
        if (done % 5 == 0 || done == (int)configs.size()) {
            std::lock_guard<std::mutex> lock(print_mutex);
            std::cout << "Progress: " << done << "/" << configs.size()
                      << " (" << (done * 100 / configs.size()) << "%)" << std::endl;
            std::cout.flush();
        }
    };

    // Parallel execution
    const int num_threads = std::thread::hardware_concurrency();
    std::cout << "Using " << num_threads << " threads\n" << std::endl;

    std::vector<std::thread> threads;
    std::atomic<size_t> next_idx(0);

    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&]() {
            while (true) {
                size_t idx = next_idx++;
                if (idx >= configs.size()) break;
                run_test(idx);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // Sort by return
    std::sort(results.begin(), results.end(), [](const SweepResult& a, const SweepResult& b) {
        return a.final_balance > b.final_balance;
    });

    // Print top 30
    std::cout << "\n\n====== TOP 30 CONFIGURATIONS ======\n" << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << std::setw(5) << "Rank"
              << std::setw(8) << "Mult"
              << std::setw(8) << "Power"
              << std::setw(10) << "StopOut"
              << std::setw(12) << "Final$"
              << std::setw(10) << "Return%"
              << std::setw(10) << "MaxDD%"
              << std::setw(8) << "Trades"
              << std::setw(8) << "PF"
              << std::endl;
    std::cout << std::string(79, '-') << std::endl;

    for (int i = 0; i < std::min(30, (int)results.size()); i++) {
        const auto& r = results[i];
        std::cout << std::setw(5) << (i + 1)
                  << std::setw(8) << r.multiplier
                  << std::setw(8) << r.power
                  << std::setw(10) << r.stop_out
                  << std::setw(12) << r.final_balance
                  << std::setw(10) << r.return_pct
                  << std::setw(10) << r.max_drawdown_pct
                  << std::setw(8) << r.total_trades
                  << std::setw(8) << r.profit_factor
                  << std::endl;
    }

    // Print bottom 10
    std::cout << "\n\n====== BOTTOM 10 (WORST) ======\n" << std::endl;
    for (int i = std::max(0, (int)results.size() - 10); i < (int)results.size(); i++) {
        const auto& r = results[i];
        std::cout << std::setw(5) << (i + 1)
                  << std::setw(8) << r.multiplier
                  << std::setw(8) << r.power
                  << std::setw(10) << r.stop_out
                  << std::setw(12) << r.final_balance
                  << std::setw(10) << r.return_pct
                  << std::setw(10) << r.max_drawdown_pct
                  << std::setw(8) << r.total_trades
                  << std::setw(8) << r.profit_factor
                  << std::endl;
    }

    // Analysis by parameter
    std::cout << "\n\n====== ANALYSIS BY MULTIPLIER ======\n" << std::endl;
    std::cout << std::setw(10) << "Mult" << std::setw(12) << "AvgRet%" << std::setw(12) << "BestRet%"
              << std::setw(12) << "WorstRet%" << std::setw(10) << "Profitable" << std::endl;
    std::cout << std::string(56, '-') << std::endl;

    for (double mult : multipliers) {
        double sum = 0, best = -1e9, worst = 1e9;
        int count = 0, profitable = 0;
        for (const auto& r : results) {
            if (r.multiplier == mult) {
                sum += r.return_pct;
                best = std::max(best, r.return_pct);
                worst = std::min(worst, r.return_pct);
                count++;
                if (r.return_pct > 0) profitable++;
            }
        }
        std::cout << std::setw(10) << mult
                  << std::setw(12) << (sum / count)
                  << std::setw(12) << best
                  << std::setw(12) << worst
                  << std::setw(10) << (profitable * 100 / count) << "%"
                  << std::endl;
    }

    std::cout << "\n\n====== ANALYSIS BY POWER ======\n" << std::endl;
    std::cout << std::setw(10) << "Power" << std::setw(12) << "AvgRet%" << std::setw(12) << "BestRet%"
              << std::setw(12) << "WorstRet%" << std::setw(10) << "Profitable" << std::endl;
    std::cout << std::string(56, '-') << std::endl;

    for (double power : powers) {
        double sum = 0, best = -1e9, worst = 1e9;
        int count = 0, profitable = 0;
        for (const auto& r : results) {
            if (r.power == power) {
                sum += r.return_pct;
                best = std::max(best, r.return_pct);
                worst = std::min(worst, r.return_pct);
                count++;
                if (r.return_pct > 0) profitable++;
            }
        }
        std::cout << std::setw(10) << power
                  << std::setw(12) << (sum / count)
                  << std::setw(12) << best
                  << std::setw(12) << worst
                  << std::setw(10) << (profitable * 100 / count) << "%"
                  << std::endl;
    }

    std::cout << "\n\n====== ANALYSIS BY STOP-OUT ======\n" << std::endl;
    std::cout << std::setw(10) << "StopOut" << std::setw(12) << "AvgRet%" << std::setw(12) << "BestRet%"
              << std::setw(12) << "WorstRet%" << std::setw(10) << "Profitable" << std::endl;
    std::cout << std::string(56, '-') << std::endl;

    for (double stop : stop_outs) {
        double sum = 0, best = -1e9, worst = 1e9;
        int count = 0, profitable = 0;
        for (const auto& r : results) {
            if (r.stop_out == stop) {
                sum += r.return_pct;
                best = std::max(best, r.return_pct);
                worst = std::min(worst, r.return_pct);
                count++;
                if (r.return_pct > 0) profitable++;
            }
        }
        std::cout << std::setw(10) << stop
                  << std::setw(12) << (sum / count)
                  << std::setw(12) << best
                  << std::setw(12) << worst
                  << std::setw(10) << (profitable * 100 / count) << "%"
                  << std::endl;
    }

    // Best risk-adjusted (return / drawdown)
    std::cout << "\n\n====== TOP 20 RISK-ADJUSTED (Return / MaxDD) ======\n" << std::endl;

    std::vector<SweepResult> risk_sorted = results;
    std::sort(risk_sorted.begin(), risk_sorted.end(), [](const SweepResult& a, const SweepResult& b) {
        double ra = (a.max_drawdown_pct > 0) ? a.return_pct / a.max_drawdown_pct : 0;
        double rb = (b.max_drawdown_pct > 0) ? b.return_pct / b.max_drawdown_pct : 0;
        return ra > rb;
    });

    std::cout << std::setw(5) << "Rank"
              << std::setw(8) << "Mult"
              << std::setw(8) << "Power"
              << std::setw(10) << "StopOut"
              << std::setw(12) << "Final$"
              << std::setw(10) << "Return%"
              << std::setw(10) << "MaxDD%"
              << std::setw(10) << "Ret/DD"
              << std::endl;
    std::cout << std::string(73, '-') << std::endl;

    for (int i = 0; i < std::min(20, (int)risk_sorted.size()); i++) {
        const auto& r = risk_sorted[i];
        double ratio = (r.max_drawdown_pct > 0) ? r.return_pct / r.max_drawdown_pct : 0;
        std::cout << std::setw(5) << (i + 1)
                  << std::setw(8) << r.multiplier
                  << std::setw(8) << r.power
                  << std::setw(10) << r.stop_out
                  << std::setw(12) << r.final_balance
                  << std::setw(10) << r.return_pct
                  << std::setw(10) << r.max_drawdown_pct
                  << std::setw(10) << ratio
                  << std::endl;
    }

    // Summary stats
    int profitable_count = 0;
    double total_return = 0;
    for (const auto& r : results) {
        if (r.return_pct > 0) profitable_count++;
        total_return += r.return_pct;
    }

    std::cout << "\n\n====== SUMMARY ======\n" << std::endl;
    std::cout << "Total configurations: " << results.size() << std::endl;
    std::cout << "Profitable: " << profitable_count << " ("
              << (profitable_count * 100.0 / results.size()) << "%)" << std::endl;
    std::cout << "Average return: " << (total_return / results.size()) << "%" << std::endl;
    std::cout << "Best return: " << results.front().return_pct << "% (mult="
              << results.front().multiplier << ", power=" << results.front().power
              << ", stop=" << results.front().stop_out << ")" << std::endl;
    std::cout << "Worst return: " << results.back().return_pct << "% (mult="
              << results.back().multiplier << ", power=" << results.back().power
              << ", stop=" << results.back().stop_out << ")" << std::endl;

    return 0;
}
