/**
 * NAS100 Improved Strategy V2: SMA Filter + Pullback Entry
 *
 * Key concept: Don't chase highs, buy pullbacks in uptrend
 * 1. Trend filter: Only trade when price > SMA (uptrend)
 * 2. Entry on pullback: Buy when price touches SMA from above
 * 3. Overbought filter: Don't buy when too far above SMA
 * 4. Take profit when price extends above SMA
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
    double entry_sma;
};

class SMA {
    std::deque<double> prices;
    int period;
    double sum = 0;
public:
    SMA(int p) : period(p) {}
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
    int sma_period = 200;
    double pullback_pct = 0.5;     // Buy when price within X% of SMA
    double overbought_pct = 3.0;   // Don't buy if > X% above SMA
    double tp_pct = 2.0;           // Take profit X% above entry
    double sl_pct = 2.0;           // Stop loss X% below entry
    double lot_size = 0.1;
    int max_positions = 10;
    double close_all_at_dd = 20.0;
};

struct Result {
    double final_balance;
    double return_multiple;
    double max_dd;
    int total_trades;
    int winning_trades;
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
    bool was_above_sma = false;

    for (const Tick& tick : ticks) {
        sma.Add(tick.bid);
        if (!sma.IsReady()) continue;

        double sma_val = sma.Get();
        double pct_from_sma = (tick.bid - sma_val) / sma_val * 100.0;
        bool is_above_sma = tick.bid > sma_val;

        // Calculate equity
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
            continue;
        }

        // Check TP/SL
        for (auto it = positions.begin(); it != positions.end();) {
            Position* p = *it;
            double pct_change = (tick.bid - p->entry_price) / p->entry_price * 100.0;
            bool close = false;

            if (pct_change >= cfg.tp_pct) close = true;  // TP
            if (pct_change <= -cfg.sl_pct) close = true; // SL

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

        // Entry: Pullback to SMA in uptrend
        bool in_uptrend = is_above_sma;
        bool pullback = pct_from_sma <= cfg.pullback_pct && pct_from_sma >= 0;
        bool not_overbought = pct_from_sma < cfg.overbought_pct;
        bool under_limit = (int)positions.size() < cfg.max_positions;
        bool was_extended = was_above_sma && (tick.bid < sma_val * 1.01);  // Came back to SMA

        if (in_uptrend && pullback && not_overbought && under_limit && was_extended) {
            double margin_needed = cfg.lot_size * contract_size * tick.ask / leverage;
            double free_margin = equity - used_margin;

            if (free_margin > margin_needed * 1.5) {
                Position* p = new Position();
                p->id = next_id++;
                p->entry_price = tick.ask;
                p->lot_size = cfg.lot_size;
                p->entry_sma = sma_val;
                positions.push_back(p);
            }
        }

        was_above_sma = is_above_sma;
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
    printf("NAS100 IMPROVED V2: SMA PULLBACK STRATEGY\n");
    printf("================================================================\n\n");

    std::vector<Tick> ticks = LoadTicks("NAS100/NAS100_TICKS_2025.csv", 60000000);
    if (ticks.empty()) { printf("ERROR: No data\n"); return 1; }
    printf("Loaded %zu ticks, price %+.1f%%\n\n", ticks.size(),
           (ticks.back().bid - ticks.front().bid) / ticks.front().bid * 100);

    // Test SMA periods
    printf("TEST 1: SMA PERIOD SWEEP\n");
    printf("%-10s %12s %8s %10s %8s %8s\n", "SMA", "Final", "Return", "MaxDD", "Trades", "WinRate");
    printf("------------------------------------------------------------------------\n");

    int periods[] = {50, 100, 200, 500, 1000};
    for (int p : periods) {
        Config cfg;
        cfg.sma_period = p;
        Result r = RunStrategy(ticks, cfg);
        double wr = r.total_trades > 0 ? r.winning_trades * 100.0 / r.total_trades : 0;
        printf("%-10d $%11.2f %7.1fx %9.1f%% %8d %7.1f%% %s\n",
               p, r.final_balance, r.return_multiple, r.max_dd, r.total_trades, wr,
               r.margin_call ? "MC" : "");
    }

    // Test pullback threshold
    printf("\nTEST 2: PULLBACK THRESHOLD SWEEP (SMA=200)\n");
    printf("%-10s %12s %8s %10s %8s %8s\n", "Pullback%", "Final", "Return", "MaxDD", "Trades", "WinRate");
    printf("------------------------------------------------------------------------\n");

    double pullbacks[] = {0.25, 0.5, 1.0, 1.5, 2.0, 3.0};
    for (double pb : pullbacks) {
        Config cfg;
        cfg.sma_period = 200;
        cfg.pullback_pct = pb;
        Result r = RunStrategy(ticks, cfg);
        double wr = r.total_trades > 0 ? r.winning_trades * 100.0 / r.total_trades : 0;
        printf("%-10.2f $%11.2f %7.1fx %9.1f%% %8d %7.1f%% %s\n",
               pb, r.final_balance, r.return_multiple, r.max_dd, r.total_trades, wr,
               r.margin_call ? "MC" : "");
    }

    // Test TP/SL ratio
    printf("\nTEST 3: TP/SL RATIO SWEEP (SMA=200)\n");
    printf("%-10s %12s %8s %10s %8s %8s\n", "TP:SL", "Final", "Return", "MaxDD", "Trades", "WinRate");
    printf("------------------------------------------------------------------------\n");

    double tp_sl[][2] = {{1.0, 1.0}, {1.5, 1.0}, {2.0, 1.0}, {2.0, 2.0}, {3.0, 1.5}, {3.0, 2.0}};
    for (auto& ts : tp_sl) {
        Config cfg;
        cfg.sma_period = 200;
        cfg.tp_pct = ts[0];
        cfg.sl_pct = ts[1];
        Result r = RunStrategy(ticks, cfg);
        double wr = r.total_trades > 0 ? r.winning_trades * 100.0 / r.total_trades : 0;
        printf("%4.1f:%-5.1f $%11.2f %7.1fx %9.1f%% %8d %7.1f%% %s\n",
               ts[0], ts[1], r.final_balance, r.return_multiple, r.max_dd, r.total_trades, wr,
               r.margin_call ? "MC" : "");
    }

    // Test lot sizes
    printf("\nTEST 4: LOT SIZE SWEEP (SMA=200)\n");
    printf("%-10s %12s %8s %10s %8s\n", "Lot", "Final", "Return", "MaxDD", "Trades");
    printf("----------------------------------------------------------------\n");

    double lots[] = {0.05, 0.1, 0.2, 0.5, 1.0};
    for (double lot : lots) {
        Config cfg;
        cfg.sma_period = 200;
        cfg.lot_size = lot;
        Result r = RunStrategy(ticks, cfg);
        printf("%-10.2f $%11.2f %7.1fx %9.1f%% %8d %s\n",
               lot, r.final_balance, r.return_multiple, r.max_dd, r.total_trades,
               r.margin_call ? "MC" : "");
    }

    printf("\n================================================================\n");
    return 0;
}
