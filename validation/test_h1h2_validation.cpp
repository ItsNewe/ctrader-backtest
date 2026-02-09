/**
 * H1/H2 Out-of-Sample Validation
 *
 * Tests FillUpOscillation ADAPTIVE_SPACING on:
 * - H1 (Jan-Jun 2025): In-sample period
 * - H2 (Jul-Dec 2025): Out-of-sample validation
 *
 * Critical test to detect overfitting like MomentumReversal showed.
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>

using namespace backtest;

struct PeriodResult {
    std::string period;
    double final_balance;
    double return_x;
    double max_dd_pct;
    int total_trades;
    double total_swap;
    bool stopped_out;
    int spacing_changes;
};

PeriodResult RunPeriod(const std::string& period_name,
                       const std::string& start_date,
                       const std::string& end_date,
                       double initial_balance = 10000.0) {
    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
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

    PeriodResult result;
    result.period = period_name;
    result.stopped_out = false;

    try {
        TickBasedEngine engine(config);
        FillUpOscillation strategy(
            13.0,   // survive_pct
            1.5,    // base_spacing
            0.01,   // min_volume
            10.0,   // max_volume
            100.0,  // contract_size
            500.0,  // leverage
            FillUpOscillation::ADAPTIVE_SPACING,
            0.1,    // antifragile_scale
            30.0,   // velocity_threshold
            4.0     // volatility_lookback_hours
        );

        double peak = initial_balance;
        double max_dd = 0;

        engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
            double eq = eng.GetEquity();
            if (eq > peak) peak = eq;
            double dd = (peak - eq) / peak * 100;
            if (dd > max_dd) max_dd = dd;
        });

        auto res = engine.GetResults();
        result.final_balance = res.final_balance;
        result.return_x = res.final_balance / initial_balance;
        result.max_dd_pct = max_dd;
        result.total_trades = res.total_trades;
        result.total_swap = res.total_swap_charged;
        result.spacing_changes = strategy.GetAdaptiveSpacingChanges();

        if (res.final_balance < initial_balance * 0.1) {
            result.stopped_out = true;
        }

    } catch (const std::exception& e) {
        std::cerr << "Error in " << period_name << ": " << e.what() << std::endl;
        result.final_balance = 0;
        result.return_x = 0;
        result.stopped_out = true;
    }

    return result;
}

int main() {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "H1/H2 OUT-OF-SAMPLE VALIDATION" << std::endl;
    std::cout << "FillUpOscillation ADAPTIVE_SPACING Mode" << std::endl;
    std::cout << "Parameters: survive=13%, spacing=$1.50, lookback=4h" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    // Run H1 (Jan-Jun 2025)
    std::cout << "\nRunning H1 (Jan-Jun 2025)..." << std::endl;
    PeriodResult h1 = RunPeriod("H1 (Jan-Jun)", "2025.01.01", "2025.06.30");
    std::cout << "  H1 Result: " << h1.return_x << "x" << (h1.stopped_out ? " STOPPED" : "") << std::endl;

    // Run H2 (Jul-Dec 2025)
    std::cout << "\nRunning H2 (Jul-Dec 2025)..." << std::endl;
    PeriodResult h2 = RunPeriod("H2 (Jul-Dec)", "2025.07.01", "2025.12.30");
    std::cout << "  H2 Result: " << h2.return_x << "x" << (h2.stopped_out ? " STOPPED" : "") << std::endl;

    // Run Full Year for comparison
    std::cout << "\nRunning Full Year 2025..." << std::endl;
    PeriodResult full = RunPeriod("Full Year", "2025.01.01", "2025.12.30");
    std::cout << "  Full Year Result: " << full.return_x << "x" << (full.stopped_out ? " STOPPED" : "") << std::endl;

    // Run H2 with H1 ending balance (sequential)
    std::cout << "\nRunning H2 Sequential (starting with H1 ending balance)..." << std::endl;
    PeriodResult h2_seq = RunPeriod("H2 Sequential", "2025.07.01", "2025.12.30", h1.final_balance);
    std::cout << "  H2 Sequential Result: " << h2_seq.return_x << "x" << (h2_seq.stopped_out ? " STOPPED" : "") << std::endl;

    // Print comparison table
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "RESULTS COMPARISON" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    std::cout << std::setw(20) << "Period"
              << std::setw(12) << "Start Bal"
              << std::setw(12) << "End Bal"
              << std::setw(10) << "Return"
              << std::setw(10) << "Max DD"
              << std::setw(10) << "Trades"
              << std::setw(10) << "Status" << std::endl;
    std::cout << std::string(80, '-') << std::endl;

    auto printRow = [](const PeriodResult& r, double start_bal) {
        std::cout << std::setw(20) << r.period
                  << std::setw(11) << "$" << std::setprecision(0) << start_bal
                  << std::setw(11) << "$" << r.final_balance
                  << std::setw(9) << std::setprecision(2) << r.return_x << "x"
                  << std::setw(9) << r.max_dd_pct << "%"
                  << std::setw(10) << r.total_trades
                  << std::setw(10) << (r.stopped_out ? "STOPPED" : "OK") << std::endl;
    };

    printRow(h1, 10000);
    printRow(h2, 10000);
    printRow(full, 10000);
    printRow(h2_seq, h1.final_balance);

    // Analysis
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "OVERFITTING ANALYSIS" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    double h1_h2_ratio = (h2.return_x > 0) ? h1.return_x / h2.return_x : 999;

    std::cout << "\nH1 Return:      " << h1.return_x << "x" << std::endl;
    std::cout << "H2 Return:      " << h2.return_x << "x" << std::endl;
    std::cout << "H1/H2 Ratio:    " << h1_h2_ratio << std::endl;

    std::cout << "\nH1 Max DD:      " << h1.max_dd_pct << "%" << std::endl;
    std::cout << "H2 Max DD:      " << h2.max_dd_pct << "%" << std::endl;

    std::cout << "\n" << std::string(80, '-') << std::endl;

    // Determine if overfit
    bool h2_survived = !h2.stopped_out && h2.return_x > 0.5;
    bool consistent = h1_h2_ratio < 3.0 && h1_h2_ratio > 0.33;

    if (!h2_survived) {
        std::cout << "VERDICT: OVERFIT - H2 failed to survive" << std::endl;
        std::cout << "Similar to MomentumReversal pattern - DO NOT DEPLOY" << std::endl;
    } else if (!consistent) {
        std::cout << "VERDICT: INCONSISTENT - H1/H2 ratio too extreme (" << h1_h2_ratio << ")" << std::endl;
        std::cout << "Strategy may be sensitive to market regime" << std::endl;
    } else {
        std::cout << "VERDICT: PASSES VALIDATION" << std::endl;
        std::cout << "Strategy shows consistent performance across both periods" << std::endl;
        std::cout << "Combined return (sequential): $10,000 -> $" << h2_seq.final_balance << std::endl;
    }

    return 0;
}
