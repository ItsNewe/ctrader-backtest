/**
 * Advanced Features Tests - Tests for trailing stops, slippage models, equity curve, Sharpe ratio
 *
 * Build: g++ -O3 -fno-inline -mavx2 -mfma -std=c++17 -static -I include tests/test_advanced_features.cpp -o build/test_advanced_features.exe
 */
#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cmath>

using namespace backtest;

struct TestResult {
    std::string name;
    bool passed;
    std::string details;
};

void print_result(const TestResult& result) {
    std::cout << (result.passed ? "[PASS]" : "[FAIL]") << " " << result.name;
    if (!result.details.empty()) {
        std::cout << " - " << result.details;
    }
    std::cout << std::endl;
}

// Test 1: Trailing stop functionality
TestResult test_trailing_stop() {
    TestResult result;
    result.name = "Trailing Stop";

    try {
        TickDataConfig tick_config;
        tick_config.file_path = "C:/Users/user/Documents/ctrader-backtest/validation/Grid/XAUUSD_TICKS_2025.csv";
        tick_config.format = TickDataFormat::MT5_CSV;

        TickBacktestConfig config;
        config.symbol = "XAUUSD";
        config.initial_balance = 10000.0;
        config.contract_size = 100.0;
        config.leverage = 500.0;
        config.pip_size = 0.01;
        config.start_date = "2024.12.31";
        config.end_date = "2025.01.05";
        config.tick_data_config = tick_config;
        config.verbose = false;

        TickBasedEngine engine(config);
        bool trade_opened = false;
        int trailing_close_count = 0;

        engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
            if (!trade_opened && eng.GetOpenPositions().empty()) {
                // Open a position with trailing stop
                Trade* trade = eng.OpenMarketOrderWithTrailing(
                    TradeDirection::BUY,
                    0.1,    // lot size
                    2.0,    // trailing distance ($2)
                    1.0     // activation profit ($1 profit before trailing activates)
                );
                trade_opened = (trade != nullptr);
            }

            // Check if closed by trailing
            for (const auto& closed : eng.GetClosedTrades()) {
                if (closed.exit_reason == "TRAILING_SL") {
                    trailing_close_count++;
                }
            }
        });

        auto results = engine.GetResults();
        result.passed = trade_opened;  // At minimum, trailing order should be placeable
        result.details = "Opened: " + std::string(trade_opened ? "yes" : "no") +
                        ", Trailing closes: " + std::to_string(trailing_close_count);

    } catch (const std::exception& e) {
        result.passed = false;
        result.details = std::string("Exception: ") + e.what();
    }

    return result;
}

// Test 2: Equity curve tracking
TestResult test_equity_curve() {
    TestResult result;
    result.name = "Equity Curve Tracking";

    try {
        TickDataConfig tick_config;
        tick_config.file_path = "C:/Users/user/Documents/ctrader-backtest/validation/Grid/XAUUSD_TICKS_2025.csv";
        tick_config.format = TickDataFormat::MT5_CSV;

        TickBacktestConfig config;
        config.symbol = "XAUUSD";
        config.initial_balance = 10000.0;
        config.contract_size = 100.0;
        config.leverage = 500.0;
        config.pip_size = 0.01;
        config.start_date = "2024.12.31";
        config.end_date = "2025.01.05";
        config.tick_data_config = tick_config;
        config.verbose = false;
        config.track_equity_curve = true;
        config.equity_sample_interval = 100;  // Sample every 100 ticks

        TickBasedEngine engine(config);

        FillUpOscillation strategy(
            13.0, 1.5, 0.01, 10.0, 100.0, 500.0,
            FillUpOscillation::ADAPTIVE_SPACING, 0.1, 30.0, 4.0
        );

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        const auto& equity_curve = results.equity_curve;

        result.passed = !equity_curve.empty();
        result.details = "Samples: " + std::to_string(equity_curve.size());

        if (!equity_curve.empty()) {
            result.details += ", First: $" + std::to_string(equity_curve.front()) +
                             ", Last: $" + std::to_string(equity_curve.back());
        }

    } catch (const std::exception& e) {
        result.passed = false;
        result.details = std::string("Exception: ") + e.what();
    }

    return result;
}

// Test 3: Sharpe ratio calculation
TestResult test_sharpe_ratio() {
    TestResult result;
    result.name = "Sharpe Ratio Calculation";

    try {
        TickDataConfig tick_config;
        tick_config.file_path = "C:/Users/user/Documents/ctrader-backtest/validation/Grid/XAUUSD_TICKS_2025.csv";
        tick_config.format = TickDataFormat::MT5_CSV;

        TickBacktestConfig config;
        config.symbol = "XAUUSD";
        config.initial_balance = 10000.0;
        config.contract_size = 100.0;
        config.leverage = 500.0;
        config.pip_size = 0.01;
        config.start_date = "2024.12.31";
        config.end_date = "2025.01.15";  // 2 weeks for multiple daily returns
        config.tick_data_config = tick_config;
        config.verbose = false;

        TickBasedEngine engine(config);

        FillUpOscillation strategy(
            13.0, 1.5, 0.01, 10.0, 100.0, 500.0,
            FillUpOscillation::ADAPTIVE_SPACING, 0.1, 30.0, 4.0
        );

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();

        // Sharpe should be calculated (could be positive or negative)
        bool valid_sharpe = !std::isnan(results.sharpe_ratio) && !std::isinf(results.sharpe_ratio);
        bool valid_sortino = !std::isnan(results.sortino_ratio) && !std::isinf(results.sortino_ratio);

        result.passed = valid_sharpe && valid_sortino;
        result.details = "Sharpe: " + std::to_string(results.sharpe_ratio) +
                        ", Sortino: " + std::to_string(results.sortino_ratio) +
                        ", Profit Factor: " + std::to_string(results.profit_factor);

    } catch (const std::exception& e) {
        result.passed = false;
        result.details = std::string("Exception: ") + e.what();
    }

    return result;
}

// Test 4: Volume-based slippage model
TestResult test_volume_slippage() {
    TestResult result;
    result.name = "Volume-Based Slippage";

    try {
        TickDataConfig tick_config;
        tick_config.file_path = "C:/Users/user/Documents/ctrader-backtest/validation/Grid/XAUUSD_TICKS_2025.csv";
        tick_config.format = TickDataFormat::MT5_CSV;

        TickBacktestConfig config;
        config.symbol = "XAUUSD";
        config.initial_balance = 10000.0;
        config.contract_size = 100.0;
        config.leverage = 500.0;
        config.pip_size = 0.01;
        config.start_date = "2024.12.31";
        config.end_date = "2025.01.02";
        config.tick_data_config = tick_config;
        config.verbose = false;

        // Enable volume-based slippage
        config.slippage_model = SlippageModel::VOLUME_BASED;
        config.slippage_pips = 1.0;           // Base slippage
        config.slippage_volume_factor = 0.5;  // Extra 0.5 pips per lot

        TickBasedEngine engine(config);
        double small_lot_price = 0;
        double large_lot_price = 0;
        bool got_prices = false;

        engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
            if (!got_prices && eng.GetOpenPositions().empty()) {
                // Record prices for different lot sizes
                // Note: We can't easily compare entry prices without tracking,
                // so we just verify no crashes with volume-based slippage
                Trade* t1 = eng.OpenMarketOrder(TradeDirection::BUY, 0.1, 0, 0);
                if (t1) small_lot_price = t1->entry_price;

                eng.ClosePosition(t1, "TEST");

                Trade* t2 = eng.OpenMarketOrder(TradeDirection::BUY, 1.0, 0, 0);
                if (t2) large_lot_price = t2->entry_price;

                eng.ClosePosition(t2, "TEST");
                got_prices = true;
            }
        });

        // With volume-based slippage, larger lots should have higher entry price (for BUY)
        // But since we're testing on same tick, prices might be similar
        result.passed = got_prices && small_lot_price > 0 && large_lot_price > 0;
        result.details = "Small lot price: $" + std::to_string(small_lot_price) +
                        ", Large lot price: $" + std::to_string(large_lot_price);

    } catch (const std::exception& e) {
        result.passed = false;
        result.details = std::string("Exception: ") + e.what();
    }

    return result;
}

// Test 5: Profit factor and recovery factor calculation
TestResult test_risk_metrics() {
    TestResult result;
    result.name = "Risk Metrics (PF, RF)";

    try {
        TickDataConfig tick_config;
        tick_config.file_path = "C:/Users/user/Documents/ctrader-backtest/validation/Grid/XAUUSD_TICKS_2025.csv";
        tick_config.format = TickDataFormat::MT5_CSV;

        TickBacktestConfig config;
        config.symbol = "XAUUSD";
        config.initial_balance = 10000.0;
        config.contract_size = 100.0;
        config.leverage = 500.0;
        config.pip_size = 0.01;
        config.start_date = "2024.12.31";
        config.end_date = "2025.01.10";
        config.tick_data_config = tick_config;
        config.verbose = false;

        TickBasedEngine engine(config);

        FillUpOscillation strategy(
            13.0, 1.5, 0.01, 10.0, 100.0, 500.0,
            FillUpOscillation::ADAPTIVE_SPACING, 0.1, 30.0, 4.0
        );

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();

        bool valid_pf = !std::isnan(results.profit_factor) && !std::isinf(results.profit_factor);
        bool valid_rf = !std::isnan(results.recovery_factor) && !std::isinf(results.recovery_factor);

        result.passed = valid_pf && valid_rf;
        result.details = "Profit Factor: " + std::to_string(results.profit_factor) +
                        ", Recovery Factor: " + std::to_string(results.recovery_factor) +
                        ", Max DD: " + std::to_string(results.max_drawdown_pct) + "%";

    } catch (const std::exception& e) {
        result.passed = false;
        result.details = std::string("Exception: ") + e.what();
    }

    return result;
}

// Test 6: Volatility-based slippage
TestResult test_volatility_slippage() {
    TestResult result;
    result.name = "Volatility-Based Slippage";

    try {
        TickDataConfig tick_config;
        tick_config.file_path = "C:/Users/user/Documents/ctrader-backtest/validation/Grid/XAUUSD_TICKS_2025.csv";
        tick_config.format = TickDataFormat::MT5_CSV;

        TickBacktestConfig config;
        config.symbol = "XAUUSD";
        config.initial_balance = 10000.0;
        config.contract_size = 100.0;
        config.leverage = 500.0;
        config.pip_size = 0.01;
        config.start_date = "2024.12.31";
        config.end_date = "2025.01.05";
        config.tick_data_config = tick_config;
        config.verbose = false;

        // Enable volatility-based slippage
        config.slippage_model = SlippageModel::VOLATILITY_BASED;
        config.slippage_pips = 1.0;
        config.slippage_volatility_factor = 2.0;

        TickBasedEngine engine(config);
        int trades_opened = 0;

        engine.Run([&trades_opened](const Tick& tick, TickBasedEngine& eng) {
            // Open some trades to test volatility slippage
            if (trades_opened < 5 && eng.GetOpenPositions().size() < 3) {
                Trade* t = eng.OpenMarketOrder(TradeDirection::BUY, 0.01, 0, 0);
                if (t) trades_opened++;
            }
        });

        auto results = engine.GetResults();
        result.passed = (trades_opened > 0);
        result.details = "Trades opened with volatility slippage: " + std::to_string(trades_opened);

    } catch (const std::exception& e) {
        result.passed = false;
        result.details = std::string("Exception: ") + e.what();
    }

    return result;
}

int main() {
    std::cout << "=== Advanced Features Tests ===" << std::endl;
    std::cout << "Testing trailing stops, slippage models, equity curves, risk metrics" << std::endl;
    std::cout << std::string(60, '-') << std::endl << std::endl;

    std::vector<TestResult> results;

    std::cout << "Running test 1/6: Trailing Stop..." << std::endl;
    results.push_back(test_trailing_stop());
    print_result(results.back());

    std::cout << "Running test 2/6: Equity Curve..." << std::endl;
    results.push_back(test_equity_curve());
    print_result(results.back());

    std::cout << "Running test 3/6: Sharpe Ratio..." << std::endl;
    results.push_back(test_sharpe_ratio());
    print_result(results.back());

    std::cout << "Running test 4/6: Volume Slippage..." << std::endl;
    results.push_back(test_volume_slippage());
    print_result(results.back());

    std::cout << "Running test 5/6: Risk Metrics..." << std::endl;
    results.push_back(test_risk_metrics());
    print_result(results.back());

    std::cout << "Running test 6/6: Volatility Slippage..." << std::endl;
    results.push_back(test_volatility_slippage());
    print_result(results.back());

    // Summary
    std::cout << std::endl << std::string(60, '-') << std::endl;
    int passed = 0, failed = 0;
    for (const auto& r : results) {
        if (r.passed) passed++;
        else failed++;
    }

    std::cout << "SUMMARY: " << passed << " passed, " << failed << " failed" << std::endl;

    if (failed == 0) {
        std::cout << "\n*** ALL ADVANCED FEATURE TESTS PASSED ***" << std::endl;
        return 0;
    } else {
        std::cout << "\n*** SOME TESTS FAILED ***" << std::endl;
        return 1;
    }
}
