/**
 * Comprehensive Oil/Gas Strategy Test Suite
 *
 * Tests 4 strategies × 7 instruments × 2 swap modes = 56 configurations
 *
 * Strategies:
 *   1. FillUp (BUY-only grid) — baseline
 *   2. FillDown (SELL-only grid) — benefits from downtrends
 *   3. FillUp+FillDown Naive — both running independently, no capital split
 *   4. CarryGrid — swap-aware bidirectional with regime detection
 *
 * Swap modes:
 *   ON  — normal instrument swap rates
 *   OFF — swap_mode=0 (DISABLED) to isolate pure grid profit
 *
 * Uses RunWithTicks pattern: load ticks once per instrument, reuse across configs.
 */

#include "../include/fill_up_oscillation.h"
#include "../include/fill_down_oscillation.h"
#include "../include/strategy_carry_grid.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <chrono>
#include <filesystem>
#include <functional>

using namespace backtest;

// ============================================================================
// Instrument configurations (from  live scan)
// ============================================================================
struct InstrumentConfig {
    std::string symbol;
    std::string description;
    double contract_size;
    double pip_size;
    int digits;
    double swap_long;
    double swap_short;
    int swap_mode;       // 0=DISABLED, 1=POINTS
    int swap_3days;
    double volume_min;
    double volume_step;
    double daily_range_pct;
};

static std::vector<InstrumentConfig> GetOilGasInstruments() {
    return {
        {"CL-OIL",    "Crude Oil Future CFD",   1000.0, 0.001, 3,  0.0,     0.0,      0, 3, 0.01, 0.01, 3.36},
        {"UKOUSD",    "Brent Crude Oil Cash",    1000.0, 0.001, 3,  9.3785, -30.539,   1, 5, 0.01, 0.01, 3.05},
        {"UKOUSDft",  "Brent Crude Oil Future",  1000.0, 0.001, 3,  56.158, -102.463,  0, 5, 0.01, 0.01, 3.14},
        {"USOUSD",    "WTI Crude Oil Cash",      1000.0, 0.001, 3, -4.8265, -13.0265,  1, 5, 0.01, 0.01, 3.33},
        {"NG-C",      "Natural Gas",            10000.0, 0.001, 3,  32.86,  -49.52,    1, 5, 0.10, 0.10, 8.94},
        {"GASOIL-C",  "Low Sulphur Gasoil",       100.0, 0.01,  2,  14.51,  -28.61,    1, 5, 0.10, 0.10, 3.42},
        {"GAS-C",     "Gasoline",               42000.0, 0.0001,4, -6.65,    3.17,     1, 5, 0.10, 0.10, 2.94},
    };
}

// ============================================================================
// Tick data loader (tab-separated MT5 format)
// ============================================================================
void LoadTickData(const std::string& path, std::vector<Tick>& dest, const std::string& label) {
    std::cout << "Loading " << label << "..." << std::flush;
    auto start = std::chrono::high_resolution_clock::now();

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cout << " FAILED (cannot open)" << std::endl;
        return;
    }

    std::string line;
    std::getline(file, line);  // Skip header

    dest.reserve(8000000);  // Most oil instruments have ~3-7M ticks

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        Tick tick;
        std::stringstream ss(line);
        std::string datetime_str, bid_str, ask_str;

        std::getline(ss, datetime_str, '\t');
        std::getline(ss, bid_str, '\t');
        std::getline(ss, ask_str, '\t');

        tick.timestamp = datetime_str;
        tick.bid = std::stod(bid_str);
        tick.ask = std::stod(ask_str);
        tick.volume = 0;

        dest.push_back(tick);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
    std::cout << " " << dest.size() << " ticks in " << duration.count() << "s" << std::endl;
}

// ============================================================================
// Engine config factory
// ============================================================================
TickBacktestConfig MakeEngineConfig(const InstrumentConfig& inst, double initial_balance, bool swap_enabled) {
    TickDataConfig tick_config;
    tick_config.file_path = "";  // Not used with RunWithTicks
    tick_config.format = TickDataFormat::MT5_CSV;

    TickBacktestConfig config;
    config.symbol = inst.symbol;
    config.initial_balance = initial_balance;
    config.contract_size = inst.contract_size;
    config.leverage = 500.0;
    config.pip_size = inst.pip_size;
    config.digits = inst.digits;

    if (swap_enabled) {
        config.swap_long = inst.swap_long;
        config.swap_short = inst.swap_short;
        config.swap_mode = inst.swap_mode;
    } else {
        config.swap_long = 0.0;
        config.swap_short = 0.0;
        config.swap_mode = 0;  // DISABLED
    }

    config.swap_3days = inst.swap_3days;
    config.volume_min = inst.volume_min;
    config.volume_step = inst.volume_step;
    config.volume_max = 20.0;
    config.start_date = "2025.01.01";
    config.end_date = "2025.12.30";
    config.verbose = false;
    config.tick_data_config = tick_config;
    return config;
}

// ============================================================================
// FillUp config per instrument (BUY-only grid)
// ============================================================================
FillUpOscillation::Config MakeFillUpConfig(const InstrumentConfig& inst) {
    FillUpOscillation::Config cfg;
    cfg.contract_size = inst.contract_size;
    cfg.leverage = 500.0;
    cfg.min_volume = inst.volume_min;
    cfg.mode = FillUpOscillation::ADAPTIVE_SPACING;
    cfg.adaptive.pct_spacing = true;

    if (inst.symbol == "NG-C") {
        cfg.survive_pct = 50.0; cfg.base_spacing = 5.0;
        cfg.volatility_lookback_hours = 2.0; cfg.adaptive.typical_vol_pct = 2.0;
        cfg.max_volume = 0.1;
    } else if (inst.symbol == "GAS-C") {
        cfg.survive_pct = 40.0; cfg.base_spacing = 3.0;
        cfg.volatility_lookback_hours = 4.0; cfg.adaptive.typical_vol_pct = 0.5;
        cfg.max_volume = 0.1;
    } else if (inst.symbol == "GASOIL-C") {
        cfg.survive_pct = 30.0; cfg.base_spacing = 2.0;
        cfg.volatility_lookback_hours = 4.0; cfg.adaptive.typical_vol_pct = 0.6;
        cfg.max_volume = 1.0;
    } else {
        // Crude oils: CL-OIL, UKOUSD, UKOUSDft, USOUSD
        cfg.survive_pct = 40.0; cfg.base_spacing = 3.0;
        cfg.volatility_lookback_hours = 4.0; cfg.adaptive.typical_vol_pct = 0.6;
        cfg.max_volume = 0.5;
    }

    cfg.safety.equity_stop_pct = 60.0;
    cfg.safety.force_min_volume_entry = false;
    return cfg;
}

// ============================================================================
// FillDown config per instrument (SELL-only grid, mirror of FillUp)
// ============================================================================
FillDownOscillation::Config MakeFillDownConfig(const InstrumentConfig& inst) {
    FillDownOscillation::Config cfg;
    cfg.contract_size = inst.contract_size;
    cfg.leverage = 500.0;
    cfg.min_volume = inst.volume_min;
    cfg.mode = FillDownOscillation::ADAPTIVE_SPACING;
    cfg.adaptive.pct_spacing = true;

    if (inst.symbol == "NG-C") {
        cfg.survive_pct = 50.0; cfg.base_spacing = 5.0;
        cfg.volatility_lookback_hours = 2.0; cfg.adaptive.typical_vol_pct = 2.0;
        cfg.max_volume = 0.1;
    } else if (inst.symbol == "GAS-C") {
        cfg.survive_pct = 40.0; cfg.base_spacing = 3.0;
        cfg.volatility_lookback_hours = 4.0; cfg.adaptive.typical_vol_pct = 0.5;
        cfg.max_volume = 0.1;
    } else if (inst.symbol == "GASOIL-C") {
        cfg.survive_pct = 30.0; cfg.base_spacing = 2.0;
        cfg.volatility_lookback_hours = 4.0; cfg.adaptive.typical_vol_pct = 0.6;
        cfg.max_volume = 1.0;
    } else {
        cfg.survive_pct = 40.0; cfg.base_spacing = 3.0;
        cfg.volatility_lookback_hours = 4.0; cfg.adaptive.typical_vol_pct = 0.6;
        cfg.max_volume = 0.5;
    }

    cfg.force_min_volume_entry = false;
    cfg.max_positions = 80;          // Cap to prevent O(N²) slowdown
    cfg.equity_stop_pct = 60.0;     // Match FillUp safety
    return cfg;
}

// ============================================================================
// CarryGrid config per instrument
// ============================================================================
CarryGrid::Config MakeCarryGridConfig(const InstrumentConfig& inst) {
    if (inst.symbol == "NG-C")      return CarryGrid::Config::NG_C();
    if (inst.symbol == "CL-OIL")    return CarryGrid::Config::CL_OIL();
    if (inst.symbol == "UKOUSD")    return CarryGrid::Config::UKOUSD();
    if (inst.symbol == "UKOUSDft")  return CarryGrid::Config::UKOUSD();  // Same as UKOUSD
    if (inst.symbol == "GASOIL-C")  return CarryGrid::Config::GASOIL_C();
    if (inst.symbol == "GAS-C")     return CarryGrid::Config::GAS_C();

    // USOUSD and others: use CL_OIL preset (all presets are now neutral 50/50)
    return CarryGrid::Config::CL_OIL();
}

// ============================================================================
// Test result structure
// ============================================================================
struct TestResult {
    std::string strategy;
    std::string symbol;
    bool swap_enabled;
    double final_balance;
    double return_mult;
    double max_dd_pct;
    int total_trades;
    double win_rate;
    double total_swap;
    double grid_profit;   // final_balance - initial_balance - total_swap
    double risk_adjusted;
    double sharpe;
    double profit_factor;
    bool stop_out;
};

// ============================================================================
// Run a single test config
// ============================================================================
TestResult RunTest(const std::string& strategy_name, const InstrumentConfig& inst,
                   const std::vector<Tick>& ticks, double initial_balance, bool swap_enabled,
                   std::function<void(const Tick&, TickBasedEngine&)> strategy_fn) {

    auto engine_cfg = MakeEngineConfig(inst, initial_balance, swap_enabled);
    TickBasedEngine engine(engine_cfg);

    engine.RunWithTicks(ticks, strategy_fn);

    auto results = engine.GetResults();

    TestResult r;
    r.strategy = strategy_name;
    r.symbol = inst.symbol;
    r.swap_enabled = swap_enabled;
    r.final_balance = results.final_balance;
    r.return_mult = results.final_balance / initial_balance;
    r.max_dd_pct = results.max_drawdown_pct;
    r.total_trades = (int)results.total_trades;
    r.win_rate = results.win_rate;
    r.total_swap = results.total_swap_charged;
    r.grid_profit = results.final_balance - initial_balance - results.total_swap_charged;
    r.risk_adjusted = r.return_mult / (r.max_dd_pct / 100.0 + 0.01);
    r.sharpe = results.sharpe_ratio;
    r.profit_factor = results.profit_factor;
    r.stop_out = results.stop_out_occurred;
    return r;
}

// ============================================================================
// MAIN
// ============================================================================
int main() {
    double initial_balance = 10000.0;
    auto instruments = GetOilGasInstruments();
    std::string data_dir = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Broker\\";

    std::cout << "================================================================" << std::endl;
    std::cout << "  COMPREHENSIVE OIL/GAS STRATEGY COMPARISON" << std::endl;
    std::cout << "  4 Strategies × 7 Instruments × 2 Swap Modes = 56 Tests" << std::endl;
    std::cout << "  Initial Balance: $" << initial_balance << " | Period: 2025" << std::endl;
    std::cout << "================================================================" << std::endl;

    std::vector<TestResult> all_results;
    all_results.reserve(56);

    // ========================================================================
    // Process each instrument
    // ========================================================================
    for (const auto& inst : instruments) {
        std::string path = data_dir + inst.symbol + "_TICKS_2025.csv";
        if (!std::filesystem::exists(path)) {
            std::cout << "\n[SKIP] " << inst.symbol << " - no tick data" << std::endl;
            continue;
        }

        // Load ticks once for this instrument
        std::vector<Tick> ticks;
        LoadTickData(path, ticks, inst.symbol);

        if (ticks.empty()) {
            std::cout << "[SKIP] " << inst.symbol << " - no ticks loaded" << std::endl;
            continue;
        }

        std::cout << "\n--- Testing " << inst.symbol << " (" << inst.description << ") ---" << std::endl;

        // For each swap mode
        for (int swap_mode = 0; swap_mode <= 1; swap_mode++) {
            bool swap_on = (swap_mode == 1);
            std::string swap_label = swap_on ? "SWAP-ON" : "SWAP-OFF";

            // Helper lambda to print result
            auto print_result = [&](const std::string& strat_name, const TestResult& result) {
                std::cout << "  " << std::left << std::setw(8) << swap_label
                          << " " << std::setw(10) << strat_name
                          << std::fixed << std::setprecision(2)
                          << result.return_mult << "x | DD:" << result.max_dd_pct
                          << "% | Trades:" << result.total_trades
                          << " | Swap:$" << result.total_swap
                          << " | Grid:$" << result.grid_profit
                          << (result.stop_out ? " [STOP-OUT]" : "")
                          << std::endl << std::flush;
            };

            // ================================================================
            // Strategy 1: FillUp (BUY-only grid)
            // ================================================================
            try {
                auto strat_cfg = MakeFillUpConfig(inst);
                FillUpOscillation strategy(strat_cfg);
                auto result = RunTest("FillUp", inst, ticks, initial_balance, swap_on,
                    [&strategy](const Tick& tick, TickBasedEngine& eng) {
                        strategy.OnTick(tick, eng);
                    });
                print_result("FillUp", result);
                all_results.push_back(result);
            } catch (const std::exception& e) {
                std::cout << "  " << swap_label << " FillUp     ERROR: " << e.what() << std::endl << std::flush;
            }

            // ================================================================
            // Strategy 2: FillDown (SELL-only grid)
            // ================================================================
            try {
                auto strat_cfg = MakeFillDownConfig(inst);
                FillDownOscillation strategy(strat_cfg);
                auto result = RunTest("FillDown", inst, ticks, initial_balance, swap_on,
                    [&strategy](const Tick& tick, TickBasedEngine& eng) {
                        strategy.OnTick(tick, eng);
                    });
                print_result("FillDown", result);
                all_results.push_back(result);
            } catch (const std::exception& e) {
                std::cout << "  " << swap_label << " FillDown   ERROR: " << e.what() << std::endl << std::flush;
            }

            // ================================================================
            // Strategy 3: FillUp + FillDown Naive (both on same engine, no split)
            // ================================================================
            try {
                auto up_cfg = MakeFillUpConfig(inst);
                auto down_cfg = MakeFillDownConfig(inst);
                FillUpOscillation up_strategy(up_cfg);
                FillDownOscillation down_strategy(down_cfg);
                auto result = RunTest("Naive-Bi", inst, ticks, initial_balance, swap_on,
                    [&up_strategy, &down_strategy](const Tick& tick, TickBasedEngine& eng) {
                        up_strategy.OnTick(tick, eng);
                        down_strategy.OnTick(tick, eng);
                    });
                print_result("NaiveBidi", result);
                all_results.push_back(result);
            } catch (const std::exception& e) {
                std::cout << "  " << swap_label << " NaiveBidi  ERROR: " << e.what() << std::endl << std::flush;
            }

            // ================================================================
            // Strategy 4: CarryGrid (swap-aware bidirectional with regime)
            // ================================================================
            try {
                auto carry_cfg = MakeCarryGridConfig(inst);
                CarryGrid strategy(carry_cfg);
                auto result = RunTest("CarryGrid", inst, ticks, initial_balance, swap_on,
                    [&strategy](const Tick& tick, TickBasedEngine& eng) {
                        strategy.OnTick(tick, eng);
                    });
                print_result("CarryGrid", result);
                all_results.push_back(result);
            } catch (const std::exception& e) {
                std::cout << "  " << swap_label << " CarryGrid  ERROR: " << e.what() << std::endl << std::flush;
            }
        }
    }

    // ========================================================================
    // SUMMARY TABLE — sorted by risk-adjusted return
    // ========================================================================
    std::cout << "\n" << std::string(140, '=') << std::endl;
    std::cout << "  SUMMARY TABLE — Sorted by Risk-Adjusted Return" << std::endl;
    std::cout << std::string(140, '=') << std::endl;

    // Sort by risk-adjusted return
    std::sort(all_results.begin(), all_results.end(),
              [](const TestResult& a, const TestResult& b) {
                  return a.risk_adjusted > b.risk_adjusted;
              });

    std::cout << std::left
              << std::setw(10) << "Strategy"
              << std::setw(10) << "Symbol"
              << std::setw(8) << "Swap"
              << std::right
              << std::setw(10) << "Return"
              << std::setw(10) << "MaxDD%"
              << std::setw(8) << "Trades"
              << std::setw(8) << "WR%"
              << std::setw(12) << "Swap$"
              << std::setw(12) << "Grid$"
              << std::setw(10) << "RiskAdj"
              << std::setw(8) << "PF"
              << std::setw(6) << "SO"
              << std::endl;
    std::cout << std::string(140, '-') << std::endl;

    for (const auto& r : all_results) {
        std::cout << std::fixed << std::setprecision(2)
                  << std::left
                  << std::setw(10) << r.strategy
                  << std::setw(10) << r.symbol
                  << std::setw(8) << (r.swap_enabled ? "ON" : "OFF")
                  << std::right
                  << std::setw(9) << r.return_mult << "x"
                  << std::setw(10) << r.max_dd_pct
                  << std::setw(8) << r.total_trades
                  << std::setw(8) << r.win_rate
                  << std::setw(12) << r.total_swap
                  << std::setw(12) << r.grid_profit
                  << std::setw(10) << r.risk_adjusted
                  << std::setw(8) << r.profit_factor
                  << std::setw(6) << (r.stop_out ? "YES" : "")
                  << std::endl;
    }

    // ========================================================================
    // SWAP vs GRID BREAKDOWN — per instrument best strategy
    // ========================================================================
    std::cout << "\n" << std::string(100, '=') << std::endl;
    std::cout << "  SWAP vs GRID PROFIT BREAKDOWN (Best strategy per instrument)" << std::endl;
    std::cout << std::string(100, '=') << std::endl;

    auto instruments_list = GetOilGasInstruments();
    for (const auto& inst : instruments_list) {
        // Find best risk-adjusted result for this instrument with swap ON
        TestResult best_swap_on;
        best_swap_on.risk_adjusted = -999;
        TestResult best_swap_off;
        best_swap_off.risk_adjusted = -999;

        for (const auto& r : all_results) {
            if (r.symbol != inst.symbol) continue;
            if (r.swap_enabled && r.risk_adjusted > best_swap_on.risk_adjusted) {
                best_swap_on = r;
            }
            if (!r.swap_enabled && r.risk_adjusted > best_swap_off.risk_adjusted) {
                best_swap_off = r;
            }
        }

        if (best_swap_on.risk_adjusted > -999) {
            double swap_pct = 0;
            double grid_pct = 0;
            double total_profit = best_swap_on.final_balance - initial_balance;
            if (total_profit > 0) {
                swap_pct = best_swap_on.total_swap / total_profit * 100.0;
                grid_pct = best_swap_on.grid_profit / total_profit * 100.0;
            }

            std::cout << std::left << std::setw(10) << inst.symbol
                      << " Best: " << std::setw(10) << best_swap_on.strategy
                      << std::fixed << std::setprecision(2)
                      << " | " << best_swap_on.return_mult << "x"
                      << " | Total: $" << std::setw(10) << total_profit
                      << " | Swap: $" << std::setw(10) << best_swap_on.total_swap
                      << " (" << std::setw(5) << swap_pct << "%)"
                      << " | Grid: $" << std::setw(10) << best_swap_on.grid_profit
                      << " (" << std::setw(5) << grid_pct << "%)"
                      << std::endl;

            // Show swap-OFF comparison
            if (best_swap_off.risk_adjusted > -999) {
                std::cout << std::left << std::setw(10) << ""
                          << " NoSwap: " << std::setw(10) << best_swap_off.strategy
                          << " | " << best_swap_off.return_mult << "x"
                          << " | Grid-only: $" << std::setw(10) << best_swap_off.grid_profit
                          << std::endl;
            }
        }
    }

    // ========================================================================
    // DIRECTION COMPARISON — FillUp vs FillDown per instrument (swap ON)
    // ========================================================================
    std::cout << "\n" << std::string(100, '=') << std::endl;
    std::cout << "  DIRECTIONAL COMPARISON (Swap ON): BUY-grid vs SELL-grid" << std::endl;
    std::cout << std::string(100, '=') << std::endl;

    for (const auto& inst : instruments_list) {
        double fillup_ret = 0, filldown_ret = 0;
        double fillup_dd = 0, filldown_dd = 0;
        std::string winner;

        for (const auto& r : all_results) {
            if (r.symbol != inst.symbol || !r.swap_enabled) continue;
            if (r.strategy == "FillUp") {
                fillup_ret = r.return_mult;
                fillup_dd = r.max_dd_pct;
            }
            if (r.strategy == "FillDown") {
                filldown_ret = r.return_mult;
                filldown_dd = r.max_dd_pct;
            }
        }

        if (fillup_ret > filldown_ret) winner = "BUY wins";
        else if (filldown_ret > fillup_ret) winner = "SELL wins";
        else winner = "TIE";

        std::cout << std::left << std::setw(10) << inst.symbol
                  << std::fixed << std::setprecision(2)
                  << " | BUY: " << fillup_ret << "x (DD:" << fillup_dd << "%)"
                  << " | SELL: " << filldown_ret << "x (DD:" << filldown_dd << "%)"
                  << " | " << winner
                  << std::endl;
    }

    // ========================================================================
    // NG-C FOCUS — All strategies compared
    // ========================================================================
    std::cout << "\n" << std::string(100, '=') << std::endl;
    std::cout << "  NG-C FOCUS — Natural Gas (Most Promising)" << std::endl;
    std::cout << std::string(100, '=') << std::endl;

    for (const auto& r : all_results) {
        if (r.symbol != "NG-C") continue;
        std::cout << "  " << std::left << std::setw(10) << r.strategy
                  << std::setw(8) << (r.swap_enabled ? "SWAP-ON" : "SWAP-OFF")
                  << std::fixed << std::setprecision(2)
                  << " | " << std::setw(8) << r.return_mult << "x"
                  << " | DD:" << std::setw(8) << r.max_dd_pct << "%"
                  << " | Trades:" << std::setw(6) << r.total_trades
                  << " | Swap:$" << std::setw(12) << r.total_swap
                  << " | Grid:$" << std::setw(12) << r.grid_profit
                  << " | RiskAdj:" << std::setw(8) << r.risk_adjusted
                  << std::endl;
    }

    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "  Total tests: " << all_results.size() << " / 56 expected" << std::endl;

    // Count stop-outs
    int stop_outs = 0;
    for (const auto& r : all_results) {
        if (r.stop_out) stop_outs++;
    }
    std::cout << "  Stop-outs: " << stop_outs << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    return 0;
}
