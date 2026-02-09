/**
 * test_v8_eurusd.cpp - Test V8 strategy on EURUSD forex data
 *
 * Purpose: Verify V8's tighter risk management works on forex pairs,
 *          not just gold (XAUUSD).
 *
 * V8 Key Features:
 * - Tighter protection: stop_new 3%, partial 5%, close_all 15%, max_pos 15
 * - Optimized ATR params: 50/1000/0.6
 * - DD-based lot scaling
 * - TP multiplier: 2.0
 *
 * Compares V8 vs V7 on same EURUSD data.
 *
 * Build: g++ -O2 -std=c++17 -I../include test_v8_eurusd.cpp -o test_v8_eurusd
 * Run: ./test_v8_eurusd
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <deque>
#include <algorithm>
#include <cfloat>

// Configuration constants for EURUSD
constexpr double EURUSD_CONTRACT_SIZE = 100000.0;  // Standard forex lot
constexpr double EURUSD_PIP_SIZE = 0.00001;        // 5-digit broker
constexpr double EURUSD_SPACING = 0.001;           // 10 pips grid spacing
constexpr double INITIAL_BALANCE = 10000.0;
constexpr double LEVERAGE = 500.0;
constexpr double MIN_VOLUME = 0.01;
constexpr double MAX_VOLUME = 100.0;

constexpr size_t MAX_TICKS = 600000;  // Up to 600K ticks

// Simplified Tick structure
struct Tick {
    char timestamp[32];
    double bid;
    double ask;
    long volume;

    double spread() const { return ask - bid; }
};

// Simple Trade structure
struct Trade {
    int id;
    char direction[8];  // "BUY" or "SELL"
    double entry_price;
    double exit_price;
    double lot_size;
    double stop_loss;
    double take_profit;
    double profit_loss;
    bool is_open;
};

// Statistics tracking
struct BacktestStats {
    double initial_balance;
    double final_balance;
    double max_drawdown;
    double max_drawdown_pct;
    int total_trades;
    int winning_trades;
    int losing_trades;
    double total_profit;
    double total_loss;
    int protection_closes;
    int max_concurrent_positions;
    int dd_lot_reductions;
};

// Strategy configuration
struct StrategyConfig {
    double spacing;
    double contract_size;
    double min_volume;
    double max_volume;
    int max_positions;
    double stop_new_at_dd;
    double partial_close_at_dd;
    double close_all_at_dd;
    int atr_short_period;
    int atr_long_period;
    double volatility_threshold;
    double tp_multiplier;
    bool enable_dd_lot_scaling;
    double lot_scale_start_dd;
    double lot_scale_min_factor;
};

// ============================================================================
// Grid Strategy Engine (V7/V8 combined with config-driven behavior)
// ============================================================================
class GridStrategy {
public:
    GridStrategy(const StrategyConfig& config, double initial_balance)
        : cfg(config),
          balance(initial_balance),
          equity(initial_balance),
          peak_equity(initial_balance),
          trade_count(0),
          next_trade_id(1),
          partial_close_done(false),
          all_closed(false),
          lowest_buy(DBL_MAX),
          highest_buy(DBL_MIN),
          last_price(0.0),
          atr_short_sum(0.0),
          atr_long_sum(0.0) {
        memset(&stats, 0, sizeof(stats));
        stats.initial_balance = initial_balance;
    }

    void OnTick(const Tick& tick) {
        current_ask = tick.ask;
        current_bid = tick.bid;
        current_spread = tick.spread();

        // Update ATR calculations
        UpdateATR(tick.bid);

        // Update equity with unrealized P/L
        UpdateEquity();

        // Reset peak equity when no positions
        if (open_position_count == 0) {
            if (peak_equity != balance) {
                peak_equity = balance;
                partial_close_done = false;
                all_closed = false;
            }
        }

        // Track peak equity
        if (equity > peak_equity) {
            peak_equity = equity;
            partial_close_done = false;
            all_closed = false;
        }

        // Calculate current drawdown
        double current_dd_pct = 0.0;
        if (peak_equity > 0) {
            current_dd_pct = (peak_equity - equity) / peak_equity * 100.0;
        }

        // Track max drawdown
        double current_dd = peak_equity - equity;
        if (current_dd > stats.max_drawdown) {
            stats.max_drawdown = current_dd;
            stats.max_drawdown_pct = current_dd_pct;
        }

        // Track max concurrent positions
        if (open_position_count > stats.max_concurrent_positions) {
            stats.max_concurrent_positions = open_position_count;
        }

        // Protection: Close ALL at threshold
        if (current_dd_pct > cfg.close_all_at_dd && !all_closed && open_position_count > 0) {
            CloseAllPositions();
            all_closed = true;
            peak_equity = balance;
            return;
        }

        // Protection: Partial close at threshold
        if (current_dd_pct > cfg.partial_close_at_dd && !partial_close_done && open_position_count > 1) {
            CloseWorstPositions(0.50);
            partial_close_done = true;
        }

        // Update position tracking
        UpdatePositionTracking();

        // Check TP for open positions
        CheckTakeProfits();

        // Check volatility filter
        bool volatility_ok = IsVolatilityLow();

        // Open new positions if allowed
        if (current_dd_pct < cfg.stop_new_at_dd && volatility_ok) {
            OpenNew(current_dd_pct);
        }
    }

    BacktestStats GetStats() {
        stats.final_balance = balance;
        return stats;
    }

private:
    StrategyConfig cfg;
    double balance;
    double equity;
    double peak_equity;

    // Trades storage
    static constexpr int MAX_TRADES = 10000;
    Trade trades[MAX_TRADES];
    int trade_count;
    int next_trade_id;
    int open_position_count = 0;

    // State
    bool partial_close_done;
    bool all_closed;
    double lowest_buy;
    double highest_buy;
    double closest_above;
    double closest_below;
    double current_ask;
    double current_bid;
    double current_spread;

    // ATR state
    double last_price;
    std::deque<double> atr_short_values;
    std::deque<double> atr_long_values;
    double atr_short_sum;
    double atr_long_sum;

    // Stats
    BacktestStats stats;

    void UpdateATR(double price) {
        if (last_price > 0) {
            double range = std::abs(price - last_price);

            // Short ATR
            atr_short_values.push_back(range);
            atr_short_sum += range;
            if ((int)atr_short_values.size() > cfg.atr_short_period) {
                atr_short_sum -= atr_short_values.front();
                atr_short_values.pop_front();
            }

            // Long ATR
            atr_long_values.push_back(range);
            atr_long_sum += range;
            if ((int)atr_long_values.size() > cfg.atr_long_period) {
                atr_long_sum -= atr_long_values.front();
                atr_long_values.pop_front();
            }
        }
        last_price = price;
    }

    double GetATRShort() const {
        if (atr_short_values.empty()) return 0.0;
        return atr_short_sum / atr_short_values.size();
    }

    double GetATRLong() const {
        if (atr_long_values.empty()) return 0.0;
        return atr_long_sum / atr_long_values.size();
    }

    bool IsVolatilityLow() const {
        if ((int)atr_short_values.size() < cfg.atr_short_period) return true;
        if ((int)atr_long_values.size() < cfg.atr_long_period) return true;

        double atr_short = GetATRShort();
        double atr_long = GetATRLong();

        if (atr_long <= 0) return true;
        return atr_short < atr_long * cfg.volatility_threshold;
    }

    double CalculateLotScale(double dd_pct) const {
        if (!cfg.enable_dd_lot_scaling) return 1.0;

        if (dd_pct <= cfg.lot_scale_start_dd) return 1.0;
        if (dd_pct >= cfg.stop_new_at_dd) return cfg.lot_scale_min_factor;

        double dd_range = cfg.stop_new_at_dd - cfg.lot_scale_start_dd;
        double scale_range = 1.0 - cfg.lot_scale_min_factor;
        double dd_progress = (dd_pct - cfg.lot_scale_start_dd) / dd_range;

        return 1.0 - (dd_progress * scale_range);
    }

    double NormalizeVolume(double volume) const {
        double normalized = std::round(volume * 100.0) / 100.0;
        return std::max(normalized, cfg.min_volume);
    }

    void UpdateEquity() {
        equity = balance;
        for (int i = 0; i < trade_count; i++) {
            if (trades[i].is_open) {
                double price_diff = current_bid - trades[i].entry_price;
                double unrealized_pl = price_diff * trades[i].lot_size * cfg.contract_size;
                equity += unrealized_pl;
            }
        }
    }

    void UpdatePositionTracking() {
        lowest_buy = DBL_MAX;
        highest_buy = DBL_MIN;
        closest_above = DBL_MAX;
        closest_below = DBL_MIN;
        open_position_count = 0;

        for (int i = 0; i < trade_count; i++) {
            if (trades[i].is_open) {
                open_position_count++;
                double open_price = trades[i].entry_price;

                lowest_buy = std::min(lowest_buy, open_price);
                highest_buy = std::max(highest_buy, open_price);

                if (open_price >= current_ask) {
                    closest_above = std::min(closest_above, open_price - current_ask);
                }
                if (open_price <= current_ask) {
                    closest_below = std::min(closest_below, current_ask - open_price);
                }
            }
        }
    }

    void CheckTakeProfits() {
        for (int i = 0; i < trade_count; i++) {
            if (trades[i].is_open && trades[i].take_profit > 0) {
                if (current_bid >= trades[i].take_profit) {
                    CloseTrade(i, "TP");
                }
            }
        }
    }

    bool OpenTrade(double lot_size, double tp) {
        if (trade_count >= MAX_TRADES) return false;

        Trade& t = trades[trade_count++];
        t.id = next_trade_id++;
        strcpy(t.direction, "BUY");
        t.entry_price = current_ask;
        t.exit_price = 0.0;
        t.lot_size = lot_size;
        t.stop_loss = 0.0;
        t.take_profit = tp;
        t.profit_loss = 0.0;
        t.is_open = true;

        open_position_count++;
        return true;
    }

    void CloseTrade(int index, const char* reason) {
        if (!trades[index].is_open) return;

        trades[index].exit_price = current_bid;
        double price_diff = trades[index].exit_price - trades[index].entry_price;
        trades[index].profit_loss = price_diff * trades[index].lot_size * cfg.contract_size;
        trades[index].is_open = false;

        balance += trades[index].profit_loss;
        stats.total_trades++;

        if (trades[index].profit_loss > 0) {
            stats.winning_trades++;
            stats.total_profit += trades[index].profit_loss;
        } else {
            stats.losing_trades++;
            stats.total_loss += trades[index].profit_loss;
        }

        if (strcmp(reason, "PROTECTION") == 0) {
            stats.protection_closes++;
        }

        open_position_count--;
    }

    void CloseAllPositions() {
        for (int i = 0; i < trade_count; i++) {
            if (trades[i].is_open) {
                CloseTrade(i, "PROTECTION");
            }
        }
    }

    void CloseWorstPositions(double pct) {
        // Collect open positions with their P/L
        std::vector<std::pair<double, int>> pl_and_index;
        for (int i = 0; i < trade_count; i++) {
            if (trades[i].is_open) {
                double pl = (current_bid - trades[i].entry_price) * trades[i].lot_size * cfg.contract_size;
                pl_and_index.push_back({pl, i});
            }
        }

        if (pl_and_index.size() <= 1) return;

        std::sort(pl_and_index.begin(), pl_and_index.end());

        int to_close = std::max(1, (int)(pl_and_index.size() * pct));
        for (int i = 0; i < to_close; i++) {
            CloseTrade(pl_and_index[i].second, "PROTECTION");
        }
    }

    void OpenNew(double current_dd_pct) {
        if (open_position_count >= cfg.max_positions) return;

        // Calculate lot size with DD scaling (V8 feature)
        double lot_scale = CalculateLotScale(current_dd_pct);
        double lot_size = NormalizeVolume(cfg.min_volume * lot_scale);

        if (lot_scale < 1.0) {
            stats.dd_lot_reductions++;
        }

        // Calculate TP
        double tp = current_ask + current_spread + (cfg.spacing * cfg.tp_multiplier);

        if (open_position_count == 0) {
            if (OpenTrade(lot_size, tp)) {
                highest_buy = current_ask;
                lowest_buy = current_ask;
            }
        } else {
            if (lowest_buy >= current_ask + cfg.spacing) {
                if (OpenTrade(lot_size, tp)) {
                    lowest_buy = current_ask;
                }
            } else if (highest_buy <= current_ask - cfg.spacing) {
                if (OpenTrade(lot_size, tp)) {
                    highest_buy = current_ask;
                }
            } else if (closest_above >= cfg.spacing && closest_below >= cfg.spacing) {
                OpenTrade(lot_size, tp);
            }
        }
    }
};

// ============================================================================
// Tick Data Loading (C-style file I/O for performance)
// ============================================================================
Tick* g_ticks = nullptr;
size_t g_tick_count = 0;

bool LoadTickData(const char* filepath, size_t max_ticks) {
    FILE* f = fopen(filepath, "r");
    if (!f) {
        printf("ERROR: Cannot open file: %s\n", filepath);
        return false;
    }

    // Allocate tick buffer
    g_ticks = (Tick*)malloc(sizeof(Tick) * max_ticks);
    if (!g_ticks) {
        printf("ERROR: Cannot allocate memory for %zu ticks\n", max_ticks);
        fclose(f);
        return false;
    }

    // Skip header
    char line[256];
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return false;
    }

    // Read ticks
    g_tick_count = 0;
    while (fgets(line, sizeof(line), f) && g_tick_count < max_ticks) {
        // Parse: Timestamp\tBid\tAsk\tVolume\tFlags
        char* timestamp = strtok(line, "\t");
        char* bid_str = strtok(NULL, "\t");
        char* ask_str = strtok(NULL, "\t");
        char* vol_str = strtok(NULL, "\t");

        if (!timestamp || !bid_str || !ask_str) continue;

        Tick& t = g_ticks[g_tick_count];
        strncpy(t.timestamp, timestamp, sizeof(t.timestamp) - 1);
        t.timestamp[sizeof(t.timestamp) - 1] = '\0';
        t.bid = atof(bid_str);
        t.ask = atof(ask_str);
        t.volume = vol_str ? atol(vol_str) : 0;

        // Validate
        if (t.bid > 0 && t.ask > 0 && t.ask >= t.bid) {
            g_tick_count++;
        }
    }

    fclose(f);
    printf("Loaded %zu ticks from %s\n", g_tick_count, filepath);
    return g_tick_count > 0;
}

void FreeTickData() {
    if (g_ticks) {
        free(g_ticks);
        g_ticks = nullptr;
    }
    g_tick_count = 0;
}

// ============================================================================
// Main Test Runner
// ============================================================================
void PrintStats(const char* name, const BacktestStats& s) {
    double return_pct = (s.final_balance - s.initial_balance) / s.initial_balance * 100.0;
    double risk_adjusted = (s.max_drawdown_pct > 0) ? return_pct / s.max_drawdown_pct : 0.0;
    double win_rate = (s.total_trades > 0) ? (double)s.winning_trades / s.total_trades * 100.0 : 0.0;
    double avg_win = (s.winning_trades > 0) ? s.total_profit / s.winning_trades : 0.0;
    double avg_loss = (s.losing_trades > 0) ? s.total_loss / s.losing_trades : 0.0;

    printf("\n====== %s ======\n", name);
    printf("Initial Balance:      $%.2f\n", s.initial_balance);
    printf("Final Balance:        $%.2f\n", s.final_balance);
    printf("Total Return:         $%.2f (%.2f%%)\n", s.final_balance - s.initial_balance, return_pct);
    printf("Max Drawdown:         $%.2f (%.2f%%)\n", s.max_drawdown, s.max_drawdown_pct);
    printf("Risk-Adjusted Score:  %.3f (return%% / DD%%)\n", risk_adjusted);
    printf("Total Trades:         %d\n", s.total_trades);
    printf("Win Rate:             %.2f%% (%d/%d)\n", win_rate, s.winning_trades, s.total_trades);
    printf("Average Win:          $%.2f\n", avg_win);
    printf("Average Loss:         $%.2f\n", avg_loss);
    printf("Protection Closes:    %d\n", s.protection_closes);
    printf("Max Concurrent Pos:   %d\n", s.max_concurrent_positions);
    if (s.dd_lot_reductions > 0) {
        printf("DD Lot Reductions:    %d\n", s.dd_lot_reductions);
    }
}

int main() {
    printf("=== V8 EURUSD Forex Test ===\n");
    printf("Testing V8's tighter risk management on EURUSD forex pair\n\n");

    // Load tick data
    const char* tick_file = "C:/Users/user/Documents/ctrader-backtest/validation/EURUSD_TICKS_202401.csv";
    if (!LoadTickData(tick_file, MAX_TICKS)) {
        printf("Failed to load tick data\n");
        return 1;
    }

    // Limit to 500K ticks as requested
    size_t test_ticks = std::min(g_tick_count, (size_t)500000);
    printf("Testing with %zu ticks\n", test_ticks);
    printf("First tick: %s Bid=%.5f Ask=%.5f\n", g_ticks[0].timestamp, g_ticks[0].bid, g_ticks[0].ask);
    printf("Last tick:  %s Bid=%.5f Ask=%.5f\n", g_ticks[test_ticks-1].timestamp, g_ticks[test_ticks-1].bid, g_ticks[test_ticks-1].ask);

    // ===== V7 Configuration (baseline) =====
    StrategyConfig v7_cfg;
    v7_cfg.spacing = EURUSD_SPACING;          // 10 pips
    v7_cfg.contract_size = EURUSD_CONTRACT_SIZE;
    v7_cfg.min_volume = MIN_VOLUME;
    v7_cfg.max_volume = MAX_VOLUME;
    v7_cfg.max_positions = 20;                // V7: 20
    v7_cfg.stop_new_at_dd = 5.0;              // V7: 5%
    v7_cfg.partial_close_at_dd = 8.0;         // V7: 8%
    v7_cfg.close_all_at_dd = 25.0;            // V7: 25%
    v7_cfg.atr_short_period = 100;            // V7: 100
    v7_cfg.atr_long_period = 500;             // V7: 500
    v7_cfg.volatility_threshold = 0.8;        // V7: 0.8
    v7_cfg.tp_multiplier = 1.0;               // V7: 1.0x
    v7_cfg.enable_dd_lot_scaling = false;     // V7: no DD lot scaling
    v7_cfg.lot_scale_start_dd = 1.0;
    v7_cfg.lot_scale_min_factor = 0.25;

    // ===== V8 Configuration (tighter protection) =====
    StrategyConfig v8_cfg;
    v8_cfg.spacing = EURUSD_SPACING;          // 10 pips
    v8_cfg.contract_size = EURUSD_CONTRACT_SIZE;
    v8_cfg.min_volume = MIN_VOLUME;
    v8_cfg.max_volume = MAX_VOLUME;
    v8_cfg.max_positions = 15;                // V8: 15 (tighter)
    v8_cfg.stop_new_at_dd = 3.0;              // V8: 3% (tighter)
    v8_cfg.partial_close_at_dd = 5.0;         // V8: 5% (tighter)
    v8_cfg.close_all_at_dd = 15.0;            // V8: 15% (tighter)
    v8_cfg.atr_short_period = 50;             // V8: 50 (more responsive)
    v8_cfg.atr_long_period = 1000;            // V8: 1000 (more stable baseline)
    v8_cfg.volatility_threshold = 0.6;        // V8: 0.6 (stricter filter)
    v8_cfg.tp_multiplier = 2.0;               // V8: 2.0x (wider TP)
    v8_cfg.enable_dd_lot_scaling = true;      // V8: enabled
    v8_cfg.lot_scale_start_dd = 1.0;
    v8_cfg.lot_scale_min_factor = 0.25;

    // Run V7 backtest
    printf("\n--- Running V7 Backtest ---\n");
    GridStrategy v7_strategy(v7_cfg, INITIAL_BALANCE);
    for (size_t i = 0; i < test_ticks; i++) {
        v7_strategy.OnTick(g_ticks[i]);
        if ((i + 1) % 100000 == 0) {
            printf("V7: Processed %zu ticks...\n", i + 1);
        }
    }
    BacktestStats v7_stats = v7_strategy.GetStats();

    // Run V8 backtest
    printf("\n--- Running V8 Backtest ---\n");
    GridStrategy v8_strategy(v8_cfg, INITIAL_BALANCE);
    for (size_t i = 0; i < test_ticks; i++) {
        v8_strategy.OnTick(g_ticks[i]);
        if ((i + 1) % 100000 == 0) {
            printf("V8: Processed %zu ticks...\n", i + 1);
        }
    }
    BacktestStats v8_stats = v8_strategy.GetStats();

    // Print results
    PrintStats("V7 EURUSD Results", v7_stats);
    PrintStats("V8 EURUSD Results (Tighter Protection)", v8_stats);

    // Comparison summary
    printf("\n====== COMPARISON SUMMARY ======\n");
    double v7_return = (v7_stats.final_balance - v7_stats.initial_balance) / v7_stats.initial_balance * 100.0;
    double v8_return = (v8_stats.final_balance - v8_stats.initial_balance) / v8_stats.initial_balance * 100.0;
    double v7_risk_adj = (v7_stats.max_drawdown_pct > 0) ? v7_return / v7_stats.max_drawdown_pct : 0.0;
    double v8_risk_adj = (v8_stats.max_drawdown_pct > 0) ? v8_return / v8_stats.max_drawdown_pct : 0.0;

    printf("%-25s %12s %12s %12s\n", "Metric", "V7", "V8", "Change");
    printf("%-25s %12.2f%% %12.2f%% %+12.2f%%\n", "Return",
           v7_return, v8_return, v8_return - v7_return);
    printf("%-25s %12.2f%% %12.2f%% %+12.2f%%\n", "Max Drawdown",
           v7_stats.max_drawdown_pct, v8_stats.max_drawdown_pct,
           v8_stats.max_drawdown_pct - v7_stats.max_drawdown_pct);
    printf("%-25s %12.3f %12.3f %+12.3f\n", "Risk-Adjusted Score",
           v7_risk_adj, v8_risk_adj, v8_risk_adj - v7_risk_adj);
    printf("%-25s %12d %12d %+12d\n", "Total Trades",
           v7_stats.total_trades, v8_stats.total_trades,
           v8_stats.total_trades - v7_stats.total_trades);
    printf("%-25s %12d %12d %+12d\n", "Protection Closes",
           v7_stats.protection_closes, v8_stats.protection_closes,
           v8_stats.protection_closes - v7_stats.protection_closes);

    // Verdict
    printf("\n====== VERDICT ======\n");
    if (v8_stats.max_drawdown_pct < v7_stats.max_drawdown_pct) {
        printf("V8 shows LOWER drawdown (%.2f%% vs %.2f%%) - tighter protection works\n",
               v8_stats.max_drawdown_pct, v7_stats.max_drawdown_pct);
    } else {
        printf("V8 drawdown (%.2f%%) is similar or higher than V7 (%.2f%%)\n",
               v8_stats.max_drawdown_pct, v7_stats.max_drawdown_pct);
    }

    if (v8_risk_adj > v7_risk_adj) {
        printf("V8 has BETTER risk-adjusted returns (%.3f vs %.3f)\n", v8_risk_adj, v7_risk_adj);
    } else if (v8_risk_adj == v7_risk_adj) {
        printf("V8 has EQUAL risk-adjusted returns to V7\n");
    } else {
        printf("V8 has LOWER risk-adjusted returns (%.3f vs %.3f) - stricter filter may reduce opportunities\n",
               v8_risk_adj, v7_risk_adj);
    }

    if (v8_stats.dd_lot_reductions > 0) {
        printf("DD-based lot scaling triggered %d times - active risk management\n", v8_stats.dd_lot_reductions);
    }

    // Clean up
    FreeTickData();
    printf("\n=== Test Complete ===\n");
    return 0;
}
