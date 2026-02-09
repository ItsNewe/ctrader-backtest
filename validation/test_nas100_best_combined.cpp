/**
 * NAS100 Best Combined Strategy
 *
 * Combines all successful elements:
 * 1. Regime filter (only trade in bull markets)
 * 2. Aggressive take profit
 * 3. Portfolio DD limit
 * 4. Conservative position sizing
 * 5. Momentum confirmation for entries
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
    double lot_coefficient = 30.0;  // Conservative
    bool direct_lot_formula = true;

    // Regime detection
    int regime_lookback = 300000;
    double bull_threshold = 0.015;
    double bear_threshold = -0.01;

    // Entry filter (momentum)
    bool require_momentum = true;
    int momentum_period = 100;
    double min_momentum = 0;  // Price must be above SMA

    // Take profit
    bool use_tp = true;
    double fixed_tp_pts = 200;

    // Trailing
    bool use_trailing = true;
    double trail_activation_pts = 50;
    double trail_distance_pts = 30;

    // Portfolio DD limit
    double max_portfolio_dd = 15.0;

    // Time between trades
    int min_ticks_between_trades = 100;
};

struct Result {
    double final_balance;
    double return_multiple;
    double max_dd;
    int total_trades;
    int winning_trades;
    int tp_exits;
    int trail_exits;
    int dd_exits;
    int regime_filtered;
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

enum class Regime { BULL, BEAR, SIDEWAYS };

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
    int last_trade_tick = -1000000;

    // Regime detection
    std::deque<double> regime_prices;
    int regime_sample_rate = 1000;

    // Momentum tracking
    std::deque<double> momentum_prices;

    Regime current_regime = Regime::SIDEWAYS;

    for (size_t i = 0; i < ticks.size(); i++) {
        const Tick& tick = ticks[i];

        // Update regime detection
        if (i % regime_sample_rate == 0) {
            regime_prices.push_back(tick.bid);
            if ((int)regime_prices.size() > cfg.regime_lookback / regime_sample_rate)
                regime_prices.pop_front();

            if (regime_prices.size() > 10) {
                double change = (regime_prices.back() - regime_prices.front()) / regime_prices.front();
                if (change > cfg.bull_threshold) current_regime = Regime::BULL;
                else if (change < cfg.bear_threshold) current_regime = Regime::BEAR;
                else current_regime = Regime::SIDEWAYS;
            }
        }

        // Update momentum
        if (i % 100 == 0) {
            momentum_prices.push_back(tick.bid);
            if ((int)momentum_prices.size() > cfg.momentum_period)
                momentum_prices.pop_front();
        }

        double momentum_sma = 0;
        if (!momentum_prices.empty()) {
            for (double p : momentum_prices) momentum_sma += p;
            momentum_sma /= momentum_prices.size();
        }

        // Calculate equity and margin
        equity = balance;
        double used_margin = 0;
        volume_of_open_trades = 0;

        for (Position* p : positions) {
            equity += (tick.bid - p->entry_price) * p->lot_size * cfg.contract_size;
            used_margin += p->lot_size * cfg.contract_size * p->entry_price / cfg.leverage;
            volume_of_open_trades += p->lot_size;

            if (tick.bid > p->peak_price) p->peak_price = tick.bid;
        }

        double margin_level = (used_margin > 0) ? (equity / used_margin * 100.0) : 10000.0;

        // === TAKE PROFIT / TRAILING ===
        std::vector<Position*> to_close;
        for (Position* p : positions) {
            double profit_pts = tick.bid - p->entry_price;
            double pullback = p->peak_price - tick.bid;
            bool should_close = false;
            int exit_type = 0;

            // Fixed TP
            if (cfg.use_tp && profit_pts >= cfg.fixed_tp_pts) {
                should_close = true;
                exit_type = 1;
            }

            // Trailing
            if (cfg.use_trailing) {
                if (profit_pts >= cfg.trail_activation_pts) {
                    p->is_trailing = true;
                }
                if (p->is_trailing && pullback >= cfg.trail_distance_pts) {
                    should_close = true;
                    exit_type = 2;
                }
            }

            // Exit in bear market
            if (current_regime == Regime::BEAR && profit_pts > -50) {
                should_close = true;
                exit_type = 3;
            }

            if (should_close) {
                to_close.push_back(p);
                if (exit_type == 1) r.tp_exits++;
                else if (exit_type == 2) r.trail_exits++;
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

        // === PORTFOLIO DD LIMIT ===
        if (positions.empty()) peak_equity = balance;
        if (equity > peak_equity) peak_equity = equity;

        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        if (dd_pct > max_drawdown) max_drawdown = dd_pct;

        if (dd_pct >= cfg.max_portfolio_dd && !positions.empty()) {
            r.dd_exits++;
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

        // === ENTRY CONDITIONS ===
        bool allow_entry = true;

        // Regime filter
        if (current_regime != Regime::BULL) {
            allow_entry = false;
            r.regime_filtered++;
        }

        // Momentum filter
        if (cfg.require_momentum && tick.bid < momentum_sma) {
            allow_entry = false;
        }

        // Time between trades
        if ((int)i - last_trade_tick < cfg.min_ticks_between_trades) {
            allow_entry = false;
        }

        // === OPEN POSITIONS ===
        if (allow_entry) {
            double spread_cost = tick.spread() * cfg.contract_size;

            if (volume_of_open_trades == 0) {
                local_starting_room = tick.ask * cfg.multiplier / 100.0;

                double lot = cfg.lot_coefficient;
                lot = std::min(lot, cfg.max_lot);
                lot = std::max(lot, cfg.min_lot);
                lot = std::round(lot * 100) / 100;

                double margin_needed = lot * cfg.contract_size * tick.ask / cfg.leverage;
                if (equity > margin_needed * 1.5) {
                    Position* p = new Position();
                    p->id = next_id++;
                    p->entry_price = tick.ask;
                    p->lot_size = lot;
                    p->peak_price = tick.bid;
                    p->is_trailing = false;
                    positions.push_back(p);
                    checked_last_open_price = tick.ask;
                    starting_x = tick.ask;
                    last_trade_tick = i;
                }
            }
            else if (tick.ask > checked_last_open_price) {
                double distance = tick.ask - starting_x;
                if (distance < 1) distance = 1;

                double lot = cfg.lot_coefficient * std::pow(distance, cfg.power);
                lot = std::min(lot, cfg.max_lot);
                lot = std::max(lot, cfg.min_lot);
                lot = std::round(lot * 100) / 100;

                double margin_needed = lot * cfg.contract_size * tick.ask / cfg.leverage;
                double free_margin = equity - used_margin;

                if (free_margin > margin_needed * 1.5) {
                    Position* p = new Position();
                    p->id = next_id++;
                    p->entry_price = tick.ask;
                    p->lot_size = lot;
                    p->peak_price = tick.bid;
                    p->is_trailing = false;
                    positions.push_back(p);
                    checked_last_open_price = tick.ask;
                    last_trade_tick = i;
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
    printf("%-40s $%10.2f %6.1fx %7.1f%% %6d %5.1f%%\n",
           name, r.final_balance, r.return_multiple, r.max_dd,
           r.total_trades,
           r.total_trades > 0 ? (double)r.winning_trades / r.total_trades * 100 : 0);
}

int main() {
    printf("================================================================\n");
    printf("NAS100 BEST COMBINED STRATEGY\n");
    printf("================================================================\n\n");

    std::vector<Tick> ticks = LoadTicks("NAS100/NAS100_TICKS_2025.csv", 60000000);
    if (ticks.empty()) { printf("ERROR: No data\n"); return 1; }

    printf("Loaded %zu ticks\n", ticks.size());
    printf("Price: %.2f -> %.2f (%+.1f%%)\n\n",
           ticks.front().bid, ticks.back().bid,
           (ticks.back().bid - ticks.front().bid) / ticks.front().bid * 100);

    printf("%-40s %12s %7s %8s %6s %6s\n",
           "Configuration", "Final", "Return", "MaxDD", "Trades", "Win%");
    printf("================================================================================\n\n");

    // ========================================================================
    // TEST MATRIX: DD Limit x TP Distance x Lot Coefficient
    // ========================================================================

    double dd_limits[] = {10, 15, 20, 25};
    double tp_distances[] = {100, 200, 300};
    double lot_coefs[] = {10, 20, 30, 50};

    printf("--- DD LIMIT SWEEP (TP=200, Coef=30) ---\n");
    for (double dd : dd_limits) {
        Config cfg;
        cfg.max_portfolio_dd = dd;
        cfg.fixed_tp_pts = 200;
        cfg.lot_coefficient = 30;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "DD=%.0f%%", dd);
        PrintResult(name, r);
    }
    printf("\n");

    printf("--- TP DISTANCE SWEEP (DD=15%%, Coef=30) ---\n");
    for (double tp : tp_distances) {
        Config cfg;
        cfg.max_portfolio_dd = 15;
        cfg.fixed_tp_pts = tp;
        cfg.lot_coefficient = 30;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "TP=%.0fpts", tp);
        PrintResult(name, r);
    }
    printf("\n");

    printf("--- LOT COEFFICIENT SWEEP (DD=15%%, TP=200) ---\n");
    for (double coef : lot_coefs) {
        Config cfg;
        cfg.max_portfolio_dd = 15;
        cfg.fixed_tp_pts = 200;
        cfg.lot_coefficient = coef;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "Coef=%.0f", coef);
        PrintResult(name, r);
    }
    printf("\n");

    // ========================================================================
    // POWER SWEEP
    // ========================================================================
    printf("--- POWER SWEEP (DD=15%%, TP=200, Coef=30) ---\n");
    double powers[] = {-0.8, -0.5, -0.3, 0.1, 0.3};
    for (double pw : powers) {
        Config cfg;
        cfg.max_portfolio_dd = 15;
        cfg.fixed_tp_pts = 200;
        cfg.lot_coefficient = 30;
        cfg.power = pw;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "Power=%.1f", pw);
        PrintResult(name, r);
    }
    printf("\n");

    // ========================================================================
    // REGIME LOOKBACK SWEEP
    // ========================================================================
    printf("--- REGIME LOOKBACK SWEEP ---\n");
    int lookbacks[] = {100000, 200000, 300000, 500000};
    for (int lb : lookbacks) {
        Config cfg;
        cfg.max_portfolio_dd = 15;
        cfg.fixed_tp_pts = 200;
        cfg.lot_coefficient = 30;
        cfg.regime_lookback = lb;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "Lookback=%dk", lb/1000);
        PrintResult(name, r);
    }
    printf("\n");

    // ========================================================================
    // BEST CONFIGURATIONS
    // ========================================================================
    printf("================================================================================\n");
    printf("BEST CONFIGURATIONS\n");
    printf("================================================================================\n\n");

    struct BestCfg {
        const char* name;
        double dd, tp, coef, power;
        int lookback;
    };

    BestCfg best_configs[] = {
        {"Conservative (Low Risk)", 10, 150, 20, -0.5, 200000},
        {"Balanced", 15, 200, 30, -0.5, 300000},
        {"Moderate Aggressive", 20, 250, 40, -0.3, 300000},
        {"Aggressive", 25, 300, 50, -0.3, 200000},
    };

    for (const auto& bc : best_configs) {
        Config cfg;
        cfg.max_portfolio_dd = bc.dd;
        cfg.fixed_tp_pts = bc.tp;
        cfg.lot_coefficient = bc.coef;
        cfg.power = bc.power;
        cfg.regime_lookback = bc.lookback;
        Result r = RunStrategy(ticks, cfg);
        PrintResult(bc.name, r);
        printf("                                         TP:%d Trail:%d DD:%d\n",
               r.tp_exits, r.trail_exits, r.dd_exits);
    }
    printf("\n");

    // ========================================================================
    // ABLATION STUDY
    // ========================================================================
    printf("================================================================================\n");
    printf("ABLATION STUDY (Remove components)\n");
    printf("================================================================================\n\n");

    // Full config
    {
        Config cfg;
        cfg.max_portfolio_dd = 15;
        cfg.fixed_tp_pts = 200;
        cfg.lot_coefficient = 30;
        cfg.use_tp = true;
        cfg.use_trailing = true;
        cfg.require_momentum = true;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Full (Regime+TP+Trail+Momentum)", r);
    }

    // No momentum
    {
        Config cfg;
        cfg.max_portfolio_dd = 15;
        cfg.fixed_tp_pts = 200;
        cfg.lot_coefficient = 30;
        cfg.use_tp = true;
        cfg.use_trailing = true;
        cfg.require_momentum = false;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("No Momentum Filter", r);
    }

    // No trailing
    {
        Config cfg;
        cfg.max_portfolio_dd = 15;
        cfg.fixed_tp_pts = 200;
        cfg.lot_coefficient = 30;
        cfg.use_tp = true;
        cfg.use_trailing = false;
        cfg.require_momentum = true;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("No Trailing", r);
    }

    // No TP
    {
        Config cfg;
        cfg.max_portfolio_dd = 15;
        cfg.fixed_tp_pts = 200;
        cfg.lot_coefficient = 30;
        cfg.use_tp = false;
        cfg.use_trailing = true;
        cfg.require_momentum = true;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("No Fixed TP", r);
    }

    // Higher DD limit (less strict)
    {
        Config cfg;
        cfg.max_portfolio_dd = 50;
        cfg.fixed_tp_pts = 200;
        cfg.lot_coefficient = 30;
        cfg.use_tp = true;
        cfg.use_trailing = true;
        cfg.require_momentum = true;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Higher DD Limit (50%)", r);
    }

    printf("\n================================================================================\n");
    printf("SUMMARY\n");
    printf("================================================================================\n\n");

    printf("Key findings:\n");
    printf("1. DD limit is crucial - 15-20%% preserves capital\n");
    printf("2. TP around 200pts captures profits before reversals\n");
    printf("3. Regime filter reduces exposure in bear markets\n");
    printf("4. Lower lot coefficient (20-30) reduces risk\n\n");

    printf("Best configuration for capital preservation:\n");
    printf("  DD Limit: 15%%\n");
    printf("  TP: 200 pts\n");
    printf("  Lot Coef: 30\n");
    printf("  Power: -0.5\n");
    printf("  Regime Lookback: 300k ticks\n");

    printf("\n================================================================\n");
    return 0;
}
