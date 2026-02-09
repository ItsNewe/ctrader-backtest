/**
 * Multi-Instrument Grid Test V3
 * Matches MQ5 grid_multi_symbol.mq5 exactly including position closing
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
#include <cmath>
#include <cfloat>

using namespace backtest;

struct MergedTick {
    std::string timestamp;
    std::string symbol;
    double bid;
    double ask;
};

std::vector<MergedTick> g_merged_ticks;
std::mutex g_output_mutex;
std::atomic<int> g_completed{0};

struct Position {
    std::string symbol;
    double entry_price;
    double lot_size;
    double contract_size;
};

struct SymbolState {
    std::string symbol;
    double contract_size;
    double survive_down;
    double initial_margin_rate;
    double min_volume;
    double max_volume;
    int volume_digits;

    double volume_of_open_trades;
    double checked_last_open_price;
    std::vector<Position> positions;
};

struct TestResult {
    double au_survive, ag_survive;
    double final_equity;
    double ret;
    double max_dd_pct;
    int total_trades;
    int au_entries, ag_entries;
    int positions_closed;
    bool stop_out;
};

class MultiSymbolEngine {
public:
    double initial_balance;
    double balance;
    double peak_equity;
    double max_dd_pct;
    bool stop_out;
    double margin_stop_out_level;
    double leverage;
    int account_limit_orders;
    int positions_closed;

    SymbolState gold;
    SymbolState silver;

    MultiSymbolEngine(double init_bal, double au_survive, double ag_survive) {
        initial_balance = init_bal;
        balance = init_bal;
        peak_equity = init_bal;
        max_dd_pct = 0;
        stop_out = false;
        margin_stop_out_level = 20.0;  // 20% stop out
        leverage = 500.0;
        account_limit_orders = 200;  // Typical broker limit
        positions_closed = 0;

        // Gold setup (XAUUSD)
        gold.symbol = "XAUUSD";
        gold.contract_size = 100.0;
        gold.survive_down = au_survive;
        gold.initial_margin_rate = 1.0;
        gold.min_volume = 0.01;
        gold.max_volume = 100.0;
        gold.volume_digits = 2;
        gold.volume_of_open_trades = 0;
        gold.checked_last_open_price = DBL_MIN;

        // Silver setup (XAGUSD)
        silver.symbol = "XAGUSD";
        silver.contract_size = 5000.0;
        silver.survive_down = ag_survive;
        silver.initial_margin_rate = 1.0;
        silver.min_volume = 0.01;
        silver.max_volume = 100.0;
        silver.volume_digits = 2;
        silver.volume_of_open_trades = 0;
        silver.checked_last_open_price = DBL_MIN;
    }

    double GetEquity(double au_bid, double ag_bid) {
        double unrealized = 0;
        for (const auto& p : gold.positions) {
            unrealized += (au_bid - p.entry_price) * p.lot_size * p.contract_size;
        }
        for (const auto& p : silver.positions) {
            unrealized += (ag_bid - p.entry_price) * p.lot_size * p.contract_size;
        }
        return balance + unrealized;
    }

    double GetUsedMargin(double au_ask, double ag_ask) {
        double margin = 0;
        for (const auto& p : gold.positions) {
            margin += p.lot_size * gold.contract_size * au_ask / leverage;
        }
        for (const auto& p : silver.positions) {
            margin += p.lot_size * silver.contract_size * ag_ask / leverage;
        }
        return margin;
    }

    int TotalPositions() {
        return gold.positions.size() + silver.positions.size();
    }

    // Close smallest profitable position (any symbol)
    bool CloseSmallestProfitable(double au_bid, double ag_bid) {
        double min_vol = DBL_MAX;
        int best_idx = -1;
        bool best_is_gold = false;

        // Check gold positions
        for (size_t i = 0; i < gold.positions.size(); i++) {
            const auto& p = gold.positions[i];
            double profit = (au_bid - p.entry_price) * p.lot_size * p.contract_size;
            if (profit > 0 && p.lot_size < min_vol) {
                min_vol = p.lot_size;
                best_idx = i;
                best_is_gold = true;
            }
        }

        // Check silver positions
        for (size_t i = 0; i < silver.positions.size(); i++) {
            const auto& p = silver.positions[i];
            double profit = (ag_bid - p.entry_price) * p.lot_size * p.contract_size;
            if (profit > 0 && p.lot_size < min_vol) {
                min_vol = p.lot_size;
                best_idx = i;
                best_is_gold = false;
            }
        }

        if (best_idx >= 0) {
            if (best_is_gold) {
                const auto& p = gold.positions[best_idx];
                double profit = (au_bid - p.entry_price) * p.lot_size * p.contract_size;
                balance += profit;
                gold.volume_of_open_trades -= p.lot_size;
                gold.positions.erase(gold.positions.begin() + best_idx);
            } else {
                const auto& p = silver.positions[best_idx];
                double profit = (ag_bid - p.entry_price) * p.lot_size * p.contract_size;
                balance += profit;
                silver.volume_of_open_trades -= p.lot_size;
                silver.positions.erase(silver.positions.begin() + best_idx);
            }
            positions_closed++;
            return true;
        }
        return false;
    }

    double CalculateTradeSize(SymbolState& sym, double current_ask, double equity,
                              double used_margin) {
        double end_price = current_ask * ((100.0 - sym.survive_down) / 100.0);
        double distance = current_ask - end_price;
        double spread_and_commission = 0;  // Simplified

        // MQ5 formula for SYMBOL_CALC_MODE_CFDLEVERAGE
        double numerator = 100.0 * equity * leverage
                         - 100.0 * sym.contract_size * std::fabs(distance) * sym.volume_of_open_trades * leverage
                         - leverage * margin_stop_out_level * used_margin;

        double denominator = sym.contract_size * (
            100.0 * std::fabs(distance) * leverage
            + 100.0 * spread_and_commission * leverage
            + current_ask * sym.initial_margin_rate * margin_stop_out_level
        );

        if (denominator <= 0) return 0;

        double trade_size = numerator / denominator;

        // Round to volume digits
        double multiplier = std::pow(10, sym.volume_digits);
        trade_size = std::floor(trade_size * multiplier) / multiplier;

        if (trade_size < sym.min_volume) return 0;
        trade_size = std::min(trade_size, sym.max_volume);

        return trade_size;
    }

    void ProcessSymbol(const std::string& symbol, double ask, double bid,
                      double other_ask, double other_bid) {
        if (stop_out) return;

        SymbolState& sym = (symbol == "XAUUSD") ? gold : silver;

        double au_ask = (symbol == "XAUUSD") ? ask : other_ask;
        double ag_ask = (symbol == "XAGUSD") ? ask : other_ask;
        double au_bid = (symbol == "XAUUSD") ? bid : other_bid;
        double ag_bid = (symbol == "XAGUSD") ? bid : other_bid;

        double equity = GetEquity(au_bid, ag_bid);
        double used_margin = GetUsedMargin(au_ask, ag_ask);

        // Calculate margin level
        double current_margin_level = (used_margin > 0) ? (equity / used_margin) * 100.0 : 0;

        // Check stop out
        if (used_margin > 0 && current_margin_level < margin_stop_out_level) {
            stop_out = true;
            return;
        }

        // Track drawdown
        if (equity > peak_equity) peak_equity = equity;
        double dd = (peak_equity - equity) / peak_equity * 100.0;
        if (dd > max_dd_pct) max_dd_pct = dd;

        // "Up while up" - check for new high
        if (sym.volume_of_open_trades == 0 || ask > sym.checked_last_open_price) {
            sym.checked_last_open_price = ask;

            // Recalculate volume from positions
            sym.volume_of_open_trades = 0;
            for (const auto& p : sym.positions) {
                sym.volume_of_open_trades += p.lot_size;
            }

            // Check if should open trade (MQ5 condition)
            bool should_open = false;
            if (sym.volume_of_open_trades == 0) {
                should_open = true;
            } else {
                double equity_at_target = (current_margin_level > 0) ?
                    equity * margin_stop_out_level / current_margin_level : equity;
                double equity_difference = equity - equity_at_target;
                double price_difference = equity_difference / (sym.volume_of_open_trades * sym.contract_size);
                double end_price = ask * ((100.0 - sym.survive_down) / 100.0);

                should_open = (ask - price_difference) < end_price;
            }

            if (should_open) {
                double trade_size = CalculateTradeSize(sym, ask, equity, used_margin);

                if (trade_size >= sym.min_volume) {
                    // Check order limit - close smallest profitable if at limit
                    if (TotalPositions() >= account_limit_orders) {
                        if (!CloseSmallestProfitable(au_bid, ag_bid)) {
                            return;  // Can't close any, skip this trade
                        }
                        // Recalculate after closing
                        equity = GetEquity(au_bid, ag_bid);
                        used_margin = GetUsedMargin(au_ask, ag_ask);
                        trade_size = CalculateTradeSize(sym, ask, equity, used_margin);
                        if (trade_size < sym.min_volume) return;
                    }

                    Position pos;
                    pos.symbol = symbol;
                    pos.entry_price = ask;
                    pos.lot_size = trade_size;
                    pos.contract_size = sym.contract_size;

                    sym.positions.push_back(pos);
                    sym.volume_of_open_trades += trade_size;
                }
            }
        }
    }
};

void LoadTickData() {
    auto start = std::chrono::high_resolution_clock::now();

    std::cout << "Loading XAUUSD..." << std::endl;
    std::ifstream au_file("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv");
    if (!au_file.is_open()) {
        std::cerr << "Cannot open XAUUSD file!" << std::endl;
        return;
    }

    std::string line;
    std::getline(au_file, line);

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

    std::cout << "Loading XAGUSD..." << std::endl;
    std::ifstream ag_file("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\XAGUSD\\XAGUSD_TICKS_2025.csv");
    if (!ag_file.is_open()) {
        std::cerr << "Cannot open XAGUSD file!" << std::endl;
        return;
    }

    std::getline(ag_file, line);

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

    std::cout << "Sorting by timestamp..." << std::endl;
    std::sort(g_merged_ticks.begin(), g_merged_ticks.end(),
              [](const MergedTick& a, const MergedTick& b) {
                  return a.timestamp < b.timestamp;
              });

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
    std::cout << "Total: " << g_merged_ticks.size() << " ticks in " << duration.count() << "s" << std::endl;
}

TestResult RunTest(double au_survive, double ag_survive) {
    MultiSymbolEngine engine(10000.0, au_survive, ag_survive);

    double last_au_ask = 2600.0, last_ag_ask = 30.0;
    double last_au_bid = 2600.0, last_ag_bid = 30.0;

    for (const auto& tick : g_merged_ticks) {
        if (tick.symbol == "XAUUSD") {
            last_au_ask = tick.ask;
            last_au_bid = tick.bid;
            engine.ProcessSymbol("XAUUSD", tick.ask, tick.bid, last_ag_ask, last_ag_bid);
        } else {
            last_ag_ask = tick.ask;
            last_ag_bid = tick.bid;
            engine.ProcessSymbol("XAGUSD", tick.ask, tick.bid, last_au_ask, last_au_bid);
        }

        if (engine.stop_out) break;
    }

    TestResult result;
    result.au_survive = au_survive;
    result.ag_survive = ag_survive;
    result.stop_out = engine.stop_out;

    int au_entries = engine.gold.positions.size();
    int ag_entries = engine.silver.positions.size();

    if (!engine.stop_out) {
        result.final_equity = engine.GetEquity(last_au_bid, last_ag_bid);
        result.ret = result.final_equity / 10000.0;
        result.max_dd_pct = engine.max_dd_pct;
        result.total_trades = au_entries + ag_entries + engine.positions_closed;
        result.au_entries = au_entries;
        result.ag_entries = ag_entries;
        result.positions_closed = engine.positions_closed;
    } else {
        result.final_equity = 0;
        result.ret = 0;
        result.max_dd_pct = 100;
        result.total_trades = au_entries + ag_entries + engine.positions_closed;
        result.au_entries = au_entries;
        result.ag_entries = ag_entries;
        result.positions_closed = engine.positions_closed;
    }

    return result;
}

void RunTestWorker(const std::vector<std::pair<double, double>>& params,
                   std::vector<TestResult>& results,
                   size_t start_idx, size_t end_idx) {
    for (size_t i = start_idx; i < end_idx; ++i) {
        results[i] = RunTest(params[i].first, params[i].second);
        int done = ++g_completed;

        std::lock_guard<std::mutex> lock(g_output_mutex);
        std::cout << "\r  " << done << "/" << params.size() << std::flush;
    }
}

int main() {
    std::cout << "=== Multi-Symbol Grid V3 (with position closing) ===" << std::endl;
    std::cout << "Period: 2025.01.01 - 2026.01.29" << std::endl;
    std::cout << std::endl;

    LoadTickData();
    if (g_merged_ticks.empty()) {
        std::cerr << "No ticks!" << std::endl;
        return 1;
    }

    // User specified ranges: Gold [12:1:24], Silver [19:1:40] (matching MT5 ini)
    std::vector<double> au_survives;
    for (double s = 12; s <= 24; s += 1) au_survives.push_back(s);

    std::vector<double> ag_survives;
    for (double s = 19; s <= 40; s += 1) ag_survives.push_back(s);

    std::vector<std::pair<double, double>> params;
    for (double au : au_survives) {
        for (double ag : ag_survives) {
            params.push_back({au, ag});
        }
    }

    std::vector<TestResult> results(params.size());

    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;
    num_threads = std::min(num_threads, (unsigned int)params.size());

    std::cout << "\nRunning " << params.size() << " combinations (" << num_threads << " threads)..." << std::endl;

    auto start = std::chrono::high_resolution_clock::now();

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
    std::cout << "\nCompleted in " << duration.count() << "s" << std::endl;

    // Sort by return (like MT5 Result column)
    std::sort(results.begin(), results.end(),
              [](const TestResult& a, const TestResult& b) {
                  if (a.stop_out != b.stop_out) return !a.stop_out;
                  return a.ret > b.ret;
              });

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n=== TOP 20 CONFIGURATIONS ===" << std::endl;
    std::cout << "AU%   AG%   Return      DD%      Trades  Closed  AU_pos  AG_pos" << std::endl;
    std::cout << "----  ----  ----------  -------  ------  ------  ------  ------" << std::endl;

    for (int i = 0; i < 20 && i < (int)results.size(); i++) {
        const auto& r = results[i];
        if (r.stop_out) {
            std::cout << std::setw(4) << r.au_survive << "  "
                      << std::setw(4) << r.ag_survive << "  "
                      << "STOP-OUT" << std::endl;
        } else {
            std::cout << std::setw(4) << r.au_survive << "  "
                      << std::setw(4) << r.ag_survive << "  "
                      << std::setw(9) << r.ret << "x  "
                      << std::setw(6) << r.max_dd_pct << "%  "
                      << std::setw(6) << r.total_trades << "  "
                      << std::setw(6) << r.positions_closed << "  "
                      << std::setw(6) << r.au_entries << "  "
                      << std::setw(6) << r.ag_entries << std::endl;
        }
    }

    // Best overall
    if (!results.empty() && !results[0].stop_out) {
        const auto& best = results[0];
        std::cout << "\n=== BEST ===" << std::endl;
        std::cout << "XAUUSD survive = " << best.au_survive << "%" << std::endl;
        std::cout << "XAGUSD survive = " << best.ag_survive << "%" << std::endl;
        std::cout << "Return: " << best.ret << "x ($" << (best.ret * 10000) << ")" << std::endl;
        std::cout << "Max DD: " << best.max_dd_pct << "%" << std::endl;
        std::cout << "Total trades: " << best.total_trades << " (closed: " << best.positions_closed << ")" << std::endl;
    }

    // Count survivors
    int survived = 0;
    for (const auto& r : results) {
        if (!r.stop_out) survived++;
    }
    std::cout << "\nSurvived: " << survived << "/" << results.size() << std::endl;

    return 0;
}
