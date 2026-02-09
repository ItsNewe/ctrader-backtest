/**
 * CycleSurfer EA Backtest
 * Based on Ehlers Roofing Filter - cycle detection strategy
 */

#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <deque>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace backtest;

class CycleSurferStrategy {
public:
    CycleSurferStrategy(double lot_size = 0.1, int hp_period = 60, int ss_period = 10)
        : lot_size_(lot_size), hp_period_(hp_period), ss_period_(ss_period),
          current_position_(0), bars_needed_(hp_period * 2) {
        // Pre-calculate filter constants
        // High Pass constants
        double angle_hp = 0.707 * 2.0 * M_PI / hp_period_;
        alpha1_ = (cos(angle_hp) + sin(angle_hp) - 1.0) / cos(angle_hp);
        c_hp_1_ = (1.0 - alpha1_ / 2.0) * (1.0 - alpha1_ / 2.0);
        c_hp_2_ = 2.0 * (1.0 - alpha1_);
        c_hp_3_ = -(1.0 - alpha1_) * (1.0 - alpha1_);

        // Super Smoother constants
        double angle_ss = 1.414 * M_PI / ss_period_;
        double a1 = exp(-angle_ss);
        double b1 = 2.0 * a1 * cos(angle_ss);
        c_ss_2_ = b1;
        c_ss_3_ = -a1 * a1;
        c_ss_1_ = 1.0 - c_ss_2_ - c_ss_3_;
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        double mid = (tick.bid + tick.ask) / 2.0;
        tick_count_++;

        // Extract hour from timestamp (format: "2025.01.02 19:00:00.123")
        int current_hour = 0;
        if (tick.timestamp.length() >= 13) {
            current_hour = std::stoi(tick.timestamp.substr(11, 2));
        }

        // Build hourly bars - detect hour change
        if (last_hour_ < 0) {
            last_hour_ = current_hour;
            bar_open_ = mid;
            bar_high_ = mid;
            bar_low_ = mid;
        }

        bar_high_ = std::max(bar_high_, mid);
        bar_low_ = std::min(bar_low_, mid);

        // Check if hour changed
        if (current_hour != last_hour_) {
            // Close the bar
            double bar_close = mid;
            closes_.push_back(bar_close);

            // Keep only what we need
            if ((int)closes_.size() > bars_needed_) {
                closes_.pop_front();
            }

            // Start new bar
            last_hour_ = current_hour;
            bar_open_ = mid;
            bar_high_ = mid;
            bar_low_ = mid;

            // Process signals if we have enough data
            if ((int)closes_.size() >= bars_needed_) {
                ProcessSignal(tick, engine);
            }
        }
    }

    int GetTradeCount() const { return trade_count_; }
    int GetCurrentPosition() const { return current_position_; }

private:
    void ProcessSignal(const Tick& tick, TickBasedEngine& engine) {
        int n = closes_.size();

        // Calculate Roofing Filter
        std::vector<double> hp(n, 0.0);
        std::vector<double> ss(n, 0.0);

        for (int i = 2; i < n; i++) {
            // High Pass Filter
            hp[i] = c_hp_1_ * (closes_[i] - 2*closes_[i-1] + closes_[i-2])
                  + c_hp_2_ * hp[i-1]
                  + c_hp_3_ * hp[i-2];

            // Super Smoother
            ss[i] = c_ss_1_ * (hp[i] + hp[i-1]) / 2.0
                  + c_ss_2_ * ss[i-1]
                  + c_ss_3_ * ss[i-2];
        }

        // Get cycle values (most recent completed bars)
        double cycle_curr = ss[n-1];
        double cycle_prev = ss[n-2];
        double cycle_old = ss[n-3];

        // Detect signals
        bool signal_buy = (cycle_curr > cycle_prev) && (cycle_prev < cycle_old);
        bool signal_sell = (cycle_curr < cycle_prev) && (cycle_prev > cycle_old);

        // Execute trades
        if (signal_buy && current_position_ <= 0) {
            // Close short if exists
            CloseAllPositions(engine);
            // Open long
            engine.OpenMarketOrder("BUY", lot_size_, 0, 0);
            current_position_ = 1;
            trade_count_++;
        }
        else if (signal_sell && current_position_ >= 0) {
            // Close long if exists
            CloseAllPositions(engine);
            // Open short
            engine.OpenMarketOrder("SELL", lot_size_, 0, 0);
            current_position_ = -1;
            trade_count_++;
        }
    }

    void CloseAllPositions(TickBasedEngine& engine) {
        auto positions = engine.GetOpenPositions();
        for (Trade* t : positions) {
            engine.ClosePosition(t, "Cycle Signal");
        }
    }

    double lot_size_;
    int hp_period_;
    int ss_period_;
    int current_position_;  // 1=long, -1=short, 0=flat
    int bars_needed_;
    int trade_count_ = 0;
    long tick_count_ = 0;

    // Filter constants
    double alpha1_, c_hp_1_, c_hp_2_, c_hp_3_;
    double c_ss_1_, c_ss_2_, c_ss_3_;

    // Bar building
    int last_hour_ = -1;
    double bar_open_ = 0, bar_high_ = 0, bar_low_ = 0;
    std::deque<double> closes_;
};

int main() {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "CYCLESURFER EA BACKTEST - XAUUSD 2025" << std::endl;
    std::cout << "Ehlers Roofing Filter Strategy" << std::endl;
    std::cout << std::string(70, '=') << std::endl;

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

    try {
        TickBasedEngine engine(config);
        CycleSurferStrategy strategy(0.1, 60, 10);  // 0.1 lot, HP=60, SS=10

        double peak = config.initial_balance;
        double max_dd = 0;

        std::cout << "\nRunning backtest..." << std::endl;

        engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);

            double eq = eng.GetEquity();
            if (eq > peak) peak = eq;
            double dd = (peak - eq) / peak * 100;
            if (dd > max_dd) max_dd = dd;
        });

        auto results = engine.GetResults();

        std::cout << "\n" << std::string(70, '-') << std::endl;
        std::cout << "RESULTS" << std::endl;
        std::cout << std::string(70, '-') << std::endl;
        std::cout << "Initial Balance:  $" << config.initial_balance << std::endl;
        std::cout << "Final Balance:    $" << results.final_balance << std::endl;
        std::cout << "Return:           " << (results.final_balance / config.initial_balance) << "x" << std::endl;
        std::cout << "Total P/L:        $" << (results.final_balance - config.initial_balance) << std::endl;
        std::cout << "Max Drawdown:     " << max_dd << "%" << std::endl;
        std::cout << "Total Trades:     " << strategy.GetTradeCount() << std::endl;
        std::cout << "Total Swap:       $" << results.total_swap_charged << std::endl;
        std::cout << "Final Position:   " << (strategy.GetCurrentPosition() == 1 ? "LONG" :
                                              strategy.GetCurrentPosition() == -1 ? "SHORT" : "FLAT") << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
