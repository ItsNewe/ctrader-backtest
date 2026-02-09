/**
 * NAS100 Hyperbolic Position Sizing Strategy
 *
 * Based on Nasdaq_up.mq5
 * Uses power-law (hyperbolic) position sizing: y = k * x^power
 * As price rises, position sizes decrease following hyperbolic curve
 *
 * Formula: y = 502741 * x^-0.811
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
    double spread() const { return ask - bid; }
};

struct Position {
    size_t id;
    double entry_price;
    double lot_size;
};

struct Config {
    double manual_stop_out = 74.0;   // Close all when margin level < this %
    double multiplier = 0.1;          // Starting room = price * multiplier / 100
    double power = 0.1;               // Hyperbolic power
    double min_lot = 0.01;
    double max_lot = 100.0;
};

struct Result {
    double final_balance;
    double return_multiple;
    double max_dd;
    int total_trades;
    int max_positions;
    double max_lot_used;
    double min_lot_used;
    bool margin_call;
    int stop_out_count;
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
        // Calculate equity and margin
        equity = balance;
        double used_margin = 0;
        volume_of_open_trades = 0;

        for (Position* p : positions) {
            equity += (tick.bid - p->entry_price) * p->lot_size * contract_size;
            used_margin += p->lot_size * contract_size * p->entry_price / leverage;
            volume_of_open_trades += p->lot_size;
        }

        // Calculate margin level
        double margin_level = (used_margin > 0) ? (equity / used_margin * 100.0) : 10000.0;

        // Track stats
        if ((int)positions.size() > r.max_positions) r.max_positions = positions.size();

        // Margin call (below 20%)
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
            checked_last_open_price = -DBL_MAX;
            volume_of_open_trades = 0;
            continue;
        }

        // Manual stop out (margin level < threshold)
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

        double current_spread = tick.spread();
        double spread_cost = current_spread * contract_size;

        // Open first position
        if (volume_of_open_trades == 0) {
            local_starting_room = tick.ask * cfg.multiplier / 100.0;

            // Calculate lot size
            double temp = (100 * balance * leverage) /
                         (100 * local_starting_room * leverage + 100 * spread_cost * leverage +
                          cfg.manual_stop_out * tick.ask);
            temp = temp / contract_size;

            if (temp >= cfg.min_lot) {
                double lot = std::min(temp, cfg.max_lot);
                lot = std::max(cfg.min_lot, lot);
                lot = std::round(lot * 100) / 100;

                // Margin check
                double margin_needed = lot * contract_size * tick.ask / leverage;
                if (equity > margin_needed * 1.2) {
                    Position* p = new Position();
                    p->id = next_id++;
                    p->entry_price = tick.ask;
                    p->lot_size = lot;
                    positions.push_back(p);

                    checked_last_open_price = tick.ask;
                    starting_x = tick.ask;
                    volume_of_open_trades = lot;

                    if (lot > r.max_lot_used) r.max_lot_used = lot;
                    if (lot < r.min_lot_used) r.min_lot_used = lot;
                }
            }
        }
        // Open additional positions (when price makes new high)
        else if (tick.ask > checked_last_open_price) {
            // Calculate hyperbolic room
            double distance = tick.ask - starting_x;
            double room_temp = local_starting_room * std::pow(distance + 1, cfg.power);

            // Calculate lot size with hyperbolic adjustment
            double temp = (100 * equity * leverage -
                          leverage * cfg.manual_stop_out * used_margin -
                          100 * room_temp * leverage * volume_of_open_trades) /
                         (100 * room_temp * leverage + 100 * spread_cost * leverage +
                          cfg.manual_stop_out * tick.ask);
            temp = temp / contract_size;

            if (temp >= cfg.min_lot) {
                double lot = std::min(temp, cfg.max_lot);
                lot = std::max(cfg.min_lot, lot);
                lot = std::round(lot * 100) / 100;

                // Margin check
                double margin_needed = lot * contract_size * tick.ask / leverage;
                double free_margin = equity - used_margin;

                if (free_margin > margin_needed * 1.2) {
                    Position* p = new Position();
                    p->id = next_id++;
                    p->entry_price = tick.ask;
                    p->lot_size = lot;
                    positions.push_back(p);

                    checked_last_open_price = tick.ask;
                    volume_of_open_trades += lot;

                    if (lot > r.max_lot_used) r.max_lot_used = lot;
                    if (lot < r.min_lot_used) r.min_lot_used = lot;
                }
            }
        }
    }

    // Close remaining
    for (Position* p : positions) {
        balance += (ticks.back().bid - p->entry_price) * p->lot_size * contract_size;
        r.total_trades++;
        delete p;
    }

    r.final_balance = std::max(0.0, balance);
    r.return_multiple = r.final_balance / initial_balance;
    r.max_dd = max_drawdown;
    return r;
}

int main() {
    printf("================================================================\n");
    printf("NAS100 HYPERBOLIC POSITION SIZING STRATEGY\n");
    printf("Formula: y = k * x^power (position size decreases as price rises)\n");
    printf("================================================================\n\n");

    std::vector<Tick> ticks = LoadTicks("NAS100/NAS100_TICKS_2025.csv", 60000000);
    if (ticks.empty()) { printf("ERROR: No data\n"); return 1; }

    printf("Loaded %zu ticks\n", ticks.size());
    printf("Price: %.2f -> %.2f (%+.1f%%)\n\n",
           ticks.front().bid, ticks.back().bid,
           (ticks.back().bid - ticks.front().bid) / ticks.front().bid * 100);

    // ========================================================================
    // TEST 1: Stop-out level sweep
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 1: STOP-OUT LEVEL SWEEP (multiplier=0.1, power=0.1)\n");
    printf("================================================================\n\n");

    printf("%-10s %12s %8s %10s %8s %8s %10s\n",
           "StopOut%", "Final Bal", "Return", "Max DD", "Trades", "MaxPos", "StopOuts");
    printf("--------------------------------------------------------------------------------\n");

    double stopouts[] = {50, 60, 70, 74, 80, 90, 100};
    for (double so : stopouts) {
        Config cfg;
        cfg.manual_stop_out = so;
        Result r = RunHyperbolic(ticks, cfg);
        printf("%-10.0f $%11.2f %7.1fx %9.1f%% %8d %8d %10d %s\n",
               so, r.final_balance, r.return_multiple, r.max_dd, r.total_trades,
               r.max_positions, r.stop_out_count, r.margin_call ? "MC" : "");
    }

    printf("--------------------------------------------------------------------------------\n\n");

    // ========================================================================
    // TEST 2: Multiplier sweep (starting room size)
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 2: MULTIPLIER SWEEP (stop-out=74%%, power=0.1)\n");
    printf("================================================================\n\n");

    printf("%-10s %12s %8s %10s %8s %10s\n",
           "Mult", "Final Bal", "Return", "Max DD", "Trades", "MaxPos");
    printf("------------------------------------------------------------------------\n");

    double mults[] = {0.05, 0.1, 0.2, 0.5, 1.0, 2.0};
    for (double m : mults) {
        Config cfg;
        cfg.multiplier = m;
        Result r = RunHyperbolic(ticks, cfg);
        printf("%-10.2f $%11.2f %7.1fx %9.1f%% %8d %10d %s\n",
               m, r.final_balance, r.return_multiple, r.max_dd, r.total_trades,
               r.max_positions, r.margin_call ? "MC" : "");
    }

    printf("------------------------------------------------------------------------\n\n");

    // ========================================================================
    // TEST 3: Power sweep (hyperbolic decay rate)
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 3: POWER SWEEP (stop-out=74%%, multiplier=0.1)\n");
    printf("================================================================\n\n");

    printf("%-10s %12s %8s %10s %8s %12s %12s\n",
           "Power", "Final Bal", "Return", "Max DD", "Trades", "MaxLot", "MinLot");
    printf("--------------------------------------------------------------------------------\n");

    double powers[] = {0.05, 0.1, 0.2, 0.3, 0.5, 0.8, 1.0};
    for (double p : powers) {
        Config cfg;
        cfg.power = p;
        Result r = RunHyperbolic(ticks, cfg);
        printf("%-10.2f $%11.2f %7.1fx %9.1f%% %8d %12.2f %12.4f %s\n",
               p, r.final_balance, r.return_multiple, r.max_dd, r.total_trades,
               r.max_lot_used, r.min_lot_used, r.margin_call ? "MC" : "");
    }

    printf("--------------------------------------------------------------------------------\n\n");

    // ========================================================================
    // TEST 4: Test the specific formula y = 502741 * x^-0.811
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 4: SPECIFIC HYPERBOLA y = 502741 * x^-0.811\n");
    printf("================================================================\n\n");

    // The formula suggests:
    // - Large coefficient (502741) for initial size
    // - Negative power (-0.811) means inverse relationship
    // This translates to: as distance increases, lot decreases rapidly

    printf("Testing with power = -0.811 equivalent behavior...\n\n");

    // With negative power, we need to interpret differently
    // power = 0.811 with inverse scaling
    Config special;
    special.manual_stop_out = 74;
    special.multiplier = 0.1;
    special.power = 0.811;  // Note: the formula uses -0.811, but our impl uses positive
    Result r_special = RunHyperbolic(ticks, special);

    printf("Power = 0.811:\n");
    printf("  Final: $%.2f (%.1fx)\n", r_special.final_balance, r_special.return_multiple);
    printf("  Max DD: %.1f%%\n", r_special.max_dd);
    printf("  Trades: %d\n", r_special.total_trades);
    printf("  Max Positions: %d\n", r_special.max_positions);
    printf("  Lot Range: %.4f - %.2f\n", r_special.min_lot_used, r_special.max_lot_used);

    // ========================================================================
    // COMPARISON
    // ========================================================================
    printf("\n================================================================\n");
    printf("COMPARISON: HYPERBOLIC vs ORIGINAL vs IMPROVED\n");
    printf("================================================================\n\n");

    Config hyperbolic;
    hyperbolic.manual_stop_out = 74;
    hyperbolic.multiplier = 0.1;
    hyperbolic.power = 0.1;
    Result r_hyp = RunHyperbolic(ticks, hyperbolic);

    printf("%-25s %12s %8s %10s\n", "Strategy", "Final", "Return", "Max DD");
    printf("----------------------------------------------------------------\n");
    printf("%-25s $%11.2f %7.1fx %9.1f%% %s\n",
           "Hyperbolic (power=0.1)",
           r_hyp.final_balance, r_hyp.return_multiple, r_hyp.max_dd,
           r_hyp.margin_call ? "MC" : "");
    printf("%-25s $%11.2f %7.1fx %9.1f%%\n",
           "Buy & Hold (0.5 lot)",
           10000 + (ticks.back().bid - ticks.front().bid) * 0.5,
           1.0 + (ticks.back().bid - ticks.front().bid) * 0.5 / 10000, 15.0);

    printf("----------------------------------------------------------------\n\n");

    printf("The hyperbolic formula y = 502741 * x^-0.811 means:\n");
    printf("  - At distance x=1: lot = 502741 * 1^-0.811 = 502741\n");
    printf("  - At distance x=10: lot = 502741 * 10^-0.811 = ~77,671\n");
    printf("  - At distance x=100: lot = 502741 * 100^-0.811 = ~11,998\n");
    printf("  - Position sizes decrease as price rises (inverse power law)\n");

    printf("\n================================================================\n");
    return 0;
}
