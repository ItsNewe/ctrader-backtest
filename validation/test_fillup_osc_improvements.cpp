/**
 * FillUpOscillation Improvements Test
 *
 * Tests actual FillUpOscillation strategy with combined improvements:
 * 1. 50% Position Sizing
 * 2. 4-Hour Time Exit
 * 3. Percentage-Based Threshold (price-normalized spacing)
 *
 * Compares ADAPTIVE_SPACING mode with various improvement combinations.
 */

#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <deque>
#include <cmath>
#include <cfloat>
#include <vector>
#include <map>

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
    int spacing_changes;
};

/**
 * Extended FillUpOscillation with improvement options
 * Based on the original class but with additional features
 */
class FillUpOscillationExtended {
public:
    FillUpOscillationExtended(double survive_pct, double base_spacing,
                              double min_volume, double max_volume,
                              double contract_size, double leverage,
                              bool use_half_sizing, bool use_time_exit, bool use_pct_threshold,
                              double volatility_lookback_hours = 4.0)
        : survive_pct_(survive_pct),
          base_spacing_(base_spacing),
          min_volume_(min_volume),
          max_volume_(max_volume),
          contract_size_(contract_size),
          leverage_(leverage),
          use_half_sizing_(use_half_sizing),
          use_time_exit_(use_time_exit),
          use_pct_threshold_(use_pct_threshold),
          pct_threshold_(0.0003),  // 0.03% of price
          time_exit_hours_(4),
          time_exit_count_(0),
          volatility_lookback_hours_(volatility_lookback_hours),
          lowest_buy_(DBL_MAX),
          highest_buy_(DBL_MIN),
          volume_of_open_trades_(0.0),
          current_ask_(0.0),
          current_bid_(0.0),
          current_spread_(0.0),
          current_equity_(0.0),
          current_balance_(0.0),
          peak_equity_(0.0),
          current_spacing_(base_spacing),
          recent_high_(0.0),
          recent_low_(DBL_MAX),
          hour_start_price_(0.0),
          ticks_processed_(0),
          adaptive_spacing_changes_(0)
    {
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_spread_ = tick.spread();
        current_equity_ = engine.GetEquity();
        current_balance_ = engine.GetBalance();
        current_timestamp_ = tick.timestamp;
        ticks_processed_++;

        // Initialize
        if (peak_equity_ == 0.0) {
            peak_equity_ = current_equity_;
            hour_start_price_ = current_bid_;
        }

        // Update peak
        if (current_equity_ > peak_equity_) {
            peak_equity_ = current_equity_;
        }

        // Track volatility
        UpdateVolatility(tick);

        // Check for time exits first (if enabled)
        if (use_time_exit_) {
            CheckTimeExits(tick, engine);
        }

        // Update spacing (adaptive or percentage-based)
        UpdateSpacing();

        // Process positions
        Iterate(engine);

        // Open new positions
        OpenNew(engine);
    }

    int GetTimeExitCount() const { return time_exit_count_; }
    int GetAdaptiveSpacingChanges() const { return adaptive_spacing_changes_; }
    double GetCurrentSpacing() const { return current_spacing_; }

private:
    void UpdateVolatility(const Tick& /*tick*/) {
        // Track hourly prices for volatility
        long ticks_per_hour = 720000;

        // Track recent high/low for spacing volatility
        long volatility_ticks = (long)(volatility_lookback_hours_ * ticks_per_hour);
        if (volatility_ticks > 0 && ticks_processed_ % volatility_ticks == 0) {
            recent_high_ = current_bid_;
            recent_low_ = current_bid_;
        }
        recent_high_ = std::max(recent_high_, current_bid_);
        recent_low_ = std::min(recent_low_, current_bid_);
    }

    void UpdateSpacing() {
        double new_spacing = base_spacing_;

        if (use_pct_threshold_) {
            // Percentage-based: spacing = price * 0.03%
            new_spacing = current_bid_ * pct_threshold_;
        } else {
            // Adaptive: based on recent volatility
            double range = recent_high_ - recent_low_;
            if (range > 0 && recent_high_ > 0) {
                // Volatility ratio: current range vs typical ($10 = normal for gold)
                double vol_ratio = range / 10.0;
                vol_ratio = std::max(0.5, std::min(3.0, vol_ratio));
                new_spacing = base_spacing_ * vol_ratio;
                new_spacing = std::max(0.5, std::min(5.0, new_spacing));
            }
        }

        if (std::abs(new_spacing - current_spacing_) > 0.05) {
            current_spacing_ = new_spacing;
            adaptive_spacing_changes_++;
        }
    }

    void CheckTimeExits(const Tick& tick, TickBasedEngine& engine) {
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
        if (ts.length() < 16) return 0;
        int day = std::stoi(ts.substr(8, 2));
        int hour = std::stoi(ts.substr(11, 2));
        int minute = std::stoi(ts.substr(14, 2));
        return day * 24 * 60 + hour * 60 + minute;
    }

    void Iterate(TickBasedEngine& engine) {
        lowest_buy_ = DBL_MAX;
        highest_buy_ = DBL_MIN;
        volume_of_open_trades_ = 0.0;

        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->direction == "BUY") {
                volume_of_open_trades_ += trade->lot_size;
                lowest_buy_ = std::min(lowest_buy_, trade->entry_price);
                highest_buy_ = std::max(highest_buy_, trade->entry_price);
            }
        }
    }

    double CalculateLotSize(TickBasedEngine& engine, int positions_total) {
        double used_margin = 0.0;
        for (const Trade* trade : engine.GetOpenPositions()) {
            used_margin += trade->lot_size * contract_size_ * trade->entry_price / leverage_;
        }

        double margin_stop_out = 20.0;

        double end_price = (positions_total == 0)
            ? current_ask_ * ((100.0 - survive_pct_) / 100.0)
            : highest_buy_ * ((100.0 - survive_pct_) / 100.0);

        double distance = current_ask_ - end_price;
        double number_of_trades = std::floor(distance / current_spacing_);
        if (number_of_trades <= 0) number_of_trades = 1;

        // Calculate base lot size
        double equity_at_target = current_equity_ - volume_of_open_trades_ * distance * contract_size_;
        if (used_margin > 0 && (equity_at_target / used_margin * 100.0) < margin_stop_out) {
            return 0.0;
        }

        double trade_size = min_volume_;
        double d_equity = contract_size_ * trade_size * current_spacing_ * (number_of_trades * (number_of_trades + 1) / 2);
        double d_margin = number_of_trades * trade_size * contract_size_ / leverage_;

        // Find multiplier
        double max_mult = max_volume_ / min_volume_;
        for (double mult = max_mult; mult >= 1.0; mult -= 0.1) {
            double test_equity = equity_at_target - mult * d_equity;
            double test_margin = used_margin + mult * d_margin;
            if (test_margin > 0 && (test_equity / test_margin * 100.0) > margin_stop_out) {
                trade_size = mult * min_volume_;
                break;
            }
        }

        // Apply 50% sizing if enabled
        if (use_half_sizing_) {
            trade_size *= 0.5;
        }

        return std::min(std::max(trade_size, min_volume_), max_volume_);
    }

    bool Open(double lots, TickBasedEngine& engine) {
        if (lots < min_volume_) return false;

        double final_lots = std::min(lots, max_volume_);
        final_lots = std::round(final_lots * 100.0) / 100.0;

        double tp = current_ask_ + current_spread_ + current_spacing_;
        Trade* trade = engine.OpenMarketOrder("BUY", final_lots, 0.0, tp);
        return (trade != nullptr);
    }

    void OpenNew(TickBasedEngine& engine) {
        int positions_total = engine.GetOpenPositions().size();

        if (positions_total == 0) {
            double lots = CalculateLotSize(engine, positions_total);
            if (Open(lots, engine)) {
                highest_buy_ = current_ask_;
                lowest_buy_ = current_ask_;
            }
        } else {
            if (lowest_buy_ >= current_ask_ + current_spacing_) {
                double lots = CalculateLotSize(engine, positions_total);
                if (Open(lots, engine)) {
                    lowest_buy_ = current_ask_;
                }
            } else if (highest_buy_ <= current_ask_ - current_spacing_) {
                double lots = CalculateLotSize(engine, positions_total);
                if (Open(lots, engine)) {
                    highest_buy_ = current_ask_;
                }
            }
        }
    }

    // Configuration
    double survive_pct_;
    double base_spacing_;
    double min_volume_;
    double max_volume_;
    double contract_size_;
    double leverage_;
    bool use_half_sizing_;
    bool use_time_exit_;
    bool use_pct_threshold_;
    double pct_threshold_;
    int time_exit_hours_;
    int time_exit_count_;
    double volatility_lookback_hours_;

    // Position tracking
    double lowest_buy_;
    double highest_buy_;
    double volume_of_open_trades_;

    // Market state
    double current_ask_;
    double current_bid_;
    double current_spread_;
    double current_equity_;
    double current_balance_;
    double peak_equity_;
    std::string current_timestamp_;

    // Adaptive spacing
    double current_spacing_;
    double recent_high_;
    double recent_low_;
    double hour_start_price_;
    long ticks_processed_;
    int adaptive_spacing_changes_;
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
    result.spacing_changes = 0;

    try {
        TickBasedEngine engine(config);
        FillUpOscillationExtended strategy(
            13.0,   // survive_pct (same as original FillUpOscillation tests)
            1.5,    // base_spacing (optimal from previous tests)
            0.01,   // min_volume
            10.0,   // max_volume
            100.0,  // contract_size
            500.0,  // leverage
            cfg.use_half_sizing,
            cfg.use_time_exit,
            cfg.use_pct_threshold,
            4.0     // volatility_lookback_hours
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
        result.spacing_changes = strategy.GetAdaptiveSpacingChanges();

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
    std::cout << "\n" << std::string(110, '=') << std::endl;
    std::cout << "FILLUP OSCILLATION - COMBINED IMPROVEMENTS TEST" << std::endl;
    std::cout << "Testing: 50% Sizing, 4h Time Exit, Percentage Threshold" << std::endl;
    std::cout << "Base: survive=13%, spacing=$1.50, lookback=4h (ADAPTIVE_SPACING equivalent)" << std::endl;
    std::cout << std::string(110, '=') << std::endl;

    // Define all 8 combinations
    std::vector<TestConfig> configs = {
        {"Adaptive (Base)",    false, false, false},
        {"+ 50% Sizing",       true,  false, false},
        {"+ 4h Exit",          false, true,  false},
        {"+ % Threshold",      false, false, true},
        {"+ 50% + 4h",         true,  true,  false},
        {"+ 50% + %Thresh",    true,  false, true},
        {"+ 4h + %Thresh",     false, true,  true},
        {"+ ALL THREE",        true,  true,  true}
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
    std::cout << "\n" << std::string(110, '=') << std::endl;
    std::cout << "RESULTS SUMMARY" << std::endl;
    std::cout << std::string(110, '=') << std::endl;

    std::cout << std::setw(20) << "Configuration"
              << std::setw(10) << "Return"
              << std::setw(10) << "Max DD"
              << std::setw(10) << "Trades"
              << std::setw(12) << "TimeExits"
              << std::setw(12) << "SpaceChg"
              << std::setw(12) << "Swap"
              << std::setw(10) << "Status" << std::endl;
    std::cout << std::string(110, '-') << std::endl;

    for (const auto& r : results) {
        std::cout << std::setw(20) << r.name
                  << std::setw(9) << r.return_x << "x"
                  << std::setw(9) << r.max_dd_pct << "%"
                  << std::setw(10) << r.total_trades
                  << std::setw(12) << r.time_exits
                  << std::setw(12) << r.spacing_changes
                  << std::setw(11) << "$" << std::setprecision(0) << r.total_swap
                  << std::setw(10) << (r.stopped_out ? "STOPPED" : "OK") << std::endl;
        std::cout << std::setprecision(2);
    }

    // Find best configuration
    std::cout << "\n" << std::string(110, '-') << std::endl;
    std::cout << "ANALYSIS" << std::endl;
    std::cout << std::string(110, '-') << std::endl;

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

    // Comparison notes
    std::cout << "\n" << std::string(110, '-') << std::endl;
    std::cout << "COMPARISON NOTES" << std::endl;
    std::cout << std::string(110, '-') << std::endl;
    std::cout << "This test uses the full FillUpOscillation logic with adaptive spacing (vol-based)." << std::endl;
    std::cout << "The '% Threshold' option replaces vol-based spacing with price-percentage spacing." << std::endl;
    std::cout << "At $3500 gold: 0.03% = $1.05 spacing (vs fixed $1.50 base)" << std::endl;

    return 0;
}
