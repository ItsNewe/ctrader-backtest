/**
 * FX Fill-Both Grid Test Suite
 *
 * Tests 16 FX pairs × 3 strategies (FillUp, FillDown, NaiveBidi)
 * to determine which forex pairs are suitable for bidirectional grid trading.
 *
 * All tests use ZERO swap (isolate pure grid profit).
 * FX pairs use pct_spacing = true (percentage of price).
 *
 * Period: 2025.01.01 - 2026.02.28
 * Initial balance: $10,000 | Leverage: 500
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
#include <map>

using namespace backtest;

// ============================================================================
// FX Instrument configurations — from live  query
// ============================================================================
struct FxConfig {
    std::string symbol;
    std::string broker;
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
    std::string category;  // "CHF", "JPY", "Cross"
};

static std::vector<FxConfig> GetFxInstruments() {
    return {
        // CHF-funded (long earns carry)
        {"AUDCHF",  "Broker", "AUD/CHF",  100000, 0.00001, 5,   3.54,  -8.16, 1, 3, 0.01, 0.01, "CHF"},
        {"USDCHF",  "Broker", "USD/CHF",  100000, 0.00001, 5,   4.73, -12.12, 1, 3, 0.01, 0.01, "CHF"},
        {"GBPCHF",  "Broker", "GBP/CHF",  100000, 0.00001, 5,   4.89, -19.06, 1, 3, 0.01, 0.01, "CHF"},
        {"NZDCHF",  "Broker", "NZD/CHF",  100000, 0.00001, 5,   1.89,  -5.91, 1, 3, 0.01, 0.01, "CHF"},
        {"CADCHF",  "Broker", "CAD/CHF",  100000, 0.00001, 5,   1.60,  -6.02, 1, 3, 0.01, 0.01, "CHF"},
        {"EURCHF",  "Broker", "EUR/CHF",  100000, 0.00001, 5,   1.95,  -9.58, 1, 3, 0.01, 0.01, "CHF"},
        // JPY-funded (long earns carry, higher vol)
        {"AUDJPY",  "Broker", "AUD/JPY",  100000, 0.001,   3,   2.04, -10.70, 1, 3, 0.01, 0.01, "JPY"},
        {"USDJPY",  "Broker", "USD/JPY",  100000, 0.001,   3,   7.28, -16.73, 1, 3, 0.01, 0.01, "JPY"},
        {"GBPJPY",  "Broker", "GBP/JPY",  100000, 0.001,   3,   5.79, -23.04, 1, 3, 0.01, 0.01, "JPY"},
        {"NZDJPY",  "Broker", "NZD/JPY",  100000, 0.001,   3,   0.91,  -7.01, 1, 3, 0.01, 0.01, "JPY"},
        {"EURJPY",  "Broker", "EUR/JPY",  100000, 0.001,   3,   1.48,  -9.67, 1, 3, 0.01, 0.01, "JPY"},
        {"CADJPY",  "Broker", "CAD/JPY",  100000, 0.001,   3,   1.78,  -6.69, 1, 3, 0.01, 0.01, "JPY"},
        // Cross pairs (range-bound champions)
        {"AUDNZD",  "Broker", "AUD/NZD",  100000, 0.00001, 5,   0.54,  -5.78, 1, 3, 0.01, 0.01, "Cross"},
        {"EURGBP",  "Broker", "EUR/GBP",  100000, 0.00001, 5,  -7.84,   1.18, 1, 3, 0.01, 0.01, "Cross"},
        {"AUDCAD",  "Broker", "AUD/CAD",  100000, 0.00001, 5,   1.82,  -6.44, 1, 3, 0.01, 0.01, "Cross"},
        {"NZDCAD",  "Broker", "NZD/CAD",  100000, 0.00001, 5,  -0.52,  -3.43, 1, 3, 0.01, 0.01, "Cross"},
    };
}

// ============================================================================
// Tick data loader
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

    dest.reserve(35000000);  // FX pairs can have 30M+ ticks

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
            continue;
        }
        tick.volume = 0;

        dest.push_back(tick);
    }

    auto end_t = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end_t - start);
    std::cout << " " << dest.size() << " ticks in " << duration.count() << "s" << std::endl;
}

// ============================================================================
// Engine config factory — FX pairs
// ============================================================================
TickBacktestConfig MakeEngineConfig(const FxConfig& inst, double initial_balance) {
    TickDataConfig tick_config;
    tick_config.file_path = "";
    tick_config.format = TickDataFormat::MT5_CSV;

    TickBacktestConfig config;
    config.symbol = inst.symbol;
    config.initial_balance = initial_balance;
    config.contract_size = inst.contract_size;  // 100,000 for FX
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
// FX strategy config — percentage-based spacing (universal for FX)
// ============================================================================
FillUpOscillation::Config MakeFillUpConfig(const FxConfig& inst) {
    FillUpOscillation::Config cfg;
    cfg.contract_size = inst.contract_size;
    cfg.leverage = 500.0;
    cfg.min_volume = inst.volume_min;
    cfg.mode = FillUpOscillation::ADAPTIVE_SPACING;
    cfg.adaptive.pct_spacing = true;

    // FX-specific parameters
    // FX pairs oscillate in tighter ranges than commodities
    // Typical daily range: 0.3-0.8% for crosses, 0.5-1.5% for JPY pairs

    if (inst.category == "JPY") {
        // JPY pairs: higher volatility, wider spacing
        cfg.survive_pct = 25.0;
        cfg.base_spacing = 1.5;          // 1.5% of price
        cfg.volatility_lookback_hours = 4.0;
        cfg.adaptive.typical_vol_pct = 0.8;
        cfg.max_volume = 0.5;
    } else if (inst.category == "CHF") {
        // CHF pairs: moderate volatility
        cfg.survive_pct = 20.0;
        cfg.base_spacing = 1.0;          // 1.0% of price
        cfg.volatility_lookback_hours = 4.0;
        cfg.adaptive.typical_vol_pct = 0.5;
        cfg.max_volume = 0.5;
    } else {
        // Cross pairs (AUDNZD, EURGBP, etc.): tight range, best for grid
        cfg.survive_pct = 15.0;
        cfg.base_spacing = 0.8;          // 0.8% of price
        cfg.volatility_lookback_hours = 4.0;
        cfg.adaptive.typical_vol_pct = 0.4;
        cfg.max_volume = 0.5;
    }

    cfg.safety.equity_stop_pct = 60.0;
    cfg.safety.force_min_volume_entry = false;
    cfg.safety.max_positions = 80;
    return cfg;
}

FillDownOscillation::Config MakeFillDownConfig(const FxConfig& inst) {
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
    std::string category;
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
TestResult RunTest(const std::string& strategy_name, const FxConfig& inst,
                   const std::vector<Tick>& ticks, double initial_balance,
                   std::function<void(const Tick&, TickBasedEngine&)> strategy_fn) {

    auto engine_cfg = MakeEngineConfig(inst, initial_balance);
    TickBasedEngine engine(engine_cfg);

    engine.RunWithTicks(ticks, strategy_fn);

    auto results = engine.GetResults();

    TestResult r;
    r.strategy = strategy_name;
    r.symbol = inst.symbol;
    r.category = inst.category;
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
// MAIN
// ============================================================================
int main() {
    double initial_balance = 10000.0;
    auto instruments = GetFxInstruments();
    std::string data_dir = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Broker\\";

    std::cout << std::string(120, '=') << std::endl;
    std::cout << "  FX FILL-BOTH GRID TEST" << std::endl;
    std::cout << "  " << instruments.size() << " FX Pairs × 3 Strategies (FillUp, FillDown, NaiveBidi)" << std::endl;
    std::cout << "  Zero swap — pure grid profit | $" << initial_balance << " | 500 leverage" << std::endl;
    std::cout << "  Period: 2025.01.01 - 2026.02.28" << std::endl;
    std::cout << std::string(120, '=') << std::endl;

    std::vector<TestResult> all_results;
    all_results.reserve(instruments.size() * 3);

    int test_num = 0;
    int total_tests = 0;

    for (const auto& inst : instruments) {
        std::string path = data_dir + inst.symbol + "_TICKS_FULL.csv";
        if (std::filesystem::exists(path)) total_tests++;
    }
    total_tests *= 3;

    for (const auto& inst : instruments) {
        std::string path = data_dir + inst.symbol + "_TICKS_FULL.csv";

        if (!std::filesystem::exists(path)) {
            std::cout << "\n  [SKIP] " << inst.symbol << " — no tick data" << std::endl;
            continue;
        }

        std::vector<Tick> ticks;
        LoadTickData(path, ticks, inst.symbol);

        if (ticks.empty()) {
            std::cout << "  [SKIP] " << inst.symbol << " — no ticks loaded" << std::endl;
            continue;
        }

        std::cout << "\n  --- " << inst.symbol << " (" << inst.category
                  << ") | " << inst.description << " ---" << std::endl;

        // FillUp (BUY grid)
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
                      << (result.stop_out ? " [STOP-OUT]" : "")
                      << std::endl << std::flush;
            all_results.push_back(result);
        } catch (const std::exception& e) {
            std::cout << "  [" << test_num << "] FillUp    ERROR: " << e.what() << std::endl;
        }

        // FillDown (SELL grid)
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
                      << (result.stop_out ? " [STOP-OUT]" : "")
                      << std::endl << std::flush;
            all_results.push_back(result);
        } catch (const std::exception& e) {
            std::cout << "  [" << test_num << "] FillDown  ERROR: " << e.what() << std::endl;
        }

        // NaiveBidi (both grids)
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
                      << (result.stop_out ? " [STOP-OUT]" : "")
                      << std::endl << std::flush;
            all_results.push_back(result);
        } catch (const std::exception& e) {
            std::cout << "  [" << test_num << "] NaiveBidi ERROR: " << e.what() << std::endl;
        }

        ticks.clear();
        ticks.shrink_to_fit();
    }

    // ========================================================================
    // SUMMARY TABLE
    // ========================================================================
    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "  SUMMARY TABLE — FX Fill-Both Grid (Zero Swap)" << std::endl;
    std::cout << std::string(120, '=') << std::endl;

    std::cout << std::left
              << std::setw(10) << "Symbol"
              << std::setw(8) << "Cat"
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
                  << std::setw(10) << r.symbol
                  << std::setw(8) << r.category
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
    // BEST STRATEGY PER PAIR + FILL_BOTH SUITABILITY
    // ========================================================================
    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "  FILL_BOTH SUITABILITY RANKING" << std::endl;
    std::cout << std::string(120, '=') << std::endl;

    std::cout << std::left
              << std::setw(10) << "Symbol"
              << std::setw(8) << "Cat"
              << std::right
              << std::setw(10) << "FillUp"
              << std::setw(10) << "FillDown"
              << std::setw(10) << "NaiveBi"
              << std::setw(10) << "NaiBi DD"
              << std::setw(10) << "RiskAdj"
              << "  Swap/day/lot  Verdict"
              << std::endl;
    std::cout << std::string(120, '-') << std::endl;

    // Collect per-pair data
    struct PairSummary {
        std::string symbol, category;
        double fillup_ret = 0, filldown_ret = 0, naive_ret = 0;
        double fillup_dd = 0, filldown_dd = 0, naive_dd = 0;
        bool fillup_so = false, filldown_so = false, naive_so = false;
        double swap_long, swap_short;
    };

    std::vector<PairSummary> pairs;
    for (const auto& inst : instruments) {
        PairSummary ps;
        ps.symbol = inst.symbol;
        ps.category = inst.category;
        ps.swap_long = inst.swap_long;
        ps.swap_short = inst.swap_short;

        for (const auto& r : all_results) {
            if (r.symbol != inst.symbol) continue;
            if (r.strategy == "FillUp") {
                ps.fillup_ret = r.return_mult; ps.fillup_dd = r.max_dd_pct;
                ps.fillup_so = r.stop_out;
            } else if (r.strategy == "FillDown") {
                ps.filldown_ret = r.return_mult; ps.filldown_dd = r.max_dd_pct;
                ps.filldown_so = r.stop_out;
            } else if (r.strategy == "NaiveBidi") {
                ps.naive_ret = r.return_mult; ps.naive_dd = r.max_dd_pct;
                ps.naive_so = r.stop_out;
            }
        }
        pairs.push_back(ps);
    }

    // Sort by NaiveBidi risk-adjusted return (non-stop-out first)
    std::sort(pairs.begin(), pairs.end(), [](const PairSummary& a, const PairSummary& b) {
        if (a.naive_so != b.naive_so) return !a.naive_so;  // Non-stopout first
        double ra = a.naive_ret / (a.naive_dd / 100.0 + 0.01);
        double rb = b.naive_ret / (b.naive_dd / 100.0 + 0.01);
        return ra > rb;
    });

    for (const auto& ps : pairs) {
        if (ps.naive_ret == 0 && ps.fillup_ret == 0) continue;

        double risk_adj = ps.naive_ret / (ps.naive_dd / 100.0 + 0.01);

        std::string verdict;
        if (!ps.naive_so && ps.naive_ret > 1.5 &&
            ps.naive_ret > ps.fillup_ret && ps.naive_ret > ps.filldown_ret)
            verdict = "EXCELLENT for fill_both";
        else if (!ps.naive_so && ps.naive_ret > 1.2)
            verdict = "GOOD for fill_both";
        else if (!ps.naive_so && ps.naive_ret > 1.0)
            verdict = "MARGINAL";
        else if (ps.naive_so)
            verdict = "NOT SUITABLE (stop-out)";
        else
            verdict = "LOSS";

        auto fmt = [](double ret, bool so) -> std::string {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.2fx%s", ret, so ? "*" : "");
            return std::string(buf);
        };

        // Swap info: show which direction earns
        char swap_buf[64];
        snprintf(swap_buf, sizeof(swap_buf), "L:%+.1f S:%+.1f",
                 ps.swap_long, ps.swap_short);

        std::cout << std::fixed << std::setprecision(2)
                  << std::left
                  << std::setw(10) << ps.symbol
                  << std::setw(8) << ps.category
                  << std::right
                  << std::setw(10) << fmt(ps.fillup_ret, ps.fillup_so)
                  << std::setw(10) << fmt(ps.filldown_ret, ps.filldown_so)
                  << std::setw(10) << fmt(ps.naive_ret, ps.naive_so)
                  << std::setw(10) << ps.naive_dd
                  << std::setw(10) << risk_adj
                  << "  " << swap_buf << "  " << verdict
                  << std::endl;
    }

    // ========================================================================
    // CATEGORY SUMMARY
    // ========================================================================
    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "  CATEGORY SUMMARY" << std::endl;
    std::cout << std::string(120, '=') << std::endl;

    std::map<std::string, std::pair<int, int>> cat_stats;  // {total, viable}
    for (const auto& ps : pairs) {
        auto& cs = cat_stats[ps.category];
        cs.first++;
        if (!ps.naive_so && ps.naive_ret > 1.2) cs.second++;
    }

    for (const auto& [cat, stats] : cat_stats) {
        std::cout << "  " << cat << ": " << stats.second << "/" << stats.first
                  << " pairs viable for fill_both" << std::endl;
    }

    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "  ANALYSIS COMPLETE — " << all_results.size() << " tests" << std::endl;
    std::cout << std::string(120, '=') << std::endl;

    return 0;
}
