/**
 * NAS100 Regime-Based Strategy V2
 *
 * Fixed issues:
 * - Longer lookback for regime detection (100k ticks ~ 1-2 days)
 * - Better trailing lock triggers based on price distance, not %
 * - More appropriate thresholds
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
    double peak_price;  // Highest price seen since entry
};

enum class Regime { BULL, BEAR, SIDEWAYS };

struct Config {
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

    // Regime detection (longer lookback for daily trends)
    int regime_lookback = 500000;    // ~2-3 days of ticks
    double bull_threshold = 0.02;    // 2% gain = bull
    double bear_threshold = -0.015;  // 1.5% loss = bear

    // Grid params
    double grid_spacing = 100.0;
    double grid_tp_multiplier = 2.0;
    double grid_lot = 0.1;
    double grid_max_dd = 20.0;

    // Trailing lock based on PRICE distance
    bool use_trailing_lock = false;
    double lock_trigger_pts = 200;   // Lock when price moved 200 pts above entry
    double lock_pullback_pts = 100;  // Close if price pulls back 100 pts from peak

    int mode = 0;
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
    int regime_changes;
    double time_in_bull;
    double time_in_bear;
    double time_in_sideways;
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
    int sample_rate;  // Only sample every N ticks
    int tick_count;

    RegimeDetector(int lb, double bt, double brt, int sr = 1000)
        : lookback(lb / sr), bull_thresh(bt), bear_thresh(brt), sample_rate(sr), tick_count(0) {}

    void addPrice(double price) {
        tick_count++;
        if (tick_count % sample_rate != 0) return;

        prices.push_back(price);
        if ((int)prices.size() > lookback) prices.pop_front();
    }

    Regime detect() const {
        if ((int)prices.size() < lookback / 2) return Regime::SIDEWAYS;

        double start_price = prices.front();
        double end_price = prices.back();
        double change = (end_price - start_price) / start_price;

        // Calculate trend strength using linear regression slope
        double sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0;
        int n = prices.size();
        for (int i = 0; i < n; i++) {
            sum_x += i;
            sum_y += prices[i];
            sum_xy += i * prices[i];
            sum_x2 += i * i;
        }
        double slope = (n * sum_xy - sum_x * sum_y) / (n * sum_x2 - sum_x * sum_x);
        double normalized_slope = slope / prices.front() * n;  // Normalize by price and period

        // Strong bull: positive change AND positive slope
        if (change > bull_thresh && normalized_slope > 0.01) {
            return Regime::BULL;
        }
        // Bear: negative change OR strong negative slope
        if (change < bear_thresh || normalized_slope < -0.015) {
            return Regime::BEAR;
        }
        return Regime::SIDEWAYS;
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
    double last_grid_price = 0;

    RegimeDetector regime(cfg.regime_lookback, cfg.bull_threshold, cfg.bear_threshold);
    Regime current_regime = Regime::SIDEWAYS;
    Regime prev_regime = Regime::SIDEWAYS;

    size_t bull_ticks = 0, bear_ticks = 0, sideways_ticks = 0;

    for (size_t i = 0; i < ticks.size(); i++) {
        const Tick& tick = ticks[i];

        // Update regime
        regime.addPrice(tick.bid);
        if (i % 10000 == 0) {
            current_regime = regime.detect();
            if (current_regime != prev_regime) {
                r.regime_changes++;
                prev_regime = current_regime;
            }
        }

        switch (current_regime) {
            case Regime::BULL: bull_ticks++; break;
            case Regime::BEAR: bear_ticks++; break;
            case Regime::SIDEWAYS: sideways_ticks++; break;
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

            // Update peak price for trailing
            if (tick.bid > p->peak_price) p->peak_price = tick.bid;
        }

        double margin_level = (used_margin > 0) ? (equity / used_margin * 100.0) : 10000.0;
        if ((int)positions.size() > r.max_positions) r.max_positions = positions.size();

        // === TRAILING PROFIT LOCK (price-based) ===
        if (cfg.use_trailing_lock) {
            std::vector<Position*> to_close;
            for (Position* p : positions) {
                double profit_pts = tick.bid - p->entry_price;
                double pullback_pts = p->peak_price - tick.bid;

                // If we've gained enough AND price pulled back too much from peak
                if (profit_pts > cfg.lock_trigger_pts && pullback_pts > cfg.lock_pullback_pts) {
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

            // Recalculate if positions closed
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

        // === STRATEGY MODE SELECTION ===
        bool allow_hyperbolic = false;
        bool allow_grid = false;
        bool should_close = false;

        switch (cfg.mode) {
            case 0:  // Pure hyperbolic
                allow_hyperbolic = true;
                break;
            case 1:  // Regime filter - only trade in bull
                allow_hyperbolic = (current_regime == Regime::BULL);
                break;
            case 2:  // Hybrid - hyperbolic in bull, grid in sideways
                allow_hyperbolic = (current_regime == Regime::BULL);
                allow_grid = (current_regime == Regime::SIDEWAYS);
                should_close = (current_regime == Regime::BEAR);
                break;
            case 3:  // With trailing lock (already applied above)
                allow_hyperbolic = true;
                break;
            case 4:  // Regime + trailing lock
                allow_hyperbolic = (current_regime == Regime::BULL);
                break;
        }

        // === CLOSE IN BEAR MARKET ===
        if (should_close && !positions.empty()) {
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

        // === HYPERBOLIC STRATEGY ===
        if (allow_hyperbolic) {
            double spread_cost = tick.spread() * cfg.contract_size;

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
                        p->peak_price = tick.bid;
                        positions.push_back(p);
                        checked_last_open_price = tick.ask;
                        starting_x = tick.ask;
                        last_grid_price = tick.ask;
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
                        p->peak_price = tick.bid;
                        positions.push_back(p);
                        checked_last_open_price = tick.ask;
                    }
                }
            }
        }

        // === GRID STRATEGY ===
        if (allow_grid) {
            double grid_dd = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;

            if (grid_dd < cfg.grid_max_dd) {
                if (positions.empty() ||
                    (last_grid_price > 0 && std::abs(tick.ask - last_grid_price) >= cfg.grid_spacing)) {

                    double margin_needed = cfg.grid_lot * cfg.contract_size * tick.ask / cfg.leverage;
                    double free_margin = equity - used_margin;

                    if (free_margin > margin_needed * 1.5) {
                        Position* p = new Position();
                        p->id = next_id++;
                        p->entry_price = tick.ask;
                        p->lot_size = cfg.grid_lot;
                        p->peak_price = tick.bid;
                        positions.push_back(p);
                        last_grid_price = tick.ask;
                        if (starting_x == 0) starting_x = tick.ask;
                        if (checked_last_open_price < 0) checked_last_open_price = tick.ask;
                    }
                }

                // Grid TP
                double tp_distance = cfg.grid_spacing * cfg.grid_tp_multiplier;
                std::vector<Position*> to_close;
                for (Position* p : positions) {
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
    r.time_in_bull = (double)bull_ticks / ticks.size() * 100;
    r.time_in_bear = (double)bear_ticks / ticks.size() * 100;
    r.time_in_sideways = (double)sideways_ticks / ticks.size() * 100;
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
    printf("NAS100 REGIME-BASED STRATEGY V2\n");
    printf("================================================================\n\n");

    std::vector<Tick> ticks = LoadTicks("NAS100/NAS100_TICKS_2025.csv", 60000000);
    if (ticks.empty()) { printf("ERROR: No data\n"); return 1; }

    printf("Loaded %zu ticks\n", ticks.size());
    printf("Price: %.2f -> %.2f (%+.1f%%)\n\n",
           ticks.front().bid, ticks.back().bid,
           (ticks.back().bid - ticks.front().bid) / ticks.front().bid * 100);

    // First, check regime detection
    printf("================================================================\n");
    printf("REGIME DETECTION CALIBRATION\n");
    printf("================================================================\n\n");

    {
        Config cfg;
        cfg.mode = 1;
        cfg.power = -0.5;
        cfg.direct_lot_formula = true;
        cfg.lot_coefficient = 50.0;
        Result r = RunStrategy(ticks, cfg);
        printf("Default thresholds (bull>2%%, bear<-1.5%%, lookback=500k):\n");
        printf("  Regime changes: %d\n", r.regime_changes);
        printf("  Time in Bull: %.1f%%\n", r.time_in_bull);
        printf("  Time in Bear: %.1f%%\n", r.time_in_bear);
        printf("  Time in Sideways: %.1f%%\n\n", r.time_in_sideways);
    }

    // Test different lookback periods
    int lookbacks[] = {100000, 250000, 500000, 1000000};
    printf("%-20s %10s %10s %10s %10s\n", "Lookback", "Bull%", "Bear%", "Side%", "Changes");
    printf("------------------------------------------------------------------------\n");

    for (int lb : lookbacks) {
        Config cfg;
        cfg.mode = 1;
        cfg.regime_lookback = lb;
        cfg.power = -0.5;
        cfg.direct_lot_formula = true;
        cfg.lot_coefficient = 50.0;
        Result r = RunStrategy(ticks, cfg);
        printf("%-20d %9.1f%% %9.1f%% %9.1f%% %10d\n",
               lb, r.time_in_bull, r.time_in_bear, r.time_in_sideways, r.regime_changes);
    }
    printf("\n");

    // ========================================================================
    // TEST 1: DIFFERENT REGIME THRESHOLDS
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 1: REGIME FILTER THRESHOLD SWEEP\n");
    printf("================================================================\n\n");

    printf("%-35s %12s %7s %8s %6s %6s %4s\n",
           "Bull/Bear Threshold", "Final", "Return", "MaxDD", "Trades", "Win%", "SOs");
    printf("--------------------------------------------------------------------------------\n");

    double bull_thresholds[] = {0.01, 0.015, 0.02, 0.03};
    double bear_thresholds[] = {-0.01, -0.015, -0.02};

    for (double bt : bull_thresholds) {
        for (double brt : bear_thresholds) {
            Config cfg;
            cfg.mode = 1;
            cfg.power = -0.5;
            cfg.direct_lot_formula = true;
            cfg.lot_coefficient = 50.0;
            cfg.bull_threshold = bt;
            cfg.bear_threshold = brt;
            cfg.regime_lookback = 500000;
            Result r = RunStrategy(ticks, cfg);
            char name[64];
            snprintf(name, sizeof(name), "Bull>%.1f%% Bear<%.1f%%", bt*100, brt*100);
            PrintResult(name, r);
        }
    }

    printf("--------------------------------------------------------------------------------\n\n");

    // ========================================================================
    // TEST 2: TRAILING PROFIT LOCK WITH PRICE-BASED TRIGGERS
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 2: PRICE-BASED TRAILING PROFIT LOCK\n");
    printf("================================================================\n\n");

    printf("%-35s %12s %7s %8s %6s %6s %4s\n",
           "Trigger/Pullback (pts)", "Final", "Return", "MaxDD", "Trades", "Win%", "SOs");
    printf("--------------------------------------------------------------------------------\n");

    double triggers[] = {100, 200, 300, 500};
    double pullbacks[] = {50, 100, 150};

    for (double trig : triggers) {
        for (double pb : pullbacks) {
            Config cfg;
            cfg.mode = 3;
            cfg.power = -0.5;
            cfg.direct_lot_formula = true;
            cfg.lot_coefficient = 50.0;
            cfg.use_trailing_lock = true;
            cfg.lock_trigger_pts = trig;
            cfg.lock_pullback_pts = pb;
            Result r = RunStrategy(ticks, cfg);
            char name[64];
            snprintf(name, sizeof(name), "Trig=%.0f PB=%.0f", trig, pb);
            PrintResult(name, r);
        }
    }

    printf("--------------------------------------------------------------------------------\n\n");

    // ========================================================================
    // TEST 3: HYBRID WITH DIFFERENT POWERS
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 3: HYBRID APPROACH (Bull=Hyperbolic, Sideways=Grid)\n");
    printf("================================================================\n\n");

    printf("%-35s %12s %7s %8s %6s %6s %4s\n",
           "Power / Grid Config", "Final", "Return", "MaxDD", "Trades", "Win%", "SOs");
    printf("--------------------------------------------------------------------------------\n");

    double powers[] = {-0.811, -0.5, -0.3, 0.1, 0.3};

    for (double pw : powers) {
        Config cfg;
        cfg.mode = 2;
        cfg.power = pw;
        cfg.direct_lot_formula = (pw < 0);
        cfg.lot_coefficient = 50.0;
        cfg.grid_spacing = 100;
        cfg.grid_lot = 0.1;
        cfg.bull_threshold = 0.015;
        cfg.bear_threshold = -0.015;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "Hybrid pw=%.3f sp=100", pw);
        PrintResult(name, r);
    }

    printf("--------------------------------------------------------------------------------\n");

    // Different grid spacings
    double grid_spacings[] = {50, 75, 100, 150};
    for (double sp : grid_spacings) {
        Config cfg;
        cfg.mode = 2;
        cfg.power = -0.5;
        cfg.direct_lot_formula = true;
        cfg.lot_coefficient = 50.0;
        cfg.grid_spacing = sp;
        cfg.grid_lot = 0.1;
        cfg.bull_threshold = 0.015;
        cfg.bear_threshold = -0.015;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "Hybrid pw=-0.5 sp=%.0f", sp);
        PrintResult(name, r);
    }

    printf("--------------------------------------------------------------------------------\n\n");

    // ========================================================================
    // TEST 4: COMBINED BEST CONFIGURATIONS
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 4: COMBINED APPROACHES\n");
    printf("================================================================\n\n");

    printf("%-35s %12s %7s %8s %6s %6s %4s\n",
           "Strategy", "Final", "Return", "MaxDD", "Trades", "Win%", "SOs");
    printf("--------------------------------------------------------------------------------\n");

    // Pure hyperbolic baseline
    {
        Config cfg;
        cfg.mode = 0;
        cfg.power = -0.5;
        cfg.direct_lot_formula = true;
        cfg.lot_coefficient = 50.0;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Pure Hyperbolic (pw=-0.5)", r);
    }

    // Regime filter only
    {
        Config cfg;
        cfg.mode = 1;
        cfg.power = -0.5;
        cfg.direct_lot_formula = true;
        cfg.lot_coefficient = 50.0;
        cfg.bull_threshold = 0.015;
        cfg.bear_threshold = -0.015;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Regime Filter Only", r);
    }

    // Trailing lock only
    {
        Config cfg;
        cfg.mode = 3;
        cfg.power = -0.5;
        cfg.direct_lot_formula = true;
        cfg.lot_coefficient = 50.0;
        cfg.use_trailing_lock = true;
        cfg.lock_trigger_pts = 200;
        cfg.lock_pullback_pts = 100;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Trailing Lock Only", r);
    }

    // Regime + Trailing
    {
        Config cfg;
        cfg.mode = 4;
        cfg.power = -0.5;
        cfg.direct_lot_formula = true;
        cfg.lot_coefficient = 50.0;
        cfg.bull_threshold = 0.015;
        cfg.bear_threshold = -0.015;
        cfg.use_trailing_lock = true;
        cfg.lock_trigger_pts = 200;
        cfg.lock_pullback_pts = 100;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Regime + Trailing Lock", r);
    }

    // Hybrid
    {
        Config cfg;
        cfg.mode = 2;
        cfg.power = -0.5;
        cfg.direct_lot_formula = true;
        cfg.lot_coefficient = 50.0;
        cfg.grid_spacing = 100;
        cfg.grid_lot = 0.1;
        cfg.bull_threshold = 0.015;
        cfg.bear_threshold = -0.015;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Hybrid (Bull+Grid)", r);
    }

    // Hybrid + Trailing
    {
        Config cfg;
        cfg.mode = 2;
        cfg.power = -0.5;
        cfg.direct_lot_formula = true;
        cfg.lot_coefficient = 50.0;
        cfg.grid_spacing = 100;
        cfg.grid_lot = 0.1;
        cfg.bull_threshold = 0.015;
        cfg.bear_threshold = -0.015;
        cfg.use_trailing_lock = true;
        cfg.lock_trigger_pts = 200;
        cfg.lock_pullback_pts = 100;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Hybrid + Trailing Lock", r);
    }

    // Conservative (smaller positions)
    {
        Config cfg;
        cfg.mode = 1;
        cfg.power = -0.5;
        cfg.direct_lot_formula = true;
        cfg.lot_coefficient = 20.0;  // Much smaller
        cfg.bull_threshold = 0.02;
        cfg.bear_threshold = -0.01;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Conservative (coef=20)", r);
    }

    printf("--------------------------------------------------------------------------------\n\n");

    // ========================================================================
    // TEST 5: LOT COEFFICIENT SENSITIVITY
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 5: LOT COEFFICIENT SENSITIVITY (Regime Filter)\n");
    printf("================================================================\n\n");

    printf("%-35s %12s %7s %8s %6s %6s %4s\n",
           "Lot Coefficient", "Final", "Return", "MaxDD", "Trades", "Win%", "SOs");
    printf("--------------------------------------------------------------------------------\n");

    double coefficients[] = {10, 20, 30, 50, 75, 100};
    for (double coef : coefficients) {
        Config cfg;
        cfg.mode = 1;
        cfg.power = -0.5;
        cfg.direct_lot_formula = true;
        cfg.lot_coefficient = coef;
        cfg.bull_threshold = 0.015;
        cfg.bear_threshold = -0.015;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "Coef=%.0f", coef);
        PrintResult(name, r);
    }

    printf("--------------------------------------------------------------------------------\n\n");

    printf("================================================================\n");
    printf("SUMMARY\n");
    printf("================================================================\n\n");

    printf("Key Findings:\n");
    printf("1. Regime filter with 500k tick lookback detects bull/bear periods\n");
    printf("2. Price-based trailing locks (200pt trigger, 100pt pullback)\n");
    printf("3. Hybrid approach trades in bull and grid in sideways\n");
    printf("4. Lower lot coefficients (20-30) reduce risk\n\n");

    printf("================================================================\n");
    return 0;
}
