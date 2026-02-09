/**
 * NAS100 Improved Strategy V3: Hybrid Grid + Trend
 *
 * Combines fill-up grid with trend awareness:
 * 1. Use SMA for trend direction
 * 2. In uptrend: Buy dips (grid below price)
 * 3. Take profit at grid spacing
 * 4. Stop buying when trend weakens
 *
 * This adapts the successful XAUUSD fill-up approach for indices
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

struct Config {
    double spacing = 50.0;         // Grid spacing in points
    double tp_multiplier = 2.0;    // TP = spacing * multiplier
    int max_positions = 15;
    double lot_size = 0.1;
    int sma_period = 200;          // Trend filter
    bool require_uptrend = true;   // Only trade in uptrend
    double stop_new_at_dd = 5.0;   // Stop new positions at X% DD
    double close_all_at_dd = 15.0; // Emergency close at X% DD
    bool session_filter = false;   // Avoid US open
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

    SMA sma(cfg.sma_period);
    double lowest_buy = DBL_MAX;
    double highest_buy = -DBL_MAX;
    bool partial_done = false;
    bool all_closed = false;

    for (const Tick& tick : ticks) {
        sma.Add(tick.bid);

        // Calculate equity
        equity = balance;
        double used_margin = 0;
        for (Position* p : positions) {
            equity += (tick.bid - p->entry_price) * p->lot_size * contract_size;
            used_margin += p->lot_size * contract_size * p->entry_price / leverage;
        }

        r.max_positions_held = std::max(r.max_positions_held, (int)positions.size());

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

        if (positions.empty()) {
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
        if (dd_pct > max_drawdown) max_drawdown = dd_pct;

        // Emergency close
        if (dd_pct > cfg.close_all_at_dd && !all_closed && !positions.empty()) {
            for (Position* p : positions) {
                balance += (tick.bid - p->entry_price) * p->lot_size * contract_size;
                r.total_trades++;
                delete p;
            }
            positions.clear();
            all_closed = true;
            equity = balance;
            peak_equity = balance;
            lowest_buy = DBL_MAX;
            highest_buy = -DBL_MAX;
            continue;
        }

        // Partial close at intermediate DD
        if (dd_pct > cfg.stop_new_at_dd * 1.5 && !partial_done && positions.size() > 1) {
            std::sort(positions.begin(), positions.end(), [&](Position* a, Position* b) {
                return (tick.bid - a->entry_price) < (tick.bid - b->entry_price);
            });
            int to_close = std::max(1, (int)(positions.size() * 0.3));
            for (int i = 0; i < to_close && !positions.empty(); i++) {
                Position* p = positions[0];
                double pl = (tick.bid - p->entry_price) * p->lot_size * contract_size;
                balance += pl;
                r.total_trades++;
                if (pl > 0) r.winning_trades++;
                delete p;
                positions.erase(positions.begin());
            }
            partial_done = true;
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
        bool trend_ok = !cfg.require_uptrend || !sma.IsReady() || (tick.bid > sma.Get());
        bool session_ok = !cfg.session_filter || (tick.hour < 14 || tick.hour >= 18);
        bool dd_ok = dd_pct < cfg.stop_new_at_dd;
        bool under_limit = (int)positions.size() < cfg.max_positions;

        if (!trend_ok || !session_ok || !dd_ok || !under_limit) continue;

        // Grid entry logic (fill-up style)
        bool should_open = false;
        if (positions.empty()) {
            should_open = true;
        } else if (lowest_buy >= tick.ask + cfg.spacing) {
            // Price dropped below grid - buy lower
            should_open = true;
        } else if (highest_buy <= tick.ask - cfg.spacing) {
            // Price rose above grid - fill gap
            should_open = true;
        }

        if (should_open) {
            double margin_needed = cfg.lot_size * contract_size * tick.ask / leverage;
            double free_margin = equity - used_margin;

            if (free_margin > margin_needed * 1.5) {
                Position* p = new Position();
                p->id = next_id++;
                p->entry_price = tick.ask;
                p->lot_size = cfg.lot_size;
                p->take_profit = tick.ask + tick.spread() + cfg.spacing * cfg.tp_multiplier;
                positions.push_back(p);
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
    printf("NAS100 IMPROVED V3: HYBRID GRID + TREND\n");
    printf("================================================================\n\n");

    std::vector<Tick> ticks = LoadTicks("NAS100/NAS100_TICKS_2025.csv", 60000000);
    if (ticks.empty()) { printf("ERROR: No data\n"); return 1; }
    printf("Loaded %zu ticks, price %+.1f%%\n\n", ticks.size(),
           (ticks.back().bid - ticks.front().bid) / ticks.front().bid * 100);

    // Test spacing
    printf("TEST 1: GRID SPACING SWEEP\n");
    printf("%-10s %12s %8s %10s %8s %8s\n", "Spacing", "Final", "Return", "MaxDD", "Trades", "WinRate");
    printf("------------------------------------------------------------------------\n");

    double spacings[] = {25, 50, 75, 100, 150, 200};
    for (double sp : spacings) {
        Config cfg;
        cfg.spacing = sp;
        Result r = RunStrategy(ticks, cfg);
        double wr = r.total_trades > 0 ? r.winning_trades * 100.0 / r.total_trades : 0;
        printf("%-10.0f $%11.2f %7.1fx %9.1f%% %8d %7.1f%% %s\n",
               sp, r.final_balance, r.return_multiple, r.max_dd, r.total_trades, wr,
               r.margin_call ? "MC" : "");
    }

    // Test TP multiplier
    printf("\nTEST 2: TP MULTIPLIER SWEEP (spacing=50)\n");
    printf("%-10s %12s %8s %10s %8s %8s\n", "TP Mult", "Final", "Return", "MaxDD", "Trades", "WinRate");
    printf("------------------------------------------------------------------------\n");

    double tps[] = {1.0, 1.5, 2.0, 2.5, 3.0, 4.0};
    for (double tp : tps) {
        Config cfg;
        cfg.spacing = 50;
        cfg.tp_multiplier = tp;
        Result r = RunStrategy(ticks, cfg);
        double wr = r.total_trades > 0 ? r.winning_trades * 100.0 / r.total_trades : 0;
        printf("%-10.1f $%11.2f %7.1fx %9.1f%% %8d %7.1f%% %s\n",
               tp, r.final_balance, r.return_multiple, r.max_dd, r.total_trades, wr,
               r.margin_call ? "MC" : "");
    }

    // Test with/without trend filter
    printf("\nTEST 3: TREND FILTER COMPARISON\n");
    printf("%-20s %12s %8s %10s %8s\n", "Config", "Final", "Return", "MaxDD", "Trades");
    printf("------------------------------------------------------------------------\n");

    Config no_trend;
    no_trend.spacing = 50;
    no_trend.require_uptrend = false;
    Result r1 = RunStrategy(ticks, no_trend);
    printf("%-20s $%11.2f %7.1fx %9.1f%% %8d %s\n", "No trend filter",
           r1.final_balance, r1.return_multiple, r1.max_dd, r1.total_trades,
           r1.margin_call ? "MC" : "");

    Config with_trend;
    with_trend.spacing = 50;
    with_trend.require_uptrend = true;
    with_trend.sma_period = 200;
    Result r2 = RunStrategy(ticks, with_trend);
    printf("%-20s $%11.2f %7.1fx %9.1f%% %8d %s\n", "SMA 200 filter",
           r2.final_balance, r2.return_multiple, r2.max_dd, r2.total_trades,
           r2.margin_call ? "MC" : "");

    // Test lot sizes
    printf("\nTEST 4: LOT SIZE SWEEP\n");
    printf("%-10s %12s %8s %10s %8s\n", "Lot", "Final", "Return", "MaxDD", "Trades");
    printf("----------------------------------------------------------------\n");

    double lots[] = {0.05, 0.1, 0.2, 0.3, 0.5};
    for (double lot : lots) {
        Config cfg;
        cfg.spacing = 50;
        cfg.lot_size = lot;
        Result r = RunStrategy(ticks, cfg);
        printf("%-10.2f $%11.2f %7.1fx %9.1f%% %8d %s\n",
               lot, r.final_balance, r.return_multiple, r.max_dd, r.total_trades,
               r.margin_call ? "MC" : "");
    }

    // Test DD protection levels
    printf("\nTEST 5: DD PROTECTION SWEEP\n");
    printf("%-12s %12s %8s %10s %8s\n", "CloseAllDD", "Final", "Return", "MaxDD", "Trades");
    printf("----------------------------------------------------------------\n");

    double dds[] = {10, 15, 20, 25, 30, 50};
    for (double dd : dds) {
        Config cfg;
        cfg.spacing = 50;
        cfg.close_all_at_dd = dd;
        Result r = RunStrategy(ticks, cfg);
        printf("%-12.0f $%11.2f %7.1fx %9.1f%% %8d %s\n",
               dd, r.final_balance, r.return_multiple, r.max_dd, r.total_trades,
               r.margin_call ? "MC" : "");
    }

    // Best configuration
    printf("\n================================================================\n");
    printf("OPTIMIZED HYBRID GRID CONFIGURATION\n");
    printf("================================================================\n");

    Config best;
    best.spacing = 50;
    best.tp_multiplier = 2.0;
    best.lot_size = 0.1;
    best.max_positions = 15;
    best.require_uptrend = true;
    best.sma_period = 200;
    best.stop_new_at_dd = 5.0;
    best.close_all_at_dd = 15.0;
    Result r = RunStrategy(ticks, best);

    printf("Config: Spacing=%.0f, TP=%.1fx, Lot=%.2f, MaxPos=%d, SMA=%d\n",
           best.spacing, best.tp_multiplier, best.lot_size, best.max_positions, best.sma_period);
    printf("DD Protection: StopNew=%.0f%%, CloseAll=%.0f%%\n",
           best.stop_new_at_dd, best.close_all_at_dd);
    printf("\nResult: $%.2f (%.1fx), MaxDD=%.1f%%, Trades=%d, WinRate=%.1f%%\n",
           r.final_balance, r.return_multiple, r.max_dd, r.total_trades,
           r.total_trades > 0 ? r.winning_trades * 100.0 / r.total_trades : 0);

    printf("\n================================================================\n");
    return 0;
}
