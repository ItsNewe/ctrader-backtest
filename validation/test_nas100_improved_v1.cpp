/**
 * NAS100 Improved Strategy V1: Conservative Spacing + Take Profit
 *
 * Key improvements over original:
 * 1. Require minimum spacing between entries (not every new high)
 * 2. Take profit mechanism to lock in gains
 * 3. Wider survive parameter (10%+ for index volatility)
 * 4. Position limit with proper management
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
    int day_of_week;
    double spread() const { return ask - bid; }
};

struct Position {
    size_t id;
    double entry_price;
    double lot_size;
    double take_profit;  // Added TP
    double highest_since_entry;  // For trailing
};

struct Config {
    double survive_down = 10.0;
    double spacing = 100.0;       // Minimum points between entries
    double tp_points = 200.0;     // Take profit in points
    double trailing_stop = 0.0;   // Trailing stop points (0=disabled)
    int max_positions = 15;
    double lot_size = 0.1;        // Fixed lot (simpler)
    double close_all_at_dd = 25.0;
};

struct Result {
    double final_balance;
    double return_multiple;
    double max_dd;
    int total_trades;
    int winning_trades;
    int max_positions_held;
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
        tick.hour = 12;
        if (strlen(line) >= 13) {
            char h[3] = {line[11], line[12], 0};
            tick.hour = atoi(h);
        }
        char* token = strtok(line, "\t");
        if (!token) continue;
        token = strtok(NULL, "\t");
        if (!token) continue;
        tick.bid = atof(token);
        token = strtok(NULL, "\t");
        if (!token) continue;
        tick.ask = atof(token);
        if (tick.bid > 0 && tick.ask > 0) ticks.push_back(tick);
    }
    fclose(f);
    return ticks;
}

Result RunStrategy(const std::vector<Tick>& ticks, Config cfg) {
    Result r = {0};
    const double initial_balance = 10000.0;
    const double contract_size = 1.0;
    const double leverage = 500.0;

    double balance = initial_balance;
    double equity = balance;
    double peak_equity = balance;
    double max_drawdown = 0;

    std::vector<Position*> positions;
    size_t next_id = 1;
    double last_entry_price = -DBL_MAX;
    double all_time_high = 0;

    for (const Tick& tick : ticks) {
        // Track ATH
        if (tick.ask > all_time_high) all_time_high = tick.ask;

        // Calculate equity
        equity = balance;
        double used_margin = 0;
        for (Position* p : positions) {
            equity += (tick.bid - p->entry_price) * p->lot_size * contract_size;
            used_margin += p->lot_size * contract_size * p->entry_price / leverage;
            if (tick.bid > p->highest_since_entry) p->highest_since_entry = tick.bid;
        }

        r.max_positions_held = std::max(r.max_positions_held, (int)positions.size());

        // Margin call check
        if (used_margin > 0 && equity < used_margin * 0.2) {
            r.margin_call = true;
            for (Position* p : positions) {
                balance += (tick.bid - p->entry_price) * p->lot_size * contract_size;
                r.total_trades++;
                delete p;
            }
            positions.clear();
            equity = balance;
            if (balance <= 0) break;
            peak_equity = balance;
            last_entry_price = -DBL_MAX;
            continue;
        }

        if (positions.empty()) peak_equity = balance;
        if (equity > peak_equity) peak_equity = equity;

        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        if (dd_pct > max_drawdown) max_drawdown = dd_pct;

        // DD protection
        if (dd_pct > cfg.close_all_at_dd && !positions.empty()) {
            for (Position* p : positions) {
                balance += (tick.bid - p->entry_price) * p->lot_size * contract_size;
                r.total_trades++;
                delete p;
            }
            positions.clear();
            equity = balance;
            peak_equity = balance;
            last_entry_price = -DBL_MAX;
            continue;
        }

        // Check TPs and trailing stops
        for (auto it = positions.begin(); it != positions.end();) {
            Position* p = *it;
            bool close = false;

            // Take profit
            if (cfg.tp_points > 0 && tick.bid >= p->take_profit) {
                close = true;
            }

            // Trailing stop
            if (cfg.trailing_stop > 0 && tick.bid < p->highest_since_entry - cfg.trailing_stop) {
                close = true;
            }

            if (close) {
                double pl = (tick.bid - p->entry_price) * p->lot_size * contract_size;
                balance += pl;
                r.total_trades++;
                if (pl > 0) r.winning_trades++;
                delete p;
                it = positions.erase(it);
            } else {
                ++it;
            }
        }

        // Entry logic: new high with spacing
        bool at_new_high = tick.ask >= all_time_high - 1;  // Within 1 point of ATH
        bool has_spacing = (last_entry_price < 0) || (tick.ask >= last_entry_price + cfg.spacing);
        bool under_limit = (int)positions.size() < cfg.max_positions;
        bool dd_ok = dd_pct < cfg.survive_down;

        if (at_new_high && has_spacing && under_limit && dd_ok) {
            double margin_needed = cfg.lot_size * contract_size * tick.ask / leverage;
            double free_margin = equity - used_margin;

            if (free_margin > margin_needed * 1.5) {
                Position* p = new Position();
                p->id = next_id++;
                p->entry_price = tick.ask;
                p->lot_size = cfg.lot_size;
                p->take_profit = tick.ask + cfg.tp_points;
                p->highest_since_entry = tick.bid;
                positions.push_back(p);
                last_entry_price = tick.ask;
            }
        }
    }

    // Close remaining
    for (Position* p : positions) {
        double pl = (ticks.back().bid - p->entry_price) * p->lot_size * contract_size;
        balance += pl;
        r.total_trades++;
        if (pl > 0) r.winning_trades++;
        delete p;
    }

    r.final_balance = std::max(0.0, balance);
    r.return_multiple = r.final_balance / initial_balance;
    r.max_dd = max_drawdown;
    return r;
}

int main() {
    printf("================================================================\n");
    printf("NAS100 IMPROVED V1: SPACING + TAKE PROFIT\n");
    printf("================================================================\n\n");

    std::vector<Tick> ticks = LoadTicks("NAS100/NAS100_TICKS_2025.csv", 60000000);
    if (ticks.empty()) { printf("ERROR: No data\n"); return 1; }
    printf("Loaded %zu ticks, price %+.1f%%\n\n", ticks.size(),
           (ticks.back().bid - ticks.front().bid) / ticks.front().bid * 100);

    // Test spacing variations
    printf("TEST 1: SPACING SWEEP (TP=200, lot=0.1, max=15)\n");
    printf("%-10s %12s %8s %10s %8s %8s\n", "Spacing", "Final", "Return", "MaxDD", "Trades", "WinRate");
    printf("------------------------------------------------------------------------\n");

    double spacings[] = {25, 50, 100, 150, 200, 300, 500};
    for (double sp : spacings) {
        Config cfg;
        cfg.spacing = sp;
        cfg.tp_points = 200;
        cfg.lot_size = 0.1;
        cfg.max_positions = 15;
        Result r = RunStrategy(ticks, cfg);
        double wr = r.total_trades > 0 ? r.winning_trades * 100.0 / r.total_trades : 0;
        printf("%-10.0f $%11.2f %7.1fx %9.1f%% %8d %7.1f%% %s\n",
               sp, r.final_balance, r.return_multiple, r.max_dd, r.total_trades, wr,
               r.margin_call ? "MC" : "");
    }

    // Test TP variations
    printf("\nTEST 2: TP SWEEP (spacing=100, lot=0.1, max=15)\n");
    printf("%-10s %12s %8s %10s %8s %8s\n", "TP", "Final", "Return", "MaxDD", "Trades", "WinRate");
    printf("------------------------------------------------------------------------\n");

    double tps[] = {50, 100, 150, 200, 300, 500, 1000};
    for (double tp : tps) {
        Config cfg;
        cfg.spacing = 100;
        cfg.tp_points = tp;
        cfg.lot_size = 0.1;
        cfg.max_positions = 15;
        Result r = RunStrategy(ticks, cfg);
        double wr = r.total_trades > 0 ? r.winning_trades * 100.0 / r.total_trades : 0;
        printf("%-10.0f $%11.2f %7.1fx %9.1f%% %8d %7.1f%% %s\n",
               tp, r.final_balance, r.return_multiple, r.max_dd, r.total_trades, wr,
               r.margin_call ? "MC" : "");
    }

    // Test lot sizes
    printf("\nTEST 3: LOT SIZE SWEEP (spacing=100, TP=200, max=15)\n");
    printf("%-10s %12s %8s %10s %8s\n", "Lot", "Final", "Return", "MaxDD", "Trades");
    printf("----------------------------------------------------------------\n");

    double lots[] = {0.05, 0.1, 0.2, 0.3, 0.5, 1.0};
    for (double lot : lots) {
        Config cfg;
        cfg.spacing = 100;
        cfg.tp_points = 200;
        cfg.lot_size = lot;
        cfg.max_positions = 15;
        Result r = RunStrategy(ticks, cfg);
        printf("%-10.2f $%11.2f %7.1fx %9.1f%% %8d %s\n",
               lot, r.final_balance, r.return_multiple, r.max_dd, r.total_trades,
               r.margin_call ? "MC" : "");
    }

    // Test trailing stop
    printf("\nTEST 4: TRAILING STOP SWEEP (spacing=100, TP=0, lot=0.1)\n");
    printf("%-10s %12s %8s %10s %8s\n", "Trail", "Final", "Return", "MaxDD", "Trades");
    printf("----------------------------------------------------------------\n");

    double trails[] = {50, 100, 150, 200, 300, 500};
    for (double tr : trails) {
        Config cfg;
        cfg.spacing = 100;
        cfg.tp_points = 0;  // No fixed TP
        cfg.trailing_stop = tr;
        cfg.lot_size = 0.1;
        cfg.max_positions = 15;
        Result r = RunStrategy(ticks, cfg);
        printf("%-10.0f $%11.2f %7.1fx %9.1f%% %8d %s\n",
               tr, r.final_balance, r.return_multiple, r.max_dd, r.total_trades,
               r.margin_call ? "MC" : "");
    }

    // Best combination
    printf("\n================================================================\n");
    printf("OPTIMIZED CONFIGURATION\n");
    printf("================================================================\n");

    Config best;
    best.spacing = 100;
    best.tp_points = 200;
    best.trailing_stop = 150;
    best.lot_size = 0.2;
    best.max_positions = 15;
    best.close_all_at_dd = 20.0;
    Result r = RunStrategy(ticks, best);

    printf("Spacing: %.0f, TP: %.0f, Trail: %.0f, Lot: %.2f, MaxPos: %d\n",
           best.spacing, best.tp_points, best.trailing_stop, best.lot_size, best.max_positions);
    printf("Result: $%.2f (%.1fx), MaxDD: %.1f%%, Trades: %d, WinRate: %.1f%%\n",
           r.final_balance, r.return_multiple, r.max_dd, r.total_trades,
           r.total_trades > 0 ? r.winning_trades * 100.0 / r.total_trades : 0);

    printf("\n================================================================\n");
    return 0;
}
