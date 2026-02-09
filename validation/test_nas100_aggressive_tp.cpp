/**
 * NAS100 Aggressive Take Profit Strategy
 *
 * The core issue: Strategy builds positions but corrections wipe out gains
 * Solution: Take profit aggressively on individual positions
 *
 * Key changes:
 * 1. Per-position trailing stop (not portfolio level)
 * 2. Fixed take-profit per position
 * 3. Quick exit when momentum turns
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cfloat>
#include <vector>
#include <algorithm>
#include <deque>

struct Tick {
    double bid;
    double ask;
    double spread() const { return ask - bid; }
};

struct Position {
    size_t id;
    double entry_price;
    double lot_size;
    double peak_price;
    bool is_trailing;
};

struct Config {
    double manual_stop_out = 74.0;
    double min_lot = 0.01;
    double max_lot = 100.0;
    double leverage = 500.0;
    double contract_size = 1.0;

    // Position sizing
    double multiplier = 0.1;
    double power = -0.5;
    double lot_coefficient = 50.0;
    bool direct_lot_formula = true;

    // Take profit modes
    int tp_mode = 0;  // 0=None, 1=Fixed TP, 2=Trailing, 3=Momentum exit

    // Fixed TP
    double fixed_tp_pts = 500;

    // Per-position trailing
    double trail_activation_pts = 100;   // Start trailing after 100 pts profit
    double trail_distance_pts = 50;      // Trail 50 pts behind peak

    // Momentum exit (SMA cross)
    int momentum_fast = 50;
    int momentum_slow = 200;
    double momentum_threshold = 0;  // Exit if fast < slow

    // Portfolio drawdown limit
    double max_portfolio_dd = 30.0;
};

struct Result {
    double final_balance;
    double return_multiple;
    double max_dd;
    int total_trades;
    int winning_trades;
    int tp_exits;
    int trail_exits;
    int momentum_exits;
    int stop_out_count;
    bool margin_call;
};

std::vector<Tick> LoadTicks(const char* filename, size_t max_count) {
    std::vector<Tick> ticks;
    ticks.reserve(max_count);
    FILE* f = fopen(filename, "r");
    if (!f) return ticks;
    char line[256];
    fgets(line, sizeof(line), f);
    while (fgets(line, sizeof(line), f) && ticks.size() < max_count) {
        Tick tick;
        char* token = strtok(line, "\t");
        if (!token) continue;
        token = strtok(NULL, "\t");
        if (!token) continue;
        tick.bid = atof(token);
        token = strtok(NULL, "\t");
        if (!token) continue;
        tick.ask = atof(token);
        if (tick.bid > 0 && tick.ask > 0) ticks.push_back(tick);
    }
    fclose(f);
    return ticks;
}

Result RunStrategy(const std::vector<Tick>& ticks, Config cfg) {
    Result r = {0};
    if (ticks.empty()) return r;

    const double initial_balance = 10000.0;
    double balance = initial_balance;
    double equity = balance;
    double peak_equity = balance;
    double max_drawdown = 0;

    std::vector<Position*> positions;
    size_t next_id = 1;

    double checked_last_open_price = -DBL_MAX;
    double starting_x = 0;
    double local_starting_room = 0;
    double volume_of_open_trades = 0;

    // Momentum tracking (simple moving averages of price)
    std::deque<double> fast_prices, slow_prices;

    for (size_t i = 0; i < ticks.size(); i++) {
        const Tick& tick = ticks[i];

        // Update momentum (sample every 100 ticks to reduce noise)
        if (i % 100 == 0) {
            fast_prices.push_back(tick.bid);
            slow_prices.push_back(tick.bid);
            if ((int)fast_prices.size() > cfg.momentum_fast) fast_prices.pop_front();
            if ((int)slow_prices.size() > cfg.momentum_slow) slow_prices.pop_front();
        }

        // Calculate SMAs
        double fast_sma = 0, slow_sma = 0;
        if (!fast_prices.empty()) {
            for (double p : fast_prices) fast_sma += p;
            fast_sma /= fast_prices.size();
        }
        if (!slow_prices.empty()) {
            for (double p : slow_prices) slow_sma += p;
            slow_sma /= slow_prices.size();
        }

        // Calculate equity and margin
        equity = balance;
        double used_margin = 0;
        volume_of_open_trades = 0;

        for (Position* p : positions) {
            double pos_pnl = (tick.bid - p->entry_price) * p->lot_size * cfg.contract_size;
            equity += pos_pnl;
            used_margin += p->lot_size * cfg.contract_size * p->entry_price / cfg.leverage;
            volume_of_open_trades += p->lot_size;

            if (tick.bid > p->peak_price) p->peak_price = tick.bid;
        }

        double margin_level = (used_margin > 0) ? (equity / used_margin * 100.0) : 10000.0;

        // === TAKE PROFIT LOGIC ===
        std::vector<Position*> to_close;
        for (Position* p : positions) {
            double profit_pts = tick.bid - p->entry_price;
            double peak_pullback = p->peak_price - tick.bid;
            bool should_close = false;
            int exit_type = 0;  // 1=TP, 2=Trail, 3=Momentum

            // Mode 1: Fixed take profit
            if (cfg.tp_mode == 1 && profit_pts >= cfg.fixed_tp_pts) {
                should_close = true;
                exit_type = 1;
            }

            // Mode 2: Trailing stop per position
            if (cfg.tp_mode == 2) {
                if (profit_pts >= cfg.trail_activation_pts) {
                    p->is_trailing = true;
                }
                if (p->is_trailing && peak_pullback >= cfg.trail_distance_pts) {
                    should_close = true;
                    exit_type = 2;
                }
            }

            // Mode 3: Momentum exit (exit when fast SMA < slow SMA)
            if (cfg.tp_mode == 3 && fast_prices.size() >= (size_t)cfg.momentum_fast &&
                slow_prices.size() >= (size_t)cfg.momentum_slow) {
                if (fast_sma < slow_sma - cfg.momentum_threshold && profit_pts > 0) {
                    should_close = true;
                    exit_type = 3;
                }
            }

            // Mode 4: Combined - TP or Trailing
            if (cfg.tp_mode == 4) {
                if (profit_pts >= cfg.fixed_tp_pts) {
                    should_close = true;
                    exit_type = 1;
                }
                if (profit_pts >= cfg.trail_activation_pts) {
                    p->is_trailing = true;
                }
                if (p->is_trailing && peak_pullback >= cfg.trail_distance_pts) {
                    should_close = true;
                    exit_type = 2;
                }
            }

            // Mode 5: Aggressive trailing (low activation, tight trail)
            if (cfg.tp_mode == 5) {
                if (profit_pts >= 50) {  // Very low activation
                    p->is_trailing = true;
                }
                if (p->is_trailing && peak_pullback >= 30) {  // Very tight trail
                    should_close = true;
                    exit_type = 2;
                }
            }

            if (should_close) {
                to_close.push_back(p);
                if (exit_type == 1) r.tp_exits++;
                else if (exit_type == 2) r.trail_exits++;
                else if (exit_type == 3) r.momentum_exits++;
            }
        }

        for (Position* p : to_close) {
            double pnl = (tick.bid - p->entry_price) * p->lot_size * cfg.contract_size;
            balance += pnl;
            r.total_trades++;
            if (pnl > 0) r.winning_trades++;
            positions.erase(std::remove(positions.begin(), positions.end(), p), positions.end());
            delete p;
        }

        // Recalculate after closes
        if (!to_close.empty()) {
            equity = balance;
            used_margin = 0;
            volume_of_open_trades = 0;
            for (Position* p : positions) {
                equity += (tick.bid - p->entry_price) * p->lot_size * cfg.contract_size;
                used_margin += p->lot_size * cfg.contract_size * p->entry_price / cfg.leverage;
                volume_of_open_trades += p->lot_size;
            }
            margin_level = (used_margin > 0) ? (equity / used_margin * 100.0) : 10000.0;
        }

        // === PORTFOLIO DRAWDOWN LIMIT ===
        double portfolio_dd = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        if (portfolio_dd > cfg.max_portfolio_dd && !positions.empty()) {
            // Close all
            for (Position* p : positions) {
                double pnl = (tick.bid - p->entry_price) * p->lot_size * cfg.contract_size;
                balance += pnl;
                r.total_trades++;
                if (pnl > 0) r.winning_trades++;
                delete p;
            }
            positions.clear();
            equity = balance;
            peak_equity = balance;
            checked_last_open_price = -DBL_MAX;
            volume_of_open_trades = 0;
            r.stop_out_count++;
            continue;
        }

        // Hard margin call
        if (used_margin > 0 && equity < used_margin * 0.2) {
            r.margin_call = true;
            for (Position* p : positions) {
                double pnl = (tick.bid - p->entry_price) * p->lot_size * cfg.contract_size;
                balance += pnl;
                r.total_trades++;
                if (pnl > 0) r.winning_trades++;
                delete p;
            }
            positions.clear();
            if (balance <= 0) break;
            peak_equity = balance;
            checked_last_open_price = -DBL_MAX;
            volume_of_open_trades = 0;
            continue;
        }

        // Manual stop out
        if (margin_level < cfg.manual_stop_out && margin_level > 0 && !positions.empty()) {
            r.stop_out_count++;
            for (Position* p : positions) {
                double pnl = (tick.bid - p->entry_price) * p->lot_size * cfg.contract_size;
                balance += pnl;
                r.total_trades++;
                if (pnl > 0) r.winning_trades++;
                delete p;
            }
            positions.clear();
            equity = balance;
            peak_equity = balance;
            checked_last_open_price = -DBL_MAX;
            volume_of_open_trades = 0;
            continue;
        }

        if (positions.empty()) peak_equity = balance;
        if (equity > peak_equity) peak_equity = equity;

        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        if (dd_pct > max_drawdown) max_drawdown = dd_pct;

        // === OPEN POSITIONS ===
        double spread_cost = tick.spread() * cfg.contract_size;

        if (volume_of_open_trades == 0) {
            local_starting_room = tick.ask * cfg.multiplier / 100.0;

            double lot;
            if (cfg.direct_lot_formula) {
                lot = cfg.lot_coefficient;
            } else {
                double temp = (100 * balance * cfg.leverage) /
                             (100 * local_starting_room * cfg.leverage + 100 * spread_cost * cfg.leverage +
                              cfg.manual_stop_out * tick.ask);
                lot = temp / cfg.contract_size;
            }

            if (lot >= cfg.min_lot) {
                lot = std::min(lot, cfg.max_lot);
                lot = std::round(lot * 100) / 100;

                double margin_needed = lot * cfg.contract_size * tick.ask / cfg.leverage;
                if (equity > margin_needed * 1.2) {
                    Position* p = new Position();
                    p->id = next_id++;
                    p->entry_price = tick.ask;
                    p->lot_size = lot;
                    p->peak_price = tick.bid;
                    p->is_trailing = false;
                    positions.push_back(p);
                    checked_last_open_price = tick.ask;
                    starting_x = tick.ask;
                }
            }
        }
        else if (tick.ask > checked_last_open_price) {
            double distance = tick.ask - starting_x;
            if (distance < 1) distance = 1;

            double lot;
            if (cfg.direct_lot_formula) {
                lot = cfg.lot_coefficient * std::pow(distance, cfg.power);
                lot = std::min(lot, cfg.max_lot);
                lot = std::max(lot, cfg.min_lot);
            } else {
                double room_temp = local_starting_room * std::pow(distance, cfg.power);
                double temp = (100 * equity * cfg.leverage -
                              cfg.leverage * cfg.manual_stop_out * used_margin -
                              100 * room_temp * cfg.leverage * volume_of_open_trades) /
                             (100 * room_temp * cfg.leverage + 100 * spread_cost * cfg.leverage +
                              cfg.manual_stop_out * tick.ask);
                lot = temp / cfg.contract_size;
                if (lot < cfg.min_lot) continue;
                lot = std::min(lot, cfg.max_lot);
            }

            lot = std::round(lot * 100) / 100;
            if (lot >= cfg.min_lot) {
                double margin_needed = lot * cfg.contract_size * tick.ask / cfg.leverage;
                double free_margin = equity - used_margin;

                if (free_margin > margin_needed * 1.2) {
                    Position* p = new Position();
                    p->id = next_id++;
                    p->entry_price = tick.ask;
                    p->lot_size = lot;
                    p->peak_price = tick.bid;
                    p->is_trailing = false;
                    positions.push_back(p);
                    checked_last_open_price = tick.ask;
                }
            }
        }
    }

    // Close remaining
    for (Position* p : positions) {
        double pnl = (ticks.back().bid - p->entry_price) * p->lot_size * cfg.contract_size;
        balance += pnl;
        r.total_trades++;
        if (pnl > 0) r.winning_trades++;
        delete p;
    }

    r.final_balance = std::max(0.0, balance);
    r.return_multiple = r.final_balance / initial_balance;
    r.max_dd = max_drawdown;
    return r;
}

void PrintResult(const char* name, Result r) {
    printf("%-35s $%10.2f %6.1fx %7.1f%% %6d %5.1f%% %4d %s\n",
           name, r.final_balance, r.return_multiple, r.max_dd,
           r.total_trades,
           r.total_trades > 0 ? (double)r.winning_trades / r.total_trades * 100 : 0,
           r.stop_out_count,
           r.margin_call ? "MC" : "");
}

int main() {
    printf("================================================================\n");
    printf("NAS100 AGGRESSIVE TAKE PROFIT STRATEGIES\n");
    printf("================================================================\n\n");

    std::vector<Tick> ticks = LoadTicks("NAS100/NAS100_TICKS_2025.csv", 60000000);
    if (ticks.empty()) { printf("ERROR: No data\n"); return 1; }

    printf("Loaded %zu ticks\n", ticks.size());
    printf("Price: %.2f -> %.2f (%+.1f%%)\n\n",
           ticks.front().bid, ticks.back().bid,
           (ticks.back().bid - ticks.front().bid) / ticks.front().bid * 100);

    // ========================================================================
    // TEST 1: BASELINE - No TP
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 1: BASELINE (No Take Profit)\n");
    printf("================================================================\n\n");

    printf("%-35s %12s %7s %8s %6s %6s %4s\n",
           "Config", "Final", "Return", "MaxDD", "Trades", "Win%", "SOs");
    printf("--------------------------------------------------------------------------------\n");

    double powers[] = {-0.811, -0.5, -0.3};
    double coefficients[] = {20, 50, 100};

    for (double pw : powers) {
        for (double coef : coefficients) {
            Config cfg;
            cfg.tp_mode = 0;  // No TP
            cfg.power = pw;
            cfg.lot_coefficient = coef;
            Result r = RunStrategy(ticks, cfg);
            char name[64];
            snprintf(name, sizeof(name), "pw=%.3f coef=%.0f", pw, coef);
            PrintResult(name, r);
        }
    }
    printf("--------------------------------------------------------------------------------\n\n");

    // ========================================================================
    // TEST 2: FIXED TAKE PROFIT
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 2: FIXED TAKE PROFIT\n");
    printf("================================================================\n\n");

    printf("%-35s %12s %7s %8s %6s %6s %4s\n",
           "TP Distance", "Final", "Return", "MaxDD", "Trades", "Win%", "SOs");
    printf("--------------------------------------------------------------------------------\n");

    double tp_distances[] = {100, 200, 300, 500, 750, 1000};
    for (double tp : tp_distances) {
        Config cfg;
        cfg.tp_mode = 1;
        cfg.fixed_tp_pts = tp;
        cfg.power = -0.5;
        cfg.lot_coefficient = 50;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "TP=%.0f pts", tp);
        PrintResult(name, r);
        printf("                                     TP exits: %d\n", r.tp_exits);
    }
    printf("--------------------------------------------------------------------------------\n\n");

    // ========================================================================
    // TEST 3: PER-POSITION TRAILING STOP
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 3: PER-POSITION TRAILING STOP\n");
    printf("================================================================\n\n");

    printf("%-35s %12s %7s %8s %6s %6s %4s\n",
           "Activation/Trail Distance", "Final", "Return", "MaxDD", "Trades", "Win%", "SOs");
    printf("--------------------------------------------------------------------------------\n");

    double activations[] = {50, 100, 200, 300};
    double trails[] = {25, 50, 100};

    for (double act : activations) {
        for (double tr : trails) {
            Config cfg;
            cfg.tp_mode = 2;
            cfg.trail_activation_pts = act;
            cfg.trail_distance_pts = tr;
            cfg.power = -0.5;
            cfg.lot_coefficient = 50;
            Result r = RunStrategy(ticks, cfg);
            char name[64];
            snprintf(name, sizeof(name), "Act=%.0f Trail=%.0f", act, tr);
            PrintResult(name, r);
        }
    }
    printf("--------------------------------------------------------------------------------\n\n");

    // ========================================================================
    // TEST 4: MOMENTUM EXIT
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 4: MOMENTUM EXIT (SMA Cross)\n");
    printf("================================================================\n\n");

    printf("%-35s %12s %7s %8s %6s %6s %4s\n",
           "Fast/Slow SMA", "Final", "Return", "MaxDD", "Trades", "Win%", "SOs");
    printf("--------------------------------------------------------------------------------\n");

    int fast_periods[] = {20, 50, 100};
    int slow_periods[] = {100, 200, 500};

    for (int fast : fast_periods) {
        for (int slow : slow_periods) {
            if (fast >= slow) continue;
            Config cfg;
            cfg.tp_mode = 3;
            cfg.momentum_fast = fast;
            cfg.momentum_slow = slow;
            cfg.power = -0.5;
            cfg.lot_coefficient = 50;
            Result r = RunStrategy(ticks, cfg);
            char name[64];
            snprintf(name, sizeof(name), "SMA %d/%d", fast, slow);
            PrintResult(name, r);
        }
    }
    printf("--------------------------------------------------------------------------------\n\n");

    // ========================================================================
    // TEST 5: COMBINED TP + TRAILING
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 5: COMBINED (TP + Trailing)\n");
    printf("================================================================\n\n");

    printf("%-35s %12s %7s %8s %6s %6s %4s\n",
           "Config", "Final", "Return", "MaxDD", "Trades", "Win%", "SOs");
    printf("--------------------------------------------------------------------------------\n");

    struct CombinedCfg { double tp; double act; double trail; };
    CombinedCfg combined[] = {
        {500, 100, 50},
        {500, 200, 100},
        {300, 100, 50},
        {300, 150, 75},
        {200, 50, 30},
        {1000, 300, 150}
    };

    for (const auto& c : combined) {
        Config cfg;
        cfg.tp_mode = 4;
        cfg.fixed_tp_pts = c.tp;
        cfg.trail_activation_pts = c.act;
        cfg.trail_distance_pts = c.trail;
        cfg.power = -0.5;
        cfg.lot_coefficient = 50;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "TP=%.0f Act=%.0f Tr=%.0f", c.tp, c.act, c.trail);
        PrintResult(name, r);
        printf("                                     TP: %d, Trail: %d\n", r.tp_exits, r.trail_exits);
    }
    printf("--------------------------------------------------------------------------------\n\n");

    // ========================================================================
    // TEST 6: AGGRESSIVE TRAILING (Mode 5)
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 6: AGGRESSIVE TRAILING (50pt act, 30pt trail)\n");
    printf("================================================================\n\n");

    printf("%-35s %12s %7s %8s %6s %6s %4s\n",
           "Config", "Final", "Return", "MaxDD", "Trades", "Win%", "SOs");
    printf("--------------------------------------------------------------------------------\n");

    for (double pw : powers) {
        for (double coef : coefficients) {
            Config cfg;
            cfg.tp_mode = 5;  // Aggressive trailing
            cfg.power = pw;
            cfg.lot_coefficient = coef;
            Result r = RunStrategy(ticks, cfg);
            char name[64];
            snprintf(name, sizeof(name), "Aggr Trail pw=%.3f coef=%.0f", pw, coef);
            PrintResult(name, r);
        }
    }
    printf("--------------------------------------------------------------------------------\n\n");

    // ========================================================================
    // TEST 7: PORTFOLIO DRAWDOWN LIMIT
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 7: PORTFOLIO DRAWDOWN LIMIT\n");
    printf("================================================================\n\n");

    printf("%-35s %12s %7s %8s %6s %6s %4s\n",
           "Max DD Limit", "Final", "Return", "MaxDD", "Trades", "Win%", "SOs");
    printf("--------------------------------------------------------------------------------\n");

    double dd_limits[] = {15, 20, 25, 30, 40, 50};
    for (double dd : dd_limits) {
        Config cfg;
        cfg.tp_mode = 2;  // Trailing
        cfg.trail_activation_pts = 100;
        cfg.trail_distance_pts = 50;
        cfg.max_portfolio_dd = dd;
        cfg.power = -0.5;
        cfg.lot_coefficient = 50;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "Max DD=%.0f%%", dd);
        PrintResult(name, r);
    }
    printf("--------------------------------------------------------------------------------\n\n");

    // ========================================================================
    // BEST CONFIGURATIONS
    // ========================================================================
    printf("================================================================\n");
    printf("BEST CONFIGURATIONS\n");
    printf("================================================================\n\n");

    printf("%-35s %12s %7s %8s %6s %6s %4s\n",
           "Strategy", "Final", "Return", "MaxDD", "Trades", "Win%", "SOs");
    printf("--------------------------------------------------------------------------------\n");

    // Best fixed TP
    {
        Config cfg;
        cfg.tp_mode = 1;
        cfg.fixed_tp_pts = 300;
        cfg.power = -0.5;
        cfg.lot_coefficient = 50;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Fixed TP 300pts", r);
    }

    // Best trailing
    {
        Config cfg;
        cfg.tp_mode = 2;
        cfg.trail_activation_pts = 100;
        cfg.trail_distance_pts = 50;
        cfg.power = -0.5;
        cfg.lot_coefficient = 50;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Trailing 100/50", r);
    }

    // Aggressive trailing
    {
        Config cfg;
        cfg.tp_mode = 5;
        cfg.power = -0.5;
        cfg.lot_coefficient = 50;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Aggressive Trail 50/30", r);
    }

    // Combined
    {
        Config cfg;
        cfg.tp_mode = 4;
        cfg.fixed_tp_pts = 300;
        cfg.trail_activation_pts = 100;
        cfg.trail_distance_pts = 50;
        cfg.power = -0.5;
        cfg.lot_coefficient = 50;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Combined TP+Trail", r);
    }

    // Combined + DD limit
    {
        Config cfg;
        cfg.tp_mode = 4;
        cfg.fixed_tp_pts = 300;
        cfg.trail_activation_pts = 100;
        cfg.trail_distance_pts = 50;
        cfg.max_portfolio_dd = 25;
        cfg.power = -0.5;
        cfg.lot_coefficient = 50;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Combined + DD Limit 25%", r);
    }

    printf("--------------------------------------------------------------------------------\n");

    printf("\n================================================================\n");
    return 0;
}
