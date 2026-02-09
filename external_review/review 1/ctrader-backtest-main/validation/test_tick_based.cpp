#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>

using namespace backtest;

/**
 * Test: SimplePriceLevelBreakout Strategy with Tick Data
 *
 * This test replicates the SimplePriceLevelBreakout strategy using tick-by-tick execution
 * for maximum accuracy comparison against MT5 "Every tick" mode.
 */

class TickBasedPriceLevelStrategy {
public:
    TickBasedPriceLevelStrategy(double long_trigger, double short_trigger,
                                double lot_size, int sl_pips, int tp_pips)
        : long_trigger_(long_trigger),
          short_trigger_(short_trigger),
          lot_size_(lot_size),
          sl_pips_(sl_pips),
          tp_pips_(tp_pips),
          long_triggered_(false),
          short_triggered_(false) {
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        // Get mid price for trigger logic
        double price = tick.mid();

        // Check if we have open positions
        const auto& open_positions = engine.GetOpenPositions();

        // Long trigger
        if (!long_triggered_ && price >= long_trigger_ && open_positions.empty()) {
            long_triggered_ = true;

            // Calculate SL and TP
            double sl = tick.ask - (sl_pips_ * 0.0001);
            double tp = tick.ask + (tp_pips_ * 0.0001);

            engine.OpenMarketOrder("BUY", lot_size_, sl, tp);
        }

        // Short trigger
        if (!short_triggered_ && price <= short_trigger_ && open_positions.empty()) {
            short_triggered_ = true;

            // Calculate SL and TP
            double sl = tick.bid + (sl_pips_ * 0.0001);
            double tp = tick.bid - (tp_pips_ * 0.0001);

            engine.OpenMarketOrder("SELL", lot_size_, sl, tp);
        }

        // Reset triggers when no positions (allow re-entry)
        if (open_positions.empty()) {
            if (price < long_trigger_ - 0.0010) {  // 10 pips below
                long_triggered_ = false;
            }
            if (price > short_trigger_ + 0.0010) {  // 10 pips above
                short_triggered_ = false;
            }
        }
    }

private:
    double long_trigger_;
    double short_trigger_;
    double lot_size_;
    int sl_pips_;
    int tp_pips_;
    bool long_triggered_;
    bool short_triggered_;
};

int main() {
    std::cout << "=== Tick-Based Backtest Test ===" << std::endl;
    std::cout << std::fixed << std::setprecision(5);

    // Configure tick data source
    TickDataConfig tick_config;
    tick_config.file_path = "EURUSD_TICKS_JAN2024_ONLY.csv";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;  // Streaming mode for large tick files

    // Configure backtest
    TickBacktestConfig config;
    config.symbol = "EURUSD";
    config.initial_balance = 10000.0;
    config.account_currency = "USD";
    config.commission_per_lot = 0.0;
    config.slippage_pips = 0.0;  // No slippage for exact MT5 comparison
    config.use_bid_ask_spread = true;
    config.tick_data_config = tick_config;

    std::cout << "\nBacktest Configuration:" << std::endl;
    std::cout << "Symbol: " << config.symbol << std::endl;
    std::cout << "Initial Balance: $" << config.initial_balance << std::endl;
    std::cout << "Tick Data: " << tick_config.file_path << std::endl;
    std::cout << "Mode: Streaming (memory efficient)" << std::endl;

    // Create strategy (using same levels as bar-based test for comparison)
    TickBasedPriceLevelStrategy strategy(
        1.0950,  // Long trigger (price must cross above)
        1.0850,  // Short trigger (price must cross below)
        0.10,    // Lot size
        50,      // SL pips
        100      // TP pips
    );

    std::cout << "\nStrategy Parameters:" << std::endl;
    std::cout << "Long Trigger:  1.0950" << std::endl;
    std::cout << "Short Trigger: 1.0850" << std::endl;
    std::cout << "Lot Size:      0.10" << std::endl;
    std::cout << "Stop Loss:     50 pips" << std::endl;
    std::cout << "Take Profit:   100 pips" << std::endl;

    try {
        // Create engine
        TickBasedEngine engine(config);

        // Run backtest
        std::cout << "\n--- Starting Tick-by-Tick Execution ---\n" << std::endl;

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& engine) {
            strategy.OnTick(tick, engine);
        });

        // Print detailed results
        std::cout << "\n=== Detailed Trade Results ===" << std::endl;
        const auto& trades = engine.GetClosedTrades();

        for (size_t i = 0; i < trades.size(); i++) {
            const auto& trade = trades[i];
            std::cout << "\nTrade #" << (i + 1) << ":" << std::endl;
            std::cout << "  Direction:   " << trade.direction << std::endl;
            std::cout << "  Entry Time:  " << trade.entry_time << std::endl;
            std::cout << "  Entry Price: " << trade.entry_price << std::endl;
            std::cout << "  Exit Time:   " << trade.exit_time << std::endl;
            std::cout << "  Exit Price:  " << trade.exit_price << std::endl;
            std::cout << "  Exit Reason: " << trade.exit_reason << std::endl;
            std::cout << "  P/L:         $" << trade.profit_loss << std::endl;
        }

        // Summary statistics
        auto results = engine.GetResults();
        std::cout << "\n=== Summary Statistics ===" << std::endl;
        std::cout << "Total Trades:     " << results.total_trades << std::endl;
        std::cout << "Winning Trades:   " << results.winning_trades << std::endl;
        std::cout << "Losing Trades:    " << results.losing_trades << std::endl;
        std::cout << "Win Rate:         " << std::setprecision(2) << results.win_rate << "%" << std::endl;
        std::cout << std::setprecision(2);
        std::cout << "Average Win:      $" << results.average_win << std::endl;
        std::cout << "Average Loss:     $" << results.average_loss << std::endl;
        std::cout << "Largest Win:      $" << results.largest_win << std::endl;
        std::cout << "Largest Loss:     $" << results.largest_loss << std::endl;

        std::cout << "\n=== Final Results ===" << std::endl;
        std::cout << std::setprecision(2);
        std::cout << "Initial Balance:  $" << results.initial_balance << std::endl;
        std::cout << "Final Balance:    $" << results.final_balance << std::endl;
        std::cout << "Total P/L:        $" << results.total_profit_loss << std::endl;

        // Calculate return percentage
        double return_pct = (results.total_profit_loss / results.initial_balance) * 100.0;
        std::cout << "Return:           " << return_pct << "%" << std::endl;

        std::cout << "\n✅ Test completed successfully!" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "\n❌ Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
