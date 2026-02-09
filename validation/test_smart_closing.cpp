#include "../include/synthetic_tick_generator.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cfloat>
#include <algorithm>
#include <deque>

using namespace backtest;

struct Config {
    std::string name;
    double close_at_dd;         // Close all at this DD%
    int confirm_ticks;          // Wait this many ticks before closing
    bool detect_recovery;       // If true, cancel close if price recovering
};

struct Result {
    double return_pct;
    double max_drawdown_pct;
    int positions_closed_by_dd;
};

Result RunWithSmartClosing(const std::vector<Tick>& ticks, const Config& cfg, double initial_balance = 10000.0) {
    Result result = {0, 0, 0};

    double balance = initial_balance;
    double equity = initial_balance;
    double peak_equity = initial_balance;
    double contract_size = 100.0;
    double leverage = 500.0;
    double spacing = 1.0;

    std::vector<Trade*> positions;
    std::deque<double> recent_prices;
    size_t next_id = 1;
    int ticks_above_threshold = 0;
    bool already_closed = false;

    for (size_t i = 0; i < ticks.size(); i++) {
        const Tick& tick = ticks[i];

        // Track recent prices for recovery detection
        recent_prices.push_back(tick.bid);
        if (recent_prices.size() > 50) recent_prices.pop_front();

        // Update equity
        equity = balance;
        for (Trade* t : positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * contract_size;
        }

        // Track drawdown
        if (equity > peak_equity) {
            peak_equity = equity;
            ticks_above_threshold = 0;
            already_closed = false;
        }
        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        result.max_drawdown_pct = std::max(result.max_drawdown_pct, dd_pct);

        // Smart closing logic
        if (dd_pct > cfg.close_at_dd && !already_closed && !positions.empty()) {
            ticks_above_threshold++;

            // Check if we should close
            bool should_close = false;

            if (ticks_above_threshold >= cfg.confirm_ticks) {
                if (cfg.detect_recovery && recent_prices.size() >= 20) {
                    // Check if price is recovering (last 10 ticks trending up)
                    double recent_change = recent_prices.back() - recent_prices[recent_prices.size() - 10];
                    double older_change = recent_prices[recent_prices.size() - 10] - recent_prices[recent_prices.size() - 20];

                    // If still dropping or dropping faster, close
                    if (recent_change <= 0 || recent_change < older_change) {
                        should_close = true;
                    }
                    // If recovering, wait
                } else {
                    should_close = true;
                }
            }

            if (should_close) {
                for (Trade* t : positions) {
                    double pl = (tick.bid - t->entry_price) * t->lot_size * contract_size;
                    balance += pl;
                    result.positions_closed_by_dd++;
                    delete t;
                }
                positions.clear();
                already_closed = true;
                equity = balance;
                peak_equity = equity;
            }
        } else if (dd_pct <= cfg.close_at_dd) {
            ticks_above_threshold = 0;
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

        // Open new positions (only if not already closed and DD below threshold)
        if (!already_closed && dd_pct < cfg.close_at_dd && (int)positions.size() < 50) {
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
    std::cout << "     SMART CLOSING - CONFIRMATION & RECOVERY DETECTION" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;

    std::vector<Config> configs = {
        {"No Closing",         999.0,   0, false},
        {"Immediate @20%",      20.0,   0, false},
        {"Confirm 10t @20%",    20.0,  10, false},
        {"Confirm 25t @20%",    20.0,  25, false},
        {"Confirm 50t @20%",    20.0,  50, false},
        {"Recovery Det @20%",   20.0,  10, true},
        {"Confirm 15t @15%",    15.0,  15, false},
        {"Recovery Det @15%",   15.0,  15, true},
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
        {"V-Recovery 10%", [](SyntheticTickGenerator& g) { g.GenerateVRecovery(2000, 2000, 10); }},
        {"Flash Crash 8%", [](SyntheticTickGenerator& g) { g.GenerateFlashCrash(8, 100, 500); }},
        {"SlowCrash+Bounce", [](SyntheticTickGenerator& g) {
            g.GenerateTrend(2000, -100, 0.05);  // Slow crash
            g.GenerateTrend(500, 20, 0.02);     // Small bounce
            g.GenerateTrend(1500, -50, 0.05);   // Continue down
        }},
        {"Bear 15%", [](SyntheticTickGenerator& g) { g.GenerateBearMarket(10000, 15, 4); }},
    };

    // Header
    std::cout << std::left << std::setw(18) << "Scenario";
    for (const auto& c : configs) {
        std::cout << " | " << std::setw(17) << c.name;
    }
    std::cout << std::endl << std::string(18 + configs.size() * 20, '-') << std::endl;

    std::vector<std::vector<Result>> all_results;

    for (size_t s = 0; s < scenarios.size(); s++) {
        std::vector<Result> row;
        std::cout << std::left << std::setw(18) << scenarios[s].name;

        for (size_t c = 0; c < configs.size(); c++) {
            SyntheticTickGenerator gen(2600.0, 0.25, s * 100 + c);
            scenarios[s].gen(gen);
            Result r = RunWithSmartClosing(gen.GetTicks(), configs[c]);
            row.push_back(r);

            std::cout << std::fixed << std::setprecision(1);
            std::cout << " | " << std::right << std::setw(6) << r.return_pct << "%";
            std::cout << " DD" << std::setw(3) << (int)r.max_drawdown_pct << "%";
        }
        std::cout << std::endl;
        all_results.push_back(row);
    }

    std::cout << std::string(18 + configs.size() * 20, '-') << std::endl;

    // Averages
    std::cout << std::left << std::setw(18) << "AVG RETURN";
    for (size_t c = 0; c < configs.size(); c++) {
        double sum = 0;
        for (const auto& row : all_results) sum += row[c].return_pct;
        std::cout << " | " << std::right << std::setw(17) << (sum / scenarios.size()) << "%";
    }
    std::cout << std::endl;

    std::cout << std::left << std::setw(18) << "WORST DD";
    for (size_t c = 0; c < configs.size(); c++) {
        double worst = 0;
        for (const auto& row : all_results) worst = std::max(worst, row[c].max_drawdown_pct);
        std::cout << " | " << std::right << std::setw(17) << worst << "%";
    }
    std::cout << std::endl;

    // Crash-specific performance
    std::cout << std::endl << "CRASH SCENARIOS ONLY:" << std::endl;
    std::vector<size_t> crash_indices = {2, 3, 4, 5, 6, 7, 8};  // Crash scenarios
    std::cout << std::left << std::setw(18) << "AVG CRASH RETURN";
    for (size_t c = 0; c < configs.size(); c++) {
        double sum = 0;
        for (size_t idx : crash_indices) sum += all_results[idx][c].return_pct;
        std::cout << " | " << std::right << std::setw(17) << (sum / crash_indices.size()) << "%";
    }
    std::cout << std::endl;

    std::cout << std::endl;
    std::cout << "NOTE: 'Confirm Nt' = wait N ticks above DD threshold before closing" << std::endl;
    std::cout << "      'Recovery Det' = cancel close if price is recovering" << std::endl;
    std::cout << "================================================================" << std::endl;

    return 0;
}
