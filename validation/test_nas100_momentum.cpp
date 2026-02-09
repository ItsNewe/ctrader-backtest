/**
 * NAS100 Momentum-Based Strategy
 *
 * Tests momentum filtering approach for trend following:
 * 1. Track price momentum (rate of change over N ticks)
 * 2. Only open positions when momentum is positive and accelerating
 * 3. Use momentum-based position sizing (larger when momentum strong)
 * 4. Close positions when momentum reverses
 *
 * Compare to buy-and-hold benchmark on NAS100 data (+23.7% uptrend)
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cfloat>
#include <vector>
#include <deque>
#include <algorithm>
#include <chrono>

struct Tick {
    double bid;
    double ask;
    int hour;
    double spread() const { return ask - bid; }
    double mid() const { return (bid + ask) / 2.0; }
};

struct Position {
    size_t id;
    double entry_price;
    double lot_size;
    double entry_momentum;       // Momentum when position was opened
    double stop_loss;            // Momentum-based stop
};

// Momentum calculator - tracks rate of change over N ticks
class MomentumCalculator {
    std::deque<double> prices;
    int period;

public:
    MomentumCalculator(int p) : period(p) {}

    void Reset() {
        prices.clear();
    }

    void Add(double price) {
        prices.push_back(price);
        if ((int)prices.size() > period) {
            prices.pop_front();
        }
    }

    bool IsReady() const {
        return (int)prices.size() >= period;
    }

    // Momentum as percentage change over period
    double GetMomentum() const {
        if (prices.size() < 2) return 0.0;
        double oldest = prices.front();
        double newest = prices.back();
        if (oldest <= 0) return 0.0;
        return (newest - oldest) / oldest * 100.0;
    }

    // Acceleration = change in momentum (2nd derivative)
    // Positive = momentum increasing, negative = momentum decreasing
    double GetAcceleration() const {
        if (prices.size() < (size_t)(period / 2 + 2)) return 0.0;

        // Compare momentum of first half vs second half
        size_t half = prices.size() / 2;
        double first_start = prices[0];
        double first_end = prices[half];
        double second_start = prices[half];
        double second_end = prices.back();

        if (first_start <= 0 || second_start <= 0) return 0.0;

        double mom1 = (first_end - first_start) / first_start * 100.0;
        double mom2 = (second_end - second_start) / second_start * 100.0;

        return mom2 - mom1;  // Positive = accelerating
    }

    double GetCurrentPrice() const {
        return prices.empty() ? 0.0 : prices.back();
    }
};

struct Config {
    int momentum_period = 5000;          // Ticks for momentum calculation
    double momentum_threshold = 0.5;     // Min momentum % to open position
    double acceleration_threshold = 0.0; // Min acceleration to open
    double lot_base = 0.1;               // Base lot size
    double lot_momentum_scale = 0.5;     // Additional lots per 1% momentum
    double max_lot = 0.5;                // Max lot size
    double stop_momentum_reversal = -0.2;// Close when momentum drops to this %
    int max_positions = 5;               // Max concurrent positions
    double add_threshold = 1.0;          // Momentum above this to add position
    double close_all_dd = 15.0;          // Emergency close at DD%
};

struct Result {
    double final_balance;
    double return_pct;
    double max_dd;
    int total_trades;
    int winning_trades;
    int max_positions_held;
    bool margin_call;
    double avg_holding_time;     // Average ticks holding position
    double profit_factor;
};

std::vector<Tick> LoadTicks(const char* filename, size_t max_count) {
    std::vector<Tick> ticks;
    ticks.reserve(max_count);
    FILE* f = fopen(filename, "r");
    if (!f) return ticks;
    char line[256];
    fgets(line, sizeof(line), f);  // Skip header
    while (fgets(line, sizeof(line), f) && ticks.size() < max_count) {
        Tick tick;
        tick.hour = 12;
        if (strlen(line) >= 13) {
            char h[3] = {line[11], line[12], 0};
            tick.hour = atoi(h);
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

Result RunMomentumStrategy(const std::vector<Tick>& ticks, Config cfg) {
    Result r = {};
    const double initial_balance = 10000.0;
    const double contract_size = 1.0;
    const double leverage = 500.0;

    double balance = initial_balance;
    double equity = balance;
    double peak_equity = balance;
    double max_drawdown = 0;

    std::vector<Position*> positions;
    size_t next_id = 1;

    MomentumCalculator mom(cfg.momentum_period);

    double total_gross_profit = 0;
    double total_gross_loss = 0;
    size_t total_holding_ticks = 0;

    size_t warmup = cfg.momentum_period;
    size_t tick_count = 0;
    size_t last_entry_tick = 0;

    for (const Tick& tick : ticks) {
        tick_count++;
        mom.Add(tick.mid());

        // Warmup period
        if (tick_count < warmup) continue;

        // Calculate equity
        equity = balance;
        double used_margin = 0;
        for (Position* p : positions) {
            equity += (tick.bid - p->entry_price) * p->lot_size * contract_size;
            used_margin += p->lot_size * contract_size * p->entry_price / leverage;
        }

        r.max_positions_held = std::max(r.max_positions_held, (int)positions.size());

        // Margin call check
        if (used_margin > 0 && equity < used_margin * 0.2) {
            r.margin_call = true;
            for (Position* p : positions) {
                double pl = (tick.bid - p->entry_price) * p->lot_size * contract_size;
                balance += pl;
                if (pl > 0) { total_gross_profit += pl; r.winning_trades++; }
                else total_gross_loss += std::abs(pl);
                r.total_trades++;
                delete p;
            }
            positions.clear();
            equity = balance;
            if (balance <= 0) break;
            peak_equity = balance;
            continue;
        }

        // Track drawdown
        if (equity > peak_equity) peak_equity = equity;
        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        if (dd_pct > max_drawdown) max_drawdown = dd_pct;

        // Emergency close
        if (dd_pct > cfg.close_all_dd && !positions.empty()) {
            for (Position* p : positions) {
                double pl = (tick.bid - p->entry_price) * p->lot_size * contract_size;
                balance += pl;
                if (pl > 0) { total_gross_profit += pl; r.winning_trades++; }
                else total_gross_loss += std::abs(pl);
                total_holding_ticks += (tick_count - p->id);  // id stores entry tick
                r.total_trades++;
                delete p;
            }
            positions.clear();
            equity = balance;
            peak_equity = balance;
            continue;
        }

        double momentum = mom.GetMomentum();
        double acceleration = mom.GetAcceleration();

        // CLOSE LOGIC: Momentum reversal
        for (auto it = positions.begin(); it != positions.end();) {
            Position* p = *it;
            bool should_close = false;

            // Close if momentum reversed significantly below entry momentum
            if (momentum < cfg.stop_momentum_reversal) {
                should_close = true;
            }
            // Close if momentum dropped significantly from entry
            else if (momentum < p->entry_momentum * 0.3 && p->entry_momentum > 0) {
                should_close = true;
            }

            if (should_close) {
                double pl = (tick.bid - p->entry_price) * p->lot_size * contract_size;
                balance += pl;
                if (pl > 0) { total_gross_profit += pl; r.winning_trades++; }
                else total_gross_loss += std::abs(pl);
                total_holding_ticks += (tick_count - p->id);
                r.total_trades++;
                delete p;
                it = positions.erase(it);
            } else {
                ++it;
            }
        }

        // ENTRY LOGIC: Momentum is positive and accelerating
        bool momentum_ok = momentum > cfg.momentum_threshold;
        bool acceleration_ok = acceleration > cfg.acceleration_threshold;
        bool under_limit = (int)positions.size() < cfg.max_positions;
        bool cooldown_ok = (tick_count - last_entry_tick) > (size_t)(cfg.momentum_period / 10);

        // Add to position if momentum even stronger
        bool add_ok = !positions.empty() && momentum > cfg.add_threshold &&
                      momentum > positions.back()->entry_momentum * 1.2;

        if ((momentum_ok && acceleration_ok && under_limit && cooldown_ok) ||
            (add_ok && under_limit && cooldown_ok)) {

            // Momentum-based position sizing
            double lot_size = cfg.lot_base;
            if (momentum > 0) {
                lot_size += momentum * cfg.lot_momentum_scale;
            }
            lot_size = std::min(lot_size, cfg.max_lot);
            lot_size = std::max(lot_size, cfg.lot_base);

            double margin_needed = lot_size * contract_size * tick.ask / leverage;
            double free_margin = equity - used_margin;

            if (free_margin > margin_needed * 1.5) {
                Position* p = new Position();
                p->id = tick_count;  // Store entry tick for holding time calc
                p->entry_price = tick.ask;
                p->lot_size = lot_size;
                p->entry_momentum = momentum;
                p->stop_loss = tick.ask * (1.0 - 0.02);  // 2% stop
                positions.push_back(p);
                last_entry_tick = tick_count;
            }
        }
    }

    // Close remaining positions
    for (Position* p : positions) {
        double pl = (ticks.back().bid - p->entry_price) * p->lot_size * contract_size;
        balance += pl;
        if (pl > 0) { total_gross_profit += pl; r.winning_trades++; }
        else total_gross_loss += std::abs(pl);
        total_holding_ticks += (tick_count - p->id);
        r.total_trades++;
        delete p;
    }

    r.final_balance = std::max(0.0, balance);
    r.return_pct = (r.final_balance - initial_balance) / initial_balance * 100.0;
    r.max_dd = max_drawdown;
    r.avg_holding_time = r.total_trades > 0 ? (double)total_holding_ticks / r.total_trades : 0;
    r.profit_factor = total_gross_loss > 0 ? total_gross_profit / total_gross_loss :
                      (total_gross_profit > 0 ? 999.0 : 0.0);

    return r;
}

// Buy and hold benchmark
Result RunBuyAndHold(const std::vector<Tick>& ticks, double lot_size) {
    Result r = {};
    const double initial_balance = 10000.0;
    const double contract_size = 1.0;
    const double leverage = 500.0;

    double entry_price = ticks.front().ask;
    double exit_price = ticks.back().bid;

    // Calculate position size based on available margin
    double max_lot = (initial_balance * leverage * 0.5) / (entry_price * contract_size);
    double actual_lot = std::min(lot_size, max_lot);

    double pl = (exit_price - entry_price) * actual_lot * contract_size;

    // Track max drawdown
    double peak_equity = initial_balance;
    double max_dd = 0;

    for (const Tick& tick : ticks) {
        double equity = initial_balance + (tick.bid - entry_price) * actual_lot * contract_size;
        if (equity > peak_equity) peak_equity = equity;
        double dd = (peak_equity - equity) / peak_equity * 100.0;
        if (dd > max_dd) max_dd = dd;
    }

    r.final_balance = initial_balance + pl;
    r.return_pct = (r.final_balance - initial_balance) / initial_balance * 100.0;
    r.max_dd = max_dd;
    r.total_trades = 1;
    r.winning_trades = pl > 0 ? 1 : 0;
    r.profit_factor = pl > 0 ? 999.0 : 0.0;

    return r;
}

int main() {
    auto start_time = std::chrono::high_resolution_clock::now();

    printf("================================================================\n");
    printf("NAS100 MOMENTUM-BASED STRATEGY TEST\n");
    printf("================================================================\n\n");

    std::vector<Tick> ticks = LoadTicks("NAS100/NAS100_TICKS_2025.csv", 60000000);
    if (ticks.empty()) {
        printf("ERROR: No data loaded\n");
        return 1;
    }

    double price_change = (ticks.back().bid - ticks.front().bid) / ticks.front().bid * 100;
    printf("Loaded %zu ticks\n", ticks.size());
    printf("Price: %.2f -> %.2f (%+.1f%%)\n\n",
           ticks.front().bid, ticks.back().bid, price_change);

    // ====================================================================
    // BUY AND HOLD BENCHMARK
    // ====================================================================
    printf("BUY AND HOLD BENCHMARK\n");
    printf("----------------------------------------------------------------\n");

    double bench_lots[] = {0.1, 0.2, 0.3, 0.5};
    printf("%-10s %12s %10s %10s\n", "Lot Size", "Final", "Return%", "MaxDD%");
    printf("----------------------------------------------------------------\n");
    for (double lot : bench_lots) {
        Result r = RunBuyAndHold(ticks, lot);
        printf("%-10.2f $%11.2f %9.1f%% %9.1f%%\n",
               lot, r.final_balance, r.return_pct, r.max_dd);
    }

    // ====================================================================
    // TEST 1: MOMENTUM PERIOD SWEEP
    // ====================================================================
    printf("\n================================================================\n");
    printf("TEST 1: MOMENTUM PERIOD SWEEP (threshold=0.5%%)\n");
    printf("================================================================\n");
    printf("%-12s %12s %10s %10s %8s %8s %10s\n",
           "Period", "Final", "Return%", "MaxDD%", "Trades", "WinRate", "PF");
    printf("------------------------------------------------------------------------\n");

    int periods[] = {1000, 5000, 10000, 50000};
    for (int period : periods) {
        Config cfg;
        cfg.momentum_period = period;
        cfg.momentum_threshold = 0.5;
        Result r = RunMomentumStrategy(ticks, cfg);
        double wr = r.total_trades > 0 ? r.winning_trades * 100.0 / r.total_trades : 0;
        printf("%-12d $%11.2f %9.1f%% %9.1f%% %8d %7.1f%% %9.2f %s\n",
               period, r.final_balance, r.return_pct, r.max_dd, r.total_trades, wr,
               r.profit_factor, r.margin_call ? "MC" : "");
    }

    // ====================================================================
    // TEST 2: MOMENTUM THRESHOLD SWEEP
    // ====================================================================
    printf("\n================================================================\n");
    printf("TEST 2: MOMENTUM THRESHOLD SWEEP (period=5000)\n");
    printf("================================================================\n");
    printf("%-12s %12s %10s %10s %8s %8s %10s\n",
           "Threshold", "Final", "Return%", "MaxDD%", "Trades", "WinRate", "PF");
    printf("------------------------------------------------------------------------\n");

    double thresholds[] = {0.1, 0.5, 1.0, 2.0};
    for (double thresh : thresholds) {
        Config cfg;
        cfg.momentum_period = 5000;
        cfg.momentum_threshold = thresh;
        Result r = RunMomentumStrategy(ticks, cfg);
        double wr = r.total_trades > 0 ? r.winning_trades * 100.0 / r.total_trades : 0;
        printf("%-12.1f%% $%10.2f %9.1f%% %9.1f%% %8d %7.1f%% %9.2f %s\n",
               thresh, r.final_balance, r.return_pct, r.max_dd, r.total_trades, wr,
               r.profit_factor, r.margin_call ? "MC" : "");
    }

    // ====================================================================
    // TEST 3: COMBINED PARAMETER SWEEP
    // ====================================================================
    printf("\n================================================================\n");
    printf("TEST 3: FULL PARAMETER SWEEP (Period x Threshold)\n");
    printf("================================================================\n");
    printf("%-8s %-8s %12s %10s %10s %8s %8s\n",
           "Period", "Thresh", "Final", "Return%", "MaxDD%", "Trades", "WinRate");
    printf("------------------------------------------------------------------------\n");

    double best_return = -1e9;
    int best_period = 0;
    double best_thresh = 0;
    Result best_result = {};

    for (int period : periods) {
        for (double thresh : thresholds) {
            Config cfg;
            cfg.momentum_period = period;
            cfg.momentum_threshold = thresh;
            Result r = RunMomentumStrategy(ticks, cfg);
            double wr = r.total_trades > 0 ? r.winning_trades * 100.0 / r.total_trades : 0;

            printf("%-8d %-7.1f%% $%11.2f %9.1f%% %9.1f%% %8d %7.1f%% %s\n",
                   period, thresh, r.final_balance, r.return_pct, r.max_dd,
                   r.total_trades, wr, r.margin_call ? "MC" : "");

            // Track best by return (non-margin-called)
            if (r.return_pct > best_return && !r.margin_call) {
                best_return = r.return_pct;
                best_period = period;
                best_thresh = thresh;
                best_result = r;
            }
        }
    }

    // ====================================================================
    // TEST 4: POSITION SIZING VARIATIONS
    // ====================================================================
    printf("\n================================================================\n");
    printf("TEST 4: MOMENTUM-BASED SIZING (period=%d, thresh=%.1f%%)\n",
           best_period, best_thresh);
    printf("================================================================\n");
    printf("%-10s %-10s %12s %10s %10s %8s\n",
           "Base Lot", "Scale", "Final", "Return%", "MaxDD%", "Trades");
    printf("------------------------------------------------------------------------\n");

    double base_lots[] = {0.05, 0.1, 0.15, 0.2};
    double scales[] = {0.0, 0.25, 0.5, 1.0};

    for (double base : base_lots) {
        for (double scale : scales) {
            Config cfg;
            cfg.momentum_period = best_period > 0 ? best_period : 5000;
            cfg.momentum_threshold = best_thresh > 0 ? best_thresh : 0.5;
            cfg.lot_base = base;
            cfg.lot_momentum_scale = scale;
            cfg.max_lot = 0.5;

            Result r = RunMomentumStrategy(ticks, cfg);
            printf("%-10.2f %-10.2f $%11.2f %9.1f%% %9.1f%% %8d %s\n",
                   base, scale, r.final_balance, r.return_pct, r.max_dd,
                   r.total_trades, r.margin_call ? "MC" : "");
        }
    }

    // ====================================================================
    // TEST 5: ACCELERATION FILTER
    // ====================================================================
    printf("\n================================================================\n");
    printf("TEST 5: ACCELERATION FILTER (period=%d, thresh=%.1f%%)\n",
           best_period > 0 ? best_period : 5000, best_thresh > 0 ? best_thresh : 0.5);
    printf("================================================================\n");
    printf("%-15s %12s %10s %10s %8s %8s\n",
           "Accel Thresh", "Final", "Return%", "MaxDD%", "Trades", "WinRate");
    printf("------------------------------------------------------------------------\n");

    double accel_thresholds[] = {-0.5, 0.0, 0.1, 0.2, 0.5};
    for (double accel : accel_thresholds) {
        Config cfg;
        cfg.momentum_period = best_period > 0 ? best_period : 5000;
        cfg.momentum_threshold = best_thresh > 0 ? best_thresh : 0.5;
        cfg.acceleration_threshold = accel;

        Result r = RunMomentumStrategy(ticks, cfg);
        double wr = r.total_trades > 0 ? r.winning_trades * 100.0 / r.total_trades : 0;
        printf("%-15.2f $%11.2f %9.1f%% %9.1f%% %8d %7.1f%% %s\n",
               accel, r.final_balance, r.return_pct, r.max_dd,
               r.total_trades, wr, r.margin_call ? "MC" : "");
    }

    // ====================================================================
    // TEST 6: EXIT MOMENTUM THRESHOLD
    // ====================================================================
    printf("\n================================================================\n");
    printf("TEST 6: EXIT MOMENTUM THRESHOLD\n");
    printf("================================================================\n");
    printf("%-15s %12s %10s %10s %8s %10s\n",
           "Exit Mom", "Final", "Return%", "MaxDD%", "Trades", "AvgHold");
    printf("------------------------------------------------------------------------\n");

    double exit_moms[] = {-1.0, -0.5, -0.2, 0.0, 0.2};
    for (double exit_mom : exit_moms) {
        Config cfg;
        cfg.momentum_period = best_period > 0 ? best_period : 5000;
        cfg.momentum_threshold = best_thresh > 0 ? best_thresh : 0.5;
        cfg.stop_momentum_reversal = exit_mom;

        Result r = RunMomentumStrategy(ticks, cfg);
        printf("%-15.2f $%11.2f %9.1f%% %9.1f%% %8d %9.0f %s\n",
               exit_mom, r.final_balance, r.return_pct, r.max_dd,
               r.total_trades, r.avg_holding_time, r.margin_call ? "MC" : "");
    }

    // ====================================================================
    // FINAL COMPARISON
    // ====================================================================
    printf("\n================================================================\n");
    printf("FINAL COMPARISON: BEST CONFIG VS BENCHMARK\n");
    printf("================================================================\n");

    // Best momentum config
    Config final_cfg;
    final_cfg.momentum_period = best_period > 0 ? best_period : 5000;
    final_cfg.momentum_threshold = best_thresh > 0 ? best_thresh : 0.5;
    final_cfg.acceleration_threshold = 0.0;
    final_cfg.lot_base = 0.1;
    final_cfg.lot_momentum_scale = 0.5;
    final_cfg.stop_momentum_reversal = -0.2;

    Result mom_result = RunMomentumStrategy(ticks, final_cfg);
    Result bh_result = RunBuyAndHold(ticks, 0.2);

    printf("\nBuy-and-Hold (0.2 lot):\n");
    printf("  Final Balance: $%.2f\n", bh_result.final_balance);
    printf("  Return:        %+.1f%%\n", bh_result.return_pct);
    printf("  Max Drawdown:  %.1f%%\n", bh_result.max_dd);

    printf("\nMomentum Strategy (period=%d, threshold=%.1f%%):\n",
           final_cfg.momentum_period, final_cfg.momentum_threshold);
    printf("  Final Balance: $%.2f\n", mom_result.final_balance);
    printf("  Return:        %+.1f%%\n", mom_result.return_pct);
    printf("  Max Drawdown:  %.1f%%\n", mom_result.max_dd);
    printf("  Trades:        %d (win rate: %.1f%%)\n",
           mom_result.total_trades,
           mom_result.total_trades > 0 ? mom_result.winning_trades * 100.0 / mom_result.total_trades : 0);
    printf("  Profit Factor: %.2f\n", mom_result.profit_factor);
    printf("  Margin Call:   %s\n", mom_result.margin_call ? "YES" : "No");

    printf("\n----------------------------------------------------------------\n");
    if (mom_result.return_pct > bh_result.return_pct) {
        printf("RESULT: Momentum strategy OUTPERFORMS buy-and-hold by %.1f%%\n",
               mom_result.return_pct - bh_result.return_pct);
    } else {
        printf("RESULT: Buy-and-hold OUTPERFORMS momentum strategy by %.1f%%\n",
               bh_result.return_pct - mom_result.return_pct);
    }

    if (mom_result.max_dd < bh_result.max_dd) {
        printf("RISK: Momentum strategy has LOWER drawdown (%.1f%% vs %.1f%%)\n",
               mom_result.max_dd, bh_result.max_dd);
    } else {
        printf("RISK: Buy-and-hold has LOWER drawdown (%.1f%% vs %.1f%%)\n",
               bh_result.max_dd, mom_result.max_dd);
    }

    // Risk-adjusted return comparison
    double mom_sharpe_like = mom_result.return_pct / (mom_result.max_dd > 0 ? mom_result.max_dd : 1);
    double bh_sharpe_like = bh_result.return_pct / (bh_result.max_dd > 0 ? bh_result.max_dd : 1);

    printf("\nRISK-ADJUSTED (Return/MaxDD):\n");
    printf("  Momentum:    %.2f\n", mom_sharpe_like);
    printf("  Buy-Hold:    %.2f\n", bh_sharpe_like);

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);
    printf("\n================================================================\n");
    printf("Test completed in %lld seconds\n", (long long)duration.count());
    printf("================================================================\n");

    return 0;
}
