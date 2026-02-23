@echo off
echo Killing all Python processes...
taskkill /im python.exe /f /t 2>nul
taskkill /im python3.exe /f /t 2>nul
echo Killing Node processes (Vite)...
taskkill /im node.exe /f /t 2>nul
echo.
echo All server processes killed.
echo You can now run start_dashboard.bat
timeout /t 3
