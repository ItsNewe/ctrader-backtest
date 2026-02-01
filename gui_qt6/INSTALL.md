# BacktestPro Qt6 GUI - Installation Guide

## Prerequisites

- Windows 10/11 (64-bit)
- Git (for cloning wgpu-native)
- Python 3.8+ (for MT5 bridge)
- MetaTrader 5 terminal (for live data download)

## Option 1: MSYS2 + MinGW (Recommended)

### Step 1: Install MSYS2

1. Download MSYS2 from: https://www.msys2.org/
2. Run the installer (default: `C:\msys64`)
3. After installation, open "MSYS2 MINGW64" terminal

### Step 2: Install Qt6 and Build Tools

```bash
# Update package database
pacman -Syu

# Install MinGW compiler and CMake
pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake

# Install Qt6
pacman -S mingw-w64-x86_64-qt6-base
pacman -S mingw-w64-x86_64-qt6-charts
pacman -S mingw-w64-x86_64-qt6-tools
```

### Step 3: Add to PATH

Add these to your system PATH:
```
C:\msys64\mingw64\bin
C:\msys64\mingw64\qt6\bin
```

### Step 4: Build BacktestPro

```bash
cd gui_qt6
mkdir build && cd build
cmake .. -G "MinGW Makefiles"
mingw32-make -j16
```

## Option 2: Qt Online Installer

### Step 1: Download Qt

1. Go to: https://www.qt.io/download-qt-installer
2. Download the Qt Online Installer
3. Run installer and create/login to Qt Account

### Step 2: Select Components

- Qt 6.7.x (or latest)
  - [x] MinGW 64-bit
  - [x] Qt Charts
  - [x] Qt Data Visualization (optional)
- Developer and Designer Tools
  - [x] MinGW 13.x 64-bit
  - [x] CMake

### Step 3: Configure Environment

Add to PATH:
```
C:\Qt\6.7.0\mingw_64\bin
C:\Qt\Tools\mingw1310_64\bin
C:\Qt\Tools\CMake_64\bin
```

### Step 4: Build

```cmd
cd gui_qt6
mkdir build
cd build
cmake .. -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH=C:/Qt/6.7.0/mingw_64
mingw32-make -j16
```

## Python Dependencies (for MT5 Bridge)

```bash
pip install MetaTrader5 pandas numpy
```

## Verifying Installation

### Check Qt6
```bash
qmake6 --version
# Should show Qt 6.x.x
```

### Check CMake
```bash
cmake --version
# Should show 3.16+
```

### Check MinGW
```bash
g++ --version
# Should show GCC 13.x or higher
```

## Troubleshooting

### "Qt6 not found"
- Ensure CMAKE_PREFIX_PATH points to Qt installation
- Verify Qt6 is in PATH

### "wgpu-native not found" (GPU acceleration)
```bash
git clone https://github.com/gfx-rs/wgpu-native
cd wgpu-native
cargo build --release
```
Note: GPU acceleration falls back to CPU if wgpu-native is not available.

### "MetaTrader5 module not found"
```bash
pip install MetaTrader5
```
Note: MT5 Python API only works on Windows.

### Build fails with linker errors
- Check that all Qt6 libraries are accessible
- Ensure MinGW version matches Qt build

## Directory Structure After Build

```
gui_qt6/
├── build/
│   └── BacktestPro.exe
├── scripts/
│   └── mt5_bridge.py
├── resources/
│   ├── icons/
│   └── styles/
│       └── dark.qss
└── src/
    ├── main.cpp
    ├── mainwindow.cpp
    └── widgets/
```

## Running BacktestPro

```bash
cd build
./BacktestPro.exe
```

Or double-click `BacktestPro.exe` in Windows Explorer.

## Connecting to MT5

1. Start MetaTrader 5 terminal
2. Login to your trading account
3. In BacktestPro: Tools → Connect to MT5
4. Wait for connection confirmation

## First Backtest

1. Select instrument from dropdown (loaded from MT5)
2. Set date range
3. Configure strategy parameters
4. Click "Start Backtest" or press F5
5. View results in Equity Curve and Results tabs

## Optimization

1. Enable "Optimization Mode" in Parameters panel
2. Set parameter ranges (Start, Stop, Step)
3. Select optimization criterion
4. Click "Start Optimization"
5. Results appear in Optimization tab, sorted by fitness
