#include "../include/fill_up_oscillation.h"
#include "../include/strategy_floating_attractor.h"
#include "../include/strategy_combined_ju.h"
#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <chrono>

using namespace backtest;

struct TestResult {
    std::string name;
    std::string year;
    double final_balance;
    double max_dd_pct;
    int trades;
    double swap;
    bool stopped_out;
};

// Run FillUpOscillation (v4 equivalent)
TestResult RunV4(const std::vector<Tick>& ticks, const std::string& year) {
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
    config.start_date = year == "2024" ? "2024.01.01" : "2025.01.01";
    config.end_date = year == "2024" ? "2024.12.30" : "2025.12.30";
    config.verbose = false;

    TickDataConfig tick_config;
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = true;
    config.tick_data_config = tick_config;

    TickBasedEngine engine(config);

    // V4 parameters: survive=13, spacing=1.5, adaptive spacing
    FillUpOscillation strategy(13.0, 1.5, 0.01, 10.0, 100.0, 500.0,
        FillUpOscillation::ADAPTIVE_SPACING, 0.0, 0.0, 4.0);

    engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();
    return {"FillUpAdaptive_v4", year, results.final_balance, results.max_drawdown_pct,
            results.total_trades, results.total_swap_charged, results.stop_out_occurred};
}

// Run FloatingAttractor (v5)
TestResult RunV5(const std::vector<Tick>& ticks, const std::string& year) {
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
    config.start_date = year == "2024" ? "2024.01.01" : "2025.01.01";
    config.end_date = year == "2024" ? "2024.12.30" : "2025.12.30";
    config.verbose = false;

    TickDataConfig tick_config;
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = true;
    config.tick_data_config = tick_config;

    TickBasedEngine engine(config);

    // V5 parameters: survive=12, EMA-200, TP mult 2.0, lookback 8h
    StrategyFloatingAttractor::Config strat_cfg;
    strat_cfg.survive_pct = 12.0;
    strat_cfg.base_spacing = 1.5;
    strat_cfg.attractor_period = 200;
    strat_cfg.attractor_type = StrategyFloatingAttractor::EMA;
    strat_cfg.tp_multiplier = 2.0;
    strat_cfg.adaptive_spacing = true;
    strat_cfg.typical_vol_pct = 0.55;
    strat_cfg.volatility_lookback_hours = 8.0;

    StrategyFloatingAttractor strategy(strat_cfg);

    engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();
    return {"FloatingAttractor_v5", year, results.final_balance, results.max_drawdown_pct,
            results.total_trades, results.total_swap_charged, results.stop_out_occurred};
}

// Run CombinedJu (THR config: pos=5, mult=2.0)
TestResult RunCombinedJuTHR(const std::vector<Tick>& ticks, const std::string& year) {
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
    config.start_date = year == "2024" ? "2024.01.01" : "2025.01.01";
    config.end_date = year == "2024" ? "2024.12.30" : "2025.12.30";
    config.verbose = false;

    TickDataConfig tick_config;
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = true;
    config.tick_data_config = tick_config;

    TickBasedEngine engine(config);

    StrategyCombinedJu::Config strat_cfg;
    strat_cfg.survive_pct = 13.0;
    strat_cfg.base_spacing = 1.5;
    strat_cfg.volatility_lookback_hours = 4.0;
    strat_cfg.typical_vol_pct = 0.55;

    // Rubber Band TP
    strat_cfg.tp_mode = StrategyCombinedJu::SQRT;
    strat_cfg.tp_sqrt_scale = 0.5;
    strat_cfg.tp_min = 1.5;

    // Velocity Filter
    strat_cfg.enable_velocity_filter = true;
    strat_cfg.velocity_threshold_pct = 0.01;

    // Threshold Barbell (THR: pos=5, mult=2.0)
    strat_cfg.sizing_mode = StrategyCombinedJu::THRESHOLD_SIZING;
    strat_cfg.sizing_threshold_pos = 5;
    strat_cfg.sizing_threshold_mult = 2.0;

    StrategyCombinedJu strategy(strat_cfg);

    engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();
    return {"CombinedJu_THR", year, results.final_balance, results.max_drawdown_pct,
            results.total_trades, results.total_swap_charged, results.stop_out_occurred};
}

// Run CombinedJu (THR3 config: pos=3, mult=2.0)
TestResult RunCombinedJuTHR3(const std::vector<Tick>& ticks, const std::string& year) {
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
    config.start_date = year == "2024" ? "2024.01.01" : "2025.01.01";
    config.end_date = year == "2024" ? "2024.12.30" : "2025.12.30";
    config.verbose = false;

    TickDataConfig tick_config;
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = true;
    config.tick_data_config = tick_config;

    TickBasedEngine engine(config);

    StrategyCombinedJu::Config strat_cfg;
    strat_cfg.survive_pct = 13.0;
    strat_cfg.base_spacing = 1.5;
    strat_cfg.volatility_lookback_hours = 4.0;
    strat_cfg.typical_vol_pct = 0.55;

    strat_cfg.tp_mode = StrategyCombinedJu::SQRT;
    strat_cfg.tp_sqrt_scale = 0.5;
    strat_cfg.tp_min = 1.5;

    strat_cfg.enable_velocity_filter = true;
    strat_cfg.velocity_threshold_pct = 0.01;

    // THR3: pos=3, mult=2.0
    strat_cfg.sizing_mode = StrategyCombinedJu::THRESHOLD_SIZING;
    strat_cfg.sizing_threshold_pos = 3;
    strat_cfg.sizing_threshold_mult = 2.0;

    StrategyCombinedJu strategy(strat_cfg);

    engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();
    return {"CombinedJu_THR3", year, results.final_balance, results.max_drawdown_pct,
            results.total_trades, results.total_swap_charged, results.stop_out_occurred};
}

// Run CombinedJu (P1_M3 config: pos=1, mult=3.0 - max return)
TestResult RunCombinedJuP1M3(const std::vector<Tick>& ticks, const std::string& year) {
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
    config.start_date = year == "2024" ? "2024.01.01" : "2025.01.01";
    config.end_date = year == "2024" ? "2024.12.30" : "2025.12.30";
    config.verbose = false;

    TickDataConfig tick_config;
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = true;
    config.tick_data_config = tick_config;

    TickBasedEngine engine(config);

    StrategyCombinedJu::Config strat_cfg;
    strat_cfg.survive_pct = 13.0;
    strat_cfg.base_spacing = 1.5;
    strat_cfg.volatility_lookback_hours = 4.0;
    strat_cfg.typical_vol_pct = 0.55;

    strat_cfg.tp_mode = StrategyCombinedJu::SQRT;
    strat_cfg.tp_sqrt_scale = 0.5;
    strat_cfg.tp_min = 1.5;

    strat_cfg.enable_velocity_filter = true;
    strat_cfg.velocity_threshold_pct = 0.01;

    // P1_M3: pos=1, mult=3.0
    strat_cfg.sizing_mode = StrategyCombinedJu::THRESHOLD_SIZING;
    strat_cfg.sizing_threshold_pos = 1;
    strat_cfg.sizing_threshold_mult = 3.0;

    StrategyCombinedJu strategy(strat_cfg);

    engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();
    return {"CombinedJu_P1_M3", year, results.final_balance, results.max_drawdown_pct,
            results.total_trades, results.total_swap_charged, results.stop_out_occurred};
}

int main() {
    std::cout << "=== EA COMPARISON: v4 vs v5 vs CombinedJu ===" << std::endl;

    // Load tick data
    std::cout << "\nLoading 2024 tick data..." << std::endl;
    std::vector<Tick> ticks_2024;
    {
        TickDataConfig tc;
        tc.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAUUSD_TICKS_2024.csv";
        tc.format = TickDataFormat::MT5_CSV;
        tc.load_all_into_memory = true;
        TickDataManager manager(tc);
        Tick tick;
        while (manager.GetNextTick(tick)) {
            ticks_2024.push_back(tick);
        }
    }
    std::cout << "Loaded " << ticks_2024.size() << " ticks (2024)" << std::endl;

    std::cout << "Loading 2025 tick data..." << std::endl;
    std::vector<Tick> ticks_2025;
    {
        TickDataConfig tc;
        tc.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
        tc.format = TickDataFormat::MT5_CSV;
        tc.load_all_into_memory = true;
        TickDataManager manager(tc);
        Tick tick;
        while (manager.GetNextTick(tick)) {
            ticks_2025.push_back(tick);
        }
    }
    std::cout << "Loaded " << ticks_2025.size() << " ticks (2025)" << std::endl;

    std::vector<TestResult> results;

    // Run all strategies on both years
    std::cout << "\nRunning v4 (FillUpOscillation)..." << std::endl;
    results.push_back(RunV4(ticks_2024, "2024"));
    results.push_back(RunV4(ticks_2025, "2025"));

    std::cout << "Running v5 (FloatingAttractor)..." << std::endl;
    results.push_back(RunV5(ticks_2024, "2024"));
    results.push_back(RunV5(ticks_2025, "2025"));

    std::cout << "Running CombinedJu_THR..." << std::endl;
    results.push_back(RunCombinedJuTHR(ticks_2024, "2024"));
    results.push_back(RunCombinedJuTHR(ticks_2025, "2025"));

    std::cout << "Running CombinedJu_THR3..." << std::endl;
    results.push_back(RunCombinedJuTHR3(ticks_2024, "2024"));
    results.push_back(RunCombinedJuTHR3(ticks_2025, "2025"));

    std::cout << "Running CombinedJu_P1_M3..." << std::endl;
    results.push_back(RunCombinedJuP1M3(ticks_2024, "2024"));
    results.push_back(RunCombinedJuP1M3(ticks_2025, "2025"));

    // Print results table
    std::cout << "\n" << std::string(95, '=') << std::endl;
    std::cout << "                         EA COMPARISON RESULTS" << std::endl;
    std::cout << std::string(95, '=') << std::endl;

    std::cout << std::left << std::setw(22) << "Strategy"
              << std::right << std::setw(6) << "Year"
              << std::setw(12) << "Return"
              << std::setw(10) << "DD%"
              << std::setw(10) << "Trades"
              << std::setw(12) << "Swap"
              << std::setw(8) << "Status" << std::endl;
    std::cout << std::string(95, '-') << std::endl;

    for (const auto& r : results) {
        std::string status = r.stopped_out ? "SO" : "OK";
        std::cout << std::left << std::setw(22) << r.name
                  << std::right << std::setw(6) << r.year
                  << std::setw(11) << std::fixed << std::setprecision(2)
                  << r.final_balance / 10000.0 << "x"
                  << std::setw(9) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(10) << r.trades
                  << std::setw(11) << std::setprecision(0) << "$" << (int)r.swap
                  << std::setw(8) << status << std::endl;
    }

    // Summary by strategy
    std::cout << "\n" << std::string(95, '=') << std::endl;
    std::cout << "                         SUMMARY BY STRATEGY" << std::endl;
    std::cout << std::string(95, '=') << std::endl;

    std::cout << std::left << std::setw(22) << "Strategy"
              << std::right << std::setw(10) << "2024"
              << std::setw(10) << "2025"
              << std::setw(10) << "Ratio"
              << std::setw(12) << "2-Year"
              << std::setw(10) << "Avg DD" << std::endl;
    std::cout << std::string(74, '-') << std::endl;

    // Group results by strategy
    std::vector<std::string> strategies = {"FillUpAdaptive_v4", "FloatingAttractor_v5",
                                           "CombinedJu_THR", "CombinedJu_THR3", "CombinedJu_P1_M3"};

    for (const auto& strat : strategies) {
        double ret_2024 = 0, ret_2025 = 0, dd_2024 = 0, dd_2025 = 0;
        bool so_2024 = false, so_2025 = false;

        for (const auto& r : results) {
            if (r.name == strat) {
                if (r.year == "2024") {
                    ret_2024 = r.final_balance / 10000.0;
                    dd_2024 = r.max_dd_pct;
                    so_2024 = r.stopped_out;
                } else {
                    ret_2025 = r.final_balance / 10000.0;
                    dd_2025 = r.max_dd_pct;
                    so_2025 = r.stopped_out;
                }
            }
        }

        double ratio = ret_2025 / ret_2024;
        double combined = ret_2024 * ret_2025;
        double avg_dd = (dd_2024 + dd_2025) / 2.0;

        std::string status = "";
        if (so_2024 || so_2025) status = " [SO]";

        std::cout << std::left << std::setw(22) << strat
                  << std::right << std::fixed
                  << std::setw(9) << std::setprecision(2) << ret_2024 << "x"
                  << std::setw(9) << std::setprecision(2) << ret_2025 << "x"
                  << std::setw(9) << std::setprecision(2) << ratio << "x"
                  << std::setw(11) << std::setprecision(1) << combined << "x"
                  << std::setw(9) << std::setprecision(1) << avg_dd << "%"
                  << status << std::endl;
    }

    std::cout << "\n=== KEY FINDINGS ===" << std::endl;
    std::cout << "- v4 (FillUpAdaptive): Baseline adaptive spacing strategy" << std::endl;
    std::cout << "- v5 (FloatingAttractor): EMA-based grid, may stop out with survive=12%" << std::endl;
    std::cout << "- CombinedJu: Rubber Band TP + Velocity Filter + Barbell Sizing" << std::endl;
    std::cout << "- Lower ratio = more regime-stable (less dependent on market conditions)" << std::endl;

    return 0;
}
