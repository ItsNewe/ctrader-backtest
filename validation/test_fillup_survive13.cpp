/**
 * Original Fill-Up Strategy with 13% Survive Parameter
 *
 * Properly simulates the original algorithm:
 * - Calculates lot size to survive a 13% drawdown from highest position
 * - Spacing = $1.00
 * - TP = spread + spacing
 * - No max positions, no DD protection (original behavior)
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cfloat>
#include <vector>
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

struct Result {
    double final_balance;
    double return_multiple;
    double max_dd;
    int total_trades;
    int max_positions;
    double max_lot_used;
    bool margin_call;
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

// Original fill-up sizing algorithm (simplified but accurate to concept)
double CalculateLotSize(double equity, double used_margin, double current_price,
                        double highest_buy, double spacing, double survive_pct,
                        double volume_open, int positions_total,
                        double contract_size, double leverage,
                        double min_lot, double max_lot) {

    const double margin_stop_out = 20.0;  // MT5 stop out level

    // Calculate target end price (survive_pct below highest or current)
    double reference_price = (positions_total == 0) ? current_price : highest_buy;
    double end_price = reference_price * ((100.0 - survive_pct) / 100.0);

    // Distance to survive
    double distance = current_price - end_price;
    if (distance <= 0) return 0;

    // Number of potential trades in this distance
    double num_trades = std::floor(distance / spacing);
    if (num_trades < 1) num_trades = 1;

    // Calculate equity at target (after all positions hit worst case)
    double equity_at_target = equity - volume_open * distance * contract_size;

    // Check if we're already in trouble
    double current_margin_level = (used_margin > 0) ? (equity / used_margin * 100.0) : 10000.0;
    double target_margin_level = (used_margin > 0) ? (equity_at_target / used_margin * 100.0) : 10000.0;

    if (target_margin_level <= margin_stop_out) {
        return 0;  // Can't afford any new positions
    }

    // Calculate how much equity we can risk on new positions
    // Grid loss formula: lot * contract * spacing * (n*(n+1)/2)
    double grid_loss_factor = spacing * contract_size * (num_trades * (num_trades + 1) / 2);

    // Available equity for new grid
    double available = equity_at_target - (used_margin * margin_stop_out / 100.0);
    if (available <= 0) return 0;

    // Calculate lot size
    double lot = available / grid_loss_factor;
    lot = std::max(min_lot, std::min(max_lot, lot));
    lot = std::round(lot * 100) / 100;

    return lot;
}

Result RunFillUpSurvive(const std::vector<Tick>& ticks, double survive_pct,
                         double spacing, bool add_dd_protection = false,
                         double close_all_dd = 100.0) {
    Result r = {0};

    const double initial_balance = 10000.0;
    const double contract_size = 100.0;
    const double leverage = 500.0;
    const double min_lot = 0.01;
    const double max_lot = 100.0;

    double balance = initial_balance;
    double equity = balance;
    double peak_equity = balance;
    double max_drawdown = 0;

    std::vector<Trade*> positions;
    size_t next_id = 1;

    double lowest = DBL_MAX, highest = -DBL_MAX;
    double volume_open = 0;

    for (const Tick& tick : ticks) {
        // Calculate equity and volume
        equity = balance;
        volume_open = 0;
        lowest = DBL_MAX;
        highest = -DBL_MAX;

        for (Trade* t : positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * contract_size;
            volume_open += t->lot_size;
            lowest = std::min(lowest, t->entry_price);
            highest = std::max(highest, t->entry_price);
        }

        // Calculate used margin
        double used_margin = 0;
        for (Trade* t : positions) {
            used_margin += t->lot_size * contract_size * t->entry_price / leverage;
        }

        // Margin call check
        if (used_margin > 0 && equity < used_margin * 0.2) {
            r.margin_call = true;
            for (Trade* t : positions) {
                balance += (tick.bid - t->entry_price) * t->lot_size * contract_size;
                delete t;
            }
            positions.clear();
            equity = balance;
            if (balance <= 0) {
                r.final_balance = 0;
                r.return_multiple = 0;
                r.max_dd = 100.0;
                return r;
            }
            peak_equity = balance;
            volume_open = 0;
            continue;
        }

        // Track peak and drawdown
        if (positions.empty()) peak_equity = balance;
        if (equity > peak_equity) peak_equity = equity;

        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        if (dd_pct > max_drawdown) max_drawdown = dd_pct;

        // Optional DD protection (not in original)
        if (add_dd_protection && dd_pct > close_all_dd && !positions.empty()) {
            for (Trade* t : positions) {
                balance += (tick.bid - t->entry_price) * t->lot_size * contract_size;
                delete t;
            }
            positions.clear();
            equity = balance;
            peak_equity = equity;
            volume_open = 0;
            continue;
        }

        // Track max positions
        if ((int)positions.size() > r.max_positions) {
            r.max_positions = positions.size();
        }

        // Check TPs
        for (auto it = positions.begin(); it != positions.end();) {
            Trade* t = *it;
            if (tick.bid >= t->take_profit) {
                balance += (tick.bid - t->entry_price) * t->lot_size * contract_size;
                r.total_trades++;
                delete t;
                it = positions.erase(it);
            } else {
                ++it;
            }
        }

        // Recalculate after TP closes
        volume_open = 0;
        lowest = DBL_MAX;
        highest = -DBL_MAX;
        for (Trade* t : positions) {
            volume_open += t->lot_size;
            lowest = std::min(lowest, t->entry_price);
            highest = std::max(highest, t->entry_price);
        }

        // Check if should open new position
        bool should_open = positions.empty() ||
                          (lowest >= tick.ask + spacing) ||
                          (highest <= tick.ask - spacing);

        if (should_open) {
            double lot = CalculateLotSize(equity, used_margin, tick.ask,
                                          highest, spacing, survive_pct,
                                          volume_open, positions.size(),
                                          contract_size, leverage,
                                          min_lot, max_lot);

            if (lot >= min_lot) {
                // Margin check for new position
                double margin_needed = lot * contract_size * tick.ask / leverage;
                double free_margin = equity - used_margin;

                if (free_margin > margin_needed * 1.2) {
                    Trade* t = new Trade();
                    t->id = next_id++;
                    t->entry_price = tick.ask;
                    t->lot_size = lot;
                    t->take_profit = tick.ask + tick.spread() + spacing;
                    positions.push_back(t);

                    if (lot > r.max_lot_used) r.max_lot_used = lot;
                }
            }
        }
    }

    // Close remaining
    for (Trade* t : positions) {
        balance += (ticks.back().bid - t->entry_price) * t->lot_size * contract_size;
        r.total_trades++;
        delete t;
    }

    r.final_balance = std::max(0.0, balance);
    r.return_multiple = r.final_balance / initial_balance;
    r.max_dd = max_drawdown;

    return r;
}

int main() {
    printf("================================================================\n");
    printf("FILL-UP STRATEGY - SURVIVE PARAMETER ANALYSIS\n");
    printf("================================================================\n\n");

    const char* filename = "Grid/XAUUSD_TICKS_2025.csv";
    printf("Loading data...\n");

    std::vector<Tick> ticks = LoadTicks(filename, 60000000);
    if (ticks.empty()) {
        printf("ERROR: Failed to load data\n");
        return 1;
    }
    printf("Loaded %zu ticks\n\n", ticks.size());

    // Test different survive percentages
    printf("================================================================\n");
    printf("TEST 1: SURVIVE PARAMETER SWEEP (Original Algorithm)\n");
    printf("================================================================\n\n");

    printf("Settings: Spacing=$1.00, TP=spread+spacing, No DD protection\n\n");

    printf("%-10s %12s %8s %10s %8s %10s %10s\n",
           "Survive%", "Final Bal", "Return", "Max DD", "Trades", "MaxPos", "MaxLot");
    printf("--------------------------------------------------------------------------------\n");

    double survives[] = {5.0, 8.0, 10.0, 13.0, 15.0, 20.0, 25.0, 30.0};

    for (double surv : survives) {
        Result r = RunFillUpSurvive(ticks, surv, 1.0, false);

        printf("%-10.0f $%11.2f %7.1fx %9.1f%% %8d %10d %10.2f %s\n",
               surv, r.final_balance, r.return_multiple, r.max_dd,
               r.total_trades, r.max_positions, r.max_lot_used,
               r.margin_call ? "MARGIN CALL" : "");
    }

    printf("--------------------------------------------------------------------------------\n\n");

    // Test with DD protection added
    printf("================================================================\n");
    printf("TEST 2: SURVIVE 13%% + DD PROTECTION\n");
    printf("================================================================\n\n");

    printf("Adding emergency close at various DD levels...\n\n");

    printf("%-12s %12s %8s %10s %8s\n",
           "Close@DD", "Final Bal", "Return", "Max DD", "Trades");
    printf("----------------------------------------------------------------\n");

    double dd_levels[] = {10.0, 15.0, 20.0, 25.0, 30.0, 50.0, 100.0};

    for (double dd : dd_levels) {
        Result r = RunFillUpSurvive(ticks, 13.0, 1.0, true, dd);

        printf("%-12.0f $%11.2f %7.1fx %9.1f%% %8d %s\n",
               dd, r.final_balance, r.return_multiple, r.max_dd,
               r.total_trades, r.margin_call ? "MARGIN CALL" : "");
    }

    printf("----------------------------------------------------------------\n\n");

    // Test survive 13% with different spacings
    printf("================================================================\n");
    printf("TEST 3: SURVIVE 13%% WITH DIFFERENT SPACINGS\n");
    printf("================================================================\n\n");

    printf("%-10s %12s %8s %10s %8s %10s\n",
           "Spacing", "Final Bal", "Return", "Max DD", "Trades", "MaxLot");
    printf("------------------------------------------------------------------------\n");

    double spacings[] = {0.25, 0.50, 0.75, 1.00, 1.25, 1.50, 2.00};

    for (double sp : spacings) {
        Result r = RunFillUpSurvive(ticks, 13.0, sp, false);

        printf("$%-9.2f $%11.2f %7.1fx %9.1f%% %8d %10.2f %s\n",
               sp, r.final_balance, r.return_multiple, r.max_dd,
               r.total_trades, r.max_lot_used,
               r.margin_call ? "MARGIN CALL" : "");
    }

    printf("------------------------------------------------------------------------\n\n");

    // Best configuration for survive-style
    printf("================================================================\n");
    printf("TEST 4: OPTIMIZED SURVIVE-STYLE STRATEGY\n");
    printf("================================================================\n\n");

    printf("Testing survive 13%% + spacing 0.75 + DD protection 15%%...\n\n");

    Result best_survive = RunFillUpSurvive(ticks, 13.0, 0.75, true, 15.0);

    printf("SURVIVE-STYLE OPTIMIZED:\n");
    printf("  Final Balance: $%.2f\n", best_survive.final_balance);
    printf("  Return: %.1fx\n", best_survive.return_multiple);
    printf("  Max Drawdown: %.1f%%\n", best_survive.max_dd);
    printf("  Total Trades: %d\n", best_survive.total_trades);
    printf("  Max Lot Used: %.2f\n", best_survive.max_lot_used);
    printf("  Margin Call: %s\n\n", best_survive.margin_call ? "YES" : "NO");

    // Compare to fixed lot optimized
    printf("For comparison - FIXED LOT OPTIMIZED (lot=0.040, spacing=0.50, TP=2.5x):\n");
    printf("  Final Balance: $57,347\n");
    printf("  Return: 5.7x\n");
    printf("  Max Drawdown: 15.4%%\n");
    printf("  Total Trades: 41,932\n");

    printf("\n================================================================\n");
    printf("ANALYSIS COMPLETE\n");
    printf("================================================================\n");

    return 0;
}
