# Validation Test Suite CMake Configuration

cmake_minimum_required(VERSION 3.10)

# Test A: SL/TP Execution Order
add_executable(test_a_backtest
    test_a_our_backtest.cpp
    ../src/backtest_engine.cpp
)

target_include_directories(test_a_backtest PRIVATE
    ../include
)

target_compile_features(test_a_backtest PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_a_backtest PRIVATE /W4 /O2)
else()
    target_compile_options(test_a_backtest PRIVATE -Wall -Wextra -O3)
endif()

# Set output directory
set_target_properties(test_a_backtest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Validation test configured: test_a_backtest")

# Unit Test: MarginManager and SwapManager
add_executable(test_margin_swap
    test_margin_swap.cpp
)

target_include_directories(test_margin_swap PRIVATE
    ../include
)

target_compile_features(test_margin_swap PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_margin_swap PRIVATE /W4 /O2)
else()
    target_compile_options(test_margin_swap PRIVATE -Wall -Wextra -O3)
endif()

set_target_properties(test_margin_swap PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Validation test configured: test_margin_swap")

# Unit Test: PositionValidator
add_executable(test_position_validator
    test_position_validator.cpp
)

target_include_directories(test_position_validator PRIVATE
    ../include
)

target_compile_features(test_position_validator PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_position_validator PRIVATE /W4 /O2)
else()
    target_compile_options(test_position_validator PRIVATE -Wall -Wextra -O3)
endif()

set_target_properties(test_position_validator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Validation test configured: test_position_validator")

# Unit Test: CurrencyConverter
add_executable(test_currency_converter
    test_currency_converter.cpp
)

target_include_directories(test_currency_converter PRIVATE
    ../include
)

target_compile_features(test_currency_converter PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_currency_converter PRIVATE /W4 /O2)
else()
    target_compile_options(test_currency_converter PRIVATE -Wall -Wextra -O3)
endif()

set_target_properties(test_currency_converter PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BUILD_DIR}/validation"
)

message(STATUS "Validation test configured: test_currency_converter")

# Unit Test: CurrencyRateManager
add_executable(test_currency_rate_manager
    test_currency_rate_manager.cpp
)

target_include_directories(test_currency_rate_manager PRIVATE
    ../include
)

target_compile_features(test_currency_rate_manager PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_currency_rate_manager PRIVATE /W4 /O2)
else()
    target_compile_options(test_currency_rate_manager PRIVATE -Wall -Wextra -O3)
endif()

set_target_properties(test_currency_rate_manager PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BUILD_DIR}/validation"
)

message(STATUS "Validation test configured: test_currency_rate_manager")

# Enable testing
enable_testing()
add_test(NAME MarginSwapUnitTests COMMAND test_margin_swap)
add_test(NAME PositionValidatorTests COMMAND test_position_validator)
add_test(NAME CurrencyConverterTests COMMAND test_currency_converter)
add_test(NAME CurrencyRateManagerTests COMMAND test_currency_rate_manager)

# Example Integration: Minimal working example
add_executable(example_integration
    example_integration.cpp
)

target_include_directories(example_integration PRIVATE
    ../include
)

target_compile_features(example_integration PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(example_integration PRIVATE /W4 /O2)
else()
    target_compile_options(example_integration PRIVATE -Wall -Wextra -O3)
endif()

set_target_properties(example_integration PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Example integration configured: example_integration")

# Fill-Up Strategy Test
add_executable(test_fill_up
    test_fill_up.cpp
)

target_include_directories(test_fill_up PRIVATE
    ../include
)

target_compile_features(test_fill_up PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_fill_up PRIVATE /W4 /O2)
else()
    target_compile_options(test_fill_up PRIVATE -Wall -Wextra -O3)
endif()

set_target_properties(test_fill_up PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Fill-Up test configured: test_fill_up")

# V5 Crash Focus Analysis
add_executable(test_v5_crash_focus
    test_v5_crash_focus.cpp
)

target_include_directories(test_v5_crash_focus PRIVATE
    ../include
)

target_compile_features(test_v5_crash_focus PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_v5_crash_focus PRIVATE /W4 /O2)
else()
    target_compile_options(test_v5_crash_focus PRIVATE -Wall -Wextra -O3)
endif()

set_target_properties(test_v5_crash_focus PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "V5 crash focus test configured: test_v5_crash_focus")

# V5 Exit Strategy Optimization
add_executable(test_v5_exits
    test_v5_exits.cpp
)

target_include_directories(test_v5_exits PRIVATE
    ../include
)

target_compile_features(test_v5_exits PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_v5_exits PRIVATE /W4 /O2)
else()
    target_compile_options(test_v5_exits PRIVATE -Wall -Wextra -O3)
endif()

set_target_properties(test_v5_exits PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "V5 exit optimization test configured: test_v5_exits")

# V6 Combined Exit Improvements Test
add_executable(test_v6_combined
    test_v6_combined.cpp
)

target_include_directories(test_v6_combined PRIVATE
    ../include
)

target_compile_features(test_v6_combined PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_v6_combined PRIVATE /W4 /O2)
else()
    target_compile_options(test_v6_combined PRIVATE -Wall -Wextra -O3)
endif()

set_target_properties(test_v6_combined PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "V6 combined exit improvements test configured: test_v6_combined")

# V6 Combined Exit Improvements Test - Quick Version
add_executable(test_v6_quick
    test_v6_quick.cpp
)

target_include_directories(test_v6_quick PRIVATE
    ../include
)

target_compile_features(test_v6_quick PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_v6_quick PRIVATE /W4 /O2)
else()
    target_compile_options(test_v6_quick PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_v6_quick PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_v6_quick PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "V6 quick test configured: test_v6_quick")

# Market-Neutral Strategy Exploration Test
add_executable(test_market_neutral
    test_market_neutral.cpp
)

target_include_directories(test_market_neutral PRIVATE
    ../include
)

target_compile_features(test_market_neutral PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_market_neutral PRIVATE /W4 /O2)
else()
    target_compile_options(test_market_neutral PRIVATE -Wall -Wextra -O3)
endif()

set_target_properties(test_market_neutral PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Market-neutral strategy test configured: test_market_neutral")

# Market-Neutral Strategy Exploration Test - Simple Version
add_executable(test_market_neutral_simple
    test_market_neutral_simple.cpp
)

target_include_directories(test_market_neutral_simple PRIVATE
    ../include
)

target_compile_features(test_market_neutral_simple PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_market_neutral_simple PRIVATE /W4 /O2)
else()
    target_compile_options(test_market_neutral_simple PRIVATE -Wall -Wextra -O3)
endif()

set_target_properties(test_market_neutral_simple PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Market-neutral simple test configured: test_market_neutral_simple")

# V7 Parameter Sweep Test
add_executable(test_v7_param_sweep
    test_v7_param_sweep.cpp
)

target_include_directories(test_v7_param_sweep PRIVATE
    ../include
)

target_compile_features(test_v7_param_sweep PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_v7_param_sweep PRIVATE /W4 /O2)
else()
    target_compile_options(test_v7_param_sweep PRIVATE -Wall -Wextra -O3)
endif()

set_target_properties(test_v7_param_sweep PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "V7 parameter sweep test configured: test_v7_param_sweep")

# Bollinger Band Width Filter Test
add_executable(test_bb_width_filter
    test_bb_width_filter.cpp
)

target_include_directories(test_bb_width_filter PRIVATE
    ../include
)

target_compile_features(test_bb_width_filter PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_bb_width_filter PRIVATE /W4 /O2)
else()
    target_compile_options(test_bb_width_filter PRIVATE -Wall -Wextra -O3)
endif()

set_target_properties(test_bb_width_filter PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "BB Width filter test configured: test_bb_width_filter")

# Hybrid V5/V7 Test: Combining Trend + Volatility Filters
add_executable(test_hybrid_v5_v7
    test_hybrid_v5_v7.cpp
)

target_include_directories(test_hybrid_v5_v7 PRIVATE
    ../include
)

target_compile_features(test_hybrid_v5_v7 PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_hybrid_v5_v7 PRIVATE /W4 /O2)
else()
    target_compile_options(test_hybrid_v5_v7 PRIVATE -Wall -Wextra -O3)
endif()

set_target_properties(test_hybrid_v5_v7 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Hybrid V5/V7 test configured: test_hybrid_v5_v7")

# V7 + Session Filter Combined Test
add_executable(test_v7_session_combined
    test_v7_session_combined.cpp
)

target_include_directories(test_v7_session_combined PRIVATE
    ../include
)

target_compile_features(test_v7_session_combined PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_v7_session_combined PRIVATE /W4 /O2)
else()
    target_compile_options(test_v7_session_combined PRIVATE -Wall -Wextra -O3)
endif()

set_target_properties(test_v7_session_combined PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "V7 + Session filter combined test configured: test_v7_session_combined")

# RSI-Based Mean Reversion Filter Test
add_executable(test_rsi_filter
    test_rsi_filter.cpp
)

target_include_directories(test_rsi_filter PRIVATE
    ../include
)

target_compile_features(test_rsi_filter PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_rsi_filter PRIVATE /W4 /O2)
else()
    target_compile_options(test_rsi_filter PRIVATE -Wall -Wextra -O3)
endif()

set_target_properties(test_rsi_filter PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "RSI filter test configured: test_rsi_filter")

# Adaptive Lot Sizing Test for V7
add_executable(test_adaptive_sizing
    test_adaptive_sizing.cpp
)

target_include_directories(test_adaptive_sizing PRIVATE
    ../include
)

target_compile_features(test_adaptive_sizing PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_adaptive_sizing PRIVATE /W4 /O2)
else()
    target_compile_options(test_adaptive_sizing PRIVATE -Wall -Wextra -O3)
endif()

set_target_properties(test_adaptive_sizing PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Adaptive lot sizing test configured: test_adaptive_sizing")

# V8 Validation Test - Extended XAUUSD Tick Data
add_executable(test_v8_validation
    test_v8_validation.cpp
)

target_include_directories(test_v8_validation PRIVATE
    ../include
)

target_compile_features(test_v8_validation PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_v8_validation PRIVATE /W4 /O2)
else()
    target_compile_options(test_v8_validation PRIVATE -Wall -Wextra -O3)
endif()

set_target_properties(test_v8_validation PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "V8 validation test configured: test_v8_validation")

# Grid Spacing Sweep Test
add_executable(test_spacing_sweep
    test_spacing_sweep.cpp
)

target_include_directories(test_spacing_sweep PRIVATE
    ../include
)

target_compile_features(test_spacing_sweep PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_spacing_sweep PRIVATE /W4 /O2)
else()
    target_compile_options(test_spacing_sweep PRIVATE -Wall -Wextra -O3)
endif()

set_target_properties(test_spacing_sweep PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Grid spacing sweep test configured: test_spacing_sweep")

# Anti-Martingale Position Sizing Test
add_executable(test_anti_martingale
    test_anti_martingale.cpp
)

target_include_directories(test_anti_martingale PRIVATE
    ../include
)

target_compile_features(test_anti_martingale PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_anti_martingale PRIVATE /W4 /O2)
else()
    target_compile_options(test_anti_martingale PRIVATE -Wall -Wextra -O3)
endif()

set_target_properties(test_anti_martingale PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Anti-martingale sizing test configured: test_anti_martingale")

# Drawdown Analysis Test
add_executable(test_drawdown_analysis
    test_drawdown_analysis.cpp
)

target_include_directories(test_drawdown_analysis PRIVATE
    ../include
)

target_compile_features(test_drawdown_analysis PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_drawdown_analysis PRIVATE /W4 /O2)
else()
    target_compile_options(test_drawdown_analysis PRIVATE -Wall -Wextra -O3)
endif()

set_target_properties(test_drawdown_analysis PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Drawdown analysis test configured: test_drawdown_analysis")

# Mean Reversion Strength Indicator Test
add_executable(test_mean_reversion_indicator
    test_mean_reversion_indicator.cpp
)

target_include_directories(test_mean_reversion_indicator PRIVATE
    ../include
)

target_compile_features(test_mean_reversion_indicator PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_mean_reversion_indicator PRIVATE /W4 /O2)
else()
    target_compile_options(test_mean_reversion_indicator PRIVATE -Wall -Wextra -O3)
endif()

set_target_properties(test_mean_reversion_indicator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Mean reversion indicator test configured: test_mean_reversion_indicator")

# V10 Full Combination Validation Test
add_executable(test_v10_validation
    test_v10_validation.cpp
)

target_include_directories(test_v10_validation PRIVATE
    ../include
)

target_compile_features(test_v10_validation PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_v10_validation PRIVATE /W4 /O2)
else()
    target_compile_options(test_v10_validation PRIVATE -Wall -Wextra -O3)
endif()

set_target_properties(test_v10_validation PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "V10 full combination validation test configured: test_v10_validation")

# Early Exit Investigation Test
add_executable(test_early_exit
    test_early_exit.cpp
)

target_include_directories(test_early_exit PRIVATE
    ../include
)

target_compile_features(test_early_exit PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_early_exit PRIVATE /W4 /O2)
else()
    target_compile_options(test_early_exit PRIVATE -Wall -Wextra -O3)
endif()

set_target_properties(test_early_exit PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Early exit investigation test configured: test_early_exit")

# Dynamic Take-Profit Strategy Comparison Test
add_executable(test_dynamic_tp
    test_dynamic_tp.cpp
)

target_include_directories(test_dynamic_tp PRIVATE
    ../include
)

target_compile_features(test_dynamic_tp PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_dynamic_tp PRIVATE /W4 /O2)
else()
    target_compile_options(test_dynamic_tp PRIVATE -Wall -Wextra -O3)
endif()

set_target_properties(test_dynamic_tp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Dynamic TP strategy comparison test configured: test_dynamic_tp")

# Trade Analysis: Winners vs Losers Deep Analysis
add_executable(test_trade_analysis
    test_trade_analysis.cpp
)

target_include_directories(test_trade_analysis PRIVATE
    ../include
)

target_compile_features(test_trade_analysis PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_trade_analysis PRIVATE /W4 /O2)
else()
    target_compile_options(test_trade_analysis PRIVATE -Wall -Wextra -O3)
endif()

set_target_properties(test_trade_analysis PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Trade analysis test configured: test_trade_analysis")

# Correlation Filter Analysis Test
add_executable(test_correlation_filter
    test_correlation_filter.cpp
)

target_include_directories(test_correlation_filter PRIVATE
    ../include
)

target_compile_features(test_correlation_filter PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_correlation_filter PRIVATE /W4 /O2)
else()
    target_compile_options(test_correlation_filter PRIVATE -Wall -Wextra -O3)
endif()

set_target_properties(test_correlation_filter PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Correlation filter analysis test configured: test_correlation_filter")

# D_8_50 Configuration Test (Survive 8%, Spacing $1, DD Protection 50%)
add_executable(test_d8_50
    test_d8_50.cpp
)

target_include_directories(test_d8_50 PRIVATE
    ../include
)

target_compile_features(test_d8_50 PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_d8_50 PRIVATE /W4 /O2)
else()
    target_compile_options(test_d8_50 PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_d8_50 PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_d8_50 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "D_8_50 configuration test configured: test_d8_50")

# Market Oscillation Analysis
add_executable(test_market_oscillation
    test_market_oscillation.cpp
)

target_include_directories(test_market_oscillation PRIVATE
    ../include
)

target_compile_features(test_market_oscillation PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_market_oscillation PRIVATE /W4 /O2)
else()
    target_compile_options(test_market_oscillation PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_market_oscillation PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_market_oscillation PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Market oscillation analysis configured: test_market_oscillation")

# Hedged Fill-Up Strategy Test
add_executable(test_fillup_hedged
    test_fillup_hedged.cpp
)

target_include_directories(test_fillup_hedged PRIVATE
    ../include
)

target_compile_features(test_fillup_hedged PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_fillup_hedged PRIVATE /W4 /O2)
else()
    target_compile_options(test_fillup_hedged PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_fillup_hedged PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_fillup_hedged PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Hedged fill-up test configured: test_fillup_hedged")

# Oscillation Enhancement Modes Test
add_executable(test_oscillation_modes
    test_oscillation_modes.cpp
)

target_include_directories(test_oscillation_modes PRIVATE
    ../include
)

target_compile_features(test_oscillation_modes PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_oscillation_modes PRIVATE /W4 /O2)
else()
    target_compile_options(test_oscillation_modes PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_oscillation_modes PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_oscillation_modes PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Oscillation enhancement modes test configured: test_oscillation_modes")

# Adaptive Spacing Parameter Sweep
add_executable(test_adaptive_spacing_sweep
    test_adaptive_spacing_sweep.cpp
)

target_include_directories(test_adaptive_spacing_sweep PRIVATE
    ../include
)

target_compile_features(test_adaptive_spacing_sweep PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_adaptive_spacing_sweep PRIVATE /W4 /O2)
else()
    target_compile_options(test_adaptive_spacing_sweep PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_adaptive_spacing_sweep PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_adaptive_spacing_sweep PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Adaptive spacing sweep configured: test_adaptive_spacing_sweep")

# Single Adaptive Spacing Test (for parallel execution)
add_executable(test_adaptive_single
    test_adaptive_single.cpp
)

target_include_directories(test_adaptive_single PRIVATE
    ../include
)

target_compile_features(test_adaptive_single PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_adaptive_single PRIVATE /W4 /O2)
else()
    target_compile_options(test_adaptive_single PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_adaptive_single PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_adaptive_single PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Single adaptive test configured: test_adaptive_single")

# Risk-Adjusted Performance Metrics Test
add_executable(test_risk_metrics
    test_risk_metrics.cpp
)

target_include_directories(test_risk_metrics PRIVATE
    ../include
)

target_compile_features(test_risk_metrics PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_risk_metrics PRIVATE /W4 /O2)
else()
    target_compile_options(test_risk_metrics PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_risk_metrics PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_risk_metrics PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Risk metrics test configured: test_risk_metrics")

# Stress Scenarios Test - Synthetic extreme market conditions
add_executable(test_stress_scenarios
    test_stress_scenarios.cpp
)

target_include_directories(test_stress_scenarios PRIVATE
    ../include
)

target_compile_features(test_stress_scenarios PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_stress_scenarios PRIVATE /W4 /O2)
else()
    target_compile_options(test_stress_scenarios PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_stress_scenarios PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_stress_scenarios PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Stress scenarios test configured: test_stress_scenarios")

# NAS100 Oscillation Strategy Test
add_executable(test_nas100_oscillation
    test_nas100_oscillation.cpp
)

target_include_directories(test_nas100_oscillation PRIVATE
    ../include
)

target_compile_features(test_nas100_oscillation PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_nas100_oscillation PRIVATE /W4 /O2)
else()
    target_compile_options(test_nas100_oscillation PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_nas100_oscillation PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_nas100_oscillation PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "NAS100 oscillation test configured: test_nas100_oscillation")

# Out-of-Sample Validation Test (2024 data)
add_executable(test_out_of_sample_2024
    test_out_of_sample_2024.cpp
)

target_include_directories(test_out_of_sample_2024 PRIVATE
    ../include
)

target_compile_features(test_out_of_sample_2024 PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_out_of_sample_2024 PRIVATE /W4 /O2)
else()
    target_compile_options(test_out_of_sample_2024 PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_out_of_sample_2024 PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_out_of_sample_2024 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Out-of-sample validation test configured: test_out_of_sample_2024")

# Adaptive Lookback Test
add_executable(test_adaptive_lookback
    test_adaptive_lookback.cpp
)

target_include_directories(test_adaptive_lookback PRIVATE
    ../include
)

target_compile_features(test_adaptive_lookback PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_adaptive_lookback PRIVATE /W4 /O2)
else()
    target_compile_options(test_adaptive_lookback PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_adaptive_lookback PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_adaptive_lookback PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Adaptive lookback test configured: test_adaptive_lookback")

# Domain 1-10 Investigation Tests
foreach(DOMAIN_NUM RANGE 1 10)
    if(DOMAIN_NUM EQUAL 1)
        set(DOMAIN_NAME "oscillation_characterization")
    elseif(DOMAIN_NUM EQUAL 2)
        set(DOMAIN_NAME "regime_detection")
    elseif(DOMAIN_NUM EQUAL 3)
        set(DOMAIN_NAME "time_patterns")
    elseif(DOMAIN_NUM EQUAL 4)
        set(DOMAIN_NAME "position_lifecycle")
    elseif(DOMAIN_NUM EQUAL 5)
        set(DOMAIN_NAME "entry_optimization")
    elseif(DOMAIN_NUM EQUAL 6)
        set(DOMAIN_NAME "exit_refinement")
    elseif(DOMAIN_NUM EQUAL 7)
        set(DOMAIN_NAME "risk_of_ruin")
    elseif(DOMAIN_NUM EQUAL 8)
        set(DOMAIN_NAME "multi_instrument")
    elseif(DOMAIN_NUM EQUAL 9)
        set(DOMAIN_NAME "capital_allocation")
    elseif(DOMAIN_NUM EQUAL 10)
        set(DOMAIN_NAME "correlation")
    endif()

    set(TARGET_NAME "test_domain${DOMAIN_NUM}_${DOMAIN_NAME}")

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${TARGET_NAME}.cpp")
        add_executable(${TARGET_NAME} ${TARGET_NAME}.cpp)
        target_include_directories(${TARGET_NAME} PRIVATE ../include)
        target_compile_features(${TARGET_NAME} PRIVATE cxx_std_17)

        if(MSVC)
            target_compile_options(${TARGET_NAME} PRIVATE /W4 /O2)
        else()
            target_compile_options(${TARGET_NAME} PRIVATE -Wall -Wextra -O3)
            target_link_libraries(${TARGET_NAME} PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
        endif()

        set_target_properties(${TARGET_NAME} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
        )

        message(STATUS "Domain ${DOMAIN_NUM} test configured: ${TARGET_NAME}")
    endif()
endforeach()

# Momentum Reversal Parameter Sweep
add_executable(test_momentum_reversal_sweep
    test_momentum_reversal_sweep.cpp
)

target_include_directories(test_momentum_reversal_sweep PRIVATE
    ../include
)

target_compile_features(test_momentum_reversal_sweep PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_momentum_reversal_sweep PRIVATE /W4 /O2)
else()
    target_compile_options(test_momentum_reversal_sweep PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_momentum_reversal_sweep PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_momentum_reversal_sweep PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Momentum reversal sweep configured: test_momentum_reversal_sweep")

# CycleSurfer EA Backtest (Ehlers Roofing Filter)
add_executable(test_cycle_surfer
    test_cycle_surfer.cpp
)

target_include_directories(test_cycle_surfer PRIVATE
    ../include
)

target_compile_features(test_cycle_surfer PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_cycle_surfer PRIVATE /W4 /O2)
else()
    target_compile_options(test_cycle_surfer PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_cycle_surfer PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_cycle_surfer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "CycleSurfer EA backtest configured: test_cycle_surfer")

# Price vs Oscillation Relation Analysis
add_executable(test_price_oscillation_relation
    test_price_oscillation_relation.cpp
)

target_include_directories(test_price_oscillation_relation PRIVATE
    ../include
)

target_compile_features(test_price_oscillation_relation PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_price_oscillation_relation PRIVATE /W4 /O2)
else()
    target_compile_options(test_price_oscillation_relation PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_price_oscillation_relation PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_price_oscillation_relation PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Price-oscillation relation analysis configured: test_price_oscillation_relation")

# Combined Improvements Test (50% sizing, 4h exit, % threshold)
add_executable(test_improvements_combined
    test_improvements_combined.cpp
)

target_include_directories(test_improvements_combined PRIVATE
    ../include
)

target_compile_features(test_improvements_combined PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_improvements_combined PRIVATE /W4 /O2)
else()
    target_compile_options(test_improvements_combined PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_improvements_combined PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_improvements_combined PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Combined improvements test configured: test_improvements_combined")

# FillUpOscillation with improvements test
add_executable(test_fillup_osc_improvements
    test_fillup_osc_improvements.cpp
)

target_include_directories(test_fillup_osc_improvements PRIVATE
    ../include
)

target_compile_features(test_fillup_osc_improvements PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_fillup_osc_improvements PRIVATE /W4 /O2)
else()
    target_compile_options(test_fillup_osc_improvements PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_fillup_osc_improvements PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_fillup_osc_improvements PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "FillUpOscillation improvements test configured: test_fillup_osc_improvements")

# H1/H2 Out-of-Sample Validation
add_executable(test_h1h2_validation
    test_h1h2_validation.cpp
)

target_include_directories(test_h1h2_validation PRIVATE
    ../include
)

target_compile_features(test_h1h2_validation PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_h1h2_validation PRIVATE /W4 /O2)
else()
    target_compile_options(test_h1h2_validation PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_h1h2_validation PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_h1h2_validation PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "H1/H2 validation test configured: test_h1h2_validation")

# Parameter Stability Analysis
add_executable(test_parameter_stability
    test_parameter_stability.cpp
)

target_include_directories(test_parameter_stability PRIVATE
    ../include
)

target_compile_features(test_parameter_stability PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_parameter_stability PRIVATE /W4 /O2)
else()
    target_compile_options(test_parameter_stability PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_parameter_stability PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_parameter_stability PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Parameter stability test configured: test_parameter_stability")

# Risk Management Research
add_executable(test_risk_management
    test_risk_management.cpp
)

target_include_directories(test_risk_management PRIVATE
    ../include
)

target_compile_features(test_risk_management PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_risk_management PRIVATE /W4 /O2)
else()
    target_compile_options(test_risk_management PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_risk_management PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_risk_management PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Risk management test configured: test_risk_management")

# Multi-Year Backtest
add_executable(test_multi_year
    test_multi_year.cpp
)

target_include_directories(test_multi_year PRIVATE
    ../include
)

target_compile_features(test_multi_year PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_multi_year PRIVATE /W4 /O2)
else()
    target_compile_options(test_multi_year PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_multi_year PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_multi_year PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Multi-year test configured: test_multi_year")

# Typical Volatility Analysis
add_executable(test_typical_volatility
    test_typical_volatility.cpp
)

target_include_directories(test_typical_volatility PRIVATE
    ../include
)

target_compile_features(test_typical_volatility PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_typical_volatility PRIVATE /W4 /O2)
else()
    target_compile_options(test_typical_volatility PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_typical_volatility PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_typical_volatility PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Typical volatility test configured: test_typical_volatility")

# Oscillation Parameter Sweep
add_executable(test_oscillation_sweep
    test_oscillation_sweep.cpp
)

target_include_directories(test_oscillation_sweep PRIVATE
    ../include
)

target_compile_features(test_oscillation_sweep PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_oscillation_sweep PRIVATE /W4 /O2)
else()
    target_compile_options(test_oscillation_sweep PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_oscillation_sweep PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_oscillation_sweep PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Oscillation sweep test configured: test_oscillation_sweep")

# Frequency Domain Analysis for FillUpOscillation
add_executable(test_frequency_analysis
    test_frequency_analysis.cpp
)

target_include_directories(test_frequency_analysis PRIVATE
    ../include
)

target_compile_features(test_frequency_analysis PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_frequency_analysis PRIVATE /W4 /O2)
else()
    target_compile_options(test_frequency_analysis PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_frequency_analysis PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_frequency_analysis PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Frequency domain analysis configured: test_frequency_analysis")

# Kapitza Advanced Control Strategy
add_executable(test_kapitza
    test_kapitza.cpp
)

target_include_directories(test_kapitza PRIVATE
    ../include
)

target_compile_features(test_kapitza PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_kapitza PRIVATE /W4 /O2)
else()
    target_compile_options(test_kapitza PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_kapitza PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_kapitza PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Kapitza test configured: test_kapitza")

# Test: Kapitza Parameter Tuning
add_executable(test_kapitza_tuning
    test_kapitza_tuning.cpp
)

target_include_directories(test_kapitza_tuning PRIVATE
    ../include
)

target_compile_features(test_kapitza_tuning PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_kapitza_tuning PRIVATE /W4 /O2)
else()
    target_compile_options(test_kapitza_tuning PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_kapitza_tuning PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_kapitza_tuning PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Kapitza tuning test configured: test_kapitza_tuning")

# Test: Attribution Analysis
add_executable(test_attribution_analysis
    test_attribution_analysis.cpp
)

target_include_directories(test_attribution_analysis PRIVATE
    ../include
)

target_compile_features(test_attribution_analysis PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_attribution_analysis PRIVATE /W4 /O2)
else()
    target_compile_options(test_attribution_analysis PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_attribution_analysis PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_attribution_analysis PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Attribution analysis test configured: test_attribution_analysis")

# Spacing Investigation Test
add_executable(test_spacing_investigation
    test_spacing_investigation.cpp
)

target_include_directories(test_spacing_investigation PRIVATE
    ../include
)

target_compile_features(test_spacing_investigation PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_spacing_investigation PRIVATE /W4 /O2)
else()
    target_compile_options(test_spacing_investigation PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_spacing_investigation PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_spacing_investigation PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Spacing investigation test configured: test_spacing_investigation")

# Trend Adaptive Test
add_executable(test_trend_adaptive
    test_trend_adaptive.cpp
)

target_include_directories(test_trend_adaptive PRIVATE
    ../include
)

target_compile_features(test_trend_adaptive PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_trend_adaptive PRIVATE /W4 /O2)
else()
    target_compile_options(test_trend_adaptive PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_trend_adaptive PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_trend_adaptive PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Trend adaptive test configured: test_trend_adaptive")

# Trend Lookback Sweep Test
add_executable(test_trend_lookback_sweep
    test_trend_lookback_sweep.cpp
)

target_include_directories(test_trend_lookback_sweep PRIVATE
    ../include
)

target_compile_features(test_trend_lookback_sweep PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_trend_lookback_sweep PRIVATE /W4 /O2)
else()
    target_compile_options(test_trend_lookback_sweep PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_trend_lookback_sweep PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_trend_lookback_sweep PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Trend lookback sweep configured: test_trend_lookback_sweep")

# Trend Lookback Test (using FillUpOscillation)
add_executable(test_trend_lookback
    test_trend_lookback.cpp
)

target_include_directories(test_trend_lookback PRIVATE
    ../include
)

target_compile_features(test_trend_lookback PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_trend_lookback PRIVATE /W4 /O2)
else()
    target_compile_options(test_trend_lookback PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_trend_lookback PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_trend_lookback PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Trend lookback test configured: test_trend_lookback")

# Trend Calibration Test
add_executable(test_trend_calibration
    test_trend_calibration.cpp
)

target_include_directories(test_trend_calibration PRIVATE
    ../include
)

target_compile_features(test_trend_calibration PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_trend_calibration PRIVATE /W4 /O2)
else()
    target_compile_options(test_trend_calibration PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_trend_calibration PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_trend_calibration PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Trend calibration test configured: test_trend_calibration")

# Self-Calibrating Test
add_executable(test_self_calibrating
    test_self_calibrating.cpp
)

target_include_directories(test_self_calibrating PRIVATE
    ../include
)

target_compile_features(test_self_calibrating PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_self_calibrating PRIVATE /W4 /O2)
else()
    target_compile_options(test_self_calibrating PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_self_calibrating PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_self_calibrating PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Self-calibrating test configured: test_self_calibrating")

add_executable(test_spacing_sweep_simple test_spacing_sweep_simple.cpp)
target_include_directories(test_spacing_sweep_simple PRIVATE ../include)
target_compile_features(test_spacing_sweep_simple PRIVATE cxx_std_17)
target_compile_options(test_spacing_sweep_simple PRIVATE -O3)
target_link_libraries(test_spacing_sweep_simple PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_spacing_sweep_simple PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")


add_executable(test_spacing_by_period test_spacing_by_period.cpp)
target_include_directories(test_spacing_by_period PRIVATE ../include)
target_compile_features(test_spacing_by_period PRIVATE cxx_std_17)
target_compile_options(test_spacing_by_period PRIVATE -O3)
target_link_libraries(test_spacing_by_period PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_spacing_by_period PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")


add_executable(test_spacing_2024 test_spacing_2024.cpp)
target_include_directories(test_spacing_2024 PRIVATE ../include)
target_compile_features(test_spacing_2024 PRIVATE cxx_std_17)
target_compile_options(test_spacing_2024 PRIVATE -O3)
target_link_libraries(test_spacing_2024 PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_spacing_2024 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")


add_executable(test_oscillation_analysis test_oscillation_analysis.cpp)
target_include_directories(test_oscillation_analysis PRIVATE ../include)
target_compile_features(test_oscillation_analysis PRIVATE cxx_std_17)
target_compile_options(test_oscillation_analysis PRIVATE -O3)
target_link_libraries(test_oscillation_analysis PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_oscillation_analysis PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")


add_executable(test_trend_direction test_trend_direction.cpp)
target_include_directories(test_trend_direction PRIVATE ../include)
target_compile_features(test_trend_direction PRIVATE cxx_std_17)
target_compile_options(test_trend_direction PRIVATE -O3)
target_link_libraries(test_trend_direction PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_trend_direction PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")


add_executable(test_bounce_analysis test_bounce_analysis.cpp)
target_include_directories(test_bounce_analysis PRIVATE ../include)
target_compile_features(test_bounce_analysis PRIVATE cxx_std_17)
target_compile_options(test_bounce_analysis PRIVATE -O3)
target_link_libraries(test_bounce_analysis PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_bounce_analysis PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")


add_executable(test_trailing_stop test_trailing_stop.cpp)
target_include_directories(test_trailing_stop PRIVATE ../include)
target_compile_features(test_trailing_stop PRIVATE cxx_std_17)
target_compile_options(test_trailing_stop PRIVATE -O3)
target_link_libraries(test_trailing_stop PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_trailing_stop PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")


add_executable(test_trailing_v2 test_trailing_v2.cpp)
target_include_directories(test_trailing_v2 PRIVATE ../include)
target_compile_features(test_trailing_v2 PRIVATE cxx_std_17)
target_compile_options(test_trailing_v2 PRIVATE -O3)
target_link_libraries(test_trailing_v2 PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_trailing_v2 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")


add_executable(test_trailing_approaches test_trailing_approaches.cpp)
target_include_directories(test_trailing_approaches PRIVATE ../include)
target_compile_features(test_trailing_approaches PRIVATE cxx_std_17)
target_compile_options(test_trailing_approaches PRIVATE -O3)
target_link_libraries(test_trailing_approaches PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_trailing_approaches PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")


add_executable(test_trailing_sweep test_trailing_sweep.cpp)
target_include_directories(test_trailing_sweep PRIVATE ../include)
target_compile_features(test_trailing_sweep PRIVATE cxx_std_17)
target_compile_options(test_trailing_sweep PRIVATE -O3)
target_link_libraries(test_trailing_sweep PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_trailing_sweep PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")


add_executable(test_trailing_full_sweep test_trailing_full_sweep.cpp)
target_include_directories(test_trailing_full_sweep PRIVATE ../include)
target_compile_features(test_trailing_full_sweep PRIVATE cxx_std_17)
target_compile_options(test_trailing_full_sweep PRIVATE -O3)
target_link_libraries(test_trailing_full_sweep PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_trailing_full_sweep PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")


add_executable(test_trailing_5d_sweep test_trailing_5d_sweep.cpp)
target_include_directories(test_trailing_5d_sweep PRIVATE ../include)
target_compile_features(test_trailing_5d_sweep PRIVATE cxx_std_17)
target_compile_options(test_trailing_5d_sweep PRIVATE -O3)
target_link_libraries(test_trailing_5d_sweep PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_trailing_5d_sweep PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")

# Multi-threaded parallel version
add_executable(test_trailing_5d_parallel test_trailing_5d_parallel.cpp)
target_include_directories(test_trailing_5d_parallel PRIVATE ../include)
target_compile_features(test_trailing_5d_parallel PRIVATE cxx_std_17)
target_compile_options(test_trailing_5d_parallel PRIVATE -O3)
target_link_libraries(test_trailing_5d_parallel PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_trailing_5d_parallel PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")


# Return Attribution Analysis
add_executable(test_return_attribution
    test_return_attribution.cpp
)

target_include_directories(test_return_attribution PRIVATE
    ../include
)

target_compile_features(test_return_attribution PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_return_attribution PRIVATE /W4 /O2)
else()
    target_compile_options(test_return_attribution PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_return_attribution PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_return_attribution PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Return attribution test configured: test_return_attribution")

# Kill Switch Stress Test
add_executable(test_kill_switch
    test_kill_switch.cpp
)

target_include_directories(test_kill_switch PRIVATE
    ../include
)

target_compile_features(test_kill_switch PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_kill_switch PRIVATE /W4 /O2)
else()
    target_compile_options(test_kill_switch PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_kill_switch PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_kill_switch PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Kill switch stress test configured: test_kill_switch")

# Kill Switch Real Data Test
add_executable(test_kill_switch_real_data
    test_kill_switch_real_data.cpp
)

target_include_directories(test_kill_switch_real_data PRIVATE
    ../include
)

target_compile_features(test_kill_switch_real_data PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_kill_switch_real_data PRIVATE /W4 /O2)
else()
    target_compile_options(test_kill_switch_real_data PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_kill_switch_real_data PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_kill_switch_real_data PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Kill switch real data test configured: test_kill_switch_real_data")

# 7 Market-Parasitic Strategies Investigation
add_executable(test_7_strategies
    test_7_strategies.cpp
)

target_include_directories(test_7_strategies PRIVATE
    ../include
)

target_compile_features(test_7_strategies PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_7_strategies PRIVATE /W4 /O2)
else()
    target_compile_options(test_7_strategies PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_7_strategies PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_7_strategies PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "7 strategies investigation configured: test_7_strategies")

# XAGUSD FillUpOscillation Parameter Sweep
add_executable(test_xagusd_fillup
    test_xagusd_fillup.cpp
)

target_include_directories(test_xagusd_fillup PRIVATE
    ../include
)

target_compile_features(test_xagusd_fillup PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_xagusd_fillup PRIVATE /W4 /O2)
else()
    target_compile_options(test_xagusd_fillup PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_xagusd_fillup PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_xagusd_fillup PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "XAGUSD FillUp sweep configured: test_xagusd_fillup")

# DD Reduction Parameter Sweep (parameterized executable for parallel sweep)
add_executable(test_dd_reduction
    test_dd_reduction.cpp
)

target_include_directories(test_dd_reduction PRIVATE
    ../include
)

target_compile_features(test_dd_reduction PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_dd_reduction PRIVATE /W4 /O2)
else()
    target_compile_options(test_dd_reduction PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_dd_reduction PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_dd_reduction PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "DD Reduction sweep configured: test_dd_reduction")

# Rubber Band TP Sweep (parallel)
add_executable(test_rubberband_tp
    test_rubberband_tp.cpp
)

target_include_directories(test_rubberband_tp PRIVATE
    ../include
)

target_compile_features(test_rubberband_tp PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_rubberband_tp PRIVATE /W4 /O2)
else()
    target_compile_options(test_rubberband_tp PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_rubberband_tp PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_rubberband_tp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Rubber Band TP sweep configured: test_rubberband_tp")

# XAGUSD FillUpStrategy Spacing Sweep (parallel)
add_executable(test_xagusd_spacing_sweep
    test_xagusd_spacing_sweep.cpp
)

target_include_directories(test_xagusd_spacing_sweep PRIVATE
    ../include
)

target_compile_features(test_xagusd_spacing_sweep PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_xagusd_spacing_sweep PRIVATE /W4 /O2)
else()
    target_compile_options(test_xagusd_spacing_sweep PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_xagusd_spacing_sweep PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_xagusd_spacing_sweep PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "XAGUSD spacing sweep configured: test_xagusd_spacing_sweep")

add_executable(test_xagusd_sweep
    test_xagusd_sweep.cpp
)
target_include_directories(test_xagusd_sweep PRIVATE
    ../include
)
target_compile_features(test_xagusd_sweep PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_xagusd_sweep PRIVATE /W4 /O2)
else()
    target_compile_options(test_xagusd_sweep PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_xagusd_sweep PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_xagusd_sweep PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)
message(STATUS "XAGUSD sweep configured: test_xagusd_sweep")

add_executable(test_xagusd_pct_spacing
    test_xagusd_pct_spacing.cpp
)
target_include_directories(test_xagusd_pct_spacing PRIVATE
    ../include
)
target_compile_features(test_xagusd_pct_spacing PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_xagusd_pct_spacing PRIVATE /W4 /O2)
else()
    target_compile_options(test_xagusd_pct_spacing PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_xagusd_pct_spacing PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_xagusd_pct_spacing PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

add_executable(test_xauusd_ath_bands
    test_xauusd_ath_bands.cpp
)
target_include_directories(test_xauusd_ath_bands PRIVATE
    ../include
)
target_compile_features(test_xauusd_ath_bands PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_xauusd_ath_bands PRIVATE /W4 /O2)
else()
    target_compile_options(test_xauusd_ath_bands PRIVATE -Wall -Wextra -O3)
endif()
set_target_properties(test_xauusd_ath_bands PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)
message(STATUS "XAUUSD ATH bands configured: test_xauusd_ath_bands")

add_executable(test_xagusd_ath_bands
    test_xagusd_ath_bands.cpp
)
target_include_directories(test_xagusd_ath_bands PRIVATE
    ../include
)
target_compile_features(test_xagusd_ath_bands PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_xagusd_ath_bands PRIVATE /W4 /O2)
else()
    target_compile_options(test_xagusd_ath_bands PRIVATE -Wall -Wextra -O3)
endif()
set_target_properties(test_xagusd_ath_bands PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)
message(STATUS "XAGUSD ATH bands configured: test_xagusd_ath_bands")

add_executable(test_xagusd_diagnostic
    test_xagusd_diagnostic.cpp
)

target_include_directories(test_xagusd_diagnostic PRIVATE
    ../include
)

target_compile_features(test_xagusd_diagnostic PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_xagusd_diagnostic PRIVATE /W4 /O2)
else()
    target_compile_options(test_xagusd_diagnostic PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_xagusd_diagnostic PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_xagusd_diagnostic PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

add_executable(test_xagusd_fixed
    test_xagusd_fixed.cpp
)

target_include_directories(test_xagusd_fixed PRIVATE
    ../include
)

target_compile_features(test_xagusd_fixed PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_xagusd_fixed PRIVATE /W4 /O2)
else()
    target_compile_options(test_xagusd_fixed PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_xagusd_fixed PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_xagusd_fixed PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

add_executable(test_silver_precise
    test_silver_precise.cpp
)

target_include_directories(test_silver_precise PRIVATE
    ../include
)

target_compile_features(test_silver_precise PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_silver_precise PRIVATE /W4 /O2)
else()
    target_compile_options(test_silver_precise PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_silver_precise PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_silver_precise PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

# Engine-based XAGUSD backtest with parallel sweep
add_executable(test_engine_xagusd
    test_engine_xagusd.cpp
)

target_include_directories(test_engine_xagusd PRIVATE
    ../include
)

target_compile_features(test_engine_xagusd PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_engine_xagusd PRIVATE /W4 /O2)
else()
    target_compile_options(test_engine_xagusd PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_engine_xagusd PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_engine_xagusd PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

add_executable(test_preset_overfitting
    test_preset_overfitting.cpp
)

target_include_directories(test_preset_overfitting PRIVATE
    ../include
)

target_compile_features(test_preset_overfitting PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_preset_overfitting PRIVATE /W4 /O2)
else()
    target_compile_options(test_preset_overfitting PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_preset_overfitting PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_preset_overfitting PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

add_executable(test_xagusd_consistency
    test_xagusd_consistency.cpp
)

target_include_directories(test_xagusd_consistency PRIVATE
    ../include
)

target_compile_features(test_xagusd_consistency PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_xagusd_consistency PRIVATE /W4 /O2)
else()
    target_compile_options(test_xagusd_consistency PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_xagusd_consistency PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_xagusd_consistency PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

add_executable(test_xagusd_compound_cap
    test_xagusd_compound_cap.cpp
)

target_include_directories(test_xagusd_compound_cap PRIVATE
    ../include
)

target_compile_features(test_xagusd_compound_cap PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_xagusd_compound_cap PRIVATE /W4 /O2)
else()
    target_compile_options(test_xagusd_compound_cap PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_xagusd_compound_cap PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_xagusd_compound_cap PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

add_executable(test_xagusd_2024_optimize
    test_xagusd_2024_optimize.cpp
)

target_include_directories(test_xagusd_2024_optimize PRIVATE
    ../include
)

target_compile_features(test_xagusd_2024_optimize PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_xagusd_2024_optimize PRIVATE /W4 /O2)
else()
    target_compile_options(test_xagusd_2024_optimize PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_xagusd_2024_optimize PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_xagusd_2024_optimize PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

# Auto-calibration validation (XAUUSD, parallel)
add_executable(test_auto_calibration
    test_auto_calibration.cpp
)

target_include_directories(test_auto_calibration PRIVATE
    ../include
)

target_compile_features(test_auto_calibration PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_auto_calibration PRIVATE /W4 /O2)
else()
    target_compile_options(test_auto_calibration PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_auto_calibration PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_auto_calibration PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

# XAUUSD Deep Parameter Sweep (parallel, ~3200 configs)
add_executable(test_xauusd_deep_sweep
    test_xauusd_deep_sweep.cpp
)

target_include_directories(test_xauusd_deep_sweep PRIVATE
    ../include
)

target_compile_features(test_xauusd_deep_sweep PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_xauusd_deep_sweep PRIVATE /W4 /O2)
else()
    target_compile_options(test_xauusd_deep_sweep PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_xauusd_deep_sweep PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_xauusd_deep_sweep PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

# Wide Spacing Analysis - Trend vs Oscillation Investigation
add_executable(test_wide_spacing_analysis
    test_wide_spacing_analysis.cpp
)

target_include_directories(test_wide_spacing_analysis PRIVATE
    ../include
)

target_compile_features(test_wide_spacing_analysis PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(test_wide_spacing_analysis PRIVATE /W4 /O2)
else()
    target_compile_options(test_wide_spacing_analysis PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_wide_spacing_analysis PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()

set_target_properties(test_wide_spacing_analysis PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)
add_executable(test_small_spacing test_small_spacing.cpp)
target_include_directories(test_small_spacing PRIVATE ../include)
target_compile_features(test_small_spacing PRIVATE cxx_std_17)
target_compile_options(test_small_spacing PRIVATE -O3)
target_link_libraries(test_small_spacing PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_small_spacing PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")

add_executable(test_small_spacing_regime test_small_spacing_regime.cpp)
target_include_directories(test_small_spacing_regime PRIVATE ../include)
target_compile_features(test_small_spacing_regime PRIVATE cxx_std_17)
target_compile_options(test_small_spacing_regime PRIVATE -O3)
target_link_libraries(test_small_spacing_regime PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_small_spacing_regime PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")

add_executable(test_pct_spacing_regime test_pct_spacing_regime.cpp)
target_include_directories(test_pct_spacing_regime PRIVATE ../include)
target_compile_features(test_pct_spacing_regime PRIVATE cxx_std_17)
target_compile_options(test_pct_spacing_regime PRIVATE -O3)
target_link_libraries(test_pct_spacing_regime PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_pct_spacing_regime PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")

add_executable(test_pct_regime_parallel test_pct_regime_parallel.cpp)
target_include_directories(test_pct_regime_parallel PRIVATE ../include)
target_compile_features(test_pct_regime_parallel PRIVATE cxx_std_17)
target_compile_options(test_pct_regime_parallel PRIVATE -O3)
target_link_libraries(test_pct_regime_parallel PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_pct_regime_parallel PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")

add_executable(test_sweetspot_sweep test_sweetspot_sweep.cpp)
target_include_directories(test_sweetspot_sweep PRIVATE ../include)
target_compile_features(test_sweetspot_sweep PRIVATE cxx_std_17)
target_compile_options(test_sweetspot_sweep PRIVATE -O3)
target_link_libraries(test_sweetspot_sweep PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_sweetspot_sweep PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")

# Edge of Chaos Parameter Investigation
add_executable(test_edge_of_chaos test_edge_of_chaos.cpp)
target_include_directories(test_edge_of_chaos PRIVATE ../include)
target_compile_features(test_edge_of_chaos PRIVATE cxx_std_17)
target_compile_options(test_edge_of_chaos PRIVATE -O3)
target_link_libraries(test_edge_of_chaos PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_edge_of_chaos PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Edge of chaos test configured: test_edge_of_chaos")

# Floating Attractor Grid - Chaos Concept #1
add_executable(test_floating_attractor test_floating_attractor.cpp)
target_include_directories(test_floating_attractor PRIVATE ../include)
target_compile_features(test_floating_attractor PRIVATE cxx_std_17)
target_compile_options(test_floating_attractor PRIVATE -O3)
target_link_libraries(test_floating_attractor PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_floating_attractor PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Floating attractor grid test configured: test_floating_attractor")

# Entropy Export Strategy - Chaos Concept #6 (Dissipative Structures)
add_executable(test_entropy_export test_entropy_export.cpp)
target_include_directories(test_entropy_export PRIVATE ../include)
target_compile_features(test_entropy_export PRIVATE cxx_std_17)
target_compile_options(test_entropy_export PRIVATE -O3)
target_link_libraries(test_entropy_export PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_entropy_export PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Entropy export strategy test configured: test_entropy_export")

# Noise Scaling Strategy - Chaos Concept #8 (Noise as Signal Carrier)
add_executable(test_noise_scaling test_noise_scaling.cpp)
target_include_directories(test_noise_scaling PRIVATE ../include)
target_compile_features(test_noise_scaling PRIVATE cxx_std_17)
target_compile_options(test_noise_scaling PRIVATE -O3)
target_link_libraries(test_noise_scaling PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_noise_scaling PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Noise scaling strategy test configured: test_noise_scaling")


# Fractal Grid Multi-Scale Strategy Test - Chaos Concept #7
add_executable(test_fractal_grid test_fractal_grid.cpp)
target_include_directories(test_fractal_grid PRIVATE ../include)
target_compile_features(test_fractal_grid PRIVATE cxx_std_17)
target_compile_options(test_fractal_grid PRIVATE -O3)
target_link_libraries(test_fractal_grid PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_fractal_grid PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Fractal grid test configured: test_fractal_grid")

# OGY Chaos Control Strategy - Chaos Concept #4
add_executable(test_ogy_control test_ogy_control.cpp)
target_include_directories(test_ogy_control PRIVATE ../include)
target_compile_features(test_ogy_control PRIVATE cxx_std_17)
target_compile_options(test_ogy_control PRIVATE -O3)
target_link_libraries(test_ogy_control PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_ogy_control PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "OGY chaos control test configured: test_ogy_control")

# Chaos Synchronization - Gold/Silver Ratio Mean-Reversion
add_executable(test_chaos_sync test_chaos_sync.cpp)
target_include_directories(test_chaos_sync PRIVATE ../include)
target_compile_features(test_chaos_sync PRIVATE cxx_std_17)
target_compile_options(test_chaos_sync PRIVATE -O3)
target_link_libraries(test_chaos_sync PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_chaos_sync PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Chaos synchronization test configured: test_chaos_sync")

# Bifurcation Detection - Chaos Concept #5 (Regime Transition Detection)
add_executable(test_bifurcation test_bifurcation.cpp)
target_include_directories(test_bifurcation PRIVATE ../include)
target_compile_features(test_bifurcation PRIVATE cxx_std_17)
target_compile_options(test_bifurcation PRIVATE -O3)
target_link_libraries(test_bifurcation PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_bifurcation PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Bifurcation detection test configured: test_bifurcation")

# Entropy Export V2 - Time and Loss based loser cutting (Dissipative Structures)
add_executable(test_entropy_export_v2 test_entropy_export_v2.cpp)
target_include_directories(test_entropy_export_v2 PRIVATE ../include)
target_compile_features(test_entropy_export_v2 PRIVATE cxx_std_17)
target_compile_options(test_entropy_export_v2 PRIVATE -O3)
target_link_libraries(test_entropy_export_v2 PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_entropy_export_v2 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Entropy export v2 test configured: test_entropy_export_v2")

# Combined Chaos Strategy - Floating Attractor + Velocity Zero-Crossing + Edge-of-Chaos
add_executable(test_combined_chaos test_combined_chaos.cpp)
target_include_directories(test_combined_chaos PRIVATE ../include)
target_compile_features(test_combined_chaos PRIVATE cxx_std_17)
target_compile_options(test_combined_chaos PRIVATE -O3)
target_link_libraries(test_combined_chaos PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_combined_chaos PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Combined chaos strategy test configured: test_combined_chaos")

# Ornstein-Uhlenbeck Mean-Reversion Strategy Test
add_executable(test_ornstein_uhlenbeck test_ornstein_uhlenbeck.cpp)
target_include_directories(test_ornstein_uhlenbeck PRIVATE ../include)
target_compile_features(test_ornstein_uhlenbeck PRIVATE cxx_std_17)
target_compile_options(test_ornstein_uhlenbeck PRIVATE -O3)
target_link_libraries(test_ornstein_uhlenbeck PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_ornstein_uhlenbeck PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "OU mean-reversion test configured: test_ornstein_uhlenbeck")

# Kalman Filter Dynamic Attractor Strategy - Optimal price estimation from noisy ticks
add_executable(test_kalman_filter test_kalman_filter.cpp)
target_include_directories(test_kalman_filter PRIVATE ../include)
target_compile_features(test_kalman_filter PRIVATE cxx_std_17)
target_compile_options(test_kalman_filter PRIVATE -O3)
target_link_libraries(test_kalman_filter PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_kalman_filter PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Kalman filter strategy test configured: test_kalman_filter")

# Regime-Switching (HMM-inspired) Strategy Parallel Sweep
add_executable(test_regime_switching test_regime_switching.cpp)
target_include_directories(test_regime_switching PRIVATE ../include)
target_compile_features(test_regime_switching PRIVATE cxx_std_17)
target_compile_options(test_regime_switching PRIVATE -O3)
target_link_libraries(test_regime_switching PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_regime_switching PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Regime-switching strategy test configured: test_regime_switching")

# Adaptive Control Strategy (Self-Tuning Parameters) Parallel Sweep
add_executable(test_adaptive_control test_adaptive_control.cpp)
target_include_directories(test_adaptive_control PRIVATE ../include)
target_compile_features(test_adaptive_control PRIVATE cxx_std_17)
target_compile_options(test_adaptive_control PRIVATE -O3)
target_link_libraries(test_adaptive_control PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_adaptive_control PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Adaptive control strategy test configured: test_adaptive_control")

# Adaptive Control Strategy Quick Sweep
add_executable(test_adaptive_control_quick test_adaptive_control_quick.cpp)
target_include_directories(test_adaptive_control_quick PRIVATE ../include)
target_compile_features(test_adaptive_control_quick PRIVATE cxx_std_17)
target_compile_options(test_adaptive_control_quick PRIVATE -O3)
target_link_libraries(test_adaptive_control_quick PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_adaptive_control_quick PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Adaptive control quick test configured: test_adaptive_control_quick")

# Van der Pol Oscillator Phase-Based Trading Parallel Sweep
add_executable(test_vanderpol test_vanderpol.cpp)
target_include_directories(test_vanderpol PRIVATE ../include)
target_compile_features(test_vanderpol PRIVATE cxx_std_17)
target_compile_options(test_vanderpol PRIVATE -O3)
target_link_libraries(test_vanderpol PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_vanderpol PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Van der Pol oscillator test configured: test_vanderpol")

# Barbell Sizing Strategy - Taleb's Asymmetric Sizing (Ju Concept #1)
add_executable(test_barbell_sizing test_barbell_sizing.cpp)
target_include_directories(test_barbell_sizing PRIVATE ../include)
target_compile_features(test_barbell_sizing PRIVATE cxx_std_17)
target_compile_options(test_barbell_sizing PRIVATE -O3)
target_link_libraries(test_barbell_sizing PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_barbell_sizing PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Barbell sizing strategy test configured: test_barbell_sizing")


# Via Negativa Strategy - Taleb's Improvement by Removal (Ju Concept #3)
add_executable(test_via_negativa test_via_negativa.cpp)
target_include_directories(test_via_negativa PRIVATE ../include)
target_compile_features(test_via_negativa PRIVATE cxx_std_17)
target_compile_options(test_via_negativa PRIVATE -O3)
target_link_libraries(test_via_negativa PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_via_negativa PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Via Negativa strategy test configured: test_via_negativa")


# Reflexivity Strategy - Soros's Feedback Loops (Ju Concept #4)
add_executable(test_reflexivity test_reflexivity.cpp)
target_include_directories(test_reflexivity PRIVATE ../include)
target_compile_features(test_reflexivity PRIVATE cxx_std_17)
target_compile_options(test_reflexivity PRIVATE -O3)
target_link_libraries(test_reflexivity PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_reflexivity PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Reflexivity strategy test configured: test_reflexivity")


# Wu Wei Strategy - Effortless Action (Ju Concept #5)
add_executable(test_wuwei test_wuwei.cpp)
target_include_directories(test_wuwei PRIVATE ../include)
target_compile_features(test_wuwei PRIVATE cxx_std_17)
target_compile_options(test_wuwei PRIVATE -O3)
target_link_libraries(test_wuwei PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_wuwei PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Wu Wei strategy test configured: test_wuwei")


# Combined Ju Strategy - All three promising concepts together
add_executable(test_combined_ju test_combined_ju.cpp)
target_include_directories(test_combined_ju PRIVATE ../include)
target_compile_features(test_combined_ju PRIVATE cxx_std_17)
target_compile_options(test_combined_ju PRIVATE -O3)
target_link_libraries(test_combined_ju PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_combined_ju PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Combined Ju strategy test configured: test_combined_ju")


# Combined Ju Strategy Sweep - Find optimal parameters
add_executable(test_combined_ju_sweep test_combined_ju_sweep.cpp)
target_include_directories(test_combined_ju_sweep PRIVATE ../include)
target_compile_features(test_combined_ju_sweep PRIVATE cxx_std_17)
target_compile_options(test_combined_ju_sweep PRIVATE -O3)
target_link_libraries(test_combined_ju_sweep PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_combined_ju_sweep PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Combined Ju sweep test configured: test_combined_ju_sweep")

# Combined Ju regime validation (2024 vs 2025)
add_executable(test_combined_ju_regime test_combined_ju_regime.cpp)
target_include_directories(test_combined_ju_regime PRIVATE ../include)
target_compile_features(test_combined_ju_regime PRIVATE cxx_std_17)
target_compile_options(test_combined_ju_regime PRIVATE -O3)
target_link_libraries(test_combined_ju_regime PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_combined_ju_regime PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Combined Ju regime test configured: test_combined_ju_regime")

# EA comparison: v4 vs v5 vs CombinedJu
add_executable(test_ea_comparison test_ea_comparison.cpp)
target_include_directories(test_ea_comparison PRIVATE ../include)
target_compile_features(test_ea_comparison PRIVATE cxx_std_17)
target_compile_options(test_ea_comparison PRIVATE -O3)
target_link_libraries(test_ea_comparison PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_ea_comparison PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "EA comparison test configured: test_ea_comparison")

# MT5 vs C++ Backtest Comparison (extended period)
add_executable(test_mt5_vs_cpp_comparison test_mt5_vs_cpp_comparison.cpp)
target_include_directories(test_mt5_vs_cpp_comparison PRIVATE ../include)
target_compile_features(test_mt5_vs_cpp_comparison PRIVATE cxx_std_17)
target_compile_options(test_mt5_vs_cpp_comparison PRIVATE -O3)
target_link_libraries(test_mt5_vs_cpp_comparison PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_mt5_vs_cpp_comparison PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "MT5 vs C++ comparison test configured: test_mt5_vs_cpp_comparison")
