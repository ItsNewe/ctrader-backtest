# Compilation Guide - Windows Development Environment

**Last Updated:** 2026-01-07
**Platform:** Windows 11 with MSYS2/MinGW-w64

---

## Issue: Direct g++ Compilation Fails

When trying to compile C++ code directly from Windows Command Prompt or PowerShell, g++ may fail silently or hang.

### Symptom
```bash
# This fails or hangs:
C:/msys64/ucrt64/bin/g++.exe test.cpp -o test.exe
```

### Root Cause
MinGW-w64 g++ requires the MSYS2 environment to be properly initialized with correct PATH and library paths.

---

## Solution: Use MSYS2 Shell

All compilation must be done from within the MSYS2 environment.

### Method 1: Direct MSYS2 Shell Command (Recommended)

```bash
# Single command compilation and execution
C:/msys64/msys2_shell.cmd -ucrt64 -defterm -no-start -here -c \
  "cd validation && \
   g++ -I../include -std=c++17 test_position_validator.cpp -o test_position_validator.exe && \
   ./test_position_validator.exe"
```

**Flags Explained:**
- `-ucrt64` - Use UCRT64 environment (modern Windows runtime)
- `-defterm` - Use default terminal
- `-no-start` - Don't launch new window
- `-here` - Start in current directory
- `-c "commands"` - Execute commands and exit

### Method 2: Interactive MSYS2 Shell

```bash
# Launch MSYS2 shell
C:/msys64/msys2_shell.cmd -ucrt64

# Then compile normally
cd /c/Users/user/Documents/ctrader-backtest/validation
g++ -I../include -std=c++17 test_position_validator.cpp -o test_position_validator.exe
./test_position_validator.exe
```

### Method 3: Batch File Wrapper

Create `compile_tests.bat`:
```batch
@echo off
C:\msys64\msys2_shell.cmd -ucrt64 -defterm -no-start -here -c ^
  "cd validation && ^
   g++ -I../include -std=c++17 test_position_validator.cpp -o test_position_validator.exe && ^
   ./test_position_validator.exe"
pause
```

Then run: `compile_tests.bat`

---

## Standard Compilation Flags

### For Tests
```bash
g++ -I../include -std=c++17 -Wall -Wextra test_file.cpp -o test_file.exe
```

### For Release Build
```bash
g++ -I./include -std=c++17 -O3 -march=native -Wall -Wextra source.cpp -o program.exe
```

### For Debug Build
```bash
g++ -I./include -std=c++17 -g -O0 -Wall -Wextra source.cpp -o program_debug.exe
```

---

## CMake Build System

### Configure (from project root)
```bash
C:/msys64/msys2_shell.cmd -ucrt64 -defterm -no-start -here -c \
  "cmake -B build -S . -G 'MinGW Makefiles'"
```

### Build Specific Target
```bash
C:/msys64/msys2_shell.cmd -ucrt64 -defterm -no-start -here -c \
  "cmake --build build --target test_position_validator"
```

### Build All
```bash
C:/msys64/msys2_shell.cmd -ucrt64 -defterm -no-start -here -c \
  "cmake --build build"
```

### Run Tests via CTest
```bash
C:/msys64/msys2_shell.cmd -ucrt64 -defterm -no-start -here -c \
  "cd build && ctest --verbose"
```

---

## Python Test Runner

The Python test runner ([validation/run_unit_tests.py](validation/run_unit_tests.py)) automatically handles MSYS2 shell invocation.

### Usage
```bash
cd validation
python run_unit_tests.py
```

The script will:
1. Detect MSYS2 g++ installation
2. Compile tests using MSYS2 shell
3. Run tests and collect results
4. Display formatted output

---

## Troubleshooting

### Issue: "g++ not found"
**Solution:** Install MinGW-w64 via MSYS2:
```bash
# Open MSYS2 terminal
pacman -S mingw-w64-ucrt-x86_64-gcc
```

### Issue: "Cannot find -lstdc++"
**Solution:** Ensure you're using MSYS2 shell environment, not Windows CMD.

### Issue: "Permission denied" when running .exe
**Solution:**
1. Check Windows Defender / antivirus
2. Run from MSYS2 shell (not Windows Explorer)
3. Disable real-time protection temporarily

### Issue: Tests hang indefinitely
**Solution:** Add timeout to commands:
```bash
C:/msys64/msys2_shell.cmd -ucrt64 -defterm -no-start -here -c \
  "timeout 30 ./test_position_validator.exe"
```

---

## Environment Setup Checklist

- [ ] MSYS2 installed (default: `C:/msys64/`)
- [ ] MinGW-w64 UCRT64 toolchain installed
- [ ] g++ available at `C:/msys64/ucrt64/bin/g++.exe`
- [ ] g++ version 15.2.0 or later
- [ ] CMake 3.15 or later (optional)
- [ ] Python 3.7 or later (for test runner)

### Verify Installation
```bash
C:/msys64/msys2_shell.cmd -ucrt64 -defterm -no-start -here -c "g++ --version"
# Should output: g++.exe (Rev8, Built by MSYS2 project) 15.2.0
```

---

## Quick Reference

### Compile Single Test
```bash
C:/msys64/msys2_shell.cmd -ucrt64 -defterm -no-start -here -c \
  "cd validation && g++ -I../include -std=c++17 test_validator_simple.cpp -o test_validator_simple.exe && ./test_validator_simple.exe"
```

### Compile and Run All Tests
```bash
cd validation
python run_unit_tests.py
```

### Build Entire Project
```bash
C:/msys64/msys2_shell.cmd -ucrt64 -defterm -no-start -here -c \
  "cmake -B build -G 'MinGW Makefiles' && cmake --build build"
```

---

## Performance Notes

- **Compilation time:** <1 second for test files
- **Test execution:** <100ms for 55 tests
- **CMake configuration:** ~5 seconds first time, cached thereafter
- **Full project build:** ~10 seconds

---

## Alternative: Visual Studio Integration

If you prefer Visual Studio:

1. Install "Desktop development with C++" workload
2. Open project folder in Visual Studio
3. VS will use CMake automatically
4. Build and debug from IDE

**Note:** Visual Studio uses MSVC compiler, not MinGW-w64. Code should compile with both, but test with MinGW-w64 for production.

---

## Summary

✅ **Always use MSYS2 shell for compilation**
✅ **Use Python test runner for automated testing**
✅ **Use CMake for full project builds**
✅ **Verify g++ version is 15.2.0 or later**

**Key Command:**
```bash
C:/msys64/msys2_shell.cmd -ucrt64 -defterm -no-start -here -c "your_commands_here"
```

---

**Document Version:** 1.0
**Maintained By:** Development Team
**Last Verified:** 2026-01-07
