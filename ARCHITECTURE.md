# Architecture & Implementation Guide

This document catalogs all implemented features, techniques, libraries, and patterns used in the cTrader-Backtest framework. It serves as a reference for maintaining consistency when adding new features.

---

## Table of Contents

1. [Project Overview](#project-overview)
2. [Build System](#build-system)
3. [Core Architecture](#core-architecture)
4. [Coding Patterns & Conventions](#coding-patterns--conventions)
5. [Performance Optimization Techniques](#performance-optimization-techniques)
6. [Data Structures](#data-structures)
7. [Strategy Implementation Patterns](#strategy-implementation-patterns)
8. [Testing Patterns](#testing-patterns)
9. [File Organization](#file-organization)

---

## Project Overview

| Attribute | Value |
|-----------|-------|
| Language | C++17 |
| Compilers | MSVC, GCC, MinGW, Clang |
| Build System | CMake 3.15+ |
| Platforms | Windows, Linux, macOS |
| Threading | `std::thread`, `std::future`, `std::async` |
| SIMD | SSE2, SSE4.1, AVX2, AVX-512, FMA |

**Primary Purpose**: High-performance tick-by-tick backtesting engine matching MetaTrader 5's "Every tick based on real ticks" mode.

---

## Build System

### CMakeLists.txt Conventions

```cmake
# Standard version
cmake_minimum_required(VERSION 3.15)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# SIMD flags
if(MSVC)
    add_compile_options(/arch:AVX2)
else()
    add_compile_options(-mavx2 -mfma)
endif()

# Optimization flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(MSVC)
        target_compile_options(target PRIVATE /O2 /Ob2)
    else()
        target_compile_options(target PRIVATE -O3 -march=native)
    endif()
endif()

# Optional libraries (graceful degradation)
find_package(Protobuf QUIET)
if(Protobuf_FOUND)
    target_link_libraries(target PRIVATE protobuf::libprotobuf)
    target_compile_definitions(target PRIVATE HAVE_PROTOBUF)
endif()
```

### Build Commands

```bash
# Windows (MinGW)
cd build && cmake .. -G "MinGW Makefiles" && mingw32-make target_name

# Linux/macOS
mkdir build && cd build && cmake .. && make -j$(nproc)
```

---

## Core Architecture

### Namespace Convention

All backtesting code lives in the `backtest` namespace:

```cpp
namespace backtest {
    // All types, classes, functions
} // namespace backtest
```

### Header-Only Design

The codebase uses header-only design for most components:

- **Reason**: Simplifies build, enables inlining, reduces link-time complexity
- **Exception**: Implementation files exist for `src/backtest_engine.cpp`, `src/ctrader_connector.cpp`, `src/metatrader_connector.cpp`
- **Convention**: All strategy, utility, and engine code is header-only

### Include Guard Convention

```cpp
#ifndef FILENAME_H
#define FILENAME_H

// ... content ...

#endif // FILENAME_H
```

### Documentation Style (Doxygen)

```cpp
/**
 * @file filename.h
 * @brief One-line description.
 *
 * Detailed description of the file/class purpose.
 *
 * @section usage Usage Example
 * @code
 * // Code example
 * @endcode
 *
 * @author ctrader-backtest project
 * @version 1.0
 * @date 2025
 */
```

---

## Coding Patterns & Conventions

### 1. Configuration Structs Pattern

All configurable components use a nested `Config` struct with:
- Default values
- Static preset factory methods
- Validation method

```cpp
class MyComponent {
public:
    struct Config {
        // Parameters with sensible defaults
        double param1 = 10.0;
        double param2 = 0.5;

        // Preset factory methods
        static Config Default() {
            Config c;
            c.param1 = 10.0;
            return c;
        }

        static Config Aggressive() {
            Config c = Default();
            c.param1 = 5.0;
            return c;
        }

        // Validation
        void Validate() const {
            if (param1 <= 0) throw std::invalid_argument("param1 must be > 0");
        }
    };

    explicit MyComponent(const Config& config) : config_(config) {
        config_.Validate();
    }

private:
    Config config_;
};
```

### 2. Enum Classes with String Conversion

Use `enum class` for type safety with helper functions for string conversion:

```cpp
enum class TradeDirection : uint8_t {
    BUY = 0,
    SELL = 1
};

inline const char* TradeDirectionStr(TradeDirection dir) {
    return dir == TradeDirection::BUY ? "BUY" : "SELL";
}
```

### 3. Callback Pattern for Strategies

Strategies are executed via callback/lambda pattern:

```cpp
using StrategyCallback = std::function<void(const Tick& tick, TickBasedEngine& engine)>;

void Run(StrategyCallback strategy) {
    while (tick_manager_.GetNextTick(tick)) {
        strategy(tick, *this);
    }
}

// Usage
engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
    strategy.OnTick(tick, eng);
});
```

### 4. Presets for Common Configurations

Broker-specific presets are provided as inline functions:

```cpp
inline TickBacktestConfig XAUUSD_Grid_Preset() {
    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.contract_size = 100.0;
    config.leverage = 500.0;
    config.pip_size = 0.01;
    config.swap_long = -66.99;
    config.swap_short = 41.2;
    return config;
}
```

### 5. Object Pool Pattern

For frequently allocated/deallocated objects (trades), use object pools:

```cpp
class TradePool {
public:
    explicit TradePool(size_t initial_capacity = 256) {
        pool_.reserve(initial_capacity);
        free_list_.reserve(initial_capacity);
    }

    Trade* Allocate() {
        if (!free_list_.empty()) {
            Trade* t = free_list_.back();
            free_list_.pop_back();
            return t;
        }
        Trade* t = new Trade();
        pool_.push_back(t);
        return t;
    }

    void Release(Trade* t) {
        // Reset state and return to pool
        *t = Trade();  // Reset
        free_list_.push_back(t);
    }

private:
    std::vector<Trade*> pool_;
    std::vector<Trade*> free_list_;
};
```

### 6. RAII Pattern for Resources

Memory-mapped files and other resources use RAII:

```cpp
class MemoryMappedFile {
public:
    MemoryMappedFile() : data_(nullptr), size_(0) {}
    ~MemoryMappedFile() { Close(); }

    // Non-copyable
    MemoryMappedFile(const MemoryMappedFile&) = delete;
    MemoryMappedFile& operator=(const MemoryMappedFile&) = delete;

    // Movable
    MemoryMappedFile(MemoryMappedFile&& other) noexcept;
    MemoryMappedFile& operator=(MemoryMappedFile&& other) noexcept;

    bool Open(const std::string& path);
    void Close();
};
```

---

## Performance Optimization Techniques

### 1. SIMD Vectorization

**Pattern**: Runtime CPU feature detection with tiered fallbacks.

```cpp
namespace simd {

struct CPUFeatures {
    bool sse2 = false;
    bool avx2 = false;
    bool avx512f = false;
    bool fma = false;
};

inline CPUFeatures g_cpu_features;

inline void init() {
    // Use CPUID to detect features
    detect_cpu_features();
}

inline bool has_avx2() { return g_cpu_features.avx2; }

// Auto-select best implementation
inline double sum(const double* data, size_t n) {
#ifdef __AVX512F__
    if (g_cpu_features.avx512f) return sum_avx512(data, n);
#endif
    if (g_cpu_features.avx2) return sum_avx2(data, n);
    return sum_sse2(data, n);
}

} // namespace simd
```

**SIMD Threshold**: Use SIMD for 8+ elements, scalar for smaller:

```cpp
const size_t SIMD_THRESHOLD = 8;
if (positions.size() >= SIMD_THRESHOLD && simd::has_avx2()) {
    // SIMD path
} else {
    // Scalar fallback
}
```

### 2. Cache-Friendly Data Layout

Separate BUY/SELL positions for vectorized operations:

```cpp
mutable std::vector<double> buy_entry_prices_;
mutable std::vector<double> buy_lot_sizes_;
mutable std::vector<double> sell_entry_prices_;
mutable std::vector<double> sell_lot_sizes_;
```

### 3. Dirty Cache Pattern

Invalidate SIMD cache only when positions change:

```cpp
mutable bool simd_cache_dirty_ = true;

void RefreshSimdCache() const {
    if (!simd_cache_dirty_) return;
    // Rebuild cache...
    simd_cache_dirty_ = false;
}

void InvalidateSimdCache() {
    simd_cache_dirty_ = true;
}
```

### 4. Memory-Mapped File I/O

Use OS-native memory mapping for large files:

```cpp
#ifdef _WIN32
    HANDLE file_handle_ = CreateFileA(...);
    HANDLE mapping_handle_ = CreateFileMappingA(...);
    data_ = MapViewOfFile(...);
#else
    int fd_ = open(path.c_str(), O_RDONLY);
    data_ = mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0);
    madvise(const_cast<char*>(data_), size_, MADV_SEQUENTIAL);
#endif
```

### 5. Zero-Allocation Parsing

Custom number parsers that avoid `std::stod` overhead:

```cpp
inline double fast_parse_double(const char* str, const char** end) {
    // Manual digit extraction without allocations
    double result = 0.0;
    while (*p >= '0' && *p <= '9') {
        result = result * 10.0 + (*p - '0');
        ++p;
    }
    // Handle decimal...
    return result;
}
```

### 6. Timestamp Parsing Optimization

Avoid `substr()` allocations in hot paths:

```cpp
// BAD: Allocates on every tick
std::string date = tick.timestamp.substr(0, 10);

// GOOD: Compare in-place
if (tick.timestamp.compare(0, 10, last_date_, 0, 10) != 0) {
    // Date changed
}

// GOOD: Manual digit extraction
int year = (ts[0] - '0') * 1000 + (ts[1] - '0') * 100 +
           (ts[2] - '0') * 10 + (ts[3] - '0');
```

### 7. Pre-allocated Buffers

Reuse buffers instead of allocating per-tick:

```cpp
// Class members (allocated once)
mutable std::vector<double> pnl_buffer_buy_;
mutable std::vector<double> pnl_buffer_sell_;

// In hot path
if (pnl_buffer_buy_.size() < buy_entry_prices_.size()) {
    pnl_buffer_buy_.resize(buy_entry_prices_.size());  // Only grows
}
```

### 8. Swap-and-Pop Removal

O(1) removal from unordered collections:

```cpp
// O(N) removal
positions.erase(std::remove(positions.begin(), positions.end(), trade), positions.end());

// O(1) swap-and-pop
for (size_t i = 0; i < positions.size(); ++i) {
    if (positions[i] == trade) {
        std::swap(positions[i], positions.back());
        positions.pop_back();
        break;
    }
}
```

### 9. Binary Search for Lot Sizing

Replace O(N) linear search with O(log N) binary search:

```cpp
double low = 1.0, high = max_mult;
double best_mult = 1.0;

while (high - low > 0.05) {
    double mid = (low + high) / 2.0;
    if (IsValid(mid)) {
        best_mult = mid;
        low = mid;
    } else {
        high = mid;
    }
}
```

### 10. Prefetching

Hint to CPU about upcoming memory access:

```cpp
_mm_prefetch(reinterpret_cast<const char*>(data + i + 16), _MM_HINT_T0);
```

---

## Data Structures

### Core Types

| Type | Location | Purpose |
|------|----------|---------|
| `Tick` | `tick_data.h` | Single market tick (timestamp, bid, ask, volume) |
| `Trade` | `trade_types.h` | Open/closed position with all metadata |
| `PendingOrder` | `trade_types.h` | Limit/stop/stop-limit orders |
| `TickBacktestConfig` | `tick_based_engine.h` | Engine configuration (50+ parameters) |
| `BacktestResults` | `tick_based_engine.h` | Final results (balance, trades, metrics) |

### Enums

| Enum | Values | Purpose |
|------|--------|---------|
| `TradeDirection` | BUY, SELL | Position direction |
| `PendingOrderType` | BUY_LIMIT, BUY_STOP, SELL_LIMIT, SELL_STOP, BUY_STOP_LIMIT, SELL_STOP_LIMIT | Order types |
| `AccountMode` | HEDGING, NETTING | MT5 account modes |
| `CommissionMode` | PER_LOT, PERCENT_OF_VOLUME, PER_DEAL | Commission calculation |
| `SwapMode` | DISABLED, POINTS, CURRENCY_SYMBOL, INTEREST, MARGIN_CURRENCY | Swap calculation |
| `SlippageModel` | FIXED, VOLUME_BASED, VOLATILITY_BASED | Slippage simulation |
| `FillingType` | FOK, IOC, RETURN | Order filling modes |

---

## Strategy Implementation Patterns

### Strategy Interface

Strategies implement an `OnTick` method:

```cpp
class MyStrategy {
public:
    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        // 1. Update internal state
        // 2. Check entry/exit conditions
        // 3. Execute trades via engine
    }
};
```

### Strategy Configuration

Use nested `Config` struct with presets:

```cpp
class FillUpOscillation {
public:
    enum Mode { BASELINE, ADAPTIVE_SPACING, ANTIFRAGILE, ... };

    struct Config {
        double survive_pct = 13.0;
        double base_spacing = 1.5;
        Mode mode = ADAPTIVE_SPACING;

        static Config XAUUSD_Default();
        static Config XAGUSD_Default();
        static Config Aggressive();
        static Config Conservative();
    };

    explicit FillUpOscillation(const Config& config);
};
```

### Trading Operations

```cpp
// Open market order
Trade* trade = engine.OpenMarketOrder(
    TradeDirection::BUY,
    lot_size,
    stop_loss,    // 0 = no SL
    take_profit   // 0 = no TP
);

// Close position
engine.ClosePosition(trade, "TP");

// Place pending order
int order_id = engine.PlacePendingOrder(
    PendingOrderType::BUY_LIMIT,
    trigger_price,
    lot_size,
    stop_loss,
    take_profit,
    expiry_time   // "" = GTC
);

// Cancel pending order
engine.CancelPendingOrder(order_id);

// Set trailing stop
engine.SetTrailingStop(trade, distance, activation_profit);
```

### Statistics Tracking

Track strategy-specific statistics:

```cpp
struct Stats {
    long forced_entries = 0;
    long max_position_blocks = 0;
    int peak_positions = 0;
};

const Stats& GetStats() const { return stats_; }
```

---

## Testing Patterns

### Test File Naming

```
tests/
├── test_strategy_minimal.cpp      # Basic strategy + engine integration
├── test_engine_minimal.cpp        # Engine core functionality
├── test_accuracy.cpp              # Precision validation
├── test_edge_cases.cpp            # Boundary conditions
├── test_simd_unit.cpp             # SIMD correctness
├── test_simd_benchmark.cpp        # SIMD performance
├── test_parser_benchmark.cpp      # Tick parser speed
└── test_strategies_comprehensive.cpp  # All strategies
```

### Test Structure

```cpp
#include "../include/component.h"
#include <iostream>
#include <iomanip>

using namespace backtest;

int main() {
    std::cout << "=== Test Name ===" << std::endl;
    std::cout << std::fixed << std::setprecision(2);

    // Step-by-step with progress output
    std::cout << "Step 1: Setup..." << std::flush;
    // ... setup code ...
    std::cout << " Done!" << std::endl;

    std::cout << "Step 2: Execute..." << std::endl;
    // ... test code ...

    std::cout << "\n=== Results ===" << std::endl;
    // ... print results ...

    // Pass/fail indication
    if (success) {
        std::cout << "\n*** TEST PASSED ***" << std::endl;
        return 0;
    } else {
        std::cout << "\n*** TEST FAILED ***" << std::endl;
        return 1;
    }
}
```

### Parallel Testing Pattern

Load ticks once, run multiple configs:

```cpp
// Load ticks ONCE
std::vector<Tick> ticks = LoadTicks(file_path, start_date, end_date);

// Test each config with fresh engine
for (const auto& params : configs) {
    TickBasedEngine engine(config);
    MyStrategy strategy(params);

    engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    results.push_back(engine.GetResults());
}
```

---

## File Organization

```
ctrader-backtest/
├── include/                    # Header-only components
│   ├── tick_based_engine.h     # Core engine (1500+ lines)
│   ├── tick_data.h             # Tick structure and formats
│   ├── tick_data_manager.h     # Tick data loading
│   ├── trade_types.h           # Trade, PendingOrder, enums
│   ├── fast_tick_parser.h      # Memory-mapped parser
│   ├── simd_intrinsics.h       # SIMD operations
│   ├── fill_up_oscillation.h   # Primary strategy
│   ├── strategy_combined_ju.h  # Hybrid strategy
│   ├── strategy_*.h            # Other strategies
│   ├── margin_manager.h        # MT5-validated margin
│   ├── swap_manager.h          # MT5-validated swap
│   ├── optimization_engine.h   # Grid search, GA, DE
│   ├── walk_forward.h          # Walk-forward analysis
│   ├── monte_carlo.h           # Monte Carlo simulation
│   └── ...
├── src/                        # Implementation files (minimal)
├── tests/                      # Test executables
├── mt5/                        # MetaTrader 5 Expert Advisors
│   ├── *.mq5                   # Strategy implementations
│   └── Presets/*.set           # Optimized parameter presets
├── validation/                 # Test data
│   └── Grid/                 # Broker-specific tick data
├── CMakeLists.txt              # Build configuration
├── CLAUDE.md                   # Quick reference (build, params)
└── ARCHITECTURE.md             # This document
```

---

## Key Principles Summary

1. **Header-only where practical** - Simplifies build, enables inlining
2. **Config struct pattern** - All parameters in one place with presets
3. **SIMD with scalar fallback** - Runtime detection, graceful degradation
4. **Object pools for hot paths** - Reduce allocation overhead
5. **Zero-allocation parsing** - Manual parsing beats `std::stod`
6. **Cache-aware data layout** - Separate BUY/SELL for vectorization
7. **Dirty flag pattern** - Rebuild caches only when necessary
8. **Memory-mapped I/O** - 5-10x faster than `std::ifstream`
9. **Pre-allocated buffers** - Reuse instead of reallocate
10. **Binary search optimization** - O(log N) beats O(N)

---

## Adding New Features

When adding new features, follow these guidelines:

1. **Create header in `include/`** with proper guards and documentation
2. **Use `backtest` namespace** for all new types
3. **Implement Config struct** with defaults and presets if configurable
4. **Add SIMD path** if processing arrays of 8+ elements
5. **Write test in `tests/`** following the test structure pattern
6. **Update CMakeLists.txt** if adding new test executables
7. **Document in CLAUDE.md** if it affects usage patterns

---

*Last updated: 2025*
