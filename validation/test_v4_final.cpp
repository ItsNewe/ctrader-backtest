#include "../include/synthetic_tick_generator.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cfloat>

using namespace backtest;

struct Config {
    std::string name;
    double stop_new;
    double partial_close;
    double close_all;
    int max_positions;
    double partial_pct;  // What % to close at partial (0.5 = 50%)
};

struct Result {
    double return_pct;
    double max_dd;
};

Result RunTest(const std::vector<Tick>& ticks, const Config& cfg, double initial_balance = 10000.0) {
    Result result = {0, 0};
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

    for (const Tick& tick : ticks) {
        equity = balance;
        for (Trade* t : positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * contract_size;
        }

        if (equity > peak_equity) {
            peak_equity = equity;
            partial_done = false;
            all_closed = false;
        }

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
            } else {
                ++it;
            }
        }

        // Open new
        if (dd_pct < cfg.stop_new && (int)positions.size() < cfg.max_positions) {
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

    // Close remaining
    if (!ticks.empty()) {
        for (Trade* t : positions) {
            balance += (ticks.back().bid - t->entry_price) * t->lot_size * contract_size;
            delete t;
        }
    }

    result.return_pct = (balance - initial_balance) / initial_balance * 100.0;
    return result;
}

std::vector<Tick> LoadTicks(const std::string& filename, size_t start_line, size_t num_lines) {
    std::vector<Tick> ticks;
    std::ifstream file(filename);
    if (!file.is_open()) return ticks;

    std::string line;
    size_t current_line = 0;
    std::getline(file, line);

    while (std::getline(file, line) && ticks.size() < num_lines) {
        current_line++;
        if (current_line < start_line) continue;

        std::stringstream ss(line);
        std::string timestamp, bid_str, ask_str;
        std::getline(ss, timestamp, '\t');
        std::getline(ss, bid_str, '\t');
        std::getline(ss, ask_str, '\t');

        try {
            Tick tick;
            tick.timestamp = timestamp;
            tick.bid = std::stod(bid_str);
            tick.ask = std::stod(ask_str);
            ticks.push_back(tick);
        } catch (...) {
            continue;
        }
    }
    return ticks;
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "     V3 vs V4 FINAL COMPARISON" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;

    std::vector<Config> configs = {
        // V3 Optimized (our current best)
        {"V3 Optimized",         5.0,  8.0, 25.0, 20, 0.50},

        // V4 candidates based on testing
        {"V4a: 70% partial",     5.0,  8.0, 25.0, 20, 0.70},
        {"V4b: Early+70%",       4.0,  6.0, 22.0, 20, 0.70},
        {"V4c: Lower MaxPos",    5.0,  8.0, 25.0, 15, 0.50},
        {"V4d: Aggressive",      3.0,  5.0, 20.0, 15, 0.70},
        {"V4e: Balanced",        4.0,  7.0, 23.0, 18, 0.60},
    };

    // Real data periods
    std::string filename = "../validation/Grid/XAUUSD_TICKS_2025.csv";

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

    std::cout << "Loading data..." << std::endl;
    std::vector<std::vector<Tick>> all_ticks;
    for (const auto& p : periods) {
        auto ticks = LoadTicks(filename, p.start, p.count);
        all_ticks.push_back(ticks);
    }
    std::cout << std::endl;

    // Header
    std::cout << std::left << std::setw(18) << "Config";
    for (const auto& p : periods) {
        std::cout << " | " << std::setw(10) << p.name;
    }
    std::cout << " | " << std::setw(8) << "TOTAL" << std::endl;
    std::cout << std::string(18 + periods.size() * 13 + 11, '-') << std::endl;

    std::vector<std::tuple<std::string, double, double>> summary;

    for (const auto& cfg : configs) {
        std::cout << std::left << std::setw(18) << cfg.name;
        double total = 0;
        double worst_dd = 0;

        for (size_t i = 0; i < periods.size(); i++) {
            Result r = RunTest(all_ticks[i], cfg);
            total += r.return_pct;
            worst_dd = std::max(worst_dd, r.max_dd);

            std::cout << std::fixed << std::setprecision(1);
            std::cout << " | " << std::right << std::setw(7) << r.return_pct << "%";
        }

        std::cout << " | " << std::right << std::setw(7) << total << "%" << std::endl;
        summary.push_back({cfg.name, total, worst_dd});
    }

    std::cout << std::string(18 + periods.size() * 13 + 11, '-') << std::endl;

    // Results summary
    std::cout << std::endl << "SUMMARY:" << std::endl;
    std::cout << std::string(50, '-') << std::endl;

    for (const auto& [name, total, dd] : summary) {
        double ra = total / std::max(1.0, dd);
        std::cout << std::left << std::setw(18) << name
                  << " | Total: " << std::right << std::setw(7) << total << "%"
                  << " | MaxDD: " << std::setw(5) << dd << "%"
                  << " | R/A: " << std::setw(7) << std::setprecision(3) << ra << std::endl;
    }

    // Find best
    std::sort(summary.begin(), summary.end(),
              [](const auto& a, const auto& b) { return std::get<1>(a) > std::get<1>(b); });

    std::cout << std::endl << "WINNER: " << std::get<0>(summary[0])
              << " with " << std::get<1>(summary[0]) << "% total return" << std::endl;

    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;

    return 0;
}
