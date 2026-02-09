/**
 * Anti-Martingale Position Sizing Test
 *
 * Tests different position sizing strategies:
 * a) Fixed: Always 0.01 lots (baseline)
 * b) Anti-martingale: Start 0.01, increase by 25% after each winning trade, reset after loss
 * c) Profit-based: Lot size = base_lot * (1 + cumulative_profit / 1000)
 * d) Win streak: Increase lot after 3 consecutive wins, reset after loss
 *
 * Anti-martingale is the opposite of martingale:
 * - Increase bets when winning (compound winners)
 * - Decrease/reset bets when losing (limit losers)
 *
 * Uses C-style file I/O for tick data loading.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <deque>
#include <algorithm>
#include <cfloat>

// Simple tick structure
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
};

// ATR calculator for volatility filter
class ATR {
    std::deque<double> ranges;
    int period;
    double sum = 0;
    double last_price = 0;
public:
    ATR(int p) : period(p) {}
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
    double Get() const { return ranges.empty() ? 0 : sum / ranges.size(); }
    bool IsReady() const { return (int)ranges.size() >= period; }
};

// Position sizing strategy types
enum SizingStrategy {
    FIXED,              // Always 0.01 lots
    ANTI_MARTINGALE,    // +25% after win, reset after loss
    PROFIT_BASED,       // base_lot * (1 + profit/1000)
    WIN_STREAK          // Increase after 3 wins, reset after loss
};

const char* GetStrategyName(SizingStrategy s) {
    switch (s) {
        case FIXED: return "Fixed (0.01)";
        case ANTI_MARTINGALE: return "Anti-Martingale (+25%)";
        case PROFIT_BASED: return "Profit-Based";
        case WIN_STREAK: return "Win Streak (3+)";
        default: return "Unknown";
    }
}

// Position sizing manager
class PositionSizer {
public:
    SizingStrategy strategy;
    double base_lot;
    double current_lot;
    double multiplier;      // For anti-martingale
    int consecutive_wins;
    double cumulative_profit;
    int total_wins;
    int total_losses;

    PositionSizer(SizingStrategy s, double base = 0.01)
        : strategy(s), base_lot(base), current_lot(base), multiplier(1.0),
          consecutive_wins(0), cumulative_profit(0), total_wins(0), total_losses(0) {}

    double GetLotSize() const {
        switch (strategy) {
            case FIXED:
                return base_lot;

            case ANTI_MARTINGALE:
                return std::min(current_lot, 1.0);  // Cap at 1.0 lot

            case PROFIT_BASED: {
                double adjusted = base_lot * (1.0 + std::max(0.0, cumulative_profit) / 1000.0);
                return std::min(adjusted, 1.0);  // Cap at 1.0 lot
            }

            case WIN_STREAK:
                return std::min(current_lot, 1.0);  // Cap at 1.0 lot

            default:
                return base_lot;
        }
    }

    void OnTradeClose(double profit) {
        cumulative_profit += profit;

        if (profit > 0) {
            total_wins++;
            consecutive_wins++;

            switch (strategy) {
                case ANTI_MARTINGALE:
                    // Increase by 25% after each win
                    multiplier *= 1.25;
                    current_lot = base_lot * multiplier;
                    break;

                case WIN_STREAK:
                    // Increase after 3 consecutive wins
                    if (consecutive_wins >= 3) {
                        current_lot = base_lot * 2.0;  // Double the lot
                        if (consecutive_wins >= 6) {
                            current_lot = base_lot * 3.0;  // Triple after 6 wins
                        }
                        if (consecutive_wins >= 9) {
                            current_lot = base_lot * 4.0;  // 4x after 9 wins
                        }
                    }
                    break;

                default:
                    break;
            }
        } else {
            total_losses++;
            consecutive_wins = 0;

            switch (strategy) {
                case ANTI_MARTINGALE:
                    // Reset to base lot after loss
                    multiplier = 1.0;
                    current_lot = base_lot;
                    break;

                case WIN_STREAK:
                    // Reset to base lot after loss
                    current_lot = base_lot;
                    break;

                default:
                    break;
            }
        }
    }
};

// Result structure
struct Result {
    SizingStrategy strategy;
    double final_balance;
    double return_pct;
    double max_dd;
    double max_dd_pct;
    int total_trades;
    int winning_trades;
    int losing_trades;
    double win_rate;
    double profit_factor;
    double risk_adjusted_return;  // Return / Max DD
    double avg_lot_size;
    double max_lot_size;
};

// Load ticks using C-style file I/O
std::vector<Tick> LoadTicksCSV(const char* filename, size_t max_count) {
    std::vector<Tick> ticks;
    ticks.reserve(max_count);

    FILE* file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "ERROR: Cannot open file: %s\n", filename);
        return ticks;
    }

    // Skip header line
    char line[512];
    if (fgets(line, sizeof(line), file) == NULL) {
        fclose(file);
        return ticks;
    }

    // Parse data lines
    double bid, ask;

    while (fgets(line, sizeof(line), file) && ticks.size() < max_count) {
        // Tab-delimited: timestamp\tbid\task\tvolume
        char* tab1 = strchr(line, '\t');
        if (!tab1) continue;

        char* tab2 = strchr(tab1 + 1, '\t');
        if (!tab2) continue;

        // Extract bid and ask
        *tab1 = '\0';
        *tab2 = '\0';

        bid = atof(tab1 + 1);
        ask = atof(tab2 + 1);

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

// Normalize lot to 2 decimal places
double NormalizeLot(double lot) {
    return floor(lot * 100.0 + 0.5) / 100.0;
}

// Run backtest with specific position sizing strategy
Result RunBacktest(const std::vector<Tick>& ticks, SizingStrategy sizing_strategy,
                   int atr_short = 100, int atr_long = 500, double vol_threshold = 0.8) {
    Result r;
    r.strategy = sizing_strategy;
    r.max_dd = 0;
    r.max_dd_pct = 0;
    r.total_trades = 0;
    r.winning_trades = 0;
    r.losing_trades = 0;
    r.avg_lot_size = 0;
    r.max_lot_size = 0;

    if (ticks.empty()) return r;

    // V7 optimized params
    const double CONTRACT_SIZE = 100.0;
    const double LEVERAGE = 500.0;
    const double SPACING = 1.0;
    const int MAX_POSITIONS = 20;
    const double STOP_NEW_DD = 5.0;
    const double PARTIAL_CLOSE_DD = 8.0;
    const double CLOSE_ALL_DD = 25.0;

    double balance = 10000.0;
    double equity = balance;
    double peak_equity = balance;
    double total_lots = 0;
    int lot_count = 0;

    std::vector<Trade*> positions;
    size_t next_id = 1;
    bool partial_done = false;
    bool all_closed = false;

    PositionSizer sizer(sizing_strategy, 0.01);

    ATR atr_s(atr_short);
    ATR atr_l(atr_long);

    double gross_profit = 0;
    double gross_loss = 0;

    for (const Tick& tick : ticks) {
        atr_s.Add(tick.bid);
        atr_l.Add(tick.bid);

        // Calculate equity
        equity = balance;
        for (Trade* t : positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * CONTRACT_SIZE;
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
        double dd = peak_equity - equity;
        double dd_pct = (peak_equity > 0) ? dd / peak_equity * 100.0 : 0.0;

        if (dd > r.max_dd) r.max_dd = dd;
        if (dd_pct > r.max_dd_pct) r.max_dd_pct = dd_pct;

        // V3 Protection: Close ALL at threshold
        if (dd_pct > CLOSE_ALL_DD && !all_closed && !positions.empty()) {
            for (Trade* t : positions) {
                double pl = (tick.bid - t->entry_price) * t->lot_size * CONTRACT_SIZE;
                balance += pl;
                sizer.OnTradeClose(pl);
                r.total_trades++;
                if (pl > 0) {
                    r.winning_trades++;
                    gross_profit += pl;
                } else {
                    r.losing_trades++;
                    gross_loss += std::abs(pl);
                }
                delete t;
            }
            positions.clear();
            all_closed = true;
            equity = balance;
            peak_equity = equity;
            continue;
        }

        // V3 Protection: Partial close at threshold
        if (dd_pct > PARTIAL_CLOSE_DD && !partial_done && positions.size() > 1) {
            // Sort by P/L (worst first)
            std::sort(positions.begin(), positions.end(), [&](Trade* a, Trade* b) {
                return (tick.bid - a->entry_price) < (tick.bid - b->entry_price);
            });

            int to_close = (int)(positions.size() * 0.5);
            to_close = std::max(1, to_close);

            for (int i = 0; i < to_close && !positions.empty(); i++) {
                Trade* t = positions[0];
                double pl = (tick.bid - t->entry_price) * t->lot_size * CONTRACT_SIZE;
                balance += pl;
                sizer.OnTradeClose(pl);
                r.total_trades++;
                if (pl > 0) {
                    r.winning_trades++;
                    gross_profit += pl;
                } else {
                    r.losing_trades++;
                    gross_loss += std::abs(pl);
                }
                delete t;
                positions.erase(positions.begin());
            }
            partial_done = true;
        }

        // Check TP for all positions
        for (auto it = positions.begin(); it != positions.end();) {
            Trade* t = *it;
            if (tick.bid >= t->take_profit) {
                double pl = (t->take_profit - t->entry_price) * t->lot_size * CONTRACT_SIZE;
                balance += pl;
                sizer.OnTradeClose(pl);
                r.total_trades++;
                if (pl > 0) {
                    r.winning_trades++;
                    gross_profit += pl;
                } else {
                    r.losing_trades++;
                    gross_loss += std::abs(pl);
                }
                delete t;
                it = positions.erase(it);
            } else {
                ++it;
            }
        }

        // V7 Volatility filter
        bool vol_ok = true;
        if (atr_s.IsReady() && atr_l.IsReady() && atr_l.Get() > 0) {
            vol_ok = atr_s.Get() < atr_l.Get() * vol_threshold;
        }

        // Open new positions
        if (dd_pct < STOP_NEW_DD && vol_ok && (int)positions.size() < MAX_POSITIONS) {
            double lowest = DBL_MAX, highest = DBL_MIN;
            for (Trade* t : positions) {
                lowest = std::min(lowest, t->entry_price);
                highest = std::max(highest, t->entry_price);
            }

            bool should_open = positions.empty() ||
                              (lowest >= tick.ask + SPACING) ||
                              (highest <= tick.ask - SPACING);

            if (should_open) {
                double lot = NormalizeLot(sizer.GetLotSize());
                if (lot < 0.01) lot = 0.01;

                double margin_needed = lot * CONTRACT_SIZE * tick.ask / LEVERAGE;
                double used_margin = 0;
                for (Trade* t : positions) {
                    used_margin += t->lot_size * CONTRACT_SIZE * t->entry_price / LEVERAGE;
                }

                if (equity - used_margin > margin_needed * 2) {
                    Trade* t = new Trade();
                    t->id = next_id++;
                    t->entry_price = tick.ask;
                    t->lot_size = lot;
                    t->take_profit = tick.ask + tick.spread() + SPACING;
                    positions.push_back(t);

                    total_lots += lot;
                    lot_count++;
                    if (lot > r.max_lot_size) r.max_lot_size = lot;
                }
            }
        }
    }

    // Close remaining positions at end
    for (Trade* t : positions) {
        double pl = (ticks.back().bid - t->entry_price) * t->lot_size * CONTRACT_SIZE;
        balance += pl;
        sizer.OnTradeClose(pl);
        r.total_trades++;
        if (pl > 0) {
            r.winning_trades++;
            gross_profit += pl;
        } else {
            r.losing_trades++;
            gross_loss += std::abs(pl);
        }
        delete t;
    }

    r.final_balance = balance;
    r.return_pct = (balance - 10000.0) / 10000.0 * 100.0;
    r.win_rate = r.total_trades > 0 ? (double)r.winning_trades / r.total_trades * 100.0 : 0.0;
    r.profit_factor = gross_loss > 0 ? gross_profit / gross_loss : (gross_profit > 0 ? 999.99 : 0);
    r.risk_adjusted_return = r.max_dd_pct > 0 ? r.return_pct / r.max_dd_pct : 0;
    r.avg_lot_size = lot_count > 0 ? total_lots / lot_count : 0;

    return r;
}

void PrintResults(const std::vector<Result>& results) {
    printf("\n");
    printf("=====================================================================================\n");
    printf("                     ANTI-MARTINGALE POSITION SIZING COMPARISON                      \n");
    printf("=====================================================================================\n");
    printf("\n");

    // Header
    printf("%-22s %10s %8s %8s %8s %7s %8s %8s\n",
           "Strategy", "Final $", "Return%", "MaxDD%", "Trades", "Win%", "PF", "RAR");
    printf("-------------------------------------------------------------------------------------\n");

    for (const Result& r : results) {
        printf("%-22s %10.2f %7.2f%% %7.2f%% %8d %6.1f%% %8.2f %8.2f\n",
               GetStrategyName(r.strategy),
               r.final_balance,
               r.return_pct,
               r.max_dd_pct,
               r.total_trades,
               r.win_rate,
               r.profit_factor,
               r.risk_adjusted_return);
    }

    printf("-------------------------------------------------------------------------------------\n");
    printf("\n");

    // Lot size statistics
    printf("LOT SIZE STATISTICS:\n");
    printf("%-22s %10s %10s\n", "Strategy", "Avg Lot", "Max Lot");
    printf("-------------------------------------------------------------------------------------\n");
    for (const Result& r : results) {
        printf("%-22s %10.4f %10.4f\n",
               GetStrategyName(r.strategy),
               r.avg_lot_size,
               r.max_lot_size);
    }
    printf("\n");
}

void PrintAnalysis(const std::vector<Result>& results) {
    printf("\n");
    printf("=====================================================================================\n");
    printf("                              ANALYSIS & RECOMMENDATIONS                              \n");
    printf("=====================================================================================\n");
    printf("\n");

    // Find best by different metrics
    const Result* best_return = &results[0];
    const Result* best_rar = &results[0];
    const Result* lowest_dd = &results[0];
    const Result* best_pf = &results[0];

    for (const Result& r : results) {
        if (r.return_pct > best_return->return_pct) best_return = &r;
        if (r.risk_adjusted_return > best_rar->risk_adjusted_return) best_rar = &r;
        if (r.max_dd_pct < lowest_dd->max_dd_pct) lowest_dd = &r;
        if (r.profit_factor > best_pf->profit_factor) best_pf = &r;
    }

    printf("BEST BY METRIC:\n");
    printf("  Highest Return:          %s (%.2f%%)\n",
           GetStrategyName(best_return->strategy), best_return->return_pct);
    printf("  Best Risk-Adjusted:      %s (RAR: %.2f)\n",
           GetStrategyName(best_rar->strategy), best_rar->risk_adjusted_return);
    printf("  Lowest Drawdown:         %s (%.2f%%)\n",
           GetStrategyName(lowest_dd->strategy), lowest_dd->max_dd_pct);
    printf("  Highest Profit Factor:   %s (%.2f)\n",
           GetStrategyName(best_pf->strategy), best_pf->profit_factor);
    printf("\n");

    // Compare anti-martingale to fixed baseline
    const Result* fixed = nullptr;
    const Result* anti_mart = nullptr;
    const Result* profit_based = nullptr;
    const Result* win_streak = nullptr;

    for (const Result& r : results) {
        switch (r.strategy) {
            case FIXED: fixed = &r; break;
            case ANTI_MARTINGALE: anti_mart = &r; break;
            case PROFIT_BASED: profit_based = &r; break;
            case WIN_STREAK: win_streak = &r; break;
        }
    }

    printf("COMPARISON VS FIXED BASELINE:\n");
    if (fixed && anti_mart) {
        double return_diff = anti_mart->return_pct - fixed->return_pct;
        double dd_diff = anti_mart->max_dd_pct - fixed->max_dd_pct;
        printf("  Anti-Martingale: %+.2f%% return, %+.2f%% drawdown\n", return_diff, dd_diff);
    }
    if (fixed && profit_based) {
        double return_diff = profit_based->return_pct - fixed->return_pct;
        double dd_diff = profit_based->max_dd_pct - fixed->max_dd_pct;
        printf("  Profit-Based:    %+.2f%% return, %+.2f%% drawdown\n", return_diff, dd_diff);
    }
    if (fixed && win_streak) {
        double return_diff = win_streak->return_pct - fixed->return_pct;
        double dd_diff = win_streak->max_dd_pct - fixed->max_dd_pct;
        printf("  Win Streak:      %+.2f%% return, %+.2f%% drawdown\n", return_diff, dd_diff);
    }
    printf("\n");

    // Strategy explanation
    printf("STRATEGY DESCRIPTIONS:\n");
    printf("  Fixed (0.01):        Constant 0.01 lot size (baseline for comparison)\n");
    printf("  Anti-Martingale:     +25%% lot after each win, reset to 0.01 after loss\n");
    printf("  Profit-Based:        lot = 0.01 * (1 + profit/1000) - scales with equity\n");
    printf("  Win Streak (3+):     2x lot after 3 wins, 3x after 6, 4x after 9, reset on loss\n");
    printf("\n");

    // Key insight
    printf("KEY INSIGHTS:\n");
    printf("  * Anti-martingale compounds winners by increasing position size\n");
    printf("  * This AMPLIFIES both returns AND drawdowns during winning streaks\n");
    printf("  * Best strategy depends on market regime:\n");
    printf("    - Trending markets: Anti-martingale/Win Streak may outperform\n");
    printf("    - Choppy markets: Fixed sizing may be safer\n");
    printf("  * Risk-adjusted return (RAR) = Return / Max Drawdown\n");
    printf("    - Higher RAR = better risk/reward tradeoff\n");
    printf("\n");

    // Final recommendation
    printf("RECOMMENDATION:\n");
    if (best_rar->strategy == FIXED) {
        printf("  FIXED sizing provides the best risk-adjusted returns.\n");
        printf("  Anti-martingale does not improve the strategy in this dataset.\n");
    } else if (best_rar->risk_adjusted_return > fixed->risk_adjusted_return * 1.1) {
        printf("  %s provides %.1f%% better risk-adjusted returns than Fixed.\n",
               GetStrategyName(best_rar->strategy),
               (best_rar->risk_adjusted_return / fixed->risk_adjusted_return - 1) * 100.0);
        printf("  Consider using this sizing strategy for better capital efficiency.\n");
    } else {
        printf("  All strategies show similar risk-adjusted performance.\n");
        printf("  Fixed sizing is recommended for simplicity and predictability.\n");
    }
    printf("\n");
    printf("=====================================================================================\n");
}

int main() {
    printf("=====================================================================================\n");
    printf("         ANTI-MARTINGALE POSITION SIZING TEST - V7 Strategy with XAUUSD             \n");
    printf("=====================================================================================\n");
    printf("\n");

    printf("Anti-Martingale Philosophy:\n");
    printf("  - INCREASE position size when WINNING (compound winners)\n");
    printf("  - DECREASE/reset position size when LOSING (limit losers)\n");
    printf("  - Opposite of Martingale (which doubles down on losses)\n");
    printf("\n");

    // Load tick data
    const char* filename = "Grid/XAUUSD_TICKS_2025.csv";
    printf("Loading 500,000 ticks from: %s\n", filename);

    std::vector<Tick> ticks = LoadTicksCSV(filename, 500000);

    if (ticks.empty()) {
        fprintf(stderr, "Failed to load tick data!\n");
        return 1;
    }

    printf("Loaded %zu ticks successfully.\n", ticks.size());
    printf("Price range: %.2f - %.2f\n",
           std::min_element(ticks.begin(), ticks.end(),
               [](const Tick& a, const Tick& b) { return a.bid < b.bid; })->bid,
           std::max_element(ticks.begin(), ticks.end(),
               [](const Tick& a, const Tick& b) { return a.bid < b.bid; })->bid);
    printf("\n");

    // Run all sizing strategies
    printf("Running backtests...\n");
    std::vector<Result> results;

    printf("  Testing FIXED sizing...\n");
    results.push_back(RunBacktest(ticks, FIXED));

    printf("  Testing ANTI_MARTINGALE sizing...\n");
    results.push_back(RunBacktest(ticks, ANTI_MARTINGALE));

    printf("  Testing PROFIT_BASED sizing...\n");
    results.push_back(RunBacktest(ticks, PROFIT_BASED));

    printf("  Testing WIN_STREAK sizing...\n");
    results.push_back(RunBacktest(ticks, WIN_STREAK));

    // Print results
    PrintResults(results);
    PrintAnalysis(results);

    return 0;
}
