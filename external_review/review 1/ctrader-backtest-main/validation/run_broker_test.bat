@echo off
cd /d %~dp0
echo Starting  Backtest...
echo.
test_fill_up_broker.exe
echo.
echo Test completed!
pause
