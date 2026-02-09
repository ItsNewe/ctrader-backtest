/**
 * Comprehensive Parameter Sweep
 *
 * Tests all key parameters to find optimal configuration:
 * 1. Spacing (0.50 - 1.50)
 * 2. TP Multiplier (1.5 - 3.0)
 * 3. Max Positions (10 - 30)
 * 4. Protection levels (10% - 25%)
 * 5. Session filter (on/off, different hours)
 * 6. Lot size (0.02 - 0.05)
 *
 * Uses V7 NoATR strategy (best performer)
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

struct Trade {
    double entry_price;
    double lot_size;
    double take_profit;
};

struct Config {
    double lot_size = 0.035;
    double spacing = 0.75;
    double tp_multiplier = 2.0;
    int max_positions = 15;
    double stop_new_at_dd = 3.0;
    double partial_close_at_dd = 5.0;
    double close_all_at_dd = 15.0;
    bool session_filter = true;
    int session_start = 14;
    int session_end = 18;
    double contract_size = 100.0;
};

struct Result {
    Config config;
    double final_balance;
    double return_pct;
    double return_multiple;
    double max_dd;
    int total_trades;
    double sharpe_approx;  // return / max_dd as simple risk metric
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
        int hour = 12;
        if (strlen(line) >= 13) {
            char h[3] = {line[11], line[12], 0};
            hour = atoi(h);
        }
        tick.hour = hour;

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

Result RunBacktest(const std::vector<Tick>& ticks, Config cfg) {
    Result r;
    r.config = cfg;

    const double initial = 10000.0;
    double balance = initial;
    double equity = initial;
    double peak = initial;
    double max_dd = 0;

    std::vector<Trade> positions;
    bool partial_done = false;
    bool all_closed = false;

    for (const Tick& tick : ticks) {
        // Calculate equity
        equity = balance;
        for (auto& t : positions) {
            equity += (tick.bid - t.entry_price) * t.lot_size * cfg.contract_size;
        }

        // Track peak/DD
        if (positions.empty() && peak != balance) {
            peak = balance;
            partial_done = false;
            all_closed = false;
        }
        if (equity > peak) {
            peak = equity;
            partial_done = false;
            all_closed = false;
        }

        double dd = (peak > 0) ? (peak - equity) / peak * 100.0 : 0;
        if (dd > max_dd) max_dd = dd;

        // Protection: Close all
        if (dd > cfg.close_all_at_dd && !all_closed && !positions.empty()) {
            for (auto& t : positions) {
                balance += (tick.bid - t.entry_price) * t.lot_size * cfg.contract_size;
            }
            positions.clear();
            all_closed = true;
            equity = balance;
            peak = equity;
            continue;
        }

        // Protection: Partial close
        if (dd > cfg.partial_close_at_dd && !partial_done && positions.size() > 1) {
            std::sort(positions.begin(), positions.end(), [&](const Trade& a, const Trade& b) {
                return (tick.bid - a.entry_price) < (tick.bid - b.entry_price);
            });
            int to_close = std::max(1, (int)(positions.size() / 2));
            for (int i = 0; i < to_close; i++) {
                balance += (tick.bid - positions[0].entry_price) * positions[0].lot_size * cfg.contract_size;
                positions.erase(positions.begin());
            }
            partial_done = true;
        }

        // Check TPs
        for (auto it = positions.begin(); it != positions.end();) {
            if (tick.bid >= it->take_profit) {
                balance += (tick.bid - it->entry_price) * it->lot_size * cfg.contract_size;
                r.total_trades++;
                it = positions.erase(it);
            } else {
                ++it;
            }
        }

        // Session filter
        bool session_ok = !cfg.session_filter ||
                         !(tick.hour >= cfg.session_start && tick.hour < cfg.session_end);

        if (dd >= cfg.stop_new_at_dd || !session_ok) continue;

        // Grid logic
        double lowest = DBL_MAX, highest = -DBL_MAX;
        for (auto& t : positions) {
            lowest = std::min(lowest, t.entry_price);
            highest = std::max(highest, t.entry_price);
        }

        if ((int)positions.size() < cfg.max_positions) {
            bool should_open = positions.empty() ||
                              (lowest >= tick.ask + cfg.spacing) ||
                              (highest <= tick.ask - cfg.spacing);

            if (should_open) {
                Trade t;
                t.entry_price = tick.ask;
                t.lot_size = cfg.lot_size;
                t.take_profit = tick.ask + tick.spread() + cfg.spacing * cfg.tp_multiplier;
                positions.push_back(t);
            }
        }
    }

    // Close remaining
    for (auto& t : positions) {
        balance += (ticks.back().bid - t.entry_price) * t.lot_size * cfg.contract_size;
        r.total_trades++;
    }

    r.final_balance = balance;
    r.return_pct = (balance - initial) / initial * 100.0;
    r.return_multiple = balance / initial;
    r.max_dd = max_dd;
    r.sharpe_approx = (max_dd > 0) ? r.return_multiple / (max_dd / 10.0) : 0;

    return r;
}

void PrintResult(const Result& r, const char* label) {
    printf("%-25s %7.1fx %8.1f%% %8d %8.2f\n",
           label, r.return_multiple, r.max_dd, r.total_trades, r.sharpe_approx);
}

int main() {
    printf("================================================================\n");
    printf("COMPREHENSIVE PARAMETER SWEEP\n");
    printf("================================================================\n\n");

    const char* filename = "Grid/XAUUSD_TICKS_2025.csv";
    printf("Loading data...\n");

    std::vector<Tick> ticks = LoadTicks(filename, 60000000);
    if (ticks.empty()) {
        printf("ERROR: Failed to load data\n");
        return 1;
    }
    printf("Loaded %zu ticks\n\n", ticks.size());

    Config base;
    base.lot_size = 0.035;
    base.spacing = 0.75;
    base.tp_multiplier = 2.0;
    base.max_positions = 15;
    base.stop_new_at_dd = 3.0;
    base.partial_close_at_dd = 5.0;
    base.close_all_at_dd = 15.0;
    base.session_filter = true;
    base.session_start = 14;
    base.session_end = 18;

    std::vector<Result> all_results;

    // ========================================================================
    // TEST 1: SPACING
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 1: SPACING SWEEP\n");
    printf("================================================================\n\n");

    printf("%-25s %7s %8s %8s %8s\n", "Spacing", "Return", "Max DD", "Trades", "Sharpe");
    printf("----------------------------------------------------------------\n");

    Result best_spacing;
    best_spacing.sharpe_approx = 0;

    double spacings[] = {0.25, 0.50, 0.75, 1.00, 1.25, 1.50, 2.00};
    for (double s : spacings) {
        Config cfg = base;
        cfg.spacing = s;
        Result r = RunBacktest(ticks, cfg);
        all_results.push_back(r);

        char label[32];
        snprintf(label, sizeof(label), "Spacing = %.2f", s);
        PrintResult(r, label);

        if (r.sharpe_approx > best_spacing.sharpe_approx) best_spacing = r;
    }
    printf("----------------------------------------------------------------\n");
    printf("BEST: Spacing = %.2f (%.1fx return, %.1f%% DD)\n\n",
           best_spacing.config.spacing, best_spacing.return_multiple, best_spacing.max_dd);

    // ========================================================================
    // TEST 2: TP MULTIPLIER
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 2: TP MULTIPLIER SWEEP\n");
    printf("================================================================\n\n");

    printf("%-25s %7s %8s %8s %8s\n", "TP Mult", "Return", "Max DD", "Trades", "Sharpe");
    printf("----------------------------------------------------------------\n");

    Result best_tp;
    best_tp.sharpe_approx = 0;

    double tp_mults[] = {1.0, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0};
    for (double tp : tp_mults) {
        Config cfg = base;
        cfg.tp_multiplier = tp;
        Result r = RunBacktest(ticks, cfg);
        all_results.push_back(r);

        char label[32];
        snprintf(label, sizeof(label), "TP Mult = %.1f", tp);
        PrintResult(r, label);

        if (r.sharpe_approx > best_tp.sharpe_approx) best_tp = r;
    }
    printf("----------------------------------------------------------------\n");
    printf("BEST: TP Mult = %.1f (%.1fx return, %.1f%% DD)\n\n",
           best_tp.config.tp_multiplier, best_tp.return_multiple, best_tp.max_dd);

    // ========================================================================
    // TEST 3: MAX POSITIONS
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 3: MAX POSITIONS SWEEP\n");
    printf("================================================================\n\n");

    printf("%-25s %7s %8s %8s %8s\n", "Max Pos", "Return", "Max DD", "Trades", "Sharpe");
    printf("----------------------------------------------------------------\n");

    Result best_maxpos;
    best_maxpos.sharpe_approx = 0;

    int max_positions[] = {5, 10, 15, 20, 25, 30, 40};
    for (int mp : max_positions) {
        Config cfg = base;
        cfg.max_positions = mp;
        Result r = RunBacktest(ticks, cfg);
        all_results.push_back(r);

        char label[32];
        snprintf(label, sizeof(label), "Max Pos = %d", mp);
        PrintResult(r, label);

        if (r.sharpe_approx > best_maxpos.sharpe_approx) best_maxpos = r;
    }
    printf("----------------------------------------------------------------\n");
    printf("BEST: Max Pos = %d (%.1fx return, %.1f%% DD)\n\n",
           best_maxpos.config.max_positions, best_maxpos.return_multiple, best_maxpos.max_dd);

    // ========================================================================
    // TEST 4: CLOSE ALL DD THRESHOLD
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 4: CLOSE ALL DD THRESHOLD SWEEP\n");
    printf("================================================================\n\n");

    printf("%-25s %7s %8s %8s %8s\n", "Close DD", "Return", "Max DD", "Trades", "Sharpe");
    printf("----------------------------------------------------------------\n");

    Result best_closedd;
    best_closedd.sharpe_approx = 0;

    double close_dds[] = {10.0, 12.0, 15.0, 18.0, 20.0, 25.0, 30.0};
    for (double dd : close_dds) {
        Config cfg = base;
        cfg.close_all_at_dd = dd;
        cfg.partial_close_at_dd = dd * 0.33;  // Proportional
        cfg.stop_new_at_dd = dd * 0.20;
        Result r = RunBacktest(ticks, cfg);
        all_results.push_back(r);

        char label[32];
        snprintf(label, sizeof(label), "Close DD = %.0f%%", dd);
        PrintResult(r, label);

        if (r.sharpe_approx > best_closedd.sharpe_approx) best_closedd = r;
    }
    printf("----------------------------------------------------------------\n");
    printf("BEST: Close DD = %.0f%% (%.1fx return, %.1f%% DD)\n\n",
           best_closedd.config.close_all_at_dd, best_closedd.return_multiple, best_closedd.max_dd);

    // ========================================================================
    // TEST 5: SESSION FILTER
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 5: SESSION FILTER SWEEP\n");
    printf("================================================================\n\n");

    printf("%-25s %7s %8s %8s %8s\n", "Session", "Return", "Max DD", "Trades", "Sharpe");
    printf("----------------------------------------------------------------\n");

    Result best_session;
    best_session.sharpe_approx = 0;

    // Test different session configurations
    struct SessionTest {
        bool enabled;
        int start;
        int end;
        const char* name;
    };

    SessionTest sessions[] = {
        {false, 0, 0, "No Filter"},
        {true, 14, 18, "Avoid 14-18 (US)"},
        {true, 13, 19, "Avoid 13-19"},
        {true, 12, 20, "Avoid 12-20"},
        {true, 14, 16, "Avoid 14-16"},
        {true, 0, 8, "Avoid 0-8 (Asia)"},
        {true, 8, 14, "Avoid 8-14 (EU)"},
    };

    for (auto& s : sessions) {
        Config cfg = base;
        cfg.session_filter = s.enabled;
        cfg.session_start = s.start;
        cfg.session_end = s.end;
        Result r = RunBacktest(ticks, cfg);
        all_results.push_back(r);

        PrintResult(r, s.name);

        if (r.sharpe_approx > best_session.sharpe_approx) {
            best_session = r;
        }
    }
    printf("----------------------------------------------------------------\n");

    // ========================================================================
    // TEST 6: LOT SIZE
    // ========================================================================
    printf("\n================================================================\n");
    printf("TEST 6: LOT SIZE SWEEP (Risk vs Return)\n");
    printf("================================================================\n\n");

    printf("%-25s %7s %8s %8s %8s\n", "Lot Size", "Return", "Max DD", "Trades", "Sharpe");
    printf("----------------------------------------------------------------\n");

    Result best_lot;
    best_lot.sharpe_approx = 0;

    double lots[] = {0.01, 0.02, 0.025, 0.03, 0.035, 0.04, 0.045, 0.05};
    for (double lot : lots) {
        Config cfg = base;
        cfg.lot_size = lot;
        Result r = RunBacktest(ticks, cfg);
        all_results.push_back(r);

        char label[32];
        snprintf(label, sizeof(label), "Lot = %.3f", lot);
        PrintResult(r, label);

        if (r.max_dd < 50 && r.sharpe_approx > best_lot.sharpe_approx) {
            best_lot = r;
        }
    }
    printf("----------------------------------------------------------------\n");
    printf("BEST (DD<50%%): Lot = %.3f (%.1fx return, %.1f%% DD)\n\n",
           best_lot.config.lot_size, best_lot.return_multiple, best_lot.max_dd);

    // ========================================================================
    // COMBINED OPTIMIZATION
    // ========================================================================
    printf("================================================================\n");
    printf("COMBINED OPTIMIZATION - Testing Best Parameters Together\n");
    printf("================================================================\n\n");

    // Test combinations of best parameters
    printf("Testing top parameter combinations...\n\n");

    printf("%-40s %7s %8s %8s\n", "Configuration", "Return", "Max DD", "Sharpe");
    printf("------------------------------------------------------------------------\n");

    Result best_overall;
    best_overall.sharpe_approx = 0;

    // Test combinations
    double test_spacings[] = {0.50, 0.75, 1.00};
    double test_tps[] = {1.5, 2.0, 2.5};
    int test_maxpos[] = {10, 15, 20};
    double test_lots[] = {0.03, 0.035, 0.04};

    for (double sp : test_spacings) {
        for (double tp : test_tps) {
            for (int mp : test_maxpos) {
                for (double lot : test_lots) {
                    Config cfg = base;
                    cfg.spacing = sp;
                    cfg.tp_multiplier = tp;
                    cfg.max_positions = mp;
                    cfg.lot_size = lot;

                    Result r = RunBacktest(ticks, cfg);

                    // Only show if it's good
                    if (r.max_dd < 25 && r.return_multiple > 3) {
                        char label[64];
                        snprintf(label, sizeof(label), "S=%.2f TP=%.1f MP=%d L=%.3f",
                                sp, tp, mp, lot);
                        printf("%-40s %6.1fx %7.1f%% %8.2f\n",
                               label, r.return_multiple, r.max_dd, r.sharpe_approx);

                        if (r.sharpe_approx > best_overall.sharpe_approx) {
                            best_overall = r;
                        }
                    }
                }
            }
        }
    }

    printf("------------------------------------------------------------------------\n\n");

    // ========================================================================
    // FINAL RESULTS
    // ========================================================================
    printf("================================================================\n");
    printf("OPTIMAL PARAMETERS FOUND\n");
    printf("================================================================\n\n");

    printf("BEST OVERALL CONFIGURATION:\n");
    printf("  Spacing:        %.2f\n", best_overall.config.spacing);
    printf("  TP Multiplier:  %.1f\n", best_overall.config.tp_multiplier);
    printf("  Max Positions:  %d\n", best_overall.config.max_positions);
    printf("  Lot Size:       %.3f\n", best_overall.config.lot_size);
    printf("  Close All DD:   %.1f%%\n", best_overall.config.close_all_at_dd);
    printf("  Session Filter: %s\n", best_overall.config.session_filter ? "ON (14-18)" : "OFF");
    printf("\n");
    printf("EXPECTED PERFORMANCE:\n");
    printf("  Annual Return:  %.1fx ($10,000 -> $%.0f)\n",
           best_overall.return_multiple, best_overall.final_balance);
    printf("  Max Drawdown:   %.1f%%\n", best_overall.max_dd);
    printf("  Total Trades:   %d\n", best_overall.total_trades);
    printf("  Risk/Reward:    %.2f\n", best_overall.sharpe_approx);

    printf("\n");
    printf("COMPARISON TO BASELINE:\n");
    Result baseline = RunBacktest(ticks, base);
    printf("  Baseline:  %.1fx return, %.1f%% DD, Sharpe %.2f\n",
           baseline.return_multiple, baseline.max_dd, baseline.sharpe_approx);
    printf("  Optimal:   %.1fx return, %.1f%% DD, Sharpe %.2f\n",
           best_overall.return_multiple, best_overall.max_dd, best_overall.sharpe_approx);

    double improvement = (best_overall.sharpe_approx - baseline.sharpe_approx) / baseline.sharpe_approx * 100;
    printf("  Improvement: %+.1f%% better risk-adjusted return\n", improvement);

    printf("\n================================================================\n");
    printf("SWEEP COMPLETE\n");
    printf("================================================================\n");

    return 0;
}
