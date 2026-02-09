/**
 * Grid Spacing Parameter Sweep Test
 *
 * Tests different grid spacing values to find optimal spacing for XAUUSD.
 * Uses V7 volatility filter with parameters (50/1000/0.6).
 *
 * Grid spacing determines:
 * - Distance between grid levels in price points
 * - Smaller spacing = more trades, smaller profits per trade
 * - Larger spacing = fewer trades, bigger moves required for TP
 *
 * Test range: 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 2.5, 3.0
 */

#include <iostream>
#include <iomanip>
#include <vector>
#include <deque>
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstdlib>
#include <cstdio>
#include <cstring>

// Simple Tick structure
struct Tick {
    double bid;
    double ask;
    double spread() const { return ask - bid; }
};

// Simple Trade structure
struct Trade {
    size_t id;
    double entry_price;
    double lot_size;
    double take_profit;
};

// ATR calculator for volatility filter
class ATR {
    std::deque<double> ranges;
    int period;
    double sum;
    double last_price;
public:
    ATR(int p) : period(p), sum(0), last_price(0) {}

    void Add(double price) {
        if (last_price > 0) {
            double range = std::abs(price - last_price);
            ranges.push_back(range);
            sum += range;
            if ((int)ranges.size() > period) {
                sum -= ranges.front();
                ranges.pop_front();
            }
        }
        last_price = price;
    }

    double Get() const {
        return ranges.empty() ? 0 : sum / ranges.size();
    }

    bool IsReady() const {
        return (int)ranges.size() >= period;
    }
};

// Configuration for each test run
struct TestConfig {
    double spacing;

    // Fixed V7 volatility filter parameters (50/1000/0.6)
    int atr_short_period;
    int atr_long_period;
    double volatility_threshold;

    // Fixed protection parameters
    double stop_new_at_dd;
    double partial_close_at_dd;
    double close_all_at_dd;
    int max_positions;
};

// Results structure
struct TestResult {
    double spacing;
    double return_pct;
    double max_dd_pct;
    int total_trades;
    int winning_trades;
    int losing_trades;
    double win_rate;
    double risk_adjusted_score;
    double profit_factor;
    double avg_trade_profit;
    int max_concurrent_positions;
};

// Load ticks from CSV file using C-style I/O
std::vector<Tick> LoadTicks(const char* filename, size_t max_ticks) {
    std::vector<Tick> ticks;

    FILE* file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Error: Cannot open file: %s\n", filename);
        return ticks;
    }

    char line[512];

    // Skip header line
    if (!fgets(line, sizeof(line), file)) {
        fclose(file);
        return ticks;
    }

    // Parse tick data
    while (fgets(line, sizeof(line), file) && ticks.size() < max_ticks) {
        char timestamp[64];
        double bid, ask;

        // Parse tab-separated format: timestamp\tbid\task\t...
        if (sscanf(line, "%63[^\t]\t%lf\t%lf", timestamp, &bid, &ask) >= 3) {
            if (bid > 0 && ask > 0 && ask >= bid) {
                Tick tick;
                tick.bid = bid;
                tick.ask = ask;
                ticks.push_back(tick);
            }
        }

        // Progress indicator
        if (ticks.size() % 100000 == 0 && ticks.size() > 0) {
            printf("  Loaded %zu ticks...\n", ticks.size());
        }
    }

    fclose(file);
    return ticks;
}

// Run backtest with specific configuration
TestResult RunBacktest(const std::vector<Tick>& ticks, const TestConfig& cfg) {
    TestResult result;
    result.spacing = cfg.spacing;
    result.return_pct = 0;
    result.max_dd_pct = 0;
    result.total_trades = 0;
    result.winning_trades = 0;
    result.losing_trades = 0;
    result.win_rate = 0;
    result.risk_adjusted_score = 0;
    result.profit_factor = 0;
    result.avg_trade_profit = 0;
    result.max_concurrent_positions = 0;

    if (ticks.empty()) return result;

    // Account settings
    const double initial_balance = 10000.0;
    const double contract_size = 100.0;  // XAUUSD
    const double leverage = 500.0;
    const double lot_size = 0.01;

    double balance = initial_balance;
    double equity = initial_balance;
    double peak_equity = initial_balance;

    // Trade tracking
    std::vector<Trade*> positions;
    size_t next_id = 1;
    bool partial_done = false;
    bool all_closed = false;

    // ATR calculators for V7 volatility filter
    ATR atr_short(cfg.atr_short_period);
    ATR atr_long(cfg.atr_long_period);

    // Statistics
    double total_gross_profit = 0;
    double total_gross_loss = 0;
    double total_profit = 0;

    for (const Tick& tick : ticks) {
        // Update ATR values
        atr_short.Add(tick.bid);
        atr_long.Add(tick.bid);

        // Calculate equity with unrealized P/L
        equity = balance;
        for (Trade* t : positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * contract_size;
        }

        // Track max concurrent positions
        if ((int)positions.size() > result.max_concurrent_positions) {
            result.max_concurrent_positions = (int)positions.size();
        }

        // Peak equity reset when no positions
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

        // Calculate drawdown percentage
        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        if (dd_pct > result.max_dd_pct) {
            result.max_dd_pct = dd_pct;
        }

        // Protection: Close ALL at threshold
        if (dd_pct > cfg.close_all_at_dd && !all_closed && !positions.empty()) {
            for (Trade* t : positions) {
                double profit = (tick.bid - t->entry_price) * t->lot_size * contract_size;
                balance += profit;
                total_profit += profit;
                if (profit > 0) {
                    result.winning_trades++;
                    total_gross_profit += profit;
                } else {
                    result.losing_trades++;
                    total_gross_loss += std::abs(profit);
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

        // Protection: Partial close at threshold
        if (dd_pct > cfg.partial_close_at_dd && !partial_done && positions.size() > 1) {
            // Sort by P/L (worst first)
            std::sort(positions.begin(), positions.end(), [&](Trade* a, Trade* b) {
                return (tick.bid - a->entry_price) < (tick.bid - b->entry_price);
            });

            int to_close = std::max(1, (int)(positions.size() / 2));
            for (int i = 0; i < to_close && !positions.empty(); i++) {
                double profit = (tick.bid - positions[0]->entry_price) * positions[0]->lot_size * contract_size;
                balance += profit;
                total_profit += profit;
                if (profit > 0) {
                    result.winning_trades++;
                    total_gross_profit += profit;
                } else {
                    result.losing_trades++;
                    total_gross_loss += std::abs(profit);
                }
                result.total_trades++;
                delete positions[0];
                positions.erase(positions.begin());
            }
            partial_done = true;
        }

        // Check Take Profit for all positions
        for (auto it = positions.begin(); it != positions.end();) {
            Trade* t = *it;
            if (tick.bid >= t->take_profit) {
                double profit = (t->take_profit - t->entry_price) * t->lot_size * contract_size;
                balance += profit;
                total_profit += profit;
                if (profit > 0) {
                    result.winning_trades++;
                    total_gross_profit += profit;
                } else {
                    result.losing_trades++;
                    total_gross_loss += std::abs(profit);
                }
                result.total_trades++;
                delete t;
                it = positions.erase(it);
            } else {
                ++it;
            }
        }

        // Check volatility filter (V7)
        bool volatility_ok = true;
        if (atr_short.IsReady() && atr_long.IsReady() && atr_long.Get() > 0) {
            volatility_ok = atr_short.Get() < atr_long.Get() * cfg.volatility_threshold;
        }

        // Open new positions
        if (dd_pct < cfg.stop_new_at_dd && volatility_ok && (int)positions.size() < cfg.max_positions) {
            double lowest = DBL_MAX;
            double highest = DBL_MIN;

            for (Trade* t : positions) {
                if (t->entry_price < lowest) lowest = t->entry_price;
                if (t->entry_price > highest) highest = t->entry_price;
            }

            bool should_open = false;

            if (positions.empty()) {
                should_open = true;
            } else if (lowest >= tick.ask + cfg.spacing) {
                // Price dropped, open new position
                should_open = true;
            } else if (highest <= tick.ask - cfg.spacing) {
                // Price rose, open new position
                should_open = true;
            }

            if (should_open) {
                // Check margin
                double used_margin = 0;
                for (Trade* t : positions) {
                    used_margin += t->lot_size * contract_size * t->entry_price / leverage;
                }
                double margin_needed = lot_size * contract_size * tick.ask / leverage;

                if (equity - used_margin > margin_needed * 2) {
                    Trade* t = new Trade();
                    t->id = next_id++;
                    t->entry_price = tick.ask;
                    t->lot_size = lot_size;
                    // TP = entry + spread + spacing
                    t->take_profit = tick.ask + tick.spread() + cfg.spacing;
                    positions.push_back(t);
                }
            }
        }
    }

    // Close remaining positions at end
    for (Trade* t : positions) {
        double profit = (ticks.back().bid - t->entry_price) * t->lot_size * contract_size;
        balance += profit;
        total_profit += profit;
        if (profit > 0) {
            result.winning_trades++;
            total_gross_profit += profit;
        } else {
            result.losing_trades++;
            total_gross_loss += std::abs(profit);
        }
        result.total_trades++;
        delete t;
    }
    positions.clear();

    // Calculate final metrics
    result.return_pct = (balance - initial_balance) / initial_balance * 100.0;

    if (result.total_trades > 0) {
        result.win_rate = (double)result.winning_trades / result.total_trades * 100.0;
        result.avg_trade_profit = total_profit / result.total_trades;
    }

    if (total_gross_loss > 0) {
        result.profit_factor = total_gross_profit / total_gross_loss;
    } else if (total_gross_profit > 0) {
        result.profit_factor = 999.99;  // All winners
    }

    // Risk-adjusted score: Return / MaxDD (Calmar-like ratio)
    if (result.max_dd_pct > 0) {
        result.risk_adjusted_score = result.return_pct / result.max_dd_pct;
    } else if (result.return_pct > 0) {
        result.risk_adjusted_score = result.return_pct * 10;  // Very good if no drawdown
    }

    return result;
}

int main() {
    printf("================================================================\n");
    printf("     GRID SPACING PARAMETER SWEEP - XAUUSD OPTIMIZATION\n");
    printf("================================================================\n");
    printf("\n");

    // Load tick data
    const char* filename = "Grid/XAUUSD_TICKS_2025.csv";
    const size_t max_ticks = 500000;

    printf("Loading tick data from: %s\n", filename);
    printf("Max ticks to load: %zu\n", max_ticks);
    printf("\n");

    std::vector<Tick> ticks = LoadTicks(filename, max_ticks);

    if (ticks.empty()) {
        fprintf(stderr, "Error: Failed to load tick data!\n");
        return 1;
    }

    printf("Loaded %zu ticks successfully\n", ticks.size());
    printf("Price range: %.2f -> %.2f\n", ticks.front().bid, ticks.back().bid);
    printf("Avg spread: %.2f\n", (ticks.front().spread() + ticks.back().spread()) / 2);
    printf("\n");

    // Spacing values to test
    double spacing_values[] = {0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 2.5, 3.0};
    int num_spacings = sizeof(spacing_values) / sizeof(spacing_values[0]);

    // V7 volatility filter parameters (fixed at 50/1000/0.6)
    const int ATR_SHORT = 50;
    const int ATR_LONG = 1000;
    const double VOL_THRESHOLD = 0.6;

    printf("V7 Volatility Filter: ATR_Short=%d, ATR_Long=%d, Threshold=%.1f\n",
           ATR_SHORT, ATR_LONG, VOL_THRESHOLD);
    printf("Protection: StopNew=5%%, PartialClose=8%%, CloseAll=25%%\n");
    printf("\n");
    printf("Testing %d spacing values: ", num_spacings);
    for (int i = 0; i < num_spacings; i++) {
        printf("%.2f", spacing_values[i]);
        if (i < num_spacings - 1) printf(", ");
    }
    printf("\n\n");

    // Store results
    std::vector<TestResult> results;

    // Run tests for each spacing
    printf("Running backtests...\n");
    printf("----------------------------------------------------------------\n");

    for (int i = 0; i < num_spacings; i++) {
        TestConfig cfg;
        cfg.spacing = spacing_values[i];
        cfg.atr_short_period = ATR_SHORT;
        cfg.atr_long_period = ATR_LONG;
        cfg.volatility_threshold = VOL_THRESHOLD;
        cfg.stop_new_at_dd = 5.0;
        cfg.partial_close_at_dd = 8.0;
        cfg.close_all_at_dd = 25.0;
        cfg.max_positions = 20;

        printf("  Testing spacing = %.2f... ", cfg.spacing);
        fflush(stdout);

        TestResult r = RunBacktest(ticks, cfg);
        results.push_back(r);

        printf("Return: %+.2f%%, MaxDD: %.2f%%, Trades: %d\n",
               r.return_pct, r.max_dd_pct, r.total_trades);
    }

    printf("----------------------------------------------------------------\n");
    printf("\n");

    // Print detailed results table
    printf("DETAILED RESULTS:\n");
    printf("================================================================================\n");
    printf("%-8s | %10s | %8s | %8s | %8s | %10s | %10s\n",
           "Spacing", "Return", "Max DD", "Trades", "Win Rate", "PF", "Risk-Adj");
    printf("--------------------------------------------------------------------------------\n");

    for (const TestResult& r : results) {
        printf("%-8.2f | %+9.2f%% | %7.2f%% | %8d | %7.1f%% | %10.2f | %10.2f\n",
               r.spacing, r.return_pct, r.max_dd_pct, r.total_trades,
               r.win_rate, r.profit_factor, r.risk_adjusted_score);
    }

    printf("================================================================================\n");
    printf("\n");

    // Find optimal spacing by different metrics
    printf("OPTIMAL SPACING ANALYSIS:\n");
    printf("--------------------------------------------------------------------------------\n");

    // Best by Return
    TestResult* best_return = &results[0];
    for (TestResult& r : results) {
        if (r.return_pct > best_return->return_pct) best_return = &r;
    }
    printf("  Best by RETURN:          Spacing=%.2f (Return: %+.2f%%)\n",
           best_return->spacing, best_return->return_pct);

    // Best by Risk-Adjusted Score
    TestResult* best_risk_adj = &results[0];
    for (TestResult& r : results) {
        if (r.risk_adjusted_score > best_risk_adj->risk_adjusted_score) best_risk_adj = &r;
    }
    printf("  Best by RISK-ADJUSTED:   Spacing=%.2f (Score: %.2f, Return/DD)\n",
           best_risk_adj->spacing, best_risk_adj->risk_adjusted_score);

    // Best by Lowest Drawdown (among profitable configs)
    TestResult* best_dd = NULL;
    for (TestResult& r : results) {
        if (r.return_pct > 0) {
            if (best_dd == NULL || r.max_dd_pct < best_dd->max_dd_pct) {
                best_dd = &r;
            }
        }
    }
    if (best_dd) {
        printf("  Best by LOWEST DD:       Spacing=%.2f (MaxDD: %.2f%%, Return: %+.2f%%)\n",
               best_dd->spacing, best_dd->max_dd_pct, best_dd->return_pct);
    }

    // Best by Win Rate (among profitable configs)
    TestResult* best_win = NULL;
    for (TestResult& r : results) {
        if (r.return_pct > 0) {
            if (best_win == NULL || r.win_rate > best_win->win_rate) {
                best_win = &r;
            }
        }
    }
    if (best_win) {
        printf("  Best by WIN RATE:        Spacing=%.2f (WinRate: %.1f%%, Return: %+.2f%%)\n",
               best_win->spacing, best_win->win_rate, best_win->return_pct);
    }

    // Best by Profit Factor (among profitable configs)
    TestResult* best_pf = NULL;
    for (TestResult& r : results) {
        if (r.return_pct > 0 && r.profit_factor < 100) {  // Exclude infinite PF
            if (best_pf == NULL || r.profit_factor > best_pf->profit_factor) {
                best_pf = &r;
            }
        }
    }
    if (best_pf) {
        printf("  Best by PROFIT FACTOR:   Spacing=%.2f (PF: %.2f, Return: %+.2f%%)\n",
               best_pf->spacing, best_pf->profit_factor, best_pf->return_pct);
    }

    printf("--------------------------------------------------------------------------------\n");
    printf("\n");

    // Final recommendation
    printf("================================================================================\n");
    printf("RECOMMENDATION:\n");
    printf("================================================================================\n");

    // Use risk-adjusted score as primary metric
    printf("\n");
    printf("  OPTIMAL SPACING: %.2f\n", best_risk_adj->spacing);
    printf("\n");
    printf("  Expected Performance:\n");
    printf("    - Return:              %+.2f%%\n", best_risk_adj->return_pct);
    printf("    - Max Drawdown:        %.2f%%\n", best_risk_adj->max_dd_pct);
    printf("    - Total Trades:        %d\n", best_risk_adj->total_trades);
    printf("    - Win Rate:            %.1f%%\n", best_risk_adj->win_rate);
    printf("    - Profit Factor:       %.2f\n", best_risk_adj->profit_factor);
    printf("    - Risk-Adj Score:      %.2f\n", best_risk_adj->risk_adjusted_score);
    printf("    - Max Open Positions:  %d\n", best_risk_adj->max_concurrent_positions);
    printf("\n");

    // Compare with default spacing (1.0)
    TestResult* default_result = NULL;
    for (TestResult& r : results) {
        if (std::abs(r.spacing - 1.0) < 0.01) {
            default_result = &r;
            break;
        }
    }

    if (default_result && std::abs(default_result->spacing - best_risk_adj->spacing) > 0.01) {
        printf("  COMPARISON WITH DEFAULT (Spacing=1.0):\n");
        printf("    Default: Return=%+.2f%%, MaxDD=%.2f%%, Score=%.2f\n",
               default_result->return_pct, default_result->max_dd_pct,
               default_result->risk_adjusted_score);
        printf("    Optimal: Return=%+.2f%%, MaxDD=%.2f%%, Score=%.2f\n",
               best_risk_adj->return_pct, best_risk_adj->max_dd_pct,
               best_risk_adj->risk_adjusted_score);

        double return_improvement = best_risk_adj->return_pct - default_result->return_pct;
        double dd_improvement = default_result->max_dd_pct - best_risk_adj->max_dd_pct;
        double score_improvement = best_risk_adj->risk_adjusted_score - default_result->risk_adjusted_score;

        printf("\n");
        printf("    Return improvement:    %+.2f%%\n", return_improvement);
        printf("    Drawdown improvement:  %+.2f%% (positive = less DD)\n", dd_improvement);
        printf("    Score improvement:     %+.2f\n", score_improvement);
    }

    printf("================================================================================\n");
    printf("\n");

    // Trade-off analysis
    printf("TRADE-OFF ANALYSIS:\n");
    printf("--------------------------------------------------------------------------------\n");
    printf("  Smaller spacing (0.5-0.75):\n");
    printf("    + More trades = more opportunities\n");
    printf("    + Smaller moves needed for profit\n");
    printf("    - Higher transaction costs relative to profit\n");
    printf("    - More sensitive to spread/slippage\n");
    printf("\n");
    printf("  Larger spacing (2.0-3.0):\n");
    printf("    + Larger profits per trade\n");
    printf("    + Less sensitive to spread\n");
    printf("    - Fewer trades = fewer opportunities\n");
    printf("    - Requires larger price movements\n");
    printf("    - May miss smaller oscillations\n");
    printf("--------------------------------------------------------------------------------\n");

    return 0;
}
