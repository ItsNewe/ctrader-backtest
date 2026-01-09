@echo off
cd /d %~dp0
echo Compiling Broker backtest...
C:\msys64\ucrt64\bin\g++.exe -std=c++17 -O2 -I..\include test_fill_up_for_broker.cpp -o test_fill_up_for_broker.exe > compile_errors.txt 2>&1
if errorlevel 1 (
    echo Compilation failed! Errors:
    type compile_errors.txt
) else (
    echo Compilation successful!
    del compile_errors.txt
)
