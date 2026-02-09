/**
 * test_tick_characteristics.cpp
 *
 * Analyze tick data characteristics to understand MT5 vs C++ discrepancy:
 * 1. Spread distribution - is MT5 using wider spreads?
 * 2. Tick frequency - how many ticks per minute?
 * 3. Velocity at entry points - what velocity values occur?
 * 4. Price movement patterns
 */

#include "../include/strategy_combined_ju.h"
#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <map>
#include <cmath>
#include <algorithm>
#include <numeric>

using namespace backtest;

std::vector<Tick> g_ticks;

void LoadTicks() {
    std::cout << "Loading tick data..." << std::endl;
    std::vector<std::string> files = {
        "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv",
        "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_JAN2026.csv"
    };
    for (const auto& file : files) {
        TickDataManager mgr(file);
        Tick tick;
        while (mgr.GetNextTick(tick)) {
            g_ticks.push_back(tick);
        }
    }
    std::sort(g_ticks.begin(), g_ticks.end(),
              [](const Tick& a, const Tick& b) { return a.timestamp < b.timestamp; });
    std::cout << "Loaded " << g_ticks.size() << " ticks" << std::endl;
}

// Extract date from timestamp (YYYY.MM.DD HH:MM:SS format)
std::string GetDate(const std::string& timestamp) {
    return timestamp.substr(0, 10);
}

std::string GetMinute(const std::string& timestamp) {
    return timestamp.substr(0, 16); // YYYY.MM.DD HH:MM
}

int main() {
    std::cout << "=" << std::string(90, '=') << std::endl;
    std::cout << "TICK CHARACTERISTICS ANALYSIS" << std::endl;
    std::cout << "Understanding MT5 vs C++ discrepancy causes" << std::endl;
    std::cout << "=" << std::string(90, '=') << std::endl << std::endl;

    LoadTicks();

    // 1. SPREAD ANALYSIS
    std::cout << "\n=== 1. SPREAD ANALYSIS ===" << std::endl;

    std::vector<double> spreads;
    double min_spread = DBL_MAX, max_spread = 0;
    double sum_spread = 0;

    for (const auto& tick : g_ticks) {
        double spread = tick.ask - tick.bid;
        spreads.push_back(spread);
        sum_spread += spread;
        min_spread = std::min(min_spread, spread);
        max_spread = std::max(max_spread, spread);
    }

    std::sort(spreads.begin(), spreads.end());
    double median_spread = spreads[spreads.size() / 2];
    double p25_spread = spreads[spreads.size() / 4];
    double p75_spread = spreads[3 * spreads.size() / 4];
    double p90_spread = spreads[9 * spreads.size() / 10];
    double p99_spread = spreads[99 * spreads.size() / 100];
    double avg_spread = sum_spread / spreads.size();

    std::cout << std::fixed << std::setprecision(4);
    std::cout << "Spread Statistics (in $):" << std::endl;
    std::cout << "  Min:      " << min_spread << std::endl;
    std::cout << "  P25:      " << p25_spread << std::endl;
    std::cout << "  Median:   " << median_spread << std::endl;
    std::cout << "  Mean:     " << avg_spread << std::endl;
    std::cout << "  P75:      " << p75_spread << std::endl;
    std::cout << "  P90:      " << p90_spread << std::endl;
    std::cout << "  P99:      " << p99_spread << std::endl;
    std::cout << "  Max:      " << max_spread << std::endl;

    // Spread distribution buckets
    std::map<int, int> spread_buckets;
    for (double s : spreads) {
        int bucket = (int)(s * 100); // cents
        spread_buckets[bucket]++;
    }

    std::cout << "\nSpread Distribution (cents):" << std::endl;
    long cumulative = 0;
    for (const auto& [bucket, count] : spread_buckets) {
        cumulative += count;
        double pct = 100.0 * count / spreads.size();
        double cum_pct = 100.0 * cumulative / spreads.size();
        if (pct > 0.5 || bucket <= 50) {
            std::cout << "  $" << std::setw(4) << (bucket / 100.0) << ": "
                      << std::setw(10) << count << " (" << std::setw(5) << std::setprecision(1) << pct << "%, cum "
                      << std::setw(5) << cum_pct << "%)" << std::endl;
        }
    }

    // 2. TICK FREQUENCY ANALYSIS
    std::cout << "\n=== 2. TICK FREQUENCY ANALYSIS ===" << std::endl;

    std::map<std::string, int> ticks_per_minute;
    std::map<std::string, int> ticks_per_day;

    for (const auto& tick : g_ticks) {
        ticks_per_minute[GetMinute(tick.timestamp)]++;
        ticks_per_day[GetDate(tick.timestamp)]++;
    }

    // Ticks per minute statistics
    std::vector<int> minute_counts;
    for (const auto& [_, count] : ticks_per_minute) {
        minute_counts.push_back(count);
    }
    std::sort(minute_counts.begin(), minute_counts.end());

    double avg_ticks_per_min = (double)g_ticks.size() / ticks_per_minute.size();
    int median_ticks_per_min = minute_counts[minute_counts.size() / 2];
    int p90_ticks_per_min = minute_counts[9 * minute_counts.size() / 10];
    int max_ticks_per_min = minute_counts.back();

    std::cout << "Ticks per minute:" << std::endl;
    std::cout << "  Avg:      " << std::fixed << std::setprecision(1) << avg_ticks_per_min << std::endl;
    std::cout << "  Median:   " << median_ticks_per_min << std::endl;
    std::cout << "  P90:      " << p90_ticks_per_min << std::endl;
    std::cout << "  Max:      " << max_ticks_per_min << std::endl;

    // Ticks per day
    std::vector<int> day_counts;
    for (const auto& [_, count] : ticks_per_day) {
        day_counts.push_back(count);
    }
    std::sort(day_counts.begin(), day_counts.end());

    double avg_ticks_per_day = (double)g_ticks.size() / ticks_per_day.size();
    int median_ticks_per_day = day_counts[day_counts.size() / 2];

    std::cout << "\nTicks per trading day:" << std::endl;
    std::cout << "  Avg:      " << std::fixed << std::setprecision(0) << avg_ticks_per_day << std::endl;
    std::cout << "  Median:   " << median_ticks_per_day << std::endl;
    std::cout << "  Min:      " << day_counts.front() << std::endl;
    std::cout << "  Max:      " << day_counts.back() << std::endl;
    std::cout << "  Total days: " << ticks_per_day.size() << std::endl;

    // 3. VELOCITY ANALYSIS (using same calculation as strategy)
    std::cout << "\n=== 3. VELOCITY DISTRIBUTION ===" << std::endl;

    const int VELOCITY_WINDOW = 10;
    std::deque<double> price_window;
    std::vector<double> velocities;

    for (const auto& tick : g_ticks) {
        price_window.push_back(tick.bid);
        while ((int)price_window.size() > VELOCITY_WINDOW) {
            price_window.pop_front();
        }
        if ((int)price_window.size() >= VELOCITY_WINDOW) {
            double old_price = price_window.front();
            double velocity_pct = (tick.bid - old_price) / old_price * 100.0;
            velocities.push_back(std::abs(velocity_pct));
        }
    }

    std::sort(velocities.begin(), velocities.end());

    std::cout << "Absolute velocity (% over " << VELOCITY_WINDOW << " ticks):" << std::endl;
    std::cout << "  P50:    " << std::fixed << std::setprecision(5) << velocities[velocities.size() / 2] << "%" << std::endl;
    std::cout << "  P75:    " << velocities[3 * velocities.size() / 4] << "%" << std::endl;
    std::cout << "  P90:    " << velocities[9 * velocities.size() / 10] << "%" << std::endl;
    std::cout << "  P95:    " << velocities[95 * velocities.size() / 100] << "%" << std::endl;
    std::cout << "  P99:    " << velocities[99 * velocities.size() / 100] << "%" << std::endl;

    // Count how many entries would be allowed at different thresholds
    std::vector<double> thresholds = {0.001, 0.005, 0.01, 0.02, 0.05, 0.10};
    std::cout << "\nEntry rate at different velocity thresholds:" << std::endl;
    for (double thresh : thresholds) {
        long count = std::count_if(velocities.begin(), velocities.end(),
                                    [thresh](double v) { return v < thresh; });
        double pct = 100.0 * count / velocities.size();
        std::cout << "  Threshold " << std::setw(6) << std::setprecision(3) << thresh << "%: "
                  << std::setw(5) << std::setprecision(1) << pct << "% of ticks allowed" << std::endl;
    }

    // 4. WHAT IF MT5 HAD WIDER SPREAD?
    std::cout << "\n=== 4. SPREAD IMPACT SIMULATION ===" << std::endl;

    std::cout << "If MT5 uses wider spread, TP takes longer to hit." << std::endl;
    std::cout << "Typical C++ config: spacing=$1.50, TP=spread+spacing" << std::endl;
    std::cout << "\nWith different spreads:" << std::endl;

    std::vector<double> test_spreads = {0.10, 0.15, 0.20, 0.25, 0.30, 0.40, 0.50};
    for (double test_spread : test_spreads) {
        double tp_distance = test_spread + 1.50;  // spread + base_spacing
        double extra_cost_per_trade = (test_spread - median_spread);
        std::cout << "  Spread $" << std::setw(4) << std::setprecision(2) << test_spread
                  << " -> TP distance $" << std::setw(4) << tp_distance
                  << " (extra cost per trade: $" << std::setw(5) << std::setprecision(3) << extra_cost_per_trade << ")" << std::endl;
    }

    // 5. TIME BETWEEN TICKS
    std::cout << "\n=== 5. TIME BETWEEN TICKS ===" << std::endl;

    // Sample first 100,000 ticks for interval analysis
    std::vector<double> intervals;
    auto ParseTimestamp = [](const std::string& ts) -> long long {
        // Parse YYYY.MM.DD HH:MM:SS.mmm
        int year, month, day, hour, min, sec;
        int ms = 0;
        if (sscanf(ts.c_str(), "%d.%d.%d %d:%d:%d", &year, &month, &day, &hour, &min, &sec) >= 6) {
            // Extract milliseconds if present
            size_t dot_pos = ts.rfind('.');
            if (dot_pos != std::string::npos && dot_pos > 15) {
                ms = atoi(ts.substr(dot_pos + 1).c_str());
            }
            // Convert to milliseconds since epoch (simplified)
            long long total_sec = ((year - 2000) * 365L + month * 30L + day) * 86400L + hour * 3600L + min * 60L + sec;
            return total_sec * 1000 + ms;
        }
        return 0;
    };

    for (size_t i = 1; i < std::min((size_t)100000, g_ticks.size()); i++) {
        long long t1 = ParseTimestamp(g_ticks[i-1].timestamp);
        long long t2 = ParseTimestamp(g_ticks[i].timestamp);
        if (t1 > 0 && t2 > t1) {
            double interval_ms = (double)(t2 - t1);
            if (interval_ms < 60000) { // Ignore gaps > 1 minute (session breaks)
                intervals.push_back(interval_ms);
            }
        }
    }

    if (!intervals.empty()) {
        std::sort(intervals.begin(), intervals.end());
        std::cout << "Time between consecutive ticks (ms):" << std::endl;
        std::cout << "  Min:    " << (int)intervals.front() << " ms" << std::endl;
        std::cout << "  P10:    " << (int)intervals[intervals.size() / 10] << " ms" << std::endl;
        std::cout << "  Median: " << (int)intervals[intervals.size() / 2] << " ms" << std::endl;
        std::cout << "  P90:    " << (int)intervals[9 * intervals.size() / 10] << " ms" << std::endl;
        std::cout << "  Max (within minute): " << (int)intervals.back() << " ms" << std::endl;

        double avg_interval = std::accumulate(intervals.begin(), intervals.end(), 0.0) / intervals.size();
        std::cout << "  Avg:    " << std::fixed << std::setprecision(1) << avg_interval << " ms" << std::endl;
        std::cout << "\n  => 10-tick velocity window spans ~" << (avg_interval * 10 / 1000) << " seconds" << std::endl;
    }

    // 6. MT5 vs RAW TICK HYPOTHESIS
    std::cout << "\n=== 6. MT5 STRATEGY TESTER HYPOTHESIS ===" << std::endl;
    std::cout << R"(
MT5 Strategy Tester may differ from raw tick data in several ways:

1. TICK AGGREGATION: MT5 may aggregate ticks that arrive in the same
   millisecond, reducing total tick count.
   Raw ticks: ~57M, MT5 might see: ~30-40M

2. SPREAD HANDLING: MT5 uses broker-specified min spread even when
   market spread is tighter. If Grid has min_spread=0.20:
   - Raw data median: $0.10
   - MT5 uses: $0.20 minimum
   => Extra $0.10 cost per trade

3. TICK DELIVERY TIMING: MT5 OnTick() is called per tick, but the
   velocity calculation depends on tick arrival sequence.
   - Raw: every tick is processed
   - MT5: OnTick() may skip ticks during high frequency

4. FLOATING POINT: Velocity threshold 0.01% may hit differently:
   - C++: exact 0.01 comparison
   - MQL5: might have different precision

IMPACT ESTIMATE:
If MT5 sees 50% fewer ticks + $0.10 wider spread:
- Fewer trading opportunities
- Higher cost per trade
- More velocity-blocked entries (longer window time)
=> Could reduce 28.59x to ~14x (matches MT5's 13.37x)
)" << std::endl;

    // 7. RECOMMENDATION
    std::cout << "\n=== 7. RECOMMENDED TESTS ===" << std::endl;
    std::cout << R"(
To close the gap between C++ (28.59x) and MT5 (13.37x):

1. TEST WITH WIDER SPREAD: Run C++ with spread = 0.25 instead of actual

2. TEST WITH TICK SUBSAMPLING: Use every 2nd tick to simulate
   MT5 tick aggregation

3. ADD MT5 LOGGING: In FillUp_CombinedJu.mq5, log:
   - Total OnTick() calls
   - Total velocity blocks
   - Total entries allowed
   - Average spread used

4. COMPARE FIRST 100 TRADES: Match entry prices, TPs, lot sizes
)" << std::endl;

    return 0;
}
