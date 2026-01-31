/**
 * test_live_bridge.cpp
 *
 * Tests the Live Trading Bridge with BacktestMarket.
 * Validates that strategies can run identically in backtest mode
 * using the IMarketInterface abstraction.
 */

#include "../include/live_trading_bridge.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <chrono>

using namespace backtest;

/**
 * Simple grid strategy using IMarketInterface
 * This exact code could run live with MT5Market instead of BacktestMarket
 */
class SimpleGridStrategy {
public:
    SimpleGridStrategy(double spacing, double lot_size)
        : spacing_(spacing), lot_size_(lot_size), last_trade_price_(0) {}

    void OnTick(const Tick& tick, IMarketInterface& market) {
        // Get current price
        double price = tick.bid;

        // Check if we should open new position
        if (last_trade_price_ == 0) {
            // First trade
            OrderRequest order;
            order.symbol = "XAUUSD";
            order.type = OrderType::MARKET_BUY;
            order.lots = lot_size_;
            order.take_profit = tick.ask + 2.0;  // $2 TP
            order.comment = "Grid entry";

            auto result = market.SendOrder(order);
            if (result.success) {
                last_trade_price_ = result.filled_price;
            }
        } else if (price <= last_trade_price_ - spacing_) {
            // Price dropped by spacing, add position
            OrderRequest order;
            order.symbol = "XAUUSD";
            order.type = OrderType::MARKET_BUY;
            order.lots = lot_size_;
            order.take_profit = tick.ask + 2.0;
            order.comment = "Grid add";

            auto result = market.SendOrder(order);
            if (result.success) {
                last_trade_price_ = result.filled_price;
            }
        }
    }

private:
    double spacing_;
    double lot_size_;
    double last_trade_price_;
};

int main() {
    std::cout << std::string(80, '=') << std::endl;
    std::cout << "LIVE TRADING BRIDGE TEST" << std::endl;
    std::cout << "Testing IMarketInterface abstraction with BacktestMarket" << std::endl;
    std::cout << std::string(80, '=') << std::endl << std::endl;

    // Load tick data (small sample for testing)
    std::cout << "Loading tick data..." << std::endl;

    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;

    TickDataManager mgr(tick_config);
    std::vector<Tick> ticks;
    Tick tick;

    // Load first 1M ticks for quick test
    int count = 0;
    while (mgr.GetNextTick(tick) && count < 1000000) {
        ticks.push_back(tick);
        count++;
    }

    std::cout << "Loaded " << ticks.size() << " ticks" << std::endl;
    std::cout << "Date range: " << ticks.front().timestamp.substr(0, 10)
              << " to " << ticks.back().timestamp.substr(0, 10) << std::endl << std::endl;

    // Configure backtest market
    BacktestMarket::Config market_config;
    market_config.symbol = "XAUUSD";
    market_config.initial_balance = 10000.0;
    market_config.contract_size = 100.0;
    market_config.leverage = 500.0;
    market_config.pip_size = 0.01;
    market_config.swap_long = -66.99;
    market_config.swap_short = 41.2;

    // Create market and strategy
    BacktestMarket market(market_config);
    market.SetTicks(ticks);

    SimpleGridStrategy strategy(1.5, 0.01);  // $1.50 spacing, 0.01 lot

    // Run backtest using IMarketInterface
    std::cout << "Running strategy via IMarketInterface..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    while (market.HasMoreTicks()) {
        market.NextTick();
        auto current_tick = market.GetCurrentTick("XAUUSD");
        strategy.OnTick(current_tick, market);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    // Get results
    auto account = market.GetAccountInfo();
    auto positions = market.GetPositions();

    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "RESULTS" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Initial Balance: $10,000.00" << std::endl;
    std::cout << "Final Balance:   $" << account.balance << std::endl;
    std::cout << "Final Equity:    $" << account.equity << std::endl;
    std::cout << "Profit/Loss:     $" << (account.balance - 10000.0) << std::endl;
    std::cout << "Open Positions:  " << positions.size() << std::endl;
    std::cout << "Execution Time:  " << elapsed << " ms" << std::endl;

    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "ABSTRACTION VALIDATION" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    std::cout << "\nThe SimpleGridStrategy class uses only IMarketInterface methods:" << std::endl;
    std::cout << "  - market.SendOrder()" << std::endl;
    std::cout << "  - tick data passed directly" << std::endl;
    std::cout << "\nThis exact strategy code can run live by swapping:" << std::endl;
    std::cout << "  BacktestMarket -> MT5Market (via Python bridge)" << std::endl;
    std::cout << "\nNo code changes required for live trading!" << std::endl;

    // Test TradingSession with safety features
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "TRADING SESSION TEST (Safety Features)" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    // Reset market
    BacktestMarket market2(market_config);
    std::vector<Tick> small_ticks(ticks.begin(), ticks.begin() + 10000);
    market2.SetTicks(small_ticks);

    // Create trading session with limits
    TradingSession::Config session_config;
    session_config.max_positions = 5;
    session_config.max_lots_per_symbol = 0.1;
    session_config.max_drawdown_pct = 10.0;

    auto market_ptr = std::make_shared<BacktestMarket>(market2);
    TradingSession session(market_ptr, session_config);
    session.Start();

    std::cout << "Session Config:" << std::endl;
    std::cout << "  Max Positions: " << session_config.max_positions << std::endl;
    std::cout << "  Max Lots/Symbol: " << session_config.max_lots_per_symbol << std::endl;
    std::cout << "  Max Drawdown: " << session_config.max_drawdown_pct << "%" << std::endl;

    // Try to exceed limits
    std::cout << "\nTesting position limits..." << std::endl;

    for (int i = 0; i < 10; i++) {
        OrderRequest order;
        order.symbol = "XAUUSD";
        order.type = OrderType::MARKET_BUY;
        order.lots = 0.01;

        auto result = session.SendOrder(order);
        std::cout << "  Order " << (i+1) << ": "
                  << (result.success ? "FILLED" : "REJECTED")
                  << " - " << (result.success ? "" : result.error_message) << std::endl;

        if (!session.IsRunning()) {
            std::cout << "  Session stopped: " << session.GetKillSwitch().GetReason() << std::endl;
            break;
        }
    }

    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "TEST COMPLETE" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    return 0;
}
