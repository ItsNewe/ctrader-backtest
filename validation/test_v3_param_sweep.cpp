#include "../include/synthetic_tick_generator.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cfloat>
#include <algorithm>

using namespace backtest;

struct V3Config {
    double stop_new_at_dd;
    double partial_close_at_dd;
    double close_all_at_dd;
    int max_positions;
};

struct Result {
    double avg_return;
    double worst_dd;
    double crash_avg;
    double risk_adjusted;
};

double RunSingleTest(const std::vector<Tick>& ticks, const V3Config& cfg, double initial_balance = 10000.0) {
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

        // Close ALL
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

        // Partial close
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
    }

    // Close remaining
    if (!ticks.empty()) {
        for (Trade* t : positions) {
            balance += (ticks.back().bid - t->entry_price) * t->lot_size * contract_size;
            delete t;
        }
    }

    return (balance - initial_balance) / initial_balance * 100.0;
}

Result TestConfig(const V3Config& cfg) {
    Result result = {0, 0, 0, 0};

    struct Scenario {
        std::string name;
        std::function<void(SyntheticTickGenerator&)> gen;
        bool is_crash;
    };

    std::vector<Scenario> scenarios = {
        {"Uptrend", [](SyntheticTickGenerator& g) { g.GenerateTrend(10000, 100, 0.1); }, false},
        {"Sideways", [](SyntheticTickGenerator& g) { g.GenerateSideways(10000, 20); }, false},
        {"Crash 5%", [](SyntheticTickGenerator& g) { g.GenerateCrash(1000, 5); }, true},
        {"Crash 10%", [](SyntheticTickGenerator& g) { g.GenerateCrash(1000, 10); }, true},
        {"V-Recovery", [](SyntheticTickGenerator& g) { g.GenerateVRecovery(2000, 2000, 10); }, true},
        {"Flash Crash", [](SyntheticTickGenerator& g) { g.GenerateFlashCrash(8, 100, 500); }, true},
        {"Bear 10%", [](SyntheticTickGenerator& g) { g.GenerateBearMarket(5000, 10, 3); }, true},
    };

    double total = 0, crash_total = 0;
    int crash_count = 0;

    for (size_t i = 0; i < scenarios.size(); i++) {
        SyntheticTickGenerator gen(2600.0, 0.25, i + 1);
        scenarios[i].gen(gen);
        double ret = RunSingleTest(gen.GetTicks(), cfg);
        total += ret;

        // Track worst case (most negative or highest positive for DD calc)
        if (ret < 0) {
            result.worst_dd = std::max(result.worst_dd, -ret);  // Convert loss to DD proxy
        }

        if (scenarios[i].is_crash) {
            crash_total += ret;
            crash_count++;
        }
    }

    result.avg_return = total / scenarios.size();
    result.crash_avg = crash_total / crash_count;
    result.risk_adjusted = result.avg_return / std::max(1.0, result.worst_dd);

    return result;
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "     V3 PARAMETER SWEEP - FINDING OPTIMAL PROTECTION" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;

    // Parameter ranges to test
    std::vector<double> stop_new_vals = {5.0, 8.0, 10.0, 12.0, 15.0};
    std::vector<double> partial_close_vals = {8.0, 10.0, 12.0, 15.0};
    std::vector<double> close_all_vals = {12.0, 15.0, 18.0, 20.0, 25.0};
    std::vector<int> max_pos_vals = {20, 30, 40, 50};

    std::vector<std::pair<V3Config, Result>> all_results;

    int total_combos = stop_new_vals.size() * partial_close_vals.size() * close_all_vals.size() * max_pos_vals.size();
    int combo = 0;

    std::cout << "Testing " << total_combos << " parameter combinations..." << std::endl;

    for (double stop_new : stop_new_vals) {
        for (double partial : partial_close_vals) {
            if (partial <= stop_new) continue;  // partial must be > stop_new
            for (double close_all : close_all_vals) {
                if (close_all <= partial) continue;  // close_all must be > partial
                for (int max_pos : max_pos_vals) {
                    V3Config cfg = {stop_new, partial, close_all, max_pos};
                    Result r = TestConfig(cfg);
                    all_results.push_back({cfg, r});
                    combo++;

                    if (combo % 50 == 0) {
                        std::cout << "  Progress: " << combo << "/" << total_combos << std::endl;
                    }
                }
            }
        }
    }

    std::cout << std::endl;
    std::cout << "Tested " << all_results.size() << " valid combinations" << std::endl;
    std::cout << std::endl;

    // Sort by risk-adjusted return
    std::sort(all_results.begin(), all_results.end(),
              [](const auto& a, const auto& b) { return a.second.risk_adjusted > b.second.risk_adjusted; });

    // Show top 10
    std::cout << "TOP 10 BY RISK-ADJUSTED RETURN:" << std::endl;
    std::cout << std::string(90, '-') << std::endl;
    std::cout << std::left << std::setw(8) << "StopNew" << std::setw(10) << "Partial"
              << std::setw(10) << "CloseAll" << std::setw(8) << "MaxPos"
              << " | " << std::setw(10) << "AvgRet" << std::setw(10) << "CrashAvg"
              << std::setw(10) << "WorstLoss" << std::setw(10) << "RiskAdj" << std::endl;
    std::cout << std::string(90, '-') << std::endl;

    for (int i = 0; i < std::min(10, (int)all_results.size()); i++) {
        const auto& [cfg, r] = all_results[i];
        std::cout << std::fixed << std::setprecision(1);
        std::cout << std::left << std::setw(8) << cfg.stop_new_at_dd
                  << std::setw(10) << cfg.partial_close_at_dd
                  << std::setw(10) << cfg.close_all_at_dd
                  << std::setw(8) << cfg.max_positions
                  << " | " << std::right << std::setw(8) << r.avg_return << "%"
                  << std::setw(9) << r.crash_avg << "%"
                  << std::setw(9) << r.worst_dd << "%"
                  << std::setw(10) << std::setprecision(3) << r.risk_adjusted << std::endl;
    }

    std::cout << std::string(90, '-') << std::endl;

    // Sort by crash performance
    std::sort(all_results.begin(), all_results.end(),
              [](const auto& a, const auto& b) { return a.second.crash_avg > b.second.crash_avg; });

    std::cout << std::endl << "TOP 10 BY CRASH SURVIVAL:" << std::endl;
    std::cout << std::string(90, '-') << std::endl;
    std::cout << std::left << std::setw(8) << "StopNew" << std::setw(10) << "Partial"
              << std::setw(10) << "CloseAll" << std::setw(8) << "MaxPos"
              << " | " << std::setw(10) << "AvgRet" << std::setw(10) << "CrashAvg"
              << std::setw(10) << "WorstLoss" << std::setw(10) << "RiskAdj" << std::endl;
    std::cout << std::string(90, '-') << std::endl;

    for (int i = 0; i < std::min(10, (int)all_results.size()); i++) {
        const auto& [cfg, r] = all_results[i];
        std::cout << std::fixed << std::setprecision(1);
        std::cout << std::left << std::setw(8) << cfg.stop_new_at_dd
                  << std::setw(10) << cfg.partial_close_at_dd
                  << std::setw(10) << cfg.close_all_at_dd
                  << std::setw(8) << cfg.max_positions
                  << " | " << std::right << std::setw(8) << r.avg_return << "%"
                  << std::setw(9) << r.crash_avg << "%"
                  << std::setw(9) << r.worst_dd << "%"
                  << std::setw(10) << std::setprecision(3) << r.risk_adjusted << std::endl;
    }

    std::cout << std::string(90, '-') << std::endl;
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;

    return 0;
}
