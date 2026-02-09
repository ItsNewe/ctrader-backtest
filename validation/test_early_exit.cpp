/**
 * Early Exit Investigation Test
 *
 * Hypothesis: When multiple positions are open and losing, there may be an
 * optimal point to cut losses rather than waiting for the emergency close.
 *
 * Exit Strategies Tested:
 * 1. Baseline: Standard V8 protection (partial at 5%, close all at 15%)
 * 2. Position-count exit: Close all if positions > 10 AND DD > 3%
 * 3. Time-based exit: Close positions open longer than N ticks without profit
 * 4. Momentum exit: Close all if price moves X% against us in Y ticks
 * 5. Equity curve exit: Close all if equity drops below 20-tick moving average
 * 6. Profit give-back: Close all if unrealized P&L drops by 50% from peak
 *
 * Goal: Find exit rules that cut losses early without exiting good trades.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cfloat>
#include <algorithm>

// =============================================================================
// DATA STRUCTURES
// =============================================================================

// Simple dynamic array
template<typename T>
struct DynArray {
    T* data;
    size_t size;
    size_t capacity;

    DynArray() : data(nullptr), size(0), capacity(0) {}
    ~DynArray() { if (data) free(data); }

    void push_back(const T& val) {
        if (size >= capacity) {
            capacity = capacity == 0 ? 16 : capacity * 2;
            data = (T*)realloc(data, capacity * sizeof(T));
        }
        data[size++] = val;
    }

    void erase(size_t idx) {
        if (idx < size) {
            memmove(&data[idx], &data[idx + 1], (size - idx - 1) * sizeof(T));
            size--;
        }
    }

    void clear() { size = 0; }
    bool empty() const { return size == 0; }
    T& operator[](size_t idx) { return data[idx]; }
    const T& operator[](size_t idx) const { return data[idx]; }
};

// Circular buffer for moving averages
struct CircularBuffer {
    double* data;
    int capacity;
    int head;
    int count;
    double sum;

    CircularBuffer(int cap) : capacity(cap), head(0), count(0), sum(0.0) {
        data = (double*)calloc(cap, sizeof(double));
    }

    ~CircularBuffer() { free(data); }

    void push(double val) {
        if (count == capacity) {
            sum -= data[head];
        } else {
            count++;
        }
        data[head] = val;
        sum += val;
        head = (head + 1) % capacity;
    }

    double average() const {
        return count > 0 ? sum / count : 0.0;
    }

    bool is_ready() const { return count >= capacity; }

    void reset() {
        head = 0;
        count = 0;
        sum = 0.0;
        memset(data, 0, capacity * sizeof(double));
    }
};

struct Tick {
    double bid;
    double ask;
    char timestamp[24];

    double spread() const { return ask - bid; }
};

struct Trade {
    size_t id;
    double entry_price;
    double lot_size;
    double take_profit;
    size_t entry_tick;  // Track which tick the trade was opened
    double peak_unrealized_pl;  // Track peak unrealized P/L for this trade
};

struct TestResult {
    char name[80];
    double return_pct;
    double max_dd_pct;
    double final_balance;
    int total_trades;
    int positions_opened;
    int early_exit_triggers;
    int emergency_close_triggers;  // 15% DD emergency closes
    double avg_recovery_ticks;     // Average ticks to recover from drawdown
    double risk_adjusted_score;
};

// =============================================================================
// EXIT STRATEGY TYPES
// =============================================================================

enum ExitStrategyType {
    EXIT_BASELINE,          // Standard V8
    EXIT_POSITION_COUNT,    // Close if positions > 10 AND DD > 3%
    EXIT_TIME_BASED,        // Close positions open > N ticks without profit
    EXIT_MOMENTUM,          // Close if price moves X% against in Y ticks
    EXIT_EQUITY_CURVE,      // Close if equity < 20-tick equity MA
    EXIT_PROFIT_GIVEBACK    // Close if unrealized P/L drops 50% from peak
};

struct ExitConfig {
    ExitStrategyType type;

    // Position count exit params
    int pos_count_threshold;    // Close if positions > this
    double pos_dd_threshold;    // AND DD > this %

    // Time-based exit params
    size_t max_ticks_no_profit; // Close if open this many ticks without profit

    // Momentum exit params
    double momentum_pct;        // Close if price moves this % against
    size_t momentum_window;     // In this many ticks

    // Equity curve params
    int equity_ma_period;       // MA period for equity curve

    // Profit give-back params
    double giveback_pct;        // Close if unrealized P/L drops by this % from peak
};

// =============================================================================
// TICK LOADING
// =============================================================================

size_t LoadTicks(const char* filename, Tick** out_ticks, size_t max_ticks) {
    FILE* fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Cannot open file: %s\n", filename);
        return 0;
    }

    Tick* ticks = (Tick*)malloc(max_ticks * sizeof(Tick));
    if (!ticks) {
        fclose(fp);
        return 0;
    }

    char line[256];
    if (!fgets(line, sizeof(line), fp)) {  // Skip header
        free(ticks);
        fclose(fp);
        return 0;
    }

    size_t count = 0;
    while (count < max_ticks && fgets(line, sizeof(line), fp)) {
        char* ts = strtok(line, "\t");
        char* bid_s = strtok(NULL, "\t");
        char* ask_s = strtok(NULL, "\t");

        if (ts && bid_s && ask_s) {
            strncpy(ticks[count].timestamp, ts, 23);
            ticks[count].timestamp[23] = '\0';
            ticks[count].bid = atof(bid_s);
            ticks[count].ask = atof(ask_s);

            if (ticks[count].bid > 0 && ticks[count].ask > 0) {
                count++;
                if (count % 1000000 == 0) {
                    printf("  Loaded %zu million ticks...\n", count / 1000000);
                }
            }
        }
    }

    fclose(fp);
    *out_ticks = ticks;
    return count;
}

// =============================================================================
// STRATEGY RUNNER WITH CONFIGURABLE EXIT
// =============================================================================

TestResult RunWithExit(const Tick* ticks, size_t tick_count, const ExitConfig& exit_cfg) {
    TestResult r;
    memset(&r, 0, sizeof(r));

    // Name based on strategy
    switch (exit_cfg.type) {
        case EXIT_BASELINE:
            strcpy(r.name, "Baseline (V8 Standard)");
            break;
        case EXIT_POSITION_COUNT:
            snprintf(r.name, sizeof(r.name), "Position-Count (>%d @ DD>%.1f%%)",
                     exit_cfg.pos_count_threshold, exit_cfg.pos_dd_threshold);
            break;
        case EXIT_TIME_BASED:
            snprintf(r.name, sizeof(r.name), "Time-Based (%zu ticks)",
                     exit_cfg.max_ticks_no_profit);
            break;
        case EXIT_MOMENTUM:
            snprintf(r.name, sizeof(r.name), "Momentum (%.2f%% in %zu ticks)",
                     exit_cfg.momentum_pct, exit_cfg.momentum_window);
            break;
        case EXIT_EQUITY_CURVE:
            snprintf(r.name, sizeof(r.name), "Equity Curve (%d-tick MA)",
                     exit_cfg.equity_ma_period);
            break;
        case EXIT_PROFIT_GIVEBACK:
            snprintf(r.name, sizeof(r.name), "Profit Give-Back (%.0f%%)",
                     exit_cfg.giveback_pct);
            break;
    }

    if (tick_count == 0) return r;

    // V8 base parameters
    const int atr_short_period = 50;
    const int atr_long_period = 1000;
    const double volatility_threshold = 0.6;
    const double tp_multiplier = 2.0;
    const double stop_new_at_dd = 3.0;
    const double partial_close_at_dd = 5.0;
    const double close_all_at_dd = 15.0;  // Emergency close at 15%
    const int max_positions = 15;
    const double spacing = 1.0;
    const double min_volume = 0.01;
    const double contract_size = 100.0;
    const double leverage = 500.0;

    double balance = 10000.0;
    double equity = balance;
    double peak_equity = balance;
    double peak_unrealized_pl = 0.0;  // Track peak unrealized for all positions

    DynArray<Trade> positions;
    size_t next_id = 1;

    // ATR tracking
    CircularBuffer atr_short(atr_short_period);
    CircularBuffer atr_long(atr_long_period);
    double last_price = 0.0;

    // Protection state
    bool partial_done = false;
    bool all_closed = false;

    // Equity curve tracking (for EXIT_EQUITY_CURVE)
    CircularBuffer equity_ma(exit_cfg.equity_ma_period > 0 ? exit_cfg.equity_ma_period : 20);

    // Momentum tracking (for EXIT_MOMENTUM)
    CircularBuffer price_history(exit_cfg.momentum_window > 0 ? (int)exit_cfg.momentum_window : 100);

    // Recovery tracking
    double recovery_start_equity = 0.0;
    size_t recovery_start_tick = 0;
    bool in_recovery = false;
    double total_recovery_ticks = 0.0;
    int recovery_count = 0;

    for (size_t i = 0; i < tick_count; i++) {
        const Tick& tick = ticks[i];

        // ATR update
        if (last_price > 0) {
            double range = fabs(tick.bid - last_price);
            atr_short.push(range);
            atr_long.push(range);
        }
        last_price = tick.bid;

        // Update price history for momentum
        price_history.push(tick.bid);

        // Calculate equity
        equity = balance;
        double current_unrealized_pl = 0.0;
        for (size_t j = 0; j < positions.size; j++) {
            double pl = (tick.bid - positions[j].entry_price) * positions[j].lot_size * contract_size;
            equity += pl;
            current_unrealized_pl += pl;

            // Update per-trade peak unrealized for profit give-back
            if (pl > positions[j].peak_unrealized_pl) {
                positions[j].peak_unrealized_pl = pl;
            }
        }

        // Update equity MA
        equity_ma.push(equity);

        // Track peak unrealized P/L (for profit give-back)
        if (current_unrealized_pl > peak_unrealized_pl) {
            peak_unrealized_pl = current_unrealized_pl;
        }

        // Reset when no positions
        if (positions.empty()) {
            if (peak_equity != balance) {
                peak_equity = balance;
                partial_done = false;
                all_closed = false;
                peak_unrealized_pl = 0.0;
            }
            if (in_recovery) {
                // Recovery complete
                total_recovery_ticks += (i - recovery_start_tick);
                recovery_count++;
                in_recovery = false;
            }
        }

        if (equity > peak_equity) {
            peak_equity = equity;
            partial_done = false;
            all_closed = false;
        }

        // Calculate drawdown
        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        if (dd_pct > r.max_dd_pct) r.max_dd_pct = dd_pct;

        // Track recovery
        if (!in_recovery && dd_pct > 1.0) {
            in_recovery = true;
            recovery_start_tick = i;
            recovery_start_equity = equity;
        }

        // =================================================================
        // EARLY EXIT CHECKS (before standard V8 protection)
        // =================================================================

        bool early_exit_triggered = false;

        switch (exit_cfg.type) {
            case EXIT_POSITION_COUNT:
                // Close all if positions > threshold AND DD > threshold
                if ((int)positions.size > exit_cfg.pos_count_threshold &&
                    dd_pct > exit_cfg.pos_dd_threshold &&
                    !positions.empty()) {
                    early_exit_triggered = true;
                }
                break;

            case EXIT_TIME_BASED:
                // Close positions that have been open too long without profit
                for (size_t j = 0; j < positions.size; ) {
                    size_t ticks_open = i - positions[j].entry_tick;
                    double pl = (tick.bid - positions[j].entry_price) * positions[j].lot_size * contract_size;

                    if (ticks_open > exit_cfg.max_ticks_no_profit && pl <= 0) {
                        balance += pl;
                        positions.erase(j);
                        r.total_trades++;
                        r.early_exit_triggers++;
                    } else {
                        j++;
                    }
                }
                break;

            case EXIT_MOMENTUM:
                // Close all if price moved significantly against us
                if (price_history.is_ready() && !positions.empty()) {
                    // Get oldest price in window
                    int oldest_idx = (price_history.head - price_history.count + price_history.capacity) % price_history.capacity;
                    double oldest_price = price_history.data[oldest_idx];

                    if (oldest_price > 0) {
                        double price_change_pct = (tick.bid - oldest_price) / oldest_price * 100.0;

                        // For long positions, we're hurt by price going DOWN
                        if (price_change_pct < -exit_cfg.momentum_pct) {
                            early_exit_triggered = true;
                        }
                    }
                }
                break;

            case EXIT_EQUITY_CURVE:
                // Close all if equity drops below equity MA
                if (equity_ma.is_ready() && !positions.empty()) {
                    double ma_value = equity_ma.average();
                    if (equity < ma_value && dd_pct > 1.0) {
                        early_exit_triggered = true;
                    }
                }
                break;

            case EXIT_PROFIT_GIVEBACK:
                // Close all if unrealized P/L dropped significantly from peak
                if (peak_unrealized_pl > 10.0 && !positions.empty()) {  // Only if we had meaningful profit
                    double giveback = peak_unrealized_pl - current_unrealized_pl;
                    double giveback_pct_of_peak = (peak_unrealized_pl > 0) ?
                                                  (giveback / peak_unrealized_pl * 100.0) : 0.0;

                    if (giveback_pct_of_peak >= exit_cfg.giveback_pct && current_unrealized_pl < 0) {
                        early_exit_triggered = true;
                    }
                }
                break;

            case EXIT_BASELINE:
            default:
                // No early exit for baseline
                break;
        }

        // Execute early exit if triggered
        if (early_exit_triggered && !positions.empty()) {
            for (size_t j = 0; j < positions.size; j++) {
                balance += (tick.bid - positions[j].entry_price) * positions[j].lot_size * contract_size;
                r.total_trades++;
            }
            positions.clear();
            r.early_exit_triggers++;
            equity = balance;
            peak_equity = balance;
            peak_unrealized_pl = 0.0;
            partial_done = false;
            all_closed = false;
            continue;
        }

        // =================================================================
        // STANDARD V8 PROTECTION (emergency close at 15%)
        // =================================================================

        if (dd_pct > close_all_at_dd && !all_closed && !positions.empty()) {
            for (size_t j = 0; j < positions.size; j++) {
                balance += (tick.bid - positions[j].entry_price) * positions[j].lot_size * contract_size;
                r.total_trades++;
            }
            positions.clear();
            all_closed = true;
            equity = balance;
            peak_equity = equity;
            peak_unrealized_pl = 0.0;
            r.emergency_close_triggers++;
            continue;
        }

        // Partial close at 5%
        if (dd_pct > partial_close_at_dd && !partial_done && positions.size > 1) {
            // Sort by P/L (worst first)
            for (size_t j = 0; j < positions.size; j++) {
                for (size_t k = j + 1; k < positions.size; k++) {
                    double pl_j = tick.bid - positions[j].entry_price;
                    double pl_k = tick.bid - positions[k].entry_price;
                    if (pl_k < pl_j) {
                        Trade tmp = positions[j];
                        positions[j] = positions[k];
                        positions[k] = tmp;
                    }
                }
            }

            int to_close = (int)(positions.size * 0.5);
            if (to_close < 1) to_close = 1;

            for (int j = 0; j < to_close && positions.size > 0; j++) {
                balance += (tick.bid - positions[0].entry_price) * positions[0].lot_size * contract_size;
                positions.erase(0);
                r.total_trades++;
            }
            partial_done = true;
        }

        // TP check
        for (size_t j = 0; j < positions.size; ) {
            if (tick.bid >= positions[j].take_profit) {
                balance += (positions[j].take_profit - positions[j].entry_price) *
                          positions[j].lot_size * contract_size;
                positions.erase(j);
                r.total_trades++;
            } else {
                j++;
            }
        }

        // Volatility filter
        bool volatility_ok = true;
        if (atr_short.is_ready() && atr_long.is_ready() && atr_long.average() > 0) {
            volatility_ok = atr_short.average() < atr_long.average() * volatility_threshold;
        }

        // Open new positions
        if (dd_pct < stop_new_at_dd && volatility_ok && (int)positions.size < max_positions) {
            double lowest = DBL_MAX, highest = -DBL_MAX;
            for (size_t j = 0; j < positions.size; j++) {
                lowest = std::min(lowest, positions[j].entry_price);
                highest = std::max(highest, positions[j].entry_price);
            }

            bool should_open = positions.empty() ||
                              (lowest >= tick.ask + spacing) ||
                              (highest <= tick.ask - spacing);

            if (should_open) {
                double lot = min_volume;
                double margin_needed = lot * contract_size * tick.ask / leverage;
                double used_margin = 0;
                for (size_t j = 0; j < positions.size; j++) {
                    used_margin += positions[j].lot_size * contract_size * positions[j].entry_price / leverage;
                }

                if (equity - used_margin > margin_needed * 2) {
                    Trade t;
                    t.id = next_id++;
                    t.entry_price = tick.ask;
                    t.lot_size = lot;
                    t.take_profit = tick.ask + tick.spread() + (spacing * tp_multiplier);
                    t.entry_tick = i;
                    t.peak_unrealized_pl = 0.0;
                    positions.push_back(t);
                    r.positions_opened++;
                }
            }
        }
    }

    // Close remaining positions
    for (size_t j = 0; j < positions.size; j++) {
        balance += (ticks[tick_count - 1].bid - positions[j].entry_price) *
                  positions[j].lot_size * contract_size;
        r.total_trades++;
    }

    r.final_balance = balance;
    r.return_pct = (balance - 10000.0) / 10000.0 * 100.0;
    r.risk_adjusted_score = (r.max_dd_pct > 0) ? r.return_pct / r.max_dd_pct : 0.0;
    r.avg_recovery_ticks = (recovery_count > 0) ? total_recovery_ticks / recovery_count : 0.0;

    return r;
}

// =============================================================================
// MAIN
// =============================================================================

void PrintResult(const TestResult& r) {
    printf("%-40s | %8.2f%% | %8.2f%% | %6.2f | %6d | %6d | %6d | %10.0f\n",
           r.name,
           r.return_pct,
           r.max_dd_pct,
           r.risk_adjusted_score,
           r.early_exit_triggers,
           r.emergency_close_triggers,
           r.total_trades,
           r.avg_recovery_ticks);
}

int main() {
    printf("=======================================================================\n");
    printf("         EARLY EXIT INVESTIGATION TEST\n");
    printf("=======================================================================\n\n");

    printf("Hypothesis: There may be an optimal point to cut losses before the\n");
    printf("emergency 15%% close, reducing max drawdown while preserving returns.\n\n");

    // Try multiple paths
    const char* filenames[] = {
        "Broker/XAUUSD_TICKS_2025.csv",
        "../validation/Broker/XAUUSD_TICKS_2025.csv",
        "validation/Broker/XAUUSD_TICKS_2025.csv"
    };
    const char* filename = nullptr;
    FILE* test_fp = nullptr;
    for (int i = 0; i < 3; i++) {
        test_fp = fopen(filenames[i], "r");
        if (test_fp) {
            fclose(test_fp);
            filename = filenames[i];
            break;
        }
    }
    if (!filename) {
        filename = "Broker/XAUUSD_TICKS_2025.csv";
    }

    printf("Loading ticks from %s...\n", filename);
    const size_t max_ticks = 5000000;  // 5 million ticks

    Tick* ticks = nullptr;
    size_t tick_count = LoadTicks(filename, &ticks, max_ticks);

    if (tick_count == 0) {
        fprintf(stderr, "Failed to load tick data!\n");
        return 1;
    }

    printf("\nLoaded %zu ticks (%.1f million)\n", tick_count, tick_count / 1000000.0);
    printf("Time span: %s to %s\n\n", ticks[0].timestamp, ticks[tick_count-1].timestamp);

    // =================================================================
    // RUN ALL EXIT STRATEGIES
    // =================================================================

    printf("Running exit strategy tests...\n\n");

    DynArray<TestResult> results;

    // 1. Baseline (Standard V8)
    printf("  Testing: Baseline (V8 Standard)...\n");
    {
        ExitConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.type = EXIT_BASELINE;
        cfg.equity_ma_period = 20;
        cfg.momentum_window = 100;
        results.push_back(RunWithExit(ticks, tick_count, cfg));
    }

    // 2. Position-count exit variants
    printf("  Testing: Position-Count exits...\n");
    {
        ExitConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.type = EXIT_POSITION_COUNT;
        cfg.equity_ma_period = 20;
        cfg.momentum_window = 100;

        cfg.pos_count_threshold = 10; cfg.pos_dd_threshold = 3.0;
        results.push_back(RunWithExit(ticks, tick_count, cfg));

        cfg.pos_count_threshold = 8; cfg.pos_dd_threshold = 2.0;
        results.push_back(RunWithExit(ticks, tick_count, cfg));

        cfg.pos_count_threshold = 5; cfg.pos_dd_threshold = 2.0;
        results.push_back(RunWithExit(ticks, tick_count, cfg));
    }

    // 3. Time-based exit variants
    printf("  Testing: Time-Based exits...\n");
    {
        ExitConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.type = EXIT_TIME_BASED;
        cfg.equity_ma_period = 20;
        cfg.momentum_window = 100;

        cfg.max_ticks_no_profit = 50000;
        results.push_back(RunWithExit(ticks, tick_count, cfg));

        cfg.max_ticks_no_profit = 100000;
        results.push_back(RunWithExit(ticks, tick_count, cfg));

        cfg.max_ticks_no_profit = 200000;
        results.push_back(RunWithExit(ticks, tick_count, cfg));
    }

    // 4. Momentum exit variants
    printf("  Testing: Momentum exits...\n");
    {
        ExitConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.type = EXIT_MOMENTUM;
        cfg.equity_ma_period = 20;

        cfg.momentum_pct = 0.5; cfg.momentum_window = 1000;
        results.push_back(RunWithExit(ticks, tick_count, cfg));

        cfg.momentum_pct = 1.0; cfg.momentum_window = 2000;
        results.push_back(RunWithExit(ticks, tick_count, cfg));

        cfg.momentum_pct = 0.3; cfg.momentum_window = 500;
        results.push_back(RunWithExit(ticks, tick_count, cfg));
    }

    // 5. Equity curve exit variants
    printf("  Testing: Equity Curve exits...\n");
    {
        ExitConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.type = EXIT_EQUITY_CURVE;
        cfg.momentum_window = 100;

        cfg.equity_ma_period = 20;
        results.push_back(RunWithExit(ticks, tick_count, cfg));

        cfg.equity_ma_period = 50;
        results.push_back(RunWithExit(ticks, tick_count, cfg));

        cfg.equity_ma_period = 100;
        results.push_back(RunWithExit(ticks, tick_count, cfg));
    }

    // 6. Profit give-back exit variants
    printf("  Testing: Profit Give-Back exits...\n");
    {
        ExitConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.type = EXIT_PROFIT_GIVEBACK;
        cfg.equity_ma_period = 20;
        cfg.momentum_window = 100;

        cfg.giveback_pct = 50.0;
        results.push_back(RunWithExit(ticks, tick_count, cfg));

        cfg.giveback_pct = 75.0;
        results.push_back(RunWithExit(ticks, tick_count, cfg));

        cfg.giveback_pct = 30.0;
        results.push_back(RunWithExit(ticks, tick_count, cfg));
    }

    // =================================================================
    // RESULTS TABLE
    // =================================================================

    printf("\n");
    printf("=======================================================================\n");
    printf("                           RESULTS\n");
    printf("=======================================================================\n");
    printf("\n");

    printf("%-40s | %9s | %9s | %6s | %6s | %6s | %6s | %10s\n",
           "Strategy", "Return", "Max DD", "Score", "Early", "Emerg", "Trades", "Recov Ticks");
    printf("-----------------------------------------------------------------------");
    printf("-----------------------------------------------------------------------\n");

    for (size_t i = 0; i < results.size; i++) {
        PrintResult(results[i]);
    }

    printf("-----------------------------------------------------------------------");
    printf("-----------------------------------------------------------------------\n");

    // =================================================================
    // ANALYSIS
    // =================================================================

    printf("\n");
    printf("=======================================================================\n");
    printf("                          ANALYSIS\n");
    printf("=======================================================================\n\n");

    // Find best by different metrics
    TestResult best_return = results[0];
    TestResult best_dd = results[0];
    TestResult best_score = results[0];
    TestResult least_emergency = results[0];

    for (size_t i = 0; i < results.size; i++) {
        if (results[i].return_pct > best_return.return_pct) best_return = results[i];
        if (results[i].max_dd_pct < best_dd.max_dd_pct) best_dd = results[i];
        if (results[i].risk_adjusted_score > best_score.risk_adjusted_score) best_score = results[i];
        if (results[i].emergency_close_triggers < least_emergency.emergency_close_triggers) {
            least_emergency = results[i];
        }
    }

    printf("Best by Return:           %s (%.2f%%)\n", best_return.name, best_return.return_pct);
    printf("Best by Max DD:           %s (%.2f%%)\n", best_dd.name, best_dd.max_dd_pct);
    printf("Best by Risk-Adj Score:   %s (%.2f)\n", best_score.name, best_score.risk_adjusted_score);
    printf("Fewest Emergency Closes:  %s (%d)\n", least_emergency.name, least_emergency.emergency_close_triggers);

    // Compare early exit strategies to baseline
    printf("\n");
    printf("COMPARISON TO BASELINE:\n");
    printf("-----------------------\n\n");

    TestResult baseline = results[0];

    printf("%-40s | %10s | %10s | %10s\n", "Strategy", "Return D", "DD D", "Score D");
    printf("-----------------------------------------------------------------------\n");

    for (size_t i = 1; i < results.size; i++) {
        double ret_diff = results[i].return_pct - baseline.return_pct;
        double dd_diff = results[i].max_dd_pct - baseline.max_dd_pct;
        double score_diff = results[i].risk_adjusted_score - baseline.risk_adjusted_score;

        printf("%-40s | %+9.2f%% | %+9.2f%% | %+10.2f\n",
               results[i].name, ret_diff, dd_diff, score_diff);
    }

    // =================================================================
    // CONCLUSIONS
    // =================================================================

    printf("\n");
    printf("=======================================================================\n");
    printf("                         CONCLUSIONS\n");
    printf("=======================================================================\n\n");

    // Identify strategies that improved DD without killing returns
    printf("Strategies that REDUCED Max DD while maintaining >80%% of baseline return:\n");
    printf("(Baseline: Return=%.2f%%, DD=%.2f%%)\n\n", baseline.return_pct, baseline.max_dd_pct);

    bool found_improvement = false;
    for (size_t i = 1; i < results.size; i++) {
        if (results[i].max_dd_pct < baseline.max_dd_pct &&
            results[i].return_pct >= baseline.return_pct * 0.80) {
            printf("  [WINNER] %s\n", results[i].name);
            printf("           Return: %.2f%% (%.1f%% of baseline)\n",
                   results[i].return_pct,
                   results[i].return_pct / baseline.return_pct * 100.0);
            printf("           Max DD: %.2f%% (%.1f%% reduction)\n",
                   results[i].max_dd_pct,
                   (baseline.max_dd_pct - results[i].max_dd_pct) / baseline.max_dd_pct * 100.0);
            printf("           Score:  %.2f (vs baseline %.2f)\n\n",
                   results[i].risk_adjusted_score, baseline.risk_adjusted_score);
            found_improvement = true;
        }
    }

    if (!found_improvement) {
        printf("  No strategy significantly improved on baseline.\n");
        printf("  The V8 standard protection may already be near-optimal.\n\n");
    }

    // Final recommendation
    printf("OPTIMAL EXIT TIMING RECOMMENDATION:\n");
    printf("-----------------------------------\n");
    if (best_score.risk_adjusted_score > baseline.risk_adjusted_score * 1.05) {
        printf("Recommended: %s\n", best_score.name);
        printf("Reason: Best risk-adjusted score (%.2f) with acceptable return (%.2f%%)\n",
               best_score.risk_adjusted_score, best_score.return_pct);
    } else {
        printf("Recommended: Keep baseline V8 protection\n");
        printf("Reason: No alternative significantly outperformed the baseline\n");
    }

    // Cleanup
    free(ticks);

    printf("\n=======================================================================\n");
    printf("                        TEST COMPLETE\n");
    printf("=======================================================================\n");

    return 0;
}
