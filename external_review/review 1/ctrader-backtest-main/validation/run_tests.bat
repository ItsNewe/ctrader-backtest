@echo off
REM Quick test runner for validation tests

echo =======================================
echo Running Validation Tests
echo =======================================
echo.

echo Compiling simple validator test...
C:\msys64\ucrt64\bin\g++.exe -I..\include -std=c++17 test_validator_simple.cpp -o test_validator_simple.exe 2>compile_errors.txt

if exist test_validator_simple.exe (
    echo Build successful!
    echo.
    echo Running tests...
    echo.
    test_validator_simple.exe
    echo.
) else (
    echo Build failed! Check compile_errors.txt
    type compile_errors.txt
    exit /b 1
)

echo.
echo =======================================
echo Tests complete!
echo =======================================
pause
