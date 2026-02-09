/**
 * CarryGrid v2 Stress Test Suite
 *
 * Section 1: Swap Inversion (7 instruments × 3 swap modes = 21 tests)
 *   - Normal swap, inverted swap, zero swap — real 2025 tick data
 *   - Verify no stop-outs on crude oils with inverted swaps
 *
 * Section 2: survive_pct Sensitivity (7 values × 4 instruments = 28 tests)
 *   - survive_pct: 25–55% in steps of 5
 *   - Zero swap only — find optimal risk-adjusted survive_pct per instrument
 *
 * Section 3: Adaptive vs Fixed Bias (7 instruments × 2 modes = 14 tests)
 *   - Old: fixed long_bias=0.7 vs New: fully adaptive
 *   - Verify adaptive doesn't stop-out where fixed did
 *
 * Total: ~63 tests
 */

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
#include <map>

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
};

static std::vector<InstrumentConfig> GetOilGasInstruments() {
    return {
        {"CL-OIL",    "Crude Oil Future CFD",   1000.0, 0.001, 3,  0.0,     0.0,      0, 3, 0.01, 0.01},
        {"UKOUSD",    "Brent Crude Oil Cash",    1000.0, 0.001, 3,  9.3785, -30.539,   1, 5, 0.01, 0.01},
        {"UKOUSDft",  "Brent Crude Oil Future",  1000.0, 0.001, 3,  56.158, -102.463,  0, 5, 0.01, 0.01},
        {"USOUSD",    "WTI Crude Oil Cash",      1000.0, 0.001, 3, -4.8265, -13.0265,  1, 5, 0.01, 0.01},
        {"NG-C",      "Natural Gas",            10000.0, 0.001, 3,  32.86,  -49.52,    1, 5, 0.10, 0.10},
        {"GASOIL-C",  "Low Sulphur Gasoil",       100.0, 0.01,  2,  14.51,  -28.61,    1, 5, 0.10, 0.10},
        {"GAS-C",     "Gasoline",               42000.0, 0.0001,4, -6.65,    3.17,     1, 5, 0.10, 0.10},
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

    dest.reserve(8000000);

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
TickBacktestConfig MakeEngineConfig(const InstrumentConfig& inst, double initial_balance,
                                     double swap_long_override, double swap_short_override,
                                     int swap_mode_override) {
    TickDataConfig tick_config;
    tick_config.file_path = "";
    tick_config.format = TickDataFormat::MT5_CSV;

    TickBacktestConfig config;
    config.symbol = inst.symbol;
    config.initial_balance = initial_balance;
    config.contract_size = inst.contract_size;
    config.leverage = 500.0;
    config.pip_size = inst.pip_size;
    config.digits = inst.digits;
    config.swap_long = swap_long_override;
    config.swap_short = swap_short_override;
    config.swap_mode = swap_mode_override;
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
// CarryGrid config per instrument (same presets as comprehensive test)
// ============================================================================
CarryGrid::Config MakeCarryGridConfig(const InstrumentConfig& inst) {
    if (inst.symbol == "NG-C")      return CarryGrid::Config::NG_C();
    if (inst.symbol == "CL-OIL")    return CarryGrid::Config::CL_OIL();
    if (inst.symbol == "UKOUSD")    return CarryGrid::Config::UKOUSD();
    if (inst.symbol == "UKOUSDft")  return CarryGrid::Config::UKOUSD();
    if (inst.symbol == "GASOIL-C")  return CarryGrid::Config::GASOIL_C();
    if (inst.symbol == "GAS-C")     return CarryGrid::Config::GAS_C();
    return CarryGrid::Config::CL_OIL();  // USOUSD and others
}

// ============================================================================
// Test result structure
// ============================================================================
struct StressResult {
    std::string section;
    std::string symbol;
    std::string variant;     // e.g., "NORMAL_SWAP", "INVERTED", "ZERO", "survive=35", "ADAPTIVE", "FIXED_0.7"
    double final_balance;
    double return_mult;
    double max_dd_pct;
    int total_trades;
    double win_rate;
    double total_swap;
    double grid_profit;
    double risk_adjusted;
    double profit_factor;
    bool stop_out;
    // CarryGrid-specific stats
    long regime_changes;
    long counter_trend_closes;
    double avg_bias;
};

// ============================================================================
// Run a single CarryGrid stress test
// ============================================================================
StressResult RunStressTest(const std::string& section, const InstrumentConfig& inst,
                           const std::vector<Tick>& ticks, double initial_balance,
                           double swap_long, double swap_short, int swap_mode,
                           const std::string& variant,
                           std::function<void(CarryGrid&)> config_modifier = nullptr) {

    auto carry_cfg = MakeCarryGridConfig(inst);
    CarryGrid strategy(carry_cfg);

    // Apply any config modifier (e.g., SetFixedBiasMode)
    if (config_modifier) {
        config_modifier(strategy);
    }

    auto engine_cfg = MakeEngineConfig(inst, initial_balance, swap_long, swap_short, swap_mode);
    TickBasedEngine engine(engine_cfg);

    engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();
    auto grid_stats = strategy.GetStats();

    StressResult r;
    r.section = section;
    r.symbol = inst.symbol;
    r.variant = variant;
    r.final_balance = results.final_balance;
    r.return_mult = results.final_balance / initial_balance;
    r.max_dd_pct = results.max_drawdown_pct;
    r.total_trades = (int)results.total_trades;
    r.win_rate = results.win_rate;
    r.total_swap = results.total_swap_charged;
    r.grid_profit = results.final_balance - initial_balance - results.total_swap_charged;
    r.risk_adjusted = r.return_mult / (r.max_dd_pct / 100.0 + 0.01);
    r.profit_factor = results.profit_factor;
    r.stop_out = results.stop_out_occurred;
    r.regime_changes = grid_stats.regime_changes;
    r.counter_trend_closes = grid_stats.counter_trend_closes;
    r.avg_bias = grid_stats.avg_effective_bias;
    return r;
}

// ============================================================================
// Run survive_pct sweep (needs custom config)
// ============================================================================
StressResult RunSurviveSweepTest(const std::string& section, const InstrumentConfig& inst,
                                  const std::vector<Tick>& ticks, double initial_balance,
                                  double survive_pct, const std::string& variant) {

    auto carry_cfg = MakeCarryGridConfig(inst);
    carry_cfg.survive_pct = survive_pct;
    CarryGrid strategy(carry_cfg);

    // Zero swap for survive_pct sensitivity
    auto engine_cfg = MakeEngineConfig(inst, initial_balance, 0.0, 0.0, 0);
    TickBasedEngine engine(engine_cfg);

    engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();
    auto grid_stats = strategy.GetStats();

    StressResult r;
    r.section = section;
    r.symbol = inst.symbol;
    r.variant = variant;
    r.final_balance = results.final_balance;
    r.return_mult = results.final_balance / initial_balance;
    r.max_dd_pct = results.max_drawdown_pct;
    r.total_trades = (int)results.total_trades;
    r.win_rate = results.win_rate;
    r.total_swap = results.total_swap_charged;
    r.grid_profit = results.final_balance - initial_balance - results.total_swap_charged;
    r.risk_adjusted = r.return_mult / (r.max_dd_pct / 100.0 + 0.01);
    r.profit_factor = results.profit_factor;
    r.stop_out = results.stop_out_occurred;
    r.regime_changes = grid_stats.regime_changes;
    r.counter_trend_closes = grid_stats.counter_trend_closes;
    r.avg_bias = grid_stats.avg_effective_bias;
    return r;
}

// ============================================================================
// Helper: print a result row
// ============================================================================
void PrintResult(const StressResult& r) {
    std::cout << "  " << std::left
              << std::setw(10) << r.symbol
              << std::setw(16) << r.variant
              << std::fixed << std::setprecision(2) << std::right
              << std::setw(8) << r.return_mult << "x"
              << " | DD:" << std::setw(6) << r.max_dd_pct << "%"
              << " | Trades:" << std::setw(6) << r.total_trades
              << " | Swap:$" << std::setw(10) << r.total_swap
              << " | Grid:$" << std::setw(10) << r.grid_profit
              << " | RiskAdj:" << std::setw(8) << r.risk_adjusted
              << " | PF:" << std::setw(6) << r.profit_factor
              << " | Bias:" << std::setw(4) << r.avg_bias
              << (r.stop_out ? " [STOP-OUT]" : "")
              << std::endl << std::flush;
}

// ============================================================================
// MAIN
// ============================================================================
int main() {
    double initial_balance = 10000.0;
    auto instruments = GetOilGasInstruments();
    std::string data_dir = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Broker\\";

    std::cout << "================================================================" << std::endl;
    std::cout << "  CARRY GRID v2 STRESS TEST SUITE" << std::endl;
    std::cout << "  Section 1: Swap Inversion (21 tests)" << std::endl;
    std::cout << "  Section 2: survive_pct Sensitivity (28 tests)" << std::endl;
    std::cout << "  Section 3: Adaptive vs Fixed Bias (14 tests)" << std::endl;
    std::cout << "  Total: ~63 tests | Initial Balance: $" << initial_balance << std::endl;
    std::cout << "  Period: 2025.01.01 - 2025.12.30" << std::endl;
    std::cout << "================================================================" << std::endl;

    std::vector<StressResult> all_results;
    all_results.reserve(70);

    // Pre-load all tick data into a map for reuse
    std::map<std::string, std::vector<Tick>> tick_cache;

    for (const auto& inst : instruments) {
        std::string path = data_dir + inst.symbol + "_TICKS_2025.csv";
        if (!std::filesystem::exists(path)) {
            std::cout << "[SKIP] " << inst.symbol << " - no tick data at " << path << std::endl;
            continue;
        }
        tick_cache[inst.symbol] = std::vector<Tick>();
        LoadTickData(path, tick_cache[inst.symbol], inst.symbol);
    }

    // ========================================================================
    // SECTION 1: SWAP INVERSION
    // 7 instruments × 3 swap modes (normal, inverted, zero) = 21 tests
    // ========================================================================
    std::cout << "\n" << std::string(100, '=') << std::endl;
    std::cout << "  SECTION 1: SWAP INVERSION STRESS TEST (21 tests)" << std::endl;
    std::cout << "  Purpose: Verify CarryGrid v2 survives even if swap direction flips" << std::endl;
    std::cout << std::string(100, '=') << std::endl;

    for (const auto& inst : instruments) {
        if (tick_cache.find(inst.symbol) == tick_cache.end()) continue;
        const auto& ticks = tick_cache[inst.symbol];
        if (ticks.empty()) continue;

        std::cout << "\n--- " << inst.symbol << " (" << inst.description << ") ---" << std::endl;

        // Mode 1: Normal swap
        {
            auto r = RunStressTest("SWAP_INVERSION", inst, ticks, initial_balance,
                                   inst.swap_long, inst.swap_short, inst.swap_mode,
                                   "NORMAL_SWAP");
            PrintResult(r);
            all_results.push_back(r);
        }

        // Mode 2: Inverted swap (swap_long <-> swap_short, with sign flip)
        {
            // Invert: what was positive becomes negative and vice versa
            double inv_long = -inst.swap_short;   // If short was -30, long becomes +30
            double inv_short = -inst.swap_long;    // If long was +9, short becomes -9
            auto r = RunStressTest("SWAP_INVERSION", inst, ticks, initial_balance,
                                   inv_long, inv_short, inst.swap_mode,
                                   "INVERTED_SWAP");
            PrintResult(r);
            all_results.push_back(r);
        }

        // Mode 3: Zero swap (pure grid profit)
        {
            auto r = RunStressTest("SWAP_INVERSION", inst, ticks, initial_balance,
                                   0.0, 0.0, 0,
                                   "ZERO_SWAP");
            PrintResult(r);
            all_results.push_back(r);
        }
    }

    // ========================================================================
    // SECTION 2: survive_pct SENSITIVITY
    // 7 values (25-55% step 5) × 4 instruments = 28 tests
    // Zero swap only — isolate pure grid performance
    // Focus on: CL-OIL, UKOUSD, USOUSD, NG-C (the most interesting)
    // ========================================================================
    std::cout << "\n" << std::string(100, '=') << std::endl;
    std::cout << "  SECTION 2: survive_pct SENSITIVITY (28 tests)" << std::endl;
    std::cout << "  Purpose: Find optimal risk-adjusted survive_pct per instrument" << std::endl;
    std::cout << "  All tests with ZERO swap to isolate grid performance" << std::endl;
    std::cout << std::string(100, '=') << std::endl;

    std::vector<std::string> sweep_instruments = {"CL-OIL", "UKOUSD", "USOUSD", "NG-C"};
    std::vector<double> survive_values = {25.0, 30.0, 35.0, 40.0, 45.0, 50.0, 55.0};

    for (const auto& sym : sweep_instruments) {
        // Find the instrument config
        const InstrumentConfig* inst_ptr = nullptr;
        for (const auto& inst : instruments) {
            if (inst.symbol == sym) { inst_ptr = &inst; break; }
        }
        if (!inst_ptr || tick_cache.find(sym) == tick_cache.end()) continue;
        const auto& ticks = tick_cache[sym];
        if (ticks.empty()) continue;

        std::cout << "\n--- " << sym << " survive_pct sweep (zero swap) ---" << std::endl;

        for (double spct : survive_values) {
            std::string variant = "survive=" + std::to_string((int)spct) + "%";
            auto r = RunSurviveSweepTest("SURVIVE_PCT", *inst_ptr, ticks, initial_balance,
                                          spct, variant);
            PrintResult(r);
            all_results.push_back(r);
        }
    }

    // ========================================================================
    // SECTION 3: ADAPTIVE vs FIXED BIAS
    // 7 instruments × 2 modes (adaptive v2 vs fixed_bias=0.7) = 14 tests
    // Zero swap — no swap advantage to either mode
    // ========================================================================
    std::cout << "\n" << std::string(100, '=') << std::endl;
    std::cout << "  SECTION 3: ADAPTIVE vs FIXED BIAS (14 tests)" << std::endl;
    std::cout << "  Purpose: Verify adaptive v2 doesn't stop-out where fixed did" << std::endl;
    std::cout << "  All tests with ZERO swap for fair comparison" << std::endl;
    std::cout << std::string(100, '=') << std::endl;

    for (const auto& inst : instruments) {
        if (tick_cache.find(inst.symbol) == tick_cache.end()) continue;
        const auto& ticks = tick_cache[inst.symbol];
        if (ticks.empty()) continue;

        std::cout << "\n--- " << inst.symbol << " (" << inst.description << ") ---" << std::endl;

        // Mode A: Adaptive v2 (default — regime-driven bias)
        {
            auto r = RunStressTest("ADAPTIVE_VS_FIXED", inst, ticks, initial_balance,
                                   0.0, 0.0, 0,
                                   "ADAPTIVE_v2");
            PrintResult(r);
            all_results.push_back(r);
        }

        // Mode B: Fixed bias 0.7 long (simulates old v1 behavior)
        {
            auto r = RunStressTest("ADAPTIVE_VS_FIXED", inst, ticks, initial_balance,
                                   0.0, 0.0, 0,
                                   "FIXED_0.70",
                                   [](CarryGrid& strat) { strat.SetFixedBiasMode(0.70); });
            PrintResult(r);
            all_results.push_back(r);
        }
    }

    // ========================================================================
    // SUMMARY TABLES
    // ========================================================================

    // ---- Section 1 Summary: Swap Inversion ----
    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "  SECTION 1 SUMMARY: SWAP INVERSION" << std::endl;
    std::cout << std::string(120, '=') << std::endl;
    std::cout << std::left
              << std::setw(10) << "Symbol"
              << std::setw(16) << "Variant"
              << std::right
              << std::setw(10) << "Return"
              << std::setw(10) << "MaxDD%"
              << std::setw(10) << "RiskAdj"
              << std::setw(10) << "Trades"
              << std::setw(12) << "Swap$"
              << std::setw(12) << "Grid$"
              << std::setw(8) << "PF"
              << std::setw(6) << "SO"
              << std::endl;
    std::cout << std::string(120, '-') << std::endl;

    for (const auto& r : all_results) {
        if (r.section != "SWAP_INVERSION") continue;
        std::cout << std::fixed << std::setprecision(2)
                  << std::left
                  << std::setw(10) << r.symbol
                  << std::setw(16) << r.variant
                  << std::right
                  << std::setw(9) << r.return_mult << "x"
                  << std::setw(10) << r.max_dd_pct
                  << std::setw(10) << r.risk_adjusted
                  << std::setw(10) << r.total_trades
                  << std::setw(12) << r.total_swap
                  << std::setw(12) << r.grid_profit
                  << std::setw(8) << r.profit_factor
                  << std::setw(6) << (r.stop_out ? "YES" : "")
                  << std::endl;
    }

    // Check: crude oil stop-outs with inverted swaps
    std::cout << "\n  CRITICAL CHECK — Crude oils with inverted swaps:" << std::endl;
    std::vector<std::string> crude_oils = {"CL-OIL", "UKOUSD", "UKOUSDft", "USOUSD"};
    int crude_inv_stopouts = 0;
    for (const auto& r : all_results) {
        if (r.section != "SWAP_INVERSION" || r.variant != "INVERTED_SWAP") continue;
        for (const auto& co : crude_oils) {
            if (r.symbol == co) {
                std::cout << "    " << r.symbol << ": " << r.return_mult << "x, DD:"
                          << r.max_dd_pct << "% " << (r.stop_out ? "[STOP-OUT!]" : "[OK]") << std::endl;
                if (r.stop_out) crude_inv_stopouts++;
            }
        }
    }
    std::cout << "    Crude oil inverted swap stop-outs: " << crude_inv_stopouts << "/4"
              << (crude_inv_stopouts == 0 ? " PASS" : " FAIL") << std::endl;

    // ---- Section 2 Summary: survive_pct per instrument ----
    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "  SECTION 2 SUMMARY: OPTIMAL survive_pct PER INSTRUMENT" << std::endl;
    std::cout << std::string(120, '=') << std::endl;

    for (const auto& sym : sweep_instruments) {
        std::cout << "\n  " << sym << ":" << std::endl;
        std::cout << "  " << std::left
                  << std::setw(16) << "survive_pct"
                  << std::right
                  << std::setw(10) << "Return"
                  << std::setw(10) << "MaxDD%"
                  << std::setw(10) << "RiskAdj"
                  << std::setw(10) << "Trades"
                  << std::setw(6) << "SO"
                  << std::endl;
        std::cout << "  " << std::string(62, '-') << std::endl;

        StressResult best;
        best.risk_adjusted = -999.0;

        for (const auto& r : all_results) {
            if (r.section != "SURVIVE_PCT" || r.symbol != sym) continue;
            std::cout << "  " << std::fixed << std::setprecision(2)
                      << std::left << std::setw(16) << r.variant
                      << std::right
                      << std::setw(9) << r.return_mult << "x"
                      << std::setw(10) << r.max_dd_pct
                      << std::setw(10) << r.risk_adjusted
                      << std::setw(10) << r.total_trades
                      << std::setw(6) << (r.stop_out ? "YES" : "")
                      << std::endl;
            if (!r.stop_out && r.risk_adjusted > best.risk_adjusted) {
                best = r;
            }
        }

        if (best.risk_adjusted > -999.0) {
            std::cout << "  >>> BEST: " << best.variant << " -> " << best.return_mult
                      << "x, DD:" << best.max_dd_pct << "%, RiskAdj:" << best.risk_adjusted << std::endl;
        } else {
            std::cout << "  >>> ALL STOPPED OUT" << std::endl;
        }
    }

    // ---- Section 3 Summary: Adaptive vs Fixed ----
    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "  SECTION 3 SUMMARY: ADAPTIVE v2 vs FIXED BIAS" << std::endl;
    std::cout << std::string(120, '=') << std::endl;
    std::cout << std::left
              << std::setw(10) << "Symbol"
              << std::setw(16) << "Mode"
              << std::right
              << std::setw(10) << "Return"
              << std::setw(10) << "MaxDD%"
              << std::setw(10) << "RiskAdj"
              << std::setw(10) << "Trades"
              << std::setw(10) << "AvgBias"
              << std::setw(10) << "RegChg"
              << std::setw(10) << "CTClose"
              << std::setw(6) << "SO"
              << std::endl;
    std::cout << std::string(120, '-') << std::endl;

    int adaptive_wins = 0, fixed_wins = 0, ties = 0;
    int adaptive_stopouts = 0, fixed_stopouts = 0;

    // Group by instrument
    for (const auto& inst : instruments) {
        StressResult adaptive_r, fixed_r;
        bool has_adaptive = false, has_fixed = false;

        for (const auto& r : all_results) {
            if (r.section != "ADAPTIVE_VS_FIXED" || r.symbol != inst.symbol) continue;
            if (r.variant == "ADAPTIVE_v2") { adaptive_r = r; has_adaptive = true; }
            if (r.variant == "FIXED_0.70") { fixed_r = r; has_fixed = true; }
        }

        if (!has_adaptive || !has_fixed) continue;

        // Print both rows
        auto print_row = [](const StressResult& r) {
            std::cout << std::fixed << std::setprecision(2)
                      << std::left
                      << std::setw(10) << r.symbol
                      << std::setw(16) << r.variant
                      << std::right
                      << std::setw(9) << r.return_mult << "x"
                      << std::setw(10) << r.max_dd_pct
                      << std::setw(10) << r.risk_adjusted
                      << std::setw(10) << r.total_trades
                      << std::setw(10) << r.avg_bias
                      << std::setw(10) << r.regime_changes
                      << std::setw(10) << r.counter_trend_closes
                      << std::setw(6) << (r.stop_out ? "YES" : "")
                      << std::endl;
        };

        print_row(adaptive_r);
        print_row(fixed_r);

        // Determine winner
        if (adaptive_r.stop_out) adaptive_stopouts++;
        if (fixed_r.stop_out) fixed_stopouts++;

        if (adaptive_r.stop_out && !fixed_r.stop_out) {
            fixed_wins++;
            std::cout << "  >>> FIXED wins (adaptive stopped out)" << std::endl;
        } else if (!adaptive_r.stop_out && fixed_r.stop_out) {
            adaptive_wins++;
            std::cout << "  >>> ADAPTIVE wins (fixed stopped out)" << std::endl;
        } else if (!adaptive_r.stop_out && !fixed_r.stop_out) {
            if (adaptive_r.risk_adjusted > fixed_r.risk_adjusted * 1.05) {
                adaptive_wins++;
                std::cout << "  >>> ADAPTIVE wins (better risk-adjusted: "
                          << adaptive_r.risk_adjusted << " vs " << fixed_r.risk_adjusted << ")" << std::endl;
            } else if (fixed_r.risk_adjusted > adaptive_r.risk_adjusted * 1.05) {
                fixed_wins++;
                std::cout << "  >>> FIXED wins (better risk-adjusted: "
                          << fixed_r.risk_adjusted << " vs " << adaptive_r.risk_adjusted << ")" << std::endl;
            } else {
                ties++;
                std::cout << "  >>> TIE (within 5%)" << std::endl;
            }
        } else {
            ties++;
            std::cout << "  >>> BOTH STOPPED OUT" << std::endl;
        }
        std::cout << std::endl;
    }

    std::cout << "\n  Adaptive v2 wins: " << adaptive_wins << " | Fixed wins: " << fixed_wins
              << " | Ties: " << ties << std::endl;
    std::cout << "  Adaptive stop-outs: " << adaptive_stopouts << " | Fixed stop-outs: " << fixed_stopouts << std::endl;

    // ========================================================================
    // OVERALL SUMMARY
    // ========================================================================
    std::cout << "\n" << std::string(100, '=') << std::endl;
    std::cout << "  OVERALL STRESS TEST SUMMARY" << std::endl;
    std::cout << std::string(100, '=') << std::endl;
    std::cout << "  Total tests run: " << all_results.size() << std::endl;

    int total_stopouts = 0;
    int section1_stopouts = 0, section2_stopouts = 0, section3_stopouts = 0;
    for (const auto& r : all_results) {
        if (r.stop_out) {
            total_stopouts++;
            if (r.section == "SWAP_INVERSION") section1_stopouts++;
            if (r.section == "SURVIVE_PCT") section2_stopouts++;
            if (r.section == "ADAPTIVE_VS_FIXED") section3_stopouts++;
        }
    }

    std::cout << "  Total stop-outs: " << total_stopouts << "/" << all_results.size() << std::endl;
    std::cout << "    Section 1 (Swap Inversion): " << section1_stopouts << "/21" << std::endl;
    std::cout << "    Section 2 (survive_pct):    " << section2_stopouts << "/28" << std::endl;
    std::cout << "    Section 3 (Adaptive/Fixed): " << section3_stopouts << "/14" << std::endl;

    // Find best overall result
    StressResult best_overall;
    best_overall.risk_adjusted = -999.0;
    for (const auto& r : all_results) {
        if (!r.stop_out && r.risk_adjusted > best_overall.risk_adjusted) {
            best_overall = r;
        }
    }
    if (best_overall.risk_adjusted > -999.0) {
        std::cout << "\n  Best overall: " << best_overall.symbol << " " << best_overall.variant
                  << " " << best_overall.section
                  << " -> " << best_overall.return_mult << "x, DD:" << best_overall.max_dd_pct
                  << "%, RiskAdj:" << best_overall.risk_adjusted << std::endl;
    }

    // Crude oil inverted swap verdict
    std::cout << "\n  VERDICT — Crude oil inverted swap survival: "
              << (crude_inv_stopouts == 0 ? "PASS" : "FAIL") << std::endl;
    std::cout << "  VERDICT — Adaptive v2 vs Fixed: "
              << (adaptive_stopouts <= fixed_stopouts ? "ADAPTIVE >= FIXED" : "FIXED > ADAPTIVE")
              << std::endl;

    std::cout << std::string(100, '=') << std::endl;

    return 0;
}
