# Detailed List of Discovered Issues in cTrader Backtest Engine

Following a thorough review of the codebase, here is a detailed breakdown of the non-ideal patterns, bugs, and missing functionalities identified.

## 1. Memory and Resource Management
*   **Memory Leak in Engine**: In `TickBasedEngine::CreateTrade`, `Trade` objects are allocated using `new` but are never `delete`-ed. This will lead to significant memory exhaustion during long-running backtests or parameter sweeps.
*   **Raw Pointer Usage**: The use of raw pointers (`Trade*`) throughout the engine is error-prone. Modern C++ (C++11 and later) should use smart pointers (`std::unique_ptr` or `std::shared_ptr`) for automatic memory management.
*   **Incomplete Socket Cleanup**: In `CTraderConnection`, the socket closure logic is a stub. This can lead to leaked file descriptors and system instability.

## 2. Core Trading Logic Bugs
*   **Hardcoded Pip Values**: `TickBasedEngine::GetPipValue` is hardcoded to `0.00001`. This makes the engine unusable for JPY pairs (which use 2/3 digits) or other assets like Gold (XAUUSD) without manual code changes.
*   **Triple Swap Calculation**: The logic for identifying the "triple swap day" is simplistic and assumes the day after `swap_3days`. This varies by broker and should be more robust.
*   **Commission Calculation**: Commissions are calculated once at order entry but not updated for partial fills or specialized closing costs.

## 3. Concurrency and Performance
*   **Busy-Waiting in Network Loops**: `CTraderConnection::ReceiveLoop` uses a `sleep_for(1ms)` call. This is inefficient, causing high CPU usage and introducing artificial latency in receiving market data.
*   **Thread Safety**: Several state variables (like `connected_`, `authenticated_`) are accessed across threads without proper synchronization (mutexes or atomics), risking race conditions.
*   **Inefficient Data Management**: Large vectors of `Trade` and `Tick` objects are frequently copied or iterated through. Using more efficient data structures or passing by reference/pointer would improve performance.

## 4. Unimplemented Features (Stubs)
*   **Missing Network Stack**: Both cTrader and MetaTrader connectors are stubs. No actual network communication, protocol serialization (Protobuf), or authentication is implemented.
*   **No Max Drawdown Calculation**: The `max_drawdown` metric in `TickBasedEngine` is currently hardcoded to `0.0` with a `TODO` comment. This is a critical metric for strategy evaluation.
*   **Pending Orders**: Only Market Orders are supported. Limit and Stop orders are not yet implemented.
*   **Equity Approximation**: In bar-based mode, equity is only updated at the end of each bar, which can hide significant margin risks that occur within the bar.

## 5. Architectural Issues
*   **Lack of Unified Build System**: The project lacks a robust `CMakeLists.txt` or similar configuration for managing dependencies (like Protobuf or Boost).
*   **Public Data Members**: Structs are used extensively without encapsulation, making it easy to accidentally modify internal state without validation.
*   **Inconsistent Logging**: Logging is scattered between `std::cout`, `std::cerr`, and various `logger` stubs, making debugging difficult.

## Recommendations
*   **Refactor to Smart Pointers**: Migrate all `Trade` and `Position` management to `std::unique_ptr`.
*   **Implement Protobuf Serialization**: Complete the cTrader message serialization to enable actual API communication.
*   **Fix Pip/Digit Logic**: Introduce a `SymbolInfo` structure to handle different asset specifications.
*   **Implement Drawdown Metrics**: Add the logic to track equity peaks and calculate maximum drawdown percentages.
