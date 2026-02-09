#include "../include/fill_up_strategy.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <fstream>

using namespace backtest;

// Simple tick counter wrapper
class TickCounter {
public:
    int max_ticks;
    int current_tick;
    bool should_stop;

    TickCounter(int max) : max_ticks(max), current_tick(0), should_stop(false) {}

    bool ProcessTick() {
        current_tick++;
        if (current_tick >= max_ticks) {
            should_stop = true;
            return false;
        }
        return true;
    }
};

int main() {
    std::cout << "=== Fill-Up Grid Trading Strategy - Short Validation Test ===" << std::endl;
    std::cout << std::fixed << std::setprecision(2);

    // Configure tick data source
    TickDataConfig tick_config;
    tick_config.file_path = "XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;  // Streaming mode

    // Configure backtest to match MT5 settings
    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = 110000.0;
    config.account_currency = "USD";
    config.commission_per_lot = 0.0;
    config.slippage_pips = 0.0;
    config.use_bid_ask_spread = true;
    config.tick_data_config = tick_config;

    std::cout << "\nBacktest Configuration:" << std::endl;
    std::cout << "Symbol: " << config.symbol << std::endl;
    std::cout << "Period: First 500,000 ticks (~1 week)" << std::endl;
    std::cout << "Initial Balance: $" << config.initial_balance << std::endl;
    std::cout << "Mode: Every tick based on real ticks" << std::endl;

    // Strategy parameters from INI file
    double survive = 13.0;
    double size = 1.0;
    double spacing = 1.0;
    double min_volume = 0.01;
    double max_volume = 100.0;
    double contract_size = 100.0;
    double leverage = 500.0;
    int symbol_digits = 2;

    std::cout << "\nStrategy Parameters:" << std::endl;
    std::cout << "Survive: " << survive << "% | Size: " << size
              << " | Spacing: $" << spacing << std::endl;
    std::cout << "Leverage: 1:" << leverage << " | Contract: " << contract_size << std::endl;

    try {
        // Create engine
        TickBasedEngine engine(config);

        // Create strategy
        FillUpStrategy strategy(survive, size, spacing, min_volume, max_volume,
                                contract_size, leverage, symbol_digits);

        // Create tick counter
        TickCounter counter(500000);  // Process first 500K ticks

        std::cout << "\n--- Starting Tick-by-Tick Execution ---\n" << std::endl;

        engine.Run([&strategy, &counter](const Tick& tick, TickBasedEngine& engine) {
            if (!counter.ProcessTick()) {
                return;
            }
            strategy.OnTick(tick, engine);
        });

        // Print results
        auto results = engine.GetResults();
        const auto& trades = engine.GetClosedTrades();

        std::cout << "\n=== Short Test Results ===" << std::endl;
        std::cout << "Ticks Processed: " << counter.current_tick << std::endl;
        std::cout << "Initial Balance:  $" << results.initial_balance << std::endl;
        std::cout << "Final Balance:    $" << results.final_balance << std::endl;
        std::cout << "Total P/L:        $" << results.total_profit_loss << std::endl;
        std::cout << "Total Trades:     " << results.total_trades << std::endl;
        std::cout << "Win Rate:         " << std::setprecision(1) << results.win_rate << "%" << std::endl;

        double return_pct = (results.total_profit_loss / results.initial_balance) * 100.0;
        std::cout << "Return:           " << std::setprecision(2) << return_pct << "%" << std::endl;

        std::cout << "\n=== Strategy Stats ===" << std::endl;
        std::cout << "Max Balance: $" << strategy.GetMaxBalance() << std::endl;
        std::cout << "Max Open Positions: " << strategy.GetMaxNumberOfOpen() << std::endl;
        std::cout << "Max Trade Size: " << strategy.GetMaxTradeSize() << " lots" << std::endl;

        if (trades.size() > 0) {
            std::cout << "\nFirst 5 Trades:" << std::endl;
            for (size_t i = 0; i < std::min(trades.size(), (size_t)5); i++) {
                const auto& trade = trades[i];
                std::cout << "#" << (i + 1) << ": " << trade.direction << " "
                          << trade.lot_size << " lots @ " << trade.entry_price
                          << " -> " << trade.exit_price << " | P/L: $"
                          << trade.profit_loss << std::endl;
            }

            if (trades.size() > 5) {
                std::cout << "\nLast 5 Trades:" << std::endl;
                size_t start = trades.size() - 5;
                for (size_t i = start; i < trades.size(); i++) {
                    const auto& trade = trades[i];
                    std::cout << "#" << (i + 1) << ": " << trade.direction << " "
                              << trade.lot_size << " lots @ " << trade.entry_price
                              << " -> " << trade.exit_price << " | P/L: $"
                              << trade.profit_loss << std::endl;
                }
            }
        }

        std::cout << "\n✓ Short validation test completed!" << std::endl;
        std::cout << "Ready to run full year test." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "\n❌ Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
