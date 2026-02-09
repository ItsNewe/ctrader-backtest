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

struct AdaptiveConfig {
    std::string name;

    // Stage 1: Normal operation (DD < threshold1)
    int max_pos_normal;
    double spacing_normal;

    // Stage 2: Cautious mode (DD between threshold1 and threshold2)
    double dd_stage2;
    int max_pos_cautious;
    double spacing_cautious;
    double partial_close_pct;  // Close this % at stage 2

    // Stage 3: Emergency (DD > threshold3)
    double dd_close_all;
};

struct Result {
    double return_pct;
    double max_dd;
};

Result RunAdaptive(const std::vector<Tick>& ticks, const AdaptiveConfig& cfg, double initial_balance = 10000.0) {
    Result result = {0, 0};
    if (ticks.empty()) return result;

    double balance = initial_balance;
    double equity = initial_balance;
    double peak_equity = initial_balance;
    double contract_size = 100.0;
    double leverage = 500.0;

    std::vector<Trade*> positions;
    size_t next_id = 1;
    bool stage2_closed = false;
    bool stage3_closed = false;

    for (const Tick& tick : ticks) {
        equity = balance;
        for (Trade* t : positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * contract_size;
        }

        if (equity > peak_equity) {
            peak_equity = equity;
            stage2_closed = false;
            stage3_closed = false;
        }

        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        result.max_dd = std::max(result.max_dd, dd_pct);

        // Determine current stage
        int current_max_pos;
        double current_spacing;

        if (dd_pct > cfg.dd_close_all) {
            // Stage 3: Emergency close all
            if (!stage3_closed && !positions.empty()) {
                for (Trade* t : positions) {
                    balance += (tick.bid - t->entry_price) * t->lot_size * contract_size;
                    delete t;
                }
                positions.clear();
                stage3_closed = true;
                equity = balance;
                peak_equity = equity;
            }
            continue;
        } else if (dd_pct > cfg.dd_stage2) {
            // Stage 2: Cautious mode
            current_max_pos = cfg.max_pos_cautious;
            current_spacing = cfg.spacing_cautious;

            // Partial close on entering stage 2
            if (!stage2_closed && positions.size() > 1 && cfg.partial_close_pct > 0) {
                std::sort(positions.begin(), positions.end(), [&](Trade* a, Trade* b) {
                    return (tick.bid - a->entry_price) < (tick.bid - b->entry_price);
                });
                int to_close = (int)(positions.size() * cfg.partial_close_pct);
                to_close = std::max(1, to_close);
                for (int i = 0; i < to_close && !positions.empty(); i++) {
                    balance += (tick.bid - positions[0]->entry_price) * positions[0]->lot_size * contract_size;
                    delete positions[0];
                    positions.erase(positions.begin());
                }
                stage2_closed = true;
            }
        } else {
            // Stage 1: Normal
            current_max_pos = cfg.max_pos_normal;
            current_spacing = cfg.spacing_normal;
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

        // Open new positions (only in stage 1 or early stage 2)
        if (dd_pct < cfg.dd_stage2 + 3.0 && (int)positions.size() < current_max_pos) {
            double lowest = DBL_MAX, highest = DBL_MIN;
            for (Trade* t : positions) {
                lowest = std::min(lowest, t->entry_price);
                highest = std::max(highest, t->entry_price);
            }

            bool should_open = positions.empty() ||
                               (lowest >= tick.ask + current_spacing) ||
                               (highest <= tick.ask - current_spacing);

            if (should_open) {
                double lot = 0.01;
                double margin_needed = lot * contract_size * tick.ask / leverage;
                if (equity - used_margin > margin_needed * 2) {
                    Trade* t = new Trade();
                    t->id = next_id++;
                    t->entry_price = tick.ask;
                    t->lot_size = lot;
                    t->take_profit = tick.ask + tick.spread() + current_spacing;
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
    std::cout << "     V4 ADAPTIVE STAGING TEST" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;

    std::vector<AdaptiveConfig> configs = {
        // Baseline comparison (simulating V3)
        {"V3-like baseline",     20, 1.0,   5.0,  20, 1.0, 0.50, 25.0},

        // Adaptive: more positions normally, fewer in cautious
        {"Adapt 25->10",         25, 1.0,   5.0,  10, 1.5, 0.50, 25.0},
        {"Adapt 30->10",         30, 1.0,   5.0,  10, 1.5, 0.60, 25.0},
        {"Adapt 20->5",          20, 1.0,   5.0,   5, 2.0, 0.70, 25.0},

        // Earlier stage 2 trigger
        {"Early caution @3%",    20, 1.0,   3.0,  10, 1.5, 0.50, 20.0},
        {"Early caution @4%",    20, 1.0,   4.0,  10, 1.5, 0.50, 22.0},

        // Wider spacing in cautious mode
        {"Wide spacing x2",      20, 1.0,   5.0,  15, 2.0, 0.50, 25.0},
        {"Wide spacing x3",      20, 1.0,   5.0,  10, 3.0, 0.50, 25.0},

        // Higher close at stage 2
        {"Close 70% @stage2",    20, 1.0,   5.0,  15, 1.5, 0.70, 25.0},
        {"Close 80% @stage2",    20, 1.0,   5.0,  10, 1.5, 0.80, 25.0},

        // Combined best ideas
        {"V4 Candidate A",       25, 1.0,   4.0,  10, 2.0, 0.60, 22.0},
        {"V4 Candidate B",       20, 1.0,   3.0,   8, 2.5, 0.70, 20.0},
        {"V4 Candidate C",       30, 0.8,   5.0,  10, 1.5, 0.50, 25.0},
    };

    // Load real data
    std::string filename = "../validation/Grid/XAUUSD_TICKS_2025.csv";

    struct Period {
        std::string name;
        size_t start;
        size_t count;
    };

    std::vector<Period> periods = {
        {"Jun Uptrend",  12000000, 500000},
        {"Oct Uptrend",  20000000, 500000},
        {"Dec Pre",      50000000, 1500000},
        {"Dec Crash",    51314023, 2000000},
    };

    std::cout << "Loading data..." << std::endl;
    std::vector<std::vector<Tick>> all_ticks;
    for (const auto& p : periods) {
        auto ticks = LoadTicks(filename, p.start, p.count);
        all_ticks.push_back(ticks);
        std::cout << "  " << p.name << ": " << ticks.size() << " ticks" << std::endl;
    }
    std::cout << std::endl;

    // Header
    std::cout << std::left << std::setw(20) << "Config";
    for (const auto& p : periods) {
        std::cout << " | " << std::setw(12) << p.name;
    }
    std::cout << " | " << std::setw(8) << "Total";
    std::cout << " | " << std::setw(8) << "MaxDD" << std::endl;
    std::cout << std::string(20 + periods.size() * 15 + 20, '-') << std::endl;

    std::vector<std::tuple<std::string, double, double>> results;

    for (const auto& cfg : configs) {
        std::cout << std::left << std::setw(20) << cfg.name;
        double total = 0;
        double worst_dd = 0;

        for (size_t i = 0; i < periods.size(); i++) {
            Result r = RunAdaptive(all_ticks[i], cfg);
            total += r.return_pct;
            worst_dd = std::max(worst_dd, r.max_dd);

            std::cout << std::fixed << std::setprecision(1);
            std::cout << " | " << std::right << std::setw(6) << r.return_pct << "%";
            std::cout << std::setw(4) << (int)r.max_dd << "%";
        }

        std::cout << " | " << std::right << std::setw(7) << total << "%";
        std::cout << " | " << std::setw(7) << worst_dd << "%" << std::endl;

        results.push_back({cfg.name, total, worst_dd});
    }

    std::cout << std::string(20 + periods.size() * 15 + 20, '-') << std::endl;

    // Sort by total return
    std::sort(results.begin(), results.end(),
              [](const auto& a, const auto& b) { return std::get<1>(a) > std::get<1>(b); });

    std::cout << std::endl << "TOP 5 BY TOTAL RETURN:" << std::endl;
    for (int i = 0; i < std::min(5, (int)results.size()); i++) {
        std::cout << "  " << (i+1) << ". " << std::get<0>(results[i])
                  << ": " << std::get<1>(results[i]) << "% (MaxDD: "
                  << std::get<2>(results[i]) << "%)" << std::endl;
    }

    // Sort by risk-adjusted
    std::sort(results.begin(), results.end(),
              [](const auto& a, const auto& b) {
                  double ra_a = std::get<1>(a) / std::max(1.0, std::get<2>(a));
                  double ra_b = std::get<1>(b) / std::max(1.0, std::get<2>(b));
                  return ra_a > ra_b;
              });

    std::cout << std::endl << "TOP 5 BY RISK-ADJUSTED (Return/MaxDD):" << std::endl;
    for (int i = 0; i < std::min(5, (int)results.size()); i++) {
        double ra = std::get<1>(results[i]) / std::max(1.0, std::get<2>(results[i]));
        std::cout << "  " << (i+1) << ". " << std::get<0>(results[i])
                  << ": " << std::fixed << std::setprecision(3) << ra
                  << " (Ret: " << std::get<1>(results[i]) << "%, DD: "
                  << std::get<2>(results[i]) << "%)" << std::endl;
    }

    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;

    return 0;
}
