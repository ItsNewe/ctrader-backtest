# Validation Test Suite CMake Configuration

cmake_minimum_required(VERSION 3.10)

# All test targets link to simd_ops for SIMD operations
link_libraries(simd_ops)

# Macro to add a test executable only if the source file exists
macro(add_test_if_exists target_name source_file)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${source_file}")
        add_executable(${target_name} ${source_file})
        target_include_directories(${target_name} PRIVATE ../include)
        target_compile_features(${target_name} PRIVATE cxx_std_17)
        if(MSVC)
            target_compile_options(${target_name} PRIVATE /W4 /O2)
        else()
            target_compile_options(${target_name} PRIVATE -Wall -Wextra -O3)
        endif()
        set_target_properties(${target_name} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation"
        )
        message(STATUS "Test configured: ${target_name}")
    else()
        message(STATUS "Skipping ${target_name} (source file not found: ${source_file})")
    endif()
endmacro()

# Unit tests
add_test_if_exists(test_margin_swap test_margin_swap.cpp)
add_test_if_exists(test_position_validator test_position_validator.cpp)
add_test_if_exists(test_currency_converter test_currency_converter.cpp)
add_test_if_exists(test_currency_rate_manager test_currency_rate_manager.cpp)
add_test_if_exists(test_kill_switch test_kill_switch.cpp)
add_test_if_exists(test_monte_carlo test_monte_carlo.cpp)
add_test_if_exists(test_walk_forward test_walk_forward.cpp)
