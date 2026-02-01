cmake_minimum_required(VERSION 3.15)
project(ctrader-backtest VERSION 1.0.0 LANGUAGES CXX)

# C++17 Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Global compiler flags for SIMD support (AVX2/FMA)
if(MSVC)
    add_compile_options(/arch:AVX2)
else()
    add_compile_options(-mavx2 -mfma)
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Source files
set(SOURCES
    src/main.cpp
    src/backtest_engine.cpp
    src/ctrader_connector.cpp
    src/metatrader_connector.cpp
)

set(HEADERS
    include/backtest_engine.h
    include/ctrader_connector.h
    include/metatrader_connector.h
    include/margin_manager.h
    include/swap_manager.h
)

# Create executable
add_executable(backtest ${SOURCES} ${HEADERS})

# Include directories
target_include_directories(backtest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Compiler flags
if(MSVC)
    target_compile_options(backtest PRIVATE /W4 /O2 /EHsc)
else()
    target_compile_options(backtest PRIVATE -Wall -Wextra -O3 -march=native)
    if(WIN32)
        # MinGW on Windows
        target_link_libraries(backtest PRIVATE ws2_32 pthread)
    else()
        # Linux/macOS
        target_link_libraries(backtest PRIVATE pthread)
    endif()
endif()

# Find optional libraries
find_package(Protobuf QUIET)
find_package(OpenSSL QUIET)
find_package(Boost QUIET)

# Link libraries if found
if(Protobuf_FOUND)
    target_link_libraries(backtest PRIVATE protobuf::libprotobuf)
    target_compile_definitions(backtest PRIVATE HAVE_PROTOBUF)
endif()

if(OpenSSL_FOUND)
    target_link_libraries(backtest PRIVATE OpenSSL::Crypto OpenSSL::SSL)
    target_compile_definitions(backtest PRIVATE HAVE_OPENSSL)
endif()

if(Boost_FOUND)
    target_link_libraries(backtest PRIVATE Boost::system Boost::thread)
    target_compile_definitions(backtest PRIVATE HAVE_BOOST)
endif()

# Release optimization
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(MSVC)
        target_compile_options(backtest PRIVATE /O2 /Ob2)
    else()
        target_compile_options(backtest PRIVATE -O3 -march=native)
    endif()
endif()

# Version info
target_compile_definitions(backtest PRIVATE 
    VERSION_MAJOR=1 
    VERSION_MINOR=0 
    VERSION_PATCH=0
)

# Validation tests subdirectory
add_subdirectory(validation)

# Installation
install(TARGETS backtest DESTINATION bin)
install(FILES ${HEADERS} DESTINATION include)
