/**
 * Currency Conversion & Trading Limits Example
 *
 * This example demonstrates:
 * 1. Querying broker-specific parameters
 * 2. Configuring BacktestEngine with cross-currency support
 * 3. Automatic currency conversion for margin and profit
 * 4. Trading limits validation (lot size, SL/TP distance)
 */

#include "backtest_engine.h"
#include "metatrader_connector.h"
#include <iostream>

using namespace backtest;

int main() {
    std::cout << "Currency Conversion & Trading Limits Example\n";
    std::cout << "============================================\n\n";

    // ========================================
    // Step 1: Connect to Broker and Query Parameters
    // ========================================

    MTConnector connector;
    MTConfig mt_config = MTConfig::Demo("GridMarkets");
    mt_config.login = "000000";
    mt_config.password = "your_password";

    if (!connector.Connect(mt_config)) {
        std::cerr << "Failed to connect to broker\n";
        return 1;
    }

    // Query account information
    MTAccount account = connector.GetAccountInfo();
    std::cout << "Account Information:\n";
    std::cout << "  Currency: " << account.currency << "\n";
    std::cout << "  Leverage: 1:" << account.leverage << "\n";
    std::cout << "  Margin Call Level: " << account.margin_call_level << "%\n";
    std::cout << "  Stop Out Level: " << account.stop_out_level << "%\n\n";

    // Query EURUSD symbol information
    MTSymbol eurusd = connector.GetSymbolInfo("EURUSD");
    std::cout << "EURUSD Symbol Information:\n";
    std::cout << "  Base Currency: " << eurusd.currency_base << "\n";
    std::cout << "  Profit Currency: " << eurusd.currency_profit << "\n";
    std::cout << "  Margin Currency: " << eurusd.currency_margin << "\n";
    std::cout << "  Contract Size: " << eurusd.contract_size << "\n";
    std::cout << "  Volume Min: " << eurusd.volume_min << "\n";
    std::cout << "  Volume Max: " << eurusd.volume_max << "\n";
    std::cout << "  Volume Step: " << eurusd.volume_step << "\n";
    std::cout << "  Stops Level: " << eurusd.stops_level << " points\n";
    std::cout << "  Margin Mode: " << static_cast<int>(eurusd.margin_mode) << "\n";
    std::cout << "  Triple Swap Day: " << eurusd.swap_rollover3days << " (0=Sun, 3=Wed)\n\n";

    // ========================================
    // Step 2: Configure Backtest with Broker Data
    // ========================================

    BacktestConfig config;

    // From account
    config.account_currency = account.currency;
    config.leverage = account.leverage;
    config.margin_call_level = account.margin_call_level;
    config.stop_out_level = account.stop_out_level;

    // From symbol - basic parameters
    config.lot_size = eurusd.contract_size;
    config.point_value = eurusd.point;
    config.spread_points = eurusd.spread;

    // From symbol - currency information
    config.symbol_base = eurusd.currency_base;
    config.symbol_quote = eurusd.currency_profit;
    config.margin_currency = eurusd.currency_margin;
    config.profit_currency = eurusd.currency_profit;

    // From symbol - trading limits
    config.volume_min = eurusd.volume_min;
    config.volume_max = eurusd.volume_max;
    config.volume_step = eurusd.volume_step;
    config.stops_level = eurusd.stops_level;

    // From symbol - margin and swap
    config.margin_mode = static_cast<int>(eurusd.margin_mode);
    config.triple_swap_day = eurusd.swap_rollover3days;
    config.swap_long_per_lot = eurusd.swap_long;
    config.swap_short_per_lot = eurusd.swap_short;

    // Backtest parameters
    config.mode = BacktestMode::BAR_BY_BAR;
    config.initial_balance = 10000.0;
    config.commission_per_lot = 7.0;

    std::cout << "BacktestConfig Populated:\n";
    std::cout << "  Account Currency: " << config.account_currency << "\n";
    std::cout << "  Margin Currency: " << config.margin_currency << "\n";
    std::cout << "  Profit Currency: " << config.profit_currency << "\n\n";

    // ========================================
    // Step 3: Create Engine (Currency Conversion Automatic)
    // ========================================

    BacktestEngine engine(config);
    std::cout << "BacktestEngine created with:\n";
    std::cout << "  - CurrencyConverter for " << config.account_currency << "\n";
    std::cout << "  - PositionValidator with broker limits\n";
    std::cout << "  - SwapManager with triple swap on day " << config.triple_swap_day << "\n\n";

    // ========================================
    // Step 4: Demonstrate Currency Conversion
    // ========================================

    std::cout << "Currency Conversion Example:\n";
    std::cout << "----------------------------\n";

    // Example: USD account trading EURUSD @ 1.2000
    double eurusd_price = 1.2000;
    double lot_size = 0.01;

    // Margin calculation (in EUR, then converted to USD)
    double margin_eur = (lot_size * config.lot_size * eurusd_price) / config.leverage;
    double margin_usd = margin_eur * eurusd_price;  // EUR → USD conversion

    std::cout << "  Position: 0.01 lots EURUSD @ 1.2000\n";
    std::cout << "  Margin in EUR: $" << margin_eur << "\n";
    std::cout << "  Margin in USD: $" << margin_usd << " (converted)\n";
    std::cout << "  Conversion rate used: EURUSD price (1.2000)\n\n";

    // Profit calculation (already in USD, no conversion needed)
    std::cout << "  Profit currency: USD (same as account)\n";
    std::cout << "  No conversion needed for profit\n\n";

    // ========================================
    // Step 5: Demonstrate Trading Limits Validation
    // ========================================

    std::cout << "Trading Limits Validation:\n";
    std::cout << "-------------------------\n";

    // Valid lot size
    std::cout << "  Valid lot sizes: ";
    for (double vol : {0.01, 0.02, 0.05, 0.10, 1.00}) {
        std::string error;
        bool valid = PositionValidator::ValidateLotSize(
            vol, config.volume_min, config.volume_max,
            config.volume_step, &error
        );
        std::cout << vol << (valid ? " ✓ " : " ✗ ");
    }
    std::cout << "\n";

    // Invalid lot sizes
    std::cout << "  Invalid lot sizes: ";
    for (double vol : {0.005, 0.035, 150.0}) {
        std::string error;
        bool valid = PositionValidator::ValidateLotSize(
            vol, config.volume_min, config.volume_max,
            config.volume_step, &error
        );
        std::cout << vol << (valid ? " ✓ " : " ✗ ");
    }
    std::cout << "\n\n";

    // Stop distance validation
    double entry_price = 1.2000;
    int stops_level = eurusd.stops_level;

    std::cout << "  Stop Distance Validation (stops_level=" << stops_level << "):\n";
    std::cout << "    Entry: 1.2000\n";

    // Valid SL (10 points away)
    double sl_valid = 1.1990;
    int distance = static_cast<int>(std::abs(entry_price - sl_valid) / config.point_value);
    std::cout << "    SL @ 1.1990: " << distance << " points - ";
    std::cout << (distance >= stops_level ? "✓ Valid" : "✗ Invalid") << "\n";

    // Invalid SL (5 points away)
    double sl_invalid = 1.1995;
    distance = static_cast<int>(std::abs(entry_price - sl_invalid) / config.point_value);
    std::cout << "    SL @ 1.1995: " << distance << " points - ";
    std::cout << (distance >= stops_level ? "✓ Valid" : "✗ Invalid") << "\n\n";

    // ========================================
    // Step 6: Cross-Currency Example (GBPJPY)
    // ========================================

    std::cout << "Cross-Currency Example (GBPJPY with USD account):\n";
    std::cout << "------------------------------------------------\n";

    // Query GBPJPY
    MTSymbol gbpjpy = connector.GetSymbolInfo("GBPJPY");
    std::cout << "  Symbol: GBPJPY @ 150.00\n";
    std::cout << "  Base: " << gbpjpy.currency_base << "\n";
    std::cout << "  Quote: " << gbpjpy.currency_profit << "\n";
    std::cout << "  Account: USD\n\n";

    std::cout << "  Margin conversion: GBP → USD (via GBPUSD)\n";
    std::cout << "  Profit conversion: JPY → USD (via USDJPY)\n";
    std::cout << "  Both conversions handled automatically by engine\n\n";

    std::cout << "  NOTE: For cross-currency pairs, query additional rates:\n";
    std::cout << "    MTSymbol gbpusd = connector.GetSymbolInfo(\"GBPUSD\");\n";
    std::cout << "    MTSymbol usdjpy = connector.GetSymbolInfo(\"USDJPY\");\n\n";

    // ========================================
    // Step 7: Load Data and Run Backtest
    // ========================================

    std::cout << "Ready to Run Backtest:\n";
    std::cout << "---------------------\n";
    std::cout << "  Engine configured with:\n";
    std::cout << "    ✓ Broker-specific parameters\n";
    std::cout << "    ✓ Currency conversion\n";
    std::cout << "    ✓ Trading limits validation\n";
    std::cout << "    ✓ Cross-currency support\n\n";

    std::cout << "  To run backtest:\n";
    std::cout << "    1. Load price data: engine.LoadBars(bars)\n";
    std::cout << "    2. Create strategy: IStrategy* strategy = new MyStrategy()\n";
    std::cout << "    3. Run backtest: auto result = engine.RunBacktest(strategy, params)\n\n";

    std::cout << "  Engine will automatically:\n";
    std::cout << "    - Validate lot sizes before opening positions\n";
    std::cout << "    - Validate SL/TP distances\n";
    std::cout << "    - Convert margin to account currency\n";
    std::cout << "    - Convert profit to account currency\n";
    std::cout << "    - Check margin requirements\n";
    std::cout << "    - Reject invalid positions\n\n";

    connector.Disconnect();
    std::cout << "Example complete!\n";

    return 0;
}

/**
 * Expected Output:
 *
 * Currency Conversion & Trading Limits Example
 * ============================================
 *
 * Account Information:
 *   Currency: USD
 *   Leverage: 1:500
 *   Margin Call Level: 100%
 *   Stop Out Level: 50%
 *
 * EURUSD Symbol Information:
 *   Base Currency: EUR
 *   Profit Currency: USD
 *   Margin Currency: EUR
 *   Contract Size: 100000
 *   Volume Min: 0.01
 *   Volume Max: 100
 *   Volume Step: 0.01
 *   Stops Level: 10 points
 *   Margin Mode: 0
 *   Triple Swap Day: 3 (0=Sun, 3=Wed)
 *
 * BacktestConfig Populated:
 *   Account Currency: USD
 *   Margin Currency: EUR
 *   Profit Currency: USD
 *
 * BacktestEngine created with:
 *   - CurrencyConverter for USD
 *   - PositionValidator with broker limits
 *   - SwapManager with triple swap on day 3
 *
 * Currency Conversion Example:
 * ----------------------------
 *   Position: 0.01 lots EURUSD @ 1.2000
 *   Margin in EUR: $2.4
 *   Margin in USD: $2.88 (converted)
 *   Conversion rate used: EURUSD price (1.2000)
 *
 *   Profit currency: USD (same as account)
 *   No conversion needed for profit
 *
 * Trading Limits Validation:
 * -------------------------
 *   Valid lot sizes: 0.01 ✓ 0.02 ✓ 0.05 ✓ 0.10 ✓ 1.00 ✓
 *   Invalid lot sizes: 0.005 ✗ 0.035 ✗ 150.0 ✗
 *
 *   Stop Distance Validation (stops_level=10):
 *     Entry: 1.2000
 *     SL @ 1.1990: 10 points - ✓ Valid
 *     SL @ 1.1995: 5 points - ✗ Invalid
 *
 * Cross-Currency Example (GBPJPY with USD account):
 * ------------------------------------------------
 *   Symbol: GBPJPY @ 150.00
 *   Base: GBP
 *   Quote: JPY
 *   Account: USD
 *
 *   Margin conversion: GBP → USD (via GBPUSD)
 *   Profit conversion: JPY → USD (via USDJPY)
 *   Both conversions handled automatically by engine
 *
 *   NOTE: For cross-currency pairs, query additional rates:
 *     MTSymbol gbpusd = connector.GetSymbolInfo("GBPUSD");
 *     MTSymbol usdjpy = connector.GetSymbolInfo("USDJPY");
 *
 * Ready to Run Backtest:
 * ---------------------
 *   Engine configured with:
 *     ✓ Broker-specific parameters
 *     ✓ Currency conversion
 *     ✓ Trading limits validation
 *     ✓ Cross-currency support
 *
 *   To run backtest:
 *     1. Load price data: engine.LoadBars(bars)
 *     2. Create strategy: IStrategy* strategy = new MyStrategy()
 *     3. Run backtest: auto result = engine.RunBacktest(strategy, params)
 *
 *   Engine will automatically:
 *     - Validate lot sizes before opening positions
 *     - Validate SL/TP distances
 *     - Convert margin to account currency
 *     - Convert profit to account currency
 *     - Check margin requirements
 *     - Reject invalid positions
 *
 * Example complete!
 */
