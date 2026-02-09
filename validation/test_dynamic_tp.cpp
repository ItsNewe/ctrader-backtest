/**
 * Dynamic Take-Profit Strategy Comparison Test
 *
 * Hypothesis: TP should be wider in high volatility (more room to move)
 * and tighter in low volatility (capture smaller moves).
 *
 * Tests 5 TP strategies:
 * 1. Fixed TP: spacing * 2.0 (baseline)
 * 2. ATR-based TP: TP = ATR_current * multiplier
 * 3. Range-based TP: TP = recent_range * multiplier
 * 4. Inverse volatility TP: Wider TP when vol is low, tighter when high
 * 5. Adaptive TP: TP shrinks as position ages (give up on big move)
 *
 * Uses V7 volatility filter with each TP strategy.
 * Compares: return, win rate, average hold time, max DD.
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
    size_t entry_tick;      // Tick number when position was opened
    double initial_tp;      // Original TP before any adjustment
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

class RangeTracker {
    std::deque<double> highs;
    std::deque<double> lows;
    int period;
    double current_high;
    double current_low;
public:
    RangeTracker(int p) : period(p), current_high(-DBL_MAX), current_low(DBL_MAX) {}

    void Add(double price) {
        // Track high/low over period
        highs.push_back(price);
        lows.push_back(price);

        if ((int)highs.size() > period) {
            highs.pop_front();
            lows.pop_front();
        }

        // Recalculate range
        current_high = -DBL_MAX;
        current_low = DBL_MAX;
        for (double h : highs) current_high = std::max(current_high, h);
        for (double l : lows) current_low = std::min(current_low, l);
    }

    double GetRange() const {
        if (current_high < current_low) return 0;
        return current_high - current_low;
    }

    bool IsReady() const { return (int)highs.size() >= period; }
};

// ============================================================================
// TP Strategies
// ============================================================================

enum TPStrategy {
    TP_FIXED,           // Fixed TP = spacing * 2.0
    TP_ATR_BASED,       // TP = ATR * multiplier
    TP_RANGE_BASED,     // TP = recent_range * multiplier
    TP_INVERSE_VOL,     // Wider TP when vol is low, tighter when high
    TP_ADAPTIVE_AGE     // TP shrinks as position ages
};

const char* TPStrategyName(TPStrategy s) {
    switch (s) {
        case TP_FIXED:       return "Fixed (2x spacing)";
        case TP_ATR_BASED:   return "ATR-based";
        case TP_RANGE_BASED: return "Range-based";
        case TP_INVERSE_VOL: return "Inverse Volatility";
        case TP_ADAPTIVE_AGE: return "Adaptive Age";
    }
    return "Unknown";
}

// ============================================================================
// Strategy Configuration
// ============================================================================

struct Config {
    // Grid parameters
    double spacing = 1.0;
    double lot_size = 0.01;
    double contract_size = 100.0;
    double leverage = 500.0;
    int max_positions = 20;

    // V3 Protection
    double stop_new_at_dd = 5.0;
    double partial_close_at_dd = 8.0;
    double close_all_at_dd = 25.0;

    // V7 Volatility filter
    int atr_short_period = 100;
    int atr_long_period = 500;
    double volatility_threshold = 0.8;

    // TP parameters
    TPStrategy tp_strategy = TP_FIXED;
    double tp_fixed_mult = 2.0;        // For FIXED: TP = spacing * this
    double tp_atr_mult = 3.0;          // For ATR_BASED: TP = ATR * this
    double tp_range_mult = 0.5;        // For RANGE_BASED: TP = range * this
    double tp_inv_vol_base = 2.0;      // For INVERSE_VOL: base multiplier
    double tp_inv_vol_scale = 2.0;     // For INVERSE_VOL: scale factor
    int tp_adaptive_decay_ticks = 1000; // For ADAPTIVE_AGE: ticks until TP shrinks to min
    double tp_adaptive_min_mult = 0.5;  // For ADAPTIVE_AGE: minimum TP multiplier
};

// ============================================================================
// Results
// ============================================================================

struct Result {
    TPStrategy strategy;
    double return_pct;
    double max_dd;
    int total_trades;
    int winning_trades;
    double win_rate;
    double avg_hold_ticks;
    int positions_opened;
    double avg_tp_distance;
    double total_profit;
};

// ============================================================================
// Load Ticks using C-style I/O
// ============================================================================

std::vector<Tick> LoadTicks(const char* filename, size_t max_count) {
    std::vector<Tick> ticks;
    ticks.reserve(max_count);

    FILE* file = fopen(filename, "r");
    if (!file) {
        printf("ERROR: Cannot open file: %s\n", filename);
        return ticks;
    }

    // Skip header
    char line[256];
    if (!fgets(line, sizeof(line), file)) {
        fclose(file);
        return ticks;
    }

    // Parse data lines
    while (fgets(line, sizeof(line), file) && ticks.size() < max_count) {
        Tick tick;
        memset(&tick, 0, sizeof(tick));

        // Parse: Timestamp\tBid\tAsk\tVolume\tFlags
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

    fclose(file);
    return ticks;
}

// ============================================================================
// Calculate Dynamic TP
// ============================================================================

double CalculateTP(const Config& cfg, double entry_price, double spread,
                   double atr_short, double atr_long, double range,
                   size_t position_age_ticks) {
    double tp_distance = 0;

    switch (cfg.tp_strategy) {
        case TP_FIXED:
            // Simple fixed TP: spacing * multiplier + spread
            tp_distance = cfg.spacing * cfg.tp_fixed_mult;
            break;

        case TP_ATR_BASED:
            // TP based on current volatility (ATR)
            // Higher ATR = wider TP to capture bigger moves
            if (atr_short > 0) {
                tp_distance = atr_short * cfg.tp_atr_mult;
            } else {
                tp_distance = cfg.spacing * cfg.tp_fixed_mult;  // Fallback
            }
            // Minimum TP to cover spread
            tp_distance = std::max(tp_distance, spread * 2.0);
            break;

        case TP_RANGE_BASED:
            // TP based on recent price range
            // Larger range = wider TP
            if (range > 0) {
                tp_distance = range * cfg.tp_range_mult;
            } else {
                tp_distance = cfg.spacing * cfg.tp_fixed_mult;  // Fallback
            }
            tp_distance = std::max(tp_distance, spread * 2.0);
            break;

        case TP_INVERSE_VOL:
            // Counter-intuitive: WIDER TP when volatility is LOW
            // Logic: In low vol, price oscillates in tight range,
            //        so we need wider TP to catch the occasional bigger move
            // In high vol, we want tighter TP to lock in quick profits
            if (atr_short > 0 && atr_long > 0) {
                double vol_ratio = atr_short / atr_long;  // <1 = low vol, >1 = high vol
                // Low vol (0.5) -> mult = 2.0 + 2.0*(1-0.5) = 3.0 (wider)
                // High vol (1.5) -> mult = 2.0 + 2.0*(1-1.5) = 1.0 (tighter)
                double mult = cfg.tp_inv_vol_base + cfg.tp_inv_vol_scale * (1.0 - vol_ratio);
                mult = std::max(0.5, std::min(4.0, mult));  // Clamp 0.5x to 4x
                tp_distance = cfg.spacing * mult;
            } else {
                tp_distance = cfg.spacing * cfg.tp_fixed_mult;
            }
            break;

        case TP_ADAPTIVE_AGE:
            // TP shrinks as position ages
            // Fresh position: full TP target
            // Aging position: lower TP to "give up" on big move and take small profit
            {
                double age_ratio = (double)position_age_ticks / cfg.tp_adaptive_decay_ticks;
                age_ratio = std::min(1.0, age_ratio);  // Cap at 1.0

                // Interpolate: starts at 1.0x, decays to min_mult
                double mult = 1.0 - (1.0 - cfg.tp_adaptive_min_mult) * age_ratio;
                tp_distance = cfg.spacing * cfg.tp_fixed_mult * mult;
            }
            break;
    }

    return entry_price + spread + tp_distance;
}

// ============================================================================
// Run Backtest
// ============================================================================

Result RunBacktest(const std::vector<Tick>& ticks, Config cfg) {
    Result r;
    memset(&r, 0, sizeof(r));
    r.strategy = cfg.tp_strategy;

    if (ticks.empty()) return r;

    double balance = 10000.0;
    double equity = balance;
    double peak_equity = balance;

    std::vector<Trade*> positions;
    size_t next_id = 1;
    bool partial_done = false;
    bool all_closed = false;

    // V7 volatility filter
    ATR atr_short(cfg.atr_short_period);
    ATR atr_long(cfg.atr_long_period);

    // Range tracker for range-based TP
    RangeTracker range_tracker(200);

    // Statistics
    size_t total_hold_ticks = 0;
    double total_tp_distance = 0;
    int tp_count = 0;

    for (size_t tick_idx = 0; tick_idx < ticks.size(); tick_idx++) {
        const Tick& tick = ticks[tick_idx];

        // Update indicators
        atr_short.Add(tick.bid);
        atr_long.Add(tick.bid);
        range_tracker.Add(tick.bid);

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
        r.max_dd = std::max(r.max_dd, dd_pct);

        // V3 Protection: Close ALL
        if (dd_pct > cfg.close_all_at_dd && !all_closed && !positions.empty()) {
            for (Trade* t : positions) {
                double pl = (tick.bid - t->entry_price) * t->lot_size * cfg.contract_size;
                balance += pl;
                r.total_trades++;
                if (pl > 0) r.winning_trades++;
                total_hold_ticks += (tick_idx - t->entry_tick);
                delete t;
            }
            positions.clear();
            all_closed = true;
            equity = balance;
            peak_equity = equity;
            continue;
        }

        // V3 Protection: Partial close
        if (dd_pct > cfg.partial_close_at_dd && !partial_done && positions.size() > 1) {
            std::sort(positions.begin(), positions.end(), [&](Trade* a, Trade* b) {
                return (tick.bid - a->entry_price) < (tick.bid - b->entry_price);
            });
            int to_close = (int)(positions.size() * 0.5);
            to_close = std::max(1, to_close);
            for (int i = 0; i < to_close && !positions.empty(); i++) {
                Trade* t = positions[0];
                double pl = (tick.bid - t->entry_price) * t->lot_size * cfg.contract_size;
                balance += pl;
                r.total_trades++;
                if (pl > 0) r.winning_trades++;
                total_hold_ticks += (tick_idx - t->entry_tick);
                delete t;
                positions.erase(positions.begin());
            }
            partial_done = true;
        }

        // Update TP for adaptive strategy (position ages each tick)
        if (cfg.tp_strategy == TP_ADAPTIVE_AGE) {
            for (Trade* t : positions) {
                size_t age = tick_idx - t->entry_tick;
                t->take_profit = CalculateTP(cfg, t->entry_price, tick.spread(),
                                             atr_short.Get(), atr_long.Get(),
                                             range_tracker.GetRange(), age);
            }
        }

        // Check TP hits
        for (auto it = positions.begin(); it != positions.end();) {
            Trade* t = *it;
            if (tick.bid >= t->take_profit) {
                double pl = (t->take_profit - t->entry_price) * t->lot_size * cfg.contract_size;
                balance += pl;
                r.total_trades++;
                r.winning_trades++;
                total_hold_ticks += (tick_idx - t->entry_tick);
                total_tp_distance += (t->take_profit - t->entry_price);
                tp_count++;
                delete t;
                it = positions.erase(it);
            } else {
                ++it;
            }
        }

        // V7: Check volatility filter
        bool volatility_ok = true;
        if (atr_short.IsReady() && atr_long.IsReady() && atr_long.Get() > 0) {
            volatility_ok = atr_short.Get() < atr_long.Get() * cfg.volatility_threshold;
        }

        // Open new positions
        if (dd_pct < cfg.stop_new_at_dd && volatility_ok && (int)positions.size() < cfg.max_positions) {
            double lowest = DBL_MAX, highest = DBL_MIN;
            for (Trade* t : positions) {
                lowest = std::min(lowest, t->entry_price);
                highest = std::max(highest, t->entry_price);
            }

            bool should_open = positions.empty() ||
                              (lowest >= tick.ask + cfg.spacing) ||
                              (highest <= tick.ask - cfg.spacing);

            if (should_open) {
                // Check margin
                double used_margin = 0;
                for (Trade* t : positions) {
                    used_margin += t->lot_size * cfg.contract_size * t->entry_price / cfg.leverage;
                }
                double margin_needed = cfg.lot_size * cfg.contract_size * tick.ask / cfg.leverage;

                if (equity - used_margin > margin_needed * 2) {
                    Trade* t = new Trade();
                    t->id = next_id++;
                    t->entry_price = tick.ask;
                    t->lot_size = cfg.lot_size;
                    t->entry_tick = tick_idx;
                    t->take_profit = CalculateTP(cfg, tick.ask, tick.spread(),
                                                 atr_short.Get(), atr_long.Get(),
                                                 range_tracker.GetRange(), 0);
                    t->initial_tp = t->take_profit;
                    positions.push_back(t);
                    r.positions_opened++;
                }
            }
        }
    }

    // Close remaining positions at market
    for (Trade* t : positions) {
        double pl = (ticks.back().bid - t->entry_price) * t->lot_size * cfg.contract_size;
        balance += pl;
        r.total_trades++;
        if (pl > 0) r.winning_trades++;
        total_hold_ticks += (ticks.size() - 1 - t->entry_tick);
        delete t;
    }

    // Calculate final stats
    r.return_pct = (balance - 10000.0) / 10000.0 * 100.0;
    r.total_profit = balance - 10000.0;
    r.win_rate = (r.total_trades > 0) ? (double)r.winning_trades / r.total_trades * 100.0 : 0;
    r.avg_hold_ticks = (r.total_trades > 0) ? (double)total_hold_ticks / r.total_trades : 0;
    r.avg_tp_distance = (tp_count > 0) ? total_tp_distance / tp_count : 0;

    return r;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("============================================================\n");
    printf("DYNAMIC TAKE-PROFIT STRATEGY COMPARISON TEST\n");
    printf("============================================================\n\n");

    printf("Hypothesis:\n");
    printf("- TP should be wider in high volatility (more room to move)\n");
    printf("- TP should be tighter in low volatility (capture smaller moves)\n\n");

    printf("TP Strategies to Test:\n");
    printf("1. Fixed TP: spacing * 2.0 (baseline)\n");
    printf("2. ATR-based TP: TP = ATR_current * multiplier\n");
    printf("3. Range-based TP: TP = recent_range * multiplier\n");
    printf("4. Inverse volatility TP: Wider when vol low, tighter when high\n");
    printf("5. Adaptive TP: TP shrinks as position ages\n\n");

    // Load data
    const char* filename = "Grid/XAUUSD_TICKS_2025.csv";
    printf("Loading ticks from %s...\n", filename);

    std::vector<Tick> ticks = LoadTicks(filename, 500000);

    if (ticks.empty()) {
        printf("ERROR: Failed to load tick data!\n");
        return 1;
    }

    printf("Loaded %zu ticks\n", ticks.size());
    printf("Price range: %.2f - %.2f\n", ticks.front().bid, ticks.back().bid);
    printf("\n");

    // Run tests for each TP strategy
    std::vector<Result> results;

    Config base_cfg;
    base_cfg.spacing = 1.0;
    base_cfg.lot_size = 0.01;
    base_cfg.contract_size = 100.0;
    base_cfg.leverage = 500.0;
    base_cfg.max_positions = 20;
    base_cfg.stop_new_at_dd = 5.0;
    base_cfg.partial_close_at_dd = 8.0;
    base_cfg.close_all_at_dd = 25.0;
    base_cfg.atr_short_period = 100;
    base_cfg.atr_long_period = 500;
    base_cfg.volatility_threshold = 0.8;

    printf("Running backtests with V7 volatility filter...\n\n");

    // 1. Fixed TP (baseline)
    {
        Config cfg = base_cfg;
        cfg.tp_strategy = TP_FIXED;
        cfg.tp_fixed_mult = 2.0;
        printf("Testing: %s (mult=%.1f)...\n", TPStrategyName(cfg.tp_strategy), cfg.tp_fixed_mult);
        results.push_back(RunBacktest(ticks, cfg));
    }

    // 2. ATR-based TP - test multiple multipliers
    for (double mult : {2.0, 3.0, 4.0, 5.0}) {
        Config cfg = base_cfg;
        cfg.tp_strategy = TP_ATR_BASED;
        cfg.tp_atr_mult = mult;
        printf("Testing: %s (mult=%.1f)...\n", TPStrategyName(cfg.tp_strategy), mult);
        results.push_back(RunBacktest(ticks, cfg));
    }

    // 3. Range-based TP - test multiple multipliers
    for (double mult : {0.3, 0.5, 0.7, 1.0}) {
        Config cfg = base_cfg;
        cfg.tp_strategy = TP_RANGE_BASED;
        cfg.tp_range_mult = mult;
        printf("Testing: %s (mult=%.2f)...\n", TPStrategyName(cfg.tp_strategy), mult);
        results.push_back(RunBacktest(ticks, cfg));
    }

    // 4. Inverse volatility TP - test different base/scale
    for (double base : {1.5, 2.0, 2.5}) {
        Config cfg = base_cfg;
        cfg.tp_strategy = TP_INVERSE_VOL;
        cfg.tp_inv_vol_base = base;
        cfg.tp_inv_vol_scale = 2.0;
        printf("Testing: %s (base=%.1f, scale=2.0)...\n", TPStrategyName(cfg.tp_strategy), base);
        results.push_back(RunBacktest(ticks, cfg));
    }

    // 5. Adaptive TP - test different decay rates
    for (int decay : {500, 1000, 2000}) {
        Config cfg = base_cfg;
        cfg.tp_strategy = TP_ADAPTIVE_AGE;
        cfg.tp_adaptive_decay_ticks = decay;
        cfg.tp_adaptive_min_mult = 0.5;
        printf("Testing: %s (decay=%d ticks)...\n", TPStrategyName(cfg.tp_strategy), decay);
        results.push_back(RunBacktest(ticks, cfg));
    }

    // Sort by return
    std::sort(results.begin(), results.end(), [](const Result& a, const Result& b) {
        return a.return_pct > b.return_pct;
    });

    // Print results table
    printf("\n============================================================\n");
    printf("RESULTS (sorted by return)\n");
    printf("============================================================\n\n");

    printf("%-25s %8s %7s %7s %8s %9s %10s\n",
           "Strategy", "Return", "MaxDD", "WinRate", "Trades", "AvgHold", "AvgTP");
    printf("%-25s %8s %7s %7s %8s %9s %10s\n",
           "-------------------------", "--------", "-------", "-------", "--------", "---------", "----------");

    for (const Result& r : results) {
        char name[32];
        snprintf(name, sizeof(name), "%s", TPStrategyName(r.strategy));

        printf("%-25s %7.2f%% %6.2f%% %6.2f%% %8d %9.0f %10.2f\n",
               name, r.return_pct, r.max_dd, r.win_rate, r.total_trades,
               r.avg_hold_ticks, r.avg_tp_distance);
    }

    // Detailed breakdown by strategy type
    printf("\n============================================================\n");
    printf("DETAILED ANALYSIS BY STRATEGY TYPE\n");
    printf("============================================================\n\n");

    // Group and summarize
    printf("1. FIXED TP (Baseline):\n");
    printf("   - Simple, predictable behavior\n");
    printf("   - Does not adapt to market conditions\n\n");

    printf("2. ATR-BASED TP:\n");
    printf("   - Widens TP in high volatility periods\n");
    printf("   - Should capture larger moves when market is active\n");
    printf("   - Risk: May wait too long in volatile conditions\n\n");

    printf("3. RANGE-BASED TP:\n");
    printf("   - Uses recent price range to set TP\n");
    printf("   - Adapts to current market conditions\n");
    printf("   - Similar concept to ATR but uses absolute range\n\n");

    printf("4. INVERSE VOLATILITY TP:\n");
    printf("   - Counter-intuitive: WIDER TP when vol is low\n");
    printf("   - Theory: In calm markets, we need bigger targets\n");
    printf("           In volatile markets, take quick profits\n");
    printf("   - Tests if the hypothesis is correct or backwards\n\n");

    printf("5. ADAPTIVE AGE TP:\n");
    printf("   - TP shrinks as position ages\n");
    printf("   - Fresh trades: aim for full profit\n");
    printf("   - Old trades: accept smaller profit, cut losses\n");
    printf("   - Reduces holding time on stuck positions\n\n");

    // Find best performer
    const Result& best = results[0];
    printf("============================================================\n");
    printf("BEST PERFORMER: %s\n", TPStrategyName(best.strategy));
    printf("============================================================\n");
    printf("Return:    %.2f%%\n", best.return_pct);
    printf("Max DD:    %.2f%%\n", best.max_dd);
    printf("Win Rate:  %.2f%%\n", best.win_rate);
    printf("Trades:    %d\n", best.total_trades);
    printf("Avg Hold:  %.0f ticks\n", best.avg_hold_ticks);
    printf("Avg TP:    %.2f\n", best.avg_tp_distance);
    printf("\n");

    // Compare Fixed vs Best
    Result fixed_result;
    for (const Result& r : results) {
        if (r.strategy == TP_FIXED) {
            fixed_result = r;
            break;
        }
    }

    printf("============================================================\n");
    printf("COMPARISON: Best vs Fixed Baseline\n");
    printf("============================================================\n");
    printf("%-20s %15s %15s %15s\n", "Metric", "Fixed", "Best", "Improvement");
    printf("%-20s %15s %15s %15s\n", "--------------------", "---------------", "---------------", "---------------");
    printf("%-20s %14.2f%% %14.2f%% %+14.2f%%\n", "Return", fixed_result.return_pct, best.return_pct,
           best.return_pct - fixed_result.return_pct);
    printf("%-20s %14.2f%% %14.2f%% %+14.2f%%\n", "Max DD", fixed_result.max_dd, best.max_dd,
           fixed_result.max_dd - best.max_dd);  // Lower is better
    printf("%-20s %14.2f%% %14.2f%% %+14.2f%%\n", "Win Rate", fixed_result.win_rate, best.win_rate,
           best.win_rate - fixed_result.win_rate);
    printf("%-20s %14.0f %14.0f %+14.0f\n", "Avg Hold (ticks)", fixed_result.avg_hold_ticks, best.avg_hold_ticks,
           fixed_result.avg_hold_ticks - best.avg_hold_ticks);  // Lower may be better
    printf("\n");

    // Conclusion
    printf("============================================================\n");
    printf("CONCLUSIONS\n");
    printf("============================================================\n\n");

    if (best.strategy == TP_FIXED) {
        printf("RESULT: Fixed TP is optimal - dynamic TP does NOT improve results.\n");
        printf("\nPossible reasons:\n");
        printf("- V7 volatility filter already filters out bad periods\n");
        printf("- Grid strategy benefits from predictable TP levels\n");
        printf("- Dynamic TP adds complexity without benefit\n");
    } else {
        printf("RESULT: Dynamic TP improves results!\n");
        printf("Best strategy: %s\n\n", TPStrategyName(best.strategy));

        if (best.strategy == TP_ATR_BASED) {
            printf("ATR-based TP works because:\n");
            printf("- Adapts TP to current volatility conditions\n");
            printf("- Wider targets when market is moving\n");
        } else if (best.strategy == TP_RANGE_BASED) {
            printf("Range-based TP works because:\n");
            printf("- Uses actual price range to set realistic targets\n");
            printf("- Better alignment with current market behavior\n");
        } else if (best.strategy == TP_INVERSE_VOL) {
            printf("Inverse volatility TP works because:\n");
            printf("- Takes quick profits in volatile markets (avoid reversals)\n");
            printf("- Waits for bigger moves in calm markets (more reliable)\n");
        } else if (best.strategy == TP_ADAPTIVE_AGE) {
            printf("Adaptive age TP works because:\n");
            printf("- Reduces holding time on stuck positions\n");
            printf("- Accepts smaller wins rather than waiting forever\n");
        }
    }

    printf("\n============================================================\n");
    printf("TEST COMPLETE\n");
    printf("============================================================\n");

    return 0;
}
