/**
 * Hybrid V5/V7 Test: Combining Trend + Volatility Filters
 *
 * Hypothesis: Trading only when BOTH conditions are met:
 *   - Price above SMA (trend filter from V5)
 *   - Low volatility (short ATR < long ATR * threshold from V7)
 * should give the best results.
 *
 * Configurations tested:
 *   1. No filter (baseline)
 *   2. Trend filter only (V5 style)
 *   3. Volatility filter only (V7 style)
 *   4. Both filters combined (hybrid)
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
// SMA Calculator (for V5 trend filter)
// ============================================================================

class SMA {
    std::deque<double> prices_;
    int period_;
    double sum_;
public:
    explicit SMA(int period) : period_(period), sum_(0.0) {}

    void Add(double price) {
        prices_.push_back(price);
        sum_ += price;
        if ((int)prices_.size() > period_) {
            sum_ -= prices_.front();
            prices_.pop_front();
        }
    }

    double Get() const {
        return prices_.empty() ? 0.0 : sum_ / (double)prices_.size();
    }

    bool IsReady() const {
        return (int)prices_.size() >= period_;
    }
};

// ============================================================================
// ATR Calculator (for V7 volatility filter)
// ============================================================================

class ATR {
    std::deque<double> ranges_;
    int period_;
    double sum_;
    double last_price_;
public:
    explicit ATR(int period) : period_(period), sum_(0.0), last_price_(0.0) {}

    void Add(double price) {
        if (last_price_ > 0.0) {
            double range = fabs(price - last_price_);
            ranges_.push_back(range);
            sum_ += range;
            if ((int)ranges_.size() > period_) {
                sum_ -= ranges_.front();
                ranges_.pop_front();
            }
        }
        last_price_ = price;
    }

    double Get() const {
        return ranges_.empty() ? 0.0 : sum_ / (double)ranges_.size();
    }

    bool IsReady() const {
        return (int)ranges_.size() >= period_;
    }
};

// ============================================================================
// Filter Types
// ============================================================================

enum FilterType {
    FILTER_NONE = 0,        // No filter (baseline)
    FILTER_TREND_ONLY = 1,  // V5 style: price > SMA
    FILTER_VOL_ONLY = 2,    // V7 style: low volatility
    FILTER_HYBRID = 3       // Both: price > SMA AND low volatility
};

const char* FilterName(FilterType f) {
    switch (f) {
        case FILTER_NONE:       return "No Filter (Baseline)";
        case FILTER_TREND_ONLY: return "Trend Only (V5)";
        case FILTER_VOL_ONLY:   return "Volatility Only (V7)";
        case FILTER_HYBRID:     return "Hybrid (V5+V7)";
        default:                return "Unknown";
    }
}

// ============================================================================
// Result Structure
// ============================================================================

struct Result {
    FilterType filter;
    int sma_period;
    int atr_short;
    int atr_long;
    double vol_threshold;

    double return_pct;
    double max_dd;
    int trades_closed;
    int positions_opened;
    double final_balance;
};

// ============================================================================
// Load Tick Data (C-style I/O)
// ============================================================================

std::vector<Tick> LoadTicks(const char* filename, size_t max_count) {
    std::vector<Tick> ticks;
    ticks.reserve(max_count);

    FILE* file = fopen(filename, "r");
    if (!file) {
        printf("ERROR: Cannot open file: %s\n", filename);
        return ticks;
    }

    // Skip header line
    char line[512];
    if (!fgets(line, sizeof(line), file)) {
        fclose(file);
        return ticks;
    }

    // Parse tick data
    while (fgets(line, sizeof(line), file) && ticks.size() < max_count) {
        // Format: timestamp\tbid\task\t...
        char timestamp[64];
        double bid = 0.0, ask = 0.0;

        if (sscanf(line, "%63[^\t]\t%lf\t%lf", timestamp, &bid, &ask) >= 3) {
            if (bid > 0.0 && ask > 0.0) {
                Tick tick;
                tick.bid = bid;
                tick.ask = ask;
                ticks.push_back(tick);
            }
        }

        // Progress indicator
        if (ticks.size() % 100000 == 0) {
            printf("  Loaded %zu ticks...\n", ticks.size());
        }
    }

    fclose(file);
    return ticks;
}

// ============================================================================
// Run Backtest
// ============================================================================

Result RunBacktest(const std::vector<Tick>& ticks, FilterType filter,
                   int sma_period, int atr_short, int atr_long, double vol_threshold,
                   double spacing, double tp_multiplier) {

    Result result;
    result.filter = filter;
    result.sma_period = sma_period;
    result.atr_short = atr_short;
    result.atr_long = atr_long;
    result.vol_threshold = vol_threshold;
    result.return_pct = 0.0;
    result.max_dd = 0.0;
    result.trades_closed = 0;
    result.positions_opened = 0;
    result.final_balance = 0.0;

    if (ticks.empty()) return result;

    // Account settings
    const double initial_balance = 10000.0;
    double balance = initial_balance;
    double equity = balance;
    double peak_equity = balance;
    const double contract_size = 100.0;  // XAUUSD
    const double leverage = 500.0;
    const int max_positions = 20;

    // V3 protection thresholds
    const double stop_new_at_dd = 5.0;
    const double partial_close_at_dd = 8.0;
    const double close_all_at_dd = 25.0;

    // Position tracking
    std::vector<Trade*> positions;
    size_t next_id = 1;
    bool partial_done = false;
    bool all_closed = false;

    // Indicators
    SMA sma(sma_period);
    ATR atr_s(atr_short);
    ATR atr_l(atr_long);

    for (const Tick& tick : ticks) {
        // Update indicators
        sma.Add(tick.bid);
        atr_s.Add(tick.bid);
        atr_l.Add(tick.bid);

        // Calculate equity
        equity = balance;
        for (Trade* t : positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * contract_size;
        }

        // Reset peak equity when no positions
        if (positions.empty() && peak_equity != balance) {
            peak_equity = balance;
            partial_done = false;
            all_closed = false;
        }

        // Update peak equity
        if (equity > peak_equity) {
            peak_equity = equity;
            partial_done = false;
            all_closed = false;
        }

        // Calculate drawdown
        double dd_pct = (peak_equity > 0.0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        if (dd_pct > result.max_dd) {
            result.max_dd = dd_pct;
        }

        // V3 Protection: Close ALL at threshold
        if (dd_pct > close_all_at_dd && !all_closed && !positions.empty()) {
            for (Trade* t : positions) {
                balance += (tick.bid - t->entry_price) * t->lot_size * contract_size;
                result.trades_closed++;
                delete t;
            }
            positions.clear();
            all_closed = true;
            equity = balance;
            peak_equity = balance;
            continue;
        }

        // V3 Protection: Partial close at threshold
        if (dd_pct > partial_close_at_dd && !partial_done && positions.size() > 1) {
            // Sort by P/L (worst first)
            std::sort(positions.begin(), positions.end(), [&](Trade* a, Trade* b) {
                double pl_a = (tick.bid - a->entry_price) * a->lot_size;
                double pl_b = (tick.bid - b->entry_price) * b->lot_size;
                return pl_a < pl_b;
            });

            int to_close = (int)(positions.size() / 2);
            if (to_close < 1) to_close = 1;

            for (int i = 0; i < to_close && !positions.empty(); i++) {
                Trade* t = positions[0];
                balance += (tick.bid - t->entry_price) * t->lot_size * contract_size;
                result.trades_closed++;
                delete t;
                positions.erase(positions.begin());
            }
            partial_done = true;
        }

        // Check TP for open positions
        for (auto it = positions.begin(); it != positions.end();) {
            Trade* t = *it;
            if (tick.bid >= t->take_profit) {
                balance += (t->take_profit - t->entry_price) * t->lot_size * contract_size;
                result.trades_closed++;
                delete t;
                it = positions.erase(it);
            } else {
                ++it;
            }
        }

        // Determine if we should trade based on filter
        bool should_trade = true;

        bool trend_ok = !sma.IsReady() || (tick.bid > sma.Get());
        bool vol_ok = true;
        if (atr_s.IsReady() && atr_l.IsReady() && atr_l.Get() > 0.0) {
            vol_ok = atr_s.Get() < atr_l.Get() * vol_threshold;
        }

        switch (filter) {
            case FILTER_NONE:
                should_trade = true;
                break;
            case FILTER_TREND_ONLY:
                should_trade = trend_ok;
                break;
            case FILTER_VOL_ONLY:
                should_trade = vol_ok;
                break;
            case FILTER_HYBRID:
                should_trade = trend_ok && vol_ok;
                break;
        }

        // Open new positions
        if (dd_pct < stop_new_at_dd && should_trade && (int)positions.size() < max_positions) {
            double lowest = DBL_MAX;
            double highest = DBL_MIN;
            for (Trade* t : positions) {
                if (t->entry_price < lowest) lowest = t->entry_price;
                if (t->entry_price > highest) highest = t->entry_price;
            }

            bool should_open = false;
            if (positions.empty()) {
                should_open = true;
            } else if (lowest >= tick.ask + spacing) {
                should_open = true;
            } else if (highest <= tick.ask - spacing) {
                should_open = true;
            }

            if (should_open) {
                // Check margin
                double used_margin = 0.0;
                for (Trade* t : positions) {
                    used_margin += t->lot_size * contract_size * t->entry_price / leverage;
                }
                double lot = 0.01;
                double margin_needed = lot * contract_size * tick.ask / leverage;

                if (equity - used_margin > margin_needed * 2.0) {
                    Trade* t = new Trade();
                    t->id = next_id++;
                    t->entry_price = tick.ask;
                    t->lot_size = lot;
                    t->take_profit = tick.ask + tick.spread() + (spacing * tp_multiplier);
                    positions.push_back(t);
                    result.positions_opened++;
                }
            }
        }
    }

    // Close remaining positions at end
    if (!ticks.empty()) {
        double final_bid = ticks.back().bid;
        for (Trade* t : positions) {
            balance += (final_bid - t->entry_price) * t->lot_size * contract_size;
            result.trades_closed++;
            delete t;
        }
        positions.clear();
    }

    result.final_balance = balance;
    result.return_pct = (balance - initial_balance) / initial_balance * 100.0;

    return result;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=====================================================\n");
    printf("HYBRID V5/V7 TEST: Trend + Volatility Filter\n");
    printf("=====================================================\n\n");

    printf("Hypothesis: Trading only when BOTH conditions are met:\n");
    printf("  - Price above SMA (trend filter from V5)\n");
    printf("  - Low volatility (short ATR < long ATR * threshold from V7)\n");
    printf("should give the best results.\n\n");

    // Load tick data
    const char* filename = "Grid/XAUUSD_TICKS_2025.csv";
    const size_t max_ticks = 500000;

    printf("Loading up to %zu ticks from %s...\n", max_ticks, filename);
    std::vector<Tick> ticks = LoadTicks(filename, max_ticks);

    if (ticks.empty()) {
        printf("ERROR: Failed to load tick data\n");
        return 1;
    }

    printf("Loaded %zu ticks\n", ticks.size());
    printf("Price range: %.2f - %.2f\n", ticks.front().bid, ticks.back().bid);
    double price_change = (ticks.back().bid - ticks.front().bid) / ticks.front().bid * 100.0;
    printf("Price change: %+.2f%%\n\n", price_change);

    // Grid parameters
    const double spacing = 1.0;
    const double tp_multiplier = 2.0;

    // V7 optimized ATR parameters
    const int atr_short = 50;
    const int atr_long = 1000;
    const double vol_threshold = 0.6;

    // SMA periods to test
    int sma_periods[] = {5000, 11000};
    int num_sma_periods = sizeof(sma_periods) / sizeof(sma_periods[0]);

    printf("Grid parameters: spacing=%.1f, TP multiplier=%.1f\n", spacing, tp_multiplier);
    printf("ATR parameters: short=%d, long=%d, threshold=%.1f\n", atr_short, atr_long, vol_threshold);
    printf("\n");

    // Run tests for each SMA period
    for (int s = 0; s < num_sma_periods; s++) {
        int sma_period = sma_periods[s];

        printf("=====================================================\n");
        printf("SMA Period: %d\n", sma_period);
        printf("=====================================================\n\n");

        // Run all 4 configurations
        std::vector<Result> results;

        printf("Running: No Filter (Baseline)...\n");
        results.push_back(RunBacktest(ticks, FILTER_NONE, sma_period, atr_short, atr_long, vol_threshold, spacing, tp_multiplier));

        printf("Running: Trend Filter Only (V5)...\n");
        results.push_back(RunBacktest(ticks, FILTER_TREND_ONLY, sma_period, atr_short, atr_long, vol_threshold, spacing, tp_multiplier));

        printf("Running: Volatility Filter Only (V7)...\n");
        results.push_back(RunBacktest(ticks, FILTER_VOL_ONLY, sma_period, atr_short, atr_long, vol_threshold, spacing, tp_multiplier));

        printf("Running: Hybrid (V5+V7)...\n");
        results.push_back(RunBacktest(ticks, FILTER_HYBRID, sma_period, atr_short, atr_long, vol_threshold, spacing, tp_multiplier));

        printf("\n");

        // Print results table
        printf("%-25s %10s %10s %10s %10s\n", "Strategy", "Return", "Max DD", "Trades", "Opened");
        printf("--------------------------------------------------------------------\n");

        for (const Result& r : results) {
            printf("%-25s %9.2f%% %9.2f%% %10d %10d\n",
                   FilterName(r.filter),
                   r.return_pct,
                   r.max_dd,
                   r.trades_closed,
                   r.positions_opened);
        }
        printf("\n");

        // Find best performer
        const Result* best = &results[0];
        for (const Result& r : results) {
            if (r.return_pct > best->return_pct) {
                best = &r;
            }
        }

        // Find lowest drawdown
        const Result* lowest_dd = &results[0];
        for (const Result& r : results) {
            if (r.max_dd < lowest_dd->max_dd) {
                lowest_dd = &r;
            }
        }

        // Calculate risk-adjusted return (return / max_dd)
        printf("Risk-Adjusted Analysis (Return / Max DD):\n");
        printf("%-25s %12s\n", "Strategy", "Risk-Adj");
        printf("--------------------------------------------\n");

        const Result* best_risk_adj = &results[0];
        double best_risk_adj_value = -DBL_MAX;

        for (const Result& r : results) {
            double risk_adj = (r.max_dd > 0) ? r.return_pct / r.max_dd : 0.0;
            printf("%-25s %11.3f\n", FilterName(r.filter), risk_adj);
            if (risk_adj > best_risk_adj_value) {
                best_risk_adj_value = risk_adj;
                best_risk_adj = &r;
            }
        }
        printf("\n");

        printf("Summary for SMA %d:\n", sma_period);
        printf("  Best Return:      %s (%.2f%%)\n", FilterName(best->filter), best->return_pct);
        printf("  Lowest Drawdown:  %s (%.2f%%)\n", FilterName(lowest_dd->filter), lowest_dd->max_dd);
        printf("  Best Risk-Adj:    %s (%.3f)\n", FilterName(best_risk_adj->filter), best_risk_adj_value);
        printf("\n");
    }

    // Final analysis
    printf("=====================================================\n");
    printf("FINAL ANALYSIS\n");
    printf("=====================================================\n\n");

    printf("The hypothesis states that combining both filters should be best.\n\n");

    printf("Why this might work:\n");
    printf("  - Trend filter (V5): Only trade when price > SMA (uptrend)\n");
    printf("  - Volatility filter (V7): Only trade when volatility is low (ranging)\n");
    printf("  - Combined: Trade only in calm uptrends - most favorable conditions\n\n");

    printf("Why this might NOT work:\n");
    printf("  - Too restrictive: May miss profitable opportunities\n");
    printf("  - Conflicting signals: Uptrends often have higher volatility\n");
    printf("  - Over-optimization: May not generalize to other periods\n\n");

    printf("Recommendation:\n");
    printf("  Compare the results above. If Hybrid shows:\n");
    printf("  - Higher return AND lower drawdown -> Hypothesis confirmed\n");
    printf("  - Higher return but similar drawdown -> Partially confirmed\n");
    printf("  - Lower return -> Individual filters may be better\n");
    printf("  - Lowest drawdown but lower return -> Risk-averse traders prefer this\n\n");

    printf("=====================================================\n");

    return 0;
}
