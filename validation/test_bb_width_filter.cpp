/**
 * Bollinger Band Width Filter Test
 *
 * Compares:
 * - V7's ATR-based volatility filter
 * - Bollinger Band Width filter (new approach)
 *
 * Idea: Trade only when BB width is below average (low volatility/ranging market)
 *
 * Bollinger Bands:
 * - Middle = 20-period SMA
 * - Upper = Middle + 2 * StdDev
 * - Lower = Middle - 2 * StdDev
 * - Width = (Upper - Lower) / Middle
 *
 * Filter: Trade when current_width < avg_width * threshold
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
// Bollinger Band Calculator
// ============================================================================

class BollingerBands {
private:
    std::deque<double> prices_;
    int period_;
    double std_dev_mult_;

public:
    BollingerBands(int period = 20, double std_dev_mult = 2.0)
        : period_(period), std_dev_mult_(std_dev_mult) {}

    void Add(double price) {
        prices_.push_back(price);
        if ((int)prices_.size() > period_) {
            prices_.pop_front();
        }
    }

    bool IsReady() const {
        return (int)prices_.size() >= period_;
    }

    double GetMiddle() const {
        if (prices_.empty()) return 0.0;
        double sum = 0.0;
        for (double p : prices_) sum += p;
        return sum / prices_.size();
    }

    double GetStdDev() const {
        if (prices_.size() < 2) return 0.0;
        double mean = GetMiddle();
        double sq_sum = 0.0;
        for (double p : prices_) {
            double diff = p - mean;
            sq_sum += diff * diff;
        }
        return std::sqrt(sq_sum / prices_.size());
    }

    double GetUpper() const {
        return GetMiddle() + std_dev_mult_ * GetStdDev();
    }

    double GetLower() const {
        return GetMiddle() - std_dev_mult_ * GetStdDev();
    }

    // Band width = (upper - lower) / middle
    // This measures volatility relative to price level
    double GetWidth() const {
        double middle = GetMiddle();
        if (middle <= 0) return 0.0;
        return (GetUpper() - GetLower()) / middle;
    }
};

// ============================================================================
// Average Tracker for BB Width
// ============================================================================

class WidthAverage {
private:
    std::deque<double> widths_;
    int period_;
    double sum_;

public:
    WidthAverage(int period = 500)
        : period_(period), sum_(0.0) {}

    void Add(double width) {
        widths_.push_back(width);
        sum_ += width;
        if ((int)widths_.size() > period_) {
            sum_ -= widths_.front();
            widths_.pop_front();
        }
    }

    bool IsReady() const {
        return (int)widths_.size() >= period_;
    }

    double Get() const {
        if (widths_.empty()) return 0.0;
        return sum_ / widths_.size();
    }
};

// ============================================================================
// ATR Calculator (for V7 comparison)
// ============================================================================

class ATR {
private:
    std::deque<double> ranges_;
    int period_;
    double sum_;
    double last_price_;

public:
    ATR(int period)
        : period_(period), sum_(0.0), last_price_(0.0) {}

    void Add(double price) {
        if (last_price_ > 0) {
            double range = std::fabs(price - last_price_);
            ranges_.push_back(range);
            sum_ += range;
            if ((int)ranges_.size() > period_) {
                sum_ -= ranges_.front();
                ranges_.pop_front();
            }
        }
        last_price_ = price;
    }

    bool IsReady() const {
        return (int)ranges_.size() >= period_;
    }

    double Get() const {
        if (ranges_.empty()) return 0.0;
        return sum_ / ranges_.size();
    }
};

// ============================================================================
// Filter Types
// ============================================================================

enum FilterType {
    NO_FILTER,          // V1 style - trades everything
    ATR_FILTER,         // V7 style - ATR ratio filter
    BB_WIDTH_FILTER     // New - Bollinger Band width filter
};

// ============================================================================
// Test Result
// ============================================================================

struct Result {
    double return_pct;
    double max_dd;
    int trades;
    int positions_opened;
    char filter_name[64];
    double threshold;
};

// ============================================================================
// Run Backtest
// ============================================================================

Result RunTest(const std::vector<Tick>& ticks, FilterType filter, double threshold,
               int bb_period = 20, int avg_period = 500,
               int atr_short = 100, int atr_long = 500) {

    Result r;
    memset(&r, 0, sizeof(r));
    r.threshold = threshold;

    if (ticks.empty()) return r;

    // Set filter name
    switch (filter) {
        case NO_FILTER:
            snprintf(r.filter_name, sizeof(r.filter_name), "No Filter");
            break;
        case ATR_FILTER:
            snprintf(r.filter_name, sizeof(r.filter_name), "ATR Ratio (t=%.1f)", threshold);
            break;
        case BB_WIDTH_FILTER:
            snprintf(r.filter_name, sizeof(r.filter_name), "BB Width (t=%.1f)", threshold);
            break;
    }

    // Account settings
    double balance = 10000.0;
    double equity = balance;
    double peak_equity = balance;
    double contract_size = 100.0;
    double leverage = 500.0;
    double spacing = 1.0;

    // Positions
    std::vector<Trade*> positions;
    size_t next_id = 1;
    bool partial_done = false;
    bool all_closed = false;

    // Indicators
    BollingerBands bb(bb_period);
    WidthAverage bb_avg(avg_period);
    ATR atr_short_calc(atr_short);
    ATR atr_long_calc(atr_long);

    // Process ticks
    for (size_t i = 0; i < ticks.size(); i++) {
        const Tick& tick = ticks[i];

        // Update indicators
        bb.Add(tick.bid);
        if (bb.IsReady()) {
            bb_avg.Add(bb.GetWidth());
        }
        atr_short_calc.Add(tick.bid);
        atr_long_calc.Add(tick.bid);

        // Calculate equity
        equity = balance;
        for (Trade* t : positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * contract_size;
        }

        // Reset peak on no positions
        if (positions.empty() && peak_equity != balance) {
            peak_equity = balance;
            partial_done = false;
            all_closed = false;
        }

        // Track peak
        if (equity > peak_equity) {
            peak_equity = equity;
            partial_done = false;
            all_closed = false;
        }

        // Calculate drawdown
        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        if (dd_pct > r.max_dd) r.max_dd = dd_pct;

        // V3 protection: Close ALL at 25% DD
        if (dd_pct > 25.0 && !all_closed && !positions.empty()) {
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

        // V3 protection: Partial close at 8% DD
        if (dd_pct > 8.0 && !partial_done && positions.size() > 1) {
            std::sort(positions.begin(), positions.end(), [&](Trade* a, Trade* b) {
                return (tick.bid - a->entry_price) < (tick.bid - b->entry_price);
            });
            int to_close = (int)(positions.size() * 0.5);
            if (to_close < 1) to_close = 1;
            for (int j = 0; j < to_close && !positions.empty(); j++) {
                balance += (tick.bid - positions[0]->entry_price) * positions[0]->lot_size * contract_size;
                delete positions[0];
                positions.erase(positions.begin());
            }
            partial_done = true;
        }

        // Check take profit
        for (auto it = positions.begin(); it != positions.end();) {
            Trade* t = *it;
            if (tick.bid >= t->take_profit) {
                balance += (t->take_profit - t->entry_price) * t->lot_size * contract_size;
                r.trades++;
                delete t;
                it = positions.erase(it);
            } else {
                ++it;
            }
        }

        // Determine if we should trade based on filter
        bool should_trade = true;

        switch (filter) {
            case NO_FILTER:
                should_trade = true;
                break;

            case ATR_FILTER:
                if (atr_short_calc.IsReady() && atr_long_calc.IsReady()) {
                    double atr_s = atr_short_calc.Get();
                    double atr_l = atr_long_calc.Get();
                    if (atr_l > 0) {
                        should_trade = atr_s < atr_l * threshold;
                    }
                }
                break;

            case BB_WIDTH_FILTER:
                if (bb.IsReady() && bb_avg.IsReady()) {
                    double curr_width = bb.GetWidth();
                    double avg_width = bb_avg.Get();
                    if (avg_width > 0) {
                        should_trade = curr_width < avg_width * threshold;
                    }
                }
                break;
        }

        // Open new positions
        if (dd_pct < 5.0 && should_trade && (int)positions.size() < 20) {
            double lowest = DBL_MAX;
            double highest = -DBL_MAX;
            for (Trade* t : positions) {
                if (t->entry_price < lowest) lowest = t->entry_price;
                if (t->entry_price > highest) highest = t->entry_price;
            }

            bool should_open = positions.empty() ||
                              (lowest >= tick.ask + spacing) ||
                              (highest <= tick.ask - spacing);

            if (should_open) {
                double lot = 0.01;
                double used_margin = 0.0;
                for (Trade* t : positions) {
                    used_margin += t->lot_size * contract_size * t->entry_price / leverage;
                }
                double margin_needed = lot * contract_size * tick.ask / leverage;

                if (equity - used_margin > margin_needed * 2) {
                    Trade* t = new Trade();
                    t->id = next_id++;
                    t->entry_price = tick.ask;
                    t->lot_size = lot;
                    t->take_profit = tick.ask + tick.spread() + spacing;
                    positions.push_back(t);
                    r.positions_opened++;
                }
            }
        }
    }

    // Close remaining positions at end
    if (!ticks.empty()) {
        const Tick& last_tick = ticks.back();
        for (Trade* t : positions) {
            balance += (last_tick.bid - t->entry_price) * t->lot_size * contract_size;
            r.trades++;
            delete t;
        }
    }

    r.return_pct = (balance - 10000.0) / 10000.0 * 100.0;
    return r;
}

// ============================================================================
// Load Ticks (C-style I/O)
// ============================================================================

std::vector<Tick> LoadTicks(const char* filename, size_t max_count) {
    std::vector<Tick> ticks;
    ticks.reserve(max_count);

    FILE* fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Cannot open file: %s\n", filename);
        return ticks;
    }

    // Skip header
    char line[512];
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return ticks;
    }

    // Parse ticks
    while (fgets(line, sizeof(line), fp) && ticks.size() < max_count) {
        char timestamp[64];
        double bid, ask;
        int volume, flags;

        // Parse tab-separated: Timestamp\tBid\tAsk\tVolume\tFlags
        int parsed = sscanf(line, "%63[^\t]\t%lf\t%lf\t%d\t%d",
                           timestamp, &bid, &ask, &volume, &flags);

        if (parsed >= 3 && bid > 0 && ask > 0) {
            Tick tick;
            tick.bid = bid;
            tick.ask = ask;
            ticks.push_back(tick);
        }

        if (ticks.size() % 100000 == 0 && ticks.size() > 0) {
            printf("  Loaded %zu ticks...\n", ticks.size());
        }
    }

    fclose(fp);
    return ticks;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=======================================================\n");
    printf("BOLLINGER BAND WIDTH FILTER TEST\n");
    printf("=======================================================\n\n");

    printf("Concept:\n");
    printf("- V7 uses ATR ratio: short_ATR < long_ATR * threshold\n");
    printf("- BB Width uses: current_width < avg_width * threshold\n");
    printf("- Both aim to trade only in low volatility (ranging) markets\n\n");

    printf("Bollinger Band Width = (Upper - Lower) / Middle\n");
    printf("- Narrow bands = low volatility = good for grid trading\n");
    printf("- Wide bands = high volatility = avoid trading\n\n");

    // Load data
    const char* filename = "Grid/XAUUSD_TICKS_2025.csv";
    printf("Loading up to 500,000 ticks from %s...\n", filename);

    std::vector<Tick> ticks = LoadTicks(filename, 500000);

    if (ticks.empty()) {
        fprintf(stderr, "Failed to load tick data!\n");
        return 1;
    }

    printf("Loaded %zu ticks\n", ticks.size());

    double start_price = ticks.front().bid;
    double end_price = ticks.back().bid;
    double price_change = (end_price - start_price) / start_price * 100.0;

    printf("Price: %.2f -> %.2f (%+.2f%%)\n\n", start_price, end_price, price_change);

    // Run tests
    printf("Running tests...\n\n");

    std::vector<Result> results;

    // Baseline: No filter
    results.push_back(RunTest(ticks, NO_FILTER, 1.0));

    // V7 ATR filter with different thresholds
    double atr_thresholds[] = {0.6, 0.7, 0.8, 0.9, 1.0};
    for (double t : atr_thresholds) {
        results.push_back(RunTest(ticks, ATR_FILTER, t));
    }

    // BB Width filter with different thresholds
    double bb_thresholds[] = {0.6, 0.7, 0.8, 0.9, 1.0};
    for (double t : bb_thresholds) {
        results.push_back(RunTest(ticks, BB_WIDTH_FILTER, t));
    }

    // Print results table
    printf("=======================================================\n");
    printf("RESULTS\n");
    printf("=======================================================\n\n");

    printf("%-25s %10s %10s %10s %10s\n", "Strategy", "Return", "Max DD", "Trades", "Opened");
    printf("-----------------------------------------------------------------------\n");

    for (const Result& r : results) {
        printf("%-25s %9.2f%% %9.2f%% %10d %10d\n",
               r.filter_name, r.return_pct, r.max_dd, r.trades, r.positions_opened);
    }

    printf("-----------------------------------------------------------------------\n\n");

    // Find best in each category
    Result best_atr = results[0];
    Result best_bb = results[0];

    for (const Result& r : results) {
        if (strstr(r.filter_name, "ATR") && r.return_pct > best_atr.return_pct) {
            best_atr = r;
        }
        if (strstr(r.filter_name, "BB Width") && r.return_pct > best_bb.return_pct) {
            best_bb = r;
        }
    }

    // Analysis
    printf("=======================================================\n");
    printf("ANALYSIS\n");
    printf("=======================================================\n\n");

    printf("Best ATR filter:      %s (%.2f%% return, %.2f%% max DD)\n",
           best_atr.filter_name, best_atr.return_pct, best_atr.max_dd);
    printf("Best BB Width filter: %s (%.2f%% return, %.2f%% max DD)\n",
           best_bb.filter_name, best_bb.return_pct, best_bb.max_dd);

    printf("\n");

    // Compare best of each
    printf("Comparison of best filters:\n");
    printf("---------------------------\n");

    if (best_bb.return_pct > best_atr.return_pct) {
        double improvement = best_bb.return_pct - best_atr.return_pct;
        printf("BB Width filter OUTPERFORMS ATR filter by %.2f%% return\n", improvement);

        if (best_bb.max_dd < best_atr.max_dd) {
            printf("BB Width also has LOWER drawdown (%.2f%% vs %.2f%%)\n",
                   best_bb.max_dd, best_atr.max_dd);
        } else {
            printf("However, BB Width has HIGHER drawdown (%.2f%% vs %.2f%%)\n",
                   best_bb.max_dd, best_atr.max_dd);
        }
    } else if (best_atr.return_pct > best_bb.return_pct) {
        double improvement = best_atr.return_pct - best_bb.return_pct;
        printf("ATR filter OUTPERFORMS BB Width filter by %.2f%% return\n", improvement);

        if (best_atr.max_dd < best_bb.max_dd) {
            printf("ATR also has LOWER drawdown (%.2f%% vs %.2f%%)\n",
                   best_atr.max_dd, best_bb.max_dd);
        } else {
            printf("However, ATR has HIGHER drawdown (%.2f%% vs %.2f%%)\n",
                   best_atr.max_dd, best_bb.max_dd);
        }
    } else {
        printf("Both filters perform EQUALLY\n");
    }

    printf("\n");
    printf("Key observations:\n");
    printf("1. Lower threshold = more selective (fewer trades, potentially higher quality)\n");
    printf("2. Higher threshold = more permissive (more trades, captures more opportunities)\n");
    printf("3. BB Width measures volatility relative to price level\n");
    printf("4. ATR measures absolute price movement\n\n");

    printf("Recommendation:\n");
    if (best_bb.return_pct > best_atr.return_pct && best_bb.max_dd <= best_atr.max_dd) {
        printf("-> USE BB WIDTH FILTER: Better returns with equal or lower risk\n");
    } else if (best_atr.return_pct > best_bb.return_pct && best_atr.max_dd <= best_bb.max_dd) {
        printf("-> USE ATR FILTER (V7): Better returns with equal or lower risk\n");
    } else if (best_bb.return_pct > best_atr.return_pct) {
        printf("-> CONSIDER BB WIDTH: Higher returns but check risk tolerance\n");
    } else {
        printf("-> STICK WITH ATR FILTER (V7): Proven approach with good results\n");
    }

    printf("\n=======================================================\n");

    return 0;
}
