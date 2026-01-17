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
