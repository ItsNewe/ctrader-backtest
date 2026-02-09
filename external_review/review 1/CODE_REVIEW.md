# Code Review and Non-Ideal Patterns

## Overview
A review of the `ctrader-backtest` codebase has identified several non-ideal patterns, potential bugs, and missing implementations. The project is currently in a "partially implemented" state with many core features (socket connections, protocol serialization, drawdown calculation) being stubs.

## 1. Non-Ideal Code Patterns

### Memory Management
- **Manual `new` without `delete`**: In `TickBasedEngine::CreateTrade`, `Trade` objects are allocated using `new` (line 327) but there is no corresponding `delete` logic in `ClosePosition` or the engine's destructor. This leads to memory leaks.
- **Raw Pointer Management**: `open_positions_` stores raw `Trade*` pointers. Using `std::unique_ptr` or `std::shared_ptr` would be safer and more idiomatic in C++11/14/17.

### Concurrency
- **Busy Waiting**: `CTraderConnection::ReceiveLoop` uses `std::this_thread::sleep_for(std::chrono::milliseconds(1))` (line 194). This is inefficient and can lead to latency. A proper socket polling mechanism (like `select`, `poll`, or `epoll`) or an asynchronous library like Boost.Asio would be better.
- **Incomplete Locking**: While `send_mutex_` is used, the `connected_` and `authenticated_` flags are not consistently protected by mutexes or made `std::atomic`, which could lead to race conditions.

### Error Handling
- **Placeholders**: Many methods (e.g., `ConnectSocket`, `SendMessage`, `Authenticate`) return `true` by default even though their implementations are stubs. This can mislead calling code into thinking operations succeeded.
- **Silent Failures**: The `HandleMessage` switch (line 296) prints "Unhandled message type" but doesn't provide a way to bubble up errors to the strategy or user interface beyond an optional `error_callback_`.

### API Design
- **Public Data Members**: Structs like `Trade` and `CTraderConfig` have public data members. While common for PODs, it lacks validation logic when members are changed.
- **Magic Numbers**: The `MessageType` enum uses hardcoded values (e.g., 2100, 50). These should be documented or derived from a central protocol definition.

## 2. Potential Bugs

### Logic Errors
- **`TickBasedEngine::GetPipValue`**: Hardcoded to `0.00001` (line 322). This will be incorrect for JPY pairs (0.01) or other instruments with different digit counts.
- **Triple Swap Timing**: The logic for triple swap (lines 527-531) assumes triple swap is charged the day *after* `swap_3days`. While this matches some brokers, it's not universal and should be configurable.
- **OHLC Tick Generation**: `BacktestEngine::RunEveryTickOHLC` generates 50 ticks per bar (line 124). This is a very rough approximation and may not accurately reflect price action within a bar.

## 3. Missing Implementations (TODOs)

- **Socket Communication**: `ctrader_connector.h` and `metatrader_connector.cpp` are entirely stubs for network communication.
- **Protobuf Serialization**: No actual serialization for cTrader Open API messages.
- **Max Drawdown**: `TickBasedEngine::GetResults` has a `TODO` for max drawdown calculation (line 277).
- **Pending Orders**: `TickBasedEngine::ProcessPendingOrders` is a stub (line 417). Only market orders are currently processed.

## 4. Environment & Build

- **Missing Workflows**: No Replit workflows are configured to run the backtester or its UI.
- **Project Structure**: There is a mix of C++, Python, and JavaScript without a clear, unified build or execution path.

## Recommendations
1. Replace raw pointers with smart pointers in the backtest engines.
2. Implement actual socket handling using a library like Boost.Asio.
3. Fix the hardcoded pip values and add symbol-specific configuration.
4. Complete the drawdown calculation logic for accurate performance metrics.
5. Set up a proper build system (CMake) and Replit workflows.
