@echo off
REM MT5 Validation Tools - Quick Access Script

echo ========================================
echo MT5 Validation Tools
echo ========================================
echo.

:menu
echo Choose an option:
echo.
echo 1. Verify MT5 test data
echo 2. Analyze all tests
echo 3. Retrieve MT5 results (specify test)
echo 4. Compare backtest results
echo 5. Exit
echo.
set /p choice="Enter choice (1-5): "

if "%choice%"=="1" goto verify
if "%choice%"=="2" goto analyze
if "%choice%"=="3" goto retrieve
if "%choice%"=="4" goto compare
if "%choice%"=="5" goto end

echo Invalid choice. Please try again.
echo.
goto menu

:verify
echo.
echo Running data verification...
python verify_mt5_data.py
echo.
echo Press any key to return to menu...
pause >nul
cls
goto menu

:analyze
echo.
echo Running complete analysis...
python analyze_all_tests.py
echo.
echo Press any key to return to menu...
pause >nul
cls
goto menu

:retrieve
echo.
set /p test="Enter test name (test_a, test_b, test_c, test_d, test_e, test_f): "
echo Retrieving results for %test%...
python retrieve_results.py %test%
echo.
echo Press any key to return to menu...
pause >nul
cls
goto menu

:compare
echo.
echo Comparing backtest results...
python compare_backtest_results.py
echo.
echo Press any key to return to menu...
pause >nul
cls
goto menu

:end
echo.
echo Goodbye!
