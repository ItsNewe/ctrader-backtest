/**
 * Position Scaling Test
 *
 * Hypothesis: As more positions accumulate, risk increases.
 * Reducing lot size for later positions should reduce cascade risk.
 *
 * Test scenarios:
 * a) Fixed: All positions 0.01 lots (baseline)
 * b) Linear: Pos 1-5 = 0.01, Pos 6-10 = 0.0075, Pos 11-15 = 0.005, Pos 16-20 = 0.0025
 * c) Inverse: lot = 0.01 / sqrt(position_count)
 * d) Aggressive inverse: lot = 0.01 / position_count
 *
 * Uses V7 strategy with optimized params and volatility filter.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cfloat>
#include <vector>
#include <algorithm>
#include <deque>

// Scaling mode enum
enum ScalingMode {
    FIXED,              // All positions same size
    LINEAR_TIERED,      // Tiered reduction by position count
    INVERSE_SQRT,       // lot = base / sqrt(pos_count)
    AGGRESSIVE_INVERSE  // lot = base / pos_count
};

const char* ScalingName(ScalingMode mode) {
    switch (mode) {
        case FIXED: return "Fixed (0.01)";
        case LINEAR_TIERED: return "Linear Tiered";
        case INVERSE_SQRT: return "Inverse Sqrt";
        case AGGRESSIVE_INVERSE: return "Aggressive Inverse";
        default: return "Unknown";
    }
}

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

// ATR calculator for volatility filter
class ATR {
    std::deque<double> ranges;
    int period;
    double sum;
    double last_price;
public:
    ATR(int p) : period(p), sum(0), last_price(0) {}

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
};

struct Result {
    ScalingMode mode;
    double return_pct;
    double max_dd_pct;
    double max_dd_dollars;
    int total_trades;
    int positions_opened;
    double total_lots_traded;
    double avg_lot_size;
    double risk_adjusted_score;  // return / max_dd
    double final_balance;
};

// Calculate lot size based on scaling mode and current position count
double CalculateLotSize(ScalingMode mode, int position_count, double base_lot) {
    double lot = base_lot;

    switch (mode) {
        case FIXED:
            lot = base_lot;
            break;

        case LINEAR_TIERED:
            // Pos 1-5 = 0.01, Pos 6-10 = 0.0075, Pos 11-15 = 0.005, Pos 16-20 = 0.0025
            if (position_count < 5) {
                lot = base_lot;           // 0.01
            } else if (position_count < 10) {
                lot = base_lot * 0.75;    // 0.0075
            } else if (position_count < 15) {
                lot = base_lot * 0.5;     // 0.005
            } else {
                lot = base_lot * 0.25;    // 0.0025
            }
            break;

        case INVERSE_SQRT:
            // lot = base / sqrt(pos_count + 1)
            lot = base_lot / sqrt((double)(position_count + 1));
            break;

        case AGGRESSIVE_INVERSE:
            // lot = base / (pos_count + 1)
            lot = base_lot / (double)(position_count + 1);
            break;
    }

    // Enforce minimum lot size (0.001) and round to 3 decimals
    lot = fmax(0.001, lot);
    lot = round(lot * 1000.0) / 1000.0;

    return lot;
}

Result RunTest(const std::vector<Tick>& ticks, ScalingMode mode) {
    Result r;
    memset(&r, 0, sizeof(r));
    r.mode = mode;

    if (ticks.empty()) return r;

    // V7 optimized parameters
    double balance = 10000.0;
    double equity = balance;
    double peak_equity = balance;
    double contract_size = 100.0;
    double leverage = 500.0;
    double spacing = 1.0;
    double base_lot = 0.01;
    int max_positions = 20;

    // V3 protection thresholds
    double stop_new_at_dd = 5.0;
    double partial_close_at_dd = 8.0;
    double close_all_at_dd = 25.0;

    // V7 volatility filter params
    int atr_short_period = 100;
    int atr_long_period = 500;
    double volatility_threshold = 0.8;

    std::vector<Trade*> positions;
    size_t next_id = 1;
    bool partial_done = false;
    bool all_closed = false;
    double max_dd_dollars = 0.0;

    ATR atr_short(atr_short_period);
    ATR atr_long(atr_long_period);

    double total_lots_traded = 0.0;

    for (const Tick& tick : ticks) {
        // Update ATR
        atr_short.Add(tick.bid);
        atr_long.Add(tick.bid);

        // Calculate equity
        equity = balance;
        for (Trade* t : positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * contract_size;
        }

        // Reset peak when flat
        if (positions.empty() && peak_equity != balance) {
            peak_equity = balance;
            partial_done = false;
            all_closed = false;
        }

        // Track peak
        if (equity > peak_equity) {
            peak_equity = equity;
            partial_done = false;
            all_closed = false;
        }

        // Calculate drawdown
        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        double dd_dollars = peak_equity - equity;
        r.max_dd_pct = fmax(r.max_dd_pct, dd_pct);
        max_dd_dollars = fmax(max_dd_dollars, dd_dollars);

        // V3 Protection: Close ALL at threshold
        if (dd_pct > close_all_at_dd && !all_closed && !positions.empty()) {
            for (Trade* t : positions) {
                double pl = (tick.bid - t->entry_price) * t->lot_size * contract_size;
                balance += pl;
                r.total_trades++;
                delete t;
            }
            positions.clear();
            all_closed = true;
            equity = balance;
            peak_equity = equity;
            continue;
        }

        // V3 Protection: Partial close at threshold
        if (dd_pct > partial_close_at_dd && !partial_done && positions.size() > 1) {
            // Sort by P/L (worst first)
            std::sort(positions.begin(), positions.end(), [&](Trade* a, Trade* b) {
                return (tick.bid - a->entry_price) < (tick.bid - b->entry_price);
            });

            int to_close = (int)(positions.size() * 0.5);
            to_close = std::max(1, to_close);

            for (int i = 0; i < to_close && !positions.empty(); i++) {
                double pl = (tick.bid - positions[0]->entry_price) * positions[0]->lot_size * contract_size;
                balance += pl;
                r.total_trades++;
                delete positions[0];
                positions.erase(positions.begin());
            }
            partial_done = true;
        }

        // Check TP for open positions
        for (auto it = positions.begin(); it != positions.end();) {
            Trade* t = *it;
            if (tick.bid >= t->take_profit) {
                double pl = (t->take_profit - t->entry_price) * t->lot_size * contract_size;
                balance += pl;
                r.total_trades++;
                delete t;
                it = positions.erase(it);
            } else {
                ++it;
            }
        }

        // V7 Volatility filter check
        bool volatility_ok = true;
        if (atr_short.IsReady() && atr_long.IsReady() && atr_long.Get() > 0) {
            volatility_ok = atr_short.Get() < atr_long.Get() * volatility_threshold;
        }

        // Open new positions if conditions met
        if (dd_pct < stop_new_at_dd && volatility_ok && (int)positions.size() < max_positions) {
            // Calculate grid levels
            double lowest = DBL_MAX, highest = DBL_MIN;
            for (Trade* t : positions) {
                lowest = std::min(lowest, t->entry_price);
                highest = std::max(highest, t->entry_price);
            }

            // Check if we should open
            bool should_open = positions.empty() ||
                              (lowest >= tick.ask + spacing) ||
                              (highest <= tick.ask - spacing);

            if (should_open) {
                // Calculate lot size based on scaling mode
                double lot = CalculateLotSize(mode, (int)positions.size(), base_lot);

                // Check margin
                double used_margin = 0;
                for (Trade* t : positions) {
                    used_margin += t->lot_size * contract_size * t->entry_price / leverage;
                }
                double margin_needed = lot * contract_size * tick.ask / leverage;

                if (equity - used_margin > margin_needed * 2) {
                    Trade* t = new Trade();
                    t->id = next_id++;
                    t->entry_price = tick.ask;
                    t->lot_size = lot;
                    t->take_profit = tick.ask + tick.spread() + spacing;
                    positions.push_back(t);
                    r.positions_opened++;
                    total_lots_traded += lot;
                }
            }
        }
    }

    // Close remaining positions at end
    for (Trade* t : positions) {
        double pl = (ticks.back().bid - t->entry_price) * t->lot_size * contract_size;
        balance += pl;
        r.total_trades++;
        delete t;
    }
    positions.clear();

    r.final_balance = balance;
    r.return_pct = (balance - 10000.0) / 10000.0 * 100.0;
    r.max_dd_dollars = max_dd_dollars;
    r.total_lots_traded = total_lots_traded;
    r.avg_lot_size = (r.positions_opened > 0) ? total_lots_traded / r.positions_opened : 0.0;

    // Risk-adjusted score: return / max_dd (higher is better)
    r.risk_adjusted_score = (r.max_dd_pct > 0) ? r.return_pct / r.max_dd_pct : 0.0;

    return r;
}

// Load ticks using C-style file I/O
std::vector<Tick> LoadTicks(const char* filename, size_t max_count) {
    std::vector<Tick> ticks;
    ticks.reserve(max_count);

    FILE* f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Error: Cannot open file %s\n", filename);
        return ticks;
    }

    // Skip header
    char line[1024];
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return ticks;
    }

    while (fgets(line, sizeof(line), f) && ticks.size() < max_count) {
        // Parse tab-delimited: timestamp\tbid\task\t...
        char* token = strtok(line, "\t");
        if (!token) continue;  // timestamp

        token = strtok(NULL, "\t");
        if (!token) continue;
        double bid = atof(token);

        token = strtok(NULL, "\t");
        if (!token) continue;
        double ask = atof(token);

        if (bid > 0 && ask > 0) {
            Tick tick;
            tick.bid = bid;
            tick.ask = ask;
            ticks.push_back(tick);
        }

        if (ticks.size() % 100000 == 0) {
            printf("  Loaded %zu ticks...\n", ticks.size());
        }
    }

    fclose(f);
    return ticks;
}

int main() {
    printf("=====================================================\n");
    printf("POSITION SCALING TEST\n");
    printf("=====================================================\n");
    printf("\n");

    printf("Hypothesis:\n");
    printf("As more positions accumulate, risk increases.\n");
    printf("Reducing lot size for later positions should reduce cascade risk.\n");
    printf("\n");

    printf("Scaling Modes:\n");
    printf("a) Fixed:      All positions 0.01 lots (baseline)\n");
    printf("b) Linear:     Pos 1-5=0.01, 6-10=0.0075, 11-15=0.005, 16-20=0.0025\n");
    printf("c) Inverse:    lot = 0.01 / sqrt(position_count + 1)\n");
    printf("d) Aggressive: lot = 0.01 / (position_count + 1)\n");
    printf("\n");

    // Show lot size examples for each mode
    printf("Lot sizes by position count:\n");
    printf("%-20s %6s %6s %6s %6s %6s %6s\n", "Mode", "Pos 1", "Pos 5", "Pos 10", "Pos 15", "Pos 20", "Avg");
    printf("----------------------------------------------------------------------\n");

    ScalingMode modes[] = {FIXED, LINEAR_TIERED, INVERSE_SQRT, AGGRESSIVE_INVERSE};
    for (ScalingMode mode : modes) {
        double lots[5];
        lots[0] = CalculateLotSize(mode, 0, 0.01);   // Position 1
        lots[1] = CalculateLotSize(mode, 4, 0.01);   // Position 5
        lots[2] = CalculateLotSize(mode, 9, 0.01);   // Position 10
        lots[3] = CalculateLotSize(mode, 14, 0.01);  // Position 15
        lots[4] = CalculateLotSize(mode, 19, 0.01);  // Position 20

        double avg = (lots[0] + lots[1] + lots[2] + lots[3] + lots[4]) / 5.0;

        printf("%-20s %6.4f %6.4f %6.4f %6.4f %6.4f %6.4f\n",
               ScalingName(mode), lots[0], lots[1], lots[2], lots[3], lots[4], avg);
    }
    printf("\n");

    // Load ticks
    const char* filename = "Grid/XAUUSD_TICKS_2025.csv";
    printf("Loading 500,000 ticks from %s...\n", filename);
    auto ticks = LoadTicks(filename, 500000);
    printf("Loaded %zu ticks\n", ticks.size());

    if (ticks.empty()) {
        fprintf(stderr, "Failed to load data\n");
        return 1;
    }

    double start_price = ticks.front().bid;
    double end_price = ticks.back().bid;
    double price_change = (end_price - start_price) / start_price * 100.0;

    printf("Price: %.2f -> %.2f (%+.2f%%)\n", start_price, end_price, price_change);
    printf("\n");

    // Run tests for each scaling mode
    printf("Running backtests...\n");
    printf("\n");

    std::vector<Result> results;
    for (ScalingMode mode : modes) {
        printf("  Testing %s...\n", ScalingName(mode));
        results.push_back(RunTest(ticks, mode));
    }

    printf("\n");

    // Results table
    printf("=====================================================\n");
    printf("RESULTS\n");
    printf("=====================================================\n");
    printf("\n");

    printf("%-20s %10s %10s %10s %10s %10s\n",
           "Scaling Mode", "Return", "Max DD%", "Max DD$", "Trades", "Opened");
    printf("------------------------------------------------------------------------------\n");

    for (const Result& r : results) {
        printf("%-20s %9.2f%% %9.2f%% %10.2f %10d %10d\n",
               ScalingName(r.mode), r.return_pct, r.max_dd_pct, r.max_dd_dollars,
               r.total_trades, r.positions_opened);
    }

    printf("\n");
    printf("%-20s %10s %10s %10s\n",
           "Scaling Mode", "Total Lots", "Avg Lot", "Risk Adj");
    printf("-------------------------------------------------------\n");

    for (const Result& r : results) {
        printf("%-20s %10.3f %10.4f %10.3f\n",
               ScalingName(r.mode), r.total_lots_traded, r.avg_lot_size, r.risk_adjusted_score);
    }

    printf("\n");

    // Find best by different metrics
    printf("=====================================================\n");
    printf("ANALYSIS\n");
    printf("=====================================================\n");
    printf("\n");

    // Best return
    Result best_return = results[0];
    for (const Result& r : results) {
        if (r.return_pct > best_return.return_pct) best_return = r;
    }
    printf("Best Return:        %s (%.2f%%)\n", ScalingName(best_return.mode), best_return.return_pct);

    // Lowest DD
    Result lowest_dd = results[0];
    for (const Result& r : results) {
        if (r.max_dd_pct < lowest_dd.max_dd_pct) lowest_dd = r;
    }
    printf("Lowest Drawdown:    %s (%.2f%%)\n", ScalingName(lowest_dd.mode), lowest_dd.max_dd_pct);

    // Best risk-adjusted
    Result best_risk_adj = results[0];
    for (const Result& r : results) {
        if (r.risk_adjusted_score > best_risk_adj.risk_adjusted_score) best_risk_adj = r;
    }
    printf("Best Risk-Adjusted: %s (%.3f)\n", ScalingName(best_risk_adj.mode), best_risk_adj.risk_adjusted_score);

    printf("\n");

    // Compare vs baseline
    double baseline_return = results[0].return_pct;
    double baseline_dd = results[0].max_dd_pct;
    double baseline_risk_adj = results[0].risk_adjusted_score;

    printf("Comparison vs Fixed (baseline):\n");
    printf("%-20s %12s %12s %12s\n", "Mode", "Return Diff", "DD Diff", "Risk Adj Diff");
    printf("-------------------------------------------------------------\n");

    for (const Result& r : results) {
        double return_diff = r.return_pct - baseline_return;
        double dd_diff = r.max_dd_pct - baseline_dd;
        double risk_adj_diff = r.risk_adjusted_score - baseline_risk_adj;

        printf("%-20s %+11.2f%% %+11.2f%% %+12.3f\n",
               ScalingName(r.mode), return_diff, dd_diff, risk_adj_diff);
    }

    printf("\n");

    // Key insights
    printf("=====================================================\n");
    printf("KEY INSIGHTS\n");
    printf("=====================================================\n");
    printf("\n");

    printf("1. Position Count Impact:\n");
    printf("   - Fixed sizing exposes equal risk at each level\n");
    printf("   - Scaling reduces exposure as risk accumulates\n");
    printf("\n");

    printf("2. Cascade Risk Theory:\n");
    printf("   - When many positions are open, a large move can cause\n");
    printf("     cascading losses across all positions\n");
    printf("   - Smaller later positions = less total exposure at risk\n");
    printf("\n");

    printf("3. Trade-off Analysis:\n");
    printf("   - Aggressive scaling: Lower DD but also lower returns\n");
    printf("   - Mild scaling: Better risk/return balance\n");
    printf("\n");

    // Determine winner
    printf("=====================================================\n");
    printf("CONCLUSION\n");
    printf("=====================================================\n");
    printf("\n");

    if (best_risk_adj.risk_adjusted_score > baseline_risk_adj * 1.1) {
        printf("WINNER: %s\n", ScalingName(best_risk_adj.mode));
        printf("Achieves %.1f%% better risk-adjusted returns than baseline.\n",
               (best_risk_adj.risk_adjusted_score / baseline_risk_adj - 1.0) * 100.0);
    } else if (lowest_dd.max_dd_pct < baseline_dd * 0.9 &&
               lowest_dd.return_pct > baseline_return * 0.8) {
        printf("WINNER: %s\n", ScalingName(lowest_dd.mode));
        printf("Reduces drawdown by %.1f%% with acceptable return loss.\n",
               (1.0 - lowest_dd.max_dd_pct / baseline_dd) * 100.0);
    } else {
        printf("WINNER: Fixed (baseline)\n");
        printf("Position scaling did not provide significant improvement.\n");
    }

    printf("\n");

    // Recommendation
    printf("Recommendation:\n");
    if (best_risk_adj.mode != FIXED) {
        printf("  Consider using %s for production.\n", ScalingName(best_risk_adj.mode));
        printf("  Implement lot size calculation in strategy:\n");
        printf("    double lot = base_lot");
        switch (best_risk_adj.mode) {
            case LINEAR_TIERED:
                printf(" * (pos < 5 ? 1.0 : pos < 10 ? 0.75 : pos < 15 ? 0.5 : 0.25);\n");
                break;
            case INVERSE_SQRT:
                printf(" / sqrt(position_count + 1);\n");
                break;
            case AGGRESSIVE_INVERSE:
                printf(" / (position_count + 1);\n");
                break;
            default:
                printf(";\n");
        }
    } else {
        printf("  Keep fixed lot sizing - scaling adds complexity without benefit.\n");
    }

    printf("\n");
    printf("=====================================================\n");

    return 0;
}
