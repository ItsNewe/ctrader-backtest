/**
 * Compare V4, V5, and CombinedJu strategies on Silver (XAGUSD)
 * Multi-year validation: 2024 + 2025
 * All using percentage-based spacing
 */

#include "../include/fill_up_oscillation.h"
#include "../include/strategy_combined_ju.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <functional>

using namespace backtest;

struct Result {
    std::string strategy;
    std::string year;
    double return_mult;
    double max_dd_pct;
    int trades;
    double swap;
    std::string status;
};

TickBacktestConfig GetSilverConfig(const std::string& year, const std::string& data_file) {
    TickDataConfig tick_config;
    tick_config.file_path = data_file;
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickBacktestConfig config;
    config.symbol = "XAGUSD";
    config.initial_balance = 10000.0;
    config.account_currency = "USD";
    config.contract_size = 5000.0;
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
    return config;
}

// V4: FillUpOscillation ADAPTIVE_SPACING with pct_spacing
Result TestV4(const std::string& year, const std::string& data_file) {
    Result r;
    r.strategy = "V4_ADAPTIVE";
    r.year = year;

    try {
        auto config = GetSilverConfig(year, data_file);
        TickBasedEngine engine(config);

        FillUpOscillation::AdaptiveConfig adaptive_cfg;
        adaptive_cfg.pct_spacing = true;
        adaptive_cfg.typical_vol_pct = 0.45;  // 1h median for silver
        adaptive_cfg.min_spacing_mult = 0.5;
        adaptive_cfg.max_spacing_mult = 3.0;

        // Best config from multi-year: s19% sp2.0% lb1
        FillUpOscillation strategy(
            19.0,   // survive_pct
            2.0,    // base_spacing (now %)
            0.01, 10.0, 5000.0, 500.0,
            FillUpOscillation::ADAPTIVE_SPACING,
            0.0, 0.0, 1.0,  // 1h lookback
            adaptive_cfg
        );

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto res = engine.GetResults();
        r.return_mult = res.final_balance / 10000.0;
        r.max_dd_pct = res.max_drawdown_pct;
        r.trades = res.total_trades;
        r.swap = res.total_swap_charged;
        r.status = res.stop_out_occurred ? "STOP-OUT" : "OK";
    } catch (const std::exception& e) {
        r.return_mult = 0;
        r.max_dd_pct = 100;
        r.trades = 0;
        r.swap = 0;
        r.status = std::string("ERROR: ") + e.what();
    }
    return r;
}

// V5: ADAPTIVE_SPACING with SMA trend filter (VELOCITY_FILTER mode approximates this)
Result TestV5(const std::string& year, const std::string& data_file) {
    Result r;
    r.strategy = "V5_VELOCITY";
    r.year = year;

    try {
        auto config = GetSilverConfig(year, data_file);
        TickBasedEngine engine(config);

        FillUpOscillation::AdaptiveConfig adaptive_cfg;
        adaptive_cfg.pct_spacing = true;
        adaptive_cfg.typical_vol_pct = 0.45;
        adaptive_cfg.min_spacing_mult = 0.5;
        adaptive_cfg.max_spacing_mult = 3.0;

        // V5 uses velocity filter to avoid entries during fast moves
        FillUpOscillation strategy(
            19.0,   // survive_pct
            2.0,    // base_spacing (%)
            0.01, 10.0, 5000.0, 500.0,
            FillUpOscillation::VELOCITY_FILTER,  // V5-like behavior
            0.0,
            0.5,    // velocity_threshold (% per tick window)
            1.0,    // 1h lookback
            adaptive_cfg
        );

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto res = engine.GetResults();
        r.return_mult = res.final_balance / 10000.0;
        r.max_dd_pct = res.max_drawdown_pct;
        r.trades = res.total_trades;
        r.swap = res.total_swap_charged;
        r.status = res.stop_out_occurred ? "STOP-OUT" : "OK";
    } catch (const std::exception& e) {
        r.return_mult = 0;
        r.max_dd_pct = 100;
        r.trades = 0;
        r.swap = 0;
        r.status = std::string("ERROR: ") + e.what();
    }
    return r;
}

// CombinedJu: Rubber Band TP + Velocity Filter + Threshold Barbell
Result TestCombinedJu(const std::string& year, const std::string& data_file) {
    Result r;
    r.strategy = "COMBINED_JU";
    r.year = year;

    try {
        auto config = GetSilverConfig(year, data_file);
        TickBasedEngine engine(config);

        StrategyCombinedJu::Config ju_cfg;
        ju_cfg.survive_pct = 19.0;
        ju_cfg.base_spacing = 2.0;  // Will be treated as % with pct_spacing
        ju_cfg.min_volume = 0.01;
        ju_cfg.max_volume = 10.0;
        ju_cfg.contract_size = 5000.0;
        ju_cfg.leverage = 500.0;

        // Rubber Band TP
        ju_cfg.tp_mode = StrategyCombinedJu::TPMode::LINEAR;
        ju_cfg.tp_linear_scale = 0.3;
        ju_cfg.tp_min = 2.0;  // % based

        // Velocity filter
        ju_cfg.enable_velocity_filter = true;
        ju_cfg.velocity_window = 10;
        ju_cfg.velocity_threshold_pct = 0.01;

        // Threshold sizing (UNIFORM is safer)
        ju_cfg.sizing_mode = StrategyCombinedJu::SizingMode::UNIFORM;

        // Volatility adaptive
        ju_cfg.volatility_lookback_hours = 1.0;
        ju_cfg.typical_vol_pct = 0.45;

        // Enable percentage-based spacing
        ju_cfg.pct_spacing = true;

        StrategyCombinedJu strategy(ju_cfg);

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto res = engine.GetResults();
        r.return_mult = res.final_balance / 10000.0;
        r.max_dd_pct = res.max_drawdown_pct;
        r.trades = res.total_trades;
        r.swap = res.total_swap_charged;
        r.status = res.stop_out_occurred ? "STOP-OUT" : "OK";
    } catch (const std::exception& e) {
        r.return_mult = 0;
        r.max_dd_pct = 100;
        r.trades = 0;
        r.swap = 0;
        r.status = std::string("ERROR: ") + e.what();
    }
    return r;
}

int main() {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "====================================================================\n";
    std::cout << "SILVER (XAGUSD) STRATEGY COMPARISON\n";
    std::cout << "V4 (ADAPTIVE) vs V5 (VELOCITY) vs CombinedJu\n";
    std::cout << "====================================================================\n\n";

    std::string data_2024 = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAGUSD_TICKS_2024.csv";
    std::string data_2025 = "C:\\Users\\user\\Documents\\ctrader-backtest\\mt5\\fill_up_xagusd\\XAGUSD_TESTER_TICKS.csv";

    std::vector<Result> results;

    // Test V4
    std::cout << "Testing V4 (ADAPTIVE_SPACING)...\n";
    std::cout << "  2024: "; std::cout.flush();
    auto v4_2024 = TestV4("2024", data_2024);
    std::cout << v4_2024.return_mult << "x DD=" << v4_2024.max_dd_pct << "% [" << v4_2024.status << "]\n";
    results.push_back(v4_2024);

    std::cout << "  2025: "; std::cout.flush();
    auto v4_2025 = TestV4("2025", data_2025);
    std::cout << v4_2025.return_mult << "x DD=" << v4_2025.max_dd_pct << "% [" << v4_2025.status << "]\n";
    results.push_back(v4_2025);

    // Test V5
    std::cout << "\nTesting V5 (VELOCITY_FILTER)...\n";
    std::cout << "  2024: "; std::cout.flush();
    auto v5_2024 = TestV5("2024", data_2024);
    std::cout << v5_2024.return_mult << "x DD=" << v5_2024.max_dd_pct << "% [" << v5_2024.status << "]\n";
    results.push_back(v5_2024);

    std::cout << "  2025: "; std::cout.flush();
    auto v5_2025 = TestV5("2025", data_2025);
    std::cout << v5_2025.return_mult << "x DD=" << v5_2025.max_dd_pct << "% [" << v5_2025.status << "]\n";
    results.push_back(v5_2025);

    // Test CombinedJu
    std::cout << "\nTesting CombinedJu (Rubber Band + Velocity + Uniform)...\n";
    std::cout << "  2024: "; std::cout.flush();
    auto ju_2024 = TestCombinedJu("2024", data_2024);
    std::cout << ju_2024.return_mult << "x DD=" << ju_2024.max_dd_pct << "% [" << ju_2024.status << "]\n";
    results.push_back(ju_2024);

    std::cout << "  2025: "; std::cout.flush();
    auto ju_2025 = TestCombinedJu("2025", data_2025);
    std::cout << ju_2025.return_mult << "x DD=" << ju_2025.max_dd_pct << "% [" << ju_2025.status << "]\n";
    results.push_back(ju_2025);

    // Summary
    std::cout << "\n====================================================================\n";
    std::cout << "SUMMARY\n";
    std::cout << "====================================================================\n\n";

    std::cout << std::left
              << std::setw(18) << "Strategy"
              << std::setw(12) << "2024"
              << std::setw(12) << "2025"
              << std::setw(10) << "Ratio"
              << std::setw(12) << "2-Year"
              << std::setw(12) << "Avg DD"
              << "\n";
    std::cout << std::string(76, '-') << "\n";

    // V4
    if (v4_2024.status == "OK" && v4_2025.status == "OK") {
        double ratio = v4_2025.return_mult / v4_2024.return_mult;
        double seq = v4_2024.return_mult * v4_2025.return_mult;
        double avg_dd = (v4_2024.max_dd_pct + v4_2025.max_dd_pct) / 2;
        std::cout << std::setw(18) << "V4_ADAPTIVE"
                  << std::setw(12) << (std::to_string(v4_2024.return_mult).substr(0,5) + "x")
                  << std::setw(12) << (std::to_string(v4_2025.return_mult).substr(0,5) + "x")
                  << std::setw(10) << (std::to_string(ratio).substr(0,4) + "x")
                  << std::setw(12) << (std::to_string(seq).substr(0,6) + "x")
                  << std::setw(12) << (std::to_string(avg_dd).substr(0,5) + "%")
                  << "\n";
    }

    // V5
    if (v5_2024.status == "OK" && v5_2025.status == "OK") {
        double ratio = v5_2025.return_mult / v5_2024.return_mult;
        double seq = v5_2024.return_mult * v5_2025.return_mult;
        double avg_dd = (v5_2024.max_dd_pct + v5_2025.max_dd_pct) / 2;
        std::cout << std::setw(18) << "V5_VELOCITY"
                  << std::setw(12) << (std::to_string(v5_2024.return_mult).substr(0,5) + "x")
                  << std::setw(12) << (std::to_string(v5_2025.return_mult).substr(0,5) + "x")
                  << std::setw(10) << (std::to_string(ratio).substr(0,4) + "x")
                  << std::setw(12) << (std::to_string(seq).substr(0,6) + "x")
                  << std::setw(12) << (std::to_string(avg_dd).substr(0,5) + "%")
                  << "\n";
    }

    // CombinedJu
    if (ju_2024.status == "OK" && ju_2025.status == "OK") {
        double ratio = ju_2025.return_mult / ju_2024.return_mult;
        double seq = ju_2024.return_mult * ju_2025.return_mult;
        double avg_dd = (ju_2024.max_dd_pct + ju_2025.max_dd_pct) / 2;
        std::cout << std::setw(18) << "COMBINED_JU"
                  << std::setw(12) << (std::to_string(ju_2024.return_mult).substr(0,5) + "x")
                  << std::setw(12) << (std::to_string(ju_2025.return_mult).substr(0,5) + "x")
                  << std::setw(10) << (std::to_string(ratio).substr(0,4) + "x")
                  << std::setw(12) << (std::to_string(seq).substr(0,6) + "x")
                  << std::setw(12) << (std::to_string(avg_dd).substr(0,5) + "%")
                  << "\n";
    }

    std::cout << "\n====================================================================\n";
    std::cout << "COMPARISON WITH GOLD (XAUUSD)\n";
    std::cout << "====================================================================\n";
    std::cout << "Gold V4:        ~2.4x (2024), ~8.1x (2025), ratio 3.4x\n";
    std::cout << "Gold CombinedJu: ~5.7x (2024), ~22x (2025), ratio 3.8x\n";

    return 0;
}
