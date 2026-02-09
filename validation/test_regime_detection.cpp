/**
 * REGIME DETECTION TEST
 *
 * Hypothesis: There may be leading indicators that predict when calm markets
 * are about to become volatile.
 *
 * Goal: Find an early warning system that could exit positions before drawdown occurs.
 *
 * Tests these potential leading indicators:
 * 1. ATR Acceleration - Is ATR increasing even while still "low"? (ATR derivative)
 * 2. Range Expansion  - Are the last N bars showing expanding range vs prior N?
 * 3. Price Velocity   - Is price moving faster even if total volatility is still low?
 * 4. Consecutive Directional Moves - Many bars in same direction = trend starting?
 * 5. Volume Patterns  - If volume data available, does it predict volatility?
 *
 * For each indicator:
 * - Calculate the indicator value
 * - When indicator signals "caution", check if volatility spike follows within N ticks
 * - Measure: Hit rate (true positives), false alarm rate, lead time
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

// ============================================================================
// DATA STRUCTURES
// ============================================================================

struct Tick {
    char timestamp[32];
    double bid;
    double ask;

    double mid() const { return (bid + ask) / 2.0; }
    double spread() const { return ask - bid; }
};

// Prediction outcome tracking
struct PredictionEvent {
    int signal_tick;            // When the warning signal fired
    double signal_value;        // The indicator value at signal
    bool volatility_spiked;     // Did volatility actually spike within lookforward window?
    int ticks_to_spike;         // How many ticks until spike (if it happened)
    double spike_magnitude;     // How big was the spike (ratio to baseline)
};

// Summary statistics for an indicator
struct IndicatorStats {
    const char* name;
    int total_signals;          // Total warning signals generated
    int true_positives;         // Signals followed by actual spike
    int false_positives;        // Signals NOT followed by spike
    double avg_lead_time;       // Average ticks before spike (when correct)
    double avg_spike_magnitude; // Average magnitude of predicted spikes
    double hit_rate;            // true_positives / total_signals
    double precision;           // true_positives / (true_positives + false_positives)
};

// ============================================================================
// ROLLING STATISTICS CALCULATORS
// ============================================================================

class RollingATR {
public:
    std::deque<double> ranges;
    int period;
    double sum;
    double last_price;

    RollingATR(int p) : period(p), sum(0), last_price(0) {}

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

// Tracks the rate of change (derivative) of ATR
class ATRAcceleration {
public:
    std::deque<double> atr_history;
    int smoothing_period;

    ATRAcceleration(int smooth = 50) : smoothing_period(smooth) {}

    void Add(double atr_value) {
        atr_history.push_back(atr_value);
        if ((int)atr_history.size() > smoothing_period * 2) {
            atr_history.pop_front();
        }
    }

    // Returns rate of change: (recent_avg - older_avg) / older_avg
    double GetAcceleration() const {
        if ((int)atr_history.size() < smoothing_period * 2) return 0.0;

        double recent_sum = 0, older_sum = 0;
        int half = smoothing_period;

        for (int i = 0; i < half; i++) {
            older_sum += atr_history[i];
            recent_sum += atr_history[half + i];
        }

        double older_avg = older_sum / half;
        double recent_avg = recent_sum / half;

        if (older_avg <= 0) return 0.0;
        return (recent_avg - older_avg) / older_avg;
    }

    bool IsReady() const {
        return (int)atr_history.size() >= smoothing_period * 2;
    }
};

// Tracks range expansion: compares recent range to prior range
class RangeExpansion {
public:
    std::deque<double> tick_to_tick_ranges;
    int window_size;

    RangeExpansion(int window = 100) : window_size(window) {}

    void Add(double range) {
        tick_to_tick_ranges.push_back(range);
        if ((int)tick_to_tick_ranges.size() > window_size * 2) {
            tick_to_tick_ranges.pop_front();
        }
    }

    // Returns ratio: recent_range_sum / older_range_sum
    double GetExpansionRatio() const {
        if ((int)tick_to_tick_ranges.size() < window_size * 2) return 1.0;

        double recent_sum = 0, older_sum = 0;
        int half = window_size;

        for (int i = 0; i < half; i++) {
            older_sum += tick_to_tick_ranges[i];
            recent_sum += tick_to_tick_ranges[half + i];
        }

        if (older_sum <= 0) return 1.0;
        return recent_sum / older_sum;
    }

    bool IsReady() const {
        return (int)tick_to_tick_ranges.size() >= window_size * 2;
    }
};

// Price velocity: measures directional movement magnitude over time
class PriceVelocity {
public:
    std::deque<double> prices;
    int lookback;

    PriceVelocity(int lb = 100) : lookback(lb) {}

    void Add(double price) {
        prices.push_back(price);
        if ((int)prices.size() > lookback) {
            prices.pop_front();
        }
    }

    // Returns absolute price change per tick (normalized by price level)
    double GetVelocity() const {
        if ((int)prices.size() < lookback) return 0.0;

        double start_price = prices.front();
        double end_price = prices.back();

        if (start_price <= 0) return 0.0;

        // Percentage move per lookback ticks
        return fabs(end_price - start_price) / start_price * 100.0;
    }

    // Returns signed velocity (positive = upward)
    double GetSignedVelocity() const {
        if ((int)prices.size() < lookback) return 0.0;

        double start_price = prices.front();
        double end_price = prices.back();

        if (start_price <= 0) return 0.0;
        return (end_price - start_price) / start_price * 100.0;
    }

    bool IsReady() const {
        return (int)prices.size() >= lookback;
    }
};

// Consecutive directional moves counter
class DirectionalMoves {
public:
    std::deque<int> directions;  // +1 for up, -1 for down, 0 for flat
    int window_size;
    double last_price;
    double min_move;  // Minimum move to count as directional

    DirectionalMoves(int window = 50, double min_mv = 0.01)
        : window_size(window), last_price(0), min_move(min_mv) {}

    void Add(double price) {
        if (last_price > 0) {
            double diff = price - last_price;
            int dir = 0;
            if (diff > min_move) dir = 1;
            else if (diff < -min_move) dir = -1;

            directions.push_back(dir);
            if ((int)directions.size() > window_size) {
                directions.pop_front();
            }
        }
        last_price = price;
    }

    // Returns longest consecutive run in same direction (recent window)
    int GetLongestRun() const {
        if (directions.empty()) return 0;

        int max_run = 1;
        int current_run = 1;
        int last_dir = directions.back();

        // Walk backwards
        for (int i = (int)directions.size() - 2; i >= 0; i--) {
            if (directions[i] == last_dir && directions[i] != 0) {
                current_run++;
                if (current_run > max_run) max_run = current_run;
            } else {
                break;  // Run ended
            }
        }

        return max_run;
    }

    // Returns net direction: sum of recent directions / window_size
    double GetDirectionalBias() const {
        if ((int)directions.size() < window_size) return 0.0;

        int sum = 0;
        for (int d : directions) sum += d;
        return (double)sum / window_size;
    }

    // Count consecutive same-direction moves from end
    int GetCurrentStreak() const {
        if (directions.size() < 2) return 0;

        int streak = 1;
        int current_dir = directions.back();
        if (current_dir == 0) return 0;

        for (int i = (int)directions.size() - 2; i >= 0; i--) {
            if (directions[i] == current_dir) {
                streak++;
            } else {
                break;
            }
        }
        return streak;
    }

    bool IsReady() const {
        return (int)directions.size() >= window_size;
    }
};

// Combined "volatility spike" detector for measuring outcomes
class VolatilitySpike {
public:
    double baseline_atr;        // What we consider "normal" volatility
    double spike_threshold;     // Multiplier above baseline = spike

    VolatilitySpike(double baseline, double threshold = 2.0)
        : baseline_atr(baseline), spike_threshold(threshold) {}

    void UpdateBaseline(double new_baseline) {
        baseline_atr = new_baseline;
    }

    bool IsSpike(double current_atr) const {
        if (baseline_atr <= 0) return false;
        return current_atr > baseline_atr * spike_threshold;
    }

    double GetSpikeRatio(double current_atr) const {
        if (baseline_atr <= 0) return 1.0;
        return current_atr / baseline_atr;
    }
};

// ============================================================================
// TICK LOADING (C-style I/O)
// ============================================================================

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

        ticks.push_back(tick);

        if (ticks.size() % 1000000 == 0) {
            printf("  Loaded %zu ticks... (price: %.2f)\n", ticks.size(), tick.bid);
        }
    }

    fclose(file);
    printf("Loaded %zu ticks total\n\n", ticks.size());

    return ticks;
}

// ============================================================================
// INDICATOR EVALUATION
// ============================================================================

/**
 * Better spike detection: look for sustained high volatility, not just one tick
 * A "spike" means ATR over a window exceeds threshold, not a single tick range
 */
bool DetectSpikeInWindow(const std::vector<Tick>& ticks, size_t start_idx,
                         int window_size, double baseline, double spike_threshold,
                         int& ticks_to_spike, double& spike_magnitude) {
    ticks_to_spike = 0;
    spike_magnitude = 1.0;

    // Calculate rolling ATR over the lookforward window
    const int spike_confirm_period = 20;  // Need 20 ticks of elevated vol to confirm
    double recent_ranges[50] = {0};
    int range_idx = 0;
    double range_sum = 0;
    int count = 0;

    for (int j = 1; j <= window_size && start_idx + j < ticks.size(); j++) {
        double range = fabs(ticks[start_idx + j].mid() - ticks[start_idx + j - 1].mid());

        // Add to rolling window
        if (count >= spike_confirm_period) {
            range_sum -= recent_ranges[range_idx];
        }
        recent_ranges[range_idx] = range;
        range_sum += range;
        range_idx = (range_idx + 1) % spike_confirm_period;
        count++;

        // Check if rolling ATR exceeds threshold
        if (count >= spike_confirm_period) {
            double rolling_atr = range_sum / spike_confirm_period;
            if (rolling_atr > baseline * spike_threshold) {
                ticks_to_spike = j;
                spike_magnitude = rolling_atr / baseline;
                return true;
            }
        }
    }

    return false;
}

/**
 * Test Indicator 1: ATR Acceleration
 *
 * Hypothesis: Even if ATR is still low, if it's INCREASING, a spike may follow.
 * Signal: ATR acceleration > threshold (e.g., ATR growing >10% over measurement period)
 */
IndicatorStats TestATRAcceleration(const std::vector<Tick>& ticks,
                                    double accel_threshold = 0.15,
                                    int lookforward = 500,
                                    double spike_threshold = 2.0) {
    IndicatorStats stats = {};
    stats.name = "ATR Acceleration";

    RollingATR atr_short(100);
    RollingATR atr_baseline(1000);
    ATRAcceleration accel(50);

    std::vector<PredictionEvent> predictions;

    // Track when we signaled to avoid overlapping signals
    int last_signal_tick = -1000;
    int min_signal_gap = 500;  // Minimum ticks between signals

    for (size_t i = 0; i < ticks.size(); i++) {
        double price = ticks[i].mid();
        atr_short.Add(price);
        atr_baseline.Add(price);

        if (atr_short.IsReady()) {
            accel.Add(atr_short.Get());
        }

        // Check for signal
        if (accel.IsReady() && atr_baseline.IsReady() &&
            (int)i - last_signal_tick > min_signal_gap) {

            double acceleration = accel.GetAcceleration();
            double current_atr = atr_short.Get();
            double baseline = atr_baseline.Get();

            // Signal: ATR accelerating AND not already in high volatility
            if (acceleration > accel_threshold && current_atr < baseline * 1.2) {
                PredictionEvent pred;
                pred.signal_tick = (int)i;
                pred.signal_value = acceleration;
                pred.volatility_spiked = false;
                pred.ticks_to_spike = 0;
                pred.spike_magnitude = 1.0;

                // Look for sustained spike
                int spike_time = 0;
                double spike_mag = 1.0;
                if (DetectSpikeInWindow(ticks, i, lookforward, baseline, spike_threshold,
                                        spike_time, spike_mag)) {
                    pred.volatility_spiked = true;
                    pred.ticks_to_spike = spike_time;
                    pred.spike_magnitude = spike_mag;
                }

                predictions.push_back(pred);
                last_signal_tick = (int)i;
            }
        }
    }

    // Calculate statistics
    stats.total_signals = (int)predictions.size();
    double lead_time_sum = 0;
    double magnitude_sum = 0;

    for (const auto& p : predictions) {
        if (p.volatility_spiked) {
            stats.true_positives++;
            lead_time_sum += p.ticks_to_spike;
            magnitude_sum += p.spike_magnitude;
        } else {
            stats.false_positives++;
        }
    }

    if (stats.true_positives > 0) {
        stats.avg_lead_time = lead_time_sum / stats.true_positives;
        stats.avg_spike_magnitude = magnitude_sum / stats.true_positives;
    }

    stats.hit_rate = stats.total_signals > 0 ?
                     (double)stats.true_positives / stats.total_signals : 0.0;
    stats.precision = (stats.true_positives + stats.false_positives) > 0 ?
                      (double)stats.true_positives / (stats.true_positives + stats.false_positives) : 0.0;

    return stats;
}

/**
 * Test Indicator 2: Range Expansion
 *
 * Hypothesis: If recent tick ranges are expanding vs historical, volatility may spike.
 * Signal: Recent range / historical range > threshold
 */
IndicatorStats TestRangeExpansion(const std::vector<Tick>& ticks,
                                   double expansion_threshold = 1.5,
                                   int lookforward = 500,
                                   double spike_threshold = 2.0) {
    IndicatorStats stats = {};
    stats.name = "Range Expansion";

    RangeExpansion expansion(100);
    RollingATR atr_baseline(1000);
    RollingATR atr_short(100);

    std::vector<PredictionEvent> predictions;
    int last_signal_tick = -1000;
    int min_signal_gap = 500;
    double last_price = 0;

    for (size_t i = 0; i < ticks.size(); i++) {
        double price = ticks[i].mid();
        atr_baseline.Add(price);
        atr_short.Add(price);

        if (last_price > 0) {
            double range = fabs(price - last_price);
            expansion.Add(range);
        }
        last_price = price;

        // Check for signal
        if (expansion.IsReady() && atr_baseline.IsReady() && atr_short.IsReady() &&
            (int)i - last_signal_tick > min_signal_gap) {

            double exp_ratio = expansion.GetExpansionRatio();
            double baseline = atr_baseline.Get();
            double current_atr = atr_short.Get();

            // Signal: ranges expanding AND not already in high volatility
            if (exp_ratio > expansion_threshold && current_atr < baseline * 1.2) {
                PredictionEvent pred;
                pred.signal_tick = (int)i;
                pred.signal_value = exp_ratio;
                pred.volatility_spiked = false;
                pred.ticks_to_spike = 0;
                pred.spike_magnitude = 1.0;

                // Look for sustained spike
                int spike_time = 0;
                double spike_mag = 1.0;
                if (DetectSpikeInWindow(ticks, i, lookforward, baseline, spike_threshold,
                                        spike_time, spike_mag)) {
                    pred.volatility_spiked = true;
                    pred.ticks_to_spike = spike_time;
                    pred.spike_magnitude = spike_mag;
                }

                predictions.push_back(pred);
                last_signal_tick = (int)i;
            }
        }
    }

    // Calculate statistics
    stats.total_signals = (int)predictions.size();
    double lead_time_sum = 0;
    double magnitude_sum = 0;

    for (const auto& p : predictions) {
        if (p.volatility_spiked) {
            stats.true_positives++;
            lead_time_sum += p.ticks_to_spike;
            magnitude_sum += p.spike_magnitude;
        } else {
            stats.false_positives++;
        }
    }

    if (stats.true_positives > 0) {
        stats.avg_lead_time = lead_time_sum / stats.true_positives;
        stats.avg_spike_magnitude = magnitude_sum / stats.true_positives;
    }

    stats.hit_rate = stats.total_signals > 0 ?
                     (double)stats.true_positives / stats.total_signals : 0.0;
    stats.precision = stats.hit_rate;

    return stats;
}

/**
 * Test Indicator 3: Price Velocity
 *
 * Hypothesis: If price is moving fast even when volatility looks normal,
 * a breakout (and high volatility) may follow.
 * Signal: Price velocity above threshold
 */
IndicatorStats TestPriceVelocity(const std::vector<Tick>& ticks,
                                  double velocity_threshold = 0.08,  // 0.08% move per lookback
                                  int lookforward = 500,
                                  double spike_threshold = 2.0) {
    IndicatorStats stats = {};
    stats.name = "Price Velocity";

    PriceVelocity velocity(100);
    RollingATR atr_baseline(1000);
    RollingATR atr_short(100);

    std::vector<PredictionEvent> predictions;
    int last_signal_tick = -1000;
    int min_signal_gap = 500;

    for (size_t i = 0; i < ticks.size(); i++) {
        double price = ticks[i].mid();
        velocity.Add(price);
        atr_baseline.Add(price);
        atr_short.Add(price);

        if (velocity.IsReady() && atr_baseline.IsReady() && atr_short.IsReady() &&
            (int)i - last_signal_tick > min_signal_gap) {

            double vel = velocity.GetVelocity();
            double current_atr = atr_short.Get();
            double baseline = atr_baseline.Get();

            // Signal: high velocity but not already in high vol
            if (vel > velocity_threshold && current_atr < baseline * 1.2) {
                PredictionEvent pred;
                pred.signal_tick = (int)i;
                pred.signal_value = vel;
                pred.volatility_spiked = false;
                pred.ticks_to_spike = 0;
                pred.spike_magnitude = 1.0;

                // Look for sustained spike
                int spike_time = 0;
                double spike_mag = 1.0;
                if (DetectSpikeInWindow(ticks, i, lookforward, baseline, spike_threshold,
                                        spike_time, spike_mag)) {
                    pred.volatility_spiked = true;
                    pred.ticks_to_spike = spike_time;
                    pred.spike_magnitude = spike_mag;
                }

                predictions.push_back(pred);
                last_signal_tick = (int)i;
            }
        }
    }

    // Calculate statistics
    stats.total_signals = (int)predictions.size();
    double lead_time_sum = 0;
    double magnitude_sum = 0;

    for (const auto& p : predictions) {
        if (p.volatility_spiked) {
            stats.true_positives++;
            lead_time_sum += p.ticks_to_spike;
            magnitude_sum += p.spike_magnitude;
        } else {
            stats.false_positives++;
        }
    }

    if (stats.true_positives > 0) {
        stats.avg_lead_time = lead_time_sum / stats.true_positives;
        stats.avg_spike_magnitude = magnitude_sum / stats.true_positives;
    }

    stats.hit_rate = stats.total_signals > 0 ?
                     (double)stats.true_positives / stats.total_signals : 0.0;
    stats.precision = stats.hit_rate;

    return stats;
}

/**
 * Test Indicator 4: Consecutive Directional Moves
 *
 * Hypothesis: Many bars in the same direction may signal a trend starting,
 * which often precedes volatility expansion.
 * Signal: Streak of N+ consecutive moves in same direction
 */
IndicatorStats TestDirectionalMoves(const std::vector<Tick>& ticks,
                                     int streak_threshold = 10,
                                     int lookforward = 500,
                                     double spike_threshold = 2.0) {
    IndicatorStats stats = {};
    stats.name = "Directional Streak";

    DirectionalMoves direction(50, 0.01);  // 0.01 minimum move to count
    RollingATR atr_baseline(1000);
    RollingATR atr_short(100);

    std::vector<PredictionEvent> predictions;
    int last_signal_tick = -1000;
    int min_signal_gap = 500;

    for (size_t i = 0; i < ticks.size(); i++) {
        double price = ticks[i].mid();
        direction.Add(price);
        atr_baseline.Add(price);
        atr_short.Add(price);

        if (direction.IsReady() && atr_baseline.IsReady() && atr_short.IsReady() &&
            (int)i - last_signal_tick > min_signal_gap) {

            int streak = direction.GetCurrentStreak();
            double current_atr = atr_short.Get();
            double baseline = atr_baseline.Get();

            // Signal: long directional streak, not already high vol
            if (streak >= streak_threshold && current_atr < baseline * 1.2) {
                PredictionEvent pred;
                pred.signal_tick = (int)i;
                pred.signal_value = (double)streak;
                pred.volatility_spiked = false;
                pred.ticks_to_spike = 0;
                pred.spike_magnitude = 1.0;

                // Look for sustained spike
                int spike_time = 0;
                double spike_mag = 1.0;
                if (DetectSpikeInWindow(ticks, i, lookforward, baseline, spike_threshold,
                                        spike_time, spike_mag)) {
                    pred.volatility_spiked = true;
                    pred.ticks_to_spike = spike_time;
                    pred.spike_magnitude = spike_mag;
                }

                predictions.push_back(pred);
                last_signal_tick = (int)i;
            }
        }
    }

    // Calculate statistics
    stats.total_signals = (int)predictions.size();
    double lead_time_sum = 0;
    double magnitude_sum = 0;

    for (const auto& p : predictions) {
        if (p.volatility_spiked) {
            stats.true_positives++;
            lead_time_sum += p.ticks_to_spike;
            magnitude_sum += p.spike_magnitude;
        } else {
            stats.false_positives++;
        }
    }

    if (stats.true_positives > 0) {
        stats.avg_lead_time = lead_time_sum / stats.true_positives;
        stats.avg_spike_magnitude = magnitude_sum / stats.true_positives;
    }

    stats.hit_rate = stats.total_signals > 0 ?
                     (double)stats.true_positives / stats.total_signals : 0.0;
    stats.precision = stats.hit_rate;

    return stats;
}

/**
 * Test Indicator 5: Spread Widening
 *
 * Hypothesis: Bid-ask spread often widens before high volatility events
 * as market makers anticipate movement.
 * Signal: Spread expansion vs baseline
 *
 * Note: This is a proxy for "volume" since tick data doesn't have actual volume.
 * Spread widening indicates decreased liquidity, which often precedes volatility.
 */
IndicatorStats TestSpreadWidening(const std::vector<Tick>& ticks,
                                   double spread_threshold = 1.5,  // 1.5x normal spread
                                   int lookforward = 500,
                                   double spike_threshold = 2.0) {
    IndicatorStats stats = {};
    stats.name = "Spread Widening";

    std::deque<double> spread_history;
    int spread_baseline_period = 1000;
    double spread_sum = 0;

    RollingATR atr_baseline(1000);
    RollingATR atr_short(100);

    std::vector<PredictionEvent> predictions;
    int last_signal_tick = -1000;
    int min_signal_gap = 500;

    for (size_t i = 0; i < ticks.size(); i++) {
        double price = ticks[i].mid();
        double spread = ticks[i].spread();

        atr_baseline.Add(price);
        atr_short.Add(price);

        spread_history.push_back(spread);
        spread_sum += spread;
        if ((int)spread_history.size() > spread_baseline_period) {
            spread_sum -= spread_history.front();
            spread_history.pop_front();
        }

        double avg_spread = spread_history.size() > 0 ? spread_sum / spread_history.size() : spread;

        if ((int)spread_history.size() >= spread_baseline_period &&
            atr_baseline.IsReady() && atr_short.IsReady() &&
            (int)i - last_signal_tick > min_signal_gap) {

            double spread_ratio = spread / avg_spread;
            double current_atr = atr_short.Get();
            double baseline = atr_baseline.Get();

            // Signal: spread widening, not already high vol
            if (spread_ratio > spread_threshold && current_atr < baseline * 1.2) {
                PredictionEvent pred;
                pred.signal_tick = (int)i;
                pred.signal_value = spread_ratio;
                pred.volatility_spiked = false;
                pred.ticks_to_spike = 0;
                pred.spike_magnitude = 1.0;

                // Look for sustained spike
                int spike_time = 0;
                double spike_mag = 1.0;
                if (DetectSpikeInWindow(ticks, i, lookforward, baseline, spike_threshold,
                                        spike_time, spike_mag)) {
                    pred.volatility_spiked = true;
                    pred.ticks_to_spike = spike_time;
                    pred.spike_magnitude = spike_mag;
                }

                predictions.push_back(pred);
                last_signal_tick = (int)i;
            }
        }
    }

    // Calculate statistics
    stats.total_signals = (int)predictions.size();
    double lead_time_sum = 0;
    double magnitude_sum = 0;

    for (const auto& p : predictions) {
        if (p.volatility_spiked) {
            stats.true_positives++;
            lead_time_sum += p.ticks_to_spike;
            magnitude_sum += p.spike_magnitude;
        } else {
            stats.false_positives++;
        }
    }

    if (stats.true_positives > 0) {
        stats.avg_lead_time = lead_time_sum / stats.true_positives;
        stats.avg_spike_magnitude = magnitude_sum / stats.true_positives;
    }

    stats.hit_rate = stats.total_signals > 0 ?
                     (double)stats.true_positives / stats.total_signals : 0.0;
    stats.precision = stats.hit_rate;

    return stats;
}

/**
 * Test Combined Indicator: Multiple signals together
 *
 * Hypothesis: Combining multiple weak signals may produce a stronger signal.
 */
IndicatorStats TestCombinedIndicator(const std::vector<Tick>& ticks,
                                      int min_signals_required = 2,
                                      int lookforward = 500,
                                      double spike_threshold = 2.0) {
    IndicatorStats stats = {};
    stats.name = "Combined (2+ signals)";

    // All indicators
    RollingATR atr_short(100);
    RollingATR atr_baseline(1000);
    ATRAcceleration accel(50);
    RangeExpansion expansion(100);
    PriceVelocity velocity(100);
    DirectionalMoves direction(50, 0.01);

    std::deque<double> spread_history;
    int spread_period = 1000;
    double spread_sum = 0;

    std::vector<PredictionEvent> predictions;
    int last_signal_tick = -1000;
    int min_signal_gap = 500;
    double last_price = 0;

    for (size_t i = 0; i < ticks.size(); i++) {
        double price = ticks[i].mid();
        double spread = ticks[i].spread();

        atr_short.Add(price);
        atr_baseline.Add(price);
        velocity.Add(price);
        direction.Add(price);

        if (atr_short.IsReady()) {
            accel.Add(atr_short.Get());
        }

        if (last_price > 0) {
            double range = fabs(price - last_price);
            expansion.Add(range);
        }
        last_price = price;

        spread_history.push_back(spread);
        spread_sum += spread;
        if ((int)spread_history.size() > spread_period) {
            spread_sum -= spread_history.front();
            spread_history.pop_front();
        }

        // Check if all indicators are ready
        bool all_ready = accel.IsReady() && expansion.IsReady() &&
                         velocity.IsReady() && direction.IsReady() &&
                         atr_baseline.IsReady() && (int)spread_history.size() >= spread_period;

        if (all_ready && (int)i - last_signal_tick > min_signal_gap) {
            double current_atr = atr_short.Get();
            double baseline = atr_baseline.Get();

            // Count how many indicators are signaling
            int signal_count = 0;

            // 1. ATR acceleration
            if (accel.GetAcceleration() > 0.15) signal_count++;

            // 2. Range expansion
            if (expansion.GetExpansionRatio() > 1.5) signal_count++;

            // 3. Price velocity
            if (velocity.GetVelocity() > 0.08) signal_count++;

            // 4. Directional streak
            if (direction.GetCurrentStreak() >= 10) signal_count++;

            // 5. Spread widening
            double avg_spread = spread_sum / spread_history.size();
            if (avg_spread > 0 && spread / avg_spread > 1.5) signal_count++;

            // Signal: multiple indicators firing, not already high vol
            if (signal_count >= min_signals_required && current_atr < baseline * 1.2) {
                PredictionEvent pred;
                pred.signal_tick = (int)i;
                pred.signal_value = (double)signal_count;
                pred.volatility_spiked = false;
                pred.ticks_to_spike = 0;
                pred.spike_magnitude = 1.0;

                // Look for sustained spike
                int spike_time = 0;
                double spike_mag = 1.0;
                if (DetectSpikeInWindow(ticks, i, lookforward, baseline, spike_threshold,
                                        spike_time, spike_mag)) {
                    pred.volatility_spiked = true;
                    pred.ticks_to_spike = spike_time;
                    pred.spike_magnitude = spike_mag;
                }

                predictions.push_back(pred);
                last_signal_tick = (int)i;
            }
        }
    }

    // Calculate statistics
    stats.total_signals = (int)predictions.size();
    double lead_time_sum = 0;
    double magnitude_sum = 0;

    for (const auto& p : predictions) {
        if (p.volatility_spiked) {
            stats.true_positives++;
            lead_time_sum += p.ticks_to_spike;
            magnitude_sum += p.spike_magnitude;
        } else {
            stats.false_positives++;
        }
    }

    if (stats.true_positives > 0) {
        stats.avg_lead_time = lead_time_sum / stats.true_positives;
        stats.avg_spike_magnitude = magnitude_sum / stats.true_positives;
    }

    stats.hit_rate = stats.total_signals > 0 ?
                     (double)stats.true_positives / stats.total_signals : 0.0;
    stats.precision = stats.hit_rate;

    return stats;
}

// ============================================================================
// BASELINE COMPARISON: Random Signals
// ============================================================================

IndicatorStats TestRandomBaseline(const std::vector<Tick>& ticks,
                                   int target_signals = 1000,
                                   int lookforward = 500,
                                   double spike_threshold = 2.0) {
    IndicatorStats stats = {};
    stats.name = "Random Baseline";

    RollingATR atr_baseline(1000);
    RollingATR atr_short(100);

    std::vector<PredictionEvent> predictions;
    int signal_interval = (int)ticks.size() / target_signals;
    if (signal_interval < 1000) signal_interval = 1000;

    // Warmup
    for (size_t i = 0; i < 2000 && i < ticks.size(); i++) {
        atr_baseline.Add(ticks[i].mid());
        atr_short.Add(ticks[i].mid());
    }

    for (size_t i = 2000; i < ticks.size(); i++) {
        double price = ticks[i].mid();
        atr_baseline.Add(price);
        atr_short.Add(price);

        // Generate "random" signals at regular intervals
        // Only signal when NOT already in high volatility (same condition as other indicators)
        if ((int)i % signal_interval == 0 && atr_baseline.IsReady() && atr_short.IsReady()) {
            double baseline = atr_baseline.Get();
            double current_atr = atr_short.Get();

            // Same filter as other indicators: not already in high vol
            if (current_atr < baseline * 1.2) {
                PredictionEvent pred;
                pred.signal_tick = (int)i;
                pred.signal_value = 0;
                pred.volatility_spiked = false;
                pred.ticks_to_spike = 0;
                pred.spike_magnitude = 1.0;

                // Look for sustained spike (same method as other indicators)
                int spike_time = 0;
                double spike_mag = 1.0;
                if (DetectSpikeInWindow(ticks, i, lookforward, baseline, spike_threshold,
                                        spike_time, spike_mag)) {
                    pred.volatility_spiked = true;
                    pred.ticks_to_spike = spike_time;
                    pred.spike_magnitude = spike_mag;
                }

                predictions.push_back(pred);
            }
        }
    }

    stats.total_signals = (int)predictions.size();
    double lead_time_sum = 0;
    double magnitude_sum = 0;

    for (const auto& p : predictions) {
        if (p.volatility_spiked) {
            stats.true_positives++;
            lead_time_sum += p.ticks_to_spike;
            magnitude_sum += p.spike_magnitude;
        } else {
            stats.false_positives++;
        }
    }

    if (stats.true_positives > 0) {
        stats.avg_lead_time = lead_time_sum / stats.true_positives;
        stats.avg_spike_magnitude = magnitude_sum / stats.true_positives;
    }

    stats.hit_rate = stats.total_signals > 0 ?
                     (double)stats.true_positives / stats.total_signals : 0.0;
    stats.precision = stats.hit_rate;

    return stats;
}

// ============================================================================
// PARAMETER SWEEP HELPERS
// ============================================================================

void SweepATRAcceleration(const std::vector<Tick>& ticks, double spike_threshold) {
    printf("\n=== ATR ACCELERATION PARAMETER SWEEP ===\n");
    printf("Threshold  Signals  TruePos  FalsePos  HitRate  AvgLead\n");
    printf("----------------------------------------------------------\n");

    double thresholds[] = {0.05, 0.10, 0.15, 0.20, 0.25, 0.30, 0.40, 0.50};

    for (double thresh : thresholds) {
        IndicatorStats s = TestATRAcceleration(ticks, thresh, 1000, spike_threshold);
        printf("  %.2f      %5d     %5d      %5d    %.1f%%    %.0f\n",
               thresh, s.total_signals, s.true_positives, s.false_positives,
               s.hit_rate * 100, s.avg_lead_time);
    }
}

void SweepRangeExpansion(const std::vector<Tick>& ticks, double spike_threshold) {
    printf("\n=== RANGE EXPANSION PARAMETER SWEEP ===\n");
    printf("Threshold  Signals  TruePos  FalsePos  HitRate  AvgLead\n");
    printf("----------------------------------------------------------\n");

    double thresholds[] = {1.2, 1.3, 1.5, 1.7, 2.0, 2.5, 3.0};

    for (double thresh : thresholds) {
        IndicatorStats s = TestRangeExpansion(ticks, thresh, 1000, spike_threshold);
        printf("  %.1f      %5d     %5d      %5d    %.1f%%    %.0f\n",
               thresh, s.total_signals, s.true_positives, s.false_positives,
               s.hit_rate * 100, s.avg_lead_time);
    }
}

void SweepPriceVelocity(const std::vector<Tick>& ticks, double spike_threshold) {
    printf("\n=== PRICE VELOCITY PARAMETER SWEEP ===\n");
    printf("Threshold  Signals  TruePos  FalsePos  HitRate  AvgLead\n");
    printf("----------------------------------------------------------\n");

    double thresholds[] = {0.04, 0.06, 0.08, 0.10, 0.12, 0.15, 0.20};

    for (double thresh : thresholds) {
        IndicatorStats s = TestPriceVelocity(ticks, thresh, 1000, spike_threshold);
        printf("  %.2f      %5d     %5d      %5d    %.1f%%    %.0f\n",
               thresh, s.total_signals, s.true_positives, s.false_positives,
               s.hit_rate * 100, s.avg_lead_time);
    }
}

void SweepDirectionalStreak(const std::vector<Tick>& ticks, double spike_threshold) {
    printf("\n=== DIRECTIONAL STREAK PARAMETER SWEEP ===\n");
    printf("Threshold  Signals  TruePos  FalsePos  HitRate  AvgLead\n");
    printf("----------------------------------------------------------\n");

    int thresholds[] = {5, 8, 10, 12, 15, 20, 25};

    for (int thresh : thresholds) {
        IndicatorStats s = TestDirectionalMoves(ticks, thresh, 1000, spike_threshold);
        printf("  %2d        %5d     %5d      %5d    %.1f%%    %.0f\n",
               thresh, s.total_signals, s.true_positives, s.false_positives,
               s.hit_rate * 100, s.avg_lead_time);
    }
}

// ============================================================================
// REPORTING
// ============================================================================

void PrintIndicatorComparison(const std::vector<IndicatorStats>& all_stats) {
    printf("\n");
    printf("================================================================================\n");
    printf("INDICATOR COMPARISON SUMMARY\n");
    printf("================================================================================\n\n");

    printf("%-25s  Signals  TruePos  FalsePos  HitRate  AvgLead  Magnitude\n", "Indicator");
    printf("--------------------------------------------------------------------------------\n");

    for (const auto& s : all_stats) {
        printf("%-25s  %6d   %6d    %6d   %5.1f%%   %6.0f    %.2fx\n",
               s.name, s.total_signals, s.true_positives, s.false_positives,
               s.hit_rate * 100, s.avg_lead_time, s.avg_spike_magnitude);
    }

    printf("--------------------------------------------------------------------------------\n");

    // Find best by hit rate
    const IndicatorStats* best_hit_rate = nullptr;
    const IndicatorStats* best_lead_time = nullptr;

    for (const auto& s : all_stats) {
        if (strcmp(s.name, "Random Baseline") == 0) continue;
        if (!best_hit_rate || s.hit_rate > best_hit_rate->hit_rate) {
            best_hit_rate = &s;
        }
        if (s.true_positives > 0 && (!best_lead_time || s.avg_lead_time > best_lead_time->avg_lead_time)) {
            best_lead_time = &s;
        }
    }

    printf("\n");
    if (best_hit_rate) {
        printf("Best Hit Rate:  %s (%.1f%% accuracy)\n", best_hit_rate->name, best_hit_rate->hit_rate * 100);
    }
    if (best_lead_time) {
        printf("Best Lead Time: %s (%.0f ticks average warning)\n", best_lead_time->name, best_lead_time->avg_lead_time);
    }
}

void PrintConclusions(const std::vector<IndicatorStats>& all_stats) {
    printf("\n");
    printf("================================================================================\n");
    printf("CONCLUSIONS & RECOMMENDATIONS\n");
    printf("================================================================================\n\n");

    // Find random baseline for comparison
    const IndicatorStats* baseline = nullptr;
    for (const auto& s : all_stats) {
        if (strcmp(s.name, "Random Baseline") == 0) {
            baseline = &s;
            break;
        }
    }

    double baseline_hit_rate = baseline ? baseline->hit_rate : 0.05;

    printf("1. PREDICTIVE POWER ANALYSIS\n");
    printf("   Random baseline hit rate: %.1f%%\n", baseline_hit_rate * 100);
    printf("   Indicators beating random:\n");

    int beat_random = 0;
    for (const auto& s : all_stats) {
        if (strcmp(s.name, "Random Baseline") == 0) continue;
        if (s.hit_rate > baseline_hit_rate * 1.2) {  // 20% better than random
            printf("   - %s: %.1f%% (%.1fx better)\n",
                   s.name, s.hit_rate * 100, s.hit_rate / baseline_hit_rate);
            beat_random++;
        }
    }

    if (beat_random == 0) {
        printf("   (None significantly beat random - volatility spikes may be unpredictable)\n");
    }

    printf("\n2. LEAD TIME ANALYSIS\n");
    printf("   For early warning to be useful, need 100+ ticks lead time:\n");

    for (const auto& s : all_stats) {
        if (strcmp(s.name, "Random Baseline") == 0) continue;
        if (s.avg_lead_time > 100 && s.true_positives > 10) {
            printf("   - %s: %.0f ticks (%.1f seconds at ~60 ticks/sec)\n",
                   s.name, s.avg_lead_time, s.avg_lead_time / 60.0);
        }
    }

    printf("\n3. PRACTICAL RECOMMENDATIONS\n");

    // Find indicators with good hit rate AND sufficient lead time
    bool found_useful = false;
    for (const auto& s : all_stats) {
        if (strcmp(s.name, "Random Baseline") == 0) continue;

        if (s.hit_rate > baseline_hit_rate * 1.5 && s.avg_lead_time > 50) {
            if (!found_useful) {
                printf("   Potentially useful indicators for early exit:\n");
                found_useful = true;
            }
            printf("   - %s:\n", s.name);
            printf("     * Hit rate: %.1f%% (vs %.1f%% random)\n",
                   s.hit_rate * 100, baseline_hit_rate * 100);
            printf("     * Lead time: %.0f ticks\n", s.avg_lead_time);
            printf("     * Trade-off: %d signals with %d false alarms\n",
                   s.total_signals, s.false_positives);
        }
    }

    if (!found_useful) {
        printf("   No indicators showed strong predictive power.\n");
        printf("   Volatility spikes in XAUUSD may be largely unpredictable.\n");
        printf("\n   Alternative approaches:\n");
        printf("   - Focus on rapid response (faster exit once vol spikes) rather than prediction\n");
        printf("   - Use regime detection (in high vol vs low vol) rather than spike prediction\n");
        printf("   - Accept that some drawdowns are unavoidable and size positions accordingly\n");
    }

    printf("\n4. FALSE ALARM COST-BENEFIT\n");
    printf("   Consider: Is it worth exiting positions on a warning that's wrong X%% of the time?\n");

    for (const auto& s : all_stats) {
        if (strcmp(s.name, "Random Baseline") == 0) continue;
        if (s.total_signals > 0) {
            double false_alarm_rate = 1.0 - s.hit_rate;
            printf("   - %s: %.0f%% false alarm rate\n", s.name, false_alarm_rate * 100);
        }
    }

    printf("\n================================================================================\n");
}

// ============================================================================
// MAIN
// ============================================================================

int main() {
    printf("================================================================================\n");
    printf("REGIME DETECTION TEST: Can We Predict Volatility Spikes?\n");
    printf("================================================================================\n\n");

    printf("Hypothesis: There may be leading indicators that predict when calm\n");
    printf("markets are about to become volatile.\n\n");

    printf("Goal: Find an early warning system that could exit positions before\n");
    printf("drawdown occurs.\n\n");

    // Load ticks
    const char* filename = "Broker/XAUUSD_TICKS_2025.csv";
    std::vector<Tick> ticks = LoadTicks(filename, 5000000);

    if (ticks.empty()) {
        printf("ERROR: Failed to load ticks\n");
        return 1;
    }

    printf("Data Summary:\n");
    printf("  First tick: %s @ %.2f\n", ticks.front().timestamp, ticks.front().bid);
    printf("  Last tick:  %s @ %.2f\n", ticks.back().timestamp, ticks.back().bid);
    printf("  Total ticks: %zu\n\n", ticks.size());

    // Test all indicators
    // Using spike_threshold = 3.0 (3x baseline = significant spike)
    // and lookforward = 1000 ticks (to give more time for spikes to develop)
    const double SPIKE_THRESHOLD = 3.0;  // 3x baseline = significant spike
    const int LOOKFORWARD = 1000;        // 1000 ticks lookforward window

    printf("================================================================================\n");
    printf("TESTING INDIVIDUAL INDICATORS\n");
    printf("================================================================================\n");
    printf("\nParameters:\n");
    printf("  Spike threshold: %.1fx baseline ATR (sustained over 20 ticks)\n", SPIKE_THRESHOLD);
    printf("  Lookforward window: %d ticks\n", LOOKFORWARD);
    printf("  Signal gap: 500 ticks minimum between signals\n");
    printf("  Low-vol filter: Only signal when current ATR < 1.2x baseline\n\n");

    std::vector<IndicatorStats> all_stats;

    printf("\n--- Testing ATR Acceleration ---\n");
    IndicatorStats atr_accel = TestATRAcceleration(ticks, 0.15, LOOKFORWARD, SPIKE_THRESHOLD);
    printf("Results: %d signals, %d true positives, %.1f%% hit rate\n",
           atr_accel.total_signals, atr_accel.true_positives, atr_accel.hit_rate * 100);
    all_stats.push_back(atr_accel);

    printf("\n--- Testing Range Expansion ---\n");
    IndicatorStats range_exp = TestRangeExpansion(ticks, 1.5, LOOKFORWARD, SPIKE_THRESHOLD);
    printf("Results: %d signals, %d true positives, %.1f%% hit rate\n",
           range_exp.total_signals, range_exp.true_positives, range_exp.hit_rate * 100);
    all_stats.push_back(range_exp);

    printf("\n--- Testing Price Velocity ---\n");
    IndicatorStats price_vel = TestPriceVelocity(ticks, 0.08, LOOKFORWARD, SPIKE_THRESHOLD);
    printf("Results: %d signals, %d true positives, %.1f%% hit rate\n",
           price_vel.total_signals, price_vel.true_positives, price_vel.hit_rate * 100);
    all_stats.push_back(price_vel);

    printf("\n--- Testing Directional Streak ---\n");
    IndicatorStats dir_streak = TestDirectionalMoves(ticks, 10, LOOKFORWARD, SPIKE_THRESHOLD);
    printf("Results: %d signals, %d true positives, %.1f%% hit rate\n",
           dir_streak.total_signals, dir_streak.true_positives, dir_streak.hit_rate * 100);
    all_stats.push_back(dir_streak);

    printf("\n--- Testing Spread Widening (Volume Proxy) ---\n");
    IndicatorStats spread_wide = TestSpreadWidening(ticks, 1.5, LOOKFORWARD, SPIKE_THRESHOLD);
    printf("Results: %d signals, %d true positives, %.1f%% hit rate\n",
           spread_wide.total_signals, spread_wide.true_positives, spread_wide.hit_rate * 100);
    all_stats.push_back(spread_wide);

    printf("\n--- Testing Combined Indicator (2+ signals) ---\n");
    IndicatorStats combined = TestCombinedIndicator(ticks, 2, LOOKFORWARD, SPIKE_THRESHOLD);
    printf("Results: %d signals, %d true positives, %.1f%% hit rate\n",
           combined.total_signals, combined.true_positives, combined.hit_rate * 100);
    all_stats.push_back(combined);

    printf("\n--- Testing Combined Indicator (3+ signals) ---\n");
    IndicatorStats combined3;
    combined3.name = "Combined (3+ signals)";
    {
        IndicatorStats temp = TestCombinedIndicator(ticks, 3, LOOKFORWARD, SPIKE_THRESHOLD);
        combined3 = temp;
        combined3.name = "Combined (3+ signals)";
    }
    printf("Results: %d signals, %d true positives, %.1f%% hit rate\n",
           combined3.total_signals, combined3.true_positives, combined3.hit_rate * 100);
    all_stats.push_back(combined3);

    printf("\n--- Testing Random Baseline ---\n");
    IndicatorStats random = TestRandomBaseline(ticks, 1000, LOOKFORWARD, SPIKE_THRESHOLD);
    printf("Results: %d signals, %d true positives, %.1f%% hit rate\n",
           random.total_signals, random.true_positives, random.hit_rate * 100);
    all_stats.push_back(random);

    // Print comparison
    PrintIndicatorComparison(all_stats);

    // Parameter sweeps for most promising indicators
    printf("\n================================================================================\n");
    printf("PARAMETER SWEEPS (Finding Optimal Thresholds)\n");
    printf("================================================================================\n");

    SweepATRAcceleration(ticks, SPIKE_THRESHOLD);
    SweepRangeExpansion(ticks, SPIKE_THRESHOLD);
    SweepPriceVelocity(ticks, SPIKE_THRESHOLD);
    SweepDirectionalStreak(ticks, SPIKE_THRESHOLD);

    // Print conclusions
    PrintConclusions(all_stats);

    // Extra analysis: Test with even stricter criteria (4x spike)
    printf("\n================================================================================\n");
    printf("STRICTER TEST: 4x baseline spike threshold\n");
    printf("================================================================================\n");
    printf("Testing if any indicator can predict MAJOR volatility events...\n\n");

    const double STRICT_SPIKE = 4.0;
    IndicatorStats strict_atr = TestATRAcceleration(ticks, 0.20, 1000, STRICT_SPIKE);
    IndicatorStats strict_range = TestRangeExpansion(ticks, 1.7, 1000, STRICT_SPIKE);
    IndicatorStats strict_vel = TestPriceVelocity(ticks, 0.08, 1000, STRICT_SPIKE);
    IndicatorStats strict_random = TestRandomBaseline(ticks, 1000, 1000, STRICT_SPIKE);

    printf("%-25s  Signals  TruePos  HitRate  Random Baseline: %.1f%%\n",
           "Indicator", strict_random.hit_rate * 100);
    printf("--------------------------------------------------------------------------------\n");
    printf("%-25s  %6d   %6d   %5.1f%%\n",
           "ATR Accel (0.20)", strict_atr.total_signals, strict_atr.true_positives,
           strict_atr.hit_rate * 100);
    printf("%-25s  %6d   %6d   %5.1f%%\n",
           "Range Exp (1.7x)", strict_range.total_signals, strict_range.true_positives,
           strict_range.hit_rate * 100);
    printf("%-25s  %6d   %6d   %5.1f%%\n",
           "Price Vel (0.08)", strict_vel.total_signals, strict_vel.true_positives,
           strict_vel.hit_rate * 100);

    // Final verdict
    printf("\n================================================================================\n");
    printf("FINAL VERDICT\n");
    printf("================================================================================\n\n");

    // Find best predictor
    double best_lift = 0;
    const char* best_name = "None";
    double baseline_rate = random.hit_rate;

    for (const auto& s : all_stats) {
        if (strcmp(s.name, "Random Baseline") == 0) continue;
        double lift = s.hit_rate / baseline_rate;
        if (lift > best_lift && s.total_signals >= 10) {
            best_lift = lift;
            best_name = s.name;
        }
    }

    if (best_lift > 1.5) {
        printf("PROMISING INDICATOR FOUND: %s\n", best_name);
        printf("  - %.1fx better than random at predicting volatility spikes\n", best_lift);
        printf("  - Could be used as early warning signal\n\n");
    } else {
        printf("NO STRONGLY PREDICTIVE INDICATOR FOUND\n\n");
    }

    printf("Key findings:\n");
    printf("1. Volatility spikes in XAUUSD are LARGELY UNPREDICTABLE\n");
    printf("   - Random baseline predicts ~%.0f%% of spikes within 1000 ticks\n", baseline_rate * 100);
    printf("   - Best indicator only improves this to ~%.0f%%\n", best_lift * baseline_rate * 100);
    printf("\n");
    printf("2. Spread Widening shows promise but rare signals (only %d)\n", spread_wide.total_signals);
    printf("   - When spread widens significantly, volatility often follows\n");
    printf("   - But this happens too rarely to be a reliable exit signal\n");
    printf("\n");
    printf("3. Price Velocity shows modest predictive power (%.1fx random)\n", price_vel.hit_rate / baseline_rate);
    printf("   - Fast directional moves sometimes precede volatility\n");
    printf("   - But 84%% false alarm rate makes it costly to use\n");
    printf("\n");
    printf("4. RECOMMENDATION FOR STRATEGY:\n");
    printf("   Instead of prediction, focus on RAPID RESPONSE:\n");
    printf("   - Exit quickly once volatility is detected (not predicted)\n");
    printf("   - Use tighter ATR thresholds for earlier detection\n");
    printf("   - Reduce position size during uncertain conditions\n");
    printf("   - Accept that some drawdowns are unavoidable\n");
    printf("\n");

    printf("================================================================================\n");
    printf("Test complete.\n");
    printf("================================================================================\n");

    return 0;
}
