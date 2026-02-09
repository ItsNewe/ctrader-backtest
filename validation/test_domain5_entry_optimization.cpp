/**
 * Domain 5: Entry Optimization
 *
 * Test alternative entry methods:
 * - Current: Open when price moves by spacing
 * - Alternative 1: Wait for momentum reversal
 * - Alternative 2: Volume confirmation
 * - Alternative 3: Time-of-day filter
 */

#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <deque>
#include <cmath>
#include <cfloat>

using namespace backtest;

// Base strategy for comparison
class EntryStrategy {
public:
    virtual ~EntryStrategy() = default;
    virtual bool ShouldEnter(const Tick& tick, double lowest_buy, double highest_buy,
                             double spacing, int position_count) = 0;
    virtual const char* GetName() const = 0;
    virtual void Reset() {}
};

// Original: Always enter when price moves by spacing
class AlwaysEnterStrategy : public EntryStrategy {
public:
    bool ShouldEnter(const Tick& tick, double lowest_buy, double highest_buy,
                     double spacing, int position_count) override {
        if (position_count == 0) return true;
        return (lowest_buy >= tick.ask + spacing) || (highest_buy <= tick.ask - spacing);
    }
    const char* GetName() const override { return "Always"; }
};

// Momentum reversal: Only enter when short-term momentum reverses
class MomentumReversalStrategy : public EntryStrategy {
public:
    MomentumReversalStrategy() : was_falling_(false), tick_count_(0) {}

    bool ShouldEnter(const Tick& tick, double lowest_buy, double highest_buy,
                     double spacing, int position_count) override {
        tick_count_++;
        prices_.push_back(tick.bid);
        if (prices_.size() > 100) prices_.pop_front();

        if (prices_.size() < 50) return false;

        // Calculate short-term momentum
        double recent_avg = 0, older_avg = 0;
        for (size_t i = prices_.size() - 25; i < prices_.size(); i++) recent_avg += prices_[i];
        for (size_t i = prices_.size() - 50; i < prices_.size() - 25; i++) older_avg += prices_[i];
        recent_avg /= 25;
        older_avg /= 25;

        bool is_falling = recent_avg < older_avg;
        bool reversal = was_falling_ && !is_falling;  // Was falling, now rising
        was_falling_ = is_falling;

        if (position_count == 0) {
            return reversal || tick_count_ > 1000;  // Don't wait too long
        }

        bool price_condition = (lowest_buy >= tick.ask + spacing) || (highest_buy <= tick.ask - spacing);
        return price_condition && reversal;
    }

    const char* GetName() const override { return "MomentumReversal"; }

    void Reset() override {
        prices_.clear();
        was_falling_ = false;
        tick_count_ = 0;
    }

private:
    std::deque<double> prices_;
    bool was_falling_;
    long tick_count_;
};

// Spread filter: Only enter when spread is normal
class SpreadFilterStrategy : public EntryStrategy {
public:
    SpreadFilterStrategy() : avg_spread_(0.30), spread_samples_(0) {}

    bool ShouldEnter(const Tick& tick, double lowest_buy, double highest_buy,
                     double spacing, int position_count) override {
        double spread = tick.ask - tick.bid;

        // Update average spread
        spread_samples_++;
        avg_spread_ = avg_spread_ * 0.999 + spread * 0.001;

        // Only enter if spread is reasonable (< 2x average)
        if (spread > avg_spread_ * 2.0) return false;

        if (position_count == 0) return true;
        return (lowest_buy >= tick.ask + spacing) || (highest_buy <= tick.ask - spacing);
    }

    const char* GetName() const override { return "SpreadFilter"; }

    void Reset() override {
        avg_spread_ = 0.30;
        spread_samples_ = 0;
    }

private:
    double avg_spread_;
    long spread_samples_;
};

// Session filter: Only trade during high-activity hours
class SessionFilterStrategy : public EntryStrategy {
public:
    bool ShouldEnter(const Tick& tick, double lowest_buy, double highest_buy,
                     double spacing, int position_count) override {
        // Parse hour from timestamp
        int hour = 0;
        if (tick.timestamp.length() >= 13) {
            hour = std::stoi(tick.timestamp.substr(11, 2));
        }

        // Only trade during London/NY overlap and extended NY (8-20 UTC)
        if (hour < 8 || hour >= 20) return false;

        if (position_count == 0) return true;
        return (lowest_buy >= tick.ask + spacing) || (highest_buy <= tick.ask - spacing);
    }

    const char* GetName() const override { return "SessionFilter"; }
};

struct TestResult {
    std::string strategy_name;
    double final_balance;
    double return_x;
    double max_dd_pct;
    int total_trades;
};

TestResult RunTest(EntryStrategy& entry_strategy) {
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
    result.strategy_name = entry_strategy.GetName();

    try {
        TickBasedEngine engine(config);
        entry_strategy.Reset();

        double spacing = 1.0;
        double peak_equity = config.initial_balance;
        double max_dd_pct = 0;
        double lowest_buy = DBL_MAX, highest_buy = -DBL_MAX;

        engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
            double equity = eng.GetEquity();
            if (equity > peak_equity) peak_equity = equity;
            double dd = (peak_equity - equity) / peak_equity * 100.0;
            if (dd > max_dd_pct) max_dd_pct = dd;

            // Update position tracking
            lowest_buy = DBL_MAX;
            highest_buy = -DBL_MAX;
            double total_volume = 0;
            for (const Trade* trade : eng.GetOpenPositions()) {
                if (trade->direction == "BUY") {
                    lowest_buy = std::min(lowest_buy, trade->entry_price);
                    highest_buy = std::max(highest_buy, trade->entry_price);
                    total_volume += trade->lot_size;
                }
            }

            int position_count = eng.GetOpenPositions().size();

            // Check if entry strategy allows entry
            if (entry_strategy.ShouldEnter(tick, lowest_buy, highest_buy, spacing, position_count)) {
                double tp = tick.ask + tick.spread() + spacing;
                eng.OpenMarketOrder("BUY", 0.01, 0.0, tp);
            }
        });

        auto results = engine.GetResults();
        result.final_balance = results.final_balance;
        result.return_x = results.final_balance / config.initial_balance;
        result.max_dd_pct = max_dd_pct;
        result.total_trades = results.total_trades;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        result.return_x = 0;
    }

    return result;
}

int main() {
    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "ENTRY OPTIMIZATION ANALYSIS" << std::endl;
    std::cout << std::string(70, '=') << std::endl;
    std::cout << std::fixed << std::setprecision(2);

    std::vector<TestResult> results;

    AlwaysEnterStrategy always;
    std::cout << "\n[1/4] Testing Always Enter (baseline)..." << std::endl;
    results.push_back(RunTest(always));
    std::cout << "  Return: " << results.back().return_x << "x" << std::endl;

    MomentumReversalStrategy momentum;
    std::cout << "\n[2/4] Testing Momentum Reversal..." << std::endl;
    results.push_back(RunTest(momentum));
    std::cout << "  Return: " << results.back().return_x << "x" << std::endl;

    SpreadFilterStrategy spread;
    std::cout << "\n[3/4] Testing Spread Filter..." << std::endl;
    results.push_back(RunTest(spread));
    std::cout << "  Return: " << results.back().return_x << "x" << std::endl;

    SessionFilterStrategy session;
    std::cout << "\n[4/4] Testing Session Filter..." << std::endl;
    results.push_back(RunTest(session));
    std::cout << "  Return: " << results.back().return_x << "x" << std::endl;

    // Summary
    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "ENTRY OPTIMIZATION RESULTS" << std::endl;
    std::cout << std::string(70, '=') << std::endl;
    std::cout << std::setw(20) << "Strategy"
              << std::setw(14) << "Final $"
              << std::setw(10) << "Return"
              << std::setw(10) << "Max DD"
              << std::setw(12) << "Trades" << std::endl;
    std::cout << std::string(66, '-') << std::endl;

    TestResult best = results[0];
    for (const auto& r : results) {
        std::cout << std::setw(20) << r.strategy_name
                  << std::setw(13) << "$" << r.final_balance
                  << std::setw(9) << r.return_x << "x"
                  << std::setw(9) << r.max_dd_pct << "%"
                  << std::setw(12) << r.total_trades << std::endl;
        if (r.return_x > best.return_x) best = r;
    }

    std::cout << "\n=== BEST ENTRY STRATEGY ===" << std::endl;
    std::cout << best.strategy_name << " with " << best.return_x << "x return" << std::endl;

    double baseline = results[0].return_x;
    std::cout << "\n=== IMPROVEMENTS VS BASELINE ===" << std::endl;
    for (size_t i = 1; i < results.size(); i++) {
        double diff = results[i].return_x - baseline;
        std::cout << results[i].strategy_name << ": " << (diff > 0 ? "+" : "") << diff << "x" << std::endl;
    }

    return 0;
}
