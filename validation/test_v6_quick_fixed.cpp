/**
 * V6 Quick Test - FIXED VERSION
 *
 * Tests V5 grid averaging strategy with V6 improvements correctly.
 * Previous version had 100x lot size bug and wrong strategy entirely.
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
#include <chrono>

using namespace backtest;

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

struct Config {
    std::string name;
    int sma_period;
    double close_all_dd;      // As percentage (25.0 = 25%)
    double tp_multiplier;     // 1.0 = normal, 2.0 = 2x wider
    size_t time_exit_ticks;   // 0 = disabled
};

struct Result {
    double return_pct;
    double max_dd;
    int total_trades;
    bool stop_out;
};

Result RunTest(const std::vector<Tick>& ticks, const Config& cfg, double initial_balance = 10000.0) {
    Result result = {0, 0, 0, false};
    if (ticks.empty()) return result;

    double balance = initial_balance;
    double equity = initial_balance;
    double peak_equity = initial_balance;
    double contract_size = 100.0;
    double leverage = 500.0;
    double spacing = 1.0;

    std::vector<Trade*> positions;
    std::map<size_t, size_t> position_ages;  // trade_id -> tick count when opened
    size_t next_id = 1;
    size_t tick_count = 0;
    bool partial_done = false;
    bool all_closed = false;

    SMA sma(cfg.sma_period);

    for (const Tick& tick : ticks) {
        tick_count++;
        sma.Add(tick.bid);

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
        if (dd_pct > cfg.close_all_dd && !all_closed && !positions.empty()) {
            for (Trade* t : positions) {
                balance += (tick.bid - t->entry_price) * t->lot_size * contract_size;
                delete t;
            }
            positions.clear();
            position_ages.clear();
            all_closed = true;
            equity = balance;
            peak_equity = equity;
            continue;
        }

        // Partial close
        if (dd_pct > 8.0 && !partial_done && positions.size() > 1) {
            std::sort(positions.begin(), positions.end(), [&](Trade* a, Trade* b) {
                return (tick.bid - a->entry_price) < (tick.bid - b->entry_price);
            });
            int to_close = (int)(positions.size() * 0.5);
            to_close = std::max(1, to_close);
            for (int i = 0; i < to_close && !positions.empty(); i++) {
                balance += (tick.bid - positions[0]->entry_price) * positions[0]->lot_size * contract_size;
                position_ages.erase(positions[0]->id);
                delete positions[0];
                positions.erase(positions.begin());
            }
            partial_done = true;
        }

        // Stop out check
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
            position_ages.clear();
            result.stop_out = true;
            break;
        }

        // V6 IMPROVEMENT: Time-based exit
        if (cfg.time_exit_ticks > 0) {
            for (auto it = positions.begin(); it != positions.end(); ) {
                Trade* t = *it;
                size_t age = tick_count - position_ages[t->id];
                if (age >= cfg.time_exit_ticks) {
                    balance += (tick.bid - t->entry_price) * t->lot_size * contract_size;
                    position_ages.erase(t->id);
                    delete t;
                    it = positions.erase(it);
                } else {
                    ++it;
                }
            }
        }

        // TP check
        for (auto it = positions.begin(); it != positions.end();) {
            Trade* t = *it;
            if (tick.bid >= t->take_profit) {
                balance += (t->take_profit - t->entry_price) * t->lot_size * contract_size;
                position_ages.erase(t->id);
                delete t;
                it = positions.erase(it);
                result.total_trades++;
            } else {
                ++it;
            }
        }

        // Open new position logic
        if (dd_pct < 5.0 && (int)positions.size() < 20 && sma.IsReady()) {
            // SMA filter: only open if price > SMA
            if (tick.bid > sma.Get()) {
                double lowest = DBL_MAX, highest = DBL_MIN;
                for (Trade* t : positions) {
                    lowest = std::min(lowest, t->entry_price);
                    highest = std::max(highest, t->entry_price);
                }

                bool should_open = positions.empty() ||
                                   (lowest >= tick.ask + spacing) ||
                                   (highest <= tick.ask - spacing);

                if (should_open) {
                    double lot = 0.01;  // CRITICAL: 0.01 lot
                    double margin_needed = lot * contract_size * tick.ask / leverage;
                    if (equity - used_margin > margin_needed * 2) {
                        Trade* t = new Trade();
                        t->id = next_id++;
                        t->entry_price = tick.ask;
                        t->lot_size = lot;
                        // V6 IMPROVEMENT: Wider TP
                        t->take_profit = tick.ask + (tick.spread() + spacing) * cfg.tp_multiplier;
                        positions.push_back(t);
                        position_ages[t->id] = tick_count;
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
    return result;
}

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

int main() {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n" << std::string(100, '=') << std::endl;
    std::cout << "V6 COMBINED EXIT IMPROVEMENTS - QUICK TEST (FIXED)" << std::endl;
    std::cout << std::string(100, '=') << std::endl;

    std::string filename = "Grid/XAUUSD_TICKS_2025.csv";

    struct Period {
        std::string name;
        size_t start;
        size_t count;
    };

    // Same periods as V5 tests for fair comparison
    std::vector<Period> periods = {
        {"Jan 2025",     1, 500000},
        {"Apr 2025",     8000000, 500000},
        {"Jun 2025",    12000000, 500000},
        {"Oct 2025",    20000000, 500000},
        {"Dec Pre",     50000000, 1500000},
        {"Dec Crash",   51314023, 2000000},
    };

    // Load data
    std::cout << "\nLoading data..." << std::endl;
    std::vector<std::vector<Tick>> all_ticks;
    for (const auto& p : periods) {
        std::cout << "  " << p.name << " (" << p.count << " ticks)... " << std::flush;
        auto ticks = LoadTicks(filename, p.start, p.count);
        all_ticks.push_back(ticks);
        std::cout << "Loaded " << ticks.size() << std::endl;
    }

    // Define configurations
    std::vector<Config> configs = {
        {"V5 Baseline",        11000, 25.0, 1.0, 0},
        {"V6a: + Time 50k",    11000, 25.0, 1.0, 50000},
        {"V6b: + CloseAll 15%", 11000, 15.0, 1.0, 0},
        {"V6c: + Wider TP 2x", 11000, 25.0, 2.0, 0},
        {"V6d: Time+CloseAll", 11000, 15.0, 1.0, 50000},
        {"V6e: Time+TP",       11000, 25.0, 2.0, 50000},
        {"V6f: CloseAll+TP",   11000, 15.0, 2.0, 0},
        {"V6 FULL",            11000, 15.0, 2.0, 50000},
    };

    std::cout << "\n" << std::string(100, '=') << std::endl;
    std::cout << "RUNNING TESTS" << std::endl;
    std::cout << std::string(100, '=') << std::endl;

    auto overall_start = std::chrono::high_resolution_clock::now();

    // Header
    std::cout << "\n" << std::left << std::setw(25) << "Config";
    for (const auto& p : periods) {
        std::cout << " | " << std::setw(12) << p.name;
    }
    std::cout << " | " << std::setw(10) << "TOTAL"
              << " | " << std::setw(10) << "MaxDD"
              << " | " << std::setw(10) << "Trades" << std::endl;
    std::cout << std::string(100, '-') << std::endl;

    std::vector<std::tuple<std::string, double, double, int>> summary;

    for (const auto& cfg : configs) {
        std::cout << std::left << std::setw(25) << cfg.name;
        double total = 0;
        double worst_dd = 0;
        int total_trades = 0;
        bool any_stop_out = false;

        for (size_t i = 0; i < periods.size(); i++) {
            if (all_ticks[i].empty()) {
                std::cout << " | " << std::right << std::setw(12) << "N/A";
                continue;
            }

            Result r = RunTest(all_ticks[i], cfg);
            total += r.return_pct;
            worst_dd = std::max(worst_dd, r.max_dd);
            total_trades += r.total_trades;
            any_stop_out = any_stop_out || r.stop_out;

            std::cout << " | " << std::right << std::setw(11) << r.return_pct << "%";
        }

        std::cout << " | " << std::right << std::setw(9) << total << "%"
                  << " | " << std::setw(9) << worst_dd << "%"
                  << " | " << std::setw(10) << total_trades;

        if (any_stop_out) {
            std::cout << " STOP OUT";
        }
        std::cout << std::endl;

        summary.push_back({cfg.name, total, worst_dd, total_trades});
    }

    auto overall_end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(overall_end - overall_start);

    std::cout << std::string(100, '-') << std::endl;

    // Find baseline
    double baseline_return = 0;
    for (const auto& [name, ret, dd, trades] : summary) {
        if (name == "V5 Baseline") {
            baseline_return = ret;
            break;
        }
    }

    // Analysis
    std::cout << "\n" << std::string(100, '=') << std::endl;
    std::cout << "IMPROVEMENT ANALYSIS (vs V5 Baseline: " << baseline_return << "%)" << std::endl;
    std::cout << std::string(100, '=') << std::endl;

    for (const auto& [name, ret, dd, trades] : summary) {
        if (name == "V5 Baseline") continue;

        double improvement = ret - baseline_return;
        double improvement_pct = (baseline_return != 0) ? (improvement / std::abs(baseline_return)) * 100.0 : 0.0;

        std::cout << std::left << std::setw(25) << name
                  << " | Return: " << std::right << std::setw(8) << ret << "%"
                  << " | DD: " << std::setw(6) << dd << "%"
                  << " | Improvement: " << std::showpos << std::setw(8) << improvement_pct << "%" << std::noshowpos
                  << std::endl;
    }

    std::cout << "\n" << std::string(100, '=') << std::endl;
    std::cout << "Test completed in " << duration.count() << " seconds" << std::endl;
    std::cout << std::string(100, '=') << std::endl;

    return 0;
}
