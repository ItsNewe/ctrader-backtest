/**
 * Cross-Currency Conversion Rates Example
 *
 * STATUS: CONCEPTUAL EXAMPLE - Does not compile with current engine
 *
 * This example demonstrates the DESIGN for future cross-currency support.
 * The classes and methods shown here (BacktestEngine, MTConnector,
 * GetRequiredConversionPairs, etc.) are placeholders for documentation.
 *
 * For working tick-based backtesting, use:
 *   - TickBasedEngine (include/tick_based_engine.h)
 *   - FillUpOscillation strategy (include/fill_up_oscillation.h)
 *   - See tests/test_strategy_minimal.cpp for working example
 *
 * This example demonstrates the intended API for:
 * 1. Determining which conversion rates are needed
 * 2. Querying those rates from the broker
 * 3. Updating the engine with current rates
 * 4. Automatic cross-currency conversion in backtesting
 *
 * NOTE: The current TickBasedEngine handles single-currency pairs
 * (XAUUSD, XAGUSD) where account currency matches profit currency.
 */

#include "backtest_engine.h"
#include "metatrader_connector.h"
#include <iostream>

using namespace backtest;

void Example_SameCurrency() {
    std::cout << "===== Example 1: Same Currency (EURUSD with USD Account) =====\n\n";

    // Configure for EURUSD with USD account
    BacktestConfig config;
    config.account_currency = "USD";
    config.symbol_base = "EUR";
    config.symbol_quote = "USD";
    config.margin_currency = "EUR";
    config.profit_currency = "USD";

    BacktestEngine engine(config);

    // Check what conversion rates are needed
    auto required_pairs = engine.GetRequiredConversionPairs();
    std::cout << "Required conversion pairs: ";
    if (required_pairs.empty()) {
        std::cout << "None (simple conversion using symbol price)\n";
    } else {
        for (const auto& pair : required_pairs) {
            std::cout << pair << " ";
        }
        std::cout << "\n";
    }

    std::cout << "\nMargin conversion: EUR → USD using EURUSD price\n";
    std::cout << "Profit conversion: USD → USD (no conversion needed)\n";
    std::cout << "\n";
}

void Example_CrossCurrency() {
    std::cout << "===== Example 2: Cross-Currency (GBPJPY with USD Account) =====\n\n";

    // Configure for GBPJPY with USD account
    BacktestConfig config;
    config.account_currency = "USD";
    config.symbol_base = "GBP";
    config.symbol_quote = "JPY";
    config.margin_currency = "GBP";
    config.profit_currency = "JPY";

    BacktestEngine engine(config);

    // Check what conversion rates are needed
    auto required_pairs = engine.GetRequiredConversionPairs();
    std::cout << "Required conversion pairs:\n";
    for (const auto& pair : required_pairs) {
        std::cout << "  - " << pair << "\n";
    }
    std::cout << "\n";

    std::cout << "Margin conversion: GBP → USD requires GBPUSD rate\n";
    std::cout << "Profit conversion: JPY → USD requires USDJPY rate\n";
    std::cout << "\n";
}

void Example_QueryAndSetRates() {
    std::cout << "===== Example 3: Querying and Setting Conversion Rates =====\n\n";

    // Connect to broker
    mt::MTConnector connector;
    mt::MTConfig mt_config = mt::MTConfig::Demo("GridMarkets");
    mt_config.login = "000000";
    mt_config.password = "your_password";

    std::cout << "Connecting to broker...\n";
    if (!connector.Connect(mt_config)) {
        std::cerr << "Failed to connect\n";
        return;
    }

    // Configure for GBPJPY with USD account
    BacktestConfig config;
    config.account_currency = "USD";
    config.symbol_base = "GBP";
    config.symbol_quote = "JPY";
    config.margin_currency = "GBP";
    config.profit_currency = "JPY";

    BacktestEngine engine(config);

    // Get required conversion pairs
    auto required_pairs = engine.GetRequiredConversionPairs();

    std::cout << "\nQuerying conversion rates:\n";
    for (const auto& pair : required_pairs) {
        // Query symbol from broker
        mt::MTSymbol symbol = connector.GetSymbolInfo(pair);

        std::cout << "  " << pair << " @ " << symbol.bid << "\n";

        // Update engine with this rate
        engine.UpdateConversionRateFromSymbol(pair, symbol.bid);
    }

    std::cout << "\nConversion rates updated!\n";
    std::cout << "Engine ready for cross-currency backtesting\n";
    std::cout << "\n";

    connector.Disconnect();
}

void Example_CompleteWorkflow() {
    std::cout << "===== Example 4: Complete Cross-Currency Workflow =====\n\n";

    // Step 1: Connect to broker
    mt::MTConnector connector;
    mt::MTConfig mt_config = mt::MTConfig::Demo("GridMarkets");
    mt_config.login = "000000";
    mt_config.password = "your_password";

    std::cout << "Step 1: Connecting to broker...\n";
    if (!connector.Connect(mt_config)) {
        std::cerr << "Failed to connect\n";
        return;
    }

    // Step 2: Get account info
    std::cout << "Step 2: Querying account information...\n";
    mt::MTAccount account = connector.GetAccountInfo();
    std::cout << "  Account currency: " << account.currency << "\n";
    std::cout << "  Leverage: 1:" << account.leverage << "\n\n";

    // Step 3: Get main symbol info (GBPJPY)
    std::cout << "Step 3: Querying main symbol (GBPJPY)...\n";
    mt::MTSymbol gbpjpy = connector.GetSymbolInfo("GBPJPY");
    std::cout << "  Base: " << gbpjpy.currency_base << "\n";
    std::cout << "  Quote: " << gbpjpy.currency_profit << "\n";
    std::cout << "  Current price: " << gbpjpy.bid << "\n\n";

    // Step 4: Configure backtest
    std::cout << "Step 4: Configuring backtest engine...\n";
    BacktestConfig config;
    config.account_currency = account.currency;
    config.leverage = account.leverage;
    config.symbol_base = gbpjpy.currency_base;
    config.symbol_quote = gbpjpy.currency_profit;
    config.margin_currency = gbpjpy.currency_margin;
    config.profit_currency = gbpjpy.currency_profit;
    config.lot_size = gbpjpy.contract_size;
    config.point_value = gbpjpy.point;

    BacktestEngine engine(config);

    // Step 5: Get required conversion pairs
    std::cout << "Step 5: Determining required conversion rates...\n";
    auto required_pairs = engine.GetRequiredConversionPairs();
    std::cout << "  Need to query: ";
    for (const auto& pair : required_pairs) {
        std::cout << pair << " ";
    }
    std::cout << "\n\n";

    // Step 6: Query and set conversion rates
    std::cout << "Step 6: Querying conversion rates...\n";
    for (const auto& pair : required_pairs) {
        mt::MTSymbol symbol = connector.GetSymbolInfo(pair);
        std::cout << "  " << pair << ": " << symbol.bid << "\n";
        engine.UpdateConversionRateFromSymbol(pair, symbol.bid);
    }
    std::cout << "\n";

    // Step 7: Load price data
    std::cout << "Step 7: Loading price data...\n";
    // (In real usage, load bars from CSV or broker)
    std::cout << "  (Skipped for example)\n\n";

    // Step 8: Ready to run backtest
    std::cout << "Step 8: Engine configured and ready!\n";
    std::cout << "  Account: USD\n";
    std::cout << "  Symbol: GBPJPY\n";
    std::cout << "  Margin conversion: GBP → USD (via GBPUSD)\n";
    std::cout << "  Profit conversion: JPY → USD (via USDJPY)\n";
    std::cout << "  All conversions will happen automatically\n\n";

    std::cout << "Ready to run: auto result = engine.RunBacktest(strategy, params)\n\n";

    connector.Disconnect();
}

void Example_ManualRateUpdate() {
    std::cout << "===== Example 5: Manual Rate Updates =====\n\n";

    BacktestConfig config;
    config.account_currency = "USD";
    config.symbol_base = "GBP";
    config.symbol_quote = "JPY";

    BacktestEngine engine(config);

    std::cout << "Setting rates manually (without broker connection):\n\n";

    // Set GBP/USD rate manually
    std::cout << "  GBPUSD @ 1.3000\n";
    engine.UpdateConversionRate("GBP", 1.3000);

    // Set USD/JPY rate manually
    std::cout << "  USDJPY @ 110.00 → JPY rate = " << (1.0/110.0) << "\n";
    engine.UpdateConversionRate("JPY", 1.0/110.0);

    std::cout << "\nRates set! Engine ready for backtesting.\n";
    std::cout << "Note: Rates should be updated periodically for accuracy\n\n";
}

void Example_RateUpdateDuringBacktest() {
    std::cout << "===== Example 6: Updating Rates During Backtest =====\n\n";

    BacktestConfig config;
    config.account_currency = "USD";
    config.symbol_base = "GBP";
    config.symbol_quote = "JPY";

    BacktestEngine engine(config);

    std::cout << "Simulating rate updates during backtest:\n\n";

    // Initial rates (start of day)
    std::cout << "09:00 - Initial rates:\n";
    std::cout << "  GBPUSD: 1.3000, USDJPY: 110.00\n";
    engine.UpdateConversionRate("GBP", 1.3000);
    engine.UpdateConversionRate("JPY", 1.0/110.0);

    // Update after 4 hours
    std::cout << "\n13:00 - Updated rates (4 hours later):\n";
    std::cout << "  GBPUSD: 1.3050, USDJPY: 110.25\n";
    engine.UpdateConversionRate("GBP", 1.3050);
    engine.UpdateConversionRate("JPY", 1.0/110.25);

    // Update end of day
    std::cout << "\n17:00 - Final rates:\n";
    std::cout << "  GBPUSD: 1.3020, USDJPY: 110.15\n";
    engine.UpdateConversionRate("GBP", 1.3020);
    engine.UpdateConversionRate("JPY", 1.0/110.15);

    std::cout << "\nNote: More frequent updates = more accurate conversions\n";
    std::cout << "For high-frequency trading, update every tick\n";
    std::cout << "For daily backtests, update once per day is sufficient\n\n";
}

int main() {
    std::cout << "Cross-Currency Conversion Rates Examples\n";
    std::cout << "========================================\n\n";

    Example_SameCurrency();
    Example_CrossCurrency();
    Example_QueryAndSetRates();
    Example_CompleteWorkflow();
    Example_ManualRateUpdate();
    Example_RateUpdateDuringBacktest();

    std::cout << "========================================\n";
    std::cout << "Examples complete!\n\n";

    std::cout << "Key Takeaways:\n";
    std::cout << "1. Use GetRequiredConversionPairs() to determine what rates are needed\n";
    std::cout << "2. Query those symbols from broker using GetSymbolInfo()\n";
    std::cout << "3. Update engine with UpdateConversionRateFromSymbol()\n";
    std::cout << "4. Engine handles all conversions automatically\n";
    std::cout << "5. Update rates periodically for accuracy\n";
    std::cout << "6. Same-currency pairs work without any rate queries\n\n";

    return 0;
}

/**
 * Expected Output:
 *
 * Cross-Currency Conversion Rates Examples
 * ========================================
 *
 * ===== Example 1: Same Currency (EURUSD with USD Account) =====
 *
 * Required conversion pairs: None (simple conversion using symbol price)
 *
 * Margin conversion: EUR → USD using EURUSD price
 * Profit conversion: USD → USD (no conversion needed)
 *
 * ===== Example 2: Cross-Currency (GBPJPY with USD Account) =====
 *
 * Required conversion pairs:
 *   - GBPUSD
 *   - USDJPY
 *
 * Margin conversion: GBP → USD requires GBPUSD rate
 * Profit conversion: JPY → USD requires USDJPY rate
 *
 * ===== Example 3: Querying and Setting Conversion Rates =====
 *
 * Connecting to broker...
 *
 * Querying conversion rates:
 *   GBPUSD @ 1.30253
 *   USDJPY @ 110.145
 *
 * Conversion rates updated!
 * Engine ready for cross-currency backtesting
 *
 * ... (additional examples)
 *
 * ========================================
 * Examples complete!
 *
 * Key Takeaways:
 * 1. Use GetRequiredConversionPairs() to determine what rates are needed
 * 2. Query those symbols from broker using GetSymbolInfo()
 * 3. Update engine with UpdateConversionRateFromSymbol()
 * 4. Engine handles all conversions automatically
 * 5. Update rates periodically for accuracy
 * 6. Same-currency pairs work without any rate queries
 */
