cmake_minimum_required(VERSION 3.16)
project(BacktestPro VERSION 1.0.0 LANGUAGES CXX)

# C++17 required for Qt6 and modern features
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable Qt MOC, UIC, RCC for automatic processing
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Try to find Qt6 in common locations
list(APPEND CMAKE_PREFIX_PATH
    "C:/msys64/mingw64"
    "C:/Qt/6.7.0/mingw_64"
    "C:/Qt/6.6.0/mingw_64"
    "$ENV{QT6_DIR}"
)

# Find required Qt6 packages
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Charts
)

# Optional: OpenGL for hardware-accelerated rendering
find_package(Qt6 OPTIONAL_COMPONENTS OpenGL OpenGLWidgets)

message(STATUS "Qt6 found at: ${Qt6_DIR}")
message(STATUS "Qt6 version: ${Qt6_VERSION}")

# Source files
set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/widgets/instrumentselector.cpp
    src/widgets/equitychart.cpp
    src/widgets/parametergrid.cpp
    src/widgets/resultstable.cpp
    src/mt5bridge.cpp
    src/backtestworker.cpp
)

# Header files (for MOC processing)
set(HEADERS
    src/mainwindow.h
    src/widgets/instrumentselector.h
    src/widgets/equitychart.h
    src/widgets/parametergrid.h
    src/widgets/resultstable.h
    src/mt5bridge.h
    src/backtestworker.h
)

# Resource files
set(RESOURCES
    resources/resources.qrc
)

# Create executable
qt_add_executable(BacktestPro
    ${SOURCES}
    ${HEADERS}
    ${RESOURCES}
)

# Link Qt6 libraries
target_link_libraries(BacktestPro PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Charts
)

# Link OpenGL if available
if(TARGET Qt6::OpenGL)
    target_link_libraries(BacktestPro PRIVATE Qt6::OpenGL)
endif()
if(TARGET Qt6::OpenGLWidgets)
    target_link_libraries(BacktestPro PRIVATE Qt6::OpenGLWidgets)
endif()

# Include directories
target_include_directories(BacktestPro PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# Windows-specific: prevent console window in release builds
if(WIN32)
    set_target_properties(BacktestPro PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

# Installation
install(TARGETS BacktestPro
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Deploy Qt dependencies (Windows)
if(WIN32)
    # Find windeployqt
    find_program(WINDEPLOYQT windeployqt HINTS "${Qt6_DIR}/../../../bin")
    if(WINDEPLOYQT)
        message(STATUS "windeployqt found: ${WINDEPLOYQT}")
        add_custom_command(TARGET BacktestPro POST_BUILD
            COMMAND ${WINDEPLOYQT}
                --no-translations
                --no-system-d3d-compiler
                $<TARGET_FILE:BacktestPro>
            COMMENT "Deploying Qt dependencies..."
        )
    endif()
endif()
