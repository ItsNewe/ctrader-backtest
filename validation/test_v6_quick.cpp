/**
 * V6 Combined Exit Improvements Test - QUICK VERSION
 *
 * Runs a quick validation using 3 key periods and 8 core configurations.
 * Should complete in 5-10 minutes instead of hours.
 *
 * Test Periods (1.2M ticks total):
 * 1. Jun 2025: 200k ticks (normal market)
 * 2. Dec Pre-Crash: 500k ticks (before crash)
 * 3. Dec Crash: 500k ticks (the crash itself)
 *
 * Test Configurations (8 total):
 * 1. V5 Baseline
 * 2. V6a: + Time Exit 50k
 * 3. V6b: + CloseAll 15%
 * 4. V6c: + Wider TP 2.0x
 * 5. V6d: Time + CloseAll
 * 6. V6e: Time + TP
 * 7. V6f: CloseAll + TP
 * 8. V6 Full: All three
 */

#include "../include/tick_based_engine.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip>
#include <map>
#include <string>
#include <chrono>
#include <cfloat>
#include <algorithm>

using namespace backtest;

// Simple Moving Average calculator
class SMA {
private:
    std::vector<double> values_;
    size_t period_;
    double sum_;

public:
    explicit SMA(size_t period) : period_(period), sum_(0.0) {
        values_.reserve(period);
    }

    void Add(double value) {
        values_.push_back(value);
        sum_ += value;

        if (values_.size() > period_) {
            sum_ -= values_[values_.size() - period_ - 1];
        }
    }

    double Get() const {
        if (values_.size() < period_) {
            return 0.0;
        }
        return sum_ / period_;
    }

    bool IsReady() const {
        return values_.size() >= period_;
    }

    size_t Count() const {
        return values_.size();
    }
};

// Strategy configuration structure
struct StrategyConfig {
    std::string name;
    size_t sma_period;
    double tp_spacing_multiplier;
    double close_all_dd_threshold;
    size_t time_exit_ticks;  // 0 = disabled

    // Constructor with defaults (V5 baseline)
    StrategyConfig(const std::string& n = "V5 Baseline")
        : name(n), sma_period(11000), tp_spacing_multiplier(1.0),
          close_all_dd_threshold(0.25), time_exit_ticks(0) {}
};

// V5 Grid Averaging Strategy with V6 improvements
class GridAveragingStrategy {
private:
    SMA sma_;
    StrategyConfig config_;
    double peak_equity_;
    bool partial_done_;
    bool all_closed_;
    double spacing_;
    size_t global_tick_count_;

public:
    explicit GridAveragingStrategy(const StrategyConfig& cfg)
        : sma_(cfg.sma_period),
          config_(cfg),
          peak_equity_(10000.0),
          partial_done_(false),
          all_closed_(false),
          spacing_(1.0),
          global_tick_count_(0) {
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        // Update SMA
        sma_.Add(tick.bid);
        global_tick_count_++;

        if (!sma_.IsReady()) {
            return;
        }

        double current_sma = sma_.Get();
        double equity = engine.GetEquity();
        auto positions = engine.GetOpenPositions();

        // Track peak equity for drawdown calculation
        if (equity > peak_equity_) {
            peak_equity_ = equity;
            partial_done_ = false;
            all_closed_ = false;
        }

        // Calculate drawdown
        double dd_pct = (peak_equity_ > 0) ? (peak_equity_ - equity) / peak_equity_ * 100.0 : 0.0;

        // Close ALL at 25% DD (or custom threshold)
        if (dd_pct > config_.close_all_dd_threshold * 100.0 && !all_closed_ && !positions.empty()) {
            for (Trade* t : positions) {
                engine.ClosePosition(t, "CloseAll DD");
            }
            all_closed_ = true;
            peak_equity_ = engine.GetEquity();
            return;
        }

        // Partial close at 8% DD
        if (dd_pct > 8.0 && !partial_done_ && positions.size() > 1) {
            // Close worst 50% of positions
            std::vector<Trade*> pos_copy = positions;
            std::sort(pos_copy.begin(), pos_copy.end(), [&](Trade* a, Trade* b) {
                double unrealized_a = (tick.bid - a->entry_price) * a->lot_size * 100.0;
                double unrealized_b = (tick.bid - b->entry_price) * b->lot_size * 100.0;
                return unrealized_a < unrealized_b;
            });
            int to_close = (int)(pos_copy.size() * 0.5);
            to_close = std::max(1, to_close);
            for (int i = 0; i < to_close; i++) {
                engine.ClosePosition(pos_copy[i], "Partial Close");
            }
            partial_done_ = true;
            return;
        }

        // V6 IMPROVEMENT: Time-based exit (close oldest positions)
        if (config_.time_exit_ticks > 0 && !positions.empty()) {
            for (Trade* t : positions) {
                if (global_tick_count_ - t->id >= config_.time_exit_ticks) {
                    engine.ClosePosition(t, "Time Exit");
                }
            }
        }

        // Open new positions if DD < 5%
        if (dd_pct < 5.0 && (int)positions.size() < 20) {
            // SMA trend filter: only open if price > SMA
            if (tick.bid > current_sma) {
                // Grid spacing logic
                double lowest = DBL_MAX, highest = DBL_MIN;
                for (Trade* t : positions) {
                    lowest = std::min(lowest, t->entry_price);
                    highest = std::max(highest, t->entry_price);
                }

                bool should_open = positions.empty() ||
                                   (lowest >= tick.ask + spacing_) ||
                                   (highest <= tick.ask - spacing_);

                if (should_open) {
                    double lot = 0.01;  // CRITICAL: 0.01 lot, not 1.0!

                    // V6 IMPROVEMENT: Wider TP
                    double tp_distance = tick.spread() + (spacing_ * config_.tp_spacing_multiplier);
                    double tp = tick.ask + tp_distance;

                    Trade* trade = engine.OpenMarketOrder("BUY", lot, 0.0, tp);
                }
            }
        }
    }
};

// Test period structure
struct TestPeriod {
    std::string name;
    std::string start_date;
    std::string end_date;
    std::string description;
};

// Result structure for test comparison
struct TestResult {
    std::string config_name;
    double final_balance;
    double total_pl;
    size_t total_trades;
    double win_rate;
    double max_drawdown;
    double max_dd_percent;
    bool stop_out;
    double elapsed_seconds;

    TestResult() : final_balance(0), total_pl(0), total_trades(0),
                   win_rate(0), max_drawdown(0), max_dd_percent(0),
                   stop_out(false), elapsed_seconds(0) {}
};

// Run a single test configuration on a specific period
TestResult RunTest(const StrategyConfig& config, const TestPeriod& period, bool verbose = false) {
    if (verbose) {
        std::cout << "\nTesting: " << config.name << " | Period: " << period.name << std::endl;
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    TickBacktestConfig bt_config;
    bt_config.symbol = "XAUUSD";
    bt_config.initial_balance = 10000.0;
    bt_config.account_currency = "USD";
    bt_config.contract_size = 100.0;
    bt_config.pip_size = 0.01;
    bt_config.leverage = 500.0;
    bt_config.use_bid_ask_spread = true;

    // Gold swap fees (holding cost)
    bt_config.swap_long = -10.0;
    bt_config.swap_short = 5.0;
    bt_config.swap_mode = 2;
    bt_config.swap_3days = 3;

    // Gold trading schedule (Mon-Fri)
    bt_config.trading_days = 0b0111110;

    // Data source: Full year 2025 XAUUSD tick data
    bt_config.tick_data_config.file_path = "C:/Users/user/Documents/ctrader-backtest/validation/Grid/XAUUSD_TICKS_2025.csv";
    bt_config.tick_data_config.load_all_into_memory = false;

    // Use date range for this period
    bt_config.start_date = period.start_date;
    bt_config.end_date = period.end_date;

    TickBasedEngine engine(bt_config);
    GridAveragingStrategy strategy(config);

    engine.Run([&strategy](const Tick& tick, TickBasedEngine& engine) {
        strategy.OnTick(tick, engine);
    });

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    auto results = engine.GetResults();

    TestResult test_result;
    test_result.config_name = config.name;
    test_result.final_balance = results.final_balance;
    test_result.total_pl = results.total_profit_loss;
    test_result.total_trades = results.total_trades;
    test_result.win_rate = results.win_rate;
    test_result.max_drawdown = results.max_drawdown;
    test_result.max_dd_percent = (results.max_drawdown / 10000.0) * 100.0;
    test_result.stop_out = engine.IsStopOutOccurred();
    test_result.elapsed_seconds = duration.count() / 1000.0;

    return test_result;
}

// Aggregate results across multiple periods
TestResult AggregateResults(const std::vector<TestResult>& period_results) {
    TestResult aggregate;
    aggregate.config_name = period_results[0].config_name;

    // Sum up P/L and trades
    aggregate.total_pl = 0;
    aggregate.total_trades = 0;
    aggregate.max_drawdown = 0;
    aggregate.elapsed_seconds = 0;
    size_t winning_trades = 0;

    for (const auto& result : period_results) {
        aggregate.total_pl += result.total_pl;
        aggregate.total_trades += result.total_trades;
        aggregate.max_drawdown = std::max(aggregate.max_drawdown, result.max_drawdown);
        aggregate.elapsed_seconds += result.elapsed_seconds;
        aggregate.stop_out = aggregate.stop_out || result.stop_out;
        winning_trades += static_cast<size_t>(result.total_trades * result.win_rate / 100.0);
    }

    aggregate.final_balance = 10000.0 + aggregate.total_pl;
    aggregate.win_rate = aggregate.total_trades > 0 ?
        (static_cast<double>(winning_trades) / aggregate.total_trades) * 100.0 : 0.0;
    aggregate.max_dd_percent = (aggregate.max_drawdown / 10000.0) * 100.0;

    return aggregate;
}

int main() {
    std::cout << std::fixed << std::setprecision(2);

    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "V6 COMBINED EXIT IMPROVEMENTS - QUICK TEST" << std::endl;
    std::cout << std::string(120, '=') << std::endl;

    // Define test periods (3 key periods)
    // Note: Using shorter time ranges for speed
    std::vector<TestPeriod> periods = {
        {"Jun 2025", "2025.06.01", "2025.06.07", "Normal market conditions (1 week)"},
        {"Dec Pre-Crash", "2025.12.01", "2025.12.14", "Market before the crash (2 weeks)"},
        {"Dec Crash", "2025.12.15", "2025.12.31", "The December crash period (2+ weeks)"}
    };

    std::cout << "\nTest Periods:" << std::endl;
    for (const auto& period : periods) {
        std::cout << "  " << period.name << ": " << period.start_date << " to " << period.end_date
                  << " - " << period.description << std::endl;
    }

    // Define test configurations (8 core configs)
    std::vector<StrategyConfig> configs;

    // 1. V5 Baseline
    StrategyConfig v5("V5 Baseline");
    v5.sma_period = 11000;
    v5.tp_spacing_multiplier = 1.0;
    v5.close_all_dd_threshold = 0.25;
    v5.time_exit_ticks = 0;
    configs.push_back(v5);

    // 2. V6a: V5 + Time Exit 50k only
    StrategyConfig v6a("V6a: + Time Exit 50k");
    v6a.sma_period = 11000;
    v6a.tp_spacing_multiplier = 1.0;
    v6a.close_all_dd_threshold = 0.25;
    v6a.time_exit_ticks = 50000;
    configs.push_back(v6a);

    // 3. V6b: V5 + CloseAll 15% only
    StrategyConfig v6b("V6b: + CloseAll 15%");
    v6b.sma_period = 11000;
    v6b.tp_spacing_multiplier = 1.0;
    v6b.close_all_dd_threshold = 0.15;
    v6b.time_exit_ticks = 0;
    configs.push_back(v6b);

    // 4. V6c: V5 + Wider TP 2.0x only
    StrategyConfig v6c("V6c: + Wider TP 2.0x");
    v6c.sma_period = 11000;
    v6c.tp_spacing_multiplier = 2.0;
    v6c.close_all_dd_threshold = 0.25;
    v6c.time_exit_ticks = 0;
    configs.push_back(v6c);

    // 5. V6d: V5 + Time Exit + CloseAll 15%
    StrategyConfig v6d("V6d: Time + CloseAll");
    v6d.sma_period = 11000;
    v6d.tp_spacing_multiplier = 1.0;
    v6d.close_all_dd_threshold = 0.15;
    v6d.time_exit_ticks = 50000;
    configs.push_back(v6d);

    // 6. V6e: V5 + Time Exit + Wider TP
    StrategyConfig v6e("V6e: Time + TP");
    v6e.sma_period = 11000;
    v6e.tp_spacing_multiplier = 2.0;
    v6e.close_all_dd_threshold = 0.25;
    v6e.time_exit_ticks = 50000;
    configs.push_back(v6e);

    // 7. V6f: V5 + CloseAll 15% + Wider TP
    StrategyConfig v6f("V6f: CloseAll + TP");
    v6f.sma_period = 11000;
    v6f.tp_spacing_multiplier = 2.0;
    v6f.close_all_dd_threshold = 0.15;
    v6f.time_exit_ticks = 0;
    configs.push_back(v6f);

    // 8. V6 Full: All three improvements
    StrategyConfig v6_full("V6 FULL: All Three");
    v6_full.sma_period = 11000;
    v6_full.tp_spacing_multiplier = 2.0;
    v6_full.close_all_dd_threshold = 0.15;
    v6_full.time_exit_ticks = 50000;
    configs.push_back(v6_full);

    std::cout << "\nTest Configurations: " << configs.size() << std::endl;
    for (const auto& config : configs) {
        std::cout << "  " << config.name << std::endl;
    }

    // Run all tests
    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "RUNNING TESTS (8 configs × 3 periods = 24 total runs)" << std::endl;
    std::cout << std::string(120, '=') << std::endl;

    auto overall_start = std::chrono::high_resolution_clock::now();

    std::vector<TestResult> aggregated_results;

    for (size_t cfg_idx = 0; cfg_idx < configs.size(); ++cfg_idx) {
        const auto& config = configs[cfg_idx];
        std::vector<TestResult> period_results;

        std::cout << "\n[" << (cfg_idx + 1) << "/" << configs.size() << "] "
                  << config.name << std::endl;

        for (size_t period_idx = 0; period_idx < periods.size(); ++period_idx) {
            const auto& period = periods[period_idx];
            std::cout << "  Period " << (period_idx + 1) << "/" << periods.size()
                      << " (" << period.name << ")... " << std::flush;

            TestResult result = RunTest(config, period, false);
            period_results.push_back(result);

            std::cout << "Done (" << result.elapsed_seconds << "s, "
                      << result.total_trades << " trades, P/L: $"
                      << result.total_pl << ")" << std::endl;
        }

        TestResult aggregate = AggregateResults(period_results);
        aggregated_results.push_back(aggregate);
    }

    auto overall_end = std::chrono::high_resolution_clock::now();
    auto overall_duration = std::chrono::duration_cast<std::chrono::seconds>(overall_end - overall_start);

    // Print summary comparison table
    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "RESULTS SUMMARY - AGGREGATED ACROSS ALL PERIODS" << std::endl;
    std::cout << std::string(120, '=') << std::endl;
    std::cout << std::left;
    std::cout << std::setw(30) << "Configuration"
              << std::setw(15) << "Total P/L"
              << std::setw(15) << "Final Balance"
              << std::setw(10) << "Trades"
              << std::setw(12) << "Win Rate"
              << std::setw(15) << "Max DD"
              << std::setw(12) << "vs Baseline"
              << std::setw(10) << "Stop Out" << std::endl;
    std::cout << std::string(120, '-') << std::endl;

    // Find baseline for comparison
    double baseline_pl = 0.0;
    for (const auto& result : aggregated_results) {
        if (result.config_name == "V5 Baseline") {
            baseline_pl = result.total_pl;
            break;
        }
    }

    for (const auto& result : aggregated_results) {
        double improvement = baseline_pl != 0 ? ((result.total_pl - baseline_pl) / std::abs(baseline_pl)) * 100.0 : 0.0;

        std::cout << std::setw(30) << result.config_name
                  << "$" << std::setw(14) << result.total_pl
                  << "$" << std::setw(14) << result.final_balance
                  << std::setw(10) << result.total_trades
                  << std::setw(11) << result.win_rate << "%"
                  << "$" << std::setw(9) << result.max_drawdown << " (" << result.max_dd_percent << "%)"
                  << std::setw(12);

        if (result.config_name != "V5 Baseline") {
            std::cout << (improvement >= 0 ? "+" : "") << improvement << "%";
        } else {
            std::cout << "-";
        }

        std::cout << std::setw(10) << (result.stop_out ? "YES" : "NO");
        std::cout << std::endl;
    }

    std::cout << std::string(120, '=') << std::endl;

    // Analysis section
    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "ANALYSIS" << std::endl;
    std::cout << std::string(120, '=') << std::endl;

    // Find individual improvements
    TestResult best_time = aggregated_results[1];  // V6a
    TestResult best_dd = aggregated_results[2];    // V6b
    TestResult best_tp = aggregated_results[3];    // V6c
    TestResult v6_full_result = aggregated_results[7];  // V6 Full

    double time_improvement = ((best_time.total_pl - baseline_pl) / std::abs(baseline_pl)) * 100.0;
    double dd_improvement = ((best_dd.total_pl - baseline_pl) / std::abs(baseline_pl)) * 100.0;
    double tp_improvement = ((best_tp.total_pl - baseline_pl) / std::abs(baseline_pl)) * 100.0;

    std::cout << "\nIndividual Improvements vs V5 Baseline:" << std::endl;
    std::cout << "  Time Exit 50k:    " << (time_improvement >= 0 ? "+" : "") << time_improvement << "%" << std::endl;
    std::cout << "  CloseAll 15%:     " << (dd_improvement >= 0 ? "+" : "") << dd_improvement << "%" << std::endl;
    std::cout << "  Wider TP 2.0x:    " << (tp_improvement >= 0 ? "+" : "") << tp_improvement << "%" << std::endl;

    // Calculate expected stacking
    double expected_additive = time_improvement + dd_improvement + tp_improvement;
    double expected_multiplicative = ((1.0 + time_improvement/100.0) *
                                      (1.0 + dd_improvement/100.0) *
                                      (1.0 + tp_improvement/100.0) - 1.0) * 100.0;
    double actual_combined = ((v6_full_result.total_pl - baseline_pl) / std::abs(baseline_pl)) * 100.0;

    std::cout << "\nStacking Analysis:" << std::endl;
    std::cout << "  Expected (Additive):         " << (expected_additive >= 0 ? "+" : "") << expected_additive << "%" << std::endl;
    std::cout << "  Expected (Multiplicative):   " << (expected_multiplicative >= 0 ? "+" : "") << expected_multiplicative << "%" << std::endl;
    std::cout << "  Actual (V6 Full):            " << (actual_combined >= 0 ? "+" : "") << actual_combined << "%" << std::endl;

    std::string stacking_type;
    if (std::abs(actual_combined - expected_additive) < std::abs(actual_combined - expected_multiplicative)) {
        stacking_type = "ADDITIVE";
    } else {
        stacking_type = "MULTIPLICATIVE";
    }

    std::cout << "\n  Stacking Behavior: " << stacking_type << std::endl;

    // Find best overall configuration
    TestResult best_config = aggregated_results[0];
    for (const auto& result : aggregated_results) {
        if (!result.stop_out && result.total_pl > best_config.total_pl) {
            best_config = result;
        }
    }

    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "RECOMMENDATION" << std::endl;
    std::cout << std::string(120, '=') << std::endl;
    std::cout << "\nBest Configuration: " << best_config.config_name << std::endl;
    std::cout << "  Final Balance:  $" << best_config.final_balance << std::endl;
    std::cout << "  Total P/L:      $" << best_config.total_pl << std::endl;
    std::cout << "  Trades:         " << best_config.total_trades << std::endl;
    std::cout << "  Win Rate:       " << best_config.win_rate << "%" << std::endl;
    std::cout << "  Max Drawdown:   $" << best_config.max_drawdown << " (" << best_config.max_dd_percent << "%)" << std::endl;
    std::cout << "  Improvement:    +" << (((best_config.total_pl - baseline_pl) / std::abs(baseline_pl)) * 100.0) << "%" << std::endl;

    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "Test completed in " << overall_duration.count() << " seconds ("
              << (overall_duration.count() / 60.0) << " minutes)" << std::endl;
    std::cout << std::string(120, '=') << std::endl;

    return 0;
}
