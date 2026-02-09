#include "../include/tick_data.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cfloat>

using namespace backtest;

struct V3Config {
    std::string name;
    double stop_new_at_dd;
    double partial_close_at_dd;
    double close_all_at_dd;
    int max_positions;
};

struct TestResult {
    double return_pct;
    double max_drawdown_pct;
    int max_positions;
};

TestResult RunV3Config(const std::vector<Tick>& ticks, const V3Config& cfg, double initial_balance = 10000.0) {
    TestResult result = {0, 0, 0};
    if (ticks.empty()) return result;

    double balance = initial_balance;
    double equity = initial_balance;
    double peak_equity = initial_balance;
    double contract_size = 100.0;
    double leverage = 500.0;
    double spacing = 1.0;

    std::vector<Trade*> positions;
    size_t next_id = 1;
    int max_pos = 0;
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
        result.max_drawdown_pct = std::max(result.max_drawdown_pct, dd_pct);

        if (dd_pct > cfg.close_all_at_dd && !all_closed && !positions.empty()) {
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

        if (dd_pct > cfg.partial_close_at_dd && !partial_done && positions.size() > 1) {
            std::sort(positions.begin(), positions.end(), [&](Trade* a, Trade* b) {
                return (tick.bid - a->entry_price) < (tick.bid - b->entry_price);
            });
            int to_close = positions.size() / 2;
            for (int i = 0; i < to_close; i++) {
                balance += (tick.bid - positions[0]->entry_price) * positions[0]->lot_size * contract_size;
                delete positions[0];
                positions.erase(positions.begin());
            }
            partial_done = true;
        }

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

        double lowest = DBL_MAX, highest = DBL_MIN;
        for (Trade* t : positions) {
            lowest = std::min(lowest, t->entry_price);
            highest = std::max(highest, t->entry_price);
        }

        bool should_open = dd_pct < cfg.stop_new_at_dd && (int)positions.size() < cfg.max_positions &&
                           (positions.empty() ||
                            (lowest >= tick.ask + spacing) ||
                            (highest <= tick.ask - spacing));

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

        max_pos = std::max(max_pos, (int)positions.size());
    }

    if (!ticks.empty()) {
        for (Trade* t : positions) {
            balance += (ticks.back().bid - t->entry_price) * t->lot_size * contract_size;
            delete t;
        }
    }

    result.return_pct = (balance - initial_balance) / initial_balance * 100.0;
    result.max_positions = max_pos;
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
    std::cout << "     V3 PARAMETER COMPARISON ON REAL CRASH DATA" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;

    std::string filename = "../validation/Grid/XAUUSD_TICKS_2025.csv";

    // Configurations to test
    std::vector<V3Config> configs = {
        {"V1 (no protect)",      100.0,  100.0,  100.0, 999},
        {"V3 Original",            8.0,   10.0,   15.0,  30},
        {"V3 Optimized",           5.0,    8.0,   25.0,  20},
        {"V3 Conservative",        5.0,    8.0,   12.0,  20},
        {"V3 Ultra-Safe",          3.0,    5.0,   10.0,  15},
    };

    // Test periods
    struct Period {
        std::string name;
        size_t start;
        size_t count;
    };

    std::vector<Period> periods = {
        {"Uptrend (Jun)",     12000000, 500000},
        {"Pre-crash (Dec)",   50000000, 1500000},
        {"Crash (Dec 26-31)", 51314023, 2000000},
    };

    // Header
    std::cout << std::left << std::setw(20) << "Config";
    for (const auto& p : periods) {
        std::cout << " | " << std::setw(18) << p.name;
    }
    std::cout << std::endl;
    std::cout << std::string(20 + periods.size() * 21, '-') << std::endl;

    // Load all periods first
    std::vector<std::vector<Tick>> all_ticks;
    for (const auto& p : periods) {
        std::cout << "Loading " << p.name << "..." << std::flush;
        auto ticks = LoadTicks(filename, p.start, p.count);
        all_ticks.push_back(ticks);
        std::cout << " " << ticks.size() << " ticks" << std::endl;
    }
    std::cout << std::endl;

    // Run tests
    for (const auto& cfg : configs) {
        std::cout << std::left << std::setw(20) << cfg.name;

        for (size_t i = 0; i < periods.size(); i++) {
            auto r = RunV3Config(all_ticks[i], cfg);
            std::cout << std::fixed << std::setprecision(1);
            std::cout << " | " << std::right << std::setw(7) << r.return_pct << "%";
            std::cout << " DD" << std::setw(3) << (int)r.max_drawdown_pct << "%";
        }
        std::cout << std::endl;
    }

    std::cout << std::string(20 + periods.size() * 21, '-') << std::endl;

    // Summary
    std::cout << std::endl << "CONFIGURATION DETAILS:" << std::endl;
    for (const auto& cfg : configs) {
        std::cout << "  " << cfg.name << ": StopNew@" << cfg.stop_new_at_dd
                  << "%, Partial@" << cfg.partial_close_at_dd
                  << "%, CloseAll@" << cfg.close_all_at_dd
                  << "%, MaxPos=" << cfg.max_positions << std::endl;
    }

    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;

    return 0;
}
