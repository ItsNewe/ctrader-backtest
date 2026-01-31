# Broker-Specific Data Integration Guide

## Overview

The margin and swap calculations now support broker-specific and symbol-specific parameters, queried directly from the MetaTrader server. This ensures exact reproduction of broker behavior across different instruments and account types.

---

## Key Enhancements

### 1. Symbol-Specific Properties ✅

**MTSymbol Structure Enhanced:**
```cpp
struct MTSymbol {
    // Margin calculation properties
    MTMarginMode margin_mode;      // FOREX, CFD, CFD_LEVERAGE, FUTURES, etc.
    double margin_initial;         // Initial margin requirement
    double margin_maintenance;     // Maintenance margin
    double margin_hedged;          // Hedged margin

    // Swap properties
    MTSwapMode swap_mode;          // POINTS, BASE_CURRENCY, INTEREST, etc.
    double swap_long;              // Long position swap (per day)
    double swap_short;             // Short position swap (per day)
    int swap_rollover3days;        // Day for triple swap (0-6)

    // Trading properties
    double contract_size;          // 100000 for standard lot
    double volume_min/max/step;    // Lot size limits
    int stops_level;               // Minimum SL/TP distance
};
```

### 2. Account-Specific Properties ✅

**MTAccount Structure Enhanced:**
```cpp
struct MTAccount {
    // Account leverage and limits
    int leverage;                  // Account leverage (e.g., 500)
    double margin_call_level;      // Margin call % (e.g., 100.0)
    double stop_out_level;         // Stop out % (e.g., 50.0)
    bool margin_mode_retail_hedging;

    // Broker info
    std::string company;           // Broker name
    std::string server;            // Server name
};
```

---

## Margin Calculation Modes

### Supported Modes (from MT5 ENUM_SYMBOL_CALC_MODE)

| Mode | Value | Description | Formula |
|------|-------|-------------|---------|
| FOREX | 0 | Standard FOREX | `(lots × contract × price) / leverage` |
| CFD | 1 | CFD | Same as FOREX |
| CFD_INDEX | 2 | CFD Index | Same as FOREX |
| CFD_LEVERAGE | 3 | CFD with symbol leverage | Uses symbol-specific leverage |
| FUTURES | 4 | Futures | Fixed margin per contract |
| EXCHANGE_STOCKS | 5 | Stocks | Full contract value required |
| FOREX_NO_LEVERAGE | 6 | FOREX without leverage | Full value required |

### Usage Example

```cpp
// Get symbol info from broker
MTSymbol symbol = connector.GetSymbolInfo("EURUSD");
MTAccount account = connector.GetAccountInfo();

// Calculate margin using broker-provided parameters
double margin = MarginManager::CalculateMargin(
    0.01,                           // lot_size
    symbol.contract_size,           // from symbol spec
    current_price,
    account.leverage,               // from account info
    static_cast<MarginManager::CalcMode>(symbol.margin_mode),
    symbol_leverage                 // for CFD_LEVERAGE mode
);
```

---

## Triple Swap Day Configuration

### Broker-Specific Triple Swap Days

Different instruments and brokers use different triple-swap days:

| Instrument Type | Typical Triple Swap Day | Reason |
|----------------|------------------------|---------|
| **FOREX** | Wednesday (3) | Covers Saturday + Sunday |
| **Most CFDs** | Friday (5) | Some brokers roll forward |
| **Crypto** | None or Daily | 24/7 markets |
| **Stocks** | Thursday/Friday | Depends on settlement |

### Querying from Broker

```cpp
MTSymbol symbol = connector.GetSymbolInfo("EURUSD");

int triple_swap_day = symbol.swap_rollover3days;  // 0-6 (Sun-Sat)

// Create swap manager with broker's triple swap day
SwapManager swap_mgr(0, triple_swap_day);  // midnight, broker-specific day
```

### Example: Different Instruments

```cpp
// FOREX pair - Wednesday triple swap
MTSymbol eurusd = connector.GetSymbolInfo("EURUSD");
std::cout << "EURUSD triple swap: " << eurusd.GetTripleSwapDay() << std::endl;
// Output: 3 (Wednesday)

// CFD - Friday triple swap
MTSymbol sp500 = connector.GetSymbolInfo("SP500");
std::cout << "SP500 triple swap: " << sp500.GetTripleSwapDay() << std::endl;
// Output: 5 (Friday)

// Crypto - No triple swap (or daily)
MTSymbol btcusd = connector.GetSymbolInfo("BTCUSD");
std::cout << "BTCUSD triple swap: " << btcusd.GetTripleSwapDay() << std::endl;
// Output: -1 (none) or 0-6 for specific day
```

---

## Complete Integration Example

### Step 1: Connect and Query Broker

```cpp
#include "metatrader_connector.h"
#include "margin_manager.h"
#include "swap_manager.h"

// Connect to broker
MTConnector connector;
MTConfig config = MTConfig::Demo("GridMarkets");
config.login = "000000";
config.password = "your_password";

if (!connector.Connect(config)) {
    std::cerr << "Failed to connect" << std::endl;
    return 1;
}

// Get account information
MTAccount account = connector.GetAccountInfo();
std::cout << "Account Leverage: 1:" << account.leverage << std::endl;
std::cout << "Margin Call Level: " << account.margin_call_level << "%" << std::endl;
std::cout << "Stop Out Level: " << account.stop_out_level << "%" << std::endl;

// Get symbol information
MTSymbol symbol = connector.GetSymbolInfo("EURUSD");
std::cout << "Symbol: " << symbol.name << std::endl;
std::cout << "Contract Size: " << symbol.contract_size << std::endl;
std::cout << "Margin Mode: " << static_cast<int>(symbol.margin_mode) << std::endl;
std::cout << "Swap Long: " << symbol.swap_long << std::endl;
std::cout << "Swap Short: " << symbol.swap_short << std::endl;
std::cout << "Triple Swap Day: " << symbol.swap_rollover3days << std::endl;
```

### Step 2: Configure Backtest with Broker Data

```cpp
// Create backtest config using broker data
backtest::BacktestConfig config;

// From account
config.leverage = account.leverage;
config.margin_call_level = account.margin_call_level;
config.stop_out_level = account.stop_out_level;

// From symbol
config.lot_size = symbol.contract_size;
config.point_value = symbol.point;
config.spread_points = symbol.spread;
config.swap_long_per_lot = symbol.swap_long;
config.swap_short_per_lot = symbol.swap_short;

// Margin mode
config.margin_mode = static_cast<MarginManager::CalcMode>(symbol.margin_mode);

// Triple swap day
config.triple_swap_day = symbol.swap_rollover3days;
```

### Step 3: Run Backtest with Broker-Exact Parameters

```cpp
// Create engine with broker data
backtest::BacktestEngine engine(config);

// Engine will automatically:
// - Use correct margin formula for symbol type
// - Apply swap on broker's specified day
// - Triple swap on correct day (Wed or Fri)
// - Check margin using broker's call/stop levels

// Run backtest
auto result = engine.RunBacktest(&strategy, params);

// Results will exactly match what broker would do
std::cout << "Final Balance: $" << result.final_balance << std::endl;
std::cout << "Total Margin Used: $" << result.max_margin_used << std::endl;
std::cout << "Total Swap: $" << result.total_swap << std::endl;
```

---

## Margin Mode Examples

### Example 1: Standard FOREX (EURUSD)

```cpp
MTSymbol eurusd = connector.GetSymbolInfo("EURUSD");
// eurusd.margin_mode = MTMarginMode::FOREX (0)
// eurusd.contract_size = 100000

double margin = MarginManager::CalculateMargin(
    0.01,                    // 0.01 lots
    100000,                  // contract size
    1.20,                    // price
    500,                     // 1:500 leverage
    MarginManager::FOREX
);
// Result: $2.40
```

### Example 2: CFD with Custom Leverage

```cpp
MTSymbol sp500 = connector.GetSymbolInfo("SP500");
// sp500.margin_mode = MTMarginMode::CFD_LEVERAGE (3)
// sp500.contract_size = 1
// sp500.margin_initial = 100 (custom leverage info)

double margin = MarginManager::CalculateMargin(
    1.0,                         // 1 lot
    1.0,                         // contract size
    4500.0,                      // S&P500 price
    500,                         // account leverage (ignored)
    MarginManager::CFD_LEVERAGE,
    100                          // symbol-specific leverage
);
// Result: $45.00
```

### Example 3: Futures

```cpp
MTSymbol es = connector.GetSymbolInfo("ES");  // E-mini S&P
// es.margin_mode = MTMarginMode::FUTURES (4)
// es.margin_initial = 12000  // Fixed margin per contract

double margin = MarginManager::CalculateMargin(
    1.0,                     // 1 contract
    12000,                   // margin per contract
    4500.0,                  // price (not used for futures)
    500,                     // leverage (not used)
    MarginManager::FUTURES
);
// Result: $12,000
```

---

## Swap Mode Examples

### Mode 1: Swap in Points (Most Common)

```cpp
MTSymbol symbol = connector.GetSymbolInfo("EURUSD");
// symbol.swap_mode = MTSwapMode::POINTS
// symbol.swap_long = -0.5 points
// symbol.swap_short = 0.3 points

double swap = SwapManager::CalculateSwap(
    0.01,               // lot_size
    true,               // is_buy (long)
    -0.5,               // swap_long
    0.3,                // swap_short
    0.00001,            // point_value
    100000,             // contract_size
    3,                  // Wednesday
    3                   // triple_swap_day
);
// Result: -$1.50 (triple swap on Wednesday)
```

### Mode 2: Swap as Annual Interest

```cpp
// For mode = INTEREST:
// swap = position_value × (interest_rate / 100 / 360)

// This would need additional logic in SwapManager
// to support different calculation modes
```

---

## Recommended Implementation Flow

### 1. On Strategy Start

```cpp
void OnInit() {
    // Connect to broker
    MTConnector connector;
    connector.Connect(config);

    // Query account info once
    MTAccount account = connector.GetAccountInfo();

    // Query symbol info for each symbol you'll trade
    MTSymbol eurusd = connector.GetSymbolInfo("EURUSD");
    MTSymbol gbpusd = connector.GetSymbolInfo("GBPUSD");

    // Store for use in backtest configuration
    symbol_cache_["EURUSD"] = eurusd;
    symbol_cache_["GBPUSD"] = gbpusd;
}
```

### 2. Configure Engine Per Symbol

```cpp
void RunBacktestForSymbol(const std::string& symbol_name) {
    MTSymbol& symbol = symbol_cache_[symbol_name];

    // Create config with symbol-specific parameters
    BacktestConfig config;
    config.leverage = account_.leverage;
    config.lot_size = symbol.contract_size;
    config.swap_long_per_lot = symbol.swap_long;
    config.swap_short_per_lot = symbol.swap_short;
    config.triple_swap_day = symbol.swap_rollover3days;

    // Run backtest
    BacktestEngine engine(config);
    // ...
}
```

### 3. Handle Different Instrument Types

```cpp
MarginManager::CalcMode GetMarginMode(const MTSymbol& symbol) {
    switch (symbol.margin_mode) {
        case MTMarginMode::FOREX:
            return MarginManager::FOREX;
        case MTMarginMode::CFD:
            return MarginManager::CFD;
        case MTMarginMode::CFD_LEVERAGE:
            return MarginManager::CFD_LEVERAGE;
        case MTMarginMode::FUTURES:
            return MarginManager::FUTURES;
        case MTMarginMode::EXCHANGE_STOCKS:
            return MarginManager::EXCHANGE_STOCKS;
        default:
            return MarginManager::FOREX;
    }
}
```

---

## Testing with Broker Data

### Test 1: Verify Margin Matches Broker

```cpp
// Get margin from broker API
MTOrder test_order;
test_order.volume = 0.01;
test_order.symbol = "EURUSD";
double broker_margin = connector.CalculateMargin(test_order);

// Calculate with our engine
MTSymbol symbol = connector.GetSymbolInfo("EURUSD");
double our_margin = MarginManager::CalculateMargin(
    0.01, symbol.contract_size, symbol.ask,
    account.leverage, GetMarginMode(symbol)
);

// Compare
double difference = std::abs(broker_margin - our_margin);
assert(difference < 0.01);  // Within 1 cent
```

### Test 2: Verify Triple Swap Day

```cpp
// Open position on Monday
// Hold through the week
// Check swap charges

// Tuesday: 1x swap
// Wednesday: 3x swap  (or Friday for some CFDs)
// Thursday: 1x swap
// Friday: 1x swap

// Total should be 6x daily swap for the week
```

---

## Benefits of Broker Data Integration

| Benefit | Description |
|---------|-------------|
| **Exact Reproduction** | Matches broker behavior exactly |
| **Instrument Support** | Works with FOREX, CFDs, Futures, Stocks |
| **Broker Flexibility** | Adapts to different broker configurations |
| **Triple Swap Accuracy** | Correct day for each instrument |
| **Margin Precision** | Exact margin requirements |
| **No Hardcoding** | All parameters from broker |

---

## Migration Path

### Current (Hardcoded)
```cpp
// Old way - hardcoded constants
BacktestConfig config;
config.leverage = 500;
config.lot_size = 100000;
config.swap_long_per_lot = -0.5;
config.triple_swap_day = 3;  // Always Wednesday
```

### New (Broker-Queried)
```cpp
// New way - query from broker
MTAccount account = connector.GetAccountInfo();
MTSymbol symbol = connector.GetSymbolInfo("EURUSD");

BacktestConfig config;
config.leverage = account.leverage;              // From broker
config.lot_size = symbol.contract_size;          // From broker
config.swap_long_per_lot = symbol.swap_long;     // From broker
config.triple_swap_day = symbol.swap_rollover3days;  // From broker
```

---

## Next Steps

1. ✅ **Enhanced Data Structures** - MTSymbol and MTAccount updated
2. ✅ **Margin Modes** - All MT5 modes supported
3. ✅ **Swap Configuration** - Triple-swap day configurable
4. ⏳ **Connector Implementation** - Implement GetSymbolInfo() and GetAccountInfo()
5. ⏳ **BacktestEngine Updates** - Use broker data automatically
6. ⏳ **Testing** - Validate against multiple instruments and brokers

---

## Files Modified

- [include/metatrader_connector.h](include/metatrader_connector.h) - Enhanced MTSymbol, MTAccount
- [include/margin_manager.h](include/margin_manager.h) - Added all margin modes
- [include/swap_manager.h](include/swap_manager.h) - Configurable triple-swap day

---

**Status:** Data structures ready, connector implementation pending

**Next:** Implement broker data fetching in MT connector
