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
    double partial_pct;        // What % to close at partial (default 50%)
    bool trailing_equity;      // Trail peak equity for close-all
    double trail_pct;          // Trail by this % below peak
};

struct Result {
    double return_pct;
    double max_dd;
    int close_alls;
};

Result RunTest(const std::vector<Tick>& ticks, const Config& cfg, double initial_balance = 10000.0) {
    Result result = {0, 0, 0};
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

        // Determine close threshold
        double close_threshold = cfg.close_all;
        if (cfg.trailing_equity) {
            // Dynamic threshold based on profit level
            double profit_pct = (peak_equity - initial_balance) / initial_balance * 100.0;
            if (profit_pct > 20) {
                close_threshold = cfg.trail_pct; // Tighter stop when profitable
            }
        }

        // Close ALL
        if (dd_pct > close_threshold && !all_closed && !positions.empty()) {
            for (Trade* t : positions) {
                balance += (tick.bid - t->entry_price) * t->lot_size * contract_size;
                delete t;
            }
            positions.clear();
            all_closed = true;
            result.close_alls++;
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
    std::cout << "     V4 AGGRESSIVE PROTECTION TESTING" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;

    std::vector<Config> configs = {
        // Baseline V3
        {"V3 Optimized",     5.0,  8.0, 25.0, 20, 0.50, false, 0},

        // More aggressive partial close
        {"Partial 75%",      5.0,  8.0, 25.0, 20, 0.75, false, 0},
        {"Partial 100%@8%",  5.0,  8.0, 25.0, 20, 1.00, false, 0},

        // Lower position limits
        {"MaxPos 15",        5.0,  8.0, 25.0, 15, 0.50, false, 0},
        {"MaxPos 10",        5.0,  8.0, 25.0, 10, 0.50, false, 0},

        // Earlier stop_new
        {"StopNew 3%",       3.0,  8.0, 25.0, 20, 0.50, false, 0},
        {"StopNew 3% Max15", 3.0,  8.0, 25.0, 15, 0.50, false, 0},

        // Tighter close_all
        {"CloseAll 20%",     5.0,  8.0, 20.0, 20, 0.50, false, 0},
        {"CloseAll 15%",     5.0,  8.0, 15.0, 20, 0.50, false, 0},

        // Trailing equity stop
        {"Trail 10% profit", 5.0,  8.0, 25.0, 20, 0.50, true, 10.0},
        {"Trail 15% profit", 5.0,  8.0, 25.0, 20, 0.50, true, 15.0},

        // Combined aggressive
        {"Ultra Safe",       3.0,  5.0, 15.0, 10, 0.75, false, 0},
        {"Balanced Safe",    4.0,  6.0, 20.0, 15, 0.60, false, 0},
    };

    // Load real data periods
    std::string filename = "../validation/Grid/XAUUSD_TICKS_2025.csv";

    struct Period {
        std::string name;
        size_t start;
        size_t count;
    };

    std::vector<Period> periods = {
        {"Jun Uptrend",  12000000, 500000},
        {"Dec Pre-crash", 50000000, 1500000},
        {"Dec Crash",    51314023, 2000000},
    };

    // Load all data
    std::cout << "Loading data..." << std::endl;
    std::vector<std::vector<Tick>> all_ticks;
    for (const auto& p : periods) {
        auto ticks = LoadTicks(filename, p.start, p.count);
        all_ticks.push_back(ticks);
        std::cout << "  " << p.name << ": " << ticks.size() << " ticks" << std::endl;
    }
    std::cout << std::endl;

    // Header
    std::cout << std::left << std::setw(18) << "Config";
    for (const auto& p : periods) {
        std::cout << " | " << std::setw(14) << p.name;
    }
    std::cout << " | " << std::setw(10) << "Total" << std::endl;
    std::cout << std::string(18 + periods.size() * 17 + 13, '-') << std::endl;

    // Test each config
    std::vector<std::pair<std::string, double>> totals;

    for (const auto& cfg : configs) {
        std::cout << std::left << std::setw(18) << cfg.name;
        double total = 0;

        for (size_t i = 0; i < periods.size(); i++) {
            Result r = RunTest(all_ticks[i], cfg);
            total += r.return_pct;

            std::cout << std::fixed << std::setprecision(1);
            std::cout << " | " << std::right << std::setw(6) << r.return_pct << "%";
            std::cout << " DD" << std::setw(2) << (int)r.max_dd << "%";
        }

        std::cout << " | " << std::right << std::setw(8) << total << "%" << std::endl;
        totals.push_back({cfg.name, total});
    }

    std::cout << std::string(18 + periods.size() * 17 + 13, '-') << std::endl;

    // Find best
    std::sort(totals.begin(), totals.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    std::cout << std::endl << "BEST CONFIGURATIONS BY TOTAL RETURN:" << std::endl;
    for (int i = 0; i < std::min(5, (int)totals.size()); i++) {
        std::cout << "  " << (i+1) << ". " << totals[i].first << ": " << totals[i].second << "%" << std::endl;
    }

    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;

    return 0;
}
