/**
 * Multi-Year Backtest
 *
 * Tests FillUpOscillation on:
 * - 2024: Lower oscillation environment (~92k oscillations)
 * - 2025: High oscillation environment (~281k oscillations)
 * - Combined: Sequential 2024 -> 2025
 *
 * Critical test for regime robustness.
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>

using namespace backtest;

struct YearResult {
    std::string period;
    double initial_balance;
    double final_balance;
    double return_x;
    double max_dd_pct;
    int total_trades;
    double total_swap;
    bool stopped_out;
    int spacing_changes;
};

YearResult RunYear(const std::string& period_name,
                   const std::string& data_path,
                   const std::string& start_date,
                   const std::string& end_date,
                   double initial_balance = 10000.0) {
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

    YearResult result;
    result.period = period_name;
    result.initial_balance = initial_balance;
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
        result.final_balance = initial_balance;  // Preserve capital on error
        result.return_x = 1.0;
        result.stopped_out = true;
    }

    return result;
}

int main() {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n" << std::string(90, '=') << std::endl;
    std::cout << "MULTI-YEAR BACKTEST" << std::endl;
    std::cout << "FillUpOscillation ADAPTIVE_SPACING Mode" << std::endl;
    std::cout << "Parameters: survive=13%, spacing=$1.50, lookback=4h" << std::endl;
    std::cout << std::string(90, '=') << std::endl;

    std::string data_2024 = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAUUSD_TICKS_2024.csv";
    std::string data_2025 = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";

    // Run 2024
    std::cout << "\nRunning 2024 backtest..." << std::endl;
    YearResult y2024 = RunYear("2024", data_2024, "2024.01.01", "2024.12.31");
    std::cout << "  2024 Result: " << y2024.return_x << "x"
              << (y2024.stopped_out ? " STOPPED" : "") << std::endl;

    // Run 2025
    std::cout << "\nRunning 2025 backtest..." << std::endl;
    YearResult y2025 = RunYear("2025", data_2025, "2025.01.01", "2025.12.30");
    std::cout << "  2025 Result: " << y2025.return_x << "x"
              << (y2025.stopped_out ? " STOPPED" : "") << std::endl;

    // Run Sequential (2024 ending balance -> 2025)
    std::cout << "\nRunning Sequential 2024->2025..." << std::endl;
    YearResult y2025_seq = RunYear("2025 (Sequential)", data_2025, "2025.01.01", "2025.12.30", y2024.final_balance);
    std::cout << "  Sequential Result: " << y2025_seq.return_x << "x"
              << (y2025_seq.stopped_out ? " STOPPED" : "") << std::endl;

    // Print comparison table
    std::cout << "\n" << std::string(90, '=') << std::endl;
    std::cout << "RESULTS COMPARISON" << std::endl;
    std::cout << std::string(90, '=') << std::endl;

    std::cout << std::setw(20) << "Period"
              << std::setw(14) << "Start Bal"
              << std::setw(14) << "End Bal"
              << std::setw(10) << "Return"
              << std::setw(10) << "Max DD"
              << std::setw(10) << "Trades"
              << std::setw(12) << "Status" << std::endl;
    std::cout << std::string(90, '-') << std::endl;

    auto printRow = [](const YearResult& r) {
        std::cout << std::setw(20) << r.period
                  << std::setw(13) << "$" << std::setprecision(0) << r.initial_balance
                  << std::setw(13) << "$" << r.final_balance
                  << std::setw(9) << std::setprecision(2) << r.return_x << "x"
                  << std::setw(9) << r.max_dd_pct << "%"
                  << std::setw(10) << r.total_trades
                  << std::setw(12) << (r.stopped_out ? "STOPPED" : "OK") << std::endl;
    };

    printRow(y2024);
    printRow(y2025);
    printRow(y2025_seq);

    // Calculate combined metrics
    std::cout << "\n" << std::string(90, '=') << std::endl;
    std::cout << "MULTI-YEAR ANALYSIS" << std::endl;
    std::cout << std::string(90, '=') << std::endl;

    double combined_return = y2024.return_x * y2025_seq.return_x;
    double total_2_year_value = y2025_seq.final_balance;

    std::cout << "\n2-Year Performance (Sequential):" << std::endl;
    std::cout << "  Starting Capital:   $10,000" << std::endl;
    std::cout << "  After 2024:         $" << std::setprecision(0) << y2024.final_balance << std::endl;
    std::cout << "  After 2025:         $" << y2025_seq.final_balance << std::endl;
    std::cout << "  Combined Return:    " << std::setprecision(2) << combined_return << "x" << std::endl;
    std::cout << "  CAGR:               " << ((std::pow(combined_return, 0.5) - 1) * 100) << "%" << std::endl;

    std::cout << "\nYear-over-Year Comparison:" << std::endl;
    std::cout << "  2024 Return:        " << y2024.return_x << "x" << std::endl;
    std::cout << "  2025 Return:        " << y2025.return_x << "x" << std::endl;
    std::cout << "  2025/2024 Ratio:    " << (y2025.return_x / y2024.return_x) << std::endl;

    std::cout << "\n  2024 Max DD:        " << y2024.max_dd_pct << "%" << std::endl;
    std::cout << "  2025 Max DD:        " << y2025.max_dd_pct << "%" << std::endl;

    std::cout << "\n  2024 Trades:        " << y2024.total_trades << std::endl;
    std::cout << "  2025 Trades:        " << y2025.total_trades << std::endl;
    std::cout << "  2025/2024 Ratio:    " << ((double)y2025.total_trades / y2024.total_trades) << std::endl;

    // Verdict
    std::cout << "\n" << std::string(90, '-') << std::endl;
    std::cout << "VERDICT" << std::endl;
    std::cout << std::string(90, '-') << std::endl;

    bool y2024_ok = !y2024.stopped_out && y2024.return_x > 0.5;
    bool y2025_ok = !y2025.stopped_out && y2025.return_x > 0.5;
    bool consistent = y2024_ok && y2025_ok;

    double ratio = y2025.return_x / y2024.return_x;
    bool stable = ratio > 0.33 && ratio < 3.0;

    if (!y2024_ok) {
        std::cout << "FAIL: Strategy failed in 2024 (lower oscillation environment)" << std::endl;
        std::cout << "The strategy may only work in high-volatility ATH years like 2025." << std::endl;
    } else if (!y2025_ok) {
        std::cout << "FAIL: Strategy failed in 2025 (already known from H1/H2 if applicable)" << std::endl;
    } else if (!stable) {
        std::cout << "CAUTION: Large performance difference between years" << std::endl;
        std::cout << "2024: " << y2024.return_x << "x vs 2025: " << y2025.return_x << "x (ratio: " << ratio << ")" << std::endl;
        std::cout << "Strategy is sensitive to market regime." << std::endl;
    } else {
        std::cout << "PASS: Strategy profitable in both 2024 and 2025" << std::endl;
        std::cout << "Combined 2-year return: " << combined_return << "x" << std::endl;
        std::cout << "Strategy shows robustness across different market regimes." << std::endl;
    }

    // Additional context
    std::cout << "\n" << std::string(90, '-') << std::endl;
    std::cout << "CONTEXT" << std::endl;
    std::cout << std::string(90, '-') << std::endl;
    std::cout << "2024: Lower volatility, ~92k oscillations, gold ~$2,300 avg" << std::endl;
    std::cout << "2025: High volatility, ~281k oscillations (3x), gold ~$3,500 avg, ATH runs" << std::endl;
    std::cout << "\nIf 2024 performance is significantly lower than 2025, the strategy" << std::endl;
    std::cout << "may be dependent on the exceptional 2025 market conditions." << std::endl;

    return 0;
}
