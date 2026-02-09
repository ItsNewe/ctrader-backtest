/**
 * RSI-Based Mean Reversion Filter Test
 *
 * Hypothesis: Grid strategies work best when RSI is in the neutral zone (40-60),
 * indicating ranging/mean-reverting conditions rather than strong trends.
 *
 * Tests:
 * 1. No filter (baseline)
 * 2. RSI 30-70 (wide - allows most conditions)
 * 3. RSI 35-65 (medium - filters extremes)
 * 4. RSI 40-60 (narrow - most selective, true mean reversion zone)
 * 5. V7 Volatility filter (for comparison)
 *
 * Uses C-style file I/O as requested.
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
// RSI Calculator (14-period standard RSI)
// ============================================================================

class RSI {
public:
    RSI(int period = 14) : period_(period), avg_gain_(0), avg_loss_(0), count_(0), last_price_(0) {}

    void Add(double price) {
        if (last_price_ > 0) {
            double change = price - last_price_;
            double gain = (change > 0) ? change : 0;
            double loss = (change < 0) ? -change : 0;

            if (count_ < period_) {
                // Initial accumulation phase
                gains_.push_back(gain);
                losses_.push_back(loss);
                count_++;

                if (count_ == period_) {
                    // Calculate first SMA
                    double sum_gain = 0, sum_loss = 0;
                    for (int i = 0; i < period_; i++) {
                        sum_gain += gains_[i];
                        sum_loss += losses_[i];
                    }
                    avg_gain_ = sum_gain / period_;
                    avg_loss_ = sum_loss / period_;
                }
            } else {
                // Wilder's smoothing (exponential moving average)
                // avg = (prev_avg * (period - 1) + current) / period
                avg_gain_ = (avg_gain_ * (period_ - 1) + gain) / period_;
                avg_loss_ = (avg_loss_ * (period_ - 1) + loss) / period_;
            }
        }
        last_price_ = price;
    }

    double Get() const {
        if (count_ < period_) return 50.0;  // Neutral during warmup
        if (avg_loss_ == 0) return 100.0;   // No losses = max RSI
        double rs = avg_gain_ / avg_loss_;
        return 100.0 - (100.0 / (1.0 + rs));
    }

    bool IsReady() const { return count_ >= period_; }

private:
    int period_;
    double avg_gain_;
    double avg_loss_;
    int count_;
    double last_price_;
    std::vector<double> gains_;
    std::vector<double> losses_;
};

// ============================================================================
// ATR Calculator (for V7 comparison)
// ============================================================================

class ATR {
public:
    ATR(int period) : period_(period), sum_(0), last_price_(0) {}

    void Add(double price) {
        if (last_price_ > 0) {
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
        if (ranges_.empty()) return 0;
        return sum_ / ranges_.size();
    }

    bool IsReady() const { return (int)ranges_.size() >= period_; }

private:
    int period_;
    std::deque<double> ranges_;
    double sum_;
    double last_price_;
};

// ============================================================================
// Filter Types
// ============================================================================

enum FilterType {
    NO_FILTER,
    RSI_30_70,      // Wide RSI range
    RSI_35_65,      // Medium RSI range
    RSI_40_60,      // Narrow RSI range (most selective)
    VOLATILITY_V7   // V7's low volatility filter
};

struct FilterConfig {
    FilterType type;
    const char* name;
    double rsi_low;
    double rsi_high;
    double vol_threshold;
};

// ============================================================================
// Test Result Structure
// ============================================================================

struct Result {
    double return_pct;
    double max_dd;
    int trades;
    int positions_opened;
    int filter_blocks;      // Number of times filter prevented trading
    double avg_rsi;         // Average RSI during test
    const char* filter_name;
};

// ============================================================================
// C-Style File Loading
// ============================================================================

std::vector<Tick> LoadTicksC(const char* filename, size_t max_count) {
    std::vector<Tick> ticks;
    ticks.reserve(max_count);

    FILE* file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "ERROR: Cannot open file: %s\n", filename);
        return ticks;
    }

    // Skip header line
    char line[256];
    if (!fgets(line, sizeof(line), file)) {
        fclose(file);
        return ticks;
    }

    // Read tick data
    // Format: timestamp\tbid\task\tvolume
    while (fgets(line, sizeof(line), file) && ticks.size() < max_count) {
        char timestamp[64];
        double bid, ask;

        // Parse tab-separated values
        char* token = strtok(line, "\t");
        if (!token) continue;
        strncpy(timestamp, token, sizeof(timestamp) - 1);

        token = strtok(NULL, "\t");
        if (!token) continue;
        bid = atof(token);

        token = strtok(NULL, "\t");
        if (!token) continue;
        ask = atof(token);

        if (bid > 0 && ask > 0 && ask >= bid) {
            Tick tick;
            tick.bid = bid;
            tick.ask = ask;
            ticks.push_back(tick);
        }

        if (ticks.size() % 100000 == 0) {
            printf("  %zu ticks loaded...\n", ticks.size());
        }
    }

    fclose(file);
    return ticks;
}

// ============================================================================
// Grid Trading Simulation with Filter
// ============================================================================

Result RunTest(const std::vector<Tick>& ticks, const FilterConfig& filter_cfg) {
    Result r = {0, 0, 0, 0, 0, 0, filter_cfg.name};

    if (ticks.empty()) return r;

    // Strategy parameters
    const double initial_balance = 10000.0;
    const double contract_size = 100.0;
    const double leverage = 500.0;
    const double spacing = 1.0;
    const double tp_offset = 2.0;  // TP = entry + spread + spacing * 2
    const int max_positions = 20;

    double balance = initial_balance;
    double equity = balance;
    double peak_equity = balance;

    std::vector<Trade*> positions;
    size_t next_id = 1;
    bool partial_done = false;
    bool all_closed = false;

    // Indicators
    RSI rsi(14);
    ATR atr_short(100);
    ATR atr_long(500);

    double rsi_sum = 0;
    int rsi_count = 0;

    for (const Tick& tick : ticks) {
        // Update indicators
        rsi.Add(tick.bid);
        atr_short.Add(tick.bid);
        atr_long.Add(tick.bid);

        double current_rsi = rsi.Get();
        if (rsi.IsReady()) {
            rsi_sum += current_rsi;
            rsi_count++;
        }

        // Calculate equity
        equity = balance;
        for (Trade* t : positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * contract_size;
        }

        // Reset peak tracking when no positions
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
        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        r.max_dd = fmax(r.max_dd, dd_pct);

        // V3 Protection: Close ALL at 25% DD
        if (dd_pct > 25.0 && !all_closed && !positions.empty()) {
            for (Trade* t : positions) {
                balance += (tick.bid - t->entry_price) * t->lot_size * contract_size;
                r.trades++;
                delete t;
            }
            positions.clear();
            all_closed = true;
            equity = balance;
            peak_equity = equity;
            continue;
        }

        // V3 Protection: Partial close at 8% DD
        if (dd_pct > 8.0 && !partial_done && positions.size() > 1) {
            std::sort(positions.begin(), positions.end(), [&](Trade* a, Trade* b) {
                return (tick.bid - a->entry_price) < (tick.bid - b->entry_price);
            });
            int to_close = (int)(positions.size() * 0.5);
            to_close = std::max(1, to_close);
            for (int i = 0; i < to_close && !positions.empty(); i++) {
                balance += (tick.bid - positions[0]->entry_price) * positions[0]->lot_size * contract_size;
                r.trades++;
                delete positions[0];
                positions.erase(positions.begin());
            }
            partial_done = true;
        }

        // Check Take Profit
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

        // Determine if filter allows trading
        bool should_trade = true;

        switch (filter_cfg.type) {
            case NO_FILTER:
                should_trade = true;
                break;

            case RSI_30_70:
            case RSI_35_65:
            case RSI_40_60:
                if (rsi.IsReady()) {
                    should_trade = (current_rsi >= filter_cfg.rsi_low &&
                                   current_rsi <= filter_cfg.rsi_high);
                }
                break;

            case VOLATILITY_V7:
                if (atr_short.IsReady() && atr_long.IsReady() && atr_long.Get() > 0) {
                    should_trade = atr_short.Get() < atr_long.Get() * filter_cfg.vol_threshold;
                }
                break;
        }

        if (!should_trade) {
            r.filter_blocks++;
        }

        // Open new positions if conditions allow
        if (dd_pct < 5.0 && should_trade && (int)positions.size() < max_positions) {
            double lowest = DBL_MAX, highest = DBL_MIN;
            for (Trade* t : positions) {
                lowest = fmin(lowest, t->entry_price);
                highest = fmax(highest, t->entry_price);
            }

            bool should_open = positions.empty() ||
                              (lowest >= tick.ask + spacing) ||
                              (highest <= tick.ask - spacing);

            if (should_open) {
                double lot = 0.01;
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
                    t->take_profit = tick.ask + tick.spread() + tp_offset;
                    positions.push_back(t);
                    r.positions_opened++;
                }
            }
        }
    }

    // Close remaining positions at end
    for (Trade* t : positions) {
        balance += (ticks.back().bid - t->entry_price) * t->lot_size * contract_size;
        r.trades++;
        delete t;
    }

    r.return_pct = (balance - initial_balance) / initial_balance * 100.0;
    r.avg_rsi = (rsi_count > 0) ? rsi_sum / rsi_count : 50.0;

    return r;
}

// ============================================================================
// Main Test Program
// ============================================================================

int main() {
    printf("=============================================================\n");
    printf("RSI-BASED MEAN REVERSION FILTER TEST\n");
    printf("=============================================================\n\n");

    printf("HYPOTHESIS:\n");
    printf("Grid strategies work best when RSI is in the neutral zone (40-60),\n");
    printf("indicating ranging/mean-reverting conditions rather than strong trends.\n\n");

    printf("RSI RANGES TESTED:\n");
    printf("- 30-70 (wide)    : Allows most conditions, filters only extremes\n");
    printf("- 35-65 (medium)  : Moderate filtering\n");
    printf("- 40-60 (narrow)  : Most selective, true mean reversion zone\n");
    printf("- No filter       : Baseline comparison\n");
    printf("- V7 Volatility   : ATR-based filter comparison\n\n");

    // Load tick data
    const char* filename = "Grid/XAUUSD_TICKS_2025.csv";
    const size_t tick_count = 500000;

    printf("Loading %zu ticks from %s...\n", tick_count, filename);
    std::vector<Tick> ticks = LoadTicksC(filename, tick_count);
    printf("Loaded %zu ticks total\n\n", ticks.size());

    if (ticks.empty()) {
        fprintf(stderr, "ERROR: Failed to load tick data\n");
        return 1;
    }

    // Price statistics
    double start_price = ticks.front().bid;
    double end_price = ticks.back().bid;
    double price_change = (end_price - start_price) / start_price * 100.0;

    printf("PRICE DATA:\n");
    printf("Start: $%.2f  End: $%.2f  Change: %+.2f%%\n\n",
           start_price, end_price, price_change);

    // Define filter configurations
    FilterConfig filters[] = {
        {NO_FILTER,     "No Filter (Baseline)",    0,  0,  0.0},
        {RSI_30_70,     "RSI 30-70 (Wide)",       30, 70,  0.0},
        {RSI_35_65,     "RSI 35-65 (Medium)",     35, 65,  0.0},
        {RSI_40_60,     "RSI 40-60 (Narrow)",     40, 60,  0.0},
        {VOLATILITY_V7, "V7 Volatility (0.8)",     0,  0,  0.8},
    };
    int num_filters = sizeof(filters) / sizeof(filters[0]);

    // Run tests
    printf("Running tests...\n");
    std::vector<Result> results;
    for (int i = 0; i < num_filters; i++) {
        printf("  Testing: %s\n", filters[i].name);
        results.push_back(RunTest(ticks, filters[i]));
    }
    printf("\n");

    // Print results table
    printf("=============================================================\n");
    printf("RESULTS\n");
    printf("=============================================================\n\n");

    printf("%-25s %10s %10s %8s %10s %12s\n",
           "Filter", "Return", "Max DD", "Trades", "Opened", "Blocks");
    printf("---------------------------------------------------------------------\n");

    for (const auto& r : results) {
        printf("%-25s %9.2f%% %9.2f%% %8d %10d %12d\n",
               r.filter_name, r.return_pct, r.max_dd, r.trades,
               r.positions_opened, r.filter_blocks);
    }
    printf("---------------------------------------------------------------------\n\n");

    // Calculate risk-adjusted metrics
    printf("RISK-ADJUSTED METRICS (Return / Max Drawdown):\n");
    printf("---------------------------------------------------------------------\n");
    for (const auto& r : results) {
        double risk_adj = (r.max_dd > 0) ? r.return_pct / r.max_dd : 0;
        printf("%-25s Risk-Adj: %6.3f", r.filter_name, risk_adj);
        if (risk_adj > 0.5) printf(" [GOOD]");
        else if (risk_adj > 0.3) printf(" [OK]");
        else printf(" [POOR]");
        printf("\n");
    }
    printf("---------------------------------------------------------------------\n\n");

    // Find best performers
    Result best_return = results[0];
    Result best_risk_adj = results[0];
    double best_risk_adj_value = 0;

    for (const auto& r : results) {
        if (r.return_pct > best_return.return_pct) {
            best_return = r;
        }
        double risk_adj = (r.max_dd > 0) ? r.return_pct / r.max_dd : 0;
        if (risk_adj > best_risk_adj_value) {
            best_risk_adj_value = risk_adj;
            best_risk_adj = r;
        }
    }

    // RSI vs V7 comparison
    Result rsi_best = results[1];  // Start with RSI 30-70
    for (int i = 1; i <= 3; i++) {
        if (results[i].return_pct > rsi_best.return_pct) {
            rsi_best = results[i];
        }
    }
    Result v7_result = results[4];

    printf("=============================================================\n");
    printf("ANALYSIS\n");
    printf("=============================================================\n\n");

    printf("BEST PERFORMERS:\n");
    printf("- Highest Return:      %s (%.2f%%)\n", best_return.filter_name, best_return.return_pct);
    printf("- Best Risk-Adjusted:  %s (ratio: %.3f)\n", best_risk_adj.filter_name, best_risk_adj_value);
    printf("\n");

    printf("RSI FILTER vs V7 VOLATILITY FILTER:\n");
    printf("---------------------------------------------------------------------\n");
    printf("Best RSI Filter:       %s\n", rsi_best.filter_name);
    printf("  Return: %.2f%%  Max DD: %.2f%%  Trades: %d\n",
           rsi_best.return_pct, rsi_best.max_dd, rsi_best.trades);
    printf("\n");
    printf("V7 Volatility Filter:  %s\n", v7_result.filter_name);
    printf("  Return: %.2f%%  Max DD: %.2f%%  Trades: %d\n",
           v7_result.return_pct, v7_result.max_dd, v7_result.trades);
    printf("\n");

    double rsi_risk = (rsi_best.max_dd > 0) ? rsi_best.return_pct / rsi_best.max_dd : 0;
    double v7_risk = (v7_result.max_dd > 0) ? v7_result.return_pct / v7_result.max_dd : 0;

    if (rsi_risk > v7_risk) {
        printf("WINNER: RSI Filter (%.1f%% better risk-adjusted)\n",
               (rsi_risk - v7_risk) / v7_risk * 100);
    } else if (v7_risk > rsi_risk) {
        printf("WINNER: V7 Volatility Filter (%.1f%% better risk-adjusted)\n",
               (v7_risk - rsi_risk) / rsi_risk * 100);
    } else {
        printf("RESULT: Both filters perform similarly\n");
    }
    printf("\n");

    // Hypothesis validation
    printf("=============================================================\n");
    printf("HYPOTHESIS VALIDATION\n");
    printf("=============================================================\n\n");

    printf("Hypothesis: RSI 40-60 (narrow neutral zone) should perform best\n");
    printf("for mean-reverting grid strategies.\n\n");

    Result no_filter = results[0];
    Result rsi_wide = results[1];
    Result rsi_medium = results[2];
    Result rsi_narrow = results[3];

    bool narrow_beats_wide = rsi_narrow.return_pct > rsi_wide.return_pct;
    bool narrow_beats_baseline = rsi_narrow.return_pct > no_filter.return_pct;
    bool narrow_best_risk = false;

    double narrow_risk = (rsi_narrow.max_dd > 0) ? rsi_narrow.return_pct / rsi_narrow.max_dd : 0;
    double wide_risk = (rsi_wide.max_dd > 0) ? rsi_wide.return_pct / rsi_wide.max_dd : 0;
    double baseline_risk = (no_filter.max_dd > 0) ? no_filter.return_pct / no_filter.max_dd : 0;

    narrow_best_risk = (narrow_risk >= wide_risk && narrow_risk >= baseline_risk);

    printf("Results:\n");
    printf("  - RSI 40-60 beats RSI 30-70 (return):     %s\n",
           narrow_beats_wide ? "YES" : "NO");
    printf("  - RSI 40-60 beats No Filter (return):     %s\n",
           narrow_beats_baseline ? "YES" : "NO");
    printf("  - RSI 40-60 has best risk-adjusted ratio: %s\n",
           narrow_best_risk ? "YES" : "NO");
    printf("\n");

    // Determine which RSI range is actually best
    double rsi_30_70_risk = (rsi_wide.max_dd > 0) ? rsi_wide.return_pct / rsi_wide.max_dd : 0;
    double rsi_35_65_risk = (rsi_medium.max_dd > 0) ? rsi_medium.return_pct / rsi_medium.max_dd : 0;
    double rsi_40_60_risk = (rsi_narrow.max_dd > 0) ? rsi_narrow.return_pct / rsi_narrow.max_dd : 0;

    printf("RSI Range Comparison (Risk-Adjusted):\n");
    printf("  - RSI 30-70: %.3f\n", rsi_30_70_risk);
    printf("  - RSI 35-65: %.3f\n", rsi_35_65_risk);
    printf("  - RSI 40-60: %.3f\n", rsi_40_60_risk);
    printf("\n");

    if (narrow_beats_wide || narrow_best_risk) {
        printf("CONCLUSION: Hypothesis SUPPORTED\n");
        printf("The narrow RSI range (40-60) shows improved performance,\n");
        printf("confirming that grid strategies benefit from trading only\n");
        printf("in neutral/ranging market conditions.\n");
    } else {
        printf("CONCLUSION: Hypothesis NOT SUPPORTED\n");
        printf("The narrow RSI range (40-60) did not outperform wider ranges.\n");
        printf("This may indicate:\n");
        printf("  1. The market conditions were unusual during this period\n");
        printf("  2. RSI neutral zone may be too restrictive (blocks too many trades)\n");
        printf("  3. Volatility-based filtering (V7) may be more effective\n");
    }
    printf("\n");

    printf("=============================================================\n");
    printf("RECOMMENDATIONS\n");
    printf("=============================================================\n\n");

    // Determine optimal filter
    struct {
        const char* name;
        double risk_adj;
    } ranked[5];

    for (int i = 0; i < 5; i++) {
        ranked[i].name = results[i].filter_name;
        ranked[i].risk_adj = (results[i].max_dd > 0) ?
                             results[i].return_pct / results[i].max_dd : 0;
    }

    // Sort by risk-adjusted return
    for (int i = 0; i < 4; i++) {
        for (int j = i + 1; j < 5; j++) {
            if (ranked[j].risk_adj > ranked[i].risk_adj) {
                auto temp = ranked[i];
                ranked[i] = ranked[j];
                ranked[j] = temp;
            }
        }
    }

    printf("Filters Ranked by Risk-Adjusted Performance:\n");
    for (int i = 0; i < 5; i++) {
        printf("  %d. %s (%.3f)\n", i + 1, ranked[i].name, ranked[i].risk_adj);
    }
    printf("\n");

    printf("Based on this analysis:\n");
    if (strcmp(ranked[0].name, "V7 Volatility (0.8)") == 0) {
        printf("- V7 Volatility filter remains the most effective approach\n");
        printf("- RSI filtering provides less benefit than ATR-based volatility\n");
    } else if (strncmp(ranked[0].name, "RSI", 3) == 0) {
        printf("- RSI-based filtering shows promise as an alternative to V7\n");
        printf("- Consider combining RSI and volatility filters for best results\n");
    } else {
        printf("- No filter may be optimal for this specific market period\n");
        printf("- Consider that filters may help more in different market conditions\n");
    }

    printf("\n=============================================================\n");

    return 0;
}
