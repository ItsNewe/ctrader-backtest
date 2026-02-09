/**
 * Comprehensive All-Instrument Test Suite
 *
 * Tests 34 instruments across Broker + Grid brokers:
 *   - NaiveBidi (FillUp + FillDown simultaneously, no capital split)
 *   - FillUp only (BUY-only grid)
 *   - FillDown only (SELL-only grid)
 *
 * All tests use ZERO swap to isolate pure grid profit.
 * Period: 2025.01.01 - 2026.02.06 (matching MT5 EA test period)
 *
 * Uses _TICKS_FULL.csv files (full period data).
 */

#include "../include/fill_up_oscillation.h"
#include "../include/fill_down_oscillation.h"
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
#include <algorithm>
#include <cmath>

using namespace backtest;

// ============================================================================
// Instrument configurations — ALL 34 instruments
// ============================================================================
struct InstrumentConfig {
    std::string symbol;
    std::string broker;         // "Broker" or "Grid"
    std::string description;
    double contract_size;
    double pip_size;
    int digits;
    double swap_long;
    double swap_short;
    int swap_mode;
    int swap_3days;
    double volume_min;
    double volume_step;
};

static std::vector<InstrumentConfig> GetAllInstruments() {
    return {
        // ================================================================
        // BROKER — Crude Oil
        // ================================================================
        {"CL-OIL",    "Broker", "Crude Oil Future CFD",       1000.0,  0.001,   3,    0.00,      0.00,   0, 3, 0.01, 0.01},
        {"UKOUSD",    "Broker", "Brent Crude Oil Cash",       1000.0,  0.001,   3,    9.38,    -30.54,   1, 5, 0.01, 0.01},
        {"UKOUSDft",  "Broker", "Brent Crude Oil Future",     1000.0,  0.001,   3,   56.16,   -102.46,   0, 5, 0.01, 0.01},
        {"USOUSD",    "Broker", "WTI Crude Oil Cash",         1000.0,  0.001,   3,   -4.83,    -13.03,   1, 5, 0.01, 0.01},
        // ================================================================
        // BROKER — Energy/Refined
        // ================================================================
        {"NG-C",      "Broker", "Natural Gas",               10000.0,  0.001,   3,   32.86,    -49.52,   1, 5, 0.10, 0.10},
        {"GASOIL-C",  "Broker", "Low Sulphur Gasoil",          100.0,  0.01,    2,   14.51,    -28.61,   1, 5, 0.10, 0.10},
        {"GAS-C",     "Broker", "Gasoline",                  42000.0,  0.0001,  4,   -6.65,      3.17,   1, 5, 0.10, 0.10},
        // ================================================================
        // BROKER — Agriculture
        // ================================================================
        {"Soybean-C", "Broker", "Soybean Cash",                500.0,  0.001,   3,   -3.27,      1.24,   1, 5, 0.10, 0.10},
        {"Sugar-C",   "Broker", "Sugar Raw Cash",            112000.0,  0.00001, 5,    2.27,     -4.80,   1, 5, 0.10, 0.10},
        {"COPPER-C",  "Broker", "Copper",                    25000.0,  0.0001,  4,  -15.93,      5.80,   1, 5, 0.10, 0.10},
        {"Cotton-C",  "Broker", "Cotton Cash",               50000.0,  0.00001, 5,  -32.29,     17.52,   1, 5, 0.10, 0.10},
        {"Cocoa-C",   "Broker", "US Cocoa Cash",                10.0,  0.1,     1,   -6.91,      1.80,   1, 5, 0.10, 0.10},
        {"Coffee-C",  "Broker", "Coffee Arabica Cash",       37500.0,  0.0001,  4,   15.56,    -26.39,   1, 5, 0.10, 0.10},
        {"OJ-C",      "Broker", "Orange Juice Cash",         15000.0,  0.0001,  4,   15.66,    -25.17,   1, 5, 0.10, 0.10},
        {"Wheat-C",   "Broker", "US Wheat SRW Cash",          1000.0,  0.001,   3,   -2.28,      1.18,   1, 5, 0.10, 0.10},
        // ================================================================
        // BROKER — Precious Metals
        // ================================================================
        {"XPTUSD",    "Broker", "Platinum",                     10.0,  0.01,    2,  -60.64,     -7.13,   1, 5, 0.10, 0.10},
        {"XPDUSD",    "Broker", "Palladium",                    10.0,  0.01,    2,  -29.61,    -19.32,   1, 5, 0.10, 0.10},
        // ================================================================
        // GRID — Energy
        // ================================================================
        {"XBRUSD",    "Grid", "Brent Crude Oil",            1000.0,  0.001,   3,   13.29,    -31.79,   1, 5, 0.01, 0.01},
        {"XNGUSD",    "Grid", "Natural Gas",               10000.0,  0.0001,  4,  112.84,   -617.55,   1, 5, 0.01, 0.01},
        {"XTIUSD",    "Grid", "WTI Crude Oil",              1000.0,  0.001,   3,    0.58,     -8.47,   1, 5, 0.01, 0.01},
        // ================================================================
        // GRID — Soft Commodities
        // ================================================================
        {"CORN",      "Grid", "Corn",                          2.0,  0.01,    2,  -21.66,      6.67,   1, 5, 1.00, 1.00},
        {"COTTON",    "Grid", "Cotton",                       10.0,  0.001,   3,  -24.36,      8.58,   1, 5, 1.00, 1.00},
        {"SOYBEAN",   "Grid", "Soybeans",                      1.0,  0.01,    2,  -34.31,      8.39,   1, 5, 1.00, 1.00},
        {"SUGARRAW",  "Grid", "Raw Sugar",                    50.0,  0.001,   3,   -6.96,      2.69,   1, 5, 1.00, 1.00},
        {"WHEAT",     "Grid", "Wheat",                         1.0,  0.01,    2,  -25.02,      9.69,   1, 5, 1.00, 1.00},
        {"COFARA",    "Grid", "Arabica Coffee",               10.0,  0.01,    2,   11.22,    -25.91,   1, 5, 1.00, 1.00},
        {"COFROB",    "Grid", "Robusta Coffee",                1.0,  0.01,    2,  129.94,   -300.48,   1, 5, 1.00, 1.00},
        {"OJ",        "Grid", "Orange Juice",                 10.0,  0.01,    2,    1.28,    -52.65,   5, 5, 1.00, 1.00},
        {"SUGAR",     "Grid", "White Sugar",                   2.0,  0.01,    2,   -1.61,     -5.59,   5, 5, 1.00, 1.00},
        {"UKCOCOA",   "Grid", "UK Cocoa",                      1.0,  0.01,    2,   -7.09,     -0.11,   5, 5, 1.00, 1.00},
        {"USCOCOA",   "Grid", "US Cocoa",                      1.0,  0.01,    2, -139.91,      2.81,   1, 5, 1.00, 1.00},
        // ================================================================
        // GRID — Metals
        // ================================================================
        {"XCUUSD",    "Grid", "Copper",                      100.0,  0.001,   3, -2199.12,     0.00,   1, 3, 0.01, 0.01},
        {"XALUSD",    "Grid", "Aluminium",                   100.0,  0.001,   3,  -394.20,     0.00,   1, 3, 0.01, 0.01},
        {"XPTUSD-F",  "Grid", "Platinum (Grid)",           100.0,  0.01,    2,   -48.87,    -3.52,   1, 3, 0.01, 0.01},
    };
}

// ============================================================================
// Tick data loader (tab-separated MT5 format)
// ============================================================================
void LoadTickData(const std::string& path, std::vector<Tick>& dest, const std::string& label) {
    std::cout << "  Loading " << label << "..." << std::flush;
    auto start = std::chrono::high_resolution_clock::now();

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cout << " FAILED (cannot open)" << std::endl;
        return;
    }

    std::string line;
    std::getline(file, line);  // Skip header

    dest.reserve(10000000);

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        Tick tick;
        std::stringstream ss(line);
        std::string datetime_str, bid_str, ask_str;

        std::getline(ss, datetime_str, '\t');
        std::getline(ss, bid_str, '\t');
        std::getline(ss, ask_str, '\t');

        tick.timestamp = datetime_str;
        try {
            tick.bid = std::stod(bid_str);
            tick.ask = std::stod(ask_str);
        } catch (...) {
            continue;  // Skip malformed lines
        }
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
TickBacktestConfig MakeEngineConfig(const InstrumentConfig& inst, double initial_balance) {
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

    // Zero swap — isolate pure grid profit
    config.swap_long = 0.0;
    config.swap_short = 0.0;
    config.swap_mode = 0;

    config.swap_3days = inst.swap_3days;
    config.volume_min = inst.volume_min;
    config.volume_step = inst.volume_step;
    config.volume_max = 20.0;
    config.start_date = "2025.01.01";
    config.end_date = "2026.02.28";
    config.verbose = false;
    config.tick_data_config = tick_config;
    return config;
}

// ============================================================================
// Strategy config — uses pct_spacing for universal compatibility
// ============================================================================
FillUpOscillation::Config MakeFillUpConfig(const InstrumentConfig& inst) {
    FillUpOscillation::Config cfg;
    cfg.contract_size = inst.contract_size;
    cfg.leverage = 500.0;
    cfg.min_volume = inst.volume_min;
    cfg.mode = FillUpOscillation::ADAPTIVE_SPACING;
    cfg.adaptive.pct_spacing = true;

    // Default parameters — conservative, universal across instruments
    cfg.survive_pct = 40.0;
    cfg.base_spacing = 3.0;      // 3% of price
    cfg.volatility_lookback_hours = 4.0;
    cfg.adaptive.typical_vol_pct = 0.6;
    cfg.max_volume = 1.0;

    // Instrument-specific overrides
    if (inst.symbol == "NG-C" || inst.symbol == "XNGUSD") {
        cfg.survive_pct = 50.0; cfg.base_spacing = 5.0;
        cfg.volatility_lookback_hours = 2.0; cfg.adaptive.typical_vol_pct = 2.0;
        cfg.max_volume = inst.volume_min;  // Minimum lot only
    } else if (inst.symbol == "GAS-C") {
        cfg.survive_pct = 40.0; cfg.base_spacing = 3.0;
        cfg.adaptive.typical_vol_pct = 0.5;
        cfg.max_volume = inst.volume_min;
    } else if (inst.symbol == "GASOIL-C") {
        cfg.survive_pct = 30.0; cfg.base_spacing = 2.0;
        cfg.adaptive.typical_vol_pct = 0.6;
        cfg.max_volume = 1.0;
    } else if (inst.symbol == "Cocoa-C" || inst.symbol == "USCOCOA" || inst.symbol == "UKCOCOA") {
        // High volatility (3%+ daily std), strong trend
        cfg.survive_pct = 50.0; cfg.base_spacing = 5.0;
        cfg.volatility_lookback_hours = 2.0; cfg.adaptive.typical_vol_pct = 3.0;
        cfg.max_volume = inst.volume_min;
    } else if (inst.symbol == "OJ-C" || inst.symbol == "OJ") {
        // Extreme volatility (4% daily std)
        cfg.survive_pct = 55.0; cfg.base_spacing = 6.0;
        cfg.volatility_lookback_hours = 2.0; cfg.adaptive.typical_vol_pct = 3.5;
        cfg.max_volume = inst.volume_min;
    } else if (inst.symbol == "Coffee-C" || inst.symbol == "COFARA" || inst.symbol == "COFROB") {
        cfg.survive_pct = 45.0; cfg.base_spacing = 4.0;
        cfg.volatility_lookback_hours = 2.0; cfg.adaptive.typical_vol_pct = 2.0;
        cfg.max_volume = inst.volume_min;
    } else if (inst.symbol == "COPPER-C" || inst.symbol == "XCUUSD" || inst.symbol == "XALUSD") {
        cfg.survive_pct = 35.0; cfg.base_spacing = 2.5;
        cfg.adaptive.typical_vol_pct = 0.8;
        cfg.max_volume = inst.volume_min;
    } else if (inst.symbol == "XPTUSD" || inst.symbol == "XPTUSD-F" || inst.symbol == "XPDUSD") {
        cfg.survive_pct = 35.0; cfg.base_spacing = 2.5;
        cfg.adaptive.typical_vol_pct = 0.8;
        cfg.max_volume = inst.volume_min;
    } else if (inst.symbol == "Sugar-C" || inst.symbol == "SUGARRAW" || inst.symbol == "SUGAR") {
        cfg.survive_pct = 40.0; cfg.base_spacing = 3.0;
        cfg.adaptive.typical_vol_pct = 1.0;
        cfg.max_volume = inst.volume_min;
    } else if (inst.symbol == "Cotton-C" || inst.symbol == "COTTON") {
        cfg.survive_pct = 40.0; cfg.base_spacing = 3.0;
        cfg.adaptive.typical_vol_pct = 1.0;
        cfg.max_volume = inst.volume_min;
    } else if (inst.symbol == "Wheat-C" || inst.symbol == "WHEAT") {
        cfg.survive_pct = 40.0; cfg.base_spacing = 3.0;
        cfg.adaptive.typical_vol_pct = 1.0;
        cfg.max_volume = inst.volume_min;
    } else if (inst.symbol == "Soybean-C" || inst.symbol == "SOYBEAN") {
        cfg.survive_pct = 40.0; cfg.base_spacing = 3.0;
        cfg.adaptive.typical_vol_pct = 1.0;
        cfg.max_volume = inst.volume_min;
    } else if (inst.symbol == "CORN") {
        cfg.survive_pct = 40.0; cfg.base_spacing = 3.0;
        cfg.adaptive.typical_vol_pct = 1.0;
        cfg.max_volume = inst.volume_min;
    } else {
        // Crude oils: CL-OIL, UKOUSD, UKOUSDft, USOUSD, XBRUSD, XTIUSD
        cfg.survive_pct = 40.0; cfg.base_spacing = 3.0;
        cfg.adaptive.typical_vol_pct = 0.6;
        cfg.max_volume = 0.5;
    }

    cfg.safety.equity_stop_pct = 60.0;
    cfg.safety.force_min_volume_entry = false;
    cfg.safety.max_positions = 80;
    return cfg;
}

FillDownOscillation::Config MakeFillDownConfig(const InstrumentConfig& inst) {
    FillDownOscillation::Config cfg;
    cfg.contract_size = inst.contract_size;
    cfg.leverage = 500.0;
    cfg.min_volume = inst.volume_min;
    cfg.mode = FillDownOscillation::ADAPTIVE_SPACING;
    cfg.adaptive.pct_spacing = true;

    // Mirror the FillUp parameters
    auto up_cfg = MakeFillUpConfig(inst);
    cfg.survive_pct = up_cfg.survive_pct;
    cfg.base_spacing = up_cfg.base_spacing;
    cfg.volatility_lookback_hours = up_cfg.volatility_lookback_hours;
    cfg.adaptive.typical_vol_pct = up_cfg.adaptive.typical_vol_pct;
    cfg.max_volume = up_cfg.max_volume;

    cfg.force_min_volume_entry = false;
    cfg.max_positions = 80;
    cfg.equity_stop_pct = 60.0;
    return cfg;
}

// ============================================================================
// Test result structure
// ============================================================================
struct TestResult {
    std::string strategy;
    std::string symbol;
    std::string broker;
    double final_balance;
    double return_mult;
    double max_dd_pct;
    int total_trades;
    double win_rate;
    double profit_factor;
    bool stop_out;
};

// ============================================================================
// Run a single test
// ============================================================================
TestResult RunTest(const std::string& strategy_name, const InstrumentConfig& inst,
                   const std::vector<Tick>& ticks, double initial_balance,
                   std::function<void(const Tick&, TickBasedEngine&)> strategy_fn) {

    auto engine_cfg = MakeEngineConfig(inst, initial_balance);
    TickBasedEngine engine(engine_cfg);

    engine.RunWithTicks(ticks, strategy_fn);

    auto results = engine.GetResults();

    TestResult r;
    r.strategy = strategy_name;
    r.symbol = inst.symbol;
    r.broker = inst.broker;
    r.final_balance = results.final_balance;
    r.return_mult = results.final_balance / initial_balance;
    r.max_dd_pct = results.max_drawdown_pct;
    r.total_trades = (int)results.total_trades;
    r.win_rate = results.win_rate;
    r.profit_factor = results.profit_factor;
    r.stop_out = results.stop_out_occurred;
    return r;
}

// ============================================================================
// Determine tick data file path
// ============================================================================
std::string GetTickDataPath(const InstrumentConfig& inst) {
    std::string base = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\";
    std::string broker_dir = inst.broker + "\\";
    std::string filename;

    // Handle XPTUSD-F (Grid) -> file is XPTUSD_TICKS_FULL.csv
    if (inst.symbol == "XPTUSD-F") {
        filename = "XPTUSD_TICKS_FULL.csv";
    } else {
        filename = inst.symbol + "_TICKS_FULL.csv";
    }

    return base + broker_dir + filename;
}

// ============================================================================
// MAIN
// ============================================================================
int main() {
    double initial_balance = 10000.0;
    auto instruments = GetAllInstruments();

    std::cout << std::string(120, '=') << std::endl;
    std::cout << "  ALL-INSTRUMENT GRID STRATEGY TEST" << std::endl;
    std::cout << "  " << instruments.size() << " Instruments × 3 Strategies (FillUp, FillDown, NaiveBidi)" << std::endl;
    std::cout << "  Zero swap — pure grid profit | $" << initial_balance << " initial | 500 leverage" << std::endl;
    std::cout << "  Period: 2025.01.01 - 2026.02.28" << std::endl;
    std::cout << std::string(120, '=') << std::endl;

    std::vector<TestResult> all_results;
    all_results.reserve(instruments.size() * 3);

    int test_num = 0;
    int total_tests = 0;

    // Count available instruments
    for (const auto& inst : instruments) {
        std::string path = GetTickDataPath(inst);
        if (std::filesystem::exists(path)) total_tests++;
    }
    total_tests *= 3;  // 3 strategies per instrument

    for (const auto& inst : instruments) {
        std::string path = GetTickDataPath(inst);

        if (!std::filesystem::exists(path)) {
            std::cout << "\n  [SKIP] " << inst.symbol << " (" << inst.broker
                      << ") — no tick data at " << path << std::endl;
            continue;
        }

        // Load ticks once
        std::vector<Tick> ticks;
        LoadTickData(path, ticks, inst.symbol + " (" + inst.broker + ")");

        if (ticks.empty()) {
            std::cout << "  [SKIP] " << inst.symbol << " — no ticks loaded" << std::endl;
            continue;
        }

        std::cout << "\n  ─── " << inst.symbol << " (" << inst.broker << ") | "
                  << inst.description << " | cs=" << inst.contract_size
                  << " ───" << std::endl;

        // ================================================================
        // Strategy 1: FillUp (BUY-only grid)
        // ================================================================
        test_num++;
        try {
            auto up_cfg = MakeFillUpConfig(inst);
            FillUpOscillation strategy(up_cfg);
            auto result = RunTest("FillUp", inst, ticks, initial_balance,
                [&strategy](const Tick& tick, TickBasedEngine& eng) {
                    strategy.OnTick(tick, eng);
                });
            std::cout << "  [" << test_num << "/" << total_tests << "] FillUp    "
                      << std::fixed << std::setprecision(2)
                      << result.return_mult << "x | DD:" << result.max_dd_pct
                      << "% | Trades:" << result.total_trades
                      << " | WR:" << result.win_rate << "%"
                      << (result.stop_out ? " [STOP-OUT]" : "")
                      << std::endl << std::flush;
            all_results.push_back(result);
        } catch (const std::exception& e) {
            std::cout << "  [" << test_num << "] FillUp    ERROR: " << e.what() << std::endl;
        }

        // ================================================================
        // Strategy 2: FillDown (SELL-only grid)
        // ================================================================
        test_num++;
        try {
            auto down_cfg = MakeFillDownConfig(inst);
            FillDownOscillation strategy(down_cfg);
            auto result = RunTest("FillDown", inst, ticks, initial_balance,
                [&strategy](const Tick& tick, TickBasedEngine& eng) {
                    strategy.OnTick(tick, eng);
                });
            std::cout << "  [" << test_num << "/" << total_tests << "] FillDown  "
                      << std::fixed << std::setprecision(2)
                      << result.return_mult << "x | DD:" << result.max_dd_pct
                      << "% | Trades:" << result.total_trades
                      << " | WR:" << result.win_rate << "%"
                      << (result.stop_out ? " [STOP-OUT]" : "")
                      << std::endl << std::flush;
            all_results.push_back(result);
        } catch (const std::exception& e) {
            std::cout << "  [" << test_num << "] FillDown  ERROR: " << e.what() << std::endl;
        }

        // ================================================================
        // Strategy 3: NaiveBidi (both grids, no capital split)
        // ================================================================
        test_num++;
        try {
            auto up_cfg = MakeFillUpConfig(inst);
            auto down_cfg = MakeFillDownConfig(inst);
            FillUpOscillation up_strategy(up_cfg);
            FillDownOscillation down_strategy(down_cfg);
            auto result = RunTest("NaiveBidi", inst, ticks, initial_balance,
                [&up_strategy, &down_strategy](const Tick& tick, TickBasedEngine& eng) {
                    up_strategy.OnTick(tick, eng);
                    down_strategy.OnTick(tick, eng);
                });
            std::cout << "  [" << test_num << "/" << total_tests << "] NaiveBidi "
                      << std::fixed << std::setprecision(2)
                      << result.return_mult << "x | DD:" << result.max_dd_pct
                      << "% | Trades:" << result.total_trades
                      << " | WR:" << result.win_rate << "%"
                      << (result.stop_out ? " [STOP-OUT]" : "")
                      << std::endl << std::flush;
            all_results.push_back(result);
        } catch (const std::exception& e) {
            std::cout << "  [" << test_num << "] NaiveBidi ERROR: " << e.what() << std::endl;
        }

        // Free memory
        ticks.clear();
        ticks.shrink_to_fit();
    }

    // ========================================================================
    // SUMMARY TABLE — All instruments, all strategies
    // ========================================================================
    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "  SUMMARY TABLE — All Instruments (Zero Swap)" << std::endl;
    std::cout << std::string(120, '=') << std::endl;

    std::cout << std::left
              << std::setw(12) << "Symbol"
              << std::setw(8) << "Broker"
              << std::setw(10) << "Strategy"
              << std::right
              << std::setw(10) << "Return"
              << std::setw(10) << "MaxDD%"
              << std::setw(8) << "Trades"
              << std::setw(8) << "WR%"
              << std::setw(8) << "PF"
              << std::setw(12) << "Final$"
              << std::setw(10) << "RiskAdj"
              << std::setw(6) << "SO"
              << std::endl;
    std::cout << std::string(120, '-') << std::endl;

    for (const auto& r : all_results) {
        double risk_adj = r.return_mult / (r.max_dd_pct / 100.0 + 0.01);
        std::cout << std::fixed << std::setprecision(2)
                  << std::left
                  << std::setw(12) << r.symbol
                  << std::setw(8) << r.broker
                  << std::setw(10) << r.strategy
                  << std::right
                  << std::setw(9) << r.return_mult << "x"
                  << std::setw(10) << r.max_dd_pct
                  << std::setw(8) << r.total_trades
                  << std::setw(8) << r.win_rate
                  << std::setw(8) << r.profit_factor
                  << std::setw(12) << r.final_balance
                  << std::setw(10) << risk_adj
                  << std::setw(6) << (r.stop_out ? "YES" : "")
                  << std::endl;
    }

    // ========================================================================
    // BEST STRATEGY PER INSTRUMENT
    // ========================================================================
    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "  BEST STRATEGY PER INSTRUMENT (by return, no stop-out)" << std::endl;
    std::cout << std::string(120, '=') << std::endl;

    std::cout << std::left
              << std::setw(14) << "Symbol"
              << std::setw(8) << "Broker"
              << std::setw(10) << "Best"
              << std::right
              << std::setw(10) << "Return"
              << std::setw(10) << "MaxDD%"
              << std::setw(10) << "FillUp"
              << std::setw(10) << "FillDown"
              << std::setw(10) << "NaiveBi"
              << "  Verdict"
              << std::endl;
    std::cout << std::string(120, '-') << std::endl;

    // Group results by symbol
    std::vector<std::string> processed;
    for (const auto& inst : instruments) {
        std::string sym = inst.symbol;
        if (std::find(processed.begin(), processed.end(), sym) != processed.end()) continue;
        processed.push_back(sym);

        double fillup_ret = 0, filldown_ret = 0, naive_ret = 0;
        double fillup_dd = 0, filldown_dd = 0, naive_dd = 0;
        bool fillup_so = false, filldown_so = false, naive_so = false;
        bool has_fillup = false, has_filldown = false, has_naive = false;

        for (const auto& r : all_results) {
            if (r.symbol != sym) continue;
            if (r.strategy == "FillUp") {
                fillup_ret = r.return_mult; fillup_dd = r.max_dd_pct;
                fillup_so = r.stop_out; has_fillup = true;
            } else if (r.strategy == "FillDown") {
                filldown_ret = r.return_mult; filldown_dd = r.max_dd_pct;
                filldown_so = r.stop_out; has_filldown = true;
            } else if (r.strategy == "NaiveBidi") {
                naive_ret = r.return_mult; naive_dd = r.max_dd_pct;
                naive_so = r.stop_out; has_naive = true;
            }
        }

        if (!has_fillup && !has_filldown && !has_naive) continue;

        // Find best (non-stop-out, highest return)
        std::string best = "NONE";
        double best_ret = 0;
        double best_dd = 0;
        if (has_fillup && !fillup_so && fillup_ret > best_ret) { best = "FillUp"; best_ret = fillup_ret; best_dd = fillup_dd; }
        if (has_filldown && !filldown_so && filldown_ret > best_ret) { best = "FillDown"; best_ret = filldown_ret; best_dd = filldown_dd; }
        if (has_naive && !naive_so && naive_ret > best_ret) { best = "NaiveBidi"; best_ret = naive_ret; best_dd = naive_dd; }

        // If all stopped out, pick the one that survived longest
        if (best == "NONE") {
            if (has_fillup) { best = "FillUp*"; best_ret = fillup_ret; best_dd = fillup_dd; }
            if (has_filldown && filldown_ret > best_ret) { best = "FillDown*"; best_ret = filldown_ret; best_dd = filldown_dd; }
            if (has_naive && naive_ret > best_ret) { best = "NaiveBi*"; best_ret = naive_ret; best_dd = naive_dd; }
        }

        // Verdict
        std::string verdict;
        if (naive_ret > 1.5 && !naive_so && naive_ret > fillup_ret && naive_ret > filldown_ret)
            verdict = "BOTH grids — oscillating";
        else if (fillup_ret > 1.5 && !fillup_so && fillup_ret > filldown_ret)
            verdict = "LONG only — bullish";
        else if (filldown_ret > 1.5 && !filldown_so && filldown_ret > fillup_ret)
            verdict = "SHORT only — bearish";
        else if (best_ret < 1.0)
            verdict = "NOT VIABLE";
        else
            verdict = "MARGINAL";

        auto fmt_ret = [](double ret, bool so) -> std::string {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.2fx%s", ret, so ? "*" : "");
            return std::string(buf);
        };

        std::cout << std::fixed << std::setprecision(2)
                  << std::left
                  << std::setw(14) << sym
                  << std::setw(8) << inst.broker
                  << std::setw(10) << best
                  << std::right
                  << std::setw(9) << best_ret << "x"
                  << std::setw(10) << best_dd
                  << std::setw(10) << (has_fillup ? fmt_ret(fillup_ret, fillup_so) : "N/A")
                  << std::setw(10) << (has_filldown ? fmt_ret(filldown_ret, filldown_so) : "N/A")
                  << std::setw(10) << (has_naive ? fmt_ret(naive_ret, naive_so) : "N/A")
                  << "  " << verdict
                  << std::endl;
    }

    // ========================================================================
    // CATEGORY SUMMARY
    // ========================================================================
    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "  CATEGORY ANALYSIS" << std::endl;
    std::cout << std::string(120, '=') << std::endl;

    // Count viable instruments by category
    struct CategoryStats {
        std::string name;
        int total = 0;
        int naive_viable = 0;     // NaiveBidi > 1.5x and no stop-out
        int fillup_viable = 0;
        int filldown_viable = 0;
        double avg_naive_ret = 0;
    };

    auto categorize = [](const std::string& sym) -> std::string {
        if (sym == "CL-OIL" || sym == "UKOUSD" || sym == "UKOUSDft" || sym == "USOUSD" ||
            sym == "XBRUSD" || sym == "XTIUSD") return "Crude Oil";
        if (sym == "NG-C" || sym == "XNGUSD") return "Natural Gas";
        if (sym == "GASOIL-C" || sym == "GAS-C") return "Refined Energy";
        if (sym == "COPPER-C" || sym == "XCUUSD" || sym == "XALUSD") return "Base Metals";
        if (sym == "XPTUSD" || sym == "XPTUSD-F" || sym == "XPDUSD") return "Precious Metals";
        if (sym == "Cocoa-C" || sym == "USCOCOA" || sym == "UKCOCOA") return "Cocoa";
        if (sym == "Coffee-C" || sym == "COFARA" || sym == "COFROB") return "Coffee";
        if (sym == "OJ-C" || sym == "OJ") return "Orange Juice";
        if (sym == "Sugar-C" || sym == "SUGARRAW" || sym == "SUGAR") return "Sugar";
        if (sym == "Cotton-C" || sym == "COTTON") return "Cotton";
        if (sym == "Soybean-C" || sym == "SOYBEAN") return "Soybean";
        if (sym == "Wheat-C" || sym == "WHEAT") return "Wheat";
        if (sym == "CORN") return "Corn";
        return "Other";
    };

    std::map<std::string, CategoryStats> cat_stats;
    for (const auto& r : all_results) {
        std::string cat = categorize(r.symbol);
        auto& cs = cat_stats[cat];
        cs.name = cat;
        if (r.strategy == "NaiveBidi") {
            cs.total++;
            cs.avg_naive_ret += r.return_mult;
            if (r.return_mult > 1.5 && !r.stop_out) cs.naive_viable++;
        } else if (r.strategy == "FillUp") {
            if (r.return_mult > 1.5 && !r.stop_out) cs.fillup_viable++;
        } else if (r.strategy == "FillDown") {
            if (r.return_mult > 1.5 && !r.stop_out) cs.filldown_viable++;
        }
    }

    std::cout << std::left
              << std::setw(18) << "Category"
              << std::right
              << std::setw(8) << "Total"
              << std::setw(12) << "NaiveBi OK"
              << std::setw(12) << "FillUp OK"
              << std::setw(12) << "FillDn OK"
              << std::setw(12) << "Avg NaiBi"
              << std::endl;
    std::cout << std::string(80, '-') << std::endl;

    for (auto& [name, cs] : cat_stats) {
        if (cs.total > 0) cs.avg_naive_ret /= cs.total;
        std::cout << std::left << std::setw(18) << name
                  << std::right
                  << std::setw(8) << cs.total
                  << std::setw(12) << cs.naive_viable
                  << std::setw(12) << cs.fillup_viable
                  << std::setw(12) << cs.filldown_viable
                  << std::fixed << std::setprecision(2)
                  << std::setw(11) << cs.avg_naive_ret << "x"
                  << std::endl;
    }

    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "  ANALYSIS COMPLETE — " << all_results.size() << " tests" << std::endl;
    std::cout << std::string(120, '=') << std::endl;

    return 0;
}
