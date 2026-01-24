#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <thread>
#include <mutex>
#include <queue>
#include <iomanip>
#include <string>
#include <chrono>

using namespace backtest;

// ============================================================
// Auto-calibration logic (mirrors EA's CalibrateTypicalVol)
// ============================================================
struct CalibrationResult {
    double lookback_hours;
    double base_spacing_pct;
    double typical_vol_pct;
    int swing_count;
    double median_swing_duration_min;
    double median_swing_amplitude_pct;
};

struct M1Bar {
    double open, high, low, close;
    long timestamp_seconds;
};

// Build M1 bars from tick data
std::vector<M1Bar> BuildM1Bars(const std::vector<Tick>& ticks, int max_days = 30) {
    std::vector<M1Bar> bars;
    if (ticks.empty()) return bars;

    // Parse first tick timestamp to get start
    auto parseSeconds = [](const std::string& ts) -> long {
        int Y, M, D, h, m, s;
        if (sscanf(ts.c_str(), "%d.%d.%d %d:%d:%d", &Y, &M, &D, &h, &m, &s) == 6) {
            // Simple seconds-of-year (approximate, good enough for duration)
            return (long)((M-1)*30*24*3600 + (D-1)*24*3600 + h*3600 + m*60 + s);
        }
        return 0;
    };

    long start_seconds = parseSeconds(ticks[0].timestamp);
    long max_seconds = start_seconds + (long)max_days * 24 * 3600;

    M1Bar current_bar = {};
    long current_bar_minute = -1;

    for (const auto& tick : ticks) {
        long ts = parseSeconds(tick.timestamp);
        if (ts >= max_seconds) break;

        long minute = ts / 60;
        if (minute != current_bar_minute) {
            if (current_bar_minute >= 0) {
                bars.push_back(current_bar);
            }
            current_bar.open = tick.bid;
            current_bar.high = tick.bid;
            current_bar.low = tick.bid;
            current_bar.close = tick.bid;
            current_bar.timestamp_seconds = ts;
            current_bar_minute = minute;
        } else {
            current_bar.high = std::max(current_bar.high, tick.bid);
            current_bar.low = std::min(current_bar.low, tick.bid);
            current_bar.close = tick.bid;
        }
    }
    if (current_bar_minute >= 0) {
        bars.push_back(current_bar);
    }

    return bars;
}

CalibrationResult CalibrateFromBars(const std::vector<M1Bar>& bars, double survive_pct) {
    CalibrationResult result = {};
    result.lookback_hours = 4.0;  // fallback
    result.base_spacing_pct = 0.06;  // fallback
    result.typical_vol_pct = 0.55;  // fallback

    if (bars.size() < 120) {
        std::cout << "Calibration: Insufficient bars (" << bars.size() << ")\n";
        return result;
    }

    // Calculate average price
    double avg_price = 0;
    int step = std::max(1, (int)bars.size() / 500);
    int samples = 0;
    for (size_t i = 0; i < bars.size(); i += step) {
        avg_price += (bars[i].high + bars[i].low) / 2.0;
        samples++;
    }
    avg_price = (samples > 0) ? avg_price / samples : bars[0].close;

    // Fixed 0.15% threshold for swing detection
    double threshold_pct = 0.15;
    double threshold = avg_price * threshold_pct / 100.0;

    // Detect swings - track both duration and amplitude
    bool in_upswing = true;
    double extreme_price = bars[0].close;
    int extreme_idx = 0;
    std::vector<double> durations;
    std::vector<double> amplitudes;

    for (size_t i = 1; i < bars.size(); i++) {
        double price = bars[i].close;

        if (in_upswing) {
            if (price > extreme_price) {
                extreme_price = price;
                extreme_idx = (int)i;
            } else if (price < extreme_price - threshold) {
                int dur = (int)i - extreme_idx;
                double amp = extreme_price - price;
                durations.push_back((double)dur);
                amplitudes.push_back(amp);
                in_upswing = false;
                extreme_price = price;
                extreme_idx = (int)i;
            }
        } else {
            if (price < extreme_price) {
                extreme_price = price;
                extreme_idx = (int)i;
            } else if (price > extreme_price + threshold) {
                int dur = (int)i - extreme_idx;
                double amp = price - extreme_price;
                durations.push_back((double)dur);
                amplitudes.push_back(amp);
                in_upswing = true;
                extreme_price = price;
                extreme_idx = (int)i;
            }
        }
    }

    result.swing_count = (int)durations.size();

    if (result.swing_count >= 10) {
        // Lookback from median swing duration
        std::sort(durations.begin(), durations.end());
        double median_duration_min = durations[result.swing_count / 2];
        result.median_swing_duration_min = median_duration_min;

        // Lookback = 2x median swing duration (clamped 0.25-8h)
        double lookback_min = median_duration_min * 2.0;
        result.lookback_hours = std::max(0.25, std::min(8.0, lookback_min / 60.0));
        // Round to nearest 0.25h
        result.lookback_hours = std::round(result.lookback_hours * 4.0) / 4.0;

        // BaseSpacing from median swing amplitude
        std::sort(amplitudes.begin(), amplitudes.end());
        double median_amplitude = amplitudes[result.swing_count / 2];
        double median_ampl_pct = median_amplitude / avg_price * 100.0;
        result.median_swing_amplitude_pct = median_ampl_pct;

        // Spacing = 50% of median swing amplitude
        result.base_spacing_pct = median_ampl_pct * 0.5;

        // Clamp: ensure 10-50 grid levels within SurvivePct distance
        double min_spacing = survive_pct / 50.0;  // Max 50 levels
        double max_spacing = survive_pct / 10.0;  // Min 10 levels
        result.base_spacing_pct = std::max(min_spacing, std::min(max_spacing, result.base_spacing_pct));

        // Round to 2 decimal places
        result.base_spacing_pct = std::round(result.base_spacing_pct * 100.0) / 100.0;
    } else {
        std::cout << "Calibration: Too few swings (" << result.swing_count << ")\n";
        return result;
    }

    // Measure typical volatility for the calibrated lookback
    int bars_per_window = (int)(result.lookback_hours * 60);
    if (bars_per_window < 1) bars_per_window = 1;

    int num_windows = (int)bars.size() / bars_per_window;
    std::vector<double> ranges;

    for (int w = 0; w < num_windows; w++) {
        int start_idx = w * bars_per_window;
        double high = bars[start_idx].high;
        double low = bars[start_idx].low;
        double mid_sum = 0;
        int mid_count = 0;

        for (int i = 0; i < bars_per_window && (start_idx + i) < (int)bars.size(); i++) {
            int idx = start_idx + i;
            high = std::max(high, bars[idx].high);
            low = std::min(low, bars[idx].low);
            mid_sum += (bars[idx].high + bars[idx].low) / 2.0;
            mid_count++;
        }

        double range = high - low;
        if (range > 0 && mid_count > 0) {
            double avg_window_price = mid_sum / mid_count;
            ranges.push_back(range / avg_window_price * 100.0);
        }
    }

    if (ranges.size() >= 5) {
        std::sort(ranges.begin(), ranges.end());
        result.typical_vol_pct = ranges[ranges.size() / 2];
    }

    return result;
}

// ============================================================
// Test configuration
// ============================================================
struct TestConfig {
    std::string name;
    double survive_pct;
    double base_spacing_pct;
    double typical_vol_pct;
    double lookback_hours;
    // Which params are auto-calibrated
    bool auto_lookback;
    bool auto_spacing;
    bool auto_typvol;
};

struct TestResult {
    std::string name;
    double final_balance;
    double return_mult;
    double max_dd_pct;
    int trades;
    double swap_charged;
    int spacing_changes;
    double base_spacing_pct;
    double typical_vol_pct;
    double lookback_hours;
};

// ============================================================
// Run a single backtest
// ============================================================
TestResult RunBacktest(const TestConfig& cfg, const std::vector<Tick>& ticks,
                       const TickBacktestConfig& base_config) {
    TickBacktestConfig config = base_config;

    FillUpOscillation::AdaptiveConfig adaptive_cfg;
    adaptive_cfg.pct_spacing = true;
    adaptive_cfg.typical_vol_pct = cfg.typical_vol_pct;
    adaptive_cfg.min_spacing_mult = 0.5;
    adaptive_cfg.max_spacing_mult = 3.0;
    adaptive_cfg.min_spacing_abs = 0.005;   // 0.005% of price
    adaptive_cfg.max_spacing_abs = 1.0;     // 1.0% of price
    adaptive_cfg.spacing_change_threshold = 0.01;  // 0.01% of price

    FillUpOscillation strategy(cfg.survive_pct, cfg.base_spacing_pct,
                               0.01, 10.0, 100.0, 500.0,
                               FillUpOscillation::ADAPTIVE_SPACING,
                               0.1, 30.0, cfg.lookback_hours, adaptive_cfg);

    TickBasedEngine engine(config);
    engine.RunWithTicks(ticks, [&strategy](const Tick& t, TickBasedEngine& e) {
        strategy.OnTick(t, e);
    });

    auto results = engine.GetResults();

    TestResult r;
    r.name = cfg.name;
    r.final_balance = results.final_balance;
    r.return_mult = results.final_balance / base_config.initial_balance;
    r.max_dd_pct = results.max_drawdown_pct;
    r.trades = results.total_trades;
    r.swap_charged = results.total_swap_charged;
    r.spacing_changes = strategy.GetAdaptiveSpacingChanges();
    r.base_spacing_pct = cfg.base_spacing_pct;
    r.typical_vol_pct = cfg.typical_vol_pct;
    r.lookback_hours = cfg.lookback_hours;
    return r;
}

// ============================================================
// Main
// ============================================================
int main() {
    std::cout << "=== Auto-Calibration Validation (XAUUSD 2025) ===" << std::endl;
    std::cout << "Testing: does auto-calibration match/beat manual params?" << std::endl;
    std::cout << std::endl;

    // --- Load tick data ---
    std::string tick_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    std::cout << "Loading XAUUSD 2025 tick data..." << std::endl;
    auto load_start = std::chrono::high_resolution_clock::now();

    std::vector<Tick> all_ticks;
    {
        std::ifstream file(tick_path);
        if (!file.is_open()) {
            std::cerr << "Cannot open: " << tick_path << std::endl;
            return 1;
        }
        std::string line;
        std::getline(file, line);  // Skip header
        all_ticks.reserve(52000000);

        while (std::getline(file, line)) {
            if (line.empty()) continue;
            Tick tick;
            std::stringstream ss(line);
            std::string datetime_str, bid_str, ask_str;
            std::getline(ss, datetime_str, '\t');
            std::getline(ss, bid_str, '\t');
            std::getline(ss, ask_str, '\t');
            tick.timestamp = datetime_str;
            tick.bid = std::stod(bid_str);
            tick.ask = std::stod(ask_str);
            tick.volume = 0;
            all_ticks.push_back(tick);
        }
    }
    auto load_end = std::chrono::high_resolution_clock::now();
    auto load_dur = std::chrono::duration_cast<std::chrono::seconds>(load_end - load_start);
    std::cout << "Loaded " << all_ticks.size() << " ticks in " << load_dur.count() << "s" << std::endl;

    TickDataConfig tick_config;
    tick_config.file_path = tick_path;
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickBacktestConfig base_config;
    base_config.symbol = "XAUUSD";
    base_config.initial_balance = 10000.0;
    base_config.account_currency = "USD";
    base_config.contract_size = 100.0;
    base_config.leverage = 500.0;
    base_config.margin_rate = 1.0;
    base_config.pip_size = 0.01;
    base_config.swap_long = -66.99;
    base_config.swap_short = 41.2;
    base_config.swap_mode = 1;
    base_config.swap_3days = 3;
    base_config.start_date = "2025.01.01";
    base_config.end_date = "2025.12.30";
    base_config.tick_data_config = tick_config;

    // --- Phase 1: Auto-calibrate from first 30 days ---
    std::cout << "\n--- Phase 1: Auto-Calibration (first 30 days) ---" << std::endl;
    std::vector<M1Bar> bars = BuildM1Bars(all_ticks, 30);
    std::cout << "Built " << bars.size() << " M1 bars from first 30 days" << std::endl;

    double survive_pct = 13.0;  // Fixed risk parameter
    CalibrationResult cal = CalibrateFromBars(bars, survive_pct);

    std::cout << "Calibration results:" << std::endl;
    std::cout << "  Swings detected: " << cal.swing_count << std::endl;
    std::cout << "  Median swing duration: " << std::fixed << std::setprecision(1)
              << cal.median_swing_duration_min << " min" << std::endl;
    std::cout << "  Median swing amplitude: " << std::setprecision(4)
              << cal.median_swing_amplitude_pct << "%" << std::endl;
    std::cout << "  => Lookback: " << std::setprecision(2) << cal.lookback_hours << " hours" << std::endl;
    std::cout << "  => BaseSpacingPct: " << std::setprecision(4) << cal.base_spacing_pct << "%" << std::endl;
    std::cout << "  => TypicalVolPct: " << std::setprecision(4) << cal.typical_vol_pct << "%" << std::endl;

    // Manual reference values (validated in prior testing)
    double manual_lookback = 4.0;
    double manual_spacing_pct = 0.06;
    double manual_typvol_pct = 0.55;

    std::cout << "\nManual reference:" << std::endl;
    std::cout << "  Lookback: " << manual_lookback << " hours" << std::endl;
    std::cout << "  BaseSpacingPct: " << manual_spacing_pct << "%" << std::endl;
    std::cout << "  TypicalVolPct: " << manual_typvol_pct << "%" << std::endl;

    // --- Phase 2: Build test configurations ---
    std::vector<TestConfig> configs;

    // 1. Baseline: all manual
    configs.push_back({"MANUAL_ALL", survive_pct, manual_spacing_pct, manual_typvol_pct, manual_lookback, false, false, false});

    // 2. All auto-calibrated
    configs.push_back({"AUTO_ALL", survive_pct, cal.base_spacing_pct, cal.typical_vol_pct, cal.lookback_hours, true, true, true});

    // 3. Incremental: only lookback auto
    configs.push_back({"AUTO_LOOKBACK", survive_pct, manual_spacing_pct, manual_typvol_pct, cal.lookback_hours, true, false, false});

    // 4. Incremental: only spacing auto
    configs.push_back({"AUTO_SPACING", survive_pct, cal.base_spacing_pct, manual_typvol_pct, manual_lookback, false, true, false});

    // 5. Incremental: only typvol auto
    configs.push_back({"AUTO_TYPVOL", survive_pct, manual_spacing_pct, cal.typical_vol_pct, manual_lookback, false, false, true});

    // 6. Lookback + TypVol auto
    configs.push_back({"AUTO_LB+TV", survive_pct, manual_spacing_pct, cal.typical_vol_pct, cal.lookback_hours, true, false, true});

    // 7. Lookback + Spacing auto
    configs.push_back({"AUTO_LB+SP", survive_pct, cal.base_spacing_pct, manual_typvol_pct, cal.lookback_hours, true, true, false});

    // 8. Spacing + TypVol auto
    configs.push_back({"AUTO_SP+TV", survive_pct, cal.base_spacing_pct, cal.typical_vol_pct, manual_lookback, false, true, true});

    // Also test with different calibration windows (15, 60, 90 days)
    std::vector<int> cal_windows = {15, 60, 90};
    for (int days : cal_windows) {
        std::vector<M1Bar> bars_w = BuildM1Bars(all_ticks, days);
        CalibrationResult cal_w = CalibrateFromBars(bars_w, survive_pct);
        std::string name = "AUTO_ALL_" + std::to_string(days) + "d";
        configs.push_back({name, survive_pct, cal_w.base_spacing_pct, cal_w.typical_vol_pct, cal_w.lookback_hours, true, true, true});
    }

    // --- Phase 3: Run all tests in parallel ---
    std::cout << "\n--- Phase 2: Running " << configs.size() << " configurations ---" << std::endl;

    std::vector<TestResult> results(configs.size());
    std::mutex queue_mutex;
    std::queue<int> work_queue;
    for (int i = 0; i < (int)configs.size(); i++) work_queue.push(i);

    int completed = 0;
    unsigned int num_threads = std::thread::hardware_concurrency();
    std::vector<std::thread> threads;

    for (unsigned int t = 0; t < num_threads; t++) {
        threads.emplace_back([&]() {
            while (true) {
                int idx = -1;
                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    if (work_queue.empty()) return;
                    idx = work_queue.front();
                    work_queue.pop();
                }

                results[idx] = RunBacktest(configs[idx], all_ticks, base_config);

                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    completed++;
                    std::cout << "  [" << completed << "/" << configs.size() << "] "
                              << configs[idx].name << ": " << std::fixed << std::setprecision(2)
                              << results[idx].return_mult << "x, DD="
                              << results[idx].max_dd_pct << "%" << std::endl;
                }
            }
        });
    }

    for (auto& t : threads) t.join();

    // --- Phase 4: Results ---
    std::cout << "\n--- Results Summary ---" << std::endl;
    std::cout << std::left << std::setw(18) << "Config"
              << std::right << std::setw(8) << "Return"
              << std::setw(8) << "MaxDD"
              << std::setw(8) << "Trades"
              << std::setw(10) << "Swap"
              << std::setw(8) << "SpChg"
              << std::setw(10) << "BaseSp%"
              << std::setw(10) << "TypVol%"
              << std::setw(8) << "LookBk"
              << std::endl;
    std::cout << std::string(88, '-') << std::endl;

    // Sort by return for readability
    std::vector<int> order(results.size());
    for (int i = 0; i < (int)order.size(); i++) order[i] = i;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return results[a].return_mult > results[b].return_mult;
    });

    for (int idx : order) {
        const auto& r = results[idx];
        std::cout << std::left << std::setw(18) << r.name
                  << std::right << std::fixed
                  << std::setw(7) << std::setprecision(2) << r.return_mult << "x"
                  << std::setw(7) << std::setprecision(1) << r.max_dd_pct << "%"
                  << std::setw(8) << r.trades
                  << std::setw(9) << std::setprecision(0) << r.swap_charged << "$"
                  << std::setw(8) << r.spacing_changes
                  << std::setw(9) << std::setprecision(4) << r.base_spacing_pct << "%"
                  << std::setw(9) << std::setprecision(4) << r.typical_vol_pct << "%"
                  << std::setw(7) << std::setprecision(2) << r.lookback_hours << "h"
                  << std::endl;
    }

    // --- Phase 5: Analysis ---
    std::cout << "\n--- Analysis ---" << std::endl;

    // Find manual baseline
    double manual_return = results[0].return_mult;
    double manual_dd = results[0].max_dd_pct;
    std::cout << "Manual baseline: " << std::setprecision(2) << manual_return << "x, "
              << std::setprecision(1) << manual_dd << "% DD" << std::endl;

    // Compare auto_all vs manual
    double auto_return = results[1].return_mult;
    double auto_dd = results[1].max_dd_pct;
    double return_diff = (auto_return - manual_return) / manual_return * 100.0;
    double dd_diff = auto_dd - manual_dd;
    std::cout << "Auto-all vs manual: " << (return_diff >= 0 ? "+" : "")
              << std::setprecision(1) << return_diff << "% return, "
              << (dd_diff >= 0 ? "+" : "") << std::setprecision(1) << dd_diff << "% DD"
              << std::endl;

    // Identify which auto-calibrated param causes issues
    std::cout << "\nIncremental impact (vs manual baseline):" << std::endl;
    for (int i = 2; i < 8; i++) {
        double ret_diff = (results[i].return_mult - manual_return) / manual_return * 100.0;
        double dd_d = results[i].max_dd_pct - manual_dd;
        std::cout << "  " << std::left << std::setw(14) << results[i].name
                  << std::right << " return: " << (ret_diff >= 0 ? "+" : "")
                  << std::setprecision(1) << ret_diff << "%"
                  << "  DD: " << (dd_d >= 0 ? "+" : "") << std::setprecision(1) << dd_d << "%"
                  << std::endl;
    }

    // Check if any stopped out
    std::cout << "\nStop-outs (return < 0.5x):" << std::endl;
    bool any_stopout = false;
    for (const auto& r : results) {
        if (r.return_mult < 0.5) {
            std::cout << "  WARNING: " << r.name << " stopped out! Return="
                      << std::setprecision(2) << r.return_mult << "x" << std::endl;
            any_stopout = true;
        }
    }
    if (!any_stopout) std::cout << "  None" << std::endl;

    // Recommendation
    std::cout << "\n--- Recommendation ---" << std::endl;
    if (auto_return >= manual_return * 0.85 && auto_dd <= manual_dd * 1.15) {
        std::cout << "AUTO_ALL is viable (within 15% of manual on both return and DD)" << std::endl;
    } else {
        std::cout << "AUTO_ALL has issues. Checking which param to keep manual:" << std::endl;
        // Find best combination
        int best_idx = 0;
        double best_score = 0;
        for (int i = 0; i < (int)results.size(); i++) {
            double score = results[i].return_mult / std::max(1.0, results[i].max_dd_pct / 100.0);
            if (score > best_score) {
                best_score = score;
                best_idx = i;
            }
        }
        std::cout << "  Best risk-adjusted: " << results[best_idx].name
                  << " (" << std::setprecision(2) << results[best_idx].return_mult << "x, "
                  << std::setprecision(1) << results[best_idx].max_dd_pct << "% DD)" << std::endl;
    }

    return 0;
}
