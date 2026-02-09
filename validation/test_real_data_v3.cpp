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
    std::string period;
    double return_pct;
    double max_drawdown_pct;
    int max_positions;
    double start_price;
    double end_price;
    double price_change_pct;
};

// V1 simulation (original - no protection)
TestResult RunV1(const std::vector<Tick>& ticks, const std::string& period, double initial_balance = 10000.0) {
    TestResult result = {"V1", period, 0, 0, 0, 0, 0, 0};
    if (ticks.empty()) return result;

    result.start_price = ticks.front().bid;
    result.end_price = ticks.back().bid;
    result.price_change_pct = (result.end_price - result.start_price) / result.start_price * 100.0;

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

// V3 simulation (aggressive protection)
TestResult RunV3(const std::vector<Tick>& ticks, const std::string& period, double initial_balance = 10000.0) {
    TestResult result = {"V3", period, 0, 0, 0, 0, 0, 0};
    if (ticks.empty()) return result;

    result.start_price = ticks.front().bid;
    result.end_price = ticks.back().bid;
    result.price_change_pct = (result.end_price - result.start_price) / result.start_price * 100.0;

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
            equity = balance;
            for (Trade* t : positions) {
                equity += (tick.bid - t->entry_price) * t->lot_size * contract_size;
            }
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

// Load ticks from CSV
std::vector<Tick> LoadTicks(const std::string& filename, size_t start_line, size_t num_lines) {
    std::vector<Tick> ticks;
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open: " << filename << std::endl;
        return ticks;
    }

    std::string line;
    size_t current_line = 0;

    // Skip header
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
    std::cout << "     V1 vs V3 ON REAL XAUUSD TICK DATA" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;

    std::string filename = "../validation/Grid/XAUUSD_TICKS_2025.csv";

    // Test different periods (approx 1M ticks each = ~1 week of data)
    struct Period {
        std::string name;
        size_t start;
        size_t count;
    };

    std::vector<Period> periods = {
        {"Week 1 (Jan)", 1, 500000},
        {"Week 8 (Feb)", 4000000, 500000},
        {"Week 16 (Apr)", 8000000, 500000},
        {"Week 24 (Jun)", 12000000, 500000},
        {"Week 32 (Aug)", 16000000, 500000},
        {"Week 40 (Oct)", 20000000, 500000},
        {"Week 48 (Dec)", 24000000, 500000},
        {"Week 52 (Jan26)", 26000000, 500000},
    };

    std::cout << std::left << std::setw(18) << "Period";
    std::cout << " | " << std::setw(10) << "Price Chg";
    std::cout << " | " << std::setw(12) << "V1 Return";
    std::cout << " | " << std::setw(10) << "V1 DD";
    std::cout << " | " << std::setw(12) << "V3 Return";
    std::cout << " | " << std::setw(10) << "V3 DD" << std::endl;
    std::cout << std::string(90, '-') << std::endl;

    double v1_total = 0, v3_total = 0;
    double v1_worst_dd = 0, v3_worst_dd = 0;
    int count = 0;

    for (const auto& period : periods) {
        std::cout << "Loading " << period.name << "..." << std::flush;
        auto ticks = LoadTicks(filename, period.start, period.count);
        std::cout << " " << ticks.size() << " ticks" << std::endl;

        if (ticks.size() < 10000) {
            std::cout << "Skipping - insufficient data" << std::endl;
            continue;
        }

        TestResult r1 = RunV1(ticks, period.name);
        TestResult r3 = RunV3(ticks, period.name);

        std::cout << std::fixed << std::setprecision(2);
        std::cout << std::left << std::setw(18) << period.name;
        std::cout << " | " << std::right << std::setw(9) << r1.price_change_pct << "%";
        std::cout << " | " << std::setw(11) << r1.return_pct << "%";
        std::cout << " | " << std::setw(9) << r1.max_drawdown_pct << "%";
        std::cout << " | " << std::setw(11) << r3.return_pct << "%";
        std::cout << " | " << std::setw(9) << r3.max_drawdown_pct << "%" << std::endl;

        v1_total += r1.return_pct;
        v3_total += r3.return_pct;
        v1_worst_dd = std::max(v1_worst_dd, r1.max_drawdown_pct);
        v3_worst_dd = std::max(v3_worst_dd, r3.max_drawdown_pct);
        count++;
    }

    std::cout << std::string(90, '-') << std::endl;
    std::cout << std::left << std::setw(18) << "TOTAL RETURN";
    std::cout << " |" << std::right << std::setw(11) << " ";
    std::cout << " | " << std::setw(11) << v1_total << "%";
    std::cout << " | " << std::setw(10) << " ";
    std::cout << " | " << std::setw(11) << v3_total << "%";
    std::cout << " |" << std::endl;

    std::cout << std::left << std::setw(18) << "AVG RETURN";
    std::cout << " |" << std::right << std::setw(11) << " ";
    std::cout << " | " << std::setw(11) << (count > 0 ? v1_total/count : 0) << "%";
    std::cout << " | " << std::setw(10) << " ";
    std::cout << " | " << std::setw(11) << (count > 0 ? v3_total/count : 0) << "%";
    std::cout << " |" << std::endl;

    std::cout << std::left << std::setw(18) << "WORST DD";
    std::cout << " |" << std::right << std::setw(11) << " ";
    std::cout << " | " << std::setw(11) << " ";
    std::cout << " | " << std::setw(9) << v1_worst_dd << "%";
    std::cout << " | " << std::setw(11) << " ";
    std::cout << " | " << std::setw(9) << v3_worst_dd << "%" << std::endl;

    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;

    return 0;
}
