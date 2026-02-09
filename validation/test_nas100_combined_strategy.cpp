/**
 * NAS100 Combined Strategy
 *
 * Combines elements from:
 * 1. Fluctuation (Grid) - spacing, TP, works in sideways
 * 2. Bull (grid_open_upwards) - survive % sizing, trend following
 * 3. Aggressive Bull (Nasdaq_up) - hyperbolic sizing, ATR trailing
 *
 * Regime-based switching:
 * - BULL: Hyperbolic position sizing + ATR trailing
 * - SIDEWAYS: Grid trading with fixed spacing + TP
 * - BEAR: No new positions, close existing with trailing
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
    double take_profit;
    int mode;  // 0=grid, 1=bull, 2=aggressive
};

enum class Regime { BULL, BEAR, SIDEWAYS };

struct Config {
    // Account settings
    double initial_balance = 10000.0;
    double leverage = 500.0;
    double contract_size = 1.0;
    double min_lot = 0.01;
    double max_lot = 100.0;

    // Regime detection
    int regime_lookback = 200000;  // ~1 day of ticks
    double bull_threshold = 0.015;  // +1.5% = bull
    double bear_threshold = -0.01;  // -1% = bear
    int regime_sample_rate = 1000;

    // === GRID MODE (Fluctuation) ===
    double grid_spacing = 100.0;
    double grid_lot = 0.1;
    double grid_tp_multiplier = 2.0;
    double grid_max_positions = 10;

    // === BULL MODE (grid_open_upwards) ===
    double survive_down_pct = 10.0;  // Survive 10% drop
    double bull_min_spacing = 50.0;  // Min distance between positions

    // === AGGRESSIVE MODE (Nasdaq_up hyperbolic) ===
    double power = -0.5;
    double multiplier = 0.1;
    double lot_coefficient = 30.0;
    double manual_stop_out = 74.0;

    // === EXIT MECHANISMS ===
    // ATR trailing (for bull/aggressive modes)
    bool use_atr_trailing = true;
    double atr_multiplier = 2.5;
    int atr_period = 100;
    int atr_sample_rate = 10;

    // Fixed trailing (backup)
    double trail_activation = 150.0;
    double trail_distance = 75.0;

    // Take profit (for all modes)
    double bull_tp_pts = 300.0;
    double aggressive_tp_pts = 500.0;

    // Portfolio protection
    double max_portfolio_dd = 25.0;
    double max_daily_loss = 10.0;

    // Mode selection
    int force_mode = -1;  // -1 = auto (regime-based), 0/1/2 = force mode
};

struct Result {
    double final_balance;
    double return_multiple;
    double max_dd;
    int total_trades;
    int winning_trades;
    int grid_trades;
    int bull_trades;
    int aggressive_trades;
    int trail_exits;
    int tp_exits;
    int regime_changes;
    double time_in_bull;
    double time_in_bear;
    double time_in_sideways;
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

    double balance = cfg.initial_balance;
    double equity = balance;
    double peak_equity = balance;
    double day_start_balance = balance;
    double max_drawdown = 0;

    std::vector<Position*> positions;
    size_t next_id = 1;

    // Regime detection
    std::deque<double> regime_prices;
    Regime current_regime = Regime::SIDEWAYS;
    Regime prev_regime = Regime::SIDEWAYS;

    // ATR calculation
    std::deque<double> price_changes;
    double current_atr = 50.0;
    double prev_price = ticks.front().bid;

    // Bull mode tracking
    double bull_last_open_price = -1e9;
    double bull_starting_x = 0;

    // Grid mode tracking
    double grid_last_price = 0;

    // Stats
    size_t bull_ticks = 0, bear_ticks = 0, sideways_ticks = 0;

    for (size_t i = 0; i < ticks.size(); i++) {
        const Tick& tick = ticks[i];

        // === UPDATE REGIME ===
        if (i % cfg.regime_sample_rate == 0) {
            regime_prices.push_back(tick.bid);
            if ((int)regime_prices.size() > cfg.regime_lookback / cfg.regime_sample_rate) {
                regime_prices.pop_front();
            }

            if (regime_prices.size() > 10) {
                double change = (regime_prices.back() - regime_prices.front()) / regime_prices.front();

                if (change > cfg.bull_threshold) {
                    current_regime = Regime::BULL;
                } else if (change < cfg.bear_threshold) {
                    current_regime = Regime::BEAR;
                } else {
                    current_regime = Regime::SIDEWAYS;
                }

                if (current_regime != prev_regime) {
                    r.regime_changes++;
                    prev_regime = current_regime;

                    // Reset mode-specific tracking on regime change
                    bull_last_open_price = -1e9;
                    grid_last_price = tick.bid;
                }
            }
        }

        // Force mode if specified
        if (cfg.force_mode >= 0) {
            current_regime = (cfg.force_mode == 0) ? Regime::SIDEWAYS :
                            (cfg.force_mode == 1) ? Regime::BULL : Regime::BULL;
        }

        switch (current_regime) {
            case Regime::BULL: bull_ticks++; break;
            case Regime::BEAR: bear_ticks++; break;
            case Regime::SIDEWAYS: sideways_ticks++; break;
        }

        // === UPDATE ATR ===
        if (i % cfg.atr_sample_rate == 0) {
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

        // === UPDATE POSITIONS ===
        equity = balance;
        double used_margin = 0;
        double total_lots = 0;

        for (Position* p : positions) {
            double pnl = (tick.bid - p->entry_price) * p->lot_size * cfg.contract_size;
            equity += pnl;
            used_margin += p->lot_size * cfg.contract_size * p->entry_price / cfg.leverage;
            total_lots += p->lot_size;

            // Update peak price
            if (tick.bid > p->peak_price) {
                p->peak_price = tick.bid;
            }

            // === UPDATE TRAILING STOPS ===
            double profit = tick.bid - p->entry_price;

            // ATR-based trailing
            if (cfg.use_atr_trailing && profit > current_atr * cfg.atr_multiplier) {
                double new_sl = p->peak_price - current_atr * cfg.atr_multiplier;
                if (new_sl > p->stop_loss) p->stop_loss = new_sl;
            }

            // Fixed trailing (backup)
            if (profit > cfg.trail_activation) {
                double new_sl = p->peak_price - cfg.trail_distance;
                if (new_sl > p->stop_loss) p->stop_loss = new_sl;
            }
        }

        double margin_level = (used_margin > 0) ? (equity / used_margin * 100.0) : 10000.0;

        // === CHECK EXITS ===
        std::vector<Position*> to_close;
        for (Position* p : positions) {
            bool should_close = false;
            int exit_type = 0;  // 1=SL/trail, 2=TP

            // Stop loss / trailing stop
            if (p->stop_loss > 0 && tick.bid <= p->stop_loss) {
                should_close = true;
                exit_type = 1;
            }

            // Take profit
            if (p->take_profit > 0 && tick.bid >= p->take_profit) {
                should_close = true;
                exit_type = 2;
            }

            // Exit in bear market (close profitable positions)
            if (current_regime == Regime::BEAR && p->mode != 0) {
                double profit = tick.bid - p->entry_price;
                if (profit > 0) {
                    should_close = true;
                    exit_type = 1;
                }
            }

            if (should_close) {
                to_close.push_back(p);
                if (exit_type == 1) r.trail_exits++;
                else if (exit_type == 2) r.tp_exits++;
            }
        }

        for (Position* p : to_close) {
            double exit_price = (p->stop_loss > 0 && tick.bid <= p->stop_loss) ? p->stop_loss :
                               (p->take_profit > 0 && tick.bid >= p->take_profit) ? p->take_profit : tick.bid;
            double pnl = (exit_price - p->entry_price) * p->lot_size * cfg.contract_size;
            balance += pnl;
            r.total_trades++;
            if (pnl > 0) r.winning_trades++;
            if (p->mode == 0) r.grid_trades++;
            else if (p->mode == 1) r.bull_trades++;
            else r.aggressive_trades++;
            positions.erase(std::remove(positions.begin(), positions.end(), p), positions.end());
            delete p;
        }

        // Recalculate after closes
        if (!to_close.empty()) {
            equity = balance;
            used_margin = 0;
            total_lots = 0;
            for (Position* p : positions) {
                equity += (tick.bid - p->entry_price) * p->lot_size * cfg.contract_size;
                used_margin += p->lot_size * cfg.contract_size * p->entry_price / cfg.leverage;
                total_lots += p->lot_size;
            }
            margin_level = (used_margin > 0) ? (equity / used_margin * 100.0) : 10000.0;
        }

        // === PORTFOLIO PROTECTION ===
        if (positions.empty()) peak_equity = balance;
        if (equity > peak_equity) peak_equity = equity;

        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        if (dd_pct > max_drawdown) max_drawdown = dd_pct;

        // DD limit exit
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
            bull_last_open_price = -1e9;
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
            bull_last_open_price = -1e9;
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
            bull_last_open_price = -1e9;
            continue;
        }

        // === OPEN NEW POSITIONS ===
        double spread_cost = tick.spread() * cfg.contract_size;

        // Count positions by mode
        int grid_positions = 0, bull_positions = 0;
        for (Position* p : positions) {
            if (p->mode == 0) grid_positions++;
            else bull_positions++;
        }

        // =====================================================================
        // SIDEWAYS MODE: Grid Trading (Fluctuation strategy)
        // =====================================================================
        if (current_regime == Regime::SIDEWAYS) {
            if (grid_positions < (int)cfg.grid_max_positions) {
                bool should_open = false;

                if (grid_positions == 0) {
                    should_open = true;
                    grid_last_price = tick.ask;
                } else if (std::abs(tick.ask - grid_last_price) >= cfg.grid_spacing) {
                    should_open = true;
                }

                if (should_open) {
                    double lot = cfg.grid_lot;
                    double margin_needed = lot * cfg.contract_size * tick.ask / cfg.leverage;
                    double free_margin = equity - used_margin;

                    if (free_margin > margin_needed * 1.5 && lot >= cfg.min_lot) {
                        Position* p = new Position();
                        p->id = next_id++;
                        p->entry_price = tick.ask;
                        p->lot_size = lot;
                        p->peak_price = tick.bid;
                        p->stop_loss = 0;
                        p->take_profit = tick.ask + cfg.grid_spacing * cfg.grid_tp_multiplier;
                        p->mode = 0;
                        positions.push_back(p);
                        grid_last_price = tick.ask;
                    }
                }
            }
        }

        // =====================================================================
        // BULL MODE: Survive-based sizing (grid_open_upwards strategy)
        // =====================================================================
        else if (current_regime == Regime::BULL && (cfg.force_mode == -1 || cfg.force_mode == 1)) {
            if (tick.ask > bull_last_open_price + cfg.bull_min_spacing) {
                // Calculate lot to survive X% drop
                double survive_drop = tick.ask * cfg.survive_down_pct / 100.0;
                double lot = equity / (survive_drop + spread_cost + tick.ask / cfg.leverage * (100 - cfg.manual_stop_out) / 100);
                lot = lot / cfg.contract_size;

                // Scale down if we have existing positions
                if (bull_positions > 0) {
                    lot = lot / (bull_positions + 1);
                }

                lot = std::min(lot, cfg.max_lot);
                lot = std::max(lot, cfg.min_lot);
                lot = std::round(lot * 100) / 100;

                double margin_needed = lot * cfg.contract_size * tick.ask / cfg.leverage;
                double free_margin = equity - used_margin;

                if (free_margin > margin_needed * 1.2 && lot >= cfg.min_lot) {
                    Position* p = new Position();
                    p->id = next_id++;
                    p->entry_price = tick.ask;
                    p->lot_size = lot;
                    p->peak_price = tick.bid;
                    p->stop_loss = 0;
                    p->take_profit = tick.ask + cfg.bull_tp_pts;
                    p->mode = 1;
                    positions.push_back(p);
                    bull_last_open_price = tick.ask;
                    if (bull_starting_x == 0) bull_starting_x = tick.ask;
                }
            }
        }

        // =====================================================================
        // AGGRESSIVE BULL: Hyperbolic sizing (Nasdaq_up strategy)
        // =====================================================================
        else if (current_regime == Regime::BULL && cfg.force_mode == 2) {
            if (total_lots == 0) {
                double lot = cfg.lot_coefficient;
                lot = std::min(lot, cfg.max_lot);
                lot = std::round(lot * 100) / 100;

                double margin_needed = lot * cfg.contract_size * tick.ask / cfg.leverage;
                if (equity > margin_needed * 1.2 && lot >= cfg.min_lot) {
                    Position* p = new Position();
                    p->id = next_id++;
                    p->entry_price = tick.ask;
                    p->lot_size = lot;
                    p->peak_price = tick.bid;
                    p->stop_loss = 0;
                    p->take_profit = tick.ask + cfg.aggressive_tp_pts;
                    p->mode = 2;
                    positions.push_back(p);
                    bull_last_open_price = tick.ask;
                    bull_starting_x = tick.ask;
                }
            }
            else if (tick.ask > bull_last_open_price) {
                double distance = tick.ask - bull_starting_x;
                if (distance < 1) distance = 1;

                double lot = cfg.lot_coefficient * std::pow(distance, cfg.power);
                lot = std::min(lot, cfg.max_lot);
                lot = std::max(lot, cfg.min_lot);
                lot = std::round(lot * 100) / 100;

                double margin_needed = lot * cfg.contract_size * tick.ask / cfg.leverage;
                double free_margin = equity - used_margin;

                if (free_margin > margin_needed * 1.2 && lot >= cfg.min_lot) {
                    Position* p = new Position();
                    p->id = next_id++;
                    p->entry_price = tick.ask;
                    p->lot_size = lot;
                    p->peak_price = tick.bid;
                    p->stop_loss = 0;
                    p->take_profit = tick.ask + cfg.aggressive_tp_pts;
                    p->mode = 2;
                    positions.push_back(p);
                    bull_last_open_price = tick.ask;
                }
            }
        }

        // BEAR MODE: No new positions (handled above - just exits)
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
    r.return_multiple = r.final_balance / cfg.initial_balance;
    r.max_dd = max_drawdown;
    r.time_in_bull = (double)bull_ticks / ticks.size() * 100;
    r.time_in_bear = (double)bear_ticks / ticks.size() * 100;
    r.time_in_sideways = (double)sideways_ticks / ticks.size() * 100;
    return r;
}

void PrintResult(const char* name, Result r) {
    printf("%-45s $%10.2f %6.2fx %6.1f%% %5d %5.1f%%\n",
           name, r.final_balance, r.return_multiple, r.max_dd,
           r.total_trades,
           r.total_trades > 0 ? (double)r.winning_trades / r.total_trades * 100 : 0);
}

int main() {
    printf("================================================================\n");
    printf("NAS100 COMBINED STRATEGY\n");
    printf("Fluctuation + Bull + Aggressive Bull\n");
    printf("================================================================\n\n");

    std::vector<Tick> ticks = LoadTicks("NAS100/NAS100_TICKS_2025.csv", 60000000);
    if (ticks.empty()) { printf("ERROR: No data\n"); return 1; }

    printf("Loaded %zu ticks (%.0f -> %.0f = %+.1f%%)\n\n",
           ticks.size(), ticks.front().bid, ticks.back().bid,
           (ticks.back().bid - ticks.front().bid) / ticks.front().bid * 100);

    printf("%-45s %12s %7s %7s %5s %6s\n",
           "Configuration", "Final", "Return", "MaxDD", "Trade", "Win%");
    printf("================================================================================\n\n");

    // ========================================================================
    // BASELINE: Individual strategies
    // ========================================================================
    printf("--- INDIVIDUAL STRATEGIES (Forced Mode) ---\n\n");

    // Grid only
    {
        Config cfg;
        cfg.force_mode = 0;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Grid Only (Fluctuation)", r);
        printf("  Grid: %d, Bull: %d, Aggr: %d | Trail: %d, TP: %d\n\n",
               r.grid_trades, r.bull_trades, r.aggressive_trades, r.trail_exits, r.tp_exits);
    }

    // Bull only
    {
        Config cfg;
        cfg.force_mode = 1;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Bull Only (grid_open_upwards)", r);
        printf("  Grid: %d, Bull: %d, Aggr: %d | Trail: %d, TP: %d\n\n",
               r.grid_trades, r.bull_trades, r.aggressive_trades, r.trail_exits, r.tp_exits);
    }

    // Aggressive only
    {
        Config cfg;
        cfg.force_mode = 2;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Aggressive Only (Nasdaq_up)", r);
        printf("  Grid: %d, Bull: %d, Aggr: %d | Trail: %d, TP: %d\n\n",
               r.grid_trades, r.bull_trades, r.aggressive_trades, r.trail_exits, r.tp_exits);
    }

    // ========================================================================
    // REGIME-BASED COMBINED
    // ========================================================================
    printf("--- REGIME-BASED COMBINED (Auto-Switch) ---\n\n");

    {
        Config cfg;
        cfg.force_mode = -1;  // Auto
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Combined (Auto Regime Switch)", r);
        printf("  Grid: %d, Bull: %d | Trail: %d, TP: %d\n",
               r.grid_trades, r.bull_trades, r.trail_exits, r.tp_exits);
        printf("  Time: Bull=%.1f%%, Bear=%.1f%%, Sideways=%.1f%% | Changes: %d\n\n",
               r.time_in_bull, r.time_in_bear, r.time_in_sideways, r.regime_changes);
    }

    // ========================================================================
    // PARAMETER TUNING
    // ========================================================================
    printf("--- PARAMETER TUNING ---\n\n");

    // Regime thresholds
    printf("Bull Threshold sweep:\n");
    double bull_thresholds[] = {0.01, 0.015, 0.02, 0.025, 0.03};
    for (double bt : bull_thresholds) {
        Config cfg;
        cfg.force_mode = -1;
        cfg.bull_threshold = bt;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "  Bull > %.1f%%", bt * 100);
        PrintResult(name, r);
    }
    printf("\n");

    // Grid spacing
    printf("Grid Spacing sweep:\n");
    double spacings[] = {50, 75, 100, 150, 200};
    for (double sp : spacings) {
        Config cfg;
        cfg.force_mode = -1;
        cfg.grid_spacing = sp;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "  Grid spacing = %.0f", sp);
        PrintResult(name, r);
    }
    printf("\n");

    // Survive percentage (Bull mode)
    printf("Survive %% sweep:\n");
    double survives[] = {5, 10, 15, 20, 30};
    for (double sv : survives) {
        Config cfg;
        cfg.force_mode = -1;
        cfg.survive_down_pct = sv;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "  Survive %.0f%%", sv);
        PrintResult(name, r);
    }
    printf("\n");

    // ATR trailing multiplier
    printf("ATR Multiplier sweep:\n");
    double atr_mults[] = {1.5, 2.0, 2.5, 3.0, 4.0};
    for (double am : atr_mults) {
        Config cfg;
        cfg.force_mode = -1;
        cfg.atr_multiplier = am;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "  ATR x %.1f", am);
        PrintResult(name, r);
    }
    printf("\n");

    // Portfolio DD limit
    printf("Portfolio DD Limit sweep:\n");
    double dd_limits[] = {15, 20, 25, 30, 40};
    for (double dd : dd_limits) {
        Config cfg;
        cfg.force_mode = -1;
        cfg.max_portfolio_dd = dd;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "  Max DD = %.0f%%", dd);
        PrintResult(name, r);
    }
    printf("\n");

    // ========================================================================
    // BEST COMBINED CONFIGURATIONS
    // ========================================================================
    printf("================================================================================\n");
    printf("BEST COMBINED CONFIGURATIONS\n");
    printf("================================================================================\n\n");

    // Conservative
    {
        Config cfg;
        cfg.force_mode = -1;
        cfg.bull_threshold = 0.02;
        cfg.grid_spacing = 100;
        cfg.grid_lot = 0.05;
        cfg.survive_down_pct = 15;
        cfg.atr_multiplier = 2.5;
        cfg.max_portfolio_dd = 20;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Conservative Combined", r);
        printf("  Bull=%.1f%% | Grid: %d, Bull: %d | Trail: %d, TP: %d\n\n",
               r.time_in_bull, r.grid_trades, r.bull_trades, r.trail_exits, r.tp_exits);
    }

    // Balanced
    {
        Config cfg;
        cfg.force_mode = -1;
        cfg.bull_threshold = 0.015;
        cfg.grid_spacing = 75;
        cfg.grid_lot = 0.1;
        cfg.survive_down_pct = 10;
        cfg.atr_multiplier = 2.0;
        cfg.max_portfolio_dd = 25;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Balanced Combined", r);
        printf("  Bull=%.1f%% | Grid: %d, Bull: %d | Trail: %d, TP: %d\n\n",
               r.time_in_bull, r.grid_trades, r.bull_trades, r.trail_exits, r.tp_exits);
    }

    // Aggressive
    {
        Config cfg;
        cfg.force_mode = -1;
        cfg.bull_threshold = 0.01;
        cfg.grid_spacing = 50;
        cfg.grid_lot = 0.15;
        cfg.survive_down_pct = 8;
        cfg.atr_multiplier = 1.5;
        cfg.max_portfolio_dd = 30;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Aggressive Combined", r);
        printf("  Bull=%.1f%% | Grid: %d, Bull: %d | Trail: %d, TP: %d\n\n",
               r.time_in_bull, r.grid_trades, r.bull_trades, r.trail_exits, r.tp_exits);
    }

    // Grid-Heavy (more time in grid mode)
    {
        Config cfg;
        cfg.force_mode = -1;
        cfg.bull_threshold = 0.03;  // Higher = less bull time
        cfg.bear_threshold = -0.005;  // Quick bear detection
        cfg.grid_spacing = 75;
        cfg.grid_lot = 0.1;
        cfg.grid_tp_multiplier = 2.5;
        cfg.max_portfolio_dd = 20;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Grid-Heavy Combined", r);
        printf("  Bull=%.1f%%, Side=%.1f%% | Grid: %d, Bull: %d\n\n",
               r.time_in_bull, r.time_in_sideways, r.grid_trades, r.bull_trades);
    }

    // Bull-Heavy (more time in bull mode)
    {
        Config cfg;
        cfg.force_mode = -1;
        cfg.bull_threshold = 0.008;  // Lower = more bull time
        cfg.bear_threshold = -0.02;  // Slower bear detection
        cfg.survive_down_pct = 12;
        cfg.bull_tp_pts = 400;
        cfg.atr_multiplier = 2.0;
        cfg.max_portfolio_dd = 30;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Bull-Heavy Combined", r);
        printf("  Bull=%.1f%%, Side=%.1f%% | Grid: %d, Bull: %d\n\n",
               r.time_in_bull, r.time_in_sideways, r.grid_trades, r.bull_trades);
    }

    printf("================================================================================\n");
    printf("SUMMARY\n");
    printf("================================================================================\n\n");

    printf("Combined Strategy Benefits:\n");
    printf("1. Grid mode provides steady income in sideways markets\n");
    printf("2. Bull mode captures trends with survive-based sizing\n");
    printf("3. ATR trailing locks profits before reversals\n");
    printf("4. Bear detection protects capital\n\n");

    printf("Key Parameters:\n");
    printf("- Bull threshold: 1.5-2%% (detect trend)\n");
    printf("- Grid spacing: 75-100 pts (capture oscillations)\n");
    printf("- Survive %%: 10-15%% (position sizing)\n");
    printf("- ATR multiplier: 2.0-2.5x (trailing distance)\n");
    printf("- Max DD: 20-25%% (capital protection)\n");

    printf("\n================================================================\n");
    return 0;
}
