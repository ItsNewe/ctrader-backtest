@echo off
setlocal enabledelayedexpansion
REM Quick-start script for cTrader Backtest Engine Web UI
REM This script installs dependencies and starts the web server

echo.
echo ===== cTrader Backtest Engine - Web UI =====
echo.

REM Check if Python is installed
python --version >nul 2>&1
if !errorlevel! equ 1 (
    echo ERROR: Python is not installed or not in PATH
    echo Please install Python 3.8+ from https://www.python.org/
    pause
    exit /b 1
)

echo [OK] Python found
python --version

REM Check if pip is available
pip --version >nul 2>&1
if !errorlevel! equ 1 (
    echo ERROR: pip is not available
    pause
    exit /b 1
)

echo [OK] pip found

REM Install Flask and dependencies
echo.
echo Installing required Python packages...
echo.
pip install --upgrade flask flask-cors 2>nul
echo [OK] Flask dependencies ready
echo.

REM Check if C++ backtest executable exists
if not exist "build\backtest.exe" (
    echo.
    echo WARNING: C++ backtest executable not found at build\backtest.exe
    echo The web UI will use mock results for demonstration
    echo To use real backtests, build the C++ project with: build.bat
)

echo ===== Starting Web Server =====
echo.
echo Dashboard URL: http://localhost:5000
echo Press Ctrl+C to stop the server
echo.
echo.

REM Start browser after brief delay
timeout /t 2 /nobreak >nul
start http://localhost:5000 >nul 2>&1

REM Run Flask server
python server.py

pause
