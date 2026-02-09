# Code Review and Recommendations

## Introduction

This document provides a detailed analysis of the `ctrader-backtest` codebase. It highlights critical issues, problems related to testing and validation, and areas for improvement in code quality and maintainability. Each issue is accompanied by a proposed, actionable fix.

---

## 1. High-Priority Issues (Bugs & Major Flaws)

These issues critically impact the core functionality of the application and should be addressed immediately.

### 1.1. API Returns Mock Data, Misleading the UI

*   **Issue:** The `/api/backtest/run` endpoint in `server.py` is not connected to the C++ backtesting engine. It returns fake, hardcoded results from a `run_backtest_sync` function, while the real backtest (if it runs at all) is lost in a background thread.
*   **Impact:** The application is non-functional. The UI displays "successful" but meaningless results, making the tool unusable for real analysis.
*   **Proposed Fix:**
    1.  Remove the `run_backtest_sync` function from `server.py`.
    2.  Modify the `run_backtest` function to properly call the C++ backtest executable using Python's `subprocess` module.
    3.  The result from the C++ engine should be captured from `stdout` and returned as the JSON response.
    4.  Update the `execute_backtest` thread function to use the real C++ engine call instead of the mock function. Implement a mechanism (like WebSockets or polling the `/api/backtest/status/<id>` endpoint) for the UI to get the result of this asynchronous task.

### 1.2. Inaccurate Backtesting Logic in C++ Engine

*   **Issue:** The bar-by-bar simulation in `src/backtest_engine.cpp` incorrectly checks for Stop Loss (SL) and Take Profit (TP) triggers. It assumes the high and low of the bar can be checked without considering the intra-bar price movement, which is a classic backtesting flaw.
*   **Impact:** The backtest results are unrealistic and cannot be trusted. The engine might show a profitable trade when, in reality, the stop loss would have been hit first.
*   **Proposed Fix:**
    1.  Refactor the `RunBarByBar` method in `src/backtest_engine.cpp`.
    2.  At a minimum, implement a simple rule: if a bar's price range includes both the SL and TP levels, assume the stop loss was hit first (a conservative and common approach).
    3.  For a more accurate solution, use the `EVERY_TICK_OHLC` mode, which simulates ticks from the OHLC data, ensuring a realistic sequence of price movements within the bar. The UI should default to this mode for better accuracy.

### 1.3. Redundant and Out-of-Sync Strategy Definitions

*   **Issue:** The list of available trading strategies and their parameters are hardcoded in two separate places: the Python backend (`server.py`) and the JavaScript frontend (`ui/dashboard.js`).
*   **Impact:** This violates the Don't Repeat Yourself (DRY) principle, making the code harder to maintain and creating a high risk of bugs and inconsistencies.
*   **Proposed Fix:**
    1.  Remove the hardcoded `strategies` object from `ui/dashboard.js`.
    2.  On page load, the frontend should make an API call to the `/api/strategies` endpoint (which is already present in `server.py`) to fetch the list of strategies.
    3.  Dynamically generate the strategy selection UI and parameter inputs in `ui/dashboard.js` based on the data received from the API.

---

## 2. Testing and Validation Issues

The project's approach to testing is manual, inefficient, and lacks the rigor required for a financial application.

### 2.1. Manual, Brittle, and Error-Prone Testing Process

*   **Issue:** The entire validation workflow, which aims to replicate MetaTrader 5's behavior, is manual. It relies on a developer to run tests in MT5, copy files, run the native backtest, and then compare the results.
*   **Impact:** This process is slow, not easily repeatable, and highly susceptible to human error, making regression testing nearly impossible.
*   **Proposed Fix:**
    1.  Automate the process. The `run_all_tests.py` script should be modified to programmatically control the MT5 terminal (if possible via its command-line interface or other means) or at least automate the file operations and comparisons.
    2.  Create a single script that runs the MT5 backtest, runs the local C++ backtest for the same parameters, and then automatically calls the comparison script.

### 2.2. Lack of a Proper C++ Testing Framework

*   **Issue:** The C++ unit tests (e.g., `validation/test_currency_converter.cpp`) use a primitive, hand-rolled assertion system instead of a standard testing framework.
*   **Impact:** The tests are harder to write and maintain, and they provide poor feedback on failures.
*   **Proposed Fix:**
    1.  Integrate a standard C++ testing framework like **Google Test (GTest)** or **Catch2**.
    2.  Update the `CMakeLists.txt` file to discover and build the tests as a separate target.
    3.  Rewrite the existing unit tests using the chosen framework's syntax and assertions.

### 2.3. No Continuous Integration (CI)

*   **Issue:** The manual nature of the tests means they are not integrated into a CI pipeline.
*   **Impact:** There is no automated quality gate to prevent regressions or ensure that new code meets project standards.
*   **Proposed Fix:**
    1.  Create a CI pipeline using **GitHub Actions**.
    2.  The pipeline should, at a minimum:
        *   Build the C++ project.
        *   Run all C++ unit tests (after migrating to GTest).
        *   Run Python linters and any Python tests.
    3.  This ensures that every pull request is automatically checked for basic correctness.

---

## 3. Medium-Priority Issues (Code Quality & Maintainability)

### 3.1. Excessive Code in C++ Header File

*   **Issue:** `include/backtest_engine.h` contains a large amount of implementation code for classes like `TickGenerator` and `DataLoader`.
*   **Impact:** Slows down compilation.
*   **Proposed Fix:** Move the implementation of these classes into corresponding `.cpp` files in the `src` directory, leaving only the declarations in the header.

### 3.2. Lack of a Modern Frontend Framework

*   **Issue:** `ui/dashboard.js` uses vanilla JavaScript with manual DOM manipulation for a complex UI.
*   **Impact:** Difficult to maintain and scale.
*   **Proposed Fix:** Migrate the frontend to a modern, component-based framework like **React, Vue, or Svelte**. This would improve structure, state management, and long-term maintainability.

### 3.3. Poor CSS Management

*   **Issue:** All CSS is embedded directly in `ui/index.html`.
*   **Impact:** Hard to manage styling.
*   **Proposed Fix:** Move all CSS rules to an external `.css` file and link it in the HTML head. Remove all inline `style="..."` attributes in favor of CSS classes.

---

## 4. Low-Priority Issues (Minor Improvements & Cleanliness)

*   **Misleading "AI" Naming:** Rename `ai_agent.py` to `strategy_optimizer.py` to more accurately reflect its function.
*   **Hardcoded File Paths:** Centralize paths like `build\backtest.exe` in a configuration file or use environment variables.
*   **Cluttered Project Root:** Move the large number of `.md` files into a `docs/` subdirectory to clean up the project's root folder.
*   **Unusual CMake Configuration:** Refactor the `CMakeLists.txt` file to use standard, toolchain-specific linking for libraries like `pthread`.
