/**
 * Adaptive Lot Sizing Test for V7 Strategy
 *
 * Goal: Test if adaptive lot sizing can reduce drawdown while maintaining returns.
 * Target: Reduce max DD below 15% while preserving profitability.
 *
 * Lot Sizing Strategies:
 * a) Fixed: Always 0.01 lots (baseline)
 * b) DD-based: Reduce lot size as DD increases
 *    - 0.01 at 0% DD
 *    - 0.005 at 2.5% DD
 *    - 0.0025 at 5% DD
 * c) Volatility-based: Smaller lots when ATR is higher (inverse relationship)
 * d) Kelly-based: Size based on win rate and avg win/loss ratio
 *
 * Uses V7 with optimized params: atr_short=50, atr_long=1000, vol_threshold=0.6, tp_mult=2.0
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <deque>
#include <algorithm>
#include <cfloat>

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
};

// ============================================================================
// ATR Calculator
// ============================================================================

class ATR {
    std::deque<double> ranges;
    int period;
    double sum;
    double last_price;
public:
    ATR(int p) : period(p), sum(0), last_price(0) {}

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
    double GetNormalized(double base_atr) const {
        if (base_atr <= 0) return 1.0;
        return Get() / base_atr;
    }
};

// ============================================================================
// Lot Sizing Strategies
// ============================================================================

enum SizingStrategy {
    FIXED,           // Always 0.01
    DD_BASED,        // Reduce as DD increases
    VOLATILITY,      // Inverse to ATR
    KELLY            // Based on historical win rate
};

const char* GetStrategyName(SizingStrategy s) {
    switch (s) {
        case FIXED: return "Fixed (0.01)";
        case DD_BASED: return "DD-Based";
        case VOLATILITY: return "Volatility-Based";
        case KELLY: return "Kelly Criterion";
    }
    return "Unknown";
}

// Calculate lot size based on drawdown
// 0% DD -> 0.01, 2.5% DD -> 0.005, 5%+ DD -> 0.0025
double CalculateDDBasedLot(double dd_pct) {
    if (dd_pct <= 0) return 0.01;
    if (dd_pct <= 2.5) {
        // Linear interpolation: 0.01 at 0%, 0.005 at 2.5%
        return 0.01 - (dd_pct / 2.5) * 0.005;
    }
    if (dd_pct <= 5.0) {
        // Linear interpolation: 0.005 at 2.5%, 0.0025 at 5%
        return 0.005 - ((dd_pct - 2.5) / 2.5) * 0.0025;
    }
    return 0.0025;  // Minimum at 5%+ DD
}

// Calculate lot size based on volatility (inverse relationship)
// Higher ATR -> smaller lots
double CalculateVolatilityLot(double atr_short, double atr_long, double base_lot) {
    if (atr_long <= 0) return base_lot;
    double ratio = atr_short / atr_long;

    // ratio < 1 means low volatility -> can use larger lots
    // ratio > 1 means high volatility -> use smaller lots
    // Scale: ratio 0.5 -> 1.5x lots, ratio 1.0 -> 1.0x lots, ratio 1.5 -> 0.67x lots
    double multiplier = 1.0 / ratio;
    multiplier = fmax(0.25, fmin(2.0, multiplier));  // Clamp to [0.25, 2.0]

    double lot = base_lot * multiplier;
    // Round to 0.01 increments, minimum 0.01
    lot = floor(lot * 100 + 0.5) / 100;
    return fmax(0.01, lot);
}

// Calculate lot size using Kelly Criterion
// f* = (p * b - q) / b where p = win rate, b = win/loss ratio, q = 1-p
double CalculateKellyLot(double win_rate, double avg_win, double avg_loss, double base_lot, double balance) {
    if (win_rate <= 0 || avg_loss >= 0) return base_lot;

    double p = win_rate;
    double q = 1.0 - p;
    double b = fabs(avg_win / avg_loss);  // Win/loss ratio

    double kelly = (p * b - q) / b;

    // Fractional Kelly (use 25% of Kelly for safety)
    kelly *= 0.25;

    // Clamp Kelly between 0.01% and 1% of balance per $100 contract value
    kelly = fmax(0.0001, fmin(0.01, kelly));

    // Convert to lots (assuming $100 contract size for XAUUSD)
    // Risk = kelly * balance, lot = risk / (potential_loss_per_lot)
    // Simplified: scale base_lot by kelly factor
    double multiplier = kelly * 100;  // Scale factor
    multiplier = fmax(0.5, fmin(2.0, multiplier));

    double lot = base_lot * multiplier;
    lot = floor(lot * 100 + 0.5) / 100;
    return fmax(0.01, lot);
}

// ============================================================================
// Test Results
// ============================================================================

struct TestResult {
    SizingStrategy strategy;
    double return_pct;
    double max_dd_pct;
    double risk_adjusted_score;  // return / max_dd
    int total_trades;
    int positions_opened;
    double final_balance;
    double avg_lot_size;
    double min_lot_used;
    double max_lot_used;
};

// ============================================================================
// Load Ticks using C-style I/O
// ============================================================================

std::vector<Tick> LoadTicks(const char* filename, size_t max_ticks) {
    std::vector<Tick> ticks;
    ticks.reserve(max_ticks);

    FILE* file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "ERROR: Cannot open file: %s\n", filename);
        return ticks;
    }

    // Skip header line
    char line[256];
    if (fgets(line, sizeof(line), file) == NULL) {
        fclose(file);
        return ticks;
    }

    // Parse tick data (tab-separated: timestamp, bid, ask, volume)
    while (fgets(line, sizeof(line), file) && ticks.size() < max_ticks) {
        Tick tick;
        memset(tick.timestamp, 0, sizeof(tick.timestamp));

        char* token = strtok(line, "\t");
        if (!token) continue;
        strncpy(tick.timestamp, token, 31);

        token = strtok(NULL, "\t");
        if (!token) continue;
        tick.bid = atof(token);

        token = strtok(NULL, "\t");
        if (!token) continue;
        tick.ask = atof(token);

        if (tick.bid > 0 && tick.ask > 0) {
            ticks.push_back(tick);
        }

        if (ticks.size() % 1000000 == 0) {
            printf("  Loaded %zu ticks...\n", ticks.size());
        }
    }

    fclose(file);
    return ticks;
}

// ============================================================================
// Run Backtest with Specific Sizing Strategy
// ============================================================================

TestResult RunBacktest(const std::vector<Tick>& ticks, SizingStrategy sizing_strategy,
                        int atr_short_period, int atr_long_period,
                        double vol_threshold, double tp_multiplier) {
    TestResult result;
    memset(&result, 0, sizeof(result));
    result.strategy = sizing_strategy;
    result.min_lot_used = DBL_MAX;

    if (ticks.empty()) return result;

    // Trading parameters
    const double initial_balance = 10000.0;
    const double contract_size = 100.0;
    const double leverage = 500.0;
    const double spacing = 1.0;
    const double base_lot = 0.01;
    const int max_positions = 20;

    // V3 protection levels
    const double stop_new_at_dd = 5.0;
    const double partial_close_at_dd = 8.0;
    const double close_all_at_dd = 25.0;

    double balance = initial_balance;
    double equity = initial_balance;
    double peak_equity = initial_balance;
    double max_dd_pct = 0.0;

    std::vector<Trade*> positions;
    size_t next_id = 1;
    bool partial_done = false;
    bool all_closed = false;

    // V7 ATR filters
    ATR atr_short(atr_short_period);
    ATR atr_long(atr_long_period);

    // Statistics for Kelly calculation
    int wins = 0;
    int losses = 0;
    double total_win_amount = 0.0;
    double total_loss_amount = 0.0;
    double total_lot_used = 0.0;
    int lot_count = 0;

    for (size_t i = 0; i < ticks.size(); i++) {
        const Tick& tick = ticks[i];

        // Update ATR
        atr_short.Add(tick.bid);
        atr_long.Add(tick.bid);

        // Calculate equity
        equity = balance;
        for (Trade* t : positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * contract_size;
        }

        // Reset peak when flat
        if (positions.empty() && peak_equity != balance) {
            peak_equity = balance;
            partial_done = false;
            all_closed = false;
        }

        // Update peak
        if (equity > peak_equity) {
            peak_equity = equity;
            partial_done = false;
            all_closed = false;
        }

        // Calculate drawdown
        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        max_dd_pct = fmax(max_dd_pct, dd_pct);

        // V3 Protection: Close ALL at 25% DD
        if (dd_pct > close_all_at_dd && !all_closed && !positions.empty()) {
            for (Trade* t : positions) {
                double pl = (tick.bid - t->entry_price) * t->lot_size * contract_size;
                balance += pl;
                if (pl > 0) {
                    wins++;
                    total_win_amount += pl;
                } else {
                    losses++;
                    total_loss_amount += pl;
                }
                result.total_trades++;
                delete t;
            }
            positions.clear();
            all_closed = true;
            equity = balance;
            peak_equity = equity;
            continue;
        }

        // V3 Protection: Partial close at 8% DD
        if (dd_pct > partial_close_at_dd && !partial_done && positions.size() > 1) {
            std::sort(positions.begin(), positions.end(), [&](Trade* a, Trade* b) {
                return (tick.bid - a->entry_price) < (tick.bid - b->entry_price);
            });
            int to_close = (int)(positions.size() * 0.5);
            to_close = std::max(1, to_close);
            for (int j = 0; j < to_close && !positions.empty(); j++) {
                Trade* t = positions[0];
                double pl = (tick.bid - t->entry_price) * t->lot_size * contract_size;
                balance += pl;
                if (pl > 0) {
                    wins++;
                    total_win_amount += pl;
                } else {
                    losses++;
                    total_loss_amount += pl;
                }
                result.total_trades++;
                delete t;
                positions.erase(positions.begin());
            }
            partial_done = true;
        }

        // Process TPs
        for (auto it = positions.begin(); it != positions.end();) {
            Trade* t = *it;
            if (tick.bid >= t->take_profit) {
                double pl = (t->take_profit - t->entry_price) * t->lot_size * contract_size;
                balance += pl;
                wins++;
                total_win_amount += pl;
                result.total_trades++;
                delete t;
                it = positions.erase(it);
            } else {
                ++it;
            }
        }

        // V7 Volatility filter check
        bool volatility_ok = true;
        if (atr_short.IsReady() && atr_long.IsReady() && atr_long.Get() > 0) {
            volatility_ok = atr_short.Get() < atr_long.Get() * vol_threshold;
        }

        // Open new positions
        if (dd_pct < stop_new_at_dd && volatility_ok && (int)positions.size() < max_positions) {
            double lowest = DBL_MAX, highest = DBL_MIN;
            for (Trade* t : positions) {
                lowest = fmin(lowest, t->entry_price);
                highest = fmax(highest, t->entry_price);
            }

            bool should_open = positions.empty() ||
                              (lowest >= tick.ask + spacing) ||
                              (highest <= tick.ask - spacing);

            if (should_open) {
                // Calculate lot size based on strategy
                double lot = base_lot;

                switch (sizing_strategy) {
                    case FIXED:
                        lot = base_lot;
                        break;

                    case DD_BASED:
                        lot = CalculateDDBasedLot(dd_pct);
                        break;

                    case VOLATILITY:
                        lot = CalculateVolatilityLot(atr_short.Get(), atr_long.Get(), base_lot);
                        break;

                    case KELLY: {
                        double win_rate = (wins + losses > 10) ? (double)wins / (wins + losses) : 0.6;
                        double avg_win = (wins > 0) ? total_win_amount / wins : 1.0;
                        double avg_loss = (losses > 0) ? total_loss_amount / losses : -1.0;
                        lot = CalculateKellyLot(win_rate, avg_win, avg_loss, base_lot, balance);
                        break;
                    }
                }

                // Check margin
                double used_margin = 0;
                for (Trade* t : positions) {
                    used_margin += t->lot_size * contract_size * t->entry_price / leverage;
                }
                double margin_needed = lot * contract_size * tick.ask / leverage;

                if (equity - used_margin > margin_needed * 2) {
                    Trade* t = new Trade();
                    t->id = next_id++;
                    t->entry_price = tick.ask;
                    t->lot_size = lot;
                    t->take_profit = tick.ask + tick.spread() + (spacing * tp_multiplier);
                    positions.push_back(t);
                    result.positions_opened++;

                    // Track lot statistics
                    total_lot_used += lot;
                    lot_count++;
                    result.min_lot_used = fmin(result.min_lot_used, lot);
                    result.max_lot_used = fmax(result.max_lot_used, lot);
                }
            }
        }
    }

    // Close remaining positions
    const Tick& last_tick = ticks.back();
    for (Trade* t : positions) {
        double pl = (last_tick.bid - t->entry_price) * t->lot_size * contract_size;
        balance += pl;
        result.total_trades++;
        delete t;
    }
    positions.clear();

    // Calculate results
    result.final_balance = balance;
    result.return_pct = (balance - initial_balance) / initial_balance * 100.0;
    result.max_dd_pct = max_dd_pct;
    result.risk_adjusted_score = (max_dd_pct > 0) ? result.return_pct / max_dd_pct : 0;
    result.avg_lot_size = (lot_count > 0) ? total_lot_used / lot_count : base_lot;

    if (result.min_lot_used == DBL_MAX) result.min_lot_used = base_lot;

    return result;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("============================================================\n");
    printf("ADAPTIVE LOT SIZING TEST FOR V7 STRATEGY\n");
    printf("============================================================\n\n");

    printf("Goal: Reduce max drawdown below 15%% while maintaining returns\n\n");

    printf("V7 Optimized Parameters:\n");
    printf("  ATR Short Period: 50\n");
    printf("  ATR Long Period: 1000\n");
    printf("  Volatility Threshold: 0.6\n");
    printf("  TP Multiplier: 2.0\n\n");

    printf("Lot Sizing Strategies:\n");
    printf("  a) Fixed: Always 0.01 lots (baseline)\n");
    printf("  b) DD-Based: 0.01 at 0%% DD, 0.005 at 2.5%%, 0.0025 at 5%%+\n");
    printf("  c) Volatility: Smaller lots when ATR is higher\n");
    printf("  d) Kelly: Size based on historical win rate and W/L ratio\n\n");

    // Load tick data
    const char* filename = "Broker/XAUUSD_TICKS_2025.csv";
    const size_t max_ticks = 10000000;  // 10 million ticks

    printf("Loading %zu ticks from %s...\n", max_ticks, filename);
    std::vector<Tick> ticks = LoadTicks(filename, max_ticks);

    if (ticks.empty()) {
        fprintf(stderr, "ERROR: Failed to load tick data\n");
        return 1;
    }

    printf("Loaded %zu ticks successfully\n\n", ticks.size());

    // Display price range
    double start_price = ticks.front().bid;
    double end_price = ticks.back().bid;
    double price_change = (end_price - start_price) / start_price * 100.0;

    printf("Price Range: %.2f -> %.2f (%+.2f%%)\n\n", start_price, end_price, price_change);

    // V7 optimized parameters
    const int atr_short = 50;
    const int atr_long = 1000;
    const double vol_threshold = 0.6;
    const double tp_mult = 2.0;

    // Run tests for all sizing strategies
    printf("Running backtests...\n");
    printf("============================================================\n\n");

    std::vector<TestResult> results;

    SizingStrategy strategies[] = {FIXED, DD_BASED, VOLATILITY, KELLY};

    for (SizingStrategy strat : strategies) {
        printf("Testing: %s...\n", GetStrategyName(strat));
        TestResult r = RunBacktest(ticks, strat, atr_short, atr_long, vol_threshold, tp_mult);
        results.push_back(r);
        printf("  Return: %.2f%%, Max DD: %.2f%%, Score: %.2f\n",
               r.return_pct, r.max_dd_pct, r.risk_adjusted_score);
    }

    printf("\n");

    // Results table
    printf("============================================================\n");
    printf("RESULTS COMPARISON\n");
    printf("============================================================\n\n");

    printf("%-20s %10s %10s %10s %10s %10s\n",
           "Strategy", "Return", "Max DD", "Score", "Trades", "Positions");
    printf("--------------------------------------------------------------------\n");

    for (const TestResult& r : results) {
        printf("%-20s %9.2f%% %9.2f%% %10.2f %10d %10d\n",
               GetStrategyName(r.strategy),
               r.return_pct, r.max_dd_pct, r.risk_adjusted_score,
               r.total_trades, r.positions_opened);
    }

    printf("\n");

    // Lot size statistics
    printf("============================================================\n");
    printf("LOT SIZE STATISTICS\n");
    printf("============================================================\n\n");

    printf("%-20s %10s %10s %10s\n", "Strategy", "Avg Lot", "Min Lot", "Max Lot");
    printf("----------------------------------------------------\n");

    for (const TestResult& r : results) {
        printf("%-20s %10.4f %10.4f %10.4f\n",
               GetStrategyName(r.strategy),
               r.avg_lot_size, r.min_lot_used, r.max_lot_used);
    }

    printf("\n");

    // Find best strategy
    printf("============================================================\n");
    printf("ANALYSIS\n");
    printf("============================================================\n\n");

    // Find strategy with best risk-adjusted score
    TestResult best_score = results[0];
    for (const TestResult& r : results) {
        if (r.risk_adjusted_score > best_score.risk_adjusted_score) {
            best_score = r;
        }
    }

    // Find strategy with lowest DD (that's still profitable)
    TestResult lowest_dd = results[0];
    for (const TestResult& r : results) {
        if (r.return_pct > 0 && r.max_dd_pct < lowest_dd.max_dd_pct) {
            lowest_dd = r;
        }
    }

    // Find strategy with highest return
    TestResult highest_return = results[0];
    for (const TestResult& r : results) {
        if (r.return_pct > highest_return.return_pct) {
            highest_return = r;
        }
    }

    // Check which strategies meet the 15% DD target
    printf("Target: Max DD < 15%%\n\n");

    printf("Strategies meeting DD target:\n");
    bool any_met = false;
    for (const TestResult& r : results) {
        if (r.max_dd_pct < 15.0) {
            printf("  [PASS] %s: %.2f%% DD, %.2f%% return\n",
                   GetStrategyName(r.strategy), r.max_dd_pct, r.return_pct);
            any_met = true;
        }
    }
    if (!any_met) {
        printf("  [NONE] No strategy achieved < 15%% DD\n");
    }
    printf("\n");

    printf("Best by Risk-Adjusted Score: %s (%.2f)\n",
           GetStrategyName(best_score.strategy), best_score.risk_adjusted_score);
    printf("Best by Lowest Drawdown: %s (%.2f%%)\n",
           GetStrategyName(lowest_dd.strategy), lowest_dd.max_dd_pct);
    printf("Best by Highest Return: %s (%.2f%%)\n",
           GetStrategyName(highest_return.strategy), highest_return.return_pct);

    printf("\n");

    // DD improvement analysis
    printf("============================================================\n");
    printf("DD REDUCTION ANALYSIS vs FIXED BASELINE\n");
    printf("============================================================\n\n");

    TestResult baseline = results[0];  // Fixed is first

    for (size_t i = 1; i < results.size(); i++) {
        const TestResult& r = results[i];
        double dd_reduction = ((baseline.max_dd_pct - r.max_dd_pct) / baseline.max_dd_pct) * 100.0;
        double return_change = ((r.return_pct - baseline.return_pct) / fabs(baseline.return_pct)) * 100.0;

        printf("%s:\n", GetStrategyName(r.strategy));
        printf("  DD Reduction: %+.1f%% (%.2f%% -> %.2f%%)\n",
               dd_reduction, baseline.max_dd_pct, r.max_dd_pct);
        printf("  Return Change: %+.1f%% (%.2f%% -> %.2f%%)\n",
               return_change, baseline.return_pct, r.return_pct);
        printf("  Score Improvement: %+.2f (%.2f -> %.2f)\n\n",
               r.risk_adjusted_score - baseline.risk_adjusted_score,
               baseline.risk_adjusted_score, r.risk_adjusted_score);
    }

    // Recommendation
    printf("============================================================\n");
    printf("RECOMMENDATION\n");
    printf("============================================================\n\n");

    // Find the best strategy that either meets the target or comes closest
    TestResult recommended = results[0];
    double best_metric = -999999;

    for (const TestResult& r : results) {
        // Prioritize strategies that meet DD target, then by score
        double metric;
        if (r.max_dd_pct < 15.0) {
            metric = 1000 + r.risk_adjusted_score;  // Bonus for meeting target
        } else {
            metric = r.risk_adjusted_score - (r.max_dd_pct - 15.0);  // Penalty for exceeding
        }

        if (metric > best_metric) {
            best_metric = metric;
            recommended = r;
        }
    }

    printf("Recommended Strategy: %s\n", GetStrategyName(recommended.strategy));
    printf("  Expected Return: %.2f%%\n", recommended.return_pct);
    printf("  Expected Max DD: %.2f%%\n", recommended.max_dd_pct);
    printf("  Risk-Adjusted Score: %.2f\n", recommended.risk_adjusted_score);
    printf("  Average Lot Size: %.4f\n", recommended.avg_lot_size);

    if (recommended.max_dd_pct < 15.0) {
        printf("\n  [SUCCESS] This strategy meets the < 15%% DD target!\n");
    } else {
        printf("\n  [NOTE] DD target of < 15%% not achieved.\n");
        printf("  Consider combining strategies or adjusting V7 parameters.\n");
    }

    printf("\n============================================================\n");
    printf("TEST COMPLETE\n");
    printf("============================================================\n");

    return 0;
}
