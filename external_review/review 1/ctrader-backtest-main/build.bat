@echo off
REM Build script for Windows

setlocal enabledelayedexpansion

set SCRIPT_DIR=%~dp0
set BUILD_DIR=%SCRIPT_DIR%build
set GENERATED_DIR=%SCRIPT_DIR%generated

echo ===== cTrader C++ Backtest Engine Build =====
echo.

REM Create generated directory if it doesn't exist
if not exist "%GENERATED_DIR%" (
  echo Creating generated/ directory...
  mkdir "%GENERATED_DIR%"
)

REM Check for proto files (placeholder)
echo Checking for proto files...
if not exist "%GENERATED_DIR%\ctrader.proto" (
  echo Proto files not found. Skipping download.
)

REM Create build directory
if not exist "%BUILD_DIR%" (
  echo Creating build directory...
  mkdir "%BUILD_DIR%"
)

REM Run CMake configure
echo Running CMake configure...
cd /d "%BUILD_DIR%"
cmake -DCMAKE_BUILD_TYPE=Release -G "MinGW Makefiles" ..
if errorlevel 1 (
  echo ERROR: CMake configure failed
  exit /b 1
)
echo [OK] CMake configured

REM Build with parallel jobs
echo Building project...
cmake --build . --config Release -- -j %NUMBER_OF_PROCESSORS%
if errorlevel 1 (
  echo ERROR: Build failed
  exit /b 1
)

echo.
echo ===== Build Successful =====
echo Executable: %BUILD_DIR%\backtest.exe
echo.
echo Next steps:
echo   1. Run: %BUILD_DIR%\backtest.exe
echo   2. Debug: gdb %BUILD_DIR%\backtest.exe
echo   3. Rebuild: cd %SCRIPT_DIR% and run build.bat
echo.
