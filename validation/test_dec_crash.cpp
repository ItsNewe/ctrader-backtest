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

struct TestResult {
    std::string version;
    double return_pct;
    double max_drawdown_pct;
    int max_positions;
    double start_price;
    double end_price;
};

TestResult RunV1(const std::vector<Tick>& ticks, double initial_balance = 10000.0) {
    TestResult result = {"V1", 0, 0, 0, 0, 0};
    if (ticks.empty()) return result;

    result.start_price = ticks.front().bid;
    result.end_price = ticks.back().bid;

    double balance = initial_balance;
    double equity = initial_balance;
    double peak_equity = initial_balance;
    double contract_size = 100.0;
    double leverage = 500.0;
    double spacing = 1.0;

    std::vector<Trade*> positions;
    size_t next_id = 1;
    int max_pos = 0;

    for (const Tick& tick : ticks) {
        equity = balance;
        for (Trade* t : positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * contract_size;
        }

        if (equity > peak_equity) peak_equity = equity;
        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        result.max_drawdown_pct = std::max(result.max_drawdown_pct, dd_pct);

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

TestResult RunV3(const std::vector<Tick>& ticks, double initial_balance = 10000.0) {
    TestResult result = {"V3", 0, 0, 0, 0, 0};
    if (ticks.empty()) return result;

    result.start_price = ticks.front().bid;
    result.end_price = ticks.back().bid;

    double balance = initial_balance;
    double equity = initial_balance;
    double peak_equity = initial_balance;
    double contract_size = 100.0;
    double leverage = 500.0;
    double spacing = 1.0;

    int max_positions = 30;
    double stop_new_at_dd = 8.0;
    double partial_close_at_dd = 10.0;
    double close_all_at_dd = 15.0;

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

        if (dd_pct > close_all_at_dd && !all_closed && !positions.empty()) {
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

        if (dd_pct > partial_close_at_dd && !partial_done && positions.size() > 1) {
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

        bool should_open = dd_pct < stop_new_at_dd && (int)positions.size() < max_positions &&
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
    std::cout << "     V1 vs V3 ON DECEMBER 2025 CRASH PERIOD" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;

    std::string filename = "../validation/Grid/XAUUSD_TICKS_2025.csv";

    // The crash started around line 51314023, peak at 2025.12.26
    // Let's test different windows around this crash

    struct Period {
        std::string name;
        size_t start;
        size_t count;
    };

    std::vector<Period> periods = {
        {"Pre-crash (Dec 20-26)", 50000000, 1500000},
        {"Crash period (Dec 26-31)", 51314023, 2000000},
        {"Full Dec period", 50000000, 3500000},
    };

    std::cout << std::left << std::setw(25) << "Period"
              << " | " << std::setw(20) << "V1"
              << " | " << std::setw(20) << "V3" << std::endl;
    std::cout << std::setw(25) << " "
              << " | " << std::setw(10) << "Return" << std::setw(10) << "MaxDD"
              << " | " << std::setw(10) << "Return" << std::setw(10) << "MaxDD" << std::endl;
    std::cout << std::string(70, '-') << std::endl;

    for (const auto& p : periods) {
        std::cout << "Loading " << p.name << "..." << std::flush;
        auto ticks = LoadTicks(filename, p.start, p.count);
        std::cout << " " << ticks.size() << " ticks" << std::endl;

        if (ticks.size() < 1000) {
            std::cout << "Insufficient data" << std::endl;
            continue;
        }

        auto r1 = RunV1(ticks);
        auto r3 = RunV3(ticks);

        std::cout << std::fixed << std::setprecision(2);
        std::cout << std::left << std::setw(25) << p.name
                  << " | " << std::right << std::setw(8) << r1.return_pct << "%"
                  << std::setw(9) << r1.max_drawdown_pct << "%"
                  << " | " << std::setw(8) << r3.return_pct << "%"
                  << std::setw(9) << r3.max_drawdown_pct << "%" << std::endl;

        std::cout << "  Price: $" << r1.start_price << " -> $" << r1.end_price
                  << " (" << ((r1.end_price - r1.start_price) / r1.start_price * 100) << "%)" << std::endl;
    }

    std::cout << std::string(70, '-') << std::endl;
    std::cout << std::endl;

    // Now simulate what would happen with a 5% crash
    std::cout << "SIMULATED 5% CRASH (injected into real data pattern):" << std::endl;
    std::cout << "Loading base period..." << std::flush;
    auto base_ticks = LoadTicks(filename, 51314023, 500000);
    std::cout << " " << base_ticks.size() << " ticks" << std::endl;

    if (!base_ticks.empty()) {
        // Scale the crash to be 5% instead of 2%
        double original_peak = 4549.86;
        double original_bottom = 4274.74;  // 2.11% drop
        double target_drop_pct = 5.0;

        std::vector<Tick> scaled_ticks;
        for (const auto& tick : base_ticks) {
            Tick scaled = tick;
            // Scale the drop proportionally
            double original_drop = (original_peak - tick.bid) / original_peak * 100.0;
            if (original_drop > 0) {
                double scaled_drop = original_drop * (target_drop_pct / 2.11);
                scaled.bid = original_peak * (1.0 - scaled_drop / 100.0);
                scaled.ask = scaled.bid + (tick.ask - tick.bid);  // Preserve spread
            }
            scaled_ticks.push_back(scaled);
        }

        auto r1 = RunV1(scaled_ticks);
        auto r3 = RunV3(scaled_ticks);

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "  V1: Return=" << r1.return_pct << "%, MaxDD=" << r1.max_drawdown_pct << "%" << std::endl;
        std::cout << "  V3: Return=" << r3.return_pct << "%, MaxDD=" << r3.max_drawdown_pct << "%" << std::endl;
        std::cout << "  Price: $" << scaled_ticks.front().bid << " -> $" << scaled_ticks.back().bid << std::endl;
    }

    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;

    return 0;
}
