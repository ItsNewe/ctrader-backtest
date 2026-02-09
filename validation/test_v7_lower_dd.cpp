/**
 * V7 Lower Drawdown Test
 *
 * Goal: Find configurations that reduce max drawdown while maintaining profitability.
 *
 * The extended V7 test showed 71.8% return but 24.24% max drawdown - dangerously
 * close to the 25% close-all threshold. This test explores parameter combinations
 * designed to keep max DD under 15% while preserving good returns.
 *
 * Parameters being tested:
 * - Tighter volatility threshold (0.4, 0.5 instead of 0.6)
 * - More aggressive position limits (max 10 or 15 instead of 20)
 * - Tighter stop_new_at_dd (3%, 4% instead of 5%)
 * - Tighter partial_close_at_dd (5%, 6% instead of 8%)
 * - Smaller close_all_at_dd (15%, 20% instead of 25%)
 *
 * Uses C-style file I/O for maximum compatibility.
 */

#include <iostream>
#include <iomanip>
#include <vector>
#include <deque>
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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
};

struct LowDDConfig {
    // Volatility filter params
    int atr_short_period;
    int atr_long_period;
    double volatility_threshold;  // Lower = stricter = fewer trades

    // Position management
    int max_positions;            // Lower = less exposure

    // V3 Protection levels (tighter = lower DD)
    double stop_new_at_dd;        // Stop opening new positions
    double partial_close_at_dd;   // Close 50% of worst positions
    double close_all_at_dd;       // Emergency close all

    // TP multiplier
    double tp_multiplier;

    // Create a descriptive name
    std::string Name() const {
        char buf[128];
        snprintf(buf, sizeof(buf), "Vol%.1f/Max%d/Stop%.0f/Part%.0f/Close%.0f",
                 volatility_threshold, max_positions,
                 stop_new_at_dd, partial_close_at_dd, close_all_at_dd);
        return std::string(buf);
    }
};

struct Result {
    double return_pct;
    double max_dd;
    int trades_completed;
    int positions_opened;
    int close_all_triggers;       // How many times close-all was triggered
    double time_in_market_pct;    // How often volatility filter allowed trading
    double risk_adjusted_return;  // Return / MaxDD
    double calmar_ratio;          // Annualized return / Max DD (approximation)
};

Result RunTest(const std::vector<Tick>& ticks, const LowDDConfig& cfg) {
    Result r = {0, 0, 0, 0, 0, 0, 0, 0};
    if (ticks.empty()) return r;

    double balance = 10000.0;
    double equity = balance;
    double peak_equity = balance;
    double contract_size = 100.0;
    double leverage = 500.0;
    double spacing = 1.0;

    std::vector<Trade*> positions;
    size_t next_id = 1;
    bool partial_done = false;
    bool all_closed = false;

    ATR atr_short(cfg.atr_short_period);
    ATR atr_long(cfg.atr_long_period);

    int volatility_allowed_count = 0;
    int volatility_check_count = 0;

    for (const Tick& tick : ticks) {
        atr_short.Add(tick.bid);
        atr_long.Add(tick.bid);

        // Calculate equity
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

        // Protection: Close ALL at threshold
        if (dd_pct > cfg.close_all_at_dd && !all_closed && !positions.empty()) {
            for (Trade* t : positions) {
                balance += (tick.bid - t->entry_price) * t->lot_size * contract_size;
                r.trades_completed++;
                delete t;
            }
            positions.clear();
            all_closed = true;
            equity = balance;
            peak_equity = equity;
            r.close_all_triggers++;
            continue;
        }

        // Protection: Partial close at threshold
        if (dd_pct > cfg.partial_close_at_dd && !partial_done && positions.size() > 1) {
            std::sort(positions.begin(), positions.end(), [&](Trade* a, Trade* b) {
                return (tick.bid - a->entry_price) < (tick.bid - b->entry_price);
            });
            int to_close = std::max(1, (int)(positions.size() / 2));
            for (int i = 0; i < to_close && !positions.empty(); i++) {
                balance += (tick.bid - positions[0]->entry_price) * positions[0]->lot_size * contract_size;
                r.trades_completed++;
                delete positions[0];
                positions.erase(positions.begin());
            }
            partial_done = true;
        }

        // TP check
        for (auto it = positions.begin(); it != positions.end();) {
            Trade* t = *it;
            if (tick.bid >= t->take_profit) {
                balance += (t->take_profit - t->entry_price) * t->lot_size * contract_size;
                r.trades_completed++;
                delete t;
                it = positions.erase(it);
            } else {
                ++it;
            }
        }

        // Check volatility filter
        bool volatility_ok = true;
        if (atr_short.IsReady() && atr_long.IsReady() && atr_long.Get() > 0) {
            volatility_check_count++;
            volatility_ok = atr_short.Get() < atr_long.Get() * cfg.volatility_threshold;
            if (volatility_ok) volatility_allowed_count++;
        }

        // Open new positions
        double used_margin = 0;
        for (Trade* t : positions) {
            used_margin += t->lot_size * contract_size * t->entry_price / leverage;
        }

        if (dd_pct < cfg.stop_new_at_dd && volatility_ok && (int)positions.size() < cfg.max_positions) {
            double lowest = DBL_MAX, highest = DBL_MIN;
            for (Trade* t : positions) {
                lowest = std::min(lowest, t->entry_price);
                highest = std::max(highest, t->entry_price);
            }

            bool should_open = positions.empty() ||
                              (lowest >= tick.ask + spacing) ||
                              (highest <= tick.ask - spacing);

            if (should_open) {
                double lot = 0.01;
                double margin_needed = lot * contract_size * tick.ask / leverage;
                if (equity - used_margin > margin_needed * 2) {
                    Trade* t = new Trade();
                    t->id = next_id++;
                    t->entry_price = tick.ask;
                    t->lot_size = lot;
                    t->take_profit = tick.ask + tick.spread() + (spacing * cfg.tp_multiplier);
                    positions.push_back(t);
                    r.positions_opened++;
                }
            }
        }
    }

    // Close remaining positions at end
    for (Trade* t : positions) {
        balance += (ticks.back().bid - t->entry_price) * t->lot_size * contract_size;
        r.trades_completed++;
        delete t;
    }

    r.return_pct = (balance - 10000.0) / 10000.0 * 100.0;
    r.time_in_market_pct = (volatility_check_count > 0) ?
                           (double)volatility_allowed_count / volatility_check_count * 100.0 : 100.0;
    r.risk_adjusted_return = (r.max_dd > 0) ? r.return_pct / r.max_dd : r.return_pct;
    // Approximate Calmar: assuming 10M ticks ~ 6 months of data, annualize
    r.calmar_ratio = (r.max_dd > 0) ? (r.return_pct * 2.0) / r.max_dd : r.return_pct;

    return r;
}

std::vector<Tick> LoadTicks(const char* filename, size_t max_ticks) {
    std::vector<Tick> ticks;

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

    size_t progress_interval = max_ticks / 20;
    if (progress_interval < 100000) progress_interval = 100000;

    while (fgets(line, sizeof(line), file) && ticks.size() < max_ticks) {
        char ts[64];
        double bid, ask;

        // Parse tab-separated: timestamp\tbid\task\t...
        if (sscanf(line, "%63[^\t]\t%lf\t%lf", ts, &bid, &ask) >= 3) {
            if (bid > 0 && ask > 0) {
                Tick tick;
                tick.bid = bid;
                tick.ask = ask;
                ticks.push_back(tick);
            }
        }

        if (ticks.size() % progress_interval == 0 && ticks.size() > 0) {
            printf("  Loaded %zu ticks (%.1f%%)...\n", ticks.size(),
                   (100.0 * ticks.size() / max_ticks));
        }
    }

    fclose(file);
    return ticks;
}

int main() {
    printf("================================================================\n");
    printf("     V7 LOWER DRAWDOWN OPTIMIZATION TEST\n");
    printf("================================================================\n\n");

    printf("OBJECTIVE:\n");
    printf("  Original V7: 71.8%% return, 24.24%% max DD (too close to 25%% limit)\n");
    printf("  Target:      Keep max DD < 15%%, maintain good returns\n\n");

    // Load tick data
    const char* filename = "Broker/XAUUSD_TICKS_2025.csv";
    size_t max_ticks = 10000000;  // 10 million ticks

    printf("Loading %zu ticks from: %s\n", max_ticks, filename);

    std::vector<Tick> ticks = LoadTicks(filename, max_ticks);

    if (ticks.empty()) {
        fprintf(stderr, "Failed to load tick data!\n");
        return 1;
    }

    printf("\nLoaded %zu ticks successfully\n", ticks.size());
    printf("Price range: %.2f -> %.2f\n", ticks.front().bid, ticks.back().bid);
    double price_change = (ticks.back().bid - ticks.front().bid) / ticks.front().bid * 100.0;
    printf("Price change: %+.2f%%\n\n", price_change);

    // Define configurations to test
    std::vector<LowDDConfig> configs;

    // Baseline V7 config (for comparison)
    LowDDConfig baseline;
    baseline.atr_short_period = 100;
    baseline.atr_long_period = 500;
    baseline.volatility_threshold = 0.8;
    baseline.max_positions = 20;
    baseline.stop_new_at_dd = 5.0;
    baseline.partial_close_at_dd = 8.0;
    baseline.close_all_at_dd = 25.0;
    baseline.tp_multiplier = 1.0;
    configs.push_back(baseline);

    // Test variations for lower drawdown
    // Volatility thresholds to test
    std::vector<double> vol_thresholds = {0.4, 0.5, 0.6, 0.7, 0.8};

    // Max positions to test
    std::vector<int> max_pos_values = {10, 15, 20};

    // Stop new at DD levels
    std::vector<double> stop_new_dd = {3.0, 4.0, 5.0};

    // Partial close at DD levels
    std::vector<double> partial_dd = {5.0, 6.0, 8.0};

    // Close all at DD levels
    std::vector<double> close_all_dd = {15.0, 20.0, 25.0};

    // Generate test configurations (selected combinations to keep test time reasonable)
    for (double vol : vol_thresholds) {
        for (int max_pos : max_pos_values) {
            for (double stop_new : stop_new_dd) {
                for (double partial : partial_dd) {
                    for (double close_all : close_all_dd) {
                        // Skip invalid combinations
                        if (partial <= stop_new) continue;
                        if (close_all <= partial) continue;

                        // Skip baseline (already added)
                        if (vol == 0.8 && max_pos == 20 && stop_new == 5.0 &&
                            partial == 8.0 && close_all == 25.0) continue;

                        LowDDConfig cfg;
                        cfg.atr_short_period = 100;
                        cfg.atr_long_period = 500;
                        cfg.volatility_threshold = vol;
                        cfg.max_positions = max_pos;
                        cfg.stop_new_at_dd = stop_new;
                        cfg.partial_close_at_dd = partial;
                        cfg.close_all_at_dd = close_all;
                        cfg.tp_multiplier = 1.0;
                        configs.push_back(cfg);
                    }
                }
            }
        }
    }

    printf("Testing %zu configurations...\n\n", configs.size());

    // Run tests and collect results
    struct ConfigResult {
        LowDDConfig config;
        Result result;
    };

    std::vector<ConfigResult> all_results;

    int test_num = 0;
    for (const auto& cfg : configs) {
        test_num++;
        printf("\rTesting configuration %d/%zu...", test_num, configs.size());
        fflush(stdout);

        Result r = RunTest(ticks, cfg);
        all_results.push_back({cfg, r});
    }

    printf("\n\nAll tests complete!\n\n");

    // Print results table header
    printf("================================================================\n");
    printf("RESULTS SORTED BY RISK-ADJUSTED RETURN (DD < 15%% FIRST)\n");
    printf("================================================================\n\n");

    // Sort: prioritize DD < 15%, then by risk-adjusted return
    std::sort(all_results.begin(), all_results.end(),
              [](const ConfigResult& a, const ConfigResult& b) {
                  // First priority: DD under 15%
                  bool a_safe = a.result.max_dd < 15.0 && a.result.return_pct > 0;
                  bool b_safe = b.result.max_dd < 15.0 && b.result.return_pct > 0;

                  if (a_safe && !b_safe) return true;
                  if (!a_safe && b_safe) return false;

                  // Second priority: risk-adjusted return
                  return a.result.risk_adjusted_return > b.result.risk_adjusted_return;
              });

    // Print table
    printf("%-35s | %8s %8s %8s %6s %8s\n",
           "Configuration", "Return", "MaxDD", "RAR", "ClsAll", "Trades");
    printf("%s\n", std::string(85, '-').c_str());

    int count = 0;
    for (const auto& cr : all_results) {
        if (count >= 30) break;  // Show top 30

        const char* indicator = "";
        if (cr.result.max_dd < 15.0 && cr.result.return_pct > 0) {
            indicator = " *";  // Mark safe configs
        }

        printf("%-35s | %7.2f%% %7.2f%% %8.2f %6d %8d%s\n",
               cr.config.Name().c_str(),
               cr.result.return_pct,
               cr.result.max_dd,
               cr.result.risk_adjusted_return,
               cr.result.close_all_triggers,
               cr.result.trades_completed,
               indicator);
        count++;
    }

    printf("%s\n", std::string(85, '-').c_str());
    printf("* = Meets criteria (DD < 15%%, profitable)\n\n");

    // Find the best config that meets our criteria
    printf("================================================================\n");
    printf("BEST CONFIGURATION FOR LOW DRAWDOWN\n");
    printf("================================================================\n\n");

    ConfigResult* best_safe = nullptr;
    ConfigResult* best_overall = nullptr;

    for (auto& cr : all_results) {
        if (cr.result.max_dd < 15.0 && cr.result.return_pct > 0) {
            if (!best_safe || cr.result.risk_adjusted_return > best_safe->result.risk_adjusted_return) {
                best_safe = &cr;
            }
        }
        if (!best_overall || cr.result.risk_adjusted_return > best_overall->result.risk_adjusted_return) {
            best_overall = &cr;
        }
    }

    // Print baseline comparison
    const auto& baseline_result = all_results[0];
    printf("BASELINE (Original V7):\n");
    printf("  Volatility Threshold:  %.1f\n", baseline.volatility_threshold);
    printf("  Max Positions:         %d\n", baseline.max_positions);
    printf("  Stop New at DD:        %.0f%%\n", baseline.stop_new_at_dd);
    printf("  Partial Close at DD:   %.0f%%\n", baseline.partial_close_at_dd);
    printf("  Close All at DD:       %.0f%%\n", baseline.close_all_at_dd);
    printf("  Return:                %.2f%%\n", baseline_result.result.return_pct);
    printf("  Max Drawdown:          %.2f%%\n", baseline_result.result.max_dd);
    printf("  Risk-Adjusted Return:  %.2f\n", baseline_result.result.risk_adjusted_return);
    printf("  Close-All Triggers:    %d\n\n", baseline_result.result.close_all_triggers);

    if (best_safe) {
        printf("RECOMMENDED (DD < 15%%):\n");
        printf("  Volatility Threshold:  %.1f\n", best_safe->config.volatility_threshold);
        printf("  Max Positions:         %d\n", best_safe->config.max_positions);
        printf("  Stop New at DD:        %.0f%%\n", best_safe->config.stop_new_at_dd);
        printf("  Partial Close at DD:   %.0f%%\n", best_safe->config.partial_close_at_dd);
        printf("  Close All at DD:       %.0f%%\n", best_safe->config.close_all_at_dd);
        printf("  Return:                %.2f%%\n", best_safe->result.return_pct);
        printf("  Max Drawdown:          %.2f%%\n", best_safe->result.max_dd);
        printf("  Risk-Adjusted Return:  %.2f\n", best_safe->result.risk_adjusted_return);
        printf("  Close-All Triggers:    %d\n\n", best_safe->result.close_all_triggers);

        // Calculate improvement
        double dd_reduction = baseline_result.result.max_dd - best_safe->result.max_dd;
        double return_change = best_safe->result.return_pct - baseline_result.result.return_pct;
        double rar_change = best_safe->result.risk_adjusted_return - baseline_result.result.risk_adjusted_return;

        printf("IMPROVEMENT VS BASELINE:\n");
        printf("  Drawdown Reduction:    %.2f%% (%.1f%% -> %.1f%%)\n",
               dd_reduction, baseline_result.result.max_dd, best_safe->result.max_dd);
        printf("  Return Change:         %+.2f%% (%.1f%% -> %.1f%%)\n",
               return_change, baseline_result.result.return_pct, best_safe->result.return_pct);
        printf("  RAR Change:            %+.2f (%.2f -> %.2f)\n",
               rar_change, baseline_result.result.risk_adjusted_return,
               best_safe->result.risk_adjusted_return);
    } else {
        printf("WARNING: No configuration found with DD < 15%% and positive returns!\n");
        printf("Consider even tighter parameters or different approach.\n");
    }

    printf("\n");

    // Show additional analysis: effect of each parameter
    printf("================================================================\n");
    printf("PARAMETER IMPACT ANALYSIS\n");
    printf("================================================================\n\n");

    // Analyze volatility threshold impact
    printf("VOLATILITY THRESHOLD IMPACT (other params at baseline):\n");
    printf("  Threshold | Return  | MaxDD   | RAR\n");
    printf("  ----------|---------|---------|--------\n");

    for (double vol : vol_thresholds) {
        for (const auto& cr : all_results) {
            if (cr.config.volatility_threshold == vol &&
                cr.config.max_positions == 20 &&
                cr.config.stop_new_at_dd == 5.0 &&
                cr.config.partial_close_at_dd == 8.0 &&
                cr.config.close_all_at_dd == 25.0) {
                printf("  %9.1f | %6.2f%% | %6.2f%% | %6.2f\n",
                       vol, cr.result.return_pct, cr.result.max_dd,
                       cr.result.risk_adjusted_return);
                break;
            }
        }
    }

    printf("\n");

    // Analyze max positions impact
    printf("MAX POSITIONS IMPACT (vol=0.6, other params at baseline):\n");
    printf("  MaxPos | Return  | MaxDD   | RAR\n");
    printf("  -------|---------|---------|--------\n");

    for (int max_pos : max_pos_values) {
        for (const auto& cr : all_results) {
            if (cr.config.max_positions == max_pos &&
                cr.config.volatility_threshold == 0.6 &&
                cr.config.stop_new_at_dd == 5.0 &&
                cr.config.partial_close_at_dd == 8.0 &&
                cr.config.close_all_at_dd == 25.0) {
                printf("  %6d | %6.2f%% | %6.2f%% | %6.2f\n",
                       max_pos, cr.result.return_pct, cr.result.max_dd,
                       cr.result.risk_adjusted_return);
                break;
            }
        }
    }

    printf("\n");

    // Analyze close_all threshold impact
    printf("CLOSE-ALL THRESHOLD IMPACT (vol=0.6, max_pos=15):\n");
    printf("  CloseAll | Return  | MaxDD   | RAR    | Triggers\n");
    printf("  ---------|---------|---------|--------|----------\n");

    for (double close_all : close_all_dd) {
        for (const auto& cr : all_results) {
            if (cr.config.close_all_at_dd == close_all &&
                cr.config.volatility_threshold == 0.6 &&
                cr.config.max_positions == 15 &&
                cr.config.stop_new_at_dd == 5.0 &&
                cr.config.partial_close_at_dd == 8.0) {
                printf("  %8.0f%% | %6.2f%% | %6.2f%% | %6.2f | %8d\n",
                       close_all, cr.result.return_pct, cr.result.max_dd,
                       cr.result.risk_adjusted_return, cr.result.close_all_triggers);
                break;
            }
        }
    }

    printf("\n");

    // Show configs with best return while keeping DD under various thresholds
    printf("================================================================\n");
    printf("BEST RETURNS AT VARIOUS DD LIMITS\n");
    printf("================================================================\n\n");

    std::vector<double> dd_limits = {10.0, 12.0, 15.0, 18.0, 20.0};

    for (double dd_limit : dd_limits) {
        ConfigResult* best_at_limit = nullptr;
        for (auto& cr : all_results) {
            if (cr.result.max_dd <= dd_limit && cr.result.return_pct > 0) {
                if (!best_at_limit || cr.result.return_pct > best_at_limit->result.return_pct) {
                    best_at_limit = &cr;
                }
            }
        }

        if (best_at_limit) {
            printf("Max DD <= %.0f%%: Return=%.2f%%, DD=%.2f%%, Config=%s\n",
                   dd_limit, best_at_limit->result.return_pct, best_at_limit->result.max_dd,
                   best_at_limit->config.Name().c_str());
        } else {
            printf("Max DD <= %.0f%%: No profitable configuration found\n", dd_limit);
        }
    }

    printf("\n================================================================\n");
    printf("TEST COMPLETE\n");
    printf("================================================================\n");

    return 0;
}
