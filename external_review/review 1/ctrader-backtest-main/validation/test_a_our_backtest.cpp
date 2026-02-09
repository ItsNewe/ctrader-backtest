//+------------------------------------------------------------------+
//| Test A - Our Backtest Version                                   |
//| Matches MT5 test: SL/TP execution order discovery               |
//+------------------------------------------------------------------+

#include "../include/backtest_engine.h"
#include <iostream>
#include <fstream>

using namespace backtest;

// Simple test strategy that mimics the MT5 EA behavior
class TestA_Strategy : public IStrategy {
private:
    bool position_opened_ = false;
    double entry_price_ = 0;
    double sl_price_ = 0;
    double tp_price_ = 0;
    uint64_t entry_time_ = 0;
    int sl_points_ = 100;
    int tp_points_ = 100;

public:
    void OnInit() override {
        std::cout << "========================================" << std::endl;
        std::cout << "TEST A: SL/TP EXECUTION ORDER DISCOVERY" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "Our C++ Backtest Engine Version" << std::endl;
        std::cout << "" << std::endl;
    }

    void OnTick(const Tick& tick, const std::vector<Bar>& bars,
                Position& position, std::vector<Trade>& trades,
                const BacktestConfig& config) override {

        // Phase 1: Open position if not already open
        if (!position_opened_ && !position.is_open) {
            OpenTestPosition(tick, position, config);
            return;
        }

        // Phase 2: Position will be closed automatically by engine
        // We just need to monitor when it closes
        if (position_opened_ && !position.is_open) {
            // Position was closed - log the last trade
            if (!trades.empty()) {
                const Trade& last_trade = trades.back();
                LogTradeResult(last_trade);
                position_opened_ = false;  // Prevent re-logging
            }
        }
    }

    void OpenTestPosition(const Tick& tick, Position& position,
                         const BacktestConfig& config) {
        std::cout << "Opening test position..." << std::endl;
        std::cout << "  Entry (ASK): " << tick.ask << std::endl;

        // Calculate SL and TP
        sl_price_ = tick.bid - sl_points_ * config.point_value;
        tp_price_ = tick.bid + tp_points_ * config.point_value;

        std::cout << "  Stop Loss:   " << sl_price_
                  << " (" << sl_points_ << " points below BID)" << std::endl;
        std::cout << "  Take Profit: " << tp_price_
                  << " (" << tp_points_ << " points above BID)" << std::endl;

        // Open position
        position.is_open = true;
        position.is_buy = true;
        position.entry_price = tick.ask;
        position.entry_time = tick.time;
        position.entry_time_msc = tick.time_msc;
        position.volume = 0.01;  // Match MT5 lot size
        position.stop_loss = sl_price_;
        position.take_profit = tp_price_;

        position_opened_ = true;
        entry_price_ = tick.ask;
        entry_time_ = tick.time;

        std::cout << "SUCCESS: Position opened" << std::endl;
        std::cout << "  Entry Time: " << entry_time_ << std::endl;
        std::cout << "" << std::endl;
        std::cout << "Now waiting for position to close via SL or TP..." << std::endl;
    }

    void LogTradeResult(const Trade& trade) {
        std::cout << "" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "POSITION CLOSED - ANALYZING RESULT" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "Exit Deal Details:" << std::endl;
        std::cout << "  Exit Time: " << trade.exit_time << std::endl;
        std::cout << "  Exit Price: " << trade.exit_price << std::endl;
        std::cout << "  Profit: " << trade.profit << std::endl;
        std::cout << "  Commission: " << trade.commission << std::endl;
        std::cout << "  Swap: " << trade.swap << std::endl;
        std::cout << "  Exit Reason: " << trade.exit_reason << std::endl;
        std::cout << "" << std::endl;
        std::cout << "Position Details:" << std::endl;
        std::cout << "  Entry Time: " << trade.entry_time << std::endl;
        std::cout << "  Entry Price: " << trade.entry_price << std::endl;
        std::cout << "  Stop Loss: " << sl_price_ << std::endl;
        std::cout << "  Take Profit: " << tp_price_ << std::endl;
        std::cout << "  Duration: " << (trade.exit_time - trade.entry_time) << " seconds" << std::endl;
        std::cout << "" << std::endl;
        std::cout << "*** EXIT REASON: " << trade.exit_reason << " ***" << std::endl;
        std::cout << "========================================" << std::endl;
    }

    void OnDeinit() override {
        std::cout << "" << std::endl;
        std::cout << "Test A Complete (Our Engine)" << std::endl;
    }

    IStrategy* Clone() const override {
        return new TestA_Strategy(*this);
    }
};

// Export results in same format as MT5
void ExportResults(const Trade& trade, double sl_price, double tp_price, double entry_price) {
    std::ofstream file("validation/ours/test_a_our_result.csv");

    if (!file.is_open()) {
        std::cerr << "ERROR: Cannot create result file" << std::endl;
        return;
    }

    // Header
    file << "ticket,entry_time,entry_price,exit_time,exit_price,sl_price,tp_price,";
    file << "profit,comment,exit_reason,source\n";

    // Data
    file << "1," << trade.entry_time << "," << entry_price << ",";
    file << trade.exit_time << "," << trade.exit_price << ",";
    file << sl_price << "," << tp_price << ",";
    file << trade.profit << ",TestA_OurEngine," << trade.exit_reason << ",";
    file << "OurBacktester\n";

    file.close();

    std::cout << "RESULTS EXPORTED TO: validation/ours/test_a_our_result.csv" << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "Loading test data..." << std::endl;

    // Load price data - will use same data as MT5 test
    // For now, we'll load from a CSV file
    std::string data_file = "validation/configs/test_a_data.csv";

    if (argc > 1) {
        data_file = argv[1];
    }

    auto bars = DataLoader::LoadBarsFromCSV(data_file);

    if (bars.empty()) {
        std::cerr << "ERROR: No data loaded from " << data_file << std::endl;
        std::cerr << "Please export EURUSD H1 data from MT5 first" << std::endl;
        return 1;
    }

    std::cout << "Loaded " << bars.size() << " bars" << std::endl;

    // Configure backtest to match MT5 settings
    BacktestConfig config;
    config.mode = BacktestMode::EVERY_TICK_OHLC;  // Generate ticks from OHLC
    config.initial_balance = 10000.0;
    config.commission_per_lot = 7.0;
    config.spread_points = 10;  // 1 pip spread for EURUSD
    config.point_value = 0.0001;
    config.lot_size = 100000.0;
    config.enable_slippage = false;  // Disable for test simplicity
    config.swap_long_per_lot = 0.0;
    config.swap_short_per_lot = 0.0;

    std::cout << "Backtest configuration:" << std::endl;
    std::cout << "  Mode: EVERY_TICK_OHLC (synthetic ticks)" << std::endl;
    std::cout << "  Initial Balance: " << config.initial_balance << std::endl;
    std::cout << "  Spread: " << config.spread_points << " points" << std::endl;
    std::cout << "" << std::endl;

    // Run backtest
    BacktestEngine engine(config);
    engine.LoadBars(bars);

    TestA_Strategy strategy;
    StrategyParams* params = nullptr;

    std::cout << "Starting backtest..." << std::endl;
    auto result = engine.RunBacktest(&strategy, params);

    std::cout << "" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "BACKTEST COMPLETE" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Total Trades: " << result.total_trades << std::endl;
    std::cout << "Execution Time: " << result.execution_time_ms << "ms" << std::endl;
    std::cout << "" << std::endl;

    if (result.total_trades > 0) {
        const auto& trade = result.trades[0];

        // Export results
        TestA_Strategy temp_strategy;
        ExportResults(trade,
                     trade.entry_price - 100 * config.point_value,  // SL
                     trade.entry_price + 100 * config.point_value,  // TP
                     trade.entry_price);

        std::cout << "" << std::endl;
        std::cout << "Next Steps:" << std::endl;
        std::cout << "1. Compare validation/mt5/test_a_mt5_result.csv" << std::endl;
        std::cout << "   with validation/ours/test_a_our_result.csv" << std::endl;
        std::cout << "2. Run: python validation/compare_test_a.py" << std::endl;
        std::cout << "3. Check if exit_reason matches between MT5 and our engine" << std::endl;
    } else {
        std::cout << "WARNING: No trades executed" << std::endl;
        std::cout << "This might indicate a problem with the test setup" << std::endl;
    }

    return 0;
}
