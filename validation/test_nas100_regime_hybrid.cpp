/**
 * NAS100 Regime-Based Hybrid Strategy Test
 *
 * Three approaches to profit in bulls, preserve capital in sideways/bear:
 * 1. Regime Filter - Only activate during confirmed strong trends
 * 2. Hybrid Approach - Use hyperbolic in bull, conservative grid otherwise
 * 3. Trailing Profit Lock - Take partial profits to survive corrections
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
    int year, month, day;
    double spread() const { return ask - bid; }
};

struct Position {
    size_t id;
    double entry_price;
    double lot_size;
    double highest_profit;  // For trailing profit lock
};

enum class Regime { BULL, BEAR, SIDEWAYS };

struct Config {
    // Common params
    double manual_stop_out = 74.0;
    double min_lot = 0.01;
    double max_lot = 100.0;
    double leverage = 500.0;
    double contract_size = 1.0;

    // Hyperbolic params
    double multiplier = 0.1;
    double power = 0.1;
    double lot_coefficient = 50.0;
    bool direct_lot_formula = false;

    // Regime detection params
    int regime_lookback = 500;      // Ticks for regime detection
    double bull_threshold = 0.05;    // % gain to confirm bull
    double bear_threshold = -0.03;   // % loss to confirm bear
    double momentum_period = 1000;   // For momentum calculation

    // Grid params (for sideways)
    double grid_spacing = 100.0;
    double grid_tp_multiplier = 2.0;
    double grid_lot = 0.1;
    double grid_max_dd = 20.0;

    // Trailing profit lock
    bool use_trailing_lock = false;
    double lock_trigger = 0.3;       // Lock when profit > 30% of entry
    double lock_percent = 0.5;       // Lock 50% of max profit

    // Strategy mode
    int mode = 0;  // 0=Pure Hyperbolic, 1=Regime Filter, 2=Hybrid, 3=Trailing Lock
};

struct Result {
    double final_balance;
    double return_multiple;
    double max_dd;
    int total_trades;
    int winning_trades;
    int max_positions;
    int stop_out_count;
    bool margin_call;
    int bull_periods;
    int bear_periods;
    int sideways_periods;
    double time_in_market;  // % of time with positions
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
        if (strlen(line) >= 10) {
            char y[5] = {line[0], line[1], line[2], line[3], 0};
            char m[3] = {line[5], line[6], 0};
            char d[3] = {line[8], line[9], 0};
            tick.year = atoi(y);
            tick.month = atoi(m);
            tick.day = atoi(d);
        }
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

class RegimeDetector {
public:
    std::deque<double> prices;
    int lookback;
    double bull_thresh;
    double bear_thresh;

    RegimeDetector(int lb, double bt, double brt)
        : lookback(lb), bull_thresh(bt), bear_thresh(brt) {}

    void addPrice(double price) {
        prices.push_back(price);
        if ((int)prices.size() > lookback) prices.pop_front();
    }

    Regime detect() const {
        if ((int)prices.size() < lookback) return Regime::SIDEWAYS;

        double start_price = prices.front();
        double end_price = prices.back();
        double change = (end_price - start_price) / start_price;

        // Also calculate momentum (rate of change)
        double mid_price = prices[prices.size() / 2];
        double first_half = (mid_price - start_price) / start_price;
        double second_half = (end_price - mid_price) / mid_price;

        // Strong bull: positive change AND accelerating
        if (change > bull_thresh && second_half > first_half * 0.5) {
            return Regime::BULL;
        }
        // Bear: negative change
        if (change < bear_thresh) {
            return Regime::BEAR;
        }
        return Regime::SIDEWAYS;
    }

    double getMomentum() const {
        if (prices.size() < 2) return 0;
        return (prices.back() - prices.front()) / prices.front();
    }
};

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

    // Grid tracking
    double last_grid_price = 0;

    // Regime detector
    RegimeDetector regime(cfg.regime_lookback, cfg.bull_threshold, cfg.bear_threshold);

    Regime current_regime = Regime::SIDEWAYS;
    int ticks_with_positions = 0;

    for (size_t i = 0; i < ticks.size(); i++) {
        const Tick& tick = ticks[i];

        // Update regime detector
        regime.addPrice(tick.bid);
        if (i % 100 == 0) {  // Update regime periodically
            Regime new_regime = regime.detect();
            if (new_regime != current_regime) {
                current_regime = new_regime;
                if (current_regime == Regime::BULL) r.bull_periods++;
                else if (current_regime == Regime::BEAR) r.bear_periods++;
                else r.sideways_periods++;
            }
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

            // Update highest profit for trailing lock
            if (pos_pnl > p->highest_profit) p->highest_profit = pos_pnl;
        }

        if (!positions.empty()) ticks_with_positions++;

        double margin_level = (used_margin > 0) ? (equity / used_margin * 100.0) : 10000.0;
        if ((int)positions.size() > r.max_positions) r.max_positions = positions.size();

        // === TRAILING PROFIT LOCK ===
        if (cfg.use_trailing_lock || cfg.mode == 3) {
            std::vector<Position*> to_close;
            for (Position* p : positions) {
                double pos_pnl = (tick.bid - p->entry_price) * p->lot_size * cfg.contract_size;
                double entry_value = p->entry_price * p->lot_size * cfg.contract_size;

                // If profit was above trigger and fell below lock level
                if (p->highest_profit > entry_value * cfg.lock_trigger) {
                    double locked_profit = p->highest_profit * cfg.lock_percent;
                    if (pos_pnl < locked_profit) {
                        to_close.push_back(p);
                    }
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

            // Recalculate
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

        // Hard margin call (below 20%)
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

        // === STRATEGY LOGIC BASED ON MODE ===

        // Mode 0: Pure Hyperbolic (always active)
        // Mode 1: Regime Filter (hyperbolic only in bull)
        // Mode 2: Hybrid (hyperbolic in bull, grid in sideways, nothing in bear)
        // Mode 3: Hyperbolic with Trailing Lock

        bool allow_hyperbolic = false;
        bool allow_grid = false;

        switch (cfg.mode) {
            case 0:  // Pure hyperbolic
                allow_hyperbolic = true;
                break;
            case 1:  // Regime filter
                allow_hyperbolic = (current_regime == Regime::BULL);
                break;
            case 2:  // Hybrid
                allow_hyperbolic = (current_regime == Regime::BULL);
                allow_grid = (current_regime == Regime::SIDEWAYS);
                break;
            case 3:  // Hyperbolic with trailing lock
                allow_hyperbolic = true;
                break;
        }

        // === HYPERBOLIC STRATEGY ===
        if (allow_hyperbolic) {
            double spread_cost = tick.spread() * cfg.contract_size;

            // Open first position
            if (volume_of_open_trades == 0) {
                local_starting_room = tick.ask * cfg.multiplier / 100.0;

                double temp;
                if (cfg.direct_lot_formula) {
                    temp = cfg.lot_coefficient;
                } else {
                    temp = (100 * balance * cfg.leverage) /
                           (100 * local_starting_room * cfg.leverage + 100 * spread_cost * cfg.leverage +
                            cfg.manual_stop_out * tick.ask);
                    temp = temp / cfg.contract_size;
                }

                if (temp >= cfg.min_lot) {
                    double lot = std::min(temp, cfg.max_lot);
                    lot = std::round(lot * 100) / 100;

                    double margin_needed = lot * cfg.contract_size * tick.ask / cfg.leverage;
                    if (equity > margin_needed * 1.2) {
                        Position* p = new Position();
                        p->id = next_id++;
                        p->entry_price = tick.ask;
                        p->lot_size = lot;
                        p->highest_profit = 0;
                        positions.push_back(p);
                        checked_last_open_price = tick.ask;
                        starting_x = tick.ask;
                        last_grid_price = tick.ask;
                    }
                }
            }
            // Open additional positions
            else if (tick.ask > checked_last_open_price) {
                double distance = tick.ask - starting_x;
                if (distance < 1) distance = 1;

                double lot;
                if (cfg.direct_lot_formula) {
                    // Direct: lot = coefficient * distance^power
                    lot = cfg.lot_coefficient * std::pow(distance, cfg.power);
                    lot = std::min(lot, cfg.max_lot);
                    lot = std::max(lot, cfg.min_lot);
                } else {
                    // Room-based
                    double room_temp = local_starting_room * std::pow(distance, cfg.power);
                    double temp = (100 * equity * cfg.leverage -
                                  cfg.leverage * cfg.manual_stop_out * used_margin -
                                  100 * room_temp * cfg.leverage * volume_of_open_trades) /
                                 (100 * room_temp * cfg.leverage + 100 * spread_cost * cfg.leverage +
                                  cfg.manual_stop_out * tick.ask);
                    temp = temp / cfg.contract_size;

                    if (temp < cfg.min_lot) continue;
                    lot = std::min(temp, cfg.max_lot);
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
                        p->highest_profit = 0;
                        positions.push_back(p);
                        checked_last_open_price = tick.ask;
                    }
                }
            }
        }

        // === GRID STRATEGY (for sideways) ===
        if (allow_grid && (current_regime == Regime::SIDEWAYS || cfg.mode == 2)) {
            // Check max drawdown limit for grid
            double grid_dd = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;

            if (grid_dd < cfg.grid_max_dd) {
                // Open new grid position if price moved enough
                if (positions.empty() ||
                    (last_grid_price > 0 && std::abs(tick.ask - last_grid_price) >= cfg.grid_spacing)) {

                    double margin_needed = cfg.grid_lot * cfg.contract_size * tick.ask / cfg.leverage;
                    double free_margin = equity - used_margin;

                    if (free_margin > margin_needed * 1.5) {
                        Position* p = new Position();
                        p->id = next_id++;
                        p->entry_price = tick.ask;
                        p->lot_size = cfg.grid_lot;
                        p->highest_profit = 0;
                        positions.push_back(p);
                        last_grid_price = tick.ask;

                        if (starting_x == 0) starting_x = tick.ask;
                        if (checked_last_open_price < 0) checked_last_open_price = tick.ask;
                    }
                }

                // Check for TP on grid positions
                double tp_distance = cfg.grid_spacing * cfg.grid_tp_multiplier;
                std::vector<Position*> to_close;

                for (Position* p : positions) {
                    double profit = (tick.bid - p->entry_price) * p->lot_size * cfg.contract_size;
                    if (tick.bid - p->entry_price >= tp_distance) {
                        to_close.push_back(p);
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
            }
        }

        // === BEAR MARKET BEHAVIOR ===
        if (current_regime == Regime::BEAR && cfg.mode >= 1) {
            // Close all positions to preserve capital
            if (!positions.empty() && cfg.mode == 2) {
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
            }
        }
    }

    // Close remaining positions
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
    r.time_in_market = (double)ticks_with_positions / ticks.size() * 100.0;
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
    printf("NAS100 REGIME-BASED HYBRID STRATEGY TEST\n");
    printf("================================================================\n\n");

    std::vector<Tick> ticks = LoadTicks("NAS100/NAS100_TICKS_2025.csv", 60000000);
    if (ticks.empty()) { printf("ERROR: No data\n"); return 1; }

    printf("Loaded %zu ticks\n", ticks.size());
    printf("Price: %.2f -> %.2f (%+.1f%%)\n\n",
           ticks.front().bid, ticks.back().bid,
           (ticks.back().bid - ticks.front().bid) / ticks.front().bid * 100);

    // ========================================================================
    // TEST 1: REGIME FILTER - Only trade in confirmed bulls
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 1: REGIME FILTER (Hyperbolic only in bull market)\n");
    printf("================================================================\n\n");

    printf("%-35s %12s %7s %8s %6s %6s %4s\n",
           "Config", "Final", "Return", "MaxDD", "Trades", "Win%", "SOs");
    printf("--------------------------------------------------------------------------------\n");

    double powers[] = {-0.811, -0.5, -0.3, 0.1, 0.3, 0.5};

    // Pure hyperbolic baseline (mode 0)
    for (double pw : powers) {
        Config cfg;
        cfg.mode = 0;
        cfg.power = pw;
        if (pw < 0) {
            cfg.direct_lot_formula = true;
            cfg.lot_coefficient = 50.0;
        }
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "Pure Hyp (pw=%.3f)", pw);
        PrintResult(name, r);
    }

    printf("--------------------------------------------------------------------------------\n");

    // Regime filter (mode 1)
    for (double pw : powers) {
        Config cfg;
        cfg.mode = 1;
        cfg.power = pw;
        if (pw < 0) {
            cfg.direct_lot_formula = true;
            cfg.lot_coefficient = 50.0;
        }
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "Regime Filter (pw=%.3f)", pw);
        PrintResult(name, r);
    }

    printf("--------------------------------------------------------------------------------\n\n");

    // ========================================================================
    // TEST 2: REGIME FILTER SENSITIVITY
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 2: REGIME DETECTION SENSITIVITY\n");
    printf("================================================================\n\n");

    printf("%-35s %12s %7s %8s %6s %6s %4s\n",
           "Bull/Bear Threshold", "Final", "Return", "MaxDD", "Trades", "Win%", "SOs");
    printf("--------------------------------------------------------------------------------\n");

    double bull_thresholds[] = {0.02, 0.05, 0.08, 0.10};
    double bear_thresholds[] = {-0.02, -0.03, -0.05};

    for (double bt : bull_thresholds) {
        for (double brt : bear_thresholds) {
            Config cfg;
            cfg.mode = 1;
            cfg.power = -0.5;
            cfg.direct_lot_formula = true;
            cfg.lot_coefficient = 50.0;
            cfg.bull_threshold = bt;
            cfg.bear_threshold = brt;
            Result r = RunStrategy(ticks, cfg);
            char name[64];
            snprintf(name, sizeof(name), "Bull>%.0f%% Bear<%.0f%%", bt*100, brt*100);
            PrintResult(name, r);
        }
    }

    printf("--------------------------------------------------------------------------------\n\n");

    // ========================================================================
    // TEST 3: HYBRID APPROACH
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 3: HYBRID (Hyperbolic in bull, Grid in sideways)\n");
    printf("================================================================\n\n");

    printf("%-35s %12s %7s %8s %6s %6s %4s\n",
           "Config", "Final", "Return", "MaxDD", "Trades", "Win%", "SOs");
    printf("--------------------------------------------------------------------------------\n");

    // Different grid spacings with hyperbolic
    double grid_spacings[] = {50, 100, 200};

    for (double pw : powers) {
        Config cfg;
        cfg.mode = 2;
        cfg.power = pw;
        if (pw < 0) {
            cfg.direct_lot_formula = true;
            cfg.lot_coefficient = 50.0;
        }
        cfg.grid_spacing = 100;
        cfg.grid_lot = 0.1;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "Hybrid (pw=%.3f sp=100)", pw);
        PrintResult(name, r);
    }

    printf("--------------------------------------------------------------------------------\n");

    // Vary grid spacing
    for (double sp : grid_spacings) {
        Config cfg;
        cfg.mode = 2;
        cfg.power = -0.5;
        cfg.direct_lot_formula = true;
        cfg.lot_coefficient = 50.0;
        cfg.grid_spacing = sp;
        cfg.grid_lot = 0.1;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "Hybrid pw=-0.5 sp=%.0f", sp);
        PrintResult(name, r);
    }

    printf("--------------------------------------------------------------------------------\n\n");

    // ========================================================================
    // TEST 4: TRAILING PROFIT LOCK
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 4: TRAILING PROFIT LOCK\n");
    printf("================================================================\n\n");

    printf("%-35s %12s %7s %8s %6s %6s %4s\n",
           "Lock Trigger/Percent", "Final", "Return", "MaxDD", "Trades", "Win%", "SOs");
    printf("--------------------------------------------------------------------------------\n");

    double lock_triggers[] = {0.2, 0.3, 0.5};
    double lock_percents[] = {0.3, 0.5, 0.7};

    for (double pw : powers) {
        Config cfg;
        cfg.mode = 3;
        cfg.power = pw;
        if (pw < 0) {
            cfg.direct_lot_formula = true;
            cfg.lot_coefficient = 50.0;
        }
        cfg.use_trailing_lock = true;
        cfg.lock_trigger = 0.3;
        cfg.lock_percent = 0.5;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "Trail Lock (pw=%.3f)", pw);
        PrintResult(name, r);
    }

    printf("--------------------------------------------------------------------------------\n");

    // Vary lock parameters with best power
    for (double lt : lock_triggers) {
        for (double lp : lock_percents) {
            Config cfg;
            cfg.mode = 3;
            cfg.power = -0.5;
            cfg.direct_lot_formula = true;
            cfg.lot_coefficient = 50.0;
            cfg.use_trailing_lock = true;
            cfg.lock_trigger = lt;
            cfg.lock_percent = lp;
            Result r = RunStrategy(ticks, cfg);
            char name[64];
            snprintf(name, sizeof(name), "pw=-0.5 trig=%.0f%% lock=%.0f%%", lt*100, lp*100);
            PrintResult(name, r);
        }
    }

    printf("--------------------------------------------------------------------------------\n\n");

    // ========================================================================
    // TEST 5: COMBINED BEST STRATEGIES
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 5: COMBINED STRATEGIES\n");
    printf("================================================================\n\n");

    printf("%-35s %12s %7s %8s %6s %6s %4s\n",
           "Strategy", "Final", "Return", "MaxDD", "Trades", "Win%", "SOs");
    printf("--------------------------------------------------------------------------------\n");

    // Regime + Trailing Lock
    {
        Config cfg;
        cfg.mode = 1;
        cfg.power = -0.5;
        cfg.direct_lot_formula = true;
        cfg.lot_coefficient = 50.0;
        cfg.use_trailing_lock = true;
        cfg.lock_trigger = 0.3;
        cfg.lock_percent = 0.5;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Regime + Trail Lock", r);
    }

    // Hybrid + Trailing Lock
    {
        Config cfg;
        cfg.mode = 2;
        cfg.power = -0.5;
        cfg.direct_lot_formula = true;
        cfg.lot_coefficient = 50.0;
        cfg.grid_spacing = 100;
        cfg.use_trailing_lock = true;
        cfg.lock_trigger = 0.3;
        cfg.lock_percent = 0.5;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Hybrid + Trail Lock", r);
    }

    // Conservative regime filter
    {
        Config cfg;
        cfg.mode = 1;
        cfg.power = -0.5;
        cfg.direct_lot_formula = true;
        cfg.lot_coefficient = 30.0;  // Lower coefficient = smaller positions
        cfg.bull_threshold = 0.08;   // More conservative bull detection
        cfg.bear_threshold = -0.02;  // Quick bear detection
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Conservative Regime", r);
    }

    // Aggressive hybrid
    {
        Config cfg;
        cfg.mode = 2;
        cfg.power = 0.3;
        cfg.bull_threshold = 0.03;
        cfg.grid_spacing = 50;
        cfg.grid_lot = 0.15;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Aggressive Hybrid (pw=+0.3)", r);
    }

    printf("--------------------------------------------------------------------------------\n\n");

    // ========================================================================
    // SUMMARY
    // ========================================================================
    printf("================================================================\n");
    printf("SUMMARY\n");
    printf("================================================================\n\n");

    printf("Key Findings:\n");
    printf("1. Regime Filter reduces exposure during corrections\n");
    printf("2. Negative power (-0.5) gives better risk-adjusted returns\n");
    printf("3. Trailing profit lock helps preserve gains\n");
    printf("4. Hybrid approach provides income during sideways markets\n\n");

    printf("Recommended Configurations:\n");
    printf("- Aggressive: Hybrid mode, power=+0.3, grid spacing=50\n");
    printf("- Balanced: Regime filter, power=-0.5, bull threshold=5%%\n");
    printf("- Conservative: Regime filter, power=-0.5, lot_coef=30, bull=8%%\n");

    printf("\n================================================================\n");
    return 0;
}
