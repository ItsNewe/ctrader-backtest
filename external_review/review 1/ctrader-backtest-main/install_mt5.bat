@echo off
REM MetaTrader5 Setup Script for Windows
REM This script helps install the MetaTrader5 Python module

echo.
echo ================================================
echo MetaTrader5 Python Module Installer
echo ================================================
echo.

REM Check Python version
python --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: Python is not installed or not in PATH
    echo Please install Python 3.6+ from https://www.python.org
    pause
    exit /b 1
)

echo ✓ Python found
python --version
echo.

REM Offer to install MetaTrader5
echo About to install MetaTrader5 Python module...
echo This is required for MetaTrader5 connectivity in the backtest engine.
echo.
echo Press any key to continue, or Ctrl+C to cancel...
pause >nul

REM Install MetaTrader5
echo.
echo Installing MetaTrader5...
python -m pip install MetaTrader5 --upgrade

if errorlevel 1 (
    echo.
    echo ERROR: Installation failed
    echo Please check your internet connection and try again
    pause
    exit /b 1
)

echo.
echo ================================================
echo ✓ Installation Complete!
echo ================================================
echo.
echo Next steps:
echo 1. Make sure MetaTrader5 terminal is open and logged in
echo 2. Restart the Flask server (python server.py)
echo 3. Try connecting to MetaTrader5 again
echo.
pause
