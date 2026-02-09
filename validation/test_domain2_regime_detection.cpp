/**
 * Domain 2: Regime Detection
 *
 * Critical for survival - detect trending vs oscillating regimes
 * Goal: Identify regime shifts BEFORE major losses occur
 */

#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <deque>
#include <cmath>
#include <algorithm>
#include <cfloat>
#include <map>

using namespace backtest;

enum class MarketRegime {
    OSCILLATING,    // Normal - good for grid trading
    TRENDING_UP,    // Sustained upward movement
    TRENDING_DOWN,  // Sustained downward movement - DANGER
    HIGH_VOLATILITY,// Large swings in both directions
    LOW_VOLATILITY  // Tight range, few opportunities
};

struct RegimeStats {
    MarketRegime regime;
    long start_tick;
    long end_tick;
    double start_price;
    double end_price;
    double max_dd_pct;     // Max drawdown during this regime
    double price_change_pct;
};

class RegimeDetector {
public:
    void Analyze(const std::string& file_path) {
        std::cout << "\n" << std::string(70, '=') << std::endl;
        std::cout << "REGIME DETECTION ANALYSIS" << std::endl;
        std::cout << std::string(70, '=') << std::endl;

        TickDataConfig config;
        config.file_path = file_path;
        config.format = TickDataFormat::MT5_CSV;
        config.load_all_into_memory = false;

        TickDataManager manager(config);
        // Manager initializes automatically in constructor

        // Parameters for regime detection
        const int short_window = 1000;     // ~5 seconds
        const int medium_window = 12000;   // ~1 minute
        const int long_window = 720000;    // ~1 hour

        std::deque<double> short_prices;
        std::deque<double> medium_prices;
        std::deque<double> long_prices;

        // Tracking variables
        long tick_count = 0;
        double peak_price = 0;
        double trough_price = DBL_MAX;

        // Regime tracking
        MarketRegime current_regime = MarketRegime::OSCILLATING;
        long regime_start_tick = 0;
        double regime_start_price = 0;
        std::vector<RegimeStats> regimes;

        // Metrics for each regime type
        std::map<MarketRegime, int> regime_counts;
        std::map<MarketRegime, long> regime_durations;
        std::map<MarketRegime, double> regime_returns;

        // Direction tracking for consecutive moves
        int consecutive_down = 0;
        int consecutive_up = 0;
        double last_hourly_price = 0;
        int hourly_tick_count = 0;

        // Danger zone tracking
        std::vector<std::pair<long, double>> danger_events;  // tick, drawdown%

        Tick tick;
        while (manager.GetNextTick(tick)) {
            tick_count++;
            double mid = (tick.bid + tick.ask) / 2.0;

            // Update price windows
            short_prices.push_back(mid);
            if (short_prices.size() > short_window) short_prices.pop_front();

            medium_prices.push_back(mid);
            if (medium_prices.size() > medium_window) medium_prices.pop_front();

            long_prices.push_back(mid);
            if (long_prices.size() > long_window) long_prices.pop_front();

            // Track peak/trough
            if (mid > peak_price) peak_price = mid;
            if (mid < trough_price) trough_price = mid;

            // Calculate drawdown from peak
            double dd_from_peak = (peak_price - mid) / peak_price * 100.0;

            // Track danger events (>5% drawdown)
            if (dd_from_peak > 5.0 && (danger_events.empty() ||
                tick_count - danger_events.back().first > 720000)) {
                danger_events.push_back({tick_count, dd_from_peak});
            }

            // Hourly direction tracking
            hourly_tick_count++;
            if (hourly_tick_count >= 720000) {
                if (last_hourly_price > 0) {
                    double hourly_change = (mid - last_hourly_price) / last_hourly_price * 100.0;
                    if (hourly_change < -0.5) {
                        consecutive_down++;
                        consecutive_up = 0;
                    } else if (hourly_change > 0.5) {
                        consecutive_up++;
                        consecutive_down = 0;
                    } else {
                        consecutive_down = 0;
                        consecutive_up = 0;
                    }
                }
                last_hourly_price = mid;
                hourly_tick_count = 0;
            }

            // Skip until we have enough data
            if (long_prices.size() < long_window) continue;

            // Calculate metrics for regime detection
            double short_high = *std::max_element(short_prices.begin(), short_prices.end());
            double short_low = *std::min_element(short_prices.begin(), short_prices.end());
            double short_range = short_high - short_low;

            double long_high = *std::max_element(long_prices.begin(), long_prices.end());
            double long_low = *std::min_element(long_prices.begin(), long_prices.end());
            double long_range = long_high - long_low;

            double long_start = long_prices.front();
            double long_end = long_prices.back();
            double directional_move = (long_end - long_start) / long_start * 100.0;

            // Regime detection logic
            MarketRegime detected_regime = MarketRegime::OSCILLATING;

            // High volatility: large range
            if (long_range / mid > 0.02) {  // >2% range in 1 hour
                detected_regime = MarketRegime::HIGH_VOLATILITY;
            }
            // Low volatility: tiny range
            else if (long_range / mid < 0.003) {  // <0.3% range in 1 hour
                detected_regime = MarketRegime::LOW_VOLATILITY;
            }
            // Trending down: consistent negative direction
            else if (directional_move < -1.5 || consecutive_down >= 3) {
                detected_regime = MarketRegime::TRENDING_DOWN;
            }
            // Trending up: consistent positive direction
            else if (directional_move > 1.5 || consecutive_up >= 3) {
                detected_regime = MarketRegime::TRENDING_UP;
            }

            // Regime change?
            if (detected_regime != current_regime) {
                // Save previous regime
                if (regime_start_tick > 0) {
                    RegimeStats stats;
                    stats.regime = current_regime;
                    stats.start_tick = regime_start_tick;
                    stats.end_tick = tick_count;
                    stats.start_price = regime_start_price;
                    stats.end_price = mid;
                    stats.price_change_pct = (mid - regime_start_price) / regime_start_price * 100.0;
                    regimes.push_back(stats);

                    regime_counts[current_regime]++;
                    regime_durations[current_regime] += (tick_count - regime_start_tick);
                    regime_returns[current_regime] += stats.price_change_pct;
                }

                current_regime = detected_regime;
                regime_start_tick = tick_count;
                regime_start_price = mid;
            }

            if (tick_count % 10000000 == 0) {
                std::cout << "Processed " << tick_count / 1000000 << "M ticks, "
                          << regimes.size() << " regime changes detected..." << std::endl;
            }
        }

        // Output results
        std::cout << "\n=== REGIME DISTRIBUTION ===" << std::endl;
        std::cout << std::setw(20) << "Regime"
                  << std::setw(12) << "Count"
                  << std::setw(15) << "Total Hours"
                  << std::setw(15) << "Avg Duration"
                  << std::setw(15) << "Avg Return" << std::endl;
        std::cout << std::string(77, '-') << std::endl;

        double ticks_per_hour = 720000.0;
        const char* regime_names[] = {"OSCILLATING", "TRENDING_UP", "TRENDING_DOWN", "HIGH_VOL", "LOW_VOL"};

        for (int i = 0; i < 5; i++) {
            MarketRegime r = static_cast<MarketRegime>(i);
            if (regime_counts[r] > 0) {
                double hours = regime_durations[r] / ticks_per_hour;
                double avg_hours = hours / regime_counts[r];
                double avg_return = regime_returns[r] / regime_counts[r];
                std::cout << std::setw(20) << regime_names[i]
                          << std::setw(12) << regime_counts[r]
                          << std::setw(15) << std::fixed << std::setprecision(1) << hours
                          << std::setw(14) << avg_hours << "h"
                          << std::setw(14) << std::setprecision(2) << avg_return << "%" << std::endl;
            }
        }

        // Danger zone analysis
        std::cout << "\n=== DANGER EVENTS (>5% Drawdown) ===" << std::endl;
        std::cout << "Total events: " << danger_events.size() << std::endl;
        for (size_t i = 0; i < std::min(danger_events.size(), size_t(10)); i++) {
            double hours = danger_events[i].first / ticks_per_hour;
            std::cout << "  Hour " << std::setw(6) << std::setprecision(1) << hours
                      << ": " << std::setprecision(2) << danger_events[i].second << "% drawdown" << std::endl;
        }

        // Trending down analysis (most dangerous)
        std::cout << "\n=== TRENDING DOWN EPISODES ===" << std::endl;
        int dangerous_count = 0;
        for (const auto& r : regimes) {
            if (r.regime == MarketRegime::TRENDING_DOWN && r.price_change_pct < -2.0) {
                dangerous_count++;
                double hours = (r.end_tick - r.start_tick) / ticks_per_hour;
                std::cout << "  Duration: " << std::setprecision(1) << hours << "h"
                          << " | Change: " << std::setprecision(2) << r.price_change_pct << "%"
                          << " | Price: $" << std::setprecision(0) << r.start_price
                          << " -> $" << r.end_price << std::endl;
            }
        }
        std::cout << "Total dangerous trending episodes: " << dangerous_count << std::endl;

        // Detection lead time analysis
        std::cout << "\n=== EARLY WARNING INDICATORS ===" << std::endl;
        AnalyzeEarlyWarnings(regimes);

        // Recommendations
        std::cout << "\n=== REGIME DETECTION RECOMMENDATIONS ===" << std::endl;
        std::cout << "1. PAUSE TRADING when:" << std::endl;
        std::cout << "   - 3+ consecutive hourly down moves (>0.5% each)" << std::endl;
        std::cout << "   - 1-hour directional move exceeds 1.5%" << std::endl;
        std::cout << "   - Current drawdown from peak exceeds 5%" << std::endl;
        std::cout << "2. REDUCE POSITION SIZE when:" << std::endl;
        std::cout << "   - HIGH_VOLATILITY regime detected" << std::endl;
        std::cout << "   - 1-hour range exceeds 2% of price" << std::endl;
        std::cout << "3. INCREASE ACTIVITY when:" << std::endl;
        std::cout << "   - OSCILLATING regime with normal volatility" << std::endl;
        std::cout << "   - Price recovering from trough" << std::endl;
    }

private:
    void AnalyzeEarlyWarnings(const std::vector<RegimeStats>& regimes) {
        // Look for patterns before TRENDING_DOWN episodes
        int warned_correctly = 0;
        int missed_warnings = 0;
        int false_alarms = 0;

        for (size_t i = 1; i < regimes.size(); i++) {
            if (regimes[i].regime == MarketRegime::TRENDING_DOWN) {
                // Check what preceded it
                MarketRegime prev = regimes[i-1].regime;
                if (prev == MarketRegime::HIGH_VOLATILITY) {
                    warned_correctly++;
                } else {
                    missed_warnings++;
                }
            } else if (regimes[i-1].regime == MarketRegime::HIGH_VOLATILITY &&
                       regimes[i].regime != MarketRegime::TRENDING_DOWN) {
                false_alarms++;
            }
        }

        std::cout << "HIGH_VOLATILITY as predictor of TRENDING_DOWN:" << std::endl;
        std::cout << "  Correct warnings: " << warned_correctly << std::endl;
        std::cout << "  Missed warnings: " << missed_warnings << std::endl;
        std::cout << "  False alarms: " << false_alarms << std::endl;

        if (warned_correctly + false_alarms > 0) {
            double precision = 100.0 * warned_correctly / (warned_correctly + false_alarms);
            std::cout << "  Precision: " << std::setprecision(1) << precision << "%" << std::endl;
        }
        if (warned_correctly + missed_warnings > 0) {
            double recall = 100.0 * warned_correctly / (warned_correctly + missed_warnings);
            std::cout << "  Recall: " << std::setprecision(1) << recall << "%" << std::endl;
        }
    }
};

int main() {
    std::cout << std::fixed;

    RegimeDetector detector;

    detector.Analyze(
        "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv"
    );

    return 0;
}
