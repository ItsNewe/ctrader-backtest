/**
 * Multi-Instrument Grid Parallel Test
 * Tests "up while up" strategy on both XAUUSD and XAGUSD from same account
 * Uses shared tick data + parallel threads for speed
 * Period: 2025.01.01 to 2026.01.29
 */

#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>
#include <fstream>
#include <sstream>
#include <chrono>

using namespace backtest;

// Merged tick with symbol identifier
struct MergedTick {
    std::string timestamp;
    std::string symbol;
    double bid;
    double ask;
};

// Global shared tick data
std::vector<MergedTick> g_merged_ticks;
std::mutex g_output_mutex;
std::atomic<int> g_completed{0};

// Position tracking for multi-instrument
struct Position {
    std::string symbol;
    double entry_price;
    double lot_size;
    double contract_size;
};

struct InstrumentState {
    std::string symbol;
    double contract_size;
    double leverage;
    double survive_pct;
    double last_high;
    double total_volume;
    std::vector<Position> positions;
};

struct MultiInstrumentResult {
    double au_survive, ag_survive;
    double final_equity;
    double ret;
    double max_dd_pct;
    int au_entries, ag_entries;
    bool stop_out;
};

class MultiInstrumentEngine {
public:
    double initial_balance;
    double balance;
    double peak_equity;
    double max_dd_pct;
    bool stop_out;

    InstrumentState gold;
    InstrumentState silver;

    double min_volume = 0.01;
    double max_volume = 10.0;
    double margin_stop_out = 20.0;

    MultiInstrumentEngine(double init_bal, double au_survive, double ag_survive) {
        initial_balance = init_bal;
        balance = init_bal;
        peak_equity = init_bal;
        max_dd_pct = 0;
        stop_out = false;

        gold.symbol = "XAUUSD";
        gold.contract_size = 100.0;
        gold.leverage = 500.0;
        gold.survive_pct = au_survive;
        gold.last_high = 0;
        gold.total_volume = 0;

        silver.symbol = "XAGUSD";
        silver.contract_size = 5000.0;
        silver.leverage = 500.0;
        silver.survive_pct = ag_survive;
        silver.last_high = 0;
        silver.total_volume = 0;
    }

    double GetEquity(double au_price, double ag_price) {
        double unrealized = 0;
        for (const auto& p : gold.positions) {
            unrealized += (au_price - p.entry_price) * p.lot_size * p.contract_size;
        }
        for (const auto& p : silver.positions) {
            unrealized += (ag_price - p.entry_price) * p.lot_size * p.contract_size;
        }
        return balance + unrealized;
    }

    double GetUsedMargin(double au_price, double ag_price) {
        double margin = 0;
        for (const auto& p : gold.positions) {
            margin += p.lot_size * p.contract_size * au_price / gold.leverage;
        }
        for (const auto& p : silver.positions) {
            margin += p.lot_size * p.contract_size * ag_price / silver.leverage;
        }
        return margin;
    }

    double CalculateLotSize(InstrumentState& inst, double price, double equity, double used_margin) {
        double end_price = price * (1.0 - inst.survive_pct / 100.0);
        double distance = price - end_price;

        double current_loss_at_target = 0;
        for (const auto& p : inst.positions) {
            current_loss_at_target += (p.entry_price - end_price) * p.lot_size * p.contract_size;
        }

        double equity_at_target = equity - current_loss_at_target;
        if (equity_at_target <= 0) return 0;

        double margin_per_lot = price * inst.contract_size / inst.leverage;
        double loss_per_lot = distance * inst.contract_size;

        double target_margin_level = margin_stop_out * 1.5;
        double required_reserve = used_margin * target_margin_level / 100.0;
        double available = equity_at_target - required_reserve;

        if (available <= 0) return 0;

        double cost_per_lot = loss_per_lot + margin_per_lot * target_margin_level / 100.0;
        if (cost_per_lot <= 0) return min_volume;

        double lot = available / cost_per_lot;
        lot *= 0.05;  // Scale down

        lot = std::floor(lot / min_volume) * min_volume;
        lot = std::max(lot, 0.0);
        lot = std::min(lot, max_volume);

        return lot;
    }

    void ProcessTick(const std::string& symbol, double price) {
        if (stop_out) return;

        InstrumentState& inst = (symbol == "XAUUSD") ? gold : silver;
        double other_price = (symbol == "XAUUSD") ?
            (silver.positions.empty() ? 30.0 : silver.last_high) :
            (gold.positions.empty() ? 2600.0 : gold.last_high);

        double au_price = (symbol == "XAUUSD") ? price : other_price;
        double ag_price = (symbol == "XAGUSD") ? price : other_price;

        double equity = GetEquity(au_price, ag_price);
        double used_margin = GetUsedMargin(au_price, ag_price);

        // Check margin level
        if (used_margin > 0) {
            double margin_level = (equity / used_margin) * 100.0;
            if (margin_level < margin_stop_out) {
                stop_out = true;
                return;
            }
        }

        // Track peak and drawdown
        if (equity > peak_equity) peak_equity = equity;
        double dd = (peak_equity - equity) / peak_equity * 100.0;
        if (dd > max_dd_pct) max_dd_pct = dd;

        // "Up while up" - open on new high
        if (price > inst.last_high) {
            inst.last_high = price;

            double lot = CalculateLotSize(inst, price, equity, used_margin);

            if (lot >= min_volume) {
                Position pos;
                pos.symbol = symbol;
                pos.entry_price = price;
                pos.lot_size = lot;
                pos.contract_size = inst.contract_size;

                inst.positions.push_back(pos);
                inst.total_volume += lot;
            }
        }
    }
};

void LoadTickData() {
    auto start = std::chrono::high_resolution_clock::now();

    // Load Gold
    std::cout << "Loading XAUUSD..." << std::endl;
    std::ifstream au_file("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv");
    if (!au_file.is_open()) {
        std::cerr << "Cannot open XAUUSD file!" << std::endl;
        return;
    }

    std::string line;
    std::getline(au_file, line);  // Skip header

    while (std::getline(au_file, line)) {
        if (line.empty()) continue;

        MergedTick tick;
        std::stringstream ss(line);
        std::string bid_str, ask_str;

        std::getline(ss, tick.timestamp, '\t');
        std::getline(ss, bid_str, '\t');
        std::getline(ss, ask_str, '\t');

        tick.symbol = "XAUUSD";
        tick.bid = std::stod(bid_str);
        tick.ask = std::stod(ask_str);

        g_merged_ticks.push_back(tick);
    }
    au_file.close();
    std::cout << "  XAUUSD: " << g_merged_ticks.size() << " ticks" << std::endl;

    size_t au_count = g_merged_ticks.size();

    // Load Silver
    std::cout << "Loading XAGUSD..." << std::endl;
    std::ifstream ag_file("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\XAGUSD\\XAGUSD_TICKS_2025.csv");
    if (!ag_file.is_open()) {
        std::cerr << "Cannot open XAGUSD file!" << std::endl;
        return;
    }

    std::getline(ag_file, line);  // Skip header

    while (std::getline(ag_file, line)) {
        if (line.empty()) continue;

        MergedTick tick;
        std::stringstream ss(line);
        std::string bid_str, ask_str;

        std::getline(ss, tick.timestamp, '\t');
        std::getline(ss, bid_str, '\t');
        std::getline(ss, ask_str, '\t');

        tick.symbol = "XAGUSD";
        tick.bid = std::stod(bid_str);
        tick.ask = std::stod(ask_str);

        g_merged_ticks.push_back(tick);
    }
    ag_file.close();
    std::cout << "  XAGUSD: " << g_merged_ticks.size() - au_count << " ticks" << std::endl;

    // Sort by timestamp
    std::cout << "Sorting by timestamp..." << std::endl;
    std::sort(g_merged_ticks.begin(), g_merged_ticks.end(),
              [](const MergedTick& a, const MergedTick& b) {
                  return a.timestamp < b.timestamp;
              });

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
    std::cout << "Total merged: " << g_merged_ticks.size() << " ticks in "
              << duration.count() << "s" << std::endl;
}

MultiInstrumentResult RunTest(double au_survive, double ag_survive) {
    MultiInstrumentEngine engine(10000.0, au_survive, ag_survive);

    double last_au_price = 0, last_ag_price = 0;

    for (const auto& tick : g_merged_ticks) {
        engine.ProcessTick(tick.symbol, tick.ask);

        if (tick.symbol == "XAUUSD") last_au_price = tick.bid;
        else last_ag_price = tick.bid;

        if (engine.stop_out) break;
    }

    MultiInstrumentResult result;
    result.au_survive = au_survive;
    result.ag_survive = ag_survive;
    result.stop_out = engine.stop_out;

    if (!engine.stop_out) {
        result.final_equity = engine.GetEquity(last_au_price, last_ag_price);
        result.ret = result.final_equity / 10000.0;
        result.max_dd_pct = engine.max_dd_pct;
        result.au_entries = engine.gold.positions.size();
        result.ag_entries = engine.silver.positions.size();
    } else {
        result.final_equity = 0;
        result.ret = 0;
        result.max_dd_pct = 100;
        result.au_entries = engine.gold.positions.size();
        result.ag_entries = engine.silver.positions.size();
    }

    return result;
}

void RunTestWorker(const std::vector<std::pair<double, double>>& params,
                   std::vector<MultiInstrumentResult>& results,
                   size_t start_idx, size_t end_idx) {
    for (size_t i = start_idx; i < end_idx; ++i) {
        results[i] = RunTest(params[i].first, params[i].second);
        int done = ++g_completed;

        std::lock_guard<std::mutex> lock(g_output_mutex);
        std::cout << "\r  " << done << "/" << params.size() << " completed" << std::flush;
    }
}

int main() {
    std::cout << "=== Multi-Instrument Grid Optimization (Parallel) ===" << std::endl;
    std::cout << "XAUUSD + XAGUSD from same account" << std::endl;
    std::cout << "Period: 2025.01.01 - 2026.01.29" << std::endl;
    std::cout << std::endl;

    LoadTickData();
    if (g_merged_ticks.empty()) {
        std::cerr << "No ticks loaded!" << std::endl;
        return 1;
    }

    // Parameter combinations
    std::vector<double> au_survives = {8, 10, 12, 15, 18, 20, 25};
    std::vector<double> ag_survives = {15, 18, 20, 22, 25, 28, 30};

    std::vector<std::pair<double, double>> params;
    for (double au : au_survives) {
        for (double ag : ag_survives) {
            params.push_back({au, ag});
        }
    }

    std::vector<MultiInstrumentResult> results(params.size());

    // Determine number of threads
    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;
    num_threads = std::min(num_threads, (unsigned int)params.size());

    std::cout << "\nRunning " << params.size() << " combinations with "
              << num_threads << " threads..." << std::endl;

    auto start = std::chrono::high_resolution_clock::now();

    // Divide work among threads
    std::vector<std::thread> threads;
    size_t chunk_size = params.size() / num_threads;
    size_t remainder = params.size() % num_threads;

    size_t start_idx = 0;
    for (unsigned int t = 0; t < num_threads; ++t) {
        size_t end_idx = start_idx + chunk_size + (t < remainder ? 1 : 0);
        threads.emplace_back(RunTestWorker, std::ref(params), std::ref(results),
                           start_idx, end_idx);
        start_idx = end_idx;
    }

    for (auto& thread : threads) {
        thread.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
    std::cout << "\nCompleted in " << duration.count() << " seconds" << std::endl;

    // Sort by return
    std::sort(results.begin(), results.end(),
              [](const MultiInstrumentResult& a, const MultiInstrumentResult& b) {
                  if (a.stop_out != b.stop_out) return !a.stop_out;
                  return a.ret > b.ret;
              });

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n=== TOP 15 CONFIGURATIONS ===" << std::endl;
    std::cout << "AU_surv  AG_surv  Return    DD%     AU_pos  AG_pos" << std::endl;
    std::cout << "-------  -------  --------  ------  ------  ------" << std::endl;

    for (int i = 0; i < 15 && i < (int)results.size(); i++) {
        const auto& r = results[i];
        if (r.stop_out) {
            std::cout << std::setw(5) << r.au_survive << "%   "
                      << std::setw(5) << r.ag_survive << "%   "
                      << "STOP-OUT" << std::endl;
        } else {
            std::cout << std::setw(5) << r.au_survive << "%   "
                      << std::setw(5) << r.ag_survive << "%   "
                      << std::setw(6) << r.ret << "x   "
                      << std::setw(5) << r.max_dd_pct << "%   "
                      << std::setw(5) << r.au_entries << "   "
                      << std::setw(5) << r.ag_entries << std::endl;
        }
    }

    // Find best risk-adjusted
    std::sort(results.begin(), results.end(),
              [](const MultiInstrumentResult& a, const MultiInstrumentResult& b) {
                  if (a.stop_out != b.stop_out) return !a.stop_out;
                  if (a.max_dd_pct == 0 || b.max_dd_pct == 0) return a.ret > b.ret;
                  return (a.ret / a.max_dd_pct) > (b.ret / b.max_dd_pct);
              });

    std::cout << "\n=== TOP 10 RISK-ADJUSTED (Return/DD) ===" << std::endl;
    for (int i = 0; i < 10 && i < (int)results.size(); i++) {
        const auto& r = results[i];
        if (r.stop_out) continue;
        double ratio = r.max_dd_pct > 0 ? r.ret / r.max_dd_pct : 0;
        std::cout << std::setw(5) << r.au_survive << "%   "
                  << std::setw(5) << r.ag_survive << "%   "
                  << std::setw(6) << r.ret << "x   "
                  << std::setw(5) << r.max_dd_pct << "%   "
                  << "ratio=" << ratio << std::endl;
    }

    // Best overall
    std::sort(results.begin(), results.end(),
              [](const MultiInstrumentResult& a, const MultiInstrumentResult& b) {
                  if (a.stop_out != b.stop_out) return !a.stop_out;
                  return a.ret > b.ret;
              });

    if (!results.empty() && !results[0].stop_out) {
        const auto& best = results[0];
        std::cout << "\n=== BEST CONFIGURATION ===" << std::endl;
        std::cout << "XAUUSD survive_pct = " << best.au_survive << "%" << std::endl;
        std::cout << "XAGUSD survive_pct = " << best.ag_survive << "%" << std::endl;
        std::cout << "Result: " << best.ret << "x return, " << best.max_dd_pct << "% max DD" << std::endl;
        std::cout << "Positions: AU=" << best.au_entries << ", AG=" << best.ag_entries << std::endl;
    }

    return 0;
}
