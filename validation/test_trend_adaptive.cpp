/**
 * Test TREND_ADAPTIVE spacing mode
 *
 * Compares three approaches:
 * 1. Fixed spacing ($0.30) - best from optimization
 * 2. ADAPTIVE_SPACING - volatility-based adjustment
 * 3. TREND_ADAPTIVE - trend strength-based adjustment
 *
 * Hypothesis: TREND_ADAPTIVE should outperform ADAPTIVE_SPACING
 * because trend strength (not volatility) determines optimal spacing.
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>

using namespace backtest;

struct TestResult {
    std::string mode_name;
    double return_multiple;
    double max_dd_pct;
    int total_trades;
    double avg_spacing;
    int spacing_changes;
    bool stopped_out;
};

TestResult RunTest(FillUpOscillation::Mode mode, double base_spacing,
                   const std::string& data_path,
                   const std::string& start_date, const std::string& end_date,
                   double initial_balance) {
    TestResult result;

    switch (mode) {
        case FillUpOscillation::BASELINE:
            result.mode_name = "Fixed $" + std::to_string(base_spacing).substr(0, 4);
            break;
        case FillUpOscillation::ADAPTIVE_SPACING:
            result.mode_name = "Vol-Adaptive";
            break;
        case FillUpOscillation::TREND_ADAPTIVE:
            result.mode_name = "Trend-Adaptive";
            break;
        default:
            result.mode_name = "Unknown";
    }

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

    // Track spacing samples
    double spacing_sum = 0;
    int spacing_count = 0;

    try {
        TickBasedEngine engine(config);

        FillUpOscillation strategy(
            13.0,           // survive_pct
            base_spacing,   // base_spacing
            0.01,           // min_volume
            10.0,           // max_volume
            100.0,          // contract_size
            500.0,          // leverage
            mode,
            0.1,            // antifragile_scale
            30.0,           // velocity_threshold
            1.0             // volatility_lookback_hours
        );

        engine.Run([&strategy, &spacing_sum, &spacing_count](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
            // Sample spacing every ~1000 ticks
            spacing_count++;
            if (spacing_count % 1000 == 0) {
                spacing_sum += strategy.GetCurrentSpacing();
            }
        });

        auto res = engine.GetResults();
        result.return_multiple = res.final_balance / initial_balance;
        double peak_equity = res.final_balance + res.max_drawdown;
        result.max_dd_pct = (peak_equity > 0) ? (res.max_drawdown / peak_equity * 100.0) : 0;
        result.total_trades = res.total_trades;
        result.avg_spacing = (spacing_count > 0) ? (spacing_sum / (spacing_count / 1000)) : base_spacing;
        result.spacing_changes = strategy.GetAdaptiveSpacingChanges() + strategy.GetTrendSpacingChanges();
        result.stopped_out = engine.IsStopOutOccurred();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        result.return_multiple = 0;
        result.max_dd_pct = 100;
        result.stopped_out = true;
    }

    return result;
}

void PrintResults(const std::vector<TestResult>& results) {
    std::cout << std::setw(16) << "Mode"
              << std::setw(10) << "Return"
              << std::setw(10) << "MaxDD%"
              << std::setw(10) << "Trades"
              << std::setw(12) << "AvgSpacing"
              << std::setw(10) << "Changes"
              << std::setw(10) << "Status"
              << std::endl;
    std::cout << std::string(78, '-') << std::endl;

    for (const auto& r : results) {
        std::cout << std::setw(16) << r.mode_name
                  << std::setw(9) << std::fixed << std::setprecision(2) << r.return_multiple << "x"
                  << std::setw(9) << std::setprecision(0) << r.max_dd_pct << "%"
                  << std::setw(10) << r.total_trades
                  << std::setw(12) << std::setprecision(2) << "$" << r.avg_spacing
                  << std::setw(10) << r.spacing_changes
                  << std::setw(10) << (r.stopped_out ? "STOP" : "OK")
                  << std::endl;
    }
}

int main() {
    std::cout << "=== Trend-Adaptive Spacing Test ===" << std::endl;
    std::cout << "Comparing volatility-based vs trend-based spacing adjustment" << std::endl;
    std::cout << std::endl;

    std::string data_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    double initial_balance = 10000.0;

    // Test periods
    struct Period {
        std::string name;
        std::string start;
        std::string end;
    };

    std::vector<Period> periods = {
        {"Full Year", "2025.01.01", "2025.12.30"},
        {"Q1 2025",   "2025.01.01", "2025.04.01"},
        {"Q2 2025",   "2025.04.01", "2025.07.01"},
        {"Q3 2025",   "2025.07.01", "2025.10.01"},
        {"Q4 2025",   "2025.10.01", "2025.12.30"},
    };

    for (const auto& period : periods) {
        std::cout << "=== " << period.name << " ===" << std::endl;

        std::vector<TestResult> results;

        // Fixed $0.30 (best from optimization)
        std::cout << "Testing Fixed $0.30..." << std::endl;
        results.push_back(RunTest(FillUpOscillation::BASELINE, 0.30,
                                  data_path, period.start, period.end, initial_balance));

        // Volatility-adaptive
        std::cout << "Testing Vol-Adaptive..." << std::endl;
        results.push_back(RunTest(FillUpOscillation::ADAPTIVE_SPACING, 1.5,
                                  data_path, period.start, period.end, initial_balance));

        // Trend-adaptive
        std::cout << "Testing Trend-Adaptive..." << std::endl;
        results.push_back(RunTest(FillUpOscillation::TREND_ADAPTIVE, 1.5,
                                  data_path, period.start, period.end, initial_balance));

        PrintResults(results);

        // Analysis
        auto& fixed = results[0];
        auto& vol_adapt = results[1];
        auto& trend_adapt = results[2];

        std::cout << std::endl;
        std::cout << "Analysis:" << std::endl;

        if (!trend_adapt.stopped_out && !vol_adapt.stopped_out) {
            double trend_vs_vol = (trend_adapt.return_multiple - vol_adapt.return_multiple)
                                  / vol_adapt.return_multiple * 100.0;
            double trend_vs_fixed = (trend_adapt.return_multiple - fixed.return_multiple)
                                   / fixed.return_multiple * 100.0;

            std::cout << "  Trend vs Vol-Adaptive: " << std::showpos << std::fixed
                      << std::setprecision(1) << trend_vs_vol << "%" << std::noshowpos << std::endl;
            std::cout << "  Trend vs Fixed $0.30:  " << std::showpos
                      << trend_vs_fixed << "%" << std::noshowpos << std::endl;

            if (trend_adapt.return_multiple > vol_adapt.return_multiple &&
                trend_adapt.return_multiple > fixed.return_multiple) {
                std::cout << "  -> TREND_ADAPTIVE WINS!" << std::endl;
            } else if (fixed.return_multiple > trend_adapt.return_multiple) {
                std::cout << "  -> Fixed $0.30 still best" << std::endl;
            } else {
                std::cout << "  -> Vol-Adaptive best" << std::endl;
            }
        }

        std::cout << std::endl;
    }

    // Summary
    std::cout << "=== SUMMARY ===" << std::endl;
    std::cout << "Trend-adaptive spacing adjusts based on price momentum:" << std::endl;
    std::cout << "  - Strong trend (>10%): tight spacing ($0.20-$0.50)" << std::endl;
    std::cout << "  - Weak trend (<6%):    wide spacing ($1.50-$5.00)" << std::endl;
    std::cout << std::endl;
    std::cout << "This should outperform vol-adaptive in choppy periods (Q2 2025)" << std::endl;
    std::cout << "where high volatility != strong trend." << std::endl;

    return 0;
}
