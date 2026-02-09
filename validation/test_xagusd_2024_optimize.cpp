#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <chrono>

using namespace backtest;

// ============================================================================
// XAGUSD Parameter Optimization on 2024 Data + Forward Test on 2025
//
// Strategy:
//   1. Load 2024 tick data (non-extreme market, ~+30% yearly move)
//   2. Sweep parameters on 2024 H1 (Jan-Jun) as in-sample
//   3. Validate on 2024 H2 (Jul-Dec) as out-of-sample
//   4. Take best configs (H1/H2 consistent) and forward-test on 2025 bull run
//   5. Report which parameters survive all regimes
// ============================================================================

struct SweepConfig {
    double survive_pct;
    double base_spacing_pct;
    double lookback_hours;
    double typical_vol_pct;
    std::string label;
};

struct PeriodResult {
    double final_balance;
    double return_x;
    double max_dd_pct;
    int total_trades;
    double total_swap;
    bool stopped_out;
    double start_price;
    double end_price;
};

struct FullResult {
    std::string label;
    SweepConfig config;

    // 2024 results (in-sample)
    PeriodResult train_full;      // 2024 full year
    PeriodResult train_h1;        // 2024 H1 (Jan-Jun)
    PeriodResult train_h2;        // 2024 H2 (Jul-Dec)
    double train_consistency;     // min(H1/H2, H2/H1) on 2024

    // 2025 results (forward test / out-of-sample)
    PeriodResult test_full;       // 2025 full year
    PeriodResult test_h1;         // 2025 H1 (Jan-Jun)
    PeriodResult test_h2;         // 2025 H2 (Jul-Dec)
    double test_consistency;      // min(H1/H2, H2/H1) on 2025

    // Cross-year consistency
    double cross_year_consistency;  // min(2024_return/2025_return, 2025/2024) normalized

    // Composite score
    double composite_score;
};

PeriodResult RunBacktest(const SweepConfig& cfg, const std::vector<Tick>& ticks,
                         const std::string& start_date, const std::string& end_date) {
    PeriodResult result = {};
    result.stopped_out = false;

    TickDataConfig tick_config;
    tick_config.file_path = "";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickBacktestConfig config;
    config.symbol = "XAGUSD";
    config.initial_balance = 10000.0;
    config.account_currency = "USD";
    config.contract_size = 5000.0;
    config.leverage = 500.0;
    config.margin_rate = 1.0;
    config.pip_size = 0.001;
    config.swap_long = -15.0;
    config.swap_short = 0.0;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = start_date;
    config.end_date = end_date;
    config.verbose = false;
    config.tick_data_config = tick_config;

    try {
        TickBasedEngine engine(config);

        FillUpOscillation::AdaptiveConfig adaptive_cfg;
        adaptive_cfg.typical_vol_pct = cfg.typical_vol_pct;
        adaptive_cfg.min_spacing_mult = 0.5;
        adaptive_cfg.max_spacing_mult = 3.0;
        adaptive_cfg.min_spacing_abs = 0.05;
        adaptive_cfg.max_spacing_abs = 15.0;
        adaptive_cfg.spacing_change_threshold = 0.2;
        adaptive_cfg.pct_spacing = true;

        FillUpOscillation strategy(
            cfg.survive_pct,
            cfg.base_spacing_pct,
            0.01,
            100.0,
            5000.0,
            500.0,
            FillUpOscillation::ADAPTIVE_SPACING,
            0.1,
            30.0,
            cfg.lookback_hours,
            adaptive_cfg
        );

        // Track start/end prices
        double first_price = 0.0, last_price = 0.0;
        engine.RunWithTicks(ticks, [&](const Tick& tick, TickBasedEngine& eng) {
            if (first_price == 0.0) first_price = tick.bid;
            last_price = tick.bid;
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        result.final_balance = results.final_balance;
        result.return_x = results.final_balance / 10000.0;
        result.max_dd_pct = results.max_drawdown_pct;
        result.total_trades = results.total_trades;
        result.total_swap = results.total_swap_charged;
        result.stopped_out = (results.final_balance < 500.0);  // Effectively wiped out
        result.start_price = first_price;
        result.end_price = last_price;
    } catch (const std::exception& e) {
        result.stopped_out = true;
        result.return_x = 0.0;
    }

    return result;
}

std::vector<Tick> LoadTickData(const std::string& file_path) {
    std::vector<Tick> ticks;
    std::cout << "Loading: " << file_path << std::endl;

    auto start = std::chrono::high_resolution_clock::now();

    TickDataConfig config;
    config.file_path = file_path;
    config.format = TickDataFormat::MT5_CSV;
    config.load_all_into_memory = true;

    TickDataManager manager(config);
    Tick tick;
    while (manager.GetNextTick(tick)) {
        ticks.push_back(tick);
    }

    auto end = std::chrono::high_resolution_clock::now();
    double secs = std::chrono::duration<double>(end - start).count();
    std::cout << "  Loaded " << ticks.size() << " ticks in " << std::fixed
              << std::setprecision(1) << secs << "s" << std::endl;

    if (!ticks.empty()) {
        std::cout << "  Date range: " << ticks.front().timestamp << " to " << ticks.back().timestamp << std::endl;
        std::cout << "  Price range: $" << std::setprecision(3) << ticks.front().bid
                  << " to $" << ticks.back().bid << std::endl;
    }

    return ticks;
}

double CalcConsistency(const PeriodResult& h1, const PeriodResult& h2) {
    if (h1.stopped_out || h2.stopped_out) return 0.0;
    if (h1.return_x <= 0.1 || h2.return_x <= 0.1) return 0.0;
    double ratio = h1.return_x / h2.return_x;
    return std::min(ratio, 1.0 / ratio);
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "  XAGUSD Parameter Optimization on 2024 + Forward Test on 2025" << std::endl;
    std::cout << "================================================================" << std::endl;

    // --- Load 2024 data ---
#ifdef _WIN32
    std::string data_2024 = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\XAGUSD\\XAGUSD_TICKS_2024.csv";
    std::string data_2025 = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\XAGUSD\\XAGUSD_TICKS_2025.csv";
#else
    std::string data_2024 = "validation/XAGUSD/XAGUSD_TICKS_2024.csv";
    std::string data_2025 = "validation/XAGUSD/XAGUSD_TICKS_2025.csv";
#endif

    std::cout << "\n--- Phase 1: Load 2024 Data (Training Set) ---" << std::endl;
    std::vector<Tick> ticks_2024 = LoadTickData(data_2024);
    if (ticks_2024.empty()) {
        std::cerr << "\nERROR: No 2024 data found at: " << data_2024 << std::endl;
        std::cerr << "Please run: python download_xagusd_historical.py 2024" << std::endl;
        std::cerr << "with MT5 connected to ." << std::endl;
        return 1;
    }

    std::cout << "\n--- Phase 2: Load 2025 Data (Forward Test Set) ---" << std::endl;
    std::vector<Tick> ticks_2025 = LoadTickData(data_2025);
    if (ticks_2025.empty()) {
        std::cerr << "\nERROR: No 2025 data found at: " << data_2025 << std::endl;
        return 1;
    }

    // Print price action context
    if (!ticks_2024.empty() && !ticks_2025.empty()) {
        double price_2024_start = ticks_2024.front().bid;
        double price_2024_end = ticks_2024.back().bid;
        double price_2025_start = ticks_2025.front().bid;
        double price_2025_end = ticks_2025.back().bid;

        std::cout << "\n--- Price Action Context ---" << std::endl;
        std::cout << "  2024: $" << std::fixed << std::setprecision(2) << price_2024_start
                  << " -> $" << price_2024_end
                  << " (" << std::showpos << ((price_2024_end/price_2024_start - 1) * 100.0)
                  << std::noshowpos << "%)" << std::endl;
        std::cout << "  2025: $" << price_2025_start
                  << " -> $" << price_2025_end
                  << " (" << std::showpos << ((price_2025_end/price_2025_start - 1) * 100.0)
                  << std::noshowpos << "%)" << std::endl;
    }

    // --- Build parameter sweep ---
    std::cout << "\n--- Phase 3: Parameter Sweep on 2024 Data ---" << std::endl;

    std::vector<SweepConfig> configs;

    // Survive percentages: 20%, 25%, 30%, 35%, 40%
    std::vector<double> survive_pcts = {20.0, 25.0, 30.0, 35.0, 40.0};
    // Spacing (% of price): 1%, 2%, 3%, 4%, 5%, 6%, 8%
    std::vector<double> spacing_pcts = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 8.0};
    // Lookback hours: 1, 4
    std::vector<double> lookbacks = {1.0, 4.0};
    // Typical volatility %: 0.5%, 1.0%
    std::vector<double> typvols = {0.5, 1.0};

    for (double survive : survive_pcts) {
        for (double spacing : spacing_pcts) {
            for (double lb : lookbacks) {
                for (double tv : typvols) {
                    SweepConfig cfg;
                    cfg.survive_pct = survive;
                    cfg.base_spacing_pct = spacing;
                    cfg.lookback_hours = lb;
                    cfg.typical_vol_pct = tv;
                    cfg.label = "s" + std::to_string((int)survive)
                              + "_" + std::to_string((int)spacing) + "pct"
                              + "_lb" + std::to_string((int)lb)
                              + "_tv" + std::to_string((int)(tv*10));
                    configs.push_back(cfg);
                }
            }
        }
    }

    std::cout << "  Total configurations: " << configs.size() << std::endl;
    std::cout << "  Periods per config: 3 (2024 full, H1, H2)" << std::endl;
    std::cout << "  Total backtests (Phase 3): " << configs.size() * 3 << std::endl;

    // --- Run 2024 sweep ---
    std::vector<FullResult> all_results;
    int completed = 0;

    for (const auto& cfg : configs) {
        FullResult fr;
        fr.label = cfg.label;
        fr.config = cfg;

        // Run 2024 full year
        fr.train_full = RunBacktest(cfg, ticks_2024, "2024.01.01", "2024.12.31");

        // Run 2024 H1
        fr.train_h1 = RunBacktest(cfg, ticks_2024, "2024.01.01", "2024.06.30");

        // Run 2024 H2
        fr.train_h2 = RunBacktest(cfg, ticks_2024, "2024.07.01", "2024.12.31");

        // Calculate 2024 consistency
        fr.train_consistency = CalcConsistency(fr.train_h1, fr.train_h2);

        all_results.push_back(fr);
        completed++;

        if (completed % 20 == 0 || completed == (int)configs.size()) {
            std::cout << "  Progress: " << completed << "/" << configs.size()
                      << " (" << (completed * 100 / configs.size()) << "%)" << std::endl;
        }
    }

    // --- Sort by 2024 consistency (training set) ---
    std::sort(all_results.begin(), all_results.end(),
              [](const FullResult& a, const FullResult& b) {
                  // Primary: consistency (higher = better)
                  // Secondary: return (higher = better)
                  if (std::abs(a.train_consistency - b.train_consistency) > 0.05)
                      return a.train_consistency > b.train_consistency;
                  return a.train_full.return_x > b.train_full.return_x;
              });

    // --- Print 2024 results (top 30) ---
    std::cout << "\n--- Phase 3 Results: Top 30 on 2024 Data ---" << std::endl;
    std::cout << std::left << std::setw(28) << "Config"
              << std::right << std::setw(8) << "Full"
              << std::setw(8) << "H1"
              << std::setw(8) << "H2"
              << std::setw(8) << "Consist"
              << std::setw(8) << "MaxDD%"
              << std::setw(8) << "Trades"
              << std::endl;
    std::cout << std::string(76, '-') << std::endl;

    int show_count = std::min(30, (int)all_results.size());
    for (int i = 0; i < show_count; i++) {
        const auto& r = all_results[i];
        std::cout << std::left << std::setw(28) << r.label
                  << std::right << std::fixed << std::setprecision(2)
                  << std::setw(8) << r.train_full.return_x << "x"
                  << std::setw(7) << r.train_h1.return_x << "x"
                  << std::setw(7) << r.train_h2.return_x << "x"
                  << std::setw(8) << r.train_consistency
                  << std::setw(7) << r.train_full.max_dd_pct << "%"
                  << std::setw(8) << r.train_full.total_trades
                  << std::endl;
    }

    // --- Phase 4: Forward test top 20 on 2025 ---
    std::cout << "\n--- Phase 4: Forward Test Top 20 on 2025 Bull Market ---" << std::endl;

    // Select top 20 by consistency (minimum 0.3 consistency, not stopped out)
    std::vector<FullResult*> forward_candidates;
    for (auto& r : all_results) {
        if (r.train_consistency >= 0.3 && !r.train_full.stopped_out && r.train_full.return_x > 1.0) {
            forward_candidates.push_back(&r);
            if (forward_candidates.size() >= 20) break;
        }
    }

    std::cout << "  Candidates for forward test: " << forward_candidates.size() << std::endl;
    std::cout << "  Backtests to run: " << forward_candidates.size() * 3 << std::endl;

    completed = 0;
    for (auto* fr : forward_candidates) {
        // Run 2025 full year
        fr->test_full = RunBacktest(fr->config, ticks_2025, "2025.01.01", "2025.12.31");

        // Run 2025 H1
        fr->test_h1 = RunBacktest(fr->config, ticks_2025, "2025.01.01", "2025.06.30");

        // Run 2025 H2
        fr->test_h2 = RunBacktest(fr->config, ticks_2025, "2025.07.01", "2025.12.31");

        // Calculate 2025 consistency
        fr->test_consistency = CalcConsistency(fr->test_h1, fr->test_h2);

        // Cross-year consistency (normalize by price move ratio)
        if (fr->train_full.return_x > 0.1 && fr->test_full.return_x > 0.1) {
            double ratio = fr->train_full.return_x / fr->test_full.return_x;
            fr->cross_year_consistency = std::min(ratio, 1.0 / ratio);
        } else {
            fr->cross_year_consistency = 0.0;
        }

        // Composite score: weights consistency + return + low DD
        // Higher is better
        double return_score = std::min(fr->test_full.return_x, 20.0) / 20.0;  // Cap at 20x
        double dd_score = 1.0 - std::min(fr->test_full.max_dd_pct, 100.0) / 100.0;
        double consist_score = (fr->train_consistency + fr->test_consistency) / 2.0;
        fr->composite_score = consist_score * 0.4 + return_score * 0.3 + dd_score * 0.3;

        completed++;
        if (completed % 5 == 0 || completed == (int)forward_candidates.size()) {
            std::cout << "  Progress: " << completed << "/" << forward_candidates.size() << std::endl;
        }
    }

    // --- Sort forward candidates by composite score ---
    std::sort(forward_candidates.begin(), forward_candidates.end(),
              [](const FullResult* a, const FullResult* b) {
                  return a->composite_score > b->composite_score;
              });

    // --- Print Forward Test Results ---
    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "  FORWARD TEST RESULTS: Parameters Optimized on 2024, Tested on 2025" << std::endl;
    std::cout << std::string(120, '=') << std::endl;

    std::cout << "\n" << std::left << std::setw(28) << "Config"
              << "| " << std::setw(24) << "--- 2024 (Train) ---"
              << "| " << std::setw(28) << "--- 2025 (Forward Test) ---"
              << "| " << std::setw(10) << "Score"
              << std::endl;
    std::cout << std::left << std::setw(28) << ""
              << "| " << std::setw(8) << "Return"
              << std::setw(8) << "DD%"
              << std::setw(8) << "Cons"
              << "| " << std::setw(8) << "Return"
              << std::setw(8) << "DD%"
              << std::setw(8) << "Cons"
              << std::setw(8) << "Srvvd?"
              << "| " << std::setw(8) << "Comp"
              << std::endl;
    std::cout << std::string(120, '-') << std::endl;

    for (const auto* fr : forward_candidates) {
        std::string survived = fr->test_full.stopped_out ? "NO" : "YES";

        std::cout << std::left << std::setw(28) << fr->label
                  << "| " << std::right << std::fixed << std::setprecision(2)
                  << std::setw(7) << fr->train_full.return_x << "x"
                  << std::setw(7) << fr->train_full.max_dd_pct << "%"
                  << std::setw(7) << fr->train_consistency
                  << " | "
                  << std::setw(7) << fr->test_full.return_x << "x"
                  << std::setw(7) << fr->test_full.max_dd_pct << "%"
                  << std::setw(7) << fr->test_consistency
                  << " " << std::left << std::setw(6) << survived
                  << " | " << std::right
                  << std::setw(6) << fr->composite_score
                  << std::endl;
    }

    // --- Summary & Recommendations ---
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "  RECOMMENDATIONS" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    // Find configs that: survived 2025, consistency > 0.3 on both years, return > 1.5x on 2024
    std::cout << "\n  Configs that PASS all criteria:" << std::endl;
    std::cout << "    - Survived 2025 bull market (not stopped out)" << std::endl;
    std::cout << "    - 2024 H1/H2 consistency > 0.3" << std::endl;
    std::cout << "    - 2025 H1/H2 consistency > 0.2" << std::endl;
    std::cout << "    - 2024 return > 1.5x" << std::endl;
    std::cout << "    - 2024 max DD < 60%" << std::endl;
    std::cout << std::endl;

    int pass_count = 0;
    for (const auto* fr : forward_candidates) {
        if (!fr->test_full.stopped_out &&
            fr->train_consistency >= 0.3 &&
            fr->test_consistency >= 0.2 &&
            fr->train_full.return_x >= 1.5 &&
            fr->train_full.max_dd_pct < 60.0) {

            pass_count++;
            std::cout << "    " << pass_count << ". " << fr->label << std::endl;
            std::cout << "       2024: " << std::fixed << std::setprecision(2)
                      << fr->train_full.return_x << "x return, "
                      << fr->train_full.max_dd_pct << "% DD, "
                      << "consistency=" << fr->train_consistency << std::endl;
            std::cout << "       2025: " << fr->test_full.return_x << "x return, "
                      << fr->test_full.max_dd_pct << "% DD, "
                      << "consistency=" << fr->test_consistency << std::endl;
            std::cout << "       Params: survive=" << fr->config.survive_pct << "%, "
                      << "spacing=" << fr->config.base_spacing_pct << "%, "
                      << "lookback=" << fr->config.lookback_hours << "h, "
                      << "typvol=" << fr->config.typical_vol_pct << "%" << std::endl;
            std::cout << std::endl;
        }
    }

    if (pass_count == 0) {
        std::cout << "    [NONE] - No configuration passes all criteria." << std::endl;
        std::cout << "    This suggests the strategy may need structural changes for XAGUSD." << std::endl;

        // Show the closest candidates
        std::cout << "\n  Closest candidates (relaxed criteria):" << std::endl;
        int close_count = 0;
        for (const auto* fr : forward_candidates) {
            if (!fr->test_full.stopped_out && fr->train_full.return_x > 1.0) {
                close_count++;
                std::cout << "    " << close_count << ". " << fr->label
                          << " | 2024: " << std::fixed << std::setprecision(2)
                          << fr->train_full.return_x << "x DD=" << fr->train_full.max_dd_pct
                          << "% cons=" << fr->train_consistency
                          << " | 2025: " << fr->test_full.return_x << "x DD=" << fr->test_full.max_dd_pct
                          << "% cons=" << fr->test_consistency << std::endl;
                if (close_count >= 10) break;
            }
        }
    }

    std::cout << "\n  Total configs tested: " << configs.size() << std::endl;
    std::cout << "  Forward-tested: " << forward_candidates.size() << std::endl;
    std::cout << "  Pass all criteria: " << pass_count << std::endl;

    return 0;
}
