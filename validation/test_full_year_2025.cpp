/**
 * Full Year 2025 Backtest
 *
 * Tests all strategy versions on XAUUSD from 2025.01.01 to 2025.12.29
 *
 * Strategies tested:
 * - V7 (ATR ON) - Original baseline
 * - V7 (ATR OFF) - Test ATR impact
 * - V10 (ATR ON) - Mean reversion filter
 * - V10 (ATR OFF) - Best performer in short tests
 * - V12 Optimized - All improvements, lowest risk
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cfloat>
#include <vector>
#include <deque>
#include <map>
#include <algorithm>

// ============================================================================
// Data Structures
// ============================================================================

struct Tick {
    char timestamp[32];
    double bid;
    double ask;
    int hour;
    int month;
    int day;
    int year;
    double spread() const { return ask - bid; }
};

struct Trade {
    size_t id;
    double entry_price;
    double lot_size;
    double take_profit;
    size_t entry_tick;
};

struct MonthlyStats {
    double start_balance;
    double end_balance;
    double max_dd;
    int trades;
    double start_price;
    double end_price;
};

// ============================================================================
// Indicators
// ============================================================================

class ATR {
    std::deque<double> ranges;
    int period;
    double sum = 0;
    double last_price = 0;
public:
    ATR(int p) : period(p) {}
    void Reset() { ranges.clear(); sum = 0; last_price = 0; }

    void Add(double price) {
        if (last_price > 0) {
            double range = fabs(price - last_price);
            ranges.push_back(range);
            sum += range;
            if ((int)ranges.size() > period) {
                sum -= ranges.front();
                ranges.pop_front();
            }
        }
        last_price = price;
    }

    double Get() const { return ranges.empty() ? 0 : sum / ranges.size(); }
    bool IsReady() const { return (int)ranges.size() >= period; }
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

// ============================================================================
// Strategy Configuration
// ============================================================================

enum StrategyVersion {
    V7_ATR_ON,
    V7_ATR_OFF,
    V10_ATR_ON,
    V10_ATR_OFF,
    V12_OPTIMIZED
};

const char* StrategyName(StrategyVersion v) {
    switch (v) {
        case V7_ATR_ON:      return "V7 (ATR ON)";
        case V7_ATR_OFF:     return "V7 (ATR OFF)";
        case V10_ATR_ON:     return "V10 (ATR ON)";
        case V10_ATR_OFF:    return "V10 (ATR OFF)";
        case V12_OPTIMIZED:  return "V12 Optimized";
    }
    return "Unknown";
}

struct Config {
    StrategyVersion version;
    double spacing = 0.75;
    double lot_size = 0.01;
    double contract_size = 100.0;
    int max_positions = 15;

    double stop_new_at_dd = 3.0;
    double partial_close_at_dd = 5.0;
    double close_all_at_dd = 15.0;

    bool enable_atr_filter = true;
    int atr_short_period = 50;
    int atr_long_period = 1000;
    double volatility_threshold = 0.6;

    bool enable_mean_reversion = false;
    int sma_period = 500;
    double mr_threshold = -0.04;

    bool enable_time_exit = false;
    int max_hold_ticks = 50000;

    bool enable_session_filter = true;
    bool avoid_hour_4 = false;
    bool avoid_hour_9 = false;
    bool avoid_hour_17 = false;
    int session_avoid_start = 14;
    int session_avoid_end = 18;

    double tp_multiplier = 2.0;
};

// ============================================================================
// Results
// ============================================================================

struct Result {
    StrategyVersion version;
    double return_pct;
    double max_dd;
    double final_balance;
    int total_trades;
    int time_exits;
    int protection_closes;
    MonthlyStats monthly[12];
};

// ============================================================================
// Parse timestamp
// ============================================================================

void ParseTimestamp(const char* ts, int& year, int& month, int& day, int& hour) {
    // Format: 2025.01.02 01:00:02.600
    year = 2025; month = 1; day = 1; hour = 12;

    if (strlen(ts) >= 10) {
        char y[5] = {ts[0], ts[1], ts[2], ts[3], 0};
        char m[3] = {ts[5], ts[6], 0};
        char d[3] = {ts[8], ts[9], 0};
        year = atoi(y);
        month = atoi(m);
        day = atoi(d);
    }
    if (strlen(ts) >= 13) {
        char h[3] = {ts[11], ts[12], 0};
        hour = atoi(h);
    }
}

// ============================================================================
// Load Ticks with date filtering
// ============================================================================

std::vector<Tick> LoadTicks(const char* filename, int start_year, int start_month, int start_day,
                            int end_year, int end_month, int end_day) {
    std::vector<Tick> ticks;
    ticks.reserve(60000000);  // Reserve for ~60M ticks

    FILE* f = fopen(filename, "r");
    if (!f) {
        printf("ERROR: Cannot open file: %s\n", filename);
        return ticks;
    }

    char line[256];
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return ticks;
    }

    size_t total_read = 0;
    size_t filtered_in = 0;

    while (fgets(line, sizeof(line), f)) {
        total_read++;

        Tick tick;
        memset(&tick, 0, sizeof(tick));

        char* token = strtok(line, "\t");
        if (!token) continue;
        strncpy(tick.timestamp, token, sizeof(tick.timestamp) - 1);

        ParseTimestamp(tick.timestamp, tick.year, tick.month, tick.day, tick.hour);

        // Filter by date range
        // Start date check
        if (tick.year < start_year) continue;
        if (tick.year == start_year && tick.month < start_month) continue;
        if (tick.year == start_year && tick.month == start_month && tick.day < start_day) continue;

        // End date check
        if (tick.year > end_year) break;  // Past end date, stop reading
        if (tick.year == end_year && tick.month > end_month) break;
        if (tick.year == end_year && tick.month == end_month && tick.day > end_day) break;

        token = strtok(NULL, "\t");
        if (!token) continue;
        tick.bid = atof(token);

        token = strtok(NULL, "\t");
        if (!token) continue;
        tick.ask = atof(token);

        if (tick.bid > 0 && tick.ask > 0) {
            ticks.push_back(tick);
            filtered_in++;
        }

        if (total_read % 5000000 == 0) {
            printf("  Read %zu ticks, filtered in %zu...\n", total_read, filtered_in);
        }
    }

    fclose(f);
    printf("  Total read: %zu, Filtered in: %zu\n", total_read, filtered_in);
    return ticks;
}

// ============================================================================
// Run Backtest
// ============================================================================

Result RunBacktest(const std::vector<Tick>& ticks, Config cfg) {
    Result r;
    memset(&r, 0, sizeof(r));
    r.version = cfg.version;

    if (ticks.empty()) return r;

    double balance = 10000.0;
    double equity = balance;
    double peak_equity = balance;
    double max_drawdown = 0;

    std::vector<Trade*> positions;
    std::map<size_t, size_t> entry_ticks;
    size_t next_id = 1;

    bool partial_done = false;
    bool all_closed = false;

    ATR atr_short(cfg.atr_short_period);
    ATR atr_long(cfg.atr_long_period);
    SMA sma(cfg.sma_period);

    double lowest = DBL_MAX, highest = -DBL_MAX;

    // Monthly tracking
    int current_month = ticks[0].month;
    r.monthly[current_month - 1].start_balance = balance;
    r.monthly[current_month - 1].start_price = ticks[0].bid;

    for (size_t tick_idx = 0; tick_idx < ticks.size(); tick_idx++) {
        const Tick& tick = ticks[tick_idx];

        // Month change tracking
        if (tick.month != current_month) {
            r.monthly[current_month - 1].end_balance = balance;
            r.monthly[current_month - 1].end_price = tick.bid;
            current_month = tick.month;
            r.monthly[current_month - 1].start_balance = balance;
            r.monthly[current_month - 1].start_price = tick.bid;
        }

        atr_short.Add(tick.bid);
        atr_long.Add(tick.bid);
        sma.Add(tick.bid);

        // Calculate equity
        equity = balance;
        for (Trade* t : positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * cfg.contract_size;
        }

        // Reset peak when no positions
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
        if (dd_pct > max_drawdown) max_drawdown = dd_pct;
        if (dd_pct > r.monthly[current_month - 1].max_dd) {
            r.monthly[current_month - 1].max_dd = dd_pct;
        }

        // Protection: Close ALL
        if (dd_pct > cfg.close_all_at_dd && !all_closed && !positions.empty()) {
            for (Trade* t : positions) {
                double pl = (tick.bid - t->entry_price) * t->lot_size * cfg.contract_size;
                balance += pl;
                r.protection_closes++;
                delete t;
            }
            positions.clear();
            entry_ticks.clear();
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
                r.protection_closes++;
                entry_ticks.erase(t->id);
                delete t;
                positions.erase(positions.begin());
            }
            partial_done = true;
        }

        // V12: Time-based exit
        if (cfg.enable_time_exit) {
            std::vector<Trade*> to_close;
            for (Trade* t : positions) {
                auto it = entry_ticks.find(t->id);
                if (it != entry_ticks.end()) {
                    size_t held = tick_idx - it->second;
                    if (held > (size_t)cfg.max_hold_ticks) {
                        double pl = (tick.bid - t->entry_price) * t->lot_size * cfg.contract_size;
                        if (pl < -0.5) {
                            to_close.push_back(t);
                        }
                    }
                }
            }
            for (Trade* t : to_close) {
                double pl = (tick.bid - t->entry_price) * t->lot_size * cfg.contract_size;
                balance += pl;
                r.time_exits++;
                entry_ticks.erase(t->id);
                positions.erase(std::remove(positions.begin(), positions.end(), t), positions.end());
                delete t;
            }
        }

        // Check TPs
        for (auto it = positions.begin(); it != positions.end();) {
            Trade* t = *it;
            if (tick.bid >= t->take_profit) {
                double pl = (tick.bid - t->entry_price) * t->lot_size * cfg.contract_size;
                balance += pl;
                r.total_trades++;
                r.monthly[current_month - 1].trades++;
                entry_ticks.erase(t->id);
                delete t;
                it = positions.erase(it);
            } else {
                ++it;
            }
        }

        // ATR filter
        bool atr_ok = true;
        if (cfg.enable_atr_filter && atr_short.IsReady() && atr_long.IsReady() && atr_long.Get() > 0) {
            atr_ok = atr_short.Get() < atr_long.Get() * cfg.volatility_threshold;
        }

        // Mean reversion filter
        bool mr_ok = true;
        if (cfg.enable_mean_reversion && sma.IsReady() && sma.Get() > 0) {
            double deviation = (tick.bid - sma.Get()) / sma.Get() * 100.0;
            mr_ok = deviation < cfg.mr_threshold;
        }

        // Session filter
        bool session_ok = true;
        if (cfg.enable_session_filter) {
            if (cfg.avoid_hour_4 && tick.hour == 4) session_ok = false;
            if (cfg.avoid_hour_9 && tick.hour == 9) session_ok = false;
            if (cfg.avoid_hour_17 && tick.hour == 17) session_ok = false;
            if (tick.hour >= cfg.session_avoid_start && tick.hour < cfg.session_avoid_end) {
                session_ok = false;
            }
        }

        if (dd_pct >= cfg.stop_new_at_dd || !atr_ok || !mr_ok || !session_ok) {
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
                Trade* t = new Trade();
                t->id = next_id++;
                t->entry_price = tick.ask;
                t->lot_size = cfg.lot_size;
                t->take_profit = tick.ask + tick.spread() + cfg.spacing * cfg.tp_multiplier;
                t->entry_tick = tick_idx;
                positions.push_back(t);
                entry_ticks[t->id] = tick_idx;
            }
        }
    }

    // Final month stats
    r.monthly[current_month - 1].end_balance = balance;
    r.monthly[current_month - 1].end_price = ticks.back().bid;

    // Close remaining
    for (Trade* t : positions) {
        double pl = (ticks.back().bid - t->entry_price) * t->lot_size * cfg.contract_size;
        balance += pl;
        r.total_trades++;
        delete t;
    }

    r.return_pct = (balance - 10000.0) / 10000.0 * 100.0;
    r.max_dd = max_drawdown;
    r.final_balance = balance;

    return r;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("================================================================\n");
    printf("FULL YEAR 2025 BACKTEST - XAUUSD\n");
    printf("================================================================\n\n");

    printf("Date Range: 2025.01.01 - 2025.12.29\n");
    printf("Strategies: V7, V10, V12 (with/without ATR filter)\n\n");

    // Load data
    const char* filename = "Grid/XAUUSD_TICKS_2025.csv";
    printf("Loading ticks from %s...\n", filename);
    printf("Filtering to 2025.01.01 - 2025.12.29...\n\n");

    std::vector<Tick> ticks = LoadTicks(filename, 2025, 1, 1, 2025, 12, 29);

    if (ticks.empty()) {
        printf("ERROR: Failed to load tick data!\n");
        return 1;
    }

    printf("\nLoaded %zu ticks\n", ticks.size());
    printf("Start: %s (%.2f)\n", ticks.front().timestamp, ticks.front().bid);
    printf("End:   %s (%.2f)\n", ticks.back().timestamp, ticks.back().bid);

    double price_change = (ticks.back().bid - ticks.front().bid) / ticks.front().bid * 100.0;
    printf("Gold price change: %+.2f%%\n\n", price_change);

    // Configure strategies
    std::vector<Config> configs;

    // V7 with ATR
    {
        Config c;
        c.version = V7_ATR_ON;
        c.enable_atr_filter = true;
        c.enable_mean_reversion = false;
        c.enable_time_exit = false;
        configs.push_back(c);
    }

    // V7 without ATR
    {
        Config c;
        c.version = V7_ATR_OFF;
        c.enable_atr_filter = false;
        c.enable_mean_reversion = false;
        c.enable_time_exit = false;
        configs.push_back(c);
    }

    // V10 with ATR
    {
        Config c;
        c.version = V10_ATR_ON;
        c.enable_atr_filter = true;
        c.enable_mean_reversion = true;
        c.enable_time_exit = false;
        configs.push_back(c);
    }

    // V10 without ATR (best in short test)
    {
        Config c;
        c.version = V10_ATR_OFF;
        c.enable_atr_filter = false;
        c.enable_mean_reversion = true;
        c.enable_time_exit = false;
        configs.push_back(c);
    }

    // V12 Optimized
    {
        Config c;
        c.version = V12_OPTIMIZED;
        c.enable_atr_filter = false;
        c.enable_mean_reversion = true;
        c.enable_time_exit = true;
        c.avoid_hour_4 = true;
        c.avoid_hour_9 = true;
        c.avoid_hour_17 = true;
        configs.push_back(c);
    }

    // Run tests
    printf("Running backtests on %zu ticks...\n\n", ticks.size());
    std::vector<Result> results;

    for (Config& cfg : configs) {
        printf("  Testing %s...", StrategyName(cfg.version));
        fflush(stdout);
        Result r = RunBacktest(ticks, cfg);
        results.push_back(r);
        printf(" Done (%.2f%% return)\n", r.return_pct);
    }

    // Sort by return
    std::vector<Result> sorted = results;
    std::sort(sorted.begin(), sorted.end(), [](const Result& a, const Result& b) {
        return a.return_pct > b.return_pct;
    });

    // Results table
    printf("\n================================================================\n");
    printf("ANNUAL RESULTS (sorted by return)\n");
    printf("================================================================\n\n");

    printf("%-18s %10s %10s %8s %8s %10s\n",
           "Strategy", "Return", "Final Bal", "MaxDD", "Trades", "Prot/Time");
    printf("------------------------------------------------------------------------\n");

    for (const Result& r : sorted) {
        printf("%-18s %+9.2f%% $%9.2f %7.2f%% %8d %5d/%d\n",
               StrategyName(r.version),
               r.return_pct,
               r.final_balance,
               r.max_dd,
               r.total_trades,
               r.protection_closes,
               r.time_exits);
    }

    printf("------------------------------------------------------------------------\n");
    printf("Initial Balance: $10,000.00\n");
    printf("Gold Price Change: %+.2f%%\n\n", price_change);

    // Monthly breakdown for best performer
    printf("================================================================\n");
    printf("MONTHLY BREAKDOWN - %s (Best)\n", StrategyName(sorted[0].version));
    printf("================================================================\n\n");

    const Result& best = sorted[0];
    printf("%-6s %12s %12s %10s %8s %10s\n",
           "Month", "Start Bal", "End Bal", "Return", "MaxDD", "Trades");
    printf("------------------------------------------------------------------------\n");

    const char* month_names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    for (int m = 0; m < 12; m++) {
        if (best.monthly[m].start_balance > 0 || best.monthly[m].end_balance > 0) {
            double monthly_return = 0;
            if (best.monthly[m].start_balance > 0) {
                monthly_return = (best.monthly[m].end_balance - best.monthly[m].start_balance)
                               / best.monthly[m].start_balance * 100.0;
            }
            printf("%-6s $%11.2f $%11.2f %+9.2f%% %7.2f%% %8d\n",
                   month_names[m],
                   best.monthly[m].start_balance,
                   best.monthly[m].end_balance,
                   monthly_return,
                   best.monthly[m].max_dd,
                   best.monthly[m].trades);
        }
    }

    printf("------------------------------------------------------------------------\n\n");

    // Risk-adjusted comparison
    printf("================================================================\n");
    printf("RISK-ADJUSTED METRICS\n");
    printf("================================================================\n\n");

    printf("%-18s %10s %8s %12s\n", "Strategy", "Return", "MaxDD", "Return/DD");
    printf("------------------------------------------------------------------------\n");

    for (const Result& r : sorted) {
        double ratio = (r.max_dd > 0) ? r.return_pct / r.max_dd : 0;
        printf("%-18s %+9.2f%% %7.2f%% %11.2f\n",
               StrategyName(r.version),
               r.return_pct,
               r.max_dd,
               ratio);
    }

    printf("------------------------------------------------------------------------\n\n");

    // Analysis
    printf("================================================================\n");
    printf("ANALYSIS\n");
    printf("================================================================\n\n");

    // Compare ATR impact
    Result v7_atr = results[0];
    Result v7_no_atr = results[1];
    Result v10_atr = results[2];
    Result v10_no_atr = results[3];
    Result v12 = results[4];

    printf("ATR Filter Impact (Full Year):\n\n");
    printf("  V7:  ATR ON: %+.2f%%, ATR OFF: %+.2f%%  -> Diff: %+.2f%%\n",
           v7_atr.return_pct, v7_no_atr.return_pct, v7_no_atr.return_pct - v7_atr.return_pct);
    printf("  V10: ATR ON: %+.2f%%, ATR OFF: %+.2f%%  -> Diff: %+.2f%%\n",
           v10_atr.return_pct, v10_no_atr.return_pct, v10_no_atr.return_pct - v10_atr.return_pct);
    printf("\n");

    // Conclusion
    printf("================================================================\n");
    printf("CONCLUSION\n");
    printf("================================================================\n\n");

    printf("BEST PERFORMER: %s\n", StrategyName(sorted[0].version));
    printf("  Annual Return: %+.2f%%\n", sorted[0].return_pct);
    printf("  Final Balance: $%.2f\n", sorted[0].final_balance);
    printf("  Max Drawdown:  %.2f%%\n", sorted[0].max_dd);
    printf("  Total Trades:  %d\n\n", sorted[0].total_trades);

    printf("LOWEST RISK: ");
    Result* lowest_dd = &results[0];
    for (Result& r : results) {
        if (r.max_dd < lowest_dd->max_dd) lowest_dd = &r;
    }
    printf("%s\n", StrategyName(lowest_dd->version));
    printf("  Max Drawdown:  %.2f%%\n", lowest_dd->max_dd);
    printf("  Annual Return: %+.2f%%\n\n", lowest_dd->return_pct);

    printf("Gold went %+.2f%% in 2025.\n", price_change);
    if (sorted[0].return_pct > price_change) {
        printf("Strategy OUTPERFORMED buy-and-hold by %.2f%%!\n",
               sorted[0].return_pct - price_change);
    } else {
        printf("Buy-and-hold outperformed by %.2f%%.\n",
               price_change - sorted[0].return_pct);
    }

    printf("\n================================================================\n");
    printf("TEST COMPLETE\n");
    printf("================================================================\n");

    return 0;
}
