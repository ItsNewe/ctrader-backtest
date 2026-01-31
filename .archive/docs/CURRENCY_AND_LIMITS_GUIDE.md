# Currency Conversion & Trading Limits Guide

## Overview

This guide covers all the additional broker parameters that affect margin calculation, profit conversion, and position validation:

1. **Currency Conversion** - When account currency ≠ symbol currency
2. **Trading Limits** - Min/max lot size, lot step, stops level
3. **Margin Specifications** - Initial/maintenance margin for futures
4. **Cross-Currency Pairs** - Handling complex currency conversions

---

## Problem Statement

### When Account Currency Differs from Symbol Currencies

**Example 1: USD Account Trading EURUSD**
```
Account Currency: USD
Symbol: EURUSD
Base Currency: EUR
Quote Currency: USD

Margin Calculation:
  - Margin is in EUR (base currency)
  - Need to convert EUR → USD using EURUSD rate
  - Margin_USD = Margin_EUR × EURUSD_price

Profit Calculation:
  - Profit is in USD (quote currency)
  - No conversion needed (already in account currency)
```

**Example 2: EUR Account Trading USDJPY**
```
Account Currency: EUR
Symbol: USDJPY
Base Currency: USD
Quote Currency: JPY

Margin Calculation:
  - Margin is in USD (base currency)
  - Need to convert USD → EUR using EURUSD rate
  - Margin_EUR = Margin_USD / EURUSD_price

Profit Calculation:
  - Profit is in JPY (quote currency)
  - Need to convert JPY → EUR using EURJPY rate
  - Profit_EUR = Profit_JPY / EURJPY_price
```

**Example 3: USD Account Trading GBPJPY**
```
Account Currency: USD
Symbol: GBPJPY
Base Currency: GBP
Quote Currency: JPY

Margin Calculation:
  - Margin is in GBP (base currency)
  - Need to convert GBP → USD using GBPUSD rate
  - Margin_USD = Margin_GBP × GBPUSD_price

Profit Calculation:
  - Profit is in JPY (quote currency)
  - Need to convert JPY → USD using USDJPY rate
  - Profit_USD = Profit_JPY / USDJPY_price
```

---

## Solution: Currency Converter

### Implementation

**File:** [include/currency_converter.h](include/currency_converter.h)

```cpp
// Create converter for account
CurrencyConverter converter("USD");  // USD account

// Set current rates (query from broker or use current prices)
converter.SetRate("EUR", 1.20);      // 1 USD = 1.20 EUR (or use 1/EURUSD)
converter.SetRate("GBP", 1.30);      // 1 USD = 1.30 GBP
converter.SetRate("JPY", 0.009);     // 1 USD = 0.009 JPY (or 1/USDJPY × 100)

// Convert margin from EUR to USD
double margin_eur = 100.0;
double margin_usd = converter.ConvertMargin(margin_eur, "EUR", 1.20);
// Result: 120.0 USD

// Convert profit from JPY to USD
double profit_jpy = 10000.0;
double profit_usd = converter.ConvertProfit(profit_jpy, "JPY", 110.0);
// Result: 90.91 USD
```

### Integration with BacktestConfig

```cpp
BacktestConfig config;

// Set currencies from symbol
config.account_currency = "USD";
config.symbol_base = "EUR";       // From EURUSD
config.symbol_quote = "USD";      // From EURUSD
config.margin_currency = "EUR";   // Usually base
config.profit_currency = "USD";   // Usually quote

// Engine uses these for automatic conversion
```

---

## Trading Limits Validation

### Symbol Specifications

All symbols have trading limits from broker:

```cpp
struct MTSymbol {
    double volume_min;    // e.g., 0.01 (micro lots)
    double volume_max;    // e.g., 100.0 (standard lots)
    double volume_step;   // e.g., 0.01 (lot increment)
    int stops_level;      // e.g., 10 points (min SL/TP distance)
};
```

### Validation Rules

**1. Lot Size Must Be Within Range**
```cpp
// MIN ≤ lot_size ≤ MAX
if (lot_size < volume_min) {
    // Error: Below minimum
}
if (lot_size > volume_max) {
    // Error: Exceeds maximum
}
```

**2. Lot Size Must Be Valid Step**
```cpp
// lot_size must be: volume_min + N × volume_step
double steps = (lot_size - volume_min) / volume_step;
if (steps != floor(steps)) {
    // Error: Not a valid step multiple
}

// Examples:
// volume_min=0.01, volume_step=0.01
// Valid: 0.01, 0.02, 0.03, 0.10, 1.00
// Invalid: 0.005, 0.015, 0.025
```

**3. Stop Loss / Take Profit Minimum Distance**
```cpp
// Distance in points must be >= stops_level
int distance_points = abs(entry_price - sl_price) / point_value;
if (distance_points < stops_level) {
    // Error: Too close to entry
}

// Example: stops_level = 10 points
// EURUSD @ 1.2000, point_value = 0.0001
// Valid SL: 1.1990 (10 points away)
// Invalid SL: 1.1995 (5 points away)
```

### Using PositionValidator

**File:** [include/position_validator.h](include/position_validator.h)

```cpp
#include "position_validator.h"

// Validate lot size
std::string error;
bool valid = PositionValidator::ValidateLotSize(
    0.03,          // requested lot size
    0.01,          // volume_min
    100.0,         // volume_max
    0.01,          // volume_step
    &error
);

if (!valid) {
    std::cout << "Invalid: " << error << std::endl;
}

// Normalize lot size to valid value
double normalized = PositionValidator::NormalizeLotSize(
    0.037,         // requested (invalid step)
    0.01,          // volume_min
    100.0,         // volume_max
    0.01           // volume_step
);
// Result: 0.04 (rounded to nearest step)
```

### Comprehensive Validation

```cpp
std::string error;
bool valid = PositionValidator::ValidatePosition(
    0.03,          // lot_size
    1.2000,        // entry_price
    1.1950,        // stop_loss
    1.2100,        // take_profit
    true,          // is_buy
    36.0,          // required_margin
    500.0,         // available_margin
    0.01,          // volume_min
    100.0,         // volume_max
    0.01,          // volume_step
    10,            // stops_level
    0.0001,        // point_value
    &error
);

if (!valid) {
    std::cout << "Cannot open position: " << error << std::endl;
}
```

---

## Margin Specifications for FUTURES

### Problem

Futures don't use leverage-based margin. They have fixed margin per contract:

```cpp
MTSymbol es;  // E-mini S&P 500
es.margin_mode = MTMarginMode::FUTURES;
es.margin_initial = 12000.0;      // Initial margin: $12,000
es.margin_maintenance = 11000.0;  // Maintenance margin: $11,000
```

### Usage

```cpp
// For FUTURES, use margin_initial directly
if (symbol.margin_mode == MTMarginMode::FUTURES) {
    double margin = symbol.margin_initial * lot_size;
    // 1 contract = $12,000
    // 2 contracts = $24,000
}

// For FOREX/CFD, calculate with leverage
else {
    double margin = MarginManager::CalculateMargin(
        lot_size, contract_size, price, leverage
    );
}
```

### Maintenance Margin

```cpp
// During backtest, check if equity falls below maintenance
if (current_equity < position.margin_maintenance) {
    // Margin call!
    // In real trading, broker might close position
}
```

---

## Complete Configuration Example

### Querying from Broker

```cpp
// Connect to broker
MTConnector connector;
connector.Connect(config);

// Get account info
MTAccount account = connector.GetAccountInfo();
// - currency: "USD"
// - leverage: 500

// Get symbol info
MTSymbol eurusd = connector.GetSymbolInfo("EURUSD");
// - contract_size: 100000
// - volume_min: 0.01
// - volume_max: 100.0
// - volume_step: 0.01
// - stops_level: 10
// - margin_mode: FOREX (0)
// - swap_long: -0.5
// - swap_short: 0.3
// - swap_rollover3days: 3 (Wednesday)
// - currency_base: "EUR"
// - currency_profit: "USD"
// - currency_margin: "EUR"
```

### Populating BacktestConfig

```cpp
backtest::BacktestConfig config;

// From account
config.initial_balance = account.balance;
config.leverage = account.leverage;
config.margin_call_level = account.margin_call_level;
config.stop_out_level = account.stop_out_level;
config.account_currency = account.currency;

// From symbol - basic
config.lot_size = eurusd.contract_size;
config.point_value = eurusd.point;
config.spread_points = eurusd.spread;

// From symbol - swap
config.swap_long_per_lot = eurusd.swap_long;
config.swap_short_per_lot = eurusd.swap_short;
config.triple_swap_day = eurusd.swap_rollover3days;

// From symbol - margin
config.margin_mode = static_cast<int>(eurusd.margin_mode);
config.symbol_leverage = /* extract if CFD_LEVERAGE */;
config.margin_initial = eurusd.margin_initial;
config.margin_maintenance = eurusd.margin_maintenance;

// From symbol - limits
config.volume_min = eurusd.volume_min;
config.volume_max = eurusd.volume_max;
config.volume_step = eurusd.volume_step;
config.stops_level = eurusd.stops_level;

// From symbol - currencies
config.symbol_base = eurusd.currency_base;
config.symbol_quote = eurusd.currency_profit;
config.margin_currency = eurusd.currency_margin;
config.profit_currency = eurusd.currency_profit;
```

---

## Cross-Currency Calculation Examples

### Example 1: USD Account, EURUSD

```
Account: USD
Symbol: EURUSD @ 1.2000
Lot Size: 0.01
Leverage: 500

Step 1: Calculate margin in EUR (base currency)
margin_eur = (0.01 × 100000 × 1.2000) / 500
          = 1200 / 500
          = 2.40 EUR

Step 2: Convert EUR to USD (account currency)
margin_usd = 2.40 × 1.2000  (multiply by EURUSD)
          = 2.88 USD

Step 3: Profit calculation
profit_pips = 10 pips
profit_usd = 0.01 × 10 × 1.0  (already in USD, no conversion)
          = 0.10 USD
```

### Example 2: EUR Account, USDJPY

```
Account: EUR
Symbol: USDJPY @ 110.00
Lot Size: 0.01
Leverage: 500
EURUSD rate: 1.2000

Step 1: Calculate margin in USD (base currency)
margin_usd = (0.01 × 100000 × 110.00) / 500
          = 110000 / 500
          = 220.00 USD

Step 2: Convert USD to EUR (account currency)
margin_eur = 220.00 / 1.2000  (divide by EURUSD)
          = 183.33 EUR

Step 3: Profit calculation (in JPY)
profit_pips = 100 pips (100 points)
profit_jpy = 0.01 × 100 × 1000 = 1000 JPY

Step 4: Convert JPY to EUR
eurjpy_rate = 132.00  (EURUSD × USDJPY)
profit_eur = 1000 / 132.00
          = 7.58 EUR
```

### Example 3: USD Account, GBPJPY

```
Account: USD
Symbol: GBPJPY @ 150.00
Lot Size: 0.01
Leverage: 500
GBPUSD rate: 1.3000
USDJPY rate: 110.00

Step 1: Calculate margin in GBP (base currency)
margin_gbp = (0.01 × 100000 × 150.00) / 500
          = 150000 / 500
          = 300.00 GBP

Step 2: Convert GBP to USD (account currency)
margin_usd = 300.00 × 1.3000  (multiply by GBPUSD)
          = 390.00 USD

Step 3: Profit calculation (in JPY)
profit_jpy = 1000 JPY (example)

Step 4: Convert JPY to USD
profit_usd = 1000 / 110.00  (divide by USDJPY)
          = 9.09 USD
```

---

## Implementation Status

### ✅ Completed

1. **Currency Converter** - [currency_converter.h](include/currency_converter.h)
2. **Position Validator** - [position_validator.h](include/position_validator.h)
3. **BacktestConfig Enhanced** - All symbol specifications added
4. **Documentation** - This guide

### ⏳ Next Steps

1. **Integrate CurrencyConverter into BacktestEngine**
   - Add converter member
   - Use in OpenPosition() for margin conversion
   - Use in ClosePosition() for profit conversion

2. **Integrate PositionValidator into BacktestEngine**
   - Validate lot size before opening
   - Validate SL/TP distances
   - Provide error messages to strategy

3. **Update Connector to Query All Parameters**
   - Implement full GetSymbolInfo()
   - Return all fields (currencies, limits, margins)

4. **Add Currency Rate Cache**
   - Query required conversion rates from broker
   - Cache for performance
   - Update periodically

---

## Testing Checklist

### Test 1: Same Currency (No Conversion)
```
Account: USD
Symbol: EURUSD
Expected: No conversion needed for profit (already USD)
```

### Test 2: Cross-Currency Margin
```
Account: USD
Symbol: GBPJPY
Expected: Margin conversion via GBPUSD rate
```

### Test 3: Cross-Currency Profit
```
Account: USD
Symbol: GBPJPY
Expected: Profit conversion via USDJPY rate
```

### Test 4: Lot Size Validation
```
Request: 0.03 lots
volume_min: 0.01
volume_step: 0.01
Expected: Valid (exact multiple)

Request: 0.035 lots
Expected: Invalid (not a multiple of 0.01)
```

### Test 5: Stop Distance Validation
```
EURUSD @ 1.2000
SL @ 1.1995
stops_level: 10 points
Distance: 5 points
Expected: Invalid (below minimum)
```

---

## Files Summary

| File | Purpose | Status |
|------|---------|--------|
| [include/currency_converter.h](include/currency_converter.h) | Currency conversion logic | ✅ Complete |
| [include/position_validator.h](include/position_validator.h) | Lot size & distance validation | ✅ Complete |
| [include/backtest_engine.h](include/backtest_engine.h) | Enhanced config with all params | ✅ Complete |
| [CURRENCY_AND_LIMITS_GUIDE.md](CURRENCY_AND_LIMITS_GUIDE.md) | This documentation | ✅ Complete |

---

**Next:** Integrate currency converter and validator into BacktestEngine position opening logic.
