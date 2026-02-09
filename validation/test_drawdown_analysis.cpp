/**
 * DRAWDOWN ANALYSIS TEST
 *
 * Analyzes what market conditions cause the worst drawdowns in V7/V8 strategies.
 *
 * Goals:
 * 1. Identify the top 5 worst drawdown events
 * 2. Analyze market characteristics during those periods:
 *    - Price movement (trend direction, magnitude)
 *    - Volatility levels
 *    - Number of open positions
 *    - Time of day patterns
 * 3. Measure recovery times
 * 4. Suggest potential improvements based on findings
 *
 * Uses C-style file I/O for performance with large tick datasets.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
#include <deque>
#include <string>

// ============================================================================
// DATA STRUCTURES
// ============================================================================

struct Tick {
    char timestamp[32];
    double bid;
    double ask;
    int hour;           // Extracted hour (0-23)
    int day_of_week;    // 0=Sun, 1=Mon, etc.

    double spread() const { return ask - bid; }
    double mid() const { return (bid + ask) / 2.0; }
};

struct Trade {
    int id;
    double entry_price;
    double lot_size;
    double take_profit;
    int entry_tick_index;
};

// Drawdown event tracking
struct DrawdownEvent {
    int start_tick;         // When DD started (crossed threshold)
    int peak_tick;          // When DD was at maximum
    int end_tick;           // When DD was fully recovered

    double peak_dd_pct;     // Maximum drawdown percentage
    double peak_dd_amount;  // Maximum drawdown in $

    // Market conditions at peak
    double price_at_start;
    double price_at_peak;
    double price_at_end;
    double price_change_pct;    // Price change from start to peak

    double volatility_at_peak;  // ATR short at peak
    double volatility_ratio;    // ATR short / ATR long at peak

    int positions_at_peak;
    int hour_at_peak;
    int day_at_peak;

    // Duration metrics
    int ticks_to_peak;          // Ticks from start to peak
    int ticks_to_recovery;      // Ticks from start to full recovery
    double recovery_price_change_pct;  // Price change during recovery

    char timestamp_start[32];
    char timestamp_peak[32];
    char timestamp_end[32];

    bool is_recovered;
};

// Price movement pattern classification
enum class PricePattern {
    SHARP_DROP,         // Fast, large decline
    GRADUAL_DECLINE,    // Slow, steady decline
    VOLATILE_CHOP,      // High volatility, no clear direction
    V_REVERSAL,         // Sharp drop then quick recovery
    EXTENDED_RANGE      // Price stuck in unfavorable range
};

// ============================================================================
// ATR CALCULATOR (for volatility analysis)
// ============================================================================

class ATR {
public:
    std::deque<double> ranges;
    int period;
    double sum;
    double last_price;

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

    double Get() const {
        return ranges.empty() ? 0.0 : sum / ranges.size();
    }

    bool IsReady() const {
        return (int)ranges.size() >= period;
    }
};

// ============================================================================
// TICK LOADING (C-style I/O for performance)
// ============================================================================

int parse_hour(const char* timestamp) {
    // Format: "YYYY.MM.DD HH:MM:SS.mmm" - hour starts at position 11
    if (strlen(timestamp) >= 13) {
        char hour_str[3] = {timestamp[11], timestamp[12], '\0'};
        return atoi(hour_str);
    }
    return 0;
}

int parse_day_of_week(const char* timestamp) {
    // Parse YYYY.MM.DD and calculate day of week using Zeller's congruence
    if (strlen(timestamp) < 10) return 0;

    int year = (timestamp[0]-'0')*1000 + (timestamp[1]-'0')*100 +
               (timestamp[2]-'0')*10 + (timestamp[3]-'0');
    int month = (timestamp[5]-'0')*10 + (timestamp[6]-'0');
    int day = (timestamp[8]-'0')*10 + (timestamp[9]-'0');

    if (month < 3) {
        month += 12;
        year--;
    }

    int century = year / 100;
    year = year % 100;

    int dow = (day + (13 * (month + 1)) / 5 + year + year / 4 + century / 4 - 2 * century) % 7;
    dow = (dow + 6) % 7;  // Convert to 0=Sun format

    return dow;
}

std::vector<Tick> LoadTicks(const char* filename, size_t max_ticks) {
    std::vector<Tick> ticks;
    ticks.reserve(max_ticks);

    FILE* file = fopen(filename, "r");
    if (!file) {
        printf("ERROR: Cannot open file: %s\n", filename);
        return ticks;
    }

    char line[256];

    // Skip header
    if (!fgets(line, sizeof(line), file)) {
        fclose(file);
        return ticks;
    }

    printf("Loading ticks (max %zu)...\n", max_ticks);

    while (fgets(line, sizeof(line), file) && ticks.size() < max_ticks) {
        Tick tick;
        char bid_str[32], ask_str[32];

        // Parse tab-delimited: timestamp \t bid \t ask \t ...
        char* token = strtok(line, "\t");
        if (!token) continue;
        strncpy(tick.timestamp, token, 31);
        tick.timestamp[31] = '\0';

        token = strtok(NULL, "\t");
        if (!token) continue;
        strncpy(bid_str, token, 31);

        token = strtok(NULL, "\t");
        if (!token) continue;
        strncpy(ask_str, token, 31);

        tick.bid = atof(bid_str);
        tick.ask = atof(ask_str);

        if (tick.bid <= 0 || tick.ask <= 0) continue;

        tick.hour = parse_hour(tick.timestamp);
        tick.day_of_week = parse_day_of_week(tick.timestamp);

        ticks.push_back(tick);

        if (ticks.size() % 500000 == 0) {
            printf("  Loaded %zu ticks... (price: %.2f)\n", ticks.size(), tick.bid);
        }
    }

    fclose(file);
    printf("Loaded %zu ticks total\n\n", ticks.size());

    return ticks;
}

// ============================================================================
// V7/V8 STRATEGY SIMULATION WITH DETAILED TRACKING
// ============================================================================

struct StrategyConfig {
    // V7 defaults
    double spacing;
    double stop_new_at_dd;
    double partial_close_at_dd;
    double close_all_at_dd;
    int max_positions;
    int atr_short_period;
    int atr_long_period;
    double volatility_threshold;
    double tp_multiplier;
    const char* name;
};

struct SimulationResult {
    double final_balance;
    double max_drawdown_pct;
    int total_trades;
    int positions_opened;
    int forced_closes;
    std::vector<DrawdownEvent> drawdown_events;
};

SimulationResult RunSimulation(const std::vector<Tick>& ticks,
                               const StrategyConfig& config,
                               double dd_event_threshold = 10.0) {
    SimulationResult result = {};
    result.drawdown_events.reserve(100);

    const double INITIAL_BALANCE = 10000.0;
    const double CONTRACT_SIZE = 100.0;
    const double LEVERAGE = 500.0;
    const double LOT_SIZE = 0.01;

    double balance = INITIAL_BALANCE;
    double equity = INITIAL_BALANCE;
    double peak_equity = INITIAL_BALANCE;

    std::vector<Trade*> positions;
    int next_id = 1;
    bool partial_done = false;
    bool all_closed = false;

    ATR atr_short(config.atr_short_period);
    ATR atr_long(config.atr_long_period);

    // Drawdown event tracking
    DrawdownEvent* current_event = nullptr;
    bool in_dd_event = false;
    double event_start_equity = 0;

    for (size_t i = 0; i < ticks.size(); i++) {
        const Tick& tick = ticks[i];

        // Update ATR
        atr_short.Add(tick.bid);
        atr_long.Add(tick.bid);

        // Calculate equity
        equity = balance;
        for (Trade* t : positions) {
            equity += (tick.bid - t->entry_price) * t->lot_size * CONTRACT_SIZE;
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

        // Calculate current drawdown
        double dd_pct = (peak_equity > 0) ? (peak_equity - equity) / peak_equity * 100.0 : 0.0;
        double dd_amount = peak_equity - equity;

        if (dd_pct > result.max_drawdown_pct) {
            result.max_drawdown_pct = dd_pct;
        }

        // === DRAWDOWN EVENT TRACKING ===

        // Start new event when DD crosses threshold
        if (dd_pct >= dd_event_threshold && !in_dd_event) {
            in_dd_event = true;
            event_start_equity = peak_equity;

            DrawdownEvent event = {};
            event.start_tick = (int)i;
            event.peak_tick = (int)i;
            event.peak_dd_pct = dd_pct;
            event.peak_dd_amount = dd_amount;
            event.price_at_start = tick.bid;
            event.price_at_peak = tick.bid;
            event.positions_at_peak = (int)positions.size();
            event.hour_at_peak = tick.hour;
            event.day_at_peak = tick.day_of_week;
            event.volatility_at_peak = atr_short.Get();
            event.volatility_ratio = (atr_long.Get() > 0) ? atr_short.Get() / atr_long.Get() : 0;
            event.is_recovered = false;
            strncpy(event.timestamp_start, tick.timestamp, 31);
            strncpy(event.timestamp_peak, tick.timestamp, 31);

            result.drawdown_events.push_back(event);
            current_event = &result.drawdown_events.back();
        }

        // Update peak of current event
        if (in_dd_event && current_event && dd_pct > current_event->peak_dd_pct) {
            current_event->peak_tick = (int)i;
            current_event->peak_dd_pct = dd_pct;
            current_event->peak_dd_amount = dd_amount;
            current_event->price_at_peak = tick.bid;
            current_event->positions_at_peak = (int)positions.size();
            current_event->hour_at_peak = tick.hour;
            current_event->day_at_peak = tick.day_of_week;
            current_event->volatility_at_peak = atr_short.Get();
            current_event->volatility_ratio = (atr_long.Get() > 0) ? atr_short.Get() / atr_long.Get() : 0;
            strncpy(current_event->timestamp_peak, tick.timestamp, 31);
        }

        // Check for recovery (equity back to peak)
        if (in_dd_event && current_event && equity >= event_start_equity * 0.995) {  // 99.5% = recovered
            current_event->end_tick = (int)i;
            current_event->price_at_end = tick.bid;
            current_event->ticks_to_peak = current_event->peak_tick - current_event->start_tick;
            current_event->ticks_to_recovery = (int)i - current_event->start_tick;
            current_event->price_change_pct = (current_event->price_at_peak - current_event->price_at_start)
                                              / current_event->price_at_start * 100.0;
            current_event->recovery_price_change_pct = (current_event->price_at_end - current_event->price_at_peak)
                                                       / current_event->price_at_peak * 100.0;
            current_event->is_recovered = true;
            strncpy(current_event->timestamp_end, tick.timestamp, 31);

            in_dd_event = false;
            current_event = nullptr;
        }

        // === PROTECTION MECHANISMS ===

        // Close all at threshold
        if (dd_pct > config.close_all_at_dd && !all_closed && !positions.empty()) {
            for (Trade* t : positions) {
                balance += (tick.bid - t->entry_price) * t->lot_size * CONTRACT_SIZE;
                result.total_trades++;
                result.forced_closes++;
                delete t;
            }
            positions.clear();
            all_closed = true;
            equity = balance;
            peak_equity = balance;
            continue;
        }

        // Partial close
        if (dd_pct > config.partial_close_at_dd && !partial_done && positions.size() > 1) {
            std::sort(positions.begin(), positions.end(), [&](Trade* a, Trade* b) {
                return (tick.bid - a->entry_price) < (tick.bid - b->entry_price);
            });

            int to_close = std::max(1, (int)(positions.size() / 2));
            for (int j = 0; j < to_close && !positions.empty(); j++) {
                balance += (tick.bid - positions[0]->entry_price) * positions[0]->lot_size * CONTRACT_SIZE;
                result.total_trades++;
                result.forced_closes++;
                delete positions[0];
                positions.erase(positions.begin());
            }
            partial_done = true;
        }

        // === TP CHECK ===
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

        // === VOLATILITY FILTER ===
        bool volatility_ok = true;
        if (atr_short.IsReady() && atr_long.IsReady() && atr_long.Get() > 0) {
            volatility_ok = atr_short.Get() < atr_long.Get() * config.volatility_threshold;
        }

        // === OPEN NEW POSITIONS ===
        if (dd_pct < config.stop_new_at_dd && volatility_ok && (int)positions.size() < config.max_positions) {
            double lowest = 1e9, highest = 0;
            for (Trade* t : positions) {
                lowest = std::min(lowest, t->entry_price);
                highest = std::max(highest, t->entry_price);
            }

            bool should_open = positions.empty() ||
                              (lowest >= tick.ask + config.spacing) ||
                              (highest <= tick.ask - config.spacing);

            if (should_open) {
                double margin_needed = LOT_SIZE * CONTRACT_SIZE * tick.ask / LEVERAGE;
                double used_margin = 0;
                for (Trade* t : positions) {
                    used_margin += t->lot_size * CONTRACT_SIZE * t->entry_price / LEVERAGE;
                }

                if (equity - used_margin > margin_needed * 2) {
                    Trade* t = new Trade();
                    t->id = next_id++;
                    t->entry_price = tick.ask;
                    t->lot_size = LOT_SIZE;
                    t->take_profit = tick.ask + tick.spread() + (config.spacing * config.tp_multiplier);
                    t->entry_tick_index = (int)i;
                    positions.push_back(t);
                    result.positions_opened++;
                }
            }
        }
    }

    // Close remaining positions
    if (!ticks.empty()) {
        for (Trade* t : positions) {
            balance += (ticks.back().bid - t->entry_price) * t->lot_size * CONTRACT_SIZE;
            result.total_trades++;
            delete t;
        }
    }

    // Finalize any unrecovered event
    if (in_dd_event && current_event && !ticks.empty()) {
        current_event->end_tick = (int)ticks.size() - 1;
        current_event->price_at_end = ticks.back().bid;
        current_event->ticks_to_peak = current_event->peak_tick - current_event->start_tick;
        current_event->ticks_to_recovery = (int)ticks.size() - 1 - current_event->start_tick;
        current_event->price_change_pct = (current_event->price_at_peak - current_event->price_at_start)
                                          / current_event->price_at_start * 100.0;
        current_event->recovery_price_change_pct = (current_event->price_at_end - current_event->price_at_peak)
                                                   / current_event->price_at_peak * 100.0;
        strncpy(current_event->timestamp_end, ticks.back().timestamp, 31);
    }

    result.final_balance = balance;
    return result;
}

// ============================================================================
// ANALYSIS AND REPORTING
// ============================================================================

const char* GetDayName(int dow) {
    static const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    return (dow >= 0 && dow <= 6) ? days[dow] : "???";
}

PricePattern ClassifyPattern(const DrawdownEvent& event) {
    double price_change = fabs(event.price_change_pct);
    double vol_ratio = event.volatility_ratio;
    int ticks_to_peak = event.ticks_to_peak;

    // Fast price drop (happens in < 10000 ticks, > 0.5% move)
    if (ticks_to_peak < 10000 && price_change > 0.5) {
        if (event.is_recovered && event.ticks_to_recovery < ticks_to_peak * 3) {
            return PricePattern::V_REVERSAL;
        }
        return PricePattern::SHARP_DROP;
    }

    // High volatility during event
    if (vol_ratio > 1.2) {
        return PricePattern::VOLATILE_CHOP;
    }

    // Extended decline
    if (ticks_to_peak > 50000) {
        return PricePattern::GRADUAL_DECLINE;
    }

    return PricePattern::EXTENDED_RANGE;
}

const char* GetPatternName(PricePattern pattern) {
    switch (pattern) {
        case PricePattern::SHARP_DROP: return "Sharp Drop";
        case PricePattern::GRADUAL_DECLINE: return "Gradual Decline";
        case PricePattern::VOLATILE_CHOP: return "Volatile Chop";
        case PricePattern::V_REVERSAL: return "V-Reversal";
        case PricePattern::EXTENDED_RANGE: return "Extended Range";
    }
    return "Unknown";
}

void PrintDrawdownReport(const std::vector<DrawdownEvent>& events, const char* strategy_name) {
    printf("\n");
    printf("================================================================================\n");
    printf("DRAWDOWN ANALYSIS REPORT: %s\n", strategy_name);
    printf("================================================================================\n");

    if (events.empty()) {
        printf("\nNo significant drawdown events (>10%%) detected.\n");
        return;
    }

    // Sort by peak DD (worst first)
    std::vector<DrawdownEvent> sorted_events = events;
    std::sort(sorted_events.begin(), sorted_events.end(),
              [](const DrawdownEvent& a, const DrawdownEvent& b) {
                  return a.peak_dd_pct > b.peak_dd_pct;
              });

    // ========== TOP 5 WORST DRAWDOWNS ==========
    printf("\n");
    printf("=== TOP 5 WORST DRAWDOWN EVENTS ===\n");
    printf("-" "--------------------------------------------------------------------------------\n");

    int count = std::min(5, (int)sorted_events.size());
    for (int i = 0; i < count; i++) {
        const DrawdownEvent& e = sorted_events[i];
        PricePattern pattern = ClassifyPattern(e);

        printf("\n#%d: %.2f%% drawdown ($%.2f)\n", i+1, e.peak_dd_pct, e.peak_dd_amount);
        printf("    Start:    %s\n", e.timestamp_start);
        printf("    Peak:     %s\n", e.timestamp_peak);
        printf("    End:      %s%s\n", e.timestamp_end, e.is_recovered ? "" : " (NOT RECOVERED)");
        printf("    \n");
        printf("    Market Conditions at Peak:\n");
        printf("      - Price:      %.2f -> %.2f (%.2f%% change)\n",
               e.price_at_start, e.price_at_peak, e.price_change_pct);
        printf("      - Volatility: %.4f (ratio: %.2fx normal)\n",
               e.volatility_at_peak, e.volatility_ratio);
        printf("      - Positions:  %d open\n", e.positions_at_peak);
        printf("      - Time:       %02d:00 on %s\n", e.hour_at_peak, GetDayName(e.day_at_peak));
        printf("      - Pattern:    %s\n", GetPatternName(pattern));
        printf("    \n");
        printf("    Duration:\n");
        printf("      - Ticks to peak:     %d (~%.0f minutes)\n",
               e.ticks_to_peak, e.ticks_to_peak / 60.0);
        printf("      - Ticks to recovery: %d (~%.0f minutes)%s\n",
               e.ticks_to_recovery, e.ticks_to_recovery / 60.0,
               e.is_recovered ? "" : " (ongoing)");
        if (e.is_recovered) {
            printf("      - Recovery price change: %.2f%%\n", e.recovery_price_change_pct);
        }
        printf("-" "--------------------------------------------------------------------------------\n");
    }

    // ========== PATTERN ANALYSIS ==========
    printf("\n=== PATTERN ANALYSIS (All Events) ===\n\n");

    int pattern_counts[5] = {0};
    double pattern_dd_sum[5] = {0};

    for (const auto& e : sorted_events) {
        PricePattern p = ClassifyPattern(e);
        pattern_counts[(int)p]++;
        pattern_dd_sum[(int)p] += e.peak_dd_pct;
    }

    printf("Pattern               Count   Avg DD%%   Description\n");
    printf("------------------------------------------------------------\n");
    for (int i = 0; i < 5; i++) {
        if (pattern_counts[i] > 0) {
            double avg_dd = pattern_dd_sum[i] / pattern_counts[i];
            const char* desc = "";
            switch ((PricePattern)i) {
                case PricePattern::SHARP_DROP: desc = "Rapid price decline"; break;
                case PricePattern::GRADUAL_DECLINE: desc = "Slow persistent trend"; break;
                case PricePattern::VOLATILE_CHOP: desc = "High vol, no direction"; break;
                case PricePattern::V_REVERSAL: desc = "Quick drop and recovery"; break;
                case PricePattern::EXTENDED_RANGE: desc = "Stuck in losing range"; break;
            }
            printf("%-20s  %3d     %5.1f%%   %s\n",
                   GetPatternName((PricePattern)i), pattern_counts[i], avg_dd, desc);
        }
    }

    // ========== TIME OF DAY ANALYSIS ==========
    printf("\n=== TIME OF DAY ANALYSIS ===\n\n");

    int hour_counts[24] = {0};
    double hour_dd_sum[24] = {0};

    for (const auto& e : sorted_events) {
        hour_counts[e.hour_at_peak]++;
        hour_dd_sum[e.hour_at_peak] += e.peak_dd_pct;
    }

    printf("Hour (UTC)  Events  Avg DD%%   Risk Level\n");
    printf("------------------------------------------\n");

    // Find max for risk assessment
    int max_count = 0;
    for (int h = 0; h < 24; h++) {
        if (hour_counts[h] > max_count) max_count = hour_counts[h];
    }

    for (int h = 0; h < 24; h++) {
        if (hour_counts[h] > 0) {
            double avg_dd = hour_dd_sum[h] / hour_counts[h];
            const char* risk = "LOW";
            if (hour_counts[h] >= max_count * 0.7 || avg_dd > 15.0) risk = "HIGH";
            else if (hour_counts[h] >= max_count * 0.4 || avg_dd > 12.0) risk = "MEDIUM";

            printf("  %02d:00      %3d     %5.1f%%    %s\n", h, hour_counts[h], avg_dd, risk);
        }
    }

    // ========== DAY OF WEEK ANALYSIS ==========
    printf("\n=== DAY OF WEEK ANALYSIS ===\n\n");

    int dow_counts[7] = {0};
    double dow_dd_sum[7] = {0};

    for (const auto& e : sorted_events) {
        dow_counts[e.day_at_peak]++;
        dow_dd_sum[e.day_at_peak] += e.peak_dd_pct;
    }

    printf("Day        Events  Avg DD%%\n");
    printf("---------------------------\n");
    for (int d = 0; d < 7; d++) {
        if (dow_counts[d] > 0) {
            double avg_dd = dow_dd_sum[d] / dow_counts[d];
            printf("%-9s   %3d     %5.1f%%\n", GetDayName(d), dow_counts[d], avg_dd);
        }
    }

    // ========== POSITION COUNT ANALYSIS ==========
    printf("\n=== POSITION COUNT AT PEAK DD ===\n\n");

    int pos_buckets[5] = {0};  // 0-5, 6-10, 11-15, 16-20, 20+
    double pos_dd_sum[5] = {0};

    for (const auto& e : sorted_events) {
        int bucket = std::min(4, e.positions_at_peak / 5);
        pos_buckets[bucket]++;
        pos_dd_sum[bucket] += e.peak_dd_pct;
    }

    printf("Positions   Events  Avg DD%%\n");
    printf("----------------------------\n");
    const char* pos_labels[] = {"1-5", "6-10", "11-15", "16-20", "20+"};
    for (int b = 0; b < 5; b++) {
        if (pos_buckets[b] > 0) {
            double avg_dd = pos_dd_sum[b] / pos_buckets[b];
            printf("%-10s   %3d     %5.1f%%\n", pos_labels[b], pos_buckets[b], avg_dd);
        }
    }

    // ========== VOLATILITY ANALYSIS ==========
    printf("\n=== VOLATILITY RATIO AT PEAK DD ===\n\n");

    int vol_buckets[5] = {0};  // <0.5, 0.5-0.8, 0.8-1.0, 1.0-1.5, >1.5
    double vol_dd_sum[5] = {0};

    for (const auto& e : sorted_events) {
        int bucket;
        if (e.volatility_ratio < 0.5) bucket = 0;
        else if (e.volatility_ratio < 0.8) bucket = 1;
        else if (e.volatility_ratio < 1.0) bucket = 2;
        else if (e.volatility_ratio < 1.5) bucket = 3;
        else bucket = 4;

        vol_buckets[bucket]++;
        vol_dd_sum[bucket] += e.peak_dd_pct;
    }

    printf("Vol Ratio     Events  Avg DD%%   Note\n");
    printf("----------------------------------------------\n");
    const char* vol_labels[] = {"< 0.5x", "0.5-0.8x", "0.8-1.0x", "1.0-1.5x", "> 1.5x"};
    const char* vol_notes[] = {"Very calm", "Below avg", "Normal", "Above avg", "High vol"};
    for (int b = 0; b < 5; b++) {
        if (vol_buckets[b] > 0) {
            double avg_dd = vol_dd_sum[b] / vol_buckets[b];
            printf("%-12s   %3d     %5.1f%%    %s\n", vol_labels[b], vol_buckets[b], avg_dd, vol_notes[b]);
        }
    }

    // ========== RECOVERY ANALYSIS ==========
    printf("\n=== RECOVERY ANALYSIS ===\n\n");

    int recovered = 0, not_recovered = 0;
    double avg_recovery_ticks = 0;
    double total_recovery_ticks = 0;

    for (const auto& e : sorted_events) {
        if (e.is_recovered) {
            recovered++;
            total_recovery_ticks += e.ticks_to_recovery;
        } else {
            not_recovered++;
        }
    }

    if (recovered > 0) {
        avg_recovery_ticks = total_recovery_ticks / recovered;
    }

    printf("Recovered events:     %d (%.1f%%)\n", recovered,
           (sorted_events.size() > 0 ? recovered * 100.0 / sorted_events.size() : 0));
    printf("Unrecovered events:   %d (%.1f%%)\n", not_recovered,
           (sorted_events.size() > 0 ? not_recovered * 100.0 / sorted_events.size() : 0));
    printf("Avg recovery time:    %.0f ticks (~%.1f minutes)\n",
           avg_recovery_ticks, avg_recovery_ticks / 60.0);
}

void PrintImprovementSuggestions(const std::vector<DrawdownEvent>& events) {
    printf("\n");
    printf("================================================================================\n");
    printf("ACTIONABLE IMPROVEMENT SUGGESTIONS\n");
    printf("================================================================================\n\n");

    if (events.empty()) {
        printf("Insufficient data for suggestions.\n");
        return;
    }

    // Analyze patterns
    int sharp_drops = 0, gradual = 0, volatile_chop = 0, high_positions = 0;
    int high_vol_events = 0, us_session_events = 0, london_session_events = 0;

    for (const auto& e : events) {
        PricePattern p = ClassifyPattern(e);
        if (p == PricePattern::SHARP_DROP) sharp_drops++;
        if (p == PricePattern::GRADUAL_DECLINE) gradual++;
        if (p == PricePattern::VOLATILE_CHOP) volatile_chop++;
        if (e.positions_at_peak > 15) high_positions++;
        if (e.volatility_ratio > 1.0) high_vol_events++;
        if (e.hour_at_peak >= 13 && e.hour_at_peak <= 17) us_session_events++;
        if (e.hour_at_peak >= 7 && e.hour_at_peak <= 10) london_session_events++;
    }

    int total = (int)events.size();

    printf("Based on analysis of %d drawdown events:\n\n", total);

    // 1. Sharp drop protection
    if (sharp_drops > total * 0.3) {
        printf("1. SHARP DROP PROTECTION (%.0f%% of events)\n", sharp_drops * 100.0 / total);
        printf("   Problem: Strategy gets caught in rapid price declines\n");
        printf("   Solutions:\n");
        printf("   - Add price velocity detection (close if price drops > X%% in Y ticks)\n");
        printf("   - Reduce max_positions from 20 to 10-12\n");
        printf("   - Tighten close_all_at_dd from 25%% to 15%%\n");
        printf("   - Consider adding trailing stop-loss on total equity\n");
        printf("\n");
    }

    // 2. Volatility filter improvement
    if (high_vol_events > total * 0.25) {
        printf("2. VOLATILITY FILTER TOO WEAK (%.0f%% events during high vol)\n",
               high_vol_events * 100.0 / total);
        printf("   Problem: Strategy still trading when volatility spikes\n");
        printf("   Solutions:\n");
        printf("   - Reduce volatility_threshold from 0.8 to 0.6\n");
        printf("   - Shorten atr_short_period from 100 to 50 for faster response\n");
        printf("   - Add immediate exit when volatility_ratio exceeds 1.5\n");
        printf("   - Consider closing positions when volatility suddenly spikes\n");
        printf("\n");
    }

    // 3. Position accumulation
    if (high_positions > total * 0.4) {
        printf("3. EXCESSIVE POSITION ACCUMULATION (%.0f%% events with >15 positions)\n",
               high_positions * 100.0 / total);
        printf("   Problem: Too many positions amplify drawdowns\n");
        printf("   Solutions:\n");
        printf("   - Reduce max_positions from 20 to 10-15\n");
        printf("   - Stop opening new positions earlier (stop_new_at_dd: 5%% -> 3%%)\n");
        printf("   - Add position scaling: reduce lot size as position count increases\n");
        printf("   - Consider grid compression: reduce spacing when many positions open\n");
        printf("\n");
    }

    // 4. Session-based protection
    if (us_session_events > total * 0.3 || london_session_events > total * 0.3) {
        printf("4. SESSION TIMING ISSUES (US: %.0f%%, London: %.0f%% of events)\n",
               us_session_events * 100.0 / total, london_session_events * 100.0 / total);
        printf("   Problem: Major drawdowns during high-activity sessions\n");
        printf("   Solutions:\n");
        printf("   - Enable session filter (avoid 13:00-17:00 UTC for US open)\n");
        printf("   - Avoid 07:00-10:00 UTC for London open\n");
        printf("   - Reduce position limits during these hours\n");
        printf("   - Only trade Asian session (20:00-06:00 UTC) for calmer markets\n");
        printf("\n");
    }

    // 5. Gradual trend protection
    if (gradual > total * 0.3) {
        printf("5. PROLONGED TREND EXPOSURE (%.0f%% of events)\n", gradual * 100.0 / total);
        printf("   Problem: Grid gets stuck in extended downtrends\n");
        printf("   Solutions:\n");
        printf("   - Add trend detection (SMA crossover or ADX)\n");
        printf("   - Close positions if price stays below entry average for X hours\n");
        printf("   - Implement time-based exits (close if position > 4 hours old)\n");
        printf("   - Add partial close at 3%% DD instead of waiting for 8%%\n");
        printf("\n");
    }

    // 6. Volatile chop protection
    if (volatile_chop > total * 0.25) {
        printf("6. CHOPPY MARKET VULNERABILITY (%.0f%% of events)\n", volatile_chop * 100.0 / total);
        printf("   Problem: Strategy struggles with directionless volatility\n");
        printf("   Solutions:\n");
        printf("   - Increase spacing from 1.0 to 1.5-2.0 in high vol\n");
        printf("   - Add Bollinger Band width check (avoid when bands expanding)\n");
        printf("   - Use ATR-based dynamic spacing\n");
        printf("   - Consider pausing for 30min after any >5%% price move\n");
        printf("\n");
    }

    // Summary recommendations
    printf("================================================================================\n");
    printf("PRIORITY RECOMMENDATIONS\n");
    printf("================================================================================\n\n");

    printf("Immediate (High Impact):\n");
    printf("  1. Reduce max_positions: 20 -> 12-15\n");
    printf("  2. Tighten DD thresholds: stop_new 5%% -> 3%%, partial 8%% -> 5%%\n");
    printf("  3. Stricter volatility filter: threshold 0.8 -> 0.6\n");
    printf("\n");

    printf("Medium Term:\n");
    printf("  4. Add session filter to avoid US/London opens\n");
    printf("  5. Implement price velocity detection for flash crash protection\n");
    printf("  6. Add time-based position exits (close stale positions)\n");
    printf("\n");

    printf("Advanced:\n");
    printf("  7. Dynamic spacing based on current volatility\n");
    printf("  8. Position scaling (smaller lots as position count increases)\n");
    printf("  9. Trend filter to avoid trading against strong directional moves\n");
    printf("\n");
}

// ============================================================================
// MAIN
// ============================================================================

int main() {
    printf("================================================================================\n");
    printf("V7/V8 DRAWDOWN ANALYSIS\n");
    printf("Analyzing what market conditions cause the worst drawdowns\n");
    printf("================================================================================\n\n");

    // Try multiple file paths
    const char* file_paths[] = {
        "Broker/XAUUSD_TICKS_2025.csv",
        "../Broker/XAUUSD_TICKS_2025.csv",
        "validation/Broker/XAUUSD_TICKS_2025.csv",
        "Grid/XAUUSD_TICKS_2025.csv",
        "../Grid/XAUUSD_TICKS_2025.csv"
    };

    std::vector<Tick> ticks;
    const char* used_path = nullptr;

    for (const char* path : file_paths) {
        FILE* test = fopen(path, "r");
        if (test) {
            fclose(test);
            used_path = path;
            printf("Found tick file: %s\n", path);
            break;
        }
    }

    if (!used_path) {
        printf("ERROR: Could not find tick data file.\n");
        printf("Tried paths:\n");
        for (const char* path : file_paths) {
            printf("  - %s\n", path);
        }
        return 1;
    }

    // Load 10M ticks for comprehensive analysis
    ticks = LoadTicks(used_path, 10000000);

    if (ticks.empty()) {
        printf("ERROR: No ticks loaded\n");
        return 1;
    }

    // Report data characteristics
    printf("Data Summary:\n");
    printf("  First tick: %s @ %.2f\n", ticks.front().timestamp, ticks.front().bid);
    printf("  Last tick:  %s @ %.2f\n", ticks.back().timestamp, ticks.back().bid);
    double price_change = (ticks.back().bid - ticks.front().bid) / ticks.front().bid * 100.0;
    printf("  Price change: %.2f%%\n", price_change);
    printf("\n");

    // Define strategy configs
    StrategyConfig v7_config = {
        .spacing = 1.0,
        .stop_new_at_dd = 5.0,
        .partial_close_at_dd = 8.0,
        .close_all_at_dd = 25.0,
        .max_positions = 20,
        .atr_short_period = 100,
        .atr_long_period = 500,
        .volatility_threshold = 0.8,
        .tp_multiplier = 1.0,
        .name = "V7 (Volatility Filter)"
    };

    StrategyConfig v8_config = {
        .spacing = 1.0,
        .stop_new_at_dd = 3.0,
        .partial_close_at_dd = 5.0,
        .close_all_at_dd = 15.0,
        .max_positions = 15,
        .atr_short_period = 50,
        .atr_long_period = 1000,
        .volatility_threshold = 0.6,
        .tp_multiplier = 2.0,
        .name = "V8 (Tighter Risk)"
    };

    // Use lower threshold (5%) to capture more drawdown events for analysis
    double dd_threshold = 5.0;  // Capture events >5% DD for better analysis

    // Run V7 analysis
    printf("Running V7 simulation (tracking DD >%.0f%%)...\n", dd_threshold);
    SimulationResult v7_result = RunSimulation(ticks, v7_config, dd_threshold);

    printf("\nV7 Results:\n");
    printf("  Final Balance: $%.2f (%.2f%% return)\n",
           v7_result.final_balance, (v7_result.final_balance - 10000.0) / 100.0);
    printf("  Max Drawdown:  %.2f%%\n", v7_result.max_drawdown_pct);
    printf("  Total Trades:  %d\n", v7_result.total_trades);
    printf("  Forced Closes: %d\n", v7_result.forced_closes);
    printf("  DD Events >%.0f%%: %zu\n", dd_threshold, v7_result.drawdown_events.size());

    // Run V8 analysis
    printf("\nRunning V8 simulation (tracking DD >%.0f%%)...\n", dd_threshold);
    SimulationResult v8_result = RunSimulation(ticks, v8_config, dd_threshold);

    printf("\nV8 Results:\n");
    printf("  Final Balance: $%.2f (%.2f%% return)\n",
           v8_result.final_balance, (v8_result.final_balance - 10000.0) / 100.0);
    printf("  Max Drawdown:  %.2f%%\n", v8_result.max_drawdown_pct);
    printf("  Total Trades:  %d\n", v8_result.total_trades);
    printf("  Forced Closes: %d\n", v8_result.forced_closes);
    printf("  DD Events >%.0f%%: %zu\n", dd_threshold, v8_result.drawdown_events.size());

    // Print detailed reports
    PrintDrawdownReport(v7_result.drawdown_events, v7_config.name);
    PrintDrawdownReport(v8_result.drawdown_events, v8_config.name);

    // Combine events for improvement suggestions
    std::vector<DrawdownEvent> all_events;
    all_events.insert(all_events.end(), v7_result.drawdown_events.begin(), v7_result.drawdown_events.end());
    all_events.insert(all_events.end(), v8_result.drawdown_events.begin(), v8_result.drawdown_events.end());

    PrintImprovementSuggestions(all_events);

    // V7 vs V8 comparison
    printf("================================================================================\n");
    printf("V7 vs V8 COMPARISON\n");
    printf("================================================================================\n\n");

    printf("Metric                    V7              V8              Improvement\n");
    printf("------------------------------------------------------------------------\n");
    printf("Final Balance         $%8.2f       $%8.2f       %+.1f%%\n",
           v7_result.final_balance, v8_result.final_balance,
           (v8_result.final_balance - v7_result.final_balance) / v7_result.final_balance * 100.0);
    printf("Max Drawdown           %6.2f%%         %6.2f%%         %+.1f%%\n",
           v7_result.max_drawdown_pct, v8_result.max_drawdown_pct,
           v7_result.max_drawdown_pct - v8_result.max_drawdown_pct);
    printf("DD Events >%.0f%%           %3zu              %3zu             %+d\n",
           dd_threshold, v7_result.drawdown_events.size(), v8_result.drawdown_events.size(),
           (int)v7_result.drawdown_events.size() - (int)v8_result.drawdown_events.size());
    printf("Forced Closes             %3d              %3d             %+d\n",
           v7_result.forced_closes, v8_result.forced_closes,
           v7_result.forced_closes - v8_result.forced_closes);
    printf("\n");

    if (v8_result.max_drawdown_pct < v7_result.max_drawdown_pct) {
        printf("V8's tighter risk management reduces maximum drawdown by %.1f%%\n",
               v7_result.max_drawdown_pct - v8_result.max_drawdown_pct);
    }

    if (v8_result.drawdown_events.size() < v7_result.drawdown_events.size()) {
        printf("V8 has %zu fewer significant drawdown events\n",
               v7_result.drawdown_events.size() - v8_result.drawdown_events.size());
    }

    printf("\n================================================================================\n");
    printf("Analysis complete.\n");
    printf("================================================================================\n");

    return 0;
}
