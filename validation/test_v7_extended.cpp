/**
 * V7 Extended Backtest - Long Duration Validation
 *
 * Tests V7 strategy with optimized params (50/1000/0.6/2.0) over extended period
 * Uses C-style I/O (fopen/fgets/sscanf) to avoid crashes
 *
 * Goal: Validate that V7's performance holds over a longer time period
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <deque>
#include <algorithm>
#include <cfloat>

// Configuration
static const size_t TARGET_TICKS = 50000000;  // 50M ticks (~full dataset)
static const char* DATA_FILE = "Broker/XAUUSD_TICKS_2025.csv";

// Tick structure
struct Tick {
    int year, month, day, hour, minute, second, ms;
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
    int entry_month;  // For monthly tracking
};

// Monthly statistics
struct MonthStats {
    int month;
    int year;
    double start_balance;
    double end_balance;
    double return_pct;
    double max_dd;
    int trades_closed;
    int positions_opened;
};

// ATR Calculator
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

// Global statistics
struct GlobalStats {
    double total_return_pct = 0;
    double max_drawdown = 0;
    int total_trades = 0;
    int total_positions_opened = 0;
    int winning_trades = 0;
    int losing_trades = 0;
    double gross_profit = 0;
    double gross_loss = 0;
    double peak_equity = 10000.0;
};

// Load ticks using C-style I/O
std::vector<Tick> LoadTicks(const char* filename, size_t max_count) {
    std::vector<Tick> ticks;
    ticks.reserve(max_count);

    FILE* fp = fopen(filename, "r");
    if (!fp) {
        printf("ERROR: Cannot open file: %s\n", filename);
        return ticks;
    }

    char line[512];
    // Skip header
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return ticks;
    }

    printf("Loading up to %zu ticks...\n", max_count);

    while (fgets(line, sizeof(line), fp) && ticks.size() < max_count) {
        Tick tick = {};

        // Parse: 2025.01.02 01:00:02.460	2625.27	2625.51	0	1158
        char timestamp[64];
        double bid, ask;
        int vol, flags;

        if (sscanf(line, "%63[^\t]\t%lf\t%lf\t%d\t%d", timestamp, &bid, &ask, &vol, &flags) >= 3) {
            // Parse timestamp: 2025.01.02 01:00:02.460
            if (sscanf(timestamp, "%d.%d.%d %d:%d:%d.%d",
                       &tick.year, &tick.month, &tick.day,
                       &tick.hour, &tick.minute, &tick.second, &tick.ms) >= 6) {
                tick.bid = bid;
                tick.ask = ask;
                ticks.push_back(tick);
            }
        }

        if (ticks.size() % 500000 == 0 && ticks.size() > 0) {
            printf("  Loaded %zu ticks (date: %04d.%02d.%02d)...\n",
                   ticks.size(), tick.year, tick.month, tick.day);
        }
    }

    fclose(fp);
    printf("Loaded %zu ticks total\n", ticks.size());
    return ticks;
}

// Run V7 backtest with monthly tracking
void RunV7Backtest(const std::vector<Tick>& ticks,
                   int atr_short_period, int atr_long_period,
                   double vol_threshold, double tp_multiplier,
                   GlobalStats& global, std::vector<MonthStats>& monthly) {

    if (ticks.empty()) return;

    // Strategy parameters
    const double initial_balance = 10000.0;
    const double contract_size = 100.0;
    const double leverage = 500.0;
    const double spacing = 1.0;
    const double lot_size = 0.01;
    const int max_positions = 20;
    const double stop_new_at_dd = 5.0;
    const double partial_close_at_dd = 8.0;
    const double close_all_at_dd = 25.0;

    double balance = initial_balance;
    double equity = balance;
    double peak_equity = balance;
    double session_max_dd = 0;

    std::vector<Trade*> positions;
    size_t next_id = 1;
    bool partial_done = false;
    bool all_closed = false;

    ATR atr_short(atr_short_period);
    ATR atr_long(atr_long_period);

    // Monthly tracking
    int current_month = ticks[0].month;
    int current_year = ticks[0].year;
    double month_start_balance = balance;
    double month_max_dd = 0;
    int month_trades = 0;
    int month_positions_opened = 0;

    int trades_closed_by_protection = 0;

    for (size_t i = 0; i < ticks.size(); i++) {
        const Tick& tick = ticks[i];

        // Update ATR
        atr_short.Add(tick.bid);
        atr_long.Add(tick.bid);

        // Check for month change
        if (tick.month != current_month || tick.year != current_year) {
            // Save previous month stats
            MonthStats ms;
            ms.month = current_month;
            ms.year = current_year;
            ms.start_balance = month_start_balance;
            ms.end_balance = balance;
            ms.return_pct = (balance - month_start_balance) / month_start_balance * 100.0;
            ms.max_dd = month_max_dd;
            ms.trades_closed = month_trades;
            ms.positions_opened = month_positions_opened;
            monthly.push_back(ms);

            // Reset for new month
            current_month = tick.month;
            current_year = tick.year;
            month_start_balance = balance;
            month_max_dd = 0;
            month_trades = 0;
            month_positions_opened = 0;
        }

        // Calculate equity
        equity = balance;
        for (Trade* t : positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * contract_size;
        }

        // Reset peak when no positions
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
        double dd_pct = 0;
        if (peak_equity > 0) {
            dd_pct = (peak_equity - equity) / peak_equity * 100.0;
        }
        session_max_dd = std::max(session_max_dd, dd_pct);
        month_max_dd = std::max(month_max_dd, dd_pct);

        // Protection: Close ALL at 25% DD
        if (dd_pct > close_all_at_dd && !all_closed && !positions.empty()) {
            for (Trade* t : positions) {
                double pnl = (tick.bid - t->entry_price) * t->lot_size * contract_size;
                balance += pnl;
                if (pnl >= 0) {
                    global.winning_trades++;
                    global.gross_profit += pnl;
                } else {
                    global.losing_trades++;
                    global.gross_loss += fabs(pnl);
                }
                month_trades++;
                global.total_trades++;
                delete t;
            }
            positions.clear();
            all_closed = true;
            trades_closed_by_protection += month_trades;
            equity = balance;
            peak_equity = balance;
            continue;
        }

        // Protection: Partial close at 8% DD
        if (dd_pct > partial_close_at_dd && !partial_done && positions.size() > 1) {
            std::sort(positions.begin(), positions.end(), [&](Trade* a, Trade* b) {
                return (tick.bid - a->entry_price) < (tick.bid - b->entry_price);
            });
            int to_close = (int)(positions.size() * 0.5);
            to_close = std::max(1, to_close);
            for (int j = 0; j < to_close && !positions.empty(); j++) {
                double pnl = (tick.bid - positions[0]->entry_price) * positions[0]->lot_size * contract_size;
                balance += pnl;
                if (pnl >= 0) {
                    global.winning_trades++;
                    global.gross_profit += pnl;
                } else {
                    global.losing_trades++;
                    global.gross_loss += fabs(pnl);
                }
                month_trades++;
                global.total_trades++;
                delete positions[0];
                positions.erase(positions.begin());
            }
            partial_done = true;
        }

        // Check TP for all positions
        for (auto it = positions.begin(); it != positions.end();) {
            Trade* t = *it;
            if (tick.bid >= t->take_profit) {
                double pnl = (t->take_profit - t->entry_price) * t->lot_size * contract_size;
                balance += pnl;
                global.winning_trades++;
                global.gross_profit += pnl;
                month_trades++;
                global.total_trades++;
                delete t;
                it = positions.erase(it);
            } else {
                ++it;
            }
        }

        // V7 Volatility Filter: Only trade when volatility is low
        bool volatility_ok = true;
        if (atr_short.IsReady() && atr_long.IsReady() && atr_long.Get() > 0) {
            volatility_ok = atr_short.Get() < atr_long.Get() * vol_threshold;
        }

        // Open new positions if conditions allow
        if (dd_pct < stop_new_at_dd && volatility_ok && (int)positions.size() < max_positions) {
            double lowest = DBL_MAX, highest = DBL_MIN;
            for (Trade* t : positions) {
                lowest = std::min(lowest, t->entry_price);
                highest = std::max(highest, t->entry_price);
            }

            bool should_open = positions.empty() ||
                              (lowest >= tick.ask + spacing) ||
                              (highest <= tick.ask - spacing);

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
                    t->take_profit = tick.ask + tick.spread() + (spacing * tp_multiplier);
                    t->entry_month = tick.month;
                    positions.push_back(t);
                    month_positions_opened++;
                    global.total_positions_opened++;
                }
            }
        }
    }

    // Close remaining positions at end
    const Tick& last_tick = ticks.back();
    for (Trade* t : positions) {
        double pnl = (last_tick.bid - t->entry_price) * t->lot_size * contract_size;
        balance += pnl;
        if (pnl >= 0) {
            global.winning_trades++;
            global.gross_profit += pnl;
        } else {
            global.losing_trades++;
            global.gross_loss += fabs(pnl);
        }
        month_trades++;
        global.total_trades++;
        delete t;
    }
    positions.clear();

    // Save last month stats
    MonthStats ms;
    ms.month = current_month;
    ms.year = current_year;
    ms.start_balance = month_start_balance;
    ms.end_balance = balance;
    ms.return_pct = (balance - month_start_balance) / month_start_balance * 100.0;
    ms.max_dd = month_max_dd;
    ms.trades_closed = month_trades;
    ms.positions_opened = month_positions_opened;
    monthly.push_back(ms);

    // Update global stats
    global.total_return_pct = (balance - initial_balance) / initial_balance * 100.0;
    global.max_drawdown = session_max_dd;
    global.peak_equity = peak_equity;
}

int main() {
    printf("=======================================================\n");
    printf("V7 EXTENDED BACKTEST - Long Duration Validation\n");
    printf("=======================================================\n\n");

    printf("Strategy: V7 with Volatility Filter\n");
    printf("Parameters:\n");
    printf("  - ATR Short: 50 ticks\n");
    printf("  - ATR Long: 1000 ticks\n");
    printf("  - Volatility Threshold: 0.6\n");
    printf("  - TP Multiplier: 2.0x\n");
    printf("\n");

    // Load tick data
    std::vector<Tick> ticks = LoadTicks(DATA_FILE, TARGET_TICKS);

    if (ticks.empty()) {
        printf("ERROR: Failed to load tick data\n");
        return 1;
    }

    // Show data range
    const Tick& first = ticks.front();
    const Tick& last = ticks.back();
    printf("\nData Range: %04d.%02d.%02d %02d:%02d -> %04d.%02d.%02d %02d:%02d\n",
           first.year, first.month, first.day, first.hour, first.minute,
           last.year, last.month, last.day, last.hour, last.minute);
    printf("Price Range: %.2f -> %.2f (%.2f%%)\n",
           first.bid, last.bid, (last.bid - first.bid) / first.bid * 100.0);
    printf("\n");

    // Run backtest
    printf("Running V7 backtest...\n\n");

    GlobalStats global;
    std::vector<MonthStats> monthly;

    RunV7Backtest(ticks, 50, 1000, 0.6, 2.0, global, monthly);

    // Print monthly breakdown
    printf("=======================================================\n");
    printf("MONTHLY BREAKDOWN\n");
    printf("=======================================================\n");
    printf("%-10s %12s %12s %10s %10s %10s\n",
           "Month", "Start Bal", "End Bal", "Return", "Max DD", "Trades");
    printf("-------------------------------------------------------\n");

    for (const MonthStats& ms : monthly) {
        printf("%04d-%02d    $%10.2f $%10.2f %8.2f%% %8.2f%% %8d\n",
               ms.year, ms.month, ms.start_balance, ms.end_balance,
               ms.return_pct, ms.max_dd, ms.trades_closed);
    }
    printf("-------------------------------------------------------\n\n");

    // Print overall statistics
    printf("=======================================================\n");
    printf("OVERALL STATISTICS\n");
    printf("=======================================================\n");
    printf("Starting Balance:     $10,000.00\n");
    printf("Final Balance:        $%.2f\n", 10000.0 * (1 + global.total_return_pct / 100.0));
    printf("Total Return:         %.2f%%\n", global.total_return_pct);
    printf("Maximum Drawdown:     %.2f%%\n", global.max_drawdown);
    printf("\n");
    printf("Total Trades:         %d\n", global.total_trades);
    printf("Positions Opened:     %d\n", global.total_positions_opened);
    printf("Winning Trades:       %d\n", global.winning_trades);
    printf("Losing Trades:        %d\n", global.losing_trades);
    printf("\n");

    // Calculate derived metrics
    double win_rate = (global.total_trades > 0) ?
                      (double)global.winning_trades / global.total_trades * 100.0 : 0;
    double profit_factor = (global.gross_loss > 0) ?
                           global.gross_profit / global.gross_loss :
                           (global.gross_profit > 0 ? 999.99 : 0);
    double avg_win = (global.winning_trades > 0) ?
                     global.gross_profit / global.winning_trades : 0;
    double avg_loss = (global.losing_trades > 0) ?
                      global.gross_loss / global.losing_trades : 0;

    printf("Win Rate:             %.2f%%\n", win_rate);
    printf("Profit Factor:        %.2f\n", profit_factor);
    printf("Gross Profit:         $%.2f\n", global.gross_profit);
    printf("Gross Loss:           $%.2f\n", global.gross_loss);
    printf("Average Win:          $%.2f\n", avg_win);
    printf("Average Loss:         $%.2f\n", avg_loss);
    printf("\n");

    // Risk-adjusted metrics
    double sharpe_approx = (global.max_drawdown > 0) ?
                           global.total_return_pct / global.max_drawdown : 0;
    printf("Return/DD Ratio:      %.2f\n", sharpe_approx);
    printf("\n");

    // Performance assessment
    printf("=======================================================\n");
    printf("PERFORMANCE ASSESSMENT\n");
    printf("=======================================================\n");

    if (global.total_return_pct > 0 && global.max_drawdown < 25) {
        printf("PASS - Strategy is profitable with controlled risk\n");
    } else if (global.total_return_pct > 0) {
        printf("CAUTION - Profitable but high drawdown risk\n");
    } else {
        printf("FAIL - Strategy not profitable on this data\n");
    }

    // Monthly consistency
    int profitable_months = 0;
    for (const MonthStats& ms : monthly) {
        if (ms.return_pct > 0) profitable_months++;
    }
    printf("Profitable Months:    %d / %zu (%.0f%%)\n",
           profitable_months, monthly.size(),
           (monthly.size() > 0 ? (double)profitable_months / monthly.size() * 100.0 : 0));
    printf("=======================================================\n");

    return 0;
}
