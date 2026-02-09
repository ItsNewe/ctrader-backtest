/**
 * Multi-year validation for Silver (XAGUSD)
 * Tests best configs from 2025 sweep on 2024 data
 * Uses percentage-based spacing (pct_spacing mode)
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
    double swap_total;
    std::string status;
};

TestResult RunTest(const std::string& year, const std::string& data_file, const TestConfig& cfg) {
    TickDataConfig tick_config;
    tick_config.file_path = data_file;
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickBacktestConfig config;
    config.symbol = "XAGUSD";
    config.initial_balance = 10000.0;
    config.account_currency = "USD";
    config.contract_size = 5000.0;  // Silver: 5000 oz per lot
    config.leverage = 500.0;
    config.margin_rate = 1.0;
    config.pip_size = 0.001;
    config.swap_long = -15.0;
    config.swap_short = 13.72;
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

        // Use percentage-based spacing (critical for silver)
        FillUpOscillation::AdaptiveConfig adaptive_cfg;
        adaptive_cfg.pct_spacing = true;
        adaptive_cfg.typical_vol_pct = cfg.typical_vol_pct;
        adaptive_cfg.min_spacing_mult = 0.5;
        adaptive_cfg.max_spacing_mult = 3.0;
        adaptive_cfg.min_spacing_abs = 0.01;   // 0.01% min
        adaptive_cfg.max_spacing_abs = 10.0;   // 10% max
        adaptive_cfg.spacing_change_threshold = 0.01;

        FillUpOscillation strategy(
            cfg.survive_pct,
            cfg.spacing_pct,  // Now percentage-based
            0.01, 10.0, 5000.0, 500.0,
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
        result.swap_total = results.total_swap_charged;
        result.status = results.stop_out_occurred ? "STOP-OUT" : "OK";

    } catch (const std::exception& e) {
        result.return_mult = 0;
        result.max_dd_pct = 100.0;
        result.trade_count = 0;
        result.swap_total = 0;
        result.status = std::string("ERROR: ") + e.what();
    }

    return result;
}

int main() {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "====================================================================\n";
    std::cout << "SILVER (XAGUSD) MULTI-YEAR VALIDATION\n";
    std::cout << "Using percentage-based spacing (pct_spacing mode)\n";
    std::cout << "====================================================================\n\n";

    // Best configs from CLAUDE.md and prior testing
    // survive%, spacing%, lookback, typical_vol%
    std::vector<TestConfig> configs = {
        {19.0, 2.0, 1.0, 0.45},   // Best from CLAUDE.md: 43.4x, 29.3% DD
        {18.0, 2.0, 1.0, 0.45},   // More aggressive
        {20.0, 2.0, 1.0, 0.45},   // More conservative
        {19.0, 1.5, 1.0, 0.45},   // Tighter spacing
        {19.0, 3.0, 1.0, 0.45},   // Wider spacing
        {19.0, 2.0, 4.0, 0.97},   // 4h lookback with appropriate typvol
    };

    std::string data_2024 = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAGUSD_TICKS_2024.csv";
    std::string data_2025 = "C:\\Users\\user\\Documents\\ctrader-backtest\\mt5\\fill_up_xagusd\\XAGUSD_TESTER_TICKS.csv";

    std::vector<TestResult> all_results;

    for (const auto& cfg : configs) {
        std::cout << "Testing s" << cfg.survive_pct << "% sp" << cfg.spacing_pct
                  << "% lb" << cfg.lookback_hours << "h tv" << cfg.typical_vol_pct << "%...\n";

        // Test 2024
        std::cout << "  2024: ";
        std::cout.flush();
        auto r2024 = RunTest("2024", data_2024, cfg);
        std::cout << r2024.return_mult << "x DD=" << r2024.max_dd_pct << "% trades="
                  << r2024.trade_count << " [" << r2024.status << "]\n";
        all_results.push_back(r2024);

        // Test 2025
        std::cout << "  2025: ";
        std::cout.flush();
        auto r2025 = RunTest("2025", data_2025, cfg);
        std::cout << r2025.return_mult << "x DD=" << r2025.max_dd_pct << "% trades="
                  << r2025.trade_count << " [" << r2025.status << "]\n";
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
              << std::setw(28) << "Config"
              << std::setw(12) << "2024"
              << std::setw(12) << "2025"
              << std::setw(10) << "Ratio"
              << std::setw(14) << "2-Year"
              << "Assessment\n";
    std::cout << std::string(90, '-') << "\n";

    for (size_t i = 0; i < configs.size(); i++) {
        auto& cfg = configs[i];
        auto& r2024 = all_results[i * 2];
        auto& r2025 = all_results[i * 2 + 1];

        std::cout << "s" << std::setw(2) << (int)cfg.survive_pct
                  << " sp" << cfg.spacing_pct << "% lb" << (int)cfg.lookback_hours
                  << " tv" << cfg.typical_vol_pct << "  ";

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
            std::cout << std::setw(14) << (std::to_string(sequential).substr(0,7) + "x");

            if (ratio > 5.0) {
                std::cout << "HIGH regime dependence";
            } else if (ratio > 3.0) {
                std::cout << "MODERATE regime dependence";
            } else if (sequential > 100.0) {
                std::cout << "EXCELLENT";
            } else if (sequential > 50.0) {
                std::cout << "VERY GOOD";
            } else if (sequential > 20.0) {
                std::cout << "GOOD";
            } else {
                std::cout << "MODERATE";
            }
        } else {
            std::cout << std::setw(10) << "N/A";
            std::cout << std::setw(14) << "N/A";
            std::cout << "NOT VIABLE";
        }
        std::cout << "\n";
    }

    // Comparison with other instruments
    std::cout << "\n====================================================================\n";
    std::cout << "COMPARISON WITH OTHER INSTRUMENTS\n";
    std::cout << "====================================================================\n";
    std::cout << "XAUUSD (Gold):     ~6.6x (2025), ~2.1x (2024), Ratio: 3.1x\n";
    std::cout << "XPTUSD (Platinum): ~22.7x (2025), ~1.4x (2024), Ratio: 16.7x\n";
    std::cout << "XAGUSD (Silver):   See results above\n";

    return 0;
}
