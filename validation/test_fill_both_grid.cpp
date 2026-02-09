/**
 * test_fill_both_grid.cpp — Comprehensive fill_both grid retest
 *
 * Tests ALL commodity instruments with fill_both grid under controlled conditions:
 * - Zero swap (isolate pure grid profit)
 * - Fixed min lot size (no flukey max-lot spikes)
 * - $10M starting balance (every grid level fills)
 * - Track peak metrics for fair comparison
 *
 * Sweeps ~50 spacing values × 3 modes (LONG, SHORT, BOTH) per instrument.
 * Uses thread pool (16 threads) to parallelize configs within each instrument.
 */

#include "../include/tick_based_engine.h"
#include <vector>
#include <string>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <chrono>
#include <fstream>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <functional>

using namespace backtest;

// ============================================================================
// Tick loading — standalone function (MT5 CSV: Time\tBid\tAsk\tLast\tVolume\tFlags)
// ============================================================================

std::vector<Tick> LoadTicks(const std::string& file_path) {
    std::vector<Tick> ticks;

    std::ifstream file(file_path);
    if (!file.is_open()) {
        std::cerr << "Failed to open: " << file_path << std::endl;
        return ticks;
    }

    std::string line;
    std::getline(file, line); // Skip header

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        Tick tick;
        size_t pos = 0;

        // Timestamp (full datetime in first column)
        pos = line.find('\t');
        if (pos == std::string::npos) continue;
        tick.timestamp = line.substr(0, pos);
        line = line.substr(pos + 1);

        // Bid
        pos = line.find('\t');
        if (pos == std::string::npos) continue;
        tick.bid = std::stod(line.substr(0, pos));
        line = line.substr(pos + 1);

        // Ask
        pos = line.find('\t');
        if (pos == std::string::npos) pos = line.length();
        tick.ask = std::stod(line.substr(0, pos));

        if (tick.bid > 0 && tick.ask > 0) {
            ticks.push_back(tick);
        }
    }

    return ticks;
}

// ============================================================================
// FillBothGrid strategy — mirrors example/fill_both/fill_both.mq5
// ============================================================================

enum class GridMode { LONG_ONLY, SHORT_ONLY, BOTH };

const char* GridModeName(GridMode m) {
    switch (m) {
        case GridMode::LONG_ONLY:  return "LONG";
        case GridMode::SHORT_ONLY: return "SHORT";
        case GridMode::BOTH:       return "BOTH";
    }
    return "?";
}

struct GridState {
    double lowest   = DBL_MAX;
    double highest  = -DBL_MAX;
    double closest_above = DBL_MAX;
    double closest_below = DBL_MAX;
    double volume_open = 0.0;
};

class FillBothGrid {
public:
    FillBothGrid(GridMode mode, double spacing, double lot_size, double pip_size)
        : mode_(mode), spacing_(spacing), lot_size_(lot_size), pip_size_(pip_size) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        ScanPositions(engine, tick.ask, tick.bid);
        if (mode_ != GridMode::SHORT_ONLY) DoBuyLogic(tick, engine);
        if (mode_ != GridMode::LONG_ONLY)  DoSellLogic(tick, engine);
    }

private:
    GridMode mode_;
    double spacing_;
    double lot_size_;
    double pip_size_;
    GridState buy_, sell_;

    void ScanPositions(TickBasedEngine& engine, double ask, double bid) {
        buy_  = {DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX, 0.0};
        sell_ = {DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX, 0.0};

        for (const Trade* pos : engine.GetOpenPositions()) {
            double price = pos->entry_price;
            double lots  = pos->lot_size;

            if (pos->IsBuy()) {
                buy_.volume_open += lots;
                if (price < buy_.lowest)  buy_.lowest  = price;
                if (price > buy_.highest) buy_.highest = price;
                if (price >= ask) buy_.closest_above = std::min(buy_.closest_above, price - ask);
                if (price <= ask) buy_.closest_below = std::min(buy_.closest_below, ask - price);
            } else {
                sell_.volume_open += lots;
                if (price < sell_.lowest)  sell_.lowest  = price;
                if (price > sell_.highest) sell_.highest = price;
                if (price >= bid) sell_.closest_above = std::min(sell_.closest_above, price - bid);
                if (price <= bid) sell_.closest_below = std::min(sell_.closest_below, bid - price);
            }
        }
    }

    void DoBuyLogic(const Tick& tick, TickBasedEngine& engine) {
        double ask = tick.ask;
        if (buy_.volume_open == 0) {
            // First buy
            if (OpenBuy(ask, tick.bid, engine)) {
                buy_.highest = ask;
                buy_.lowest  = ask;
            }
        } else {
            // Price dropped below grid -> Buy lower
            if (buy_.lowest >= ask + spacing_) {
                OpenBuy(ask, tick.bid, engine);
            }
            // Price rose above grid -> Buy higher
            else if (buy_.highest <= ask - spacing_) {
                OpenBuy(ask, tick.bid, engine);
            }
            // Fill internal gap
            else if (buy_.closest_above >= spacing_ && buy_.closest_below >= spacing_) {
                OpenBuy(ask, tick.bid, engine);
            }
        }
    }

    void DoSellLogic(const Tick& tick, TickBasedEngine& engine) {
        double bid = tick.bid;
        if (sell_.volume_open == 0) {
            // First sell
            if (OpenSell(bid, tick.ask, engine)) {
                sell_.highest = bid;
                sell_.lowest  = bid;
            }
        } else {
            // Price rose above grid -> Sell higher
            if (sell_.highest <= bid - spacing_) {
                OpenSell(bid, tick.ask, engine);
            }
            // Price dropped below grid -> Sell lower
            else if (sell_.lowest >= bid + spacing_) {
                OpenSell(bid, tick.ask, engine);
            }
            // Fill internal gap
            else if (sell_.closest_above >= spacing_ && sell_.closest_below >= spacing_) {
                OpenSell(bid, tick.ask, engine);
            }
        }
    }

    bool OpenBuy(double ask, double bid, TickBasedEngine& engine) {
        double spread = ask - bid;
        double tp = ask + spread + spacing_;
        return engine.OpenMarketOrder(TradeDirection::BUY, lot_size_, 0.0, tp) != nullptr;
    }

    bool OpenSell(double bid, double ask, TickBasedEngine& engine) {
        double spread = ask - bid;
        double tp = bid - spread - spacing_;
        return engine.OpenMarketOrder(TradeDirection::SELL, lot_size_, 0.0, tp) != nullptr;
    }
};

// ============================================================================
// Instrument configuration — populated from query_all_margin_specs.py output
// ============================================================================

struct InstrumentConfig {
    const char* symbol;
    const char* broker;
    const char* tick_file;
    double contract_size;
    double pip_size;
    double min_lot;
    double volume_step;
    int digits;
    double leverage;
    TradeCalcMode calc_mode;
    double margin_rate;
    double margin_initial_fixed;
};

const InstrumentConfig INSTRUMENTS[] = {
    // Broker commodities (calc_mode=CFD for all)
    {"CL-OIL",    "Broker", "Broker/CL-OIL_TICKS_FULL.csv",    1000.0, 0.001,   0.01, 0.01, 3, 500.0, TradeCalcMode::CFD, 0.00199918, 0.00},
    {"UKOUSD",    "Broker", "Broker/UKOUSD_TICKS_FULL.csv",     1000.0, 0.001,   0.01, 0.01, 3, 500.0, TradeCalcMode::CFD, 0.00200547, 0.00},
    {"UKOUSDft",  "Broker", "Broker/UKOUSDft_TICKS_FULL.csv",   1000.0, 0.001,   0.01, 0.01, 3, 500.0, TradeCalcMode::CFD, 0.00200185, 0.00},
    {"USOUSD",    "Broker", "Broker/USOUSD_TICKS_FULL.csv",     1000.0, 0.001,   0.01, 0.01, 3, 500.0, TradeCalcMode::CFD, 0.00199849, 0.00},
    {"NG-C",      "Broker", "Broker/NG-C_TICKS_FULL.csv",      10000.0, 0.001,   0.10, 0.10, 3, 500.0, TradeCalcMode::CFD, 0.05000000, 0.00},
    {"GASOIL-C",  "Broker", "Broker/GASOIL-C_TICKS_FULL.csv",    100.0, 0.01,    0.10, 0.10, 2, 500.0, TradeCalcMode::CFD, 0.05000072, 0.00},
    {"GAS-C",     "Broker", "Broker/GAS-C_TICKS_FULL.csv",     42000.0, 0.0001,  0.10, 0.10, 4, 500.0, TradeCalcMode::CFD, 0.05000000, 0.00},
    {"Soybean-C", "Broker", "Broker/Soybean-C_TICKS_FULL.csv",   500.0, 0.001,   0.10, 0.10, 3, 500.0, TradeCalcMode::CFD, 0.04999549, 0.00},
    {"Sugar-C",   "Broker", "Broker/Sugar-C_TICKS_FULL.csv",  112000.0, 0.00001, 0.10, 0.10, 5, 500.0, TradeCalcMode::CFD, 0.05000000, 0.00},
    {"COPPER-C",  "Broker", "Broker/COPPER-C_TICKS_FULL.csv",  25000.0, 0.0001,  0.10, 0.10, 4, 500.0, TradeCalcMode::CFD, 0.02000034, 0.00},
    {"Cotton-C",  "Broker", "Broker/Cotton-C_TICKS_FULL.csv",  50000.0, 0.00001, 0.10, 0.10, 5, 500.0, TradeCalcMode::CFD, 0.05000164, 0.00},
    {"Cocoa-C",   "Broker", "Broker/Cocoa-C_TICKS_FULL.csv",      10.0, 0.1,     0.10, 0.10, 1, 500.0, TradeCalcMode::CFD, 0.05000119, 0.00},
    {"Coffee-C",  "Broker", "Broker/Coffee-C_TICKS_FULL.csv",  37500.0, 0.0001,  0.10, 0.10, 4, 500.0, TradeCalcMode::CFD, 0.05000022, 0.00},
    {"OJ-C",      "Broker", "Broker/OJ-C_TICKS_FULL.csv",     15000.0, 0.0001,  0.10, 0.10, 4, 500.0, TradeCalcMode::CFD, 0.05000102, 0.00},
    {"Wheat-C",   "Broker", "Broker/Wheat-C_TICKS_FULL.csv",    1000.0, 0.001,   0.10, 0.10, 3, 500.0, TradeCalcMode::CFD, 0.05000000, 0.00},
    {"XPTUSD",    "Broker", "Broker/XPTUSD_TICKS_FULL.csv",       10.0, 0.01,    0.10, 0.10, 2, 500.0, TradeCalcMode::CFD, 0.05000118, 0.00},
    {"XPDUSD",    "Broker", "Broker/XPDUSD_TICKS_FULL.csv",       10.0, 0.01,    0.10, 0.10, 2, 500.0, TradeCalcMode::CFD, 0.05000000, 0.00},
    // Grid — FOREX mode instruments
    {"XBRUSD",    "Grid", "Grid/XBRUSD_TICKS_FULL.csv",     1000.0, 0.001,   0.01, 0.01, 3, 500.0, TradeCalcMode::FOREX, 68.50000000, 0.00},
    {"XTIUSD",    "Grid", "Grid/XTIUSD_TICKS_FULL.csv",     1000.0, 0.001,   0.01, 0.01, 3, 500.0, TradeCalcMode::FOREX, 63.50000000, 0.00},
    {"XCUUSD",    "Grid", "Grid/XCUUSD_TICKS_FULL.csv",      100.0, 0.001,   0.01, 0.01, 3, 500.0, TradeCalcMode::FOREX, 65370.00000000, 0.00},
    {"XALUSD",    "Grid", "Grid/XALUSD_TICKS_FULL.csv",      100.0, 0.001,   0.01, 0.01, 3, 500.0, TradeCalcMode::FOREX, 15575.00000000, 0.00},
    {"XPTUSD-F",  "Grid", "Grid/XPTUSD_TICKS_FULL.csv",      100.0, 0.01,    0.01, 0.01, 2, 500.0, TradeCalcMode::FOREX, 2115.00000000, 0.00},
    {"XAUUSD",    "Grid", "Grid/XAUUSD_TICKS_2025.csv",      100.0, 0.01,    0.01, 0.01, 2, 500.0, TradeCalcMode::FOREX, 4965.00000000, 0.00},
    {"XAGUSD",    "Grid", "Grid/XAGUSD_TICKS_FULL.csv",     5000.0, 0.001,   0.01, 0.01, 3, 500.0, TradeCalcMode::FOREX, 78.00000000, 0.00},
    // Grid — FOREX_NO_LEVERAGE
    {"XNGUSD",    "Grid", "Grid/XNGUSD_TICKS_FULL.csv",    10000.0, 0.0001,  0.01, 0.01, 4, 500.0, TradeCalcMode::FOREX_NO_LEVERAGE, 0.08860000, 0.00},
    // Grid — CFD mode instruments
    {"CORN",      "Grid", "Grid/CORN_TICKS_FULL.csv",           2.0, 0.01,    1.00, 1.00, 2, 500.0, TradeCalcMode::CFD, 0.00999648, 0.00},
    {"COTTON",    "Grid", "Grid/COTTON_TICKS_FULL.csv",         10.0, 0.001,   1.00, 1.00, 3, 500.0, TradeCalcMode::CFD, 0.01000330, 0.00},
    {"SOYBEAN",   "Grid", "Grid/SOYBEAN_TICKS_FULL.csv",        1.0, 0.01,    1.00, 1.00, 2, 500.0, TradeCalcMode::CFD, 0.00999765, 0.00},
    {"SUGARRAW",  "Grid", "Grid/SUGARRAW_TICKS_FULL.csv",      50.0, 0.001,   1.00, 1.00, 3, 500.0, TradeCalcMode::CFD, 0.01000713, 0.00},
    {"WHEAT",     "Grid", "Grid/WHEAT_TICKS_FULL.csv",          1.0, 0.01,    1.00, 1.00, 2, 500.0, TradeCalcMode::CFD, 0.01000762, 0.00},
    {"COFARA",    "Grid", "Grid/COFARA_TICKS_FULL.csv",         10.0, 0.01,    1.00, 1.00, 2, 500.0, TradeCalcMode::CFD, 0.00999870, 0.00},
    {"COFROB",    "Grid", "Grid/COFROB_TICKS_FULL.csv",          1.0, 0.01,    1.00, 1.00, 2, 500.0, TradeCalcMode::CFD, 0.01000049, 0.00},
    {"OJ",        "Grid", "Grid/OJ_TICKS_FULL.csv",             10.0, 0.01,    1.00, 1.00, 2, 500.0, TradeCalcMode::CFD, 0.01000244, 0.00},
    {"SUGAR",     "Grid", "Grid/SUGAR_TICKS_FULL.csv",           2.0, 0.01,    1.00, 1.00, 2, 500.0, TradeCalcMode::CFD, 0.01000146, 0.00},
    {"UKCOCOA",   "Grid", "Grid/UKCOCOA_TICKS_FULL.csv",         1.0, 0.01,    1.00, 1.00, 2, 500.0, TradeCalcMode::CFD, 0.01361336, 0.00},
    {"USCOCOA",   "Grid", "Grid/USCOCOA_TICKS_FULL.csv",         1.0, 0.01,    1.00, 1.00, 2, 500.0, TradeCalcMode::CFD, 0.00999978, 0.00},
};
constexpr size_t NUM_INSTRUMENTS = sizeof(INSTRUMENTS) / sizeof(INSTRUMENTS[0]);

// ============================================================================
// Spacing generation — logspace from small to max $1.00 (cap at 50% of price)
// ============================================================================

std::vector<double> GenerateSpacings(double pip_size, double approx_price, int digits) {
    std::vector<double> spacings;

    // Max spacing = min($1.00, 50% of price)
    double max_spacing = std::min(1.0, approx_price * 0.5);

    // Min spacing = 10 * pip_size baseline
    double min_spacing = pip_size * 10.0;

    // Ensure min < max
    if (min_spacing >= max_spacing) {
        min_spacing = max_spacing / 10.0;
    }

    // Use logarithmic spacing to cover wide range
    // ~50 steps in log space
    int num_steps = 50;
    double log_min = std::log10(min_spacing);
    double log_max = std::log10(max_spacing);
    double log_step = (log_max - log_min) / (num_steps - 1);

    for (int i = 0; i < num_steps; ++i) {
        double val = std::pow(10.0, log_min + i * log_step);
        // Round to instrument's precision
        double precision = pip_size;
        val = std::round(val / precision) * precision;
        if (val > 0 && (spacings.empty() || std::abs(val - spacings.back()) > precision * 0.5)) {
            spacings.push_back(val);
        }
    }

    return spacings;
}

// ============================================================================
// Result storage
// ============================================================================

struct TestResult {
    std::string symbol;
    double spacing;
    GridMode mode;
    double peak_balance_profit;  // peak_balance - initial
    double peak_equity_profit;   // peak_equity - initial
    double final_balance_profit; // final_balance - initial
    double max_used_funds;
    double max_used_margin;
    int max_open_positions;
    double efficiency;           // peak_balance_profit / max_used_funds
    bool stop_out;
};

// ============================================================================
// Worker task — one per (spacing, mode) config
// ============================================================================

struct WorkItem {
    double spacing;
    GridMode mode;
    const InstrumentConfig* inst;
    double initial_balance;
    int result_index;  // index into results array
};

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    const double INITIAL_BALANCE = 10000000.0;  // $10M
    const std::string BASE_PATH = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\";

    // Thread count — use all available cores minus 1 (for main thread + OS)
    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 8;
    if (num_threads > 1) num_threads -= 1;  // Leave 1 core for OS

    // Allow running a single instrument by name, or "--skip-metals" to skip heavy tick files
    std::string only_symbol = "";
    bool skip_metals = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--skip-metals") {
            skip_metals = true;
        } else {
            only_symbol = arg;
        }
    }

    // Metals to skip (huge tick files, very slow with tight spacings)
    auto is_metal = [](const char* sym) {
        std::string s = sym;
        return s == "XAUUSD" || s == "XAGUSD" || s == "XPTUSD" || s == "XPTUSD-F"
            || s == "XPDUSD" || s == "XCUUSD" || s == "XALUSD";
    };

    std::vector<TestResult> all_results;
    auto total_start = std::chrono::high_resolution_clock::now();

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "================================================================\n";
    std::cout << "  Fill-Both Grid Comprehensive Retest\n";
    std::cout << "  Balance: $" << INITIAL_BALANCE << "  Swap: OFF  Fixed lot\n";
    std::cout << "  Instruments: " << NUM_INSTRUMENTS << "  Threads: " << num_threads << "\n";
    std::cout << "================================================================\n" << std::endl;

    for (size_t inst_idx = 0; inst_idx < NUM_INSTRUMENTS; ++inst_idx) {
        const auto& inst = INSTRUMENTS[inst_idx];

        // Filter if single instrument requested
        if (!only_symbol.empty() && only_symbol != inst.symbol) continue;
        if (skip_metals && is_metal(inst.symbol)) continue;

        std::string tick_path = BASE_PATH + inst.tick_file;

        std::cout << "\n--- " << inst.symbol << " (" << inst.broker << ") ---" << std::endl;
        std::cout << "  Tick file: " << inst.tick_file << std::endl;
        std::cout << "  CS=" << inst.contract_size << " pip=" << inst.pip_size
                  << " lot=" << inst.min_lot << " lev=" << inst.leverage
                  << " mode=" << static_cast<int>(inst.calc_mode)
                  << " rate=" << inst.margin_rate << std::endl;

        // Load ticks once
        auto load_start = std::chrono::high_resolution_clock::now();

        std::vector<Tick> ticks;
        try {
            ticks = LoadTicks(tick_path);
        } catch (...) {
            std::cout << "  [SKIP] Failed to load tick data" << std::endl;
            continue;
        }

        if (ticks.empty()) {
            std::cout << "  [SKIP] No ticks loaded" << std::endl;
            continue;
        }

        auto load_end = std::chrono::high_resolution_clock::now();
        double load_ms = std::chrono::duration<double, std::milli>(load_end - load_start).count();

        // Get approximate price from first tick
        double approx_price = ticks[0].ask;
        std::cout << "  Loaded " << ticks.size() << " ticks in " << (load_ms / 1000.0) << "s"
                  << "  Price: ~" << approx_price << std::endl;

        // Generate spacings
        auto spacings = GenerateSpacings(inst.pip_size, approx_price, inst.digits);
        std::cout << "  Spacings: " << spacings.size() << " values ["
                  << spacings.front() << " .. " << spacings.back() << "]" << std::endl;

        GridMode modes[] = { GridMode::LONG_ONLY, GridMode::SHORT_ONLY, GridMode::BOTH };
        int total_configs = spacings.size() * 3;

        // Build work items
        std::vector<WorkItem> work_items;
        work_items.reserve(total_configs);
        for (double spacing : spacings) {
            for (GridMode mode : modes) {
                WorkItem wi;
                wi.spacing = spacing;
                wi.mode = mode;
                wi.inst = &inst;
                wi.initial_balance = INITIAL_BALANCE;
                wi.result_index = work_items.size();
                work_items.push_back(wi);
            }
        }

        // Pre-allocate results
        std::vector<TestResult> inst_results(total_configs);

        // Thread-safe progress counter
        std::atomic<int> done{0};
        std::atomic<int> work_idx{0};

        auto run_start = std::chrono::high_resolution_clock::now();

        // Worker function — pull work items from atomic counter
        auto worker = [&]() {
            while (true) {
                int idx = work_idx.fetch_add(1, std::memory_order_relaxed);
                if (idx >= total_configs) break;

                const auto& wi = work_items[idx];

                // Create engine config — each thread gets its own
                TickBacktestConfig config;
                config.symbol = wi.inst->symbol;
                config.initial_balance = wi.initial_balance;
                config.contract_size = wi.inst->contract_size;
                config.leverage = wi.inst->leverage;
                config.pip_size = wi.inst->pip_size;
                config.digits = wi.inst->digits;
                config.volume_min = wi.inst->min_lot;
                config.volume_max = wi.inst->min_lot;  // Force fixed lot
                config.volume_step = wi.inst->volume_step;
                config.margin_rate = wi.inst->margin_rate;
                config.trade_calc_mode = wi.inst->calc_mode;
                config.margin_initial_fixed = wi.inst->margin_initial_fixed;
                config.swap_long = 0.0;
                config.swap_short = 0.0;
                config.swap_mode = 0;  // Disable swap
                config.stop_out_level = 0.0;  // Never stop out
                config.verbose = false;
                config.track_equity_curve = false;

                // Create engine and strategy
                TickBasedEngine engine(config);
                FillBothGrid strategy(wi.mode, wi.spacing, wi.inst->min_lot, wi.inst->pip_size);

                // Run — ticks is shared read-only across all threads
                engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
                    strategy.OnTick(tick, eng);
                });

                auto results = engine.GetResults();

                double peak_bal_profit = results.peak_balance - wi.initial_balance;
                double peak_eq_profit  = results.peak_equity - wi.initial_balance;
                double final_bal_profit = results.final_balance - wi.initial_balance;
                double efficiency = (results.max_used_funds > 0)
                    ? peak_bal_profit / results.max_used_funds : 0.0;

                TestResult& tr = inst_results[wi.result_index];
                tr.symbol = wi.inst->symbol;
                tr.spacing = wi.spacing;
                tr.mode = wi.mode;
                tr.peak_balance_profit = peak_bal_profit;
                tr.peak_equity_profit = peak_eq_profit;
                tr.final_balance_profit = final_bal_profit;
                tr.max_used_funds = results.max_used_funds;
                tr.max_used_margin = results.max_used_margin;
                tr.max_open_positions = results.max_open_positions;
                tr.efficiency = efficiency;
                tr.stop_out = results.stop_out_occurred;

                int completed = done.fetch_add(1, std::memory_order_relaxed) + 1;
                if (completed % 10 == 0 || completed == total_configs) {
                    auto now = std::chrono::high_resolution_clock::now();
                    double elapsed = std::chrono::duration<double>(now - run_start).count();
                    double per_config = elapsed / completed;
                    double eta = per_config * (total_configs - completed);
                    std::cerr << "  [" << completed << "/" << total_configs
                              << "] " << elapsed << "s elapsed, ~" << eta << "s remaining"
                              << std::endl;
                }
            }
        };

        // Launch threads
        std::vector<std::thread> threads;
        threads.reserve(num_threads);
        for (unsigned int t = 0; t < num_threads; ++t) {
            threads.emplace_back(worker);
        }

        // Wait for all threads
        for (auto& t : threads) {
            t.join();
        }

        auto run_end = std::chrono::high_resolution_clock::now();
        double run_s = std::chrono::duration<double>(run_end - run_start).count();
        std::cout << "  All " << total_configs << " configs completed in " << run_s << "s ("
                  << (run_s / total_configs) << "s/config avg)" << std::endl;

        // Print results table — sorted by spacing then mode
        std::cout << "\n  " << std::left << std::setw(10) << "Spacing"
                  << std::setw(6)  << "Mode"
                  << std::right
                  << std::setw(14) << "PeakBalProf"
                  << std::setw(14) << "PeakEqProf"
                  << std::setw(14) << "FinalBalProf"
                  << std::setw(14) << "MaxUsedFunds"
                  << std::setw(12) << "MaxMargin"
                  << std::setw(8)  << "MaxPos"
                  << std::setw(12) << "Efficiency"
                  << "\n";
        std::cout << "  " << std::string(104, '-') << "\n";

        for (const auto& tr : inst_results) {
            std::cout << "  " << std::left << std::setw(10) << tr.spacing
                      << std::setw(6)  << GridModeName(tr.mode)
                      << std::right
                      << std::setw(14) << tr.peak_balance_profit
                      << std::setw(14) << tr.peak_equity_profit
                      << std::setw(14) << tr.final_balance_profit
                      << std::setw(14) << tr.max_used_funds
                      << std::setw(12) << tr.max_used_margin
                      << std::setw(8)  << tr.max_open_positions
                      << std::setw(12) << tr.efficiency
                      << "\n";
        }
        std::cout << std::flush;

        // Add to global results
        all_results.insert(all_results.end(), inst_results.begin(), inst_results.end());
    }

    auto total_end = std::chrono::high_resolution_clock::now();
    double total_s = std::chrono::duration<double>(total_end - total_start).count();

    // ========================================================================
    // Grand summary — best config per instrument
    // ========================================================================
    std::cout << "\n\n================================================================\n";
    std::cout << "  GRAND SUMMARY — Best config per instrument (by efficiency)\n";
    std::cout << "================================================================\n\n";

    std::cout << std::left << std::setw(12) << "Symbol"
              << std::setw(10) << "Spacing"
              << std::setw(6)  << "Mode"
              << std::right
              << std::setw(14) << "PeakBalProf"
              << std::setw(14) << "MaxUsedFunds"
              << std::setw(12) << "Efficiency"
              << std::setw(8)  << "MaxPos"
              << "\n";
    std::cout << std::string(76, '-') << "\n";

    // Group by symbol, find best efficiency
    std::string current_symbol = "";
    const TestResult* best = nullptr;

    for (const auto& r : all_results) {
        if (r.symbol != current_symbol) {
            if (best != nullptr) {
                std::cout << std::left << std::setw(12) << best->symbol
                          << std::setw(10) << best->spacing
                          << std::setw(6)  << GridModeName(best->mode)
                          << std::right
                          << std::setw(14) << best->peak_balance_profit
                          << std::setw(14) << best->max_used_funds
                          << std::setw(12) << best->efficiency
                          << std::setw(8)  << best->max_open_positions
                          << "\n";
            }
            current_symbol = r.symbol;
            best = &r;
        } else {
            if (r.efficiency > best->efficiency) {
                best = &r;
            }
        }
    }
    // Print last
    if (best != nullptr) {
        std::cout << std::left << std::setw(12) << best->symbol
                  << std::setw(10) << best->spacing
                  << std::setw(6)  << GridModeName(best->mode)
                  << std::right
                  << std::setw(14) << best->peak_balance_profit
                  << std::setw(14) << best->max_used_funds
                  << std::setw(12) << best->efficiency
                  << std::setw(8)  << best->max_open_positions
                  << "\n";
    }

    std::cout << "\nTotal tests: " << all_results.size()
              << "  Total time: " << total_s << "s\n";

    return 0;
}
