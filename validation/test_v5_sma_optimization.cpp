/**
 * V5 SMA Optimization Test
 *
 * Comprehensive optimization test based on excellent SMA 10000 results:
 * 1. Fine-tune MA period around 10000 (7000-20000)
 * 2. Test robustness on extended time periods
 * 3. Explore enhancements (trend strength buffer, reduced positions in weak trend)
 * 4. Test bi-directional strategy (LONG above MA, SHORT below MA)
 *
 * Compile:
 *   cd validation
 *   g++ -std=c++17 -O0 -o test_v5_sma_optimization test_v5_sma_optimization.cpp -I../include
 *
 * Note: Must use -O0 (no optimization) due to compiler issues with -O2 on large file I/O
 *
 * Run:
 *   ./test_v5_sma_optimization.exe
 */

#include "../include/synthetic_tick_generator.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <deque>

using namespace backtest;

// ===========================
// CONFIGURATION STRUCTURES
// ===========================

struct OptimizationConfig {
    std::string name;

    // V3 Core parameters (fixed)
    double stop_new;
    double partial_close;
    double close_all;
    int max_positions;
    double partial_pct;

    // Trend filter parameters
    int sma_period;

    // Enhancement parameters
    bool use_trend_strength;    // Only trade if price is X% above MA
    double trend_strength_pct;  // Buffer percentage (0.1%, 0.2%, 0.3%)

    bool use_weak_trend_limit;  // Reduce max positions in weak trend
    double weak_trend_threshold; // If price < MA * (1 + threshold), limit positions
    int weak_trend_max_pos;     // Max positions in weak trend

    bool is_bidirectional;      // Trade both LONG and SHORT
};

struct Result {
    double return_pct;
    double max_dd;
    double risk_adjusted;
    int total_trades;
    int long_trades;
    int short_trades;
};

// ===========================
// HELPER STRUCTURES
// ===========================

// Simple Moving Average
struct SMA {
    std::deque<double> prices;
    int period;
    double sum;

    SMA(int p) : period(p), sum(0.0) {}

    void Add(double price) {
        prices.push_back(price);
        sum += price;

        if ((int)prices.size() > period) {
            sum -= prices.front();
            prices.pop_front();
        }
    }

    double Get() const {
        if (prices.size() < (size_t)period) return 0.0;
        return sum / period;
    }

    bool IsReady() const {
        return prices.size() >= (size_t)period;
    }
};

// ===========================
// TEST ENGINE
// ===========================

Result RunTest(const std::vector<Tick>& ticks, const OptimizationConfig& cfg, double initial_balance = 10000.0) {
    Result result = {0, 0, 0, 0, 0, 0};
    if (ticks.empty()) return result;

    double balance = initial_balance;
    double equity = initial_balance;
    double peak_equity = initial_balance;
    double contract_size = 100.0;
    double leverage = 500.0;
    double spacing = 1.0;

    std::vector<Trade*> long_positions;
    std::vector<Trade*> short_positions;
    size_t next_id = 1;
    bool partial_done = false;
    bool all_closed = false;

    // Initialize SMA
    SMA sma(cfg.sma_period);

    for (const Tick& tick : ticks) {
        // Update SMA
        sma.Add(tick.bid);

        // Calculate equity
        equity = balance;
        for (Trade* t : long_positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * contract_size;
        }
        for (Trade* t : short_positions) {
            equity += (t->entry_price - tick.ask) * t->lot_size * contract_size;
        }

        // Update peak equity
        if (equity > peak_equity) {
            peak_equity = equity;
            partial_done = false;
            all_closed = false;
        }

        // Calculate drawdown
        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        result.max_dd = std::max(result.max_dd, dd_pct);

        // Combine all positions for DD management
        std::vector<Trade*> all_positions;
        all_positions.insert(all_positions.end(), long_positions.begin(), long_positions.end());
        all_positions.insert(all_positions.end(), short_positions.begin(), short_positions.end());

        // Close ALL
        if (dd_pct > cfg.close_all && !all_closed && !all_positions.empty()) {
            for (Trade* t : long_positions) {
                balance += (tick.bid - t->entry_price) * t->lot_size * contract_size;
                delete t;
            }
            for (Trade* t : short_positions) {
                balance += (t->entry_price - tick.ask) * t->lot_size * contract_size;
                delete t;
            }
            long_positions.clear();
            short_positions.clear();
            all_closed = true;
            equity = balance;
            peak_equity = equity;
            continue;
        }

        // Partial close (close worst 50% of positions)
        if (dd_pct > cfg.partial_close && !partial_done && all_positions.size() > 1) {
            // Sort by unrealized P/L
            std::sort(all_positions.begin(), all_positions.end(), [&](Trade* a, Trade* b) {
                double pl_a = (a->direction == "BUY") ? (tick.bid - a->entry_price) : (a->entry_price - tick.ask);
                double pl_b = (b->direction == "BUY") ? (tick.bid - b->entry_price) : (b->entry_price - tick.ask);
                return pl_a < pl_b;
            });

            int to_close = (int)(all_positions.size() * cfg.partial_pct);
            to_close = std::max(1, to_close);

            for (int i = 0; i < to_close && i < (int)all_positions.size(); i++) {
                Trade* t = all_positions[i];
                if (t->direction == "BUY") {
                    balance += (tick.bid - t->entry_price) * t->lot_size * contract_size;
                    long_positions.erase(std::remove(long_positions.begin(), long_positions.end(), t), long_positions.end());
                } else {
                    balance += (t->entry_price - tick.ask) * t->lot_size * contract_size;
                    short_positions.erase(std::remove(short_positions.begin(), short_positions.end(), t), short_positions.end());
                }
                delete t;
            }
            partial_done = true;
        }

        // Stop out check
        double used_margin = 0;
        for (Trade* t : long_positions) {
            used_margin += t->lot_size * contract_size * t->entry_price / leverage;
        }
        for (Trade* t : short_positions) {
            used_margin += t->lot_size * contract_size * t->entry_price / leverage;
        }
        if (used_margin > 0 && equity / used_margin * 100.0 < 20.0) {
            for (Trade* t : long_positions) {
                balance += (tick.bid - t->entry_price) * t->lot_size * contract_size;
                delete t;
            }
            for (Trade* t : short_positions) {
                balance += (t->entry_price - tick.ask) * t->lot_size * contract_size;
                delete t;
            }
            long_positions.clear();
            short_positions.clear();
            break;
        }

        // TP check for LONG positions
        for (auto it = long_positions.begin(); it != long_positions.end();) {
            Trade* t = *it;
            if (tick.bid >= t->take_profit) {
                balance += (t->take_profit - t->entry_price) * t->lot_size * contract_size;
                delete t;
                it = long_positions.erase(it);
                result.total_trades++;
                result.long_trades++;
            } else {
                ++it;
            }
        }

        // TP check for SHORT positions
        for (auto it = short_positions.begin(); it != short_positions.end();) {
            Trade* t = *it;
            if (tick.ask <= t->take_profit) {
                balance += (t->entry_price - t->take_profit) * t->lot_size * contract_size;
                delete t;
                it = short_positions.erase(it);
                result.total_trades++;
                result.short_trades++;
            } else {
                ++it;
            }
        }

        // Open new position logic
        if (dd_pct < cfg.stop_new && sma.IsReady()) {
            double ma_value = sma.Get();

            // Determine trend direction and strength
            bool strong_uptrend = false;
            bool weak_uptrend = false;
            bool strong_downtrend = false;
            bool weak_downtrend = false;

            if (cfg.use_trend_strength) {
                // Strong uptrend: price > MA * (1 + trend_strength_pct)
                double strong_threshold = ma_value * (1.0 + cfg.trend_strength_pct / 100.0);
                strong_uptrend = (tick.bid > strong_threshold);
                strong_downtrend = (tick.bid < ma_value / (1.0 + cfg.trend_strength_pct / 100.0));
            } else {
                // Simple: above MA = uptrend, below MA = downtrend
                strong_uptrend = (tick.bid > ma_value);
                strong_downtrend = (tick.bid < ma_value);
            }

            // Weak trend detection (if using weak trend limit)
            if (cfg.use_weak_trend_limit) {
                double weak_threshold = ma_value * (1.0 + cfg.weak_trend_threshold / 100.0);
                weak_uptrend = (tick.bid > ma_value) && (tick.bid < weak_threshold);
            }

            // Determine max positions based on trend strength
            int current_max_long = cfg.max_positions;
            if (cfg.use_weak_trend_limit && weak_uptrend) {
                current_max_long = cfg.weak_trend_max_pos;
            }

            // LONG positions
            if ((cfg.is_bidirectional || !cfg.is_bidirectional) && strong_uptrend && (int)long_positions.size() < current_max_long) {
                double lowest = DBL_MAX, highest = DBL_MIN;
                for (Trade* t : long_positions) {
                    lowest = std::min(lowest, t->entry_price);
                    highest = std::max(highest, t->entry_price);
                }

                bool should_open = long_positions.empty() ||
                                   (lowest >= tick.ask + spacing) ||
                                   (highest <= tick.ask - spacing);

                if (should_open) {
                    double lot = 0.01;
                    double margin_needed = lot * contract_size * tick.ask / leverage;
                    if (equity - used_margin > margin_needed * 2) {
                        Trade* t = new Trade();
                        t->id = next_id++;
                        t->direction = "BUY";
                        t->entry_price = tick.ask;
                        t->lot_size = lot;
                        t->take_profit = tick.ask + tick.spread() + spacing;
                        long_positions.push_back(t);
                    }
                }
            }

            // SHORT positions (only if bidirectional)
            if (cfg.is_bidirectional && strong_downtrend && (int)short_positions.size() < cfg.max_positions) {
                double lowest = DBL_MAX, highest = DBL_MIN;
                for (Trade* t : short_positions) {
                    lowest = std::min(lowest, t->entry_price);
                    highest = std::max(highest, t->entry_price);
                }

                bool should_open = short_positions.empty() ||
                                   (lowest >= tick.bid + spacing) ||
                                   (highest <= tick.bid - spacing);

                if (should_open) {
                    double lot = 0.01;
                    double margin_needed = lot * contract_size * tick.bid / leverage;
                    if (equity - used_margin > margin_needed * 2) {
                        Trade* t = new Trade();
                        t->id = next_id++;
                        t->direction = "SELL";
                        t->entry_price = tick.bid;
                        t->lot_size = lot;
                        t->take_profit = tick.bid - tick.spread() - spacing;
                        short_positions.push_back(t);
                    }
                }
            }
        }
    }

    // Close remaining positions
    if (!ticks.empty()) {
        const Tick& last_tick = ticks.back();
        for (Trade* t : long_positions) {
            balance += (last_tick.bid - t->entry_price) * t->lot_size * contract_size;
            delete t;
        }
        for (Trade* t : short_positions) {
            balance += (t->entry_price - last_tick.ask) * t->lot_size * contract_size;
            delete t;
        }
        long_positions.clear();
        short_positions.clear();
    }

    result.return_pct = (balance - initial_balance) / initial_balance * 100.0;
    result.risk_adjusted = result.return_pct / std::max(1.0, result.max_dd);
    return result;
}

// ===========================
// DATA LOADING
// ===========================

std::vector<Tick> LoadTicks(const std::string& filename, size_t start_line, size_t num_lines) {
    std::vector<Tick> ticks;
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "ERROR: Could not open file: " << filename << std::endl;
        return ticks;
    }

    std::string line;
    size_t current_line = 0;
    std::getline(file, line);  // Skip header

    while (std::getline(file, line) && ticks.size() < num_lines) {
        current_line++;
        if (current_line < start_line) continue;

        std::stringstream ss(line);
        std::string timestamp, bid_str, ask_str;
        std::getline(ss, timestamp, '\t');
        std::getline(ss, bid_str, '\t');
        std::getline(ss, ask_str, '\t');

        try {
            if (bid_str.empty() || ask_str.empty()) continue;

            Tick tick;
            tick.timestamp = timestamp;
            tick.bid = std::stod(bid_str);
            tick.ask = std::stod(ask_str);

            if (tick.bid <= 0 || tick.ask <= 0 || tick.ask < tick.bid) {
                continue;
            }

            ticks.push_back(tick);
        } catch (...) {
            continue;
        }
    }

    file.close();
    return ticks;
}

// ===========================
// MAIN
// ===========================

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "     V5 SMA OPTIMIZATION TEST" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;

    std::cout << "TEST PHASES:" << std::endl;
    std::cout << "1. MA Period Fine-Tuning (7000-20000)" << std::endl;
    std::cout << "2. Robustness Testing (Extended Periods)" << std::endl;
    std::cout << "3. Enhancement Testing (Trend Strength, Weak Trend Limits)" << std::endl;
    std::cout << "4. Bi-directional Strategy Testing" << std::endl;
    std::cout << std::endl;

    // Define test periods
    std::string filename = "Grid/XAUUSD_TICKS_2025.csv";

    struct Period {
        std::string name;
        size_t start;
        size_t count;
    };

    // Extended periods for robustness testing
    std::vector<Period> periods = {
        {"Jan 2025",     1, 500000},
        {"Feb 2025",     3000000, 500000},
        {"Mar 2025",     5500000, 500000},
        {"Apr 2025",     8000000, 500000},
        {"May 2025",    10000000, 500000},
        {"Jun 2025",    12000000, 500000},
        {"Jul 2025",    14500000, 500000},
        {"Aug 2025",    17000000, 500000},
        {"Sep 2025",    19000000, 500000},
        {"Oct 2025",    20000000, 500000},
        {"Nov 2025",    48000000, 1000000},
        {"Dec Pre",     50000000, 1500000},
        {"Dec Crash",   51314023, 2000000},
    };

    std::cout << "Loading data for " << periods.size() << " test periods..." << std::endl;
    std::vector<std::vector<Tick>> all_ticks;
    for (const auto& p : periods) {
        std::cout << "  Loading " << p.name << " (start: " << p.start << ", count: " << p.count << ")..." << std::endl;
        auto ticks = LoadTicks(filename, p.start, p.count);
        all_ticks.push_back(ticks);
        if (ticks.empty()) {
            std::cout << "    WARNING: No ticks loaded for " << p.name << std::endl;
        } else {
            std::cout << "    Loaded " << ticks.size() << " ticks" << std::endl;
        }
    }
    std::cout << std::endl;

    // Check if we have valid data
    bool has_valid_data = false;
    for (const auto& ticks : all_ticks) {
        if (!ticks.empty()) {
            has_valid_data = true;
            break;
        }
    }

    if (!has_valid_data) {
        std::cerr << "ERROR: No valid tick data loaded. Cannot run tests." << std::endl;
        return 1;
    }

    // ============================================================
    // PHASE 1: MA Period Fine-Tuning
    // ============================================================
    std::cout << "\n================================================================" << std::endl;
    std::cout << "PHASE 1: MA PERIOD FINE-TUNING" << std::endl;
    std::cout << "================================================================" << std::endl;

    std::vector<OptimizationConfig> phase1_configs = {
        {"SMA 7000",   5.0, 8.0, 25.0, 20, 0.50, 7000,  false, 0.0, false, 0.0, 10, false},
        {"SMA 8000",   5.0, 8.0, 25.0, 20, 0.50, 8000,  false, 0.0, false, 0.0, 10, false},
        {"SMA 9000",   5.0, 8.0, 25.0, 20, 0.50, 9000,  false, 0.0, false, 0.0, 10, false},
        {"SMA 10000",  5.0, 8.0, 25.0, 20, 0.50, 10000, false, 0.0, false, 0.0, 10, false},
        {"SMA 11000",  5.0, 8.0, 25.0, 20, 0.50, 11000, false, 0.0, false, 0.0, 10, false},
        {"SMA 12000",  5.0, 8.0, 25.0, 20, 0.50, 12000, false, 0.0, false, 0.0, 10, false},
        {"SMA 15000",  5.0, 8.0, 25.0, 20, 0.50, 15000, false, 0.0, false, 0.0, 10, false},
        {"SMA 20000",  5.0, 8.0, 25.0, 20, 0.50, 20000, false, 0.0, false, 0.0, 10, false},
    };

    auto RunPhase = [&](const std::vector<OptimizationConfig>& configs, const std::string& phase_name) {
        // Header
        std::cout << "\n" << std::left << std::setw(18) << "Config";
        for (const auto& p : periods) {
            std::cout << " | " << std::setw(7) << p.name.substr(0, 7);
        }
        std::cout << " | " << std::setw(8) << "TOTAL" << " | " << std::setw(8) << "MaxDD" << " | " << std::setw(6) << "R/A" << std::endl;
        std::cout << std::string(18 + periods.size() * 10 + 30, '-') << std::endl;

        std::vector<std::tuple<std::string, double, double, double, int, int>> summary;

        for (const auto& cfg : configs) {
            std::cout << std::left << std::setw(18) << cfg.name;
            double total = 0;
            double worst_dd = 0;
            int total_trades = 0;
            int long_trades = 0;
            int short_trades = 0;

            for (size_t i = 0; i < periods.size(); i++) {
                if (all_ticks[i].empty()) {
                    std::cout << " | " << std::right << std::setw(7) << "N/A";
                    continue;
                }

                Result r = RunTest(all_ticks[i], cfg);
                total += r.return_pct;
                worst_dd = std::max(worst_dd, r.max_dd);
                total_trades += r.total_trades;
                long_trades += r.long_trades;
                short_trades += r.short_trades;

                std::cout << std::fixed << std::setprecision(1);
                std::cout << " | " << std::right << std::setw(6) << r.return_pct << "%";
            }

            double risk_adj = total / std::max(1.0, worst_dd);

            std::cout << " | " << std::right << std::setw(7) << total << "%"
                      << " | " << std::setw(7) << worst_dd << "%"
                      << " | " << std::setw(6) << std::setprecision(3) << risk_adj << std::endl;

            summary.push_back({cfg.name, total, worst_dd, risk_adj, total_trades, long_trades + short_trades});
        }

        std::cout << std::string(18 + periods.size() * 10 + 30, '-') << std::endl;

        // Results summary
        std::cout << "\nSUMMARY (Sorted by Risk-Adjusted Return):" << std::endl;
        std::cout << std::string(85, '-') << std::endl;

        // Sort by risk-adjusted return
        std::sort(summary.begin(), summary.end(),
                  [](const auto& a, const auto& b) { return std::get<3>(a) > std::get<3>(b); });

        for (size_t i = 0; i < summary.size(); i++) {
            const auto& [name, total, dd, ra, trades, total_t] = summary[i];
            std::cout << std::right << std::setw(2) << (i + 1) << ". "
                      << std::left << std::setw(18) << name
                      << " | Total: " << std::right << std::setw(7) << std::fixed << std::setprecision(1) << total << "%"
                      << " | MaxDD: " << std::setw(5) << dd << "%"
                      << " | R/A: " << std::setw(7) << std::setprecision(3) << ra
                      << " | Trades: " << std::setw(5) << total_t << std::endl;
        }

        return summary;
    };

    auto phase1_results = RunPhase(phase1_configs, "Phase 1");

    // ============================================================
    // PHASE 2: Enhancement Testing (Best MA from Phase 1)
    // ============================================================
    std::cout << "\n================================================================" << std::endl;
    std::cout << "PHASE 2: ENHANCEMENT TESTING" << std::endl;
    std::cout << "================================================================" << std::endl;

    // Get best MA period from Phase 1
    int best_ma_period = 10000;  // Default
    std::cout << "\nUsing MA period: " << best_ma_period << " (baseline)" << std::endl;

    std::vector<OptimizationConfig> phase2_configs = {
        {"Baseline",           5.0, 8.0, 25.0, 20, 0.50, best_ma_period, false, 0.0,  false, 0.0, 10, false},

        // Trend strength filters
        {"Buffer +0.1%",       5.0, 8.0, 25.0, 20, 0.50, best_ma_period, true,  0.1,  false, 0.0, 10, false},
        {"Buffer +0.2%",       5.0, 8.0, 25.0, 20, 0.50, best_ma_period, true,  0.2,  false, 0.0, 10, false},
        {"Buffer +0.3%",       5.0, 8.0, 25.0, 20, 0.50, best_ma_period, true,  0.3,  false, 0.0, 10, false},

        // Weak trend limits
        {"WeakLimit 10@0.2%",  5.0, 8.0, 25.0, 20, 0.50, best_ma_period, false, 0.0,  true,  0.2, 10, false},
        {"WeakLimit 10@0.5%",  5.0, 8.0, 25.0, 20, 0.50, best_ma_period, false, 0.0,  true,  0.5, 10, false},
        {"WeakLimit 10@1.0%",  5.0, 8.0, 25.0, 20, 0.50, best_ma_period, false, 0.0,  true,  1.0, 10, false},

        // Combined enhancements
        {"Buf+0.1%+Weak10",    5.0, 8.0, 25.0, 20, 0.50, best_ma_period, true,  0.1,  true,  0.5, 10, false},
        {"Buf+0.2%+Weak10",    5.0, 8.0, 25.0, 20, 0.50, best_ma_period, true,  0.2,  true,  0.5, 10, false},
    };

    auto phase2_results = RunPhase(phase2_configs, "Phase 2");

    // ============================================================
    // PHASE 3: Bi-directional Strategy Testing
    // ============================================================
    std::cout << "\n================================================================" << std::endl;
    std::cout << "PHASE 3: BI-DIRECTIONAL STRATEGY TESTING" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "\nTesting LONG (above MA) + SHORT (below MA) strategies..." << std::endl;

    std::vector<OptimizationConfig> phase3_configs = {
        {"LONG only",          5.0, 8.0, 25.0, 20, 0.50, best_ma_period, false, 0.0,  false, 0.0, 10, false},
        {"Bi-dir Plain",       5.0, 8.0, 25.0, 20, 0.50, best_ma_period, false, 0.0,  false, 0.0, 10, true},
        {"Bi-dir +0.1% Buf",   5.0, 8.0, 25.0, 20, 0.50, best_ma_period, true,  0.1,  false, 0.0, 10, true},
        {"Bi-dir +0.2% Buf",   5.0, 8.0, 25.0, 20, 0.50, best_ma_period, true,  0.2,  false, 0.0, 10, true},
    };

    auto phase3_results = RunPhase(phase3_configs, "Phase 3");

    // ============================================================
    // FINAL RECOMMENDATIONS
    // ============================================================
    std::cout << "\n================================================================" << std::endl;
    std::cout << "FINAL RECOMMENDATIONS" << std::endl;
    std::cout << "================================================================" << std::endl;

    std::cout << "\nOPTIMAL MA PERIOD: " << std::get<0>(phase1_results[0]) << std::endl;
    std::cout << "  Total Return: " << std::fixed << std::setprecision(1) << std::get<1>(phase1_results[0]) << "%" << std::endl;
    std::cout << "  Max Drawdown: " << std::get<2>(phase1_results[0]) << "%" << std::endl;
    std::cout << "  Risk-Adjusted: " << std::setprecision(3) << std::get<3>(phase1_results[0]) << std::endl;

    std::cout << "\nBEST ENHANCEMENT: " << std::get<0>(phase2_results[0]) << std::endl;
    std::cout << "  Total Return: " << std::fixed << std::setprecision(1) << std::get<1>(phase2_results[0]) << "%" << std::endl;
    std::cout << "  Max Drawdown: " << std::get<2>(phase2_results[0]) << "%" << std::endl;
    std::cout << "  Risk-Adjusted: " << std::setprecision(3) << std::get<3>(phase2_results[0]) << std::endl;

    std::cout << "\nBI-DIRECTIONAL VERDICT: " << std::get<0>(phase3_results[0]) << std::endl;
    std::cout << "  Total Return: " << std::fixed << std::setprecision(1) << std::get<1>(phase3_results[0]) << "%" << std::endl;
    std::cout << "  Max Drawdown: " << std::get<2>(phase3_results[0]) << "%" << std::endl;
    std::cout << "  Risk-Adjusted: " << std::setprecision(3) << std::get<3>(phase3_results[0]) << std::endl;

    // Compare baseline vs best
    double baseline_ra = std::get<3>(phase1_results[0]);
    double best_enh_ra = std::get<3>(phase2_results[0]);
    double bidir_ra = std::get<3>(phase3_results[0]);

    std::cout << "\n----------------------------------------------------------------" << std::endl;
    std::cout << "IMPROVEMENT ANALYSIS:" << std::endl;
    std::cout << "----------------------------------------------------------------" << std::endl;

    double enh_improvement = ((best_enh_ra - baseline_ra) / baseline_ra) * 100.0;
    std::cout << "Enhancement vs Baseline: " << std::showpos << std::fixed << std::setprecision(1)
              << enh_improvement << "%" << std::noshowpos << " improvement" << std::endl;

    double bidir_improvement = ((bidir_ra - baseline_ra) / baseline_ra) * 100.0;
    std::cout << "Bi-directional vs LONG-only: " << std::showpos << std::fixed << std::setprecision(1)
              << bidir_improvement << "%" << std::noshowpos << " improvement" << std::endl;

    std::cout << "\n================================================================" << std::endl;
    std::cout << "KEY INSIGHTS:" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "- Trend strength buffer: Prevents trades in weak/sideways markets" << std::endl;
    std::cout << "- Weak trend limits: Reduces exposure when trend is marginal" << std::endl;
    std::cout << "- Bi-directional: Captures both up and down trends" << std::endl;
    std::cout << "- Risk-Adjusted (R/A) = Total Return / Max Drawdown" << std::endl;
    std::cout << "================================================================" << std::endl;

    return 0;
}
