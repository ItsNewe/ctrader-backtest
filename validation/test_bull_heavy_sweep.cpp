/**
 * Bull-Heavy Combined Strategy - Parameter Sweep
 *
 * Tests on both NAS100 and XAUUSD (Gold)
 * Sweeps all key parameters to find optimal configuration
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
    int mode;
};

enum class Regime { BULL, BEAR, SIDEWAYS };

struct SymbolParams {
    const char* name;
    const char* file;
    double contract_size;
    double leverage;
    double min_lot;
    double max_lot;
    double default_grid_spacing;
    double default_tp_pts;
};

struct Config {
    // Symbol params (set from SymbolParams)
    double contract_size = 1.0;
    double leverage = 500.0;
    double min_lot = 0.01;
    double max_lot = 100.0;

    // Regime detection
    int regime_lookback = 200000;
    double bull_threshold = 0.008;   // Bull-heavy: lower threshold
    double bear_threshold = -0.02;
    int regime_sample_rate = 1000;

    // Grid mode
    double grid_spacing = 100.0;
    double grid_lot = 0.1;
    double grid_tp_multiplier = 2.0;
    int grid_max_positions = 10;

    // Bull mode
    double survive_down_pct = 12.0;
    double bull_min_spacing = 50.0;
    double bull_tp_pts = 400.0;

    // ATR trailing
    double atr_multiplier = 2.0;
    int atr_period = 100;
    int atr_sample_rate = 10;

    // Portfolio protection
    double max_portfolio_dd = 30.0;
    double manual_stop_out = 74.0;
};

struct Result {
    double final_balance;
    double return_multiple;
    double max_dd;
    int total_trades;
    int winning_trades;
    int grid_trades;
    int bull_trades;
    double time_in_bull;
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

    std::deque<double> regime_prices;
    Regime current_regime = Regime::SIDEWAYS;

    std::deque<double> price_changes;
    double current_atr = 50.0;
    double prev_price = ticks.front().bid;

    double bull_last_open_price = -1e9;
    double grid_last_price = 0;

    size_t bull_ticks = 0;

    for (size_t i = 0; i < ticks.size(); i++) {
        const Tick& tick = ticks[i];

        // Update regime
        if (i % cfg.regime_sample_rate == 0) {
            regime_prices.push_back(tick.bid);
            if ((int)regime_prices.size() > cfg.regime_lookback / cfg.regime_sample_rate) {
                regime_prices.pop_front();
            }
            if (regime_prices.size() > 10) {
                double change = (regime_prices.back() - regime_prices.front()) / regime_prices.front();
                if (change > cfg.bull_threshold) current_regime = Regime::BULL;
                else if (change < cfg.bear_threshold) current_regime = Regime::BEAR;
                else current_regime = Regime::SIDEWAYS;
            }
        }

        if (current_regime == Regime::BULL) bull_ticks++;

        // Update ATR
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

        // Update positions
        equity = balance;
        double used_margin = 0;
        double total_lots = 0;

        for (Position* p : positions) {
            equity += (tick.bid - p->entry_price) * p->lot_size * cfg.contract_size;
            used_margin += p->lot_size * cfg.contract_size * p->entry_price / cfg.leverage;
            total_lots += p->lot_size;

            if (tick.bid > p->peak_price) p->peak_price = tick.bid;

            // ATR trailing
            double profit = tick.bid - p->entry_price;
            if (profit > current_atr * cfg.atr_multiplier) {
                double new_sl = p->peak_price - current_atr * cfg.atr_multiplier;
                if (new_sl > p->stop_loss) p->stop_loss = new_sl;
            }
        }

        double margin_level = (used_margin > 0) ? (equity / used_margin * 100.0) : 10000.0;

        // Check exits
        std::vector<Position*> to_close;
        for (Position* p : positions) {
            bool should_close = false;

            if (p->stop_loss > 0 && tick.bid <= p->stop_loss) should_close = true;
            if (p->take_profit > 0 && tick.bid >= p->take_profit) should_close = true;
            if (current_regime == Regime::BEAR && p->mode != 0) {
                double profit = tick.bid - p->entry_price;
                if (profit > 0) should_close = true;
            }

            if (should_close) to_close.push_back(p);
        }

        for (Position* p : to_close) {
            double exit_price = (p->stop_loss > 0 && tick.bid <= p->stop_loss) ? p->stop_loss :
                               (p->take_profit > 0 && tick.bid >= p->take_profit) ? p->take_profit : tick.bid;
            double pnl = (exit_price - p->entry_price) * p->lot_size * cfg.contract_size;
            balance += pnl;
            r.total_trades++;
            if (pnl > 0) r.winning_trades++;
            if (p->mode == 0) r.grid_trades++;
            else r.bull_trades++;
            positions.erase(std::remove(positions.begin(), positions.end(), p), positions.end());
            delete p;
        }

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

        // Portfolio protection
        if (positions.empty()) peak_equity = balance;
        if (equity > peak_equity) peak_equity = equity;
        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        if (dd_pct > max_drawdown) max_drawdown = dd_pct;

        if (dd_pct >= cfg.max_portfolio_dd && !positions.empty()) {
            for (Position* p : positions) {
                balance += (tick.bid - p->entry_price) * p->lot_size * cfg.contract_size;
                r.total_trades++;
                delete p;
            }
            positions.clear();
            equity = balance;
            peak_equity = balance;
            bull_last_open_price = -1e9;
            continue;
        }

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

        if (margin_level < cfg.manual_stop_out && margin_level > 0 && !positions.empty()) {
            for (Position* p : positions) {
                balance += (tick.bid - p->entry_price) * p->lot_size * cfg.contract_size;
                r.total_trades++;
                delete p;
            }
            positions.clear();
            equity = balance;
            peak_equity = balance;
            bull_last_open_price = -1e9;
            continue;
        }

        // Open positions
        int grid_positions = 0, bull_positions = 0;
        for (Position* p : positions) {
            if (p->mode == 0) grid_positions++;
            else bull_positions++;
        }

        // SIDEWAYS: Grid
        if (current_regime == Regime::SIDEWAYS) {
            if (grid_positions < cfg.grid_max_positions) {
                bool should_open = (grid_positions == 0) ||
                                  (grid_last_price > 0 && std::abs(tick.ask - grid_last_price) >= cfg.grid_spacing);

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

        // BULL: Survive-based
        else if (current_regime == Regime::BULL) {
            if (tick.ask > bull_last_open_price + cfg.bull_min_spacing) {
                double survive_drop = tick.ask * cfg.survive_down_pct / 100.0;
                double spread_cost = tick.spread() * cfg.contract_size;
                double lot = equity / (survive_drop + spread_cost + tick.ask / cfg.leverage * (100 - cfg.manual_stop_out) / 100);
                lot = lot / cfg.contract_size;

                if (bull_positions > 0) lot = lot / (bull_positions + 1);

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
                }
            }
        }
    }

    for (Position* p : positions) {
        balance += (ticks.back().bid - p->entry_price) * p->lot_size * cfg.contract_size;
        r.total_trades++;
        delete p;
    }

    r.final_balance = std::max(0.0, balance);
    r.return_multiple = r.final_balance / 10000.0;
    r.max_dd = max_drawdown;
    r.time_in_bull = (double)bull_ticks / ticks.size() * 100;
    return r;
}

void PrintResult(const char* name, Result r) {
    printf("%-50s $%10.2f %6.2fx %6.1f%% %5d %5.1f%% %s\n",
           name, r.final_balance, r.return_multiple, r.max_dd,
           r.total_trades,
           r.total_trades > 0 ? (double)r.winning_trades / r.total_trades * 100 : 0,
           r.margin_call ? "MC" : "");
}

void RunSweep(const char* symbol_name, const std::vector<Tick>& ticks, SymbolParams sp) {
    printf("\n================================================================================\n");
    printf("%s PARAMETER SWEEP\n", symbol_name);
    printf("================================================================================\n");
    printf("Ticks: %zu | Price: %.2f -> %.2f (%+.1f%%)\n",
           ticks.size(), ticks.front().bid, ticks.back().bid,
           (ticks.back().bid - ticks.front().bid) / ticks.front().bid * 100);
    printf("Contract: %.2f | Leverage: %.0f\n\n", sp.contract_size, sp.leverage);

    printf("%-50s %12s %7s %7s %5s %6s\n",
           "Configuration", "Final", "Return", "MaxDD", "Trade", "Win%");
    printf("--------------------------------------------------------------------------------\n");

    // Baseline Bull-Heavy
    {
        Config cfg;
        cfg.contract_size = sp.contract_size;
        cfg.leverage = sp.leverage;
        cfg.min_lot = sp.min_lot;
        cfg.max_lot = sp.max_lot;
        cfg.grid_spacing = sp.default_grid_spacing;
        cfg.bull_tp_pts = sp.default_tp_pts;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Baseline Bull-Heavy", r);
    }
    printf("\n");

    // === BULL THRESHOLD ===
    printf("--- Bull Threshold ---\n");
    double bull_thresholds[] = {0.005, 0.008, 0.01, 0.012, 0.015, 0.02};
    for (double bt : bull_thresholds) {
        Config cfg;
        cfg.contract_size = sp.contract_size;
        cfg.leverage = sp.leverage;
        cfg.min_lot = sp.min_lot;
        cfg.max_lot = sp.max_lot;
        cfg.grid_spacing = sp.default_grid_spacing;
        cfg.bull_tp_pts = sp.default_tp_pts;
        cfg.bull_threshold = bt;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "Bull > %.1f%%", bt * 100);
        PrintResult(name, r);
    }
    printf("\n");

    // === BEAR THRESHOLD ===
    printf("--- Bear Threshold ---\n");
    double bear_thresholds[] = {-0.01, -0.015, -0.02, -0.025, -0.03};
    for (double bt : bear_thresholds) {
        Config cfg;
        cfg.contract_size = sp.contract_size;
        cfg.leverage = sp.leverage;
        cfg.min_lot = sp.min_lot;
        cfg.max_lot = sp.max_lot;
        cfg.grid_spacing = sp.default_grid_spacing;
        cfg.bull_tp_pts = sp.default_tp_pts;
        cfg.bear_threshold = bt;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "Bear < %.1f%%", bt * 100);
        PrintResult(name, r);
    }
    printf("\n");

    // === REGIME LOOKBACK ===
    printf("--- Regime Lookback ---\n");
    int lookbacks[] = {50000, 100000, 200000, 400000, 800000};
    for (int lb : lookbacks) {
        Config cfg;
        cfg.contract_size = sp.contract_size;
        cfg.leverage = sp.leverage;
        cfg.min_lot = sp.min_lot;
        cfg.max_lot = sp.max_lot;
        cfg.grid_spacing = sp.default_grid_spacing;
        cfg.bull_tp_pts = sp.default_tp_pts;
        cfg.regime_lookback = lb;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "Lookback = %dk ticks", lb / 1000);
        PrintResult(name, r);
    }
    printf("\n");

    // === GRID SPACING ===
    printf("--- Grid Spacing ---\n");
    double grid_spacings[6];
    double base_spacing = sp.default_grid_spacing;
    grid_spacings[0] = base_spacing * 0.25;
    grid_spacings[1] = base_spacing * 0.5;
    grid_spacings[2] = base_spacing * 0.75;
    grid_spacings[3] = base_spacing;
    grid_spacings[4] = base_spacing * 1.5;
    grid_spacings[5] = base_spacing * 2.0;

    for (double gs : grid_spacings) {
        Config cfg;
        cfg.contract_size = sp.contract_size;
        cfg.leverage = sp.leverage;
        cfg.min_lot = sp.min_lot;
        cfg.max_lot = sp.max_lot;
        cfg.grid_spacing = gs;
        cfg.bull_tp_pts = sp.default_tp_pts;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "Grid Spacing = %.1f", gs);
        PrintResult(name, r);
    }
    printf("\n");

    // === GRID LOT SIZE ===
    printf("--- Grid Lot Size ---\n");
    double grid_lots[] = {0.05, 0.1, 0.15, 0.2, 0.3};
    for (double gl : grid_lots) {
        Config cfg;
        cfg.contract_size = sp.contract_size;
        cfg.leverage = sp.leverage;
        cfg.min_lot = sp.min_lot;
        cfg.max_lot = sp.max_lot;
        cfg.grid_spacing = sp.default_grid_spacing;
        cfg.bull_tp_pts = sp.default_tp_pts;
        cfg.grid_lot = gl;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "Grid Lot = %.2f", gl);
        PrintResult(name, r);
    }
    printf("\n");

    // === GRID TP MULTIPLIER ===
    printf("--- Grid TP Multiplier ---\n");
    double tp_mults[] = {1.5, 2.0, 2.5, 3.0, 4.0};
    for (double tm : tp_mults) {
        Config cfg;
        cfg.contract_size = sp.contract_size;
        cfg.leverage = sp.leverage;
        cfg.min_lot = sp.min_lot;
        cfg.max_lot = sp.max_lot;
        cfg.grid_spacing = sp.default_grid_spacing;
        cfg.bull_tp_pts = sp.default_tp_pts;
        cfg.grid_tp_multiplier = tm;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "Grid TP = %.1fx spacing", tm);
        PrintResult(name, r);
    }
    printf("\n");

    // === SURVIVE PERCENTAGE ===
    printf("--- Survive Down %% ---\n");
    double survives[] = {5, 8, 10, 12, 15, 20};
    for (double sv : survives) {
        Config cfg;
        cfg.contract_size = sp.contract_size;
        cfg.leverage = sp.leverage;
        cfg.min_lot = sp.min_lot;
        cfg.max_lot = sp.max_lot;
        cfg.grid_spacing = sp.default_grid_spacing;
        cfg.bull_tp_pts = sp.default_tp_pts;
        cfg.survive_down_pct = sv;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "Survive = %.0f%%", sv);
        PrintResult(name, r);
    }
    printf("\n");

    // === BULL MIN SPACING ===
    printf("--- Bull Min Spacing ---\n");
    double bull_spacings[5];
    bull_spacings[0] = sp.default_grid_spacing * 0.25;
    bull_spacings[1] = sp.default_grid_spacing * 0.5;
    bull_spacings[2] = sp.default_grid_spacing * 0.75;
    bull_spacings[3] = sp.default_grid_spacing;
    bull_spacings[4] = sp.default_grid_spacing * 1.5;

    for (double bs : bull_spacings) {
        Config cfg;
        cfg.contract_size = sp.contract_size;
        cfg.leverage = sp.leverage;
        cfg.min_lot = sp.min_lot;
        cfg.max_lot = sp.max_lot;
        cfg.grid_spacing = sp.default_grid_spacing;
        cfg.bull_tp_pts = sp.default_tp_pts;
        cfg.bull_min_spacing = bs;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "Bull Spacing = %.1f", bs);
        PrintResult(name, r);
    }
    printf("\n");

    // === BULL TP ===
    printf("--- Bull Take Profit ---\n");
    double bull_tps[5];
    bull_tps[0] = sp.default_tp_pts * 0.5;
    bull_tps[1] = sp.default_tp_pts * 0.75;
    bull_tps[2] = sp.default_tp_pts;
    bull_tps[3] = sp.default_tp_pts * 1.5;
    bull_tps[4] = sp.default_tp_pts * 2.0;

    for (double tp : bull_tps) {
        Config cfg;
        cfg.contract_size = sp.contract_size;
        cfg.leverage = sp.leverage;
        cfg.min_lot = sp.min_lot;
        cfg.max_lot = sp.max_lot;
        cfg.grid_spacing = sp.default_grid_spacing;
        cfg.bull_tp_pts = tp;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "Bull TP = %.0f pts", tp);
        PrintResult(name, r);
    }
    printf("\n");

    // === ATR MULTIPLIER ===
    printf("--- ATR Trailing Multiplier ---\n");
    double atr_mults[] = {1.0, 1.5, 2.0, 2.5, 3.0, 4.0};
    for (double am : atr_mults) {
        Config cfg;
        cfg.contract_size = sp.contract_size;
        cfg.leverage = sp.leverage;
        cfg.min_lot = sp.min_lot;
        cfg.max_lot = sp.max_lot;
        cfg.grid_spacing = sp.default_grid_spacing;
        cfg.bull_tp_pts = sp.default_tp_pts;
        cfg.atr_multiplier = am;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "ATR x %.1f", am);
        PrintResult(name, r);
    }
    printf("\n");

    // === MAX DD ===
    printf("--- Max Portfolio DD ---\n");
    double max_dds[] = {15, 20, 25, 30, 40, 50};
    for (double dd : max_dds) {
        Config cfg;
        cfg.contract_size = sp.contract_size;
        cfg.leverage = sp.leverage;
        cfg.min_lot = sp.min_lot;
        cfg.max_lot = sp.max_lot;
        cfg.grid_spacing = sp.default_grid_spacing;
        cfg.bull_tp_pts = sp.default_tp_pts;
        cfg.max_portfolio_dd = dd;
        Result r = RunStrategy(ticks, cfg);
        char name[64];
        snprintf(name, sizeof(name), "Max DD = %.0f%%", dd);
        PrintResult(name, r);
    }
    printf("\n");

    // === BEST COMBINATIONS ===
    printf("================================================================================\n");
    printf("BEST COMBINATIONS FOR %s\n", symbol_name);
    printf("================================================================================\n\n");

    // Conservative
    {
        Config cfg;
        cfg.contract_size = sp.contract_size;
        cfg.leverage = sp.leverage;
        cfg.min_lot = sp.min_lot;
        cfg.max_lot = sp.max_lot;
        cfg.bull_threshold = 0.012;
        cfg.bear_threshold = -0.015;
        cfg.grid_spacing = sp.default_grid_spacing;
        cfg.grid_lot = 0.05;
        cfg.survive_down_pct = 15;
        cfg.bull_tp_pts = sp.default_tp_pts;
        cfg.atr_multiplier = 2.5;
        cfg.max_portfolio_dd = 20;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Conservative", r);
    }

    // Balanced
    {
        Config cfg;
        cfg.contract_size = sp.contract_size;
        cfg.leverage = sp.leverage;
        cfg.min_lot = sp.min_lot;
        cfg.max_lot = sp.max_lot;
        cfg.bull_threshold = 0.008;
        cfg.bear_threshold = -0.02;
        cfg.grid_spacing = sp.default_grid_spacing;
        cfg.grid_lot = 0.1;
        cfg.survive_down_pct = 12;
        cfg.bull_tp_pts = sp.default_tp_pts;
        cfg.atr_multiplier = 2.0;
        cfg.max_portfolio_dd = 30;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Balanced", r);
    }

    // Aggressive
    {
        Config cfg;
        cfg.contract_size = sp.contract_size;
        cfg.leverage = sp.leverage;
        cfg.min_lot = sp.min_lot;
        cfg.max_lot = sp.max_lot;
        cfg.bull_threshold = 0.005;
        cfg.bear_threshold = -0.025;
        cfg.grid_spacing = sp.default_grid_spacing * 0.75;
        cfg.grid_lot = 0.15;
        cfg.survive_down_pct = 10;
        cfg.bull_tp_pts = sp.default_tp_pts * 1.5;
        cfg.atr_multiplier = 1.5;
        cfg.max_portfolio_dd = 40;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Aggressive", r);
    }

    // Max Return
    {
        Config cfg;
        cfg.contract_size = sp.contract_size;
        cfg.leverage = sp.leverage;
        cfg.min_lot = sp.min_lot;
        cfg.max_lot = sp.max_lot;
        cfg.bull_threshold = 0.005;
        cfg.bear_threshold = -0.03;
        cfg.grid_spacing = sp.default_grid_spacing * 0.5;
        cfg.grid_lot = 0.2;
        cfg.survive_down_pct = 8;
        cfg.bull_tp_pts = sp.default_tp_pts * 2.0;
        cfg.atr_multiplier = 1.5;
        cfg.max_portfolio_dd = 50;
        Result r = RunStrategy(ticks, cfg);
        PrintResult("Max Return", r);
    }
}

int main() {
    printf("================================================================\n");
    printf("BULL-HEAVY STRATEGY - MULTI-SYMBOL PARAMETER SWEEP\n");
    printf("================================================================\n");

    // NAS100 parameters
    SymbolParams nas100 = {
        "NAS100",
        "NAS100/NAS100_TICKS_2025.csv",
        1.0,      // contract_size
        500.0,    // leverage
        0.01,     // min_lot
        100.0,    // max_lot
        100.0,    // default_grid_spacing (points)
        400.0     // default_tp_pts
    };

    // Gold (XAUUSD) parameters
    SymbolParams gold = {
        "XAUUSD",
        "XAUUSD_TICKS_2025.csv",
        100.0,    // contract_size (100 oz per lot)
        500.0,    // leverage
        0.01,     // min_lot
        100.0,    // max_lot
        5.0,      // default_grid_spacing ($5 for gold)
        20.0      // default_tp_pts ($20 for gold)
    };

    // Load NAS100
    printf("\nLoading NAS100 data...\n");
    std::vector<Tick> nas100_ticks = LoadTicks(nas100.file, 60000000);

    if (!nas100_ticks.empty()) {
        RunSweep("NAS100", nas100_ticks, nas100);
    } else {
        printf("ERROR: Could not load NAS100 data from %s\n", nas100.file);
    }

    // Load Gold
    printf("\nLoading XAUUSD data...\n");
    std::vector<Tick> gold_ticks = LoadTicks(gold.file, 60000000);

    if (!gold_ticks.empty()) {
        RunSweep("XAUUSD (Gold)", gold_ticks, gold);
    } else {
        printf("Note: Gold data not found at %s\n", gold.file);
        printf("Trying alternative path...\n");

        // Try alternative paths
        const char* alt_paths[] = {
            "XAUUSD_TICKS_2025.csv",
            "../XAUUSD/XAUUSD_TICKS_2025.csv",
            "gold/XAUUSD_TICKS_2025.csv"
        };

        for (const char* path : alt_paths) {
            gold_ticks = LoadTicks(path, 60000000);
            if (!gold_ticks.empty()) {
                gold.file = path;
                RunSweep("XAUUSD (Gold)", gold_ticks, gold);
                break;
            }
        }

        if (gold_ticks.empty()) {
            printf("Gold data not found. Skipping XAUUSD test.\n");
        }
    }

    printf("\n================================================================\n");
    printf("SWEEP COMPLETE\n");
    printf("================================================================\n");

    return 0;
}
