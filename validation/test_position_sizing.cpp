/**
 * Position Sizing Analysis for 10x and 100x Returns
 *
 * Explores different position sizing strategies to achieve:
 * - 10x returns ($10,000 -> $100,000)
 * - 100x returns ($10,000 -> $1,000,000)
 * While keeping risk under control (target max DD < 30%)
 *
 * Strategies tested:
 * 1. Fixed lot sizes (0.01 to 0.20)
 * 2. Compound sizing (scale lots with equity growth)
 * 3. Risk-based sizing (risk X% of equity per position)
 * 4. Drawdown-responsive (reduce lots when DD increases)
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cfloat>
#include <vector>
#include <deque>
#include <algorithm>

// ============================================================================
// Data Structures
// ============================================================================

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

// ============================================================================
// Indicators
// ============================================================================

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

// ============================================================================
// Position Sizing Modes
// ============================================================================

enum SizingMode {
    FIXED,              // Fixed lot size
    COMPOUND,           // Scale with equity (lot = base * equity / initial)
    RISK_PERCENT,       // Risk X% of equity per position
    DD_RESPONSIVE       // Reduce lots when DD increases
};

const char* SizingName(SizingMode m) {
    switch (m) {
        case FIXED:         return "Fixed";
        case COMPOUND:      return "Compound";
        case RISK_PERCENT:  return "Risk %";
        case DD_RESPONSIVE: return "DD Responsive";
    }
    return "Unknown";
}

struct Config {
    SizingMode mode = FIXED;
    double base_lot = 0.01;
    double risk_percent = 1.0;      // For RISK_PERCENT mode
    double compound_factor = 1.0;   // For COMPOUND mode
    double max_lot = 10.0;          // Maximum lot size cap

    // Strategy params (V7 NoATR - best performer)
    double spacing = 0.75;
    double contract_size = 100.0;
    int max_positions = 15;
    double stop_new_at_dd = 3.0;
    double partial_close_at_dd = 5.0;
    double close_all_at_dd = 15.0;
    double tp_multiplier = 2.0;
    int sma_period = 500;
    double mr_threshold = -0.04;
    bool enable_mean_reversion = false;  // V7 mode
    int session_avoid_start = 14;
    int session_avoid_end = 18;
};

struct Result {
    SizingMode mode;
    double base_lot;
    double final_balance;
    double return_pct;
    double return_multiple;  // e.g., 10x, 100x
    double max_dd;
    double max_dd_dollars;
    int total_trades;
    double peak_balance;
    double lowest_balance;
    int margin_calls;        // Times we couldn't open due to margin
};

// ============================================================================
// Load Ticks (simplified - assumes file is filtered)
// ============================================================================

std::vector<Tick> LoadTicks(const char* filename, size_t max_count) {
    std::vector<Tick> ticks;
    ticks.reserve(max_count);

    FILE* f = fopen(filename, "r");
    if (!f) return ticks;

    char line[256];
    fgets(line, sizeof(line), f);  // Skip header

    while (fgets(line, sizeof(line), f) && ticks.size() < max_count) {
        Tick tick;

        // Parse timestamp for hour
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

        if (ticks.size() % 5000000 == 0) {
            printf("  Loaded %zu ticks...\n", ticks.size());
        }
    }

    fclose(f);
    return ticks;
}

// ============================================================================
// Calculate Position Size
// ============================================================================

double CalculateLotSize(const Config& cfg, double equity, double initial_balance,
                        double current_dd_pct, double price) {
    double lot = cfg.base_lot;

    switch (cfg.mode) {
        case FIXED:
            lot = cfg.base_lot;
            break;

        case COMPOUND:
            // Scale lots with equity growth
            // If equity doubled, lots double
            lot = cfg.base_lot * (equity / initial_balance) * cfg.compound_factor;
            break;

        case RISK_PERCENT:
            // Risk X% of equity per position
            // risk_amount = equity * risk_percent / 100
            // lot = risk_amount / (spacing * contract_size)
            {
                double risk_amount = equity * cfg.risk_percent / 100.0;
                lot = risk_amount / (cfg.spacing * cfg.contract_size);
            }
            break;

        case DD_RESPONSIVE:
            // Start with compound, but reduce when DD increases
            {
                double base = cfg.base_lot * (equity / initial_balance);

                // Reduce by 50% at 5% DD, 75% at 10% DD
                if (current_dd_pct > 10.0) {
                    lot = base * 0.25;
                } else if (current_dd_pct > 5.0) {
                    lot = base * 0.5;
                } else if (current_dd_pct > 2.0) {
                    lot = base * 0.75;
                } else {
                    lot = base;
                }
            }
            break;
    }

    // Apply limits
    lot = std::max(0.01, lot);
    lot = std::min(cfg.max_lot, lot);

    // Round to 2 decimal places
    lot = std::round(lot * 100) / 100;

    return lot;
}

// ============================================================================
// Run Backtest
// ============================================================================

Result RunBacktest(const std::vector<Tick>& ticks, Config cfg) {
    Result r;
    memset(&r, 0, sizeof(r));
    r.mode = cfg.mode;
    r.base_lot = cfg.base_lot;

    if (ticks.empty()) return r;

    const double initial_balance = 10000.0;
    double balance = initial_balance;
    double equity = balance;
    double peak_equity = balance;
    double lowest_equity = balance;
    double max_drawdown = 0;
    double max_dd_dollars = 0;

    std::vector<Trade*> positions;
    size_t next_id = 1;

    bool partial_done = false;
    bool all_closed = false;

    SMA sma(cfg.sma_period);

    double lowest = DBL_MAX, highest = -DBL_MAX;

    for (size_t tick_idx = 0; tick_idx < ticks.size(); tick_idx++) {
        const Tick& tick = ticks[tick_idx];

        sma.Add(tick.bid);

        // Calculate equity
        equity = balance;
        for (Trade* t : positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * cfg.contract_size;
        }

        // Track peaks and lows
        if (equity > peak_equity) {
            peak_equity = equity;
            r.peak_balance = peak_equity;
        }
        if (equity < lowest_equity) {
            lowest_equity = equity;
            r.lowest_balance = lowest_equity;
        }

        // Reset peak when no positions
        if (positions.empty() && peak_equity != balance) {
            peak_equity = balance;
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
                double pl = (tick.bid - t->entry_price) * t->lot_size * cfg.contract_size;
                balance += pl;
                delete t;
            }
            positions.clear();
            all_closed = true;
            equity = balance;
            peak_equity = equity;
            lowest = DBL_MAX; highest = -DBL_MAX;
            continue;
        }

        // Protection: Partial close
        if (dd_pct > cfg.partial_close_at_dd && !partial_done && positions.size() > 1) {
            std::sort(positions.begin(), positions.end(), [&](Trade* a, Trade* b) {
                return (tick.bid - a->entry_price) < (tick.bid - b->entry_price);
            });
            int to_close = std::max(1, (int)(positions.size() * 0.5));
            for (int i = 0; i < to_close && !positions.empty(); i++) {
                Trade* t = positions[0];
                double pl = (tick.bid - t->entry_price) * t->lot_size * cfg.contract_size;
                balance += pl;
                delete t;
                positions.erase(positions.begin());
            }
            partial_done = true;
        }

        // Check TPs
        for (auto it = positions.begin(); it != positions.end();) {
            Trade* t = *it;
            if (tick.bid >= t->take_profit) {
                double pl = (tick.bid - t->entry_price) * t->lot_size * cfg.contract_size;
                balance += pl;
                r.total_trades++;
                delete t;
                it = positions.erase(it);
            } else {
                ++it;
            }
        }

        // Mean reversion filter (optional)
        bool mr_ok = true;
        if (cfg.enable_mean_reversion && sma.IsReady() && sma.Get() > 0) {
            double deviation = (tick.bid - sma.Get()) / sma.Get() * 100.0;
            mr_ok = deviation < cfg.mr_threshold;
        }

        // Session filter
        bool session_ok = !(tick.hour >= cfg.session_avoid_start && tick.hour < cfg.session_avoid_end);

        if (dd_pct >= cfg.stop_new_at_dd || !mr_ok || !session_ok) {
            continue;
        }

        // Update grid bounds
        lowest = DBL_MAX; highest = -DBL_MAX;
        for (Trade* t : positions) {
            lowest = std::min(lowest, t->entry_price);
            highest = std::max(highest, t->entry_price);
        }

        // Open new positions
        if ((int)positions.size() < cfg.max_positions) {
            bool should_open = positions.empty() ||
                              (lowest >= tick.ask + cfg.spacing) ||
                              (highest <= tick.ask - cfg.spacing);

            if (should_open) {
                double lot = CalculateLotSize(cfg, equity, initial_balance, dd_pct, tick.ask);

                // Check if we have enough margin (simplified check)
                double margin_needed = lot * cfg.contract_size * tick.ask / 500.0;  // 500:1 leverage
                double used_margin = 0;
                for (Trade* t : positions) {
                    used_margin += t->lot_size * cfg.contract_size * t->entry_price / 500.0;
                }

                if (equity - used_margin > margin_needed * 1.5) {
                    Trade* t = new Trade();
                    t->id = next_id++;
                    t->entry_price = tick.ask;
                    t->lot_size = lot;
                    t->take_profit = tick.ask + tick.spread() + cfg.spacing * cfg.tp_multiplier;
                    positions.push_back(t);
                } else {
                    r.margin_calls++;
                }
            }
        }
    }

    // Close remaining
    for (Trade* t : positions) {
        double pl = (ticks.back().bid - t->entry_price) * t->lot_size * cfg.contract_size;
        balance += pl;
        r.total_trades++;
        delete t;
    }

    r.final_balance = balance;
    r.return_pct = (balance - initial_balance) / initial_balance * 100.0;
    r.return_multiple = balance / initial_balance;
    r.max_dd = max_drawdown;
    r.max_dd_dollars = max_dd_dollars;

    return r;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("================================================================\n");
    printf("POSITION SIZING ANALYSIS FOR 10x AND 100x RETURNS\n");
    printf("================================================================\n\n");

    printf("Goal: Find position sizing to achieve:\n");
    printf("  - 10x returns ($10,000 -> $100,000)\n");
    printf("  - 100x returns ($10,000 -> $1,000,000)\n");
    printf("  - While keeping max DD under control\n\n");

    // Load data
    const char* filename = "Grid/XAUUSD_TICKS_2025.csv";
    printf("Loading full year 2025 data...\n");

    std::vector<Tick> ticks = LoadTicks(filename, 60000000);

    if (ticks.empty()) {
        printf("ERROR: Failed to load data\n");
        return 1;
    }

    printf("Loaded %zu ticks\n\n", ticks.size());

    std::vector<Result> results;

    // ========================================================================
    // TEST 1: Fixed lot sizes
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 1: FIXED LOT SIZES\n");
    printf("================================================================\n\n");

    double lot_sizes[] = {0.01, 0.02, 0.03, 0.05, 0.07, 0.10, 0.15, 0.20, 0.30, 0.50};

    printf("%-8s %12s %10s %8s %12s %10s\n",
           "Lot", "Final Bal", "Return", "Multiple", "Max DD $", "Max DD %");
    printf("------------------------------------------------------------------------\n");

    for (double lot : lot_sizes) {
        Config cfg;
        cfg.mode = FIXED;
        cfg.base_lot = lot;
        cfg.enable_mean_reversion = false;  // V7 mode

        Result r = RunBacktest(ticks, cfg);
        results.push_back(r);

        printf("%-8.2f $%11.2f %+9.1f%% %7.1fx $%10.2f %9.1f%%\n",
               lot, r.final_balance, r.return_pct, r.return_multiple,
               r.max_dd_dollars, r.max_dd);
    }

    printf("------------------------------------------------------------------------\n\n");

    // ========================================================================
    // TEST 2: Compound position sizing
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 2: COMPOUND POSITION SIZING\n");
    printf("================================================================\n\n");

    printf("Lots scale with equity: lot = base_lot * (equity / initial)\n\n");

    double compound_bases[] = {0.01, 0.02, 0.03, 0.05};

    printf("%-10s %12s %10s %8s %12s %10s\n",
           "Base Lot", "Final Bal", "Return", "Multiple", "Max DD $", "Max DD %");
    printf("------------------------------------------------------------------------\n");

    for (double base : compound_bases) {
        Config cfg;
        cfg.mode = COMPOUND;
        cfg.base_lot = base;
        cfg.compound_factor = 1.0;
        cfg.enable_mean_reversion = false;

        Result r = RunBacktest(ticks, cfg);
        results.push_back(r);

        printf("%-10.2f $%11.2f %+9.1f%% %7.1fx $%10.2f %9.1f%%\n",
               base, r.final_balance, r.return_pct, r.return_multiple,
               r.max_dd_dollars, r.max_dd);
    }

    printf("------------------------------------------------------------------------\n\n");

    // ========================================================================
    // TEST 3: Risk-percent sizing
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 3: RISK-PERCENT SIZING\n");
    printf("================================================================\n\n");

    printf("Risk X%% of equity per position\n\n");

    double risk_pcts[] = {0.5, 1.0, 2.0, 3.0, 5.0};

    printf("%-10s %12s %10s %8s %12s %10s\n",
           "Risk %", "Final Bal", "Return", "Multiple", "Max DD $", "Max DD %");
    printf("------------------------------------------------------------------------\n");

    for (double risk : risk_pcts) {
        Config cfg;
        cfg.mode = RISK_PERCENT;
        cfg.risk_percent = risk;
        cfg.enable_mean_reversion = false;

        Result r = RunBacktest(ticks, cfg);
        results.push_back(r);

        printf("%-10.1f $%11.2f %+9.1f%% %7.1fx $%10.2f %9.1f%%\n",
               risk, r.final_balance, r.return_pct, r.return_multiple,
               r.max_dd_dollars, r.max_dd);
    }

    printf("------------------------------------------------------------------------\n\n");

    // ========================================================================
    // TEST 4: DD-Responsive sizing
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 4: DD-RESPONSIVE SIZING\n");
    printf("================================================================\n\n");

    printf("Compound sizing that reduces when DD increases\n\n");

    double dd_bases[] = {0.02, 0.03, 0.05, 0.07};

    printf("%-10s %12s %10s %8s %12s %10s\n",
           "Base Lot", "Final Bal", "Return", "Multiple", "Max DD $", "Max DD %");
    printf("------------------------------------------------------------------------\n");

    for (double base : dd_bases) {
        Config cfg;
        cfg.mode = DD_RESPONSIVE;
        cfg.base_lot = base;
        cfg.enable_mean_reversion = false;

        Result r = RunBacktest(ticks, cfg);
        results.push_back(r);

        printf("%-10.2f $%11.2f %+9.1f%% %7.1fx $%10.2f %9.1f%%\n",
               base, r.final_balance, r.return_pct, r.return_multiple,
               r.max_dd_dollars, r.max_dd);
    }

    printf("------------------------------------------------------------------------\n\n");

    // ========================================================================
    // ANALYSIS: Find optimal for 10x and 100x
    // ========================================================================
    printf("================================================================\n");
    printf("ANALYSIS: FINDING OPTIMAL CONFIGURATIONS\n");
    printf("================================================================\n\n");

    // Find configurations that achieve targets with acceptable DD
    printf("Configurations achieving 10x+ with DD < 30%%:\n\n");
    printf("%-15s %-10s %10s %8s %10s\n",
           "Mode", "Param", "Multiple", "Max DD", "Risk/Reward");
    printf("------------------------------------------------------------------------\n");

    for (const Result& r : results) {
        if (r.return_multiple >= 10.0 && r.max_dd < 30.0) {
            double risk_reward = r.return_multiple / (r.max_dd / 10.0);  // Normalize DD to 10%
            printf("%-15s %-10.2f %9.1fx %9.1f%% %10.2f\n",
                   SizingName(r.mode), r.base_lot, r.return_multiple, r.max_dd, risk_reward);
        }
    }

    printf("\n");
    printf("Configurations achieving 100x+ (any DD):\n\n");
    printf("%-15s %-10s %12s %10s %12s\n",
           "Mode", "Param", "Final Bal", "Multiple", "Max DD");
    printf("------------------------------------------------------------------------\n");

    for (const Result& r : results) {
        if (r.return_multiple >= 100.0) {
            printf("%-15s %-10.2f $%11.2f %9.1fx %10.1f%%\n",
                   SizingName(r.mode), r.base_lot, r.final_balance, r.return_multiple, r.max_dd);
        }
    }

    // ========================================================================
    // RECOMMENDATIONS
    // ========================================================================
    printf("\n================================================================\n");
    printf("RECOMMENDATIONS\n");
    printf("================================================================\n\n");

    // Find best 10x config
    Result* best_10x = nullptr;
    double best_10x_ratio = 0;
    for (Result& r : results) {
        if (r.return_multiple >= 10.0 && r.max_dd < 30.0) {
            double ratio = r.return_multiple / r.max_dd;
            if (ratio > best_10x_ratio) {
                best_10x_ratio = ratio;
                best_10x = &r;
            }
        }
    }

    if (best_10x) {
        printf("BEST FOR 10x TARGET (Max DD < 30%%):\n");
        printf("  Mode: %s\n", SizingName(best_10x->mode));
        printf("  Base Lot: %.2f\n", best_10x->base_lot);
        printf("  Final Balance: $%.2f\n", best_10x->final_balance);
        printf("  Return Multiple: %.1fx\n", best_10x->return_multiple);
        printf("  Max Drawdown: %.1f%% ($%.2f)\n", best_10x->max_dd, best_10x->max_dd_dollars);
        printf("\n");
    }

    // Find any 100x config
    Result* best_100x = nullptr;
    double best_100x_dd = 1000;
    for (Result& r : results) {
        if (r.return_multiple >= 100.0 && r.max_dd < best_100x_dd) {
            best_100x_dd = r.max_dd;
            best_100x = &r;
        }
    }

    if (best_100x) {
        printf("BEST FOR 100x TARGET:\n");
        printf("  Mode: %s\n", SizingName(best_100x->mode));
        printf("  Base Lot: %.2f\n", best_100x->base_lot);
        printf("  Final Balance: $%.2f\n", best_100x->final_balance);
        printf("  Return Multiple: %.1fx\n", best_100x->return_multiple);
        printf("  Max Drawdown: %.1f%% ($%.2f)\n", best_100x->max_dd, best_100x->max_dd_dollars);
        printf("  WARNING: High drawdown - risk of margin call!\n");
        printf("\n");
    } else {
        printf("100x NOT ACHIEVED in single year with tested configurations.\n");
        printf("Options to reach 100x:\n");
        printf("  1. Run for multiple years with compounding\n");
        printf("  2. Use higher leverage/lot sizes (higher risk)\n");
        printf("  3. Start with more capital\n");
        printf("\n");
    }

    // Multi-year projection
    printf("================================================================\n");
    printf("MULTI-YEAR PROJECTION (Compound Growth)\n");
    printf("================================================================\n\n");

    if (best_10x) {
        double annual_return = best_10x->return_pct / 100.0 + 1.0;  // e.g., 2.15 for 115%

        printf("Using best 10x config (%.1fx annual):\n\n", best_10x->return_multiple);
        printf("%-6s %15s %12s\n", "Year", "Balance", "Multiple");
        printf("------------------------------------------\n");

        double balance = 10000.0;
        for (int year = 0; year <= 6; year++) {
            double multiple = balance / 10000.0;
            printf("%-6d $%14.2f %11.1fx", year, balance, multiple);
            if (multiple >= 10 && multiple < 100) printf(" <- 10x");
            if (multiple >= 100) printf(" <- 100x");
            printf("\n");
            balance *= annual_return;
        }
    }

    printf("\n================================================================\n");
    printf("TEST COMPLETE\n");
    printf("================================================================\n");

    return 0;
}
