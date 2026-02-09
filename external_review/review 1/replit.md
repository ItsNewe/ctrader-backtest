# cTrader Backtest Engine

## Overview

A high-performance C++17 backtesting engine for algorithmic trading with support for cTrader Open API and MetaTrader 4/5 integration. The project includes a Python Flask web dashboard for configuration and visualization, along with comprehensive validation testing against MT5 Strategy Tester.

**Core Purpose:** Test trading strategies on historical data with production-grade accuracy using real broker specifications.

**Key Capabilities:**
- Three backtesting modes: BAR_BY_BAR (fastest), EVERY_TICK (most realistic), EVERY_TICK_OHLC (balanced)
- Multi-broker support (cTrader OpenAPI, MetaTrader 5)
- Real-time instrument specification fetching from brokers
- Parallel parameter optimization with ThreadPoolExecutor
- Web-based dashboard with candlestick charts and equity curves
- MT5-validated margin and swap calculations

## Recent Changes (January 2026)

### Engine Improvements
- **Robust Validation:** Added input validation for `contract_size` and `leverage` in `TickBasedEngine` to prevent mathematical errors and ensure configuration integrity.
- **Namespace Refactoring:** Resolved nested namespace conflicts in `backtest_engine.h` to ensure consistent compilation across different environments.
- **Performance Optimization:** Refactored `TickBacktestConfig` to use `std::string_view` for string parameters, reducing memory allocations during high-frequency backtests.

### User Interface & Examples
- **Streamlined Demonstration:** Simplified `main.cpp` to provide a clean, focused demonstration of the bar-based backtesting engine.
- **Error Handling:** Implemented comprehensive `try-catch` blocks in the main execution path to gracefully handle and report engine exceptions.

## User Preferences

Preferred communication style: Simple, everyday language.

## System Architecture

### Language Distribution
- **C++17**: Core backtesting engine (~2,900 lines)
- **Python**: Flask REST API, broker integration, automation scripts
- **JavaScript**: Dashboard frontend with Chart.js visualization
- **HTML/CSS**: Responsive web UI

### Core C++ Components

**Backtest Engine** (`include/backtest_engine.h`, `src/backtest_engine.cpp`)
- Position management with margin tracking
- SL/TP execution on every tick
- Unrealized P&L and equity calculation
- 17+ performance metrics (Sharpe ratio, profit factor, drawdown)

**Margin Manager** (`include/margin_manager.h`)
- MT5-validated margin calculation formulas
- 7 margin modes: FOREX, CFD, CFD_LEVERAGE, FUTURES, etc.
- Symbol-specific and account leverage support

**Swap Manager** (`include/swap_manager.h`)
- Daily swap application at 00:00 server time
- Configurable triple-swap day (Wednesday/Friday)
- 8 swap calculation modes

**Currency Conversion** (`include/currency_rate_manager.h`)
- Cross-currency margin and profit calculations
- Rate caching with configurable expiry
- Handles complex multi-currency scenarios

**Position Validator** (integrated in backtest_engine.h)
- Lot size validation (min/max/step)
- Margin requirement checking
- Stops level enforcement

### Python Backend

**Flask Server** (`server.py`)
- REST API endpoints for backtest execution
- Broker connection management
- Price history fetching
- Static file serving for dashboard

**Broker API** (`broker_api.py`)
- Abstract base class pattern for broker implementations
- CTraderAPI: OAuth authentication, instrument specs, price history
- MetaTrader5API: Local terminal connection, symbol info, OHLC data
- 24-hour intelligent caching with JSON persistence

**Parallel Sweep** (`backtest_sweep.py`)
- ThreadPoolExecutor for parallel parameter optimization
- SQLite storage for sweep results
- Grid search and random search parameter generators

### Frontend Dashboard

**Web UI** (`ui/index.html`, `ui/dashboard.js`)
- Strategy selection with 4 built-in options
- Broker connection panel with credential management
- Interactive candlestick charts using Chart.js
- Real-time backtest results with equity curves
- Responsive design for all screen sizes

### Build System

**CMake** (`CMakeLists.txt`)
- C++17 standard requirement
- Cross-platform support (Windows/Linux/macOS)
- Optional Protobuf integration for cTrader messages
- Compiler flags optimized per platform (MSVC vs GCC)

### Validation Framework

**MT5 Comparison Testing** (`validation/` directory)
- Micro-tests for specific behaviors (SL/TP order, swap timing, margin calc)
- Automated comparison scripts between C++ engine and MT5 Strategy Tester
- Target: <1% final balance difference from MT5

## External Dependencies

### Python Packages
- **Flask 3.1.2**: REST API server
- **Flask-CORS**: Cross-origin request handling
- **requests**: HTTP client for broker APIs
- **MetaTrader5** (optional): MT5 terminal integration via Python

### JavaScript Libraries
- **Chart.js 3.9.1**: Candlestick and equity curve visualization (CDN)

### C++ Libraries (Optional)
- **Protobuf**: cTrader Open API message serialization
- **OpenSSL**: Secure broker connections
- **Boost**: Socket handling (if implementing live connections)

### Broker APIs
- **cTrader Open API**: OAuth2 authentication, REST endpoints for specs and quotes
- **MetaTrader 5 Terminal**: Local Python API connection (requires MT5 running)

### Data Storage
- **SQLite**: Sweep results persistence
- **JSON files**: Cached broker specifications (`cache/` directory)
- **CSV files**: Historical OHLC data (`data/` directory)

### Development Tools
- **CMake 3.15+**: Build system generator
- **MinGW-w64 or MSVC**: C++ compiler on Windows
- **MSYS2**: Recommended development environment for Windows compilation