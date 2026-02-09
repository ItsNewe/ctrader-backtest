/**
 * Trend Lookback Period Sweep
 *
 * Tests different lookback periods for trend measurement:
 * - Short: 4h, 12h, 24h
 * - Medium: 3 days, 7 days
 * - Long: 14 days, 30 days
 *
 * Hypothesis: Longer lookback should work better because
 * the quarterly trend (not daily) determines optimal spacing.
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>

using namespace backtest;

// Modified strategy that allows configurable trend lookback
class TrendAdaptiveStrategy {
public:
    TrendAdaptiveStrategy(double survive_pct, double base_spacing,
                          double min_volume, double max_volume,
                          double contract_size, double leverage,
                          double trend_lookback_hours)
        : survive_pct_(survive_pct),
          base_spacing_(base_spacing),
          min_volume_(min_volume),
          max_volume_(max_volume),
          contract_size_(contract_size),
          leverage_(leverage),
          trend_lookback_hours_(trend_lookback_hours),
          lowest_buy_(DBL_MAX),
          highest_buy_(DBL_MIN),
          volume_of_open_trades_(0.0),
          current_spacing_(base_spacing),
          trend_start_price_(0.0),
          ticks_processed_(0),
          spacing_changes_(0),
          current_equity_(0.0),
          peak_equity_(0.0)
    {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_bid_ = tick.bid;
        current_ask_ = tick.ask;
        current_equity_ = engine.GetEquity();
        ticks_processed_++;

        if (peak_equity_ == 0.0) peak_equity_ = current_equity_;
        if (current_equity_ > peak_equity_) peak_equity_ = current_equity_;

        // Update trend-adaptive spacing
        UpdateTrendSpacing();

        // Process existing positions
        Iterate(engine);

        // Open new positions
        OpenNew(engine);
    }

    double GetCurrentSpacing() const { return current_spacing_; }
    int GetSpacingChanges() const { return spacing_changes_; }
    double GetTrendStrength() const {
        if (trend_start_price_ <= 0) return 0;
        return (current_bid_ - trend_start_price_) / trend_start_price_ * 100.0;
    }

private:
    double survive_pct_;
    double base_spacing_;
    double min_volume_;
    double max_volume_;
    double contract_size_;
    double leverage_;
    double trend_lookback_hours_;

    double lowest_buy_;
    double highest_buy_;
    double volume_of_open_trades_;
    double current_spacing_;
    double trend_start_price_;
    long ticks_processed_;
    int spacing_changes_;

    double current_bid_;
    double current_ask_;
    double current_equity_;
    double peak_equity_;

    void UpdateTrendSpacing() {
        long ticks_per_hour = 720000;
        long trend_ticks = (long)(trend_lookback_hours_ * ticks_per_hour);

        // Reset trend start price at lookback interval
        if (trend_ticks > 0 && ticks_processed_ % trend_ticks == 0) {
            trend_start_price_ = current_bid_;
        }

        if (trend_start_price_ <= 0) {
            trend_start_price_ = current_bid_;
            return;
        }

        // Calculate trend strength
        double trend_pct = (current_bid_ - trend_start_price_) / trend_start_price_ * 100.0;
        double abs_trend = std::abs(trend_pct);

        // Determine spacing based on trend strength
        double new_spacing;
        if (abs_trend >= 10.0) {
            double trend_factor = std::min(1.0, (abs_trend - 10.0) / 10.0);
            new_spacing = 0.50 - trend_factor * 0.30;
            new_spacing = std::max(0.20, new_spacing);
        } else if (abs_trend <= 3.0) {
            new_spacing = 5.0;
        } else if (abs_trend <= 6.0) {
            double trend_factor = (abs_trend - 3.0) / 3.0;
            new_spacing = 5.0 - trend_factor * 3.5;
        } else {
            double trend_factor = (abs_trend - 6.0) / 4.0;
            new_spacing = 1.50 - trend_factor * 1.0;
        }

        if (std::abs(new_spacing - current_spacing_) > 0.05) {
            current_spacing_ = new_spacing;
            spacing_changes_++;
        }
    }

    void Iterate(TickBasedEngine& engine) {
        auto positions = engine.GetOpenPositions();
        volume_of_open_trades_ = 0.0;
        lowest_buy_ = DBL_MAX;
        highest_buy_ = DBL_MIN;

        for (const auto* pos : positions) {
            if (pos->direction == "BUY") {
                volume_of_open_trades_ += pos->lot_size;
                lowest_buy_ = std::min(lowest_buy_, pos->entry_price);
                highest_buy_ = std::max(highest_buy_, pos->entry_price);
            }
        }
    }

    void OpenNew(TickBasedEngine& engine) {
        double equity = engine.GetEquity();
        double balance = engine.GetBalance();

        // Calculate max position value based on survive percentage
        double max_position_value = equity * leverage_ / (survive_pct_ / 100.0 * current_bid_);
        double current_position_value = volume_of_open_trades_ * contract_size_;
        double available_value = max_position_value - current_position_value;

        if (available_value <= 0) return;

        // Check if we should open a new position
        bool should_open = false;
        double target_price = 0;

        if (lowest_buy_ == DBL_MAX) {
            // No positions - open first
            should_open = true;
            target_price = current_ask_;
        } else if (current_ask_ <= lowest_buy_ - current_spacing_) {
            // Price dropped - open lower
            should_open = true;
            target_price = current_ask_;
        }

        if (should_open) {
            // Calculate lot size
            double lot_size = (equity * 0.01) / (current_spacing_ * contract_size_);
            lot_size = std::max(min_volume_, std::min(max_volume_, lot_size));

            // Check if we have room
            double position_value = lot_size * contract_size_;
            if (position_value <= available_value) {
                double tp = target_price + current_spacing_;
                engine.OpenMarketOrder("BUY", lot_size, 0, tp);
            }
        }
    }
};

struct TestResult {
    double lookback_hours;
    double return_multiple;
    double max_dd_pct;
    int total_trades;
    double avg_spacing;
    int spacing_changes;
};

TestResult RunTest(double lookback_hours, const std::string& data_path,
                   const std::string& start_date, const std::string& end_date,
                   double initial_balance) {
    TestResult result;
    result.lookback_hours = lookback_hours;

    TickDataConfig tick_config;
    tick_config.file_path = data_path;
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = initial_balance;
    config.account_currency = "USD";
    config.contract_size = 100.0;
    config.leverage = 500.0;
    config.margin_rate = 1.0;
    config.pip_size = 0.01;
    config.swap_long = -66.99;
    config.swap_short = 41.2;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = start_date;
    config.end_date = end_date;
    config.tick_data_config = tick_config;
    config.verbose = false;

    double spacing_sum = 0;
    int spacing_samples = 0;

    try {
        TickBasedEngine engine(config);

        TrendAdaptiveStrategy strategy(
            13.0,           // survive_pct
            1.5,            // base_spacing
            0.01,           // min_volume
            10.0,           // max_volume
            100.0,          // contract_size
            500.0,          // leverage
            lookback_hours  // trend_lookback_hours
        );

        engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
            spacing_samples++;
            if (spacing_samples % 10000 == 0) {
                spacing_sum += strategy.GetCurrentSpacing();
            }
        });

        auto res = engine.GetResults();
        result.return_multiple = res.final_balance / initial_balance;
        double peak = res.final_balance + res.max_drawdown;
        result.max_dd_pct = (peak > 0) ? (res.max_drawdown / peak * 100.0) : 0;
        result.total_trades = res.total_trades;
        result.avg_spacing = (spacing_samples > 0) ? spacing_sum / (spacing_samples / 10000) : 1.5;
        result.spacing_changes = strategy.GetSpacingChanges();

    } catch (...) {
        result.return_multiple = 0;
        result.max_dd_pct = 100;
    }

    return result;
}

int main() {
    std::cout << "=== Trend Lookback Period Sweep ===" << std::endl;
    std::cout << "Finding optimal lookback for trend measurement" << std::endl;
    std::cout << std::endl;

    std::string data_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    double initial_balance = 10000.0;

    // Lookback periods to test (in hours)
    std::vector<double> lookbacks = {
        4.0,      // 4 hours
        12.0,     // 12 hours
        24.0,     // 1 day
        72.0,     // 3 days
        168.0,    // 7 days (1 week)
        336.0,    // 14 days (2 weeks)
        720.0     // 30 days (1 month)
    };

    std::cout << "Testing Full Year 2025..." << std::endl;
    std::cout << std::endl;

    std::cout << std::setw(12) << "Lookback"
              << std::setw(10) << "Return"
              << std::setw(10) << "MaxDD%"
              << std::setw(10) << "Trades"
              << std::setw(12) << "AvgSpacing"
              << std::setw(12) << "Changes"
              << std::endl;
    std::cout << std::string(66, '-') << std::endl;

    std::vector<TestResult> results;

    for (double lb : lookbacks) {
        std::string lb_name;
        if (lb < 24) lb_name = std::to_string((int)lb) + "h";
        else if (lb < 168) lb_name = std::to_string((int)(lb/24)) + "d";
        else lb_name = std::to_string((int)(lb/168)) + "w";

        std::cout << "Testing " << lb_name << "..." << std::flush;

        auto result = RunTest(lb, data_path, "2025.01.01", "2025.12.30", initial_balance);
        results.push_back(result);

        std::cout << "\r" << std::setw(12) << lb_name
                  << std::setw(9) << std::fixed << std::setprecision(2) << result.return_multiple << "x"
                  << std::setw(9) << std::setprecision(0) << result.max_dd_pct << "%"
                  << std::setw(10) << result.total_trades
                  << std::setw(12) << std::setprecision(2) << "$" << result.avg_spacing
                  << std::setw(12) << result.spacing_changes
                  << std::endl;
    }

    // Find best
    double best_return = 0;
    double best_lookback = 0;
    for (const auto& r : results) {
        if (r.return_multiple > best_return) {
            best_return = r.return_multiple;
            best_lookback = r.lookback_hours;
        }
    }

    std::cout << std::endl;
    std::cout << "=== RESULT ===" << std::endl;
    std::cout << "Best lookback: ";
    if (best_lookback < 24) std::cout << best_lookback << " hours";
    else if (best_lookback < 168) std::cout << (best_lookback/24) << " days";
    else std::cout << (best_lookback/168) << " weeks";
    std::cout << " -> " << std::fixed << std::setprecision(2) << best_return << "x return" << std::endl;

    std::cout << std::endl;
    std::cout << "For comparison:" << std::endl;
    std::cout << "  Fixed $0.30:    8.80x" << std::endl;
    std::cout << "  Vol-Adaptive:   7.05x" << std::endl;

    return 0;
}
