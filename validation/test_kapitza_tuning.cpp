/**
 * Kapitza Parameter Tuning - Finding the Right Balance
 *
 * The baseline Kapitza is too conservative (1.47x return vs 7.05x oscillation).
 * This sweep tests parameter combinations to find optimal risk/return balance.
 */

#include "../include/fill_up_kapitza.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>

using namespace backtest;

struct TestConfig {
    std::string name;
    double target_dd_pct;
    double trend_threshold;
    double resonance_threshold;
    double pid_kp;
    double pid_ki;
    double pid_kd;
    bool allow_trending_down;
    bool allow_high_vol;
    double trending_mult;
    double volatility_mult;
    double resonance_mult;
};

struct TestResult {
    std::string name;
    double final_balance;
    double return_multiple;
    double max_dd_dollars;
    double max_dd_pct;      // As percentage of peak equity (correct method)
    double total_swap;
    int total_trades;
    bool stopped_out;
    double sharpe_approx;   // Return / max_dd as rough risk-adjusted metric
};

TestResult RunTest(const TestConfig& tc, const std::string& data_path,
                   const std::string& start_date, const std::string& end_date,
                   double initial_balance) {
    TestResult result;
    result.name = tc.name;

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

        FillUpKapitza::Config kconfig;
        kconfig.survive_pct = 13.0;
        kconfig.base_spacing = 1.5;
        kconfig.min_volume = 0.01;
        kconfig.max_volume = 10.0;
        kconfig.contract_size = 100.0;
        kconfig.leverage = 500.0;
        kconfig.lookback_hours = 1.0;
        kconfig.typical_vol_pct = 0.5;
        kconfig.momentum_period = 50;
        kconfig.reversal_threshold = 0.3;

        // Apply tuned parameters
        kconfig.target_dd_pct = tc.target_dd_pct;
        kconfig.trend_threshold = tc.trend_threshold;
        kconfig.resonance_threshold = tc.resonance_threshold;
        kconfig.pid_kp = tc.pid_kp;
        kconfig.pid_ki = tc.pid_ki;
        kconfig.pid_kd = tc.pid_kd;
        kconfig.regime_lookback = 200;
        kconfig.allow_trending_down_entry = tc.allow_trending_down;
        kconfig.allow_high_vol_entry = tc.allow_high_vol;
        kconfig.trending_down_mult = tc.trending_mult;
        kconfig.high_volatility_mult = tc.volatility_mult;
        kconfig.resonance_mult = tc.resonance_mult;

        FillUpKapitza strategy(kconfig);

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto res = engine.GetResults();
        result.final_balance = res.final_balance;
        result.return_multiple = res.final_balance / initial_balance;
        result.max_dd_dollars = res.max_drawdown;
        // Calculate DD% correctly: as percentage of peak equity
        // Peak equity ≈ current balance at trough + drawdown amount
        // For strategies that recovered: peak = final + max_dd (approximately)
        double peak_equity = res.final_balance + res.max_drawdown;
        result.max_dd_pct = (peak_equity > 0) ? (res.max_drawdown / peak_equity * 100.0) : 0;
        result.total_swap = res.total_swap_charged;
        result.total_trades = res.total_trades;
        result.stopped_out = engine.IsStopOutOccurred();
        result.sharpe_approx = (result.max_dd_pct > 0) ?
            (result.return_multiple - 1.0) / (result.max_dd_pct / 100.0) : 0;

    } catch (const std::exception& e) {
        result.final_balance = 0;
        result.return_multiple = 0;
        result.max_dd_dollars = initial_balance;
        result.max_dd_pct = 100;
        result.stopped_out = true;
        result.sharpe_approx = 0;
    }

    return result;
}

int main() {
    std::cout << "=== Kapitza Parameter Tuning ===" << std::endl;
    std::cout << "Finding the right balance between return and drawdown" << std::endl;
    std::cout << std::endl;

    double initial_balance = 10000.0;
    std::string data_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";

    // Format: name, target_dd%, trend_thresh, res_thresh, kp, ki, kd, allow_trend, allow_vol, trend_mult, vol_mult, res_mult
    std::vector<TestConfig> configs = {
        // Original (too conservative)
        {"Original",   20.0, 0.6,  0.8,  0.5,  0.1,   0.2,  false, false, 0.5, 0.3, 0.5},

        // Higher target DD - allow more drawdown
        {"DD30%",      30.0, 0.6,  0.8,  0.5,  0.1,   0.2,  false, false, 0.5, 0.3, 0.5},
        {"DD40%",      40.0, 0.6,  0.8,  0.5,  0.1,   0.2,  false, false, 0.5, 0.3, 0.5},
        {"DD50%",      50.0, 0.6,  0.8,  0.5,  0.1,   0.2,  false, false, 0.5, 0.3, 0.5},

        // Relaxed trend threshold - less likely to detect trends
        {"Trend0.7",   40.0, 0.7,  0.8,  0.5,  0.1,   0.2,  false, false, 0.5, 0.3, 0.5},
        {"Trend0.8",   40.0, 0.8,  0.8,  0.5,  0.1,   0.2,  false, false, 0.5, 0.3, 0.5},
        {"Trend0.9",   40.0, 0.9,  0.8,  0.5,  0.1,   0.2,  false, false, 0.5, 0.3, 0.5},

        // Relaxed resonance - less likely to pause
        {"Res0.9",     40.0, 0.8,  0.9,  0.5,  0.1,   0.2,  false, false, 0.5, 0.3, 0.5},
        {"Res0.95",    40.0, 0.8,  0.95, 0.5,  0.1,   0.2,  false, false, 0.5, 0.3, 0.5},
        {"Res1.0",     40.0, 0.8,  1.0,  0.5,  0.1,   0.2,  false, false, 0.5, 0.3, 0.5},

        // Reduced PID gains - less aggressive sizing
        {"PID-Low",    40.0, 0.8,  0.95, 0.2,  0.02,  0.05, false, false, 0.5, 0.3, 0.5},
        {"PID-Min",    40.0, 0.8,  0.95, 0.1,  0.01,  0.02, false, false, 0.5, 0.3, 0.5},
        {"PID-Off",    40.0, 0.8,  0.95, 0.0,  0.0,   0.0,  false, false, 1.0, 1.0, 1.0},

        // Allow entries in all regimes but with multipliers
        {"AllRegime",  50.0, 0.8,  0.95, 0.1,  0.01,  0.02, true,  true,  0.7, 0.5, 0.7},

        // Higher multipliers - less sizing reduction
        {"HighMult",   50.0, 0.8,  0.95, 0.1,  0.01,  0.02, true,  true,  0.9, 0.8, 0.9},

        // Best combined (hypothesis)
        {"Balanced",   50.0, 0.85, 0.95, 0.15, 0.02,  0.05, true,  false, 0.8, 0.6, 0.8},
        {"Aggressive", 60.0, 0.9,  1.0,  0.1,  0.01,  0.02, true,  true,  0.9, 0.8, 0.9},
        {"MaxReturn",  70.0, 0.95, 1.0,  0.05, 0.005, 0.01, true,  true,  1.0, 1.0, 1.0},
    };

    std::vector<TestResult> results;

    std::cout << std::setw(12) << "Config"
              << std::setw(10) << "Return"
              << std::setw(10) << "MaxDD%"
              << std::setw(10) << "Sharpe"
              << std::setw(10) << "Trades"
              << std::setw(10) << "Status"
              << std::endl;
    std::cout << std::string(62, '-') << std::endl;

    for (const auto& tc : configs) {
        auto result = RunTest(tc, data_path, "2025.01.01", "2025.12.30", initial_balance);
        results.push_back(result);

        std::cout << std::setw(12) << result.name
                  << std::setw(10) << std::fixed << std::setprecision(2) << result.return_multiple << "x"
                  << std::setw(10) << std::setprecision(0) << result.max_dd_pct << "%"
                  << std::setw(10) << std::setprecision(2) << result.sharpe_approx
                  << std::setw(10) << result.total_trades
                  << std::setw(10) << (result.stopped_out ? "STOP" : "OK")
                  << std::endl;
    }

    // Sort by Sharpe-like ratio to find best risk-adjusted
    std::sort(results.begin(), results.end(), [](const TestResult& a, const TestResult& b) {
        if (a.stopped_out != b.stopped_out) return !a.stopped_out;
        return a.sharpe_approx > b.sharpe_approx;
    });

    std::cout << std::endl;
    std::cout << "=== Best Risk-Adjusted Configurations ===" << std::endl;
    std::cout << std::setw(12) << "Config"
              << std::setw(10) << "Return"
              << std::setw(10) << "MaxDD%"
              << std::setw(10) << "Sharpe"
              << std::endl;
    std::cout << std::string(42, '-') << std::endl;

    for (int i = 0; i < std::min(5, (int)results.size()); i++) {
        const auto& r = results[i];
        if (!r.stopped_out) {
            std::cout << std::setw(12) << r.name
                      << std::setw(10) << std::fixed << std::setprecision(2) << r.return_multiple << "x"
                      << std::setw(10) << std::setprecision(0) << r.max_dd_pct << "%"
                      << std::setw(10) << std::setprecision(2) << r.sharpe_approx
                      << std::endl;
        }
    }

    // Sort by raw return
    std::sort(results.begin(), results.end(), [](const TestResult& a, const TestResult& b) {
        if (a.stopped_out != b.stopped_out) return !a.stopped_out;
        return a.return_multiple > b.return_multiple;
    });

    std::cout << std::endl;
    std::cout << "=== Best Raw Return Configurations ===" << std::endl;
    for (int i = 0; i < std::min(5, (int)results.size()); i++) {
        const auto& r = results[i];
        if (!r.stopped_out) {
            std::cout << std::setw(12) << r.name
                      << std::setw(10) << std::fixed << std::setprecision(2) << r.return_multiple << "x"
                      << std::setw(10) << std::setprecision(0) << r.max_dd_pct << "%"
                      << std::setw(10) << std::setprecision(2) << r.sharpe_approx
                      << std::endl;
        }
    }

    return 0;
}
