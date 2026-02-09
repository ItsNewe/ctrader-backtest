/**
 * Session Filter Test for Grid Strategy
 *
 * Hypothesis: Grid strategies work better during specific market sessions
 * - Asian session (0-8 UTC): tends to be ranging/consolidating
 * - London session (8-16 UTC): more trending/volatile
 * - NY session (13-21 UTC): most volatile, trends + reversals
 *
 * This test filters trades by time of day to find optimal trading windows.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cfloat>
#include <vector>
#include <algorithm>

// Simple tick structure
struct Tick {
    char timestamp[32];
    double bid;
    double ask;
    int hour;  // 0-23 UTC extracted from timestamp
};

// Simple trade structure
struct Trade {
    int id;
    double entry_price;
    double take_profit;
    double lot_size;
    char entry_time[32];
};

// Session filter configuration
struct SessionFilter {
    const char* name;
    int start_hour;  // inclusive
    int end_hour;    // exclusive
};

// Test result structure
struct TestResult {
    const char* session_name;
    double final_balance;
    double return_pct;
    double max_drawdown_pct;
    int total_trades;
    int winning_trades;
    int losing_trades;
    double win_rate;
    double profit_factor;
    double sharpe_ratio;
    int max_positions;
    int ticks_traded;
};

// Grid strategy parameters
const double SPACING = 1.0;
const double LOT_SIZE = 0.01;
const double CONTRACT_SIZE = 100.0;  // XAUUSD
const double LEVERAGE = 500.0;
const double INITIAL_BALANCE = 10000.0;
const int MAX_POSITIONS = 20;

// Parse hour from timestamp: "2025.01.02 01:00:02.600"
int ParseHour(const char* timestamp) {
    // Find the space, then parse next 2 digits
    const char* space = strchr(timestamp, ' ');
    if (space && strlen(space) >= 3) {
        int hour = 0;
        sscanf(space + 1, "%2d", &hour);
        return hour;
    }
    return -1;
}

// Check if hour is within session
bool IsInSession(int hour, int start_hour, int end_hour) {
    if (start_hour < end_hour) {
        // Normal range (e.g., 8-16)
        return hour >= start_hour && hour < end_hour;
    } else {
        // Wrap around midnight (not used in current sessions)
        return hour >= start_hour || hour < end_hour;
    }
}

// Load ticks using C-style I/O
int LoadTicks(const char* filename, Tick* ticks, int max_ticks) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        printf("ERROR: Could not open file: %s\n", filename);
        return 0;
    }

    // Skip header line
    char line[256];
    if (!fgets(line, sizeof(line), file)) {
        fclose(file);
        return 0;
    }

    int count = 0;
    while (count < max_ticks && fgets(line, sizeof(line), file)) {
        // Parse TAB-delimited: timestamp\tbid\task\t...
        char* token = strtok(line, "\t");
        if (!token) continue;

        strncpy(ticks[count].timestamp, token, 31);
        ticks[count].timestamp[31] = '\0';

        token = strtok(NULL, "\t");
        if (!token) continue;
        ticks[count].bid = atof(token);

        token = strtok(NULL, "\t");
        if (!token) continue;
        ticks[count].ask = atof(token);

        // Extract hour
        ticks[count].hour = ParseHour(ticks[count].timestamp);
        if (ticks[count].hour < 0) continue;

        // Validate prices
        if (ticks[count].bid <= 0 || ticks[count].ask <= 0) continue;

        count++;
    }

    fclose(file);
    return count;
}

// Run grid strategy with session filter
TestResult RunSessionTest(const Tick* ticks, int tick_count, const SessionFilter& filter) {
    TestResult result;
    memset(&result, 0, sizeof(result));
    result.session_name = filter.name;

    double balance = INITIAL_BALANCE;
    double equity = INITIAL_BALANCE;
    double peak_equity = INITIAL_BALANCE;
    double max_drawdown = 0.0;

    std::vector<Trade*> positions;
    std::vector<double> daily_returns;

    int next_id = 1;
    int total_trades = 0;
    int winning_trades = 0;
    int losing_trades = 0;
    double total_profit = 0.0;
    double total_loss = 0.0;
    int max_pos = 0;
    int ticks_traded = 0;

    double last_equity = INITIAL_BALANCE;
    const char* last_day = "";
    char current_day[16] = "";

    for (int i = 0; i < tick_count; i++) {
        const Tick& tick = ticks[i];

        // Track daily returns for Sharpe ratio
        strncpy(current_day, tick.timestamp, 10);
        current_day[10] = '\0';
        if (strcmp(current_day, last_day) != 0 && positions.empty()) {
            if (strlen(last_day) > 0) {
                double daily_ret = (equity - last_equity) / last_equity;
                daily_returns.push_back(daily_ret);
            }
            last_equity = equity;
            last_day = tick.timestamp;  // Note: points to tick array
        }

        // Update equity
        equity = balance;
        for (Trade* t : positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * CONTRACT_SIZE;
        }

        // Track drawdown
        if (equity > peak_equity) {
            peak_equity = equity;
        }
        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        if (dd_pct > max_drawdown) {
            max_drawdown = dd_pct;
        }

        // Emergency close at 25% drawdown
        if (dd_pct > 25.0 && !positions.empty()) {
            for (Trade* t : positions) {
                double pnl = (tick.bid - t->entry_price) * t->lot_size * CONTRACT_SIZE;
                balance += pnl;
                total_trades++;
                if (pnl > 0) {
                    winning_trades++;
                    total_profit += pnl;
                } else {
                    losing_trades++;
                    total_loss += (-pnl);
                }
                delete t;
            }
            positions.clear();
            equity = balance;
            peak_equity = equity;
            continue;
        }

        // Margin check (20% stop-out)
        double used_margin = 0;
        for (Trade* t : positions) {
            used_margin += t->lot_size * CONTRACT_SIZE * t->entry_price / LEVERAGE;
        }
        if (used_margin > 0 && equity / used_margin * 100.0 < 20.0) {
            for (Trade* t : positions) {
                double pnl = (tick.bid - t->entry_price) * t->lot_size * CONTRACT_SIZE;
                balance += pnl;
                total_trades++;
                if (pnl > 0) {
                    winning_trades++;
                    total_profit += pnl;
                } else {
                    losing_trades++;
                    total_loss += (-pnl);
                }
                delete t;
            }
            positions.clear();
            break;
        }

        // Check take profits
        for (auto it = positions.begin(); it != positions.end();) {
            Trade* t = *it;
            if (tick.bid >= t->take_profit) {
                double pnl = (t->take_profit - t->entry_price) * t->lot_size * CONTRACT_SIZE;
                balance += pnl;
                total_trades++;
                if (pnl > 0) {
                    winning_trades++;
                    total_profit += pnl;
                } else {
                    losing_trades++;
                    total_loss += (-pnl);
                }
                delete t;
                it = positions.erase(it);
            } else {
                ++it;
            }
        }

        // Check if we're in the trading session
        bool in_session = IsInSession(tick.hour, filter.start_hour, filter.end_hour);

        // Only open new positions if in session
        if (in_session && dd_pct < 15.0 && (int)positions.size() < MAX_POSITIONS) {
            ticks_traded++;

            double lowest = DBL_MAX;
            double highest = -DBL_MAX;
            for (Trade* t : positions) {
                if (t->entry_price < lowest) lowest = t->entry_price;
                if (t->entry_price > highest) highest = t->entry_price;
            }

            bool should_open = positions.empty() ||
                               (lowest >= tick.ask + SPACING) ||
                               (highest <= tick.ask - SPACING);

            if (should_open) {
                double margin_needed = LOT_SIZE * CONTRACT_SIZE * tick.ask / LEVERAGE;
                if (equity - used_margin > margin_needed * 2) {
                    Trade* t = new Trade();
                    t->id = next_id++;
                    t->entry_price = tick.ask;
                    t->lot_size = LOT_SIZE;
                    t->take_profit = tick.ask + (tick.ask - tick.bid) + SPACING;  // spread + spacing
                    strncpy(t->entry_time, tick.timestamp, 31);
                    t->entry_time[31] = '\0';
                    positions.push_back(t);
                }
            }
        }

        max_pos = std::max(max_pos, (int)positions.size());
    }

    // Close remaining positions at last tick
    if (tick_count > 0) {
        const Tick& last_tick = ticks[tick_count - 1];
        for (Trade* t : positions) {
            double pnl = (last_tick.bid - t->entry_price) * t->lot_size * CONTRACT_SIZE;
            balance += pnl;
            total_trades++;
            if (pnl > 0) {
                winning_trades++;
                total_profit += pnl;
            } else {
                losing_trades++;
                total_loss += (-pnl);
            }
            delete t;
        }
        positions.clear();
    }

    // Calculate Sharpe ratio (annualized, assuming 252 trading days)
    double sharpe = 0.0;
    if (daily_returns.size() > 1) {
        double sum = 0.0;
        for (double r : daily_returns) sum += r;
        double mean = sum / daily_returns.size();

        double sq_sum = 0.0;
        for (double r : daily_returns) sq_sum += (r - mean) * (r - mean);
        double stddev = sqrt(sq_sum / (daily_returns.size() - 1));

        if (stddev > 0) {
            sharpe = (mean / stddev) * sqrt(252.0);
        }
    }

    result.final_balance = balance;
    result.return_pct = (balance - INITIAL_BALANCE) / INITIAL_BALANCE * 100.0;
    result.max_drawdown_pct = max_drawdown;
    result.total_trades = total_trades;
    result.winning_trades = winning_trades;
    result.losing_trades = losing_trades;
    result.win_rate = total_trades > 0 ? (double)winning_trades / total_trades * 100.0 : 0.0;
    result.profit_factor = total_loss > 0 ? total_profit / total_loss : (total_profit > 0 ? 999.0 : 0.0);
    result.sharpe_ratio = sharpe;
    result.max_positions = max_pos;
    result.ticks_traded = ticks_traded;

    return result;
}

void PrintResults(const std::vector<TestResult>& results) {
    printf("\n");
    printf("================================================================================\n");
    printf("                    SESSION FILTER ANALYSIS RESULTS\n");
    printf("================================================================================\n\n");

    // Header
    printf("%-20s %10s %8s %8s %8s %8s %8s %8s\n",
           "Session", "Return%", "MaxDD%", "Trades", "WinRate", "PF", "Sharpe", "MaxPos");
    printf("--------------------------------------------------------------------------------\n");

    // Find best metrics for highlighting
    double best_return = -DBL_MAX;
    double best_sharpe = -DBL_MAX;
    double best_pf = -DBL_MAX;
    double best_dd = DBL_MAX;

    for (const auto& r : results) {
        if (r.return_pct > best_return) best_return = r.return_pct;
        if (r.sharpe_ratio > best_sharpe) best_sharpe = r.sharpe_ratio;
        if (r.profit_factor > best_pf && r.profit_factor < 100) best_pf = r.profit_factor;
        if (r.max_drawdown_pct < best_dd) best_dd = r.max_drawdown_pct;
    }

    for (const auto& r : results) {
        printf("%-20s %9.2f%% %7.2f%% %8d %7.1f%% %8.2f %8.2f %8d",
               r.session_name,
               r.return_pct,
               r.max_drawdown_pct,
               r.total_trades,
               r.win_rate,
               r.profit_factor < 100 ? r.profit_factor : 99.99,
               r.sharpe_ratio,
               r.max_positions);

        // Mark best
        if (r.return_pct == best_return) printf(" [BEST RETURN]");
        if (r.sharpe_ratio == best_sharpe && r.sharpe_ratio > 0) printf(" [BEST SHARPE]");
        if (r.profit_factor == best_pf && best_pf > 0) printf(" [BEST PF]");
        if (r.max_drawdown_pct == best_dd) printf(" [LOWEST DD]");

        printf("\n");
    }

    printf("--------------------------------------------------------------------------------\n\n");

    // Detailed analysis
    printf("DETAILED BREAKDOWN:\n\n");
    for (const auto& r : results) {
        printf("%s:\n", r.session_name);
        printf("  Final Balance:  $%.2f (%.2f%% return)\n", r.final_balance, r.return_pct);
        printf("  Max Drawdown:   %.2f%%\n", r.max_drawdown_pct);
        printf("  Total Trades:   %d (Win: %d, Loss: %d)\n", r.total_trades, r.winning_trades, r.losing_trades);
        printf("  Win Rate:       %.1f%%\n", r.win_rate);
        printf("  Profit Factor:  %.2f\n", r.profit_factor < 100 ? r.profit_factor : 99.99);
        printf("  Sharpe Ratio:   %.2f\n", r.sharpe_ratio);
        printf("  Max Positions:  %d\n", r.max_positions);
        printf("  Active Ticks:   %d\n", r.ticks_traded);
        printf("\n");
    }

    // Risk-adjusted analysis
    printf("================================================================================\n");
    printf("                       RISK-ADJUSTED ANALYSIS\n");
    printf("================================================================================\n\n");

    printf("Return per Unit of Max Drawdown (Return/MaxDD ratio):\n");
    for (const auto& r : results) {
        double ratio = r.max_drawdown_pct > 0 ? r.return_pct / r.max_drawdown_pct : 0;
        printf("  %-20s: %6.2f\n", r.session_name, ratio);
    }

    printf("\nCalmar Ratio (Annualized Return / Max DD, assuming ~10 days of data = ~25x multiplier):\n");
    for (const auto& r : results) {
        double annual_return = r.return_pct * 25.0;  // Rough annualization
        double calmar = r.max_drawdown_pct > 0 ? annual_return / r.max_drawdown_pct : 0;
        printf("  %-20s: %6.2f\n", r.session_name, calmar);
    }
}

int main() {
    printf("================================================================\n");
    printf("     SESSION FILTER TEST FOR GRID STRATEGY (XAUUSD)\n");
    printf("================================================================\n\n");

    printf("HYPOTHESIS: Grid strategies work better during specific sessions:\n");
    printf("  - Asian (0-8 UTC): Ranging/consolidating, good for grids\n");
    printf("  - London (8-16 UTC): Trending, potentially harder for grids\n");
    printf("  - NY (13-21 UTC): Most volatile, mixed signals\n\n");

    printf("Loading tick data...\n");

    // Allocate memory for ticks
    const int MAX_TICKS = 500000;
    Tick* ticks = (Tick*)malloc(sizeof(Tick) * MAX_TICKS);
    if (!ticks) {
        printf("ERROR: Failed to allocate memory for ticks\n");
        return 1;
    }

    const char* filename = "C:/Users/user/Documents/ctrader-backtest/validation/Grid/XAUUSD_TICKS_2025.csv";
    int tick_count = LoadTicks(filename, ticks, MAX_TICKS);

    if (tick_count == 0) {
        printf("ERROR: No ticks loaded\n");
        free(ticks);
        return 1;
    }

    printf("Loaded %d ticks\n", tick_count);
    printf("First tick: %s (Hour: %d)\n", ticks[0].timestamp, ticks[0].hour);
    printf("Last tick:  %s (Hour: %d)\n", ticks[tick_count-1].timestamp, ticks[tick_count-1].hour);
    printf("\n");

    // Analyze hour distribution
    printf("Hour distribution in data:\n");
    int hour_counts[24] = {0};
    for (int i = 0; i < tick_count; i++) {
        if (ticks[i].hour >= 0 && ticks[i].hour < 24) {
            hour_counts[ticks[i].hour]++;
        }
    }
    for (int h = 0; h < 24; h++) {
        printf("  Hour %02d: %6d ticks (%.1f%%)\n", h, hour_counts[h],
               tick_count > 0 ? hour_counts[h] * 100.0 / tick_count : 0);
    }
    printf("\n");

    // Define session filters to test
    SessionFilter sessions[] = {
        {"No Filter (24h)",     0,  24},  // Full day (no filter)
        {"Asian (0-8 UTC)",     0,   8},
        {"London (8-16 UTC)",   8,  16},
        {"NY (13-21 UTC)",     13,  21},
        {"Asian+EarlyLdn",      0,  12},  // Asian + early London
        {"London+NY",           8,  21},  // London + NY overlap
        {"Low Vol (22-6)",     22,   6},  // Late night / early morning
        {"Peak Vol (14-18)",   14,  18},  // London/NY overlap peak
    };
    int num_sessions = sizeof(sessions) / sizeof(sessions[0]);

    printf("Running tests for %d session filters...\n\n", num_sessions);

    // Run tests
    std::vector<TestResult> results;
    for (int s = 0; s < num_sessions; s++) {
        printf("Testing: %s (Hours %02d-%02d)...\n",
               sessions[s].name, sessions[s].start_hour, sessions[s].end_hour);

        TestResult r = RunSessionTest(ticks, tick_count, sessions[s]);
        results.push_back(r);

        printf("  Return: %.2f%%, MaxDD: %.2f%%, Trades: %d\n",
               r.return_pct, r.max_drawdown_pct, r.total_trades);
    }

    // Print comprehensive results
    PrintResults(results);

    // Conclusion
    printf("================================================================================\n");
    printf("                           CONCLUSIONS\n");
    printf("================================================================================\n\n");

    // Find best risk-adjusted session
    int best_idx = 0;
    double best_score = -DBL_MAX;
    for (size_t i = 0; i < results.size(); i++) {
        // Score = Sharpe * (1 + return/100) / (1 + maxdd/100)
        double score = results[i].sharpe_ratio *
                      (1.0 + results[i].return_pct / 100.0) /
                      (1.0 + results[i].max_drawdown_pct / 100.0);
        if (score > best_score) {
            best_score = score;
            best_idx = i;
        }
    }

    printf("Best risk-adjusted session: %s\n", results[best_idx].session_name);
    printf("  - Return: %.2f%%\n", results[best_idx].return_pct);
    printf("  - Max Drawdown: %.2f%%\n", results[best_idx].max_drawdown_pct);
    printf("  - Sharpe Ratio: %.2f\n", results[best_idx].sharpe_ratio);
    printf("  - Profit Factor: %.2f\n", results[best_idx].profit_factor < 100 ? results[best_idx].profit_factor : 99.99);
    printf("\n");

    // Compare Asian vs trending sessions
    double asian_return = 0, london_return = 0, ny_return = 0;
    double asian_dd = 0, london_dd = 0, ny_dd = 0;
    for (const auto& r : results) {
        if (strcmp(r.session_name, "Asian (0-8 UTC)") == 0) {
            asian_return = r.return_pct;
            asian_dd = r.max_drawdown_pct;
        } else if (strcmp(r.session_name, "London (8-16 UTC)") == 0) {
            london_return = r.return_pct;
            london_dd = r.max_drawdown_pct;
        } else if (strcmp(r.session_name, "NY (13-21 UTC)") == 0) {
            ny_return = r.return_pct;
            ny_dd = r.max_drawdown_pct;
        }
    }

    printf("Session Comparison:\n");
    printf("  Asian:  Return %.2f%%, MaxDD %.2f%% (Return/DD: %.2f)\n",
           asian_return, asian_dd, asian_dd > 0 ? asian_return/asian_dd : 0);
    printf("  London: Return %.2f%%, MaxDD %.2f%% (Return/DD: %.2f)\n",
           london_return, london_dd, london_dd > 0 ? london_return/london_dd : 0);
    printf("  NY:     Return %.2f%%, MaxDD %.2f%% (Return/DD: %.2f)\n",
           ny_return, ny_dd, ny_dd > 0 ? ny_return/ny_dd : 0);
    printf("\n");

    if (asian_dd > 0 && london_dd > 0 && ny_dd > 0) {
        double asian_ratio = asian_return / asian_dd;
        double london_ratio = london_return / london_dd;
        double ny_ratio = ny_return / ny_dd;

        if (asian_ratio > london_ratio && asian_ratio > ny_ratio) {
            printf("FINDING: Asian session shows BETTER risk-adjusted returns!\n");
            printf("  -> Hypothesis SUPPORTED: Ranging markets favor grid strategies.\n");
        } else if (london_ratio > asian_ratio || ny_ratio > asian_ratio) {
            printf("FINDING: Trending sessions (London/NY) show better risk-adjusted returns.\n");
            printf("  -> Hypothesis NOT SUPPORTED for this data period.\n");
        }
    }

    printf("\n================================================================\n");

    // Cleanup
    free(ticks);

    return 0;
}
