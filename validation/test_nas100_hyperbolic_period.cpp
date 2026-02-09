/**
 * NAS100 Hyperbolic Strategy - Period Comparison
 * Test on strong bull period (Apr-Jul) vs full year
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
    double power = 0.1;
    double min_lot = 0.01;
    double max_lot = 100.0;
};

struct Result {
    double final_balance;
    double return_multiple;
    double max_dd;
    int total_trades;
    int max_positions;
    int stop_out_count;
    bool margin_call;
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
            double temp = (100 * balance * leverage) /
                         (100 * local_starting_room * leverage + 100 * spread_cost * leverage +
                          cfg.manual_stop_out * tick.ask);
            temp = temp / contract_size;

            if (temp >= cfg.min_lot) {
                double lot = std::min(temp, cfg.max_lot);
                lot = std::round(lot * 100) / 100;

                double margin_needed = lot * contract_size * tick.ask / leverage;
                if (equity > margin_needed * 1.2) {
                    Position* p = new Position();
                    p->id = next_id++;
                    p->entry_price = tick.ask;
                    p->lot_size = lot;
                    positions.push_back(p);
                    checked_last_open_price = tick.ask;
                    starting_x = tick.ask;
                }
            }
        }
        else if (tick.ask > checked_last_open_price) {
            double distance = tick.ask - starting_x;
            double room_temp = local_starting_room * std::pow(distance + 1, cfg.power);

            double temp = (100 * equity * leverage -
                          leverage * cfg.manual_stop_out * used_margin -
                          100 * room_temp * leverage * volume_of_open_trades) /
                         (100 * room_temp * leverage + 100 * spread_cost * leverage +
                          cfg.manual_stop_out * tick.ask);
            temp = temp / contract_size;

            if (temp >= cfg.min_lot) {
                double lot = std::min(temp, cfg.max_lot);
                lot = std::round(lot * 100) / 100;

                double margin_needed = lot * contract_size * tick.ask / leverage;
                double free_margin = equity - used_margin;

                if (free_margin > margin_needed * 1.2) {
                    Position* p = new Position();
                    p->id = next_id++;
                    p->entry_price = tick.ask;
                    p->lot_size = lot;
                    positions.push_back(p);
                    checked_last_open_price = tick.ask;
                }
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
    return r;
}

int main() {
    printf("================================================================\n");
    printf("NAS100 HYPERBOLIC STRATEGY - PERIOD COMPARISON\n");
    printf("================================================================\n\n");

    const char* file = "NAS100/NAS100_TICKS_2025.csv";

    // Load different periods
    printf("Loading periods...\n");
    auto apr_jul = LoadTicks(file, 60000000, 2025, 4, 22, 2025, 7, 31);
    auto full_year = LoadTicks(file, 60000000, 2025, 1, 1, 2026, 1, 15);
    auto q1 = LoadTicks(file, 60000000, 2025, 1, 1, 2025, 3, 31);

    printf("Apr-Jul: %zu ticks (%.2f -> %.2f = %+.1f%%)\n",
           apr_jul.size(), apr_jul.front().bid, apr_jul.back().bid,
           (apr_jul.back().bid - apr_jul.front().bid) / apr_jul.front().bid * 100);
    printf("Full Year: %zu ticks\n", full_year.size());
    printf("Q1: %zu ticks\n\n", q1.size());

    // Test hyperbolic on Apr-Jul (strong bull)
    printf("================================================================\n");
    printf("TEST ON APR-JUL 2025 (+30.4%% rally)\n");
    printf("================================================================\n\n");

    printf("%-12s %-8s %12s %8s %10s %8s %8s\n",
           "StopOut", "Power", "Final Bal", "Return", "Max DD", "Trades", "StopOuts");
    printf("--------------------------------------------------------------------------------\n");

    double stopouts[] = {50, 60, 70, 74, 80, 90};
    double powers[] = {0.1, 0.2, 0.5};

    for (double so : stopouts) {
        for (double pw : powers) {
            Config cfg;
            cfg.manual_stop_out = so;
            cfg.power = pw;
            Result r = RunHyperbolic(apr_jul, cfg);
            printf("%-12.0f %-8.1f $%11.2f %7.1fx %9.1f%% %8d %8d %s\n",
                   so, pw, r.final_balance, r.return_multiple, r.max_dd,
                   r.total_trades, r.stop_out_count, r.margin_call ? "MC" : "");
        }
    }

    printf("--------------------------------------------------------------------------------\n\n");

    // Best config on Apr-Jul vs Full Year
    printf("================================================================\n");
    printf("BEST CONFIG COMPARISON: APR-JUL vs FULL YEAR\n");
    printf("================================================================\n\n");

    Config best;
    best.manual_stop_out = 90;
    best.power = 0.1;

    Result r_apr = RunHyperbolic(apr_jul, best);
    Result r_full = RunHyperbolic(full_year, best);
    Result r_q1 = RunHyperbolic(q1, best);

    printf("Config: StopOut=90%%, Power=0.1\n\n");
    printf("%-15s %12s %8s %10s %8s\n", "Period", "Final Bal", "Return", "Max DD", "StopOuts");
    printf("----------------------------------------------------------------\n");
    printf("%-15s $%11.2f %7.1fx %9.1f%% %8d %s\n", "Apr-Jul (+30%)",
           r_apr.final_balance, r_apr.return_multiple, r_apr.max_dd,
           r_apr.stop_out_count, r_apr.margin_call ? "MC" : "");
    printf("%-15s $%11.2f %7.1fx %9.1f%% %8d %s\n", "Full Year (+24%)",
           r_full.final_balance, r_full.return_multiple, r_full.max_dd,
           r_full.stop_out_count, r_full.margin_call ? "MC" : "");
    printf("%-15s $%11.2f %7.1fx %9.1f%% %8d %s\n", "Q1 (-7%)",
           r_q1.final_balance, r_q1.return_multiple, r_q1.max_dd,
           r_q1.stop_out_count, r_q1.margin_call ? "MC" : "");

    printf("----------------------------------------------------------------\n\n");

    // Compare with simple fixed lot
    printf("================================================================\n");
    printf("HYPERBOLIC vs SIMPLE FIXED LOT\n");
    printf("================================================================\n\n");

    double apr_move = (apr_jul.back().bid - apr_jul.front().bid);
    printf("On Apr-Jul period (+30.4%% move):\n\n");
    printf("%-25s %12s %8s %10s\n", "Strategy", "Final", "Return", "Max DD");
    printf("----------------------------------------------------------------\n");
    printf("%-25s $%11.2f %7.1fx %9.1f%%\n", "Hyperbolic (so=90,pw=0.1)",
           r_apr.final_balance, r_apr.return_multiple, r_apr.max_dd);
    printf("%-25s $%11.2f %7.1fx %9.1f%%\n", "Buy & Hold (0.5 lot)",
           10000 + apr_move * 0.5, 1.0 + apr_move * 0.5 / 10000, 15.0);
    printf("%-25s $%11.2f %7.1fx %9.1f%%\n", "Buy & Hold (1.0 lot)",
           10000 + apr_move * 1.0, 1.0 + apr_move * 1.0 / 10000, 30.0);

    printf("----------------------------------------------------------------\n");

    printf("\n================================================================\n");
    return 0;
}
