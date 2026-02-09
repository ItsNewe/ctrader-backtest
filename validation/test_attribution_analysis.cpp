/**
 * Attribution Analysis: Decomposing FillUpOscillation Returns
 *
 * This test answers: How much of ADAPTIVE_SPACING's performance is due to:
 * 1. Directional alpha (gold's uptrend)
 * 2. Spacing alpha (having the right fixed spacing)
 * 3. Adaptation alpha (dynamically changing spacing)
 *
 * Method:
 * - Run BASELINE with spacing 0.1 to 10.0 (100 configs)
 * - Run ADAPTIVE_SPACING, track average effective spacing
 * - Run BASELINE at ADAPTIVE's average spacing
 * - Calculate buy-and-hold benchmark
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <cmath>

using namespace backtest;

struct TestResult {
    double spacing;
    double final_balance;
    double return_multiple;
    double max_dd_pct;
    int total_trades;
    bool stopped_out;
    double avg_spacing;  // For ADAPTIVE mode
};

// Track spacing samples during ADAPTIVE run
struct SpacingTracker {
    double sum = 0;
    int count = 0;
    double min_spacing = 999;
    double max_spacing = 0;

    void sample(double spacing) {
        sum += spacing;
        count++;
        min_spacing = std::min(min_spacing, spacing);
        max_spacing = std::max(max_spacing, spacing);
    }

    double average() const { return count > 0 ? sum / count : 0; }
};

TestResult RunBaseline(double spacing, const std::string& data_path,
                       const std::string& start_date, const std::string& end_date,
                       double initial_balance, double survive_pct) {
    TestResult result;
    result.spacing = spacing;
    result.avg_spacing = spacing;  // Fixed for baseline

    TickDataConfig tick_config;
    tick_config.file_path = data_path;
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = initial_balance;
    config.account_currency = "USD";
    config.contract_size = 100.0;
    config.leverage = 500.0;
    config.margin_rate = 1.0;
    config.pip_size = 0.01;
    config.swap_long = -66.99;
    config.swap_short = 41.2;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = start_date;
    config.end_date = end_date;
    config.tick_data_config = tick_config;
    config.verbose = false;

    try {
        TickBasedEngine engine(config);

        FillUpOscillation strategy(
            survive_pct,
            spacing,      // Fixed spacing
            0.01,         // min_volume
            10.0,         // max_volume
            100.0,        // contract_size
            500.0,        // leverage
            FillUpOscillation::BASELINE  // No adaptation
        );

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto res = engine.GetResults();
        result.final_balance = res.final_balance;
        result.return_multiple = res.final_balance / initial_balance;
        double peak_equity = res.final_balance + res.max_drawdown;
        result.max_dd_pct = (peak_equity > 0) ? (res.max_drawdown / peak_equity * 100.0) : 0;
        result.total_trades = res.total_trades;
        result.stopped_out = engine.IsStopOutOccurred();

    } catch (const std::exception& e) {
        result.final_balance = 0;
        result.return_multiple = 0;
        result.max_dd_pct = 100;
        result.stopped_out = true;
    }

    return result;
}

TestResult RunAdaptive(const std::string& data_path,
                       const std::string& start_date, const std::string& end_date,
                       double initial_balance, double survive_pct, double base_spacing,
                       SpacingTracker& tracker) {
    TestResult result;
    result.spacing = base_spacing;

    TickDataConfig tick_config;
    tick_config.file_path = data_path;
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = initial_balance;
    config.account_currency = "USD";
    config.contract_size = 100.0;
    config.leverage = 500.0;
    config.margin_rate = 1.0;
    config.pip_size = 0.01;
    config.swap_long = -66.99;
    config.swap_short = 41.2;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = start_date;
    config.end_date = end_date;
    config.tick_data_config = tick_config;
    config.verbose = false;

    try {
        TickBasedEngine engine(config);

        FillUpOscillation strategy(
            survive_pct,
            base_spacing,
            0.01,
            10.0,
            100.0,
            500.0,
            FillUpOscillation::ADAPTIVE_SPACING,
            0.1,   // antifragile_scale (unused)
            30.0,  // velocity_threshold (unused)
            4.0    // lookback_hours
        );

        long sample_interval = 100000;  // Sample spacing every 100k ticks
        long tick_count = 0;

        engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
            tick_count++;
            if (tick_count % sample_interval == 0) {
                tracker.sample(strategy.GetCurrentSpacing());
            }
        });

        auto res = engine.GetResults();
        result.final_balance = res.final_balance;
        result.return_multiple = res.final_balance / initial_balance;
        double peak_equity = res.final_balance + res.max_drawdown;
        result.max_dd_pct = (peak_equity > 0) ? (res.max_drawdown / peak_equity * 100.0) : 0;
        result.total_trades = res.total_trades;
        result.stopped_out = engine.IsStopOutOccurred();
        result.avg_spacing = tracker.average();

    } catch (const std::exception& e) {
        result.final_balance = 0;
        result.return_multiple = 0;
        result.max_dd_pct = 100;
        result.stopped_out = true;
    }

    return result;
}

// Calculate buy-and-hold return (simple: buy 1 lot at start, sell at end)
double CalculateBuyAndHold(const std::string& data_path,
                           const std::string& start_date, const std::string& end_date,
                           double initial_balance) {
    TickDataConfig tick_config;
    tick_config.file_path = data_path;
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = initial_balance;
    config.account_currency = "USD";
    config.contract_size = 100.0;
    config.leverage = 500.0;
    config.margin_rate = 1.0;
    config.pip_size = 0.01;
    config.swap_long = -66.99;
    config.swap_short = 41.2;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = start_date;
    config.end_date = end_date;
    config.tick_data_config = tick_config;
    config.verbose = false;

    double start_price = 0;
    double end_price = 0;
    double lot_size = 0.1;  // Small position for comparison

    try {
        TickBasedEngine engine(config);

        bool first_tick = true;
        engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
            if (first_tick) {
                start_price = tick.ask;
                // Open a single long position
                eng.OpenMarketOrder("BUY", lot_size, 0, 0);
                first_tick = false;
            }
            end_price = tick.bid;
        });

        // Calculate P/L from the position
        double price_change = end_price - start_price;
        double pnl = price_change * lot_size * 100.0;  // contract_size = 100

        // Approximate swap cost (rough estimate)
        double days = 252;  // Trading days
        double daily_swap = -66.99 * 0.01 * 100 * lot_size;  // per day
        double total_swap = daily_swap * days * 1.4;  // 1.4x for triple swap days

        double final_equity = initial_balance + pnl + total_swap;
        return final_equity / initial_balance;

    } catch (const std::exception& e) {
        return 1.0;
    }
}

int main() {
    std::cout << "=== Attribution Analysis ===" << std::endl;
    std::cout << "Decomposing FillUpOscillation returns" << std::endl;
    std::cout << std::endl;

    double initial_balance = 10000.0;
    double survive_pct = 13.0;
    std::string data_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    std::string start_date = "2025.01.01";
    std::string end_date = "2025.12.30";

    // =========================================================================
    // Step 1: Buy-and-Hold Benchmark
    // =========================================================================
    std::cout << "Step 1: Calculating buy-and-hold benchmark..." << std::endl;
    double buy_hold_return = CalculateBuyAndHold(data_path, start_date, end_date, initial_balance);
    std::cout << "Buy-and-Hold Return: " << std::fixed << std::setprecision(2)
              << buy_hold_return << "x" << std::endl;
    std::cout << std::endl;

    // =========================================================================
    // Step 2: BASELINE Sweep (spacing 0.1 to 10.0)
    // =========================================================================
    std::cout << "Step 2: Running BASELINE sweep (spacing 0.1 to 10.0)..." << std::endl;
    std::cout << "This will take a while (~100 configurations)..." << std::endl;
    std::cout << std::endl;

    std::vector<TestResult> baseline_results;
    TestResult best_baseline;
    best_baseline.return_multiple = 0;

    // Coarse sweep first (0.5 steps) to find region of interest
    std::cout << "Phase 2a: Coarse sweep (0.5 steps)..." << std::endl;
    for (double spacing = 0.5; spacing <= 10.0; spacing += 0.5) {
        auto result = RunBaseline(spacing, data_path, start_date, end_date,
                                   initial_balance, survive_pct);
        baseline_results.push_back(result);

        if (!result.stopped_out && result.return_multiple > best_baseline.return_multiple) {
            best_baseline = result;
        }

        std::cout << "  Spacing $" << std::fixed << std::setprecision(1) << spacing
                  << ": " << std::setprecision(2) << result.return_multiple << "x"
                  << (result.stopped_out ? " [STOPPED]" : "") << std::endl;
    }

    // Fine sweep around best region
    double best_region = best_baseline.spacing;
    std::cout << std::endl;
    std::cout << "Phase 2b: Fine sweep around $" << best_region << "..." << std::endl;

    for (double spacing = std::max(0.1, best_region - 1.0);
         spacing <= best_region + 1.0; spacing += 0.1) {
        // Skip already tested values
        bool already_tested = false;
        for (const auto& r : baseline_results) {
            if (std::abs(r.spacing - spacing) < 0.05) {
                already_tested = true;
                break;
            }
        }
        if (already_tested) continue;

        auto result = RunBaseline(spacing, data_path, start_date, end_date,
                                   initial_balance, survive_pct);
        baseline_results.push_back(result);

        if (!result.stopped_out && result.return_multiple > best_baseline.return_multiple) {
            best_baseline = result;
        }

        std::cout << "  Spacing $" << std::fixed << std::setprecision(2) << spacing
                  << ": " << result.return_multiple << "x"
                  << (result.stopped_out ? " [STOPPED]" : "") << std::endl;
    }

    std::cout << std::endl;
    std::cout << "Best BASELINE: spacing=$" << std::fixed << std::setprecision(2)
              << best_baseline.spacing << " -> " << best_baseline.return_multiple << "x"
              << " (DD: " << std::setprecision(0) << best_baseline.max_dd_pct << "%)" << std::endl;

    // =========================================================================
    // Step 3: ADAPTIVE_SPACING with tracking
    // =========================================================================
    std::cout << std::endl;
    std::cout << "Step 3: Running ADAPTIVE_SPACING with spacing tracking..." << std::endl;

    SpacingTracker tracker;
    auto adaptive_result = RunAdaptive(data_path, start_date, end_date,
                                        initial_balance, survive_pct, 1.5, tracker);

    std::cout << "ADAPTIVE Return: " << std::fixed << std::setprecision(2)
              << adaptive_result.return_multiple << "x" << std::endl;
    std::cout << "ADAPTIVE Avg Spacing: $" << tracker.average() << std::endl;
    std::cout << "ADAPTIVE Spacing Range: $" << tracker.min_spacing
              << " - $" << tracker.max_spacing << std::endl;
    std::cout << "ADAPTIVE Max DD: " << std::setprecision(0)
              << adaptive_result.max_dd_pct << "%" << std::endl;

    // =========================================================================
    // Step 4: BASELINE at ADAPTIVE's average spacing
    // =========================================================================
    std::cout << std::endl;
    std::cout << "Step 4: Running BASELINE at ADAPTIVE's average spacing ($"
              << std::fixed << std::setprecision(2) << tracker.average() << ")..." << std::endl;

    auto baseline_at_avg = RunBaseline(tracker.average(), data_path, start_date, end_date,
                                        initial_balance, survive_pct);

    std::cout << "BASELINE(avg) Return: " << baseline_at_avg.return_multiple << "x" << std::endl;

    // =========================================================================
    // Step 5: Attribution Calculation
    // =========================================================================
    std::cout << std::endl;
    std::cout << "==========================================================" << std::endl;
    std::cout << "                   ATTRIBUTION ANALYSIS" << std::endl;
    std::cout << "==========================================================" << std::endl;
    std::cout << std::endl;

    std::cout << "RAW RETURNS:" << std::endl;
    std::cout << "  Buy-and-Hold:           " << std::fixed << std::setprecision(2)
              << buy_hold_return << "x" << std::endl;
    std::cout << "  Best Fixed Spacing:     " << best_baseline.return_multiple << "x"
              << " (at $" << best_baseline.spacing << ")" << std::endl;
    std::cout << "  ADAPTIVE_SPACING:       " << adaptive_result.return_multiple << "x"
              << " (avg $" << tracker.average() << ")" << std::endl;
    std::cout << "  Fixed at Avg Spacing:   " << baseline_at_avg.return_multiple << "x"
              << " (at $" << tracker.average() << ")" << std::endl;
    std::cout << std::endl;

    // Calculate alphas
    double directional_alpha = buy_hold_return - 1.0;  // Return from gold's move
    double best_fixed_excess = best_baseline.return_multiple - buy_hold_return;
    double adaptive_excess = adaptive_result.return_multiple - buy_hold_return;
    double adaptation_alpha = adaptive_result.return_multiple - baseline_at_avg.return_multiple;
    double spacing_alpha = baseline_at_avg.return_multiple - buy_hold_return;

    std::cout << "ALPHA DECOMPOSITION:" << std::endl;
    std::cout << "  Directional Alpha (gold uptrend):    " << std::showpos
              << directional_alpha * 100 << "%" << std::noshowpos << std::endl;
    std::cout << "  Best Fixed Spacing Excess:           " << std::showpos
              << best_fixed_excess * 100 << "%" << std::noshowpos
              << " (strategy over buy-hold)" << std::endl;
    std::cout << std::endl;

    std::cout << "ADAPTIVE BREAKDOWN:" << std::endl;
    std::cout << "  Spacing Alpha (having avg spacing):  " << std::showpos
              << spacing_alpha * 100 << "%" << std::noshowpos << std::endl;
    std::cout << "  Adaptation Alpha (dynamic change):   " << std::showpos
              << adaptation_alpha * 100 << "%" << std::noshowpos << std::endl;
    std::cout << std::endl;

    // Percentage attribution
    double total_excess = adaptive_result.return_multiple - 1.0;
    if (total_excess > 0) {
        std::cout << "PERCENTAGE ATTRIBUTION (of total " << total_excess * 100 << "% gain):" << std::endl;
        std::cout << "  From Gold Uptrend:     " << std::fixed << std::setprecision(1)
                  << (directional_alpha / total_excess * 100) << "%" << std::endl;
        std::cout << "  From Spacing Choice:   "
                  << (spacing_alpha / total_excess * 100) << "%" << std::endl;
        std::cout << "  From Adaptation:       "
                  << (adaptation_alpha / total_excess * 100) << "%" << std::endl;
    }

    std::cout << std::endl;
    std::cout << "==========================================================" << std::endl;
    std::cout << "VERDICT:" << std::endl;

    if (adaptation_alpha > 0.1) {  // More than 10% improvement
        std::cout << "  ADAPTIVE_SPACING provides meaningful improvement ("
                  << std::showpos << adaptation_alpha * 100 << "%" << std::noshowpos
                  << ") over fixed spacing." << std::endl;
    } else if (adaptation_alpha > 0) {
        std::cout << "  ADAPTIVE_SPACING provides marginal improvement ("
                  << std::showpos << adaptation_alpha * 100 << "%" << std::noshowpos
                  << "). Most gain is from spacing choice." << std::endl;
    } else {
        std::cout << "  ADAPTIVE_SPACING provides NO improvement over fixed spacing!" << std::endl;
        std::cout << "  The dynamic adaptation adds complexity without benefit." << std::endl;
    }

    if (best_baseline.return_multiple > adaptive_result.return_multiple) {
        std::cout << std::endl;
        std::cout << "  NOTE: Best fixed spacing ($" << best_baseline.spacing
                  << ") OUTPERFORMS ADAPTIVE!" << std::endl;
        std::cout << "  Consider using fixed spacing of $" << best_baseline.spacing << std::endl;
    }

    std::cout << "==========================================================" << std::endl;

    // =========================================================================
    // Bonus: Full spacing curve
    // =========================================================================
    std::cout << std::endl;
    std::cout << "SPACING vs RETURN CURVE (survived only):" << std::endl;
    std::cout << std::setw(10) << "Spacing" << std::setw(12) << "Return"
              << std::setw(10) << "MaxDD%" << std::endl;
    std::cout << std::string(32, '-') << std::endl;

    // Sort by spacing
    std::sort(baseline_results.begin(), baseline_results.end(),
              [](const TestResult& a, const TestResult& b) { return a.spacing < b.spacing; });

    for (const auto& r : baseline_results) {
        if (!r.stopped_out) {
            std::cout << std::setw(10) << std::fixed << std::setprecision(2) << r.spacing
                      << std::setw(12) << r.return_multiple << "x"
                      << std::setw(10) << std::setprecision(0) << r.max_dd_pct << "%"
                      << std::endl;
        }
    }

    return 0;
}
