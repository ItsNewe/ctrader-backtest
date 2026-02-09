/**
 * Combined Improvements Test
 *
 * Tests all combinations of:
 * 1. 50% Position Sizing (Domain 9)
 * 2. 4-Hour Time Exit (Domain 6)
 * 3. Percentage-Based Threshold (price-normalized spacing)
 *
 * 8 combinations tested: baseline + 7 improvement combos
 */

#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <deque>
#include <cmath>
#include <cfloat>
#include <vector>

using namespace backtest;

struct TestConfig {
    std::string name;
    bool use_half_sizing;      // 50% position sizing
    bool use_time_exit;        // 4-hour time exit
    bool use_pct_threshold;    // Percentage-based spacing
};

struct TestResult {
    std::string name;
    double final_balance;
    double return_x;
    double max_dd_pct;
    int total_trades;
    int time_exits;
    double total_swap;
    bool stopped_out;
};

class ImprovedFillUpStrategy {
public:
    ImprovedFillUpStrategy(double survive_pct, double base_spacing,
                           bool use_half_sizing, bool use_time_exit, bool use_pct_threshold)
        : survive_pct_(survive_pct),
          base_spacing_(base_spacing),
          use_half_sizing_(use_half_sizing),
          use_time_exit_(use_time_exit),
          use_pct_threshold_(use_pct_threshold),
          pct_threshold_(0.0003),  // 0.03% of price
          time_exit_hours_(4),
          time_exit_count_(0) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        double mid = (tick.bid + tick.ask) / 2.0;
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_timestamp_ = tick.timestamp;

        // Calculate effective spacing
        double spacing = base_spacing_;
        if (use_pct_threshold_) {
            spacing = mid * pct_threshold_;  // e.g., 0.03% of $3500 = $1.05
        }

        // Check for 4-hour time exits
        if (use_time_exit_) {
            CheckTimeExits(tick, engine);
        }

        // Get current positions
        double lowest_buy = DBL_MAX;
        double total_volume = 0;
        int pos_count = 0;

        for (const Trade* t : engine.GetOpenPositions()) {
            if (t->direction == "BUY") {
                lowest_buy = std::min(lowest_buy, t->entry_price);
                total_volume += t->lot_size;
                pos_count++;
            }
        }

        // Calculate lot size
        double equity = engine.GetEquity();
        double balance = engine.GetBalance();
        double survive_amount = balance * (survive_pct_ / 100.0);
        double available = equity - survive_amount;

        if (available <= 0) return;

        // Base lot calculation
        double margin_per_lot = (mid * 100.0) / 500.0;  // contract_size=100, leverage=500
        double max_lots = available / margin_per_lot;
        double lot_size = std::max(0.01, std::min(max_lots * 0.1, 10.0));

        // Apply 50% sizing if enabled
        if (use_half_sizing_) {
            lot_size *= 0.5;
        }
        lot_size = std::max(0.01, lot_size);

        // Check spacing condition
        bool should_open = false;
        if (pos_count == 0) {
            should_open = true;
        } else if (lowest_buy >= tick.ask + spacing) {
            should_open = true;
        }

        if (should_open) {
            double tp = tick.ask + tick.spread() + spacing;
            engine.OpenMarketOrder("BUY", lot_size, 0.0, tp);
        }
    }

    int GetTimeExitCount() const { return time_exit_count_; }

private:
    void CheckTimeExits(const Tick& tick, TickBasedEngine& engine) {
        // Parse current timestamp to get total minutes
        // Format: "2025.01.02 19:30:45.123"
        int current_minutes = ParseTimestampToMinutes(tick.timestamp);

        auto positions = engine.GetOpenPositions();
        std::vector<Trade*> to_close;

        for (Trade* t : positions) {
            int entry_minutes = ParseTimestampToMinutes(t->entry_time);
            int hold_minutes = current_minutes - entry_minutes;

            // Handle day wrap
            if (hold_minutes < 0) hold_minutes += 24 * 60;

            if (hold_minutes >= time_exit_hours_ * 60) {
                to_close.push_back(t);
            }
        }

        for (Trade* t : to_close) {
            engine.ClosePosition(t, "Time Exit 4h");
            time_exit_count_++;
        }
    }

    int ParseTimestampToMinutes(const std::string& ts) {
        // "2025.01.02 19:30:45.123" -> extract day, hour, minute
        if (ts.length() < 16) return 0;

        int day = std::stoi(ts.substr(8, 2));
        int hour = std::stoi(ts.substr(11, 2));
        int minute = std::stoi(ts.substr(14, 2));

        return day * 24 * 60 + hour * 60 + minute;
    }

    double survive_pct_;
    double base_spacing_;
    bool use_half_sizing_;
    bool use_time_exit_;
    bool use_pct_threshold_;
    double pct_threshold_;
    int time_exit_hours_;
    int time_exit_count_;

    double current_ask_ = 0;
    double current_bid_ = 0;
    std::string current_timestamp_;
};

TestResult RunTest(const TestConfig& cfg) {
    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = 10000.0;
    config.account_currency = "USD";
    config.contract_size = 100.0;
    config.leverage = 500.0;
    config.margin_rate = 1.0;
    config.pip_size = 0.01;
    config.swap_long = -66.99;
    config.swap_short = 41.2;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = "2025.01.01";
    config.end_date = "2025.12.30";
    config.tick_data_config = tick_config;

    TestResult result;
    result.name = cfg.name;
    result.stopped_out = false;
    result.time_exits = 0;

    try {
        TickBasedEngine engine(config);
        ImprovedFillUpStrategy strategy(
            13.0,   // survive_pct
            1.0,    // base_spacing
            cfg.use_half_sizing,
            cfg.use_time_exit,
            cfg.use_pct_threshold
        );

        double peak = config.initial_balance;
        double max_dd = 0;

        engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);

            double eq = eng.GetEquity();
            if (eq > peak) peak = eq;
            double dd = (peak - eq) / peak * 100;
            if (dd > max_dd) max_dd = dd;
        });

        auto res = engine.GetResults();
        result.final_balance = res.final_balance;
        result.return_x = res.final_balance / config.initial_balance;
        result.max_dd_pct = max_dd;
        result.total_trades = res.total_trades;
        result.total_swap = res.total_swap_charged;
        result.time_exits = strategy.GetTimeExitCount();

        if (res.final_balance < config.initial_balance * 0.1) {
            result.stopped_out = true;
        }

    } catch (const std::exception& e) {
        result.final_balance = 0;
        result.return_x = 0;
        result.stopped_out = true;
    }

    return result;
}

int main() {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n" << std::string(100, '=') << std::endl;
    std::cout << "COMBINED IMPROVEMENTS TEST" << std::endl;
    std::cout << "Testing: 50% Sizing, 4h Time Exit, Percentage Threshold" << std::endl;
    std::cout << std::string(100, '=') << std::endl;

    // Define all 8 combinations
    std::vector<TestConfig> configs = {
        {"Baseline",           false, false, false},
        {"50% Sizing",         true,  false, false},
        {"4h Exit",            false, true,  false},
        {"% Threshold",        false, false, true},
        {"50% + 4h",           true,  true,  false},
        {"50% + %Thresh",      true,  false, true},
        {"4h + %Thresh",       false, true,  true},
        {"ALL THREE",          true,  true,  true}
    };

    std::vector<TestResult> results;

    std::cout << "\nRunning 8 test configurations...\n" << std::endl;

    for (size_t i = 0; i < configs.size(); i++) {
        std::cout << "Testing [" << (i+1) << "/8]: " << configs[i].name << "..." << std::flush;
        TestResult r = RunTest(configs[i]);
        results.push_back(r);
        std::cout << " " << r.return_x << "x" << (r.stopped_out ? " STOPPED" : "") << std::endl;
    }

    // Print results table
    std::cout << "\n" << std::string(100, '=') << std::endl;
    std::cout << "RESULTS SUMMARY" << std::endl;
    std::cout << std::string(100, '=') << std::endl;

    std::cout << std::setw(20) << "Configuration"
              << std::setw(12) << "Return"
              << std::setw(12) << "Max DD"
              << std::setw(10) << "Trades"
              << std::setw(12) << "TimeExits"
              << std::setw(12) << "Swap"
              << std::setw(12) << "Status" << std::endl;
    std::cout << std::string(100, '-') << std::endl;

    for (const auto& r : results) {
        std::cout << std::setw(20) << r.name
                  << std::setw(11) << r.return_x << "x"
                  << std::setw(11) << r.max_dd_pct << "%"
                  << std::setw(10) << r.total_trades
                  << std::setw(12) << r.time_exits
                  << std::setw(11) << "$" << std::setprecision(0) << r.total_swap
                  << std::setw(12) << (r.stopped_out ? "STOPPED" : "OK") << std::endl;
        std::cout << std::setprecision(2);
    }

    // Find best configuration
    std::cout << "\n" << std::string(100, '-') << std::endl;
    std::cout << "ANALYSIS" << std::endl;
    std::cout << std::string(100, '-') << std::endl;

    // Best return (non-stopped)
    double best_return = 0;
    std::string best_return_name;
    for (const auto& r : results) {
        if (!r.stopped_out && r.return_x > best_return) {
            best_return = r.return_x;
            best_return_name = r.name;
        }
    }

    // Lowest DD (non-stopped, return > 1.0)
    double lowest_dd = 100;
    std::string lowest_dd_name;
    for (const auto& r : results) {
        if (!r.stopped_out && r.return_x > 1.0 && r.max_dd_pct < lowest_dd) {
            lowest_dd = r.max_dd_pct;
            lowest_dd_name = r.name;
        }
    }

    // Best risk-adjusted (return / dd)
    double best_ratio = 0;
    std::string best_ratio_name;
    for (const auto& r : results) {
        if (!r.stopped_out && r.max_dd_pct > 0) {
            double ratio = r.return_x / (r.max_dd_pct / 100.0);
            if (ratio > best_ratio) {
                best_ratio = ratio;
                best_ratio_name = r.name;
            }
        }
    }

    std::cout << "Best Return:        " << best_return_name << " (" << best_return << "x)" << std::endl;
    std::cout << "Lowest Drawdown:    " << lowest_dd_name << " (" << lowest_dd << "%)" << std::endl;
    std::cout << "Best Risk-Adjusted: " << best_ratio_name << " (Return/DD = " << best_ratio << ")" << std::endl;

    return 0;
}
