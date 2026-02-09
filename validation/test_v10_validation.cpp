/**
 * V10 Full Combination Validation Test
 *
 * Tests if combining ALL filters gives the best results or if they
 * cancel each other out (over-filtering).
 *
 * Configurations tested:
 * a) Baseline: No filters
 * b) V7: Volatility filter only (ATR short 50, ATR long 1000, threshold 0.6)
 * c) Mean Reversion only: SMA 500, threshold -0.04%
 * d) V10 Full: Volatility + Mean Reversion + Session filter (22:00-06:00 UTC)
 *
 * Parameters: spacing 0.75, TP 2.0, protection 3/5/15%, max 15 positions
 *
 * Goal: Determine if filter combination provides synergy or over-filters.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cfloat>
#include <vector>
#include <deque>
#include <algorithm>

// ----------------------------------------------------------------------------
// Data Structures
// ----------------------------------------------------------------------------

struct Tick {
    char timestamp[32];  // "YYYY.MM.DD HH:MM:SS.mmm"
    double bid;
    double ask;
    int hour;  // Extracted hour (0-23)

    double spread() const { return ask - bid; }
};

struct Trade {
    size_t id;
    double entry_price;
    double lot_size;
    double take_profit;
};

// ----------------------------------------------------------------------------
// Indicator Classes
// ----------------------------------------------------------------------------

class SMA {
    std::deque<double> prices_;
    int period_;
    double sum_;
public:
    SMA(int period) : period_(period), sum_(0) {}

    void Add(double price) {
        prices_.push_back(price);
        sum_ += price;
        if ((int)prices_.size() > period_) {
            sum_ -= prices_.front();
            prices_.pop_front();
        }
    }

    double Get() const {
        return prices_.empty() ? 0 : sum_ / prices_.size();
    }

    bool IsReady() const {
        return (int)prices_.size() >= period_;
    }

    void Reset() {
        prices_.clear();
        sum_ = 0;
    }
};

class ATR {
    std::deque<double> ranges_;
    int period_;
    double sum_;
    double last_price_;
public:
    ATR(int period) : period_(period), sum_(0), last_price_(0) {}

    void Add(double price) {
        if (last_price_ > 0) {
            double range = fabs(price - last_price_);
            ranges_.push_back(range);
            sum_ += range;
            if ((int)ranges_.size() > period_) {
                sum_ -= ranges_.front();
                ranges_.pop_front();
            }
        }
        last_price_ = price;
    }

    double Get() const {
        return ranges_.empty() ? 0 : sum_ / ranges_.size();
    }

    bool IsReady() const {
        return (int)ranges_.size() >= period_;
    }

    void Reset() {
        ranges_.clear();
        sum_ = 0;
        last_price_ = 0;
    }
};

// ----------------------------------------------------------------------------
// Filter Configuration
// ----------------------------------------------------------------------------

enum FilterMode {
    BASELINE,           // No filters at all
    V7_VOLATILITY,      // ATR volatility filter only
    MEAN_REVERSION,     // SMA mean reversion filter only
    SESSION_ONLY,       // Session filter only (22:00-06:00 UTC)
    V7_MEAN_REV,        // Volatility + Mean Reversion (no session)
    V7_SESSION,         // Volatility + Session (no mean reversion)
    V10_FULL            // All filters combined: Volatility + Mean Reversion + Session
};

const char* GetFilterName(FilterMode mode) {
    switch (mode) {
        case BASELINE:        return "Baseline (No Filters)";
        case V7_VOLATILITY:   return "V7 Volatility Only";
        case MEAN_REVERSION:  return "Mean Reversion Only";
        case SESSION_ONLY:    return "Session Only (22-06)";
        case V7_MEAN_REV:     return "V7 + Mean Reversion";
        case V7_SESSION:      return "V7 + Session";
        case V10_FULL:        return "V10 Full (All 3)";
        default:              return "Unknown";
    }
}

// ----------------------------------------------------------------------------
// Test Configuration
// ----------------------------------------------------------------------------

struct Config {
    // V7 Volatility filter parameters
    int atr_short_period;       // Short-term ATR period
    int atr_long_period;        // Long-term ATR period
    double volatility_threshold; // Trade when short ATR < long ATR * this

    // Mean Reversion filter parameters
    int sma_period;             // SMA period for mean reversion
    double mean_reversion_threshold; // Trade when price < SMA * (1 + threshold)
                                     // -0.04% means price is 0.04% below SMA

    // Session filter parameters
    int session_start_hour;     // Session start (e.g., 22 for 22:00 UTC)
    int session_end_hour;       // Session end (e.g., 6 for 06:00 UTC)

    // Grid parameters
    double spacing;
    double tp_multiplier;

    // V3 Protection levels
    double stop_new_at_dd;      // Stop opening new positions at this DD%
    double partial_close_at_dd; // Close 50% of positions at this DD%
    double close_all_at_dd;     // Close all positions at this DD%
    int max_positions;

    Config() :
        atr_short_period(50),
        atr_long_period(1000),
        volatility_threshold(0.6),
        sma_period(500),
        mean_reversion_threshold(-0.0004),  // -0.04%
        session_start_hour(22),
        session_end_hour(6),
        spacing(0.75),
        tp_multiplier(2.0),
        stop_new_at_dd(3.0),
        partial_close_at_dd(5.0),
        close_all_at_dd(15.0),
        max_positions(15) {}
};

// ----------------------------------------------------------------------------
// Test Results
// ----------------------------------------------------------------------------

struct Result {
    const char* filter_name;
    double return_pct;
    double max_dd;
    int trades_completed;
    int winning_trades;
    int losing_trades;
    double win_rate;
    double risk_adjusted_score;  // Return / Max DD
    int positions_opened;
    double time_trading_pct;     // % of time filters allowed trading
};

// ----------------------------------------------------------------------------
// Helper Functions
// ----------------------------------------------------------------------------

// Extract hour from timestamp "YYYY.MM.DD HH:MM:SS"
int ExtractHour(const char* timestamp) {
    if (strlen(timestamp) >= 13) {
        int h = (timestamp[11] - '0') * 10 + (timestamp[12] - '0');
        if (h >= 0 && h <= 23) return h;
    }
    return 0;
}

// Check if hour is in session window (handles overnight wraparound)
bool IsInSession(int hour, int start_hour, int end_hour) {
    if (start_hour < end_hour) {
        // Normal range (e.g., 8-21)
        return hour >= start_hour && hour < end_hour;
    } else {
        // Overnight range (e.g., 22-6 wraps around midnight)
        return hour >= start_hour || hour < end_hour;
    }
}

// Load ticks using C-style file I/O
std::vector<Tick> LoadTicks(const char* filename, size_t max_ticks) {
    std::vector<Tick> ticks;
    ticks.reserve(max_ticks);

    FILE* file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "ERROR: Cannot open file: %s\n", filename);
        return ticks;
    }

    char line[512];

    // Skip header
    if (!fgets(line, sizeof(line), file)) {
        fclose(file);
        return ticks;
    }

    printf("Loading ticks from: %s\n", filename);

    while (fgets(line, sizeof(line), file) && ticks.size() < max_ticks) {
        Tick tick;
        memset(&tick, 0, sizeof(tick));

        // Parse tab-separated: timestamp\tbid\task\t...
        if (sscanf(line, "%31[^\t]\t%lf\t%lf", tick.timestamp, &tick.bid, &tick.ask) >= 3) {
            if (tick.bid > 0 && tick.ask > 0) {
                tick.hour = ExtractHour(tick.timestamp);
                ticks.push_back(tick);
            }
        }

        if (ticks.size() % 100000 == 0 && ticks.size() > 0) {
            printf("  Loaded %zu ticks...\n", ticks.size());
        }
    }

    fclose(file);
    printf("Loaded %zu ticks total\n\n", ticks.size());

    return ticks;
}

// ----------------------------------------------------------------------------
// Backtest Engine
// ----------------------------------------------------------------------------

Result RunTest(const std::vector<Tick>& ticks, FilterMode mode, const Config& cfg) {
    Result r = {};
    r.filter_name = GetFilterName(mode);

    if (ticks.empty()) return r;

    const double initial_balance = 10000.0;
    double balance = initial_balance;
    double equity = balance;
    double peak_equity = balance;
    const double contract_size = 100.0;  // XAUUSD
    const double leverage = 500.0;

    std::vector<Trade*> positions;
    size_t next_id = 1;
    bool partial_done = false;
    bool all_closed = false;

    // Indicators
    ATR atr_short(cfg.atr_short_period);
    ATR atr_long(cfg.atr_long_period);
    SMA sma(cfg.sma_period);

    // Statistics
    int trading_allowed_count = 0;
    int trading_check_count = 0;
    double largest_win = 0;
    double largest_loss = 0;

    for (const Tick& tick : ticks) {
        // Update indicators
        atr_short.Add(tick.bid);
        atr_long.Add(tick.bid);
        sma.Add(tick.bid);

        // Update equity
        equity = balance;
        for (Trade* t : positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * contract_size;
        }

        // Peak equity reset when no positions (fresh start)
        if (positions.empty() && peak_equity != balance) {
            peak_equity = balance;
            partial_done = false;
            all_closed = false;
        }

        // Update peak equity
        if (equity > peak_equity) {
            peak_equity = equity;
            partial_done = false;
            all_closed = false;
        }

        // Calculate current drawdown
        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        r.max_dd = fmax(r.max_dd, dd_pct);

        // V3 Protection: Close ALL at threshold
        if (dd_pct > cfg.close_all_at_dd && !all_closed && !positions.empty()) {
            for (Trade* t : positions) {
                double profit = (tick.bid - t->entry_price) * t->lot_size * contract_size;
                balance += profit;
                if (profit > 0) {
                    r.winning_trades++;
                    if (profit > largest_win) largest_win = profit;
                } else {
                    r.losing_trades++;
                    if (profit < largest_loss) largest_loss = profit;
                }
                r.trades_completed++;
                delete t;
            }
            positions.clear();
            all_closed = true;
            equity = balance;
            peak_equity = equity;
            continue;
        }

        // V3 Protection: Partial close at threshold
        if (dd_pct > cfg.partial_close_at_dd && !partial_done && positions.size() > 1) {
            // Sort by P/L (worst first)
            std::sort(positions.begin(), positions.end(), [&](Trade* a, Trade* b) {
                return (tick.bid - a->entry_price) < (tick.bid - b->entry_price);
            });
            int to_close = (int)(positions.size() / 2);
            to_close = (to_close < 1) ? 1 : to_close;
            for (int i = 0; i < to_close && !positions.empty(); i++) {
                double profit = (tick.bid - positions[0]->entry_price) * positions[0]->lot_size * contract_size;
                balance += profit;
                if (profit > 0) {
                    r.winning_trades++;
                    if (profit > largest_win) largest_win = profit;
                } else {
                    r.losing_trades++;
                    if (profit < largest_loss) largest_loss = profit;
                }
                r.trades_completed++;
                delete positions[0];
                positions.erase(positions.begin());
            }
            partial_done = true;
        }

        // Take profit check
        for (auto it = positions.begin(); it != positions.end();) {
            Trade* t = *it;
            if (tick.bid >= t->take_profit) {
                double profit = (t->take_profit - t->entry_price) * t->lot_size * contract_size;
                balance += profit;
                if (profit > 0) {
                    r.winning_trades++;
                    if (profit > largest_win) largest_win = profit;
                } else {
                    r.losing_trades++;
                    if (profit < largest_loss) largest_loss = profit;
                }
                r.trades_completed++;
                delete t;
                it = positions.erase(it);
            } else {
                ++it;
            }
        }

        // Determine if trading is allowed based on filter mode
        bool volatility_ok = true;
        bool mean_reversion_ok = true;
        bool session_ok = true;

        switch (mode) {
            case BASELINE:
                // No filters - always trade
                break;

            case V7_VOLATILITY:
                // Only volatility filter
                if (atr_short.IsReady() && atr_long.IsReady() && atr_long.Get() > 0) {
                    volatility_ok = atr_short.Get() < atr_long.Get() * cfg.volatility_threshold;
                }
                break;

            case MEAN_REVERSION:
                // Only mean reversion filter
                // Trade when price is BELOW SMA (looking for bounce up)
                if (sma.IsReady() && sma.Get() > 0) {
                    double sma_val = sma.Get();
                    double threshold_price = sma_val * (1.0 + cfg.mean_reversion_threshold);
                    mean_reversion_ok = tick.bid < threshold_price;
                }
                break;

            case SESSION_ONLY:
                // Only session filter (22:00-06:00 UTC)
                session_ok = IsInSession(tick.hour, cfg.session_start_hour, cfg.session_end_hour);
                break;

            case V7_MEAN_REV:
                // Volatility + Mean Reversion (no session)
                if (atr_short.IsReady() && atr_long.IsReady() && atr_long.Get() > 0) {
                    volatility_ok = atr_short.Get() < atr_long.Get() * cfg.volatility_threshold;
                }
                if (sma.IsReady() && sma.Get() > 0) {
                    double sma_val = sma.Get();
                    double threshold_price = sma_val * (1.0 + cfg.mean_reversion_threshold);
                    mean_reversion_ok = tick.bid < threshold_price;
                }
                break;

            case V7_SESSION:
                // Volatility + Session (no mean reversion)
                if (atr_short.IsReady() && atr_long.IsReady() && atr_long.Get() > 0) {
                    volatility_ok = atr_short.Get() < atr_long.Get() * cfg.volatility_threshold;
                }
                session_ok = IsInSession(tick.hour, cfg.session_start_hour, cfg.session_end_hour);
                break;

            case V10_FULL:
                // All filters combined
                // 1. Volatility filter
                if (atr_short.IsReady() && atr_long.IsReady() && atr_long.Get() > 0) {
                    volatility_ok = atr_short.Get() < atr_long.Get() * cfg.volatility_threshold;
                }
                // 2. Mean reversion filter
                if (sma.IsReady() && sma.Get() > 0) {
                    double sma_val = sma.Get();
                    double threshold_price = sma_val * (1.0 + cfg.mean_reversion_threshold);
                    mean_reversion_ok = tick.bid < threshold_price;
                }
                // 3. Session filter (22:00-06:00 UTC)
                session_ok = IsInSession(tick.hour, cfg.session_start_hour, cfg.session_end_hour);
                break;
        }

        bool trading_allowed = volatility_ok && mean_reversion_ok && session_ok;

        // Track filter statistics
        trading_check_count++;
        if (trading_allowed) trading_allowed_count++;

        // Open new positions if allowed
        double used_margin = 0;
        for (Trade* t : positions) {
            used_margin += t->lot_size * contract_size * t->entry_price / leverage;
        }

        if (dd_pct < cfg.stop_new_at_dd && trading_allowed && (int)positions.size() < cfg.max_positions) {
            // Find lowest and highest entry prices
            double lowest = DBL_MAX, highest = DBL_MIN;
            for (Trade* t : positions) {
                if (t->entry_price < lowest) lowest = t->entry_price;
                if (t->entry_price > highest) highest = t->entry_price;
            }

            bool should_open = positions.empty() ||
                              (lowest >= tick.ask + cfg.spacing) ||
                              (highest <= tick.ask - cfg.spacing);

            if (should_open) {
                double lot = 0.01;
                double margin_needed = lot * contract_size * tick.ask / leverage;
                if (equity - used_margin > margin_needed * 2) {
                    Trade* t = new Trade();
                    t->id = next_id++;
                    t->entry_price = tick.ask;
                    t->lot_size = lot;
                    t->take_profit = tick.ask + tick.spread() + (cfg.spacing * cfg.tp_multiplier);
                    positions.push_back(t);
                    r.positions_opened++;
                }
            }
        }
    }

    // Close remaining positions at end
    for (Trade* t : positions) {
        double profit = (ticks.back().bid - t->entry_price) * t->lot_size * contract_size;
        balance += profit;
        if (profit > 0) {
            r.winning_trades++;
            if (profit > largest_win) largest_win = profit;
        } else {
            r.losing_trades++;
            if (profit < largest_loss) largest_loss = profit;
        }
        r.trades_completed++;
        delete t;
    }

    // Calculate final statistics
    r.return_pct = (balance - initial_balance) / initial_balance * 100.0;
    r.time_trading_pct = (trading_check_count > 0) ?
                         (double)trading_allowed_count / trading_check_count * 100.0 : 0.0;
    r.win_rate = (r.trades_completed > 0) ?
                 (double)r.winning_trades / r.trades_completed * 100.0 : 0.0;
    r.risk_adjusted_score = (r.max_dd > 0) ? r.return_pct / r.max_dd : r.return_pct;

    return r;
}

// ----------------------------------------------------------------------------
// Main
// ----------------------------------------------------------------------------

int main() {
    printf("================================================================\n");
    printf("   V10 FULL COMBINATION VALIDATION TEST\n");
    printf("================================================================\n\n");

    printf("Goal: Test if combining ALL filters gives the best results\n");
    printf("      or if they cancel each other out (over-filtering).\n\n");

    printf("Configurations tested:\n");
    printf("  a) Baseline:       No filters (trade everything)\n");
    printf("  b) V7 Volatility:  ATR short 50 / long 1000 / threshold 0.6\n");
    printf("  c) Mean Reversion: SMA 500, threshold -0.04%% (buy below SMA)\n");
    printf("  d) Session Only:   22:00-06:00 UTC session filter\n");
    printf("  e) V7 + MeanRev:   Volatility + Mean Reversion (no session)\n");
    printf("  f) V7 + Session:   Volatility + Session (no mean reversion)\n");
    printf("  g) V10 Full:       All 3 filters combined\n\n");

    // Configuration
    Config cfg;
    cfg.atr_short_period = 50;
    cfg.atr_long_period = 1000;
    cfg.volatility_threshold = 0.6;
    cfg.sma_period = 500;
    cfg.mean_reversion_threshold = -0.0004;  // -0.04%
    cfg.session_start_hour = 22;
    cfg.session_end_hour = 6;
    cfg.spacing = 0.75;
    cfg.tp_multiplier = 2.0;
    cfg.stop_new_at_dd = 3.0;
    cfg.partial_close_at_dd = 5.0;
    cfg.close_all_at_dd = 15.0;
    cfg.max_positions = 15;

    printf("Grid Parameters:\n");
    printf("  Spacing:              %.2f\n", cfg.spacing);
    printf("  TP Multiplier:        %.1f (TP = spacing * %.1f = %.2f)\n",
           cfg.tp_multiplier, cfg.tp_multiplier, cfg.spacing * cfg.tp_multiplier);
    printf("  Max Positions:        %d\n", cfg.max_positions);
    printf("\n");

    printf("Protection Levels:\n");
    printf("  Stop new at DD:       %.1f%%\n", cfg.stop_new_at_dd);
    printf("  Partial close at DD:  %.1f%%\n", cfg.partial_close_at_dd);
    printf("  Close all at DD:      %.1f%%\n", cfg.close_all_at_dd);
    printf("\n");

    printf("Filter Parameters:\n");
    printf("  V7 ATR Short:         %d ticks\n", cfg.atr_short_period);
    printf("  V7 ATR Long:          %d ticks\n", cfg.atr_long_period);
    printf("  V7 Vol Threshold:     %.2f (trade when short < long * %.2f)\n",
           cfg.volatility_threshold, cfg.volatility_threshold);
    printf("  Mean Rev SMA:         %d ticks\n", cfg.sma_period);
    printf("  Mean Rev Threshold:   %.4f%% (trade when price < SMA * %.4f)\n",
           cfg.mean_reversion_threshold * 100, 1.0 + cfg.mean_reversion_threshold);
    printf("  Session Hours:        %02d:00 - %02d:00 UTC\n",
           cfg.session_start_hour, cfg.session_end_hour);
    printf("\n");

    // Load tick data
    const char* filename = "Grid/XAUUSD_TICKS_2025.csv";
    std::vector<Tick> ticks = LoadTicks(filename, 500000);

    if (ticks.empty()) {
        fprintf(stderr, "ERROR: Failed to load tick data!\n");
        return 1;
    }

    // Show price range
    double start_price = ticks.front().bid;
    double end_price = ticks.back().bid;
    double price_change = (end_price - start_price) / start_price * 100.0;

    printf("Price Range: %.2f -> %.2f (%+.2f%%)\n", start_price, end_price, price_change);
    printf("Date Range:  %.10s -> %.10s\n",
           ticks.front().timestamp, ticks.back().timestamp);
    printf("\n");

    // Run all test configurations
    printf("Running tests...\n");
    printf("================================================================\n\n");

    FilterMode modes[] = {BASELINE, V7_VOLATILITY, MEAN_REVERSION, SESSION_ONLY, V7_MEAN_REV, V7_SESSION, V10_FULL};
    std::vector<Result> results;

    for (FilterMode mode : modes) {
        printf("  Testing: %s\n", GetFilterName(mode));
        Result r = RunTest(ticks, mode, cfg);
        results.push_back(r);
    }

    // Print results table
    printf("\n================================================================\n");
    printf("                        RESULTS\n");
    printf("================================================================\n\n");

    // Header
    printf("%-25s | %8s | %7s | %7s | %7s | %8s\n",
           "Configuration", "Return", "Max DD", "Trades", "WinRate", "RiskAdj");
    printf("%-25s-+-%8s-+-%7s-+-%7s-+-%7s-+-%8s\n",
           "-------------------------", "--------", "-------", "-------", "-------", "--------");

    for (const Result& r : results) {
        printf("%-25s | %7.2f%% | %6.2f%% | %7d | %6.1f%% | %8.2f\n",
               r.filter_name,
               r.return_pct,
               r.max_dd,
               r.trades_completed,
               r.win_rate,
               r.risk_adjusted_score);
    }

    printf("%-25s-+-%8s-+-%7s-+-%7s-+-%7s-+-%8s\n",
           "-------------------------", "--------", "-------", "-------", "-------", "--------");

    // Extended metrics table
    printf("\n%-25s | %8s | %8s | %8s\n",
           "Configuration", "Opened", "Time%%", "EffRate");
    printf("%-25s-+-%8s-+-%8s-+-%8s\n",
           "-------------------------", "--------", "--------", "--------");

    for (const Result& r : results) {
        double efficiency = (r.time_trading_pct > 0) ?
                           r.return_pct / (r.time_trading_pct / 100.0) : r.return_pct;
        printf("%-25s | %8d | %7.1f%% | %8.2f\n",
               r.filter_name,
               r.positions_opened,
               r.time_trading_pct,
               efficiency);
    }

    printf("%-25s-+-%8s-+-%8s-+-%8s\n",
           "-------------------------", "--------", "--------", "--------");

    // Analysis
    printf("\n================================================================\n");
    printf("                       ANALYSIS\n");
    printf("================================================================\n\n");

    // Find best by each metric
    int best_return_idx = 0;
    int best_risk_adj_idx = 0;
    int lowest_dd_idx = 0;
    int best_win_rate_idx = 0;

    for (size_t i = 1; i < results.size(); i++) {
        if (results[i].return_pct > results[best_return_idx].return_pct) {
            best_return_idx = (int)i;
        }
        if (results[i].risk_adjusted_score > results[best_risk_adj_idx].risk_adjusted_score) {
            best_risk_adj_idx = (int)i;
        }
        if (results[i].max_dd < results[lowest_dd_idx].max_dd && results[i].return_pct > 0) {
            lowest_dd_idx = (int)i;
        }
        if (results[i].win_rate > results[best_win_rate_idx].win_rate) {
            best_win_rate_idx = (int)i;
        }
    }

    printf("BEST BY METRIC:\n");
    printf("  Highest Return:       %s (%.2f%%)\n",
           results[best_return_idx].filter_name, results[best_return_idx].return_pct);
    printf("  Best Risk-Adjusted:   %s (%.2f)\n",
           results[best_risk_adj_idx].filter_name, results[best_risk_adj_idx].risk_adjusted_score);
    printf("  Lowest Max DD:        %s (%.2f%%)\n",
           results[lowest_dd_idx].filter_name, results[lowest_dd_idx].max_dd);
    printf("  Highest Win Rate:     %s (%.1f%%)\n",
           results[best_win_rate_idx].filter_name, results[best_win_rate_idx].win_rate);
    printf("\n");

    // Detailed comparisons
    printf("FILTER COMPARISON:\n");
    printf("------------------\n\n");

    const Result& baseline = results[0];
    const Result& v7 = results[1];
    const Result& mean_rev = results[2];
    const Result& session = results[3];
    const Result& v7_mean_rev = results[4];
    const Result& v7_session = results[5];
    const Result& v10 = results[6];

    // Single filter analysis
    printf("SINGLE FILTERS vs BASELINE:\n");
    printf("  V7 Volatility:  %+.2f%% return, %+.2f risk-adj  %s\n",
           v7.return_pct - baseline.return_pct,
           v7.risk_adjusted_score - baseline.risk_adjusted_score,
           (v7.risk_adjusted_score > baseline.risk_adjusted_score) ? "[BETTER]" : "[WORSE]");
    printf("  Mean Reversion: %+.2f%% return, %+.2f risk-adj  %s\n",
           mean_rev.return_pct - baseline.return_pct,
           mean_rev.risk_adjusted_score - baseline.risk_adjusted_score,
           (mean_rev.risk_adjusted_score > baseline.risk_adjusted_score) ? "[BETTER]" : "[WORSE]");
    printf("  Session Only:   %+.2f%% return, %+.2f risk-adj  %s\n",
           session.return_pct - baseline.return_pct,
           session.risk_adjusted_score - baseline.risk_adjusted_score,
           (session.risk_adjusted_score > baseline.risk_adjusted_score) ? "[BETTER]" : "[WORSE]");
    printf("\n");

    // Two-filter combinations
    printf("TWO-FILTER COMBINATIONS vs V7 ALONE:\n");
    printf("  V7 + Mean Rev:  %+.2f%% return, %+.2f risk-adj  %s\n",
           v7_mean_rev.return_pct - v7.return_pct,
           v7_mean_rev.risk_adjusted_score - v7.risk_adjusted_score,
           (v7_mean_rev.risk_adjusted_score > v7.risk_adjusted_score) ? "[SYNERGY]" : "[OVER-FILTER]");
    printf("  V7 + Session:   %+.2f%% return, %+.2f risk-adj  %s\n",
           v7_session.return_pct - v7.return_pct,
           v7_session.risk_adjusted_score - v7.risk_adjusted_score,
           (v7_session.risk_adjusted_score > v7.risk_adjusted_score) ? "[SYNERGY]" : "[OVER-FILTER]");
    printf("\n");

    // Three-filter combination
    printf("THREE-FILTER COMBINATION:\n");
    printf("  V10 Full vs V7:         %+.2f%% return, %+.2f risk-adj  %s\n",
           v10.return_pct - v7.return_pct,
           v10.risk_adjusted_score - v7.risk_adjusted_score,
           (v10.risk_adjusted_score > v7.risk_adjusted_score) ? "[SYNERGY]" : "[OVER-FILTER]");
    printf("  V10 Full vs V7+Session: %+.2f%% return, %+.2f risk-adj  %s\n",
           v10.return_pct - v7_session.return_pct,
           v10.risk_adjusted_score - v7_session.risk_adjusted_score,
           (v10.risk_adjusted_score > v7_session.risk_adjusted_score) ? "[SYNERGY]" : "[OVER-FILTER]");
    printf("\n");

    // Find best single, best double, and V10
    int best_single = 1;  // Start with V7
    if (mean_rev.risk_adjusted_score > results[best_single].risk_adjusted_score) best_single = 2;
    if (session.risk_adjusted_score > results[best_single].risk_adjusted_score) best_single = 3;

    int best_double = 4;  // Start with V7 + Mean Rev
    if (v7_session.risk_adjusted_score > results[best_double].risk_adjusted_score) best_double = 5;

    printf("SUMMARY:\n");
    printf("  Best Single Filter: %s (Risk-Adj: %.2f)\n",
           results[best_single].filter_name, results[best_single].risk_adjusted_score);
    printf("  Best Double Filter: %s (Risk-Adj: %.2f)\n",
           results[best_double].filter_name, results[best_double].risk_adjusted_score);
    printf("  V10 Full (All 3):   %s (Risk-Adj: %.2f)\n",
           v10.filter_name, v10.risk_adjusted_score);
    printf("\n");

    // Conclusions
    printf("================================================================\n");
    printf("                      CONCLUSIONS\n");
    printf("================================================================\n\n");

    // Determine if over-filtering occurred
    bool v10_beats_all_singles = (v10.risk_adjusted_score > v7.risk_adjusted_score) &&
                                 (v10.risk_adjusted_score > mean_rev.risk_adjusted_score) &&
                                 (v10.risk_adjusted_score > session.risk_adjusted_score);

    bool v10_beats_all_doubles = (v10.risk_adjusted_score > v7_mean_rev.risk_adjusted_score) &&
                                 (v10.risk_adjusted_score > v7_session.risk_adjusted_score);

    bool full_synergy = v10_beats_all_singles && v10_beats_all_doubles;
    bool partial_synergy = v10.risk_adjusted_score > v7.risk_adjusted_score;
    bool over_filtered = !partial_synergy && (v10.trades_completed < v7.trades_completed / 5);

    if (full_synergy) {
        printf("VERDICT: FULL FILTER SYNERGY\n\n");
        printf("The V10 full combination outperforms ALL single and double filter\n");
        printf("combinations. Combining all three filters creates significant synergy.\n\n");
    } else if (partial_synergy) {
        printf("VERDICT: PARTIAL SYNERGY\n\n");
        printf("V10 full combination improves on some but not all configurations.\n");
        printf("Adding filters provides some benefit but diminishing returns.\n\n");
    } else if (over_filtered) {
        printf("VERDICT: OVER-FILTERING DETECTED\n\n");
        printf("The V10 full combination is too restrictive.\n");
        printf("Too few trades (%d) compared to V7 alone (%d).\n",
               v10.trades_completed, v7.trades_completed);
        printf("Simpler filter configurations may be more practical.\n\n");
    } else {
        printf("VERDICT: MIXED RESULTS\n\n");
        printf("Filter combination shows context-dependent results.\n");
        printf("Optimal choice depends on risk tolerance and trading frequency goals.\n\n");
    }

    printf("OPTIMAL CONFIGURATION RECOMMENDATION:\n");
    printf("-------------------------------------\n");

    // Find overall optimal by risk-adjusted score
    int optimal_idx = 0;
    for (size_t i = 1; i < results.size(); i++) {
        if (results[i].risk_adjusted_score > results[optimal_idx].risk_adjusted_score) {
            optimal_idx = (int)i;
        }
    }

    // Also find best for different priorities
    int best_return_overall = 0;
    int best_safety_overall = 0;
    for (size_t i = 1; i < results.size(); i++) {
        if (results[i].return_pct > results[best_return_overall].return_pct) {
            best_return_overall = (int)i;
        }
        if (results[i].max_dd < results[best_safety_overall].max_dd && results[i].return_pct > 0) {
            best_safety_overall = (int)i;
        }
    }

    printf("\n  FOR RISK-ADJUSTED RETURNS:\n");
    printf("    %s\n", results[optimal_idx].filter_name);
    printf("    Return: %.2f%%, Max DD: %.2f%%, Risk-Adj: %.2f\n",
           results[optimal_idx].return_pct, results[optimal_idx].max_dd,
           results[optimal_idx].risk_adjusted_score);

    printf("\n  FOR MAXIMUM RETURNS:\n");
    printf("    %s\n", results[best_return_overall].filter_name);
    printf("    Return: %.2f%%, Max DD: %.2f%%, Risk-Adj: %.2f\n",
           results[best_return_overall].return_pct, results[best_return_overall].max_dd,
           results[best_return_overall].risk_adjusted_score);

    printf("\n  FOR MAXIMUM SAFETY (lowest DD with positive return):\n");
    printf("    %s\n", results[best_safety_overall].filter_name);
    printf("    Return: %.2f%%, Max DD: %.2f%%, Risk-Adj: %.2f\n",
           results[best_safety_overall].return_pct, results[best_safety_overall].max_dd,
           results[best_safety_overall].risk_adjusted_score);

    printf("\n");
    printf("  TRADE-OFF ANALYSIS:\n");
    if (v10.return_pct < v7.return_pct && v10.max_dd < v7.max_dd) {
        printf("  - V10 trades safety for returns (%.2f%% less return for %.2f%% less DD)\n",
               v7.return_pct - v10.return_pct, v7.max_dd - v10.max_dd);
    }
    if (v7_session.risk_adjusted_score > v7.risk_adjusted_score) {
        printf("  - Adding session filter to V7 improves risk-adjusted performance\n");
    }
    if (v10.trades_completed < 20) {
        printf("  - WARNING: V10 Full has very few trades (%d), results may not be statistically significant\n",
               v10.trades_completed);
    }

    printf("\n================================================================\n");
    printf("                     TEST COMPLETE\n");
    printf("================================================================\n");

    return 0;
}
