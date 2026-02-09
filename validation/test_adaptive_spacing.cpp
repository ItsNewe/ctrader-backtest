/**
 * Adaptive Spacing Test
 *
 * Hypothesis: As more positions open, we should space them further apart
 *             to reduce cascade risk.
 *
 * Test these spacing strategies:
 * 1. Fixed: Always 0.75 spacing (baseline)
 * 2. Linear increase: spacing = 0.75 * (1 + 0.1 * position_count)
 *    - Pos 1: 0.75, Pos 5: 1.125, Pos 10: 1.5
 * 3. Exponential increase: spacing = 0.75 * (1.1 ^ position_count)
 * 4. Volatility-adaptive: spacing = 0.75 * (ATR_current / ATR_baseline)
 * 5. Hybrid: Both position count AND volatility adjustment
 *
 * Uses V7 volatility filter, loads 500K XAUUSD ticks
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cfloat>
#include <vector>
#include <deque>
#include <algorithm>

// ============================================================================
// Data Structures
// ============================================================================

struct Tick {
    double bid;
    double ask;
    double spread() const { return ask - bid; }
};

struct Trade {
    size_t id;
    double entry_price;
    double lot_size;
    double take_profit;
};

// ============================================================================
// ATR Calculator (for V7 volatility filter and volatility-adaptive spacing)
// ============================================================================

class ATR {
    std::deque<double> ranges;
    int period;
    double sum = 0;
    double last_price = 0;
public:
    ATR(int p) : period(p) {}

    void Add(double price) {
        if (last_price > 0) {
            double range = fabs(price - last_price);
            ranges.push_back(range);
            sum += range;
            if ((int)ranges.size() > period) {
                sum -= ranges.front();
                ranges.pop_front();
            }
        }
        last_price = price;
    }

    double Get() const { return ranges.empty() ? 0 : sum / ranges.size(); }
    bool IsReady() const { return (int)ranges.size() >= period; }
    void Reset() { ranges.clear(); sum = 0; last_price = 0; }
};

// ============================================================================
// Spacing Strategy Types
// ============================================================================

enum SpacingType {
    SPACING_FIXED,              // Always 0.75
    SPACING_LINEAR,             // 0.75 * (1 + 0.1 * pos_count)
    SPACING_EXPONENTIAL,        // 0.75 * (1.1 ^ pos_count)
    SPACING_VOLATILITY,         // 0.75 * (atr_current / atr_baseline)
    SPACING_HYBRID              // position count + volatility
};

const char* SpacingName(SpacingType type) {
    switch (type) {
        case SPACING_FIXED:       return "Fixed (0.75)";
        case SPACING_LINEAR:      return "Linear (pos count)";
        case SPACING_EXPONENTIAL: return "Exponential (pos count)";
        case SPACING_VOLATILITY:  return "Volatility-adaptive";
        case SPACING_HYBRID:      return "Hybrid (pos + vol)";
        default:                  return "Unknown";
    }
}

// ============================================================================
// Test Result
// ============================================================================

struct TestResult {
    SpacingType type;
    double return_pct;
    double max_dd_pct;
    int max_positions;
    int total_trades;
    int positions_opened;
    double risk_adjusted_score;  // return / max_dd
    double avg_spacing_used;     // average spacing over all opens
};

// ============================================================================
// Calculate Spacing
// ============================================================================

double CalculateSpacing(SpacingType type, int position_count,
                        double atr_current, double atr_baseline) {
    const double BASE_SPACING = 0.75;

    switch (type) {
        case SPACING_FIXED:
            return BASE_SPACING;

        case SPACING_LINEAR:
            // spacing = 0.75 * (1 + 0.1 * pos_count)
            // Pos 0: 0.75, Pos 5: 1.125, Pos 10: 1.5
            return BASE_SPACING * (1.0 + 0.1 * position_count);

        case SPACING_EXPONENTIAL:
            // spacing = 0.75 * (1.1 ^ pos_count)
            // Pos 0: 0.75, Pos 5: 1.21, Pos 10: 1.95
            return BASE_SPACING * pow(1.1, position_count);

        case SPACING_VOLATILITY:
            // spacing = 0.75 * (atr_current / atr_baseline)
            // If current vol is 2x baseline, spacing is 1.5
            if (atr_baseline <= 0) return BASE_SPACING;
            return BASE_SPACING * (atr_current / atr_baseline);

        case SPACING_HYBRID:
            // Combine position count (linear) with volatility
            // spacing = 0.75 * (1 + 0.05 * pos_count) * (atr_current / atr_baseline)
            {
                double pos_factor = 1.0 + 0.05 * position_count;
                double vol_factor = (atr_baseline > 0) ? (atr_current / atr_baseline) : 1.0;
                return BASE_SPACING * pos_factor * vol_factor;
            }

        default:
            return BASE_SPACING;
    }
}

// ============================================================================
// Run Backtest with Spacing Strategy
// ============================================================================

TestResult RunTest(const std::vector<Tick>& ticks, SpacingType spacing_type) {
    TestResult result;
    result.type = spacing_type;
    result.return_pct = 0;
    result.max_dd_pct = 0;
    result.max_positions = 0;
    result.total_trades = 0;
    result.positions_opened = 0;
    result.risk_adjusted_score = 0;
    result.avg_spacing_used = 0;

    if (ticks.empty()) return result;

    // Account settings
    double balance = 10000.0;
    double equity = balance;
    double peak_equity = balance;
    const double CONTRACT_SIZE = 100.0;
    const double LEVERAGE = 500.0;
    const int MAX_POSITIONS = 20;

    // V3 protection thresholds
    const double STOP_NEW_DD = 5.0;
    const double PARTIAL_CLOSE_DD = 8.0;
    const double CLOSE_ALL_DD = 25.0;

    // V7 volatility filter settings
    const int ATR_SHORT_PERIOD = 100;
    const int ATR_LONG_PERIOD = 500;
    const double VOL_THRESHOLD = 0.8;

    // Position tracking
    std::vector<Trade*> positions;
    size_t next_id = 1;
    bool partial_done = false;
    bool all_closed = false;

    // ATR calculators
    ATR atr_short(ATR_SHORT_PERIOD);
    ATR atr_long(ATR_LONG_PERIOD);

    // For volatility-adaptive spacing: track baseline ATR
    double atr_baseline = 0;
    bool baseline_set = false;

    // Tracking for average spacing
    double total_spacing_used = 0;
    int spacing_count = 0;

    for (const Tick& tick : ticks) {
        // Update ATR
        atr_short.Add(tick.bid);
        atr_long.Add(tick.bid);

        // Set baseline once we have enough data
        if (!baseline_set && atr_long.IsReady()) {
            atr_baseline = atr_long.Get();
            baseline_set = true;
        }

        // Update equity
        equity = balance;
        for (Trade* t : positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * CONTRACT_SIZE;
        }

        // Reset peak when no positions
        if (positions.empty() && peak_equity != balance) {
            peak_equity = balance;
            partial_done = false;
            all_closed = false;
        }

        // Track peak equity
        if (equity > peak_equity) {
            peak_equity = equity;
            partial_done = false;
            all_closed = false;
        }

        // Calculate drawdown
        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        result.max_dd_pct = fmax(result.max_dd_pct, dd_pct);

        // Track max positions
        result.max_positions = std::max(result.max_positions, (int)positions.size());

        // V3 Protection: Close ALL at 25% DD
        if (dd_pct > CLOSE_ALL_DD && !all_closed && !positions.empty()) {
            for (Trade* t : positions) {
                balance += (tick.bid - t->entry_price) * t->lot_size * CONTRACT_SIZE;
                result.total_trades++;
                delete t;
            }
            positions.clear();
            all_closed = true;
            equity = balance;
            peak_equity = equity;
            continue;
        }

        // V3 Protection: Partial close at 8% DD
        if (dd_pct > PARTIAL_CLOSE_DD && !partial_done && positions.size() > 1) {
            std::sort(positions.begin(), positions.end(), [&](Trade* a, Trade* b) {
                return (tick.bid - a->entry_price) < (tick.bid - b->entry_price);
            });
            int to_close = (int)(positions.size() * 0.5);
            to_close = std::max(1, to_close);
            for (int i = 0; i < to_close && !positions.empty(); i++) {
                balance += (tick.bid - positions[0]->entry_price) * positions[0]->lot_size * CONTRACT_SIZE;
                result.total_trades++;
                delete positions[0];
                positions.erase(positions.begin());
            }
            partial_done = true;
        }

        // Check take profit
        for (auto it = positions.begin(); it != positions.end();) {
            Trade* t = *it;
            if (tick.bid >= t->take_profit) {
                balance += (t->take_profit - t->entry_price) * t->lot_size * CONTRACT_SIZE;
                result.total_trades++;
                delete t;
                it = positions.erase(it);
            } else {
                ++it;
            }
        }

        // V7 Volatility filter: only trade in low volatility
        bool volatility_ok = true;
        if (atr_short.IsReady() && atr_long.IsReady() && atr_long.Get() > 0) {
            volatility_ok = atr_short.Get() < atr_long.Get() * VOL_THRESHOLD;
        }

        // Only open if DD < threshold and volatility OK
        if (dd_pct >= STOP_NEW_DD || !volatility_ok) {
            continue;
        }

        // Check if we can open new position
        if ((int)positions.size() >= MAX_POSITIONS) {
            continue;
        }

        // Calculate current spacing based on strategy
        double current_atr = atr_short.IsReady() ? atr_short.Get() : 0.05;
        double baseline = baseline_set ? atr_baseline : current_atr;
        double spacing = CalculateSpacing(spacing_type, (int)positions.size(),
                                          current_atr, baseline);

        // Find lowest and highest entry prices
        double lowest = DBL_MAX;
        double highest = DBL_MIN;
        for (Trade* t : positions) {
            lowest = fmin(lowest, t->entry_price);
            highest = fmax(highest, t->entry_price);
        }

        // Determine if we should open
        bool should_open = positions.empty() ||
                          (lowest >= tick.ask + spacing) ||
                          (highest <= tick.ask - spacing);

        if (should_open) {
            // Check margin
            double used_margin = 0;
            for (Trade* t : positions) {
                used_margin += t->lot_size * CONTRACT_SIZE * t->entry_price / LEVERAGE;
            }

            double lot = 0.01;
            double margin_needed = lot * CONTRACT_SIZE * tick.ask / LEVERAGE;

            if (equity - used_margin > margin_needed * 2) {
                Trade* t = new Trade();
                t->id = next_id++;
                t->entry_price = tick.ask;
                t->lot_size = lot;
                t->take_profit = tick.ask + tick.spread() + spacing;
                positions.push_back(t);
                result.positions_opened++;

                // Track spacing for average
                total_spacing_used += spacing;
                spacing_count++;
            }
        }
    }

    // Close remaining positions at end
    for (Trade* t : positions) {
        balance += (ticks.back().bid - t->entry_price) * t->lot_size * CONTRACT_SIZE;
        result.total_trades++;
        delete t;
    }

    // Calculate final metrics
    result.return_pct = (balance - 10000.0) / 10000.0 * 100.0;
    result.avg_spacing_used = spacing_count > 0 ? total_spacing_used / spacing_count : 0.75;
    result.risk_adjusted_score = result.max_dd_pct > 0 ? result.return_pct / result.max_dd_pct : 0;

    return result;
}

// ============================================================================
// Load Ticks using C-style I/O
// ============================================================================

std::vector<Tick> LoadTicks(const char* filename, size_t max_count) {
    std::vector<Tick> ticks;
    ticks.reserve(max_count);

    FILE* file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "ERROR: Cannot open file: %s\n", filename);
        return ticks;
    }

    // Skip header
    char line[512];
    if (!fgets(line, sizeof(line), file)) {
        fclose(file);
        return ticks;
    }

    // Read ticks
    while (fgets(line, sizeof(line), file) && ticks.size() < max_count) {
        // Format: timestamp\tbid\task\t...
        char* token = strtok(line, "\t");
        if (!token) continue;  // timestamp

        token = strtok(NULL, "\t");
        if (!token) continue;
        double bid = atof(token);

        token = strtok(NULL, "\t");
        if (!token) continue;
        double ask = atof(token);

        if (bid > 0 && ask > 0 && ask >= bid) {
            Tick tick;
            tick.bid = bid;
            tick.ask = ask;
            ticks.push_back(tick);
        }

        if (ticks.size() % 100000 == 0) {
            printf("  Loaded %zu ticks...\n", ticks.size());
        }
    }

    fclose(file);
    return ticks;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=============================================================\n");
    printf("ADAPTIVE SPACING TEST\n");
    printf("=============================================================\n\n");

    printf("Hypothesis: As positions accumulate, wider spacing reduces\n");
    printf("cascade risk by preventing position clustering.\n\n");

    printf("Spacing Strategies Tested:\n");
    printf("1. Fixed:       spacing = 0.75 (baseline)\n");
    printf("2. Linear:      spacing = 0.75 * (1 + 0.1 * pos_count)\n");
    printf("   Pos 0: 0.75, Pos 5: 1.125, Pos 10: 1.5\n");
    printf("3. Exponential: spacing = 0.75 * (1.1 ^ pos_count)\n");
    printf("   Pos 0: 0.75, Pos 5: 1.21, Pos 10: 1.95\n");
    printf("4. Volatility:  spacing = 0.75 * (ATR_current / ATR_baseline)\n");
    printf("5. Hybrid:      spacing = 0.75 * (1+0.05*pos) * (ATR/baseline)\n");
    printf("\n");

    // Load data
    const char* filename = "Grid/XAUUSD_TICKS_2025.csv";
    const size_t TICK_COUNT = 500000;

    printf("Loading %zu ticks from %s...\n", TICK_COUNT, filename);
    std::vector<Tick> ticks = LoadTicks(filename, TICK_COUNT);

    if (ticks.empty()) {
        fprintf(stderr, "ERROR: Failed to load tick data\n");
        return 1;
    }

    printf("Loaded %zu ticks successfully\n", ticks.size());

    // Show price range
    double start_price = ticks.front().bid;
    double end_price = ticks.back().bid;
    double price_change = (end_price - start_price) / start_price * 100.0;
    printf("Price: %.2f -> %.2f (%+.2f%%)\n\n", start_price, end_price, price_change);

    // Run all tests
    printf("Running backtests with V7 volatility filter...\n\n");

    std::vector<TestResult> results;

    SpacingType types[] = {
        SPACING_FIXED,
        SPACING_LINEAR,
        SPACING_EXPONENTIAL,
        SPACING_VOLATILITY,
        SPACING_HYBRID
    };

    for (SpacingType type : types) {
        printf("  Testing: %s\n", SpacingName(type));
        TestResult r = RunTest(ticks, type);
        results.push_back(r);
    }

    printf("\n");

    // Print results table
    printf("=============================================================\n");
    printf("RESULTS\n");
    printf("=============================================================\n\n");

    printf("%-22s %8s %8s %8s %8s %8s %10s\n",
           "Strategy", "Return", "Max DD", "MaxPos", "Trades", "AvgSpac", "RiskAdj");
    printf("----------------------------------------------------------------------\n");

    for (const TestResult& r : results) {
        printf("%-22s %7.2f%% %7.2f%% %8d %8d %8.2f %10.3f\n",
               SpacingName(r.type),
               r.return_pct,
               r.max_dd_pct,
               r.max_positions,
               r.total_trades,
               r.avg_spacing_used,
               r.risk_adjusted_score);
    }
    printf("----------------------------------------------------------------------\n\n");

    // Find best by different metrics
    TestResult* best_return = &results[0];
    TestResult* best_dd = &results[0];
    TestResult* best_risk_adj = &results[0];
    TestResult* lowest_max_pos = &results[0];

    for (TestResult& r : results) {
        if (r.return_pct > best_return->return_pct) best_return = &r;
        if (r.max_dd_pct < best_dd->max_dd_pct) best_dd = &r;
        if (r.risk_adjusted_score > best_risk_adj->risk_adjusted_score) best_risk_adj = &r;
        if (r.max_positions < lowest_max_pos->max_positions) lowest_max_pos = &r;
    }

    printf("=============================================================\n");
    printf("ANALYSIS\n");
    printf("=============================================================\n\n");

    printf("Best Return:           %s (%.2f%%)\n",
           SpacingName(best_return->type), best_return->return_pct);
    printf("Lowest Max DD:         %s (%.2f%%)\n",
           SpacingName(best_dd->type), best_dd->max_dd_pct);
    printf("Best Risk-Adjusted:    %s (%.3f)\n",
           SpacingName(best_risk_adj->type), best_risk_adj->risk_adjusted_score);
    printf("Lowest Max Positions:  %s (%d)\n",
           SpacingName(lowest_max_pos->type), lowest_max_pos->max_positions);

    printf("\n");

    // Compare to fixed baseline
    TestResult& baseline = results[0];  // SPACING_FIXED

    printf("=============================================================\n");
    printf("COMPARISON TO FIXED SPACING BASELINE\n");
    printf("=============================================================\n\n");

    for (size_t i = 1; i < results.size(); i++) {
        TestResult& r = results[i];
        double return_diff = r.return_pct - baseline.return_pct;
        double dd_diff = r.max_dd_pct - baseline.max_dd_pct;
        int pos_diff = r.max_positions - baseline.max_positions;

        printf("%-22s vs Fixed:\n", SpacingName(r.type));
        printf("  Return:     %+.2f%% (%s)\n",
               return_diff, return_diff >= 0 ? "BETTER" : "worse");
        printf("  Max DD:     %+.2f%% (%s)\n",
               dd_diff, dd_diff <= 0 ? "BETTER" : "worse");
        printf("  Max Pos:    %+d (%s)\n",
               pos_diff, pos_diff <= 0 ? "BETTER" : "worse");
        printf("\n");
    }

    // Conclusion
    printf("=============================================================\n");
    printf("CONCLUSION\n");
    printf("=============================================================\n\n");

    // Determine overall best
    // Weight: 40% return, 40% risk-adjusted, 20% lower DD
    double scores[5];
    double max_return = 0, min_return = DBL_MAX;
    double max_risk_adj = 0, min_risk_adj = DBL_MAX;
    double max_dd_val = 0, min_dd_val = DBL_MAX;

    for (const TestResult& r : results) {
        max_return = fmax(max_return, r.return_pct);
        min_return = fmin(min_return, r.return_pct);
        max_risk_adj = fmax(max_risk_adj, r.risk_adjusted_score);
        min_risk_adj = fmin(min_risk_adj, r.risk_adjusted_score);
        max_dd_val = fmax(max_dd_val, r.max_dd_pct);
        min_dd_val = fmin(min_dd_val, r.max_dd_pct);
    }

    double best_score = -DBL_MAX;
    int best_idx = 0;

    for (size_t i = 0; i < results.size(); i++) {
        const TestResult& r = results[i];

        // Normalize metrics (0-1)
        double norm_return = (max_return > min_return) ?
            (r.return_pct - min_return) / (max_return - min_return) : 0.5;
        double norm_risk_adj = (max_risk_adj > min_risk_adj) ?
            (r.risk_adjusted_score - min_risk_adj) / (max_risk_adj - min_risk_adj) : 0.5;
        double norm_dd = (max_dd_val > min_dd_val) ?
            1.0 - (r.max_dd_pct - min_dd_val) / (max_dd_val - min_dd_val) : 0.5;  // Lower is better

        scores[i] = 0.4 * norm_return + 0.4 * norm_risk_adj + 0.2 * norm_dd;

        if (scores[i] > best_score) {
            best_score = scores[i];
            best_idx = (int)i;
        }
    }

    printf("Weighted Score (40%% return, 40%% risk-adj, 20%% low DD):\n");
    for (size_t i = 0; i < results.size(); i++) {
        printf("  %-22s: %.3f%s\n",
               SpacingName(results[i].type),
               scores[i],
               i == (size_t)best_idx ? " <-- BEST" : "");
    }
    printf("\n");

    printf("RECOMMENDATION:\n");
    printf("  %s is the best spacing strategy overall.\n\n", SpacingName(results[best_idx].type));

    // Theory check
    printf("Theory check:\n");
    if (best_idx == 0) {
        printf("  Fixed spacing performed best. Adaptive spacing did NOT help.\n");
        printf("  Possible reasons:\n");
        printf("  - Market conditions during test period didn't require adaptation\n");
        printf("  - Base spacing of 0.75 was already optimal\n");
        printf("  - V7 volatility filter already handles risk well\n");
    } else if (best_idx == 3 || best_idx == 4) {
        printf("  Volatility-based spacing performed best!\n");
        printf("  This confirms that adapting to market conditions helps.\n");
        printf("  Higher volatility -> wider spacing -> better risk control.\n");
    } else {
        printf("  Position-based spacing performed best!\n");
        printf("  This confirms: wider spacing as positions accumulate\n");
        printf("  reduces cascade risk and improves risk-adjusted returns.\n");
    }

    printf("\n=============================================================\n");

    return 0;
}
