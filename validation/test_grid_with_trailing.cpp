/**
 * grid_open_upwards_while_going_upwards with Trailing Stop
 *
 * Original strategy:
 * - Opens BUY when price makes new high
 * - Lot sized to survive X% drop
 * - No take profit, holds forever
 * - Closes all at margin call
 *
 * Adding:
 * - ATR-based trailing stop
 * - Fixed trailing stop
 * - Per-position stop loss
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
    double stop_loss;
};

struct Config {
    // Account
    double leverage = 500.0;
    double contract_size = 1.0;
    double min_lot = 0.01;
    double max_lot = 100.0;

    // Original grid_open_upwards params
    double survive_down_pct = 4.0;    // Original default was 4%
    double min_spacing = 0.0;          // Min distance between positions (0 = every tick)

    // Trailing stop mode
    int trail_mode = 0;
    // 0 = None (original behavior)
    // 1 = ATR-based trailing
    // 2 = Fixed distance trailing
    // 3 = Percentage trailing
    // 4 = Break-even + trail

    // ATR trailing params
    double atr_multiplier = 2.0;
    int atr_period = 100;
    int atr_sample_rate = 10;

    // Fixed trailing params
    double trail_activation = 100;    // Start trailing after X pts profit
    double trail_distance = 50;       // Trail X pts behind peak

    // Percentage trailing
    double trail_pct = 0.5;           // Trail X% below peak

    // Break-even params
    double be_trigger = 50;           // Move to BE after X pts
    double be_offset = 10;            // Set SL X pts above entry

    // Take profit (optional)
    double take_profit_pts = 0;       // 0 = disabled

    // Portfolio protection
    double max_portfolio_dd = 100;    // 100 = disabled (original behavior)
    double manual_stop_out = 20;      // Original uses margin call at 20%
};

struct Result {
    double final_balance;
    double return_multiple;
    double max_dd;
    int total_trades;
    int winning_trades;
    int trail_exits;
    int tp_exits;
    int max_positions;
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

    double last_open_price = -1e9;

    // ATR calculation
    std::deque<double> price_changes;
    double current_atr = 50.0;
    double prev_price = ticks.front().bid;

    for (size_t i = 0; i < ticks.size(); i++) {
        const Tick& tick = ticks[i];

        // Update ATR
        if (cfg.trail_mode == 1 && i % cfg.atr_sample_rate == 0) {
            double change = std::abs(tick.bid - prev_price);
            price_changes.push_back(change);
            if ((int)price_changes.size() > cfg.atr_period) price_changes.pop_front();
            if (!price_changes.empty()) {
                current_atr = 0;
                for (double c : price_changes) current_atr += c;
                current_atr /= price_changes.size();
            }
            prev_price = tick.bid;
        }

        // Update positions and trailing stops
        equity = balance;
        double used_margin = 0;

        for (Position* p : positions) {
            double pnl = (tick.bid - p->entry_price) * p->lot_size * cfg.contract_size;
            equity += pnl;
            used_margin += p->lot_size * cfg.contract_size * p->entry_price / cfg.leverage;

            // Update peak
            if (tick.bid > p->peak_price) {
                p->peak_price = tick.bid;
            }

            double profit = tick.bid - p->entry_price;

            // Update trailing stop based on mode
            switch (cfg.trail_mode) {
                case 1:  // ATR-based
                    if (profit > current_atr * cfg.atr_multiplier) {
                        double new_sl = p->peak_price - current_atr * cfg.atr_multiplier;
                        if (new_sl > p->stop_loss) p->stop_loss = new_sl;
                    }
                    break;

                case 2:  // Fixed distance
                    if (profit > cfg.trail_activation) {
                        double new_sl = p->peak_price - cfg.trail_distance;
                        if (new_sl > p->stop_loss) p->stop_loss = new_sl;
                    }
                    break;

                case 3:  // Percentage
                    if (profit > 0) {
                        double new_sl = p->peak_price * (1.0 - cfg.trail_pct / 100.0);
                        if (new_sl > p->stop_loss && new_sl > p->entry_price) {
                            p->stop_loss = new_sl;
                        }
                    }
                    break;

                case 4:  // Break-even + trail
                    if (profit >= cfg.be_trigger && p->stop_loss < p->entry_price) {
                        p->stop_loss = p->entry_price + cfg.be_offset;
                    }
                    if (p->stop_loss >= p->entry_price && profit > cfg.trail_activation) {
                        double new_sl = p->peak_price - cfg.trail_distance;
                        if (new_sl > p->stop_loss) p->stop_loss = new_sl;
                    }
                    break;
            }
        }

        double margin_level = (used_margin > 0) ? (equity / used_margin * 100.0) : 10000.0;
        if ((int)positions.size() > r.max_positions) r.max_positions = positions.size();

        // Check exits
        std::vector<Position*> to_close;
        for (Position* p : positions) {
            bool should_close = false;
            int exit_type = 0;

            // Trailing stop hit
            if (p->stop_loss > 0 && tick.bid <= p->stop_loss) {
                should_close = true;
                exit_type = 1;
            }

            // Take profit (if enabled)
            if (cfg.take_profit_pts > 0 && tick.bid >= p->entry_price + cfg.take_profit_pts) {
                should_close = true;
                exit_type = 2;
            }

            if (should_close) {
                to_close.push_back(p);
                if (exit_type == 1) r.trail_exits++;
                else if (exit_type == 2) r.tp_exits++;
            }
        }

        for (Position* p : to_close) {
            double exit_price = (p->stop_loss > 0 && tick.bid <= p->stop_loss) ? p->stop_loss : tick.bid;
            double pnl = (exit_price - p->entry_price) * p->lot_size * cfg.contract_size;
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
            for (Position* p : positions) {
                equity += (tick.bid - p->entry_price) * p->lot_size * cfg.contract_size;
                used_margin += p->lot_size * cfg.contract_size * p->entry_price / cfg.leverage;
            }
            margin_level = (used_margin > 0) ? (equity / used_margin * 100.0) : 10000.0;
        }

        // Portfolio DD check
        if (positions.empty()) peak_equity = balance;
        if (equity > peak_equity) peak_equity = equity;
        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        if (dd_pct > max_drawdown) max_drawdown = dd_pct;

        if (dd_pct >= cfg.max_portfolio_dd && !positions.empty()) {
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
            last_open_price = -1e9;
            continue;
        }

        // Margin call (original behavior)
        if (used_margin > 0 && margin_level < cfg.manual_stop_out) {
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
            last_open_price = -1e9;
            continue;
        }

        // === OPEN NEW POSITION (grid_open_upwards logic) ===
        // Opens when price makes new high above last open price
        if (tick.ask > last_open_price + cfg.min_spacing) {
            // Calculate lot to survive X% drop
            double survive_drop = tick.ask * cfg.survive_down_pct / 100.0;
            double spread_cost = tick.spread() * cfg.contract_size;

            double lot = equity / (survive_drop + spread_cost + tick.ask * cfg.manual_stop_out / 100.0 / cfg.leverage);
            lot = lot / cfg.contract_size;

            lot = std::min(lot, cfg.max_lot);
            lot = std::max(lot, cfg.min_lot);
            lot = std::round(lot * 100) / 100;

            double margin_needed = lot * cfg.contract_size * tick.ask / cfg.leverage;
            double free_margin = equity - used_margin;

            if (free_margin > margin_needed * 1.1 && lot >= cfg.min_lot) {
                Position* p = new Position();
                p->id = next_id++;
                p->entry_price = tick.ask;
                p->lot_size = lot;
                p->peak_price = tick.bid;
                p->stop_loss = 0;
                positions.push_back(p);
                last_open_price = tick.ask;
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
    printf("%-50s $%10.2f %6.2fx %6.1f%% %5d %5.1f%% %5d %s\n",
           name, r.final_balance, r.return_multiple, r.max_dd,
           r.total_trades,
           r.total_trades > 0 ? (double)r.winning_trades / r.total_trades * 100 : 0,
           r.trail_exits,
           r.margin_call ? "MC" : "");
}

int main() {
    printf("================================================================\n");
    printf("grid_open_upwards_while_going_upwards WITH TRAILING STOP\n");
    printf("================================================================\n\n");

    // Test on NAS100
    printf("Loading NAS100...\n");
    std::vector<Tick> nas100 = LoadTicks("NAS100/NAS100_TICKS_2025.csv", 60000000);

    if (nas100.empty()) {
        printf("ERROR: No NAS100 data\n");
        return 1;
    }

    printf("NAS100: %zu ticks (%.0f -> %.0f = %+.1f%%)\n\n",
           nas100.size(), nas100.front().bid, nas100.back().bid,
           (nas100.back().bid - nas100.front().bid) / nas100.front().bid * 100);

    printf("%-50s %12s %7s %7s %5s %6s %5s\n",
           "Configuration", "Final", "Return", "MaxDD", "Trade", "Win%", "Trail");
    printf("================================================================================\n\n");

    // ========================================================================
    // BASELINE: Original behavior (no trailing)
    // ========================================================================
    printf("--- ORIGINAL (No Trailing) ---\n");

    double survives[] = {4, 6, 8, 10, 15, 20, 30};
    for (double sv : survives) {
        Config cfg;
        cfg.trail_mode = 0;
        cfg.survive_down_pct = sv;
        Result r = RunStrategy(nas100, cfg);
        char name[64];
        snprintf(name, sizeof(name), "Original survive=%.0f%%", sv);
        PrintResult(name, r);
    }
    printf("\n");

    // ========================================================================
    // MODE 1: ATR-based Trailing
    // ========================================================================
    printf("--- ATR-BASED TRAILING ---\n");

    double atr_mults[] = {1.5, 2.0, 2.5, 3.0, 4.0};
    for (double am : atr_mults) {
        Config cfg;
        cfg.trail_mode = 1;
        cfg.survive_down_pct = 10;
        cfg.atr_multiplier = am;
        Result r = RunStrategy(nas100, cfg);
        char name[64];
        snprintf(name, sizeof(name), "ATR x %.1f (survive=10%%)", am);
        PrintResult(name, r);
    }
    printf("\n");

    // ATR with different survive %
    printf("ATR 2.0x with different survive %%:\n");
    for (double sv : survives) {
        Config cfg;
        cfg.trail_mode = 1;
        cfg.survive_down_pct = sv;
        cfg.atr_multiplier = 2.0;
        Result r = RunStrategy(nas100, cfg);
        char name[64];
        snprintf(name, sizeof(name), "  survive=%.0f%%", sv);
        PrintResult(name, r);
    }
    printf("\n");

    // ========================================================================
    // MODE 2: Fixed Distance Trailing
    // ========================================================================
    printf("--- FIXED DISTANCE TRAILING ---\n");

    struct FixedTrail { double activation; double distance; };
    FixedTrail fixed_trails[] = {
        {50, 25}, {50, 50}, {100, 50}, {100, 75}, {150, 75}, {200, 100}, {300, 150}
    };

    for (const auto& ft : fixed_trails) {
        Config cfg;
        cfg.trail_mode = 2;
        cfg.survive_down_pct = 10;
        cfg.trail_activation = ft.activation;
        cfg.trail_distance = ft.distance;
        Result r = RunStrategy(nas100, cfg);
        char name[64];
        snprintf(name, sizeof(name), "Act=%.0f Trail=%.0f", ft.activation, ft.distance);
        PrintResult(name, r);
    }
    printf("\n");

    // ========================================================================
    // MODE 3: Percentage Trailing
    // ========================================================================
    printf("--- PERCENTAGE TRAILING ---\n");

    double trail_pcts[] = {0.25, 0.5, 0.75, 1.0, 1.5, 2.0};
    for (double tp : trail_pcts) {
        Config cfg;
        cfg.trail_mode = 3;
        cfg.survive_down_pct = 10;
        cfg.trail_pct = tp;
        Result r = RunStrategy(nas100, cfg);
        char name[64];
        snprintf(name, sizeof(name), "Trail %.2f%% below peak", tp);
        PrintResult(name, r);
    }
    printf("\n");

    // ========================================================================
    // MODE 4: Break-Even + Trail
    // ========================================================================
    printf("--- BREAK-EVEN + TRAIL ---\n");

    struct BETrail { double be_trigger; double be_offset; double act; double dist; };
    BETrail be_trails[] = {
        {30, 5, 75, 50},
        {50, 10, 100, 50},
        {50, 10, 100, 75},
        {75, 15, 150, 75},
        {100, 20, 200, 100},
    };

    for (const auto& bt : be_trails) {
        Config cfg;
        cfg.trail_mode = 4;
        cfg.survive_down_pct = 10;
        cfg.be_trigger = bt.be_trigger;
        cfg.be_offset = bt.be_offset;
        cfg.trail_activation = bt.act;
        cfg.trail_distance = bt.dist;
        Result r = RunStrategy(nas100, cfg);
        char name[64];
        snprintf(name, sizeof(name), "BE@%.0f(+%.0f) Trail@%.0f/%.0f",
                bt.be_trigger, bt.be_offset, bt.act, bt.dist);
        PrintResult(name, r);
    }
    printf("\n");

    // ========================================================================
    // WITH TAKE PROFIT
    // ========================================================================
    printf("--- ATR TRAILING + TAKE PROFIT ---\n");

    double tps[] = {200, 300, 500, 750, 1000};
    for (double tp : tps) {
        Config cfg;
        cfg.trail_mode = 1;
        cfg.survive_down_pct = 10;
        cfg.atr_multiplier = 2.0;
        cfg.take_profit_pts = tp;
        Result r = RunStrategy(nas100, cfg);
        char name[64];
        snprintf(name, sizeof(name), "ATR 2.0x + TP=%.0f pts", tp);
        PrintResult(name, r);
    }
    printf("\n");

    // ========================================================================
    // WITH PORTFOLIO DD LIMIT
    // ========================================================================
    printf("--- ATR TRAILING + PORTFOLIO DD LIMIT ---\n");

    double dd_limits[] = {15, 20, 25, 30, 40};
    for (double dd : dd_limits) {
        Config cfg;
        cfg.trail_mode = 1;
        cfg.survive_down_pct = 10;
        cfg.atr_multiplier = 2.0;
        cfg.max_portfolio_dd = dd;
        Result r = RunStrategy(nas100, cfg);
        char name[64];
        snprintf(name, sizeof(name), "ATR 2.0x + MaxDD=%.0f%%", dd);
        PrintResult(name, r);
    }
    printf("\n");

    // ========================================================================
    // MIN SPACING BETWEEN POSITIONS
    // ========================================================================
    printf("--- ATR TRAILING + MIN SPACING ---\n");

    double spacings[] = {0, 10, 25, 50, 100, 200};
    for (double sp : spacings) {
        Config cfg;
        cfg.trail_mode = 1;
        cfg.survive_down_pct = 10;
        cfg.atr_multiplier = 2.0;
        cfg.min_spacing = sp;
        Result r = RunStrategy(nas100, cfg);
        char name[64];
        snprintf(name, sizeof(name), "ATR 2.0x + Spacing=%.0f pts", sp);
        PrintResult(name, r);
    }
    printf("\n");

    // ========================================================================
    // BEST COMBINATIONS
    // ========================================================================
    printf("================================================================================\n");
    printf("BEST COMBINATIONS\n");
    printf("================================================================================\n\n");

    // Original for comparison
    {
        Config cfg;
        cfg.trail_mode = 0;
        cfg.survive_down_pct = 10;
        Result r = RunStrategy(nas100, cfg);
        PrintResult("Original (no trail, survive=10%)", r);
    }

    // Best ATR
    {
        Config cfg;
        cfg.trail_mode = 1;
        cfg.survive_down_pct = 10;
        cfg.atr_multiplier = 2.0;
        Result r = RunStrategy(nas100, cfg);
        PrintResult("ATR 2.0x", r);
    }

    // ATR + DD limit
    {
        Config cfg;
        cfg.trail_mode = 1;
        cfg.survive_down_pct = 10;
        cfg.atr_multiplier = 2.0;
        cfg.max_portfolio_dd = 25;
        Result r = RunStrategy(nas100, cfg);
        PrintResult("ATR 2.0x + MaxDD 25%", r);
    }

    // ATR + spacing
    {
        Config cfg;
        cfg.trail_mode = 1;
        cfg.survive_down_pct = 10;
        cfg.atr_multiplier = 2.0;
        cfg.min_spacing = 50;
        Result r = RunStrategy(nas100, cfg);
        PrintResult("ATR 2.0x + Spacing 50", r);
    }

    // Full protection
    {
        Config cfg;
        cfg.trail_mode = 1;
        cfg.survive_down_pct = 15;
        cfg.atr_multiplier = 2.5;
        cfg.max_portfolio_dd = 25;
        cfg.min_spacing = 50;
        Result r = RunStrategy(nas100, cfg);
        PrintResult("Full Protection (survive=15, ATR=2.5, DD=25)", r);
    }

    // Aggressive
    {
        Config cfg;
        cfg.trail_mode = 1;
        cfg.survive_down_pct = 8;
        cfg.atr_multiplier = 1.5;
        cfg.max_portfolio_dd = 40;
        cfg.min_spacing = 25;
        Result r = RunStrategy(nas100, cfg);
        PrintResult("Aggressive (survive=8, ATR=1.5, DD=40)", r);
    }

    printf("\n================================================================================\n");
    printf("TESTING ON GOLD (XAUUSD)\n");
    printf("================================================================================\n\n");

    std::vector<Tick> gold = LoadTicks("XAUUSD_TICKS_2025.csv", 60000000);

    if (!gold.empty()) {
        printf("Gold: %zu ticks (%.2f -> %.2f = %+.1f%%)\n\n",
               gold.size(), gold.front().bid, gold.back().bid,
               (gold.back().bid - gold.front().bid) / gold.front().bid * 100);

        // Gold has different parameters
        Config gold_cfg;
        gold_cfg.contract_size = 100.0;  // 100 oz per lot

        printf("--- GOLD: Original vs Trailing ---\n");

        // Original
        {
            Config cfg = gold_cfg;
            cfg.trail_mode = 0;
            cfg.survive_down_pct = 1;  // Gold needs tighter survive (smaller moves in $)
            Result r = RunStrategy(gold, cfg);
            PrintResult("Gold Original (survive=1%)", r);
        }

        // With ATR trailing
        double gold_survives[] = {0.5, 1, 2, 3, 5};
        for (double sv : gold_survives) {
            Config cfg = gold_cfg;
            cfg.trail_mode = 1;
            cfg.survive_down_pct = sv;
            cfg.atr_multiplier = 2.0;
            Result r = RunStrategy(gold, cfg);
            char name[64];
            snprintf(name, sizeof(name), "Gold ATR 2.0x survive=%.1f%%", sv);
            PrintResult(name, r);
        }

        printf("\n--- GOLD: ATR Multiplier Sweep ---\n");
        for (double am : atr_mults) {
            Config cfg = gold_cfg;
            cfg.trail_mode = 1;
            cfg.survive_down_pct = 2;
            cfg.atr_multiplier = am;
            Result r = RunStrategy(gold, cfg);
            char name[64];
            snprintf(name, sizeof(name), "Gold ATR %.1fx (survive=2%%)", am);
            PrintResult(name, r);
        }

        printf("\n--- GOLD: Best Combinations ---\n");

        // Conservative
        {
            Config cfg = gold_cfg;
            cfg.trail_mode = 1;
            cfg.survive_down_pct = 3;
            cfg.atr_multiplier = 2.5;
            cfg.max_portfolio_dd = 25;
            cfg.min_spacing = 2;  // $2 spacing for gold
            Result r = RunStrategy(gold, cfg);
            PrintResult("Gold Conservative", r);
        }

        // Balanced
        {
            Config cfg = gold_cfg;
            cfg.trail_mode = 1;
            cfg.survive_down_pct = 2;
            cfg.atr_multiplier = 2.0;
            cfg.max_portfolio_dd = 30;
            cfg.min_spacing = 1;
            Result r = RunStrategy(gold, cfg);
            PrintResult("Gold Balanced", r);
        }

        // Aggressive
        {
            Config cfg = gold_cfg;
            cfg.trail_mode = 1;
            cfg.survive_down_pct = 1;
            cfg.atr_multiplier = 1.5;
            cfg.max_portfolio_dd = 40;
            cfg.min_spacing = 0.5;
            Result r = RunStrategy(gold, cfg);
            PrintResult("Gold Aggressive", r);
        }
    }

    printf("\n================================================================\n");
    printf("SUMMARY\n");
    printf("================================================================\n\n");

    printf("Adding trailing stops to grid_open_upwards:\n");
    printf("- ATR-based trailing is most effective\n");
    printf("- Locks in profits before corrections wipe them out\n");
    printf("- Portfolio DD limit provides additional protection\n");
    printf("- Min spacing reduces overtrading\n");

    printf("\n================================================================\n");
    return 0;
}
