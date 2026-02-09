/**
 * V6 Combined Exit Improvements Test
 *
 * Tests how three individual improvements stack together:
 * 1. Time-based exit at 50k ticks
 * 2. Close all at 15% DD (instead of 25%)
 * 3. Wider TP at 2.0x spacing
 *
 * Test matrix includes:
 * - Individual improvements
 * - Pair combinations
 * - All three combined
 * - Intermediate parameter values
 *
 * Goal: Determine if improvements stack multiplicatively, additively, or interfere
 */

#include "../include/tick_based_engine.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip>
#include <map>
#include <string>

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

// Grid Gold Strategy with configurable parameters
class GridGoldStrategy {
private:
    SMA sma_;
    double last_open_price_;
    double entry_price_;
    bool position_open_;
    StrategyConfig config_;
    size_t ticks_in_position_;
    double peak_equity_;

public:
    explicit GridGoldStrategy(const StrategyConfig& cfg)
        : sma_(cfg.sma_period),
          last_open_price_(0.0),
          entry_price_(0.0),
          position_open_(false),
          config_(cfg),
          ticks_in_position_(0),
          peak_equity_(10000.0) {
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        // Update SMA
        sma_.Add(tick.bid);

        if (!sma_.IsReady()) {
            return;
        }

        double current_sma = sma_.Get();

        // Track peak equity for drawdown calculation
        if (engine.GetEquity() > peak_equity_) {
            peak_equity_ = engine.GetEquity();
        }

        if (!position_open_) {
            // Entry logic: Grid Open Upwards While Going Upwards
            if (tick.bid > current_sma && tick.bid > last_open_price_) {
                // Open BUY position (1 lot)
                double lot_size = 1.0;

                // Calculate TP based on ATR-like spacing
                double atr_spacing = 5.0;  // Base: 5 USD
                double tp_distance = atr_spacing * config_.tp_spacing_multiplier;
                double tp = tick.ask + tp_distance;

                Trade* trade = engine.OpenMarketOrder("BUY", lot_size, 0.0, tp);
                if (trade) {
                    position_open_ = true;
                    entry_price_ = trade->entry_price;
                    ticks_in_position_ = 0;
                    std::cout << tick.timestamp << " - ENTRY: BUY @ " << entry_price_
                              << " | TP: " << tp << " (spacing: " << tp_distance << ")"
                              << " | SMA: " << current_sma << std::endl;
                }
            }

            last_open_price_ = tick.bid;
        } else {
            // Position management
            ticks_in_position_++;

            // Exit 1: Time-based exit (if enabled)
            if (config_.time_exit_ticks > 0 && ticks_in_position_ >= config_.time_exit_ticks) {
                std::cout << tick.timestamp << " - TIME EXIT triggered at "
                          << ticks_in_position_ << " ticks" << std::endl;
                for (Trade* trade : engine.GetOpenPositions()) {
                    engine.ClosePosition(trade, "Time Exit");
                }
                position_open_ = false;
                ticks_in_position_ = 0;
                return;
            }

            // Exit 2: Close all at DD threshold
            double current_dd = (peak_equity_ - engine.GetEquity()) / peak_equity_;
            if (current_dd >= config_.close_all_dd_threshold) {
                std::cout << tick.timestamp << " - CLOSE ALL DD triggered at "
                          << (current_dd * 100.0) << "%" << std::endl;
                for (Trade* trade : engine.GetOpenPositions()) {
                    engine.ClosePosition(trade, "DD CloseAll");
                }
                position_open_ = false;
                ticks_in_position_ = 0;
                return;
            }

            // Exit 3: Price crosses below SMA
            if (tick.bid < current_sma) {
                std::cout << tick.timestamp << " - SMA EXIT: Price " << tick.bid
                          << " < SMA " << current_sma << std::endl;
                for (Trade* trade : engine.GetOpenPositions()) {
                    engine.ClosePosition(trade, "SMA Cross");
                }
                position_open_ = false;
                ticks_in_position_ = 0;
                return;
            }

            // Check if position was closed by TP
            if (engine.GetOpenPositions().empty()) {
                position_open_ = false;
                ticks_in_position_ = 0;
            }
        }
    }
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

    TestResult() : final_balance(0), total_pl(0), total_trades(0),
                   win_rate(0), max_drawdown(0), max_dd_percent(0), stop_out(false) {}
};

// Run a single test configuration
TestResult RunTest(const StrategyConfig& config) {
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "Testing: " << config.name << std::endl;
    std::cout << "  SMA Period: " << config.sma_period << std::endl;
    std::cout << "  TP Spacing: " << config.tp_spacing_multiplier << "x" << std::endl;
    std::cout << "  Close All DD: " << (config.close_all_dd_threshold * 100.0) << "%" << std::endl;
    std::cout << "  Time Exit: " << (config.time_exit_ticks > 0 ? std::to_string(config.time_exit_ticks) + " ticks" : "Disabled") << std::endl;
    std::cout << std::string(80, '=') << std::endl;

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
    bt_config.tick_data_config.load_all_into_memory = false;  // Use streaming mode for large file

    // Use first 6 months for testing (Jan-Jun 2025)
    bt_config.start_date = "2025.01.01";
    bt_config.end_date = "2025.07.01";

    TickBasedEngine engine(bt_config);
    GridGoldStrategy strategy(config);

    engine.Run([&strategy](const Tick& tick, TickBasedEngine& engine) {
        strategy.OnTick(tick, engine);
    });

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

    return test_result;
}

int main() {
    std::cout << std::fixed << std::setprecision(2);

    std::vector<StrategyConfig> configs;

    // 1. V5 Baseline
    StrategyConfig v5("V5 Baseline");
    v5.sma_period = 11000;
    v5.tp_spacing_multiplier = 1.0;
    v5.close_all_dd_threshold = 0.25;
    v5.time_exit_ticks = 0;
    configs.push_back(v5);

    // 2. V6a: V5 + Time Exit 50k only
    StrategyConfig v6a("V6a: Time Exit 50k");
    v6a.sma_period = 11000;
    v6a.tp_spacing_multiplier = 1.0;
    v6a.close_all_dd_threshold = 0.25;
    v6a.time_exit_ticks = 50000;
    configs.push_back(v6a);

    // 3. V6b: V5 + CloseAll 15% only
    StrategyConfig v6b("V6b: CloseAll 15%");
    v6b.sma_period = 11000;
    v6b.tp_spacing_multiplier = 1.0;
    v6b.close_all_dd_threshold = 0.15;
    v6b.time_exit_ticks = 0;
    configs.push_back(v6b);

    // 4. V6c: V5 + Wider TP 2.0x only
    StrategyConfig v6c("V6c: Wider TP 2.0x");
    v6c.sma_period = 11000;
    v6c.tp_spacing_multiplier = 2.0;
    v6c.close_all_dd_threshold = 0.25;
    v6c.time_exit_ticks = 0;
    configs.push_back(v6c);

    // 5. V6d: V5 + Time Exit + CloseAll 15%
    StrategyConfig v6d("V6d: Time 50k + CloseAll 15%");
    v6d.sma_period = 11000;
    v6d.tp_spacing_multiplier = 1.0;
    v6d.close_all_dd_threshold = 0.15;
    v6d.time_exit_ticks = 50000;
    configs.push_back(v6d);

    // 6. V6e: V5 + Time Exit + Wider TP
    StrategyConfig v6e("V6e: Time 50k + TP 2.0x");
    v6e.sma_period = 11000;
    v6e.tp_spacing_multiplier = 2.0;
    v6e.close_all_dd_threshold = 0.25;
    v6e.time_exit_ticks = 50000;
    configs.push_back(v6e);

    // 7. V6f: V5 + CloseAll 15% + Wider TP
    StrategyConfig v6f("V6f: CloseAll 15% + TP 2.0x");
    v6f.sma_period = 11000;
    v6f.tp_spacing_multiplier = 2.0;
    v6f.close_all_dd_threshold = 0.15;
    v6f.time_exit_ticks = 0;
    configs.push_back(v6f);

    // 8. V6 Full: All three improvements
    StrategyConfig v6_full("V6 FULL: Time 50k + CloseAll 15% + TP 2.0x");
    v6_full.sma_period = 11000;
    v6_full.tp_spacing_multiplier = 2.0;
    v6_full.close_all_dd_threshold = 0.15;
    v6_full.time_exit_ticks = 50000;
    configs.push_back(v6_full);

    // 9. Time Exit 30k ticks (faster exit)
    StrategyConfig time_30k("Time Exit 30k");
    time_30k.sma_period = 11000;
    time_30k.tp_spacing_multiplier = 1.0;
    time_30k.close_all_dd_threshold = 0.25;
    time_30k.time_exit_ticks = 30000;
    configs.push_back(time_30k);

    // 10. Time Exit 75k ticks (slower exit)
    StrategyConfig time_75k("Time Exit 75k");
    time_75k.sma_period = 11000;
    time_75k.tp_spacing_multiplier = 1.0;
    time_75k.close_all_dd_threshold = 0.25;
    time_75k.time_exit_ticks = 75000;
    configs.push_back(time_75k);

    // 11. CloseAll 18% (middle ground)
    StrategyConfig dd_18("CloseAll 18%");
    dd_18.sma_period = 11000;
    dd_18.tp_spacing_multiplier = 1.0;
    dd_18.close_all_dd_threshold = 0.18;
    dd_18.time_exit_ticks = 0;
    configs.push_back(dd_18);

    // 12. CloseAll 20%
    StrategyConfig dd_20("CloseAll 20%");
    dd_20.sma_period = 11000;
    dd_20.tp_spacing_multiplier = 1.0;
    dd_20.close_all_dd_threshold = 0.20;
    dd_20.time_exit_ticks = 0;
    configs.push_back(dd_20);

    // 13. TP 1.5x spacing
    StrategyConfig tp_15("TP 1.5x spacing");
    tp_15.sma_period = 11000;
    tp_15.tp_spacing_multiplier = 1.5;
    tp_15.close_all_dd_threshold = 0.25;
    tp_15.time_exit_ticks = 0;
    configs.push_back(tp_15);

    // Run all tests
    std::vector<TestResult> results;
    for (const auto& config : configs) {
        TestResult result = RunTest(config);
        results.push_back(result);
    }

    // Print summary comparison table
    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "RESULTS SUMMARY - V6 COMBINED EXIT IMPROVEMENTS" << std::endl;
    std::cout << std::string(120, '=') << std::endl;
    std::cout << std::left;
    std::cout << std::setw(40) << "Configuration"
              << std::setw(15) << "Final Balance"
              << std::setw(12) << "Total P/L"
              << std::setw(10) << "Trades"
              << std::setw(12) << "Win Rate"
              << std::setw(15) << "Max DD"
              << std::setw(10) << "Stop Out" << std::endl;
    std::cout << std::string(120, '-') << std::endl;

    // Find baseline for comparison
    double baseline_pl = 0.0;
    for (const auto& result : results) {
        if (result.config_name == "V5 Baseline") {
            baseline_pl = result.total_pl;
            break;
        }
    }

    for (const auto& result : results) {
        double improvement = baseline_pl != 0 ? ((result.total_pl - baseline_pl) / std::abs(baseline_pl)) * 100.0 : 0.0;

        std::cout << std::setw(40) << result.config_name
                  << "$" << std::setw(14) << result.final_balance
                  << "$" << std::setw(11) << result.total_pl
                  << std::setw(10) << result.total_trades
                  << std::setw(11) << result.win_rate << "%"
                  << "$" << std::setw(9) << result.max_drawdown << " (" << result.max_dd_percent << "%)"
                  << std::setw(10) << (result.stop_out ? "YES" : "NO");

        if (result.config_name != "V5 Baseline") {
            std::cout << " [" << (improvement >= 0 ? "+" : "") << improvement << "%]";
        }
        std::cout << std::endl;
    }

    std::cout << std::string(120, '=') << std::endl;

    // Analysis section
    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "ANALYSIS" << std::endl;
    std::cout << std::string(120, '=') << std::endl;

    // Find best individual improvements
    TestResult best_time = results[1];  // V6a
    TestResult best_dd = results[2];    // V6b
    TestResult best_tp = results[3];    // V6c
    TestResult v6_full_result = results[7];  // V6 Full

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
    TestResult best_config = results[0];
    for (const auto& result : results) {
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

    return 0;
}
