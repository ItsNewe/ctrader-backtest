/**
 * Original Fill-Up vs Optimized Comparison
 *
 * Compares:
 * 1. Original fill_up strategy (dynamic margin-based sizing)
 * 2. Optimized V7-style strategy (fixed lots with DD protection)
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
    size_t id;
    double entry_price;
    double lot_size;
    double take_profit;
};

// ============================================================================
// ORIGINAL FILL-UP STRATEGY (Simplified simulation)
// - Dynamic lot sizing based on margin/equity
// - Spacing = 1.0, survive_pct = 13%
// - No max positions limit, no DD protection
// ============================================================================
struct OriginalResult {
    double final_balance;
    double return_pct;
    double return_multiple;
    double max_dd;
    int total_trades;
    bool margin_call;
};

OriginalResult RunOriginal(const std::vector<Tick>& ticks) {
    OriginalResult r = {0};

    const double initial_balance = 10000.0;
    const double contract_size = 100.0;
    const double leverage = 500.0;
    const double spacing = 1.0;
    const double survive_pct = 13.0;
    const double min_lot = 0.01;
    const double max_lot = 100.0;

    double balance = initial_balance;
    double equity = balance;
    double peak_equity = balance;
    double max_drawdown = 0;

    std::vector<Trade*> positions;
    size_t next_id = 1;

    double lowest = DBL_MAX, highest = -DBL_MAX;

    for (const Tick& tick : ticks) {
        // Calculate equity
        equity = balance;
        double total_lots = 0;
        for (Trade* t : positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * contract_size;
            total_lots += t->lot_size;
        }

        // Margin call check (equity < 20% of used margin)
        double used_margin = 0;
        for (Trade* t : positions) {
            used_margin += t->lot_size * contract_size * t->entry_price / leverage;
        }

        if (used_margin > 0 && equity < used_margin * 0.2) {
            r.margin_call = true;
            for (Trade* t : positions) {
                balance += (tick.bid - t->entry_price) * t->lot_size * contract_size;
                delete t;
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

        // Check TPs (original: TP = entry + spread + spacing)
        for (auto it = positions.begin(); it != positions.end();) {
            Trade* t = *it;
            if (tick.bid >= t->take_profit) {
                balance += (tick.bid - t->entry_price) * t->lot_size * contract_size;
                r.total_trades++;
                delete t;
                it = positions.erase(it);
            } else {
                ++it;
            }
        }

        // Grid bounds
        lowest = DBL_MAX; highest = -DBL_MAX;
        for (Trade* t : positions) {
            lowest = std::min(lowest, t->entry_price);
            highest = std::max(highest, t->entry_price);
        }

        // Dynamic lot sizing (simplified from original algorithm)
        // Original calculates based on surviving survive_pct drawdown
        bool should_open = positions.empty() ||
                          (lowest >= tick.ask + spacing) ||
                          (highest <= tick.ask - spacing);

        if (should_open) {
            // Simplified dynamic sizing: scale with equity
            double lot = min_lot * (equity / initial_balance);
            lot = std::max(min_lot, std::min(max_lot, lot));
            lot = std::round(lot * 100) / 100;

            // Margin check
            double margin_needed = lot * contract_size * tick.ask / leverage;
            double free_margin = equity - used_margin;

            if (free_margin > margin_needed * 1.5) {
                Trade* t = new Trade();
                t->id = next_id++;
                t->entry_price = tick.ask;
                t->lot_size = lot;
                t->take_profit = tick.ask + tick.spread() + spacing;  // Original TP = spread + spacing
                positions.push_back(t);
            }
        }
    }

    // Close remaining
    for (Trade* t : positions) {
        balance += (ticks.back().bid - t->entry_price) * t->lot_size * contract_size;
        r.total_trades++;
        delete t;
    }

    r.final_balance = std::max(0.0, balance);
    r.return_pct = (r.final_balance - initial_balance) / initial_balance * 100.0;
    r.return_multiple = r.final_balance / initial_balance;
    r.max_dd = max_drawdown;

    return r;
}

// ============================================================================
// OPTIMIZED STRATEGY (V7-style with best parameters)
// - Fixed lot size with DD protection
// - Session filter, partial close, emergency close
// ============================================================================
struct OptimizedConfig {
    double lot_size = 0.040;
    double spacing = 0.50;
    double tp_multiplier = 2.5;
    int max_positions = 15;
    double stop_new_at_dd = 3.0;
    double partial_close_at_dd = 5.0;
    double close_all_at_dd = 15.0;
    int session_avoid_start = 14;
    int session_avoid_end = 18;
};

OriginalResult RunOptimized(const std::vector<Tick>& ticks, OptimizedConfig cfg) {
    OriginalResult r = {0};

    const double initial_balance = 10000.0;
    const double contract_size = 100.0;

    double balance = initial_balance;
    double equity = balance;
    double peak_equity = balance;
    double max_drawdown = 0;

    std::vector<Trade*> positions;
    size_t next_id = 1;
    bool partial_done = false;
    bool all_closed = false;

    double lowest = DBL_MAX, highest = -DBL_MAX;

    for (const Tick& tick : ticks) {
        // Calculate equity
        equity = balance;
        for (Trade* t : positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * contract_size;
        }

        if (equity <= 0) {
            r.margin_call = true;
            break;
        }

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
                balance += (tick.bid - t->entry_price) * t->lot_size * contract_size;
                delete t;
            }
            positions.clear();
            all_closed = true;
            equity = balance;
            peak_equity = equity;
            continue;
        }

        // Partial close
        if (dd_pct > cfg.partial_close_at_dd && !partial_done && positions.size() > 1) {
            std::sort(positions.begin(), positions.end(), [&](Trade* a, Trade* b) {
                return (tick.bid - a->entry_price) < (tick.bid - b->entry_price);
            });
            int to_close = std::max(1, (int)(positions.size() * 0.5));
            for (int i = 0; i < to_close && !positions.empty(); i++) {
                Trade* t = positions[0];
                balance += (tick.bid - t->entry_price) * t->lot_size * contract_size;
                delete t;
                positions.erase(positions.begin());
            }
            partial_done = true;
        }

        // Check TPs
        for (auto it = positions.begin(); it != positions.end();) {
            Trade* t = *it;
            if (tick.bid >= t->take_profit) {
                balance += (tick.bid - t->entry_price) * t->lot_size * contract_size;
                r.total_trades++;
                delete t;
                it = positions.erase(it);
            } else {
                ++it;
            }
        }

        // Session filter
        bool session_ok = !(tick.hour >= cfg.session_avoid_start && tick.hour < cfg.session_avoid_end);
        if (dd_pct >= cfg.stop_new_at_dd || !session_ok) continue;

        // Grid bounds
        lowest = DBL_MAX; highest = -DBL_MAX;
        for (Trade* t : positions) {
            lowest = std::min(lowest, t->entry_price);
            highest = std::max(highest, t->entry_price);
        }

        // Open new
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
                positions.push_back(t);
            }
        }
    }

    // Close remaining
    for (Trade* t : positions) {
        balance += (ticks.back().bid - t->entry_price) * t->lot_size * contract_size;
        r.total_trades++;
        delete t;
    }

    r.final_balance = std::max(0.0, balance);
    r.return_pct = (r.final_balance - initial_balance) / initial_balance * 100.0;
    r.return_multiple = r.final_balance / initial_balance;
    r.max_dd = max_drawdown;

    return r;
}

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

int main() {
    printf("================================================================\n");
    printf("ORIGINAL FILL-UP vs OPTIMIZED COMPARISON\n");
    printf("================================================================\n\n");

    const char* filename = "Grid/XAUUSD_TICKS_2025.csv";
    printf("Loading data...\n");

    std::vector<Tick> ticks = LoadTicks(filename, 60000000);
    if (ticks.empty()) {
        printf("ERROR: Failed to load data\n");
        return 1;
    }
    printf("Loaded %zu ticks\n\n", ticks.size());

    // Run original fill-up
    printf("Running ORIGINAL fill-up strategy...\n");
    printf("  - Dynamic lot sizing based on margin\n");
    printf("  - Spacing: $1.00\n");
    printf("  - TP: spread + spacing (1x multiplier)\n");
    printf("  - No max positions limit\n");
    printf("  - No DD protection\n\n");

    OriginalResult orig = RunOriginal(ticks);

    // Run baseline optimized (original parameters we started with)
    printf("Running BASELINE optimized (starting point)...\n");
    OptimizedConfig baseline;
    baseline.lot_size = 0.035;
    baseline.spacing = 0.75;
    baseline.tp_multiplier = 2.0;
    baseline.max_positions = 15;
    printf("  - Fixed lot: 0.035\n");
    printf("  - Spacing: $0.75\n");
    printf("  - TP multiplier: 2.0\n");
    printf("  - Max positions: 15\n");
    printf("  - DD protection: 3%%/5%%/15%%\n\n");

    OriginalResult base = RunOptimized(ticks, baseline);

    // Run fully optimized
    printf("Running OPTIMIZED strategy (best parameters)...\n");
    OptimizedConfig optimized;
    optimized.lot_size = 0.040;
    optimized.spacing = 0.50;
    optimized.tp_multiplier = 2.5;
    optimized.max_positions = 15;
    printf("  - Fixed lot: 0.040\n");
    printf("  - Spacing: $0.50\n");
    printf("  - TP multiplier: 2.5\n");
    printf("  - Max positions: 15\n");
    printf("  - DD protection: 3%%/5%%/15%%\n\n");

    OriginalResult opt = RunOptimized(ticks, optimized);

    // Print comparison
    printf("================================================================\n");
    printf("RESULTS COMPARISON\n");
    printf("================================================================\n\n");

    printf("%-25s %12s %12s %12s\n", "Metric", "Original", "Baseline", "Optimized");
    printf("------------------------------------------------------------------------\n");
    printf("%-25s $%11.2f $%11.2f $%11.2f\n", "Final Balance", orig.final_balance, base.final_balance, opt.final_balance);
    printf("%-25s %11.1fx %11.1fx %11.1fx\n", "Return Multiple", orig.return_multiple, base.return_multiple, opt.return_multiple);
    printf("%-25s %11.1f%% %11.1f%% %11.1f%%\n", "Max Drawdown", orig.max_dd, base.max_dd, opt.max_dd);
    printf("%-25s %12d %12d %12d\n", "Total Trades", orig.total_trades, base.total_trades, opt.total_trades);
    printf("%-25s %12s %12s %12s\n", "Margin Call?", orig.margin_call ? "YES" : "NO", base.margin_call ? "YES" : "NO", opt.margin_call ? "YES" : "NO");

    double orig_sharpe = (orig.max_dd > 0) ? (orig.return_multiple - 1) * 100 / orig.max_dd : 0;
    double base_sharpe = (base.max_dd > 0) ? (base.return_multiple - 1) * 100 / base.max_dd : 0;
    double opt_sharpe = (opt.max_dd > 0) ? (opt.return_multiple - 1) * 100 / opt.max_dd : 0;
    printf("%-25s %12.2f %12.2f %12.2f\n", "Risk/Reward Ratio", orig_sharpe, base_sharpe, opt_sharpe);

    printf("------------------------------------------------------------------------\n\n");

    // Calculate improvements
    printf("================================================================\n");
    printf("IMPROVEMENT ANALYSIS\n");
    printf("================================================================\n\n");

    if (orig.return_multiple > 0) {
        double base_vs_orig = ((base.return_multiple / orig.return_multiple) - 1) * 100;
        double opt_vs_orig = ((opt.return_multiple / orig.return_multiple) - 1) * 100;
        double opt_vs_base = ((opt.return_multiple / base.return_multiple) - 1) * 100;

        printf("Baseline vs Original:   %+.1f%% return\n", base_vs_orig);
        printf("Optimized vs Original:  %+.1f%% return\n", opt_vs_orig);
        printf("Optimized vs Baseline:  %+.1f%% return\n\n", opt_vs_base);
    }

    printf("KEY DIFFERENCES:\n");
    printf("  Original fill-up:\n");
    printf("    - Dynamic margin-based lot sizing (scales with equity)\n");
    printf("    - Wider spacing ($1.00)\n");
    printf("    - Smaller TP (1x spacing only)\n");
    printf("    - No position limit or DD protection\n");
    printf("    - Higher risk, potentially unlimited positions\n\n");

    printf("  Optimized strategy:\n");
    printf("    - Fixed lot size (0.040) - predictable risk\n");
    printf("    - Tighter spacing ($0.50) - more trade opportunities\n");
    printf("    - Larger TP (2.5x spacing) - better profit per trade\n");
    printf("    - 15 position limit - controlled exposure\n");
    printf("    - 3-tier DD protection (3%%/5%%/15%%)\n");
    printf("    - Session filter (avoid US peak hours)\n");

    printf("\n================================================================\n");
    printf("COMPARISON COMPLETE\n");
    printf("================================================================\n");

    return 0;
}
