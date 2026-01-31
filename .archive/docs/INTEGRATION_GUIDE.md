# Integration Guide: MarginManager & SwapManager

## Quick Start - Integrating into BacktestEngine

This guide shows how to integrate the MT5-validated `MarginManager` and `SwapManager` classes into your existing backtest engine.

---

## Step 1: Update BacktestEngine Header

**File:** `include/backtest_engine.h`

Add the new includes and member variables:

```cpp
#ifndef BACKTEST_ENGINE_H
#define BACKTEST_ENGINE_H

#include "margin_manager.h"
#include "swap_manager.h"
// ... existing includes

class BacktestEngine {
private:
    // Account state
    double account_balance_;
    double account_equity_;
    double current_margin_used_;
    int leverage_;

    // Managers
    SwapManager swap_manager_;

    // ... existing members

public:
    BacktestEngine(double initial_balance, int leverage)
        : account_balance_(initial_balance),
          account_equity_(initial_balance),
          current_margin_used_(0.0),
          leverage_(leverage),
          swap_manager_(0)  // Swap at midnight
    {
        // ... initialization
    }

    // ... existing methods
};

#endif
```

---

## Step 2: Modify OpenPosition Method

**File:** `src/backtest_engine.cpp`

Update your position opening logic to check margin:

```cpp
bool BacktestEngine::OpenPosition(
    const std::string& symbol,
    double lot_size,
    bool is_buy,
    double stop_loss,
    double take_profit
) {
    // Get current market price
    double current_price = is_buy ? GetAskPrice(symbol) : GetBidPrice(symbol);

    // Calculate required margin
    double contract_size = GetContractSize(symbol);  // Usually 100,000 for FOREX
    double required_margin = MarginManager::CalculateMargin(
        lot_size,
        contract_size,
        current_price,
        leverage_
    );

    // Check if we have sufficient margin
    if (!MarginManager::HasSufficientMargin(
        account_equity_,
        current_margin_used_,
        required_margin,
        100.0  // Minimum margin level (100% = no margin call)
    )) {
        LogError("Insufficient margin to open position");
        LogInfo("Required: $" + std::to_string(required_margin));
        LogInfo("Available: $" + std::to_string(MarginManager::GetFreeMargin(
            account_equity_, current_margin_used_
        )));
        return false;
    }

    // Create position
    Position pos;
    pos.symbol = symbol;
    pos.lot_size = lot_size;
    pos.is_buy = is_buy;
    pos.open_price = current_price;
    pos.open_time = current_time_;
    pos.stop_loss = stop_loss;
    pos.take_profit = take_profit;
    pos.margin = required_margin;

    // Update margin tracking
    current_margin_used_ += required_margin;

    // Add to positions
    positions_.push_back(pos);

    LogInfo("Position opened: " + symbol + " " +
            (is_buy ? "BUY" : "SELL") + " " +
            std::to_string(lot_size) + " lots");
    LogInfo("Margin used: $" + std::to_string(required_margin));
    LogInfo("Total margin: $" + std::to_string(current_margin_used_));

    return true;
}
```

---

## Step 3: Update ClosePosition Method

Update position closing to free margin:

```cpp
void BacktestEngine::ClosePosition(Position& pos) {
    // Calculate P/L
    double close_price = pos.is_buy ? GetBidPrice(pos.symbol) : GetAskPrice(pos.symbol);
    double profit = CalculateProfit(pos, close_price);

    // Update account balance
    account_balance_ += profit;
    account_equity_ = account_balance_;  // Simplified (would include floating P/L)

    // Free up margin
    current_margin_used_ -= pos.margin;

    LogInfo("Position closed: " + pos.symbol + " " +
            (pos.is_buy ? "BUY" : "SELL") + " " +
            std::to_string(pos.lot_size) + " lots");
    LogInfo("Profit: $" + std::to_string(profit));
    LogInfo("Balance: $" + std::to_string(account_balance_));
    LogInfo("Margin freed: $" + std::to_string(pos.margin));

    // Remove from positions list
    // ... (your existing logic)
}
```

---

## Step 4: Add Swap Application to Main Loop

**File:** `src/backtest_engine.cpp`

In your main backtest loop, check for swap on each bar:

```cpp
void BacktestEngine::ProcessBar(const Bar& bar) {
    current_time_ = bar.timestamp;

    // Check if swap should be applied
    if (swap_manager_.ShouldApplySwap(current_time_)) {
        ApplySwapToAllPositions();
    }

    // Update equity (balance + floating P/L)
    UpdateAccountEquity();

    // Check for stop-loss and take-profit
    CheckStopLossTakeProfit(bar);

    // Your strategy logic here
    // ...
}

void BacktestEngine::ApplySwapToAllPositions() {
    int day_of_week = SwapManager::GetDayOfWeek(current_time_);

    LogInfo("Applying daily swap (day: " + std::to_string(day_of_week) + ")");

    for (auto& pos : positions_) {
        // Get symbol swap rates (you need to store these)
        SymbolInfo symbol_info = GetSymbolInfo(pos.symbol);

        double swap = SwapManager::CalculateSwap(
            pos.lot_size,
            pos.is_buy,
            symbol_info.swap_long,    // Points per lot (e.g., -0.5)
            symbol_info.swap_short,   // Points per lot (e.g., 0.3)
            symbol_info.point_value,  // Value of 1 point (e.g., 0.00001)
            symbol_info.contract_size,
            day_of_week
        );

        // Apply swap to position
        pos.swap += swap;
        account_balance_ += swap;

        LogInfo("  " + pos.symbol + ": $" + std::to_string(swap) +
                (day_of_week == 3 ? " (TRIPLE)" : ""));
    }

    account_equity_ = account_balance_;  // Update equity
}
```

---

## Step 5: Add Symbol Information Structure

**File:** `include/symbol_info.h` (create if needed)

```cpp
#ifndef SYMBOL_INFO_H
#define SYMBOL_INFO_H

struct SymbolInfo {
    std::string name;
    double contract_size;      // 100,000 for standard FOREX
    double point_value;        // 0.00001 for 5-digit pricing
    double swap_long;          // Swap for long positions (points)
    double swap_short;         // Swap for short positions (points)
    double min_lot;            // Minimum lot size (e.g., 0.01)
    double max_lot;            // Maximum lot size (e.g., 100.0)
    double lot_step;           // Lot size step (e.g., 0.01)

    // Constructor for EURUSD defaults
    SymbolInfo(const std::string& symbol = "EURUSD")
        : name(symbol),
          contract_size(100000.0),
          point_value(0.00001),
          swap_long(-0.5),      // Example: -$0.50 per lot per day
          swap_short(0.3),      // Example: +$0.30 per lot per day
          min_lot(0.01),
          max_lot(100.0),
          lot_step(0.01)
    {}
};

#endif
```

---

## Step 6: Update Position Structure

**File:** `include/position.h` (or wherever Position is defined)

```cpp
struct Position {
    std::string symbol;
    double lot_size;
    bool is_buy;
    double open_price;
    time_t open_time;
    double stop_loss;
    double take_profit;

    // NEW: Track margin and swap
    double margin;           // Margin reserved for this position
    double swap;             // Accumulated swap charges/credits

    Position()
        : lot_size(0),
          is_buy(true),
          open_price(0),
          open_time(0),
          stop_loss(0),
          take_profit(0),
          margin(0),
          swap(0)
    {}
};
```

---

## Step 7: Add Account Monitoring

Add methods to monitor account health:

```cpp
void BacktestEngine::UpdateAccountEquity() {
    // Calculate floating P/L
    double floating_pl = 0.0;
    for (const auto& pos : positions_) {
        double current_price = pos.is_buy ?
            GetBidPrice(pos.symbol) : GetAskPrice(pos.symbol);
        floating_pl += CalculateProfit(pos, current_price);
    }

    account_equity_ = account_balance_ + floating_pl;
}

double BacktestEngine::GetMarginLevel() const {
    return MarginManager::GetMarginLevel(
        account_equity_,
        current_margin_used_
    );
}

double BacktestEngine::GetFreeMargin() const {
    return MarginManager::GetFreeMargin(
        account_equity_,
        current_margin_used_
    );
}

bool BacktestEngine::CheckMarginCall() {
    double margin_level = GetMarginLevel();

    if (margin_level < 100.0 && margin_level > 0) {
        LogWarning("Low margin level: " + std::to_string(margin_level) + "%");
    }

    if (margin_level < 50.0 && margin_level > 0) {
        LogError("MARGIN CALL! Margin level: " + std::to_string(margin_level) + "%");
        // Close positions to restore margin level
        CloseWorstPosition();
        return true;
    }

    return false;
}
```

---

## Step 8: Example Usage in Main

**File:** `src/main.cpp`

```cpp
int main() {
    // Initialize engine
    double initial_balance = 10000.0;
    int leverage = 500;
    BacktestEngine engine(initial_balance, leverage);

    // Load historical data
    std::vector<Bar> bars = LoadHistoricalData("EURUSD", "2025-12-01", "2025-12-31");

    // Run backtest
    for (const auto& bar : bars) {
        engine.ProcessBar(bar);

        // Check margin call
        if (engine.CheckMarginCall()) {
            std::cout << "Margin call triggered!" << std::endl;
        }

        // Example strategy: Simple moving average crossover
        if (ShouldOpenPosition(bar)) {
            engine.OpenPosition("EURUSD", 0.01, true, bar.low - 0.001, bar.high + 0.001);
        }
    }

    // Print results
    std::cout << "Final balance: $" << engine.GetBalance() << std::endl;
    std::cout << "Total trades: " << engine.GetTotalTrades() << std::endl;
    std::cout << "Margin level: " << engine.GetMarginLevel() << "%" << std::endl;

    return 0;
}
```

---

## Testing Your Integration

### Test 1: Margin Calculation
Create a simple test to verify margin is calculated correctly:

```cpp
// Test margin calculation
BacktestEngine engine(10000.0, 500);
bool opened = engine.OpenPosition("EURUSD", 1.0, true, 0, 0);

// With 1:500 leverage and 1.0 lot at ~1.20 price:
// Expected margin = (1.0 * 100000 * 1.20) / 500 = $240
// Should have ~$240 margin used
assert(opened == true);
assert(engine.GetMarginUsed() > 230 && engine.GetMarginUsed() < 250);
```

### Test 2: Margin Limit
Test that margin check prevents overleveraging:

```cpp
BacktestEngine engine(100.0, 500);  // Only $100 balance
bool opened = engine.OpenPosition("EURUSD", 1.0, true, 0, 0);

// Should fail - needs ~$240 margin but only has $100
assert(opened == false);
```

### Test 3: Swap Application
Test that swap is applied daily:

```cpp
BacktestEngine engine(10000.0, 500);
engine.OpenPosition("EURUSD", 0.01, true, 0, 0);

double balance_before = engine.GetBalance();

// Simulate 2 days of bars
for (int day = 0; day < 2; day++) {
    for (int hour = 0; hour < 24; hour++) {
        Bar bar;
        bar.timestamp = start_time + (day * 86400) + (hour * 3600);
        engine.ProcessBar(bar);
    }
}

double balance_after = engine.GetBalance();
// Balance should have changed due to 2 days of swap
assert(balance_after != balance_before);
```

---

## Validation Against MT5

To verify your implementation matches MT5:

1. **Run same backtest in both engines:**
   - Same symbol (EURUSD)
   - Same period (Dec 2025)
   - Same lot sizes (0.01)
   - Same leverage (1:500)

2. **Compare results:**
   - Final balance should match within $0.10
   - Number of trades should match exactly
   - Margin calculations should match within $0.01
   - Swap charges should match within $0.01

3. **Expected differences:**
   - None! With zero slippage, results should be identical

---

## Troubleshooting

### Issue: Positions not opening
**Check:**
- Is leverage set correctly?
- Is margin calculation using correct contract size?
- Print margin values to debug

### Issue: Swap not applying
**Check:**
- Is `ShouldApplySwap()` being called in main loop?
- Is time advancing correctly in backtest?
- Check swap manager hour setting (should be 0)

### Issue: Margin call when shouldn't be
**Check:**
- Are you updating `account_equity_` with floating P/L?
- Is `current_margin_used_` being decremented on position close?
- Print margin level to debug

---

## Next Steps

1. ✅ Integrate MarginManager and SwapManager
2. ⏳ Run simple backtest and verify margin calculations
3. ⏳ Compare results with MT5 Test F data
4. ⏳ Implement full strategy and validate against MT5
5. ⏳ Achieve <1% difference in backtest results

---

**You now have everything needed to create an MT5-exact backtest engine!**

All formulas are validated, all timing is verified, and integration examples are provided. The next step is to integrate these classes into your existing BacktestEngine and start validating against real MT5 Strategy Tester results.
