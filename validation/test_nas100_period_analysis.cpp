/**
 * NAS100 Period Analysis
 * Tests strategies on specific date range and with higher survive %
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
    int year, month, day;
    int hour;
    double spread() const { return ask - bid; }
};

struct Position {
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

struct Result {
    double final_balance;
    double return_multiple;
    double max_dd;
    int total_trades;
    int winning_trades;
    bool margin_call;
    double first_price;
    double last_price;
    double market_change_pct;
};

std::vector<Tick> LoadTicks(const char* filename, size_t max_count,
                            int start_year, int start_month, int start_day,
                            int end_year, int end_month, int end_day) {
    std::vector<Tick> ticks;
    ticks.reserve(max_count);

    FILE* f = fopen(filename, "r");
    if (!f) return ticks;

    char line[256];
    fgets(line, sizeof(line), f);  // Skip header

    while (fgets(line, sizeof(line), f) && ticks.size() < max_count) {
        Tick tick;

        // Parse date: "2025.04.22 11:40:30.123"
        if (strlen(line) >= 19) {
            char y[5] = {line[0], line[1], line[2], line[3], 0};
            char m[3] = {line[5], line[6], 0};
            char d[3] = {line[8], line[9], 0};
            char h[3] = {line[11], line[12], 0};

            tick.year = atoi(y);
            tick.month = atoi(m);
            tick.day = atoi(d);
            tick.hour = atoi(h);

            // Date filter
            int date = tick.year * 10000 + tick.month * 100 + tick.day;
            int start_date = start_year * 10000 + start_month * 100 + start_day;
            int end_date = end_year * 10000 + end_month * 100 + end_day;

            if (date < start_date || date > end_date) continue;
        }

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

// Original strategy with configurable survive %
Result RunOriginal(const std::vector<Tick>& ticks, double survive_pct, double lot_base) {
    Result r = {0};
    if (ticks.empty()) return r;

    r.first_price = ticks.front().bid;
    r.last_price = ticks.back().bid;
    r.market_change_pct = (r.last_price - r.first_price) / r.first_price * 100.0;

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

    for (const Tick& tick : ticks) {
        equity = balance;
        double volume_open = 0;
        double used_margin = 0;

        for (Position* p : positions) {
            equity += (tick.bid - p->entry_price) * p->lot_size * contract_size;
            volume_open += p->lot_size;
            used_margin += p->lot_size * contract_size * p->entry_price / leverage;
        }

        // Margin call
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
            continue;
        }

        if (positions.empty()) peak_equity = balance;
        if (equity > peak_equity) peak_equity = equity;

        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        if (dd_pct > max_drawdown) max_drawdown = dd_pct;

        // Original entry: open when price makes new high
        if (volume_open == 0 || tick.ask > checked_last_open_price) {
            // Calculate lot based on surviving survive_pct drop
            double end_price = tick.ask * ((100.0 - survive_pct) / 100.0);
            double distance = tick.ask - end_price;

            if (distance > 0) {
                double lot = lot_base;  // Use fixed lot for simplicity

                double margin_needed = lot * contract_size * tick.ask / leverage;
                double free_margin = equity - used_margin;

                if (free_margin > margin_needed * 1.2) {
                    Position* p = new Position();
                    p->id = next_id++;
                    p->entry_price = tick.ask;
                    p->lot_size = lot;
                    p->take_profit = 0;  // No TP in original
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
    r.return_multiple = r.final_balance / initial_balance;
    r.max_dd = max_drawdown;
    return r;
}

// Improved strategy with spacing, TP, and trend filter
Result RunImproved(const std::vector<Tick>& ticks, double spacing, double tp_mult,
                   double lot_size, int max_pos, double close_dd) {
    Result r = {0};
    if (ticks.empty()) return r;

    r.first_price = ticks.front().bid;
    r.last_price = ticks.back().bid;
    r.market_change_pct = (r.last_price - r.first_price) / r.first_price * 100.0;

    const double initial_balance = 10000.0;
    const double contract_size = 1.0;
    const double leverage = 500.0;

    double balance = initial_balance;
    double equity = balance;
    double peak_equity = balance;
    double max_drawdown = 0;

    std::vector<Position*> positions;
    size_t next_id = 1;

    SMA sma(200);
    double lowest_buy = DBL_MAX;
    double highest_buy = -DBL_MAX;

    for (const Tick& tick : ticks) {
        sma.Add(tick.bid);

        equity = balance;
        double used_margin = 0;
        for (Position* p : positions) {
            equity += (tick.bid - p->entry_price) * p->lot_size * contract_size;
            used_margin += p->lot_size * contract_size * p->entry_price / leverage;
        }

        // Margin call
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
            lowest_buy = DBL_MAX;
            highest_buy = -DBL_MAX;
            continue;
        }

        if (positions.empty()) peak_equity = balance;
        if (equity > peak_equity) peak_equity = equity;

        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        if (dd_pct > max_drawdown) max_drawdown = dd_pct;

        // DD protection
        if (dd_pct > close_dd && !positions.empty()) {
            for (Position* p : positions) {
                double pl = (tick.bid - p->entry_price) * p->lot_size * contract_size;
                balance += pl;
                r.total_trades++;
                if (pl > 0) r.winning_trades++;
                delete p;
            }
            positions.clear();
            equity = balance;
            peak_equity = balance;
            lowest_buy = DBL_MAX;
            highest_buy = -DBL_MAX;
            continue;
        }

        // Check TPs
        for (auto it = positions.begin(); it != positions.end();) {
            Position* p = *it;
            if (tick.bid >= p->take_profit) {
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

        // Update grid bounds
        lowest_buy = DBL_MAX;
        highest_buy = -DBL_MAX;
        for (Position* p : positions) {
            lowest_buy = std::min(lowest_buy, p->entry_price);
            highest_buy = std::max(highest_buy, p->entry_price);
        }

        // Entry conditions
        bool trend_ok = !sma.IsReady() || (tick.bid > sma.Get());
        bool dd_ok = dd_pct < 5.0;
        bool under_limit = (int)positions.size() < max_pos;

        if (!trend_ok || !dd_ok || !under_limit) continue;

        // Grid entry
        bool should_open = positions.empty() ||
                          (lowest_buy >= tick.ask + spacing) ||
                          (highest_buy <= tick.ask - spacing);

        if (should_open) {
            double margin_needed = lot_size * contract_size * tick.ask / leverage;
            double free_margin = equity - used_margin;

            if (free_margin > margin_needed * 1.5) {
                Position* p = new Position();
                p->id = next_id++;
                p->entry_price = tick.ask;
                p->lot_size = lot_size;
                p->take_profit = tick.ask + tick.spread() + spacing * tp_mult;
                positions.push_back(p);
            }
        }
    }

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
    printf("NAS100 PERIOD ANALYSIS\n");
    printf("================================================================\n\n");

    const char* filename = "NAS100/NAS100_TICKS_2025.csv";

    // Test period: 2025.04.22 to 2025.07.31
    printf("Loading data for period: 2025.04.22 to 2025.07.31...\n");
    std::vector<Tick> period_ticks = LoadTicks(filename, 60000000, 2025, 4, 22, 2025, 7, 31);

    if (period_ticks.empty()) {
        printf("ERROR: No data for specified period\n");
        return 1;
    }

    printf("Loaded %zu ticks for period\n", period_ticks.size());
    printf("Price: %.2f -> %.2f (%+.1f%%)\n\n",
           period_ticks.front().bid, period_ticks.back().bid,
           (period_ticks.back().bid - period_ticks.front().bid) / period_ticks.front().bid * 100);

    // ========================================================================
    // TEST 1: Original strategy with different survive %
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 1: ORIGINAL STRATEGY - SURVIVE %% SWEEP\n");
    printf("================================================================\n\n");

    printf("%-12s %12s %8s %10s %8s %10s\n",
           "Survive%", "Final Bal", "Return", "Max DD", "Trades", "Status");
    printf("------------------------------------------------------------------------\n");

    double survives[] = {4, 6, 8, 10, 15, 20, 25, 30, 40, 50};
    for (double surv : survives) {
        Result r = RunOriginal(period_ticks, surv, 0.05);
        printf("%-12.0f $%11.2f %7.1fx %9.1f%% %8d %s\n",
               surv, r.final_balance, r.return_multiple, r.max_dd, r.total_trades,
               r.margin_call ? "MARGIN CALL" : "OK");
    }

    printf("------------------------------------------------------------------------\n\n");

    // ========================================================================
    // TEST 2: Original with different lot sizes at higher survive %
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 2: ORIGINAL @ SURVIVE 30%% - LOT SIZE SWEEP\n");
    printf("================================================================\n\n");

    printf("%-12s %12s %8s %10s %8s\n", "Lot", "Final Bal", "Return", "Max DD", "Trades");
    printf("----------------------------------------------------------------\n");

    double lots[] = {0.01, 0.02, 0.03, 0.05, 0.1, 0.2};
    for (double lot : lots) {
        Result r = RunOriginal(period_ticks, 30.0, lot);
        printf("%-12.2f $%11.2f %7.1fx %9.1f%% %8d %s\n",
               lot, r.final_balance, r.return_multiple, r.max_dd, r.total_trades,
               r.margin_call ? "MC" : "");
    }

    printf("----------------------------------------------------------------\n\n");

    // ========================================================================
    // TEST 3: Improved strategy on this period
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 3: IMPROVED STRATEGY ON THIS PERIOD\n");
    printf("================================================================\n\n");

    printf("%-20s %12s %8s %10s %8s\n", "Config", "Final Bal", "Return", "Max DD", "Trades");
    printf("------------------------------------------------------------------------\n");

    // Various improved configs
    Result r1 = RunImproved(period_ticks, 50, 2.0, 0.1, 15, 15);
    printf("%-20s $%11.2f %7.1fx %9.1f%% %8d\n", "Grid Sp=50 TP=2x",
           r1.final_balance, r1.return_multiple, r1.max_dd, r1.total_trades);

    Result r2 = RunImproved(period_ticks, 100, 2.0, 0.1, 15, 15);
    printf("%-20s $%11.2f %7.1fx %9.1f%% %8d\n", "Grid Sp=100 TP=2x",
           r2.final_balance, r2.return_multiple, r2.max_dd, r2.total_trades);

    Result r3 = RunImproved(period_ticks, 50, 3.0, 0.1, 15, 15);
    printf("%-20s $%11.2f %7.1fx %9.1f%% %8d\n", "Grid Sp=50 TP=3x",
           r3.final_balance, r3.return_multiple, r3.max_dd, r3.total_trades);

    Result r4 = RunImproved(period_ticks, 50, 2.0, 0.2, 15, 15);
    printf("%-20s $%11.2f %7.1fx %9.1f%% %8d\n", "Grid Lot=0.2",
           r4.final_balance, r4.return_multiple, r4.max_dd, r4.total_trades);

    Result r5 = RunImproved(period_ticks, 50, 2.0, 0.1, 15, 25);
    printf("%-20s $%11.2f %7.1fx %9.1f%% %8d\n", "Grid DD=25%",
           r5.final_balance, r5.return_multiple, r5.max_dd, r5.total_trades);

    printf("------------------------------------------------------------------------\n\n");

    // ========================================================================
    // TEST 4: Compare different periods
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 4: PERIOD COMPARISON\n");
    printf("================================================================\n\n");

    // Full year
    std::vector<Tick> full_year = LoadTicks(filename, 60000000, 2025, 1, 1, 2026, 1, 15);
    // Q1 2025
    std::vector<Tick> q1 = LoadTicks(filename, 60000000, 2025, 1, 1, 2025, 3, 31);
    // Q2 2025
    std::vector<Tick> q2 = LoadTicks(filename, 60000000, 2025, 4, 1, 2025, 6, 30);
    // Q3 2025
    std::vector<Tick> q3 = LoadTicks(filename, 60000000, 2025, 7, 1, 2025, 9, 30);
    // Q4 2025
    std::vector<Tick> q4 = LoadTicks(filename, 60000000, 2025, 10, 1, 2025, 12, 31);

    printf("%-15s %10s %12s %8s %10s\n", "Period", "Ticks", "Market", "Return", "Max DD");
    printf("------------------------------------------------------------------------\n");

    auto test_period = [](const std::vector<Tick>& t, const char* name) {
        if (t.empty()) {
            printf("%-15s %10s %12s %8s %10s\n", name, "NO DATA", "-", "-", "-");
            return;
        }
        Result r = RunImproved(t, 50, 2.0, 0.1, 15, 15);
        printf("%-15s %10zu %+11.1f%% %7.1fx %9.1f%%\n",
               name, t.size(), r.market_change_pct, r.return_multiple, r.max_dd);
    };

    test_period(full_year, "Full Year");
    test_period(q1, "Q1 2025");
    test_period(q2, "Q2 2025");
    test_period(q3, "Q3 2025");
    test_period(q4, "Q4 2025");
    test_period(period_ticks, "Apr22-Jul31");

    printf("------------------------------------------------------------------------\n\n");

    // ========================================================================
    // SUMMARY
    // ========================================================================
    printf("================================================================\n");
    printf("SUMMARY FOR PERIOD 2025.04.22 - 2025.07.31\n");
    printf("================================================================\n\n");

    Result orig_best = RunOriginal(period_ticks, 30.0, 0.05);
    Result impr_best = RunImproved(period_ticks, 50, 2.0, 0.1, 15, 15);

    printf("Market moved: %+.1f%%\n\n", orig_best.market_change_pct);

    printf("Original (survive=30%%, lot=0.05):\n");
    printf("  Return: %.1fx ($%.2f)\n", orig_best.return_multiple, orig_best.final_balance);
    printf("  Max DD: %.1f%%\n", orig_best.max_dd);
    printf("  Status: %s\n\n", orig_best.margin_call ? "MARGIN CALL" : "OK");

    printf("Improved (grid sp=50, tp=2x):\n");
    printf("  Return: %.1fx ($%.2f)\n", impr_best.return_multiple, impr_best.final_balance);
    printf("  Max DD: %.1f%%\n", impr_best.max_dd);
    printf("  Win Rate: %.1f%%\n", impr_best.total_trades > 0 ?
           impr_best.winning_trades * 100.0 / impr_best.total_trades : 0);

    printf("\n================================================================\n");
    return 0;
}
