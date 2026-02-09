# Triple Swap (3-Day Rollover) Implementation

## Overview

MT5 charges **triple swap on Wednesday** to cover weekend overnight fees (Friday night, Saturday night, and Sunday night). This implementation adds full support for configurable triple swap to the tick-based backtesting engine.

## Implementation Details

### Configuration Parameter

Added to `TickBacktestConfig` in [include/tick_based_engine.h](../include/tick_based_engine.h):

```cpp
int swap_3days = 3;  // Day of week for triple swap (0=Sun, 1=Mon, 2=Tue, 3=Wed, 4=Thu, 5=Fri, 6=Sat)
```

### Day of Week Calculation

Implemented `GetDayOfWeek()` function using **Zeller's congruence algorithm**:

```cpp
int GetDayOfWeek(const std::string& date_str) {
    // Parse YYYY.MM.DD format
    int year = std::stoi(date_str.substr(0, 4));
    int month = std::stoi(date_str.substr(5, 2));
    int day = std::stoi(date_str.substr(8, 2));

    // Zeller's congruence algorithm
    if (month < 3) {
        month += 12;
        year--;
    }
    int century = year / 100;
    year = year % 100;
    int day_of_week = (day + (13 * (month + 1)) / 5 + year + year / 4 + century / 4 - 2 * century) % 7;

    // Convert: Zeller's returns (0=Sat, 1=Sun, 2=Mon, ...) -> (0=Sun, 1=Mon, ...)
    day_of_week = (day_of_week + 6) % 7;
    return day_of_week;
}
```

### Swap Processing with Triple Multiplier

Modified `ProcessSwap()` in [include/tick_based_engine.h:468-534](../include/tick_based_engine.h#L468-L534):

```cpp
void ProcessSwap(const Tick& tick) {
    std::string current_date = tick.timestamp.substr(0, 10);

    if (current_date != last_swap_date_ && !last_swap_date_.empty()) {
        if (!open_positions_.empty()) {
            // Check if this is the triple swap day
            int day_of_week = GetDayOfWeek(current_date);
            int swap_multiplier = (day_of_week == config_.swap_3days) ? 3 : 1;

            double daily_swap = 0.0;

            for (Trade* trade : open_positions_) {
                // ... calculate position_swap based on swap mode ...

                // Apply swap multiplier for triple swap day
                position_swap *= swap_multiplier;

                daily_swap += position_swap;
                trade->commission += position_swap;
            }

            balance_ += daily_swap;
            total_swap_charged_ += daily_swap;
        }
    }

    last_swap_date_ = current_date;
}
```

## Broker Settings

### 
- **Swap Long**: -66.99 points/lot/day
- **Swap Short**: 41.2 points/lot/day
- **Swap Mode**: 1 (SYMBOL_SWAP_MODE_POINTS)
- **Triple Swap Day**: 3 (Wednesday)

### 
- **Swap Long**: -66.99 points/lot/day
- **Swap Short**: 41.2 points/lot/day
- **Swap Mode**: 1 (SYMBOL_SWAP_MODE_POINTS)
- **Triple Swap Day**: 3 (Wednesday)

Both brokers use identical swap settings.

## Fetching Triple Swap Day from MT5 API

Updated both fetch scripts to query `swap_rollover3days` from MT5 API:

**[fetch_xauusd_swap.py](../fetch_xauusd_swap.py):**
```python
swap_3days = symbol_info.swap_rollover3days
print(f"Triple Swap Day:          {swap_3days} (0=Sun, 3=Wed, etc)")
```

**[fetch_broker_settings.py](../fetch_broker_settings.py):**
```python
swap_3days = symbol_info.swap_rollover3days
day_names = {0: "Sunday", 1: "Monday", 2: "Tuesday", 3: "Wednesday", 4: "Thursday", 5: "Friday", 6: "Saturday"}
swap_3days_name = day_names.get(swap_3days, f"UNKNOWN ({swap_3days})")
print(f"Triple Swap Day:          {swap_3days} ({swap_3days_name})")
```

## Test Configuration

**[validation/test_fill_up.cpp](test_fill_up.cpp)** ():
```cpp
config.swap_long = -66.99;   // -66.99 points/lot/day
config.swap_short = 41.2;
config.swap_mode = 1;        // SYMBOL_SWAP_MODE_POINTS
config.swap_3days = 3;       // Triple swap on Wednesday
```

**[validation/test_fill_up_broker.cpp](test_fill_up_broker.cpp)** ():
```cpp
config.swap_long = -66.99;   // -66.99 points/lot/day
config.swap_short = 41.2;
config.swap_mode = 1;        // SYMBOL_SWAP_MODE_POINTS
config.swap_3days = 3;       // Triple swap on Wednesday
```

## Impact on Results

For XAUUSD with typical grid trading strategy:

- **Normal day**: -66.99 points/lot = -$66.99/lot
- **Wednesday (triple swap)**: -200.97 points/lot = -$200.97/lot
- **Annual impact**: ~52 Wednesdays × extra $133.98/lot = **-$6,966.96/lot/year** additional cost

With average 0.5 lots open:
- **Additional annual cost from triple swap**: ~$3,483.48

This is a significant factor that affects strategy profitability and **must be included** for accurate backtesting.

## Verification

To verify triple swap is working correctly:

1. **Enable debug output** in ProcessSwap():
```cpp
if (daily_swap != 0.0) {
    std::cout << current_date << " - Swap charged: $" << daily_swap
              << " (" << (swap_multiplier == 3 ? "TRIPLE SWAP" : "normal")
              << ", Total: $" << total_swap_charged_ << ")" << std::endl;
}
```

2. **Check Wednesdays in output** - should show 3x swap charges
3. **Compare total swap** with MT5 report

## MT5 Comparison

### Expected Results

**** (XAUUSD_TICKS_2025.csv):
- Initial Balance: $110,000
- MT5 Final Balance: $528,153.53
- C++ Target: Within 5-10% of MT5

**** (validation/Broker/XAUUSD_TICKS_2025.csv):
- Initial Balance: $110,000
- MT5 Final Balance: **$430,579.99**
- C++ Target: Within 5-10% of MT5

The significant difference between Grid ($528K) and Broker ($430K) suggests:
- Different tick data timing/pricing
- Possible spread differences
- Emphasizes importance of broker-specific testing

## Usage

```cpp
// Configure triple swap
config.swap_3days = 3;  // Wednesday (default)

// For brokers using different day:
config.swap_3days = 2;  // Tuesday
config.swap_3days = 4;  // Thursday
```

## References

- MT5 API: `symbol_info.swap_rollover3days`
- Zeller's Congruence: https://en.wikipedia.org/wiki/Zeller%27s_congruence
- Fill-up strategy validation: [FINAL_RESULTS_SUMMARY.md](FINAL_RESULTS_SUMMARY.md)
