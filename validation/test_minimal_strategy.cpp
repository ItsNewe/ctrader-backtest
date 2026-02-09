/**
 * Test: How simple can we go?
 *
 * Hypothesis: If simple beats smart, the SIMPLEST strategy might be best.
 *
 * Strategies tested (simplest to most complex):
 * 1. FIXED_SPACING: Just fixed $ spacing, no adaptation (2 params: survive, spacing)
 * 2. PCT_SPACING: Fixed % spacing, scales with price (2 params)
 * 3. ADAPTIVE: Current V4 with volatility adaptation (6+ params)
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>

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

// Strategy 1: BASELINE mode - fixed absolute spacing, no adaptation
Result TestFixed(const std::string& year, const std::string& data_file, double spacing) {
    Result r;
    r.name = "FIXED_$" + std::to_string((int)spacing);
    r.year = year;

    try {
        auto config = GetGoldConfig(year, data_file);
        TickBasedEngine engine(config);

        // BASELINE mode = fixed spacing, no adaptation
        FillUpOscillation strategy(
            13.0,   // survive_pct
            spacing, // fixed spacing in $
            0.01, 10.0, 100.0, 500.0,
            FillUpOscillation::BASELINE,  // No adaptation
            0.0, 0.0, 4.0
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

// Strategy 2: Fixed percentage spacing (scales with price automatically)
Result TestFixedPct(const std::string& year, const std::string& data_file, double spacing_pct) {
    Result r;
    r.name = "FIXED_" + std::to_string(spacing_pct).substr(0,4) + "%";
    r.year = year;

    try {
        auto config = GetGoldConfig(year, data_file);
        TickBasedEngine engine(config);

        FillUpOscillation::AdaptiveConfig adaptive_cfg;
        adaptive_cfg.pct_spacing = true;
        adaptive_cfg.typical_vol_pct = 0.55;  // Won't matter much for BASELINE
        adaptive_cfg.min_spacing_mult = 1.0;  // No adaptation
        adaptive_cfg.max_spacing_mult = 1.0;  // No adaptation

        // BASELINE with pct_spacing = fixed % spacing
        FillUpOscillation strategy(
            13.0,
            spacing_pct,  // percentage
            0.01, 10.0, 100.0, 500.0,
            FillUpOscillation::BASELINE,
            0.0, 0.0, 4.0,
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

// Strategy 3: ADAPTIVE (current V4)
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
            13.0,
            1.5,  // base spacing
            0.01, 10.0, 100.0, 500.0,
            FillUpOscillation::ADAPTIVE_SPACING,
            0.0, 0.0, 4.0,
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
    std::cout << "SIMPLICITY TEST: How simple can we go?\n";
    std::cout << "====================================================================\n\n";

    std::string data_2024 = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAUUSD_TICKS_2024.csv";
    std::string data_2025 = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";

    std::vector<Result> results;

    // Test fixed $ spacing at different levels
    std::cout << "Testing FIXED $ spacing...\n";
    for (double sp : {1.0, 1.5, 2.0, 3.0}) {
        std::cout << "  $" << sp << " spacing: ";
        auto r24 = TestFixed("2024", data_2024, sp);
        auto r25 = TestFixed("2025", data_2025, sp);
        std::cout << "2024=" << r24.return_mult << "x, 2025=" << r25.return_mult << "x\n";
        results.push_back(r24);
        results.push_back(r25);
    }

    // Test fixed % spacing
    std::cout << "\nTesting FIXED % spacing...\n";
    for (double sp : {0.04, 0.05, 0.06, 0.08}) {
        std::cout << "  " << sp << "% spacing: ";
        auto r24 = TestFixedPct("2024", data_2024, sp);
        auto r25 = TestFixedPct("2025", data_2025, sp);
        std::cout << "2024=" << r24.return_mult << "x, 2025=" << r25.return_mult << "x\n";
        results.push_back(r24);
        results.push_back(r25);
    }

    // Test ADAPTIVE
    std::cout << "\nTesting ADAPTIVE (current V4)...\n";
    auto a24 = TestAdaptive("2024", data_2024);
    auto a25 = TestAdaptive("2025", data_2025);
    std::cout << "  2024=" << a24.return_mult << "x DD=" << a24.max_dd << "%\n";
    std::cout << "  2025=" << a25.return_mult << "x DD=" << a25.max_dd << "%\n";
    results.push_back(a24);
    results.push_back(a25);

    // Summary
    std::cout << "\n====================================================================\n";
    std::cout << "SUMMARY - Simplicity vs Complexity\n";
    std::cout << "====================================================================\n\n";

    std::cout << std::left << std::setw(20) << "Strategy"
              << std::setw(10) << "2024"
              << std::setw(10) << "2025"
              << std::setw(10) << "Ratio"
              << std::setw(12) << "2-Year"
              << "Params\n";
    std::cout << std::string(72, '-') << "\n";

    // Print results in pairs
    for (size_t i = 0; i < results.size(); i += 2) {
        auto& r24 = results[i];
        auto& r25 = results[i+1];

        double ratio = (r24.return_mult > 0) ? r25.return_mult / r24.return_mult : 0;
        double seq = r24.return_mult * r25.return_mult;

        int params = 2;  // Default for fixed
        if (r24.name == "ADAPTIVE") params = 6;

        std::cout << std::setw(20) << r24.name
                  << std::setw(10) << (std::to_string(r24.return_mult).substr(0,5) + "x")
                  << std::setw(10) << (std::to_string(r25.return_mult).substr(0,5) + "x")
                  << std::setw(10) << (std::to_string(ratio).substr(0,4) + "x")
                  << std::setw(12) << (std::to_string(seq).substr(0,6) + "x")
                  << params << "\n";
    }

    std::cout << "\n====================================================================\n";
    std::cout << "QUESTION: Does simpler = better? Or is there an optimal complexity?\n";
    std::cout << "====================================================================\n";

    return 0;
}
