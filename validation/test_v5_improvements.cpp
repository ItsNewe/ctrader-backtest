/**
 * V5 Strategy Improvements Test
 *
 * Tests various improvements to the V3 baseline strategy using real XAUUSD tick data.
 *
 * Compile:
 *   cd validation
 *   g++ -std=c++17 -O0 -o test_v5_improvements test_v5_improvements.cpp -I../include
 *
 * Note: Must use -O0 (no optimization) due to compiler issues with -O2 on large file I/O
 *
 * Run:
 *   ./test_v5_improvements.exe
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

struct V5Config {
    std::string name;
    double stop_new;
    double partial_close;
    double close_all;
    int max_positions;
    double partial_pct;

    // V5 Improvements
    bool use_trend_filter;
    int trend_ma_period;

    bool use_profit_lock;
    double profit_lock_threshold;  // % profit to trigger

    bool use_atr_spacing;
    int atr_window;

    bool use_recovery_mode;
    int recovery_ticks;
    double recovery_size_mult;
    double recovery_spacing_mult;
};

struct Result {
    double return_pct;
    double max_dd;
    double risk_adjusted;
};

// ===========================
// HELPER STRUCTURES
// ===========================

struct MovingAverage {
    std::deque<double> prices;
    int period;
    double sum;

    MovingAverage(int p) : period(p), sum(0.0) {}

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

struct ATRCalculator {
    std::deque<double> ranges;
    int window;
    double sum;
    double last_price;
    bool first_tick;

    ATRCalculator(int w) : window(w), sum(0.0), last_price(0.0), first_tick(true) {}

    void Add(double price) {
        if (first_tick) {
            last_price = price;
            first_tick = false;
            return;
        }

        double true_range = std::abs(price - last_price);
        ranges.push_back(true_range);
        sum += true_range;

        if ((int)ranges.size() > window) {
            sum -= ranges.front();
            ranges.pop_front();
        }

        last_price = price;
    }

    double Get() const {
        if (ranges.empty()) return 1.0;  // Default baseline
        return sum / ranges.size();
    }

    bool IsReady() const {
        return ranges.size() >= (size_t)window;
    }
};

// ===========================
// TEST ENGINE
// ===========================

Result RunTest(const std::vector<Tick>& ticks, const V5Config& cfg, double initial_balance = 10000.0) {
    Result result = {0, 0, 0};
    if (ticks.empty()) return result;

    double balance = initial_balance;
    double equity = initial_balance;
    double peak_equity = initial_balance;
    double contract_size = 100.0;
    double leverage = 500.0;
    double base_spacing = 1.0;
    double current_spacing = base_spacing;

    std::vector<Trade*> positions;
    size_t next_id = 1;
    bool partial_done = false;
    bool all_closed = false;

    // V5 tracking
    MovingAverage trend_ma(cfg.trend_ma_period);
    ATRCalculator atr(cfg.atr_window);
    double baseline_atr = 0.0;
    double highest_equity = initial_balance;
    bool profit_lock_active = false;
    double dynamic_close_all = cfg.close_all;

    // Recovery mode
    bool in_recovery = false;
    int recovery_tick_count = 0;

    for (size_t tick_idx = 0; tick_idx < ticks.size(); tick_idx++) {
        const Tick& tick = ticks[tick_idx];

        // Update indicators
        if (cfg.use_trend_filter) {
            trend_ma.Add(tick.bid);
        }

        if (cfg.use_atr_spacing) {
            atr.Add(tick.bid);

            // Set baseline ATR once ready
            if (!baseline_atr && atr.IsReady()) {
                baseline_atr = atr.Get();
                if (baseline_atr == 0.0) baseline_atr = 1.0;  // Prevent division by zero
            }

            // Update spacing based on ATR
            if (baseline_atr > 0 && atr.IsReady()) {
                double current_atr = atr.Get();
                current_spacing = base_spacing * (current_atr / baseline_atr);
                current_spacing = std::max(0.5, std::min(current_spacing, 3.0));  // Clamp to 0.5x - 3x
            }
        } else {
            current_spacing = base_spacing;
        }

        // Calculate equity
        equity = balance;
        for (Trade* t : positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * contract_size;
        }

        // Track highest equity for profit locking
        if (equity > highest_equity) {
            highest_equity = equity;
        }

        // Update peak equity for DD tracking
        if (equity > peak_equity) {
            peak_equity = equity;
            partial_done = false;
            all_closed = false;
        }

        // Profit lock logic
        if (cfg.use_profit_lock && !profit_lock_active) {
            double gain_pct = (highest_equity - initial_balance) / initial_balance * 100.0;
            if (gain_pct >= cfg.profit_lock_threshold) {
                profit_lock_active = true;
                // Reduce close_all threshold to lock in profits
                // Example: If gained 15%, reduce close_all from 25% to 15%
                dynamic_close_all = cfg.profit_lock_threshold;
            }
        }

        // Calculate drawdown
        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        result.max_dd = std::max(result.max_dd, dd_pct);

        // Recovery mode tracking
        if (cfg.use_recovery_mode && in_recovery) {
            recovery_tick_count++;
            if (recovery_tick_count >= cfg.recovery_ticks) {
                in_recovery = false;
                recovery_tick_count = 0;
            }
        }

        // Close ALL
        if (dd_pct > dynamic_close_all && !all_closed && !positions.empty()) {
            for (Trade* t : positions) {
                balance += (tick.bid - t->entry_price) * t->lot_size * contract_size;
                delete t;
            }
            positions.clear();
            all_closed = true;
            equity = balance;
            peak_equity = equity;

            // Activate recovery mode
            if (cfg.use_recovery_mode) {
                in_recovery = true;
                recovery_tick_count = 0;
            }

            // Reset profit lock after close_all
            if (cfg.use_profit_lock) {
                profit_lock_active = false;
                dynamic_close_all = cfg.close_all;
                highest_equity = equity;
            }

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
            } else {
                ++it;
            }
        }

        // Open new position logic
        if (dd_pct < cfg.stop_new && (int)positions.size() < cfg.max_positions) {
            // Trend filter check
            bool trend_ok = true;
            if (cfg.use_trend_filter && trend_ma.IsReady()) {
                double ma_value = trend_ma.Get();
                trend_ok = (tick.bid > ma_value);  // Only buy when price is above MA (uptrend)
            }

            if (trend_ok) {
                double lowest = DBL_MAX, highest = DBL_MIN;
                for (Trade* t : positions) {
                    lowest = std::min(lowest, t->entry_price);
                    highest = std::max(highest, t->entry_price);
                }

                bool should_open = positions.empty() ||
                                   (lowest >= tick.ask + current_spacing) ||
                                   (highest <= tick.ask - current_spacing);

                if (should_open) {
                    // Base lot size
                    double lot = 0.01;

                    // Recovery mode: reduce position size
                    if (in_recovery) {
                        lot *= cfg.recovery_size_mult;
                    }

                    double margin_needed = lot * contract_size * tick.ask / leverage;
                    if (equity - used_margin > margin_needed * 2) {
                        Trade* t = new Trade();
                        t->id = next_id++;
                        t->entry_price = tick.ask;
                        t->lot_size = lot;

                        // Recovery mode: wider spacing for TP
                        double tp_spacing = current_spacing;
                        if (in_recovery) {
                            tp_spacing *= cfg.recovery_spacing_mult;
                        }

                        t->take_profit = tick.ask + tick.spread() + tp_spacing;
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

            // Validate prices
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
    std::cout << "     V5 STRATEGY IMPROVEMENTS vs V3 BASELINE" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;

    // Define test configurations
    std::vector<V5Config> configs = {
        // V3 Baseline (5/8/25, max 20 pos)
        {"V3 Baseline",          5.0, 8.0, 25.0, 20, 0.50, false, 0,    false, 0.0,  false, 0,   false, 0,     0.0,  0.0},

        // Trend Filter variations
        {"Trend MA500",          5.0, 8.0, 25.0, 20, 0.50, true,  500,  false, 0.0,  false, 0,   false, 0,     0.0,  0.0},
        {"Trend MA2000",         5.0, 8.0, 25.0, 20, 0.50, true,  2000, false, 0.0,  false, 0,   false, 0,     0.0,  0.0},
        {"Trend MA5000",         5.0, 8.0, 25.0, 20, 0.50, true,  5000, false, 0.0,  false, 0,   false, 0,     0.0,  0.0},

        // Profit Lock variations
        {"Profit Lock 10%",      5.0, 8.0, 25.0, 20, 0.50, false, 0,    true,  10.0, false, 0,   false, 0,     0.0,  0.0},
        {"Profit Lock 15%",      5.0, 8.0, 25.0, 20, 0.50, false, 0,    true,  15.0, false, 0,   false, 0,     0.0,  0.0},
        {"Profit Lock 20%",      5.0, 8.0, 25.0, 20, 0.50, false, 0,    true,  20.0, false, 0,   false, 0,     0.0,  0.0},

        // ATR Spacing variations
        {"ATR Spacing 500",      5.0, 8.0, 25.0, 20, 0.50, false, 0,    false, 0.0,  true,  500, false, 0,     0.0,  0.0},
        {"ATR Spacing 2000",     5.0, 8.0, 25.0, 20, 0.50, false, 0,    false, 0.0,  true,  2000,false, 0,     0.0,  0.0},

        // Recovery Mode variations
        {"Recovery 50k/50%",     5.0, 8.0, 25.0, 20, 0.50, false, 0,    false, 0.0,  false, 0,   true,  50000, 0.50, 1.5},
        {"Recovery 100k/50%",    5.0, 8.0, 25.0, 20, 0.50, false, 0,    false, 0.0,  false, 0,   true,  100000,0.50, 1.5},

        // Combined strategies
        {"Trend MA2000 + Lock15", 5.0, 8.0, 25.0, 20, 0.50, true, 2000, true,  15.0, false, 0,   false, 0,     0.0,  0.0},
        {"Trend MA2000 + Recov",  5.0, 8.0, 25.0, 20, 0.50, true, 2000, false, 0.0,  false, 0,   true,  50000, 0.50, 1.5},
    };

    // Real data periods
    std::string filename = "Grid/XAUUSD_TICKS_2025.csv";

    struct Period {
        std::string name;
        size_t start;
        size_t count;
    };

    std::vector<Period> periods = {
        {"Jan 2025",     1, 100000},
        {"Apr 2025",     8000000, 100000},
        {"Jun 2025",     12000000, 100000},
        {"Oct 2025",     20000000, 100000},
        {"Dec Pre",      50000000, 300000},
        {"Dec Crash",    51314023, 500000},
    };

    std::cout << "Loading data..." << std::endl;
    std::vector<std::vector<Tick>> all_ticks;
    for (const auto& p : periods) {
        std::cout << "  Loading " << p.name << " (" << p.count << " ticks)..." << std::endl;
        auto ticks = LoadTicks(filename, p.start, p.count);
        all_ticks.push_back(ticks);
        if (ticks.empty()) {
            std::cout << "    WARNING: No ticks loaded for " << p.name << std::endl;
        } else {
            std::cout << "    Loaded " << ticks.size() << " ticks" << std::endl;
        }
    }
    std::cout << std::endl;

    // Header
    std::cout << std::left << std::setw(22) << "Config";
    for (const auto& p : periods) {
        std::cout << " | " << std::setw(10) << p.name;
    }
    std::cout << " | " << std::setw(8) << "TOTAL" << " | " << std::setw(8) << "MaxDD" << " | " << std::setw(6) << "R/A" << std::endl;
    std::cout << std::string(22 + periods.size() * 13 + 30, '-') << std::endl;

    std::vector<std::tuple<std::string, double, double, double>> summary;

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

    for (const auto& cfg : configs) {
        std::cout << std::left << std::setw(22) << cfg.name;
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

    std::cout << std::string(22 + periods.size() * 13 + 30, '-') << std::endl;

    // Results summary
    std::cout << std::endl << "SUMMARY (Sorted by Risk-Adjusted Return):" << std::endl;
    std::cout << std::string(70, '-') << std::endl;

    // Sort by risk-adjusted return
    std::sort(summary.begin(), summary.end(),
              [](const auto& a, const auto& b) { return std::get<3>(a) > std::get<3>(b); });

    for (const auto& [name, total, dd, ra] : summary) {
        std::cout << std::left << std::setw(22) << name
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
    std::cout << "- Trend Filter: Only opens BUY when price > MA (avoids falling markets)" << std::endl;
    std::cout << "- Profit Lock: Reduces close_all threshold after X% profit to protect gains" << std::endl;
    std::cout << "- ATR Spacing: Adjusts position spacing based on market volatility" << std::endl;
    std::cout << "- Recovery Mode: Uses smaller positions & wider spacing after close_all" << std::endl;
    std::cout << "- Risk-Adjusted (R/A) = Total Return / Max Drawdown (higher is better)" << std::endl;
    std::cout << "================================================================" << std::endl;

    return 0;
}
