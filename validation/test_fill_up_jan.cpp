#include "../include/fill_up_strategy.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <fstream>

using namespace backtest;

int main() {
    std::cout << "=== Fill-Up Grid Trading Strategy Test (January 2025 Only) ===" << std::endl;
    std::cout << std::fixed << std::setprecision(2);

    // Configure tick data source - FULL FILE (will stop after January ticks)
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
    config.tick_data_config = tick_config;

    std::cout << "\nBacktest Configuration:" << std::endl;
    std::cout << "Symbol: " << config.symbol << std::endl;
    std::cout << "Period: 2025.01.01 - 2025.01.31 (January only)" << std::endl;
    std::cout << "Initial Balance: $" << config.initial_balance << std::endl;
    std::cout << "Tick Data: " << tick_config.file_path << std::endl;
    std::cout << "Mode: Every tick based on real ticks (streaming)" << std::endl;

    // Strategy parameters from INI file
    double survive = 13.0;  // 13% drawdown tolerance
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

        // Run backtest - but stop after January
        std::cout << "\n--- Starting Tick-by-Tick Execution (January 2025) ---\n" << std::endl;

        int tick_count = 0;
        std::time_t jan_end = 1738367999;  // 2025-01-31 23:59:59 UTC

        engine.Run([&strategy, &tick_count, jan_end](const Tick& tick, TickBasedEngine& engine) {
            // Convert tick time string to timestamp and check if still in January
            // Format: "2025.01.02 01:00:02.600"
            int year, month, day;
            sscanf(tick.timestamp.c_str(), "%d.%d.%d", &year, &month, &day);

            if (year > 2025 || (year == 2025 && month > 1)) {
                // Past January, stop processing
                std::cout << "\nReached end of January 2025. Stopping backtest." << std::endl;
                return; // This will just skip remaining ticks
            }

            strategy.OnTick(tick, engine);
            tick_count++;
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
        std::cout << "Total Ticks Processed: " << tick_count << std::endl;

        if (trades.size() > 0) {
            std::cout << "\nFirst 10 Trades:" << std::endl;
            for (size_t i = 0; i < std::min(trades.size(), (size_t)10); i++) {
                const auto& trade = trades[i];
                std::cout << "Trade #" << (i + 1) << ": "
                          << trade.GetDirectionStr() << " "
                          << trade.lot_size << " lots @ "
                          << trade.entry_price << " -> "
                          << trade.exit_price << " ("
                          << trade.exit_reason << ") P/L: $"
                          << trade.profit_loss << std::endl;
            }

            if (trades.size() > 10) {
                std::cout << "\nLast 10 Trades:" << std::endl;
                size_t start = trades.size() - 10;
                for (size_t i = start; i < trades.size(); i++) {
                    const auto& trade = trades[i];
                    std::cout << "Trade #" << (i + 1) << ": "
                              << trade.GetDirectionStr() << " "
                              << trade.lot_size << " lots @ "
                              << trade.entry_price << " -> "
                              << trade.exit_price << " ("
                              << trade.exit_reason << ") P/L: $"
                              << trade.profit_loss << std::endl;
                }
            }
        }

        // Summary
        auto results = engine.GetResults();
        std::cout << "\n=== Final Results (January 2025) ===" << std::endl;
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

        // Save to file for comparison
        std::ofstream outfile("FILL_UP_RESULTS_JAN2025.txt");
        outfile << std::fixed << std::setprecision(2);
        outfile << "=== Fill-Up Strategy - January 2025 Results ===" << std::endl;
        outfile << "Initial Balance: $" << results.initial_balance << std::endl;
        outfile << "Final Balance: $" << results.final_balance << std::endl;
        outfile << "Total P/L: $" << results.total_profit_loss << std::endl;
        outfile << "Return: " << return_pct << "%" << std::endl;
        outfile << "Total Trades: " << results.total_trades << std::endl;
        outfile << "Win Rate: " << results.win_rate << "%" << std::endl;
        outfile << "Max Balance: $" << strategy.GetMaxBalance() << std::endl;
        outfile << "Max Open Positions: " << strategy.GetMaxNumberOfOpen() << std::endl;
        outfile << "Max Trade Size: " << strategy.GetMaxTradeSize() << " lots" << std::endl;
        outfile.close();

        std::cout << "\n✓ Test completed successfully!" << std::endl;
        std::cout << "Results saved to: FILL_UP_RESULTS_JAN2025.txt" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "\n❌ Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
