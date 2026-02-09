@echo off
echo Running D8_50 test... > d8_50_run.log 2>&1
echo. >> d8_50_run.log 2>&1
C:\Users\user\Documents\ctrader-backtest\build\validation\test_d8_50.exe 8 1 0 >> d8_50_run.log 2>&1
echo. >> d8_50_run.log 2>&1
echo Exit code: %errorlevel% >> d8_50_run.log 2>&1
