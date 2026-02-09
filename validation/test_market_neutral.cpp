#include "../include/tick_based_engine.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>

using namespace backtest;

// Helper class for moving statistics (SMA, ATR, etc.)
class RollingStats {
private:
    std::vector<double> values_;
    size_t max_size_;

public:
    explicit RollingStats(size_t max_size) : max_size_(max_size) {
        values_.reserve(max_size);
    }

    void Add(double value) {
        values_.push_back(value);
        if (values_.size() > max_size_) {
            values_.erase(values_.begin());
        }
    }

    double GetSMA() const {
        if (values_.empty()) return 0.0;
        return std::accumulate(values_.begin(), values_.end(), 0.0) / values_.size();
    }

    double GetStdDev() const {
        if (values_.size() < 2) return 0.0;
        double mean = GetSMA();
        double variance = 0.0;
        for (double v : values_) {
            variance += (v - mean) * (v - mean);
        }
        return std::sqrt(variance / (values_.size() - 1));
    }

    double GetMax() const {
        if (values_.empty()) return 0.0;
        return *std::max_element(values_.begin(), values_.end());
    }

    double GetMin() const {
        if (values_.empty()) return 0.0;
        return *std::min_element(values_.begin(), values_.end());
    }

    double GetRange() const {
        return GetMax() - GetMin();
    }

    size_t Size() const { return values_.size(); }
    bool IsFull() const { return values_.size() == max_size_; }
    double GetLast() const { return values_.empty() ? 0.0 : values_.back(); }
};

// Helper: Calculate ATR (Average True Range)
class ATRCalculator {
private:
    std::vector<double> true_ranges_;
    size_t period_;
    double last_close_ = 0.0;

public:
    explicit ATRCalculator(size_t period) : period_(period) {
        true_ranges_.reserve(period);
    }

    void Update(double high, double low, double close) {
        if (last_close_ == 0.0) {
            // First tick
            last_close_ = close;
            return;
        }

        // True Range = max(high-low, |high-prev_close|, |low-prev_close|)
        double tr1 = high - low;
        double tr2 = std::abs(high - last_close_);
        double tr3 = std::abs(low - last_close_);
        double tr = std::max({tr1, tr2, tr3});

        true_ranges_.push_back(tr);
        if (true_ranges_.size() > period_) {
            true_ranges_.erase(true_ranges_.begin());
        }

        last_close_ = close;
    }

    double GetATR() const {
        if (true_ranges_.empty()) return 0.0;
        return std::accumulate(true_ranges_.begin(), true_ranges_.end(), 0.0) / true_ranges_.size();
    }

    bool IsReady() const {
        return true_ranges_.size() >= period_;
    }
};

// Helper: Count price crossings (for range detection)
class CrossingCounter {
private:
    std::vector<bool> above_level_;  // true if price was above reference
    size_t period_;

public:
    explicit CrossingCounter(size_t period) : period_(period) {
        above_level_.reserve(period);
    }

    void Update(double price, double reference) {
        above_level_.push_back(price > reference);
        if (above_level_.size() > period_) {
            above_level_.erase(above_level_.begin());
        }
    }

    int CountCrossings() const {
        if (above_level_.size() < 2) return 0;

        int crossings = 0;
        for (size_t i = 1; i < above_level_.size(); ++i) {
            if (above_level_[i] != above_level_[i-1]) {
                crossings++;
            }
        }
        return crossings;
    }

    bool IsReady() const {
        return above_level_.size() >= period_;
    }
};

// ====================================================================================
// STRATEGY 1: Volatility-Based Trading (ATR Filter)
// Trade when volatility (ATR) is above average - exploits fluctuations, not direction
// ====================================================================================
void TestVolatilityBasedStrategy() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "TEST 1: VOLATILITY-BASED TRADING (ATR FILTER)" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Hypothesis: Trade only when ATR is above average" << std::endl;
    std::cout << "Higher volatility = more fluctuations to exploit" << std::endl;
    std::cout << "Should work in both up and down volatile markets" << std::endl;

    TickDataConfig tick_config;
    tick_config.file_path = "Grid/XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = 110000.0;
    config.contract_size = 100.0;
    config.tick_data_config = tick_config;
    config.leverage = 500.0;
    config.pip_size = 0.01;

    TickBasedEngine engine(config);

    // Strategy state
    ATRCalculator atr_short(100);  // Short-term ATR (100 ticks)
    ATRCalculator atr_long(500);   // Long-term ATR for average (500 ticks)
    RollingStats prices(100);
    int tick_count = 0;
    const int MAX_TICKS = 500000;
    const double GRID_SIZE = 3.0;  // $3 grid
    const double LOT_SIZE = 0.01;
    double last_buy_price = 0.0;

    engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
        tick_count++;
        if (tick_count > MAX_TICKS) return;

        double mid = (tick.bid + tick.ask) / 2.0;

        // Update ATR calculators
        atr_short.Update(tick.ask, tick.bid, mid);
        atr_long.Update(tick.ask, tick.bid, mid);
        prices.Add(mid);

        // Wait for warmup
        if (!atr_long.IsReady() || !prices.IsFull()) return;

        double current_atr = atr_short.GetATR();
        double avg_atr = atr_long.GetATR();

        // VOLATILITY FILTER: Only trade when ATR is above average
        if (current_atr < avg_atr * 1.2) return;  // Need 20% above average

        // Simple grid logic on high volatility
        if (last_buy_price == 0.0 || tick.ask <= last_buy_price - GRID_SIZE) {
            eng.OpenMarketOrder("BUY", LOT_SIZE);
            last_buy_price = tick.ask;
        }

        // Close profitable positions
        for (Trade* trade : eng.GetOpenPositions()) {
            double unrealized = (tick.bid - trade->entry_price) * trade->lot_size * config.contract_size;
            if (unrealized >= 150.0) {  // $150 profit target
                eng.ClosePosition(trade, "Profit Target");
                break;
            }
        }
    });

    std::cout << "\n--- VOLATILITY STRATEGY RESULTS ---" << std::endl;
    auto results = engine.GetResults();
    std::cout << "Final Balance: $" << results.final_balance << std::endl;
    std::cout << "Total P/L: $" << results.total_profit_loss << std::endl;
    std::cout << "Total Trades: " << results.total_trades << std::endl;
    std::cout << "Win Rate: " << results.win_rate << "%" << std::endl;
    std::cout << "Max Drawdown: $" << results.max_drawdown << std::endl;
}

// ====================================================================================
// STRATEGY 2: Range Detection
// Calculate if price is ranging vs trending, only trade during ranging periods
// ====================================================================================
void TestRangeDetectionStrategy() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "TEST 2: RANGE DETECTION STRATEGY" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Hypothesis: Detect ranging markets and only trade then" << std::endl;
    std::cout << "Ranging = high/low within X% over last N ticks" << std::endl;

    TickDataConfig tick_config;
    tick_config.file_path = "Grid/XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = 110000.0;
    config.contract_size = 100.0;
    config.tick_data_config = tick_config;
    config.leverage = 500.0;
    config.pip_size = 0.01;

    TickBasedEngine engine(config);

    // Strategy state
    RollingStats prices(1000);  // Look at last 1000 ticks
    int tick_count = 0;
    const int MAX_TICKS = 500000;
    const double GRID_SIZE = 3.0;
    const double LOT_SIZE = 0.01;
    double last_buy_price = 0.0;

    engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
        tick_count++;
        if (tick_count > MAX_TICKS) return;

        double mid = (tick.bid + tick.ask) / 2.0;
        prices.Add(mid);

        if (!prices.IsFull()) return;

        // Range detection: high/low range as % of average price
        double range = prices.GetRange();
        double avg_price = prices.GetSMA();
        double range_percent = (range / avg_price) * 100.0;

        // RANGE FILTER: Only trade when market is ranging (not trending)
        // Ranging = range is small relative to price (< 1.5%)
        if (range_percent > 1.5) return;  // Too much trending

        // Simple grid logic during ranging
        if (last_buy_price == 0.0 || tick.ask <= last_buy_price - GRID_SIZE) {
            eng.OpenMarketOrder("BUY", LOT_SIZE);
            last_buy_price = tick.ask;
        }

        // Close profitable positions
        for (Trade* trade : eng.GetOpenPositions()) {
            double unrealized = (tick.bid - trade->entry_price) * trade->lot_size * config.contract_size;
            if (unrealized >= 150.0) {
                eng.ClosePosition(trade, "Profit Target");
                break;
            }
        }
    });

    std::cout << "\n--- RANGE DETECTION RESULTS ---" << std::endl;
    auto results = engine.GetResults();
    std::cout << "Final Balance: $" << results.final_balance << std::endl;
    std::cout << "Total P/L: $" << results.total_profit_loss << std::endl;
    std::cout << "Total Trades: " << results.total_trades << std::endl;
    std::cout << "Win Rate: " << results.win_rate << "%" << std::endl;
    std::cout << "Max Drawdown: $" << results.max_drawdown << std::endl;
}

// ====================================================================================
// STRATEGY 3: Mean Reversion with Tight Stops
// Original grid logic but close positions quickly if they go against us
// ====================================================================================
void TestMeanReversionTightStopsStrategy() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "TEST 3: MEAN REVERSION WITH TIGHT STOPS" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Hypothesis: Grid logic but close losing positions fast" << std::endl;
    std::cout << "Prevents accumulation during trends" << std::endl;

    TickDataConfig tick_config;
    tick_config.file_path = "Grid/XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = 110000.0;
    config.contract_size = 100.0;
    config.tick_data_config = tick_config;
    config.leverage = 500.0;
    config.pip_size = 0.01;

    TickBasedEngine engine(config);

    // Strategy state
    int tick_count = 0;
    const int MAX_TICKS = 500000;
    const double GRID_SIZE = 3.0;
    const double LOT_SIZE = 0.01;
    const double STOP_LOSS_DOLLARS = 200.0;  // Cut losses at $200
    double last_buy_price = 0.0;

    engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
        tick_count++;
        if (tick_count > MAX_TICKS) return;

        // Simple grid logic
        if (last_buy_price == 0.0 || tick.ask <= last_buy_price - GRID_SIZE) {
            eng.OpenMarketOrder("BUY", LOT_SIZE);
            last_buy_price = tick.ask;
        }

        // Check positions: close winners and cut losers quickly
        for (Trade* trade : eng.GetOpenPositions()) {
            double unrealized = (tick.bid - trade->entry_price) * trade->lot_size * config.contract_size;

            if (unrealized >= 150.0) {
                eng.ClosePosition(trade, "Profit Target");
                break;
            } else if (unrealized <= -STOP_LOSS_DOLLARS) {
                // TIGHT STOP: Cut losses quickly
                eng.ClosePosition(trade, "Stop Loss Hit");
                break;
            }
        }
    });

    std::cout << "\n--- TIGHT STOPS RESULTS ---" << std::endl;
    auto results = engine.GetResults();
    std::cout << "Final Balance: $" << results.final_balance << std::endl;
    std::cout << "Total P/L: $" << results.total_profit_loss << std::endl;
    std::cout << "Total Trades: " << results.total_trades << std::endl;
    std::cout << "Win Rate: " << results.win_rate << "%" << std::endl;
    std::cout << "Max Drawdown: $" << results.max_drawdown << std::endl;
}

// ====================================================================================
// STRATEGY 4: Bi-Directional Trading with Momentum Confirmation
// BUY when price > SMA AND momentum up, SELL when price < SMA AND momentum down
// ====================================================================================
void TestBiDirectionalStrategy() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "TEST 4: BI-DIRECTIONAL WITH MOMENTUM" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Hypothesis: Trade both directions based on momentum" << std::endl;
    std::cout << "BUY when price > SMA AND momentum up" << std::endl;
    std::cout << "SELL when price < SMA AND momentum down" << std::endl;

    TickDataConfig tick_config;
    tick_config.file_path = "Grid/XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = 110000.0;
    config.contract_size = 100.0;
    config.tick_data_config = tick_config;
    config.leverage = 500.0;
    config.pip_size = 0.01;

    TickBasedEngine engine(config);

    // Strategy state
    RollingStats sma_prices(500);  // SMA(500)
    RollingStats momentum(50);     // Recent momentum
    int tick_count = 0;
    const int MAX_TICKS = 500000;
    const double GRID_SIZE = 3.0;
    const double LOT_SIZE = 0.01;
    double last_buy_price = 0.0;
    double last_sell_price = 0.0;

    engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
        tick_count++;
        if (tick_count > MAX_TICKS) return;

        double mid = (tick.bid + tick.ask) / 2.0;
        sma_prices.Add(mid);

        if (!sma_prices.IsFull()) return;

        // Calculate momentum (price change over last 50 ticks)
        momentum.Add(mid);
        if (!momentum.IsFull()) return;

        double sma = sma_prices.GetSMA();
        double momentum_change = momentum.GetLast() - momentum.GetLast();
        double recent_slope = (mid - sma_prices.GetLast()) * 50;  // Approximate slope

        // BI-DIRECTIONAL LOGIC
        bool bullish = mid > sma && recent_slope > 0.5;  // Above SMA and rising
        bool bearish = mid < sma && recent_slope < -0.5; // Below SMA and falling

        // Open BUY positions
        if (bullish && (last_buy_price == 0.0 || tick.ask <= last_buy_price - GRID_SIZE)) {
            eng.OpenMarketOrder("BUY", LOT_SIZE);
            last_buy_price = tick.ask;
        }

        // Open SELL positions
        if (bearish && (last_sell_price == 0.0 || tick.bid >= last_sell_price + GRID_SIZE)) {
            eng.OpenMarketOrder("SELL", LOT_SIZE);
            last_sell_price = tick.bid;
        }

        // Close profitable positions
        for (Trade* trade : eng.GetOpenPositions()) {
            double unrealized = 0.0;
            if (trade->direction == "BUY") {
                unrealized = (tick.bid - trade->entry_price) * trade->lot_size * config.contract_size;
            } else {
                unrealized = (trade->entry_price - tick.ask) * trade->lot_size * config.contract_size;
            }

            if (unrealized >= 150.0) {
                eng.ClosePosition(trade, "Profit Target");
                break;
            }
        }
    });

    std::cout << "\n--- BI-DIRECTIONAL RESULTS ---" << std::endl;
    auto results = engine.GetResults();
    std::cout << "Final Balance: $" << results.final_balance << std::endl;
    std::cout << "Total P/L: $" << results.total_profit_loss << std::endl;
    std::cout << "Total Trades: " << results.total_trades << std::endl;
    std::cout << "Win Rate: " << results.win_rate << "%" << std::endl;
    std::cout << "Max Drawdown: $" << results.max_drawdown << std::endl;
}

// ====================================================================================
// STRATEGY 5: Fluctuation Counter
// Count price crossings to detect ranging markets
// ====================================================================================
void TestFluctuationCounterStrategy() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "TEST 5: FLUCTUATION COUNTER" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Hypothesis: Count SMA crossings to detect ranging markets" << std::endl;
    std::cout << "More crossings = ranging = good for trading" << std::endl;
    std::cout << "Few crossings = trending = don't trade" << std::endl;

    TickDataConfig tick_config;
    tick_config.file_path = "Grid/XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = 110000.0;
    config.contract_size = 100.0;
    config.tick_data_config = tick_config;
    config.leverage = 500.0;
    config.pip_size = 0.01;

    TickBasedEngine engine(config);

    // Strategy state
    RollingStats sma_prices(500);
    CrossingCounter crossing_counter(1000);  // Count crossings over last 1000 ticks
    int tick_count = 0;
    const int MAX_TICKS = 500000;
    const double GRID_SIZE = 3.0;
    const double LOT_SIZE = 0.01;
    double last_buy_price = 0.0;

    engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
        tick_count++;
        if (tick_count > MAX_TICKS) return;

        double mid = (tick.bid + tick.ask) / 2.0;
        sma_prices.Add(mid);

        if (!sma_prices.IsFull()) return;

        double sma = sma_prices.GetSMA();
        crossing_counter.Update(mid, sma);

        if (!crossing_counter.IsReady()) return;

        int crossings = crossing_counter.CountCrossings();

        // FLUCTUATION FILTER: Only trade when there are many crossings
        // More crossings = ranging market = good for mean reversion
        if (crossings < 10) return;  // Need at least 10 crossings in 1000 ticks

        // Simple grid logic
        if (last_buy_price == 0.0 || tick.ask <= last_buy_price - GRID_SIZE) {
            eng.OpenMarketOrder("BUY", LOT_SIZE);
            last_buy_price = tick.ask;
        }

        // Close profitable positions
        for (Trade* trade : eng.GetOpenPositions()) {
            double unrealized = (tick.bid - trade->entry_price) * trade->lot_size * config.contract_size;
            if (unrealized >= 150.0) {
                eng.ClosePosition(trade, "Profit Target");
                break;
            }
        }
    });

    std::cout << "\n--- FLUCTUATION COUNTER RESULTS ---" << std::endl;
    auto results = engine.GetResults();
    std::cout << "Final Balance: $" << results.final_balance << std::endl;
    std::cout << "Total P/L: $" << results.total_profit_loss << std::endl;
    std::cout << "Total Trades: " << results.total_trades << std::endl;
    std::cout << "Win Rate: " << results.win_rate << "%" << std::endl;
    std::cout << "Max Drawdown: $" << results.max_drawdown << std::endl;
}

// ====================================================================================
// STRATEGY 6 (BONUS): Pure Grid with No Filters
// Baseline for comparison - original V1 strategy
// ====================================================================================
void TestPureGridStrategy() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "TEST 6 (BASELINE): PURE GRID - NO FILTERS" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Original V1 strategy for comparison" << std::endl;

    TickDataConfig tick_config;
    tick_config.file_path = "Grid/XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = 110000.0;
    config.contract_size = 100.0;
    config.tick_data_config = tick_config;
    config.leverage = 500.0;
    config.pip_size = 0.01;

    TickBasedEngine engine(config);

    // Strategy state
    int tick_count = 0;
    const int MAX_TICKS = 500000;
    const double GRID_SIZE = 3.0;
    const double LOT_SIZE = 0.01;
    double last_buy_price = 0.0;

    engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
        tick_count++;
        if (tick_count > MAX_TICKS) return;

        // Pure grid logic - no filters
        if (last_buy_price == 0.0 || tick.ask <= last_buy_price - GRID_SIZE) {
            eng.OpenMarketOrder("BUY", LOT_SIZE);
            last_buy_price = tick.ask;
        }

        // Close profitable positions
        for (Trade* trade : eng.GetOpenPositions()) {
            double unrealized = (tick.bid - trade->entry_price) * trade->lot_size * config.contract_size;
            if (unrealized >= 150.0) {
                eng.ClosePosition(trade, "Profit Target");
                break;
            }
        }
    });

    std::cout << "\n--- PURE GRID (BASELINE) RESULTS ---" << std::endl;
    auto results = engine.GetResults();
    std::cout << "Final Balance: $" << results.final_balance << std::endl;
    std::cout << "Total P/L: $" << results.total_profit_loss << std::endl;
    std::cout << "Total Trades: " << results.total_trades << std::endl;
    std::cout << "Win Rate: " << results.win_rate << "%" << std::endl;
    std::cout << "Max Drawdown: $" << results.max_drawdown << std::endl;
}

// ====================================================================================
// MAIN
// ====================================================================================
int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "MARKET-NEUTRAL STRATEGY EXPLORATION" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Testing first 500k ticks from start of file" << std::endl;
    std::cout << "Goal: Find strategies that work in BOTH trending and ranging markets" << std::endl;
    std::cout << std::endl;

    try {
        // Test all strategies
        TestPureGridStrategy();           // Baseline
        TestVolatilityBasedStrategy();    // Strategy 1
        TestRangeDetectionStrategy();     // Strategy 2
        TestMeanReversionTightStopsStrategy();  // Strategy 3
        TestBiDirectionalStrategy();      // Strategy 4
        TestFluctuationCounterStrategy(); // Strategy 5

        // Summary
        std::cout << "\n========================================" << std::endl;
        std::cout << "SUMMARY & ANALYSIS" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "Compare the results above to determine which approach:" << std::endl;
        std::cout << "1. Is profitable in the first 500k ticks" << std::endl;
        std::cout << "2. Has the LEAST dependence on directional movement" << std::endl;
        std::cout << "3. Works in both volatile and calm markets" << std::endl;
        std::cout << "\nKey metrics to compare:" << std::endl;
        std::cout << "- Total P/L (higher is better)" << std::endl;
        std::cout << "- Win Rate (should be reasonable, 50%+)" << std::endl;
        std::cout << "- Max Drawdown (lower is better)" << std::endl;
        std::cout << "- Total Trades (more trades = more opportunities)" << std::endl;
        std::cout << "\nExpected findings:" << std::endl;
        std::cout << "- Pure Grid: Works if gold goes up, fails if it goes down" << std::endl;
        std::cout << "- Volatility Filter: Should be more market-neutral" << std::endl;
        std::cout << "- Range Detection: Best in sideways markets only" << std::endl;
        std::cout << "- Tight Stops: Limits losses but may reduce profits" << std::endl;
        std::cout << "- Bi-Directional: True market-neutral if balanced" << std::endl;
        std::cout << "- Fluctuation Counter: Avoids trending disasters" << std::endl;

        std::cout << "\nTest PASSED!" << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }
}
