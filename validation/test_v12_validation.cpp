/**
 * V12 Strategy Validation Test
 *
 * V12 incorporates findings from deep analysis:
 * 1. ATR filter DISABLED (may hurt performance - 45.9% accuracy)
 * 2. Time-based exit for stuck positions (losers held 5.3x longer)
 * 3. Enhanced session filter (avoid hours 4, 9, 17)
 * 4. Keep mean reversion filter (proven 2.7x improvement)
 *
 * Compares: V7 (baseline), V10 (mean rev), V12 (optimized)
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
    int hour;  // Parsed hour
    double spread() const { return ask - bid; }
};

struct Trade {
    size_t id;
    double entry_price;
    double lot_size;
    double take_profit;
    size_t entry_tick;
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
// Strategy Versions
// ============================================================================

enum StrategyVersion {
    V7_BASELINE,           // ATR filter ON, no mean reversion
    V7_NO_ATR,             // ATR filter OFF (test if it hurts)
    V10_MEAN_REV,          // ATR filter ON + mean reversion
    V10_NO_ATR,            // ATR filter OFF + mean reversion
    V12_OPTIMIZED,         // No ATR + mean rev + time exit + enhanced session
    V12_WITH_ATR           // Same as V12 but ATR filter ON (for comparison)
};

const char* StrategyName(StrategyVersion v) {
    switch (v) {
        case V7_BASELINE:    return "V7 (ATR ON)";
        case V7_NO_ATR:      return "V7 (ATR OFF)";
        case V10_MEAN_REV:   return "V10 (ATR ON)";
        case V10_NO_ATR:     return "V10 (ATR OFF)";
        case V12_OPTIMIZED:  return "V12 Optimized";
        case V12_WITH_ATR:   return "V12 + ATR";
    }
    return "Unknown";
}

struct Config {
    StrategyVersion version;

    // Core
    double spacing = 0.75;
    double lot_size = 0.01;
    double contract_size = 100.0;
    int max_positions = 15;

    // Protection
    double stop_new_at_dd = 3.0;
    double partial_close_at_dd = 5.0;
    double close_all_at_dd = 15.0;

    // ATR filter
    bool enable_atr_filter = true;
    int atr_short_period = 50;
    int atr_long_period = 1000;
    double volatility_threshold = 0.6;

    // Mean reversion
    bool enable_mean_reversion = false;
    int sma_period = 500;
    double mr_threshold = -0.04;

    // Time-based exit (V12)
    bool enable_time_exit = false;
    int max_hold_ticks = 50000;

    // Enhanced session filter (V12)
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
    int total_trades;
    int time_exits;
    int protection_closes;
    double profit;
};

// ============================================================================
// Load Ticks
// ============================================================================

int ParseHour(const char* timestamp) {
    // Find space and parse hour
    const char* space = strchr(timestamp, ' ');
    if (space && strlen(space) > 2) {
        char hour_str[3] = {space[1], space[2], 0};
        return atoi(hour_str);
    }
    return 12;  // Default
}

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
        tick.hour = ParseHour(tick.timestamp);

        token = strtok(NULL, "\t");
        if (!token) continue;
        tick.bid = atof(token);

        token = strtok(NULL, "\t");
        if (!token) continue;
        tick.ask = atof(token);

        if (tick.bid > 0 && tick.ask > 0) {
            ticks.push_back(tick);
        }

        if (ticks.size() % 500000 == 0) {
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

    std::vector<Trade*> positions;
    std::map<size_t, size_t> entry_ticks;  // trade id -> entry tick
    size_t next_id = 1;

    bool partial_done = false;
    bool all_closed = false;

    // Indicators
    ATR atr_short(cfg.atr_short_period);
    ATR atr_long(cfg.atr_long_period);
    SMA sma(cfg.sma_period);

    // Grid tracking
    double lowest = DBL_MAX, highest = -DBL_MAX;

    for (size_t tick_idx = 0; tick_idx < ticks.size(); tick_idx++) {
        const Tick& tick = ticks[tick_idx];

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
                        if (pl < -0.5) {  // Only exit if in loss
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

        // Don't open if filters fail or DD too high
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

    // Close remaining
    for (Trade* t : positions) {
        double pl = (ticks.back().bid - t->entry_price) * t->lot_size * cfg.contract_size;
        balance += pl;
        r.total_trades++;
        delete t;
    }

    r.return_pct = (balance - 10000.0) / 10000.0 * 100.0;
    r.max_dd = max_drawdown;
    r.profit = balance - 10000.0;

    return r;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("================================================================\n");
    printf("V12 STRATEGY VALIDATION TEST\n");
    printf("================================================================\n\n");

    printf("V12 Changes Based on Analysis:\n");
    printf("1. ATR filter DISABLED (45.9%% accuracy - worse than random)\n");
    printf("2. Time-based exit for stuck positions (50K tick limit)\n");
    printf("3. Enhanced session filter (avoid hours 4, 9, 17)\n");
    printf("4. Mean reversion filter KEPT (proven 2.7x improvement)\n\n");

    // Load data - use more ticks for reliable comparison
    const char* filename = "Grid/XAUUSD_TICKS_2025.csv";
    printf("Loading ticks from %s...\n", filename);

    std::vector<Tick> ticks = LoadTicks(filename, 2000000);  // 2M ticks

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

    // V7 with ATR (baseline)
    {
        Config c;
        c.version = V7_BASELINE;
        c.enable_atr_filter = true;
        c.enable_mean_reversion = false;
        c.enable_time_exit = false;
        c.avoid_hour_4 = false;
        c.avoid_hour_9 = false;
        c.avoid_hour_17 = false;
        configs.push_back(c);
    }

    // V7 without ATR (test hypothesis)
    {
        Config c;
        c.version = V7_NO_ATR;
        c.enable_atr_filter = false;
        c.enable_mean_reversion = false;
        c.enable_time_exit = false;
        c.avoid_hour_4 = false;
        c.avoid_hour_9 = false;
        c.avoid_hour_17 = false;
        configs.push_back(c);
    }

    // V10 with ATR
    {
        Config c;
        c.version = V10_MEAN_REV;
        c.enable_atr_filter = true;
        c.enable_mean_reversion = true;
        c.enable_time_exit = false;
        c.avoid_hour_4 = false;
        c.avoid_hour_9 = false;
        c.avoid_hour_17 = false;
        configs.push_back(c);
    }

    // V10 without ATR
    {
        Config c;
        c.version = V10_NO_ATR;
        c.enable_atr_filter = false;
        c.enable_mean_reversion = true;
        c.enable_time_exit = false;
        c.avoid_hour_4 = false;
        c.avoid_hour_9 = false;
        c.avoid_hour_17 = false;
        configs.push_back(c);
    }

    // V12 Optimized (no ATR, time exit, enhanced session)
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

    // V12 with ATR (for comparison)
    {
        Config c;
        c.version = V12_WITH_ATR;
        c.enable_atr_filter = true;
        c.enable_mean_reversion = true;
        c.enable_time_exit = true;
        c.avoid_hour_4 = true;
        c.avoid_hour_9 = true;
        c.avoid_hour_17 = true;
        configs.push_back(c);
    }

    // Run tests
    printf("Running backtests...\n\n");
    std::vector<Result> results;

    for (Config& cfg : configs) {
        printf("  Testing %s...\n", StrategyName(cfg.version));
        results.push_back(RunBacktest(ticks, cfg));
    }

    // Sort by return
    std::vector<Result> sorted = results;
    std::sort(sorted.begin(), sorted.end(), [](const Result& a, const Result& b) {
        return a.return_pct > b.return_pct;
    });

    // Results table
    printf("\n================================================================\n");
    printf("RESULTS (sorted by return)\n");
    printf("================================================================\n\n");

    printf("%-20s %9s %7s %8s %8s %8s\n",
           "Strategy", "Return", "MaxDD", "Trades", "TimeExit", "ProtExit");
    printf("--------------------------------------------------------------------------\n");

    for (const Result& r : sorted) {
        printf("%-20s %+8.2f%% %6.2f%% %8d %8d %8d\n",
               StrategyName(r.version),
               r.return_pct,
               r.max_dd,
               r.total_trades,
               r.time_exits,
               r.protection_closes);
    }

    printf("--------------------------------------------------------------------------\n\n");

    // Analysis: ATR filter impact
    printf("================================================================\n");
    printf("ANALYSIS: ATR FILTER IMPACT\n");
    printf("================================================================\n\n");

    Result v7_atr = results[0];
    Result v7_no_atr = results[1];
    Result v10_atr = results[2];
    Result v10_no_atr = results[3];

    printf("V7 Comparison:\n");
    printf("  With ATR:    %+.2f%% return, %.2f%% max DD\n", v7_atr.return_pct, v7_atr.max_dd);
    printf("  Without ATR: %+.2f%% return, %.2f%% max DD\n", v7_no_atr.return_pct, v7_no_atr.max_dd);
    if (v7_no_atr.return_pct > v7_atr.return_pct) {
        printf("  -> ATR filter HURTS V7 by %.2f%%\n", v7_atr.return_pct - v7_no_atr.return_pct);
    } else {
        printf("  -> ATR filter HELPS V7 by %.2f%%\n", v7_no_atr.return_pct - v7_atr.return_pct);
    }
    printf("\n");

    printf("V10 Comparison:\n");
    printf("  With ATR:    %+.2f%% return, %.2f%% max DD\n", v10_atr.return_pct, v10_atr.max_dd);
    printf("  Without ATR: %+.2f%% return, %.2f%% max DD\n", v10_no_atr.return_pct, v10_no_atr.max_dd);
    if (v10_no_atr.return_pct > v10_atr.return_pct) {
        printf("  -> ATR filter HURTS V10 by %.2f%%\n", v10_atr.return_pct - v10_no_atr.return_pct);
    } else {
        printf("  -> ATR filter HELPS V10 by %.2f%%\n", v10_no_atr.return_pct - v10_atr.return_pct);
    }
    printf("\n");

    // Analysis: V12 improvements
    printf("================================================================\n");
    printf("ANALYSIS: V12 IMPROVEMENTS\n");
    printf("================================================================\n\n");

    Result v12 = results[4];
    Result v12_atr = results[5];

    printf("V12 Optimized vs V10 (ATR ON):\n");
    printf("  V10:  %+.2f%% return, %.2f%% max DD, %d trades\n",
           v10_atr.return_pct, v10_atr.max_dd, v10_atr.total_trades);
    printf("  V12:  %+.2f%% return, %.2f%% max DD, %d trades\n",
           v12.return_pct, v12.max_dd, v12.total_trades);
    printf("  Time exits triggered: %d\n", v12.time_exits);
    printf("\n");

    if (v12.return_pct > v10_atr.return_pct) {
        printf("  -> V12 IMPROVES returns by %.2f%%\n", v12.return_pct - v10_atr.return_pct);
    } else {
        printf("  -> V12 DECREASES returns by %.2f%%\n", v10_atr.return_pct - v12.return_pct);
    }

    if (v12.max_dd < v10_atr.max_dd) {
        printf("  -> V12 REDUCES max DD by %.2f%%\n", v10_atr.max_dd - v12.max_dd);
    } else {
        printf("  -> V12 INCREASES max DD by %.2f%%\n", v12.max_dd - v10_atr.max_dd);
    }

    // Conclusion
    printf("\n================================================================\n");
    printf("CONCLUSION\n");
    printf("================================================================\n\n");

    // Find best performer
    Result best = sorted[0];
    printf("BEST PERFORMER: %s\n", StrategyName(best.version));
    printf("  Return: %+.2f%%\n", best.return_pct);
    printf("  Max DD: %.2f%%\n", best.max_dd);
    printf("  Trades: %d\n\n", best.total_trades);

    // Check if removing ATR helps
    bool atr_hurts_v7 = v7_no_atr.return_pct > v7_atr.return_pct;
    bool atr_hurts_v10 = v10_no_atr.return_pct > v10_atr.return_pct;

    if (atr_hurts_v7 && atr_hurts_v10) {
        printf("CONFIRMED: ATR filter HURTS performance in both V7 and V10!\n");
        printf("Recommendation: DISABLE ATR filter for production.\n");
    } else if (atr_hurts_v7 || atr_hurts_v10) {
        printf("MIXED: ATR filter helps one version but not the other.\n");
        printf("Recommendation: Test further with different market conditions.\n");
    } else {
        printf("CONTRARY: ATR filter actually HELPS in this test period.\n");
        printf("Recommendation: Keep ATR filter, original analysis may be period-specific.\n");
    }

    printf("\n================================================================\n");
    printf("TEST COMPLETE\n");
    printf("================================================================\n");

    return 0;
}
