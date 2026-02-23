@echo off
title Backtest Dashboard Launcher
color 0A

echo ============================================
echo   Backtest Dashboard Launcher
echo ============================================
echo.

set ROOT=%~dp0

:: Kill any previous instances by port
echo Cleaning up old processes...
python -c "import subprocess; out=subprocess.check_output('netstat -ano',text=True); [subprocess.run(['taskkill','/pid',l.split()[-1],'/f','/t'],capture_output=True) for l in out.splitlines() if ':8000' in l and 'LISTEN' in l]" 2>nul
python -c "import subprocess; out=subprocess.check_output('netstat -ano',text=True); [subprocess.run(['taskkill','/pid',l.split()[-1],'/f','/t'],capture_output=True) for l in out.splitlines() if ':5173' in l and 'LISTEN' in l]" 2>nul
timeout /t 2 /nobreak >nul

:: Start the FastAPI backend (minimized)
echo Starting API server on port 8000...
start "Backtest API" /min cmd /k "cd /d %ROOT% && python -m uvicorn api.main:app --host 0.0.0.0 --port 8000"

:: Wait for API to be ready
timeout /t 3 /nobreak >nul

:: Start the Vite dev server (minimized)
echo Starting dashboard on port 5173...
start "Dashboard Dev" /min cmd /k "cd /d %ROOT%dashboard && npm run dev"

:: Wait for Vite to be ready
timeout /t 4 /nobreak >nul

:: Open the browser
echo Opening browser...
start http://localhost:5173

echo.
echo ============================================
echo   Dashboard is running!
echo   API:       http://localhost:8000
echo   Dashboard: http://localhost:5173
echo ============================================
echo.
echo Press any key to stop both servers...
echo.
pause >nul

:: Cleanup on exit
echo Stopping servers...
python -c "import subprocess; out=subprocess.check_output('netstat -ano',text=True); [subprocess.run(['taskkill','/pid',l.split()[-1],'/f','/t'],capture_output=True) for l in out.splitlines() if (':8000' in l or ':5173' in l) and 'LISTEN' in l]" 2>nul
echo Done.
timeout /t 2 >nul
