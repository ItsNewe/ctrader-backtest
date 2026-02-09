#include "../include/synthetic_tick_generator.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <deque>
#include <algorithm>
#include <cfloat>
#include <cmath>

using namespace backtest;

struct V4Config {
    std::string name;

    // V3 base parameters
    double stop_new_at_dd;
    double partial_close_at_dd;
    double close_all_at_dd;
    int max_positions;

    // V4 improvements
    bool dynamic_spacing;        // Adjust spacing based on volatility
    double base_spacing;
    double max_spacing_mult;     // Max multiplier for spacing

    bool trend_filter;           // Reduce size in downtrends
    double trend_size_reduction; // Reduction factor in downtrend

    bool profit_lock;            // Lock in profits
    double profit_lock_pct;      // Lock profits after this % gain
    double lock_dd_from_profit;  // Allow this much DD from locked profit

    int cooldown_ticks;          // Ticks to wait after close-all
};

struct Result {
    double return_pct;
    double max_dd;
    int max_positions_used;
    int close_all_triggers;
};

class V4Strategy {
public:
    V4Config cfg;

    double balance;
    double equity;
    double peak_equity;
    double locked_profit_level;

    std::vector<Trade*> positions;
    std::deque<double> price_history;
    size_t next_id;

    bool partial_done;
    bool all_closed;
    int cooldown_remaining;
    int close_all_count;
    int max_pos_used;
    double max_dd_seen;

    double current_spacing;

    V4Strategy(const V4Config& config) : cfg(config) {
        Reset(10000.0);
    }

    void Reset(double initial_balance) {
        balance = initial_balance;
        equity = initial_balance;
        peak_equity = initial_balance;
        locked_profit_level = 0;
        positions.clear();
        price_history.clear();
        next_id = 1;
        partial_done = false;
        all_closed = false;
        cooldown_remaining = 0;
        close_all_count = 0;
        max_pos_used = 0;
        max_dd_seen = 0;
        current_spacing = cfg.base_spacing;
    }

    double CalculateVolatility() {
        if (price_history.size() < 20) return 0;

        double sum = 0, sum_sq = 0;
        int n = price_history.size() - 1;

        for (size_t i = 1; i < price_history.size(); i++) {
            double change = std::abs(price_history[i] - price_history[i-1]);
            sum += change;
            sum_sq += change * change;
        }

        double mean = sum / n;
        double variance = (sum_sq / n) - (mean * mean);
        return std::sqrt(std::max(0.0, variance));
    }

    double CalculateTrend() {
        if (price_history.size() < 50) return 0;

        // Simple: compare recent price to older price
        double recent = 0, older = 0;
        int count = 10;

        for (int i = 0; i < count; i++) {
            recent += price_history[price_history.size() - 1 - i];
            older += price_history[price_history.size() - 40 - i];
        }

        recent /= count;
        older /= count;

        return (recent - older) / older * 100.0; // % change
    }

    Result Run(const std::vector<Tick>& ticks, double initial_balance = 10000.0) {
        Reset(initial_balance);

        double contract_size = 100.0;
        double leverage = 500.0;

        for (const Tick& tick : ticks) {
            // Track price history
            price_history.push_back(tick.bid);
            if (price_history.size() > 100) price_history.pop_front();

            // Update equity
            equity = balance;
            for (Trade* t : positions) {
                equity += (tick.bid - t->entry_price) * t->lot_size * contract_size;
            }

            // Update peak and check profit lock
            if (equity > peak_equity) {
                peak_equity = equity;
                partial_done = false;
                all_closed = false;

                // Profit lock: update locked level
                if (cfg.profit_lock) {
                    double profit_pct = (equity - initial_balance) / initial_balance * 100.0;
                    if (profit_pct > cfg.profit_lock_pct) {
                        double new_lock = equity * (1.0 - cfg.lock_dd_from_profit / 100.0);
                        if (new_lock > locked_profit_level) {
                            locked_profit_level = new_lock;
                        }
                    }
                }
            }

            // Calculate drawdown
            double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
            max_dd_seen = std::max(max_dd_seen, dd_pct);

            // Check profit lock violation
            if (cfg.profit_lock && locked_profit_level > 0 && equity < locked_profit_level) {
                // Close all to protect locked profit
                for (Trade* t : positions) {
                    balance += (tick.bid - t->entry_price) * t->lot_size * contract_size;
                    delete t;
                }
                positions.clear();
                close_all_count++;
                cooldown_remaining = cfg.cooldown_ticks;
                equity = balance;
                peak_equity = equity;
                locked_profit_level = 0; // Reset lock
                continue;
            }

            // V3 protection: Close ALL
            if (dd_pct > cfg.close_all_at_dd && !all_closed && !positions.empty()) {
                for (Trade* t : positions) {
                    balance += (tick.bid - t->entry_price) * t->lot_size * contract_size;
                    delete t;
                }
                positions.clear();
                all_closed = true;
                close_all_count++;
                cooldown_remaining = cfg.cooldown_ticks;
                equity = balance;
                peak_equity = equity;
                continue;
            }

            // V3 protection: Partial close
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

            // Cooldown check
            if (cooldown_remaining > 0) {
                cooldown_remaining--;
                continue;
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

            // Dynamic spacing
            if (cfg.dynamic_spacing) {
                double vol = CalculateVolatility();
                double vol_mult = 1.0 + std::min(vol * 2.0, cfg.max_spacing_mult - 1.0);
                current_spacing = cfg.base_spacing * vol_mult;
            } else {
                current_spacing = cfg.base_spacing;
            }

            // Open new positions
            if (dd_pct < cfg.stop_new_at_dd && (int)positions.size() < cfg.max_positions) {
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

                    // Trend filter
                    if (cfg.trend_filter) {
                        double trend = CalculateTrend();
                        if (trend < -1.0) { // Downtrend > 1%
                            lot *= cfg.trend_size_reduction;
                        }
                    }

                    lot = std::max(0.01, lot);

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

            max_pos_used = std::max(max_pos_used, (int)positions.size());
        }

        // Close remaining
        if (!ticks.empty()) {
            for (Trade* t : positions) {
                balance += (ticks.back().bid - t->entry_price) * t->lot_size * contract_size;
                delete t;
            }
            positions.clear();
        }

        Result r;
        r.return_pct = (balance - initial_balance) / initial_balance * 100.0;
        r.max_dd = max_dd_seen;
        r.max_positions_used = max_pos_used;
        r.close_all_triggers = close_all_count;
        return r;
    }
};

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
    std::cout << "     V4 IMPROVEMENTS TESTING" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::endl;

    // Configurations to test
    std::vector<V4Config> configs = {
        // V3 Baseline
        {"V3 Baseline", 5.0, 8.0, 25.0, 20, false, 1.0, 1.0, false, 1.0, false, 0, 0, 0},

        // Dynamic spacing
        {"+ DynSpacing 2x", 5.0, 8.0, 25.0, 20, true, 1.0, 2.0, false, 1.0, false, 0, 0, 0},
        {"+ DynSpacing 3x", 5.0, 8.0, 25.0, 20, true, 1.0, 3.0, false, 1.0, false, 0, 0, 0},

        // Trend filter
        {"+ TrendFilter 50%", 5.0, 8.0, 25.0, 20, false, 1.0, 1.0, true, 0.5, false, 0, 0, 0},
        {"+ TrendFilter 25%", 5.0, 8.0, 25.0, 20, false, 1.0, 1.0, true, 0.25, false, 0, 0, 0},

        // Profit lock
        {"+ ProfitLock 10%/5%", 5.0, 8.0, 25.0, 20, false, 1.0, 1.0, false, 1.0, true, 10.0, 5.0, 0},
        {"+ ProfitLock 20%/10%", 5.0, 8.0, 25.0, 20, false, 1.0, 1.0, false, 1.0, true, 20.0, 10.0, 0},

        // Cooldown
        {"+ Cooldown 100", 5.0, 8.0, 25.0, 20, false, 1.0, 1.0, false, 1.0, false, 0, 0, 100},
        {"+ Cooldown 500", 5.0, 8.0, 25.0, 20, false, 1.0, 1.0, false, 1.0, false, 0, 0, 500},

        // Combined
        {"V4 Combined", 5.0, 8.0, 25.0, 20, true, 1.0, 2.0, true, 0.5, true, 15.0, 8.0, 100},
    };

    // Synthetic scenarios
    struct Scenario {
        std::string name;
        std::function<void(SyntheticTickGenerator&)> gen;
    };

    std::vector<Scenario> scenarios = {
        {"Uptrend +$100", [](SyntheticTickGenerator& g) { g.GenerateTrend(10000, 100, 0.1); }},
        {"Sideways $20", [](SyntheticTickGenerator& g) { g.GenerateSideways(10000, 20); }},
        {"Crash 5%", [](SyntheticTickGenerator& g) { g.GenerateCrash(1000, 5); }},
        {"Crash 10%", [](SyntheticTickGenerator& g) { g.GenerateCrash(1000, 10); }},
        {"V-Recovery 10%", [](SyntheticTickGenerator& g) { g.GenerateVRecovery(2000, 2000, 10); }},
        {"Flash Crash 8%", [](SyntheticTickGenerator& g) { g.GenerateFlashCrash(8, 100, 500); }},
        {"Bear 10%", [](SyntheticTickGenerator& g) { g.GenerateBearMarket(5000, 10, 3); }},
        {"Volatile Walk", [](SyntheticTickGenerator& g) { g.GenerateRandomWalk(10000, 0.5, 0); }},
    };

    std::cout << "PART 1: SYNTHETIC SCENARIOS" << std::endl;
    std::cout << std::string(100, '-') << std::endl;

    // Header
    std::cout << std::left << std::setw(20) << "Config";
    std::cout << " | " << std::setw(8) << "AvgRet";
    std::cout << " | " << std::setw(8) << "WorstDD";
    std::cout << " | " << std::setw(10) << "CrashAvg";
    std::cout << " | " << std::setw(10) << "RiskAdj" << std::endl;
    std::cout << std::string(70, '-') << std::endl;

    std::vector<std::tuple<std::string, double, double, double>> summary;

    for (const auto& cfg : configs) {
        V4Strategy strategy(cfg);

        double total_ret = 0;
        double worst_dd = 0;
        double crash_total = 0;
        int crash_count = 0;

        for (size_t i = 0; i < scenarios.size(); i++) {
            SyntheticTickGenerator gen(2600.0, 0.25, i + 1);
            scenarios[i].gen(gen);

            Result r = strategy.Run(gen.GetTicks());
            total_ret += r.return_pct;
            worst_dd = std::max(worst_dd, r.max_dd);

            // Crash scenarios: indices 2-6
            if (i >= 2 && i <= 6) {
                crash_total += r.return_pct;
                crash_count++;
            }
        }

        double avg_ret = total_ret / scenarios.size();
        double crash_avg = crash_total / crash_count;
        double risk_adj = avg_ret / std::max(1.0, worst_dd);

        summary.push_back({cfg.name, avg_ret, worst_dd, crash_avg});

        std::cout << std::fixed << std::setprecision(2);
        std::cout << std::left << std::setw(20) << cfg.name;
        std::cout << " | " << std::right << std::setw(7) << avg_ret << "%";
        std::cout << " | " << std::setw(7) << worst_dd << "%";
        std::cout << " | " << std::setw(9) << crash_avg << "%";
        std::cout << " | " << std::setw(10) << std::setprecision(4) << risk_adj << std::endl;
    }

    std::cout << std::string(70, '-') << std::endl;

    // Test on real data
    std::cout << std::endl << "PART 2: REAL DATA (Dec 2025 Crash)" << std::endl;
    std::cout << std::string(70, '-') << std::endl;

    std::string filename = "../validation/Grid/XAUUSD_TICKS_2025.csv";

    std::cout << "Loading crash period..." << std::flush;
    auto crash_ticks = LoadTicks(filename, 51314023, 2000000);
    std::cout << " " << crash_ticks.size() << " ticks" << std::endl;

    if (crash_ticks.size() > 1000) {
        std::cout << std::left << std::setw(20) << "Config";
        std::cout << " | " << std::setw(10) << "Return";
        std::cout << " | " << std::setw(10) << "MaxDD";
        std::cout << " | " << std::setw(10) << "CloseAlls" << std::endl;
        std::cout << std::string(60, '-') << std::endl;

        for (const auto& cfg : configs) {
            V4Strategy strategy(cfg);
            Result r = strategy.Run(crash_ticks);

            std::cout << std::fixed << std::setprecision(2);
            std::cout << std::left << std::setw(20) << cfg.name;
            std::cout << " | " << std::right << std::setw(9) << r.return_pct << "%";
            std::cout << " | " << std::setw(9) << r.max_dd << "%";
            std::cout << " | " << std::setw(10) << r.close_all_triggers << std::endl;
        }
    }

    std::cout << std::string(70, '-') << std::endl;
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;

    return 0;
}
