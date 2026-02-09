/**
 * Test FillUpKapitza - Advanced Oscillation Control Strategy
 *
 * Compares the Kapitza strategy (with all 5 control mechanisms)
 * against the baseline FillUpOscillation strategy.
 */

#include "../include/fill_up_kapitza.h"
#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>

using namespace backtest;

struct TestResult {
    std::string name;
    double final_balance;
    double return_multiple;
    double max_dd;
    double total_swap;
    int total_trades;
    bool stopped_out;
};

TestResult RunKapitza(const std::string& data_path, const std::string& start_date,
                      const std::string& end_date, double initial_balance) {
    TestResult result;
    result.name = "KAPITZA";

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
        kconfig.resonance_threshold = 0.9;
        kconfig.trend_threshold = 0.6;
        kconfig.regime_lookback = 200;
        kconfig.pid_kp = 0.3;
        kconfig.pid_ki = 0.05;
        kconfig.pid_kd = 0.1;
        kconfig.target_dd_pct = 30.0;

        FillUpKapitza strategy(kconfig);

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto res = engine.GetResults();
        result.final_balance = res.final_balance;
        result.return_multiple = res.final_balance / initial_balance;
        result.max_dd = res.max_drawdown;
        result.total_swap = res.total_swap_charged;
        result.total_trades = res.total_trades;
        result.stopped_out = engine.IsStopOutOccurred();

    } catch (const std::exception& e) {
        std::cerr << "Kapitza error: " << e.what() << std::endl;
        result.final_balance = 0;
        result.return_multiple = 0;
        result.stopped_out = true;
    }

    return result;
}

TestResult RunOscillation(const std::string& data_path, const std::string& start_date,
                          const std::string& end_date, double initial_balance) {
    TestResult result;
    result.name = "OSCILLATION";

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
            13.0,   // survive_pct
            1.5,    // base_spacing
            0.01,   // min_volume
            10.0,   // max_volume
            100.0,  // contract_size
            500.0,  // leverage
            FillUpOscillation::Mode::ADAPTIVE_SPACING,
            0.1,    // antifragile_scale
            30.0,   // velocity_threshold
            1.0     // volatility_lookback_hours
        );

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto res = engine.GetResults();
        result.final_balance = res.final_balance;
        result.return_multiple = res.final_balance / initial_balance;
        result.max_dd = res.max_drawdown;
        result.total_swap = res.total_swap_charged;
        result.total_trades = res.total_trades;
        result.stopped_out = engine.IsStopOutOccurred();

    } catch (const std::exception& e) {
        std::cerr << "Oscillation error: " << e.what() << std::endl;
        result.final_balance = 0;
        result.return_multiple = 0;
        result.stopped_out = true;
    }

    return result;
}

void PrintResult(const TestResult& r, double initial_balance) {
    std::cout << std::setw(12) << r.name
              << std::setw(12) << std::fixed << std::setprecision(0) << r.final_balance
              << std::setw(10) << std::setprecision(2) << r.return_multiple << "x"
              << std::setw(12) << std::setprecision(0) << r.max_dd
              << std::setw(10) << (r.max_dd / initial_balance * 100) << "%"
              << std::setw(10) << r.total_swap
              << std::setw(10) << r.total_trades
              << std::setw(10) << (r.stopped_out ? "STOP" : "OK")
              << std::endl;
}

int main() {
    std::cout << "=== FillUpKapitza vs FillUpOscillation Comparison ===" << std::endl;
    std::cout << "Testing advanced control mechanisms" << std::endl;
    std::cout << std::endl;

    double initial_balance = 10000.0;
    std::string data_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";

    // Test Full Year 2025
    std::cout << "=== Full Year 2025 ===" << std::endl;
    std::cout << std::setw(12) << "Strategy"
              << std::setw(12) << "Final$"
              << std::setw(10) << "Return"
              << std::setw(12) << "MaxDD$"
              << std::setw(10) << "MaxDD%"
              << std::setw(10) << "Swap"
              << std::setw(10) << "Trades"
              << std::setw(10) << "Status"
              << std::endl;
    std::cout << std::string(96, '-') << std::endl;

    auto osc_result = RunOscillation(data_path, "2025.01.01", "2025.12.30", initial_balance);
    PrintResult(osc_result, initial_balance);

    auto kap_result = RunKapitza(data_path, "2025.01.01", "2025.12.30", initial_balance);
    PrintResult(kap_result, initial_balance);

    // Analysis
    std::cout << std::endl;
    std::cout << "=== Analysis ===" << std::endl;

    if (!osc_result.stopped_out && !kap_result.stopped_out) {
        double return_diff = (kap_result.return_multiple - osc_result.return_multiple) / osc_result.return_multiple * 100;
        double dd_diff = (kap_result.max_dd - osc_result.max_dd) / osc_result.max_dd * 100;
        double trade_diff = (kap_result.total_trades - osc_result.total_trades);

        std::cout << "Return difference: " << std::showpos << std::fixed << std::setprecision(1)
                  << return_diff << "%" << std::noshowpos << std::endl;
        std::cout << "Max DD difference: " << std::showpos << dd_diff << "%" << std::noshowpos << std::endl;
        std::cout << "Trade count difference: " << std::showpos << trade_diff << std::noshowpos << std::endl;

        if (kap_result.return_multiple > osc_result.return_multiple && kap_result.max_dd <= osc_result.max_dd) {
            std::cout << "\nKAPITZA WINS: Higher return with same or lower drawdown" << std::endl;
        } else if (kap_result.return_multiple >= osc_result.return_multiple * 0.9 &&
                   kap_result.max_dd < osc_result.max_dd * 0.8) {
            std::cout << "\nKAPITZA WINS: Similar return with significantly lower drawdown" << std::endl;
        } else if (osc_result.return_multiple > kap_result.return_multiple) {
            std::cout << "\nOSCILLATION WINS: Higher return" << std::endl;
            std::cout << "Kapitza may be too conservative with its control mechanisms" << std::endl;
        } else {
            std::cout << "\nRESULTS MIXED: Trade-offs between strategies" << std::endl;
        }
    }

    // Test H1 (first half of year)
    std::cout << std::endl;
    std::cout << "=== H1 2025 (Out-of-sample check) ===" << std::endl;
    std::cout << std::setw(12) << "Strategy"
              << std::setw(12) << "Final$"
              << std::setw(10) << "Return"
              << std::setw(12) << "MaxDD$"
              << std::setw(10) << "MaxDD%"
              << std::setw(10) << "Swap"
              << std::setw(10) << "Trades"
              << std::setw(10) << "Status"
              << std::endl;
    std::cout << std::string(96, '-') << std::endl;

    auto osc_h1 = RunOscillation(data_path, "2025.01.01", "2025.07.01", initial_balance);
    PrintResult(osc_h1, initial_balance);

    auto kap_h1 = RunKapitza(data_path, "2025.01.01", "2025.07.01", initial_balance);
    PrintResult(kap_h1, initial_balance);

    // Test H2 (second half of year)
    std::cout << std::endl;
    std::cout << "=== H2 2025 (Out-of-sample check) ===" << std::endl;
    std::cout << std::setw(12) << "Strategy"
              << std::setw(12) << "Final$"
              << std::setw(10) << "Return"
              << std::setw(12) << "MaxDD$"
              << std::setw(10) << "MaxDD%"
              << std::setw(10) << "Swap"
              << std::setw(10) << "Trades"
              << std::setw(10) << "Status"
              << std::endl;
    std::cout << std::string(96, '-') << std::endl;

    auto osc_h2 = RunOscillation(data_path, "2025.07.01", "2025.12.30", initial_balance);
    PrintResult(osc_h2, initial_balance);

    auto kap_h2 = RunKapitza(data_path, "2025.07.01", "2025.12.30", initial_balance);
    PrintResult(kap_h2, initial_balance);

    return 0;
}
