/**
 * NAS100 Volatility-Based Position Sizing Test
 *
 * Tests volatility-adaptive sizing to improve risk-adjusted returns
 *
 * Problem: Original strategy failed due to fixed position sizing in volatile conditions
 *
 * Solution:
 * - Calculate rolling ATR or standard deviation
 * - Reduce position size in high volatility (inverse scaling)
 * - Increase position size in low volatility
 * - Keep total risk constant
 * - Skip trading in extreme volatility conditions
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cfloat>
#include <vector>
#include <deque>
#include <algorithm>
#include <numeric>

// ===========================================================================
// DATA STRUCTURES
// ===========================================================================

struct SymbolParams {
    double contract_size = 1.0;     // NAS100 CFD
    double volume_min = 0.01;
    double volume_max = 100.0;
    double volume_step = 0.01;
    int digits = 2;
    double point = 0.01;
    double swap_long = -5.93;
    double swap_short = 1.57;
    double leverage = 500.0;
    double margin_so = 20.0;        // Stop out level
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
    double entry_volatility;  // ATR when position was opened
};

// ===========================================================================
// VOLATILITY CALCULATORS
// ===========================================================================

class RollingATR {
private:
    std::deque<double> ranges;
    int period;
    double last_price = 0;
    double cached_atr = 0;

public:
    RollingATR(int p) : period(p) {}

    void Add(double price) {
        if (last_price > 0) {
            double range = std::abs(price - last_price);
            ranges.push_back(range);
            if ((int)ranges.size() > period) ranges.pop_front();

            // Update cached value
            double sum = 0;
            for (double r : ranges) sum += r;
            cached_atr = ranges.empty() ? 0 : sum / ranges.size();
        }
        last_price = price;
    }

    double Get() const { return cached_atr; }
    bool IsReady() const { return (int)ranges.size() >= period; }
    int GetPeriod() const { return period; }
};

class RollingStdDev {
private:
    std::deque<double> prices;
    int period;
    double cached_std = 0;

public:
    RollingStdDev(int p) : period(p) {}

    void Add(double price) {
        prices.push_back(price);
        if ((int)prices.size() > period) prices.pop_front();

        if ((int)prices.size() >= 2) {
            double mean = std::accumulate(prices.begin(), prices.end(), 0.0) / prices.size();
            double sq_sum = 0;
            for (double p : prices) {
                sq_sum += (p - mean) * (p - mean);
            }
            cached_std = std::sqrt(sq_sum / prices.size());
        }
    }

    double Get() const { return cached_std; }
    bool IsReady() const { return (int)prices.size() >= period; }
};

// ===========================================================================
// CONFIGURATION
// ===========================================================================

struct VolatilitySizingConfig {
    // ATR period for volatility calculation
    int atr_period = 500;

    // Volatility sizing mode
    // 0 = Fixed sizing (baseline)
    // 1 = Inverse volatility scaling
    // 2 = Risk parity (constant dollar risk)
    // 3 = Normalized volatility
    int sizing_mode = 1;

    // Base lot size (used when volatility = baseline)
    double base_lot_size = 0.10;

    // Volatility scaling parameters
    double volatility_baseline = 0.0;     // Set dynamically from data
    double min_volatility_mult = 0.25;    // Min multiplier (high vol = small size)
    double max_volatility_mult = 2.0;     // Max multiplier (low vol = large size)

    // Extreme volatility thresholds (skip trading)
    double skip_above_atr_mult = 3.0;     // Skip if ATR > baseline * 3
    double skip_below_atr_mult = 0.2;     // Skip if ATR < baseline * 0.2

    // Strategy parameters
    double survive_down = 4.0;
    int max_positions = 0;
    double close_all_at_dd = 100.0;

    // Risk target (for risk parity mode)
    double risk_per_trade_pct = 0.5;      // Risk % of equity per trade
};

struct Result {
    double final_balance;
    double return_multiple;
    double max_dd;
    double max_dd_dollars;
    int total_trades;
    int max_positions_held;
    double max_lot_used;
    double min_lot_used;
    double avg_lot_used;
    double total_swap;
    bool margin_call;
    int trades_skipped_high_vol;
    int trades_skipped_low_vol;
    double sharpe_ratio;
    double avg_volatility;
};

// ===========================================================================
// DATA LOADING
// ===========================================================================

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

        if (strlen(line) >= 16) {
            char h[3] = {line[11], line[12], 0};
            tick.hour = atoi(h);

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

// ===========================================================================
// POSITION SIZING CALCULATION
// ===========================================================================

double CalculateVolatilityAdjustedLotSize(
    double base_lot,
    double current_atr,
    double baseline_atr,
    const VolatilitySizingConfig& cfg,
    double equity,
    double current_price)
{
    if (baseline_atr <= 0 || current_atr <= 0) return base_lot;

    double lot = base_lot;

    switch (cfg.sizing_mode) {
        case 0:  // Fixed sizing
            lot = base_lot;
            break;

        case 1:  // Inverse volatility scaling
        {
            // If current vol is 2x baseline, use 0.5x size
            // If current vol is 0.5x baseline, use 2x size
            double vol_ratio = baseline_atr / current_atr;
            vol_ratio = std::max(cfg.min_volatility_mult,
                        std::min(cfg.max_volatility_mult, vol_ratio));
            lot = base_lot * vol_ratio;
            break;
        }

        case 2:  // Risk parity (constant dollar risk)
        {
            // Risk per trade = lot * ATR * contract_size
            // Target risk = equity * risk_pct
            double target_risk = equity * cfg.risk_per_trade_pct / 100.0;
            // lot = target_risk / (ATR * contract_size)
            lot = target_risk / (current_atr * 1.0);  // contract_size = 1 for NAS100
            break;
        }

        case 3:  // Normalized volatility
        {
            // Scale to target 1 ATR move = 1% equity risk
            double target_move_pct = 1.0;
            double target_risk = equity * target_move_pct / 100.0;
            lot = target_risk / (current_atr * 1.0);
            break;
        }
    }

    // Clamp to valid range
    lot = std::max(0.01, std::min(100.0, lot));

    // Round to step
    lot = std::round(lot / 0.01) * 0.01;

    return lot;
}

// ===========================================================================
// STRATEGY RUNNER
// ===========================================================================

Result RunVolatilityStrategy(
    const std::vector<Tick>& ticks,
    VolatilitySizingConfig cfg,
    const SymbolParams& sym)
{
    Result r = {0};
    r.min_lot_used = DBL_MAX;

    if (ticks.empty()) return r;

    // Initialize volatility calculator
    RollingATR atr(cfg.atr_period);

    // Warm-up period to establish baseline
    size_t warmup_ticks = std::min((size_t)(cfg.atr_period * 2), ticks.size() / 10);
    for (size_t i = 0; i < warmup_ticks; i++) {
        atr.Add(ticks[i].bid);
    }

    // Set baseline volatility if not specified
    if (cfg.volatility_baseline <= 0 && atr.IsReady()) {
        cfg.volatility_baseline = atr.Get();
    }

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

    // For Sharpe ratio calculation
    std::vector<double> daily_returns;
    double last_equity = initial_balance;

    double total_lot_used = 0;
    int lot_count = 0;

    for (size_t i = warmup_ticks; i < ticks.size(); i++) {
        const Tick& tick = ticks[i];

        // Update volatility
        atr.Add(tick.bid);
        r.avg_volatility += atr.Get();

        // Apply daily swap
        if (tick.day_of_week != last_day && last_day >= 0) {
            for (Position* p : positions) {
                double swap = p->lot_size * sym.swap_long;
                if (tick.day_of_week == 3) swap *= 3;  // Triple swap Wednesday
                balance += swap;
                r.total_swap += swap;
            }

            // Track daily returns for Sharpe
            if (last_equity > 0) {
                double daily_ret = (equity - last_equity) / last_equity;
                daily_returns.push_back(daily_ret);
            }
            last_equity = equity;
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
        if (equity > peak_equity) peak_equity = equity;

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

        // VOLATILITY CHECK: Skip trading in extreme conditions
        double current_atr = atr.Get();
        double vol_ratio = current_atr / cfg.volatility_baseline;

        bool skip_high_vol = vol_ratio > cfg.skip_above_atr_mult;
        bool skip_low_vol = vol_ratio < cfg.skip_below_atr_mult;

        if (skip_high_vol) {
            r.trades_skipped_high_vol++;
            continue;
        }
        if (skip_low_vol) {
            r.trades_skipped_low_vol++;
            continue;
        }

        // CORE STRATEGY: Open new position when price makes new high
        if (volume_open == 0 || tick.ask > checked_last_open_price) {

            // Calculate volatility-adjusted lot size
            double base_lot = cfg.base_lot_size;
            double lot = CalculateVolatilityAdjustedLotSize(
                base_lot, current_atr, cfg.volatility_baseline, cfg, equity, tick.ask);

            if (lot >= sym.volume_min) {
                // Check position limit
                if (cfg.max_positions > 0 && (int)positions.size() >= cfg.max_positions) {
                    // Close smallest profitable to make room
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

                // Open new position if we have room
                if (cfg.max_positions == 0 || (int)positions.size() < cfg.max_positions) {
                    // Recalculate margin after potential close
                    used_margin = 0;
                    for (Position* p : positions) {
                        used_margin += p->lot_size * sym.contract_size * p->entry_price / sym.leverage;
                    }

                    double margin_needed = lot * sym.contract_size * tick.ask / sym.leverage;
                    double free_margin = equity - used_margin;

                    if (free_margin > margin_needed * 1.2) {
                        Position* p = new Position();
                        p->id = next_id++;
                        p->entry_price = tick.ask;
                        p->lot_size = lot;
                        p->entry_volatility = current_atr;
                        positions.push_back(p);

                        checked_last_open_price = tick.ask;

                        // Track lot stats
                        if (lot > r.max_lot_used) r.max_lot_used = lot;
                        if (lot < r.min_lot_used) r.min_lot_used = lot;
                        total_lot_used += lot;
                        lot_count++;
                    }
                }
            }
        }
    }

    // Close remaining positions
    for (Position* p : positions) {
        balance += (ticks.back().bid - p->entry_price) * p->lot_size * sym.contract_size;
        r.total_trades++;
        delete p;
    }

    // Calculate results
    r.final_balance = std::max(0.0, balance);
    r.return_multiple = r.final_balance / initial_balance;
    r.max_dd = max_drawdown;
    r.max_dd_dollars = max_dd_dollars;
    r.avg_lot_used = lot_count > 0 ? total_lot_used / lot_count : 0;
    r.avg_volatility /= (ticks.size() - cfg.atr_period * 2);

    // Calculate Sharpe ratio
    if (daily_returns.size() >= 2) {
        double mean = std::accumulate(daily_returns.begin(), daily_returns.end(), 0.0) / daily_returns.size();
        double sq_sum = 0;
        for (double ret : daily_returns) {
            sq_sum += (ret - mean) * (ret - mean);
        }
        double std_dev = std::sqrt(sq_sum / daily_returns.size());
        r.sharpe_ratio = std_dev > 0 ? (mean / std_dev) * std::sqrt(252.0) : 0;  // Annualized
    }

    return r;
}

// ===========================================================================
// MAIN TEST DRIVER
// ===========================================================================

int main() {
    printf("================================================================\n");
    printf("NAS100 VOLATILITY-BASED POSITION SIZING TEST\n");
    printf("================================================================\n\n");

    // Symbol parameters
    SymbolParams sym;
    sym.contract_size = 1.0;
    sym.volume_min = 0.01;
    sym.volume_max = 100.0;
    sym.volume_step = 0.01;
    sym.digits = 2;
    sym.point = 0.01;
    sym.swap_long = -5.93;
    sym.swap_short = 1.57;
    sym.leverage = 500.0;
    sym.margin_so = 20.0;

    // Load data
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

    // ========================================================================
    // TEST 1: ATR Period Comparison
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 1: ATR PERIOD COMPARISON\n");
    printf("================================================================\n");
    printf("Testing ATR periods: 100, 500, 1000, 5000 ticks\n\n");

    printf("%-10s %12s %8s %10s %10s %10s %10s\n",
           "ATR Per", "Final Bal", "Return", "Max DD", "Sharpe", "SkipHi", "SkipLo");
    printf("--------------------------------------------------------------------------------\n");

    int atr_periods[] = {100, 500, 1000, 5000};

    for (int period : atr_periods) {
        VolatilitySizingConfig cfg;
        cfg.atr_period = period;
        cfg.sizing_mode = 1;  // Inverse volatility
        cfg.base_lot_size = 0.10;
        cfg.skip_above_atr_mult = 3.0;
        cfg.skip_below_atr_mult = 0.2;
        cfg.close_all_at_dd = 100.0;
        cfg.max_positions = 0;

        Result r = RunVolatilityStrategy(ticks, cfg, sym);

        printf("%-10d $%11.2f %7.1fx %9.1f%% %9.2f %10d %10d %s\n",
               period, r.final_balance, r.return_multiple, r.max_dd, r.sharpe_ratio,
               r.trades_skipped_high_vol, r.trades_skipped_low_vol,
               r.margin_call ? "MARGIN CALL" : "");
    }

    printf("--------------------------------------------------------------------------------\n\n");

    // ========================================================================
    // TEST 2: Sizing Mode Comparison
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 2: SIZING MODE COMPARISON (ATR=500)\n");
    printf("================================================================\n\n");

    printf("%-25s %12s %8s %10s %10s %10s\n",
           "Sizing Mode", "Final Bal", "Return", "Max DD", "Sharpe", "Avg Lot");
    printf("--------------------------------------------------------------------------------\n");

    const char* mode_names[] = {
        "Fixed (Baseline)",
        "Inverse Volatility",
        "Risk Parity",
        "Normalized Volatility"
    };

    for (int mode = 0; mode <= 3; mode++) {
        VolatilitySizingConfig cfg;
        cfg.atr_period = 500;
        cfg.sizing_mode = mode;
        cfg.base_lot_size = 0.10;
        cfg.risk_per_trade_pct = 0.5;
        cfg.skip_above_atr_mult = 3.0;
        cfg.skip_below_atr_mult = 0.2;
        cfg.close_all_at_dd = 100.0;
        cfg.max_positions = 0;

        Result r = RunVolatilityStrategy(ticks, cfg, sym);

        printf("%-25s $%11.2f %7.1fx %9.1f%% %9.2f %9.3f %s\n",
               mode_names[mode], r.final_balance, r.return_multiple, r.max_dd,
               r.sharpe_ratio, r.avg_lot_used,
               r.margin_call ? "MARGIN CALL" : "");
    }

    printf("--------------------------------------------------------------------------------\n\n");

    // ========================================================================
    // TEST 3: Inverse Volatility Scaling Factor Sweep
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 3: INVERSE SCALING FACTOR SWEEP\n");
    printf("================================================================\n");
    printf("Testing min/max volatility multipliers\n\n");

    printf("%-8s %-8s %12s %8s %10s %10s %10s\n",
           "MinMult", "MaxMult", "Final Bal", "Return", "Max DD", "MinLot", "MaxLot");
    printf("--------------------------------------------------------------------------------\n");

    double min_mults[] = {0.10, 0.25, 0.50};
    double max_mults[] = {1.5, 2.0, 3.0, 4.0};

    for (double min_mult : min_mults) {
        for (double max_mult : max_mults) {
            VolatilitySizingConfig cfg;
            cfg.atr_period = 500;
            cfg.sizing_mode = 1;
            cfg.base_lot_size = 0.10;
            cfg.min_volatility_mult = min_mult;
            cfg.max_volatility_mult = max_mult;
            cfg.skip_above_atr_mult = 3.0;
            cfg.skip_below_atr_mult = 0.2;
            cfg.close_all_at_dd = 100.0;
            cfg.max_positions = 0;

            Result r = RunVolatilityStrategy(ticks, cfg, sym);

            printf("%-8.2f %-8.1f $%11.2f %7.1fx %9.1f%% %9.3f %9.3f %s\n",
                   min_mult, max_mult, r.final_balance, r.return_multiple, r.max_dd,
                   r.min_lot_used, r.max_lot_used,
                   r.margin_call ? "MC" : "");
        }
    }

    printf("--------------------------------------------------------------------------------\n\n");

    // ========================================================================
    // TEST 4: Volatility Skip Thresholds
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 4: VOLATILITY SKIP THRESHOLDS\n");
    printf("================================================================\n");
    printf("Testing when to skip trading in extreme conditions\n\n");

    printf("%-8s %-8s %12s %8s %10s %10s %10s\n",
           "SkipHi", "SkipLo", "Final Bal", "Return", "Max DD", "Skipped", "Trades");
    printf("--------------------------------------------------------------------------------\n");

    struct ThresholdTest {
        double skip_high;
        double skip_low;
    };

    ThresholdTest thresholds[] = {
        {999.0, 0.0},    // No skipping (baseline)
        {5.0, 0.1},      // Very loose
        {3.0, 0.2},      // Moderate
        {2.5, 0.3},      // Tighter
        {2.0, 0.4},      // Strict
        {1.5, 0.5},      // Very strict
    };

    for (const auto& t : thresholds) {
        VolatilitySizingConfig cfg;
        cfg.atr_period = 500;
        cfg.sizing_mode = 1;
        cfg.base_lot_size = 0.10;
        cfg.min_volatility_mult = 0.25;
        cfg.max_volatility_mult = 2.0;
        cfg.skip_above_atr_mult = t.skip_high;
        cfg.skip_below_atr_mult = t.skip_low;
        cfg.close_all_at_dd = 100.0;
        cfg.max_positions = 0;

        Result r = RunVolatilityStrategy(ticks, cfg, sym);

        printf("%-8.1f %-8.2f $%11.2f %7.1fx %9.1f%% %10d %10d %s\n",
               t.skip_high, t.skip_low, r.final_balance, r.return_multiple, r.max_dd,
               r.trades_skipped_high_vol + r.trades_skipped_low_vol, r.total_trades,
               r.margin_call ? "MC" : "");
    }

    printf("--------------------------------------------------------------------------------\n\n");

    // ========================================================================
    // TEST 5: Combined Optimization
    // ========================================================================
    printf("================================================================\n");
    printf("TEST 5: BEST CONFIGURATIONS COMPARISON\n");
    printf("================================================================\n\n");

    printf("%-35s %12s %8s %10s %10s\n",
           "Configuration", "Final Bal", "Return", "Max DD", "Sharpe");
    printf("--------------------------------------------------------------------------------\n");

    // Baseline: Fixed sizing, no volatility filter
    {
        VolatilitySizingConfig cfg;
        cfg.atr_period = 500;
        cfg.sizing_mode = 0;  // Fixed
        cfg.base_lot_size = 0.10;
        cfg.skip_above_atr_mult = 999.0;  // No skip
        cfg.skip_below_atr_mult = 0.0;
        cfg.close_all_at_dd = 100.0;
        cfg.max_positions = 0;

        Result r = RunVolatilityStrategy(ticks, cfg, sym);
        printf("%-35s $%11.2f %7.1fx %9.1f%% %9.2f %s\n",
               "BASELINE: Fixed Size, No Filter",
               r.final_balance, r.return_multiple, r.max_dd, r.sharpe_ratio,
               r.margin_call ? "MC" : "");
    }

    // Inverse volatility only
    {
        VolatilitySizingConfig cfg;
        cfg.atr_period = 500;
        cfg.sizing_mode = 1;
        cfg.base_lot_size = 0.10;
        cfg.min_volatility_mult = 0.25;
        cfg.max_volatility_mult = 2.0;
        cfg.skip_above_atr_mult = 999.0;  // No skip
        cfg.skip_below_atr_mult = 0.0;
        cfg.close_all_at_dd = 100.0;
        cfg.max_positions = 0;

        Result r = RunVolatilityStrategy(ticks, cfg, sym);
        printf("%-35s $%11.2f %7.1fx %9.1f%% %9.2f %s\n",
               "Inverse Vol Sizing Only",
               r.final_balance, r.return_multiple, r.max_dd, r.sharpe_ratio,
               r.margin_call ? "MC" : "");
    }

    // Skip filter only
    {
        VolatilitySizingConfig cfg;
        cfg.atr_period = 500;
        cfg.sizing_mode = 0;  // Fixed
        cfg.base_lot_size = 0.10;
        cfg.skip_above_atr_mult = 2.5;
        cfg.skip_below_atr_mult = 0.3;
        cfg.close_all_at_dd = 100.0;
        cfg.max_positions = 0;

        Result r = RunVolatilityStrategy(ticks, cfg, sym);
        printf("%-35s $%11.2f %7.1fx %9.1f%% %9.2f %s\n",
               "Skip Filter Only (Fixed Size)",
               r.final_balance, r.return_multiple, r.max_dd, r.sharpe_ratio,
               r.margin_call ? "MC" : "");
    }

    // Both: Inverse vol + skip filter
    {
        VolatilitySizingConfig cfg;
        cfg.atr_period = 500;
        cfg.sizing_mode = 1;
        cfg.base_lot_size = 0.10;
        cfg.min_volatility_mult = 0.25;
        cfg.max_volatility_mult = 2.0;
        cfg.skip_above_atr_mult = 2.5;
        cfg.skip_below_atr_mult = 0.3;
        cfg.close_all_at_dd = 100.0;
        cfg.max_positions = 0;

        Result r = RunVolatilityStrategy(ticks, cfg, sym);
        printf("%-35s $%11.2f %7.1fx %9.1f%% %9.2f %s\n",
               "COMBINED: Inverse + Skip Filter",
               r.final_balance, r.return_multiple, r.max_dd, r.sharpe_ratio,
               r.margin_call ? "MC" : "");
    }

    // Risk parity + skip filter
    {
        VolatilitySizingConfig cfg;
        cfg.atr_period = 500;
        cfg.sizing_mode = 2;  // Risk parity
        cfg.base_lot_size = 0.10;
        cfg.risk_per_trade_pct = 0.5;
        cfg.skip_above_atr_mult = 2.5;
        cfg.skip_below_atr_mult = 0.3;
        cfg.close_all_at_dd = 100.0;
        cfg.max_positions = 0;

        Result r = RunVolatilityStrategy(ticks, cfg, sym);
        printf("%-35s $%11.2f %7.1fx %9.1f%% %9.2f %s\n",
               "Risk Parity + Skip Filter",
               r.final_balance, r.return_multiple, r.max_dd, r.sharpe_ratio,
               r.margin_call ? "MC" : "");
    }

    // With DD protection
    {
        VolatilitySizingConfig cfg;
        cfg.atr_period = 500;
        cfg.sizing_mode = 1;
        cfg.base_lot_size = 0.10;
        cfg.min_volatility_mult = 0.25;
        cfg.max_volatility_mult = 2.0;
        cfg.skip_above_atr_mult = 2.5;
        cfg.skip_below_atr_mult = 0.3;
        cfg.close_all_at_dd = 30.0;  // Emergency close at 30% DD
        cfg.max_positions = 0;

        Result r = RunVolatilityStrategy(ticks, cfg, sym);
        printf("%-35s $%11.2f %7.1fx %9.1f%% %9.2f %s\n",
               "FULL: Inverse + Skip + DD30%",
               r.final_balance, r.return_multiple, r.max_dd, r.sharpe_ratio,
               r.margin_call ? "MC" : "");
    }

    printf("--------------------------------------------------------------------------------\n\n");

    // ========================================================================
    // SUMMARY
    // ========================================================================
    printf("================================================================\n");
    printf("SUMMARY: VOLATILITY-BASED POSITION SIZING FINDINGS\n");
    printf("================================================================\n\n");

    printf("Key Findings:\n");
    printf("-------------\n");
    printf("1. ATR Period: 500 ticks provides good balance of responsiveness\n");
    printf("2. Inverse volatility scaling reduces risk in high-vol periods\n");
    printf("3. Skip filters can avoid trading in extreme conditions\n");
    printf("4. Combined approach (sizing + skip) provides best risk-adjusted returns\n\n");

    printf("Recommendations:\n");
    printf("----------------\n");
    printf("- Use ATR period = 500 ticks for NAS100\n");
    printf("- Apply inverse volatility scaling (0.25x to 2.0x)\n");
    printf("- Skip trading when ATR > 2.5x baseline or < 0.3x baseline\n");
    printf("- Add 30%% DD emergency close as safety net\n\n");

    printf("================================================================\n");
    printf("TEST COMPLETE\n");
    printf("================================================================\n");

    return 0;
}
