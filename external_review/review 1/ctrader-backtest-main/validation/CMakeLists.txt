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
