/**
 * V11 Strategy Validation Test
 *
 * Tests the new V11 features:
 * 1. Bidirectional trading (separate long and short grids)
 * 2. Inverse volatility TP (wider in calm, tighter in volatile)
 *
 * Compares V11 against V10 (long-only) and V7 baseline.
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
    char timestamp[32];
    double bid;
    double ask;
    double spread() const { return ask - bid; }
};

struct Trade {
    size_t id;
    double entry_price;
    double lot_size;
    double take_profit;
    bool is_long;
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
// Strategy Configurations
// ============================================================================

enum StrategyVersion {
    V7_BASELINE,        // Long-only with volatility filter
    V10_MEAN_REV,       // + Mean reversion filter
    V11_BIDIRECTIONAL,  // + Bidirectional + Inverse vol TP
    V11_LONG_ONLY       // V11 features but long-only (for comparison)
};

const char* StrategyName(StrategyVersion v) {
    switch (v) {
        case V7_BASELINE:        return "V7 (Baseline)";
        case V10_MEAN_REV:       return "V10 (Mean Rev)";
        case V11_BIDIRECTIONAL:  return "V11 (Bidir+InvTP)";
        case V11_LONG_ONLY:      return "V11 (LongOnly+InvTP)";
    }
    return "Unknown";
}

struct Config {
    StrategyVersion version;

    // Core
    double spacing = 0.75;
    double lot_size = 0.01;
    double contract_size = 100.0;
    double leverage = 500.0;
    int max_positions = 15;

    // Protection
    double stop_new_at_dd = 3.0;
    double partial_close_at_dd = 5.0;
    double close_all_at_dd = 15.0;

    // ATR filter
    int atr_short_period = 50;
    int atr_long_period = 1000;
    double volatility_threshold = 0.6;

    // Mean reversion (V10+)
    bool enable_mean_reversion = false;
    int sma_period = 500;
    double mr_threshold_long = -0.04;
    double mr_threshold_short = 0.04;

    // Bidirectional (V11)
    bool enable_bidirectional = false;

    // Inverse vol TP (V11)
    bool enable_inverse_vol_tp = false;
    double tp_base = 2.0;
    double tp_scale = 2.0;
    double tp_min = 0.5;
    double tp_max = 4.0;
};

// ============================================================================
// Results
// ============================================================================

struct Result {
    StrategyVersion version;
    double return_pct;
    double max_dd;
    int total_trades;
    int long_trades;
    int short_trades;
    double profit_from_longs;
    double profit_from_shorts;
    double directional_dependency;
    double avg_tp_mult;
};

// ============================================================================
// Load Ticks
// ============================================================================

std::vector<Tick> LoadTicks(const char* filename, size_t max_count) {
    std::vector<Tick> ticks;
    ticks.reserve(max_count);

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

    while (fgets(line, sizeof(line), f) && ticks.size() < max_count) {
        Tick tick;
        memset(&tick, 0, sizeof(tick));

        char* token = strtok(line, "\t");
        if (!token) continue;
        strncpy(tick.timestamp, token, sizeof(tick.timestamp) - 1);

        token = strtok(NULL, "\t");
        if (!token) continue;
        tick.bid = atof(token);

        token = strtok(NULL, "\t");
        if (!token) continue;
        tick.ask = atof(token);

        if (tick.bid > 0 && tick.ask > 0) {
            ticks.push_back(tick);
        }

        if (ticks.size() % 100000 == 0) {
            printf("  Loaded %zu ticks...\n", ticks.size());
        }
    }

    fclose(f);
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

    std::vector<Trade*> long_positions;
    std::vector<Trade*> short_positions;
    size_t next_id = 1;

    bool partial_done = false;
    bool all_closed = false;

    // Indicators
    ATR atr_short(cfg.atr_short_period);
    ATR atr_long(cfg.atr_long_period);
    SMA sma(cfg.sma_period);

    // Grid tracking
    double lowest_long = DBL_MAX, highest_long = -DBL_MAX;
    double lowest_short = DBL_MAX, highest_short = -DBL_MAX;

    // Stats
    double total_tp_mult = 0;
    int tp_mult_count = 0;

    for (const Tick& tick : ticks) {
        atr_short.Add(tick.bid);
        atr_long.Add(tick.bid);
        sma.Add(tick.bid);

        // Calculate equity
        equity = balance;
        for (Trade* t : long_positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * cfg.contract_size;
        }
        for (Trade* t : short_positions) {
            equity += (t->entry_price - tick.ask) * t->lot_size * cfg.contract_size;
        }

        // Reset peak when no positions
        if (long_positions.empty() && short_positions.empty()) {
            if (peak_equity != balance) {
                peak_equity = balance;
                partial_done = false;
                all_closed = false;
            }
        }

        if (equity > peak_equity) {
            peak_equity = equity;
            partial_done = false;
            all_closed = false;
        }

        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        if (dd_pct > max_drawdown) max_drawdown = dd_pct;

        // Protection: Close ALL
        if (dd_pct > cfg.close_all_at_dd && !all_closed &&
            (!long_positions.empty() || !short_positions.empty())) {
            for (Trade* t : long_positions) {
                double pl = (tick.bid - t->entry_price) * t->lot_size * cfg.contract_size;
                balance += pl;
                r.profit_from_longs += pl;
                r.long_trades++;
                delete t;
            }
            long_positions.clear();

            for (Trade* t : short_positions) {
                double pl = (t->entry_price - tick.ask) * t->lot_size * cfg.contract_size;
                balance += pl;
                r.profit_from_shorts += pl;
                r.short_trades++;
                delete t;
            }
            short_positions.clear();

            all_closed = true;
            equity = balance;
            peak_equity = equity;
            lowest_long = DBL_MAX; highest_long = -DBL_MAX;
            lowest_short = DBL_MAX; highest_short = -DBL_MAX;
            continue;
        }

        // Protection: Partial close
        if (dd_pct > cfg.partial_close_at_dd && !partial_done &&
            (long_positions.size() > 1 || short_positions.size() > 1)) {

            // Close worst 50% of each side
            auto close_worst = [&](std::vector<Trade*>& positions, bool is_long) {
                if (positions.size() <= 1) return;
                std::sort(positions.begin(), positions.end(), [&](Trade* a, Trade* b) {
                    if (is_long) {
                        return (tick.bid - a->entry_price) < (tick.bid - b->entry_price);
                    } else {
                        return (a->entry_price - tick.ask) < (b->entry_price - tick.ask);
                    }
                });
                int to_close = std::max(1, (int)(positions.size() * 0.5));
                for (int i = 0; i < to_close && !positions.empty(); i++) {
                    Trade* t = positions[0];
                    double pl;
                    if (is_long) {
                        pl = (tick.bid - t->entry_price) * t->lot_size * cfg.contract_size;
                        r.profit_from_longs += pl;
                        r.long_trades++;
                    } else {
                        pl = (t->entry_price - tick.ask) * t->lot_size * cfg.contract_size;
                        r.profit_from_shorts += pl;
                        r.short_trades++;
                    }
                    balance += pl;
                    delete t;
                    positions.erase(positions.begin());
                }
            };

            close_worst(long_positions, true);
            close_worst(short_positions, false);
            partial_done = true;
        }

        // Check TPs - Long
        for (auto it = long_positions.begin(); it != long_positions.end();) {
            Trade* t = *it;
            if (tick.bid >= t->take_profit) {
                double pl = (tick.bid - t->entry_price) * t->lot_size * cfg.contract_size;
                balance += pl;
                r.profit_from_longs += pl;
                r.long_trades++;
                delete t;
                it = long_positions.erase(it);
            } else {
                ++it;
            }
        }

        // Check TPs - Short
        for (auto it = short_positions.begin(); it != short_positions.end();) {
            Trade* t = *it;
            if (tick.ask <= t->take_profit) {
                double pl = (t->entry_price - tick.ask) * t->lot_size * cfg.contract_size;
                balance += pl;
                r.profit_from_shorts += pl;
                r.short_trades++;
                delete t;
                it = short_positions.erase(it);
            } else {
                ++it;
            }
        }

        // Volatility filter
        bool volatility_ok = true;
        if (atr_short.IsReady() && atr_long.IsReady() && atr_long.Get() > 0) {
            volatility_ok = atr_short.Get() < atr_long.Get() * cfg.volatility_threshold;
        }

        if (dd_pct >= cfg.stop_new_at_dd || !volatility_ok) {
            continue;
        }

        // Mean reversion check
        double deviation = 0;
        if (sma.IsReady() && sma.Get() > 0) {
            deviation = (tick.bid - sma.Get()) / sma.Get() * 100.0;
        }

        bool can_long = !cfg.enable_mean_reversion || !sma.IsReady() ||
                       deviation < cfg.mr_threshold_long;
        bool can_short = !cfg.enable_mean_reversion || !sma.IsReady() ||
                        deviation > cfg.mr_threshold_short;

        // Calculate TP multiplier
        double tp_mult = cfg.tp_base;
        if (cfg.enable_inverse_vol_tp && atr_short.IsReady() && atr_long.IsReady() && atr_long.Get() > 0) {
            double vol_ratio = atr_short.Get() / atr_long.Get();
            tp_mult = cfg.tp_base + cfg.tp_scale * (1.0 - vol_ratio);
            tp_mult = std::max(cfg.tp_min, std::min(cfg.tp_max, tp_mult));
            total_tp_mult += tp_mult;
            tp_mult_count++;
        }

        // Update grid bounds
        lowest_long = DBL_MAX; highest_long = -DBL_MAX;
        for (Trade* t : long_positions) {
            lowest_long = std::min(lowest_long, t->entry_price);
            highest_long = std::max(highest_long, t->entry_price);
        }
        lowest_short = DBL_MAX; highest_short = -DBL_MAX;
        for (Trade* t : short_positions) {
            lowest_short = std::min(lowest_short, t->entry_price);
            highest_short = std::max(highest_short, t->entry_price);
        }

        // Open long positions
        if (can_long && (int)long_positions.size() < cfg.max_positions) {
            bool should_open = long_positions.empty() ||
                              (lowest_long >= tick.ask + cfg.spacing) ||
                              (highest_long <= tick.ask - cfg.spacing);

            if (should_open) {
                Trade* t = new Trade();
                t->id = next_id++;
                t->entry_price = tick.ask;
                t->lot_size = cfg.lot_size;
                t->take_profit = tick.ask + tick.spread() + cfg.spacing * tp_mult;
                t->is_long = true;
                long_positions.push_back(t);
            }
        }

        // Open short positions (if bidirectional enabled)
        if (cfg.enable_bidirectional && can_short && (int)short_positions.size() < cfg.max_positions) {
            bool should_open = short_positions.empty() ||
                              (highest_short <= tick.bid - cfg.spacing) ||
                              (lowest_short >= tick.bid + cfg.spacing);

            if (should_open) {
                Trade* t = new Trade();
                t->id = next_id++;
                t->entry_price = tick.bid;
                t->lot_size = cfg.lot_size;
                t->take_profit = tick.bid - tick.spread() - cfg.spacing * tp_mult;
                t->is_long = false;
                short_positions.push_back(t);
            }
        }
    }

    // Close remaining positions
    for (Trade* t : long_positions) {
        double pl = (ticks.back().bid - t->entry_price) * t->lot_size * cfg.contract_size;
        balance += pl;
        r.profit_from_longs += pl;
        r.long_trades++;
        delete t;
    }
    for (Trade* t : short_positions) {
        double pl = (t->entry_price - ticks.back().ask) * t->lot_size * cfg.contract_size;
        balance += pl;
        r.profit_from_shorts += pl;
        r.short_trades++;
        delete t;
    }

    // Calculate results
    r.return_pct = (balance - 10000.0) / 10000.0 * 100.0;
    r.max_dd = max_drawdown;
    r.total_trades = r.long_trades + r.short_trades;
    r.avg_tp_mult = (tp_mult_count > 0) ? total_tp_mult / tp_mult_count : cfg.tp_base;

    // Directional dependency
    double total_profit = fabs(r.profit_from_longs) + fabs(r.profit_from_shorts);
    if (total_profit > 0) {
        double long_share = fabs(r.profit_from_longs) / total_profit;
        double short_share = fabs(r.profit_from_shorts) / total_profit;
        r.directional_dependency = fabs(long_share - short_share);
    } else {
        r.directional_dependency = 1.0;
    }

    return r;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("================================================================\n");
    printf("V11 STRATEGY VALIDATION TEST\n");
    printf("================================================================\n\n");

    printf("V11 New Features:\n");
    printf("1. Bidirectional Trading: Separate long AND short grids\n");
    printf("2. Inverse Volatility TP: Wider in calm, tighter in volatile\n\n");

    // Load data
    const char* filename = "Grid/XAUUSD_TICKS_2025.csv";
    printf("Loading ticks from %s...\n", filename);

    std::vector<Tick> ticks = LoadTicks(filename, 1000000);  // 1M ticks for thorough test

    if (ticks.empty()) {
        printf("ERROR: Failed to load tick data!\n");
        return 1;
    }

    printf("Loaded %zu ticks\n", ticks.size());
    double start_price = ticks.front().bid;
    double end_price = ticks.back().bid;
    double price_change = (end_price - start_price) / start_price * 100.0;
    printf("Price: %.2f -> %.2f (%+.2f%%)\n\n", start_price, end_price, price_change);

    // Configure strategies
    std::vector<Config> configs;

    // V7 Baseline
    {
        Config c;
        c.version = V7_BASELINE;
        c.enable_mean_reversion = false;
        c.enable_bidirectional = false;
        c.enable_inverse_vol_tp = false;
        configs.push_back(c);
    }

    // V10 Mean Reversion
    {
        Config c;
        c.version = V10_MEAN_REV;
        c.enable_mean_reversion = true;
        c.enable_bidirectional = false;
        c.enable_inverse_vol_tp = false;
        configs.push_back(c);
    }

    // V11 Long-Only + Inverse TP
    {
        Config c;
        c.version = V11_LONG_ONLY;
        c.enable_mean_reversion = true;
        c.enable_bidirectional = false;
        c.enable_inverse_vol_tp = true;
        configs.push_back(c);
    }

    // V11 Full (Bidirectional + Inverse TP)
    {
        Config c;
        c.version = V11_BIDIRECTIONAL;
        c.enable_mean_reversion = true;
        c.enable_bidirectional = true;
        c.enable_inverse_vol_tp = true;
        configs.push_back(c);
    }

    // Run tests
    printf("Running backtests...\n\n");
    std::vector<Result> results;

    for (Config& cfg : configs) {
        printf("  Testing %s...\n", StrategyName(cfg.version));
        results.push_back(RunBacktest(ticks, cfg));
    }

    // Results table
    printf("\n================================================================\n");
    printf("RESULTS COMPARISON\n");
    printf("================================================================\n\n");

    printf("%-22s %8s %7s %8s %8s %8s %7s\n",
           "Strategy", "Return", "MaxDD", "Trades", "L/S", "DirDep", "AvgTP");
    printf("--------------------------------------------------------------------------------\n");

    for (const Result& r : results) {
        char ls_str[16];
        if (r.short_trades > 0) {
            snprintf(ls_str, sizeof(ls_str), "%d/%d", r.long_trades, r.short_trades);
        } else {
            snprintf(ls_str, sizeof(ls_str), "%d/0", r.long_trades);
        }

        printf("%-22s %+7.2f%% %6.2f%% %8d %8s %6.2f %7.2f\n",
               StrategyName(r.version),
               r.return_pct,
               r.max_dd,
               r.total_trades,
               ls_str,
               r.directional_dependency,
               r.avg_tp_mult);
    }

    printf("--------------------------------------------------------------------------------\n\n");

    // Detailed breakdown
    printf("================================================================\n");
    printf("PROFIT BREAKDOWN\n");
    printf("================================================================\n\n");

    printf("%-22s %12s %12s %12s\n", "Strategy", "Long P/L", "Short P/L", "Total P/L");
    printf("--------------------------------------------------------------------------------\n");

    for (const Result& r : results) {
        double total_pl = r.profit_from_longs + r.profit_from_shorts;
        printf("%-22s $%+10.2f $%+10.2f $%+10.2f\n",
               StrategyName(r.version),
               r.profit_from_longs,
               r.profit_from_shorts,
               total_pl);
    }

    printf("--------------------------------------------------------------------------------\n\n");

    // Analysis
    printf("================================================================\n");
    printf("ANALYSIS\n");
    printf("================================================================\n\n");

    // Compare V11 vs V10
    Result v10 = results[1];
    Result v11 = results[3];

    printf("V11 vs V10 Comparison:\n\n");
    printf("  Return:     %+.2f%% vs %+.2f%% ", v11.return_pct, v10.return_pct);
    if (v11.return_pct > v10.return_pct) {
        printf("(V11 is %.1f%% better)\n", (v11.return_pct / v10.return_pct - 1) * 100);
    } else {
        printf("(V10 is %.1f%% better)\n", (v10.return_pct / v11.return_pct - 1) * 100);
    }

    printf("  Max DD:     %.2f%% vs %.2f%% ", v11.max_dd, v10.max_dd);
    if (v11.max_dd < v10.max_dd) {
        printf("(V11 has %.1f%% lower DD)\n", (1 - v11.max_dd / v10.max_dd) * 100);
    } else {
        printf("(V10 has %.1f%% lower DD)\n", (1 - v10.max_dd / v11.max_dd) * 100);
    }

    printf("  Dir Dep:    %.2f vs %.2f ", v11.directional_dependency, v10.directional_dependency);
    if (v11.directional_dependency < v10.directional_dependency) {
        printf("(V11 is %.0f%% more market-neutral)\n",
               (1 - v11.directional_dependency / v10.directional_dependency) * 100);
    } else {
        printf("(V10 is more market-neutral)\n");
    }

    printf("\nMarket Neutrality Interpretation:\n");
    printf("  Dir Dep = 0.0: Perfectly balanced long/short profits\n");
    printf("  Dir Dep = 1.0: All profit from one direction\n");
    printf("  V11 Dep = %.2f: ", v11.directional_dependency);
    if (v11.directional_dependency < 0.3) {
        printf("MARKET NEUTRAL\n");
    } else if (v11.directional_dependency < 0.6) {
        printf("MODERATELY BALANCED\n");
    } else {
        printf("DIRECTIONALLY DEPENDENT\n");
    }

    // Conclusion
    printf("\n================================================================\n");
    printf("CONCLUSION\n");
    printf("================================================================\n\n");

    bool v11_better_return = v11.return_pct > v10.return_pct;
    bool v11_better_dd = v11.max_dd < v10.max_dd;
    bool v11_more_neutral = v11.directional_dependency < v10.directional_dependency;

    if (v11_better_return && v11_more_neutral) {
        printf("V11 WINS: Higher returns AND more market-neutral!\n\n");
        printf("Key advantages:\n");
        printf("- Bidirectional trading profits in both up and down markets\n");
        printf("- Inverse volatility TP optimizes exits based on conditions\n");
        printf("- Reduced directional dependency = more consistent performance\n");
    } else if (v11_better_return) {
        printf("V11 has higher returns but less market-neutral.\n");
        printf("Trade-off: More profit but more directional risk.\n");
    } else if (v11_more_neutral) {
        printf("V11 is more market-neutral but with lower returns.\n");
        printf("Trade-off: More consistent but less profitable.\n");
    } else {
        printf("V10 outperforms V11 in this test period.\n");
        printf("Consider: Market conditions may favor different strategies.\n");
    }

    if (v11_better_dd) {
        printf("\nV11 also has lower maximum drawdown (%.2f%% vs %.2f%%).\n",
               v11.max_dd, v10.max_dd);
    }

    printf("\n================================================================\n");
    printf("TEST COMPLETE\n");
    printf("================================================================\n");

    return 0;
}
