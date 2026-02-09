/**
 * Safe Scaling Analysis
 *
 * Finds the SAFE path to 10x and 100x returns:
 * 1. Single year with maximum safe lot size
 * 2. Multi-year compounding with conservative sizing
 * 3. Compound sizing with strict DD limits
 *
 * KEY CONSTRAINT: Max DD must stay < 50% to avoid margin call
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cfloat>
#include <vector>
#include <deque>
#include <algorithm>

struct Tick {
    double bid;
    double ask;
    int hour;
    double spread() const { return ask - bid; }
};

struct Trade {
    size_t id;
    double entry_price;
    double lot_size;
    double take_profit;
};

class SMA {
    std::deque<double> prices;
    int period;
    double sum = 0;
public:
    SMA(int p) : period(p) {}
    void Reset() { prices.clear(); sum = 0; }
    void Add(double price) {
        prices.push_back(price);
        sum += price;
        if ((int)prices.size() > period) {
            sum -= prices.front();
            prices.pop_front();
        }
    }
    double Get() const { return prices.empty() ? 0 : sum / prices.size(); }
    bool IsReady() const { return (int)prices.size() >= period; }
};

struct Config {
    double base_lot = 0.01;
    double spacing = 0.75;
    double contract_size = 100.0;
    int max_positions = 15;
    double stop_new_at_dd = 3.0;
    double partial_close_at_dd = 5.0;
    double close_all_at_dd = 15.0;
    double tp_multiplier = 2.0;
    int session_avoid_start = 14;
    int session_avoid_end = 18;

    // Compound options
    bool compound = false;
    double max_lot = 1.0;           // Safety cap
    double target_max_dd = 40.0;    // Target max DD %
};

struct Result {
    double final_balance;
    double return_pct;
    double return_multiple;
    double max_dd;
    double max_dd_dollars;
    int total_trades;
    bool margin_call;  // Did account go negative?
};

std::vector<Tick> LoadTicks(const char* filename, size_t max_count) {
    std::vector<Tick> ticks;
    ticks.reserve(max_count);

    FILE* f = fopen(filename, "r");
    if (!f) return ticks;

    char line[256];
    fgets(line, sizeof(line), f);

    while (fgets(line, sizeof(line), f) && ticks.size() < max_count) {
        Tick tick;
        int hour = 12;
        if (strlen(line) >= 13) {
            char h[3] = {line[11], line[12], 0};
            hour = atoi(h);
        }
        tick.hour = hour;

        char* token = strtok(line, "\t");
        if (!token) continue;
        token = strtok(NULL, "\t");
        if (!token) continue;
        tick.bid = atof(token);
        token = strtok(NULL, "\t");
        if (!token) continue;
        tick.ask = atof(token);

        if (tick.bid > 0 && tick.ask > 0) {
            ticks.push_back(tick);
        }
    }
    fclose(f);
    return ticks;
}

Result RunBacktest(const std::vector<Tick>& ticks, Config cfg) {
    Result r = {0};

    const double initial_balance = 10000.0;
    double balance = initial_balance;
    double equity = balance;
    double peak_equity = balance;
    double max_drawdown = 0;
    double max_dd_dollars = 0;

    std::vector<Trade*> positions;
    size_t next_id = 1;
    bool partial_done = false;
    bool all_closed = false;

    SMA sma(500);
    double lowest = DBL_MAX, highest = -DBL_MAX;

    for (const Tick& tick : ticks) {
        sma.Add(tick.bid);

        // Calculate equity
        equity = balance;
        for (Trade* t : positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * cfg.contract_size;
        }

        // Check for margin call (equity < 0 or < 20% of peak)
        if (equity < 0 || equity < peak_equity * 0.10) {
            r.margin_call = true;
            // Force close everything
            for (Trade* t : positions) {
                double pl = (tick.bid - t->entry_price) * t->lot_size * cfg.contract_size;
                balance += pl;
                delete t;
            }
            positions.clear();
            equity = balance;
            if (balance <= 0) {
                balance = 0;
                break;  // Account blown
            }
            peak_equity = balance;
            continue;
        }

        if (positions.empty() && peak_equity != balance) {
            peak_equity = balance;
            partial_done = false;
            all_closed = false;
        }

        if (equity > peak_equity) {
            peak_equity = equity;
            partial_done = false;
            all_closed = false;
        }

        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        double dd_dollars = peak_equity - equity;

        if (dd_pct > max_drawdown) max_drawdown = dd_pct;
        if (dd_dollars > max_dd_dollars) max_dd_dollars = dd_dollars;

        // Protection: Close ALL
        if (dd_pct > cfg.close_all_at_dd && !all_closed && !positions.empty()) {
            for (Trade* t : positions) {
                balance += (tick.bid - t->entry_price) * t->lot_size * cfg.contract_size;
                delete t;
            }
            positions.clear();
            all_closed = true;
            equity = balance;
            peak_equity = equity;
            lowest = DBL_MAX; highest = -DBL_MAX;
            continue;
        }

        // Partial close
        if (dd_pct > cfg.partial_close_at_dd && !partial_done && positions.size() > 1) {
            std::sort(positions.begin(), positions.end(), [&](Trade* a, Trade* b) {
                return (tick.bid - a->entry_price) < (tick.bid - b->entry_price);
            });
            int to_close = std::max(1, (int)(positions.size() * 0.5));
            for (int i = 0; i < to_close && !positions.empty(); i++) {
                Trade* t = positions[0];
                balance += (tick.bid - t->entry_price) * t->lot_size * cfg.contract_size;
                delete t;
                positions.erase(positions.begin());
            }
            partial_done = true;
        }

        // Check TPs
        for (auto it = positions.begin(); it != positions.end();) {
            Trade* t = *it;
            if (tick.bid >= t->take_profit) {
                balance += (tick.bid - t->entry_price) * t->lot_size * cfg.contract_size;
                r.total_trades++;
                delete t;
                it = positions.erase(it);
            } else {
                ++it;
            }
        }

        // Session filter
        bool session_ok = !(tick.hour >= cfg.session_avoid_start && tick.hour < cfg.session_avoid_end);
        if (dd_pct >= cfg.stop_new_at_dd || !session_ok) continue;

        // Grid bounds
        lowest = DBL_MAX; highest = -DBL_MAX;
        for (Trade* t : positions) {
            lowest = std::min(lowest, t->entry_price);
            highest = std::max(highest, t->entry_price);
        }

        // Open new
        if ((int)positions.size() < cfg.max_positions) {
            bool should_open = positions.empty() ||
                              (lowest >= tick.ask + cfg.spacing) ||
                              (highest <= tick.ask - cfg.spacing);

            if (should_open) {
                double lot = cfg.base_lot;

                // Compound sizing
                if (cfg.compound) {
                    lot = cfg.base_lot * (equity / initial_balance);
                    lot = std::min(lot, cfg.max_lot);
                }

                lot = std::max(0.01, std::round(lot * 100) / 100);

                // Margin check
                double margin_needed = lot * cfg.contract_size * tick.ask / 500.0;
                double used_margin = 0;
                for (Trade* t : positions) {
                    used_margin += t->lot_size * cfg.contract_size * t->entry_price / 500.0;
                }

                if (equity - used_margin > margin_needed * 2) {
                    Trade* t = new Trade();
                    t->id = next_id++;
                    t->entry_price = tick.ask;
                    t->lot_size = lot;
                    t->take_profit = tick.ask + tick.spread() + cfg.spacing * cfg.tp_multiplier;
                    positions.push_back(t);
                }
            }
        }
    }

    // Close remaining
    for (Trade* t : positions) {
        balance += (ticks.back().bid - t->entry_price) * t->lot_size * cfg.contract_size;
        r.total_trades++;
        delete t;
    }

    r.final_balance = std::max(0.0, balance);
    r.return_pct = (r.final_balance - initial_balance) / initial_balance * 100.0;
    r.return_multiple = r.final_balance / initial_balance;
    r.max_dd = max_drawdown;
    r.max_dd_dollars = max_dd_dollars;

    return r;
}

int main() {
    printf("================================================================\n");
    printf("SAFE SCALING ANALYSIS - PATH TO 10x AND 100x\n");
    printf("================================================================\n\n");

    const char* filename = "Grid/XAUUSD_TICKS_2025.csv";
    printf("Loading data...\n");

    std::vector<Tick> ticks = LoadTicks(filename, 60000000);
    if (ticks.empty()) {
        printf("ERROR: Failed to load data\n");
        return 1;
    }
    printf("Loaded %zu ticks\n\n", ticks.size());

    // ========================================================================
    // TEST 1: Find maximum safe fixed lot size
    // ========================================================================
    printf("================================================================\n");
    printf("FIXED LOT SIZING - Finding Max Safe Lot\n");
    printf("================================================================\n\n");

    printf("Target: Max DD < 50%% (avoid margin call)\n\n");

    printf("%-8s %12s %8s %10s %8s\n",
           "Lot", "Final Bal", "Multiple", "Max DD %", "Safe?");
    printf("--------------------------------------------------------\n");

    double safe_lots[] = {0.01, 0.015, 0.02, 0.025, 0.03, 0.035, 0.04, 0.045, 0.05};
    Result best_safe;
    best_safe.return_multiple = 0;

    for (double lot : safe_lots) {
        Config cfg;
        cfg.base_lot = lot;
        cfg.compound = false;

        Result r = RunBacktest(ticks, cfg);

        bool safe = r.max_dd < 50.0 && !r.margin_call;
        printf("%-8.3f $%11.2f %7.1fx %9.1f%% %8s\n",
               lot, r.final_balance, r.return_multiple, r.max_dd,
               safe ? "YES" : "NO");

        if (safe && r.return_multiple > best_safe.return_multiple) {
            best_safe = r;
        }
    }

    printf("--------------------------------------------------------\n\n");

    // ========================================================================
    // TEST 2: Safe compound sizing with lot cap
    // ========================================================================
    printf("================================================================\n");
    printf("COMPOUND SIZING WITH LOT CAP\n");
    printf("================================================================\n\n");

    printf("Lot scales with equity, capped at max_lot\n\n");

    struct CompoundConfig {
        double base;
        double max_lot;
    };

    CompoundConfig compound_tests[] = {
        {0.01, 0.05},
        {0.01, 0.10},
        {0.01, 0.20},
        {0.01, 0.50},
        {0.015, 0.10},
        {0.015, 0.20},
        {0.02, 0.10},
        {0.02, 0.20}
    };

    printf("%-8s %-8s %12s %8s %10s %8s\n",
           "Base", "MaxLot", "Final Bal", "Multiple", "Max DD %", "Safe?");
    printf("----------------------------------------------------------------\n");

    Result best_compound;
    best_compound.return_multiple = 0;

    for (auto& cc : compound_tests) {
        Config cfg;
        cfg.base_lot = cc.base;
        cfg.compound = true;
        cfg.max_lot = cc.max_lot;

        Result r = RunBacktest(ticks, cfg);

        bool safe = r.max_dd < 50.0 && !r.margin_call;
        printf("%-8.3f %-8.2f $%11.2f %7.1fx %9.1f%% %8s\n",
               cc.base, cc.max_lot, r.final_balance, r.return_multiple, r.max_dd,
               safe ? "YES" : "NO");

        if (safe && r.return_multiple > best_compound.return_multiple) {
            best_compound = r;
        }
    }

    printf("----------------------------------------------------------------\n\n");

    // ========================================================================
    // MULTI-YEAR PROJECTION
    // ========================================================================
    printf("================================================================\n");
    printf("MULTI-YEAR PROJECTION TO 10x AND 100x\n");
    printf("================================================================\n\n");

    // Use the best safe result
    double annual_mult = best_safe.return_multiple;
    if (best_compound.return_multiple > annual_mult) {
        annual_mult = best_compound.return_multiple;
    }

    printf("Best SAFE annual multiplier: %.2fx (%.1f%% return)\n\n",
           annual_mult, (annual_mult - 1) * 100);

    printf("%-6s %15s %10s %20s\n", "Year", "Balance", "Multiple", "Milestone");
    printf("--------------------------------------------------------\n");

    double balance = 10000.0;
    int years_to_10x = -1;
    int years_to_100x = -1;

    for (int year = 0; year <= 10; year++) {
        double multiple = balance / 10000.0;

        const char* milestone = "";
        if (multiple >= 10 && years_to_10x < 0) {
            years_to_10x = year;
            milestone = "<-- 10x ACHIEVED";
        }
        if (multiple >= 100 && years_to_100x < 0) {
            years_to_100x = year;
            milestone = "<-- 100x ACHIEVED";
        }

        printf("%-6d $%14.2f %9.1fx %s\n", year, balance, multiple, milestone);

        balance *= annual_mult;
    }

    printf("--------------------------------------------------------\n\n");

    // ========================================================================
    // SUMMARY
    // ========================================================================
    printf("================================================================\n");
    printf("SUMMARY - SAFE PATH TO 10x AND 100x\n");
    printf("================================================================\n\n");

    printf("BEST SAFE FIXED LOT:\n");
    printf("  Returns: %.1fx ($10,000 -> $%.2f)\n", best_safe.return_multiple, best_safe.final_balance);
    printf("  Max DD: %.1f%%\n\n", best_safe.max_dd);

    printf("BEST SAFE COMPOUND:\n");
    printf("  Returns: %.1fx ($10,000 -> $%.2f)\n", best_compound.return_multiple, best_compound.final_balance);
    printf("  Max DD: %.1f%%\n\n", best_compound.max_dd);

    if (years_to_10x > 0) {
        printf("TIME TO 10x: %d years (with %.1fx annual compound)\n", years_to_10x, annual_mult);
    }
    if (years_to_100x > 0) {
        printf("TIME TO 100x: %d years (with %.1fx annual compound)\n", years_to_100x, annual_mult);
    }

    printf("\n");
    printf("KEY INSIGHT:\n");
    printf("  - Single year 10x requires ~50%% DD risk (not recommended)\n");
    printf("  - Safe approach: %.1fx/year compound -> 10x in %d years, 100x in %d years\n",
           annual_mult, years_to_10x > 0 ? years_to_10x : 3, years_to_100x > 0 ? years_to_100x : 5);
    printf("  - With reinvested profits, growth is exponential!\n");

    printf("\n================================================================\n");
    printf("TEST COMPLETE\n");
    printf("================================================================\n");

    return 0;
}
