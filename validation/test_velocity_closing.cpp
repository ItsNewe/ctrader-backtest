#include "../include/synthetic_tick_generator.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cfloat>
#include <algorithm>
#include <deque>
#include <cmath>

using namespace backtest;

struct Config {
    std::string name;
    double close_at_dd;         // Close all at this DD%
    double fast_drop_threshold; // $/tick velocity above this = "fast" drop, wait for recovery
    int recovery_wait_ticks;    // Ticks to wait when fast drop detected
};

struct Result {
    double return_pct;
    double max_drawdown_pct;
    int fast_drops_detected;
    int closes_triggered;
};

Result RunWithVelocityClosing(const std::vector<Tick>& ticks, const Config& cfg, double initial_balance = 10000.0) {
    Result result = {0, 0, 0, 0};

    double balance = initial_balance;
    double equity = initial_balance;
    double peak_equity = initial_balance;
    double contract_size = 100.0;
    double leverage = 500.0;
    double spacing = 1.0;

    std::vector<Trade*> positions;
    std::deque<double> price_history;
    size_t next_id = 1;
    bool already_closed = false;
    int recovery_wait_counter = 0;
    double dd_when_wait_started = 0;

    for (size_t i = 0; i < ticks.size(); i++) {
        const Tick& tick = ticks[i];

        // Track price history
        price_history.push_back(tick.bid);
        if (price_history.size() > 100) price_history.pop_front();

        // Calculate drop velocity ($/tick over last 20 ticks)
        double velocity = 0;
        if (price_history.size() >= 20) {
            velocity = (price_history[price_history.size() - 20] - tick.bid) / 20.0;
        }

        // Update equity
        equity = balance;
        for (Trade* t : positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * contract_size;
        }

        // Track drawdown
        if (equity > peak_equity) {
            peak_equity = equity;
            already_closed = false;
            recovery_wait_counter = 0;
        }
        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        result.max_drawdown_pct = std::max(result.max_drawdown_pct, dd_pct);

        // Velocity-based closing logic
        if (dd_pct > cfg.close_at_dd && !already_closed && !positions.empty()) {
            bool is_fast_drop = velocity > cfg.fast_drop_threshold;

            if (is_fast_drop && recovery_wait_counter == 0) {
                // Fast drop detected - start waiting for potential recovery
                recovery_wait_counter = cfg.recovery_wait_ticks;
                dd_when_wait_started = dd_pct;
                result.fast_drops_detected++;
            } else if (recovery_wait_counter > 0) {
                recovery_wait_counter--;

                // If DD reduced significantly, cancel close (recovering)
                if (dd_pct < dd_when_wait_started * 0.7) {
                    recovery_wait_counter = 0;  // Cancel - it's recovering
                }
                // If wait expired and still above threshold, close
                else if (recovery_wait_counter == 0 && dd_pct > cfg.close_at_dd) {
                    goto do_close;
                }
            } else {
                // Slow drop - close immediately
                do_close:
                for (Trade* t : positions) {
                    double pl = (tick.bid - t->entry_price) * t->lot_size * contract_size;
                    balance += pl;
                    delete t;
                }
                positions.clear();
                already_closed = true;
                result.closes_triggered++;
                equity = balance;
                peak_equity = equity;
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

        // Open new positions
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
    std::cout << "     VELOCITY-BASED CLOSING - FAST vs SLOW DROP DETECTION" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;
    std::cout << "Hypothesis: Fast drops often recover (flash crash), slow drops don't" << std::endl;
    std::cout << std::endl;

    std::vector<Config> configs = {
        {"No Closing",       999.0,  999.0,   0},
        {"Immediate @20%",    20.0,  999.0,   0},  // Baseline - close everything
        {"Vel>0.5 wait100",   20.0,    0.5, 100},  // Fast=0.5$/tick, wait 100 ticks
        {"Vel>1.0 wait100",   20.0,    1.0, 100},  // Fast=1.0$/tick, wait 100 ticks
        {"Vel>2.0 wait100",   20.0,    2.0, 100},  // Fast=2.0$/tick, wait 100 ticks
        {"Vel>1.0 wait200",   20.0,    1.0, 200},  // Fast=1.0$/tick, wait 200 ticks
        {"Immediate @15%",    15.0,  999.0,   0},
        {"Vel>1.0 w100 @15",  15.0,    1.0, 100},
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
        {"SlowDrop 8%", [](SyntheticTickGenerator& g) { g.GenerateTrend(5000, -208, 0.05); }},  // Same % as flash but slow
        {"Bear 15%", [](SyntheticTickGenerator& g) { g.GenerateBearMarket(10000, 15, 4); }},
    };

    // Header
    std::cout << std::left << std::setw(16) << "Scenario";
    for (const auto& c : configs) {
        std::cout << " | " << std::setw(15) << c.name;
    }
    std::cout << std::endl << std::string(16 + configs.size() * 18, '-') << std::endl;

    std::vector<std::vector<Result>> all_results;

    for (size_t s = 0; s < scenarios.size(); s++) {
        std::vector<Result> row;
        std::cout << std::left << std::setw(16) << scenarios[s].name;

        for (size_t c = 0; c < configs.size(); c++) {
            SyntheticTickGenerator gen(2600.0, 0.25, s * 100 + c);
            scenarios[s].gen(gen);
            Result r = RunWithVelocityClosing(gen.GetTicks(), configs[c]);
            row.push_back(r);

            std::cout << std::fixed << std::setprecision(1);
            std::cout << " | " << std::right << std::setw(5) << r.return_pct << "%";
            std::cout << " DD" << std::setw(2) << (int)r.max_drawdown_pct << "%";
        }
        std::cout << std::endl;
        all_results.push_back(row);
    }

    std::cout << std::string(16 + configs.size() * 18, '-') << std::endl;

    // Summary stats
    std::cout << std::left << std::setw(16) << "AVG RETURN";
    for (size_t c = 0; c < configs.size(); c++) {
        double sum = 0;
        for (const auto& row : all_results) sum += row[c].return_pct;
        std::cout << " | " << std::right << std::setw(15) << (sum / scenarios.size()) << "%";
    }
    std::cout << std::endl;

    std::cout << std::left << std::setw(16) << "WORST DD";
    for (size_t c = 0; c < configs.size(); c++) {
        double worst = 0;
        for (const auto& row : all_results) worst = std::max(worst, row[c].max_drawdown_pct);
        std::cout << " | " << std::right << std::setw(15) << worst << "%";
    }
    std::cout << std::endl;

    // Compare Flash Crash vs Slow Drop specifically
    std::cout << std::endl << "FLASH vs SLOW COMPARISON (8% drops):" << std::endl;
    size_t flash_idx = 6, slow_idx = 7;
    std::cout << std::left << std::setw(16) << "Flash Crash 8%";
    for (size_t c = 0; c < configs.size(); c++) {
        std::cout << " | " << std::right << std::setw(15) << all_results[flash_idx][c].return_pct << "%";
    }
    std::cout << std::endl;
    std::cout << std::left << std::setw(16) << "Slow Drop 8%";
    for (size_t c = 0; c < configs.size(); c++) {
        std::cout << " | " << std::right << std::setw(15) << all_results[slow_idx][c].return_pct << "%";
    }
    std::cout << std::endl;

    std::cout << std::endl;
    std::cout << "NOTE: Velocity = price drop rate ($/tick). High velocity = potential flash crash" << std::endl;
    std::cout << "================================================================" << std::endl;

    return 0;
}
