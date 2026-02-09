#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <chrono>

using namespace backtest;

struct RunConfig {
    double survive_pct;
    double base_spacing;
    double lookback_hours;
    double typical_vol_pct;
    double min_spacing_abs;
    double max_spacing_abs;
    double min_spacing_mult;
    double max_spacing_mult;
    double swap_long_points;
    double spacing_change_threshold = 0.1;
    bool pct_spacing = false;
    std::string label;
};

struct RunResult {
    std::string label;
    double final_equity;
    double return_x;
    double max_dd_pct;
    int total_trades;
    int spacing_changes;
    double total_swap;
    bool stopped_out;
};

std::mutex print_mutex;

RunResult RunBacktest(const RunConfig& cfg, const std::vector<Tick>& shared_ticks, bool verbose) {
    RunResult result;
    result.label = cfg.label;

    TickDataConfig tick_config;
    tick_config.file_path = "";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = 10000.0;
    config.account_currency = "USD";
    config.contract_size = 100.0;
    config.leverage = 500.0;
    config.margin_rate = 1.0;
    config.pip_size = 0.01;

    config.swap_long = cfg.swap_long_points;
    config.swap_short = 41.2;
    config.swap_mode = 1;
    config.swap_3days = 3;

    config.start_date = "2025.01.01";
    config.end_date = "2025.12.29";
    config.verbose = false;
    config.tick_data_config = tick_config;

    try {
        TickBasedEngine engine(config);

        FillUpOscillation::AdaptiveConfig adaptive_cfg;
        adaptive_cfg.typical_vol_pct = cfg.typical_vol_pct;
        adaptive_cfg.min_spacing_mult = cfg.min_spacing_mult;
        adaptive_cfg.max_spacing_mult = cfg.max_spacing_mult;
        adaptive_cfg.min_spacing_abs = cfg.min_spacing_abs;
        adaptive_cfg.max_spacing_abs = cfg.max_spacing_abs;
        adaptive_cfg.spacing_change_threshold = cfg.spacing_change_threshold;
        adaptive_cfg.pct_spacing = cfg.pct_spacing;

        FillUpOscillation strategy(
            cfg.survive_pct,
            cfg.base_spacing,
            0.01,
            10.0,
            100.0,   // contract_size (XAUUSD)
            500.0,
            FillUpOscillation::ADAPTIVE_SPACING,
            0.1,
            30.0,
            cfg.lookback_hours,
            adaptive_cfg
        );

        engine.RunWithTicks(shared_ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        double floating = 0;
        for (const Trade* trade : engine.GetOpenPositions()) {
            double exit_price = engine.GetCurrentTick().bid;
            floating += (exit_price - trade->entry_price) * trade->lot_size * config.contract_size;
        }
        result.final_equity = results.final_balance + floating;
        result.return_x = result.final_equity / 10000.0;
        result.max_dd_pct = results.max_drawdown_pct;
        result.total_trades = results.total_trades_opened;
        result.spacing_changes = strategy.GetAdaptiveSpacingChanges();
        result.total_swap = results.total_swap_charged;
        result.stopped_out = results.stop_out_occurred;

    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(print_mutex);
        std::cerr << "Error [" << cfg.label << "]: " << e.what() << std::endl;
        result.final_equity = 0;
        result.return_x = 0;
        result.stopped_out = true;
    }

    if (verbose) {
        std::lock_guard<std::mutex> lock(print_mutex);
        std::cout << std::setw(22) << result.label
                  << std::setw(12) << std::fixed << std::setprecision(0) << result.final_equity << "$"
                  << std::setw(8) << std::setprecision(1) << result.return_x << "x"
                  << std::setw(7) << result.max_dd_pct << "%"
                  << std::setw(7) << result.total_trades
                  << std::setw(8) << result.spacing_changes
                  << std::setw(9) << std::setprecision(0) << result.total_swap << "$"
                  << std::setw(5) << (result.stopped_out ? "SO" : "ok") << std::endl;
    }

    return result;
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << " FillUpAdaptive Pct-Based Backtest for XAUUSD" << std::endl;
    std::cout << "================================================================" << std::endl;

    std::string tick_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";

    int num_threads = (int)std::thread::hardware_concurrency();
    if (num_threads < 1) num_threads = 4;
    std::cout << "Threads: " << num_threads << std::endl;

    // Load tick data once
    std::cout << "Loading tick data..." << std::flush;
    auto load_start = std::chrono::steady_clock::now();
    TickDataConfig load_config;
    load_config.file_path = tick_path;
    load_config.format = TickDataFormat::MT5_CSV;
    load_config.load_all_into_memory = true;
    TickDataManager data_loader(load_config);
    const std::vector<Tick>& shared_ticks = data_loader.GetAllTicks();
    auto load_elapsed = std::chrono::steady_clock::now() - load_start;
    double load_sec = std::chrono::duration<double>(load_elapsed).count();
    std::cout << " " << shared_ticks.size() << " ticks loaded in "
              << std::fixed << std::setprecision(1) << load_sec << "s" << std::endl;

    // === Baseline: absolute spacing (known best) ===
    std::cout << "\n=== BASELINE (absolute spacing, known optimal) ===" << std::endl;
    RunConfig baseline;
    baseline.survive_pct = 13.0;
    baseline.base_spacing = 1.5;
    baseline.lookback_hours = 4.0;
    baseline.typical_vol_pct = 0.5;
    baseline.min_spacing_abs = 0.5;
    baseline.max_spacing_abs = 5.0;
    baseline.min_spacing_mult = 0.5;
    baseline.max_spacing_mult = 3.0;
    baseline.swap_long_points = -66.99;
    baseline.pct_spacing = false;
    baseline.label = "ABS_baseline";

    auto baseline_result = RunBacktest(baseline, shared_ticks, false);
    std::cout << "Absolute $1.50:  " << std::fixed << std::setprecision(1)
              << baseline_result.return_x << "x, DD=" << baseline_result.max_dd_pct << "%"
              << ", Trades=" << baseline_result.total_trades
              << ", Swap=$" << std::setprecision(0) << baseline_result.total_swap << std::endl;

    // === Percentage-based sweep ===
    std::cout << "\n=== PERCENTAGE-BASED SWEEP ===" << std::endl;
    std::cout << std::setw(22) << "Config"
              << std::setw(13) << "Equity"
              << std::setw(8) << "Return"
              << std::setw(7) << "MaxDD"
              << std::setw(7) << "Trades"
              << std::setw(8) << "SpChg"
              << std::setw(9) << "Swap"
              << std::setw(5) << "St" << std::endl;
    std::cout << std::string(71, '-') << std::endl;

    std::vector<RunConfig> configs;

    // Gold price avg ~$3,518 in 2025
    // Absolute $1.50 = 0.043% of price
    // Sweep: 0.02% to 0.20% with various survive/lookback
    std::vector<double> survive_vals = {11, 12, 13, 14, 15};
    std::vector<double> pct_spacing_vals = {0.02, 0.03, 0.04, 0.05, 0.06, 0.08, 0.10, 0.15, 0.20};
    std::vector<double> lookback_vals = {1.0, 4.0};

    for (double survive : survive_vals) {
        for (double spacing_pct : pct_spacing_vals) {
            for (double lookback : lookback_vals) {
                RunConfig cfg;
                cfg.survive_pct = survive;
                cfg.base_spacing = spacing_pct;
                cfg.lookback_hours = lookback;
                // Measured XAUUSD median ranges: 1h=0.27%, 4h=0.55%
                cfg.typical_vol_pct = (lookback <= 1.0) ? 0.27 : 0.55;
                cfg.min_spacing_abs = 0.005;      // 0.005% of price (~$0.18)
                cfg.max_spacing_abs = 1.0;        // 1.0% of price (~$35)
                cfg.min_spacing_mult = 0.5;
                cfg.max_spacing_mult = 3.0;
                cfg.swap_long_points = -66.99;
                cfg.spacing_change_threshold = 0.01;  // 0.01% of price (~$0.35)
                cfg.pct_spacing = true;

                // Label: s13_0.04%_lb4
                char buf[32];
                snprintf(buf, sizeof(buf), "s%d_%.2f%%_lb%d",
                         (int)survive, spacing_pct, (int)lookback);
                cfg.label = buf;
                configs.push_back(cfg);
            }
        }
    }

    // Run sweep parallel
    std::vector<RunResult> results(configs.size());
    auto start_time = std::chrono::steady_clock::now();

    std::vector<std::thread> threads;
    size_t next_config = 0;
    std::mutex config_mutex;

    auto worker = [&]() {
        while (true) {
            size_t idx;
            {
                std::lock_guard<std::mutex> lock(config_mutex);
                if (next_config >= configs.size()) return;
                idx = next_config++;
            }
            results[idx] = RunBacktest(configs[idx], shared_ticks, true);
        }
    };

    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) {
        t.join();
    }

    auto elapsed = std::chrono::steady_clock::now() - start_time;
    double elapsed_sec = std::chrono::duration<double>(elapsed).count();

    // === Summary ===
    std::cout << "\n=== TOP 20 (sorted by return, survived only) ===" << std::endl;
    std::vector<size_t> indices(results.size());
    for (size_t i = 0; i < indices.size(); i++) indices[i] = i;
    std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
        if (results[a].stopped_out != results[b].stopped_out)
            return !results[a].stopped_out;
        return results[a].return_x > results[b].return_x;
    });

    std::cout << std::setw(22) << "Config"
              << std::setw(8) << "Return"
              << std::setw(7) << "MaxDD"
              << std::setw(7) << "Trades"
              << std::setw(9) << "Swap"
              << std::setw(5) << "St" << std::endl;
    std::cout << std::string(58, '-') << std::endl;

    int shown = 0;
    for (size_t idx : indices) {
        if (shown >= 20) break;
        auto& r = results[idx];
        if (r.stopped_out) continue;
        std::cout << std::setw(22) << r.label
                  << std::setw(7) << std::fixed << std::setprecision(1) << r.return_x << "x"
                  << std::setw(6) << r.max_dd_pct << "%"
                  << std::setw(7) << r.total_trades
                  << std::setw(8) << std::setprecision(0) << r.total_swap << "$"
                  << std::setw(5) << "ok" << std::endl;
        shown++;
    }

    // Count stopped out
    int so_count = 0;
    for (auto& r : results) if (r.stopped_out) so_count++;

    std::cout << "\nBaseline (ABS $1.50, s13, lb4): " << std::setprecision(1)
              << baseline_result.return_x << "x, " << baseline_result.max_dd_pct << "% DD" << std::endl;
    std::cout << "Stopped out: " << so_count << "/" << configs.size() << std::endl;
    std::cout << "Completed " << configs.size() << " backtests in "
              << std::fixed << std::setprecision(1) << elapsed_sec << "s"
              << " (" << std::setprecision(1) << (elapsed_sec / configs.size()) << "s/run)" << std::endl;

    return 0;
}
