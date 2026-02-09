#include "../include/synthetic_tick_generator.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cfloat>
#include <algorithm>

using namespace backtest;

struct Config {
    std::string name;
    double stop_new_at_dd;      // Stop opening at this DD%
    double close_half_at_dd;    // Close 50% of positions at this DD%
    double close_all_at_dd;     // Close all positions at this DD%
    int max_positions;
};

struct Result {
    double return_pct;
    double max_drawdown_pct;
    int trades_closed_by_dd;
};

Result RunWithClosing(const std::vector<Tick>& ticks, const Config& cfg, double initial_balance = 10000.0) {
    Result result = {0, 0, 0};

    double balance = initial_balance;
    double equity = initial_balance;
    double peak_equity = initial_balance;
    double contract_size = 100.0;
    double leverage = 500.0;
    double spacing = 1.0;

    std::vector<Trade*> positions;
    size_t next_id = 1;
    bool half_closed = false;
    bool all_closed = false;

    for (const Tick& tick : ticks) {
        // Update equity
        equity = balance;
        for (Trade* t : positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * contract_size;
        }

        // Track drawdown
        if (equity > peak_equity) {
            peak_equity = equity;
            half_closed = false;  // Reset on new high
            all_closed = false;
        }
        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        result.max_drawdown_pct = std::max(result.max_drawdown_pct, dd_pct);

        // Close ALL positions if DD exceeds close_all threshold
        if (dd_pct > cfg.close_all_at_dd && !all_closed && !positions.empty()) {
            for (Trade* t : positions) {
                double pl = (tick.bid - t->entry_price) * t->lot_size * contract_size;
                balance += pl;
                result.trades_closed_by_dd++;
                delete t;
            }
            positions.clear();
            all_closed = true;
            // Update equity after closing
            equity = balance;
            peak_equity = equity;  // Reset peak after forced close
        }
        // Close HALF positions if DD exceeds close_half threshold
        else if (dd_pct > cfg.close_half_at_dd && !half_closed && positions.size() > 1) {
            // Sort by P/L (close worst performers first)
            std::sort(positions.begin(), positions.end(), [&](Trade* a, Trade* b) {
                double pl_a = (tick.bid - a->entry_price) * a->lot_size;
                double pl_b = (tick.bid - b->entry_price) * b->lot_size;
                return pl_a < pl_b;  // Worst first
            });

            int to_close = positions.size() / 2;
            for (int i = 0; i < to_close; i++) {
                Trade* t = positions[0];
                double pl = (tick.bid - t->entry_price) * t->lot_size * contract_size;
                balance += pl;
                result.trades_closed_by_dd++;
                delete t;
                positions.erase(positions.begin());
            }
            half_closed = true;
            // Update equity
            equity = balance;
            for (Trade* t : positions) {
                equity += (tick.bid - t->entry_price) * t->lot_size * contract_size;
            }
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

        // Check TP
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

        // Open new positions (only if not past stop threshold)
        if (dd_pct < cfg.stop_new_at_dd && (int)positions.size() < cfg.max_positions) {
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

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "     POSITION CLOSING ON DRAWDOWN - IMPACT ANALYSIS" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;

    std::vector<Config> configs = {
        {"No Closing",              20.0, 999.0, 999.0, 50},
        {"Close All @ 30%",         20.0, 999.0,  30.0, 50},
        {"Close All @ 25%",         20.0, 999.0,  25.0, 50},
        {"Close All @ 20%",         20.0, 999.0,  20.0, 50},
        {"Close Half@15 All@25",    20.0,  15.0,  25.0, 50},
        {"Close Half@10 All@20",    15.0,  10.0,  20.0, 50},
        {"Aggr: Half@8 All@15",     12.0,   8.0,  15.0, 30},
    };

    struct Scenario {
        std::string name;
        std::function<void(SyntheticTickGenerator&)> gen;
    };

    std::vector<Scenario> scenarios = {
        {"Uptrend +$100", [](SyntheticTickGenerator& g) { g.GenerateTrend(10000, 100, 0.1); }},
        {"Sideways $20", [](SyntheticTickGenerator& g) { g.GenerateSideways(10000, 20); }},
        {"Crash 5%", [](SyntheticTickGenerator& g) { g.GenerateCrash(1000, 5); }},
        {"Crash 10%", [](SyntheticTickGenerator& g) { g.GenerateCrash(1000, 10); }},
        {"Crash 15%", [](SyntheticTickGenerator& g) { g.GenerateCrash(1000, 15); }},
        {"V-Recovery 10%", [](SyntheticTickGenerator& g) { g.GenerateVRecovery(5000, 5000, 10); }},
        {"Flash Crash 8%", [](SyntheticTickGenerator& g) { g.GenerateFlashCrash(8, 100, 500); }},
        {"Bear 15%", [](SyntheticTickGenerator& g) { g.GenerateBearMarket(10000, 15, 4); }},
    };

    // Header
    std::cout << std::left << std::setw(18) << "Scenario";
    for (const auto& c : configs) {
        std::cout << " | " << std::setw(20) << c.name;
    }
    std::cout << std::endl << std::string(18 + configs.size() * 23, '-') << std::endl;

    std::vector<std::vector<Result>> all_results;

    for (size_t s = 0; s < scenarios.size(); s++) {
        std::vector<Result> row;
        std::cout << std::left << std::setw(18) << scenarios[s].name;

        for (size_t c = 0; c < configs.size(); c++) {
            SyntheticTickGenerator gen(2600.0, 0.25, s * 100 + c);
            scenarios[s].gen(gen);
            Result r = RunWithClosing(gen.GetTicks(), configs[c]);
            row.push_back(r);

            std::cout << std::fixed << std::setprecision(1);
            std::cout << " | " << std::right << std::setw(7) << r.return_pct << "%";
            std::cout << " DD" << std::setw(4) << (int)r.max_drawdown_pct << "%";
        }
        std::cout << std::endl;
        all_results.push_back(row);
    }

    std::cout << std::string(18 + configs.size() * 23, '-') << std::endl;

    // Averages
    std::cout << std::left << std::setw(18) << "AVG RETURN";
    for (size_t c = 0; c < configs.size(); c++) {
        double sum = 0;
        for (const auto& row : all_results) sum += row[c].return_pct;
        std::cout << " | " << std::right << std::setw(20) << (sum / scenarios.size()) << "%";
    }
    std::cout << std::endl;

    std::cout << std::left << std::setw(18) << "WORST DD";
    for (size_t c = 0; c < configs.size(); c++) {
        double worst = 0;
        for (const auto& row : all_results) worst = std::max(worst, row[c].max_drawdown_pct);
        std::cout << " | " << std::right << std::setw(20) << worst << "%";
    }
    std::cout << std::endl;

    std::cout << std::left << std::setw(18) << "RISK-ADJ";
    for (size_t c = 0; c < configs.size(); c++) {
        double sum = 0, worst = 0;
        for (const auto& row : all_results) {
            sum += row[c].return_pct;
            worst = std::max(worst, row[c].max_drawdown_pct);
        }
        double score = (sum / scenarios.size()) / std::max(1.0, worst);
        std::cout << " | " << std::right << std::setw(21) << score;
    }
    std::cout << std::endl;

    std::cout << std::endl;
    std::cout << "NOTE: 'Close Half' closes 50% of worst-performing positions" << std::endl;
    std::cout << "      'Close All' closes all positions and resets" << std::endl;
    std::cout << "================================================================" << std::endl;

    return 0;
}
