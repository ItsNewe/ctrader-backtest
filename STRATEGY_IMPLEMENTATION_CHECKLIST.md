# Strategy Implementation Checklist

## Quick Reference for New Strategies

### Files to Create
1. `include/strategy_[name].h` - Strategy header
2. `validation/test_[name]_minimal.cpp` - Basic single test
3. `validation/test_[name]_sweep.cpp` - Parallel parameter sweep
4. Update `validation/CMakeLists.txt` - Add build targets

---

## Strategy Header Structure

### Required Components
```cpp
#ifndef STRATEGY_NAME_H
#define STRATEGY_NAME_H

#include "tick_based_engine.h"
#include <vector>
#include <deque>
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace backtest {

class StrategyName {
public:
    // 1. Config struct with all parameters
    struct Config {
        double param1 = 0.1;
        double param2 = 100.0;
        // ... all configurable parameters

        // Factory presets
        static Config Baseline();
        static Config Aggressive();
        static Config Conservative();
        static Config XAUUSD();
        static Config XAGUSD();

        void Validate() const;
    };

    // 2. Stats struct for metrics
    struct Stats {
        int total_entries = 0;
        int stop_outs = 0;
        double max_equity = 0.0;
        double peak_volume = 0.0;
        // ... tracking metrics
    };

    // 3. Constructor (validates config)
    explicit StrategyName(const Config& config);

    // 4. Required method - called by engine
    void OnTick(const Tick& tick, TickBasedEngine& engine);

    // 5. Accessors
    const Stats& GetStats() const { return stats_; }
    const Config& GetConfig() const { return config_; }

private:
    Config config_;
    Stats stats_;

    // Market state (updated per tick)
    double current_ask_ = 0.0;
    double current_bid_ = 0.0;
    double current_spread_ = 0.0;

    // Position tracking
    double volume_of_open_trades_ = 0.0;
    // ... other state

    // Helper methods
    double CalculateLotSize(double price, TickBasedEngine& engine);
    void ProcessExits(TickBasedEngine& engine);
    void ProcessEntries(TickBasedEngine& engine);
};

} // namespace backtest
#endif
```

---

## Test File Structure (Parallel Sweep)

### Template Pattern
```cpp
#include "../include/strategy_name.h"
#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"  // For efficient loading
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>

using namespace backtest;

// GLOBAL shared tick data (read-only)
std::vector<Tick> g_ticks;

// Test configuration and result structs
struct TestConfig { /* params */ };
struct TestResult { /* results */ };

// Thread-safe work queue
std::mutex g_queue_mutex;
std::mutex g_results_mutex;
std::queue<TestConfig> g_work_queue;
std::vector<TestResult> g_results;
std::atomic<int> g_completed(0);
int g_total_tasks = 0;

// Load ticks ONCE using TickDataManager
void LoadTickData() {
    TickDataConfig cfg;
    cfg.file_path = "path/to/ticks.csv";
    cfg.format = TickDataFormat::MT5_CSV;

    TickDataManager mgr(cfg);
    Tick tick;
    while (mgr.GetNextTick(tick)) {
        g_ticks.push_back(tick);
    }
}

// Run single test (called from worker thread)
TestResult RunTest(const TestConfig& cfg) {
    TickBasedEngine engine(base_config);
    StrategyName strategy(cfg.strategy_config);

    // Use RunWithTicks - NOT Run()
    engine.RunWithTicks(g_ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    // Return results
    return { engine.GetResults(), strategy.GetStats() };
}

// Worker thread function
void Worker() {
    while (true) {
        TestConfig task;
        {
            std::lock_guard<std::mutex> lock(g_queue_mutex);
            if (g_work_queue.empty()) return;
            task = g_work_queue.front();
            g_work_queue.pop();
        }

        TestResult result = RunTest(task);

        {
            std::lock_guard<std::mutex> lock(g_results_mutex);
            g_results.push_back(result);
        }

        g_completed++;
    }
}

int main() {
    // 1. Load ticks ONCE
    LoadTickData();

    // 2. Build work queue
    for (auto& params : param_combinations) {
        g_work_queue.push({params});
    }
    g_total_tasks = g_work_queue.size();

    // 3. Spawn threads
    int num_threads = std::thread::hardware_concurrency();
    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back(Worker);
    }

    // 4. Wait and report
    for (auto& t : threads) t.join();

    // 5. Sort and display results
    std::sort(g_results.begin(), g_results.end(), ...);
    // Print top/bottom configs
}
```

---

## CMakeLists.txt Entry

```cmake
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_strategy_name.cpp")
add_executable(test_strategy_name test_strategy_name.cpp)
target_include_directories(test_strategy_name PRIVATE ../include)
target_compile_features(test_strategy_name PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_strategy_name PRIVATE /W4 /O2)
else()
    target_compile_options(test_strategy_name PRIVATE -Wall -Wextra -O3 -mavx2 -mfma)
    target_link_libraries(test_strategy_name PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_strategy_name PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Test configured: test_strategy_name")
endif()
```

---

## Broker Settings Reference

### XAUUSD (Gold)
```cpp
config.contract_size = 100.0;
config.leverage = 500.0;
config.pip_size = 0.01;
config.swap_long = -66.99;
config.swap_short = 41.2;
```

### XAGUSD (Silver)
```cpp
config.contract_size = 5000.0;
config.leverage = 500.0;
config.pip_size = 0.001;
config.swap_long = -15.0;
config.swap_short = 13.72;
```

### NAS100 (NASDAQ)
```cpp
config.contract_size = 1.0;
config.leverage = 100.0;  // or 200, 500
config.pip_size = 0.01;
config.swap_long = -5.96;
config.swap_short = 1.6;
```

---

## Critical Rules

1. **Load ticks ONCE** using `TickDataManager` into global vector
2. **Use `RunWithTicks()`** not `Run()` for preloaded data
3. **Fresh engine per test** - each test starts from clean state
4. **Thread-safe access** - use mutexes for queue/results
5. **Binary search for lot sizing** - O(log N) not O(N)
6. **Validate config** in constructor

---

## Common Pitfalls

| Issue | Cause | Fix |
|-------|-------|-----|
| Slow tests | Reloading ticks each config | Use `RunWithTicks()` with global ticks |
| Single-threaded | Not using thread pool | Add `std::thread` workers |
| Race conditions | Shared mutable state | Read-only g_ticks, mutex on results |
| NaN values | Division by zero | Check denominators, use `std::max()` |
| Wrong returns | Mismatched broker settings | Use symbol-specific presets |

---

*Updated: 2025-02*
