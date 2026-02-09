/**
 * Test: Can FIXED spacing work across regimes?
 *
 * Hypothesis: The "right" FIXED parameter should work in both 2024 and 2025.
 * Also testing: Does FIXED % (percentage of price) beat FIXED $ (absolute)?
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>

using namespace backtest;

struct Result {
    std::string name;
    std::string year;
    double return_mult;
    double max_dd;
    int trades;
    std::string status;
};

TickBacktestConfig GetGoldConfig(const std::string& year, const std::string& data_file) {
    TickDataConfig tick_config;
    tick_config.file_path = data_file;
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = 10000.0;
    config.account_currency = "USD";
    config.contract_size = 100.0;
    config.leverage = 500.0;
    config.margin_rate = 1.0;
    config.pip_size = 0.01;
    config.swap_long = -66.99;
    config.swap_short = 41.2;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = year + ".01.01";
    config.end_date = year + ".12.31";
    config.tick_data_config = tick_config;
    return config;
}

// Test FIXED absolute spacing ($ value)
Result TestFixedAbsolute(const std::string& year, const std::string& data_file, double spacing) {
    Result r;
    r.name = "FIXED_$" + std::to_string((int)(spacing*10)/10.0).substr(0,4);
    r.year = year;

    try {
        auto config = GetGoldConfig(year, data_file);
        TickBasedEngine engine(config);

        FillUpOscillation strategy(
            13.0, spacing, 0.01, 10.0, 100.0, 500.0,
            FillUpOscillation::BASELINE, 0.0, 0.0, 4.0
        );

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto res = engine.GetResults();
        r.return_mult = res.final_balance / 10000.0;
        r.max_dd = res.max_drawdown_pct;
        r.trades = res.total_trades;
        r.status = res.stop_out_occurred ? "STOP-OUT" : "OK";
    } catch (const std::exception& e) {
        r.return_mult = 0; r.max_dd = 100; r.trades = 0;
        r.status = std::string("ERROR: ") + e.what();
    }
    return r;
}

// Test FIXED percentage spacing (% of price)
Result TestFixedPercentage(const std::string& year, const std::string& data_file, double pct_spacing) {
    Result r;
    r.name = "FIXED_" + std::to_string(pct_spacing).substr(0,5) + "%";
    r.year = year;

    try {
        auto config = GetGoldConfig(year, data_file);
        TickBasedEngine engine(config);

        FillUpOscillation::AdaptiveConfig adaptive_cfg;
        adaptive_cfg.pct_spacing = true;
        adaptive_cfg.min_spacing_mult = 1.0;  // No adaptation
        adaptive_cfg.max_spacing_mult = 1.0;  // No adaptation
        adaptive_cfg.typical_vol_pct = 0.55;  // Won't matter with mult=1.0

        FillUpOscillation strategy(
            13.0, pct_spacing, 0.01, 10.0, 100.0, 500.0,
            FillUpOscillation::BASELINE, 0.0, 0.0, 4.0,
            adaptive_cfg
        );

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto res = engine.GetResults();
        r.return_mult = res.final_balance / 10000.0;
        r.max_dd = res.max_drawdown_pct;
        r.trades = res.total_trades;
        r.status = res.stop_out_occurred ? "STOP-OUT" : "OK";
    } catch (const std::exception& e) {
        r.return_mult = 0; r.max_dd = 100; r.trades = 0;
        r.status = std::string("ERROR: ") + e.what();
    }
    return r;
}

// Test ADAPTIVE (baseline for comparison)
Result TestAdaptive(const std::string& year, const std::string& data_file) {
    Result r;
    r.name = "ADAPTIVE";
    r.year = year;

    try {
        auto config = GetGoldConfig(year, data_file);
        TickBasedEngine engine(config);

        FillUpOscillation::AdaptiveConfig adaptive_cfg;
        adaptive_cfg.pct_spacing = false;
        adaptive_cfg.typical_vol_pct = 0.55;
        adaptive_cfg.min_spacing_mult = 0.5;
        adaptive_cfg.max_spacing_mult = 3.0;

        FillUpOscillation strategy(
            13.0, 1.5, 0.01, 10.0, 100.0, 500.0,
            FillUpOscillation::ADAPTIVE_SPACING, 0.0, 0.0, 4.0,
            adaptive_cfg
        );

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto res = engine.GetResults();
        r.return_mult = res.final_balance / 10000.0;
        r.max_dd = res.max_drawdown_pct;
        r.trades = res.total_trades;
        r.status = res.stop_out_occurred ? "STOP-OUT" : "OK";
    } catch (const std::exception& e) {
        r.return_mult = 0; r.max_dd = 100; r.trades = 0;
        r.status = std::string("ERROR: ") + e.what();
    }
    return r;
}

int main() {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "====================================================================\n";
    std::cout << "REGIME ROBUSTNESS: Can FIXED spacing work across years?\n";
    std::cout << "====================================================================\n\n";

    std::string data_2024 = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAUUSD_TICKS_2024.csv";
    std::string data_2025 = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";

    std::vector<std::pair<Result, Result>> paired_results;

    // Test FIXED absolute spacings
    std::cout << "Testing FIXED absolute spacings ($)...\n";
    for (double sp : {1.5, 2.0, 2.5, 3.0}) {
        std::cout << "  $" << sp << ": ";
        auto r24 = TestFixedAbsolute("2024", data_2024, sp);
        auto r25 = TestFixedAbsolute("2025", data_2025, sp);
        std::cout << "2024=" << r24.return_mult << "x (" << r24.status << "), "
                  << "2025=" << r25.return_mult << "x (" << r25.status << ")\n";
        paired_results.push_back({r24, r25});
    }

    // Test FIXED percentage spacings
    std::cout << "\nTesting FIXED percentage spacings (%)...\n";
    for (double pct : {0.05, 0.06, 0.08, 0.10}) {
        std::cout << "  " << pct << "%: ";
        auto r24 = TestFixedPercentage("2024", data_2024, pct);
        auto r25 = TestFixedPercentage("2025", data_2025, pct);
        std::cout << "2024=" << r24.return_mult << "x (" << r24.status << "), "
                  << "2025=" << r25.return_mult << "x (" << r25.status << ")\n";
        paired_results.push_back({r24, r25});
    }

    // Test ADAPTIVE baseline
    std::cout << "\nTesting ADAPTIVE (baseline)...\n";
    auto a24 = TestAdaptive("2024", data_2024);
    auto a25 = TestAdaptive("2025", data_2025);
    std::cout << "  ADAPTIVE: 2024=" << a24.return_mult << "x, 2025=" << a25.return_mult << "x\n";
    paired_results.push_back({a24, a25});

    // Summary table
    std::cout << "\n====================================================================\n";
    std::cout << "SUMMARY: Regime Robustness Comparison\n";
    std::cout << "====================================================================\n\n";

    std::cout << std::left << std::setw(16) << "Strategy"
              << std::setw(10) << "2024"
              << std::setw(10) << "2025"
              << std::setw(10) << "Ratio"
              << std::setw(12) << "2-Year Seq"
              << std::setw(10) << "Avg DD"
              << "Status\n";
    std::cout << std::string(78, '-') << "\n";

    for (const auto& [r24, r25] : paired_results) {
        double ratio = (r24.return_mult > 0.01) ? r25.return_mult / r24.return_mult : 999.0;
        double seq = r24.return_mult * r25.return_mult;
        double avg_dd = (r24.max_dd + r25.max_dd) / 2.0;
        std::string status = (r24.status == "OK" && r25.status == "OK") ? "BOTH_OK" : "FAIL";

        std::cout << std::setw(16) << r24.name
                  << std::setw(10) << (std::to_string(r24.return_mult).substr(0,5) + "x")
                  << std::setw(10) << (std::to_string(r25.return_mult).substr(0,5) + "x")
                  << std::setw(10) << (ratio < 100 ? std::to_string(ratio).substr(0,4) + "x" : "INF")
                  << std::setw(12) << (std::to_string(seq).substr(0,6) + "x")
                  << std::setw(10) << (std::to_string(avg_dd).substr(0,5) + "%")
                  << status << "\n";
    }

    // Analysis
    std::cout << "\n====================================================================\n";
    std::cout << "ANALYSIS\n";
    std::cout << "====================================================================\n";

    // Find best configs
    double best_seq = 0;
    std::string best_name;
    double best_adaptive_seq = a24.return_mult * a25.return_mult;

    for (const auto& [r24, r25] : paired_results) {
        if (r24.status == "OK" && r25.status == "OK") {
            double seq = r24.return_mult * r25.return_mult;
            if (seq > best_seq) {
                best_seq = seq;
                best_name = r24.name;
            }
        }
    }

    std::cout << "\nBest 2-year return: " << best_name << " (" << best_seq << "x)\n";
    std::cout << "ADAPTIVE baseline:  " << best_adaptive_seq << "x\n";

    if (best_seq >= best_adaptive_seq * 0.9) {
        std::cout << "\n=> SIMPLE CAN MATCH COMPLEX if you choose the right parameter!\n";
    } else {
        std::cout << "\n=> ADAPTIVE's complexity is justified (>10% better than best FIXED)\n";
    }

    // Regime stability analysis
    std::cout << "\nRegime Stability (lower ratio = more stable):\n";
    for (const auto& [r24, r25] : paired_results) {
        if (r24.status == "OK" && r25.status == "OK" && r24.return_mult > 0.1) {
            double ratio = r25.return_mult / r24.return_mult;
            std::cout << "  " << std::setw(15) << r24.name << ": " << ratio << "x\n";
        }
    }

    return 0;
}
