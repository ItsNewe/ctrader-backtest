/**
 * NAS100 Hyperbolic Strategy - Negative Power Testing
 *
 * Original formula: y = 502741 * x^-0.811
 *
 * With NEGATIVE power:
 * - room = starting_room * distance^(-power)
 * - As distance increases, room DECREASES
 * - This means MORE aggressive pyramiding at higher prices
 *
 * With POSITIVE power:
 * - room = starting_room * distance^(+power)
 * - As distance increases, room INCREASES
 * - This means LESS aggressive pyramiding at higher prices
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
    int year, month, day;
    double spread() const { return ask - bid; }
};

struct Position {
    size_t id;
    double entry_price;
    double lot_size;
};

struct Config {
    double manual_stop_out = 74.0;
    double multiplier = 0.1;
    double power = 0.1;           // Can be negative!
    double min_lot = 0.01;
    double max_lot = 100.0;
    bool direct_lot_formula = false;  // Apply formula directly to lot size
    double lot_coefficient = 502741;  // k in y = k * x^power
};

struct Result {
    double final_balance;
    double return_multiple;
    double max_dd;
    int total_trades;
    int max_positions;
    int stop_out_count;
    bool margin_call;
    double max_lot_used;
    double min_lot_used;
    double avg_lot;
    double total_lots;
};

std::vector<Tick> LoadTicks(const char* filename, size_t max_count,
                            int start_y, int start_m, int start_d,
                            int end_y, int end_m, int end_d) {
    std::vector<Tick> ticks;
    ticks.reserve(max_count);
    FILE* f = fopen(filename, "r");
    if (!f) return ticks;
    char line[256];
    fgets(line, sizeof(line), f);
    while (fgets(line, sizeof(line), f) && ticks.size() < max_count) {
        Tick tick;
        if (strlen(line) >= 10) {
            char y[5] = {line[0], line[1], line[2], line[3], 0};
            char m[3] = {line[5], line[6], 0};
            char d[3] = {line[8], line[9], 0};
            tick.year = atoi(y);
            tick.month = atoi(m);
            tick.day = atoi(d);
            int date = tick.year * 10000 + tick.month * 100 + tick.day;
            int start = start_y * 10000 + start_m * 100 + start_d;
            int end = end_y * 10000 + end_m * 100 + end_d;
            if (date < start || date > end) continue;
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

Result RunHyperbolic(const std::vector<Tick>& ticks, Config cfg) {
    Result r = {0};
    r.min_lot_used = DBL_MAX;
    if (ticks.empty()) return r;

    const double initial_balance = 10000.0;
    const double contract_size = 1.0;
    const double leverage = 500.0;

    double balance = initial_balance;
    double equity = balance;
    double peak_equity = balance;
    double max_drawdown = 0;

    std::vector<Position*> positions;
    size_t next_id = 1;

    double checked_last_open_price = -DBL_MAX;
    double starting_x = 0;
    double local_starting_room = 0;
    double volume_of_open_trades = 0;

    for (const Tick& tick : ticks) {
        equity = balance;
        double used_margin = 0;
        volume_of_open_trades = 0;

        for (Position* p : positions) {
            equity += (tick.bid - p->entry_price) * p->lot_size * contract_size;
            used_margin += p->lot_size * contract_size * p->entry_price / leverage;
            volume_of_open_trades += p->lot_size;
        }

        double margin_level = (used_margin > 0) ? (equity / used_margin * 100.0) : 10000.0;

        if ((int)positions.size() > r.max_positions) r.max_positions = positions.size();

        // Hard margin call
        if (used_margin > 0 && equity < used_margin * 0.2) {
            r.margin_call = true;
            for (Position* p : positions) {
                balance += (tick.bid - p->entry_price) * p->lot_size * contract_size;
                r.total_trades++;
                delete p;
            }
            positions.clear();
            if (balance <= 0) break;
            peak_equity = balance;
            checked_last_open_price = -DBL_MAX;
            volume_of_open_trades = 0;
            continue;
        }

        // Manual stop out
        if (margin_level < cfg.manual_stop_out && margin_level > 0 && !positions.empty()) {
            r.stop_out_count++;
            for (Position* p : positions) {
                balance += (tick.bid - p->entry_price) * p->lot_size * contract_size;
                r.total_trades++;
                delete p;
            }
            positions.clear();
            equity = balance;
            peak_equity = balance;
            checked_last_open_price = -DBL_MAX;
            volume_of_open_trades = 0;
            continue;
        }

        if (positions.empty()) peak_equity = balance;
        if (equity > peak_equity) peak_equity = equity;

        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        if (dd_pct > max_drawdown) max_drawdown = dd_pct;

        double spread_cost = tick.spread() * contract_size;

        // Open first position
        if (volume_of_open_trades == 0) {
            local_starting_room = tick.ask * cfg.multiplier / 100.0;

            double lot;
            if (cfg.direct_lot_formula) {
                // Direct formula: lot = k * 1^power (at start, distance=1)
                lot = cfg.lot_coefficient * std::pow(1.0, cfg.power);
                lot = std::min(lot, cfg.max_lot);
                lot = std::max(lot, cfg.min_lot);
            } else {
                double temp = (100 * balance * leverage) /
                             (100 * local_starting_room * leverage + 100 * spread_cost * leverage +
                              cfg.manual_stop_out * tick.ask);
                temp = temp / contract_size;
                lot = std::min(temp, cfg.max_lot);
            }

            lot = std::max(cfg.min_lot, lot);
            lot = std::round(lot * 100) / 100;

            double margin_needed = lot * contract_size * tick.ask / leverage;
            if (equity > margin_needed * 1.2 && lot >= cfg.min_lot) {
                Position* p = new Position();
                p->id = next_id++;
                p->entry_price = tick.ask;
                p->lot_size = lot;
                positions.push_back(p);
                checked_last_open_price = tick.ask;
                starting_x = tick.ask;

                if (lot > r.max_lot_used) r.max_lot_used = lot;
                if (lot < r.min_lot_used) r.min_lot_used = lot;
                r.total_lots += lot;
            }
        }
        else if (tick.ask > checked_last_open_price) {
            double distance = tick.ask - starting_x;
            if (distance < 1) distance = 1;  // Avoid issues with very small distances

            double lot;
            if (cfg.direct_lot_formula) {
                // Direct hyperbolic formula: lot = k * distance^power
                lot = cfg.lot_coefficient * std::pow(distance, cfg.power);
                lot = std::min(lot, cfg.max_lot);
                lot = std::max(lot, cfg.min_lot);
            } else {
                // Original room-based calculation with power
                double room_temp = local_starting_room * std::pow(distance, cfg.power);
                // Clamp room to reasonable values
                room_temp = std::max(0.01, std::min(room_temp, tick.ask * 0.1));

                double temp = (100 * equity * leverage -
                              leverage * cfg.manual_stop_out * used_margin -
                              100 * room_temp * leverage * volume_of_open_trades) /
                             (100 * room_temp * leverage + 100 * spread_cost * leverage +
                              cfg.manual_stop_out * tick.ask);
                temp = temp / contract_size;
                lot = std::min(temp, cfg.max_lot);
            }

            lot = std::max(cfg.min_lot, lot);
            lot = std::round(lot * 100) / 100;

            double margin_needed = lot * contract_size * tick.ask / leverage;
            double free_margin = equity - used_margin;

            if (free_margin > margin_needed * 1.2 && lot >= cfg.min_lot) {
                Position* p = new Position();
                p->id = next_id++;
                p->entry_price = tick.ask;
                p->lot_size = lot;
                positions.push_back(p);
                checked_last_open_price = tick.ask;

                if (lot > r.max_lot_used) r.max_lot_used = lot;
                if (lot < r.min_lot_used) r.min_lot_used = lot;
                r.total_lots += lot;
            }
        }
    }

    for (Position* p : positions) {
        balance += (ticks.back().bid - p->entry_price) * p->lot_size * contract_size;
        r.total_trades++;
        delete p;
    }

    r.final_balance = std::max(0.0, balance);
    r.return_multiple = r.final_balance / 10000.0;
    r.max_dd = max_drawdown;
    if (r.total_trades > 0) r.avg_lot = r.total_lots / r.total_trades;
    return r;
}

int main() {
    printf("================================================================\n");
    printf("NAS100 HYPERBOLIC - NEGATIVE POWER TESTING\n");
    printf("================================================================\n\n");

    printf("Formula: y = k * x^power\n\n");
    printf("POSITIVE power (+0.5): room INCREASES with distance\n");
    printf("  -> Positions spaced further apart at higher prices\n");
    printf("  -> Less aggressive pyramiding\n\n");
    printf("NEGATIVE power (-0.5): room DECREASES with distance\n");
    printf("  -> Positions spaced closer at higher prices\n");
    printf("  -> MORE aggressive pyramiding\n\n");

    const char* file = "NAS100/NAS100_TICKS_2025.csv";

    // Load Apr-Jul period (strong bull)
    printf("Loading Apr-Jul 2025 period...\n");
    auto apr_jul = LoadTicks(file, 60000000, 2025, 4, 22, 2025, 7, 31);
    printf("Loaded %zu ticks (%.2f -> %.2f = +%.1f%%)\n\n",
           apr_jul.size(), apr_jul.front().bid, apr_jul.back().bid,
           (apr_jul.back().bid - apr_jul.front().bid) / apr_jul.front().bid * 100);

    // ========================================================================
    // TEST 1: Negative vs Positive Power (Room-based)
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 1: POWER SWEEP (Room-based calculation)\n");
    printf("================================================================\n\n");

    printf("%-10s %12s %8s %10s %8s %8s %10s %10s\n",
           "Power", "Final Bal", "Return", "Max DD", "Trades", "MaxPos", "MaxLot", "MinLot");
    printf("--------------------------------------------------------------------------------------------\n");

    double powers[] = {-2.0, -1.5, -1.0, -0.811, -0.5, -0.3, -0.1, 0.0, 0.1, 0.3, 0.5, 0.811, 1.0};

    for (double pw : powers) {
        Config cfg;
        cfg.manual_stop_out = 74;
        cfg.multiplier = 0.1;
        cfg.power = pw;
        cfg.direct_lot_formula = false;

        Result r = RunHyperbolic(apr_jul, cfg);
        printf("%-10.3f $%11.2f %7.1fx %9.1f%% %8d %8d %10.2f %10.4f %s\n",
               pw, r.final_balance, r.return_multiple, r.max_dd, r.total_trades,
               r.max_positions, r.max_lot_used, r.min_lot_used,
               r.margin_call ? "MC" : "");
    }

    printf("--------------------------------------------------------------------------------------------\n\n");

    // ========================================================================
    // TEST 2: Direct Lot Formula y = k * x^power
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 2: DIRECT LOT FORMULA y = k * x^power\n");
    printf("================================================================\n\n");

    printf("Testing with k=1.0 (scaled by max_lot)\n\n");

    printf("%-10s %12s %8s %10s %8s %10s %10s\n",
           "Power", "Final Bal", "Return", "Max DD", "Trades", "MaxLot", "MinLot");
    printf("--------------------------------------------------------------------------------\n");

    double direct_powers[] = {-1.0, -0.811, -0.5, -0.3, -0.1, 0.0, 0.1, 0.3, 0.5};

    for (double pw : direct_powers) {
        Config cfg;
        cfg.manual_stop_out = 74;
        cfg.power = pw;
        cfg.direct_lot_formula = true;
        cfg.lot_coefficient = 1.0;  // Will be scaled by max_lot

        Result r = RunHyperbolic(apr_jul, cfg);
        printf("%-10.3f $%11.2f %7.1fx %9.1f%% %8d %10.2f %10.4f %s\n",
               pw, r.final_balance, r.return_multiple, r.max_dd, r.total_trades,
               r.max_lot_used, r.min_lot_used,
               r.margin_call ? "MC" : "");
    }

    printf("--------------------------------------------------------------------------------\n\n");

    // ========================================================================
    // TEST 3: Fine-tune around -0.811
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 3: FINE-TUNE AROUND -0.811 (original formula)\n");
    printf("================================================================\n\n");

    printf("%-10s %12s %8s %10s %8s %8s\n",
           "Power", "Final Bal", "Return", "Max DD", "Trades", "StopOuts");
    printf("------------------------------------------------------------------------\n");

    double fine_powers[] = {-0.9, -0.85, -0.811, -0.75, -0.7, -0.6, -0.5, -0.4};

    for (double pw : fine_powers) {
        Config cfg;
        cfg.manual_stop_out = 74;
        cfg.multiplier = 0.1;
        cfg.power = pw;
        cfg.direct_lot_formula = false;

        Result r = RunHyperbolic(apr_jul, cfg);
        printf("%-10.3f $%11.2f %7.1fx %9.1f%% %8d %8d %s\n",
               pw, r.final_balance, r.return_multiple, r.max_dd, r.total_trades,
               r.stop_out_count, r.margin_call ? "MC" : "");
    }

    printf("------------------------------------------------------------------------\n\n");

    // ========================================================================
    // TEST 4: Full Year with best negative power
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 4: FULL YEAR TEST WITH NEGATIVE POWER\n");
    printf("================================================================\n\n");

    printf("Loading full year...\n");
    auto full_year = LoadTicks(file, 60000000, 2025, 1, 1, 2026, 1, 15);
    printf("Loaded %zu ticks\n\n", full_year.size());

    printf("%-15s %-10s %12s %8s %10s %8s\n",
           "Period", "Power", "Final Bal", "Return", "Max DD", "StopOuts");
    printf("--------------------------------------------------------------------------------\n");

    double test_powers[] = {-0.811, -0.5, 0.5};

    for (double pw : test_powers) {
        Config cfg;
        cfg.manual_stop_out = 74;
        cfg.multiplier = 0.1;
        cfg.power = pw;

        Result r_apr = RunHyperbolic(apr_jul, cfg);
        Result r_full = RunHyperbolic(full_year, cfg);

        printf("%-15s %-10.3f $%11.2f %7.1fx %9.1f%% %8d %s\n",
               "Apr-Jul", pw, r_apr.final_balance, r_apr.return_multiple,
               r_apr.max_dd, r_apr.stop_out_count, r_apr.margin_call ? "MC" : "");
        printf("%-15s %-10.3f $%11.2f %7.1fx %9.1f%% %8d %s\n",
               "Full Year", pw, r_full.final_balance, r_full.return_multiple,
               r_full.max_dd, r_full.stop_out_count, r_full.margin_call ? "MC" : "");
        printf("\n");
    }

    printf("--------------------------------------------------------------------------------\n\n");

    // ========================================================================
    // SUMMARY
    // ========================================================================
    printf("================================================================\n");
    printf("SUMMARY: NEGATIVE vs POSITIVE POWER\n");
    printf("================================================================\n\n");

    printf("NEGATIVE POWER (e.g., -0.811):\n");
    printf("  - Room DECREASES as price rises: room = base * dist^(-0.811)\n");
    printf("  - At dist=10: room = base * 0.154 (15%% of original)\n");
    printf("  - At dist=100: room = base * 0.024 (2.4%% of original)\n");
    printf("  - Result: MORE positions, CLOSER together at higher prices\n");
    printf("  - Risk: VERY aggressive pyramiding\n\n");

    printf("POSITIVE POWER (e.g., +0.5):\n");
    printf("  - Room INCREASES as price rises: room = base * dist^(+0.5)\n");
    printf("  - At dist=10: room = base * 3.16 (316%% of original)\n");
    printf("  - At dist=100: room = base * 10.0 (1000%% of original)\n");
    printf("  - Result: FEWER positions, FARTHER apart at higher prices\n");
    printf("  - Risk: Less aggressive, but may miss moves\n\n");

    printf("================================================================\n");
    return 0;
}
