/**
 * NAS100 Trend-Following Strategy Backtest
 *
 * Tests the "open_upwards_while_going_upwards" strategy on NAS100
 *
 * Strategy Logic:
 * - Only BUY positions
 * - Opens new position when price makes new high above last opened price
 * - Dynamic lot sizing to survive X% drop
 * - No TP/SL - positions stay open (trend following)
 * - If at position limit, close smallest profitable to make room
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cfloat>
#include <vector>
#include <algorithm>

// Symbol parameters from MT5 API
struct SymbolParams {
    double contract_size = 1.0;
    double volume_min = 0.01;
    double volume_max = 100.0;
    double volume_step = 0.01;
    int digits = 2;
    double point = 0.01;
    double swap_long = -5.93;
    double swap_short = 1.57;
    int swap_mode = 5;  // SWAP_MODE_CURRENCY_DEPOSIT
    double tick_value = 0.01;
    int trade_calc_mode = 2;  // FUTURES
    double leverage = 500.0;
    double margin_so = 20.0;
};

struct Tick {
    double bid;
    double ask;
    int hour;
    int day_of_week;
    double spread() const { return ask - bid; }
};

struct Position {
    size_t id;
    double entry_price;
    double lot_size;
    double swap_accumulated;
};

struct Config {
    double survive_down = 4.0;      // Survive X% drop from current price
    int max_positions = 200;        // Max positions (0 = unlimited)
    bool close_profitable_for_new = true;  // Close smallest profitable if at limit

    // Additional protections (not in original)
    double close_all_at_dd = 100.0; // Emergency close at DD%
    double trailing_stop_pct = 0.0; // Trailing stop (0 = disabled)
};

struct Result {
    double final_balance;
    double return_multiple;
    double max_dd;
    double max_dd_dollars;
    int total_trades;
    int max_positions_held;
    double max_lot_used;
    double total_swap;
    bool margin_call;
    double peak_equity;
    double lowest_equity;
};

std::vector<Tick> LoadTicks(const char* filename, size_t max_count) {
    std::vector<Tick> ticks;
    ticks.reserve(max_count);

    FILE* f = fopen(filename, "r");
    if (!f) {
        printf("ERROR: Cannot open file %s\n", filename);
        return ticks;
    }

    char line[256];
    fgets(line, sizeof(line), f);  // Skip header

    int prev_day = -1;
    int day_counter = 0;

    while (fgets(line, sizeof(line), f) && ticks.size() < max_count) {
        Tick tick;
        tick.hour = 12;
        tick.day_of_week = 0;

        // Parse datetime: "2025.01.15 11:40:30.123"
        if (strlen(line) >= 16) {
            char h[3] = {line[11], line[12], 0};
            tick.hour = atoi(h);

            // Extract day for swap calculation
            char d[3] = {line[8], line[9], 0};
            int day = atoi(d);
            if (day != prev_day) {
                day_counter++;
                prev_day = day;
            }
            tick.day_of_week = day_counter % 7;
        }

        char* token = strtok(line, "\t");
        if (!token) continue;
        token = strtok(NULL, "\t");
        if (!token) continue;
        tick.bid = atof(token);
        token = strtok(NULL, "\t");
        if (!token) continue;
        tick.ask = atof(token);

        if (tick.bid > 0 && tick.ask > 0) {
            ticks.push_back(tick);
        }
    }
    fclose(f);
    return ticks;
}

// Calculate lot size to survive a percentage drop (original algorithm)
double CalculateLotSize(double equity, double used_margin, double current_ask,
                        double volume_open, const SymbolParams& sym, double survive_pct) {

    const double margin_stop_out = sym.margin_so;

    // Target end price (survive_pct below current)
    double end_price = current_ask * ((100.0 - survive_pct) / 100.0);
    double distance = current_ask - end_price;

    if (distance <= 0) return 0;

    // Equity after existing positions hit worst case
    double equity_at_target = equity - volume_open * distance * sym.contract_size;

    // Check margin level at target
    double margin_level_at_target = (used_margin > 0) ? (equity_at_target / used_margin * 100.0) : 10000.0;

    if (margin_level_at_target <= margin_stop_out) {
        return 0;  // Can't afford any new positions
    }

    // Calculate trade size based on FUTURES calc mode
    // For FUTURES: Margin = Lots * InitialMargin
    // Since margin_initial = 0, use leverage-based calculation
    double spread_cost = (current_ask - sym.point * sym.digits) * sym.contract_size;

    // Simplified: available equity / (distance * contract_size + margin per lot)
    double margin_per_lot = current_ask * sym.contract_size / sym.leverage;
    double available = equity_at_target - (used_margin * margin_stop_out / 100.0);

    if (available <= 0) return 0;

    double lot = available / (distance * sym.contract_size + margin_per_lot);
    lot = std::max(sym.volume_min, std::min(sym.volume_max, lot));
    lot = std::round(lot / sym.volume_step) * sym.volume_step;

    return lot;
}

Result RunStrategy(const std::vector<Tick>& ticks, Config cfg, SymbolParams sym) {
    Result r = {0};

    const double initial_balance = 10000.0;
    double balance = initial_balance;
    double equity = balance;
    double peak_equity = balance;
    double max_drawdown = 0;
    double max_dd_dollars = 0;

    std::vector<Position*> positions;
    size_t next_id = 1;
    double checked_last_open_price = -DBL_MAX;

    int last_day = -1;

    for (size_t i = 0; i < ticks.size(); i++) {
        const Tick& tick = ticks[i];

        // Apply daily swap (simplified)
        if (tick.day_of_week != last_day && last_day >= 0) {
            for (Position* p : positions) {
                double swap = p->lot_size * sym.swap_long;
                // Triple swap on Wednesday (day 3)
                if (tick.day_of_week == 3) swap *= 3;
                p->swap_accumulated += swap;
                balance += swap;
                r.total_swap += swap;
            }
        }
        last_day = tick.day_of_week;

        // Calculate equity and metrics
        equity = balance;
        double volume_open = 0;
        double used_margin = 0;

        for (Position* p : positions) {
            double pl = (tick.bid - p->entry_price) * p->lot_size * sym.contract_size;
            equity += pl;
            volume_open += p->lot_size;
            used_margin += p->lot_size * sym.contract_size * p->entry_price / sym.leverage;
        }

        // Track stats
        if ((int)positions.size() > r.max_positions_held) {
            r.max_positions_held = positions.size();
        }

        // Margin call check
        if (used_margin > 0 && equity < used_margin * sym.margin_so / 100.0) {
            r.margin_call = true;
            for (Position* p : positions) {
                balance += (tick.bid - p->entry_price) * p->lot_size * sym.contract_size;
                r.total_trades++;
                delete p;
            }
            positions.clear();
            equity = balance;
            if (balance <= 0) break;
            peak_equity = balance;
            checked_last_open_price = -DBL_MAX;
            continue;
        }

        // Track peak/drawdown
        if (positions.empty()) peak_equity = balance;
        if (equity > peak_equity) {
            peak_equity = equity;
            r.peak_equity = peak_equity;
        }
        if (equity < r.lowest_equity || r.lowest_equity == 0) {
            r.lowest_equity = equity;
        }

        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        double dd_dollars = peak_equity - equity;

        if (dd_pct > max_drawdown) max_drawdown = dd_pct;
        if (dd_dollars > max_dd_dollars) max_dd_dollars = dd_dollars;

        // Emergency close (optional protection)
        if (cfg.close_all_at_dd < 100.0 && dd_pct > cfg.close_all_at_dd && !positions.empty()) {
            for (Position* p : positions) {
                balance += (tick.bid - p->entry_price) * p->lot_size * sym.contract_size;
                r.total_trades++;
                delete p;
            }
            positions.clear();
            equity = balance;
            peak_equity = balance;
            checked_last_open_price = -DBL_MAX;
            continue;
        }

        // CORE STRATEGY: Open new position when price makes new high
        if (volume_open == 0 || tick.ask > checked_last_open_price) {

            // Calculate lot size
            double lot = CalculateLotSize(equity, used_margin, tick.ask, volume_open, sym, cfg.survive_down);

            if (lot >= sym.volume_min) {
                // Check if at position limit
                if (cfg.max_positions > 0 && (int)positions.size() >= cfg.max_positions) {
                    if (cfg.close_profitable_for_new) {
                        // Find smallest profitable position to close
                        double min_lot = DBL_MAX;
                        Position* to_close = nullptr;

                        for (Position* p : positions) {
                            double profit = (tick.bid - p->entry_price) * p->lot_size * sym.contract_size;
                            if (profit > 0 && p->lot_size < min_lot) {
                                min_lot = p->lot_size;
                                to_close = p;
                            }
                        }

                        if (to_close) {
                            balance += (tick.bid - to_close->entry_price) * to_close->lot_size * sym.contract_size;
                            r.total_trades++;
                            positions.erase(std::find(positions.begin(), positions.end(), to_close));
                            delete to_close;
                        }
                    }
                }

                // Open new position if we have room
                if (cfg.max_positions == 0 || (int)positions.size() < cfg.max_positions) {
                    // Final margin check
                    double margin_needed = lot * sym.contract_size * tick.ask / sym.leverage;
                    double free_margin = equity - used_margin;

                    if (free_margin > margin_needed * 1.2) {
                        Position* p = new Position();
                        p->id = next_id++;
                        p->entry_price = tick.ask;
                        p->lot_size = lot;
                        p->swap_accumulated = 0;
                        positions.push_back(p);

                        checked_last_open_price = tick.ask;

                        if (lot > r.max_lot_used) r.max_lot_used = lot;
                    }
                }
            }
        }
    }

    // Close remaining positions at end
    for (Position* p : positions) {
        balance += (ticks.back().bid - p->entry_price) * p->lot_size * sym.contract_size;
        r.total_trades++;
        delete p;
    }

    r.final_balance = std::max(0.0, balance);
    r.return_multiple = r.final_balance / initial_balance;
    r.max_dd = max_drawdown;
    r.max_dd_dollars = max_dd_dollars;

    return r;
}

int main() {
    printf("================================================================\n");
    printf("NAS100 TREND-FOLLOWING STRATEGY BACKTEST\n");
    printf("================================================================\n\n");

    // Load symbol parameters from MT5
    SymbolParams sym;
    sym.contract_size = 1.0;
    sym.volume_min = 0.01;
    sym.volume_max = 100.0;
    sym.volume_step = 0.01;
    sym.digits = 2;
    sym.point = 0.01;
    sym.swap_long = -5.93;
    sym.swap_short = 1.57;
    sym.swap_mode = 5;
    sym.tick_value = 0.01;
    sym.trade_calc_mode = 2;  // FUTURES
    sym.leverage = 500.0;
    sym.margin_so = 20.0;

    printf("Symbol Parameters (from MT5 API):\n");
    printf("  Contract Size: %.2f\n", sym.contract_size);
    printf("  Leverage: 1:%.0f\n", sym.leverage);
    printf("  Volume Min/Max: %.2f / %.2f\n", sym.volume_min, sym.volume_max);
    printf("  Swap Long: %.2f\n", sym.swap_long);
    printf("  Margin SO: %.1f%%\n\n", sym.margin_so);

    // Load tick data
    const char* filename = "NAS100/NAS100_TICKS_2025.csv";
    printf("Loading tick data from %s...\n", filename);

    std::vector<Tick> ticks = LoadTicks(filename, 60000000);
    if (ticks.empty()) {
        printf("ERROR: Failed to load data\n");
        return 1;
    }

    printf("Loaded %zu ticks\n", ticks.size());
    printf("Price range: %.2f to %.2f\n", ticks.front().bid, ticks.back().bid);

    double price_change = (ticks.back().bid - ticks.front().bid) / ticks.front().bid * 100;
    printf("Price change: %.2f -> %.2f (%+.1f%%)\n\n", ticks.front().bid, ticks.back().bid, price_change);

    if (price_change > 5) printf("Market Direction: UPWARD TREND\n\n");
    else if (price_change < -5) printf("Market Direction: DOWNWARD TREND\n\n");
    else printf("Market Direction: SIDEWAYS\n\n");

    // ========================================================================
    // TEST 1: Original strategy with different survive percentages
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 1: SURVIVE PARAMETER SWEEP\n");
    printf("================================================================\n\n");

    printf("%-10s %12s %8s %10s %8s %10s %10s\n",
           "Survive%", "Final Bal", "Return", "Max DD", "Trades", "MaxPos", "MaxLot");
    printf("--------------------------------------------------------------------------------\n");

    double survives[] = {2.0, 3.0, 4.0, 5.0, 6.0, 8.0, 10.0, 15.0, 20.0};
    Result best_result;
    best_result.return_multiple = 0;
    double best_survive = 4.0;

    for (double surv : survives) {
        Config cfg;
        cfg.survive_down = surv;
        cfg.max_positions = 0;  // No limit (original behavior)
        cfg.close_all_at_dd = 100.0;  // No emergency close

        Result r = RunStrategy(ticks, cfg, sym);

        printf("%-10.1f $%11.2f %7.1fx %9.1f%% %8d %10d %10.2f %s\n",
               surv, r.final_balance, r.return_multiple, r.max_dd,
               r.total_trades, r.max_positions_held, r.max_lot_used,
               r.margin_call ? "MARGIN CALL" : "");

        if (!r.margin_call && r.return_multiple > best_result.return_multiple) {
            best_result = r;
            best_survive = surv;
        }
    }

    printf("--------------------------------------------------------------------------------\n\n");

    // ========================================================================
    // TEST 2: Add position limits
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 2: POSITION LIMIT SWEEP (survive=%.1f%%)\n", best_survive);
    printf("================================================================\n\n");

    printf("%-10s %12s %8s %10s %8s\n",
           "MaxPos", "Final Bal", "Return", "Max DD", "Trades");
    printf("----------------------------------------------------------------\n");

    int pos_limits[] = {10, 25, 50, 100, 200, 500, 0};

    for (int limit : pos_limits) {
        Config cfg;
        cfg.survive_down = best_survive;
        cfg.max_positions = limit;
        cfg.close_all_at_dd = 100.0;

        Result r = RunStrategy(ticks, cfg, sym);

        printf("%-10d $%11.2f %7.1fx %9.1f%% %8d %s\n",
               limit == 0 ? 9999 : limit, r.final_balance, r.return_multiple, r.max_dd,
               r.total_trades, r.margin_call ? "MARGIN CALL" : "");
    }

    printf("----------------------------------------------------------------\n\n");

    // ========================================================================
    // TEST 3: Add DD protection
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 3: DD PROTECTION SWEEP (survive=%.1f%%)\n", best_survive);
    printf("================================================================\n\n");

    printf("%-12s %12s %8s %10s %8s\n",
           "Close@DD", "Final Bal", "Return", "Max DD", "Trades");
    printf("----------------------------------------------------------------\n");

    double dd_levels[] = {10.0, 15.0, 20.0, 25.0, 30.0, 40.0, 50.0, 100.0};

    for (double dd : dd_levels) {
        Config cfg;
        cfg.survive_down = best_survive;
        cfg.max_positions = 0;
        cfg.close_all_at_dd = dd;

        Result r = RunStrategy(ticks, cfg, sym);

        printf("%-12.0f $%11.2f %7.1fx %9.1f%% %8d %s\n",
               dd, r.final_balance, r.return_multiple, r.max_dd,
               r.total_trades, r.margin_call ? "MARGIN CALL" : "");
    }

    printf("----------------------------------------------------------------\n\n");

    // ========================================================================
    // FINAL COMPARISON
    // ========================================================================
    printf("================================================================\n");
    printf("STRATEGY COMPARISON\n");
    printf("================================================================\n\n");

    // Original strategy
    Config orig_cfg;
    orig_cfg.survive_down = 4.0;
    orig_cfg.max_positions = 0;
    orig_cfg.close_all_at_dd = 100.0;
    Result orig = RunStrategy(ticks, orig_cfg, sym);

    // Buy and hold simulation
    double buy_hold_lots = 0.5;  // Moderate position
    double buy_hold_return = (ticks.back().bid - ticks.front().bid) * buy_hold_lots * sym.contract_size;
    double buy_hold_pct = buy_hold_return / 10000.0 * 100;

    printf("%-30s %12s %8s %10s\n", "Strategy", "Final Bal", "Return", "Max DD");
    printf("----------------------------------------------------------------\n");
    printf("%-30s $%11.2f %7.1fx %9.1f%% %s\n",
           "Original (survive=4%)",
           orig.final_balance, orig.return_multiple, orig.max_dd,
           orig.margin_call ? "MARGIN CALL" : "");

    // Best configuration
    Config best_cfg;
    best_cfg.survive_down = best_survive;
    best_cfg.max_positions = 100;
    best_cfg.close_all_at_dd = 30.0;
    Result best = RunStrategy(ticks, best_cfg, sym);

    printf("%-30s $%11.2f %7.1fx %9.1f%%\n",
           "Optimized (with protection)",
           best.final_balance, best.return_multiple, best.max_dd);

    printf("%-30s $%11.2f %7.1fx %9.1f%%\n",
           "Buy & Hold (0.5 lots)",
           10000.0 + buy_hold_return, 1.0 + buy_hold_return/10000.0, price_change > 0 ? 0.0 : -price_change);

    printf("----------------------------------------------------------------\n\n");

    printf("Market moved: %+.1f%% (NAS100 in 2025)\n", price_change);
    printf("Total Swap Cost: $%.2f\n", orig.total_swap);

    printf("\n================================================================\n");
    printf("BACKTEST COMPLETE\n");
    printf("================================================================\n");

    return 0;
}
