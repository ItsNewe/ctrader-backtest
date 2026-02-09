/**
 * Bidirectional Grid Strategy Test
 *
 * Tests whether adding short positions improves market neutrality.
 *
 * Current V7 strategy only buys (goes long), which means:
 * - Profits when gold goes UP
 * - Loses when gold goes DOWN
 * - Directionally dependent on gold's upward movement
 *
 * Bidirectional grid strategies tested:
 * 1. Long only (baseline V7)
 * 2. Short only (mirror - only sells)
 * 3. Separate grids (independent buy and sell grids)
 * 4. Alternating (buy/sell based on price vs SMA)
 * 5. Hedged (roughly equal long/short exposure)
 *
 * Goal: Reduce reliance on gold going up, achieve market neutrality
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>
#include <cfloat>

// === DATA STRUCTURES ===

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
    double stop_loss;
    bool is_long;  // true = BUY, false = SELL
};

// Simple moving average tracker
struct SMA {
    double* prices;
    int period;
    int count;
    int head;
    double sum;

    SMA(int p) : period(p), count(0), head(0), sum(0) {
        prices = (double*)malloc(p * sizeof(double));
        memset(prices, 0, p * sizeof(double));
    }

    ~SMA() { free(prices); }

    void Add(double price) {
        if (count >= period) {
            sum -= prices[head];
        }
        prices[head] = price;
        sum += price;
        head = (head + 1) % period;
        if (count < period) count++;
    }

    double Get() const { return count > 0 ? sum / count : 0; }
    bool IsReady() const { return count >= period; }
};

// ATR tracker for volatility
struct ATR {
    double* ranges;
    int period;
    int count;
    int head;
    double sum;
    double last_price;

    ATR(int p) : period(p), count(0), head(0), sum(0), last_price(0) {
        ranges = (double*)malloc(p * sizeof(double));
        memset(ranges, 0, p * sizeof(double));
    }

    ~ATR() { free(ranges); }

    void Add(double price) {
        if (last_price > 0) {
            double range = fabs(price - last_price);
            if (count >= period) {
                sum -= ranges[head];
            }
            ranges[head] = range;
            sum += range;
            head = (head + 1) % period;
            if (count < period) count++;
        }
        last_price = price;
    }

    double Get() const { return count > 0 ? sum / count : 0; }
    bool IsReady() const { return count >= period; }
};

// === STRATEGY TYPES ===

enum StrategyType {
    LONG_ONLY,       // V7 baseline - only buys
    SHORT_ONLY,      // Mirror - only sells
    SEPARATE_GRIDS,  // Independent buy and sell grids
    ALTERNATING,     // Buy/sell based on price vs SMA
    HEDGED           // Equal long/short exposure
};

const char* StrategyName(StrategyType t) {
    switch (t) {
        case LONG_ONLY:      return "Long Only (V7)";
        case SHORT_ONLY:     return "Short Only";
        case SEPARATE_GRIDS: return "Separate Grids";
        case ALTERNATING:    return "Alternating";
        case HEDGED:         return "Hedged";
    }
    return "Unknown";
}

// === RESULTS ===

struct Result {
    StrategyType type;
    double return_pct;
    double max_dd;
    int total_trades;
    int long_trades;
    int short_trades;
    double avg_net_exposure;   // Average (long_lots - short_lots)
    double max_long_exposure;
    double max_short_exposure;
    double profit_from_longs;
    double profit_from_shorts;
    double directional_dependency;  // How much profit depends on price direction
};

// === LOAD TICKS (C-style file I/O) ===

std::vector<Tick> LoadTicks(const char* filename, size_t max_count) {
    std::vector<Tick> ticks;
    ticks.reserve(max_count);

    FILE* f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Cannot open file: %s\n", filename);
        return ticks;
    }

    // Skip header
    char line[256];
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return ticks;
    }

    while (fgets(line, sizeof(line), f) && ticks.size() < max_count) {
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

    fclose(f);
    return ticks;
}

// === RUN BACKTEST ===

Result RunBacktest(const std::vector<Tick>& ticks, StrategyType strategy_type) {
    Result r;
    memset(&r, 0, sizeof(r));
    r.type = strategy_type;

    if (ticks.empty()) return r;

    // Configuration
    const double initial_balance = 10000.0;
    const double contract_size = 100.0;
    const double leverage = 500.0;
    const double spacing = 1.0;
    const double lot_size = 0.01;
    const int max_positions = 20;  // per side for bidirectional
    const int sma_period = 1000;
    const int atr_short = 100;
    const int atr_long = 500;
    const double vol_threshold = 0.8;

    double balance = initial_balance;
    double equity = balance;
    double peak_equity = balance;
    double max_drawdown = 0;

    std::vector<Trade*> long_positions;
    std::vector<Trade*> short_positions;
    size_t next_id = 1;

    // Tracking
    double total_net_exposure = 0;
    double exposure_samples = 0;
    double profit_from_longs = 0;
    double profit_from_shorts = 0;
    int long_trades = 0;
    int short_trades = 0;

    // Indicators
    SMA sma(sma_period);
    ATR atr_s(atr_short);
    ATR atr_l(atr_long);

    // Protection state
    bool partial_done_long = false;
    bool partial_done_short = false;
    bool all_closed = false;

    // Grid tracking
    double lowest_long = DBL_MAX;
    double highest_long = -DBL_MAX;
    double lowest_short = DBL_MAX;
    double highest_short = -DBL_MAX;

    for (const Tick& tick : ticks) {
        sma.Add(tick.bid);
        atr_s.Add(tick.bid);
        atr_l.Add(tick.bid);

        // Calculate equity
        equity = balance;
        double long_lots = 0, short_lots = 0;

        for (Trade* t : long_positions) {
            double pl = (tick.bid - t->entry_price) * t->lot_size * contract_size;
            equity += pl;
            long_lots += t->lot_size;
        }
        for (Trade* t : short_positions) {
            double pl = (t->entry_price - tick.ask) * t->lot_size * contract_size;
            equity += pl;
            short_lots += t->lot_size;
        }

        // Track exposure
        double net_exposure = long_lots - short_lots;
        total_net_exposure += net_exposure;
        exposure_samples++;

        if (long_lots > r.max_long_exposure) r.max_long_exposure = long_lots;
        if (short_lots > r.max_short_exposure) r.max_short_exposure = short_lots;

        // Reset peak when all positions closed
        if (long_positions.empty() && short_positions.empty()) {
            if (peak_equity != balance) {
                peak_equity = balance;
                partial_done_long = false;
                partial_done_short = false;
                all_closed = false;
            }
        }

        if (equity > peak_equity) {
            peak_equity = equity;
            partial_done_long = false;
            partial_done_short = false;
            all_closed = false;
        }

        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        if (dd_pct > max_drawdown) max_drawdown = dd_pct;

        // === V3 PROTECTION ===

        // Close ALL at 25% DD
        if (dd_pct > 25.0 && !all_closed && (!long_positions.empty() || !short_positions.empty())) {
            for (Trade* t : long_positions) {
                double pl = (tick.bid - t->entry_price) * t->lot_size * contract_size;
                balance += pl;
                profit_from_longs += pl;
                long_trades++;
                delete t;
            }
            long_positions.clear();

            for (Trade* t : short_positions) {
                double pl = (t->entry_price - tick.ask) * t->lot_size * contract_size;
                balance += pl;
                profit_from_shorts += pl;
                short_trades++;
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

        // Partial close at 8% DD (long positions)
        if (dd_pct > 8.0 && !partial_done_long && long_positions.size() > 1) {
            std::sort(long_positions.begin(), long_positions.end(), [&](Trade* a, Trade* b) {
                return (tick.bid - a->entry_price) < (tick.bid - b->entry_price);
            });
            int to_close = std::max(1, (int)(long_positions.size() * 0.5));
            for (int i = 0; i < to_close && !long_positions.empty(); i++) {
                Trade* t = long_positions[0];
                double pl = (tick.bid - t->entry_price) * t->lot_size * contract_size;
                balance += pl;
                profit_from_longs += pl;
                long_trades++;
                delete t;
                long_positions.erase(long_positions.begin());
            }
            partial_done_long = true;
        }

        // Partial close at 8% DD (short positions)
        if (dd_pct > 8.0 && !partial_done_short && short_positions.size() > 1) {
            std::sort(short_positions.begin(), short_positions.end(), [&](Trade* a, Trade* b) {
                return (a->entry_price - tick.ask) < (b->entry_price - tick.ask);
            });
            int to_close = std::max(1, (int)(short_positions.size() * 0.5));
            for (int i = 0; i < to_close && !short_positions.empty(); i++) {
                Trade* t = short_positions[0];
                double pl = (t->entry_price - tick.ask) * t->lot_size * contract_size;
                balance += pl;
                profit_from_shorts += pl;
                short_trades++;
                delete t;
                short_positions.erase(short_positions.begin());
            }
            partial_done_short = true;
        }

        // === TAKE PROFIT CHECK ===

        // Long positions TP
        for (auto it = long_positions.begin(); it != long_positions.end();) {
            Trade* t = *it;
            if (tick.bid >= t->take_profit) {
                double pl = (tick.bid - t->entry_price) * t->lot_size * contract_size;
                balance += pl;
                profit_from_longs += pl;
                long_trades++;
                delete t;
                it = long_positions.erase(it);
            } else {
                ++it;
            }
        }

        // Short positions TP
        for (auto it = short_positions.begin(); it != short_positions.end();) {
            Trade* t = *it;
            if (tick.ask <= t->take_profit) {
                double pl = (t->entry_price - tick.ask) * t->lot_size * contract_size;
                balance += pl;
                profit_from_shorts += pl;
                short_trades++;
                delete t;
                it = short_positions.erase(it);
            } else {
                ++it;
            }
        }

        // === VOLATILITY FILTER (V7) ===
        bool volatility_ok = true;
        if (atr_s.IsReady() && atr_l.IsReady() && atr_l.Get() > 0) {
            volatility_ok = atr_s.Get() < atr_l.Get() * vol_threshold;
        }

        // Don't open new if DD too high or volatility too high
        if (dd_pct >= 5.0 || !volatility_ok) {
            continue;
        }

        // === OPENING LOGIC (strategy-specific) ===

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

        bool open_long = false;
        bool open_short = false;

        switch (strategy_type) {
            case LONG_ONLY:
                // Original V7: only opens longs
                if ((int)long_positions.size() < max_positions) {
                    if (long_positions.empty()) {
                        open_long = true;
                    } else if (lowest_long >= tick.ask + spacing) {
                        open_long = true;  // Price dropped, buy lower
                    } else if (highest_long <= tick.ask - spacing) {
                        open_long = true;  // Price rose, fill gap
                    }
                }
                break;

            case SHORT_ONLY:
                // Mirror of V7: only opens shorts
                if ((int)short_positions.size() < max_positions) {
                    if (short_positions.empty()) {
                        open_short = true;
                    } else if (highest_short <= tick.bid - spacing) {
                        open_short = true;  // Price rose, sell higher
                    } else if (lowest_short >= tick.bid + spacing) {
                        open_short = true;  // Price dropped, fill gap
                    }
                }
                break;

            case SEPARATE_GRIDS:
                // Independent buy and sell grids
                // LONG grid: buy when price drops (mean reversion up)
                if ((int)long_positions.size() < max_positions) {
                    if (long_positions.empty()) {
                        open_long = true;
                    } else if (lowest_long >= tick.ask + spacing) {
                        open_long = true;  // Price dropped
                    } else if (highest_long <= tick.ask - spacing) {
                        open_long = true;
                    }
                }
                // SHORT grid: sell when price rises (mean reversion down)
                if ((int)short_positions.size() < max_positions) {
                    if (short_positions.empty()) {
                        open_short = true;
                    } else if (highest_short <= tick.bid - spacing) {
                        open_short = true;  // Price rose
                    } else if (lowest_short >= tick.bid + spacing) {
                        open_short = true;
                    }
                }
                break;

            case ALTERNATING:
                // Buy when price below SMA, sell when above
                if (sma.IsReady()) {
                    double sma_val = sma.Get();
                    if (tick.bid < sma_val) {
                        // Below SMA: expect mean reversion up, go long
                        if ((int)long_positions.size() < max_positions) {
                            if (long_positions.empty()) {
                                open_long = true;
                            } else if (lowest_long >= tick.ask + spacing) {
                                open_long = true;
                            }
                        }
                    } else {
                        // Above SMA: expect mean reversion down, go short
                        if ((int)short_positions.size() < max_positions) {
                            if (short_positions.empty()) {
                                open_short = true;
                            } else if (highest_short <= tick.bid - spacing) {
                                open_short = true;
                            }
                        }
                    }
                } else {
                    // SMA not ready, default to long only
                    if ((int)long_positions.size() < max_positions && long_positions.empty()) {
                        open_long = true;
                    }
                }
                break;

            case HEDGED:
                // Try to maintain equal long/short exposure
                // Open whichever side has less exposure
                {
                    double imbalance = long_lots - short_lots;
                    // Threshold to trigger hedging
                    double hedge_threshold = 0.02;  // Open opposite when imbalance > 0.02 lots

                    if (imbalance > hedge_threshold && (int)short_positions.size() < max_positions) {
                        // More long than short, open short
                        if (short_positions.empty()) {
                            open_short = true;
                        } else if (highest_short <= tick.bid - spacing) {
                            open_short = true;
                        }
                    } else if (imbalance < -hedge_threshold && (int)long_positions.size() < max_positions) {
                        // More short than long, open long
                        if (long_positions.empty()) {
                            open_long = true;
                        } else if (lowest_long >= tick.ask + spacing) {
                            open_long = true;
                        }
                    } else {
                        // Balanced or near balanced, can open either based on grid
                        if ((int)long_positions.size() < max_positions) {
                            if (long_positions.empty()) {
                                open_long = true;
                            } else if (lowest_long >= tick.ask + spacing) {
                                open_long = true;
                            }
                        }
                        if ((int)short_positions.size() < max_positions) {
                            if (short_positions.empty()) {
                                open_short = true;
                            } else if (highest_short <= tick.bid - spacing) {
                                open_short = true;
                            }
                        }
                    }
                }
                break;
        }

        // === EXECUTE OPENS ===

        if (open_long) {
            Trade* t = new Trade();
            t->id = next_id++;
            t->entry_price = tick.ask;
            t->lot_size = lot_size;
            t->take_profit = tick.ask + tick.spread() + spacing;
            t->stop_loss = 0;
            t->is_long = true;
            long_positions.push_back(t);
        }

        if (open_short) {
            Trade* t = new Trade();
            t->id = next_id++;
            t->entry_price = tick.bid;
            t->lot_size = lot_size;
            t->take_profit = tick.bid - tick.spread() - spacing;
            t->stop_loss = 0;
            t->is_long = false;
            short_positions.push_back(t);
        }
    }

    // === CLOSE REMAINING POSITIONS ===

    for (Trade* t : long_positions) {
        double pl = (ticks.back().bid - t->entry_price) * t->lot_size * contract_size;
        balance += pl;
        profit_from_longs += pl;
        long_trades++;
        delete t;
    }
    for (Trade* t : short_positions) {
        double pl = (t->entry_price - ticks.back().ask) * t->lot_size * contract_size;
        balance += pl;
        profit_from_shorts += pl;
        short_trades++;
        delete t;
    }

    // === CALCULATE RESULTS ===

    r.return_pct = (balance - initial_balance) / initial_balance * 100.0;
    r.max_dd = max_drawdown;
    r.total_trades = long_trades + short_trades;
    r.long_trades = long_trades;
    r.short_trades = short_trades;
    r.avg_net_exposure = exposure_samples > 0 ? total_net_exposure / exposure_samples : 0;
    r.profit_from_longs = profit_from_longs;
    r.profit_from_shorts = profit_from_shorts;

    // Directional dependency: how much of profit comes from one direction
    double total_profit = profit_from_longs + profit_from_shorts;
    if (total_profit > 0) {
        double long_share = fabs(profit_from_longs) / (fabs(profit_from_longs) + fabs(profit_from_shorts));
        double short_share = 1.0 - long_share;
        // Dependency = how skewed towards one direction (0 = balanced, 1 = completely one-sided)
        r.directional_dependency = fabs(long_share - short_share);
    } else {
        r.directional_dependency = 1.0;  // All losses, fully dependent
    }

    return r;
}

// === MAIN ===

int main() {
    printf("=====================================================\n");
    printf("BIDIRECTIONAL GRID STRATEGY TEST\n");
    printf("=====================================================\n\n");

    printf("Testing if adding short positions improves market neutrality.\n");
    printf("Current V7 only goes long (profits when gold rises).\n\n");

    printf("Strategies tested:\n");
    printf("1. Long Only (V7):   Only buys - baseline\n");
    printf("2. Short Only:       Only sells - mirror of V7\n");
    printf("3. Separate Grids:   Independent buy AND sell grids\n");
    printf("4. Alternating:      Buy below SMA, sell above SMA\n");
    printf("5. Hedged:           Maintain equal long/short exposure\n\n");

    // Load ticks
    const char* filename = "Grid/XAUUSD_TICKS_2025.csv";
    printf("Loading 500,000 ticks from %s...\n", filename);

    std::vector<Tick> ticks = LoadTicks(filename, 500000);

    if (ticks.empty()) {
        fprintf(stderr, "Failed to load ticks\n");
        return 1;
    }

    printf("Loaded %zu ticks\n\n", ticks.size());

    // Price movement analysis
    double start_price = ticks.front().bid;
    double end_price = ticks.back().bid;
    double price_change_pct = (end_price - start_price) / start_price * 100.0;

    printf("Price movement: %.2f -> %.2f (%+.2f%%)\n", start_price, end_price, price_change_pct);
    if (price_change_pct > 0) {
        printf("  -> Gold went UP in this period\n");
    } else {
        printf("  -> Gold went DOWN in this period\n");
    }
    printf("\n");

    // Run all strategies
    printf("Running backtests...\n\n");

    std::vector<Result> results;
    StrategyType strategies[] = {LONG_ONLY, SHORT_ONLY, SEPARATE_GRIDS, ALTERNATING, HEDGED};

    for (StrategyType st : strategies) {
        printf("  Testing %s...\n", StrategyName(st));
        Result r = RunBacktest(ticks, st);
        results.push_back(r);
    }

    // === RESULTS TABLE ===

    printf("\n=====================================================\n");
    printf("RESULTS COMPARISON\n");
    printf("=====================================================\n\n");

    // Header
    printf("%-20s %8s %8s %8s %8s %8s\n",
           "Strategy", "Return", "Max DD", "Trades", "L/S", "Dir Dep");
    printf("%-20s %8s %8s %8s %8s %8s\n",
           "", "(%)", "(%)", "", "Ratio", "(0-1)");
    printf("---------------------------------------------------------------------\n");

    for (const Result& r : results) {
        double ls_ratio = r.short_trades > 0 ? (double)r.long_trades / r.short_trades : 999.9;
        if (ls_ratio > 999) ls_ratio = 999.9;

        printf("%-20s %+7.2f%% %7.2f%% %8d %7.1f:1 %8.2f\n",
               StrategyName(r.type),
               r.return_pct,
               r.max_dd,
               r.total_trades,
               ls_ratio,
               r.directional_dependency);
    }

    printf("---------------------------------------------------------------------\n\n");

    // === DETAILED ANALYSIS ===

    printf("=====================================================\n");
    printf("DETAILED BREAKDOWN\n");
    printf("=====================================================\n\n");

    printf("%-20s %10s %10s %12s %12s\n",
           "Strategy", "Long P/L", "Short P/L", "Avg Net Exp", "Max L/S Exp");
    printf("---------------------------------------------------------------------\n");

    for (const Result& r : results) {
        printf("%-20s $%+8.2f $%+8.2f %+11.3f   %.2f/%.2f\n",
               StrategyName(r.type),
               r.profit_from_longs,
               r.profit_from_shorts,
               r.avg_net_exposure,
               r.max_long_exposure,
               r.max_short_exposure);
    }

    printf("---------------------------------------------------------------------\n\n");

    // === MARKET NEUTRALITY ANALYSIS ===

    printf("=====================================================\n");
    printf("MARKET NEUTRALITY ANALYSIS\n");
    printf("=====================================================\n\n");

    printf("Directional Dependency Score (0 = neutral, 1 = fully directional):\n\n");

    for (const Result& r : results) {
        printf("  %-20s: %.2f ", StrategyName(r.type), r.directional_dependency);
        if (r.directional_dependency < 0.3) {
            printf("[MARKET NEUTRAL]\n");
        } else if (r.directional_dependency < 0.6) {
            printf("[MODERATELY DEPENDENT]\n");
        } else {
            printf("[DIRECTIONALLY DEPENDENT]\n");
        }
    }

    printf("\n");

    // Find most neutral profitable strategy
    Result* best_neutral = nullptr;
    double best_neutral_score = DBL_MAX;

    for (Result& r : results) {
        if (r.return_pct > 0 && r.directional_dependency < best_neutral_score) {
            best_neutral_score = r.directional_dependency;
            best_neutral = &r;
        }
    }

    // Find most profitable
    Result* best_profit = &results[0];
    for (Result& r : results) {
        if (r.return_pct > best_profit->return_pct) {
            best_profit = &r;
        }
    }

    // === CONCLUSION ===

    printf("=====================================================\n");
    printf("CONCLUSION\n");
    printf("=====================================================\n\n");

    printf("Most Profitable:     %s (%+.2f%%)\n",
           StrategyName(best_profit->type), best_profit->return_pct);

    if (best_neutral) {
        printf("Most Market-Neutral: %s (dep: %.2f)\n",
               StrategyName(best_neutral->type), best_neutral->directional_dependency);
    }

    printf("\n");

    // Analysis based on results
    printf("Key Findings:\n\n");

    // Compare long-only vs short-only
    Result& long_only = results[0];
    Result& short_only = results[1];

    if (price_change_pct > 0) {
        printf("1. Gold went UP %.2f%% in this period:\n", price_change_pct);
        if (long_only.return_pct > short_only.return_pct) {
            printf("   -> Long-only outperformed short-only (as expected)\n");
            printf("   -> Long: %+.2f%%, Short: %+.2f%%\n",
                   long_only.return_pct, short_only.return_pct);
        }
    } else {
        printf("1. Gold went DOWN %.2f%% in this period:\n", fabs(price_change_pct));
        if (short_only.return_pct > long_only.return_pct) {
            printf("   -> Short-only outperformed long-only (as expected)\n");
            printf("   -> Short: %+.2f%%, Long: %+.2f%%\n",
                   short_only.return_pct, long_only.return_pct);
        }
    }
    printf("\n");

    // Analyze bidirectional strategies
    Result& separate = results[2];
    Result& alternating = results[3];
    Result& hedged = results[4];

    printf("2. Bidirectional Strategies Analysis:\n\n");

    printf("   Separate Grids: %+.2f%% return, %.2f dir dependency\n",
           separate.return_pct, separate.directional_dependency);
    printf("   -> Runs independent long and short grids\n");
    printf("   -> Long P/L: $%+.2f, Short P/L: $%+.2f\n\n",
           separate.profit_from_longs, separate.profit_from_shorts);

    printf("   Alternating: %+.2f%% return, %.2f dir dependency\n",
           alternating.return_pct, alternating.directional_dependency);
    printf("   -> Buys below SMA, sells above SMA\n");
    printf("   -> Long P/L: $%+.2f, Short P/L: $%+.2f\n\n",
           alternating.profit_from_longs, alternating.profit_from_shorts);

    printf("   Hedged: %+.2f%% return, %.2f dir dependency\n",
           hedged.return_pct, hedged.directional_dependency);
    printf("   -> Maintains equal long/short exposure\n");
    printf("   -> Long P/L: $%+.2f, Short P/L: $%+.2f\n\n",
           hedged.profit_from_longs, hedged.profit_from_shorts);

    // Final recommendation
    printf("3. Recommendation:\n\n");

    bool bidirectional_better = false;
    for (int i = 2; i < 5; i++) {
        if (results[i].return_pct > long_only.return_pct &&
            results[i].directional_dependency < long_only.directional_dependency) {
            bidirectional_better = true;
        }
    }

    if (bidirectional_better) {
        printf("   [YES] Bidirectional is MORE market-neutral AND profitable!\n");
        printf("   -> Consider switching to a bidirectional approach\n");
    } else {
        // Check if any bidirectional is more neutral even if less profitable
        bool more_neutral = false;
        for (int i = 2; i < 5; i++) {
            if (results[i].directional_dependency < long_only.directional_dependency * 0.7 &&
                results[i].return_pct > 0) {
                more_neutral = true;
                printf("   [PARTIAL] %s is more market-neutral\n", StrategyName(results[i].type));
                printf("             but with lower returns (%+.2f%% vs %+.2f%%)\n",
                       results[i].return_pct, long_only.return_pct);
            }
        }

        if (!more_neutral) {
            printf("   [NO] Bidirectional strategies don't clearly outperform\n");
            printf("   -> The directional (long-only) V7 remains competitive\n");
            printf("   -> Grid strategies may naturally favor long positions in this asset\n");
        }
    }

    printf("\n=====================================================\n");
    printf("SEGMENT ANALYSIS (Market Direction Sensitivity)\n");
    printf("=====================================================\n\n");

    // Split ticks into segments and analyze each
    size_t segment_size = ticks.size() / 5;

    printf("Analyzing 5 segments to see performance in different conditions:\n\n");

    printf("%-8s %10s %12s %12s %12s %12s\n",
           "Segment", "Price Chg", "Long Only", "Separate", "Alternating", "Hedged");
    printf("-------------------------------------------------------------------------------\n");

    for (int seg = 0; seg < 5; seg++) {
        size_t start = seg * segment_size;
        size_t end = (seg == 4) ? ticks.size() : (seg + 1) * segment_size;

        std::vector<Tick> segment_ticks(ticks.begin() + start, ticks.begin() + end);

        double seg_start = segment_ticks.front().bid;
        double seg_end = segment_ticks.back().bid;
        double seg_change = (seg_end - seg_start) / seg_start * 100.0;

        Result r_long = RunBacktest(segment_ticks, LONG_ONLY);
        Result r_sep = RunBacktest(segment_ticks, SEPARATE_GRIDS);
        Result r_alt = RunBacktest(segment_ticks, ALTERNATING);
        Result r_hedge = RunBacktest(segment_ticks, HEDGED);

        printf("  %d      %+7.2f%%   %+9.2f%%   %+9.2f%%   %+9.2f%%   %+9.2f%%\n",
               seg + 1, seg_change, r_long.return_pct, r_sep.return_pct,
               r_alt.return_pct, r_hedge.return_pct);
    }

    printf("-------------------------------------------------------------------------------\n\n");

    printf("Legend:\n");
    printf("  - Positive price change = gold went UP\n");
    printf("  - Negative price change = gold went DOWN\n");
    printf("  - Better bidirectional performance in DOWN segments = more market-neutral\n");

    printf("\n=====================================================\n");
    printf("TEST COMPLETE\n");
    printf("=====================================================\n");

    return 0;
}
