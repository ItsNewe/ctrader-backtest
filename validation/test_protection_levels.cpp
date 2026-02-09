#include "../include/synthetic_tick_generator.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cfloat>

using namespace backtest;

struct ProtectionConfig {
    std::string name;
    double max_drawdown_pct;    // Circuit breaker threshold
    int max_positions;          // Position limit
    double reduce_size_at_dd;   // Start reducing size at this DD%
};

struct Result {
    double return_pct;
    double max_drawdown_pct;
    int max_positions_used;
    bool circuit_triggered;
};

Result RunWithProtection(const std::vector<Tick>& ticks,
                         const ProtectionConfig& config,
                         double initial_balance = 10000.0) {
    Result result = {0, 0, 0, false};

    double balance = initial_balance;
    double equity = initial_balance;
    double peak_equity = initial_balance;
    double contract_size = 100.0;
    double leverage = 500.0;
    double spacing = 1.0;

    std::vector<Trade*> positions;
    std::vector<Trade> closed;
    size_t next_id = 1;
    bool circuit_triggered = false;

    for (const Tick& tick : ticks) {
        // Update equity
        equity = balance;
        for (Trade* t : positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * contract_size;
        }

        // Track drawdown
        if (equity > peak_equity) peak_equity = equity;
        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        result.max_drawdown_pct = std::max(result.max_drawdown_pct, dd_pct);

        // Circuit breaker
        if (dd_pct > config.max_drawdown_pct && !circuit_triggered) {
            circuit_triggered = true;
            result.circuit_triggered = true;
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
                closed.push_back(*t);
                delete t;
                it = positions.erase(it);
            } else {
                ++it;
            }
        }

        // Open new positions
        if (!circuit_triggered && (int)positions.size() < config.max_positions) {
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
                // Reduce size based on DD
                if (dd_pct > config.reduce_size_at_dd) {
                    double reduction = 1.0 - (dd_pct - config.reduce_size_at_dd) / (config.max_drawdown_pct - config.reduce_size_at_dd);
                    reduction = std::max(0.25, reduction);  // Min 25% of normal size
                    lot *= reduction;
                }
                lot = std::max(0.01, lot);

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

        result.max_positions_used = std::max(result.max_positions_used, (int)positions.size());
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
    std::cout << "     PROTECTION LEVEL OPTIMIZATION" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;

    // Define protection configurations to test
    std::vector<ProtectionConfig> configs = {
        {"No Protection",     100.0,  200, 100.0},
        {"Mild (50/50/25)",    50.0,   50,  25.0},
        {"Moderate (40/40/20)",40.0,   40,  20.0},
        {"Aggressive (30/30/15)", 30.0, 30, 15.0},
        {"Very Aggr (25/25/10)", 25.0, 25, 10.0},
        {"Ultra (20/20/10)",   20.0,   20,  10.0},
    };

    // Define test scenarios
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
        {"Flash Crash 8%", [](SyntheticTickGenerator& g) { g.GenerateFlashCrash(8, 100, 500); }},
        {"Bear 15%", [](SyntheticTickGenerator& g) { g.GenerateBearMarket(10000, 15, 4); }},
        {"V-Recovery 10%", [](SyntheticTickGenerator& g) { g.GenerateVRecovery(5000, 5000, 10); }},
    };

    // Print header
    std::cout << std::left << std::setw(20) << "Scenario";
    for (const auto& c : configs) {
        std::cout << " | " << std::setw(18) << c.name;
    }
    std::cout << std::endl;
    std::cout << std::string(20 + configs.size() * 21, '-') << std::endl;

    // Run all combinations
    std::vector<std::vector<Result>> all_results;

    for (size_t s = 0; s < scenarios.size(); s++) {
        std::vector<Result> scenario_results;
        std::cout << std::left << std::setw(20) << scenarios[s].name;

        for (size_t c = 0; c < configs.size(); c++) {
            SyntheticTickGenerator gen(2600.0, 0.25, s * 100 + c);
            scenarios[s].gen(gen);

            Result r = RunWithProtection(gen.GetTicks(), configs[c]);
            scenario_results.push_back(r);

            std::cout << std::fixed << std::setprecision(1);
            std::cout << " | " << std::right << std::setw(6) << r.return_pct << "%";
            std::cout << " DD" << std::setw(4) << (int)r.max_drawdown_pct << "%";
        }
        std::cout << std::endl;
        all_results.push_back(scenario_results);
    }

    std::cout << std::string(20 + configs.size() * 21, '-') << std::endl;

    // Calculate averages per config
    std::cout << std::left << std::setw(20) << "AVG RETURN";
    for (size_t c = 0; c < configs.size(); c++) {
        double sum = 0;
        for (const auto& sr : all_results) {
            sum += sr[c].return_pct;
        }
        std::cout << " | " << std::right << std::setw(17) << (sum / scenarios.size()) << "%";
    }
    std::cout << std::endl;

    std::cout << std::left << std::setw(20) << "AVG MAX DD";
    for (size_t c = 0; c < configs.size(); c++) {
        double sum = 0;
        for (const auto& sr : all_results) {
            sum += sr[c].max_drawdown_pct;
        }
        std::cout << " | " << std::right << std::setw(17) << (sum / scenarios.size()) << "%";
    }
    std::cout << std::endl;

    std::cout << std::left << std::setw(20) << "WORST DD";
    for (size_t c = 0; c < configs.size(); c++) {
        double worst = 0;
        for (const auto& sr : all_results) {
            worst = std::max(worst, sr[c].max_drawdown_pct);
        }
        std::cout << " | " << std::right << std::setw(17) << worst << "%";
    }
    std::cout << std::endl;

    // Risk-adjusted score
    std::cout << std::left << std::setw(20) << "RISK-ADJ SCORE";
    for (size_t c = 0; c < configs.size(); c++) {
        double sum_ret = 0, worst_dd = 0;
        for (const auto& sr : all_results) {
            sum_ret += sr[c].return_pct;
            worst_dd = std::max(worst_dd, sr[c].max_drawdown_pct);
        }
        double score = (sum_ret / scenarios.size()) / std::max(1.0, worst_dd);
        std::cout << " | " << std::right << std::setw(18) << score;
    }
    std::cout << std::endl;

    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;

    return 0;
}
