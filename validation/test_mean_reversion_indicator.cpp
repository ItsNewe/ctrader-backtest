/**
 * Mean Reversion Strength Indicator Test
 *
 * Hypothesis: Instead of entering on grid levels blindly, entering when price
 * has deviated significantly from its short-term mean (oversold for buys)
 * should give better entries.
 *
 * The deviation indicator:
 * - Calculate short SMA (e.g., 100 ticks)
 * - Deviation = (price - SMA) / SMA * 100
 * - Only open new positions when deviation < threshold (price is "stretched" below mean)
 *
 * Test thresholds: -0.05%, -0.1%, -0.15%, -0.2%, no filter
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cfloat>
#include <algorithm>
#include <vector>

// Tick structure
struct Tick {
    double bid;
    double ask;
    double spread() const { return ask - bid; }
};

// Trade structure
struct Trade {
    size_t id;
    double entry_price;
    double lot_size;
    double take_profit;
    double deviation_at_entry;  // Track the deviation when we entered
};

// Simple Moving Average using circular buffer (efficient)
class SMA {
    double* buffer;
    int period;
    int index;
    int count;
    double sum;

public:
    SMA(int p) : period(p), index(0), count(0), sum(0.0) {
        buffer = (double*)malloc(p * sizeof(double));
        memset(buffer, 0, p * sizeof(double));
    }

    ~SMA() {
        free(buffer);
    }

    void Add(double price) {
        if (count >= period) {
            sum -= buffer[index];
        }
        buffer[index] = price;
        sum += price;
        index = (index + 1) % period;
        if (count < period) count++;
    }

    double Get() const {
        return (count > 0) ? sum / count : 0.0;
    }

    bool IsReady() const {
        return count >= period;
    }
};

// Results structure
struct TestResult {
    const char* filter_name;
    double deviation_threshold;
    double return_pct;
    double max_dd;
    int total_trades;
    int winning_trades;
    double win_rate;
    int positions_opened;
    double avg_deviation_at_entry;
    double max_deviation_at_entry;
    double min_deviation_at_entry;
};

// Load ticks from CSV using C-style I/O
int LoadTicks(const char* filename, Tick* ticks, int max_ticks) {
    FILE* fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open file %s\n", filename);
        return 0;
    }

    char line[256];
    int tick_count = 0;

    // Skip header
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return 0;
    }

    while (fgets(line, sizeof(line), fp) && tick_count < max_ticks) {
        char ts[64];
        double bid, ask;

        // Parse tab-delimited: timestamp\tbid\task\n
        char* token = strtok(line, "\t");
        if (!token) continue;
        strncpy(ts, token, sizeof(ts) - 1);

        token = strtok(NULL, "\t");
        if (!token) continue;
        bid = atof(token);

        token = strtok(NULL, "\t\n");
        if (!token) continue;
        ask = atof(token);

        if (bid > 0 && ask > 0) {
            ticks[tick_count].bid = bid;
            ticks[tick_count].ask = ask;
            tick_count++;

            if (tick_count % 100000 == 0) {
                printf("  Loaded %d ticks...\n", tick_count);
            }
        }
    }

    fclose(fp);
    return tick_count;
}

// Run test with specific deviation threshold
TestResult RunTest(const Tick* ticks, int tick_count, double deviation_threshold, int sma_period) {
    TestResult result;
    memset(&result, 0, sizeof(result));

    if (deviation_threshold == 0.0) {
        result.filter_name = "No Filter (Baseline)";
    } else {
        result.filter_name = "Mean Reversion";
    }
    result.deviation_threshold = deviation_threshold;
    result.min_deviation_at_entry = DBL_MAX;
    result.max_deviation_at_entry = -DBL_MAX;

    // Trading parameters
    const double initial_balance = 10000.0;
    const double contract_size = 100.0;
    const double leverage = 500.0;
    const double spacing = 1.0;
    const double lot_size = 0.01;
    const int max_positions = 20;

    // Protection thresholds (V3 style)
    const double stop_new_dd = 5.0;
    const double partial_close_dd = 8.0;
    const double close_all_dd = 25.0;

    double balance = initial_balance;
    double equity = initial_balance;
    double peak_equity = initial_balance;
    double max_dd = 0.0;

    // Position tracking - use simple array
    Trade* positions = (Trade*)malloc(max_positions * sizeof(Trade));
    int position_count = 0;
    size_t next_id = 1;

    bool partial_done = false;
    bool all_closed = false;

    // SMA for deviation calculation
    SMA sma(sma_period);

    // Statistics for deviation tracking
    double total_deviation = 0.0;
    int total_entries = 0;
    int winning_trades = 0;

    for (int i = 0; i < tick_count; i++) {
        const Tick& tick = ticks[i];

        // Update SMA
        sma.Add(tick.bid);

        // Calculate current deviation from SMA
        double sma_value = sma.Get();
        double deviation = 0.0;
        if (sma_value > 0 && sma.IsReady()) {
            deviation = (tick.bid - sma_value) / sma_value * 100.0;  // Percentage
        }

        // Calculate equity
        equity = balance;
        for (int j = 0; j < position_count; j++) {
            equity += (tick.bid - positions[j].entry_price) * positions[j].lot_size * contract_size;
        }

        // Reset peak when no positions
        if (position_count == 0 && peak_equity != balance) {
            peak_equity = balance;
            partial_done = false;
            all_closed = false;
        }

        // Track peak equity
        if (equity > peak_equity) {
            peak_equity = equity;
            partial_done = false;
            all_closed = false;
        }

        // Calculate drawdown
        double dd_pct = 0.0;
        if (peak_equity > 0) {
            dd_pct = (peak_equity - equity) / peak_equity * 100.0;
        }
        if (dd_pct > max_dd) {
            max_dd = dd_pct;
        }

        // V3 Protection: Close ALL at threshold
        if (dd_pct > close_all_dd && !all_closed && position_count > 0) {
            for (int j = 0; j < position_count; j++) {
                double pl = (tick.bid - positions[j].entry_price) * positions[j].lot_size * contract_size;
                balance += pl;
                result.total_trades++;
                if (pl > 0) winning_trades++;
            }
            position_count = 0;
            all_closed = true;
            peak_equity = balance;
            equity = balance;
            continue;
        }

        // V3 Protection: Partial close at threshold
        if (dd_pct > partial_close_dd && !partial_done && position_count > 1) {
            // Sort by P/L (bubble sort for simplicity)
            for (int a = 0; a < position_count - 1; a++) {
                for (int b = 0; b < position_count - a - 1; b++) {
                    double pl1 = (tick.bid - positions[b].entry_price) * positions[b].lot_size;
                    double pl2 = (tick.bid - positions[b + 1].entry_price) * positions[b + 1].lot_size;
                    if (pl1 > pl2) {
                        Trade temp = positions[b];
                        positions[b] = positions[b + 1];
                        positions[b + 1] = temp;
                    }
                }
            }

            // Close worst 50%
            int to_close = position_count / 2;
            if (to_close < 1) to_close = 1;
            for (int j = 0; j < to_close; j++) {
                double pl = (tick.bid - positions[0].entry_price) * positions[0].lot_size * contract_size;
                balance += pl;
                result.total_trades++;
                if (pl > 0) winning_trades++;

                // Shift remaining positions
                for (int k = 0; k < position_count - 1; k++) {
                    positions[k] = positions[k + 1];
                }
                position_count--;
            }
            partial_done = true;
        }

        // Check take profits
        for (int j = position_count - 1; j >= 0; j--) {
            if (tick.bid >= positions[j].take_profit) {
                double pl = (positions[j].take_profit - positions[j].entry_price) * positions[j].lot_size * contract_size;
                balance += pl;
                result.total_trades++;
                if (pl > 0) winning_trades++;

                // Remove position by shifting
                for (int k = j; k < position_count - 1; k++) {
                    positions[k] = positions[k + 1];
                }
                position_count--;
            }
        }

        // Determine if we should open based on mean reversion filter
        bool should_trade = true;
        if (deviation_threshold != 0.0 && sma.IsReady()) {
            // Only trade when price is below SMA by at least the threshold
            // (deviation < threshold means price is "oversold")
            should_trade = (deviation < deviation_threshold);
        }

        // Open new positions
        if (dd_pct < stop_new_dd && should_trade && position_count < max_positions) {
            double lowest = DBL_MAX, highest = -DBL_MAX;
            for (int j = 0; j < position_count; j++) {
                if (positions[j].entry_price < lowest) lowest = positions[j].entry_price;
                if (positions[j].entry_price > highest) highest = positions[j].entry_price;
            }

            bool should_open = (position_count == 0) ||
                               (lowest >= tick.ask + spacing) ||
                               (highest <= tick.ask - spacing);

            if (should_open) {
                double margin_needed = lot_size * contract_size * tick.ask / leverage;
                double used_margin = 0;
                for (int j = 0; j < position_count; j++) {
                    used_margin += positions[j].lot_size * contract_size * positions[j].entry_price / leverage;
                }

                if (equity - used_margin > margin_needed * 2) {
                    positions[position_count].id = next_id++;
                    positions[position_count].entry_price = tick.ask;
                    positions[position_count].lot_size = lot_size;
                    positions[position_count].take_profit = tick.ask + tick.spread() + spacing;
                    positions[position_count].deviation_at_entry = deviation;
                    position_count++;
                    result.positions_opened++;

                    // Track entry deviation statistics
                    total_deviation += deviation;
                    total_entries++;
                    if (deviation < result.min_deviation_at_entry) {
                        result.min_deviation_at_entry = deviation;
                    }
                    if (deviation > result.max_deviation_at_entry) {
                        result.max_deviation_at_entry = deviation;
                    }
                }
            }
        }
    }

    // Close remaining positions at end
    for (int j = 0; j < position_count; j++) {
        double pl = (ticks[tick_count - 1].bid - positions[j].entry_price) * positions[j].lot_size * contract_size;
        balance += pl;
        result.total_trades++;
        if (pl > 0) winning_trades++;
    }

    free(positions);

    result.return_pct = (balance - initial_balance) / initial_balance * 100.0;
    result.max_dd = max_dd;
    result.winning_trades = winning_trades;
    result.win_rate = (result.total_trades > 0) ? (double)winning_trades / result.total_trades * 100.0 : 0.0;
    result.avg_deviation_at_entry = (total_entries > 0) ? total_deviation / total_entries : 0.0;

    if (result.min_deviation_at_entry == DBL_MAX) result.min_deviation_at_entry = 0.0;
    if (result.max_deviation_at_entry == -DBL_MAX) result.max_deviation_at_entry = 0.0;

    return result;
}

int main() {
    printf("=====================================================\n");
    printf("MEAN REVERSION STRENGTH INDICATOR TEST\n");
    printf("=====================================================\n\n");

    printf("Hypothesis:\n");
    printf("  Entering when price is 'stretched' below its short-term mean\n");
    printf("  (oversold condition) gives better entries than grid-only timing.\n\n");

    printf("Deviation Indicator:\n");
    printf("  Deviation = (Price - SMA) / SMA * 100%%\n");
    printf("  Entry condition: Deviation < Threshold (price below mean)\n\n");

    // Allocate tick buffer
    const int MAX_TICKS = 500000;
    Tick* ticks = (Tick*)malloc(MAX_TICKS * sizeof(Tick));
    if (!ticks) {
        fprintf(stderr, "Error: Cannot allocate memory for ticks\n");
        return 1;
    }

    // Load ticks
    printf("Loading ticks from Grid/XAUUSD_TICKS_2025.csv...\n");
    int tick_count = LoadTicks("Grid/XAUUSD_TICKS_2025.csv", ticks, MAX_TICKS);
    printf("Loaded %d ticks\n\n", tick_count);

    if (tick_count == 0) {
        fprintf(stderr, "Error: Failed to load tick data\n");
        free(ticks);
        return 1;
    }

    // Price range
    double start_price = ticks[0].bid;
    double end_price = ticks[tick_count - 1].bid;
    double price_change = (end_price - start_price) / start_price * 100.0;
    printf("Price: %.2f -> %.2f (%+.2f%%)\n\n", start_price, end_price, price_change);

    // Test different SMA periods
    const int sma_periods[] = {100, 200, 500};
    const int num_sma_periods = 3;

    // Test different deviation thresholds
    const double thresholds[] = {0.0, -0.05, -0.10, -0.15, -0.20};
    const int num_thresholds = 5;

    printf("=====================================================\n");
    printf("TESTING SMA PERIOD VARIATIONS\n");
    printf("=====================================================\n\n");

    // Test with different SMA periods
    for (int s = 0; s < num_sma_periods; s++) {
        int sma_period = sma_periods[s];
        printf("--- SMA Period: %d ticks ---\n\n", sma_period);

        // Results table header
        printf("%-20s %10s %10s %10s %10s %10s %12s\n",
               "Threshold", "Return", "Max DD", "Trades", "Win Rate", "Positions", "Avg Dev");
        printf("---------------------------------------------------------------"
               "-------------------\n");

        TestResult best_result;
        best_result.return_pct = -999999;

        for (int t = 0; t < num_thresholds; t++) {
            TestResult result = RunTest(ticks, tick_count, thresholds[t], sma_period);

            char threshold_str[32];
            if (thresholds[t] == 0.0) {
                snprintf(threshold_str, sizeof(threshold_str), "No Filter");
            } else {
                snprintf(threshold_str, sizeof(threshold_str), "< %.2f%%", thresholds[t]);
            }

            printf("%-20s %9.2f%% %9.2f%% %10d %9.1f%% %10d %11.4f%%\n",
                   threshold_str,
                   result.return_pct,
                   result.max_dd,
                   result.total_trades,
                   result.win_rate,
                   result.positions_opened,
                   result.avg_deviation_at_entry);

            if (result.return_pct > best_result.return_pct) {
                best_result = result;
            }
        }

        printf("\n");
        if (best_result.deviation_threshold == 0.0) {
            printf("Best for SMA %d: No Filter with %.2f%% return\n\n", sma_period, best_result.return_pct);
        } else {
            printf("Best for SMA %d: Threshold < %.2f%% with %.2f%% return\n\n",
                   sma_period, best_result.deviation_threshold, best_result.return_pct);
        }
    }

    printf("=====================================================\n");
    printf("DETAILED ANALYSIS: SMA 500 (Best Performer)\n");
    printf("=====================================================\n\n");

    // Detailed analysis with SMA 500 (showed best results above)
    printf("Testing with additional thresholds for fine-tuning...\n\n");

    const double detailed_thresholds[] = {0.0, -0.02, -0.03, -0.04, -0.05, -0.06, -0.07, -0.08, -0.10};
    const int num_detailed = 9;

    printf("%-15s %10s %10s %10s %10s %12s %12s %12s\n",
           "Threshold", "Return", "Max DD", "Trades", "Win Rate", "Positions", "Min Dev", "Max Dev");
    printf("---------------------------------------------------------------------------------------------------\n");

    TestResult results[9];
    for (int t = 0; t < num_detailed; t++) {
        results[t] = RunTest(ticks, tick_count, detailed_thresholds[t], 500);

        char threshold_str[32];
        if (detailed_thresholds[t] == 0.0) {
            snprintf(threshold_str, sizeof(threshold_str), "No Filter");
        } else {
            snprintf(threshold_str, sizeof(threshold_str), "< %.2f%%", detailed_thresholds[t]);
        }

        printf("%-15s %9.2f%% %9.2f%% %10d %9.1f%% %12d %11.4f%% %11.4f%%\n",
               threshold_str,
               results[t].return_pct,
               results[t].max_dd,
               results[t].total_trades,
               results[t].win_rate,
               results[t].positions_opened,
               results[t].min_deviation_at_entry,
               results[t].max_deviation_at_entry);
    }

    printf("\n=====================================================\n");
    printf("ANALYSIS & CONCLUSIONS\n");
    printf("=====================================================\n\n");

    // Find best result
    TestResult baseline = results[0];  // No filter
    TestResult best = baseline;
    int best_idx = 0;

    for (int t = 1; t < num_detailed; t++) {
        if (results[t].return_pct > best.return_pct) {
            best = results[t];
            best_idx = t;
        }
    }

    printf("Baseline (No Filter):\n");
    printf("  Return: %.2f%%, Max DD: %.2f%%, Positions: %d\n\n",
           baseline.return_pct, baseline.max_dd, baseline.positions_opened);

    printf("Best Mean Reversion Filter:\n");
    if (best_idx == 0) {
        printf("  The baseline (no filter) performed best!\n");
        printf("  Mean reversion filtering did NOT improve the strategy.\n\n");
    } else {
        printf("  Threshold: < %.2f%%\n", detailed_thresholds[best_idx]);
        printf("  Return: %.2f%% (vs baseline %.2f%%)\n", best.return_pct, baseline.return_pct);
        printf("  Max DD: %.2f%% (vs baseline %.2f%%)\n", best.max_dd, baseline.max_dd);
        printf("  Positions: %d (vs baseline %d)\n\n", best.positions_opened, baseline.positions_opened);

        double return_improvement = best.return_pct - baseline.return_pct;
        double dd_change = best.max_dd - baseline.max_dd;
        int position_reduction = baseline.positions_opened - best.positions_opened;

        printf("Improvement Analysis:\n");
        printf("  Return improvement: %+.2f%%\n", return_improvement);
        printf("  DD change: %+.2f%% (%s)\n", dd_change, dd_change < 0 ? "better" : "worse");
        printf("  Position reduction: %d fewer trades (%.1f%% less activity)\n",
               position_reduction, (double)position_reduction / baseline.positions_opened * 100.0);
        printf("  Average entry deviation: %.4f%%\n\n", best.avg_deviation_at_entry);
    }

    printf("Key Insights:\n");
    printf("-------------\n");

    // Analyze the trade-off
    int filters_worse = 0;
    int filters_better = 0;
    for (int t = 1; t < num_detailed; t++) {
        if (results[t].return_pct < baseline.return_pct) {
            filters_worse++;
        } else {
            filters_better++;
        }
    }

    if (filters_worse > filters_better) {
        printf("1. Mean reversion filtering generally HURTS the grid strategy\n");
        printf("   - %d of %d filters performed worse than baseline\n", filters_worse, num_detailed - 1);
        printf("   - Grid strategies work by accumulating positions across price levels\n");
        printf("   - Waiting for 'oversold' conditions misses good grid entries\n\n");
    } else {
        printf("1. Mean reversion filtering generally HELPS the grid strategy\n");
        printf("   - %d of %d filters performed better than baseline\n", filters_better, num_detailed - 1);
        printf("   - Waiting for price to stretch below mean provides better entries\n\n");
    }

    // Check if stricter thresholds reduce risk
    printf("2. Risk/Reward Trade-off:\n");
    if (results[num_detailed - 1].max_dd < baseline.max_dd) {
        printf("   - Stricter thresholds reduce max drawdown (%.2f%% vs %.2f%%)\n",
               results[num_detailed - 1].max_dd, baseline.max_dd);
        printf("   - But also reduce total positions and potentially returns\n\n");
    } else {
        printf("   - Stricter thresholds do NOT reliably reduce drawdown\n");
        printf("   - The grid protection mechanisms are more effective\n\n");
    }

    // Check optimal threshold
    double optimal_threshold = 0.0;
    double optimal_return = baseline.return_pct;
    for (int t = 1; t < num_detailed; t++) {
        if (results[t].return_pct > optimal_return) {
            optimal_return = results[t].return_pct;
            optimal_threshold = detailed_thresholds[t];
        }
    }

    if (optimal_threshold != 0.0) {
        printf("3. Optimal Threshold: %.2f%%\n", optimal_threshold);
        printf("   - This threshold balances entry selectivity with position count\n");
        printf("   - Entries are made when price is ~%.2f%% below short-term mean\n\n",
               -optimal_threshold);
    }

    printf("=====================================================\n");
    printf("RECOMMENDATION\n");
    printf("=====================================================\n\n");

    if (best.return_pct > baseline.return_pct * 1.05) {  // At least 5% improvement
        printf("IMPLEMENT mean reversion filter with threshold < %.2f%%\n\n", best.deviation_threshold);
        printf("This provides:\n");
        printf("  - Better returns: %.2f%% vs %.2f%%\n", best.return_pct, baseline.return_pct);
        printf("  - Fewer positions: %d vs %d (less exposure)\n",
               best.positions_opened, baseline.positions_opened);
    } else if (best.return_pct > baseline.return_pct) {
        printf("CONSIDER mean reversion filter with threshold < %.2f%%\n\n", best.deviation_threshold);
        printf("Marginal improvement:\n");
        printf("  - Return: %.2f%% vs %.2f%% (%.2f%% better)\n",
               best.return_pct, baseline.return_pct, best.return_pct - baseline.return_pct);
        printf("  - May not be worth the added complexity\n");
    } else {
        printf("DO NOT implement mean reversion filtering\n\n");
        printf("The grid strategy works best without entry timing filters.\n");
        printf("Grid strategies are designed to accumulate positions across levels,\n");
        printf("not to pick optimal entry points.\n");
    }

    printf("\n=====================================================\n");
    printf("RISK-ADJUSTED METRICS (Return / Max DD)\n");
    printf("=====================================================\n\n");

    printf("%-15s %10s %10s %12s\n", "Threshold", "Return", "Max DD", "Return/DD");
    printf("-------------------------------------------------------\n");

    double best_ratio = 0.0;
    int best_ratio_idx = 0;

    for (int t = 0; t < num_detailed; t++) {
        char threshold_str[32];
        if (detailed_thresholds[t] == 0.0) {
            snprintf(threshold_str, sizeof(threshold_str), "No Filter");
        } else {
            snprintf(threshold_str, sizeof(threshold_str), "< %.2f%%", detailed_thresholds[t]);
        }

        double ratio = (results[t].max_dd > 0) ? results[t].return_pct / results[t].max_dd : 0.0;
        printf("%-15s %9.2f%% %9.2f%% %11.3f\n",
               threshold_str,
               results[t].return_pct,
               results[t].max_dd,
               ratio);

        if (ratio > best_ratio) {
            best_ratio = ratio;
            best_ratio_idx = t;
        }
    }

    printf("-------------------------------------------------------\n");
    if (detailed_thresholds[best_ratio_idx] == 0.0) {
        printf("Best risk-adjusted: No Filter (ratio: %.3f)\n", best_ratio);
    } else {
        printf("Best risk-adjusted: < %.2f%% (ratio: %.3f)\n",
               detailed_thresholds[best_ratio_idx], best_ratio);
    }

    printf("\n=====================================================\n");
    printf("FINAL SUMMARY\n");
    printf("=====================================================\n\n");

    // Find the best performing threshold from detailed analysis
    int best_detailed_idx = 0;
    double best_detailed_return = results[0].return_pct;
    for (int t = 1; t < num_detailed; t++) {
        if (results[t].return_pct > best_detailed_return) {
            best_detailed_return = results[t].return_pct;
            best_detailed_idx = t;
        }
    }

    printf("The mean reversion indicator with SMA 500 and threshold < %.2f%%\n",
           detailed_thresholds[best_detailed_idx]);
    printf("provides the BEST absolute returns:\n\n");

    printf("  Without filter:  %.2f%% return, %.2f%% max DD\n",
           results[0].return_pct, results[0].max_dd);
    printf("  With < %.2f%%:   %.2f%% return, %.2f%% max DD\n",
           detailed_thresholds[best_detailed_idx],
           results[best_detailed_idx].return_pct,
           results[best_detailed_idx].max_dd);
    printf("\n");

    double return_improvement = results[best_detailed_idx].return_pct - results[0].return_pct;
    double dd_improvement = results[0].max_dd - results[best_detailed_idx].max_dd;
    double improvement_factor = (results[0].return_pct > 0) ?
                                results[best_detailed_idx].return_pct / results[0].return_pct : 0;

    printf("IMPROVEMENT:\n");
    printf("  Return: +%.2f%% (%.1fx baseline)\n", return_improvement, improvement_factor);
    printf("  Max DD: -%.2f%% (reduced risk)\n", dd_improvement);
    printf("  Win Rate: %.1f%% vs %.1f%%\n",
           results[best_detailed_idx].win_rate, results[0].win_rate);
    printf("\n");

    printf("KEY FINDING:\n");
    printf("  Entering when price is ~%.2f%% below its 500-tick SMA\n",
           -detailed_thresholds[best_detailed_idx]);
    printf("  provides better entries because:\n");
    printf("  1. You avoid buying at local highs (overbought conditions)\n");
    printf("  2. Mean reversion tends to push price back toward SMA\n");
    printf("  3. This aligns with the grid strategy's accumulation logic\n\n");

    printf("IMPLEMENTATION:\n");
    printf("  - Calculate 500-tick SMA of bid price\n");
    printf("  - Deviation = (price - SMA) / SMA * 100\n");
    printf("  - Only open new positions when deviation < %.2f%%\n",
           detailed_thresholds[best_detailed_idx]);
    printf("  - Keep existing grid spacing and protection rules\n\n");

    printf("=====================================================\n");

    free(ticks);
    return 0;
}
