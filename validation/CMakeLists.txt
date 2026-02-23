# Validation Test Suite CMake Configuration

cmake_minimum_required(VERSION 3.10)

# Macro to add a test executable only if the source file exists
# This prevents CMake errors when test files are removed or not yet created
macro(add_test_if_exists target_name source_file)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${source_file}")
        add_executable(${target_name} ${source_file})
        target_include_directories(${target_name} PRIVATE ../include)
        target_compile_features(${target_name} PRIVATE cxx_std_17)
        if(MSVC)
            target_compile_options(${target_name} PRIVATE /W4 /O2)
        else()
            target_compile_options(${target_name} PRIVATE -Wall -Wextra -O3 -mavx512f -mavx512dq -mavx2 -mfma)
        endif()
        set_target_properties(${target_name} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
        )
        message(STATUS "Test configured: ${target_name}")
    else()
        message(STATUS "Skipping ${target_name} (source file not found: ${source_file})")
    endif()
endmacro()

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
    target_compile_options(test_fill_up PRIVATE /W4 /O2 /arch:AVX512)
else()
    target_compile_options(test_fill_up PRIVATE -Wall -Wextra -O3 -mavx512f -mavx512dq -mavx2 -mfma)
endif()

set_target_properties(test_fill_up PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
)

message(STATUS "Fill-Up test configured: test_fill_up")

# test_v5_crash_focus - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_v5_crash_focus.cpp")
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

endif()
# test_v5_exits - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_v5_exits.cpp")
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

endif()
# test_v6_combined - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_v6_combined.cpp")
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

endif()
# test_v6_quick - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_v6_quick.cpp")
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

endif()
# test_market_neutral - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_market_neutral.cpp")
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

endif()
# test_market_neutral_simple - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_market_neutral_simple.cpp")
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

endif()
# test_v7_param_sweep - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_v7_param_sweep.cpp")
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

endif()
# test_bb_width_filter - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_bb_width_filter.cpp")
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

endif()
# test_hybrid_v5_v7 - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_hybrid_v5_v7.cpp")
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

endif()
# test_v7_session_combined - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_v7_session_combined.cpp")
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

endif()
# test_rsi_filter - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_rsi_filter.cpp")
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

endif()
# test_adaptive_sizing - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_adaptive_sizing.cpp")
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

endif()
# test_v8_validation - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_v8_validation.cpp")
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

endif()
# test_spacing_sweep - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_spacing_sweep.cpp")
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

endif()
# test_anti_martingale - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_anti_martingale.cpp")
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

endif()
# test_drawdown_analysis - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_drawdown_analysis.cpp")
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

endif()
# test_mean_reversion_indicator - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_mean_reversion_indicator.cpp")
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

endif()
# test_v10_validation - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_v10_validation.cpp")
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

endif()
# test_early_exit - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_early_exit.cpp")
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

endif()
# test_dynamic_tp - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_dynamic_tp.cpp")
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

endif()
# test_trade_analysis - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_trade_analysis.cpp")
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

endif()
# test_correlation_filter - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_correlation_filter.cpp")
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

endif()
# test_d8_50 - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_d8_50.cpp")
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

endif()
# test_market_oscillation - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_market_oscillation.cpp")
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

endif()
# test_fillup_hedged - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_fillup_hedged.cpp")
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

endif()
# test_oscillation_modes - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_oscillation_modes.cpp")
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

endif()
# test_adaptive_spacing_sweep - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_adaptive_spacing_sweep.cpp")
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

endif()
# test_adaptive_single - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_adaptive_single.cpp")
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

endif()
# test_risk_metrics - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_risk_metrics.cpp")
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

endif()
# test_stress_scenarios - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_stress_scenarios.cpp")
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

endif()
# test_nas100_oscillation - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_nas100_oscillation.cpp")
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

endif()
# test_out_of_sample_2024 - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_out_of_sample_2024.cpp")
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

endif()
# test_adaptive_lookback - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_adaptive_lookback.cpp")
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

endif()
# test_momentum_reversal_sweep - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_momentum_reversal_sweep.cpp")
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

endif()
# test_cycle_surfer - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_cycle_surfer.cpp")
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

endif()
# test_price_oscillation_relation - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_price_oscillation_relation.cpp")
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

endif()
# test_improvements_combined - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_improvements_combined.cpp")
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

endif()
# test_fillup_osc_improvements - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_fillup_osc_improvements.cpp")
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

endif()
# test_h1h2_validation - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_h1h2_validation.cpp")
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

endif()
# test_parameter_stability - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_parameter_stability.cpp")
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

endif()
# test_risk_management - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_risk_management.cpp")
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

endif()
# test_multi_year - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_multi_year.cpp")
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

endif()
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

# test_oscillation_sweep - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_oscillation_sweep.cpp")
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

endif()
# test_frequency_analysis - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_frequency_analysis.cpp")
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

endif()
# test_kapitza - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_kapitza.cpp")
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

endif()
# test_kapitza_tuning - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_kapitza_tuning.cpp")
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

endif()
# test_attribution_analysis - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_attribution_analysis.cpp")
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

endif()
# test_spacing_investigation - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_spacing_investigation.cpp")
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

endif()
# test_trend_adaptive - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_trend_adaptive.cpp")
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

endif()
# test_trend_lookback_sweep - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_trend_lookback_sweep.cpp")
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

endif()
# test_trend_lookback - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_trend_lookback.cpp")
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

endif()
# test_trend_calibration - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_trend_calibration.cpp")
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

endif()
# test_self_calibrating - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_self_calibrating.cpp")
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

endif()
# test_spacing_sweep_simple - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_spacing_sweep_simple.cpp")
add_executable(test_spacing_sweep_simple
    test_spacing_sweep_simple.cpp)
target_include_directories(test_spacing_sweep_simple PRIVATE ../include)
target_compile_features(test_spacing_sweep_simple PRIVATE cxx_std_17)
target_compile_options(test_spacing_sweep_simple PRIVATE -O3)
target_link_libraries(test_spacing_sweep_simple PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_spacing_sweep_simple PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")


endif()
# test_spacing_by_period - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_spacing_by_period.cpp")
add_executable(test_spacing_by_period
    test_spacing_by_period.cpp)
target_include_directories(test_spacing_by_period PRIVATE ../include)
target_compile_features(test_spacing_by_period PRIVATE cxx_std_17)
target_compile_options(test_spacing_by_period PRIVATE -O3)
target_link_libraries(test_spacing_by_period PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_spacing_by_period PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")


endif()
# test_spacing_2024 - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_spacing_2024.cpp")
add_executable(test_spacing_2024
    test_spacing_2024.cpp)
target_include_directories(test_spacing_2024 PRIVATE ../include)
target_compile_features(test_spacing_2024 PRIVATE cxx_std_17)
target_compile_options(test_spacing_2024 PRIVATE -O3)
target_link_libraries(test_spacing_2024 PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_spacing_2024 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")


endif()
# test_oscillation_analysis - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_oscillation_analysis.cpp")
add_executable(test_oscillation_analysis
    test_oscillation_analysis.cpp)
target_include_directories(test_oscillation_analysis PRIVATE ../include)
target_compile_features(test_oscillation_analysis PRIVATE cxx_std_17)
target_compile_options(test_oscillation_analysis PRIVATE -O3)
target_link_libraries(test_oscillation_analysis PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_oscillation_analysis PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")


endif()
# test_trend_direction - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_trend_direction.cpp")
add_executable(test_trend_direction
    test_trend_direction.cpp)
target_include_directories(test_trend_direction PRIVATE ../include)
target_compile_features(test_trend_direction PRIVATE cxx_std_17)
target_compile_options(test_trend_direction PRIVATE -O3)
target_link_libraries(test_trend_direction PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_trend_direction PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")


endif()
# test_bounce_analysis - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_bounce_analysis.cpp")
add_executable(test_bounce_analysis
    test_bounce_analysis.cpp)
target_include_directories(test_bounce_analysis PRIVATE ../include)
target_compile_features(test_bounce_analysis PRIVATE cxx_std_17)
target_compile_options(test_bounce_analysis PRIVATE -O3)
target_link_libraries(test_bounce_analysis PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_bounce_analysis PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")


endif()
# test_trailing_stop - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_trailing_stop.cpp")
add_executable(test_trailing_stop
    test_trailing_stop.cpp)
target_include_directories(test_trailing_stop PRIVATE ../include)
target_compile_features(test_trailing_stop PRIVATE cxx_std_17)
target_compile_options(test_trailing_stop PRIVATE -O3)
target_link_libraries(test_trailing_stop PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_trailing_stop PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")


endif()
# test_trailing_v2 - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_trailing_v2.cpp")
add_executable(test_trailing_v2
    test_trailing_v2.cpp)
target_include_directories(test_trailing_v2 PRIVATE ../include)
target_compile_features(test_trailing_v2 PRIVATE cxx_std_17)
target_compile_options(test_trailing_v2 PRIVATE -O3)
target_link_libraries(test_trailing_v2 PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_trailing_v2 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")


endif()
# test_trailing_approaches - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_trailing_approaches.cpp")
add_executable(test_trailing_approaches
    test_trailing_approaches.cpp)
target_include_directories(test_trailing_approaches PRIVATE ../include)
target_compile_features(test_trailing_approaches PRIVATE cxx_std_17)
target_compile_options(test_trailing_approaches PRIVATE -O3)
target_link_libraries(test_trailing_approaches PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_trailing_approaches PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")


endif()
# test_trailing_sweep - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_trailing_sweep.cpp")
add_executable(test_trailing_sweep
    test_trailing_sweep.cpp)
target_include_directories(test_trailing_sweep PRIVATE ../include)
target_compile_features(test_trailing_sweep PRIVATE cxx_std_17)
target_compile_options(test_trailing_sweep PRIVATE -O3)
target_link_libraries(test_trailing_sweep PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_trailing_sweep PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")


endif()
# test_trailing_full_sweep - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_trailing_full_sweep.cpp")
add_executable(test_trailing_full_sweep
    test_trailing_full_sweep.cpp)
target_include_directories(test_trailing_full_sweep PRIVATE ../include)
target_compile_features(test_trailing_full_sweep PRIVATE cxx_std_17)
target_compile_options(test_trailing_full_sweep PRIVATE -O3)
target_link_libraries(test_trailing_full_sweep PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_trailing_full_sweep PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")


endif()
# test_trailing_5d_sweep - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_trailing_5d_sweep.cpp")
add_executable(test_trailing_5d_sweep
    test_trailing_5d_sweep.cpp)
target_include_directories(test_trailing_5d_sweep PRIVATE ../include)
target_compile_features(test_trailing_5d_sweep PRIVATE cxx_std_17)
target_compile_options(test_trailing_5d_sweep PRIVATE -O3)
target_link_libraries(test_trailing_5d_sweep PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_trailing_5d_sweep PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")

endif()
# test_trailing_5d_parallel - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_trailing_5d_parallel.cpp")
# Multi-threaded parallel version
add_executable(test_trailing_5d_parallel
    test_trailing_5d_parallel.cpp)
target_include_directories(test_trailing_5d_parallel PRIVATE ../include)
target_compile_features(test_trailing_5d_parallel PRIVATE cxx_std_17)
target_compile_options(test_trailing_5d_parallel PRIVATE -O3)
target_link_libraries(test_trailing_5d_parallel PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_trailing_5d_parallel PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")


endif()
# test_return_attribution - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_return_attribution.cpp")
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

endif()
# test_kill_switch - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_kill_switch.cpp")
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

endif()
# test_kill_switch_real_data - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_kill_switch_real_data.cpp")
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

endif()
# test_7_strategies - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_7_strategies.cpp")
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

endif()
# test_xagusd_fillup - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_xagusd_fillup.cpp")
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

endif()
# test_dd_reduction - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_dd_reduction.cpp")
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

endif()
# test_rubberband_tp - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_rubberband_tp.cpp")
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

endif()
# test_xagusd_spacing_sweep - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_xagusd_spacing_sweep.cpp")
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

endif()
# test_xagusd_sweep - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_xagusd_sweep.cpp")
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

endif()
# test_xagusd_pct_spacing - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_xagusd_pct_spacing.cpp")
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

endif()
# test_xauusd_ath_bands - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_xauusd_ath_bands.cpp")
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

endif()
# test_xagusd_ath_bands - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_xagusd_ath_bands.cpp")
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

endif()
# test_xagusd_diagnostic - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_xagusd_diagnostic.cpp")
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

endif()
# test_xagusd_fixed - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_xagusd_fixed.cpp")
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

endif()
# test_silver_precise - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_silver_precise.cpp")
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

endif()
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

# test_preset_overfitting - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_preset_overfitting.cpp")
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

endif()
# test_xagusd_consistency - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_xagusd_consistency.cpp")
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

endif()
# test_xagusd_compound_cap - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_xagusd_compound_cap.cpp")
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

endif()
# test_xagusd_2024_optimize - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_xagusd_2024_optimize.cpp")
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

endif()
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

# test_xauusd_deep_sweep - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_xauusd_deep_sweep.cpp")
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

endif()
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
# test_small_spacing - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_small_spacing.cpp")
add_executable(test_small_spacing
    test_small_spacing.cpp)
target_include_directories(test_small_spacing PRIVATE ../include)
target_compile_features(test_small_spacing PRIVATE cxx_std_17)
target_compile_options(test_small_spacing PRIVATE -O3)
target_link_libraries(test_small_spacing PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_small_spacing PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")

endif()
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

# test_entropy_export_v2 - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_entropy_export_v2.cpp")
# Entropy Export V2 - Time and Loss based loser cutting (Dissipative Structures)
add_executable(test_entropy_export_v2
    test_entropy_export_v2.cpp)
target_include_directories(test_entropy_export_v2 PRIVATE ../include)
target_compile_features(test_entropy_export_v2 PRIVATE cxx_std_17)
target_compile_options(test_entropy_export_v2 PRIVATE -O3)
target_link_libraries(test_entropy_export_v2 PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_entropy_export_v2 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Entropy export v2 test configured: test_entropy_export_v2")

endif()
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

# test_barbell_sizing - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_barbell_sizing.cpp")
# Barbell Sizing Strategy - Taleb's Asymmetric Sizing (Ju Concept #1)
add_executable(test_barbell_sizing
    test_barbell_sizing.cpp)
target_include_directories(test_barbell_sizing PRIVATE ../include)
target_compile_features(test_barbell_sizing PRIVATE cxx_std_17)
target_compile_options(test_barbell_sizing PRIVATE -O3)
target_link_libraries(test_barbell_sizing PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_barbell_sizing PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Barbell sizing strategy test configured: test_barbell_sizing")


endif()
# test_via_negativa - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_via_negativa.cpp")
# Via Negativa Strategy - Taleb's Improvement by Removal (Ju Concept #3)
add_executable(test_via_negativa
    test_via_negativa.cpp)
target_include_directories(test_via_negativa PRIVATE ../include)
target_compile_features(test_via_negativa PRIVATE cxx_std_17)
target_compile_options(test_via_negativa PRIVATE -O3)
target_link_libraries(test_via_negativa PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_via_negativa PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Via Negativa strategy test configured: test_via_negativa")


endif()
# test_reflexivity - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_reflexivity.cpp")
# Reflexivity Strategy - Soros's Feedback Loops (Ju Concept #4)
add_executable(test_reflexivity
    test_reflexivity.cpp)
target_include_directories(test_reflexivity PRIVATE ../include)
target_compile_features(test_reflexivity PRIVATE cxx_std_17)
target_compile_options(test_reflexivity PRIVATE -O3)
target_link_libraries(test_reflexivity PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_reflexivity PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Reflexivity strategy test configured: test_reflexivity")


endif()
# test_wuwei - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_wuwei.cpp")
# Wu Wei Strategy - Effortless Action (Ju Concept #5)
add_executable(test_wuwei
    test_wuwei.cpp)
target_include_directories(test_wuwei PRIVATE ../include)
target_compile_features(test_wuwei PRIVATE cxx_std_17)
target_compile_options(test_wuwei PRIVATE -O3)
target_link_libraries(test_wuwei PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_wuwei PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Wu Wei strategy test configured: test_wuwei")


endif()
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

# test_velocity_filter_diagnosis - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_velocity_filter_diagnosis.cpp")
# Velocity filter diagnosis test
add_executable(test_velocity_filter_diagnosis
    test_velocity_filter_diagnosis.cpp)
target_include_directories(test_velocity_filter_diagnosis PRIVATE ../include)
target_compile_features(test_velocity_filter_diagnosis PRIVATE cxx_std_17)
target_compile_options(test_velocity_filter_diagnosis PRIVATE -O3)
target_link_libraries(test_velocity_filter_diagnosis PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_velocity_filter_diagnosis PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Velocity filter diagnosis test configured: test_velocity_filter_diagnosis")

endif()
# test_velocity_diagnosis_extended - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_velocity_diagnosis_extended.cpp")
# Velocity filter extended diagnosis
add_executable(test_velocity_diagnosis_extended
    test_velocity_diagnosis_extended.cpp)
target_include_directories(test_velocity_diagnosis_extended PRIVATE ../include)
target_compile_features(test_velocity_diagnosis_extended PRIVATE cxx_std_17)
target_compile_options(test_velocity_diagnosis_extended PRIVATE -O3)
target_link_libraries(test_velocity_diagnosis_extended PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_velocity_diagnosis_extended PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Velocity diagnosis extended test configured: test_velocity_diagnosis_extended")

endif()
# test_tick_characteristics - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_tick_characteristics.cpp")
# Tick characteristics analysis
add_executable(test_tick_characteristics
    test_tick_characteristics.cpp)
target_include_directories(test_tick_characteristics PRIVATE ../include)
target_compile_features(test_tick_characteristics PRIVATE cxx_std_17)
target_compile_options(test_tick_characteristics PRIVATE -O3)
target_link_libraries(test_tick_characteristics PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_tick_characteristics PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Tick characteristics test configured: test_tick_characteristics")

endif()
# test_mt5_simulation - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_mt5_simulation.cpp")
# MT5 simulation test (wider spread, tick subsampling)
add_executable(test_mt5_simulation
    test_mt5_simulation.cpp)
target_include_directories(test_mt5_simulation PRIVATE ../include)
target_compile_features(test_mt5_simulation PRIVATE cxx_std_17)
target_compile_options(test_mt5_simulation PRIVATE -O3)
target_link_libraries(test_mt5_simulation PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_mt5_simulation PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "MT5 simulation test configured: test_mt5_simulation")

endif()
# Combined Ju detailed stats test
add_executable(test_combined_ju_detailed_stats test_combined_ju_detailed_stats.cpp)
target_include_directories(test_combined_ju_detailed_stats PRIVATE ../include)
target_compile_features(test_combined_ju_detailed_stats PRIVATE cxx_std_17)
target_compile_options(test_combined_ju_detailed_stats PRIVATE -O3)
target_link_libraries(test_combined_ju_detailed_stats PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_combined_ju_detailed_stats PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Combined Ju detailed stats test configured: test_combined_ju_detailed_stats")

# Forced entry validation test (2024 vs 2025 regime independence)
add_executable(test_forced_entry_validation test_forced_entry_validation.cpp)
target_include_directories(test_forced_entry_validation PRIVATE ../include)
target_compile_features(test_forced_entry_validation PRIVATE cxx_std_17)
target_compile_options(test_forced_entry_validation PRIVATE -O3)
target_link_libraries(test_forced_entry_validation PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_forced_entry_validation PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Forced entry validation test configured: test_forced_entry_validation")

# Forced Entry Parallel (shared tick data, multi-threaded)
add_executable(test_forced_entry_parallel test_forced_entry_parallel.cpp)
target_include_directories(test_forced_entry_parallel PRIVATE ../include)
target_compile_features(test_forced_entry_parallel PRIVATE cxx_std_17)
target_compile_options(test_forced_entry_parallel PRIVATE -O3)
target_link_libraries(test_forced_entry_parallel PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_forced_entry_parallel PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Forced entry parallel test configured: test_forced_entry_parallel")

# Forced Entry Features Comparison (survive%, modes, years)
add_executable(test_forced_entry_features test_forced_entry_features.cpp)
target_include_directories(test_forced_entry_features PRIVATE ../include)
target_compile_features(test_forced_entry_features PRIVATE cxx_std_17)
target_compile_options(test_forced_entry_features PRIVATE -O3)
target_link_libraries(test_forced_entry_features PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_forced_entry_features PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Forced entry features test configured: test_forced_entry_features")

# Test V4, V5, and CombinedJu with forced entry
add_executable(test_v4_v5_combinedju_forced test_v4_v5_combinedju_forced.cpp)
target_include_directories(test_v4_v5_combinedju_forced PRIVATE ../include)
target_compile_features(test_v4_v5_combinedju_forced PRIVATE cxx_std_17)
target_compile_options(test_v4_v5_combinedju_forced PRIVATE -O3)
target_link_libraries(test_v4_v5_combinedju_forced PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_v4_v5_combinedju_forced PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "V4/V5/CombinedJu forced entry test configured: test_v4_v5_combinedju_forced")

# test_combinedju_sweep - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_combinedju_sweep.cpp")
# CombinedJu parameter sweep
add_executable(test_combinedju_sweep
    test_combinedju_sweep.cpp)
target_include_directories(test_combinedju_sweep PRIVATE ../include)
target_compile_features(test_combinedju_sweep PRIVATE cxx_std_17)
target_compile_options(test_combinedju_sweep PRIVATE -O3)
target_link_libraries(test_combinedju_sweep PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_combinedju_sweep PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "CombinedJu sweep test configured: test_combinedju_sweep")

endif()
# test_forced_entry_crash - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_forced_entry_crash.cpp")
add_executable(test_forced_entry_crash
    test_forced_entry_crash.cpp)
target_include_directories(test_forced_entry_crash PRIVATE ../include)
target_compile_features(test_forced_entry_crash PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_forced_entry_crash PRIVATE /W4 /O2)
else()
    target_compile_options(test_forced_entry_crash PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_forced_entry_crash PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_forced_entry_crash PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")


endif()
# test_force_entry_mitigation - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_force_entry_mitigation.cpp")
add_executable(test_force_entry_mitigation
    test_force_entry_mitigation.cpp)
target_include_directories(test_force_entry_mitigation PRIVATE ../include)
target_compile_features(test_force_entry_mitigation PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_force_entry_mitigation PRIVATE /W4 /O2)
else()
    target_compile_options(test_force_entry_mitigation PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_force_entry_mitigation PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_force_entry_mitigation PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")

endif()
# test_combinedju_noforce_sweep - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_combinedju_noforce_sweep.cpp")
# CombinedJu sweep with force=false (crash-safe)
add_executable(test_combinedju_noforce_sweep
    test_combinedju_noforce_sweep.cpp)
target_include_directories(test_combinedju_noforce_sweep PRIVATE ../include)
target_compile_features(test_combinedju_noforce_sweep PRIVATE cxx_std_17)
target_compile_options(test_combinedju_noforce_sweep PRIVATE -O3)
target_link_libraries(test_combinedju_noforce_sweep PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_combinedju_noforce_sweep PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "CombinedJu no-force sweep test configured: test_combinedju_noforce_sweep")

endif()
# test_combinedju_thorough_sweep - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_combinedju_thorough_sweep.cpp")
# CombinedJu Thorough Sweep (all threshold_pos and threshold_mult combinations)
add_executable(test_combinedju_thorough_sweep
    test_combinedju_thorough_sweep.cpp)
target_include_directories(test_combinedju_thorough_sweep PRIVATE ../include)
target_compile_features(test_combinedju_thorough_sweep PRIVATE cxx_std_17)
target_compile_options(test_combinedju_thorough_sweep PRIVATE -O3)
target_link_libraries(test_combinedju_thorough_sweep PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_combinedju_thorough_sweep PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "CombinedJu thorough sweep test configured: test_combinedju_thorough_sweep")

endif()
# test_combinedju_oct_start - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_combinedju_oct_start.cpp")
add_executable(test_combinedju_oct_start
    test_combinedju_oct_start.cpp)
target_include_directories(test_combinedju_oct_start PRIVATE ../include)
target_compile_features(test_combinedju_oct_start PRIVATE cxx_std_17)
target_compile_options(test_combinedju_oct_start PRIVATE -O3)
target_link_libraries(test_combinedju_oct_start PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_combinedju_oct_start PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "CombinedJu October start test configured: test_combinedju_oct_start")

endif()
# test_robust_full_year - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_robust_full_year.cpp")
add_executable(test_robust_full_year
    test_robust_full_year.cpp)
target_include_directories(test_robust_full_year PRIVATE ../include)
target_compile_features(test_robust_full_year PRIVATE cxx_std_17)
target_compile_options(test_robust_full_year PRIVATE -O3)
target_link_libraries(test_robust_full_year PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_robust_full_year PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Robust presets full year test configured: test_robust_full_year")

endif()
# test_instrument_investigation - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_instrument_investigation.cpp")
# Instrument Investigation Test
add_executable(test_instrument_investigation
    test_instrument_investigation.cpp)
target_include_directories(test_instrument_investigation PRIVATE ../include)
target_compile_features(test_instrument_investigation PRIVATE cxx_std_17)
target_compile_options(test_instrument_investigation PRIVATE -O3)
target_link_libraries(test_instrument_investigation PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_instrument_investigation PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")

endif()
# test_new_instruments - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_new_instruments.cpp")
# New Instruments Test (Platinum, Palladium, Ethereum)
add_executable(test_new_instruments
    test_new_instruments.cpp)
target_include_directories(test_new_instruments PRIVATE ../include)
target_compile_features(test_new_instruments PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_new_instruments PRIVATE /W4 /O2)
else()
    target_compile_options(test_new_instruments PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_new_instruments PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_new_instruments PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "New instruments test configured: test_new_instruments")

endif()
# test_platinum_multiyear - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_platinum_multiyear.cpp")
# Platinum Multi-Year Validation Test
add_executable(test_platinum_multiyear
    test_platinum_multiyear.cpp)
target_include_directories(test_platinum_multiyear PRIVATE ../include)
target_compile_features(test_platinum_multiyear PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_platinum_multiyear PRIVATE /W4 /O2)
else()
    target_compile_options(test_platinum_multiyear PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_platinum_multiyear PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_platinum_multiyear PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Platinum multiyear test configured: test_platinum_multiyear")

endif()
# test_silver_multiyear - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_silver_multiyear.cpp")
# Silver Multi-Year Validation Test
add_executable(test_silver_multiyear
    test_silver_multiyear.cpp)
target_include_directories(test_silver_multiyear PRIVATE ../include)
target_compile_features(test_silver_multiyear PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_silver_multiyear PRIVATE /W4 /O2)
else()
    target_compile_options(test_silver_multiyear PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_silver_multiyear PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_silver_multiyear PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Silver multiyear test configured: test_silver_multiyear")

endif()
# test_silver_strategies - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_silver_strategies.cpp")
# Silver Strategy Comparison Test (V4 vs V5 vs CombinedJu)
add_executable(test_silver_strategies
    test_silver_strategies.cpp)
target_include_directories(test_silver_strategies PRIVATE ../include)
target_compile_features(test_silver_strategies PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_silver_strategies PRIVATE /W4 /O2)
else()
    target_compile_options(test_silver_strategies PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_silver_strategies PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_silver_strategies PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Silver strategies test configured: test_silver_strategies")

endif()
# test_minimal_strategy - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_minimal_strategy.cpp")
# Minimal Strategy Test
add_executable(test_minimal_strategy
    test_minimal_strategy.cpp)
target_include_directories(test_minimal_strategy PRIVATE ../include)
target_compile_features(test_minimal_strategy PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_minimal_strategy PRIVATE /W4 /O2)
else()
    target_compile_options(test_minimal_strategy PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_minimal_strategy PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_minimal_strategy PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Minimal strategy test configured")

endif()
# test_fixed_regime_robustness - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_fixed_regime_robustness.cpp")
# Fixed vs Regime Robustness Test
add_executable(test_fixed_regime_robustness
    test_fixed_regime_robustness.cpp)
target_include_directories(test_fixed_regime_robustness PRIVATE ../include)
target_compile_features(test_fixed_regime_robustness PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_fixed_regime_robustness PRIVATE /W4 /O2)
else()
    target_compile_options(test_fixed_regime_robustness PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_fixed_regime_robustness PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_fixed_regime_robustness PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Fixed regime robustness test configured")

endif()
# test_regime_parallel - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_regime_parallel.cpp")
# Parallel Regime Robustness Test (FIXED vs ADAPTIVE across years)
add_executable(test_regime_parallel
    test_regime_parallel.cpp)
target_include_directories(test_regime_parallel PRIVATE ../include)
target_compile_features(test_regime_parallel PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_regime_parallel PRIVATE /W4 /O2)
else()
    target_compile_options(test_regime_parallel PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_regime_parallel PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_regime_parallel PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")

endif()
# test_v4_survive_sweep - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_v4_survive_sweep.cpp")
# V4 Survive Percentage Sweep
add_executable(test_v4_survive_sweep
    test_v4_survive_sweep.cpp)
target_include_directories(test_v4_survive_sweep PRIVATE ../include)
target_compile_features(test_v4_survive_sweep PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_v4_survive_sweep PRIVATE /W4 /O2)
else()
    target_compile_options(test_v4_survive_sweep PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_v4_survive_sweep PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_v4_survive_sweep PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")

endif()
# test_extraction_model - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_extraction_model.cpp")
# Profit Extraction Model Test
add_executable(test_extraction_model
    test_extraction_model.cpp)
target_include_directories(test_extraction_model PRIVATE ../include)
target_compile_features(test_extraction_model PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_extraction_model PRIVATE /W4 /O2)
else()
    target_compile_options(test_extraction_model PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_extraction_model PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_extraction_model PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")

endif()
# test_continuous_extraction - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_continuous_extraction.cpp")
# Continuous Extraction Model Test
add_executable(test_continuous_extraction
    test_continuous_extraction.cpp)
target_include_directories(test_continuous_extraction PRIVATE ../include)
target_compile_features(test_continuous_extraction PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_continuous_extraction PRIVATE /W4 /O2)
else()
    target_compile_options(test_continuous_extraction PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_continuous_extraction PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_continuous_extraction PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")

endif()
# test_scaled_extraction - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_scaled_extraction.cpp")
# Scaled Extraction Model Test
add_executable(test_scaled_extraction
    test_scaled_extraction.cpp)
target_include_directories(test_scaled_extraction PRIVATE ../include)
target_compile_features(test_scaled_extraction PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_scaled_extraction PRIVATE /W4 /O2)
else()
    target_compile_options(test_scaled_extraction PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_scaled_extraction PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_scaled_extraction PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")

endif()
# test_hybrid_fillup_extended - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_hybrid_fillup_extended.cpp")
# Hybrid FillUp Extended Test
add_executable(test_hybrid_fillup_extended
    test_hybrid_fillup_extended.cpp)
target_include_directories(test_hybrid_fillup_extended PRIVATE ../include)
target_compile_features(test_hybrid_fillup_extended PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_hybrid_fillup_extended PRIVATE /W4 /O2)
else()
    target_compile_options(test_hybrid_fillup_extended PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_hybrid_fillup_extended PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_hybrid_fillup_extended PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")

endif()
# test_dual_allocation_sweep - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_dual_allocation_sweep.cpp")
# Dual Allocation Strategy Sweep
add_executable(test_dual_allocation_sweep
    test_dual_allocation_sweep.cpp)
target_include_directories(test_dual_allocation_sweep PRIVATE ../include)
target_compile_features(test_dual_allocation_sweep PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_dual_allocation_sweep PRIVATE /W4 /O2)
else()
    target_compile_options(test_dual_allocation_sweep PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_dual_allocation_sweep PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_dual_allocation_sweep PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")

endif()
# test_triple_hybrid - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_triple_hybrid.cpp")
# Test: Triple Hybrid Strategy
add_executable(test_triple_hybrid
    test_triple_hybrid.cpp)
target_include_directories(test_triple_hybrid PRIVATE ../include)
target_compile_features(test_triple_hybrid PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_triple_hybrid PRIVATE /W4 /O2)
else()
    target_compile_options(test_triple_hybrid PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_triple_hybrid PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_triple_hybrid PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")

endif()
# test_upwhileup_upwhiledown - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_upwhileup_upwhiledown.cpp")
# Test: Up While Up + Up While Down
add_executable(test_upwhileup_upwhiledown
    test_upwhileup_upwhiledown.cpp)
target_include_directories(test_upwhileup_upwhiledown PRIVATE ../include)
target_compile_features(test_upwhileup_upwhiledown PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_upwhileup_upwhiledown PRIVATE /W4 /O2)
else()
    target_compile_options(test_upwhileup_upwhiledown PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_upwhileup_upwhiledown PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_upwhileup_upwhiledown PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")

endif()
# test_margin_allocation - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_margin_allocation.cpp")
# Test: Margin Allocation Diagnostic
add_executable(test_margin_allocation
    test_margin_allocation.cpp)
target_include_directories(test_margin_allocation PRIVATE ../include)
target_compile_features(test_margin_allocation PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_margin_allocation PRIVATE /W4 /O2)
else()
    target_compile_options(test_margin_allocation PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_margin_allocation PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_margin_allocation PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")

endif()
# test_harmony - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_harmony.cpp")
# Test: Harmony Strategy (Up While Up + Up While Down combined)
add_executable(test_harmony
    test_harmony.cpp)
target_include_directories(test_harmony PRIVATE ../include)
target_compile_features(test_harmony PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_harmony PRIVATE /W4 /O2)
else()
    target_compile_options(test_harmony PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_harmony PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_harmony PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")

endif()
# test_harmony_parallel - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_harmony_parallel.cpp")
add_executable(test_harmony_parallel
    test_harmony_parallel.cpp)
target_include_directories(test_harmony_parallel PRIVATE ../include)
target_compile_features(test_harmony_parallel PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_harmony_parallel PRIVATE /W4 /O2)
else()
    target_compile_options(test_harmony_parallel PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_harmony_parallel PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_harmony_parallel PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")

endif()
# test_harmony_comparison - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_harmony_comparison.cpp")
add_executable(test_harmony_comparison
    test_harmony_comparison.cpp)
target_include_directories(test_harmony_comparison PRIVATE ../include)
target_compile_features(test_harmony_comparison PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_harmony_comparison PRIVATE /W4 /O2)
else()
    target_compile_options(test_harmony_comparison PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_harmony_comparison PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_harmony_comparison PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")

endif()
# test_harmony_mt5_accurate - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_harmony_mt5_accurate.cpp")
add_executable(test_harmony_mt5_accurate
    test_harmony_mt5_accurate.cpp)
target_include_directories(test_harmony_mt5_accurate PRIVATE ../include)
target_compile_features(test_harmony_mt5_accurate PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_harmony_mt5_accurate PRIVATE /W4 /O2)
else()
    target_compile_options(test_harmony_mt5_accurate PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_harmony_mt5_accurate PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_harmony_mt5_accurate PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")

endif()
# test_fillup_with_upwhileup - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_fillup_with_upwhileup.cpp")
add_executable(test_fillup_with_upwhileup
    test_fillup_with_upwhileup.cpp)
target_include_directories(test_fillup_with_upwhileup PRIVATE ../include)
target_compile_features(test_fillup_with_upwhileup PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_fillup_with_upwhileup PRIVATE /W4 /O2)
else()
    target_compile_options(test_fillup_with_upwhileup PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_fillup_with_upwhileup PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_fillup_with_upwhileup PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")

endif()
# test_harmony_engine - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_harmony_engine.cpp")
add_executable(test_harmony_engine
    test_harmony_engine.cpp)
target_include_directories(test_harmony_engine PRIVATE ../include)
target_compile_features(test_harmony_engine PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_harmony_engine PRIVATE /W4 /O2)
else()
    target_compile_options(test_harmony_engine PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_harmony_engine PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_harmony_engine PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")

endif()
# test_bidirectional_momentum - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_bidirectional_momentum.cpp")
# Bidirectional Momentum Strategy Test (combined up-while-up and up-while-down)
add_executable(test_bidirectional_momentum
    test_bidirectional_momentum.cpp)
target_include_directories(test_bidirectional_momentum PRIVATE ../include)
target_compile_features(test_bidirectional_momentum PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_bidirectional_momentum PRIVATE /W4 /O2)
else()
    target_compile_options(test_bidirectional_momentum PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_bidirectional_momentum PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_bidirectional_momentum PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Bidirectional momentum test configured: test_bidirectional_momentum")

endif()
# test_parallel_dual - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_parallel_dual.cpp")
# Parallel Dual Strategy Test (grid + momentum running simultaneously)
add_executable(test_parallel_dual
    test_parallel_dual.cpp)
target_include_directories(test_parallel_dual PRIVATE ../include)
target_compile_features(test_parallel_dual PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_parallel_dual PRIVATE /W4 /O2)
else()
    target_compile_options(test_parallel_dual PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_parallel_dual PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_parallel_dual PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Parallel dual strategy test configured: test_parallel_dual")

endif()
# test_parallel_dual_quick - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_parallel_dual_quick.cpp")
# Parallel Dual Strategy Quick Validation Test
add_executable(test_parallel_dual_quick
    test_parallel_dual_quick.cpp)
target_include_directories(test_parallel_dual_quick PRIVATE ../include)
target_compile_features(test_parallel_dual_quick PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_parallel_dual_quick PRIVATE /W4 /O2)
else()
    target_compile_options(test_parallel_dual_quick PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_parallel_dual_quick PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_parallel_dual_quick PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Parallel dual quick test configured: test_parallel_dual_quick")

endif()
# test_parallel_dual_comparison - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_parallel_dual_comparison.cpp")
# Parallel Dual Strategy Comparison Test (NEW vs OLD)
add_executable(test_parallel_dual_comparison
    test_parallel_dual_comparison.cpp)
target_include_directories(test_parallel_dual_comparison PRIVATE ../include)
target_compile_features(test_parallel_dual_comparison PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_parallel_dual_comparison PRIVATE /W4 /O2)
else()
    target_compile_options(test_parallel_dual_comparison PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_parallel_dual_comparison PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_parallel_dual_comparison PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Parallel dual comparison test configured: test_parallel_dual_comparison")

endif()
# test_parallel_dual_debug - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_parallel_dual_debug.cpp")
# Parallel Dual Strategy Debug Test
add_executable(test_parallel_dual_debug
    test_parallel_dual_debug.cpp)
target_include_directories(test_parallel_dual_debug PRIVATE ../include)
target_compile_features(test_parallel_dual_debug PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_parallel_dual_debug PRIVATE /W4 /O2)
else()
    target_compile_options(test_parallel_dual_debug PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_parallel_dual_debug PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_parallel_dual_debug PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Parallel dual debug test configured: test_parallel_dual_debug")

endif()
# test_parallel_dual_million - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_parallel_dual_million.cpp")
# Parallel Dual Strategy Million Dollar Test
add_executable(test_parallel_dual_million
    test_parallel_dual_million.cpp)
target_include_directories(test_parallel_dual_million PRIVATE ../include)
target_compile_features(test_parallel_dual_million PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_parallel_dual_million PRIVATE /W4 /O2)
else()
    target_compile_options(test_parallel_dual_million PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_parallel_dual_million PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_parallel_dual_million PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Parallel dual million test configured: test_parallel_dual_million")

endif()
# MT5 Comparison Test
add_executable(test_mt5_comparison test_mt5_comparison.cpp)
target_include_directories(test_mt5_comparison PRIVATE ../include)
target_compile_features(test_mt5_comparison PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_mt5_comparison PRIVATE /W4 /O2)
else()
    target_compile_options(test_mt5_comparison PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_mt5_comparison PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_mt5_comparison PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "MT5 comparison test configured: test_mt5_comparison")

# test_silver_pdual_sweep - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_silver_pdual_sweep.cpp")
# Silver Parallel Dual Sweep
add_executable(test_silver_pdual_sweep
    test_silver_pdual_sweep.cpp)
target_include_directories(test_silver_pdual_sweep PRIVATE ../include)
target_compile_features(test_silver_pdual_sweep PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_silver_pdual_sweep PRIVATE /W4 /O2)
else()
    target_compile_options(test_silver_pdual_sweep PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_silver_pdual_sweep PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_silver_pdual_sweep PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Silver parallel dual sweep configured: test_silver_pdual_sweep")

endif()
# test_silver_pdual_fine - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_silver_pdual_fine.cpp")
# Silver Parallel Dual Fine Optimization
add_executable(test_silver_pdual_fine
    test_silver_pdual_fine.cpp)
target_include_directories(test_silver_pdual_fine PRIVATE ../include)
target_compile_features(test_silver_pdual_fine PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_silver_pdual_fine PRIVATE /W4 /O2)
else()
    target_compile_options(test_silver_pdual_fine PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_silver_pdual_fine PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_silver_pdual_fine PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Silver parallel dual fine optimization configured: test_silver_pdual_fine")

endif()
# test_grid_multi_instrument - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_grid_multi_instrument.cpp")
# Multi-Instrument Grid Test
add_executable(test_grid_multi_instrument
    test_grid_multi_instrument.cpp)
target_include_directories(test_grid_multi_instrument PRIVATE ../include)
target_compile_features(test_grid_multi_instrument PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_grid_multi_instrument PRIVATE /W4 /O2)
else()
    target_compile_options(test_grid_multi_instrument PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_grid_multi_instrument PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_grid_multi_instrument PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Multi-instrument grid test configured: test_grid_multi_instrument")

endif()
# test_grid_multi_parallel - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_grid_multi_parallel.cpp")
# Multi-Instrument Grid Parallel Test
add_executable(test_grid_multi_parallel
    test_grid_multi_parallel.cpp)
target_include_directories(test_grid_multi_parallel PRIVATE ../include)
target_compile_features(test_grid_multi_parallel PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_grid_multi_parallel PRIVATE /W4 /O2)
else()
    target_compile_options(test_grid_multi_parallel PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_grid_multi_parallel PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_grid_multi_parallel PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Multi-instrument grid parallel test configured: test_grid_multi_parallel")

endif()
# test_silver_quick - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../validation/test_silver_quick.cpp")
add_executable(test_silver_quick
    ../validation/test_silver_quick.cpp)
target_include_directories(test_silver_quick PRIVATE ../include)
target_compile_features(test_silver_quick PRIVATE cxx_std_17)


endif()
# test_grid_multi_v2 - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_grid_multi_v2.cpp")
# Multi-Symbol Grid V2 Test
add_executable(test_grid_multi_v2
    test_grid_multi_v2.cpp)
target_include_directories(test_grid_multi_v2 PRIVATE ../include)
target_compile_features(test_grid_multi_v2 PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_grid_multi_v2 PRIVATE /W4 /O2)
else()
    target_compile_options(test_grid_multi_v2 PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_grid_multi_v2 PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_grid_multi_v2 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")

endif()
# test_grid_multi_v3 - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_grid_multi_v3.cpp")
# Multi-Symbol Grid V3 Test
add_executable(test_grid_multi_v3
    test_grid_multi_v3.cpp)
target_include_directories(test_grid_multi_v3 PRIVATE ../include)
target_compile_features(test_grid_multi_v3 PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_grid_multi_v3 PRIVATE /W4 /O2)
else()
    target_compile_options(test_grid_multi_v3 PRIVATE -Wall -Wextra -O3)
    target_link_libraries(test_grid_multi_v3 PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_grid_multi_v3 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")

endif()
# test_grid_debug - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_grid_debug.cpp")
add_executable(test_grid_debug
    test_grid_debug.cpp)
target_include_directories(test_grid_debug PRIVATE ../include)
target_compile_features(test_grid_debug PRIVATE cxx_std_17)
target_compile_options(test_grid_debug PRIVATE -Wall -Wextra -O3)
target_link_libraries(test_grid_debug PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_grid_debug PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")

endif()
# test_grid_multi_optimized - SIMD + std::thread parallel optimized version
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_grid_multi_optimized.cpp")
add_executable(test_grid_multi_optimized
    test_grid_multi_optimized.cpp)
target_include_directories(test_grid_multi_optimized PRIVATE ../include)
target_compile_features(test_grid_multi_optimized PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_grid_multi_optimized PRIVATE /W4 /O2 /arch:AVX512)
else()
    # AVX-512 SIMD + std::thread parallelism
    target_compile_options(test_grid_multi_optimized PRIVATE -Wall -Wextra -O3 -mavx512f -mavx512dq -mavx2 -pthread)
    target_link_libraries(test_grid_multi_optimized PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_grid_multi_optimized PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Multi-instrument grid optimized test configured: test_grid_multi_optimized (AVX-512 + threads)")
endif()

# test_grid_full_period - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_grid_full_period.cpp")
add_executable(test_grid_full_period
    test_grid_full_period.cpp)
target_include_directories(test_grid_full_period PRIVATE ../include)
target_compile_features(test_grid_full_period PRIVATE cxx_std_17)
target_compile_options(test_grid_full_period PRIVATE -Wall -Wextra -O3)
target_link_libraries(test_grid_full_period PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_grid_full_period PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")

endif()
# test_grid_fine_sweep - SKIPPED (source file not found)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_grid_fine_sweep.cpp")
add_executable(test_grid_fine_sweep
    test_grid_fine_sweep.cpp)
target_include_directories(test_grid_fine_sweep PRIVATE ../include)
target_compile_features(test_grid_fine_sweep PRIVATE cxx_std_17)
target_compile_options(test_grid_fine_sweep PRIVATE -Wall -Wextra -O3)
target_link_libraries(test_grid_fine_sweep PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
set_target_properties(test_grid_fine_sweep PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")

endif()

# VIX Strategy Comparison - Spike Fade vs SHORT-Only Grid
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_vix_strategies.cpp")
add_executable(test_vix_strategies
    test_vix_strategies.cpp)
target_include_directories(test_vix_strategies PRIVATE ../include)
target_compile_features(test_vix_strategies PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_vix_strategies PRIVATE /W4 /O2)
else()
    # Note: -Og instead of -O3 due to GCC 15 AVX stack alignment bug with large tick vectors
    target_compile_options(test_vix_strategies PRIVATE -Wall -Wextra -Og -mavx512f -mavx512dq -mavx2 -mfma)
    target_link_libraries(test_vix_strategies PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
    target_link_options(test_vix_strategies PRIVATE -Wl,--stack,67108864)
endif()
set_target_properties(test_vix_strategies PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "VIX strategies test configured: test_vix_strategies")
endif()

# NasdaqUp minimal test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_nasdaq_up_minimal.cpp")
add_executable(test_nasdaq_up_minimal test_nasdaq_up_minimal.cpp)
target_include_directories(test_nasdaq_up_minimal PRIVATE ../include)
target_compile_features(test_nasdaq_up_minimal PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_nasdaq_up_minimal PRIVATE /W4 /O2)
else()
    target_compile_options(test_nasdaq_up_minimal PRIVATE -Wall -Wextra -O3 -mavx2 -mfma)
    target_link_libraries(test_nasdaq_up_minimal PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_nasdaq_up_minimal PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "NasdaqUp minimal test configured: test_nasdaq_up_minimal")
endif()

# NasdaqUp sweep test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_nasdaq_up_sweep.cpp")
add_executable(test_nasdaq_up_sweep test_nasdaq_up_sweep.cpp)
target_include_directories(test_nasdaq_up_sweep PRIVATE ../include)
target_compile_features(test_nasdaq_up_sweep PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_nasdaq_up_sweep PRIVATE /W4 /O2)
else()
    target_compile_options(test_nasdaq_up_sweep PRIVATE -Wall -Wextra -O3 -mavx2 -mfma)
    target_link_libraries(test_nasdaq_up_sweep PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_nasdaq_up_sweep PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "NasdaqUp sweep test configured: test_nasdaq_up_sweep")
endif()

# NasdaqUp NAS100 test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_nasdaq_up_nas100.cpp")
add_executable(test_nasdaq_up_nas100 test_nasdaq_up_nas100.cpp)
target_include_directories(test_nasdaq_up_nas100 PRIVATE ../include)
target_compile_features(test_nasdaq_up_nas100 PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_nasdaq_up_nas100 PRIVATE /W4 /O2)
else()
    target_compile_options(test_nasdaq_up_nas100 PRIVATE -Wall -Wextra -O3 -mavx2 -mfma)
    target_link_libraries(test_nasdaq_up_nas100 PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_nasdaq_up_nas100 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "NasdaqUp NAS100 test configured: test_nasdaq_up_nas100")
endif()

# Room curve visualization
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/visualize_room_curve.cpp")
add_executable(visualize_room_curve visualize_room_curve.cpp)
target_include_directories(visualize_room_curve PRIVATE ../include)
target_compile_features(visualize_room_curve PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(visualize_room_curve PRIVATE /W4 /O2)
else()
    target_compile_options(visualize_room_curve PRIVATE -Wall -Wextra -O3 -mavx2 -mfma)
    target_link_libraries(visualize_room_curve PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(visualize_room_curve PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Room curve visualization configured: visualize_room_curve")
endif()

# Room decay function comparison
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_room_decay_functions.cpp")
add_executable(test_room_decay_functions test_room_decay_functions.cpp)
target_include_directories(test_room_decay_functions PRIVATE ../include)
target_compile_features(test_room_decay_functions PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_room_decay_functions PRIVATE /W4 /O2)
else()
    target_compile_options(test_room_decay_functions PRIVATE -Wall -Wextra -O3 -mavx2 -mfma)
    target_link_libraries(test_room_decay_functions PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_room_decay_functions PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Room decay function test configured: test_room_decay_functions")
endif()

# Hyperbola sweep test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_hyperbola_sweep.cpp")
add_executable(test_hyperbola_sweep test_hyperbola_sweep.cpp)
target_include_directories(test_hyperbola_sweep PRIVATE ../include)
target_compile_features(test_hyperbola_sweep PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_hyperbola_sweep PRIVATE /W4 /O2)
else()
    target_compile_options(test_hyperbola_sweep PRIVATE -Wall -Wextra -O3 -mavx2 -mfma)
    target_link_libraries(test_hyperbola_sweep PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_hyperbola_sweep PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Hyperbola sweep test configured: test_hyperbola_sweep")
endif()

# Trailing stop sweep test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_trailing_stop_sweep.cpp")
add_executable(test_trailing_stop_sweep test_trailing_stop_sweep.cpp)
target_include_directories(test_trailing_stop_sweep PRIVATE ../include)
target_compile_features(test_trailing_stop_sweep PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_trailing_stop_sweep PRIVATE /W4 /O2)
else()
    target_compile_options(test_trailing_stop_sweep PRIVATE -Wall -Wextra -O3 -mavx2 -mfma)
    target_link_libraries(test_trailing_stop_sweep PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_trailing_stop_sweep PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Trailing stop sweep test configured: test_trailing_stop_sweep")
endif()

# Compile-all-strategies verification test (no AVX-512 to avoid build issues)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_compile_all_strategies.cpp")
add_executable(test_compile_all test_compile_all_strategies.cpp)
target_include_directories(test_compile_all PRIVATE ../include)
target_compile_features(test_compile_all PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_compile_all PRIVATE /W4 /O2)
else()
    target_compile_options(test_compile_all PRIVATE -Wall -Wextra -O2 -mavx2 -mfma)
    target_link_libraries(test_compile_all PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(test_compile_all PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Compile-all strategies test configured: test_compile_all")
endif()

# ──────────────────── Dashboard CLI ────────────────────
# Extended backtest CLI for React dashboard (broker params, trade list, equity sampling)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/dashboard_cli.cpp")
add_executable(dashboard_cli dashboard_cli.cpp)
target_include_directories(dashboard_cli PRIVATE ../include)
target_compile_features(dashboard_cli PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(dashboard_cli PRIVATE /W4 /O2)
else()
    target_compile_options(dashboard_cli PRIVATE -Wall -Wextra -O3 -mavx512f -mavx512dq -mavx2 -mfma)
    target_link_libraries(dashboard_cli PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
endif()
set_target_properties(dashboard_cli PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
message(STATUS "Dashboard CLI configured: dashboard_cli")
endif()