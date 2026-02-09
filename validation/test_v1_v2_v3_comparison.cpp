#include "../include/fill_up_strategy.h"
#include "../include/fill_up_strategy_v2.h"
#include "../include/fill_up_strategy_v3.h"
#include "../include/synthetic_tick_generator.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <functional>

using namespace backtest;

struct TestResult {
    std::string version;
    double return_pct;
    double max_drawdown_pct;
    int max_positions;
    bool stopped_out;
};

// Simplified V1 simulation (original behavior)
TestResult RunV1(const std::vector<Tick>& ticks, double initial_balance = 10000.0) {
    TestResult result = {"V1", 0, 0, 0, false};

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
            result.stopped_out = true;
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

        // V1: No position limit, no protection
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

// V2 simulation (circuit breaker, position limit 50)
TestResult RunV2(const std::vector<Tick>& ticks, double initial_balance = 10000.0) {
    TestResult result = {"V2", 0, 0, 0, false};

    double balance = initial_balance;
    double equity = initial_balance;
    double peak_equity = initial_balance;
    double contract_size = 100.0;
    double leverage = 500.0;
    double spacing = 1.0;
    int max_positions = 50;
    double circuit_breaker_dd = 50.0;

    std::vector<Trade*> positions;
    size_t next_id = 1;
    int max_pos = 0;
    bool circuit_triggered = false;

    for (const Tick& tick : ticks) {
        equity = balance;
        for (Trade* t : positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * contract_size;
        }

        if (equity > peak_equity) peak_equity = equity;
        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        result.max_drawdown_pct = std::max(result.max_drawdown_pct, dd_pct);

        // V2: Circuit breaker
        if (dd_pct > circuit_breaker_dd) circuit_triggered = true;

        double used_margin = 0;
        for (Trade* t : positions) {
            used_margin += t->lot_size * contract_size * t->entry_price / leverage;
        }
        if (used_margin > 0 && equity / used_margin * 100.0 < 20.0) {
            result.stopped_out = true;
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

        // V2: Position limit + circuit breaker
        bool should_open = !circuit_triggered && (int)positions.size() < max_positions &&
                           (positions.empty() ||
                            (lowest >= tick.ask + spacing) ||
                            (highest <= tick.ask - spacing));

        if (should_open) {
            double lot = 0.01;
            // V2: Reduce size when DD > 25%
            if (dd_pct > 25.0) lot *= 0.5;

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

// V3 simulation (aggressive protection based on empirical testing)
TestResult RunV3(const std::vector<Tick>& ticks, double initial_balance = 10000.0) {
    TestResult result = {"V3", 0, 0, 0, false};

    double balance = initial_balance;
    double equity = initial_balance;
    double peak_equity = initial_balance;
    double contract_size = 100.0;
    double leverage = 500.0;
    double spacing = 1.0;

    // V3 Protection parameters (empirically optimized)
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

        // V3: Close ALL at threshold
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

        // V3: Close HALF at threshold
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
            result.stopped_out = true;
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

        // V3: Position limit + DD threshold for new trades
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

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "     FILL-UP STRATEGY: V1 vs V2 vs V3 COMPARISON" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;
    std::cout << "V1: Original (no protection)" << std::endl;
    std::cout << "V2: Circuit breaker@50% DD, max 50 positions" << std::endl;
    std::cout << "V3: Stop new@8% DD, partial close@10%, full close@15%, max 30 pos" << std::endl;
    std::cout << std::endl;

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
        {"V-Recovery 10%", [](SyntheticTickGenerator& g) { g.GenerateVRecovery(2000, 2000, 10); }},
        {"Flash Crash 8%", [](SyntheticTickGenerator& g) { g.GenerateFlashCrash(8, 100, 500); }},
        {"Slow Drop 8%", [](SyntheticTickGenerator& g) { g.GenerateTrend(5000, -208, 0.05); }},
        {"Bear 15%", [](SyntheticTickGenerator& g) { g.GenerateBearMarket(10000, 15, 4); }},
        {"Whipsaw 10x$10", [](SyntheticTickGenerator& g) { g.GenerateWhipsaw(10, 10.0, 1000); }},
    };

    // Header
    std::cout << std::left << std::setw(16) << "Scenario";
    std::cout << " |       V1       |       V2       |       V3       |" << std::endl;
    std::cout << std::string(16, ' ') << " | Ret%   DD%  Pos| Ret%   DD%  Pos| Ret%   DD%  Pos|" << std::endl;
    std::cout << std::string(70, '-') << std::endl;

    std::vector<TestResult> v1_results, v2_results, v3_results;

    for (size_t i = 0; i < scenarios.size(); i++) {
        SyntheticTickGenerator gen(2600.0, 0.25, i + 1);
        scenarios[i].gen(gen);
        const auto& ticks = gen.GetTicks();

        TestResult r1 = RunV1(ticks);
        TestResult r2 = RunV2(ticks);
        TestResult r3 = RunV3(ticks);

        v1_results.push_back(r1);
        v2_results.push_back(r2);
        v3_results.push_back(r3);

        std::cout << std::fixed << std::setprecision(1);
        std::cout << std::left << std::setw(16) << scenarios[i].name;
        std::cout << " |" << std::right << std::setw(5) << r1.return_pct << "%" << std::setw(4) << (int)r1.max_drawdown_pct << "%" << std::setw(4) << r1.max_positions;
        std::cout << " |" << std::setw(5) << r2.return_pct << "%" << std::setw(4) << (int)r2.max_drawdown_pct << "%" << std::setw(4) << r2.max_positions;
        std::cout << " |" << std::setw(5) << r3.return_pct << "%" << std::setw(4) << (int)r3.max_drawdown_pct << "%" << std::setw(4) << r3.max_positions;
        std::cout << " |" << std::endl;
    }

    std::cout << std::string(70, '-') << std::endl;

    // Summary
    double v1_sum = 0, v2_sum = 0, v3_sum = 0;
    double v1_worst_dd = 0, v2_worst_dd = 0, v3_worst_dd = 0;

    for (size_t i = 0; i < scenarios.size(); i++) {
        v1_sum += v1_results[i].return_pct;
        v2_sum += v2_results[i].return_pct;
        v3_sum += v3_results[i].return_pct;
        v1_worst_dd = std::max(v1_worst_dd, v1_results[i].max_drawdown_pct);
        v2_worst_dd = std::max(v2_worst_dd, v2_results[i].max_drawdown_pct);
        v3_worst_dd = std::max(v3_worst_dd, v3_results[i].max_drawdown_pct);
    }

    int n = scenarios.size();
    std::cout << std::left << std::setw(16) << "AVG RETURN";
    std::cout << " |" << std::right << std::setw(14) << (v1_sum/n) << "%";
    std::cout << " |" << std::setw(14) << (v2_sum/n) << "%";
    std::cout << " |" << std::setw(14) << (v3_sum/n) << "% |" << std::endl;

    std::cout << std::left << std::setw(16) << "WORST DD";
    std::cout << " |" << std::right << std::setw(14) << v1_worst_dd << "%";
    std::cout << " |" << std::setw(14) << v2_worst_dd << "%";
    std::cout << " |" << std::setw(14) << v3_worst_dd << "% |" << std::endl;

    // Risk-adjusted (return / worst DD)
    double v1_ra = (v1_sum/n) / std::max(1.0, v1_worst_dd);
    double v2_ra = (v2_sum/n) / std::max(1.0, v2_worst_dd);
    double v3_ra = (v3_sum/n) / std::max(1.0, v3_worst_dd);

    std::cout << std::left << std::setw(16) << "RISK-ADJ";
    std::cout << " |" << std::right << std::setw(15) << v1_ra;
    std::cout << " |" << std::setw(15) << v2_ra;
    std::cout << " |" << std::setw(15) << v3_ra << " |" << std::endl;

    std::cout << std::endl;

    // Crash-only summary
    std::cout << "CRASH SCENARIOS ONLY (5%, 10%, 15%, V-Rec, Flash, Slow, Bear):" << std::endl;
    std::vector<size_t> crash_idx = {2, 3, 4, 5, 6, 7, 8};
    double v1_crash = 0, v2_crash = 0, v3_crash = 0;
    for (size_t idx : crash_idx) {
        v1_crash += v1_results[idx].return_pct;
        v2_crash += v2_results[idx].return_pct;
        v3_crash += v3_results[idx].return_pct;
    }
    int nc = crash_idx.size();
    std::cout << "  V1 avg: " << (v1_crash/nc) << "%" << std::endl;
    std::cout << "  V2 avg: " << (v2_crash/nc) << "%" << std::endl;
    std::cout << "  V3 avg: " << (v3_crash/nc) << "%" << std::endl;

    // Determine winner
    std::cout << std::endl;
    if (v3_ra > v2_ra && v3_ra > v1_ra) {
        std::cout << ">>> V3 HAS BEST RISK-ADJUSTED PERFORMANCE <<<" << std::endl;
    } else if (v2_ra > v1_ra) {
        std::cout << ">>> V2 HAS BEST RISK-ADJUSTED PERFORMANCE <<<" << std::endl;
    } else {
        std::cout << ">>> V1 HAS BEST RISK-ADJUSTED PERFORMANCE <<<" << std::endl;
    }

    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;

    return 0;
}
