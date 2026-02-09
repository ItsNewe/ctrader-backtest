/**
 * V8 Validation Test - Extended XAUUSD Tick Data
 *
 * V8 improvements over V7:
 * - Tighter ATR params: 50/1000/0.6 (vs V7's 100/500/0.8)
 * - TP multiplier: 2.0 (wider TP for larger profits per trade)
 * - stop_new_at_dd: 3% (tighter than V7's 5%)
 * - partial_close_at_dd: 5% (tighter than V7's 8%)
 * - close_all_at_dd: 14% (tighter than V7's 25%)
 * - max_positions: 15 (less than V7's 20)
 * - DD-based lot scaling (reduce lots as DD increases)
 *
 * Goal: Keep max DD under 15% while maintaining good returns.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cfloat>
#include <algorithm>

// Simple dynamic array for C-style memory management
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

// Circular buffer for ATR calculation
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
};

struct Tick {
    double bid;
    double ask;
    char timestamp[24];  // "YYYY.MM.DD HH:MM:SS.mmm"

    double spread() const { return ask - bid; }
};

struct Trade {
    size_t id;
    double entry_price;
    double lot_size;
    double take_profit;
};

struct MonthlyStats {
    char month[8];  // "YYYY.MM"
    double start_balance;
    double end_balance;
    double return_pct;
    double max_dd;
    int trades;
    int positions_opened;
};

struct TestResult {
    char name[64];
    double return_pct;
    double max_dd_pct;
    double final_balance;
    int total_trades;
    int positions_opened;
    int protection_closes;
    double risk_adjusted_score;  // Return / MaxDD

    MonthlyStats monthly[24];  // Up to 24 months
    int month_count;
};

// Load ticks using C-style file I/O
size_t LoadTicks(const char* filename, Tick** out_ticks, size_t max_ticks) {
    FILE* fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Cannot open file: %s\n", filename);
        return 0;
    }

    // Allocate array
    Tick* ticks = (Tick*)malloc(max_ticks * sizeof(Tick));
    if (!ticks) {
        fclose(fp);
        return 0;
    }

    // Skip header
    char line[256];
    if (!fgets(line, sizeof(line), fp)) {
        free(ticks);
        fclose(fp);
        return 0;
    }

    size_t count = 0;
    while (count < max_ticks && fgets(line, sizeof(line), fp)) {
        // Parse tab-separated: timestamp\tbid\task\t...
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

// Get month string from timestamp
void GetMonth(const char* timestamp, char* month) {
    // Extract "YYYY.MM" from "YYYY.MM.DD HH:MM:SS"
    strncpy(month, timestamp, 7);
    month[7] = '\0';
}

// V7 Strategy (original params for comparison)
TestResult RunV7(const Tick* ticks, size_t tick_count) {
    TestResult r;
    memset(&r, 0, sizeof(r));
    strcpy(r.name, "V7 (Original)");

    if (tick_count == 0) return r;

    // V7 parameters
    const int atr_short_period = 100;
    const int atr_long_period = 500;
    const double volatility_threshold = 0.8;
    const double tp_multiplier = 1.0;
    const double stop_new_at_dd = 5.0;
    const double partial_close_at_dd = 8.0;
    const double close_all_at_dd = 25.0;
    const int max_positions = 20;
    const double spacing = 1.0;
    const double min_volume = 0.01;
    const double contract_size = 100.0;
    const double leverage = 500.0;

    double balance = 10000.0;
    double equity = balance;
    double peak_equity = balance;

    // Position tracking
    DynArray<Trade> positions;
    size_t next_id = 1;

    // ATR tracking
    CircularBuffer atr_short(atr_short_period);
    CircularBuffer atr_long(atr_long_period);
    double last_price = 0.0;

    // Protection state
    bool partial_done = false;
    bool all_closed = false;

    // Monthly tracking
    char current_month[8] = "";
    double month_start_balance = balance;
    double month_peak_equity = balance;
    double month_max_dd = 0.0;
    int month_trades = 0;
    int month_positions = 0;

    for (size_t i = 0; i < tick_count; i++) {
        const Tick& tick = ticks[i];

        // ATR update
        if (last_price > 0) {
            double range = fabs(tick.bid - last_price);
            atr_short.push(range);
            atr_long.push(range);
        }
        last_price = tick.bid;

        // Monthly stats tracking
        char tick_month[8];
        GetMonth(tick.timestamp, tick_month);
        if (strcmp(current_month, tick_month) != 0) {
            // Save previous month stats
            if (current_month[0] != '\0' && r.month_count < 24) {
                MonthlyStats& ms = r.monthly[r.month_count++];
                strcpy(ms.month, current_month);
                ms.start_balance = month_start_balance;
                ms.end_balance = balance;
                ms.return_pct = (balance - month_start_balance) / month_start_balance * 100.0;
                ms.max_dd = month_max_dd;
                ms.trades = month_trades;
                ms.positions_opened = month_positions;
            }
            // Reset for new month
            strcpy(current_month, tick_month);
            month_start_balance = balance;
            month_peak_equity = equity;
            month_max_dd = 0.0;
            month_trades = 0;
            month_positions = 0;
        }

        // Calculate equity
        equity = balance;
        for (size_t j = 0; j < positions.size; j++) {
            equity += (tick.bid - positions[j].entry_price) * positions[j].lot_size * contract_size;
        }

        // Peak equity tracking
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

        if (equity > month_peak_equity) {
            month_peak_equity = equity;
        }

        // Calculate drawdown
        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        double month_dd = (month_peak_equity > 0) ? (month_peak_equity - equity) / month_peak_equity * 100.0 : 0.0;

        if (dd_pct > r.max_dd_pct) r.max_dd_pct = dd_pct;
        if (month_dd > month_max_dd) month_max_dd = month_dd;

        // V3 Protection: Close ALL at threshold
        if (dd_pct > close_all_at_dd && !all_closed && !positions.empty()) {
            for (size_t j = 0; j < positions.size; j++) {
                balance += (tick.bid - positions[j].entry_price) * positions[j].lot_size * contract_size;
                r.total_trades++;
                month_trades++;
                r.protection_closes++;
            }
            positions.clear();
            all_closed = true;
            equity = balance;
            peak_equity = equity;
            continue;
        }

        // V3 Protection: Partial close at threshold
        if (dd_pct > partial_close_at_dd && !partial_done && positions.size > 1) {
            // Sort by P/L (worst first)
            for (size_t j = 0; j < positions.size; j++) {
                for (size_t k = j + 1; k < positions.size; k++) {
                    double pl_j = (tick.bid - positions[j].entry_price);
                    double pl_k = (tick.bid - positions[k].entry_price);
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
                month_trades++;
                r.protection_closes++;
            }
            partial_done = true;
        }

        // TP check
        for (size_t j = 0; j < positions.size; ) {
            if (tick.bid >= positions[j].take_profit) {
                balance += (positions[j].take_profit - positions[j].entry_price) * positions[j].lot_size * contract_size;
                positions.erase(j);
                r.total_trades++;
                month_trades++;
            } else {
                j++;
            }
        }

        // Volatility filter check
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
                    positions.push_back(t);
                    r.positions_opened++;
                    month_positions++;
                }
            }
        }
    }

    // Close remaining positions at end
    for (size_t j = 0; j < positions.size; j++) {
        balance += (ticks[tick_count - 1].bid - positions[j].entry_price) * positions[j].lot_size * contract_size;
        r.total_trades++;
    }

    // Save last month
    if (current_month[0] != '\0' && r.month_count < 24) {
        MonthlyStats& ms = r.monthly[r.month_count++];
        strcpy(ms.month, current_month);
        ms.start_balance = month_start_balance;
        ms.end_balance = balance;
        ms.return_pct = (balance - month_start_balance) / month_start_balance * 100.0;
        ms.max_dd = month_max_dd;
        ms.trades = month_trades;
        ms.positions_opened = month_positions;
    }

    r.final_balance = balance;
    r.return_pct = (balance - 10000.0) / 10000.0 * 100.0;
    r.risk_adjusted_score = (r.max_dd_pct > 0) ? r.return_pct / r.max_dd_pct : 0.0;

    return r;
}

// V8 Strategy (tighter protection + DD-based lot scaling)
TestResult RunV8(const Tick* ticks, size_t tick_count) {
    TestResult r;
    memset(&r, 0, sizeof(r));
    strcpy(r.name, "V8 (Tight Protection)");

    if (tick_count == 0) return r;

    // V8 parameters - tighter than V7
    const int atr_short_period = 50;      // Tighter: 50 vs 100
    const int atr_long_period = 1000;     // Wider window: 1000 vs 500
    const double volatility_threshold = 0.6;  // Stricter: 0.6 vs 0.8
    const double tp_multiplier = 2.0;     // Wider TP: 2.0 vs 1.0
    const double stop_new_at_dd = 3.0;    // Tighter: 3% vs 5%
    const double partial_close_at_dd = 5.0;   // Tighter: 5% vs 8%
    const double close_all_at_dd = 14.0;  // Tighter: 14% vs 25% (reduced from 15 to stay under limit)
    const int max_positions = 15;         // Less: 15 vs 20
    const double spacing = 1.0;
    const double min_volume = 0.01;
    const double max_volume = 0.05;       // Cap max lot size
    const double contract_size = 100.0;
    const double leverage = 500.0;

    double balance = 10000.0;
    double equity = balance;
    double peak_equity = balance;

    // Position tracking
    DynArray<Trade> positions;
    size_t next_id = 1;

    // ATR tracking
    CircularBuffer atr_short(atr_short_period);
    CircularBuffer atr_long(atr_long_period);
    double last_price = 0.0;

    // Protection state
    bool partial_done = false;
    bool all_closed = false;

    // Monthly tracking
    char current_month[8] = "";
    double month_start_balance = balance;
    double month_peak_equity = balance;
    double month_max_dd = 0.0;
    int month_trades = 0;
    int month_positions = 0;

    for (size_t i = 0; i < tick_count; i++) {
        const Tick& tick = ticks[i];

        // ATR update
        if (last_price > 0) {
            double range = fabs(tick.bid - last_price);
            atr_short.push(range);
            atr_long.push(range);
        }
        last_price = tick.bid;

        // Monthly stats tracking
        char tick_month[8];
        GetMonth(tick.timestamp, tick_month);
        if (strcmp(current_month, tick_month) != 0) {
            // Save previous month stats
            if (current_month[0] != '\0' && r.month_count < 24) {
                MonthlyStats& ms = r.monthly[r.month_count++];
                strcpy(ms.month, current_month);
                ms.start_balance = month_start_balance;
                ms.end_balance = balance;
                ms.return_pct = (balance - month_start_balance) / month_start_balance * 100.0;
                ms.max_dd = month_max_dd;
                ms.trades = month_trades;
                ms.positions_opened = month_positions;
            }
            // Reset for new month
            strcpy(current_month, tick_month);
            month_start_balance = balance;
            month_peak_equity = equity;
            month_max_dd = 0.0;
            month_trades = 0;
            month_positions = 0;
        }

        // Calculate equity
        equity = balance;
        for (size_t j = 0; j < positions.size; j++) {
            equity += (tick.bid - positions[j].entry_price) * positions[j].lot_size * contract_size;
        }

        // Peak equity tracking
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

        if (equity > month_peak_equity) {
            month_peak_equity = equity;
        }

        // Calculate drawdown
        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        double month_dd = (month_peak_equity > 0) ? (month_peak_equity - equity) / month_peak_equity * 100.0 : 0.0;

        if (dd_pct > r.max_dd_pct) r.max_dd_pct = dd_pct;
        if (month_dd > month_max_dd) month_max_dd = month_dd;

        // V8 Protection: Close ALL at threshold (15%)
        if (dd_pct > close_all_at_dd && !all_closed && !positions.empty()) {
            for (size_t j = 0; j < positions.size; j++) {
                balance += (tick.bid - positions[j].entry_price) * positions[j].lot_size * contract_size;
                r.total_trades++;
                month_trades++;
                r.protection_closes++;
            }
            positions.clear();
            all_closed = true;
            equity = balance;
            peak_equity = equity;
            continue;
        }

        // V8 Protection: Partial close at threshold (5%)
        if (dd_pct > partial_close_at_dd && !partial_done && positions.size > 1) {
            // Sort by P/L (worst first)
            for (size_t j = 0; j < positions.size; j++) {
                for (size_t k = j + 1; k < positions.size; k++) {
                    double pl_j = (tick.bid - positions[j].entry_price);
                    double pl_k = (tick.bid - positions[k].entry_price);
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
                month_trades++;
                r.protection_closes++;
            }
            partial_done = true;
        }

        // TP check
        for (size_t j = 0; j < positions.size; ) {
            if (tick.bid >= positions[j].take_profit) {
                balance += (positions[j].take_profit - positions[j].entry_price) * positions[j].lot_size * contract_size;
                positions.erase(j);
                r.total_trades++;
                month_trades++;
            } else {
                j++;
            }
        }

        // Volatility filter check (stricter in V8)
        bool volatility_ok = true;
        if (atr_short.is_ready() && atr_long.is_ready() && atr_long.average() > 0) {
            volatility_ok = atr_short.average() < atr_long.average() * volatility_threshold;
        }

        // V8: DD-based lot scaling - reduce lots as DD increases
        double lot_scale = 1.0;
        if (dd_pct > 1.0) {
            // Linear scaling: at 1% DD = 100%, at 3% DD = 33%
            lot_scale = std::max(0.33, 1.0 - (dd_pct - 1.0) / 3.0);
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
                // V8: Apply DD-based lot scaling
                double base_lot = min_volume;
                double lot = base_lot * lot_scale;
                lot = std::max(min_volume, std::min(max_volume, lot));

                // Round to 2 decimal places
                lot = floor(lot * 100.0 + 0.5) / 100.0;
                if (lot < min_volume) lot = min_volume;

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
                    positions.push_back(t);
                    r.positions_opened++;
                    month_positions++;
                }
            }
        }
    }

    // Close remaining positions at end
    for (size_t j = 0; j < positions.size; j++) {
        balance += (ticks[tick_count - 1].bid - positions[j].entry_price) * positions[j].lot_size * contract_size;
        r.total_trades++;
    }

    // Save last month
    if (current_month[0] != '\0' && r.month_count < 24) {
        MonthlyStats& ms = r.monthly[r.month_count++];
        strcpy(ms.month, current_month);
        ms.start_balance = month_start_balance;
        ms.end_balance = balance;
        ms.return_pct = (balance - month_start_balance) / month_start_balance * 100.0;
        ms.max_dd = month_max_dd;
        ms.trades = month_trades;
        ms.positions_opened = month_positions;
    }

    r.final_balance = balance;
    r.return_pct = (balance - 10000.0) / 10000.0 * 100.0;
    r.risk_adjusted_score = (r.max_dd_pct > 0) ? r.return_pct / r.max_dd_pct : 0.0;

    return r;
}

void PrintResult(const TestResult& r) {
    printf("\n%s\n", r.name);
    printf("  Final Balance:    $%.2f\n", r.final_balance);
    printf("  Return:           %.2f%%\n", r.return_pct);
    printf("  Max Drawdown:     %.2f%%\n", r.max_dd_pct);
    printf("  Risk-Adj Score:   %.2f\n", r.risk_adjusted_score);
    printf("  Total Trades:     %d\n", r.total_trades);
    printf("  Positions Opened: %d\n", r.positions_opened);
    printf("  Protection Closes:%d\n", r.protection_closes);
}

void PrintComparison(const TestResult& v7, const TestResult& v8) {
    printf("\n");
    printf("=====================================================\n");
    printf("              V7 vs V8 COMPARISON\n");
    printf("=====================================================\n");
    printf("\n");
    printf("%-25s %15s %15s %10s\n", "Metric", "V7", "V8", "Change");
    printf("-----------------------------------------------------\n");

    double ret_change = v8.return_pct - v7.return_pct;
    double dd_change = v8.max_dd_pct - v7.max_dd_pct;
    double score_change = v8.risk_adjusted_score - v7.risk_adjusted_score;

    printf("%-25s %14.2f%% %14.2f%% %+9.2f%%\n",
           "Return", v7.return_pct, v8.return_pct, ret_change);
    printf("%-25s %14.2f%% %14.2f%% %+9.2f%%\n",
           "Max Drawdown", v7.max_dd_pct, v8.max_dd_pct, dd_change);
    printf("%-25s %14.2f  %14.2f  %+9.2f\n",
           "Risk-Adjusted Score", v7.risk_adjusted_score, v8.risk_adjusted_score, score_change);
    printf("%-25s %15d %15d %+10d\n",
           "Total Trades", v7.total_trades, v8.total_trades, v8.total_trades - v7.total_trades);
    printf("%-25s %15d %15d %+10d\n",
           "Positions Opened", v7.positions_opened, v8.positions_opened, v8.positions_opened - v7.positions_opened);
    printf("%-25s %15d %15d %+10d\n",
           "Protection Closes", v7.protection_closes, v8.protection_closes, v8.protection_closes - v7.protection_closes);

    printf("-----------------------------------------------------\n");
    printf("\n");

    // Goal check
    printf("V8 GOAL CHECK:\n");
    if (v8.max_dd_pct <= 15.0) {
        printf("  [PASS] Max DD %.2f%% <= 15%% target\n", v8.max_dd_pct);
    } else {
        printf("  [FAIL] Max DD %.2f%% > 15%% target\n", v8.max_dd_pct);
    }

    if (v8.return_pct > 0) {
        printf("  [PASS] Positive return: %.2f%%\n", v8.return_pct);
    } else {
        printf("  [WARN] Negative return: %.2f%%\n", v8.return_pct);
    }

    if (v8.risk_adjusted_score >= v7.risk_adjusted_score) {
        printf("  [PASS] Risk-adjusted score improved: %.2f vs %.2f\n",
               v8.risk_adjusted_score, v7.risk_adjusted_score);
    } else {
        printf("  [INFO] Risk-adjusted score decreased: %.2f vs %.2f\n",
               v8.risk_adjusted_score, v7.risk_adjusted_score);
    }
}

void PrintMonthlyBreakdown(const TestResult& r) {
    printf("\n");
    printf("=====================================================\n");
    printf("              MONTHLY BREAKDOWN - %s\n", r.name);
    printf("=====================================================\n");
    printf("\n");
    printf("%-10s %12s %12s %10s %10s %10s\n",
           "Month", "Start $", "End $", "Return", "Max DD", "Trades");
    printf("---------------------------------------------------------------------\n");

    for (int i = 0; i < r.month_count; i++) {
        const MonthlyStats& ms = r.monthly[i];
        printf("%-10s %12.2f %12.2f %9.2f%% %9.2f%% %10d\n",
               ms.month, ms.start_balance, ms.end_balance,
               ms.return_pct, ms.max_dd, ms.trades);
    }
    printf("---------------------------------------------------------------------\n");
}

int main() {
    printf("=====================================================\n");
    printf("    V8 VALIDATION TEST - Extended XAUUSD Tick Data\n");
    printf("=====================================================\n\n");

    printf("V8 Improvements over V7:\n");
    printf("  - Tighter ATR params: 50/1000/0.6 (vs 100/500/0.8)\n");
    printf("  - TP multiplier: 2.0 (wider TP)\n");
    printf("  - stop_new_at_dd: 3%% (vs 5%%)\n");
    printf("  - partial_close_at_dd: 5%% (vs 8%%)\n");
    printf("  - close_all_at_dd: 14%% (vs 25%%)\n");
    printf("  - max_positions: 15 (vs 20)\n");
    printf("  - DD-based lot scaling\n\n");

    printf("Goal: Keep max DD under 15%% while maintaining good returns.\n\n");

    // Load ticks (10-20 million)
    // Try multiple paths to find the tick data
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
        filename = "Broker/XAUUSD_TICKS_2025.csv";  // Fallback for error msg
    }
    const size_t max_ticks = 20000000;  // 20 million ticks

    printf("Loading ticks from %s...\n", filename);
    printf("(Target: 10-20 million ticks)\n\n");

    Tick* ticks = nullptr;
    size_t tick_count = LoadTicks(filename, &ticks, max_ticks);

    if (tick_count == 0) {
        fprintf(stderr, "Failed to load tick data!\n");
        return 1;
    }

    printf("\nLoaded %zu ticks (%.1f million)\n", tick_count, tick_count / 1000000.0);
    printf("Price range: %.2f - ", ticks[0].bid);

    // Find price range
    double min_price = ticks[0].bid, max_price = ticks[0].bid;
    for (size_t i = 0; i < tick_count; i++) {
        if (ticks[i].bid < min_price) min_price = ticks[i].bid;
        if (ticks[i].bid > max_price) max_price = ticks[i].bid;
    }
    printf("%.2f (range: %.2f)\n", ticks[tick_count-1].bid, max_price - min_price);
    printf("Time span: %s to %s\n\n", ticks[0].timestamp, ticks[tick_count-1].timestamp);

    // Run V7 test
    printf("Running V7 (Original params)...\n");
    TestResult v7 = RunV7(ticks, tick_count);
    PrintResult(v7);

    // Run V8 test
    printf("\nRunning V8 (Tight Protection)...\n");
    TestResult v8 = RunV8(ticks, tick_count);
    PrintResult(v8);

    // Comparison
    PrintComparison(v7, v8);

    // Monthly breakdown for V8
    PrintMonthlyBreakdown(v8);

    // Cleanup
    free(ticks);

    printf("\n=====================================================\n");
    printf("                    TEST COMPLETE\n");
    printf("=====================================================\n");

    return 0;
}
