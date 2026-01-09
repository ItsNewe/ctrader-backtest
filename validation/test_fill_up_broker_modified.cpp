#include "../include/fill_up_strategy.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>

using namespace backtest;

int main() {
    std::cout << "=== Fill-Up Grid Trading Strategy Test ===" << std::endl;
    std::cout << std::fixed << std::setprecision(2);

    // Configure tick data source
    TickDataConfig tick_config;
    tick_config.file_path = "XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;  // Streaming mode for large file

    // Configure backtest to match MT5 settings
    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = 110000.0;  // $110,000 from INI file
    config.account_currency = "USD";
    config.commission_per_lot = 0.0;
    config.slippage_pips = 0.0;  // No slippage in tester
    config.use_bid_ask_spread = true;
    config.contract_size = 100.0;  // XAUUSD contract size
    config.leverage = 500.0;       // 1:500 leverage from INI file
    config.margin_rate = 1.0;      // Margin rate = 1.0

    // Date filtering (MT5 behavior: start inclusive, end exclusive)
    config.start_date = "2025.01.01";
    config.end_date = "2025.12.29";  // Exclusive - stops before market open on 12.29

    // Swap rates (from MT5 API - )
    // Mode 1 = SYMBOL_SWAP_MODE_POINTS (points per lot per day)
    config.swap_long = -66.99;   // -66.99 points/lot/day (= -$66.99/lot/day for XAUUSD)
    config.swap_short = 41.2;    // Only long positions in this strategy
    config.swap_mode = 1;        // SYMBOL_SWAP_MODE_POINTS
    config.swap_3days = 3;       // Triple swap on Wednesday (0=Sun, 3=Wed, etc)

    config.tick_data_config = tick_config;

    std::cout << "\nBacktest Configuration:" << std::endl;
    std::cout << "Symbol: " << config.symbol << std::endl;
    std::cout << "Period: 2025.01.01 - 2025.12.29" << std::endl;
    std::cout << "Initial Balance: $" << config.initial_balance << std::endl;
    std::cout << "Tick Data: " << tick_config.file_path << std::endl;
    std::cout << "Mode: Every tick based on real ticks (streaming)" << std::endl;

    // Strategy parameters from fill up.ini file
    double survive = 13.0;  // 13% drawdown tolerance (from INI file)
    double size = 1.0;      // Lot size multiplier
    double spacing = 1.0;   // Spacing in dollars

    // XAUUSD symbol parameters
    double min_volume = 0.01;     // Minimum lot size
    double max_volume = 100.0;    // Maximum lot size
    double contract_size = 100.0; // Contract size for XAUUSD
    double leverage = 500.0;      // 1:500 leverage
    int symbol_digits = 2;        // XAUUSD has 2 decimal places

    std::cout << "\nStrategy Parameters:" << std::endl;
    std::cout << "Survive: " << survive << "%" << std::endl;
    std::cout << "Size Multiplier: " << size << std::endl;
    std::cout << "Spacing: $" << spacing << std::endl;
    std::cout << "Min Volume: " << min_volume << " lots" << std::endl;
    std::cout << "Max Volume: " << max_volume << " lots" << std::endl;
    std::cout << "Contract Size: " << contract_size << std::endl;
    std::cout << "Leverage: 1:" << leverage << std::endl;

    try {
        // Create engine
        TickBasedEngine engine(config);

        // Create strategy
        FillUpStrategy strategy(survive, size, spacing, min_volume, max_volume,
                                contract_size, leverage, symbol_digits);

        // Run backtest
        std::cout << "\n--- Starting Tick-by-Tick Execution ---\n" << std::endl;

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& engine) {
            strategy.OnTick(tick, engine);
        });

        // Print strategy statistics
        std::cout << "\n=== Strategy Statistics ===" << std::endl;
        std::cout << "Max Balance: $" << std::setprecision(2) << strategy.GetMaxBalance() << std::endl;
        std::cout << "Max Number of Open Positions: " << strategy.GetMaxNumberOfOpen() << std::endl;
        std::cout << "Max Used Funds: $" << std::setprecision(2) << strategy.GetMaxUsedFunds() << std::endl;
        std::cout << "Max Trade Size: " << std::setprecision(2) << strategy.GetMaxTradeSize() << " lots" << std::endl;

        // Print detailed results
        std::cout << "\n=== Trade Statistics ===" << std::endl;
        const auto& trades = engine.GetClosedTrades();
        std::cout << "Total Closed Trades: " << trades.size() << std::endl;

        if (trades.size() > 0) {
            std::cout << "\nFirst 10 Trades:" << std::endl;
            for (size_t i = 0; i < std::min(trades.size(), (size_t)10); i++) {
                const auto& trade = trades[i];
                std::cout << "Trade #" << (i + 1) << ": "
                          << trade.direction << " "
                          << trade.lot_size << " lots @ "
                          << trade.entry_price << " -> "
                          << trade.exit_price << " ("
                          << trade.exit_reason << ") P/L: $"
                          << trade.profit_loss << std::endl;
            }

            if (trades.size() > 10) {
                std::cout << "... (" << (trades.size() - 10) << " more trades)" << std::endl;
            }
        }

        // Summary
        auto results = engine.GetResults();
        std::cout << "\n=== Final Results ===" << std::endl;
        std::cout << std::setprecision(2);
        std::cout << "Initial Balance:  $" << results.initial_balance << std::endl;
        std::cout << "Final Balance:    $" << results.final_balance << std::endl;
        std::cout << "Total P/L:        $" << results.total_profit_loss << std::endl;
        std::cout << "Total Trades:     " << results.total_trades << std::endl;
        std::cout << "Winning Trades:   " << results.winning_trades << std::endl;
        std::cout << "Losing Trades:    " << results.losing_trades << std::endl;
        std::cout << "Win Rate:         " << std::setprecision(1) << results.win_rate << "%" << std::endl;

        double return_pct = (results.total_profit_loss / results.initial_balance) * 100.0;
        std::cout << "Return:           " << std::setprecision(2) << return_pct << "%" << std::endl;

        std::cout << "\n✅ Test completed successfully!" << std::endl;
        std::cout << "\nNow run MT5 test with fill_up EA and compare results." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "\n❌ Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
