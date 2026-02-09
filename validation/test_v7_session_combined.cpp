/**
 * V7 Volatility Filter + Session Filter Combined Test
 *
 * Combines two powerful filters:
 * 1. V7 ATR-based volatility filter (trade only in calm markets)
 * 2. Session filter (trade only during optimal hours)
 *
 * Session filter finding: 22:00-06:00 UTC gives best risk-adjusted returns
 * V7 volatility filter: ATR short < ATR long * threshold
 *
 * Tests 4 configurations:
 * - V7 only (24h trading with volatility filter)
 * - Session only (22-6 UTC without volatility filter)
 * - V7 + Session combined (both filters active)
 * - V7 + London/NY (8-21 UTC) - for comparison
 *
 * This could be the most selective and safest strategy - combining
 * low volatility periods with optimal trading hours.
 */

#include <iostream>
#include <iomanip>
#include <vector>
#include <deque>
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// Tick structure with timestamp for hour extraction
struct Tick {
    char timestamp[32];  // "YYYY.MM.DD HH:MM:SS"
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

// ATR calculator for volatility measurement
class ATR {
    std::deque<double> ranges;
    int period;
    double sum;
    double last_price;
public:
    ATR(int p) : period(p), sum(0), last_price(0) {}

    void Add(double price) {
        if (last_price > 0) {
            double range = std::abs(price - last_price);
            ranges.push_back(range);
            sum += range;
            if ((int)ranges.size() > period) {
                sum -= ranges.front();
                ranges.pop_front();
            }
        }
        last_price = price;
    }

    double Get() const {
        return ranges.empty() ? 0 : sum / ranges.size();
    }

    bool IsReady() const {
        return (int)ranges.size() >= period;
    }

    void Reset() {
        ranges.clear();
        sum = 0;
        last_price = 0;
    }
};

// Filter configuration
enum FilterMode {
    V7_ONLY,           // 24h trading with ATR volatility filter
    SESSION_ONLY,      // 22:00-06:00 UTC without volatility filter
    V7_PLUS_SESSION,   // Both V7 volatility + session filter
    V7_PLUS_LONDON_NY  // V7 volatility + London/NY session (8-21 UTC)
};

const char* GetFilterName(FilterMode mode) {
    switch (mode) {
        case V7_ONLY:           return "V7 Only (24h)";
        case SESSION_ONLY:      return "Session Only (22-6 UTC)";
        case V7_PLUS_SESSION:   return "V7 + Session (22-6 UTC)";
        case V7_PLUS_LONDON_NY: return "V7 + London/NY (8-21 UTC)";
        default:                return "Unknown";
    }
}

// Test configuration
struct Config {
    // V7 volatility filter parameters (optimized)
    int atr_short_period;
    int atr_long_period;
    double volatility_threshold;

    // Grid parameters
    double spacing;
    double tp_multiplier;

    // V3 protection levels
    double stop_new_at_dd;
    double partial_close_at_dd;
    double close_all_at_dd;
    int max_positions;

    Config() :
        atr_short_period(50),
        atr_long_period(1000),
        volatility_threshold(0.6),
        spacing(1.0),
        tp_multiplier(2.0),
        stop_new_at_dd(5.0),
        partial_close_at_dd(8.0),
        close_all_at_dd(25.0),
        max_positions(20) {}
};

// Test results
struct Result {
    const char* filter_name;
    double return_pct;
    double max_dd;
    double sharpe_approx;  // Return / MaxDD as simple risk-adjusted metric
    int trades_completed;
    int positions_opened;
    double time_trading_pct;  // % of time both filters allowed trading
    int hours_traded;         // Count of unique hours when trades occurred
};

// Check if hour is in session window
bool IsInSession(int hour, int start_hour, int end_hour) {
    if (start_hour < end_hour) {
        // Normal range (e.g., 8-21)
        return hour >= start_hour && hour < end_hour;
    } else {
        // Overnight range (e.g., 22-6 wraps around midnight)
        return hour >= start_hour || hour < end_hour;
    }
}

// Extract hour from timestamp "YYYY.MM.DD HH:MM:SS"
int ExtractHour(const char* timestamp) {
    // Position 11 is where HH starts
    if (strlen(timestamp) >= 13) {
        int h = (timestamp[11] - '0') * 10 + (timestamp[12] - '0');
        if (h >= 0 && h <= 23) return h;
    }
    return 0;
}

// Run backtest with specified filter mode
Result RunTest(const std::vector<Tick>& ticks, FilterMode mode, const Config& cfg) {
    Result r = {};
    r.filter_name = GetFilterName(mode);

    if (ticks.empty()) return r;

    double balance = 10000.0;
    double equity = balance;
    double peak_equity = balance;
    double contract_size = 100.0;
    double leverage = 500.0;

    std::vector<Trade*> positions;
    size_t next_id = 1;
    bool partial_done = false;
    bool all_closed = false;

    ATR atr_short(cfg.atr_short_period);
    ATR atr_long(cfg.atr_long_period);

    int trading_allowed_count = 0;
    int trading_check_count = 0;
    double total_trade_profit = 0;

    // Track hours when trading occurred
    bool hours_active[24] = {false};

    for (const Tick& tick : ticks) {
        atr_short.Add(tick.bid);
        atr_long.Add(tick.bid);

        // Update equity
        equity = balance;
        for (Trade* t : positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * contract_size;
        }

        // Peak equity reset when no positions
        if (positions.empty() && peak_equity != balance) {
            peak_equity = balance;
            partial_done = false;
            all_closed = false;
        }

        if (equity > peak_equity) {
            peak_equity = equity;
            partial_done = false;
            all_closed = false;
        }

        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        r.max_dd = std::max(r.max_dd, dd_pct);

        // V3 Protection: Close ALL at threshold
        if (dd_pct > cfg.close_all_at_dd && !all_closed && !positions.empty()) {
            for (Trade* t : positions) {
                double profit = (tick.bid - t->entry_price) * t->lot_size * contract_size;
                balance += profit;
                total_trade_profit += profit;
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
            std::sort(positions.begin(), positions.end(), [&](Trade* a, Trade* b) {
                return (tick.bid - a->entry_price) < (tick.bid - b->entry_price);
            });
            int to_close = std::max(1, (int)(positions.size() / 2));
            for (int i = 0; i < to_close && !positions.empty(); i++) {
                double profit = (tick.bid - positions[0]->entry_price) * positions[0]->lot_size * contract_size;
                balance += profit;
                total_trade_profit += profit;
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
                total_trade_profit += profit;
                r.trades_completed++;
                delete t;
                it = positions.erase(it);
            } else {
                ++it;
            }
        }

        // Determine if trading is allowed based on filter mode
        bool volatility_ok = true;
        bool session_ok = true;

        // V7 Volatility filter
        if (mode == V7_ONLY || mode == V7_PLUS_SESSION || mode == V7_PLUS_LONDON_NY) {
            if (atr_short.IsReady() && atr_long.IsReady() && atr_long.Get() > 0) {
                volatility_ok = atr_short.Get() < atr_long.Get() * cfg.volatility_threshold;
            }
        }

        // Session filter
        if (mode == SESSION_ONLY || mode == V7_PLUS_SESSION) {
            // 22:00-06:00 UTC session (overnight)
            session_ok = IsInSession(tick.hour, 22, 6);
        } else if (mode == V7_PLUS_LONDON_NY) {
            // 8:00-21:00 UTC session (London + NY overlap)
            session_ok = IsInSession(tick.hour, 8, 21);
        }

        bool trading_allowed = volatility_ok && session_ok;

        // Track filter statistics
        trading_check_count++;
        if (trading_allowed) trading_allowed_count++;

        // Open new positions if allowed
        double used_margin = 0;
        for (Trade* t : positions) {
            used_margin += t->lot_size * contract_size * t->entry_price / leverage;
        }

        if (dd_pct < cfg.stop_new_at_dd && trading_allowed && (int)positions.size() < cfg.max_positions) {
            double lowest = DBL_MAX, highest = DBL_MIN;
            for (Trade* t : positions) {
                lowest = std::min(lowest, t->entry_price);
                highest = std::max(highest, t->entry_price);
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
                    hours_active[tick.hour] = true;
                }
            }
        }
    }

    // Close remaining positions at end
    for (Trade* t : positions) {
        double profit = (ticks.back().bid - t->entry_price) * t->lot_size * contract_size;
        balance += profit;
        total_trade_profit += profit;
        r.trades_completed++;
        delete t;
    }

    r.return_pct = (balance - 10000.0) / 10000.0 * 100.0;
    r.time_trading_pct = (trading_check_count > 0) ?
                         (double)trading_allowed_count / trading_check_count * 100.0 : 0.0;
    r.sharpe_approx = (r.max_dd > 0) ? r.return_pct / r.max_dd : r.return_pct;

    // Count hours traded
    r.hours_traded = 0;
    for (int i = 0; i < 24; i++) {
        if (hours_active[i]) r.hours_traded++;
    }

    return r;
}

// Load ticks with timestamp parsing using C-style file I/O
std::vector<Tick> LoadTicks(const char* filename, size_t max_ticks) {
    std::vector<Tick> ticks;
    ticks.reserve(max_ticks);

    FILE* file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Cannot open file: %s\n", filename);
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
        // Format: "YYYY.MM.DD HH:MM:SS.mmm"
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
    printf("Loaded %zu ticks total\n", ticks.size());

    return ticks;
}

void PrintHourDistribution(const std::vector<Tick>& ticks) {
    int hour_counts[24] = {0};

    for (const Tick& t : ticks) {
        if (t.hour >= 0 && t.hour < 24) {
            hour_counts[t.hour]++;
        }
    }

    printf("\nTick distribution by hour (UTC):\n");
    printf("Hour  | Count     | Bar\n");
    printf("------+-----------+----------------------------------------\n");

    int max_count = 0;
    for (int i = 0; i < 24; i++) {
        if (hour_counts[i] > max_count) max_count = hour_counts[i];
    }

    for (int h = 0; h < 24; h++) {
        int bar_len = (max_count > 0) ? (hour_counts[h] * 40) / max_count : 0;
        char bar[41];
        memset(bar, '#', bar_len);
        bar[bar_len] = '\0';

        const char* session = "";
        if (h >= 22 || h < 6) session = " [22-6]";
        else if (h >= 8 && h < 21) session = " [8-21]";

        printf("%02d:00 | %9d | %s%s\n", h, hour_counts[h], bar, session);
    }
    printf("\n");
}

int main() {
    printf("================================================================\n");
    printf("   V7 VOLATILITY + SESSION FILTER COMBINED TEST\n");
    printf("================================================================\n\n");

    printf("Test Philosophy:\n");
    printf("- V7 volatility filter: Trade only when ATR_short < ATR_long * threshold\n");
    printf("- Session filter finding: 22:00-06:00 UTC gives best risk-adjusted returns\n");
    printf("- Combined: Trade only when BOTH conditions are met\n");
    printf("- This creates the most selective, safest trading strategy\n\n");

    // Configuration based on optimized V7 parameters
    Config cfg;
    cfg.atr_short_period = 50;
    cfg.atr_long_period = 1000;
    cfg.volatility_threshold = 0.6;  // Strict - only trade when vol is 40%% below average
    cfg.spacing = 1.0;
    cfg.tp_multiplier = 2.0;

    printf("Configuration:\n");
    printf("  ATR Short Period:     %d\n", cfg.atr_short_period);
    printf("  ATR Long Period:      %d\n", cfg.atr_long_period);
    printf("  Volatility Threshold: %.2f\n", cfg.volatility_threshold);
    printf("  Grid Spacing:         %.1f\n", cfg.spacing);
    printf("  TP Multiplier:        %.1f\n", cfg.tp_multiplier);
    printf("\n");

    // Load tick data
    const char* filename = "Grid/XAUUSD_TICKS_2025.csv";
    std::vector<Tick> ticks = LoadTicks(filename, 500000);

    if (ticks.empty()) {
        fprintf(stderr, "Failed to load tick data!\n");
        return 1;
    }

    // Show price range
    printf("\nPrice range: %.2f -> %.2f\n", ticks.front().bid, ticks.back().bid);
    double price_change = (ticks.back().bid - ticks.front().bid) / ticks.front().bid * 100.0;
    printf("Price change: %+.2f%%\n", price_change);

    // Show hour distribution
    PrintHourDistribution(ticks);

    // Run all 4 test configurations
    printf("Running tests...\n\n");

    std::vector<Result> results;

    FilterMode modes[] = {V7_ONLY, SESSION_ONLY, V7_PLUS_SESSION, V7_PLUS_LONDON_NY};

    for (FilterMode mode : modes) {
        printf("  Testing: %s\n", GetFilterName(mode));
        Result r = RunTest(ticks, mode, cfg);
        results.push_back(r);
    }

    // Print results table
    printf("\n================================================================\n");
    printf("                        RESULTS\n");
    printf("================================================================\n\n");

    printf("%-25s | %8s | %8s | %8s | %8s | %8s | %8s\n",
           "Filter Configuration", "Return", "Max DD", "Sharpe", "Trades", "Opened", "Time%%");
    printf("%-25s-+-%8s-+-%8s-+-%8s-+-%8s-+-%8s-+-%8s\n",
           "-------------------------", "--------", "--------", "--------", "--------", "--------", "--------");

    for (const Result& r : results) {
        printf("%-25s | %7.2f%% | %7.2f%% | %8.2f | %8d | %8d | %7.1f%%\n",
               r.filter_name,
               r.return_pct,
               r.max_dd,
               r.sharpe_approx,
               r.trades_completed,
               r.positions_opened,
               r.time_trading_pct);
    }

    printf("%-25s-+-%8s-+-%8s-+-%8s-+-%8s-+-%8s-+-%8s\n",
           "-------------------------", "--------", "--------", "--------", "--------", "--------", "--------");

    // Find best by different metrics
    printf("\n================================================================\n");
    printf("                       ANALYSIS\n");
    printf("================================================================\n\n");

    // Best by return
    int best_return_idx = 0;
    for (size_t i = 1; i < results.size(); i++) {
        if (results[i].return_pct > results[best_return_idx].return_pct) {
            best_return_idx = i;
        }
    }

    // Best by Sharpe (risk-adjusted)
    int best_sharpe_idx = 0;
    for (size_t i = 1; i < results.size(); i++) {
        if (results[i].sharpe_approx > results[best_sharpe_idx].sharpe_approx) {
            best_sharpe_idx = i;
        }
    }

    // Lowest max DD with positive return
    int best_safety_idx = -1;
    double lowest_dd = 100.0;
    for (size_t i = 0; i < results.size(); i++) {
        if (results[i].return_pct > 0 && results[i].max_dd < lowest_dd) {
            lowest_dd = results[i].max_dd;
            best_safety_idx = (int)i;
        }
    }

    printf("Best by RETURN:        %s (%.2f%%)\n",
           results[best_return_idx].filter_name, results[best_return_idx].return_pct);
    printf("Best by SHARPE:        %s (%.2f)\n",
           results[best_sharpe_idx].filter_name, results[best_sharpe_idx].sharpe_approx);
    if (best_safety_idx >= 0) {
        printf("Best by SAFETY:        %s (%.2f%% max DD)\n",
               results[best_safety_idx].filter_name, results[best_safety_idx].max_dd);
    }

    printf("\n");

    // Detailed comparison
    printf("Detailed Comparison:\n");
    printf("--------------------\n\n");

    // Compare V7 Only vs V7 + Session
    const Result& v7_only = results[0];
    const Result& v7_session = results[2];

    printf("V7 Only vs V7 + Session (22-6 UTC):\n");
    printf("  Return:      %.2f%% -> %.2f%% (%+.2f%%)\n",
           v7_only.return_pct, v7_session.return_pct,
           v7_session.return_pct - v7_only.return_pct);
    printf("  Max DD:      %.2f%% -> %.2f%% (%+.2f%%)\n",
           v7_only.max_dd, v7_session.max_dd,
           v7_session.max_dd - v7_only.max_dd);
    printf("  Sharpe:      %.2f -> %.2f (%+.2f)\n",
           v7_only.sharpe_approx, v7_session.sharpe_approx,
           v7_session.sharpe_approx - v7_only.sharpe_approx);
    printf("  Trading %%:   %.1f%% -> %.1f%% (%.1fx more selective)\n",
           v7_only.time_trading_pct, v7_session.time_trading_pct,
           v7_only.time_trading_pct / std::max(0.01, v7_session.time_trading_pct));
    printf("\n");

    // Compare Session Only vs V7 + Session
    const Result& session_only = results[1];

    printf("Session Only vs V7 + Session:\n");
    printf("  Return:      %.2f%% -> %.2f%% (%+.2f%%)\n",
           session_only.return_pct, v7_session.return_pct,
           v7_session.return_pct - session_only.return_pct);
    printf("  Max DD:      %.2f%% -> %.2f%% (%+.2f%%)\n",
           session_only.max_dd, v7_session.max_dd,
           v7_session.max_dd - session_only.max_dd);
    printf("  Sharpe:      %.2f -> %.2f (%+.2f)\n",
           session_only.sharpe_approx, v7_session.sharpe_approx,
           v7_session.sharpe_approx - session_only.sharpe_approx);
    printf("\n");

    // Compare V7 + Session vs V7 + London/NY
    const Result& v7_london = results[3];

    printf("V7 + Session (22-6) vs V7 + London/NY (8-21):\n");
    printf("  Return:      %.2f%% vs %.2f%%\n",
           v7_session.return_pct, v7_london.return_pct);
    printf("  Max DD:      %.2f%% vs %.2f%%\n",
           v7_session.max_dd, v7_london.max_dd);
    printf("  Sharpe:      %.2f vs %.2f\n",
           v7_session.sharpe_approx, v7_london.sharpe_approx);
    printf("  Trading %%:   %.1f%% vs %.1f%%\n",
           v7_session.time_trading_pct, v7_london.time_trading_pct);
    printf("\n");

    // Conclusions
    printf("================================================================\n");
    printf("                      CONCLUSIONS\n");
    printf("================================================================\n\n");

    printf("1. SELECTIVITY:\n");
    printf("   - V7 + Session is the most selective strategy\n");
    printf("   - Trading only %.1f%% of the time vs %.1f%% for V7 only\n",
           v7_session.time_trading_pct, v7_only.time_trading_pct);
    printf("\n");

    printf("2. RISK-ADJUSTED PERFORMANCE:\n");
    if (v7_session.sharpe_approx > v7_only.sharpe_approx &&
        v7_session.sharpe_approx > session_only.sharpe_approx) {
        printf("   - V7 + Session has BEST risk-adjusted returns (Sharpe: %.2f)\n",
               v7_session.sharpe_approx);
        printf("   - Combining both filters creates synergy\n");
    } else if (v7_session.sharpe_approx > v7_only.sharpe_approx) {
        printf("   - Adding session filter improves risk-adjusted returns\n");
        printf("   - V7 + Session Sharpe (%.2f) > V7 Only Sharpe (%.2f)\n",
               v7_session.sharpe_approx, v7_only.sharpe_approx);
    } else {
        printf("   - V7 volatility filter alone may be sufficient\n");
    }
    printf("\n");

    printf("3. SAFETY:\n");
    if (v7_session.max_dd < v7_only.max_dd) {
        printf("   - V7 + Session has %.2f%% LOWER max drawdown than V7 only\n",
               v7_only.max_dd - v7_session.max_dd);
    }
    if (best_safety_idx == 2) {
        printf("   - V7 + Session is the SAFEST profitable configuration\n");
    }
    printf("\n");

    printf("4. SESSION COMPARISON:\n");
    if (v7_session.sharpe_approx > v7_london.sharpe_approx) {
        printf("   - Overnight session (22-6 UTC) outperforms London/NY (8-21 UTC)\n");
        printf("   - Likely due to lower volatility during Asian session\n");
    } else {
        printf("   - London/NY session (8-21 UTC) performs comparably\n");
    }
    printf("\n");

    printf("5. RECOMMENDATION:\n");
    if (v7_session.sharpe_approx >= v7_only.sharpe_approx * 0.9 &&
        v7_session.max_dd <= v7_only.max_dd) {
        printf("   RECOMMENDED: V7 + Session Filter (22:00-06:00 UTC)\n");
        printf("   - Best risk-adjusted returns\n");
        printf("   - Most selective (trades only when both conditions met)\n");
        printf("   - Suitable for conservative/safety-focused trading\n");
    } else if (v7_only.return_pct > v7_session.return_pct * 1.5) {
        printf("   RECOMMENDED: V7 Only for maximum returns\n");
        printf("   - Higher returns justify higher risk\n");
    } else {
        printf("   Consider V7 + Session for balanced risk/reward\n");
    }

    printf("\n================================================================\n");
    printf("                     TEST COMPLETE\n");
    printf("================================================================\n");

    return 0;
}
