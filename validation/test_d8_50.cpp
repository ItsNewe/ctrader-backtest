/**
 * Test D_8_50 Configuration
 * - Survive Down: 8%
 * - Spacing: $1.00
 * - DD Protection: 50%
 *
 * Uses the proper TickBasedEngine with:
 * - Swap calculation ( rates)
 * - Margin stop-out (20% level)
 * - Date filtering (MT5 behavior)
 */

#include <iostream>
#include <iomanip>

#include "../include/fill_up_strategy.h"
#include "../include/tick_based_engine.h"

using namespace backtest;

int main(int argc, char* argv[]) {
    std::ios_base::sync_with_stdio(true);
    std::cout << "=== D_8_50 Fill-Up Strategy Test ===" << std::endl;
    std::cout << "Survive: 8%, Spacing: $1.00, DD Protection: 50%" << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    std::cout.flush();

    // Parse optional command line args
    double survive = 8.0;
    double spacing = 1.0;
    double dd_threshold = 50.0;
    bool enable_dd = false;  // Default: disabled to match baseline

    if (argc >= 2) survive = std::atof(argv[1]);
    if (argc >= 3) spacing = std::atof(argv[2]);
    if (argc >= 4) {
        dd_threshold = std::atof(argv[3]);
        enable_dd = (dd_threshold > 0);
    }

    // Configure tick data source
    TickDataConfig tick_config;
    // Use absolute path for reliability (can be overridden by env)
    #ifdef _WIN32
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    #else
    tick_config.file_path = "C:/Users/user/Documents/ctrader-backtest/validation/Grid/XAUUSD_TICKS_2025.csv";
    #endif
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;  // Streaming mode

    // Configure backtest -  XAUUSD settings
    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = 10000.0;  // $10,000 starting balance
    config.account_currency = "USD";
    config.commission_per_lot = 0.0;
    config.slippage_pips = 0.0;
    config.use_bid_ask_spread = true;
    config.contract_size = 100.0;  // XAUUSD contract size
    config.leverage = 500.0;       // 1:500 leverage
    config.margin_rate = 1.0;
    config.pip_size = 0.01;        // XAUUSD pip size

    // Date filtering (MT5 behavior: start inclusive, end exclusive)
    config.start_date = "2025.01.01";
    config.end_date = "2025.12.30";

    // Swap rates from MT5 API -  XAUUSD
    // These are in SYMBOL_SWAP_MODE_POINTS (mode 1)
    config.swap_long = -66.99;   // -$66.99/lot/day for long positions
    config.swap_short = 41.2;    // +$41.20/lot/day for short positions (not used here)
    config.swap_mode = 1;        // SYMBOL_SWAP_MODE_POINTS
    config.swap_3days = 3;       // Triple swap charged Thursday (for Wednesday rollover)

    config.tick_data_config = tick_config;

    std::cout << "\nConfiguration:" << std::endl;
    std::cout << "  Symbol: " << config.symbol << std::endl;
    std::cout << "  Period: 2025.01.01 - 2025.12.29" << std::endl;
    std::cout << "  Initial Balance: $" << config.initial_balance << std::endl;
    std::cout << "  Leverage: 1:" << config.leverage << std::endl;
    std::cout << "  Swap Long: " << config.swap_long << " points/lot/day" << std::endl;
    std::cout << "  Swap Mode: " << config.swap_mode << std::endl;

    std::cout << "\nStrategy Parameters:" << std::endl;
    std::cout << "  Survive Down: " << survive << "%" << std::endl;
    std::cout << "  Spacing: $" << spacing << std::endl;
    std::cout << "  DD Protection: " << (enable_dd ? "ON at " + std::to_string(dd_threshold) + "%" : "OFF") << std::endl;

    // Strategy parameters
    double size_multiplier = 1.0;
    double min_volume = 0.01;
    double max_volume = 10.0;
    double contract_size = 100.0;
    double leverage = 500.0;
    int symbol_digits = 2;

    try {
        // Create engine
        TickBasedEngine engine(config);

        // Create strategy with DD protection
        FillUpStrategy strategy(survive, size_multiplier, spacing,
                                min_volume, max_volume, contract_size,
                                leverage, symbol_digits, 1.0,
                                enable_dd, dd_threshold);

        // Run backtest
        std::cout << "\n--- Starting Tick-by-Tick Backtest ---\n" << std::endl;

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& engine) {
            strategy.OnTick(tick, engine);
        });

        // Get results
        auto results = engine.GetResults();

        // Print results
        std::cout << "\n=== Results ===" << std::endl;
        std::cout << "Initial Balance:  $" << results.initial_balance << std::endl;
        std::cout << "Final Balance:    $" << results.final_balance << std::endl;
        std::cout << "Total P/L:        $" << results.total_profit_loss << std::endl;
        std::cout << "Return:           " << (results.final_balance / results.initial_balance) << "x" << std::endl;
        std::cout << "Max Drawdown:     $" << results.max_drawdown << std::endl;
        std::cout << "Total Trades:     " << results.total_trades << std::endl;
        std::cout << "Win Rate:         " << results.win_rate << "%" << std::endl;
        std::cout << "Total Swap:       $" << results.total_swap_charged << std::endl;

        std::cout << "\n=== Strategy Stats ===" << std::endl;
        std::cout << "Max Balance:      $" << strategy.GetMaxBalance() << std::endl;
        std::cout << "Max Open Pos:     " << strategy.GetMaxNumberOfOpen() << std::endl;
        std::cout << "Max Trade Size:   " << strategy.GetMaxTradeSize() << " lots" << std::endl;
        std::cout << "DD Triggers:      " << strategy.GetDDTriggers() << std::endl;
        std::cout << "Peak Equity:      $" << strategy.GetPeakEquity() << std::endl;

        if (engine.IsStopOutOccurred()) {
            std::cout << "\n!!! MARGIN STOP-OUT OCCURRED !!!" << std::endl;
        }

        std::cout << "\nTest completed." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "\nError: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
