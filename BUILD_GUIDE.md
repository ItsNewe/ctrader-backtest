# Build Instructions for cTrader C++ Backtesting Engine

## Prerequisites Installation

### Windows Setup

The project requires three key tools to be installed on Windows:

#### 1. CMake (Required)
```powershell
# Option A: Using Chocolatey (recommended)
choco install cmake

# Option B: Download from
https://cmake.org/download/

# Verify installation
cmake --version
```

#### 2. C++ Compiler (Choose One)

**Option A: MinGW (Recommended for open source)**
```powershell
choco install mingw
# Verify
g++ --version
```

**Option B: MSVC (Visual Studio Community)**
```powershell
choco install visualstudio2022community
# During installation, check "Desktop development with C++"
# Then run Visual Studio developer tools
```

#### 3. Build Dependencies

```powershell
# Using vcpkg (recommended package manager)
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
.\vcpkg.exe integrate install

# Install required libraries
.\vcpkg.exe install protobuf:x64-windows
.\vcpkg.exe install openssl:x64-windows
.\vcpkg.exe install boost:x64-windows
```

**OR using Chocolatey:**
```powershell
choco install protobuf
choco install openssl
```

## Building the Project

### Method 1: Using Batch Script (Easiest)

Once prerequisites are installed:

```cmd
cd c:\Users\user\Documents\ctrader-backtest
.\build.bat
```

The script will:
- Create build directory
- Run CMake configuration
- Compile with optimizations (-O3)
- Output: `build\backtest.exe`

### Method 2: Manual CMake Build

```cmd
cd c:\Users\user\Documents\ctrader-backtest
mkdir build
cd build

# For MinGW
cmake -DCMAKE_BUILD_TYPE=Release -G "MinGW Makefiles" ..
cmake --build . --config Release -- -j 4

# For MSVC
cmake -DCMAKE_BUILD_TYPE=Release -G "Visual Studio 17 2022" ..
cmake --build . --config Release -- /M
```

### Method 3: Direct Compilation (If CMake unavailable)

```cmd
cd c:\Users\user\Documents\ctrader-backtest\src

g++ -std=c++17 -O3 -pthread -march=native ^
    -I..\include ^
    main.cpp backtest_engine.cpp ctrader_connector.cpp ^
    -o ..\build\backtest.exe

# On MSVC
cl.exe /std:c++17 /O2 /EHsc ^
    /I..\include ^
    main.cpp backtest_engine.cpp ctrader_connector.cpp
```

## Running the Program

```cmd
cd c:\Users\user\Documents\ctrader-backtest
.\build\backtest.exe
```

### Expected Output

```
=== C++ Backtesting Engine - Online Sources Integration ===

=== Example 1: Bar-Based MA Crossover ===
Created 500 sample bars for testing
Progress: ...
Final Balance: $...
...

=== Example 4: Parallel Optimization ===
Total combinations: 81
Progress: 25/81 (30.8%)
Progress: 50/81 (61.7%)
...
=== Top 10 Results ===
--- Rank 1 ---
Parameters: Buffer=100, Threshold=0.0001, SL=50, TP=100
Final Balance: $...
...
```

## Troubleshooting

### CMake not found
```powershell
# Add to PATH
$env:PATH += ";C:\Program Files\CMake\bin"
cmake --version
```

### MinGW not found
```powershell
# Check installation
where g++
# If not found, reinstall or add to PATH
$env:PATH += ";C:\MinGW\bin"
```

### Compiler not found (Windows)
```powershell
# For Visual Studio, run Developer PowerShell instead
"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1"
```

### Missing headers
```cmd
# Ensure all include directories are in PATH
set INCLUDE=%INCLUDE%;C:\path\to\boost\include;C:\path\to\openssl\include
```

### protobuf/openssl not found during linking
```cmd
# Download pre-built libraries or use vcpkg
vcpkg install protobuf:x64-windows openssl:x64-windows boost:x64-windows
vcpkg integrate install
```

## Linux/macOS Build

```bash
cd ~/Documents/ctrader-backtest
chmod +x build.sh
./build.sh

# Or manual
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release -- -j 4
./backtest
```

### Linux prerequisites
```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libprotobuf-dev protobuf-compiler libssl-dev libboost-all-dev
```

### macOS prerequisites
```bash
brew install cmake protobuf openssl boost
```

## Project Structure for Build

```
ctrader-backtest/
├── CMakeLists.txt          # Build configuration
├── include/
│   ├── backtest_engine.h   # Main engine (773 lines)
│   └── ctrader_connector.h # API connector
├── src/
│   ├── main.cpp            # Examples and tests
│   ├── backtest_engine.cpp # Implementation
│   └── ctrader_connector.cpp
├── build/                   # Generated (CMake creates this)
│   └── backtest.exe         # Final executable
├── build.bat               # Windows build script
└── build.sh                # Linux/Mac build script
```

## Build Output

When successful, you'll see:
- `build/backtest.exe` (Windows)
- `build/backtest` (Linux/macOS)

File size: ~500KB - 5MB depending on debug symbols

## Next Steps After Successful Build

1. **Run the program:**
   ```cmd
   .\build\backtest.exe
   ```

2. **Add your own CSV data** in `data/` directory:
   ```
   data/eurusd_h1.csv
   data/eurusd_ticks.csv
   ```

3. **Implement remaining TODOs:**
   - CSV parsing in DataLoader (if not loading sample data)
   - cTrader API integration
   - Protobuf message handling

4. **Create custom strategies:**
   - Extend `IStrategy` class
   - Implement `OnBar()` and `OnTick()`
   - Set `StrategyParams`

## Performance Notes

- First build: 30-60 seconds
- Incremental rebuild: 5-15 seconds
- Release build is 10-20x faster than Debug
- Recommended: Always use Release build for backtesting

## Getting Help

If build fails:
1. Check prerequisite versions match (CMake 3.15+, C++17 compiler)
2. Ensure all libraries are properly linked
3. Verify include paths in `CMakeLists.txt`
4. Try cleaning build directory: `rm -rf build/` then rebuild

## Visual Studio Code Integration

If using VS Code:
1. Install "C/C++" extension
2. Install "CMake" extension  
3. Install "CMake Tools" extension
4. Open project folder
5. Select MinGW or MSVC compiler kit when prompted
6. Press Ctrl+Shift+B to build
7. Press F5 to debug
