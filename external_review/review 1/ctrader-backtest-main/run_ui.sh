#!/bin/bash

# Quick-start script for cTrader Backtest Engine Web UI
# This script installs dependencies and starts the web server

echo ""
echo "===== cTrader Backtest Engine - Web UI ====="
echo ""

# Check if Python is installed
if ! command -v python3 &> /dev/null; then
    echo "ERROR: Python 3 is not installed"
    echo "Please install Python 3.8+ using your package manager"
    echo "  Ubuntu/Debian: sudo apt-get install python3 python3-pip"
    echo "  macOS: brew install python3"
    exit 1
fi

echo "[OK] Python found"
python3 --version

# Check if pip is available
if ! command -v pip3 &> /dev/null; then
    echo "ERROR: pip3 is not available"
    exit 1
fi

echo "[OK] pip3 found"

# Install Flask and dependencies
echo ""
echo "Installing required Python packages..."
pip3 install flask flask-cors

if [ $? -ne 0 ]; then
    echo "ERROR: Failed to install dependencies"
    exit 1
fi

echo "[OK] Dependencies installed"

# Check if C++ backtest executable exists
if [ ! -f "build/backtest" ] && [ ! -f "build/backtest.exe" ]; then
    echo ""
    echo "WARNING: C++ backtest executable not found"
    echo "The web UI will use mock results for demonstration"
    echo "To use real backtests, build the C++ project with: ./build.sh"
fi

# Start the web server
echo ""
echo "===== Starting Web Server ====="
echo ""
echo ""
echo "Opening dashboard in browser..."
echo "Navigate to: http://localhost:5000"
echo ""
echo "Press Ctrl+C to stop the server"
echo ""

# Try to open browser
if command -v xdg-open &> /dev/null; then
    # Linux
    xdg-open http://localhost:5000 &
elif command -v open &> /dev/null; then
    # macOS
    open http://localhost:5000 &
fi

sleep 2

python3 server.py
