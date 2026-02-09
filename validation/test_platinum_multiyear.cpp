/**
 * Multi-year validation for Platinum (XPTUSD)
 * Tests best configs from 2025 sweep on 2024 data
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>

using namespace backtest;

struct TestConfig {
    double survive_pct;
    double spacing_pct;
    double lookback_hours;
    double typical_vol_pct;
};

struct TestResult {
    std::string year;
    TestConfig config;
    double return_mult;
    double max_dd_pct;
    int trade_count;
    std::string status;
};

TestResult RunTest(const std::string& year, const std::string& data_file, const TestConfig& cfg) {
    TickDataConfig tick_config;
    tick_config.file_path = data_file;
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickBacktestConfig config;
    config.symbol = "XPTUSD";
    config.initial_balance = 10000.0;
    config.account_currency = "USD";
    config.contract_size = 100.0;
    config.leverage = 500.0;
    config.margin_rate = 1.0;
    config.pip_size = 0.01;
    config.swap_long = -43.73;
    config.swap_short = 3.11;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = year + ".01.01";
    config.end_date = year + ".12.31";
    config.tick_data_config = tick_config;

    TestResult result;
    result.year = year;
    result.config = cfg;

    try {
        TickBasedEngine engine(config);

        FillUpOscillation::AdaptiveConfig adaptive_cfg;
        adaptive_cfg.pct_spacing = true;
        adaptive_cfg.typical_vol_pct = cfg.typical_vol_pct;
        adaptive_cfg.min_spacing_mult = 0.5;
        adaptive_cfg.max_spacing_mult = 3.0;
        adaptive_cfg.min_spacing_abs = 0.01;
        adaptive_cfg.max_spacing_abs = 10.0;
        adaptive_cfg.spacing_change_threshold = 0.01;

        FillUpOscillation strategy(
            cfg.survive_pct,
            cfg.spacing_pct,
            0.01, 10.0, 100.0, 500.0,
            FillUpOscillation::ADAPTIVE_SPACING,
            0.0, 0.0, cfg.lookback_hours,
            adaptive_cfg
        );

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& engine) {
            strategy.OnTick(tick, engine);
        });

        auto results = engine.GetResults();
        result.return_mult = results.final_balance / 10000.0;
        result.max_dd_pct = results.max_drawdown_pct;
        result.trade_count = results.total_trades;
        result.status = results.stop_out_occurred ? "STOP-OUT" : "OK";

    } catch (const std::exception& e) {
        result.return_mult = 0;
        result.max_dd_pct = 100.0;
        result.trade_count = 0;
        result.status = std::string("ERROR: ") + e.what();
    }

    return result;
}

int main() {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "====================================================================\n";
    std::cout << "PLATINUM (XPTUSD) MULTI-YEAR VALIDATION\n";
    std::cout << "====================================================================\n\n";

    // Best configs from 2025 sweep
    std::vector<TestConfig> configs = {
        {18.0, 1.5, 1.0, 1.73},   // Best return: 22.66x
        {18.0, 3.0, 1.0, 1.73},   // Good return, lower DD: 16.11x
        {20.0, 3.0, 1.0, 1.73},   // Best risk-adjusted: 8.95x, 71.5% DD
        {20.0, 2.0, 4.0, 3.46},   // Balanced: 11.27x
        {25.0, 2.0, 4.0, 3.46},   // Conservative: 4.67x, 51.5% DD
    };

    std::string data_2024 = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XPTUSD_TICKS_2024.csv";
    std::string data_2025 = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XPTUSD_TICKS_2025.csv";

    std::vector<TestResult> all_results;

    for (const auto& cfg : configs) {
        std::cout << "Testing s" << cfg.survive_pct << "% sp" << cfg.spacing_pct
                  << "% lb" << cfg.lookback_hours << "h...\n";

        // Test 2024
        std::cout << "  2024: ";
        auto r2024 = RunTest("2024", data_2024, cfg);
        std::cout << r2024.return_mult << "x DD=" << r2024.max_dd_pct << "% [" << r2024.status << "]\n";
        all_results.push_back(r2024);

        // Test 2025
        std::cout << "  2025: ";
        auto r2025 = RunTest("2025", data_2025, cfg);
        std::cout << r2025.return_mult << "x DD=" << r2025.max_dd_pct << "% [" << r2025.status << "]\n";
        all_results.push_back(r2025);

        // Calculate ratio
        if (r2024.status == "OK" && r2025.status == "OK") {
            double ratio = r2025.return_mult / r2024.return_mult;
            double sequential = r2024.return_mult * r2025.return_mult;
            std::cout << "  Ratio: " << ratio << "x | Sequential 2-year: " << sequential << "x\n";
        }
        std::cout << "\n";
    }

    // Summary table
    std::cout << "\n====================================================================\n";
    std::cout << "SUMMARY\n";
    std::cout << "====================================================================\n\n";

    std::cout << std::left
              << std::setw(22) << "Config"
              << std::setw(12) << "2024"
              << std::setw(12) << "2025"
              << std::setw(10) << "Ratio"
              << std::setw(12) << "2-Year"
              << "Assessment\n";
    std::cout << std::string(80, '-') << "\n";

    for (size_t i = 0; i < configs.size(); i++) {
        auto& cfg = configs[i];
        auto& r2024 = all_results[i * 2];
        auto& r2025 = all_results[i * 2 + 1];

        std::cout << "s" << std::setw(2) << (int)cfg.survive_pct
                  << " sp" << cfg.spacing_pct << "% lb" << (int)cfg.lookback_hours << "  ";

        if (r2024.status == "OK") {
            std::cout << std::setw(12) << (std::to_string(r2024.return_mult).substr(0,5) + "x");
        } else {
            std::cout << std::setw(12) << "STOP-OUT";
        }

        if (r2025.status == "OK") {
            std::cout << std::setw(12) << (std::to_string(r2025.return_mult).substr(0,5) + "x");
        } else {
            std::cout << std::setw(12) << "STOP-OUT";
        }

        if (r2024.status == "OK" && r2025.status == "OK") {
            double ratio = r2025.return_mult / r2024.return_mult;
            double sequential = r2024.return_mult * r2025.return_mult;
            std::cout << std::setw(10) << (std::to_string(ratio).substr(0,4) + "x");
            std::cout << std::setw(12) << (std::to_string(sequential).substr(0,6) + "x");

            if (ratio < 0.5 || ratio > 2.0) {
                std::cout << "HIGH regime dependence";
            } else if (sequential > 50.0) {
                std::cout << "EXCELLENT";
            } else if (sequential > 20.0) {
                std::cout << "GOOD";
            } else {
                std::cout << "MODERATE";
            }
        } else {
            std::cout << std::setw(10) << "N/A";
            std::cout << std::setw(12) << "N/A";
            std::cout << "NOT VIABLE";
        }
        std::cout << "\n";
    }

    return 0;
}
