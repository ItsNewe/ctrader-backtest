/**
 * Fine parameter sweep + drawdown analysis
 * Analyzes WHY certain parameters work and whether it's reproducible
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
#include <map>

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

// Track price extremes for analysis
struct PriceStats {
    double start_price = 0;
    double end_price = 0;
    double high_price = 0;
    double low_price = DBL_MAX;
    double max_drawdown_from_high = 0;  // Maximum % drop from any high
    std::string max_dd_date;
};

PriceStats g_gold_stats, g_silver_stats;

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
    int positions_closed;
    bool stop_out;
    std::string stop_out_date;
};

class MultiSymbolEngine {
public:
    double balance, peak_equity, max_dd_pct;
    bool stop_out;
    std::string stop_out_date;
    double margin_stop_out_level = 20.0;
    double leverage = 500.0;
    int account_limit_orders = 200;
    int positions_closed = 0;
    SymbolState gold, silver;

    MultiSymbolEngine(double init_bal, double au_survive, double ag_survive) {
        balance = init_bal;
        peak_equity = init_bal;
        max_dd_pct = 0;
        stop_out = false;

        gold.symbol = "XAUUSD";
        gold.contract_size = 100.0;
        gold.survive_down = au_survive;
        gold.volume_of_open_trades = 0;
        gold.checked_last_open_price = DBL_MIN;

        silver.symbol = "XAGUSD";
        silver.contract_size = 5000.0;
        silver.survive_down = ag_survive;
        silver.volume_of_open_trades = 0;
        silver.checked_last_open_price = DBL_MIN;
    }

    double GetEquity(double au_bid, double ag_bid) {
        double unrealized = 0;
        for (const auto& p : gold.positions)
            unrealized += (au_bid - p.entry_price) * p.lot_size * p.contract_size;
        for (const auto& p : silver.positions)
            unrealized += (ag_bid - p.entry_price) * p.lot_size * p.contract_size;
        return balance + unrealized;
    }

    double GetUsedMargin(double au_ask, double ag_ask) {
        double margin = 0;
        for (const auto& p : gold.positions)
            margin += p.lot_size * gold.contract_size * au_ask / leverage;
        for (const auto& p : silver.positions)
            margin += p.lot_size * silver.contract_size * ag_ask / leverage;
        return margin;
    }

    bool CloseSmallestProfitable(double au_bid, double ag_bid) {
        double min_vol = DBL_MAX;
        int best_idx = -1;
        bool is_gold = false;

        for (size_t i = 0; i < gold.positions.size(); i++) {
            double profit = (au_bid - gold.positions[i].entry_price) * gold.positions[i].lot_size * gold.contract_size;
            if (profit > 0 && gold.positions[i].lot_size < min_vol) {
                min_vol = gold.positions[i].lot_size;
                best_idx = i;
                is_gold = true;
            }
        }
        for (size_t i = 0; i < silver.positions.size(); i++) {
            double profit = (ag_bid - silver.positions[i].entry_price) * silver.positions[i].lot_size * silver.contract_size;
            if (profit > 0 && silver.positions[i].lot_size < min_vol) {
                min_vol = silver.positions[i].lot_size;
                best_idx = i;
                is_gold = false;
            }
        }

        if (best_idx >= 0) {
            if (is_gold) {
                balance += (au_bid - gold.positions[best_idx].entry_price) * gold.positions[best_idx].lot_size * gold.contract_size;
                gold.volume_of_open_trades -= gold.positions[best_idx].lot_size;
                gold.positions.erase(gold.positions.begin() + best_idx);
            } else {
                balance += (ag_bid - silver.positions[best_idx].entry_price) * silver.positions[best_idx].lot_size * silver.contract_size;
                silver.volume_of_open_trades -= silver.positions[best_idx].lot_size;
                silver.positions.erase(silver.positions.begin() + best_idx);
            }
            positions_closed++;
            return true;
        }
        return false;
    }

    double CalculateTradeSize(SymbolState& sym, double ask, double equity, double used_margin) {
        double distance = ask * sym.survive_down / 100.0;
        double numerator = 100.0 * equity * leverage
                         - 100.0 * sym.contract_size * distance * sym.volume_of_open_trades * leverage
                         - leverage * margin_stop_out_level * used_margin;
        double denominator = sym.contract_size * (100.0 * distance * leverage + ask * margin_stop_out_level);
        if (denominator <= 0) return 0;
        double lot = std::floor(numerator / denominator * 100.0) / 100.0;
        return (lot >= 0.01) ? std::min(lot, 100.0) : 0;
    }

    void ProcessTick(const std::string& symbol, double ask, double bid,
                     double other_ask, double other_bid, const std::string& timestamp) {
        if (stop_out) return;

        SymbolState& sym = (symbol == "XAUUSD") ? gold : silver;
        double au_ask = (symbol == "XAUUSD") ? ask : other_ask;
        double ag_ask = (symbol == "XAGUSD") ? ask : other_ask;
        double au_bid = (symbol == "XAUUSD") ? bid : other_bid;
        double ag_bid = (symbol == "XAGUSD") ? bid : other_bid;

        double equity = GetEquity(au_bid, ag_bid);
        double used_margin = GetUsedMargin(au_ask, ag_ask);
        double margin_level = (used_margin > 0) ? (equity / used_margin) * 100.0 : 10000.0;

        if (used_margin > 0 && margin_level < margin_stop_out_level) {
            stop_out = true;
            stop_out_date = timestamp;
            return;
        }

        if (equity > peak_equity) peak_equity = equity;
        double dd = (peak_equity - equity) / peak_equity * 100.0;
        if (dd > max_dd_pct) max_dd_pct = dd;

        if (sym.volume_of_open_trades == 0 || ask > sym.checked_last_open_price) {
            sym.checked_last_open_price = ask;
            sym.volume_of_open_trades = 0;
            for (const auto& p : sym.positions) sym.volume_of_open_trades += p.lot_size;

            bool should_open = (sym.volume_of_open_trades == 0);
            if (!should_open) {
                double eq_at_target = (margin_level > 0) ? equity * margin_stop_out_level / margin_level : equity;
                double price_diff = (equity - eq_at_target) / (sym.volume_of_open_trades * sym.contract_size);
                should_open = (ask - price_diff) < ask * (1.0 - sym.survive_down / 100.0);
            }

            if (should_open) {
                double lot = CalculateTradeSize(sym, ask, equity, used_margin);
                if (lot >= 0.01) {
                    if ((int)(gold.positions.size() + silver.positions.size()) >= account_limit_orders) {
                        if (!CloseSmallestProfitable(au_bid, ag_bid)) return;
                        equity = GetEquity(au_bid, ag_bid);
                        used_margin = GetUsedMargin(au_ask, ag_ask);
                        lot = CalculateTradeSize(sym, ask, equity, used_margin);
                        if (lot < 0.01) return;
                    }
                    Position pos;
                    pos.symbol = symbol;
                    pos.entry_price = ask;
                    pos.lot_size = lot;
                    pos.contract_size = sym.contract_size;
                    sym.positions.push_back(pos);
                    sym.volume_of_open_trades += lot;
                }
            }
        }
    }
};

size_t LoadTickFile(const std::string& path, const std::string& symbol) {
    std::ifstream file(path);
    if (!file.is_open()) return 0;
    std::string line;
    std::getline(file, line);
    size_t count = 0;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        MergedTick tick;
        std::stringstream ss(line);
        std::string bid_str, ask_str;
        std::getline(ss, tick.timestamp, '\t');
        std::getline(ss, bid_str, '\t');
        std::getline(ss, ask_str, '\t');
        tick.symbol = symbol;
        tick.bid = std::stod(bid_str);
        tick.ask = std::stod(ask_str);
        g_merged_ticks.push_back(tick);
        count++;
    }
    return count;
}

void AnalyzePriceAction() {
    double au_high = 0, ag_high = 0;

    for (const auto& tick : g_merged_ticks) {
        if (tick.symbol == "XAUUSD") {
            if (g_gold_stats.start_price == 0) g_gold_stats.start_price = tick.bid;
            g_gold_stats.end_price = tick.bid;
            if (tick.bid > g_gold_stats.high_price) g_gold_stats.high_price = tick.bid;
            if (tick.bid < g_gold_stats.low_price) g_gold_stats.low_price = tick.bid;

            if (tick.bid > au_high) au_high = tick.bid;
            double dd = (au_high - tick.bid) / au_high * 100.0;
            if (dd > g_gold_stats.max_drawdown_from_high) {
                g_gold_stats.max_drawdown_from_high = dd;
                g_gold_stats.max_dd_date = tick.timestamp;
            }
        } else {
            if (g_silver_stats.start_price == 0) g_silver_stats.start_price = tick.bid;
            g_silver_stats.end_price = tick.bid;
            if (tick.bid > g_silver_stats.high_price) g_silver_stats.high_price = tick.bid;
            if (tick.bid < g_silver_stats.low_price) g_silver_stats.low_price = tick.bid;

            if (tick.bid > ag_high) ag_high = tick.bid;
            double dd = (ag_high - tick.bid) / ag_high * 100.0;
            if (dd > g_silver_stats.max_drawdown_from_high) {
                g_silver_stats.max_drawdown_from_high = dd;
                g_silver_stats.max_dd_date = tick.timestamp;
            }
        }
    }
}

TestResult RunTest(double au_survive, double ag_survive) {
    MultiSymbolEngine engine(10000.0, au_survive, ag_survive);
    double last_au_ask = 2600, last_ag_ask = 30, last_au_bid = 2600, last_ag_bid = 30;

    for (const auto& tick : g_merged_ticks) {
        if (tick.symbol == "XAUUSD") {
            last_au_ask = tick.ask; last_au_bid = tick.bid;
            engine.ProcessTick("XAUUSD", tick.ask, tick.bid, last_ag_ask, last_ag_bid, tick.timestamp);
        } else {
            last_ag_ask = tick.ask; last_ag_bid = tick.bid;
            engine.ProcessTick("XAGUSD", tick.ask, tick.bid, last_au_ask, last_au_bid, tick.timestamp);
        }
        if (engine.stop_out) break;
    }

    TestResult r;
    r.au_survive = au_survive;
    r.ag_survive = ag_survive;
    r.stop_out = engine.stop_out;
    r.stop_out_date = engine.stop_out_date;
    r.positions_closed = engine.positions_closed;
    r.total_trades = engine.gold.positions.size() + engine.silver.positions.size() + engine.positions_closed;

    if (!engine.stop_out) {
        r.final_equity = engine.GetEquity(last_au_bid, last_ag_bid);
        r.ret = r.final_equity / 10000.0;
        r.max_dd_pct = engine.max_dd_pct;
    } else {
        r.final_equity = 0;
        r.ret = 0;
        r.max_dd_pct = 100;
    }
    return r;
}

void RunWorker(const std::vector<std::pair<double,double>>& params, std::vector<TestResult>& results, size_t s, size_t e) {
    for (size_t i = s; i < e; ++i) {
        results[i] = RunTest(params[i].first, params[i].second);
        g_completed++;
    }
}

int main() {
    std::cout << "=== Multi-Symbol Grid Fine Sweep + Analysis ===" << std::endl;

    // Load all data
    LoadTickFile("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv", "XAUUSD");
    LoadTickFile("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_JAN2026.csv", "XAUUSD");
    LoadTickFile("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\XAGUSD\\XAGUSD_TICKS_2025.csv", "XAGUSD");
    LoadTickFile("C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\XAGUSD\\XAGUSD_TICKS_JAN2026.csv", "XAGUSD");

    std::cout << "Sorting " << g_merged_ticks.size() << " ticks..." << std::endl;
    std::sort(g_merged_ticks.begin(), g_merged_ticks.end(),
              [](const MergedTick& a, const MergedTick& b) { return a.timestamp < b.timestamp; });

    // Analyze price action
    std::cout << "\nAnalyzing price action..." << std::endl;
    AnalyzePriceAction();

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n=== PRICE ACTION ANALYSIS ===" << std::endl;
    std::cout << "GOLD (XAUUSD):" << std::endl;
    std::cout << "  Start: $" << g_gold_stats.start_price << " -> End: $" << g_gold_stats.end_price << std::endl;
    std::cout << "  High: $" << g_gold_stats.high_price << ", Low: $" << g_gold_stats.low_price << std::endl;
    std::cout << "  Total gain: " << ((g_gold_stats.end_price - g_gold_stats.start_price) / g_gold_stats.start_price * 100) << "%" << std::endl;
    std::cout << "  MAX DRAWDOWN FROM HIGH: " << g_gold_stats.max_drawdown_from_high << "% on " << g_gold_stats.max_dd_date << std::endl;

    std::cout << "\nSILVER (XAGUSD):" << std::endl;
    std::cout << "  Start: $" << g_silver_stats.start_price << " -> End: $" << g_silver_stats.end_price << std::endl;
    std::cout << "  High: $" << g_silver_stats.high_price << ", Low: $" << g_silver_stats.low_price << std::endl;
    std::cout << "  Total gain: " << ((g_silver_stats.end_price - g_silver_stats.start_price) / g_silver_stats.start_price * 100) << "%" << std::endl;
    std::cout << "  MAX DRAWDOWN FROM HIGH: " << g_silver_stats.max_drawdown_from_high << "% on " << g_silver_stats.max_dd_date << std::endl;

    // Fine parameter sweep
    std::vector<double> au_survives, ag_survives;
    for (double s = 8; s <= 30; s += 1) au_survives.push_back(s);
    for (double s = 10; s <= 45; s += 1) ag_survives.push_back(s);

    std::vector<std::pair<double,double>> params;
    for (double au : au_survives)
        for (double ag : ag_survives)
            params.push_back({au, ag});

    std::vector<TestResult> results(params.size());
    unsigned int nt = std::min(std::thread::hardware_concurrency(), (unsigned)params.size());
    if (nt == 0) nt = 4;

    std::cout << "\nRunning " << params.size() << " combinations (" << nt << " threads)..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    size_t chunk = params.size() / nt, rem = params.size() % nt, idx = 0;
    for (unsigned t = 0; t < nt; ++t) {
        size_t end = idx + chunk + (t < rem ? 1 : 0);
        threads.emplace_back(RunWorker, std::ref(params), std::ref(results), idx, end);
        idx = end;
    }
    for (auto& t : threads) t.join();

    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - start);
    std::cout << "Completed in " << elapsed.count() << "s" << std::endl;

    // Sort by return
    std::sort(results.begin(), results.end(), [](const TestResult& a, const TestResult& b) {
        if (a.stop_out != b.stop_out) return !a.stop_out;
        return a.ret > b.ret;
    });

    std::cout << "\n=== TOP 25 CONFIGURATIONS ===" << std::endl;
    std::cout << "AU%   AG%   Return       DD%      Trades" << std::endl;
    for (int i = 0; i < 25 && i < (int)results.size(); i++) {
        const auto& r = results[i];
        if (r.stop_out) {
            std::cout << std::setw(4) << r.au_survive << "  " << std::setw(4) << r.ag_survive << "  STOP-OUT @ " << r.stop_out_date.substr(0,10) << std::endl;
        } else {
            std::cout << std::setw(4) << r.au_survive << "  " << std::setw(4) << r.ag_survive << "  "
                      << std::setw(10) << r.ret << "x  " << std::setw(6) << r.max_dd_pct << "%  " << r.total_trades << std::endl;
        }
    }

    // Analyze survival threshold
    std::cout << "\n=== SURVIVAL ANALYSIS ===" << std::endl;
    std::cout << "Minimum survive% to avoid stop-out:" << std::endl;

    std::map<double, double> min_ag_for_au;
    for (const auto& r : results) {
        if (!r.stop_out) {
            if (min_ag_for_au.find(r.au_survive) == min_ag_for_au.end() || r.ag_survive < min_ag_for_au[r.au_survive]) {
                min_ag_for_au[r.au_survive] = r.ag_survive;
            }
        }
    }

    std::cout << "AU%   Min AG% to survive" << std::endl;
    for (auto& kv : min_ag_for_au) {
        std::cout << std::setw(4) << kv.first << "  " << std::setw(4) << kv.second << std::endl;
    }

    // Count survivors by threshold
    std::cout << "\n=== RATIONALIZATION ===" << std::endl;
    std::cout << "Gold max DD from high: " << g_gold_stats.max_drawdown_from_high << "%" << std::endl;
    std::cout << "Silver max DD from high: " << g_silver_stats.max_drawdown_from_high << "%" << std::endl;
    std::cout << "\nTheoretically, survive% should be >= max DD to avoid stop-out." << std::endl;
    std::cout << "But with shared margin + compounding, you need HIGHER survive% as buffer." << std::endl;
    std::cout << "\nBest params (AU=" << results[0].au_survive << "%, AG=" << results[0].ag_survive << "%) are NOT random:" << std::endl;
    std::cout << "- They survived the worst crashes in the test period" << std::endl;
    std::cout << "- They maximized leverage given that survival constraint" << std::endl;
    std::cout << "\nFOR FUTURE USE: Add safety margin above historical max DD." << std::endl;

    int survived = 0;
    for (const auto& r : results) if (!r.stop_out) survived++;
    std::cout << "\nSurvived: " << survived << "/" << results.size() << std::endl;

    return 0;
}
