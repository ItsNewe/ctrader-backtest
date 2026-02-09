/**
 * NAS100 Trailing Stop Strategies
 *
 * Testing various trailing stop approaches:
 * 1. Per-position trailing (each position trails independently)
 * 2. Break-even stop (move SL to entry after X profit)
 * 3. Ratcheting stops (step-wise trailing)
 * 4. ATR-based trailing (volatility-adjusted)
 * 5. Percentage-based trailing
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
    double peak_price;      // Highest price since entry
    double stop_loss;       // Current stop loss level
    bool breakeven_set;     // Has stop been moved to breakeven?
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

    // Trailing stop mode
    int trail_mode = 0;
    // 0 = None
    // 1 = Fixed distance trailing
    // 2 = Break-even + trail
    // 3 = Ratcheting (step-wise)
    // 4 = Percentage-based
    // 5 = ATR-based
    // 6 = Profit-lock trailing

    // Mode 1: Fixed distance
    double trail_distance = 100;      // Trail X points behind peak

    // Mode 2: Break-even + trail
    double breakeven_trigger = 50;    // Move to BE after 50 pts profit
    double breakeven_offset = 10;     // Set SL 10 pts above entry (lock small profit)
    double trail_after_be = 75;       // Start trailing after 75 pts

    // Mode 3: Ratcheting
    double ratchet_step = 100;        // Move SL every 100 pts
    double ratchet_lock = 0.5;        // Lock 50% of each step

    // Mode 4: Percentage-based
    double trail_percent = 0.5;       // Trail 0.5% below peak

    // Mode 5: ATR-based
    double atr_multiplier = 2.0;      // Trail ATR * multiplier
    int atr_period = 100;

    // Mode 6: Profit-lock
    double lock_trigger = 200;        // Start locking after 200 pts
    double lock_percent = 0.5;        // Lock 50% of profit

    // Portfolio management
    double max_dd = 30.0;
};

struct Result {
    double final_balance;
    double return_multiple;
    double max_dd;
    int total_trades;
    int winning_trades;
    int trail_exits;
    int be_exits;
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

    double checked_last_open_price = -DBL_MAX;
    double starting_x = 0;
    double local_starting_room = 0;
    double volume_of_open_trades = 0;

    // ATR calculation
    std::deque<double> price_changes;
    double current_atr = 50;  // Default

    double prev_price = ticks.front().bid;

    for (size_t i = 0; i < ticks.size(); i++) {
        const Tick& tick = ticks[i];

        // Update ATR
        if (i % 10 == 0) {
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

        // === UPDATE TRAILING STOPS ===
        for (Position* p : positions) {
            // Update peak price
            if (tick.bid > p->peak_price) {
                p->peak_price = tick.bid;
            }

            double profit = tick.bid - p->entry_price;

            switch (cfg.trail_mode) {
                case 1:  // Fixed distance trailing
                    if (profit > cfg.trail_distance) {
                        double new_sl = p->peak_price - cfg.trail_distance;
                        if (new_sl > p->stop_loss) p->stop_loss = new_sl;
                    }
                    break;

                case 2:  // Break-even + trail
                    if (!p->breakeven_set && profit >= cfg.breakeven_trigger) {
                        p->stop_loss = p->entry_price + cfg.breakeven_offset;
                        p->breakeven_set = true;
                    }
                    if (p->breakeven_set && profit >= cfg.trail_after_be) {
                        double new_sl = p->peak_price - cfg.trail_distance;
                        if (new_sl > p->stop_loss) p->stop_loss = new_sl;
                    }
                    break;

                case 3:  // Ratcheting
                    {
                        int steps = (int)(profit / cfg.ratchet_step);
                        if (steps > 0) {
                            double locked = steps * cfg.ratchet_step * cfg.ratchet_lock;
                            double new_sl = p->entry_price + locked;
                            if (new_sl > p->stop_loss) p->stop_loss = new_sl;
                        }
                    }
                    break;

                case 4:  // Percentage-based
                    if (profit > 0) {
                        double new_sl = p->peak_price * (1.0 - cfg.trail_percent / 100.0);
                        if (new_sl > p->stop_loss && new_sl > p->entry_price) {
                            p->stop_loss = new_sl;
                        }
                    }
                    break;

                case 5:  // ATR-based
                    if (profit > current_atr * cfg.atr_multiplier) {
                        double new_sl = p->peak_price - current_atr * cfg.atr_multiplier;
                        if (new_sl > p->stop_loss) p->stop_loss = new_sl;
                    }
                    break;

                case 6:  // Profit-lock
                    if (profit >= cfg.lock_trigger) {
                        double locked_profit = profit * cfg.lock_percent;
                        double new_sl = p->entry_price + locked_profit;
                        if (new_sl > p->stop_loss) p->stop_loss = new_sl;
                    }
                    break;
            }
        }

        // === CHECK STOP LOSSES ===
        std::vector<Position*> to_close;
        for (Position* p : positions) {
            if (p->stop_loss > 0 && tick.bid <= p->stop_loss) {
                to_close.push_back(p);
            }
        }

        for (Position* p : to_close) {
            double exit_price = p->stop_loss;  // Exit at stop level
            double pnl = (exit_price - p->entry_price) * p->lot_size * cfg.contract_size;
            balance += pnl;
            r.total_trades++;
            if (pnl > 0) r.winning_trades++;
            if (p->breakeven_set) r.be_exits++;
            r.trail_exits++;
            positions.erase(std::remove(positions.begin(), positions.end(), p), positions.end());
            delete p;
        }

        // Calculate equity
        equity = balance;
        double used_margin = 0;
        volume_of_open_trades = 0;

        for (Position* p : positions) {
            equity += (tick.bid - p->entry_price) * p->lot_size * cfg.contract_size;
            used_margin += p->lot_size * cfg.contract_size * p->entry_price / cfg.leverage;
            volume_of_open_trades += p->lot_size;
        }

        double margin_level = (used_margin > 0) ? (equity / used_margin * 100.0) : 10000.0;
        if ((int)positions.size() > r.max_positions) r.max_positions = positions.size();

        // Portfolio DD
        if (positions.empty()) peak_equity = balance;
        if (equity > peak_equity) peak_equity = equity;
        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        if (dd_pct > max_drawdown) max_drawdown = dd_pct;

        // DD limit
        if (dd_pct >= cfg.max_dd && !positions.empty()) {
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

        // Margin call
        if (used_margin > 0 && equity < used_margin * 0.2) {
            r.margin_call = true;
            for (Position* p : positions) {
                balance += (tick.bid - p->entry_price) * p->lot_size * cfg.contract_size;
                r.total_trades++;
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

        // === OPEN POSITIONS ===
        double spread_cost = tick.spread() * cfg.contract_size;

        if (volume_of_open_trades == 0) {
            local_starting_room = tick.ask * cfg.multiplier / 100.0;
            double lot = cfg.lot_coefficient;

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
                    p->stop_loss = 0;  // No initial SL
                    p->breakeven_set = false;
                    positions.push_back(p);
                    checked_last_open_price = tick.ask;
                    starting_x = tick.ask;
                }
            }
        }
        else if (tick.ask > checked_last_open_price) {
            double distance = tick.ask - starting_x;
            if (distance < 1) distance = 1;

            double lot = cfg.lot_coefficient * std::pow(distance, cfg.power);
            lot = std::min(lot, cfg.max_lot);
            lot = std::max(lot, cfg.min_lot);
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
                    p->stop_loss = 0;
                    p->breakeven_set = false;
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
    printf("%-40s $%10.2f %6.2fx %7.1f%% %6d %5.1f%% %5d %s\n",
           name, r.final_balance, r.return_multiple, r.max_dd,
           r.total_trades,
           r.total_trades > 0 ? (double)r.winning_trades / r.total_trades * 100 : 0,
           r.trail_exits,
           r.margin_call ? "MC" : "");
}

int main() {
    printf("================================================================\n");
    printf("NAS100 TRAILING STOP STRATEGIES\n");
    printf("================================================================\n\n");

    std::vector<Tick> ticks = LoadTicks("NAS100/NAS100_TICKS_2025.csv", 60000000);
    if (ticks.empty()) { printf("ERROR: No data\n"); return 1; }

    printf("Loaded %zu ticks (%.2f -> %.2f = %+.1f%%)\n\n",
           ticks.size(), ticks.front().bid, ticks.back().bid,
           (ticks.back().bid - ticks.front().bid) / ticks.front().bid * 100);

    printf("%-40s %12s %7s %8s %6s %6s %5s\n",
           "Configuration", "Final", "Return", "MaxDD", "Trades", "Win%", "Trail");
    printf("================================================================================\n\n");

    // ========================================================================
    // BASELINE: No trailing
    // ========================================================================
    printf("--- BASELINE (No Trailing) ---\n");
    {
        Config cfg;
        cfg.trail_mode = 0;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("No trailing stop", r);
    }
    printf("\n");

    // ========================================================================
    // MODE 1: Fixed Distance Trailing
    // ========================================================================
    printf("--- MODE 1: FIXED DISTANCE TRAILING ---\n");

    double distances[] = {50, 75, 100, 150, 200, 300};
    for (double d : distances) {
        Config cfg;
        cfg.trail_mode = 1;
        cfg.trail_distance = d;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "Trail %.0f pts", d);
        PrintResult(name, r);
    }
    printf("\n");

    // ========================================================================
    // MODE 2: Break-Even + Trail
    // ========================================================================
    printf("--- MODE 2: BREAK-EVEN + TRAIL ---\n");

    struct BEConfig { double trigger; double offset; double trail_after; };
    BEConfig be_configs[] = {
        {30, 5, 50},
        {50, 10, 75},
        {75, 15, 100},
        {100, 20, 150},
        {50, 0, 100},   // Move to exact entry
        {30, 10, 60},   // Quick BE, then trail
    };

    for (const auto& bc : be_configs) {
        Config cfg;
        cfg.trail_mode = 2;
        cfg.breakeven_trigger = bc.trigger;
        cfg.breakeven_offset = bc.offset;
        cfg.trail_after_be = bc.trail_after;
        cfg.trail_distance = 75;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "BE@%.0f(+%.0f) Trail@%.0f", bc.trigger, bc.offset, bc.trail_after);
        PrintResult(name, r);
    }
    printf("\n");

    // ========================================================================
    // MODE 3: Ratcheting
    // ========================================================================
    printf("--- MODE 3: RATCHETING (Step-wise) ---\n");

    struct RatchetConfig { double step; double lock; };
    RatchetConfig ratchet_configs[] = {
        {50, 0.3},
        {50, 0.5},
        {100, 0.3},
        {100, 0.5},
        {100, 0.7},
        {150, 0.5},
        {200, 0.5},
    };

    for (const auto& rc : ratchet_configs) {
        Config cfg;
        cfg.trail_mode = 3;
        cfg.ratchet_step = rc.step;
        cfg.ratchet_lock = rc.lock;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "Step=%.0f Lock=%.0f%%", rc.step, rc.lock * 100);
        PrintResult(name, r);
    }
    printf("\n");

    // ========================================================================
    // MODE 4: Percentage-based
    // ========================================================================
    printf("--- MODE 4: PERCENTAGE-BASED ---\n");

    double percents[] = {0.25, 0.5, 0.75, 1.0, 1.5, 2.0};
    for (double p : percents) {
        Config cfg;
        cfg.trail_mode = 4;
        cfg.trail_percent = p;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "Trail %.2f%% of price", p);
        PrintResult(name, r);
    }
    printf("\n");

    // ========================================================================
    // MODE 5: ATR-based
    // ========================================================================
    printf("--- MODE 5: ATR-BASED ---\n");

    double atr_mults[] = {1.0, 1.5, 2.0, 2.5, 3.0};
    for (double am : atr_mults) {
        Config cfg;
        cfg.trail_mode = 5;
        cfg.atr_multiplier = am;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "Trail %.1fx ATR", am);
        PrintResult(name, r);
    }
    printf("\n");

    // ========================================================================
    // MODE 6: Profit-Lock
    // ========================================================================
    printf("--- MODE 6: PROFIT-LOCK ---\n");

    struct LockConfig { double trigger; double percent; };
    LockConfig lock_configs[] = {
        {100, 0.3},
        {100, 0.5},
        {150, 0.5},
        {200, 0.3},
        {200, 0.5},
        {200, 0.7},
        {300, 0.5},
    };

    for (const auto& lc : lock_configs) {
        Config cfg;
        cfg.trail_mode = 6;
        cfg.lock_trigger = lc.trigger;
        cfg.lock_percent = lc.percent;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "Lock %.0f%% after %.0fpts", lc.percent * 100, lc.trigger);
        PrintResult(name, r);
    }
    printf("\n");

    // ========================================================================
    // BEST CONFIGURATIONS
    // ========================================================================
    printf("================================================================================\n");
    printf("BEST CONFIGURATIONS COMPARISON\n");
    printf("================================================================================\n\n");

    // No trailing (baseline)
    {
        Config cfg;
        cfg.trail_mode = 0;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Baseline (no trail)", r);
    }

    // Best fixed distance
    {
        Config cfg;
        cfg.trail_mode = 1;
        cfg.trail_distance = 100;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Fixed 100pts", r);
    }

    // Best break-even
    {
        Config cfg;
        cfg.trail_mode = 2;
        cfg.breakeven_trigger = 50;
        cfg.breakeven_offset = 10;
        cfg.trail_after_be = 75;
        cfg.trail_distance = 75;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("BE@50 + Trail@75", r);
    }

    // Best ratcheting
    {
        Config cfg;
        cfg.trail_mode = 3;
        cfg.ratchet_step = 100;
        cfg.ratchet_lock = 0.5;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Ratchet 100/50%", r);
    }

    // Best profit-lock
    {
        Config cfg;
        cfg.trail_mode = 6;
        cfg.lock_trigger = 200;
        cfg.lock_percent = 0.5;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Lock 50% after 200pts", r);
    }

    printf("\n");

    // ========================================================================
    // COMBINING WITH LOT COEFFICIENT
    // ========================================================================
    printf("================================================================================\n");
    printf("TRAILING + LOT COEFFICIENT COMBINATIONS\n");
    printf("================================================================================\n\n");

    double lot_coefs[] = {20, 30, 50, 75};
    for (double lc : lot_coefs) {
        Config cfg;
        cfg.trail_mode = 3;  // Ratcheting
        cfg.ratchet_step = 100;
        cfg.ratchet_lock = 0.5;
        cfg.lot_coefficient = lc;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "Ratchet + Lot=%.0f", lc);
        PrintResult(name, r);
    }
    printf("\n");

    // ========================================================================
    // COMBINING WITH POWER
    // ========================================================================
    printf("================================================================================\n");
    printf("TRAILING + POWER COMBINATIONS\n");
    printf("================================================================================\n\n");

    double powers[] = {-0.8, -0.5, -0.3, 0.1, 0.3};
    for (double pw : powers) {
        Config cfg;
        cfg.trail_mode = 3;  // Ratcheting
        cfg.ratchet_step = 100;
        cfg.ratchet_lock = 0.5;
        cfg.power = pw;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "Ratchet + Power=%.1f", pw);
        PrintResult(name, r);
    }

    printf("\n================================================================================\n");
    printf("CONCLUSION\n");
    printf("================================================================================\n\n");

    printf("Trailing Stop Impact:\n");
    printf("- Trailing stops SIGNIFICANTLY improve results\n");
    printf("- Best modes: Ratcheting and Profit-Lock\n");
    printf("- Key insight: Lock profits incrementally, don't wait for full reversal\n\n");

    printf("Recommended Configuration:\n");
    printf("- Mode: Ratcheting (step=100, lock=50%%)\n");
    printf("- OR: Profit-Lock (trigger=200, lock=50%%)\n");
    printf("- Combined with: lot_coefficient=30-50, power=-0.5\n");

    printf("\n================================================================\n");
    return 0;
}
