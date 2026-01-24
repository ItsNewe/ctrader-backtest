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
    double swap_long_points; // Swap in points (e.g., -15 for XAGUSD)
    double spacing_change_threshold = 0.1;
    bool pct_spacing = false;  // When true, spacing values are % of price
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

    // === Engine Configuration ===
    TickDataConfig tick_config;
    tick_config.file_path = "";  // Empty - using shared pre-loaded ticks
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickBacktestConfig config;
    config.symbol = "XAGUSD";
    config.initial_balance = 10000.0;
    config.account_currency = "USD";
    config.contract_size = 5000.0;    // XAGUSD: 5000 oz per lot
    config.leverage = 500.0;
    config.margin_rate = 1.0;
    config.pip_size = 0.001;          // XAGUSD: 3 decimal places

    // Swap settings
    config.swap_long = cfg.swap_long_points;
    config.swap_short = 0.0;          // Not relevant (only buy)
    config.swap_mode = 1;             // Points mode
    config.swap_3days = 3;            // Triple swap charged Thursday

    // Date range
    config.start_date = "2025.01.02";
    config.end_date = "2026.01.24";

    config.verbose = false;           // Suppress per-trade logging
    config.tick_data_config = tick_config;

    try {
        TickBasedEngine engine(config);

        // === Strategy Configuration ===
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
            0.01,              // min_volume
            100.0,             // max_volume
            5000.0,            // contract_size (XAGUSD)
            500.0,             // leverage
            FillUpOscillation::ADAPTIVE_SPACING,
            0.1,               // antifragile_scale (unused in this mode)
            30.0,              // velocity_threshold (unused)
            cfg.lookback_hours,
            adaptive_cfg
        );

        // === Run Backtest (using shared pre-loaded ticks) ===
        engine.RunWithTicks(shared_ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        // === Get Results ===
        auto results = engine.GetResults();
        result.final_equity = results.final_balance; // After all trades closed
        // Equity includes unrealized P&L from open positions
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
        std::cout << std::setw(20) << result.label
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

int main(int argc, char* argv[]) {
    std::cout << "================================================================" << std::endl;
    std::cout << " FillUpAdaptive Engine-Based Backtest for XAGUSD" << std::endl;
    std::cout << " Using TickBasedEngine with proper swap/margin" << std::endl;
    std::cout << "================================================================" << std::endl;

    std::string tick_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\mt5\\fill_up_xagusd\\XAGUSD_TESTER_TICKS.csv";

    // Thread count: default to all hardware threads, user can override
    int num_threads = (int)std::thread::hardware_concurrency();
    if (num_threads < 1) num_threads = 4;  // Fallback if detection fails
    if (argc > 1) {
        num_threads = std::atoi(argv[1]);
        if (num_threads < 1) num_threads = 1;
    }
    std::cout << "Threads: " << num_threads << std::endl;

    // === Load tick data ONCE (shared across all threads) ===
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

    // === MT5 Validation Run ===
    // Matching: SurvivePct=19, BaseSpacing=0.75, Lookback=1h,
    //           TypicalVolPct=0.5, MinSpacingAbs=0.02, MaxSpacingAbs=5.0
    //           swap_long=-15 points (≈ -$75/lot/day)
    std::cout << "\n=== MT5 VALIDATION (matching EA v3 parameters) ===" << std::endl;
    RunConfig mt5_cfg;
    mt5_cfg.survive_pct = 19.0;
    mt5_cfg.base_spacing = 0.75;
    mt5_cfg.lookback_hours = 1.0;
    mt5_cfg.typical_vol_pct = 0.45;  // Measured XAGUSD 1h median range
    mt5_cfg.min_spacing_abs = 0.02;
    mt5_cfg.max_spacing_abs = 5.0;
    mt5_cfg.min_spacing_mult = 0.5;
    mt5_cfg.max_spacing_mult = 3.0;
    mt5_cfg.swap_long_points = -15.0;
    mt5_cfg.label = "MT5-match";

    auto mt5_result = RunBacktest(mt5_cfg, shared_ticks, false);

    std::cout << "C++ Result:  Equity=$" << std::fixed << std::setprecision(0) << mt5_result.final_equity
              << " (" << std::setprecision(1) << mt5_result.return_x << "x)"
              << " MaxDD=" << mt5_result.max_dd_pct << "%"
              << " Trades=" << mt5_result.total_trades
              << " SpChanges=" << mt5_result.spacing_changes
              << " Swap=$" << std::setprecision(0) << mt5_result.total_swap << std::endl;
    std::cout << "MT5 Target:  Equity=$263,924 (26.4x) MaxDD=68.6% Trades=522 SpChanges=28931" << std::endl;
    double ratio = mt5_result.final_equity / 263924.0;
    std::cout << "Match ratio: " << std::setprecision(2) << ratio << "x"
              << " (" << std::setprecision(1) << std::abs(ratio - 1.0) * 100.0 << "% off)" << std::endl;

    // === Parameter Sweep (Parallel) ===
    std::cout << "\n=== PARAMETER SWEEP ===" << std::endl;
    std::cout << std::setw(20) << "Config"
              << std::setw(13) << "Equity"
              << std::setw(8) << "Return"
              << std::setw(7) << "MaxDD"
              << std::setw(7) << "Trades"
              << std::setw(8) << "SpChg"
              << std::setw(9) << "Swap"
              << std::setw(5) << "St" << std::endl;
    std::cout << std::string(69, '-') << std::endl;

    // Build sweep configurations
    std::vector<RunConfig> configs;

    // Percentage spacing sweep: survive 18-20, spacing 1.0%-4.0%, lookback 1h/4h
    std::vector<double> survive_vals = {18, 19, 20};
    std::vector<double> pct_spacing_vals = {1.0, 1.5, 2.0, 2.5, 3.0, 3.5, 4.0};
    std::vector<double> lookback_vals = {1.0, 4.0};

    for (double survive : survive_vals) {
        for (double spacing_pct : pct_spacing_vals) {
            for (double lookback : lookback_vals) {
                RunConfig cfg;
                cfg.survive_pct = survive;
                cfg.base_spacing = spacing_pct;
                cfg.lookback_hours = lookback;
                // Measured XAGUSD median ranges: 1h=0.45%, 4h=0.97%
                cfg.typical_vol_pct = (lookback <= 1.0) ? 0.45 : 0.97;
                cfg.min_spacing_abs = 0.05;       // 0.05% of price
                cfg.max_spacing_abs = 15.0;       // 15% of price
                cfg.min_spacing_mult = 0.5;
                cfg.max_spacing_mult = 3.0;
                cfg.swap_long_points = -15.0;
                cfg.spacing_change_threshold = 0.2;
                cfg.pct_spacing = true;
                cfg.label = "s" + std::to_string((int)survive)
                          + "_" + std::to_string(spacing_pct).substr(0, 3) + "%"
                          + "_lb" + std::to_string((int)lookback);
                configs.push_back(cfg);
            }
        }
    }

    // Run sweep (parallel or sequential)
    std::vector<RunResult> results(configs.size());
    auto start_time = std::chrono::steady_clock::now();

    if (num_threads > 1) {
        // Parallel execution
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
    } else {
        // Sequential execution
        for (size_t i = 0; i < configs.size(); i++) {
            results[i] = RunBacktest(configs[i], shared_ticks, true);
        }
    }

    auto elapsed = std::chrono::steady_clock::now() - start_time;
    double elapsed_sec = std::chrono::duration<double>(elapsed).count();

    // === Summary ===
    std::cout << "\n=== SUMMARY (sorted by return) ===" << std::endl;
    std::vector<size_t> indices(results.size());
    for (size_t i = 0; i < indices.size(); i++) indices[i] = i;
    std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
        return results[a].return_x > results[b].return_x;
    });

    std::cout << std::setw(20) << "Config"
              << std::setw(8) << "Return"
              << std::setw(7) << "MaxDD"
              << std::setw(7) << "Trades"
              << std::setw(9) << "Swap"
              << std::setw(5) << "St" << std::endl;
    std::cout << std::string(56, '-') << std::endl;

    for (size_t idx : indices) {
        auto& r = results[idx];
        std::cout << std::setw(20) << r.label
                  << std::setw(7) << std::fixed << std::setprecision(1) << r.return_x << "x"
                  << std::setw(6) << r.max_dd_pct << "%"
                  << std::setw(7) << r.total_trades
                  << std::setw(8) << std::setprecision(0) << r.total_swap << "$"
                  << std::setw(5) << (r.stopped_out ? "SO" : "ok") << std::endl;
    }

    std::cout << "\nCompleted " << configs.size() << " backtests in "
              << std::fixed << std::setprecision(1) << elapsed_sec << "s"
              << " (" << std::setprecision(1) << (elapsed_sec / configs.size()) << "s/run)" << std::endl;

    return 0;
}
