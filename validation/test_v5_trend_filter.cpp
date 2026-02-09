/**
 * V5 Trend Filter Comprehensive Test
 *
 * Tests various trend filter configurations to find optimal settings.
 * Uses the same test periods and V3 baseline (5/8/25 DD, max 20 pos, 50% partial) from test_v4_final.cpp
 *
 * Compile:
 *   cd validation
 *   g++ -std=c++17 -O0 -o test_v5_trend_filter test_v5_trend_filter.cpp -I../include
 *
 * Note: Must use -O0 (no optimization) due to compiler issues with -O2 on large file I/O
 *
 * Run:
 *   ./test_v5_trend_filter.exe
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

struct TrendFilterConfig {
    std::string name;

    // V3 Core parameters (fixed)
    double stop_new;
    double partial_close;
    double close_all;
    int max_positions;
    double partial_pct;

    // Trend filter parameters
    bool use_sma;           // Simple Moving Average
    int sma_period;

    bool use_ema;           // Exponential Moving Average
    int ema_period;
    double ema_alpha;       // Smoothing factor (calculated from period)

    bool use_buffer;        // Price must be X% above MA
    double buffer_pct;      // Buffer percentage (e.g., 0.5 for 0.5%)

    bool use_dual_ma;       // Price must be above both MA1 and MA2
    int dual_ma1_period;
    int dual_ma2_period;

    bool use_momentum;      // Only open if price rose in last N ticks
    int momentum_ticks;
};

struct Result {
    double return_pct;
    double max_dd;
    double risk_adjusted;
    int total_trades;
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

// Exponential Moving Average
struct EMA {
    double value;
    double alpha;
    bool initialized;

    EMA(int period) : value(0.0), initialized(false) {
        // alpha = 2 / (N + 1)
        alpha = 2.0 / (period + 1);
    }

    void Add(double price) {
        if (!initialized) {
            value = price;
            initialized = true;
        } else {
            value = alpha * price + (1.0 - alpha) * value;
        }
    }

    double Get() const {
        return value;
    }

    bool IsReady() const {
        return initialized;
    }
};

// Momentum tracker (price change over N ticks)
struct Momentum {
    std::deque<double> prices;
    int window;

    Momentum(int w) : window(w) {}

    void Add(double price) {
        prices.push_back(price);
        if ((int)prices.size() > window) {
            prices.pop_front();
        }
    }

    bool IsRising() const {
        if (prices.size() < 2) return true;  // Default to true if not enough data
        return prices.back() > prices.front();
    }

    bool IsReady() const {
        return prices.size() >= (size_t)window;
    }
};

// ===========================
// TEST ENGINE
// ===========================

Result RunTest(const std::vector<Tick>& ticks, const TrendFilterConfig& cfg, double initial_balance = 10000.0) {
    Result result = {0, 0, 0, 0};
    if (ticks.empty()) return result;

    double balance = initial_balance;
    double equity = initial_balance;
    double peak_equity = initial_balance;
    double contract_size = 100.0;
    double leverage = 500.0;
    double spacing = 1.0;

    std::vector<Trade*> positions;
    size_t next_id = 1;
    bool partial_done = false;
    bool all_closed = false;

    // Initialize indicators
    SMA sma1(cfg.sma_period);
    SMA dual_ma1(cfg.dual_ma1_period);
    SMA dual_ma2(cfg.dual_ma2_period);
    EMA ema(cfg.ema_period);
    Momentum momentum(cfg.momentum_ticks);

    for (const Tick& tick : ticks) {
        // Update indicators
        if (cfg.use_sma) {
            sma1.Add(tick.bid);
        }
        if (cfg.use_dual_ma) {
            dual_ma1.Add(tick.bid);
            dual_ma2.Add(tick.bid);
        }
        if (cfg.use_ema) {
            ema.Add(tick.bid);
        }
        if (cfg.use_momentum) {
            momentum.Add(tick.bid);
        }

        // Calculate equity
        equity = balance;
        for (Trade* t : positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * contract_size;
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

        // Close ALL
        if (dd_pct > cfg.close_all && !all_closed && !positions.empty()) {
            for (Trade* t : positions) {
                balance += (tick.bid - t->entry_price) * t->lot_size * contract_size;
                delete t;
            }
            positions.clear();
            all_closed = true;
            equity = balance;
            peak_equity = equity;
            continue;
        }

        // Partial close
        if (dd_pct > cfg.partial_close && !partial_done && positions.size() > 1) {
            std::sort(positions.begin(), positions.end(), [&](Trade* a, Trade* b) {
                return (tick.bid - a->entry_price) < (tick.bid - b->entry_price);
            });
            int to_close = (int)(positions.size() * cfg.partial_pct);
            to_close = std::max(1, to_close);
            for (int i = 0; i < to_close && !positions.empty(); i++) {
                balance += (tick.bid - positions[0]->entry_price) * positions[0]->lot_size * contract_size;
                delete positions[0];
                positions.erase(positions.begin());
            }
            partial_done = true;
        }

        // Stop out
        double used_margin = 0;
        for (Trade* t : positions) {
            used_margin += t->lot_size * contract_size * t->entry_price / leverage;
        }
        if (used_margin > 0 && equity / used_margin * 100.0 < 20.0) {
            for (Trade* t : positions) {
                balance += (tick.bid - t->entry_price) * t->lot_size * contract_size;
                delete t;
            }
            positions.clear();
            break;
        }

        // TP check
        for (auto it = positions.begin(); it != positions.end();) {
            Trade* t = *it;
            if (tick.bid >= t->take_profit) {
                balance += (t->take_profit - t->entry_price) * t->lot_size * contract_size;
                delete t;
                it = positions.erase(it);
                result.total_trades++;
            } else {
                ++it;
            }
        }

        // Open new position logic
        if (dd_pct < cfg.stop_new && (int)positions.size() < cfg.max_positions) {
            // Check all trend filters
            bool trend_ok = true;

            // SMA filter
            if (cfg.use_sma && sma1.IsReady()) {
                double ma = sma1.Get();
                if (cfg.use_buffer) {
                    // Price must be buffer_pct% above MA
                    double threshold = ma * (1.0 + cfg.buffer_pct / 100.0);
                    trend_ok = (tick.bid > threshold);
                } else {
                    trend_ok = (tick.bid > ma);
                }
            }

            // EMA filter
            if (trend_ok && cfg.use_ema && ema.IsReady()) {
                double ema_val = ema.Get();
                if (cfg.use_buffer) {
                    double threshold = ema_val * (1.0 + cfg.buffer_pct / 100.0);
                    trend_ok = (tick.bid > threshold);
                } else {
                    trend_ok = (tick.bid > ema_val);
                }
            }

            // Dual MA filter
            if (trend_ok && cfg.use_dual_ma && dual_ma1.IsReady() && dual_ma2.IsReady()) {
                double ma1 = dual_ma1.Get();
                double ma2 = dual_ma2.Get();
                trend_ok = (tick.bid > ma1) && (tick.bid > ma2);
            }

            // Momentum filter
            if (trend_ok && cfg.use_momentum && momentum.IsReady()) {
                trend_ok = momentum.IsRising();
            }

            if (trend_ok) {
                double lowest = DBL_MAX, highest = DBL_MIN;
                for (Trade* t : positions) {
                    lowest = std::min(lowest, t->entry_price);
                    highest = std::max(highest, t->entry_price);
                }

                bool should_open = positions.empty() ||
                                   (lowest >= tick.ask + spacing) ||
                                   (highest <= tick.ask - spacing);

                if (should_open) {
                    double lot = 0.01;
                    double margin_needed = lot * contract_size * tick.ask / leverage;
                    if (equity - used_margin > margin_needed * 2) {
                        Trade* t = new Trade();
                        t->id = next_id++;
                        t->entry_price = tick.ask;
                        t->lot_size = lot;
                        t->take_profit = tick.ask + tick.spread() + spacing;
                        positions.push_back(t);
                    }
                }
            }
        }
    }

    // Close remaining
    if (!ticks.empty() && !positions.empty()) {
        const Tick& last_tick = ticks.back();
        for (Trade* t : positions) {
            balance += (last_tick.bid - t->entry_price) * t->lot_size * contract_size;
            delete t;
        }
        positions.clear();
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
    std::cout << "     V5 TREND FILTER COMPREHENSIVE TEST" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;

    // Define test configurations
    // All use V3 baseline: 5/8/25 DD thresholds, max 20 positions, 50% partial close
    std::vector<TrendFilterConfig> configs = {
        // V3 Baseline (no filters)
        {"V3 Baseline",    5.0, 8.0, 25.0, 20, 0.50, false, 0,    false, 0,    0.0,   false, 0.0, false, 0, 0,    false, 0},

        // Simple Moving Average filters
        {"SMA 3000",       5.0, 8.0, 25.0, 20, 0.50, true,  3000, false, 0,    0.0,   false, 0.0, false, 0, 0,    false, 0},
        {"SMA 4000",       5.0, 8.0, 25.0, 20, 0.50, true,  4000, false, 0,    0.0,   false, 0.0, false, 0, 0,    false, 0},
        {"SMA 5000",       5.0, 8.0, 25.0, 20, 0.50, true,  5000, false, 0,    0.0,   false, 0.0, false, 0, 0,    false, 0},
        {"SMA 6000",       5.0, 8.0, 25.0, 20, 0.50, true,  6000, false, 0,    0.0,   false, 0.0, false, 0, 0,    false, 0},
        {"SMA 8000",       5.0, 8.0, 25.0, 20, 0.50, true,  8000, false, 0,    0.0,   false, 0.0, false, 0, 0,    false, 0},
        {"SMA 10000",      5.0, 8.0, 25.0, 20, 0.50, true,  10000,false, 0,    0.0,   false, 0.0, false, 0, 0,    false, 0},

        // Exponential Moving Average filters
        {"EMA 3000",       5.0, 8.0, 25.0, 20, 0.50, false, 0,    true,  3000, 0.0,   false, 0.0, false, 0, 0,    false, 0},
        {"EMA 5000",       5.0, 8.0, 25.0, 20, 0.50, false, 0,    true,  5000, 0.0,   false, 0.0, false, 0, 0,    false, 0},

        // Buffer zone filters (SMA 5000 with buffer)
        {"SMA5k +0.5%",    5.0, 8.0, 25.0, 20, 0.50, true,  5000, false, 0,    0.0,   true,  0.5, false, 0, 0,    false, 0},
        {"SMA5k +1.0%",    5.0, 8.0, 25.0, 20, 0.50, true,  5000, false, 0,    0.0,   true,  1.0, false, 0, 0,    false, 0},

        // Dual MA confirmation (price above both MA1000 AND MA5000)
        {"Dual 1k+5k",     5.0, 8.0, 25.0, 20, 0.50, false, 0,    false, 0,    0.0,   false, 0.0, true,  1000, 5000, false, 0},
        {"Dual 2k+8k",     5.0, 8.0, 25.0, 20, 0.50, false, 0,    false, 0,    0.0,   false, 0.0, true,  2000, 8000, false, 0},

        // Momentum filters
        {"Momentum 500",   5.0, 8.0, 25.0, 20, 0.50, false, 0,    false, 0,    0.0,   false, 0.0, false, 0, 0,    true,  500},
        {"Momentum 1000",  5.0, 8.0, 25.0, 20, 0.50, false, 0,    false, 0,    0.0,   false, 0.0, false, 0, 0,    true,  1000},

        // Combined filters (SMA + Momentum)
        {"SMA5k+Mom500",   5.0, 8.0, 25.0, 20, 0.50, true,  5000, false, 0,    0.0,   false, 0.0, false, 0, 0,    true,  500},
        {"SMA5k+Mom1k",    5.0, 8.0, 25.0, 20, 0.50, true,  5000, false, 0,    0.0,   false, 0.0, false, 0, 0,    true,  1000},
    };

    // Real data periods - SAME AS test_v4_final.cpp for baseline verification
    std::string filename = "Grid/XAUUSD_TICKS_2025.csv";

    struct Period {
        std::string name;
        size_t start;
        size_t count;
    };

    std::vector<Period> periods = {
        {"Jan 2025",     1, 500000},
        {"Apr 2025",     8000000, 500000},
        {"Jun 2025",    12000000, 500000},
        {"Oct 2025",    20000000, 500000},
        {"Dec Pre",     50000000, 1500000},
        {"Dec Crash",   51314023, 2000000},
    };

    std::cout << "Loading data (same periods as test_v4_final.cpp)..." << std::endl;
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

    // Header
    std::cout << std::left << std::setw(18) << "Config";
    for (const auto& p : periods) {
        std::cout << " | " << std::setw(10) << p.name;
    }
    std::cout << " | " << std::setw(8) << "TOTAL" << " | " << std::setw(8) << "MaxDD" << " | " << std::setw(6) << "R/A" << std::endl;
    std::cout << std::string(18 + periods.size() * 13 + 30, '-') << std::endl;

    std::vector<std::tuple<std::string, double, double, double>> summary;

    for (const auto& cfg : configs) {
        std::cout << std::left << std::setw(18) << cfg.name;
        double total = 0;
        double worst_dd = 0;

        for (size_t i = 0; i < periods.size(); i++) {
            if (all_ticks[i].empty()) {
                std::cout << " | " << std::right << std::setw(7) << "N/A   ";
                continue;
            }

            Result r = RunTest(all_ticks[i], cfg);
            total += r.return_pct;
            worst_dd = std::max(worst_dd, r.max_dd);

            std::cout << std::fixed << std::setprecision(1);
            std::cout << " | " << std::right << std::setw(7) << r.return_pct << "%";
        }

        double risk_adj = total / std::max(1.0, worst_dd);

        std::cout << " | " << std::right << std::setw(7) << total << "%"
                  << " | " << std::setw(7) << worst_dd << "%"
                  << " | " << std::setw(6) << std::setprecision(3) << risk_adj << std::endl;

        summary.push_back({cfg.name, total, worst_dd, risk_adj});
    }

    std::cout << std::string(18 + periods.size() * 13 + 30, '-') << std::endl;

    // Results summary
    std::cout << std::endl << "SUMMARY (Sorted by Risk-Adjusted Return):" << std::endl;
    std::cout << std::string(70, '-') << std::endl;

    // Sort by risk-adjusted return
    std::sort(summary.begin(), summary.end(),
              [](const auto& a, const auto& b) { return std::get<3>(a) > std::get<3>(b); });

    for (size_t i = 0; i < summary.size(); i++) {
        const auto& [name, total, dd, ra] = summary[i];
        std::cout << std::right << std::setw(2) << (i + 1) << ". "
                  << std::left << std::setw(18) << name
                  << " | Total: " << std::right << std::setw(7) << std::fixed << std::setprecision(1) << total << "%"
                  << " | MaxDD: " << std::setw(5) << dd << "%"
                  << " | R/A: " << std::setw(7) << std::setprecision(3) << ra << std::endl;
    }

    std::cout << std::endl << "TOP PERFORMER (Risk-Adjusted): " << std::get<0>(summary[0])
              << " with R/A ratio of " << std::fixed << std::setprecision(3) << std::get<3>(summary[0]) << std::endl;

    // Also show best absolute return
    std::sort(summary.begin(), summary.end(),
              [](const auto& a, const auto& b) { return std::get<1>(a) > std::get<1>(b); });

    std::cout << "BEST ABSOLUTE RETURN: " << std::get<0>(summary[0])
              << " with " << std::fixed << std::setprecision(1) << std::get<1>(summary[0]) << "% total return" << std::endl;

    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "Analysis Notes:" << std::endl;
    std::cout << "- SMA: Only opens when price > Simple Moving Average" << std::endl;
    std::cout << "- EMA: Only opens when price > Exponential Moving Average (more responsive)" << std::endl;
    std::cout << "- Buffer: Only opens when price is X% above MA (stricter filter)" << std::endl;
    std::cout << "- Dual MA: Price must be above BOTH moving averages" << std::endl;
    std::cout << "- Momentum: Only opens when price rising in last N ticks" << std::endl;
    std::cout << "- Risk-Adjusted (R/A) = Total Return / Max Drawdown (higher is better)" << std::endl;
    std::cout << "================================================================" << std::endl;

    return 0;
}
